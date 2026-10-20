# M3 validation

## Provenance and scope

`m3/basic-search` starts at fetched `origin/main`
`47fc857` (merged M0/M1/M2, approved M3 specification and kickoff present).
Implementation follows Decisions 41–49, including 42A. No M4, opening-book
lookup, UCI, controller, GUI, quiescence, extensions, iterative deepening,
transposition tables, deadlines or cancellation. M1 public contracts and the
M2 evaluator C ABI are unchanged.

## Concrete C++ API and invariants

`engine/include/chess_lab/search.hpp` exposes `ChessEngineLab::Search`
(`chess_search`), `SearchScore`, `GameHistory`, `SearchOptions`, `ChessDecision`,
`SearchResult`, and synchronous `search(core, root, history, evaluator, options)`.
Only repository-owned types appear in the public header. Consumers link the
search target, which transitively links core/evaluation. Exceptions are enabled
for WASM through the existing core usage requirements.

`SearchScore` is trivially copyable, 24 bytes, alignment 8, with a uint64 tag,
double and uint64 distance. Safe factories reject nonfinite values. Comparison
is exact, White-positive, including signed-zero ties and finite values up to
binary64 extremes; mate distances use checked public increments. There are no
finite mate sentinels or score negations. Internally both values and alpha/beta
bounds use **root-relative** mate distances: a terminal at ply P is Mate(P).
This is exactly the order-preserving transformation of node-relative Mate(0)
through P parent increments. It keeps all comparisons in one coordinate system,
including bound endpoints near mate zero. Absent alpha/beta values are the
explicit unbounded bottom/top endpoints. Root output distances are relative
to the root. The independent oracle instead increments on every return.

Depth must be in [1,64]; invalid requests, including zero, return SEARCH_ERROR.
The maximum rejects unreasonable recursive stack requests; it is not a promise
of practical runtime at that depth. All terminal checks precede static leaves.
The only dead-position detector is M1's sound, incomplete detector (D47).
The enum names are lowercase C++ spellings of the specification's statuses and
actions. Results are SEARCH-origin only; an OPENING_BOOK provenance enum is
reserved for later integration, with optional score/depth fields ready for it.

`GameHistory::starting_at` records one known occurrence and defaults to an
incomplete preceding history. `replay` and immutable `after_move` accept legal
actual moves; callers set `complete=true` only when the entire preceding game
is known. Root validation compares every position field, including clocks and
raw EP; repetition equality excludes clocks and uses normalized *legal* EP.
Signatures compare all 64 pieces, turn, rights and legal EP exactly. The initial
reference implementation uses a linear exact-signature scan, **no hash
prefilter**, so collisions in either M1 key cannot cause draws.

Search clones the root once and copies its occurrence history. Every hypothetical
move and prospective claim probe is made/undone in LIFO order, with exactly one
history push/pop. Claim probes never persist. Cleanup explicitly propagates undo
errors rather than throwing from destructors or concealing broken restoration.
An undo failure discards the private clone, returns SEARCH_ERROR, and leaves
caller state unchanged. GameHistory replay helpers use their own disposable
clones; they do not change the supplied positions.

Legal coordinate moves are checked for canonical spelling/uniqueness and sorted
lexicographically (promotion suffix included). Claims initialize the incumbent:
current claim, then earliest intended-move claim, then moves. Strict improvements
alone replace it, preserving first-best and claim-on-tie. Bounds can prune losing
or tied alternate branches but cannot replace the exact incumbent root branch
with a bound-only alternate. No alternate scores/PVs are published.
PV moves are actually played moves only; `pv_end` distinguishes horizon,
terminal, or a claim. `pv_intended_move` announces an unplayed endpoint move.
Failures clear all decisions, scores, PV and endpoint metadata, with completed
depth zero. EvaluationError invalidates the whole search and preserves M2
quarantine. No fallback is returned. Identity/configuration and incomplete
history are retained as provenance.

Statistics count recursive node entries, attempted host evaluations, and
alpha/beta closures that skip remaining traversal. Claim probes are not visited
nodes or evaluations; they still contribute to elapsed time. Elapsed steady-clock
time covers validation, identity capture, cloning, traversal and cleanup. With
pruning disabled, cutoffs are zero. These are aggregate counters: no per-node
timing, tracing, logging or allocation hooks.

## Correctness evidence

