# M5 transposition table: implementation and validation

## PR #10 cancellation review follow-up

Traversal move enumeration, traversal polling and TT intern/probe polling now
call the same private cancellation helper, and the invocation explicitly catches
`detail::SearchCancelled`. The reviewed head already aliased its local name to
that shared type; this update removes the alias and duplicated throwing sites
so the common exception boundary is visible directly.

`m5_search` now deterministically requests cancellation **inside** exact TT
candidate verification, after at least 32 history nodes have been visited in a
257-occurrence context. Both before-first-commit and after-depth-1 cases assert
`cancelled`, canonical fallback or unchanged committed MultiPV, balanced LIFO
restoration, unchanged caller root/history, usable evaluator, retained completed
cache entries, and no controller error-clear. A subsequent uncancelled search
matches the exhaustive result, also checking guard/prefix cleanup and cache reuse.
Temporarily replacing the shared traversal exception alias with an unrelated
type made this regression fail with the expected normal-cancellation assertion;
the mutation was removed before validation and publication.

The deterministic fixture uses an idle-configured, per-table private verification
observer. It requests the real cancellation source inside the history loop;
the next production cancellation poll throws the exception. It does not throw
from an evaluator or progress callback, use sleeps, depend on thread scheduling,
or alter the public API. Its one pointer is included in `sizeof(Impl)` and the
existing strict capacity accounting. Earlier benchmark/CI numbers below describe
the original implementation commit, before this observer was added; fresh review
validation and CI evidence are recorded on PR #10.

Fresh local full-suite runs after this change: native Debug **8/8, 53.73 s**,
Release **8/8, 4.20 s**, Profile **8/8, 3.58 s**. Commands were
`cmake --build --preset native-{debug,release,profile}` followed by
`ctest --preset native-{debug,release,profile} --output-on-failure` (each preset
separately). The existing ASan/UBSan build was rebuilt for `m5_search` and passed
with `ASAN_OPTIONS=detect_leaks=0`. These elapsed times include concurrent local
validation activity and are correctness evidence, not benchmark comparisons.

## Base, scope and public contract

