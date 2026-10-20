# Chess Engine Lab — M4 Iterative Search Specification v1.0

**Status:** APPROVED — Decisions 50–66; merged in documentation PR #7. Codex may implement M4 on a separate feature branch under the kickoff's review gate.  
**Milestone:** M4 — iterative deepening, cooperative cancellation, progress, exact MultiPV, previous-PV ordering.  
**Authority:** Extends docs/architecture-v1.0.md and docs/m3-search-spec-v1.0.md, using the concrete shipped engine/include/chess_lab/search.hpp and engine/src/search/search.cpp on main. M3 semantics remain authoritative where unchanged. No M2 evaluator C ABI or ChessCore contract change.  
**Boundaries:** This milestone adds search-library functionality and test/benchmark harnesses, not the full UCI, desktop, mobile, or browser controller. Controller interactions below are API contracts or minimal test adapters, not a commitment to deliver the later application controller.

## 1. Approved decision register

| Decision | Binding outcome |
|---|---|
| D50 | Controller owns clocks, deadlines, Stop/app lifecycle and requests cancellation; search only polls an externally supplied cooperative signal and does not infer its cause. |
| D51 | Optional synchronous progress callback after each entirely completed depth: depth, exact score, selected decision, PV, cumulative statistics and completed MultiPV ranking; every progress update is provisional until final status. |
| D52 | Uniform nominal-depth iterations 1, 2, ...; optional maximum; no extensions, reductions, quiescence or selective search. A cancelled iteration is not committed. |
| D53 | Exact top-N MultiPV, N default 1, canonical White-max/Black-min ranking and deterministic ties; all root alternatives must be evaluated to exact scores (not merely bounds) as necessary for ranking. |
| D54 | Cancelled before depth 1: one lexicographically first legal played move, explicitly marked unevaluated fallback; no score/PV/completed depth; never fallback on evaluator failure. |
| D55 | Every iteration performs fresh search at full deeper uniform horizon. Previous PV and ranked root choices may reorder the *next* iteration's move exploration, optionally disabled. No cached prior scores replacing deeper searches; TT deferred. |
| D56 | Explicit termination reasons: DEPTH_REACHED, CANCELLED, DEPTH_LIMIT_REACHED, EVALUATOR_FAILED, SEARCH_ERROR. No TIME_EXPIRED in search API; terminal-root result has its own terminal status. |
| D57 | Controller owns worker/thread; search stays synchronous and single-threaded. Callback executes in search invocation's thread. |
| D58 | Initially poll cancellation at each recursive node entry, before evaluator calls, and during potentially lengthy loops/probes; profile before weakening polling frequency. |
| D59 | Completed-depth progress snapshots are immutable. Callback exceptions yield SEARCH_ERROR and invalidate result, not evaluator quarantine. Callback may request cancellation; no reentrant active-resource search. |
| D60 | MultiPV ranks distinct legal *decisions*, not just moves; include eligible immediate and intended-move draw claims, deduplicated by kind as specified below; claims precede played moves on exact ties. |
| D61 | Preserve existing M3 search() API unchanged; add a separate iterative_search() API over shared search logic, retaining the M3 exhaustive reference oracle. |
| D62 | Internal depth cap 64 plies; caller optional max depth 1..64; no max means continue until cancellation or cap; terminal root returns immediately with depth 0. |
| D63 | Controller owns search session IDs, stale-result suppression, and lifetime of active position/evaluator/token; never let old session results be accepted for new position. |
| D64 | Platform-independent cancellation API; native thread-safe signaling; WASM signaling depends on actual supported environment, which must be verified, not assumed. |
| D65 | Preserve *future pondering* hooks: separate real vs hypothetical positions, history, root provenance and session metadata; defer pondering and cross-root search reuse. See GitHub issue #6. |
| D66 | Acceptance gates: oracle equivalence, cancellations, MultiPV/rules, ordering, errors, platform tests, compatibility and fair performance comparisons. |