`m3_search` runs the same tests natively and under Node/WASM:

- 160 deterministic generated ternary trees, alternating White/Black roots,
  independently enumerated by an exhaustive oracle with no shared traversal or
  bounds code. Values include ±1e300, signed zero, exact ties, claims, automatic
  draws, known-dead nodes and both mate winners. Both pruning modes must match
  exact scores, selected decisions, full PVs and endpoint metadata.
- Explicit mirrored trees require faster victory and slower defeat; a controlled
  adapter supplies **constant zero for every hash**, while exact signatures
  remain distinct. LIFO token checks and frame balance verify restoration.
- A separate clone-per-edge real-chess reference compares scores, actions and
  PVs for mate-in-one, a Black-to-move pawn endgame and the initial position,
  at depth 2 with -1e300, zero and +1e300 static values. Additional genuine
  positions (including Kiwipete) compare exhaustive/pruned search at depth 2/3.
- Root/depth-zero mate and stalemate; root mates with clock 150; mate-in-one
  from clock 149; known-dead king/minor configurations; 98/99/100/150 clock
  boundaries; pawn and capture resets; immediate/intended claims and ties;
  a winning pawn continuation declines an available claim. Horizon claims
  compare against static continuation rather than claiming unconditionally.
- Actual knight-cycle replay verifies the intended third occurrence, immediate
  third and automatic fifth; imported FEN does not invent earlier occurrences.
  Root consistency includes clocks. EP fixtures cover legal, pinned and
  noncapturable EP; castling-right differences remain distinct.
- Controlled C ABI provider fails on evaluation 2 after a root branch completed:
  explicit error status, invalid status, NaN and both infinities. Both pruning
  modes must discard everything, preserve inputs, unwind frames, retain
  diagnostics and quarantine the evaluator. Reuse performs no further ABI
  dispatch; replacement succeeds. Independent traversal and undo failures
  return SEARCH_ERROR. No C++ exception crosses the provider C boundary.
- Score factories/accessors, mate overflow, negative zero, invalid depths and
  history, identity/options provenance, legal PV replay, and no-pruning zero
  cutoffs are checked. Existing M0/M1/M2 and C/C++ ABI layout suites remain.

Representative vectors: mate at the root = Mate(0), mating move = Mate(1),
stalemate/automatic draw = Finite(0); optional claim on an exact tie chooses
CLAIM_DRAW before intended claims, which precede a played move. Intended
50-move claim on the rook fixture at clock 99 announces `a1a2` and returns an
empty PV. At the knight-cycle position before `f6g8` completes occurrence 3,
that move is announced, not played.

## Reproducible commands and observed results

Linux x86_64 cloud VM, Intel Xeon Platinum 8573C, GCC/G++ 14.2.0,
CMake 3.31.6, Ninja 1.11.1. From the checkout:

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

All six tests passed locally in Debug (53.63 s), Release (3.51 s), and
Profile (5.30 s), including M0/M1/M2, both ABI layouts and M3.
After the final fixture/provenance additions, Debug M3 passed again (27.36 s),
Release passed all six again (1.17 s), and Profile M3 passed again (0.57 s). These suite
wall times include concurrent build activity and are not benchmarks. Final CI
evidence is recorded below.

Supplemental instrumentation (separate build, never throughput baseline):

```sh
cmake -S . -B build/sanitize-m3 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sanitize-m3 --parallel 2
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize-m3 --output-on-failure
```

All six ASan/UBSan suites passed (152.83 s), including M3 (94.08 s), with
no reported runtime sanitizer errors. A missing-field initializer warning in
the tree fixture was corrected explicitly; no production change was needed. After rebuilding the final fixture, M3
passed ASan/UBSan again (117.14 s), without compiler warnings or reported
runtime sanitizer errors.
Leak detection is disabled for this managed execution environment; no leak
sanitizer validation is claimed.

Local pinned SDK installation was attempted:
`/workspace/emsdk-m0/emsdk install 4.0.10`. The Node 22.16.0 archive request to
`storage.googleapis.com` failed with proxy HTTP 403, leaving no local compiler.
This is not a local WASM pass. The existing GitHub-hosted pinned workflow runs:

```sh
source "$RUNNER_TEMP/emsdk/emsdk_env.sh"
# emsdk commit 62a853cd3b3134398ce85cde8bb5cbb2ef0194cb / Emscripten 4.0.10
cmake --preset wasm-debug -DCHESS_LAB_NODE_EXECUTABLE="$EMSDK_NODE"
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

M3 uses the M2 Node launcher, which hashes the actual encompassing WASM
artifact before constructing the controlled provider identity. No fake digest
is used for the WASM acceptance execution. The native fixture's documented
64-character dummy digest identifies a controlled statically linked test
provider, not a separately loaded production artifact.

## D49 profiling and benchmark protocol

`native-profile` uses RelWithDebInfo (`-O2 -g -DNDEBUG` on this GCC), preserving
frame pointers consistently in core, evaluation host/provider, search and
benchmark via `-fno-omit-frame-pointer`. MSVC uses `/Oy-`; those toolchains are
unvalidated. Normal Release is `-O3 -DNDEBUG`, without the profile option.
No LTO, architecture-specific flags, forced no-inlining, sanitizers or tracing
are enabled in either normal preset. Inlined call sites remain inspectable in
debug information. Native CI includes Debug, Release and Profile correctness.

The headless native `m3_benchmark` is also built with BUILD_TESTING=OFF:

```sh
build/native-release/m3_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1
build/native-profile/m3_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 3 1
# Arguments: module path, depth, measured iterations, warm-up runs, optional
# start FEN, optional comma-separated actual legal moves to replay as history.
build/native-release/m3_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1 \
  'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1' \
  'g1f3,g8f6,f3g1,f6g8'
```

The default workload records the initial FEN, one known occurrence with unknown
prefix, material evaluator `{}` effective defaults, actual native artifact
fingerprint, build/compiler/platform, fixed depth, warm-up/repetitions and each
run's nodes/evaluations/cutoffs/wall time. Both modes run with the same immutable
evaluator instance/config. The rolling checksum consumes action, score and PV,
including warm-up results. Module loading/configuration/hash and history replay
are outside search timing. Root validation/cloning and draw probes are inside.
Keep native module files immutable until their benchmark process exits.

For external Linux sampling, install perf where supported and run a longer
workload (or attach `perf record -p PID -g --call-graph dwarf`):

```sh
mkdir -p work
perf record -o work/m3-perf.data -g --call-graph dwarf -- \
  build/native-profile/m3_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3
perf report -i work/m3-perf.data --inline
perf stat -e cycles,instructions,cache-misses,branches,branch-misses -- \
  build/native-profile/m3_benchmark "$PWD/build/native-profile/libmaterial_evaluator.so" 3 100 3
```

Inspect CPU samples/call stacks across recursive search, legal move generation,
make/undo, exact repetition scans, snapshot conversion and evaluator dispatch.
Inclusive stack costs show callers, self costs attribute leaf work; inlined
frames need debug-aware unwinding. CPU samples are not wall-clock latency or
allocation counts. Use a separately enabled heap profiler to measure allocations;
PMU cache/branch misses can guide a hypothesis but do not establish its cause.
Restricted hosts may deny perf/PMU access. No such measurements are fabricated
or required for correctness. Instrumented/profile runs have tool and unwinding
overhead; compare normal Release independently, never as interchangeable timings.
No always-on hot-path instrumentation was added and no M1/M2 optimization is
included in this milestone.

### Observed Release measurements

Serial runs, with no concurrent builds/tests, no profiler, no CPU affinity and
no instrumented allocator. The VM exposes three CPUs with a two-CPU cgroup
quota (`cpu.max = 200000 100000`); shared scheduling remains a limitation.
Each mode has one warm-up search, then three measured searches at depth 3.
Counts below are **per search**, identical across the three measured runs.
These are observational timings, not confidence intervals or universal speedups.

Release evaluator fingerprint:
`3eb703deada5c36e85443bee37435625eceadf2b9fa2182a553b6ab36803ceb0`.
Effective configuration:
`{"bishop_value":3.25,"knight_value":3,"pawn_value":1,"queen_value":9,"rook_value":5}`.
Both workloads have a single recorded occurrence with an unknown prefix and no
replay moves. Default workload FEN and command are given above. Pawn workload:

```sh
build/native-release/m3_benchmark "$PWD/build/native-release/libmaterial_evaluator.so" 3 3 1 \
  '4k3/8/8/8/8/8/P7/4K3 b - - 0 1'
