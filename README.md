# Chess Engine Lab

**S2.1 + E1-C1 candidate:** Material remains the packaged default evaluator; C1 (material + PST) is available as a selectable evaluator. See [S2.1 PVS and E1-C1](docs/s2-pvs-e1.md) for native/bundled selection and validation status.

**S2.0 accepted development candidate (2026-11-25):** PR #55 is merged at `0a23602e30ec0151200f5b874902b5c49d499e27`. The integer-centipawn migration passed standard and COMP1 optimized Windows/WSL development validation and was explicitly accepted. See [S2.0 scoring contract](docs/s2-centipawn.md) and [paired validation results](docs/s2-centipawn-validation.md). The committed-revision 54-gate Full CI, historical-reference review and separate publication/merge approvals remain pending. Older dated entries describe their historical scope.

See [S1 production policy and optimized local builds](docs/s1-production.md) for the merged S1 quiescence policy, compatibility controls, and reproducible opt-in Clang/PGO workflow; see [production validation results](docs/s1-production-validation.md) for the historical isolated native evidence. The current scoring contract is S2.0.

C++20 chess engine, headless controller and local desktop GUI following the
[approved architecture](docs/architecture-v1.0.md).

M0–M6 supply chess rules, configurable evaluators, search, history-safe optional
transposition tables and durable paired experiments. **M7 desktop GUI is
implemented for review**: Play, Analyze, Experiments and Engines over an
authenticated local server, with backward-compatible optional evaluator
calibration. See the [GUI guide](gui/README.md), [M7 validation](docs/m7-validation.md)
and [design checkpoint](docs/m7-design-checkpoint.md). No M8 work is included.

M7.1 adds independent engine search limits, optional bounded UCI experiment
capture and per-move search statistics. See the [configuration example](controller/README.md#m71-independent-experiment-limits-and-diagnostics)
and [local validation](docs/m7.1-validation.md).

M7.2 optimizes single-PV root search and moves actual-game root draw-claim
selection to the controller using trusted final scores. See the
[compatibility and measured results](docs/m7.2-validation.md).

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
| `controller/` | M6 headless orchestration and M7 workspace/local server |
| `gui/` | M7 React/Vite desktop UI and browser/component tests |
| `third_party/` | Pinned MIT chess library and dependency policy |
| `docs/` | Approved architecture, milestone instructions, developer guide |

The C++ contracts are source-level APIs, separate from the approved evaluator C ABI.
See [contract notes](docs/m0-contracts.md) and
[third-party license inventory](THIRD_PARTY_LICENSES.md). This project does not
assign a license to the project's original code.
