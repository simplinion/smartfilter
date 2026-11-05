# SFE1 protocol

Transport: sequential request/response on a persistent Unix stream socket.
All integers and IEEE-754 float32 values are big-endian. Each frame is:

```
4 bytes   magic: SFE1
u32       payload byte length (maximum 4 MiB)
bytes     payload
```

Request payload:

```
u32       item count (0..32)
repeat item count times:
  u8      role: 0=query, 1=passage
  u32     UTF-8 text byte length (1..65536)
  bytes   text, no NUL terminator
```

Text must contain non-whitespace characters and valid UTF-8, without NUL.
A zero-item request is a readiness/model-identity check. The server only
opens its socket after loading and warming the model.

Successful response payload:

```
u32       status = 0
u32       embedding dimension
u32       item count, matching the request
u32       model identity byte length
bytes     model identity, no NUL
float32[] vectors, item-major, dimension values per item
```

The model identity includes the pinned revision, manifest hash and pooling
algorithm identifier. A client must reject identity/dimension changes within
a query session. The current C client supports up to 4096 dimensions and
255 identity bytes. It rejects NaN/infinite/zero vectors and normalizes valid
vectors before computing cosine similarity.

Error response payload is `u32 status=1` followed by a generic UTF-8 error.
Malformed outer framing or lost connections close the connection. There are
no request IDs or multiplexing: a client must finish one exchange before
sending the next. Separate clients use separate connections. The client
uses one absolute deadline for sending and receiving each complete RPC.

The server encodes role prefixes, tokenization and pooling; the client must
not add `query:`/`passage:` itself. Different roles have different cache keys.
SGR color/style sequences are removed before embedding and cache lookup.
Caching and batching do not change response order. Service errors never
instruct the client to discard logs; this prototype disables semantic
selection for the remaining stream and preserves all unscored buffered input.