```

| Release workload | Alpha-beta | Nodes | Evaluations | Cutoffs | Run times (ms) | Decision / root score |
| --- | --- | ---: | ---: | ---: | --- | --- |
| Initial | off | 9323 | 8902 | 0 | 517.118069, 512.239855, 491.999384 | PLAY a2a3 / Finite(0) |
| Initial | on | 546 | 485 | 38 | 29.669299, 29.269636, 29.887340 | PLAY a2a3 / Finite(0) |
| Black-to-move pawn | off | 279 | 238 | 0 | 1.084324, 1.080950, 1.202750 | PLAY e8d7 / Finite(1) |
| Black-to-move pawn | on | 57 | 40 | 10 | 0.160095, 0.160570, 0.185551 | PLAY e8d7 / Finite(1) |

Final rolling checksum after both modes and their warm-ups:
initial `4369480127475266195`; pawn `4673612655167615891`.
Separate untimed result inspection confirmed both modes return PVs
`a2a3 a7a5 a1a2` / Finite(0) for initial and
`e8d7 a2a3 d7c6` / Finite(1) for the pawn fixture.
The checksum consumes score, decision and PV; equal-score move ties are common
with the material-only evaluator, so this workload strongly favors pruning on
equality. No claim is made about other evaluators or positions. Intended-claim
probes perform extra make/undo/snapshot work even when no draw is available;
that cost is included but not counted as visited search nodes.

### Separate profile-build smoke workload

The identical initial-position workload ran successfully in native-profile,
with the same nodes/evaluations/cutoffs/decision and final checksum. Profile
evaluator fingerprint:
`5fc336739aa4069fecd996020484ee6d3c2c2eda527ce1b4c08b77277b757acd`.
This differs from Release because identity includes build/artifact information.
No external profiler was attached. Times (ms): off
540.547029, 559.816513, 594.249238; on 32.865600, 29.750050, 32.102243.
These are profile-build smoke timings, **not** the canonical Release baseline.
`readelf -S build/native-profile/m3_benchmark` confirmed `.debug_info` and
`.debug_line`; generated Ninja compile flags confirmed frame pointers on core,
evaluation, search and benchmark. `perf` is not installed in this environment;
no sampling stacks, allocations, PMU measurements or profiler overhead were
measured. The commands above are repeatable instructions for a supported host.

## Observed GitHub Actions

Implementation/test commit: `065d30886cb238a7ce0535754899af68693f48c8`.
PR: https://github.com/yctekmen-sudo/chess-engine-lab/pull/4

- [Native PR run 39461230316](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39461230316):
  completed successfully in all three matrix configurations (Debug, Release,
  Profile): 6/6 each, 22.20 s / 1.17 s / 1.16 s respectively. Ubuntu 24.04,
  GCC/G++ 13.3.0. M3 durations: 12.10 s / 0.49 s / 0.39 s.
- [WASM PR run 39461230712](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39461230712):
  configure/build and all 6/6 tests passed (92.54 s): M0 0.06 s, M1 48.78 s,
  M2 0.64 s, C layout 0.03 s, C++ layout 0.04 s, M3 42.87 s. Logs confirm
  the generated-tree and genuine-chess references, FIDE rules, PV, history,
  late failure/quarantine and restoration checks. Emscripten 4.0.10,
  SDK Node 22.16.0, Ubuntu 24.04. The M3 encompassing WASM SHA-256 was
  `b4c84130483fb5cf2e0b568d686e7ae79cfa970e4edff637d6cf940c22db7e4c`.
  The existing M2 artifact digest remained
  `6e843873b5aa63dc49ac0300c2ee6b06de342b25a30d8674e1816f437f7298e6`.
  This establishes Node/WASM correctness, not browser/device portability.

The report/readme/development follow-up changes documentation only; production
source and tests are those of the commit above.

## Limitations and review boundary

Full general FIDE dead-position detection is not implemented (approved D47).
Unknown pre-import history cannot prove missing repetitions. Fixed-depth search
has horizon effects and no cancellation; high depths may be very expensive.
The reference implementation deliberately probes intended claims and uses
linear exact-history comparisons; benchmark throughput includes those costs.
Only Linux native and the observed Node/WASM execution are validation targets.
Browser, mobile, Windows and macOS success is not claimed. M3 stops at an open
PR for ChatGPT architecture/code review; do not merge or begin M4.
