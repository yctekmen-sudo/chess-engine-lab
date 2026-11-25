# S1 production policy and optimized local builds

Candidate based on pre-S1 main `84ea6ba22ffc52a5a4c42e5218c969052c0e99cb`.
Accepted S1.13 source was independently validated in
`CELab-S1-integration-evidence-s1int-20261125T032534Z-ef993d8b.zip`
(324 native tests, 18,594 searches, exact frozen parity). That evidence applies
only to the original experimental overlay. Production v2 subsequently passed
isolated native validation on both platforms; see [final production validation
and performance](s1-production-validation.md). Committed-revision Full CI
and separate publication approval remain outstanding.

## Policy

Quiescence defaults on; `CELAB_S1_QUIESCENCE=0` is a process-start opt-out
and `-DCHESS_LAB_QUIESCENCE_DEFAULT=OFF` is a portable build-time opt-out.
The native tactical generator includes captures and quiet/capture promotions;
check evasions are searched, with the retained emergency depth cap. Ordinary
quiet moves and ordinary quiet checks are not added to the tactical search.
SEE uses the accepted native/reused bitboard path, with generic adapter
fallback. The accepted SEE nonpositive-capture and delta-0 pruning are enabled
for the exact default material identity/configuration. Other evaluator
identities/configurations use SEE ordering without these pruning rules; this
avoids applying material units to arbitrary evaluator scores. The existing
material identity check assumes truthful evaluator metadata, not a security
boundary. There is no evaluator ABI or public C++ API change. The existing
TT policy is unchanged. Historical experimental S1 selectors no longer select
search variants. Scalar reference and direct-input/check differential verification
remain available via `CELAB_S17C_VERIFY=1` (diagnostic only). To enable legacy
timing traces in a separate build use `-DCHESS_LAB_S1_DIAGNOSTICS=ON` and
`CELAB_S11_TRACE=1; CELAB_S17A_TRACE=1` in the process environment.

Static-horizon oracle suites explicitly opt out of QS rather than relaxing their
assertions. The production default, explicit opt-out and differential verification
have dedicated native/WASM horizon regressions including non-default material
configuration. The retired synthetic variant registrations all invoked the same
fixture; the new registrations cover the supported modes. The real UCI protocol tests run both in the historical static-horizon mode
and in the default mode. Full CI and the controller/GUI integration tests
remain required.

## Portable build

Use the existing native-debug, native-release and WASM presets as before.
Optimized builds are explicit local alternatives, not the portable distribution
configuration. Native targeting can make binaries incompatible with other CPUs.

## Optimized local build

Use Python 3, CMake, Ninja and matching Clang/LLVM tools. On Windows start
an x64 Visual Studio Developer PowerShell and choose `clang-cl.exe`; WSL uses
`clang++` and matching `clang`, LLD, llvm-ar, llvm-ranlib, llvm-profdata and the
Clang profiling runtime. The script requires a new output directory without
spaces, and does not install software or alter source files. From the repo root:

```text
python scripts/performance/build_optimized.py --output build/optimized-local --compiler <absolute-clang-cl-or-clang++-path>
```

On Linux if the C compiler is not the matching sibling `clang`, supply `--cc`.
Use `--parallel 4` (default) or another explicit build concurrency. The script
builds native O3 + Full LTO + instrumentation, trains on the pinned independent
48-root corpus (120 searches across QS and TT modes), merges the profiles and
builds a fresh profile-use configuration at the same source path. Only the six
production targets consume profiles; test-only TUs do not. Stale production
profiles are errors. Logs include flags, actual driver CPU/features, compiler
and linker versions, source hashes, raw profiles and binary hashes. The final
executable is in `build/optimized-local/use/`; the neighboring material evaluator
must remain beside it. Run CTest on that build and the ordinary portable build
as required. Re-run in a new output directory after source or toolchain changes.
The training corpus is synthetic and independent of the frozen S0 validation
roots, not a claim of representative playing strength for all future evaluators.

## Evidence and integration

Original integration showed same-compiler QS-off NPS gains of 23.23%/21.57%
(MSVC/GCC) and 34.75%/30.94% (optimized Clang Windows/WSL) versus exact main.
QS-on search elapsed time fell 8.95%/19.59% (standard) and 15.88%/21.40%
(optimized). Depth-3 tactical matches were 15/204 with QS vs 16/204 without;
exact mate detections were 9/32 vs 8/32. These do not establish strength gains.
Fresh production v2 comparisons against exact main and S1.13 are complete;
see [the production validation report](s1-production-validation.md) for all
paired results, evidence provenance and limitations. Do not merge or replace
historical baselines without separate approval and clean Full CI.
