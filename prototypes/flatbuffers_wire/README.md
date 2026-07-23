# FlatBuffers wire prototype

> **PROTOTYPE — throw away or absorb after the protocol decision.**

Question: can Amrvis2 keep its Qt UI local while a Qt-free server opens a
remote plotfile and executes the existing demand-driven slice query through a
small, versioned FlatBuffers protocol over a loopback TCP connection?

The server binds only to `127.0.0.1`. The same endpoint can therefore be used
directly on one machine or exposed locally through SSH:

```bash
# Remote host:
./build-wire-prototype/prototypes/flatbuffers_wire/amrvis_wire_server \
  --port 48192

# Local host:
ssh -N -L 48192:127.0.0.1:48192 user@server
./build-wire-prototype/prototypes/flatbuffers_wire/amrvis_wire_client \
  127.0.0.1 48192 /remote/path/to/plotfile
```

SSH owns authentication, encryption, and optional compression. The prototype
adds none of those concerns to the application protocol.

Run the complete build and interaction with:

```bash
./prototypes/flatbuffers_wire/run_demo.sh
```

The demo materializes a small plotfile fixture, starts the headless server on
an ephemeral loopback port, and runs a client that performs:

1. protocol handshake;
2. remote dataset open and metadata response;
3. real `SliceQuery` execution and `ScalarPlane` response.

The four-byte frame length is network byte order. The framed body is an
`AVR2` FlatBuffer whose root `Envelope` carries the protocol version, request
ID, and a payload union. Received buffers are size-limited and verified before
access.