`m5/transposition-table` starts at approved main
`f643bf116d4c2d997173dd46efbee9f3d379d0e7` (merged documentation PR #9).
Both approved M5 documents were read on main before implementation. The shell
proxy was unreachable for Git fetch/push; the connected GitHub API supplied
main's missing Git objects. Imported commits (including signatures), blobs and
trees were verified against their exact Git SHA before branching. Published
GitHub tree SHAs also match local `git write-tree`.

The pre-change working handoff is recorded in
[m5-design-checkpoint.md](m5-design-checkpoint.md), including the later
allocation-free verification refinement. Implementation code/tests/build commit:
`d88f60fa7c0780153cd1906270ba60773151cf24`.

M5 adds optional C++ storage and test/benchmark harnesses only. M1–M4 public
call sites/defaults, SearchScore's 24-byte/8-byte layout, evaluator ABI/version,
quarantine, rule precedence, nominal depth and committed-result semantics stay
intact. Fixed-depth `search()` remains TT-free. No controller application, UI,
UCI/book, selective search, TT persistence, parallel search or pondering is
implemented. Deferred work: [#5](https://github.com/yctekmen-sudo/chess-engine-lab/issues/5)
and [#6](https://github.com/yctekmen-sudo/chess-engine-lab/issues/6).

`engine/include/chess_lab/transposition_table.hpp` exposes:

- Noncopyable/nonmovable `TranspositionTable(maximum_bytes=0)`, idle-only
  `clear()`/`resize(bytes)`, actual `capacity_bytes()` and retained `entry_count()`.
  A controller owns its lifetime across sequential invocations/games. Destroy
  only after active search has returned. A per-table atomic guard rejects a
  second active invocation and active clear/resize with an explicit logic error.
- `TranspositionTableUse {table, evaluator, value_policy}` appended as a defaulted
  final parameter to `iterative_search`. The pointer defaults to null. A zero or
  too-small capacity is disabled. No table interaction, even guard/hint access,
  occurs in alpha-beta-disabled exhaustive mode.
- `EvaluatorCacheCompatibility::not_certified` is the conservative default.
  Only explicit `deterministic_position` certification allows numeric reads or
  writes. This is controller metadata, separate from M2's fingerprint and ABI.
  Certification means a deterministic function of all declared inputs. If
  relevant behavior changes without a fingerprint change, change `value_policy`
  (or refrain from certification). Stateful/random providers may receive legal
  ordering hints only; disable all TT interaction when strict call-order
  comparability matters for them.
- `TranspositionStatistics`, appended to `SearchStatistics::transposition`.
  `SearchProvenance` additionally reports `tt_numeric_certified` and the policy
  namespace, without retaining a table pointer in detached progress/results.

Example with an already usable evaluator:

```cpp
chess_lab::TranspositionTable table(8 * 1024 * 1024);
chess_lab::TranspositionTableUse use{
    &table, chess_lab::EvaluatorCacheCompatibility::deterministic_position, 1};
auto r = chess_lab::iterative_search(core, root, history, evaluator,
                                    options, token, progress, use);
// The OWNER does this after search returns and releases its exclusive guard.
if (r.status == chess_lab::IterativeSearchStatus::evaluator_failed ||
    r.status == chess_lab::IterativeSearchStatus::search_error)
  table.clear();
```

The owner serializes evaluator use/replacement and table lifetime. Rejected
concurrent attachment does not acquire/touch the active owner's entries; await
that owner before clearing/resizing. Cancellation retains completed subtree
entries. Errors invalidate final playable fields and all provisional progress
as in M4; **search never clears the attached table**. No rollback, write logs,
generation invalidation or error recovery is added to traversal.

## Storage, exact history proof and byte accounting

The only retained dynamic allocations are one `Impl` metadata object and one
fixed byte arena. There are no strings, vectors, maps, context allocations or
per-entry heap allocations inside retained table storage. Compact played/intended
moves have six bytes including terminator; fingerprints use M2's 64-byte SHA-256
text. A nonstandard fingerprint length declines numeric storage rather than
truncating identity.

The arena partitions into four-way entry buckets, a fixed history-node pool
(eight nodes per entry slot, plus a null node), and fixed history-interning index
heads. A history node contains one canonical signature, a parent index,
reference count and index/free-list link. Interning is exact on `(parent,
signature)`, with collision chains. Entries retain tail references; immutable
prefixes are shared across entries, depths, branches and matching subsequent
searches. Active traversal pins its current/parent tails. Refcount release
iteratively frees unreachable prefixes; it never recursively consumes the C++
call stack. This avoids copying the complete path into each entry.

For B power-of-two buckets, W=4, E=`sizeof(Entry)`, H=`sizeof(HistoryNode)`,
and M=`sizeof(Impl)`, the allocated-byte formula is:

```
slots = B * W
history_nodes = slots * 8
capacity_bytes = M + slots*E + (history_nodes+1)*H
                   + history_nodes*sizeof(uint32_t)
```

The largest fitting power-of-two bucket count is selected. `Impl` and the
unused pool space are counted, not only live entries. The caller-owned fixed
handle and allocator bookkeeping are outside the owned allocation budget;
there are no additional retained allocation/control blocks. Too-small/zero
budgets allocate neither metadata nor arena and report capacity zero. Native
measured allocations: 1,048,576 requested -> 565,428 actual bytes; 8,192 requested
-> 4,596 actual bytes. Layout/rounding can differ on WASM; tests assert the actual
reported allocation never exceeds the requested budget. Resize allocates a
replacement before swapping; allocation failure preserves the old valid table.

The invocation separately maintains its required exact occurrence-count map,
including scratch verification counters, and a commutative fingerprint. It
initializes once from supplied history. Each make/undo changes exactly one
signature's count and XORs out/in its mixed `(signature,count)` contribution;
updates are O(log U) for U distinct signatures, plus fixed-size signature
hashing. The composite prefilter adds evaluator-visible snapshot fields and
history completeness; depth, evaluator and policy are independently checked.
No full-history hash scan occurs at each recursive node. The existing actual/
hypothetical history vector is still preserved for M3/M4 semantics.

A numeric prefilter candidate must match every snapshot field (including raw EP,
halfmove/fullmove clocks and schema), effective legal-EP repetition signature,
completeness, exactly equal remaining depth, fingerprint and value policy.
Then candidate evidence is reconstructed into scratch counters in the existing
invocation count-map nodes: zero counters, walk the shared prefix, find each
exact signature and increment its counter, then compare every exact count.
This needs no extra candidate-verification allocation and takes O(Hlog U+U)
for H stored occurrences. Chronological order is deliberately irrelevant under
M3/M4's tracked rules. A fingerprint collision is never proof of compatibility.
Forced collision injection exists only in the private test/benchmark interface.

If active pinned history exhausts the pool, evict entries deterministically to
recover unpinned nodes. If that cannot provide a complete context, decline
numeric storage for the affected branch while permitting legal hints. Never
retain a truncated history as complete evidence. Test histories exceeding the
pool exercise this path. Invocation working memory belongs to ordinary search
and is not retained by the TT; no hidden validation cache survives return.

Replacement first uses an empty bucket slot; otherwise it minimizes
`(touch_sequence/8, remaining_depth, touch_sequence, slot_index)`. This favors
recent entries, then deeper entries within a recency band, without assuming
that deeper scores are reusable at shallower depth. Multiple depths can coexist.
A structurally identical numeric record refresh updates its slot without counting an
eviction. History-pool pressure uses the same ordering across live slots.
Ordinary replacement inspects four slots; global pressure scans all slots.
Eviction and allocation refusal affect performance only, never numeric semantics.
There is no TTL or global shared table.

## Bounds, mate coordinates and canonical PV proof

`detail::classify_bound` compares a completed node result with the **original**
incoming window: score <= original alpha is UPPER; score >= original beta is
LOWER; otherwise EXACT. The window is saved before local claim baselines and
TT probes. A zero claim that improves original alpha=-1 within beta=1 is EXACT,
not an upper bound merely because local alpha later became zero.

The first implementation uses compatible bounds only when they prove a closed
window; it does not tighten still-open windows. This avoids conflating a
TT-narrowed result with an exact original-window value. Bounds return only into
score-only traversal and never become published exact root candidates. All
root PLAY candidates still receive independent full-window subtree resolution.

Storage normalizes either mate color by subtracting current ply; lookup adds
current ply, with checked underflow/overflow. Finite scores are unchanged. For
example WhiteMate(9) at ply 4 stores WhiteMate(5); the same proven context at
ply 12 reconstructs WhiteMate(17). The BlackMate transform is identical in
distance arithmetic, preserving White-perspective comparison. A distance less
than the insertion ply or an overflowing lookup is declined. Traversal stores
actual established fail-soft node outcomes, never inherited window endpoints;
there is no fabricated ancestor-relative score to normalize.

Every node checks current terminal rules before probing. Claims are computed
and legal intended moves probed canonically before any hit can shortcut their
selection. A cached exact leaf can establish its empty horizon/claim endpoint;
a matching exact claim can return only after current eligibility is revalidated.
An internal cached played decision **does not prove a PV**: a publishable line
is re-searched. Bound/exact scores may shortcut nonpublishable score queries;
any promising/canonically earlier tie candidate is refined full-window before
replacing an exact incumbent. With TT hints active this proof applies throughout
the tree, not just the previous-PV prefix. Missing/evicted links consequently
cause safe re-search; no speculative chain is followed.

Position-only played hints are separate from numeric compatibility and must be
found in the current canonical legal-move array before reordering. Claims keep
their original priority over plays; intended claim moves remain unplayed and
outside PVs. Neither eviction, cache state, ordering mode, MultiPV N nor an
incompatible evaluator can make a visited-first move win a canonical tie.

Writes occur only on returning an established terminal, horizon result, exact
subtree or valid bound. Cancellation is polled before probes, during history
verification/interning/pressure scans, before writes and at M4's existing
recursion/evaluation/commit boundaries. Cancellation before depth one retains
only the explicitly unevaluated legal fallback; later cancellation retains the
last committed depth. Incomplete nodes write nothing, but completed descendants
from the interrupted iteration may remain. Root/actual history are unchanged;
normal and exceptional traversal retain exception-safe LIFO cleanup.

## Counter definitions

All TT counters are per invocation and cumulative in committed snapshots:

| Field | Meaning |
| --- | --- |
| lookups | Node probes, including probes finding nothing |
| candidate_hits | Numeric records matching the cheap composite prefilter |
| exact_hits / bound_hits | Fully verified usable exact/bound candidates selected by a probe, before deciding whether a PV needs re-search |
| bound_cutoffs | Verified bound actually closed the incoming score-only window |
| hint_hits | Returned played hint found legal and used as an order hint, including a hint already first |
| context_rejections | Prefilter candidates failing exact context/compatibility or checked mate conversion |
| stores | Completed record writes, including refreshes and hint-only records |
| evictions | Discarded different records during replacement/pool recovery; same-record refreshes excluded |
| pv_researches | Verified exact internal played score needing canonical-line re-search |
| verified_history_nodes | Stored prefix nodes inspected during candidate exact verification |
| verification_elapsed | Time checking completed candidates; invocation elapsed includes any interrupted verification too |
| skipped_stores | Writes retaining only a hint because certification/evidence/normalization did not permit a numeric value |

Existing visited-node counts still include node entries/cache-hit entries;
evaluator calls count actual attempted dispatch; cutoffs include valid TT
closures. Whole invocation elapsed covers validation, table locking/context
setup, progress-copy cost and cleanup. No node tracing or artificial delay is
introduced. Candidate verification timing is specific to the requested TT
observability; off/exhaustive paths incur none of that measurement.

## Regression fixtures

The original M0–M4 test sources are unchanged; `m5_search` is an eighth CTest
suite enrolled natively and with the existing pinned Node/WASM launcher.

- Twenty-four seeded ternary trees compare every intermediate depth and every
  ranked line against a separate exhaustive oracle, TT off, cold and warm, both
  colors, previous-PV modes and N=1/3/8. Scores include ±1e300 and signed zero,
  claims, mates, dead and stalemated nodes. Old snapshots are checked after later
  computation. Verified exact/bound hits and real TT bound cutoffs are required.
- Mirrored mate races require faster victory/slower defeat for both colors with
  cold/warm caches and both ordering modes. Direct private-layer fixtures
  independently check all bound kinds at different root plies, subtraction and
  overflow rejection, equality boundaries and claim-adjusted original windows.
- Real-chess all-line equivalence to TT-free exhaustive mode covers mate over
  clock 149, root mate over clock 150, stalemate/known-dead/75-move roots,
  clock-99 intended claims, clock-100 immediate claims, both sides, extreme
  finite values, legal/pinned EP, actual third/intended-third/fifth repetition
  and incomplete imported histories. Every published variation replays legally.
- Private exact-context fixtures force all prefilter/index hashes to collide,
  distinguish history counts/completeness, depth, evaluator/policy, raw EP,
  fullmove and halfmove fields, and accept reordered chronology with equal
  canonical counts. Multiple equal-position remaining depths coexist.
- Zero/tiny capacity, variable/oversized evidence, repeat clear/resize, forced
  deterministic eviction and missing/evicted PV proof all preserve output.
  Active clear/resize and second active search are rejected. Native additionally
  acquires from a real second thread; exhaustive mode bypasses even an active
  populated table, with zero reads/hints/writes.
- Certification-off checks prohibit numeric reads/writes despite a matching
  populated fingerprint; a changed actual M2 identity cannot inherit values.
  A private illegal played-hint fixture is rejected by current move legality.
  Explicit internal noncanonical-order traps cover true equality versus a
  misleading equality bound hiding a worse exact alternative.
- Cancellation before first depth, during evaluation and after prior progress
  checks fallback/last depth and surviving completed subtree writes. The
  controller-style `Owner` harness injects late evaluator failure, callback
  EvaluationError and core traversal failure: engine leaves entries until
  return, owner then clears only its table. Evaluator quarantine, no further
  dispatch, root/history restoration and independent table survival are checked.

## Actual commands and local results

Linux x86_64 managed VM, AMD EPYC 9V74, three visible CPUs with two-CPU cgroup
quota (`200000 100000`), GCC/G++ 14.2.0, CMake 3.31.6,
Ninja 1.11.1.git.kitware.jobserver-1. Suite times include shared scheduling and
some concurrent builds; they are not throughput measurements.

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

All eight passed on the implementation commit: Debug 52.09 s (M5 2.17 s),
Release 1.81 s (M5 0.16 s), Profile 2.34 s (M5 0.15 s).
Supplemental M5 ASan/UBSan also passed without diagnostics:

```sh
cmake -S . -B build/m5-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic' \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/m5-sanitize --target m5_search --parallel 2
ASAN_OPTIONS=detect_leaks=0 build/m5-sanitize/m5_search
```

Leak detection is disabled for this managed environment; no leak-sanitizer or
all-suite sanitizer pass is claimed. The initial private capacity fixture
incorrectly assumed 4 KiB enabled storage and was corrected to use a sufficient
budget; disabled-session internal methods also safely decline access.

Local `/workspace/emsdk-m0/emsdk install 4.0.10` was attempted again; its SDK Node
archive download from `storage.googleapis.com` failed with `Operation not
permitted`. No local WASM pass is claimed. Actual pinned GitHub CI uses:

```sh
source "$RUNNER_TEMP/emsdk/emsdk_env.sh"
# immutable emsdk 62a853cd3b3134398ce85cde8bb5cbb2ef0194cb / 4.0.10
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

The actual encompassing WASM artifact is hashed by the existing Node launcher
before test evaluator construction. Tests keep `-fexceptions` and M4's 2 MiB
search stack budget/Debug overflow diagnostics. Native/WASM success does not
prove live browser Worker postMessage responsiveness, shared-memory deployment,
Windows, macOS or mobile behavior. TT lifetime/signaling transport remains
controller work; no additional platform assumptions are introduced.

## Reproducible cold/warm measurements

The native `m5_benchmark` uses the actual dynamically loaded material evaluator,
including for the explicitly synthetic tree adapters. Loading/hashing/replay and
printing are excluded from search timing. The progress callback only retains
snapshots, with that copying cost included. Parameters:

```
MODULE [depth=3] [iterations=3] [warmup=1] [bytes=1048576]
       [initial|pawn|tree|history|collision|small] [N=3] [PV-order=1]
```

Benchmark depth is deliberately bounded to 1..5. The same binary, artifact,
configuration, supplied history, depth/N and ordering mode run in three states:
TT off; cold, cleared before every invocation; and warm, populated once with a
complete identical search, followed by the requested warmups and measured
invocations without clearing. State changes are outside timing. Warm retained
contents may evolve; per-run counters are recorded rather than assumed equal.
Every complete best/ranked score, decision, full PV/end/intended metadata across
all committed depths feeds a hexfloat-based deterministic checksum. Cache states
must agree or the benchmark fails. No evaluator delay or fake centipawn scaling
is introduced.

```sh
for workload in initial pawn tree history collision small; do
  build/native-release/m5_benchmark \
    "$PWD/build/native-release/libmaterial_evaluator.so" \
    3 3 1 1048576 "$workload" 3 1
done
# Same initial workload with previous-PV hints off:
build/native-release/m5_benchmark \
  "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1 1048576 initial 3 0
# Separate profile-build smoke/measurement (never mix artifact identities):
build/native-profile/m5_benchmark \
  "$PWD/build/native-profile/libmaterial_evaluator.so" 3 3 1 1048576 initial 3 1
```

Workloads:

- `initial`: standard initial FEN, one known occurrence/incomplete prefix.
- `pawn`: `4k3/8/8/8/8/8/P7/4K3 b - - 0 1`, one known occurrence/incomplete
  prefix. Repeated identical warm invocations permit numeric reuse across
  sessions with exactly matching context.
- `tree`: distinct-node ternary synthetic adapter, depth-first IDs, no graph
  transpositions. All prior actual history is known, initially just node 0.
  Synthetic snapshots are deterministically encoded by the shared test adapter;
  horizon children self-reference only for legal claim probes. Any cold
  repetition of work is PV refinement, not a hidden graph transposition.
- `history`: the same tree shape after a 256-edge single-child stem, replaying
  `a1a2` at each step, complete history with 257 distinct known occurrences.
  Prefix signatures are shared inside bounded storage. This is an abstract
  adapter workload, not a claim that repeated `a1a2` is a real-chess game.
- `collision`: initial position with private prefilter/index mask zero, forcing
  exact candidate rejection and four-way replacement pressure.
- `small`: initial position with an 8,192-byte budget (4,596 allocated on this
  native build), overriding the ordinary budget. No test is skipped when useful
  numeric context is evicted or cannot fit.

Effective material configuration for every measurement:
`{"bishop_value":3.25,"knight_value":3,"pawn_value":1,"queen_value":9,"rook_value":5}`.
Release artifact SHA-256
`c22b71130bff9671bec260fb6312707597ccedde68858e3b043f02297f7a3368`,
fingerprint `3eb703deada5c36e85443bee37435625eceadf2b9fa2182a553b6ab36803ceb0`.
Profile artifact SHA-256
`c03a4b5f32ac53b45c193ef7367507a6d9330009dd0a57cc8c9f07c270bd60d4`,
fingerprint `5fc336739aa4069fecd996020484ee6d3c2c2eda527ce1b4c08b77277b757acd`.

All runs use depth 3, N=3, alpha-beta on, explicit cache certification and policy
1, one warmup and three measured invocations per state. Workloads other than
`small` request 1 MiB and allocate 565,428 bytes in the current layout. Local
builds/tests had completed before serial measurements, with no perf, CPU
pinning, sanitizers or intentional heavy concurrent workload. Shared VM noise
and fixed mode order remain limitations; medians are observational.
The complete 72 measured invocation records, including all times/counters,
checksums and identities, are in [m5-benchmark-results.csv](m5-benchmark-results.csv).

| Build/workload (previous PV) | State | Median ms | Nodes / evaluator calls | Exact-line checksum |
| --- | --- | ---: | --- | --- |
| Release/initial (1) | off | 62.927704 | 1708 / 1265 | 3239843763328268135 |
| Release/initial (1) | cold | 94.019316 | 1731 / 1265 | 3239843763328268135 |
| Release/initial (1) | warm | 92.649636 | 1729 / 1254 | 3239843763328268135 |
| Release/pawn (1) | off | 0.461624 | 152 / 104 | 6543094783694917155 |
| Release/pawn (1) | cold | 1.395839 | 152 / 104 | 6543094783694917155 |
| Release/pawn (1) | warm | 1.208227 | 126 / 26 | 6543094783694917155 |
| Release/tree (1) | off | 0.059570 | 48 / 30 | 8100785470849578103 |
| Release/tree (1) | cold | 0.184167 | 50 / 28 | 8100785470849578103 |
| Release/tree (1) | warm | 0.167983 | 40 / 3 | 8100785470849578103 |
| Release/history (1) | off | 0.592411 | 48 / 30 | 9103053822950254227 |
| Release/history (1) | cold | 0.709597 | 50 / 28 | 9103053822950254227 |
| Release/history (1) | warm | 2.858669 | 40 / 2 | 9103053822950254227 |
| Release/collision (1) | off | 62.692521 | 1708 / 1265 | 3239843763328268135 |
| Release/collision (1) | cold | 94.677506 | 1731 / 1286 | 3239843763328268135 |
| Release/collision (1) | warm | 93.040935 | 1731 / 1286 | 3239843763328268135 |
| Release/small (1) | off | 66.541057 | 1708 / 1265 | 3239843763328268135 |
| Release/small (1) | cold | 96.593257 | 1731 / 1286 | 3239843763328268135 |
| Release/small (1) | warm | 97.781323 | 1731 / 1286 | 3239843763328268135 |
| Release/initial (0) | off | 64.698016 | 1708 / 1265 | 3239843763328268135 |
| Release/initial (0) | cold | 105.911565 | 1731 / 1265 | 3239843763328268135 |
| Release/initial (0) | warm | 97.748377 | 1729 / 1254 | 3239843763328268135 |
| RelWithDebInfo/initial (1) | off | 71.311792 | 1708 / 1265 | 3239843763328268135 |
| RelWithDebInfo/initial (1) | cold | 99.599370 | 1731 / 1265 | 3239843763328268135 |
| RelWithDebInfo/initial (1) | warm | 101.418716 | 1729 / 1254 | 3239843763328268135 |

Representative warm run 0 counters (Release, prior-PV ordering on):

| Workload | Probes / candidates | Exact / bounds / bound cutoffs | Hints / rejects | Stores / evictions | Verification nodes / ms |
| --- | --- | --- | --- | --- | --- |
| initial | 1726 / 226 | 3 / 31 / 31 | 89 / 192 | 1726 / 1692 | 131 / 0.046509 |
| pawn | 123 / 189 | 20 / 69 / 69 | 80 / 100 | 123 / 34 | 278 / 0.047712 |
| tree | 37 / 63 | 13 / 17 / 17 | 27 / 33 | 37 / 7 | 95 / 0.015293 |
| history | 37 / 68 | 16 / 17 / 17 | 26 / 35 | 37 / 4 | 8550 / 2.489538 |
| collision | 1728 / 6912 | 2 / 0 / 0 | 1 / 6910 | 1728 / 1727 | 7 / 0.22596 |
| small | 1728 / 2 | 2 / 0 / 0 | 1 / 0 | 1728 / 1727 | 7 / 0.002234 |

These measurements show **no wall-time speedup over TT off** with this cheap
material evaluator. Warm pawn reuse cuts evaluator calls from 104 to 26 and
nodes from 152 to 126, yet median time rises from 0.461624 to 1.208227 ms.
The no-graph-transposition tree drops 30 calls to 3 after exact repeated-session
population but still spends more time in hashing/interning/verification and
canonical proof. With the long history, warm exact verification inspects 8,550
stored prefix nodes in run 0 and dominates invocation time (2.489538 ms of
2.858669 ms), despite only two remaining evaluator calls. Tight capacity and
forced collisions cause heavy replacement and almost no useful numeric reuse.

The table-enabled traversal also performs conservative full-window canonical
refinement throughout hinted subtrees, which can add visits in cold/no-hit
workloads. No-hit cold performance should not be assumed equal to uninstrumented
M4 traversal. Exact-history contexts are stricter than ordinary board-only TT
keys; many chess transpositions are deliberately ineligible. Future optimization
can reduce proof/interner/verification costs, but must keep D67–D83 semantics.
None of these costs is concealed as a speedup; no acceptance threshold requires
one. Complete cold/warm/off checksums agree for every workload and ordering mode.

Existing `native-profile` keeps optimized debug symbols and frame pointers in
core, host/provider, traversal, table and benchmark. External sampling on a host
where perf is available is reproducible:

```sh
mkdir -p work
perf record -o work/m5-perf.data -g --call-graph dwarf -- \
  build/native-profile/m5_benchmark \
  "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3 1048576 history 3 1
perf report -i work/m5-perf.data --inline
perf stat -e cycles,instructions,cache-misses,branches,branch-misses -- \
  build/native-profile/m5_benchmark \
  "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3 1048576 initial 3 1
```

Perf is not installed in this environment. No sampling stacks, PMU counters,
allocator profile or instrumented throughput result is fabricated or required
in CI. Inspect history intern/proof, legal generation/claim probes, map/hash
maintenance and exact-PV refinement rather than attributing all cost to lookup.


## Observed GitHub Actions and review gate

Validated code/tests/build commit:
`d88f60fa7c0780153cd1906270ba60773151cf24`.

- [Native run 39504509083](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39504509083)
  passed all eight suites on Ubuntu 24.04 / GCC 13.3.0 in each matrix job:
  Debug 37.08 s (M5 2.07 s), Release 2.47 s (M5 0.19 s), and Profile 2.38 s
  (M5 0.18 s).
- [Pinned WASM run 39504509179](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39504509179)
  passed all eight suites with Emscripten 4.0.10 / SDK Node 22.16.0, Ubuntu
  24.04, total 151.96 s. Per-suite: M0 0.05 s, M1 51.40 s, M2 0.61 s,
  C layout 0.03 s, C++ layout 0.04 s, M3 45.22 s, M4 45.41 s, M5 9.13 s.
  Logs confirm all generated, exact-context/bound/mate, real-chess, bounded
  memory/exclusive use, cancellation/controller-error and ordered-PV fixtures.
  M5 encompassing WASM SHA-256:
  `480ce7a5aabe57d7577afe28b048d0d884fa0ef2d0f8444bf5c4483da5b84426`.
  M3/M4 digests respectively:
  `a8c710218e2c0a7ee2fbe936db381fa43ebd273b014a4081d7a9df7cc745a1be`,
  `27dfd48259d8fad5cfb18fd4243307d3efd7309222f27c539c61043de1a5247b`.

The final README/developer/validation/CSV follow-up changes documentation and
recorded measurements only. Runtime source, tests and build configuration are
those of the passing implementation commit above. PR-triggered checks may run
again; no unobserved run is described as passing.

M5 ends at an implementation PR for explicit architecture/code review. Do not
merge, enable auto-merge, or start M6/controller/UCI/UI, pondering or selective
search. Review attention: fixed-arena capacity/rounding, shared-prefix refcount
proof, original-window bounds, conservative exact-PV re-search, owner failure
protocol and the openly measured cold/warm performance regressions.
