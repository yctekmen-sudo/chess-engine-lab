# Third-party license inventory

No license is assigned to this project's original code.

| Component | Revision | License | Bundled files / notices |
| --- | --- | --- | --- |
| [Disservin/chess-library](https://github.com/Disservin/chess-library) | 53e6a841dcda7059a2af363d85f785ef1817304a | MIT | third_party/chess-library/chess.hpp and LICENSE, both unmodified; retain copyright and permission notice with distributions. |

Pin provenance and checksums: [dependency record](third_party/chess-library/README.md).
The header is used privately by the replaceable adapter. No dependency is
downloaded during configure, build or runtime.

Compiler, CMake, Ninja, Emscripten/SDK and Node.js are external development tools
with their own licenses; none is vendored here. Record distributed runtime or
toolchain artifacts and their obligations when packaging is introduced.

## M2 evaluation utilities

- nlohmann/json v3.11.3, MIT: `third_party/nlohmann/LICENSE.MIT`.
  Provenance/checksum: `third_party/nlohmann/README.md`.
- PicoSHA2 commit `27fcf6979298949e8a462e16d09a0351c18fcaf2`, MIT:
  `third_party/picosha2/LICENSE`. Provenance/checksum:
  `third_party/picosha2/README.md`.

Both are vendored and require no build-time network access.

## M6 controller dependencies

Exact resolutions and registry integrity hashes are in `controller/package-lock.json`.

| Package | Version | License | Use |
| --- | --- | --- | --- |
| chess.js | 1.4.0 | BSD-2-Clause | Runtime legal move/board adapter; notice copied to `controller/CHESS_JS_LICENSE`. |
| TypeScript | 5.9.3 | Apache-2.0 | Build/typecheck/lint development tool; package LICENSE.txt. |
| @types/node | 24.10.0 | MIT | Development declarations; package LICENSE. |
| undici-types | 7.16.0 | MIT | Transitive development declarations; package LICENSE. |

Runtime is Node.js 24 (Node distribution license applies separately).
M6 adds no license grant to original repository source. Keep dependency notices
when distributing dependencies or generated controller bundles. There is no
runtime download or network service requirement after `npm ci` and native build.
