# Remote client/server architecture plan

Status: proposed for review; no production implementation has started.

## 1. Objective

Promote the successful `prototypes/flatbuffers_wire` experiment into a
production client/server architecture that lets the existing Qt application
run locally while all plotfile metadata and data access happens on a remote
machine.

The production path must preserve the current local-file workflow and the
existing demand-driven query behavior. Remote use will look like:

```text
local Qt UI
    |
    | high-level dataset operations
    v
remote client ---- framed FlatBuffers over TCP ---- headless server
                                                     |
                                                     v
                                              PlotfileDataset
                                              SliceQuery
                                              LineQuery
```

The supported deployment model is a server bound to the remote loopback
interface and a client connected through an SSH port forward. SSH remains
responsible for authentication, encryption, host verification, and optional
transport compression.

## 2. Prototype findings to retain

The prototype established that the right remote boundary is above block I/O:

- the server can open an actual plotfile and execute the existing
  `SliceQuery`;
- the client can receive a raw `ScalarPlane` and continue to apply palettes,
  ranges, logarithmic mapping, contours, and other presentation locally;
- a length-prefixed FlatBuffer with a file identifier, protocol version,
  request ID, payload union, frame-size limit, and verifier is a workable
  protocol foundation;
- the server can remain independent of Qt;
- build-time generation from one checked-in schema works with the current
  CMake build.

The prototype is not production-ready because it stops after one slice and
one connection. It does not support the complete metadata needed by the UI,
line plots, the Dataset window, multiple datasets, multiple outstanding
requests, cancellation, orderly close, reconnect behavior, or application
integration.

## 3. Scope

### In scope

- A reusable dataset-session abstraction used by the Qt application for both
  local and remote datasets.
- Production FlatBuffers schema, codec, framing, client connection, server
  session, and server executable.
- Full view-facing metadata transfer.
- Remote slice queries, line queries, and Dataset-window level extraction.
- Multiple datasets and multiple outstanding requests on one connection.
- Cooperative cancellation using the existing `StopToken` model.
- Deterministic dataset close, connection shutdown, disconnect reporting, and
  manual reconnect/reopen.
- Local plotfiles, remote plotfiles, and local or remote plotfile sequences.
- Loopback-only server operation designed for SSH port forwarding.
- Unit, integration, end-to-end, and Qt smoke coverage.
- Build, installation, command-line, and user documentation.
- Removal of the prototype after production equivalence is demonstrated.

### Out of scope for protocol 1.0

- Directly exposing the server on an untrusted network.
- Application-level authentication, authorization, or TLS.
- Remote filesystem browsing.
- Server-side rendering, palettes, contours, glyph generation, or image
  export.
- Shared datasets or cache state between separate client connections.
- Automatic retry of interrupted requests.
- Application-level compression of `ScalarPlane` data.
- Chunked or streaming query results.

These exclusions keep the first production protocol aligned with the
validated prototype. They do not prevent additive protocol extensions.

## 4. Proposed architecture

### 4.1 Dataset session boundary

Introduce `amrvis::DatasetSession`, a high-level interface representing one
open dataset. The Qt layer will depend on this interface rather than directly
on `PlotfileDataset`.

The interface will expose:

- dataset ID;
- immutable `DatasetMetadata`;
- `MetadataReadMetrics` and file-version text;
- `executeSlice(SliceRequest, StopToken)`;
- `executeLine(LineRequest, StopToken)`;
- `extractLevel(DatasetLevelRequest, StopToken)` for the Dataset window;
- a current cache-metrics snapshot;
- `clearUnpinnedCache()`;
- a best-effort, idempotent close operation.

Two implementations will provide the same behavior:

- `LocalDatasetSession` owns a `PlotfileDataset` and invokes
  `SliceQuery`, `LineQuery`, and the extracted Dataset-window query directly.
- `RemoteDatasetSession` owns a remote dataset handle and sends the equivalent
  operations through a shared `WireConnection`.

This boundary avoids making the remote client emulate block reads. It also
keeps AMR composition, sampling, cache policy, and file parsing on the machine
that owns the data.

The proposed dependency direction is:

```text
Amrvis::core
    ^
Amrvis::io <- Amrvis::query
                    ^
              Amrvis::data
                    ^
              Amrvis::remote
                    ^
                Qt client
```

`Amrvis::data` will contain the session interface and local implementation.
`Amrvis::remote` will add FlatBuffers and networking without making the local
data/query libraries depend on either.

### 4.2 Dataset-window extraction

`src/qt/DatasetExtract.hpp` currently reads blocks directly from
`PlotfileDataset`. Move this non-Qt operation and its result types into the new
data layer.

The resulting operation will accept:

- dataset ID and field;
- level;
- physical region;
- normal axis and slice position;
- maximum table extent;
- cancellation token.

Its result will retain the current cell-index bounds, dimensions, values,
coverage mask, minimum/maximum, slice index, and truncation flags. The Qt
`DatasetWindow` will consume this result through `DatasetSession`, so its
behavior is identical for local and remote datasets.

### 4.3 Qt integration

Replace `std::shared_ptr<PlotfileDataset>` in `MainWindow`,
`InitialSliceResult`, `DatasetRequest`, and helper functions with
`std::shared_ptr<DatasetSession>`.

The following operations will be routed through the session:

- initial and refreshed slices;
- contour source slices;
- vector-component slices;
- line plots;
- Dataset-window extraction;
- cache metrics and cache clearing;
- plotfile-sequence frame load and prefetch.

Presentation stays local. `ScalarRenderer`, contour extraction, vector-glyph
generation, range selection, image composition, and export do not move to the
server.

Opening a dataset will be refactored so the metadata and session are created
once. The current local flow reads metadata, then constructs
`PlotfileDataset` and reads it again for the initial slice. The unified
session factory will return the open session and its metadata together, for
both local and remote paths.

### 4.4 Dataset locations and connection ownership

Add a value type that distinguishes:

- local path;
- remote endpoint plus server-visible path.

One `MainWindow` will own at most one active `WireConnection`. Dataset
sessions and prefetched sequence frames from that window may share the
connection. Independent top-level windows will use independent connections,
matching their existing independent dataset/cache state.

Remote dataset handles are scoped to a single server connection. After a
disconnect they are invalid and are never silently reused on a new
connection.

## 5. Wire protocol 1.0

### 5.1 Framing

Retain the prototype framing:

- four-byte unsigned payload length in network byte order;
- one non-empty FlatBuffer body;
- `AVR2` FlatBuffers file identifier;
- a configurable hard frame limit, defaulting to 128 MiB;
- exact-read and exact-write loops that handle interruption and partial I/O;
- buffer verification before any generated accessor is used.

The client and server will reject zero-length, oversized, corrupt, wrongly
identified, or unverifiable frames before dispatch. Length arithmetic,
vector-size multiplication, output dimensions, cache budgets, string lengths,
dataset counts, and outstanding-request counts will also be bounded before
allocation.

### 5.2 Envelope and version policy

Every message will retain:

- protocol major and minor version;
- nonzero request ID;
- typed payload union.

Protocol policy:

- a major change may break compatibility;
- a minor change must be additive;
- the handshake selects a minor version supported by both peers;
- the server only sends messages valid for the negotiated version;
- unknown or invalid client requests receive a typed error when the envelope
  is otherwise valid;
- malformed framing, failed verification, duplicate live request IDs, or
  messages sent before the handshake close the connection.

Packed messages are transient network data, not files. No persistence or
cross-major migration path is promised.

### 5.3 Handshake and capabilities

The first request must be `HelloRequest`. It will carry:

- client name and software version;
- supported protocol major and minor range;
- maximum accepted frame size;
- supported optional capabilities.

`HelloResponse` will return:

- server name and software version;
- selected protocol version;
- negotiated maximum frame size;
- enabled capabilities;
- server worker and resource limits useful for diagnostics.

Protocol 1.0 will define capabilities even if none are optional initially.
This gives compression or streaming a compatible negotiation point later.

### 5.4 Request and response messages

The production schema will cover:

| Request | Successful response | Purpose |
|---|---|---|
| `HelloRequest` | `HelloResponse` | Negotiate the session |
| `OpenDatasetRequest` | `DatasetOpened` | Open a path and return metadata |
| `CloseDatasetRequest` | `DatasetClosed` | Release one remote handle |
| `SliceRequest` | `ScalarPlane` | Execute `SliceQuery` |
| `LineRequest` | `LineSamples` | Execute `LineQuery` |
| `DatasetLevelRequest` | `DatasetLevel` | Populate one Dataset-window tab |
| `ClearCacheRequest` | `CacheState` | Clear unpinned blocks |
| `CancelRequest` | `CancelAcknowledged` | Request cancellation by request ID |
| `PingRequest` | `PongResponse` | Explicit health check |
| any request | `ErrorResponse` | Typed terminal failure |

Every ordinary request has exactly one terminal response with the same
request ID. Responses may arrive in a different order from requests.

`CancelRequest` has its own request ID and names a target request ID. The
target request still receives its own terminal response, normally a
`Cancelled` error. If completion wins the race, the target's normal response
is valid and the cancellation acknowledgment reports that it was too late.

### 5.5 Metadata representation

`DatasetOpened` will carry the complete current `DatasetMetadata` needed for
local behavior:

- dimension, finest level, time, coordinate system, and physical domain;
- field names, component counts, centering, and component names;
- level numbers, steps, integer domains, refinement ratios, cell sizes,
  ghost widths, component counts, boxes, and block metadata;
- optional per-block statistics;
- format/file-version text and metadata-read metrics;
- initial cache state.

The first implementation will mirror all current metadata fields at the
schema conversion boundary. This keeps range selection, level selection, grid
overlays, diagnostics, and sequence behavior identical and avoids fabricated
client-side metadata. Filesystem-specific fields are informational on the
client and are never used for client-side reads.

Decoders will validate semantic invariants after FlatBuffers verification,
including vector lengths, level counts, box bounds, field indices, and
metadata consistency.

### 5.6 Query results

`ScalarPlane` will carry:

- dimensions and physical region;
- float values;
- validity mask;
- source-level array;
- `SliceQueryMetrics`;
- current server-side cache state.

`LineSamples` will carry:

- line axis;
- positions, values, validity mask, and source-level arrays;
- `SliceQueryMetrics`;
- current server-side cache state.

`DatasetLevel` will carry the complete moved `DatasetLevelExtract` result and
the current cache state.

The client copies verified FlatBuffers vectors into the existing owning
domain result types before releasing the receive buffer. This preserves the
current lifetime model and keeps FlatBuffers-generated types out of the UI and
query APIs.

### 5.7 Errors

Use a stable error-code enum with a human-readable message. Initial codes:

- unsupported protocol;
- invalid request;
- unknown dataset;
- dataset open failure;
- cancelled;
- cache budget exceeded;
- resource limit exceeded;
- operation failure;
- internal server error.

The remote client maps cancellation back to `ReadCancelled` and cache-budget
failure back to `CacheBudgetExceeded`, so the existing UI suppression and
lower-level fallback behavior remain intact. Other failures become typed
connection, protocol, or remote-operation exceptions with the server message.

The server will not send stack traces. It may send path and parser context
that the same authenticated operating-system user would see locally.

### 5.8 Compression decision

Protocol 1.0 will not add application-level compression.

Reasons:

- the prototype's raw-plane design preserves simple verification and direct
  typed vectors;
- SSH already offers optional stream compression;
- adding zstd would add a dependency, a second size domain, decompression
  limits, and another full-buffer allocation before measurements establish a
  benefit;
- the current 4096-by-4096 slice cap keeps the three result arrays below the
  proposed 128 MiB frame limit.

The implementation will record encoded bytes and request latency in
diagnostics. If representative remote workloads show that transfer dominates,
an additive minor version can introduce a negotiated compressed-plane payload
without changing request semantics.

This decision is explicitly part of the review for this plan.

## 6. Client design

`WireConnection` will own:

- one TCP socket;
- a monotonically increasing atomic request-ID source;
- a send mutex;
- a dedicated receive thread;
- a mutex-protected pending-request map;
- negotiated protocol state;
- connection state and the terminal disconnect reason.

