# Chess Engine Lab — Codex Kickoff for M5

**Status:** APPROVED BY USER — implementation must not start until this document plus the approved M5 specification are merged into `main`.  
**Milestone:** M5 — controller-owned, bounded, history-safe transposition table for exact-depth iterative alpha-beta search.  
**Authoritative document:** `docs/m5-transposition-table-spec-v1.0.md` (approved Decisions 67–83).  
**Existing contracts:** `docs/architecture-v1.0.md`, `docs/m3-search-spec-v1.0.md`, `docs/m4-search-spec-v1.0.md`, `docs/m4-validation.md`, `docs/evaluation-abi-v1.0.md`, `engine/include/chess_lab/search.hpp`, `engine/include/chess_lab/evaluation.hpp`, `engine/src/search/search.cpp`, and the current CMake/CTest presets.  
**Delivery boundary:** M5 C++ implementation, tests, benchmarks, documentation and an implementation PR for review. **Do not merge.** No application controller/UI, pondering execution, new evaluator C ABI or selective search.

## 1. Mandatory preflight and design checkpoint

1. Fetch latest `origin/main`; verify the **approved** M5 specification and this **approved** kickoff are both present on `main`. **STOP** if either is missing, still a draft, or materially different from the reviewed text. Do not infer approval from this kickoff alone.
2. Create branch `m5/transposition-table` from current `origin/main`; record base SHA. Never implement directly on `main`.
3. Inspect actual M3/M4 code, including recursive traversal, `SearchScore`, `GameHistory`, repetition signatures, claims, `EvaluationIdentity`, evaluator error handling, cancellation, progress publication, root full-window MultiPV, CMake tests and native/WASM CI. Use repository code as the concrete baseline, with the approved specification authoritative where new behavior is defined.
4. **Before substantial code changes**, publish a concise design checkpoint in the implementation PR or working handoff: exact public C++ API, table lifetime/locking contract, bound classification at original windows, node-relative mate transformation, incremental history fingerprint, exact bounded history evidence and memory accounting, evaluator-cache-compatibility opt-in, PV proof/re-search policy, counter definitions, and deterministic replacement choice. Explain anticipated complexity/performance overhead.
5. If any of these requirements cannot be satisfied without changing an established M2/M3/M4 contract, stop for review rather than weakening the correctness requirement. Prefer fewer TT hits over a semantic compromise.

## 2. Preserve M1–M4 and establish the TT interface

- Preserve all existing public `search(...)` and `iterative_search(...)` call sites, defaults, return/status semantics, and the M2 evaluator C ABI. Add source-compatible optional TT attachment/API in C++20; do not require existing callers to construct a TT. With no table, or zero capacity, existing behavior must remain logically unchanged.
- The controller or a minimal controller-style test harness owns a noncopyable, bounded, in-memory `TranspositionTable` across multiple sequential searches and games. Provide idle-only `clear()`/`resize()`, capacity reporting, and deterministic handling of a second simultaneous active search using the same instance. No global singleton, disk backing or internal controller/session implementation.
- The TT is enabled only for optimized alpha-beta mode when supplied. The `alpha_beta_enabled=false` exhaustive reference must **completely bypass TT probes, writes and ordering hints**, including with a populated TT argument.
- Explicit evaluator cache compatibility is **opt-in**, separate from the existing M2 fingerprint. A fingerprint does not certify determinism. Noncertified/stateful/random evaluators get no numeric score/bound reuse but may obtain legal positional move-ordering hints. Prefer C++ search/controller metadata rather than changing evaluator ABI.
- Cache values/bounds require matching evaluator fingerprint, score-relevant search policy, complete position/evaluator-visible state, exact repetition-count context including hypothetical path, history completeness, and **exactly equal remaining nominal depth**. Different depths/configurations may supply legal ordering hints only. Do not let a score entry bypass evaluator `usable()` or terminal rules.

## 3. Implement fast, exact, bounded context validation

