# Development workflow

## Local Windows and WSL2 CI

Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1`
for the local full matrix. See [local-ci.md](local-ci.md) for prerequisites,
coverage, quick selections, source-identity checks and timestamped reports.
The harness does not synchronize checkouts or change GitHub settings. Hosted
workflow definitions are retired; GitHub hosts source control, PRs and reviews.
Only a clean, unchanged same-commit full run can claim authoritative local acceptance;
quick, dirty, blocked and incomplete runs are qualified explicitly.

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
git init /workspace/emsdk-m0
cd /workspace/emsdk-m0
git remote add origin https://github.com/emscripten-core/emsdk.git
git fetch --depth 1 origin 62a853cd3b3134398ce85cde8bb5cbb2ef0194cb
git checkout --detach FETCH_HEAD
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

**Historical M0/M1 cloud blocker:** the cloud environment returned HTTP 403 for the SDK's
Node archive at `storage.googleapis.com`. The M1 retry on 2026-10-20 returned the same tunnel HTTP 403. No Emscripten compiler was installed,
and no local WASM compilation or execution passed. The GitHub-hosted workflow
subsequently passed both tests; see [M1 results](m1-validation.md). Permit that destination in the
environment network settings and rerun SDK installation/activation and the
three WASM commands above in that environment. This historical blocker does not
describe the subsequently validated Windows/WSL2 setup. A preset alone is not
portability evidence.

## CI and verified scope

Local Full mode runs native Debug/Release/Profile configure/build/CTest on
Windows 11/MSVC and WSL2 Ubuntu 24.04/GCC, controller and GUI checks on both,
Windows desktop smoke, Linux Playwright and WASM correctness. The 54-gate mapping,
reviewed coverage floors and acceptance rules are in [local-ci.md](local-ci.md).
Executed results are attributed to their exact revisions in
[CURRENT_STATUS.md](CURRENT_STATUS.md). Older milestone reports retain their
historical hosted environments and results. macOS, mobile, browser/Worker WASM
integration and Windows browser gameplay are not local acceptance gates.

## Local WebAssembly CI

`scripts/ci/run-linux.sh` checks emsdk immutable commit
`62a853cd3b3134398ce85cde8bb5cbb2ef0194cb`, Emscripten **4.0.10** and SDK Node
**22.16.0**. [local-ci.md](local-ci.md) maintains this configuration independently
of retired hosted workflows. The activated SDK Node is passed by absolute path to
CMake; application/native UCI tests retain Node 24. Full mode configures/builds
`wasm-debug` and executes all **11** WASM CTest suites with verbose output.
Any missing dependency, disabled suite, reduced inventory or failed command
prevents acceptance. No tests or timeouts are relaxed. SDK downloads may still
require network access; execution has no dependency on hosted CI.

### M7.2 test runtime and full WebAssembly matrix

Native CTest runs the full `m72_search` matrix by default. WASM CTest selects
`--matrix=ci`: all 13 real-board fixtures at depth two with a zero evaluator,
both previous-PV ordering modes, the actual-history checkpoints at plies 7/8/16,
six deterministic generated trees, all four mate races, explicit root TT policy
isolation, cancellation and evaluator-error tests. Small real/generated fixtures
also check TT off, hint-only and certified numeric modes with fresh-table cold,
warm and both cross-policy directions. Every completed depth still compares its
exact decision, score, canonical PV and termination against the exhaustive legacy
reference. The native full matrix retains all three real-board evaluator values,
30 generated trees and the original ordering/cache/pass Cartesian product.

The original bounded/full matrices perform 242 / 2,208 searches. The additional
fixed M7.4a.1 horizon regressions perform 358 inexpensive searches in either mode,
bringing the earlier totals to 600 bounded / 2,566 full. M7.4a.2 adds 1,461 guarded
leaf regression searches in either mode: the merged M7.4a.2 totals are 2,061
bounded / 4,027 full. The reviewed, uncommitted M7.5b candidate adds 132 searches
in either mode, for 2,193 bounded / 4,159 full; these candidate totals are not yet
on main. Matrix selection preserves assertions and never treats timeout as
success. Both print flushed start/end
checkpoints with monotonic elapsed seconds and search counts at group, fixture,
reference and configuration boundaries. Only successful completion prints an end
checkpoint. CTest's existing M7.2 timeout remains 300 seconds.

With the pinned SDK activated, run the bounded WASM gate and retain its timings:

```sh
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

