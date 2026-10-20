# Development workflow

## Native prerequisites and commands

Use a C++20 compiler, CMake **3.21+** (preset schema 3), and Ninja.
The prepared cloud environment provides GCC 14.2.0, CMake 3.31.6, and Ninja
1.11.1. In that environment, activate the build tools in each shell:

```sh
export PATH=/workspace/.cloud-toolchain/bin:$PATH
cd /workspace/chess-engine-lab
```

Other systems can install the same prerequisites with their normal package
manager. Node.js is unnecessary for native builds. The pinned rules header is vendored
locally; no secrets, services or runtime network connection are required.

From the repository root, run:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug

cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release
```

Presets build in ignored `build/<preset>` directories with two parallel jobs.
CTest fails if no tests are found. The smoke test compiles the public contracts,
links the static library, verifies its version at runtime, and returns a nonzero
status on failure in Debug and Release. The separate m1_core executable validates chess
behavior, reference perft and full state/hash restoration in both build modes.
Change the parallelism with `cmake --build --preset native-debug --parallel N`.

For an external build directory or a different native generator:

```sh
cmake -S . -B /tmp/chess-engine-lab-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/chess-engine-lab-build --parallel 2
ctest --test-dir /tmp/chess-engine-lab-build --output-on-failure --no-tests=error
```

For library-only builds, configure a separate directory with
`-DBUILD_TESTING=OFF`. CMake also supports multi-configuration generators;
use `--config Debug` with build and `-C Debug` with CTest in that case.
Never reuse a native build directory for Emscripten or a different compiler.

## WebAssembly correctness targets

The `wasm-debug` preset builds the **same** library, smoke source and M1
correctness source with Emscripten. CTest runs the generated `m0_smoke.js` and
`m1_core.js` WASM runners with Node.js; there is no browser or Worker harness. The core itself does not use Node.

Install the pinned SDK outside the checkout (choose a fresh path if it exists):

```sh
git clone --depth 1 --branch 4.0.10 https://github.com/emscripten-core/emsdk.git /workspace/emsdk-m0
cd /workspace/emsdk-m0
./emsdk install 4.0.10
./emsdk activate 4.0.10
. ./emsdk_env.sh
cd /workspace/chess-engine-lab
export PATH=/workspace/.cloud-toolchain/bin:$PATH  # cloud CMake/Ninja only
emcc --version
node --version
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug
```

SDK tag `4.0.10` resolved to commit
`62a853cd3b3134398ce85cde8bb5cbb2ef0194cb` during M0 setup. On a machine with
another checkout path or system CMake/Ninja, adjust those paths. `EMSDK` must be
set by SDK activation before configuring; the preset uses its toolchain file.
The SDK download requires GitHub and `storage.googleapis.com` access. No TLS or
artifact verification should be disabled to bypass a download failure.

**Local cloud blocker:** the cloud environment returned HTTP 403 for the SDK's
Node archive at `storage.googleapis.com`. The M1 retry on 2026-10-20 returned the same tunnel HTTP 403. No Emscripten compiler was installed,
and no local WASM compilation or execution passed. The GitHub-hosted workflow
subsequently passed both tests; see [M1 results](m1-validation.md). Permit that destination in the
environment network settings and rerun SDK installation/activation and the
three WASM commands above. A preset alone is not portability evidence.

## CI and verified scope

`.github/workflows/native.yml` prepares Ubuntu 24.04 build tools and runs the
native Debug, Release and Profile configure/build/CTest sequences on pushes and
pull requests. Observed current native and WASM results are linked in
[M4 validation](m4-validation.md); earlier milestone reports retain their
historical environments and results.

Linux native behavior has been exercised locally, and GitHub-hosted Linux
CI passes the shared golden tests under Node/WebAssembly. Windows, macOS, mobile
and browser/Worker integration remain unvalidated. M1 includes the rules adapter
and shared perft/special-move/round-trip tests.
See [M1 contracts](m1-contracts.md) and [M1 results](m1-validation.md).

## GitHub-hosted WebAssembly CI

The separate `.github/workflows/wasm.yml` workflow runs on Ubuntu 24.04 for
pushes and pull requests, alongside the unchanged native workflow. It installs
emsdk from immutable commit `62a853cd3b3134398ce85cde8bb5cbb2ef0194cb` and
SDK version `4.0.10` (explicitly selecting its Node 22.16.0 for CTest), then configures/builds the
existing `wasm-debug` preset and runs all seven M0–M4 suites with verbose CTest.
Successful logs include all golden perft counts and the M1 correctness result.
The job summary reports installation, configure, build and test outcomes;
any failed command fails the job. No tests are skipped or relaxed.

This runner can download the SDK independently of the local cloud environment's
HTTP policy. CI results, rather than the presence of this workflow, determine
whether the WASM acceptance gate has passed.


Emscripten builds enable `-fexceptions` at compilation and linking, propagated
through the core target to consumers, because ChessCore's source contract throws
C++ exceptions for invalid input and undo misuse. The same error-path tests run
on native and WASM; the WASM job does not disable them.

## M2 evaluators

The native build produces `libmaterial_evaluator.so` (platform equivalent on
other native targets) separately from the host. `EvaluatorModule::native(path)`
loads the mandatory C entry point. `create(json)` returns a move-owned instance
whose shared module ownership prevents premature unloading. Configuration is
immutable. `EvaluationSelection::replace` prepares a full replacement before
publishing it and increments `generation()` for dependent cache owners.
Selection changes must be serialized with uses of the old evaluator. Each
instance serializes its own evaluation calls and remains quarantined after an
error until replacement. No search or fallback policy is implemented here.

Native evaluator artifact files must remain immutable from hashing/loading until
the module is unloaded, including while any instance retains it. Do not overwrite
or rebuild a loaded library in place: its recorded SHA-256 identifies the bytes
that were hashed, and mutation can invalidate that identity or mapped code.
Build replacements at a separate, versioned path, then stop uses and destroy all
instances before unloading the old module. The in-process loader does not enforce
filesystem immutability or protect against concurrent artifact modification.

WebAssembly selects `EvaluatorModule::bundled("material-v1", wasm_sha256)`.
The digest must be the encompassing WASM artifact's actual SHA-256, supplied by
packaging. `tests/evaluation/run_wasm.cjs` demonstrates this with Node's crypto
module. Native dynamic loading is rejected under WASM. ABI types and algorithm
are identical in both provider paths. Raw values remain in arbitrary units.

CTest includes `m2_evaluation`, `abi_c`, and `abi_cpp` in addition to M0/M1.
The M2 host enforces 1 MiB JSON, 64 nesting levels and 4 KiB diagnostic buffers.
`build/native-release/m2_benchmark /absolute/path/to/libmaterial_evaluator.so`
measures conversion and host/ABI evaluation; see `docs/m2-validation.md`.

## M3 basic search and profiling

Link `ChessEngineLab::Search` and include `chess_lab/search.hpp`. For example,
with an existing usable `Evaluator& evaluator`:

```cpp
auto core = chess_lab::create_chess_core();
auto root = core->initial_position();
auto history = chess_lab::GameHistory::starting_at(*core, *root, true);
auto result = chess_lab::search(*core, *root, history, evaluator, {3, true});
// Check result.status before reading optional decision/score. Never substitute
// a move for an EVALUATOR_FAILED or SEARCH_ERROR result.
```

For imported FEN use the default `complete=false`; record known actual moves
with immutable `after_move`, or construct a history with `replay`. The final
root must match the history including clocks. Callers serialize evaluator
replacement with search. Native/WASM share the same synchronous API and tests.

Use `native-profile` in the configure/build/test preset commands for optimized
symbols and frame pointers. `m3_benchmark` takes a native evaluator path,
depth, measured iterations, warm-up count, optional FEN and optional actual
move replay. It runs both pruning modes. See [M3 validation](m3-validation.md)
for exact invocations, counter definitions, Release measurements and sampling
profiler commands. External profilers and privileged hardware counters are
optional; normal builds do not log or timestamp nodes.


## M4 iterative search

The separate `iterative_search` API preserves fixed-depth `search` above:

```cpp
chess_lab::CancellationSource stop;
chess_lab::IterativeSearchOptions options;
options.max_depth = 3;
options.multipv_count = 3;
auto result = chess_lab::iterative_search(
    *core, *root, history, evaluator, options, stop.token(),
    [](chess_lab::CompletedDepth snapshot) {
      // Detached value, on this search thread. Forward through the controller.
      // All progress remains provisional until final status.
    });
// Inspect result.status. Read result.completed only when present.
// result.unevaluated_fallback is explicitly unscored, and exists only for
// cancellation before depth one. Failures have no final action or ranking.
```

A controller can request `stop.request()` from another native thread. Use a
fresh source for each invocation; token copies retain shared state safely.
Callbacks may request cancellation but must not reenter using active resources.
The controller owns evaluator serialization, workers, deadlines, session IDs
and stale progress suppression. A later search/evaluator failure invalidates
every earlier provisional update. Terminal roots return their outcome at depth
zero without callbacks or fallback. There is no search-owned clock.

Linking the search target under WASM exports a 2 MiB stack budget for the
64-ply uniform-depth cap; Debug also enables stack-overflow diagnostics. The
Node suite includes the full cheap generated depth-64 test, not a real-board
benchmark. Cooperative cancellation paths compile without C++ threads, but
browser message/shared-memory transport remains future controller work.

`m4_benchmark` compares ordering off/on with all-depth exact-line checksums and
per-depth/cumulative work, then runs an M3 depth-N reference. See
[M4 validation](m4-validation.md) for arguments, observed ordering overhead,
pinned CI evidence and the initial WASM stack-budget failure/correction.
