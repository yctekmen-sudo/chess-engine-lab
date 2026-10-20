# Chess Engine Lab — M3 Search Specification v1.0

**Status:** APPROVED — Decisions 41–49 (including 42A).  
**Milestone:** M3 — synchronous fixed-depth reference search.  
**Authority:** Supplements `docs/architecture-v1.0.md`, `docs/m1-contracts.md`, `docs/evaluation-abi-v1.0.md`, and the implemented public C++ headers. It does **not** change the evaluator C ABI.  
**Repository path:** `docs/m3-search-spec-v1.0.md`.

## 1. Decision register and scope

| Decision | Approved outcome |
|---|---|
| D41 | Tagged finite/WhiteMate/BlackMate score; direct White-perspective minimax; no magic mate constants. |
| D42 | Chess-rule terminal conditions precede static evaluation, including at depth zero. Fixed nominal depth, no quiescence or extensions. Claimable draws are optional actions, including claims based on an intended legal move. |
| D42A | Optional **root-only** opening-book decision provider, consulted after authoritative terminal adjudication; legal book selection may bypass search/evaluation. **Integration deferred beyond M3/M4.** |
| D43 | Reversible, search-local history combines actual position occurrences and hypothetical traversal. Repetition equality is collision-safe, not hash-only. |
| D44 | Exact fixed-depth minimax with optional alpha-beta; fresh bounds per new search; errors from evaluator invalidate whole search (D37); no persistent pruning data. |
| D45 | Configurable synchronous depth and alpha-beta enabled/disabled; generalized decision, structured score, completed depth, PV, statistics and evaluator identity. |
| D46 | Stable deterministic legal-move ordering, first-best tie among moves, claim-on-tie, reproducible PV; no search randomness. |
| D47 | M3 uses only ChessCore's **sound but incomplete** `known_dead_position()` detector; false means unknown. Full general FIDE dead-position detection is **not** claimed. |
| D48 | M3 is a fixed-depth correctness baseline. Iterative deepening, deadlines, cancellation, MultiPV, TT, ordering heuristics, quiescence, extensions, opening books and UCI/controller/UI are out of scope. |
| D49 | Profiling-ready optimized native builds and reproducible search workloads are standard from M3 onward, without intrusive always-on hot-path tracing; measure before optimizing M1/M2/search internals. |

The M3 code is a source-level C++20 interface usable by both native and Emscripten/WASM. It must not include Disservin types in its public headers or change the established C evaluator ABI. Favor exact rules and correctness over premature changes to M1 allocation mechanics; measure costs.

## 2. Required existing interfaces

Implement against the actual shipped interfaces (do not infer a missing `game_over` or `history` API):

- `ChessCore` (`engine/include/chess_lab/chess_core.hpp`) provides opaque `Position`, `Move`, `UndoToken`; `clone`, `legal_moves`, `make_move`, `undo_move`, `status`, `known_dead_position`, `position_key`, `repetition_key`, `snapshot`, `encode_move` and `decode_move`.
- `PositionSnapshot` (`engine/include/chess_lab/position_snapshot.hpp`) supplies 64 piece codes, `Side`, castling flags, **raw** FEN en-passant target and halfmove/fullmove clocks.
- `evaluation_snapshot(const PositionSnapshot&)` converts to the approved 160-byte C position ABI; `Evaluator::evaluate(const ChessEvalPosition&)` returns a finite `double` or throws `EvaluationError`, quarantining the instance. `Evaluator::identity()` and `.usable()` are available. The owner serializes evaluator replacement with search.
- M1's two 64-bit position keys are currently identical, omit clocks, and are **not** collision-proof repetition evidence. M1 does **not** provide the full preceding game history. M1 `known_dead_position()` reports only provable sufficient cases.

## 3. Public search API — semantic contract

Codex may choose appropriate source filenames and exact C++ declarations; maintain the following behavior and document the concrete declarations. Suggested public header: `engine/include/chess_lab/search.hpp`, library target `chess_search` / `ChessEngineLab::Search`.