For the **full** WASM matrix, explicitly invoke the same artifact through the
digest-supplying runner outside the bounded CTest gate (allow longer than five
minutes; it still returns a nonzero exit code on any assertion/error):

```sh
"$EMSDK_NODE" tests/evaluation/run_wasm.cjs build/wasm-debug/m72_search.js --matrix=full
```

`run_wasm.cjs` supplies the artifact SHA-256 first and forwards additional test
arguments unchanged. Native executables accept the same `--matrix=ci` and
`--matrix=full` switches, allowing both schedules to be verified without the SDK.
Omitting the switch selects the full matrix; unknown arguments fail. Local Windows
users with the SDK activated in PowerShell can use `& $env:EMSDK_NODE` for Node
and `"-DCHESS_LAB_NODE_EXECUTABLE=$env:EMSDK_NODE"` for the configure argument.


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


## M5 optional transposition tables

M7.2 changes iterative `MultiPV=1` to PLAY-only root selection with shared bounds.
The appended `IterativeSearchOptions::single_pv_play_only` flag defaults to true;
false selects the historical exact root-ranking/claim semantics for reference
callers. MultiPV greater than one and fixed-depth `search()` are unchanged.
See [M7.2 validation](m7.2-validation.md) for final-score/controller handling,
TT root-policy separation, tests and reproducible Release measurements.

Existing calls remain TT-free. An owning controller can attach bounded storage
across sequential searches/games without changing the evaluator ABI:

```cpp
chess_lab::TranspositionTable table(8 * 1024 * 1024);
chess_lab::TranspositionTableUse use{
    &table, chess_lab::EvaluatorCacheCompatibility::deterministic_position, 1};
auto result = chess_lab::iterative_search(
    *core, *root, history, evaluator, options, stop.token(), {}, use);
if (result.status == chess_lab::IterativeSearchStatus::evaluator_failed ||
    result.status == chess_lab::IterativeSearchStatus::search_error)
  table.clear(); // OWNER action, after invocation returns
```

Certify only deterministic evaluators whose complete inputs/behavior are
identified by the fingerprint and value-policy namespace. Certification is not
inferred from M2 identity. The default attachment is hint-only; stateful/random
experiments should omit the table entirely when preserving evaluation call order
matters. Exhaustive (`alpha_beta_enabled=false`) mode ignores even a populated
table, with no probes, hints or writes.

A table allows one active invocation; clear/resize are idle-only and checked.
The owner must retain it and await search return before resource changes.
Cancellation preserves completed subtree entries; errors require owner clearing
of only the attached table. New evaluator/depth/history/policy contexts are
verified automatically, so correctness does not rely on a manual clear at each
new game or evaluator selection.

`capacity_bytes()` reports actual owned allocation after conservative rounding,
including retained context/index storage. Zero/too-small budgets disable
allocation. Internal cached values/bounds cannot prove a canonical played PV;
search revalidates endpoints or re-searches before publishing any exact line.
The initial implementation prioritizes proof over hit rate and measured slower
than TT off with the cheap material provider, despite useful warm evaluator-call
reductions. See [M5 validation](m5-validation.md) and its raw CSV for exact
commands, byte accounting, all cold/warm/off measurements and pinned CI evidence.


## M7.4a.2 guarded native leaf checkpoint: 2026-10-24 UTC

**Historical pre-commit snapshot:** preserve the implementation, measurements
and acceptance statements below as dated evidence. M7.4a.2 subsequently merged
through PR #24 and passed committed Full CI; the current M7.5b checkpoint below
records that completion and supersedes this snapshot's branch/acceptance status.

ChatGPT reviewed the implementation and TT investigation and decided to retain
the original guarded native leaf preparation unchanged. It remains uncommitted
on `feature/m7.4a2-guarded-leaf`, base
`1d461dfab38e2f105ecb67529192c34b880f3efc`. The two-file source/test patch SHA-256
is `a23441a795db11d9aa19699529f9f6258b4fb5456b318f9ff699d88b35c33e47`.
The isolated in-place signature variation was not adopted.

