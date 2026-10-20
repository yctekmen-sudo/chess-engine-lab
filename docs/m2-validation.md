# M2 validation

## Provenance and scope

Branch `m2/evaluation-abi`, based on fetched `origin/main`
`3f938236b9154678edffa22600287fbe63546c5c`. M0 merge PR #1 and M1 merge
PR #2 are ancestors. Verified all four kickoff-required files on that base.
The approved architecture, ABI specification and public header are unchanged.
No search, quiescence, TT, UCI, controller, GUI or M3 work is included.
Decision 37 is implemented at the M2 boundary by propagating failures and
quarantining the instance until replacement. Whole-search invalidation belongs
to M3/M4; no fallback decision is provided here.

## Implementation and verification

The independently built native shared material evaluator and bundled provider
compile the same source. Only the native wrapper exports the required entry.
Weights are finite, nonnegative and capped at 1e100 to prevent overflow even
on structurally valid promotion-heavy snapshots; units remain arbitrary.

C11 and C++20 translation units assert status width, both struct sizes,
8-byte structure alignment and every top-level field offset (including wasm32
function-pointer slots). The header itself is kept byte-for-byte unchanged.

M2 tests cover all twelve bitboards and snapshot fields against an independent
FEN decoder, initial/Kiwipete/castling/check positions, capturable and
noncapturable raw EP, maximum uint32 clocks and both colors' four promotions.
They run the same golden material vectors against native loading and bundled
providers, with turn-independent White-positive values and varied parameters.

Tests cover ABI major/minor/size/pointer rejection, incompatible snapshot
requirements, missing library and symbol, null arguments, insufficient buffers
without partial JSON, invalid snapshot fields, invalid JSON/UTF-8, duplicates,
unknown/invalid parameters, flat scalar schema rules, required/default semantics,
metadata size/length/NUL limits, failed creation and effective-configuration
mismatch cleanup. Controlled providers test OOM, unknown/negative statuses,
error diagnostics, NaN and both infinities. Evaluation failure quarantines the
instance and prevents further provider calls. Live-handle counters verify
cleanup, and native instances outlive their module facade without unloading.

Independent configurations and interleaved calls produce stable scores.
Failed replacement preserves the active instance and generation; successful
replacement increments the cache invalidation generation. This is an explicit
signal for future cache owners, not an implementation of search caches.

Identity tests cover canonical effective defaults, whitespace/order/numeric
spelling, Unicode UTF-16 key ordering, Unicode scalar lengths, negative zero,
JCS decimal/exponent thresholds and a published SHA-256 `abc` vector. Identity
records ABI/snapshot/schema versions, module ID/version, canonical configuration,
artifact digest, provider, compiler, engine, platform and build mode. WASM's Node
launcher hashes the actual encompassing `.wasm` before supplying the digest to
the registry. Packaging callers must likewise supply the encompassing artifact's
SHA-256; the host rejects missing/malformed digests.

## Commands and results

Local environment: Linux x86_64, GCC/G++ 14.2.0, CMake 3.31.6, Ninja 1.11.1.
Run from the repository root with `/workspace/.cloud-toolchain/bin` on PATH:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
```

Initial Debug execution passed 5/5 tests (M0, M1, M2, C layout, C++ layout),
16.08 seconds. Release also passed 5/5 (0.44 seconds). Address/undefined-behavior
instrumented builds passed 5/5 (39.36 seconds), with leak detection disabled
because the managed environment uses ptrace. No leak-sanitizer claim is made.
Native and WASM CI both passed at `d839498144973834673e0421e50f39648cb4a8a7`;
see the observed CI results below.

The pinned GitHub Actions WASM job uses Ubuntu 24.04, Emscripten 4.0.10,
emsdk commit `62a853cd3b3134398ce85cde8bb5cbb2ef0194cb`, SDK Node 22.16.0:

```sh
source "$RUNNER_TEMP/emsdk/emsdk_env.sh"
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

`-fexceptions` is enabled during compilation and linking. The WASM harness
uses the same C/C++ layouts, golden vectors, JSON and hostile-provider tests.
Native CI has Debug and Release jobs on pinned Ubuntu 24.04 and pinned checkout.
The local SDK is absent, as documented in M1; successful WASM evidence comes
from the observed GitHub-hosted executions below.

## Performance

Method: optimized Release build, separately loaded native material evaluator,
steady_clock, 1,000 warm-up host calls then 200,000 conversions followed by
200,000 host+ABI evaluations per position. Snapshot construction/FEN parsing,
module loading, configuration parsing and hashing are outside timed loops.
The checksum consumes results. Conversion includes validation. Evaluation
includes host validation, serialized-instance locking, diagnostics validation,
ABI dispatch, module snapshot validation and material arithmetic. Measurements
are observational wall-clock averages, not isolated instruction counts.