- **Inputs:** `const ChessCore&`, `const Position& root`, a validated immutable actual-game history context ending at `root`, a usable `Evaluator&`, and `SearchOptions { positive nominal_depth, bool alpha_beta_enabled }`.
- **Isolation:** Search clones the root once (or provides equivalent demonstrable nonmutation); it never changes the caller's position or supplied history, including on any error. Its recursive make/undo and pushed/popped hypothetical occurrences are LIFO and exception-safe.
- **Outputs:** `SearchResult` with status `COMPLETE`, `TERMINAL`, `EVALUATOR_FAILED`, or `SEARCH_ERROR`/invalid input; optional generalized `ChessDecision` (`PLAY`, `CLAIM_DRAW`, `CLAIM_DRAW_WITH_INTENDED_MOVE`, `TERMINAL`); optional structured score; nominal completed depth; stable PV; visited-node count, static-evaluation calls, alpha-beta cutoffs, elapsed duration; evaluator identity/fingerprint; diagnostic on failure; indication that pre-import history may be incomplete.
- **Successful nonterminal search:** Returns **one** decision and an **exact minimax root score**, not an arbitrary fail-soft bound. A `PLAY` decision carries a legal root move. If an eligible claim ties best continuation, return a claim instead. A terminal result has `completed_depth=0` and the appropriate terminal score. Failure has **no** decision, score or PV and does not return any fallback.
- **Root nominal depth:** require at least 1 for a nonterminal move-selection request. Internal depth-zero positions are valid evaluation leaves. Reject invalid options predictably rather than recurse with negative depth or unbounded stack use. No separate time budget or cancellation is introduced in M3.
- **PV:** A sequence of legal coordinate moves from the root, ending at the horizon, termination or a draw-claim action. Claims are actions, not played moves: do not append the *announced but unplayed* intended move to a PV. Document termination/claim as separate PV metadata if useful. Return only lines established by the exact selected principal branch; never label a bound-only alternate as a completely analyzed PV.
- **Book readiness:** Future results can identify `OPENING_BOOK` as decision source, with no fabricated score and completed depth 0. M3 implements `SEARCH`-origin results only; does not perform any book lookup.

Use stable move encodings (canonical lowercase coordinate UCI-style strings, including promotion suffix) for public results and stable ordering. No third-party types escape.

## 4. Structured score and exact comparison (D41)

Use a trivially stored value with an explicit tag: `Finite(double v)`, `WhiteMate(uint64_t d)`, `BlackMate(uint64_t d)`. Only finite `v` is allowed. Recommended source representation is `alignas(8)` with an eight-byte enum tag, an eight-byte `double`, and an eight-byte distance: **24 bytes / align 8 on tested targets**, with compile-time layout checks; this is **not** a newly frozen wire/C ABI. Enforce valid construction and hide irrelevant fields behind accessors if appropriate.

The complete White-perspective ordering is:

```
WhiteMate(0) > WhiteMate(1) > ... > Finite(any finite number)
             > ... > BlackMate(large distance) > ... > BlackMate(1) > BlackMate(0)
```

For finite scores, use ordinary numeric comparison; negative zero ties positive zero. Every White mate outranks every finite score; every Black mate ranks below every finite score, independent of evaluator units/magnitude. A forced mate is preferred sooner by its delivering side; a losing side prefers to delay it. Mated terminal position is mate-distance zero **at that node**. Propagating one ply to its parent increments mate distance by one; finite values are unchanged. Bound comparisons must also account for the parent/child mate-distance coordinate change. Use explicit search-bound endpoints conceptually `BOTTOM`/`TOP`, not `double` infinities or fake finite mate values. Check distance increment overflow rather than silently wrap.

Minimax maximizes for White and minimizes for Black. **Never negate scores just because the side to move changes.** Returned scores remain in White perspective, including forced mate and draw scores.

## 5. Fixed-depth semantics and terminal precedence (D42, D47)

At **every** visited node, including depth zero, adjudicate in this order:

1. Ask ChessCore for `status`. `checkmate` returns the winner's `Mate(0)`; `stalemate` returns `Finite(0)`. A delivered checkmate takes precedence over the automatic 75-move rule.
2. Check sound `known_dead_position()`: true returns `Finite(0)`. False is **not** proof of a live game; do not manufacture a general detector.
3. Check **automatic** draws using historical occurrences and current halfmove clock: fivefold repetition or `halfmove_clock >= 150` returns `Finite(0)` (after checkmate precedence).
4. Identify **optional**, currently available claims: at least three occurrences or `halfmove_clock >= 100`; additionally, a legal *announced intended move* that would create the third occurrence or reach `halfmove_clock >= 100` provides `CLAIM_DRAW_WITH_INTENDED_MOVE`. Claims do **not** make the position terminal and score `Finite(0)` only if selected.
5. If remaining nominal depth is zero, obtain the static score by constructing a current-position snapshot through M1+M2 and calling `Evaluator::evaluate`; compare that continuation value against any available claim as the side to move would. At positive depth, enumerate all legal moves (or prune by exact alpha-beta bounds) and recursively evaluate continuations, including optional claims. Never treat check alone as an extension.

For optional draws the player prefers a draw when its score `Finite(0)` is **at least as favorable** as continuation. Therefore White selects max of continuation and claim; Black selects min. On exact ties, prefer claims to play, with deterministic order for multiple claims: `CLAIM_DRAW` before `CLAIM_DRAW_WITH_INTENDED_MOVE`, and lexicographically earliest intended move for same-kind claims. An intended-move draw claim is **not** equivalent to making that move and giving the opponent a turn; the claim is lodged before it is played. Verify intended move legality before treating it as an option. This search policy applies to hypothetical nodes as well as the root.

At depth zero, a nonterminal node with a claim **still must** compare the claim against its static continuation score; claiming unconditionally is incorrect. As an optimization, an already proven bound may sometimes avoid evaluation while preserving exact root results; the initial reference implementation may simply evaluate it.

**Explicit limitation:** M3 does not completely recognize exotic FIDE-dead positions that require analysis of possible future legal sequences. It must never return draw on the basis of a merely plausible insufficient-material heuristic. Other supported draw rules remain mandatory.

## 6. History and FIDE-equivalent repetition (D43)

Define an input `GameHistory` (or equivalently named immutable record) that contains the canonical position **occurrences**, including the current root, plus a marker indicating whether all occurrences since the start of the game are known. An imported FEN without earlier positions is represented as a *known current occurrence with incomplete preceding history*; never invent earlier matches. Provide clear construction helpers for replaying actual legal moves, recording current root, and validating the history's last position against the root (including clocks for root consistency). History is not passed to the evaluator. The controller will be the eventual live authoritative owner (M6).

The search uses a private, reversible `SearchHistory` derived from that input. At each hypothetical `make_move`, append the child occurrence **once**; on `undo_move`, remove it exactly once. The pre-root occurrences are never lost or mutated. Ensure snapshot/repetition updates and move restoration are exception-safe. Count every occurrence, not merely positions on the current principal variation.

**Repetition equivalence:** piece placement, side to move, castling rights, and **whether a legal en-passant capture is actually available** (and which EP target) must match. Exclude halfmove/fullmove clocks and raw EP metadata when no legal EP capture exists. M1's `repetition_key` may serve as a fast bucket, but verify matches using a collision-resistant **exact canonical signature**, e.g., 64 piece codes + side + four castling flags + a normalized legal EP marker. Derive the legal EP marker using ChessCore legality (the mere presence of raw FEN EP is insufficient; a pinned capture can be illegal). Never adjudicate on a 64-bit hash match alone.

Past and hypothetical occurrences are combined when computing immediate threefold/fivefold results and intended-move threefold claims. A prospective move used only to check claim eligibility must be undone and must **not** remain in the history unless normal minimax traversal later chooses to explore it. Fifty-/seventy-five-move tests use ChessCore's authoritative uint32 halfmove clock after each hypothetical move; do not estimate it from move numbers. If earlier occurrences are unknown, refrain from asserting repetitions that cannot be proved; surface the incompleteness so users do not mistake an imported FEN for a fully adjudicable game.

## 7. Alpha-beta and deterministic choice (D44–D46)