Only genuine depth-zero search children of the recognized native adapter can
retain a complete raw legal-move set, including exact-PV re-search. Eligibility
needs no effective TT session, no raw EP square, child clock below 99, and
pairwise-distinct exact prospective history. Before history push, the guard checks
`duplicate(H + [r]) = duplicate(H) || contains(H, r)` with cancellation polling.
Active hint-only/numeric TT,
EP-sensitive, claim-sensitive and synthetic cases stay eager, as do roots and
positive-depth ordering. Full legal generation, snapshots, terminal/claim
semantics, evaluation and restoration remain required. External interfaces,
evaluator ABI, TT contracts and deterministic search behavior are unchanged.

Recorded implementation checks passed, all final exits 0: native Release, Debug
and Profile **12/12 each**, complete pinned Emscripten 4.0.10 / SDK Node 22.16.0
WASM **11/11**, **1,703 exact differential records per platform**, **2,000 separate
diagnostic cancellation injections**, and identical canonical completed-depth
results/counter prefixes (225 of each). The **1,461 new regression searches**
cover eligibility/fallback, history/clock/EP boundaries, synthetic encoding
errors, TT modes, exact PVs, cancellation and restoration. These are checked
existing results; no suites were rerun for this documentation handoff.

At initial-position depth six with TT off, copied-source counters measured
**266,606 raw leaf preparations**, **6,703,200 eliminated encodes** and
**266,606 eliminated initial sorts**. Actual legal generations remain 385,132,
visited nodes 381,938 and evaluator calls 328,475. Instrumented timing is excluded
from canonical Release comparisons.

The original 216-invocation interleaved ABBA/BAAB experiment measured paired time
reductions of **29.61% / 41.38% / 40.18%** at TT-off depths **4 / 5 / 6**.
Independent balanced follow-ups retained depth-six TT-off gains of **42.35% and
44.04%**. They used fresh processes, normal evaluator identity/ABI checks and a
common frozen material DLL, preserving single-PV PLAY-only options, complete
history, 8 MiB TT request, equivalent warmup/population and cold clearing outside
timing. Exact result/work checks matched and every outlier was retained.

TT-enabled findings were mixed, including small regressions: the original
experiment observed **0.64% slower depth-five cold** and **2.77% slower depth-six
warm**, and follow-up directions varied. **TT-enabled neutrality has not been
demonstrated, and the root cause of observed regressions remains unresolved.**
Confirmed eager storage/temporary-copy changes are not proven timing causes.
The in-place experiment lacked a consistent paired benefit and is excluded.
These are single-machine Windows/MSVC initial-position findings, not universal
engine speedups; thermal/core residency and desktop scheduling were not isolated.

Valid long-history comparisons at depths 1/2 improved on the 49-entry distinct
fixture (paired ratios 0.60971 / 0.67969) and were near neutral with mixed signs
on the 113-entry repeated fixture (0.99465 / 1.00150). The guard replaces
pre-existing pairwise claim scanning on eligible leaves; repeated histories
reject early and retain eager claim lookahead. No pathological incremental cost
or evidence supporting a fallback threshold was found, so none was introduced.
Longer distinct histories remain unmeasured; the scan has quadratic worst-case
cost and these short timings do not establish a universal bound.

