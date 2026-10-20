# Chess Engine Lab

C++20 chess engine and experimentation platform, following the approved
[architecture](docs/architecture-v1.0.md) and
[milestone kickoff](docs/codex-kickoff-m0-m1.md).

Current implementation: **M3 basic search**, on top of the M1 chess core and
M2 evaluator host/C ABI. Search provides fixed-depth minimax, optional
alpha-beta, structured scores, FIDE draw claims and deterministic principal
variations. See [M3 validation](docs/m3-validation.md) for commands, measured
benchmarks, test evidence and limitations. M3 awaits architecture/code review;
M4 has not begun.

## Build and test

Prerequisites: C++20 compiler, CMake 3.21+, and Ninja. From the repository root:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

Use `native-release` for throughput benchmarks or `native-profile` for an
optimized build with symbols and frame pointers (same configure/build/test commands). See the
[development guide](docs/development.md) for cloud tool activation, custom
build directories, WebAssembly instructions, and validation limitations.
The native core has no Node.js, browser, process, network, or GUI dependency.
No dependencies are downloaded by CMake or needed at runtime.

## Layout

| Path | Responsibility |
| --- | --- |
| `engine/` | Core, evaluation and search libraries with repository-owned contracts |
| `tests/` | ABI, perft, rule, search oracle and failure tests; headless benchmarks |
| `evaluators/` | Future external evaluator packaging (M2 implementation is in engine/) |
| `controller/` | Reserved for M6 desktop/headless orchestration |
| `gui/` | Reserved for M7 UI and M8 mobile packaging |
| `third_party/` | Pinned MIT chess library and dependency policy |
| `docs/` | Approved architecture, milestone instructions, developer guide |

The C++ contracts are source-level APIs, separate from the approved evaluator C ABI.
See [contract notes](docs/m0-contracts.md) and
[third-party license inventory](THIRD_PARTY_LICENSES.md). This project does not
assign a license to the project's original code.
