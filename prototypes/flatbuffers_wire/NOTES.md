# Prototype verdict

The request/response shape fits the current query boundary without changing
the core domain types. A Qt-free server successfully opened a real plotfile,
executed `SliceQuery`, and returned its `ScalarPlane` to a separate client.

Returning the raw plane preserves local palette, range, and logarithmic
remapping without another server request. The cost is one copied FlatBuffers
vector per plane; measure that cost at production display sizes before
choosing it over a server-rendered image.

If this design is absorbed, keep:

- the versioned envelope, request ID, payload union, identifier, verifier, and
  frame-size limit;
- the schema-to-domain conversion boundary;
- the loopback-only server default;
- build-time generation from the schema.

The production design still needs cancellation, multiple outstanding
requests, connection shutdown/reconnect behavior, and a decision about
`ScalarPlane` compression. Delete this prototype after those requirements
move into production code or an ADR.