See [CURRENT_STATUS.md](CURRENT_STATUS.md#m74a2-guarded-native-leaf-preparation-2026-10-24-utc)
for the detailed checkpoint. Ignored local-only evidence is in
`work/m7.4a2/implementation-20261024T050939Z/` and
`work/m7.4a2/tt-investigation-20261024T055228Z/`: reports, raw timing CSVs, artifact
hashes, differential/counter records and the retained original patch. Preserve
the latter's 3,743-file historical verification, all frozen binaries and all
M7.3/M7.4/M7.4a.1/M7.4a.2 evidence. Use new ignored output directories for any
separately authorized rerun. These results are not authoritative Full CI; that
requires a separately authorized commit and clean, aligned Windows/Linux
validation through [local-ci.md](local-ci.md).

## M7.5a / M7.5b reviewed checkpoint: 2026-10-24 UTC

M7.4a.2 merged on 2026-10-24 through
[PR #24](https://github.com/yctekmen-sudo/chess-engine-lab/pull/24), main merge
`f3aaee0a284b206a347786a8e5418c3773654a40`. Its approved feature commit
`8b68e241717a8e205d86904fc2d26f9077dca76f` passed authoritative **54/54 Full
local CI gates**, clean and aligned, all worker/coordinator exits 0. Original
local-only acceptance evidence: `work/ci/20261024T185729687Z-1e5cefff/`.
Its historical TT-performance uncertainty remains unresolved.

M7.5a profiling is complete, reviewed and accepted. The separate merged-baseline
session measured initial-position native Release depth-six TT-off **348,774 NPS**.
Sampled CPU stacks showed **60.86% exclusive move/list ownership**, **41.76%
inclusive heap-path stack union** (overlapping the owner categories, not additive)
and **2.20% evaluator**. Provenance is
`work/m7.5a/investigation-20261024T202503Z/`: `report.md`,
`throughput.csv` and `cpu-breakdown.json`, with executable/toolchain identities.
These are workload-specific measurements, not general cost or savings bounds.

The reviewed M7.5b candidate uses an **adapter-private complete native legal-count
path** only for the previously guarded genuine depth-zero leaves. The recognized
native adapter performs the same full legal generation without creating unused
native move wrappers. The exact prospective-history guard, cancellation,
restoration, terminal/claim handling and eager fallbacks for effective TT,
EP-sensitive, claim-sensitive and synthetic-adapter cases remain unchanged.
There are no public ChessCore, evaluator ABI, controller, GUI or dependency
changes. Frozen depth-six initial-position TT-off diagnostics measured exactly
**6,703,200 avoided wrappers**; actual legal-generation calls **385,132**, nodes
**381,938**, evaluations **328,475**, decisions, scores, complete PVs, claims and
deterministic statistics remain identical. Active TT sessions never use the
count path.

Recorded correctness: **132 additional searches**, **43-position count corpus**,
native **Release/Debug/Profile 12/12 each**, complete pinned Emscripten 4.0.10 /
SDK Node 22.16.0 **WASM 11/11**; **1,855 direct structured records**, **225 exact
completed-depth comparisons** with deterministic counter prefixes, and **2,000
diagnostic cancellation injections**. Native/WASM comparison only normalizes
lossless hexadecimal-float spelling, without numerical tolerance. These checks
are existing implementation evidence, not suites rerun for documentation.

The controlled **408-invocation** crossover used six balanced ABBA/BAAB blocks
per configuration and a common frozen evaluator DLL through normal ABI/identity
checks. Paired median TT-off time reductions were **22.36% / 29.75% / 31.71%**
at initial-position depths **4 / 5 / 6**, and **39.43% tactical / 42.84%
middlegame** at depth four; each favored the candidate in all six blocks.
Descriptive crossover depth-six TT-off throughput was approximately **320K
baseline / 467K candidate NPS**, distinct from the separate M7.5a session.
**TT-enabled neutrality is unproven:** depth-six TT-warm was **0.89% slower**,
four of six blocks favoring the baseline. The cause remains unresolved.
All raw observations, including outliers, are retained. These single-machine
Windows/MSVC workload findings are not universal speedup or playing-strength
claims; thermal/frequency/core-affinity and desktop activity were not isolated.

See [CURRENT_STATUS.md](CURRENT_STATUS.md#m75b-approved-native-leaf-count-handoff-2026-10-24-utc)
and the original ignored `work/m7.5b/implementation-20261024T210345Z/`
(`report.md`, `final-audit.json`, differential/diagnostic/cancellation records,
`ab/timings.csv`, block summaries and artifact hashes). These local-only files
are not published through GitHub. Preserve the **5,555 historical files**, **213
frozen local artifacts**, frozen binaries and original three-file review patch.

**Acceptance at that historical checkpoint:** ChatGPT approved the original M7.5b implementation/tests
and controlled evidence. On `feature/m7.5b-native-leaf-count` at `f3aaee0`,
the three implementation/test files plus these two documentation files remain
uncommitted, index empty. Linux remains clean on main at the merged baseline.
There is **no authoritative 54-gate Full CI for M7.5b yet**. Commit and publication
need separate authorization; clean aligned Full validation follows the
[local CI guide](local-ci.md). Documentation maintenance does not authorize
benchmark reruns or changes to frozen evidence.

## M7.5c accepted implementation checkpoint: 2026-10-25 (America/New_York)

Base is the local M7.5b PR #25 merge
`5f3654c80cdc7ad3f7c01a9c0f70877a1bd531e2`. ChatGPT accepted **Choice 1** after
reviewing M7.5c.2 and M7.5c.3: **retain the original compact native TT-leaf batch
unchanged, with the measured regressions explicitly retained as known tradeoffs**.
No performance workaround, selective fallback or diagnostic variant is adopted.

### Scope and invariants

The adapter-private batch owns compact complete native move identities and a
position-lifetime/frame certificate. Only genuine depth-zero search children
with an **effective hint-only or certified numeric TT** select it. Complete legal
generation, UCI encoding, canonical sorting, capture/pawn/legal-EP metadata and
legal TT-hint validation remain intact. Ordinary leaf preparation omits individual
polymorphic move wrappers; exceptional intended-claim lookahead materializes a
certified ordinary move and uses the existing make/undo restoration path.

Public ChessCore/evaluator interfaces and ABI, move validation, lifetime/frame
checks, cancellation, error propagation, restoration, terminal/draw semantics,
exact decisions/scores/PVs and deterministic counters are preserved. Roots,
positive-depth nodes, TT-off M7.5b count leaves, synthetic adapters and speculative
claim children keep their established paths. No TT bookkeeping, evaluator,
controller, GUI or dependency changes are included.

### Existing correctness and allocation evidence

Focused implementation checks passed native **Release/Debug/Profile 12/12 each**
and complete pinned **Emscripten 4.0.10 / SDK Node 22.16.0 WASM 11/11**. Evidence
includes **162 new regression searches**, **2,053 exact structured differential
records per platform**, **225 exact completed-depth results and deterministic work
prefixes**, **2,000 cancellation injections**, and allocation, evaluator,
materialization and undo-failure/restoration checks. Interrupted/failed searches
retain the existing score-validity and restoration contracts. These are existing
implementation results, not reruns for this documentation task.

Initial-position depth-six numeric-TT diagnostics avoided **7,557,314 wrappers
cold / 7,557,332 warm**. Both modes create **302,740 batch owners**, with **45
exceptional leaf encounters / 1,052 materializations**. Full legal generations
**356,926**, visited nodes **355,879** and evaluations **301,184**, along with
complete results and deterministic statistics, remain unchanged. These are avoided
individual wrappers, not total allocations eliminated; batch ownership, temporary
vectors, snapshots and exceptional materializations still have costs.

### Controlled performance: keep TT policies separate

The original **1,008 fresh-process invocation** experiment used an **8 MiB
certified-numeric TT request**, 21 configurations, two independent serial sessions
and six balanced ABBA/BAAB blocks per configuration/session. Depth-six cold
paired reductions were **18.00% / 17.87%**, warm **19.15% / 18.32%**. Its separate
repetition-cold regression was **1.44% / 2.07%**, and middlegame TT-off control
regression **4.24% / 2.49%**. All raw observations and outliers remain retained.

The real native UCI defaults are **Hash=16 MiB requested, NumericTT=false** and
value policy 1. This is an attached effective **hint-only** table, not TT disabled.
The separate **336-invocation default-policy** study used the iterative API with
`TranspositionTableUse{&table, EvaluatorCacheCompatibility::not_certified, 1}`.
Actual allocated capacity was verified as **9,044,156 bytes**, with positive hint,
probe and store activity and zero numeric exact/bound reuse. It comprised seven
configurations, two independently launched serial sessions and six balanced blocks
each. The same frozen material DLL, options and original complete replayed or
explicitly incomplete imported-FEN histories were preserved.

| Default-policy workload | Session 1 paired time reduction | Session 2 paired time reduction |
| --- | --- | --- |
| Initial depth-six hint-only cold | 18.27% | 18.06% |
| Initial depth-six hint-only warm | 17.22% | 17.11% |
| Tactical depth-four hint-only cold | 19.97% | 20.06% |
| Tactical depth-four hint-only warm | 20.57% | 19.98% |
| Repetition depth-four hint-only cold | -2.73% (slower) | -2.62% (slower) |
| Repetition depth-four hint-only warm | -1.26% (slower) | -2.52% (slower) |
| Middlegame depth-four TT off | -2.57% (slower) | -1.65% (slower) |

Every measured invocation preserved exact completed-depth scores, decisions, full
PVs and deterministic counters. **Do not pool numeric and hint-only observations**
or infer equivalent policy performance. These are single-machine Windows/MSVC,
workload-specific findings, not universal engine-speed or playing-strength claims.
Thermal/frequency/core-affinity and exclusive desktop activity were not controlled.

### M7.5c.3 attribution and final decision

The follow-up ran **480 controlled benchmark invocations**, **32 separate diagnostic
invocations** and **2,112 exact completed-depth records**, with all observations
retained. Copied-source controls tested equivalent rebuilds, an inert owner field
and constant-disabled batch dispatch; none became production code.

No increased TT-off heap traffic was demonstrated: both variants made **350,114
intercepted host allocations / 47,396,724 requested bytes** in middlegame, with
the same generations, encodings, sorts and search work. `PreparedNode` grew
**120 to 128 bytes**, while `OrderedMove` stayed 48. The inert-field control was
**6.59% / 6.47% slower**, but equivalent rebuild/relink controls also changed
timings despite identical generated raw function bodies and different linked
placements. These facts do not isolate size, initialization, destruction,
mode/accessor checks, inlining or placement as a single runtime cause.

Hint-only repetition cold creates **3,102 batches / 74,656 exceptional move
materializations**; warm **2,816 / 67,363**, avoiding only **20 wrappers** each.
Net host allocation counts nevertheless decrease because eager wrapper-vector
growth is removed. Disabling batches did not consistently improve repetition;
globally disabling them made the initial depth-six benefit control **23.60% /
23.01% slower**. Intrusive function/allocation timer sums overlap and were kept
separate from canonical timing. No hardware sampling profile or specific cache,
thermal or frequency mechanism is claimed.

**The precise regression mechanism remains unresolved.** Choice 1 accepts the
original M7.5c.2 unchanged with explicit adverse workloads. No fix, proven causal
attribution or safe benefit-preserving narrow refinement has been established.
Any future refinement requires a new scoped authorization, focused correctness
and cancellation/restoration validation, native/WASM checks and controlled A/B
evidence; it is not part of this milestone handoff.

### Local-only provenance and pending acceptance gates

Preserve these ignored directories; their raw artifacts are not published through
GitHub or included in a fresh clone:

- `work/m7.5c/investigation-20261025T005211Z/`: approved design/plans and baseline census.
- `work/m7.5c/implementation-20261025T012607Z/`: focused correctness, differential/cancellation logs, allocation census and original numeric `ab/` experiment.
- `work/m7.5c/default-tt-verification-20261025T021117Z/`: default-policy raw timing, exact TT counters, reports and artifact identities.
- `work/m7.5c/regression-attribution-20261025T050554Z/`: compiler/disassembly/layout findings, counts/costs, isolated source diffs, raw timings, hashes and decision analysis.

Preserve all **13,311 previously verified frozen files**, the attribution archive
and all Linux historical evidence. Windows remains on
`feature/m7.5c2-native-tt-leaf-batch` at the full baseline above; Linux stays clean
on `main` at that same SHA. The prospective six-file change is **uncommitted**,
including the still-untracked private header, with an empty index. Local review
acceptance is complete, but **no authoritative 54-gate Full CI for M7.5c has run**
and these changes are unpublished. Commit, clean aligned Full validation and
publication require the applicable separate authorization. This documentation
task performs no builds, tests, benchmarks, GitHub refresh or publication.

## M6 controller and M7 desktop GUI

Node 24 is required for controller/UI development, separate from native core
builds. Follow [GUI build/launch/test instructions](../gui/README.md). The
controller package builds independently, and importing its public library does
not load the GUI/server. `chess-lab gui DIRECTORY` lazily starts/reconnects the
loopback server after both packages are built; other CLI commands retain their
M6 behavior.

Local CI preserves all native Debug/Release/Profile, Node controller and pinned
Emscripten gates. The WASM suite now also executes the legacy72/extended80 C and
C++ calibration checks. The local runner uses Linux Node 24.10.0 and installed
Windows Node 24, locked Vite/React checks and Vitest/RTL on both platforms,
Linux Playwright Chromium against the real server/native engine, and Windows
desktop smoke. Historical results and D118–D138 traceability remain in
[M7 validation](m7-validation.md); current local evidence is in
[CURRENT_STATUS.md](CURRENT_STATUS.md).
