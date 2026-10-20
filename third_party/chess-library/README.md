# Disservin/chess-library pin

- Origin: https://github.com/Disservin/chess-library
- Immutable commit: 53e6a841dcda7059a2af363d85f785ef1817304a
- Retrieved: 2026-10-20
- License: MIT; complete upstream notice in LICENSE and header.
- Vendored files: include/chess.hpp (stored as chess.hpp), LICENSE, unmodified.
- Native and Node/WASM validation: see docs/m1-validation.md (Emscripten 4.0.10).

The build uses this local header only; it never fetches a mutable branch.
To update, explicitly review and select a new immutable commit, copy the two
upstream files unchanged, update this inventory and checksums, and rerun native
and WASM golden tests. A newer pin must not silently imply portability.

SHA-256:
f2c8e2e929641e2c71cbe9d8abd718cf3cac46c2a34531215ebd733905e98d7f  third_party/chess-library/chess.hpp
5860e7607afbf4c7e91bad5549b71d16fc4eceb90f0c671cd77a343ae7461a2a  third_party/chess-library/LICENSE