- Search legal moves in a documented stable order, e.g. lexicographic order of `ChessCore::encode_move(position, move)` including promotion suffix. If distinct legal moves can share a string, treat that as a contract error rather than accepting ambiguous ordering. No randomization or speculative heuristic ordering in M3.
- Preserve the **first** move among exactly equal scores in this stable order; optional claims win ties over any play action. Use exact binary64 comparison (not arbitrary epsilon) for ties. Evaluate available claims as candidate actions with precise tie priority.
- With alpha-beta disabled, traverse every needed legal continuation through fixed depth. With it enabled, introduce valid alpha (White lower) / beta (Black upper) bounds, use cutoff when `beta <= alpha` in **the current node's comparable score coordinates**, and preserve exact minimax root score and decision. Each fresh search initializes unbounded endpoints. No TT or persistent pruning history.
- A pruned node can yield an upper or lower bound rather than its exact minimax value. **Do not present such a bound as an exact alternate root score or PV.** Design PV and root selection so pruning-on-equality does not change the specified first-best and claim-on-tie policy. In particular, claims can establish a zero bound before exploring continuations. A root candidate that is only proved unable to improve on the incumbent must not replace it.
- Include a correctness-oracle mode with alpha-beta disabled. Verify optimized and exhaustive search yield the same optimal root score **and selected decision**, not just same numeric score. Validate both White and Black, extreme finite scores, mate distance ties, and claims.

Search statistics must separate at least visited nodes, evaluator calls, alpha-beta cutoffs, and elapsed wall-clock time; counter and duration semantics must be documented. With pruning off, alpha-beta cutoffs must be zero. Benchmarks should compare both modes at equal depth with one evaluator identity and environment; do not infer universal performance factors from a single position.

## 8. Failures, transactional behavior, and evaluation identity (D37)

Before search, require an active usable evaluator and capture its identity/fingerprint. Every static leaf evaluates through the existing M2 host. If it reports any `EvaluationError` (explicit module failure, invalid status, NaN, infinity or already-quarantined instance), stop the **entire** search, discard all candidate decisions/scores/PVs, and return `EVALUATOR_FAILED` with diagnostic. Do **not** use the highest-ranked earlier move, partial-depth results, legal fallback, substitute evaluator, or opening book. The M2 instance remains quarantined until a successful replacement. There is no hot evaluator swap during one search.

The caller-owned root and supplied actual history must remain unchanged under success, failure or unexpected exceptions. Treat invalid search parameters/history separately from evaluator failures. Non-evaluator traversal/contract exceptions become `SEARCH_ERROR` (or a documented propagating API error), never `EVALUATOR_FAILED`; do not silently return a play decision. Use reliable LIFO make/undo cleanup and private root/history copies. Recoverable errors must not hide lost undo state. An aborted search never publishes a completed result.

The result records the evaluation identity and configuration, not a fabricated centipawn mapping. All tests use finite White-positive evaluation scores in the module's native units. Stable results are expected for identical inputs/build/provider without a cross-platform bitwise-FP promise.

## 9. Profiling and measurement readiness (D49)

The engine must support **both** reproducible benchmarks and sampling-profiler inspection, without changing chess behavior, the evaluation ABI, or hot-path semantics.

- Add a documented native `native-profile` CMake preset (or equivalently named configure/build/test presets) using optimized `RelWithDebInfo` or an equivalent optimized configuration with usable debug symbols. Preserve frame pointers with `-fno-omit-frame-pointer` for GCC/Clang where supported, applied consistently to code being profiled. Use toolchain-appropriate approaches elsewhere; do not assume GCC flags work with MSVC. Keep existing Debug, Release and WASM configurations intact.
- Make the exact profiling build reproducible via repository commands; CTest must work under that configuration. Do not force `-O0`, globally inhibit inlining, add sanitizers or enable tracing simply to make a profiling build. Inlined frames may be presented by a capable profiler from debug information.
- Provide a deterministic, headless native search benchmark executable or mode with stable workload inputs: FEN plus supplied occurrence history, evaluator module/parameters or fingerprint, fixed depth, alpha-beta mode, warm-up/iterations, and a consumed result/checksum. Record the build/compiler and platform, visited nodes, evaluator calls, cutoffs, per-run elapsed time and relevant workload identifiers. No network or GUI dependency.
- Keep standard M3 aggregate search counters inexpensive and precisely defined. Optional detailed tracing, per-phase clocks, allocation tracking or event markers must be explicitly enabled and have negligible/no work in normal benchmark builds; **do not timestamp or log every search node by default**.
- Preserve distinguishable source-level boundaries for legal move generation, make/undo, draw/repetition checks, snapshot conversion, evaluator dispatch and recursive search. Optimizations may inline functions; do not require artificial no-inline attributes or freeze implementation layout.
- Document how to attach an external native sampling profiler (for example Linux `perf`, VTune, Windows Performance Analyzer or macOS Instruments where supported) to the headless benchmark, and how to interpret CPU time, call stacks, allocations, cache misses and branch misses. Hardware counters and external tools may be unavailable in CI or restricted environments; do not fabricate such measurements or make them mandatory acceptance gates.
- Differentiate **normal Release performance benchmarks** from instrumented/traced runs or a profiling build; record compile flags, profiler overhead, warm-up and run conditions. A profiling build is for attributing costs, not automatically the canonical throughput baseline. WASM/browser source-map profiling and device-specific profiler validation may be added later; Node/WASM correctness remains a separate gate.

