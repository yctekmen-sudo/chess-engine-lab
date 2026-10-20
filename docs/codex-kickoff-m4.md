# Chess Engine Lab — Codex Kickoff for M4

**Status:** DRAFT FOR ARCHITECT REVIEW — do not start coding until approved and present on main.  
**Milestone:** M4 — iterative deepening, cancellation, progress, exact MultiPV, PV ordering.  
**Authoritative document:** docs/m4-search-spec-v1.0.md (Decisions 50–66).  
**Existing contracts:** docs/architecture-v1.0.md, docs/m3-search-spec-v1.0.md, docs/m3-validation.md, docs/evaluation-abi-v1.0.md, engine/include/chess_lab/search.hpp, engine/src/search/search.cpp, CMakeLists.txt.  
**Delivery boundary:** An implementation PR for ChatGPT/user review, **not** merge or M5.

## 1. Stop/check before implementation

1. Fetch latest origin/main. Verify the **reviewed/approved** docs/m4-search-spec-v1.0.md and this kickoff have been merged onto main. If missing or still review-only, STOP; do not implement against a draft or infer details from the kickoff.
2. Create m4/iterative-search from latest origin/main, recording base SHA. Do not commit implementation directly to main.
3. Read the actual M3 search.hpp, search.cpp, SearchScore, SearchResult, ChessDecision, GameHistory, evaluation bridge, Node/WASM harness, native-profile preset, benchmark, existing tests and CI workflows. Disservin is behind ChessCore; do not leak it.
4. Report an API-design summary **before substantial refactoring**: proposed C++ M4 types, callback/token lifecycle, committed-depth snapshot type, terminal/fallback status representation, exact MultiPV strategy and code-sharing plan.
5. If anything in the actual implementation contradicts a normative M4 requirement or demands changing evaluator ABI, pause and ask for architecture review. Do not silently weaken the spec.

## 2. Implement only M4

- **Preserve M3:** Retain existing search(ChessCore, Position, GameHistory, Evaluator, SearchOptions) behavior and public header compatibility. Do not remove existing SearchStatus cases or reinterpret nominal_depth. Add a new iterative_search API and M4 option/result types without a second copy of minimax rules. Refactor internal traversal carefully, preserving M3's exact repetition history, claim policies, terminal precedence, structured scores, mate-distance handling, canonical PV, error quarantine and exception-safe LIFO restoration.
- **Iterations:** Run complete uniform-depth iterations 1..max_depth, or 1..64 until cancelled when max absent. Do not add quiescence, extensions, reductions, persistent TT, cross-iteration score caches or selective depth. New alpha-beta bounds for each iteration. Depth 64 is a safety guard, not a required practical board-search benchmark.
- **Cancellation:** A platform-neutral, externally requestable and lifetime-safe signal. Check node entry, before evaluator dispatch, during lengthy loops/claim probes, between depths and before commit. Controller owns time and reasons; search reports CANCELLED but never TIME_EXPIRED. Cancellation safely unwinds position/history; no partial depth published. Native atomic should work across threads; design does not force threading into WASM code. Cooperative cancellation may be delayed by a blocking evaluator.
- **Progress:** Optional synchronous callback on the executing search thread, one immutable detached snapshot after each complete committed depth: depth, exact decision and score, full PV/PVEnd metadata, exact ranked MultiPV, cumulative counters and provenance. Partial root alternatives and alpha-beta bounds are NEVER progress events. Treat past progress as provisional; evaluator or callback failure later invalidates it. Callback may request cancellation; its thrown exception is SEARCH_ERROR, not EVALUATOR_FAILED.
- **Termination:** Distinguish DEPTH_REACHED, CANCELLED, DEPTH_LIMIT_REACHED, EVALUATOR_FAILED, SEARCH_ERROR, and terminal root. Return last fully committed depth on cancellation. Cancellation before depth one yields exactly one marked unevaluated canonical **legal PLAY fallback** and no score/PV/MultiPV. Root terminal yields actual terminal outcome, depth zero and no fallback. Error or evaluator failure yields no final action/ranking, even if depth(s) finished earlier. Error diagnostics must make invalid options/root mismatch clear.
- **MultiPV:** N default 1, configurable positive N with documented sane validation. Treat every root legal PLAY action as a distinct candidate; include current CLAIM_DRAW or single canonical intended-move CLAIM_DRAW_WITH_INTENDED_MOVE when eligible. Only one representation per equivalent claim choice. Exact White-max/Black-min SearchScore ordering, proper mate distance, claims first on ties, lexicographically sorted played moves on ties. Return min(N, distinct candidates). Per-entry PV/end metadata must be legal and exact. A root candidate scored only by alpha-beta bound cannot be published as exact; use full-window per-candidate search or provably exact re-search. With N=1 and no ordering, match M3.
- **Previous-PV ordering:** On depth d+1, prefer the prior completed PV move at corresponding positions plus previous MultiPV-ranked root decisions as move *order* hints only. Preserve canonical tie selection and exact PV even when alpha-beta explores moves in a different order. Add an option disabling the hints; compare against exhaustive, fixed-depth results. Avoid equating same best score with same correct canonical decision or same PV.
- **Controller boundaries:** Search stays synchronous single-threaded; owning controller/adapter starts worker and schedules time/cancellation/UI callbacks. No controller implementation or UI deliverable required in M4. Search session IDs, stale suppression and replacement are controller-owned; a minimal native testing harness may prove them. Evaluators cannot be concurrently reused by multiple active sessions without a designed ownership mechanism.
- **Pondering readiness ONLY:** Preserve ability for a future controller to search an immutable hypothetical legal post-opponent-move position with matching updated GameHistory and session/root/evaluator provenance. No side-to-move override for current real position. Do not add ponderhit, predicted-move search, controller pondering policy, or cross-root TT reuse. Track future work at https://github.com/yctekmen-sudo/chess-engine-lab/issues/6.
- **No evaluator changes:** Preserve M2 C ABI/version, evaluator failure quarantine and identity/fingerprint. Do not synthesize centipawns. Do not add artificial per-node tracing to normal builds. Keep Disservin opaque and ChessCore authoritative.