```sh
build/native-release/m2_benchmark "$PWD/build/native-release/libmaterial_evaluator.so"
```

Measured on an AMD EPYC 9V74 virtual machine (three visible vCPUs), GCC 14.2
Release, 200,000 calls per row, checksum 401000:

| Position | Validated conversion ns/call | Native host + ABI ns/call | Bundled raw ABI ns/call |
| --- | ---: | ---: | ---: |
| Initial | 129.747 | 176.013 | 32.956 |
| Kings plus White pawn on a2 | 117.140 | 179.516 | 33.728 |

The raw ABI column invokes the same material implementation through its bundled
function table without the host wrapper; it includes module input validation,
not just popcount arithmetic. It uses `(NULL,0)` diagnostics; host measurements
use the normal 4 KiB diagnostic buffer. The extra host work is observable in
this microbenchmark. These single-run averages have no confidence intervals;
shared-VM scheduling, warm caches and timer overhead limit interpretation.
Earlier samples varied with concurrent system activity. No CPU affinity,
architecture-specific instruction flags, LTO or instruction tracing was used.
No ABI layout/performance claim is inferred from CPU instruction expectations.

## Limits

Only Linux native and Node/WASM are targeted by this validation. Windows/macOS
loading paths are implemented but unvalidated; browser/Worker, Android and iOS
validation is unclaimed. Native plugins execute in-process: invalid *non-null*
pointers or arbitrary native memory corruption cannot be made safe by this ABI.
Callers serialize selection changes with evaluator use; instances serialize
individual evaluation calls internally. Destroy instances after uses stop.


## Initial WASM failure and correction

[Initial run 39379396453](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39379396453)
at `59a98d1` compiled successfully. M0/M1 and both ABI layout tests passed;
M2 failed with a memory access trap inside artifact hashing. The hashing
function placed a 64 KiB I/O array on Emscripten's default 64 KiB stack,
leaving no room for call frames. The correction allocates that I/O buffer on
the heap. It preserves the approved ABI, hash algorithm, stack settings and
all tests. This initial run is not a passing WASM validation result.

The final canonicalizer also has exact RFC 8785 Appendix B binary64 vectors,
including significant-digit rounding for large fixed-format integers.


## Supplemental native instrumentation

```sh
cmake -S . -B build/sanitize-m2 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sanitize-m2 --parallel 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-m2 --output-on-failure
```

All 5/5 passed (39.36 s), without reported compiler warnings or sanitizer
errors. After the canonicalization and WASM hashing corrections, rebuilt the
instrumented targets and reran `ctest --test-dir build/sanitize-m2 -R
m2_evaluation --output-on-failure` with the same `ASAN_OPTIONS`: passed (4.99 s).
Native Debug full suite passed 5/5 in 11.21 s, then its M2 test passed again
(1.08 s) after the hash buffer correction. Final Release passed 5/5 in 0.44 s.


## Observed GitHub Actions acceptance

Implementation commit: `d839498144973834673e0421e50f39648cb4a8a7`.
PR: https://github.com/yctekmen-sudo/chess-engine-lab/pull/3

- [Native push run 39379684606](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39379684606):
  Debug and Release jobs both completed successfully, 5/5 tests each.
  GCC/G++ 13.3.0, CMake 3.28.3 and Ninja 1.11.1 on Ubuntu 24.04;
  Debug CTest: 6.77 s, Release CTest: 0.85 s.
- [Native PR run 39379689114](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39379689114):
  both matrix jobs completed successfully.
- [WASM push run 39379684542](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39379684542):
  configure/build and all 5/5 tests passed, 49.18 s total CTest.
  M0: 0.05 s; M1: 48.44 s; M2: 0.55 s; C layout: 0.03 s;
  C++ layout: 0.04 s. All golden FEN scores, all M1 perft counts,
  hostile-provider checks and RFC 8785 vectors passed.
- [WASM PR run 39379689095](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39379689095):
  also completed successfully.

The push WASM test recorded the actual encompassing artifact SHA-256
`baa3d84c697ac28427b27df322cc1c765245bcfc31d556380d9bf600694021aa`.
Both languages verified the 160/72-byte, 8-byte-aligned structures on wasm32.
These results establish Node/WASM correctness only, not browser/device behavior.
The final report update changes documentation only; implementation and tests
are exactly those of the validated commit above. Approved ABI header and
contract remain unchanged. M2 stops for review; no merging or M3 work.

## Architecture-review performance follow-up

Baseline: `5c6961656db787eed2587c1d64f72a2388245333`, on the existing
`m2/evaluation-abi` branch. Two targeted implementation changes:

- The bridge obtains the source `squares()` span once before the 64-square loop.
- Status checking returns immediately only for `CHESS_EVAL_OK` with an empty
  diagnostic (`error[0]=='\0'`). This proves NUL termination and valid empty
  UTF-8 without constructing/serializing JSON. Every other case retains the
  existing termination, UTF-8, known-status and success-diagnostic checks.
  Finite-score validation and failure quarantine remain in place.

New fixtures return a finite score with success plus a nonempty, unterminated,
or invalid-UTF-8 diagnostic; each must fail and quarantine the instance.
Existing fixtures continue to reject unknown/negative statuses with empty
buffers, explicit errors, malformed error diagnostics, and NaN/±infinity.
The approved architecture, ABI specification and public header are unchanged.

### Before/after benchmark

Same Linux x86_64 AMD EPYC 9V74 VM, GCC 14.2.0, Release preset. Built and ran
the unchanged benchmark at the baseline before editing, and preserved its
executable and native evaluator library in ignored `work/m2-perf/before/`.
Built the updated host/bridge and preserved the new binaries in
`work/m2-perf/after/`. The native material library is unchanged. No evaluator
artifact was modified while a benchmark process had it loaded.

```sh
export PATH=/workspace/.cloud-toolchain/bin:$PATH
cmake --preset native-release
cmake --build --preset native-release
# Before editing, then after editing/rebuilding, respectively:
taskset -c 0 work/m2-perf/before/m2_benchmark "$PWD/work/m2-perf/before/libmaterial_evaluator.so"
taskset -c 0 work/m2-perf/after/m2_benchmark "$PWD/work/m2-perf/after/libmaterial_evaluator.so"
```

Ran 11 pairs serially, reversing before/after order on alternate pairs, all
pinned to CPU 0 with no concurrent build/test commands. Each process uses the
existing 1,000-call warm-up and 200,000 calls per measurement/position. Every
checksum was 401000. Reported values are medians of those 11 process samples,
not the initial baseline sample. Absolute differences are after minus before;
percent differences use the before median as denominator.

| Position | Operation | Before ns/call | After ns/call | Difference ns | Difference % |
| --- | --- | ---: | ---: | ---: | ---: |
| Initial | Validated conversion | 131.780 | 80.129 | -51.651 | -39.19% |
| Initial | Native host + ABI evaluation | 180.816 | 81.449 | -99.367 | -54.95% |
| Initial | Bundled raw ABI evaluation | 32.032 | 31.446 | -0.586 | -1.83% |
| Kings + White pawn a2 | Validated conversion | 121.834 | 64.431 | -57.403 | -47.12% |
| Kings + White pawn a2 | Native host + ABI evaluation | 179.816 | 82.061 | -97.755 | -54.36% |
| Kings + White pawn a2 | Bundled raw ABI evaluation | 32.345 | 31.844 | -0.501 | -1.55% |

Observed min–max ranges (before → after, ns/call): initial conversion
124.753–251.967 → 76.707–125.867; initial host evaluation
170.664–388.392 → 76.165–140.457; pawn conversion
113.750–134.816 → 60.020–214.825; pawn host evaluation
166.351–245.861 → 75.831–96.184. Raw evaluator ranges were
30.012–35.093 → 30.226–85.070 (initial), and
29.939–36.005 → 30.291–45.004 (pawn).

The medians decreased on this host. Shared-VM scheduling still produced
outliers despite CPU affinity. No statistical significance, CPU instruction
behavior, cross-platform speedup, or WASM performance improvement is claimed.
The small raw-ABI differences are not evidence of a change to the unchanged
material evaluator. Timed work includes validation; the host path includes
locking, diagnostic handling and finite-score checks. Loading, hashing,
configuration and FEN parsing remain outside timed loops.

### Native artifact immutability

Native evaluator artifacts must remain immutable from hashing/loading until
unloaded, including the lifetime of every module-owned instance. Overwriting or
rebuilding a loaded library can invalidate both its recorded artifact SHA-256
identity and its mapped executable code. Build replacement libraries at a new,
versioned path, stop uses and destroy instances, then unload the old module.
The in-process loader does not enforce filesystem immutability or protect
against concurrent artifact modification. See `docs/development.md`.

### Follow-up correctness validation

Run all suites using the unchanged native and pinned WASM workflows:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release

# GitHub-hosted Ubuntu 24.04, pinned Emscripten 4.0.10 / Node 22.16.0:
source "$RUNNER_TEMP/emsdk/emsdk_env.sh"
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

Observed follow-up execution results and CI links will be recorded after the
runs complete. Local WASM remains unavailable because the pinned SDK is absent;
Node/WASM evidence comes from the GitHub-hosted workflow. Browser/device and
Windows/macOS validation remain unclaimed. No merge or M3 work is included.
