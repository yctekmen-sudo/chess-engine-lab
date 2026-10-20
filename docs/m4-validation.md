# M4 implementation and validation

## Base and boundary

Branch `m4/iterative-search` starts at approved main
`daa499bd9b8fac5f77fc626362d959b9dd0126d6`. Both M4 documents on that
commit explicitly authorize implementation after documentation PR #7.
The local checkout initially pointed at M3. The environment's HTTP proxy was
unreachable for `git fetch`/push, so the connected GitHub API supplied latest
main's commit/tree metadata. Its existing local tree/parent/blob objects and the
imported commit were verified by exact Git object SHA before switching branches.
Publication uses the same GitHub connection; remote tree SHAs match local
`git write-tree`. No implementation is committed to main.

This implements Decisions 50–66 in the search library, tests and headless
benchmark. It does not implement controllers, UCI, UI, books, TT, selective
depth, quiescence or pondering. Future work remains [#5](https://github.com/yctekmen-sudo/chess-engine-lab/issues/5)
and [#6](https://github.com/yctekmen-sudo/chess-engine-lab/issues/6).
Stop at the implementation PR for architecture/code review; do not merge.

## Concrete public API

All additions are in `engine/include/chess_lab/search.hpp`, C++20 only:

- `CancellationSource` owns a shared atomic Boolean. `token()` returns a
  value-owned `CancellationToken`; `request()` is thread-safe and one-way.
  Copies refer to the same signal; tokens outlive the original source safely.
  There is no reset. Use a fresh source for a new invocation. The default token
  never requests cancellation. Relaxed atomic accesses communicate only the
  cancellation flag, not other controller state.
- `IterativeSearchOptions` contains optional `max_depth` (1..64 when present),
  `alpha_beta_enabled`, `multipv_count` (1..256, default 1), and
  `previous_pv_ordering_enabled` (default true). Invalid parameters return
  `search_error` with explicit diagnostics. A missing maximum runs uniform
  depths through the safety cap, subject to cancellation.
- `SearchVariation` owns its `ChessDecision`, exact `SearchScore`, legal
  `principal_variation`, `PVEnd`, and unplayed `pv_intended_move`.
- `CompletedDepth` owns `depth`, `best`, exact ranked `multipv`, cumulative
  statistics and `SearchProvenance`. Its best entry equals rank one.
- `SearchProvenance` records actual `EvaluationIdentity`, effective options,
  full root `ChessEvalPosition`, exact ordered history signatures and the
  history-incomplete flag. These identify supplied evidence without pretending
  that a 64-bit board key is sufficient. No controller session IDs are invented.
- `SearchProgress = std::function<void(CompletedDepth)>` delivers a detached
  value after each complete committed depth, synchronously on the calling
  thread. It may retain or change its own copy without changing search or
  earlier/later snapshots. There are no partial candidate or bound events.
- `IterativeSearchResult` has an explicit `IterativeSearchStatus`, optional
  `completed`, cumulative statistics, provenance and diagnostic. Cancellation
  before depth one has only `unevaluated_fallback`: exactly one canonical legal
  PLAY, no score, PV, endpoint or ranking. A terminal root instead has
  `terminal_decision` (including actual outcome) and `terminal_score`, no
  completed depth/ranking/fallback. Errors clear every final playable field.
  Thus depth zero and unevaluated status are unambiguous without fake values.

`iterative_search(core, root, history, evaluator, options, token, callback)` is
synchronous and single-threaded. The original `search(...)`, its enums, depth
meaning, score layout and result fields remain source-compatible. M2 C ABI,
provider selection, artifact hashing, quarantine and all M1 public contracts
are unchanged. Disservin remains private to ChessCore.

## Shared traversal, exactness and cancellation

M3 and M4 use one `Traversal::node`, terminal adjudication, draw-claim probing,
root-relative mate score/bound coordinates and exception-safe `child`/`with_move`
restoration. Both clone caller state and copy actual history. Claims still
compare with evaluation at the horizon; mate takes precedence over clock draws.
Intended claim probing uses canonical legal moves, deduplicates by choosing the
earliest qualifying move, and never appends that unplayed move to the PV.

Every iteration starts fresh. All root PLAY candidates are independently
searched with unbounded windows at ply one, even when N=1. An eligible root
claim supplies one additional decision, not multiple equivalent rows. Sorting
uses exact White-max/Black-min score order, then immediate claim, intended claim,
then lexicographic played move. Only after evaluating all distinct decisions
is the exact sorted ranking truncated to N. This deliberately costs more than
M3's root-window pruning, but no root bound can be published as an exact score.

Previous ranked root plays are visited first; internal previous-PV hints apply
only while the actual move path matches the previous best PV prefix. Remaining
moves retain canonical order. No prior scores are reused. Within a subtree
that can receive such a hint, any improving or canonically earlier equal
candidate from a bounded traversal is re-searched full-window with exact-PV
refinement before replacement. In particular an equality bound cannot masquerade
as an exact tie. A noncompetitive bound cannot displace the exact incumbent.
Outside that prefix, traversal has M3's canonical order and needs no extra
PV refinement. Claims retain their tie priority independently of visitation.
The full per-candidate PV, not just the best score, is checked against the oracle.

Cancellation is a dedicated internal abort, not a score or evaluator error.
Polls occur on recursive entry, immediately before evaluator dispatch, throughout
move processing/repetition scans/claim probing, between depths and before
publication. Library calls and a blocking evaluator cannot be interrupted;
there is no promised hard latency. Initial root validation, terminal detection
and fallback enumeration finish before attaching the observer so a precancelled
terminal still reports its real outcome. An error establishing fallback is an
error, never an invented move.

The final successful precommit poll is the commit linearization point. Moving
the fully prepared detached snapshot into committed storage follows immediately;
requests observed before that point discard the iteration. Later requests retain
it. Callback forwarding occurs after commit, and callback cancellation returns
CANCELLED with that committed depth, even at the requested maximum. A callback
exception, including one typed EvaluationError, becomes SEARCH_ERROR without
quarantining a healthy evaluator. Provider EvaluationError retains M2 quarantine
and invalidates the entire final result, including any earlier provisional
progress. Undo/traversal/unknown failures are distinct search errors. Cleanup
failures are not hidden behind a cancellation or evaluator error.

All progress is provisional until final status. Controller code must invalidate
previous UI progress on final evaluator/search error, serialize active evaluator
use/replacement, own session IDs/stale suppression, clocks and worker/UI
forwarding, and avoid reentrant search on active resources. The API has no time
expiry reason. Hypothetical analysis uses a cloned legal descendant plus its
matching `GameHistory::after_move`; real side-to-move is never overridden.

## Regression evidence

`tests/search/m4_search.cpp` is enrolled in native and pinned WASM CTest.
The existing M0/M1/M2/M3 suites and sources are unchanged. Test-only adapter and
exhaustive oracle fixtures were copied into `m4_fixtures.hpp` so M3 stays intact;
production minimax is not duplicated.

- Eighty seeded ternary generated trees, depths 1..3, both root sides, N=1/2/3/8,
  ordering on/off and alpha-beta on/off. Every committed ranking entry and
  best action/score/full PV/end/intended metadata matches the independent
  exhaustive tree oracle. Each completed depth also matches a separately
  invoked M3 fixed-depth search. Old snapshots are rechecked after later depths.
  Values include signed-zero ties, ±1e300, draws/claims/dead nodes and mates.
- Explicit previous root and internal noncanonical PV hints. A canonical earlier
  alternative returns an equality bound with either a genuine equal exact value
  or a hidden worse value. Both cases must preserve all ranked canonical PVs.
  Mirrored mate trees separately require faster victory and slower defeat for
  White and Black and compare every ranked candidate at every depth.
- An independent clone-per-edge genuine-chess oracle enumerates all decisions
  with exact supplied repetition history. It checks rook clock-99 intended
  claims (multiple eligible intended moves collapse to one), clock-100 immediate
  claims, both sides, extreme finite values, mate over clock 149, and legal and
  pinned en passant. N=256 checks all decisions; N=1 ordering-off compares every
  depth against M3. Every published PV replays through ChessCore.
- Actual knight-cycle history checks prospective third repetition, immediate
  third, and automatic fifth. Full and incomplete histories, mismatches,
  root mates at clock 150, stalemate, known-dead and 75-move roots are tested.
  Terminal precedence is checked even with pre-requested cancellation.
- Deterministic cancellation before depth one, during claim probes, during
  recursive evaluation, after an earlier committed depth, from the callback,
  and on the final root undo immediately before publication. Assertions cover
  single unevaluated fallback, no partial ranking, last committed depth,
  cumulative abandoned work, exact history and LIFO restoration.
- Native tests additionally signal from a real second thread during an
  evaluator call with a bounded five-second handshake, and verify callback
  thread affinity. Default/fresh tokens and tokens whose source has died
  exercise lifetime and invocation isolation.
- Late provider failure after depth-one progress clears final action/ranking,
  quarantines, restores frames and permits no further ABI calls. Callback
  runtime/EvaluationError/unknown exceptions, late ChessCore traversal error,
  unknown core exception and independent undo failure separately check status
  classification and caller isolation. Invalid depth/N/history diagnostics,
  evaluator identity and hypothetical Black-root provenance are asserted.
- The absent-maximum cap and explicit maximum 64 use a cheap generated linear
  66-node adapter. No real-chess search is driven to depth 64.

## Commands and local environment

Linux x86_64 managed VM, AMD EPYC 9V74, GCC/G++ 14.2.0, CMake 3.31.6,
Ninja 1.11.1. Three visible CPUs, cgroup quota two CPUs (`200000 100000`).
Tests/builds may share the VM; suite times are not performance measurements.

```sh
export PATH=/workspace/.cloud-toolchain/bin:$PATH
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
cmake --preset native-profile
cmake --build --preset native-profile
ctest --preset native-profile --output-on-failure
```

Local pinned WASM installation was attempted with
`/workspace/emsdk-m0/emsdk install 4.0.10`. Downloading its Node 22.16.0 archive
from `storage.googleapis.com` failed with `Operation not permitted` under the
managed network policy, so there is no local WASM pass. The unchanged pinned
GitHub-hosted workflow installs emsdk commit
`62a853cd3b3134398ce85cde8bb5cbb2ef0194cb` / Emscripten 4.0.10 and SDK Node:

```sh
source "$RUNNER_TEMP/emsdk/emsdk_env.sh"
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

M4 uses the existing M2 Node launcher, which hashes the actual encompassing
WASM artifact before initializing the controlled evaluator identity. The native
fixture's dummy digest denotes a statically linked controlled test provider,
not a production artifact. Native-only threads are excluded from WASM; the
same deterministic mid-iteration cancellation and unwind cases run under Node.
This does not prove that a browser Worker can handle ordinary postMessage while
synchronous C++ is busy. Browser signaling/shared-memory deployment, worker
termination, mobile, Windows and macOS remain unvalidated controller/platform
work. There are no claims of browser responsiveness or hardware counters.

## Headless benchmark protocol

`m4_benchmark` is available in native builds including BUILD_TESTING=OFF,
using the existing native-profile frame-pointer configuration. It records
original/root FEN, supplied replay and history completeness/size, actual module
and artifact identity/configuration, max depth, N, alpha-beta, ordering,
warmup/repetitions, per-depth and cumulative nodes/evaluations/cutoffs/elapsed
work, and a deterministic checksum over all committed best/ranked scores,
decisions, full PVs and endpoint/intended metadata (including termination).
Finite score serialization uses hexfloat, not lossy decimal rounding. It verifies
ordering-off/on checksums agree. Loading/hashing, replay and printing are outside
search timing. The progress callback only retains detached snapshots, with that
copying cost included in invocation time. No per-node tracing is introduced.
A separate M3 depth-N reference reports its work and decision; iterative totals
are not presented as a single fixed-depth search cost.

```sh
# MODULE, depth, measured repetitions, warmups, optional FEN, replay, N, AB
build/native-release/m4_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1
build/native-release/m4_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1 \
  '4k3/8/8/8/8/8/P7/4K3 b - - 0 1' '' 3 1
build/native-profile/m4_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 3 1
```

Compare ordering only within the same invocation/build/evaluator identity and
options. Search uniformity and all exact choices must remain equal; N requires
extra exact candidates even if it does not change visited counts here (this
initial implementation resolves every root candidate for any N). Shared-VM
measurements without affinity are observational, not universal speedups.

External sampling is reproducible on a host with perf installed/authorized:

```sh
mkdir -p work
perf record -o work/m4-perf.data -g --call-graph dwarf -- \
  build/native-profile/m4_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3
perf report -i work/m4-perf.data --inline
perf stat -e cycles,instructions,cache-misses,branches,branch-misses -- \
  build/native-profile/m4_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3
```

Perf is not installed locally; no sampling/PMU or allocation evidence is claimed
or required in CI. Profile retains optimized code, debug information and frame
pointers; Release is measured separately. No instrumentation is added to normal
per-node execution.

## Observed local results and measurements

Code/test oracle commit: `131ad356e89dfb7727c9fb4693c8eef6c9625591`.
The subsequent WASM-only link configuration is
`31ecae3b7f08da5701991eac10104a9090e56792`; native production/test code is identical.
All seven CTest suites passed locally in Debug (41.69 s, M4 13.27 s), Release
(1.74 s, M4 0.81 s), and Profile (1.94 s, M4 0.86 s). The final fixture change
adds a true-tie versus misleading-bound variant; production source is identical
to implementation commit `48a063cfb863d697224ac576deb0141b2a472f20`.
`git diff --check` is clean. No sanitizer or unexecuted platform result is implied.

Benchmarks ran serially after all local builds/tests completed, without perf,
affinity, allocator instrumentation or other local concurrent workloads. Each
nonterminal row has depth 3, N=3, alpha-beta on, one warmup and three measured
invocations. All histories contain only the supplied root (incomplete prefix),
with no replay. The default material configuration is
`{"bishop_value":3.25,"knight_value":3,"pawn_value":1,"queen_value":9,"rook_value":5}`.
Release artifact SHA-256:
`c22b71130bff9671bec260fb6312707597ccedde68858e3b043f02297f7a3368`;
fingerprint `3eb703deada5c36e85443bee37435625eceadf2b9fa2182a553b6ab36803ceb0`.
Profile artifact SHA-256:
`c03a4b5f32ac53b45c193ef7367507a6d9330009dd0a57cc8c9f07c270bd60d4`;
fingerprint `5fc336739aa4069fecd996020484ee6d3c2c2eda527ce1b4c08b77277b757acd`.
The differing artifacts/build identities are never mixed into one comparison.

| Build/workload | Ordering | Cumulative nodes / evals / cutoffs | Invocation times (ms) | All-depth exact-line checksum |
| --- | --- | --- | --- | --- |
| Release initial | off | 1708 / 1265 / 379 | 68.701433, 67.796151, 71.455614 | 4054485809429033441 |
| Release initial | on | 1708 / 1265 / 379 | 71.675674, 70.960739, 68.488192 | 4054485809429033441 |
| Release Black pawn | off | 152 / 104 / 30 | 0.411449, 0.440963, 0.469866 | 10368194649929238215 |
| Release Black pawn | on | 152 / 104 / 30 | 0.494794, 0.491359, 0.441844 | 10368194649929238215 |
| Release queen tactic | off | 1196 / 848 / 290 | 10.701310, 10.330070, 10.549660 | 2079499143374710101 |
| Release queen tactic | on | 1196 / 848 / 290 | 12.267446, 12.457734, 12.513258 | 2079499143374710101 |
| Profile initial | off | 1708 / 1265 / 379 | 68.416565, 67.571453, 68.119538 | 4054485809429033441 |
| Profile initial | on | 1708 / 1265 / 379 | 71.665199, 71.520992, 69.430389 | 4054485809429033441 |

The initial and pawn FENs/commands are above. Queen tactic command:

```sh
build/native-release/m4_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1 \
  '4k3/8/8/8/3q4/8/4P3/3QK3 w - - 0 1' '' 3 1
```

Per-depth work is identical between ordering modes in these measured fixtures:

| Workload | Depth | Iteration nodes / evaluations | Cumulative nodes / evaluations / cutoffs |
| --- | ---: | --- | --- |
| Initial | 1 | 21 / 20 | 21 / 20 / 0 |
| Initial | 2 | 421 / 400 | 442 / 420 / 0 |
| Initial | 3 | 1266 / 845 | 1708 / 1265 / 379 |
| Black pawn | 1 | 6 / 5 | 6 / 5 / 0 |
| Black pawn | 2 | 41 / 35 | 47 / 40 / 0 |
| Black pawn | 3 | 105 / 64 | 152 / 104 / 30 |
| Queen tactic | 1 | 13 / 12 | 13 / 12 / 0 |
| Queen tactic | 2 | 333 / 319 | 346 / 331 / 0 |
| Queen tactic | 3 | 850 / 517 | 1196 / 848 / 290 |

Ordering provided **no work reduction or measured speedup** in these workloads;
median elapsed time increased. They are shallow, largely tie-heavy material
searches; root full-window independence makes root ordering ineffective for
root pruning, while hint/path/PV bookkeeping has a cost. Generated regression
trees verify behavior when visitation and canonical selection actually differ.
These measurements justify correctness and expose the cost; they do not imply
that another position/evaluator will have the same ordering impact.

Single separate M3 reference invocations at depth 3 (same Release provider):
initial 546 nodes / 485 evals / 38 cutoffs, 23.143800 ms; pawn 57 / 40 / 10,
0.151648 ms; queen tactic 297 / 207 / 70, 2.339616 ms. Selected decisions
agree with M4: `a2a3`, `e8d7`, `d1d4`, respectively. These individual reference
times are not three-run baseline estimates. M4 intentionally repeats depths
and resolves every root candidate exactly, explaining its larger aggregate work.

Terminal smoke uses
`'7k/6Q1/5K2/8/8/8/8/8 b - - 150 1'`, depth=3, N=3, AB=1, no warmup,
one measured run. Both ordering modes return TERMINAL, WhiteMate(0), depth zero,
one visited root, zero evaluator calls, no ranking/fallback, checksum
`7612728725252968601`. Times 0.004707 / 0.001472 ms are only smoke observations,
not a comparison at this scale; checkmate overrides clock 150.


## WASM stack budget and CI evidence

The first pinned WASM run on oracle commit `131ad356e89dfb7727c9fb4693c8eef6c9625591`
[failed](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39481555037)
with an out-of-bounds access during deep recursive evaluation in the cheap
64-ply cap fixture. Earlier M4 oracle/rules/cancellation/error cases and all
M0–M3 suites passed. This exposed the inadequate default 64 KiB Emscripten
stack for Debug recursive search. The test and cap remain intact.

`ChessEngineLab::Search` now exports `-sSTACK_SIZE=2097152` to WASM consumers
and `-sSTACK_OVERFLOW_CHECK=2` in Debug for clear diagnostics. This is a runtime
resource requirement, not an evaluator ABI or search-depth change. The normal
native link/compile flags are unaffected. Applications that do not link via the
CMake target must provide an equivalent stack budget. Larger application call
stacks or evaluator requirements need their own resource validation.

Validation code/build commit:
`31ecae3b7f08da5701991eac10104a9090e56792`.
[Native run 39482126541](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39482126541)
passed all three Ubuntu 24.04/GCC 13.3.0 jobs, seven suites each:
Debug 42.43 s (M4 13.72 s), Release 1.78 s (M4 0.70 s), Profile 2.56 s
(M4 1.00 s). These are CI correctness times, not benchmark comparisons.

[Pinned WASM run 39482126385](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39482126385)
passed all seven suites on the same code/build commit, Ubuntu 24.04,
Emscripten 4.0.10 and SDK Node 22.16.0. Total CTest time 142.57 s:
M0 0.06 s, M1 48.81 s, M2 0.62 s, C layout 0.03 s, C++ layout 0.04 s,
M3 47.45 s, M4 45.45 s. Logs confirm all M4 generated/oracle, real-chess,
cancellation/error and full cap assertions completed. M4's actual encompassing
WASM SHA-256 was
`d26cbc905bf304f0b5b7a0973529e069ae689c96b5cac5aaa43f21333c26896e`;
M3's was `f5d4210f9bb3b25d2b3bd640a37718eb3a6cdc74e6b6b898d1dca3f775416f31`.
This validates the explicit stack budget and supported Node exception/atomic
paths; it does not establish browser Worker signaling or other platforms.
The final README/developer/report follow-up changes documentation only; code,
tests and build configuration are those of the passing commit above.
