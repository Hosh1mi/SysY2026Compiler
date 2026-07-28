# Deprecated AArch64 backend

The legacy AArch64 implementation is retained for differential diagnosis and
is reachable only through the `--legacy-backend` driver option and the
`backend/backend_depre/arm64/codegen.hpp` compatibility facade.

Its implementation files remain in their historical `backend/arm64` paths so
that the retired backend can still be built without a mechanical include-path
rewrite. They are not part of the default code-generation pipeline. New
backend work belongs under `backend/arm64/rewrite`.

The historical implementation contains fixed x16/x17 scratch conventions.
Those conventions are one reason it is deprecated and must never be copied
into the rewrite.  In the rewrite, x16/x17 occur only in the target register
description and may be chosen by graph coloring like other caller-saved
registers; no lowering or post-RA pass names them as scratch registers.