Related deferred investigations: [#5 selective-depth/alpha-beta equivalence](https://github.com/yctekmen-sudo/chess-engine-lab/issues/5) and [#6 pondering readiness](https://github.com/yctekmen-sudo/chess-engine-lab/issues/6).

## 2. Existing M3 baseline must survive intact

The exact public M3 function is:

~~~
SearchResult search(const ChessCore&, const Position&, const GameHistory&,
                    Evaluator&, SearchOptions = {});
~~~

SearchOptions presently contains int nominal_depth (1..64) and bool alpha_beta_enabled. SearchResult carries SearchStatus, optional ChessDecision, optional White-perspective SearchScore, completed_depth, principal_variation, pv_end, pv_intended_move, SearchStatistics, evaluator_identity, options, history_incomplete, diagnostic. GameHistory is an immutable-by-API occurrence record with full root snapshot validation. Do not remove, reinterpret, or silently change those fields, enums, defaults or behavior. In particular, M3's status values complete, terminal, evaluator_failed, search_error remain source-compatible.

Introduce a **distinct** iterative_search(...) public C++20 API, plus M4-specific options, result/progress types and cooperative cancellation abstraction. Names/struct organization are implementation choices, documented in the handoff, but **do not graft incompatible M4 semantics onto M3 SearchOptions or change the evaluator C ABI**. Only source-level C++ changes are allowed. Reuse the same underlying traversal/rules/minimax logic rather than a second independent engine.

At each identical fixed depth, when MultiPV=1, previous-PV ordering is off and no cancellation/error occurs, the selected decision, exact score and principal-variation semantics must match the M3 fixed-depth reference. Committed results must also match the exhaustive (alpha_beta_enabled=false) oracle when ordering is on. Stats need not be identical because iterative deepening repeats work.

## 3. M4 public API semantics

**Inputs:** ChessCore, immutable root Position and matching immutable GameHistory, usable Evaluator, IterativeSearchOptions {optional max_depth, alpha_beta_enabled, multipv_count>=1, previous_pv_ordering_enabled}, optional cancellation observer, optional completed-depth progress callback. MultiPV defaults to 1; optional max_depth absent means no user depth bound, only the engine safety cap. Establish documented reasonable positive limits for multipv_count and reject invalid parameters predictably rather than allocating unbounded memory. The internal depth cap is 64 and not user-configurable.

**Cancellation signaling contract:** The observer is lightweight, can be requested from another execution context when the platform supports it, is specific to one invocation, and has a well-defined safe lifetime until that invocation returns. A native atomic Boolean/token is sufficient. Encapsulate its mechanics so platform-specific signaling is **not** baked into recursive traversal. Don't require C++ threads for WASM compilation. If absent, interpret as no cancellation requested; no engine clock or timeout is introduced.

**Completed-depth snapshot:** depth, best ChessDecision, exact structured SearchScore, legal PV with PVEnd and intended-move metadata, up to N exact ranked MultiPV entries including per-entry decision/score/PV/end metadata, cumulative SearchStatistics, evaluator identity and context options. The top-ranked entry agrees with the selected decision and score. A progress event represents **one atomic complete depth**, not one event per root candidate. It is immutable across further computation, either value-owned or otherwise proven detached from mutable traversal state.

**Final result:** explicit M4 termination reason, optional committed last-completed-depth result, cumulative stats of all iterations including abandoned work, provenance (evaluator identity, options, history completeness, root identity as applicable), diagnostic. Distinguish whether the playable move was obtained by fully completed search or an **unevaluated fallback**, never just by inferring an empty PV. For terminal root, return the exact TerminalOutcome with SearchScore, depth 0, no iterative PV/MultiPV and status TERMINAL, distinct from errors/fallbacks. Document unambiguous fields for status, completed depth, fallback marker and rank availability; no fake values.

**Identity and provenance:** Search library reports the actual evaluator identity and rooted search inputs/history provenance. Controller-owned session/generation IDs and ponder prediction labels may be transported through wrapper metadata; the library need not know UI session semantics. Results must never be silently relabeled as from a different root or evaluator.

## 4. Iterative-deepening control and linearization

After validating inputs and determining whether the root is terminal, run independent full-width nominal-depth iterations d=1,2,... until requested depth reached, controller cancellation observed, internal cap reached, evaluator failure or search error. Reuse previous completed PV and ranked root alternatives **only as move ordering hints** (see §7); do not transplant shallow leaf values, prune to a narrower horizon, skip evaluation based on previous depth, or use a TT in M4.

Every iteration starts with fresh root alpha/beta bounds and exhaustively resolves the root decision/MultiPV under exact mathematical alpha-beta rules. On completion, assemble one fully detached result snapshot. **Commit/publish boundary:** check cancellation before committing a completed iteration; if cancellation was observed, discard that iteration and keep earlier committed depth. If cancellation arrives after the completed snapshot has been committed, that depth remains available, even if callback forwarding or cancellation is nearly simultaneous. Define and test a consistent observable boundary; no sub-node or per-root partial progress is ever committed. Poll between iterations as well.

Termination:
- Requested maximum completed: DEPTH_REACHED.
- Controller signal observed: CANCELLED with last committed complete depth if any.
- No caller max and completed depth 64: DEPTH_LIMIT_REACHED (with last committed depth 64).
- A root already terminal: TERMINAL with proper outcome and depth 0.
- Any evaluator failure, including *after earlier progress snapshots*: EVALUATOR_FAILED; **invalidate all** previously delivered provisional progress and final decisions, scores, PVs, MultiPV, and fallback; quarantine evaluator through existing M2 behavior.
- Other failures, including callback exceptions: SEARCH_ERROR; invalidate final playable result and prior provisional progress; do not quarantine healthy evaluator.
- Reject invalid options/root/history with explicit SEARCH_ERROR diagnostic (or clearly documented typed validation error consistent with M3); no fallback.

A synchronous callback is invoked from the search thread only after a completed depth is committed. It must not launch a reentrant search using the same evaluator/root resources. It *may* request cancellation via the token. Cancellation requested during a callback yields CANCELLED retaining that just-committed depth; treat callback exceptions as SEARCH_ERROR. Controller must discard/supersede earlier provisional UI results if the final search reports evaluator_failed/search_error or a later session ID replaces them. No unconditional callback for a terminal root or fallback; caller observes those via final result.

All position/history make/undo operations must be exception-safe and LIFO; caller-owned root and history must be unchanged by return, failure, or cancellation. No concurrent overlapping use of an evaluator instance or its mutable selection without explicit safe ownership by controller. Eval calls themselves may block, so no hard cancellation latency guarantee.

## 5. Fallback and terminal edge cases

A cancellation received before the first complete nonterminal iteration returns CANCELLED, completed_depth=0 and exactly one deterministic lexicographically first **legal PLAY decision** that is explicitly unevaluated and may be discarded by the controller. Return no score, PV, PVEnd or MultiPV ranking. Do **not** fabricate N fallback alternatives. A genuine terminal root is not a fallback, even if cancellation was requested before search.

Root validation and legal-move enumeration needed to establish safe fallback may have costs. Errors during these operations become SEARCH_ERROR, not an invented move; if an evaluator error occurred, EVALUATOR_FAILED takes precedence over any fallback even if already computed earlier. The fallback does not claim to consider strategic score or optimal draw claims. Nonterminal roots have at least one legal played move under M1 status semantics.

## 6. MultiPV exact decision ranking

MultiPV candidates are distinct **root decisions**:
- Each legal played move is a distinct candidate with an exact resulting minimax value and its legal continuation PV.
- If immediately eligible, include exactly one CLAIM_DRAW decision at exact Finite(0).
- If no immediate claim exists but one or more intended legal moves permit a claim, include exactly one CLAIM_DRAW_WITH_INTENDED_MOVE candidate using the lexicographically earliest qualifying intended move, scored Finite(0). These are announced unplayed moves; do not include the announced move in the PV.
- Do not multiply equivalent intended-move claim choices into different MultiPV rows. Use established M3 preference of immediate claim before intended-move claim before play on exact ties.

Sort descending for White, ascending for Black, using SearchScore's exact total ordering and mate-distance semantics. For score ties, claims precede plays in their canonical order and played moves sort lexicographically regardless of visitation/order/pruning. Return min(requested N, number of distinct candidates). Always return the global top N, not simply the first N searched; no synthesized centipawn units. If N is one, selected decision and score must agree with M3. A terminal root has no candidate ranking.

**Exactness trap:** ordinary alpha-beta may return only a bound for an alternate root candidate. A bound-only root branch must **not** be represented as a numerically exact MultiPV entry, even if its value appears tied with another. Use independent full-window searches or another provably exact re-search policy for candidate scores/PVs, and document it. A first implementation may choose the simpler, slower method. Root cutoff/pruning cannot silently omit a candidate which could rank among N. The authoritative N candidates and per-entry PVs must remain stable under moving previously preferred moves to the front.

## 7. Previous-PV ordering, equality and oracle invariants

The next iteration may prioritize the last completed PV moves at corresponding legal nodes and the previous iteration's ranked root alternatives. All other moves retain canonical ordering; provide an option to disable the optimization. Do not force an illegal prior PV move into a changed legal move set. No reuse of previous iteration scores as search results, no selective depths, no TT.

Alpha-beta correctness is relative to the same full uniform-depth tree. Search ordering can change which equal-score branches are pruned; **canonical tie resolution and every published exact PV must therefore be independent of visitation order**, not merely score-equal. An implementation must not assume that first visited move wins ties after enabling PV ordering. If an alternate's value or canonical PV cannot be proven exact due to a bound, re-search/refine as needed. Compare full published decision, score, mate distance, per-line PV/PVEnd and intended claim against a fixed-depth alpha-beta-off oracle. Do not weaken the oracle or silently redefine M3 deterministic policy to accommodate ordering.

## 8. Cancellation checks, worker constraints and errors

Check cancellation on node entry, immediately before static evaluation, during loops that may be long (move enumeration/claim probes and when practical around library calls), between iterations and before depth commit. An external cancellation request is not a score/bound and must never be treated as a drawn or mated leaf. Unwind via a dedicated internal abort path, restoring history and board state; do not convert cancellation to an evaluator failure. Report cumulative statistics, including interrupted work, with precise counter definitions inherited from M3. Timings are search-invocation wall time, not an engine-held deadline.

**Native:** exercise actual request signaling from a separate thread with a thread-safe atomic or equivalent. **WASM/Node:** compile/test the same source-level cancellation path, including deterministic mid-iteration cancellation. Do not claim that Node/WASM test proves a browser Worker can receive regular postMessage while synchronous C++ is busy. Browser shared-memory atomics require supported toolchain, thread model and deployment isolation; Worker termination is not cooperative cancellation and cannot guarantee unwinding/last-depth result. Browser Worker transport/real-time signaling remains deferred to controller integration, with a documented portability limitation.

Callbacks must be safe to forward/marshal onto a UI thread from controller side. Callback exceptions catch at an engine boundary and return SEARCH_ERROR, invalidating published results. Do not quarantine the evaluator on callback or ChessCore failures. Unknown exceptions must not escape into the C ABI or silently turn into a success. Avoid asynchronous progress invocation from inside the search library.

## 9. Pondering and other deferred integration

Future opponent-time analysis uses a *hypothetical legal descendant* after a predicted opponent move, plus GameHistory::after_move or equivalent complete history propagation. A White-to-move root remains White to move; it is not made Black-to-move by controller override. Scores remain White-perspective, and Black response selection must occur at an actual Black-to-move descendant. Controller session identity, predicted move, root/history identity and evaluator provenance distinguish speculative work from normal authoritative decisions. Reuse only after validating the actual opponent move and position/history; cancel stale sessions and finish unwinding before resource reuse.

M4 does **not** implement pondering, ponderhit, cross-root reuse, opening books, transposition tables, extensions, quiescence, reductions, adaptive search depth, an integrated controller UI, or multithreaded tree search. Preserve API extension points without building a speculative cache or injecting controller concepts into minimax. Reference GitHub issue #6. See #5 for later study of alpha-beta correctness under dynamically selective search trees.

## 10. Required tests and acceptance

1. **Compatibility:** Existing M0/M1/M2/M3 tests, SearchScore layout, GameHistory, SearchResult and evaluator ABI remain green. M3 search() directly reproduces baseline outputs. M4 MultiPV=1 no-ordering final depth equals M3 including result action/score/PV/end metadata.
2. **Iterative depth:** Verify each completed depth 1..K against independent fixed-depth searches, White and Black, mates, large finite values, signed zero, exact ties, draw claims at root/horizon, incomplete history and legal EP normalization. Depth limit 64 can be validated by controlled cheap tree/adapter rather than an impractical real-chess depth-64 full-width search.
3. **Cancellation:** deterministic request (a) before first depth, (b) during node traversal, (c) during intended-claim probing, (d) during later depth after ≥1 committed update, (e) from progress callback, (f) at depth-commit boundary. Verify fallback exactness only in case a/b before depth 1, restoration, no publication of partial depth, and correct statuses. Native additionally uses a true second thread; deterministic test adapters supplement rather than replace it.
4. **Progress:** snapshots detached and atomic, successive depths, cumulative counters, sane PV/replay, no bound-only values, immutability after later depths, no contradictory selected/rank #1. Test callback exception after completed progress yields SEARCH_ERROR and no final decision; prior provisional event must be understood as invalid.
5. **Evaluator failure:** controlled late EvaluationError after depth(s) have published; no decision/score/PV/MultiPV/fallback in final; documented invalidation of earlier notifications; M2 evaluator quarantined; exception-safe traversal cleanup; separately test non-evaluator search error.
6. **MultiPV:** N=1, N>1, N > legal decision count, both side-to-move rankings, mate-distance priorities, immediate draw claim, intended-move claim dedup, tie priority, no invented centipawns, exact per-entry score/PV, no alpha-beta bound leakage. Compare to exhaustive oracle enumerating each distinct legal root decision.
7. **PV ordering:** enabled/disabled versus exhaustive fixed-depth oracle at same depths and roots; equality-heavy positions and generated trees engineered to change visitation order, including same-value PV ties and claim ties. Record ordering mode and traversal stats.
8. **Portability:** native Debug, Release, Profile CTest and pinned Emscripten 4.0.10 / Node WASM CTest via existing presets/workflows. Test non-running WASM cancellation signaling only to extent of actually supported harness; document the difference between Node test and browser Worker integration.
9. **Benchmark/profiling:** same evaluator artifact/configuration, FEN/history, depth, N, alpha-beta mode and warm-up when contrasting previous-PV ordering on/off; include standalone M3 fixed-depth reference where useful. Record per-iteration/cumulative node counts, eval calls, cutoffs, wall times, result checksum and environment. Cumulative iterative-deepening work must not be confused with one depth-N M3 search. No invented speedup or profiler evidence. Reuse native-profile sampling instrumentation; no default per-node tracing.

### Validated commands expected in implementation PR

~~~
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
cmake --preset native-profile
cmake --build --preset native-profile
ctest --preset native-profile --output-on-failure
# Under pinned activated Emscripten 4.0.10/SDK Node:
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
~~~

Record real results, CI run links, important fixtures, effective options, limitations, fair before/after measurements and profiler reproducibility in docs/m4-validation.md. No requirement to reach depth 64 on a realistic board.

## 11. Handoff and review boundary

Codex implements M4 only in a feature branch from approved, updated main and opens a PR for architectural review; Codex must **not** merge, silently reinterpret decisions, replace M3 reference implementation, introduce M5 TT/pondering features, or claim unverified browser portability. Stop for clarification on contract ambiguities or unexpected cross-platform incompatibility. Do not start work until this specification and the kickoff have been reviewed and incorporated into main.

**Open implementation design latitude:** Exact C++ M4 types, spelling of M4 termination enum, placement of cancellation token, snapshot ownership, minimal controller shim and full-window per-root MultiPV strategy are left to Codex as long as all semantics and acceptance tests above hold.