## 3. Concrete correctness tests

Implement tests as regression additions under tests/search, with native and WASM CTest enrollment.

**M3 preservation and references.** Existing suites must remain unchanged and pass. For representative real chess roots with complete and incomplete history, assert final M4 depth-N MultiPV=1 ordering-off decision, exact SearchScore, PV/PVEnd/intended-move against M3 search() at N; also compare each published intermediate depth against independent fixed-depth M3 calls. Repeat alpha-beta off/on and both sides. Use independent small-tree oracle where M3 itself might not expose subtle ordering/pv defects.

**Draws/mates/MultiPV.** Include immediate claim, intended-move claim only, multiple intended legal moves (deduplicated canonical earliest), actual threefold/fivefold history, 50/75 clocks, root mate/stalemate/known-dead, mate-over-clock precedence, mate-distance ordering, and claimant-vs-play tie. Set N=1,2,3 and N larger than distinct root decisions; all top candidates must be exact and sorted correctly. Include tied finite scores and ±1e300, signed zero, and Black-to-move minimization. Legal root moves must encode unambiguously, and all PVs must replay legally with announced-but-unplayed intended claim excluded.

**Ordering and alpha-beta bounds.** Construct deterministic generated trees where previous-PV move differs from canonical lexicographic first move and where alpha-beta yields non-exact intermediate bounds, including exact tie across pruned branches. Compare decision **and** full per-candidate score/PV/PVEnd to exhaustive independent oracle with PV ordering on/off and alpha-beta enabled/disabled. No candidate from only a bound may appear as an exact MultiPV score.

**Cancellation and progress.** Controlled cancellation before depth 1, inside recursion, during prospective claim probing, after some completed depth, between depths, from callback, near publication boundary. Verify only last fully completed depth is retained, callbacks never expose partial candidate arrays, fallback has precisely one canonical legal PLAY/no score/PV/MultiPV, and terminal root has true outcome. A native test must request cancellation from a second real thread; deterministic probes/test adapter make WASM behavior testable without implying unavailable browser signaling. Verify progress snapshots remain unchanged across later callbacks/results, and thread affinity is documented.

**Failure and cleanup.** Inject EvaluationError at a deeper iteration *after* earlier valid progress. Final status EVALUATOR_FAILED, quarantine/no scores/actions/MultiPV/fallback, unchanged root/history, zero further calls on quarantined evaluator. Inject callback exception after a completed snapshot: SEARCH_ERROR, no final decision, healthy evaluator remains usable. Inject ChessCore undo/traversal errors separately. Verify LIFO unwind in error and cancellation paths and prevent status confusion.

**Input/provenance.** Invalid max depth (zero, >64), bad N, history/root mismatch, unusable evaluator, optional token lifetime semantics, full/partial actual-game history. Evaluation identity must match actual provider; token cancellation must not cross-contaminate a later search. Ponder-prepared hypothetical GameHistory matches the hypothetically advanced root without changing the real root/side to move.

**Safety/runtime.** Unit tests must be bounded in time. Do not drive real-chess uniform full-width search to depth 64. Use a generated low-branching controlled adapter to exercise cap and limit statuses efficiently. WASM tests need to run under pinned Emscripten/Node with -fexceptions as in M3; do not infer browser Worker postMessage responsiveness.

## 4. Benchmark and validation

Reuse M3 native-profile and benchmark infrastructure. Extend or introduce an M4 headless workload, recording original FEN and supplied history, evaluator identity/params, optional max depth, MultiPV N, alpha-beta, prior-PV ordering, warmup/iterations, deterministic checksum of decision/score/ranked entries/PVs, counts per completed depth and cumulative elapsed work. Time search only; exclude module loading/hashing. Compare ordering on/off under the same environment/evaluator/target depth, with terminal behavior and exact choices equal; note extra work due to iterative deepening and MultiPV. If previous-PV ordering is not faster on some positions, report it rather than suppress it.

Document observed outputs in docs/m4-validation.md, including genuine run environment, native and WASM CI links, representative oracle fixtures, error/cancellation restoration, workload settings and trustworthy performance data. Keep external sampling profiler invocation reproducible; do not require perf in CI or fabricate hardware counters.

Build and test with existing presets:

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

# On activated pinned emsdk 4.0.10 with SDK Node:
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
~~~

Ensure tests, not mere compilation, exercise M4 on each supported validation target. Native and pinned WASM/Node success do NOT prove browser, mobile, Windows or macOS portability.

## 5. Handoff / PR review gate

Commit implementation only to m4/iterative-search, push and open a PR to main. In its PR body report the base SHA, exact public API choices, code-sharing/refactoring summary, how atomic progress/cancellation are implemented, how MultiPV scores are guaranteed exact, why ordered alpha-beta still preserves ties/PVs, test results/CI evidence, benchmark methodology and measured impact. Link deferred selective-search issue #5 and pondering issue #6 only as future work.

**Stop once the PR is open. DO NOT MERGE. Do not begin M5, UCI, UI, book integration or a browser Worker/controller implementation.** Await architecture and code review. If CI is not available, say so and provide actual local results; do not describe unrun tests as passing.