Sending a request will:

1. register its expected response before writing;
2. serialize a verified-size message;
3. write one complete frame under the send mutex;
4. wait on that request's future while polling its `StopToken`;
5. send one cancellation request if local cancellation is observed;
6. decode the typed terminal response.

The receive thread will be the only socket reader. It will verify each frame,
look up the request ID, validate the expected payload type, and complete the
matching promise. This permits the existing Qt worker tasks to issue slices,
line queries, Dataset-window reads, and sequence prefetch concurrently.

On EOF, socket error, or protocol failure, the client will atomically mark the
connection closed and fail every pending operation. Socket shutdown will be
used to unblock the receiver during application exit.

Reconnect is explicit:

- no in-flight operation is retried;
- no dataset handle survives;
- the UI retains the endpoint and dataset path;
- the user can reconnect and reopen that path;
- a future enhancement may add an opt-in automatic reopen policy, but it is
  not safe to infer idempotence for every future request type.

`RemoteDatasetSession` will maintain the latest cache snapshot from responses,
so ordinary UI diagnostics do not need a second network request after every
query.

## 7. Server design

The `amrvis2-server` executable will be Qt-free and will link the production
remote, data, query, I/O, cache, and core libraries.

Startup interface:

```text
amrvis2-server [--port PORT] [--threads COUNT]
               [--max-frame-mib SIZE] [--max-datasets COUNT]
```

The server will always bind to `127.0.0.1`. Port zero will remain available
for tests and automation. The startup line will retain a machine-readable
form:

```text
LISTENING 127.0.0.1 <port>
```

The server process will have:

- one loopback listener;
- an accept loop supporting multiple client connections;
- a bounded shared worker pool;
- configured frame, dataset, cache, and outstanding-request limits;
- signal-driven orderly shutdown.

Each accepted connection gets an isolated `ServerSession` containing:

- handshake state;
- a dataset-handle registry;
- active request IDs and their `StopSource` objects;
- a reader/dispatcher;
- a serialized response writer;
- a session stop source.

Control messages are handled promptly by the reader/dispatcher. Potentially
blocking dataset opens and queries run on the worker pool. Workers hold
`shared_ptr` references to datasets, so closing a handle prevents new work
without invalidating an operation that is already unwinding.

Dataset close will:

- remove the handle from the registry;
- request cancellation of its active operations;
- acknowledge the close;
- release the dataset after outstanding worker references finish.

Connection close will request cancellation for all work, close all handles,
and release the session. A slow or non-cooperative read may finish after the
socket is gone, but its response is discarded and it holds no process-global
session state.

The current query and block-read cancellation checkpoints will be reused.
Cancellation latency is therefore bounded by the longest uncancellable
filesystem operation, not by network handling.

## 8. Socket portability

Promote `Frame` into a production transport implementation with:

- POSIX sockets on Linux and macOS;
- Winsock initialization, close, shutdown, and error mapping on Windows;
- `getaddrinfo` for client connections;
- explicit loopback bind for the server;
- suppression/avoidance of process-terminating broken-pipe behavior;
- RAII socket ownership;
- a shutdown operation that interrupts blocked reads.

No networking types will appear in the dataset or Qt-facing APIs.

## 9. UI and command-line workflow

### 9.1 Server and SSH tunnel

Document the normal workflow:

```bash
# Remote machine
amrvis2-server --port 48192

# Local machine
ssh -N -L 48192:127.0.0.1:48192 user@remote
amrvis2 --connect 127.0.0.1:48192 /remote/path/to/plt00010
```

### 9.2 Desktop actions

Add:

- **File > Connect to Remote Server...**
- **File > Open Remote Plotfile...**
- **File > Open Remote Plotfile Sequence...**
- a connection-status entry in diagnostics.

The connection dialog will collect host and port. The remote-open dialog will
accept server-visible paths; protocol 1.0 will not browse the server
filesystem. The sequence dialog will accept multiple paths and preserve their
entered order.

Switching back to a local open remains supported without restarting the
application. Changing remote endpoints closes the old remote datasets after
cancelling their work.

### 9.3 CLI

