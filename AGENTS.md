# Chess Engine Lab

This guidance applies throughout the repository. Read [README.md](README.md),
[docs/development.md](docs/development.md), and the relevant module README and
approved milestone specifications before changing behavior. The
[architecture baseline](docs/architecture-v1.0.md) includes planned features;
check implementation and current contracts before treating a feature as present.

## Project handoff

- Read [PROJECT_CONTEXT.md](docs/PROJECT_CONTEXT.md) for enduring architectural
  decisions and [CURRENT_STATUS.md](docs/CURRENT_STATUS.md) for the last verified
  milestone, PR, validation and profiling state. Recheck dated claims against
  the relevant source revision, local evidence and current GitHub state.
- Update `docs/CURRENT_STATUS.md` when implementation milestones conclude,
  profiling results become available, or PR/CI states change during authorized
  work. Record the date, SHA and evidence provenance; distinguish implemented
  behavior from approved designs and proposed optimizations. Label ignored,
  local-only results so a fresh session does not assume they were published.
- Modify `docs/PROJECT_CONTEXT.md` only when enduring architecture or workflow
  decisions change. Handoff maintenance does not grant permission to commit,
  push, create a PR or merge; the authorization rules below still apply.

## Development workflow

- ChatGPT supports architecture, planning, and design review. Codex in VS Code
  implements, tests, and prepares changes. GitHub hosts version control, CI,
  code review, and integration.
- Inspect Git status and existing changes first. Work on feature branches based
  on `main`, not directly on `main`; preserve unrelated and user-authored changes.
- Commit, push, create a PR, or merge only with explicit user authorization for
  that specific action and scope. Approval to implement is not approval to
  publish; approval to publish is not approval to merge. Never merge without
  explicit approval. Historical milestone delivery instructions are not standing
  authorization for these actions.
- Keep changes focused and minimal. Preserve public C++ interfaces, evaluator C
  ABI, controller exports/protocols, persistence formats, and established behavior
  unless the task explicitly authorizes a change. Avoid unrelated refactoring,
  dependency upgrades, and generated artifacts in source changes.

## Architecture and boundaries

- `engine/include/chess_lab/` defines repository-owned contracts.
  `engine/src/chess_core.cpp` adapts the vendored rules library; do not expose
  third-party rules types through public interfaces.
- `engine/src/evaluation/` implements the evaluator host, snapshot bridge, and
  material provider. Native plugins use the versioned C ABI; WASM uses bundled
  providers. Evaluators receive current-position snapshots, not move history or
  search state. Raw scores are finite White-perspective values; centipawn
  presentation requires explicit calibration.
- `engine/src/search/` implements fixed-depth and iterative alpha-beta search,
  MultiPV, cancellation, and optional history-aware transposition tables. Only
  completed depths supply authoritative rankings; later failures invalidate
  provisional results. Preserve draw/history semantics and labeled unevaluated
  cancellation fallbacks. Native UCI/process adaptation lives in `engine/src/uci/`.
- `controller/` is a Node.js 24/TypeScript library and CLI. It owns authoritative
  games, clocks, independent rules validation through `chess.js`, UCI subprocess
  lifecycles, analysis, experiments, and durable records. `src/workspace/` and
  `src/server/` add persistent workspaces and authenticated loopback HTTP/WS;
  importing the public library does not start the server or GUI.
- `gui/` is a React/Vite interface served by the controller. It renders Play,
  Analyze, Engines, and Experiments; it does not own authoritative game state.
  Analysis must not auto-play moves. Preserve session/root binding, clock
  authority, durable acknowledgments, and local-server security checks.
- `tests/` contains native/shared WASM correctness suites and benchmarks;
  `controller/test/` contains Node integration tests; `gui/test/` contains
  Vitest/RTL tests and `gui/e2e/` contains Playwright scenarios. Vendored sources
  and notices live in `third_party/` and `THIRD_PARTY_LICENSES.md`; preserve pins
  and licenses. `evaluators/` is reserved for future external packaging.

## Native builds: Windows and Linux

Use a C++20 compiler, CMake 3.21+, and Ninja. On Windows, use an x64 developer
shell with the Visual Studio C++ toolchain initialized. On Linux, use GCC or
Clang with CMake/Ninja available. From the repository root:

```text
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

Substitute `native-release` for optimized builds or `native-profile` for optimized
symbols/frame pointers. Outputs go to `build/<preset>`. The C++ libraries do not
require Node or downloads, but the native UCI CTest integration needs Node 24.
For other generators/build directories, see the development guide; multi-config
builds need the configuration selected for both build and CTest. Keep compiler
and native/WASM build directories separate.

## Controller and GUI

Use Node 24 and the package lockfiles. Build native binaries before integration
tests. On Windows PowerShell, use `npm.cmd` and `npx.cmd` if script execution
policy blocks `npm`/`npx`. Commands below run from the repository root:

```text
npm --prefix controller ci --ignore-scripts
npm --prefix controller run typecheck
npm --prefix controller run lint
npm --prefix controller test
npm --prefix gui ci --ignore-scripts
npm --prefix gui run typecheck
npm --prefix gui run lint
npm --prefix gui test
npm --prefix gui run build
```

Controller tests compile TypeScript first; GUI tests use Vitest/RTL. For browser
tests, build native binaries, the controller, and GUI assets, then run
`npx playwright install --with-deps chromium` and `npm run e2e` from `gui/`.
Playwright supports `CHROMIUM_EXECUTABLE`; see [gui/README.md](gui/README.md).
Launch the built GUI with `node controller/dist/cli.js gui work/my-workspace`.
Register the absolute native engine path, including `.exe` on Windows.

## WebAssembly and CI

Install and activate Emscripten 4.0.10 outside the checkout; activation must set
`EMSDK`. From the repository root, run:

```text
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug
```

The pinned setup and SDK Node selection are in `.github/workflows/wasm.yml`;
the WASM harness may use the SDK's Node rather than the Node 24 required by the
application packages. Shared rules, ABI,
evaluation, search, and calibration tests run under Node/WASM. Native dynamic
plugins and UCI subprocess adapters are excluded; this is not browser/Worker or
mobile integration coverage. Report SDK/network blockers without claiming a pass.

Local Windows x64 validation of the controller portability changes recorded
11/11 native CTest suites and 74/74 controller tests passing. Manual GUI validation
also confirmed launch, registration of `chess_lab_uci.exe`, and gameplay against
the engine. These results apply to the validated revisions, not the CI smoke job.

GitHub Actions configures Ubuntu native Debug/Release/Profile, controller, WASM,
and GUI/browser gates. The Windows Server 2022 job builds the controller and GUI,
runs frontend tests, and checks desktop static assets, executable registration,
orientation, and launcher IPC using a Node-based UCI fixture. It does not build
the C++ engine, run the full controller suite, or validate Windows browser gameplay.
There is no macOS job. Configured CI is not evidence that a particular commit
passed. Historical platform limitations in validation documents may be outdated.

## Portability and validation

- For paths, executables, libraries, and process changes, consider Windows,
  Linux, and macOS explicitly. Use native path APIs, absolute executable paths,
  argument arrays, and bounded process cleanup; account for spaces, separators,
  symlinks/junctions, locking, and filesystem durability semantics.
- Reuse `controller/test/native-paths.mjs`: Windows executables use `.exe` and
  libraries `.dll`; Linux libraries use `lib*.so`; macOS libraries use
  `lib*.dylib`. Preserve `CHESS_LAB_UCI` and `CHESS_LAB_BUILD` independently.
  Native UCI test libraries stay beside the selected executable; calibration
  fixtures use explicit `CHESS_LAB_BUILD`, otherwise the executable directory.
- Add focused regression coverage for bug fixes and relevant validation for new
  features. Run affected suites and cross-layer integration checks; engine/shared
  contract changes also need native/WASM validation where applicable. Do not
  weaken assertions, skip failing tests, or silently relax durability guarantees
  to obtain a pass.
- Before handoff, review the diff and Git status and run `git diff --check`.
  Report commands, platform/toolchain, pass/fail/skip counts, unresolved issues,
  unavailable checks, and unverified assumptions. Distinguish tests executed now
  from historical logs, manual reports, and pending CI; do not infer portability
  from another platform's success.