- Reuse `GameHistory` and `RepetitionSignature` semantics, including legal-en-passant normalization, current halfmove clock, fullmove number where evaluator-visible, and the history-completeness bit. Use an **incrementally maintained composite fingerprint** as a cheap lookup prefilter; update on make/undo without scanning full history every node.
- A hash/fingerprint collision is never proof of numeric compatibility. On every candidate numeric hit, verify **exact canonical repetition-signature occurrence counts**, position/evaluator-visible snapshot fields and all other relevant rule context. Count maps, not chronology, are sufficient only under the existing M3/M4 tracked-rule contract.
- Design bounded exact-context evidence so owned buckets, keys, history data and auxiliary allocations **all count toward the configured TT byte limit**. Avoid copying full path histories to every entry or accumulating unbounded validation objects. Clearly document your chosen data representation and candidate-hit verification cost.
- Inject deliberately colliding fingerprints in tests. Verify that collisions can at most lose a potential hit or provide a separately legality-validated hint, never cause incorrect numeric reuse.

## 4. Alpha-beta cache semantics and exact PVs

- Distinguish **EXACT**, **LOWER_BOUND**, **UPPER_BOUND** entries. Classify proven node outcomes against the node's **original incoming alpha/beta window** (not tightened windows). Use bounds only for sound tightening/cutoffs. Never treat a bound as an exact score or exact MultiPV candidate.
- Store mate distances **node-relative**, translating to/from M3/M4 root-relative WhiteMate/BlackMate scores at each access with checked subtraction/addition. Do not cache a transformed bound or endpoint unless its node-local meaning is mathematically sound. Keep finite White-perspective scores unchanged; do not convert to negamax or centipawns.
- Keep M4's exact root MultiPV contract: independently establish exact score/continuation for every necessary root candidate; compare full canonical top-N ranked lines, including exact ties, under different cache states. An internal cutoff must never silently turn into a published exact candidate score.
- Optional cached `ChessDecision` is a **hint**, not proof of the complete canonical PV. Reconstruct only with legal, context/depth/evaluator-validated links that establish canonical ties. If links are missing, evicted, bound-only, ambiguous or noncanonical, **re-search/refine**. Root claims precede played moves on exact ties; immediate claims, intended-move claims and played moves remain distinct. Announced intended moves are **not played** or inserted into PVs.
- Cache eligible terminal/draw information only under exact current-rule validation: checkmate, stalemate, dead positions, automatic fivefold/75 and optional immediate/intended-move threefold/50 claims. Never reuse a history-specific draw result for another incompatible game merely because boards match.
- Insert an entry only when an exact value or sound bound has actually been established. No partially unwound subtree, speculative PV, cancellation marker or evaluator failure may be written as a proven result.

## 5. Lifetime, replacement, cancellation and failure

- TT lifetime belongs to the controller instance; entries survive across searches/games until evicted, cleared, resized or destroyed. No fixed TTL. Match total allocated TT-owned memory against the user-selected budget, including exact history evidence.
- Implement a reproducible **recency-and-depth-aware** replacement policy and allow useful coexistence of multiple remaining depths for one position when capacity permits. The rule for reusing scores remains equal-depth, not `stored_depth >= requested_depth`. Document chosen policy and entry accounting; quantify replacement costs.
- Preserve completed valid subtree entries from a **normally cancelled** iteration, while returning only the last fully committed M4 depth or the explicitly unevaluated legal fallback. Check cancellation before expensive probes and on cache-hit fast paths.
- On `EVALUATOR_FAILED` or `SEARCH_ERROR`, the engine only reports status and invalidates published result/progress as M4 already does. **After the invocation returns**, the **controller-style owner clears only the attached TT**. Do not put clearing inside the search algorithm. Do not add generation IDs, write logs, rollback, or selective invalidation. The evaluator remains quarantined when M2 requires it.
- Provide per-invocation TT statistics for lookup/candidate hit/exact hit/usable bound/hint-only hit/context rejection/store/eviction and relevant verification overhead, with documented definitions. Keep existing M4 counters and provenance compatible.

## 6. Required correctness tests

Add focused native and WASM-enrolled tests, plus test adapters where needed. All existing M0–M4 tests remain green.