Preserve all current local and smoke-test forms. Add:

```text
amrvis2 --connect HOST:PORT REMOTE_PATH [REMOTE_PATH ...]
```

One path opens a dataset and multiple paths open a sequence, matching the
current local positional behavior. Invalid endpoints or conflicting options
will produce a usage error before the Qt event loop starts.

## 10. Build and source layout

Proposed production layout:

```text
schemas/amrvis_wire.fbs
include/amrvis/data/DatasetSession.hpp
include/amrvis/data/DatasetExtract.hpp
include/amrvis/remote/Connection.hpp
include/amrvis/remote/RemoteDatasetSession.hpp
src/data/LocalDatasetSession.cpp
src/data/DatasetExtract.cpp
src/remote/Codec.cpp
src/remote/Frame.cpp
src/remote/Connection.cpp
src/remote/RemoteDatasetSession.cpp
src/remote/Server.cpp
tools/amrvis2_server/main.cpp
```

Exact private-header splitting may change during implementation, but generated
FlatBuffers declarations will remain private to `Amrvis::remote`.

Build policy:

- replace `AMRVIS_ENABLE_WIRE_PROTOTYPE` with
  `AMRVIS_ENABLE_REMOTE`;
- build remote support by default for normal Qt builds and explicitly for the
  headless server preset;
- generate C++ bindings into the build tree from the checked-in schema;
- do not check generated FlatBuffers code into the repository;
- use an installed compatible FlatBuffers package when available and retain a
  pinned FetchContent fallback;
- make the generated header an explicit dependency of every codec target;
- install `amrvis2-server` alongside the desktop executable;
- replace the `wire-prototype` preset with a `remote` headless preset that
  builds the server and all remote tests.

The implementation will retain the currently pinned FlatBuffers version until
the production build is green on the compiler matrix, then document the
minimum compatible version separately from the FetchContent fallback version.

## 11. Validation strategy

### 11.1 Codec and framing unit tests

- Round-trip every request, response, enum, optional field, and metadata type.
- Verify domain-to-wire-to-domain equality.
- Reject wrong identifiers, truncated buffers, corrupt offsets, invalid
  unions, zero/oversized frames, invalid vector lengths, overflowed sizes, and
  semantically invalid requests.
- Test fragmented reads/writes and connection closure between length and body.
- Test protocol major rejection and minor negotiation.
- Test typed error mapping.

### 11.2 Dataset-session equivalence tests

Materialize existing 2-D and 3-D fixtures and compare local with remote:

- metadata and file-version data;
- slice plane and metrics;
- line samples and metrics;
- each Dataset-window level extraction;
- cache clear and cache-budget fallback behavior.

For deterministic fixtures, values, masks, source levels, positions, bounds,
and relevant metrics should match exactly.

### 11.3 Server lifecycle tests

- Multiple requests outstanding on one connection.
- Responses correctly matched when completion order differs.
- Concurrent slices against one dataset.
- Multiple datasets on one connection.
- Multiple client connections.
- Cancellation before dispatch, during a query, and racing with completion.
- Dataset close with active work.
- Clean client EOF and abrupt disconnect.
- Server shutdown with connected and idle clients.
- Duplicate request ID and request-before-handshake rejection.
- Resource-limit enforcement without process termination.

### 11.4 Reconnect tests

- Disconnect fails every pending operation once.
- Old dataset handles cannot be used after reconnect.
- A new connection can reopen the same path and resume queries.
- Destroying a connection unblocks its receiver and does not delay process
  shutdown.

### 11.5 Qt smoke tests

Extend the current materialized-fixture smoke harness to:

- launch an ephemeral loopback server;
- open a remote 2-D dataset and wait for the initial slice;
- open a remote 3-D dataset and verify all three initial views;
- create a remote line plot;
- populate the remote Dataset window;
- step a two-frame remote sequence;
- cancel or supersede an in-flight remote slice;
- shut down without a hanging Qt thread pool or receive thread.

The test driver must always terminate the child server and print both process
logs on failure.

### 11.6 Build matrix and manual validation