## 10. Required tests and acceptance gate

**Reference/oracle testing.** Build an independently readable exhaustive reference minimax for small synthetic deterministic trees and selected genuine chess positions. Compare alpha-beta on/off exact root score and selected action, including first-best ties, claim ties, White/Black perspective, strict bound cutoffs, mate preference (faster victory/slower defeat), and finite values above any traditional mate sentinel. Assert each returned PV is legal and ends in a position consistent with the returned decision/termination. Validate root-to-child mate increments and score comparison edge cases (negative zero; reject NaN/infinity).

**Rules testing.** Root and depth-zero checkmates, stalemates, known-dead cases, 50/75 clock thresholds and reset by pawn moves/captures, exact third/fifth repetitions, pinned/noncapturable EP normalization, castling-right differences, and collision-resistance (inject an intentionally colliding hash in a synthetic ChessCore test). Cover threefold and 50-move claims **before** an intended legal move, and ensure such a move is not applied when claiming. Verify checkmate overrides 75-move termination, claim ties, and automatic-versus-optional draw distinctions. Test imported-FEN history with unknown prefix and successful validation of root history consistency.

**Failure testing.** Controlled host ABI provider fails on a later evaluation call after earlier candidate moves have been examined; induce explicit statuses, invalid status and nonfinite values through M2 where practical. Assert `EVALUATOR_FAILED`, empty decision/score/PV, quarantined instance, diagnostic, no fallback, unchanged source root/history, and no retained hypothetical frames. Exercise non-evaluator traversal errors independently. Confirm subsequent search with replaced evaluator succeeds. No C++ exceptions cross the C ABI.

**Portability/performance.** Integrate with existing CMake/CTest presets: native Debug, native Release, the D49 optimized native profiling preset, and WASM Debug under pinned Emscripten 4.0.10 / Node and GitHub Actions. Existing M0/M1/M2 and ABI layout tests remain green. Add at least one Release benchmark with reproducible FEN/history, evaluator fingerprint, depth, pruning mode, node/evaluation/cutoff counts, elapsed duration, hardware/compiler/toolchain, and limitations. Run sanitizers where practical; do not claim untested browser, Android, iOS, Windows or macOS success.

Document real commands and measured results in `docs/m3-validation.md`, including the profiling preset, repeatable profiler command/workload, observed Release benchmark baseline, and any known limitations or departures from the approved spec. No simulated passing CI and no speculative performance percentages.

## 11. M3 completion boundary

M3 is complete when a new C++ search library API is exercised by independently verified native and WASM tests; all exact-score, optional-claim, history, deterministic-choice, evaluator-error and restoration requirements pass; and the validation report and benchmarks are submitted for review.

**Explicitly deferred:** iterative deepening / deadlines / cancellation / MultiPV / persistent TT / ordering heuristics / quiescence / extensions / opening-book integration / UCI / GUI / controller. D42A reserves root-book architecture but does not license implementation during M3. Full dead-position detection beyond proven cases is an accepted documented limitation (D47). Stop after a reviewable M3 PR; do not merge or start M4.