1. **TT off/on/reference:** Compare exact best decision, full structured `SearchScore`, mate distance, full canonical PV, `PVEnd`, unplayed intended-move metadata and **each exact ranked MultiPV line** across TT-disabled, TT-enabled cold, TT-enabled warm, and TT-free alpha-beta-off exhaustive oracle for tractable depths. Both colors, various MultiPV N, previous-PV ordering on/off, ties and finite extremes.
2. **History/rule cases:** Two histories with identical boards but different repetition multisets, completeness or halfmove clocks; legal EP normalization, fullmove/evaluator snapshot distinctions, claims at root and horizon, intended legal draw claims, terminal precedence, threefold/fivefold and 50/75-move thresholds.
3. **Depth/context isolation:** Same board at several nominal remaining depths, multiple depths in one table, reused positions across different games, incompatible evaluator fingerprints and policies, explicit cache-compatibility off, illegal cross-evaluator hints rejected, and stale move suggestions unable to displace canonical ties.
4. **Collision and memory:** Deliberate hash collisions, exact evidence rejection, forced evictions, strict owned-byte limits including variable-size history/context data, zero/tiny capacity, clear/resize while idle, deterministic replacement and controlled second-active-use rejection.
5. **Bound and PV proof:** Fail-high/fail-low/closed-window scenarios, original-window classification, mate bound normalization at different root plies for White and Black, invalid conversion/overflow refusal, evicted or missing PV links causing exact re-search, complete MultiPV exactness despite internal cutoffs.
6. **Cancellation/error/controller:** Cancel before first completed depth and during a deeper iteration; verify completed valid subtree caching and unchanged published M4 result/fallback. Inject evaluator failure after cached writes and after progress, callback exception and traversal error; assert the **controller-style harness**, not engine, clears the supplied TT and leaves unrelated instances untouched.
7. **Reference isolation:** When a populated TT is handed to exhaustive mode, assert **zero** lookups, hint uses and writes, with unchanged oracle output. On invalid or quarantined evaluator, TT cannot fabricate a valid result.
8. **Platform regression:** Build and execute tests on native debug/release/profile and pinned Emscripten/Node WASM. Test deterministic cancellation in WASM; do not assert that a Node result proves live browser Worker cancellation behavior.

Treat **any** reproducible mismatch in the required decision/score/PV/ranking metadata as an M5 correctness failure; do not mark it an acceptable cache difference.

## 7. Benchmarks, evidence and validation record

- Compare the **same** position/history, evaluator artifact/configuration, depth, MultiPV, ordering options, binary/profile and platform under (a) TT disabled, (b) TT cold/empty, and (c) TT warm/reused. Label the cache state explicitly and describe warm-up/population.
- Record elapsed wall time, visited nodes, evaluator calls, alpha-beta cutoffs, TT probe/valid-hit/bound/hint/eviction counters, memory cap and history-verification cost. Include favorable transpositions and adversarial/no-hit/high-history workloads. Report regressions openly; **no first-release mandatory speedup target**.
- Write `docs/m5-validation.md` with actual test results and commands, toolchain and CI links, design choices, expected limitations, benchmark fixtures and reproducible measurements. Do not invent benchmark numbers or describe unexecuted tests as passing.

Run applicable existing presets:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure

cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure

cmake --preset native-profile
cmake --build --preset native-profile
ctest --preset native-profile --output-on-failure

# In the pinned, activated Emscripten 4.0.10 / SDK Node environment:
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

Report any unavailable environment or CI limitations accurately. These presets do not alone prove Windows, macOS, mobile or real browser interoperability.

## 8. PR handoff and explicit stop point

Push only feature-branch implementation commits and open an M5 implementation PR to `main`. In its body include:

- Base SHA and important files changed; C++ API and evaluator cache-compatibility opt-in.
- Bound proof/classification and node-relative mate conversion examples.
- Incremental fingerprint design and **exact** repetition-history verification structure, worst-case owned-memory accounting, and deliberate collision test.
- Replacement policy/observability choices and real cold/warm benchmark numbers.
- How exact MultiPV and canonical full PVs are preserved under cache hits, bounds and missing links.
- Cancellation/exception boundaries and proof that **controller** clears TT after error, rather than the engine doing so.
- Full local/native/WASM tests run, CI links/results, validation document and any deviations requiring review.

**STOP when the implementation PR is opened. DO NOT MERGE IT.** Wait for the user and ChatGPT architectural/code review. Do not proceed into pondering (issue #6), selective search (issue #5), application controller/UI, book/UCI interface, disk persistence or parallel TT access.