- Current default, debug, headless, sanitizer, and new remote presets.
- GCC, Clang, AppleClang, and MSVC warning-as-error builds.
- ASan/UBSan server and client integration tests.
- Manual SSH-tunnel run against a representative remote plotfile.
- Diagnostics review at native and maximum slice sizes.

## 12. Implementation sequence

### Phase 1: Local abstraction with no networking

1. Move Dataset-window extraction out of Qt.
2. Add `DatasetSession` and `LocalDatasetSession`.
3. Route all current Qt data access through the session.
4. Remove duplicate metadata reads during local open.
5. Run the complete existing test and smoke suite.

Exit criterion: local behavior and all existing features remain unchanged
without FlatBuffers or a server.

### Phase 2: Production schema, codecs, and framing

1. Add the full schema and build-time generation.
2. Implement domain conversion and semantic validation.
3. Promote and port the framing/socket layer.
4. Add protocol, codec, corruption, and limit tests.

Exit criterion: every operation round-trips through verified buffers and the
transport tests pass on supported platforms.

### Phase 3: Concurrent headless server

1. Add handshake and session state.
2. Add dataset registry and open/close.
3. Dispatch slice, line, and level-extraction requests.
4. Add request tracking, cancellation, bounded workers, and shutdown.
5. Add server lifecycle and resource-limit tests.

Exit criterion: a non-Qt integration client exercises all operations,
concurrency, cancellation, and teardown.

### Phase 4: Production client and remote dataset session

1. Add the single-reader connection and pending-request map.
2. Add request futures, cancellation forwarding, and typed errors.
3. Add `RemoteDatasetSession` and cache snapshots.
4. Add disconnect and reconnect/reopen tests.

Exit criterion: local and remote dataset-session equivalence tests pass.

### Phase 5: Qt and CLI integration

1. Add dataset-location routing and remote connection ownership.
2. Add remote CLI parsing and desktop dialogs/actions.
3. Enable remote single datasets, sequences, prefetch, line plots, and the
   Dataset window.
4. Add connection diagnostics and user-facing disconnect handling.
5. Add the remote Qt smoke suite.

Exit criterion: the local UI has feature parity for the named remote
workflows, including cancellation and clean exit.

### Phase 6: Packaging, documentation, and prototype retirement

1. Install the server and production remote library dependencies.
2. Update `README.md`, `INSTALL.md`, `docs/building.md`, and the user guide.
3. Document the SSH-tunnel workflow, limitations, and troubleshooting.
4. Replace the prototype preset.
5. Delete `prototypes/flatbuffers_wire` only after its demo is covered by
   production tests.
6. Run the full build/test matrix and the manual SSH validation.

Exit criterion: there is one documented production path and no duplicate
prototype implementation.

## 13. Acceptance criteria

The architecture is complete when:

- existing local-file behavior and tests still pass;
- the Qt client can open the same supported dataset types remotely;
- slice, contour, vector, line-plot, Dataset-window, sequence, animation, and
  export workflows operate with server-side data access;
- one connection safely supports overlapping UI and prefetch requests;
- superseded work is cancelled on the server;
- disconnects fail promptly and cleanly, and reconnect/reopen succeeds;
- all received FlatBuffers are bounded and verified before access;
- server resource limits prevent unbounded client-controlled allocation;
- the server binds only to loopback and the SSH security boundary is
  documented;
- the server and client shut down without blocked receive or worker threads;
- generated bindings come only from the checked-in schema at build time;
- production tests replace the prototype demo, and the prototype is removed.

## 14. Review decisions

Please review these choices before implementation:

1. `DatasetSession` is the shared local/remote boundary; block reads are not a
   public remote operation.
2. The server is loopback-only and relies on SSH for all security.
3. Protocol 1.0 transfers raw owning query results and has no application
   compression or chunking.
4. Remote paths are entered explicitly; there is no remote file browser.
5. Reconnect is explicit and reopens datasets; requests are never
   automatically replayed.
6. Remote support is built by default for the Qt application, with generated
   code kept out of git.
7. The first production release aims for the listed feature parity rather
   than limiting remote support to slices alone.

Implementation should begin only after these decisions and any requested
scope changes are approved.
