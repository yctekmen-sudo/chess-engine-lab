# Chess Engine Lab

C++20 chess engine and experimentation platform, following the approved
[architecture](docs/architecture-v1.0.md) and
[milestone kickoff](docs/codex-kickoff-m0-m1.md).

Current implementation: **M1 chess core**, with a pinned Disservin adapter,
FEN, legal moves, reversible traversal, snapshots, position identity and
correctness tests. Native and Node/WebAssembly correctness tests pass in
GitHub Actions with pinned Emscripten 4.0.10. See [M1 validation](docs/m1-validation.md).
M1 is awaiting review; M2 has not begun.

## Build and test

Prerequisites: C++20 compiler, CMake 3.21+, and Ninja. From the repository root:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

Use `native-release` in all three commands for an optimized build. See the
[development guide](docs/development.md) for cloud tool activation, custom
build directories, WebAssembly instructions, and validation limitations.
The native core has no Node.js, browser, process, network, or GUI dependency.
No dependencies are downloaded by CMake or needed at runtime.

## Layout

| Path | Responsibility |
| --- | --- |
| `engine/` | Core library and our own opaque chess/source snapshot contracts |
| `tests/` | Contract smoke, perft, special-rule and restoration tests |
| `evaluators/` | Reserved for M2 evaluator ABI and modules |
| `controller/` | Reserved for M6 desktop/headless orchestration |
| `gui/` | Reserved for M7 UI and M8 mobile packaging |
| `third_party/` | Pinned MIT chess library and dependency policy |
| `docs/` | Approved architecture, milestone instructions, developer guide |

The C++ contracts are intentionally provisional; they are not the future C ABI.
See [contract notes](docs/m0-contracts.md) and
[third-party license inventory](THIRD_PARTY_LICENSES.md). This project does not
assign a license to the project's original code.
