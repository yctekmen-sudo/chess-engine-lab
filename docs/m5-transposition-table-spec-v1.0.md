# Chess Engine Lab — M5 Transposition Table Specification v1.0

**Status:** APPROVED BY USER — architecture decisions D67–D83 and specification reviewed on 2026-10-20. The separate Codex kickoff is also approved; implementation remains gated on incorporating both documents into `main`.  
**Milestone:** M5 — optional, bounded, reusable transposition table for exact-depth iterative alpha-beta search.  
**Authority:** Extends `docs/architecture-v1.0.md`, `docs/m3-search-spec-v1.0.md`, `docs/m4-search-spec-v1.0.md`, and the shipped C++20 headers `engine/include/chess_lab/search.hpp`, `engine/include/chess_lab/evaluation.hpp`. M1–M4 semantics and evaluator C ABI remain authoritative where unchanged.  
**Scope:** Search-library TT facility, minimal controller-style ownership/failure harness, native/WASM correctness and performance tests. Not the full UI/controller, concurrent TT access, disk persistence, pondering execution, selective search, or new evaluator ABI.

## 1. Binding decision register

| ID | Approved requirement |
|---|---|
| D67 | Optional bounded TT, disabled by default. Identical exact decision, score, canonical PV, draw treatment and MultiPV to M4 at equal search context/depth. Eviction affects only performance. |
| D68 | Separate fully validated score/bound reuse from position-valid legal move-order hints. Hash matches alone never prove numeric compatibility. |
| D69 | Incremental composite fingerprint for position and relevant history/rule context; on candidate hash hit verify exact canonical repetition-signature counts, including hypothetical descendants, and all relevant state before numeric reuse. Cache key maintenance must avoid full-history scans on every node. |
| D70 | Numeric TT reuse requires **equal** remaining nominal depth, not stored depth >= requested depth. Other depths may provide hints. |
| D71 | TT entries classify as EXACT, LOWER_BOUND or UPPER_BOUND by the node's **original** alpha-beta window. Sound bound cutoffs/tightening only. Never publish bounds as exact MultiPV/PV. Exhaustive reference never prunes from TT. |
| D72 | Store mate distances node-relative; convert safely to/from M4 root-relative SearchScore at each TT access. |
| D73 | Controller-owned, reusable, bounded, in-memory TT; lifetime of owning controller, usable across sequential searches and games. Initial single-active-search-per-instance rule; no disk persistence. |
| D74 | Numerical value/bound reuse requires exact M2 evaluator fingerprint and compatible search-value policy; incompatible entries may supply legal ordering hints. Controller can clear at will; correctness does not rely on clearing for evaluator changes. |
| D75 | Strict total TT-owned memory budget, including retained history/context evidence; deterministic recency/depth-aware replacement, potential coexistence of entries at multiple depths, clearing/resizing between searches, cache statistics. |
| D76 | Compact optional best-decision metadata; reconstruct canonical exact PV through verified links or re-search/refine when proof is unavailable. Intended draw-claim move is not played. |
| D77 | Fully established exact/bound entries from a search normally cancelled mid-iteration may remain; incomplete nodes cannot produce valid cache entries. M4 committed-depth result unchanged. |
| D78 | **Controller**, not search engine, clears its supplied TT on EVALUATOR_FAILED or SEARCH_ERROR after search returns. No write generations, logs, transaction rollback or in-search clearing. Normal cancellation retains valid entries. |
| D79 | Separate TT-off, cold-cache and warm-cache experiments; explicit cache-state reporting, statistics and reproducible comparisons. |
| D80 | Numerical caching requires evaluator explicitly certified cache-compatible: deterministic function of all declared inputs. Otherwise ordering-hint-only use. Prefer no M2 C ABI change. |
| D81 | Exhaustive alpha-beta-disabled reference ignores TT completely: no reads, hints, writes or pruning. |
| D82 | Terminal positions, automatic draws, immediate claims and intended-move draw claims must be validated against exact rule/history context, with distinct ChessDecision types and existing canonical tie order. |
| D83 | Mandatory exact result equivalence and adversarial testing, plus native/WASM and cold/warm benchmarks; measure improvements or regressions with no arbitrary first-release speedup threshold. |

## 2. Non-negotiable semantic invariants

1. For identical root `Position`, matching `GameHistory`, evaluator state/configuration and policy, options, nominal depth, and cancellation-free completion, M5 TT-enabled outputs must match the TT-disabled M4 output: best `ChessDecision`, complete `SearchScore` (including mate distance), PV, `PVEnd`, intended-move metadata, and every exact ranked MultiPV entry. Search node counts, evaluator calls, elapsed time, cache counters and cutoff counts may differ.
2. Preserve M3 `search(...)` and M4 `iterative_search(...)` source compatibility and defaults. A TT-free invocation is the default, and disabling TT incurs no logical behavior changes. M5 may add C++20 overloads or trailing defaulted parameters where source-compatible; never alter the M2 C ABI.
3. All numeric scores are White-perspective, with existing `SearchScore` ordering; White maximizes, Black minimizes. No conversion to centipawns or negamax. Canonical decisions rank immediate claim, then intended-move claim, then lexicographic played move on exact ties, subject to existing M3/M4 applicability.
4. A hash is **only an indexing prefilter**, not authoritative proof of position, history, evaluator or depth identity. On every numeric candidate hit, compare exact canonical state and history-context evidence. Deliberately forced hash collisions must be harmless.
5. No incomplete, cancelled, failed or unproven bound is ever promoted to EXACT, nor may an incomplete/cached bound be published as an exact MultiPV score, PV or finished depth result.
6. Search/root/history copy, mutation/undo and evaluator quarantine/error semantics are unchanged. The TT is a performance facility, not a second source of chess rules or a substitute for `Evaluator::usable()` checks.

## 3. TT ownership, API, defaults, and controller boundary

**Controller ownership:** The controller (or test harness standing in for it) constructs a TT instance, sets its capacity, passes it optionally to a search, may clear/resize it between searches, and destroys it when the controller instance ends. The table can span games, evaluator selections, and search sessions. Only one active search may use a given TT at a time; passing the same instance concurrently is a contract violation handled predictably (e.g. explicit error / checked guard). No global singleton and no disk serialization.

**Default:** TT disabled unless caller supplies/enables a nonzero-capacity table. A zero-capacity table behaves as disabled. Existing callers require no changes. Retain the independent `search()` exhaustive mode and the `iterative_search()` alpha-beta-disabled mode as reference oracles, both without any TT access. At least iterative alpha-beta must support TT; optional fixed-depth alpha-beta integration must not modify legacy caller behavior or oracle independence.

**Public C++ shape (illustrative, not a mandated spelling):** A noncopyable `TranspositionTable` with explicit byte capacity, `clear()`, `resize()` when idle, `capacity_bytes()`, and an optional search attachment. Add optional evaluator cache-compatibility metadata at the search/controller interface without modifying M2's evaluator ABI. Explicit compatibility must be conservative by default; a mere fingerprint does not certify deterministic behavior. Public APIs should make accidental use of an incompatible evaluator for numeric caching difficult.

**Controller failure protocol:** After a search returns `EVALUATOR_FAILED` or `SEARCH_ERROR`, the controller clears **only the TT supplied to that search** before subsequent reuse. No cache rollback/undo-log generation mechanism. For `CANCELLED`, `DEPTH_REACHED`, `DEPTH_LIMIT_REACHED`, and `TERMINAL`, retain fully validated entries. M3 `SearchStatus` equivalents follow the same policy when applicable. Harness tests must verify the owner performs clearing; the engine itself reports the failure but does not clear the TT. If the application fails to follow the protocol, its cache state is not certified after an error. Quarantined evaluator instances remain unusable regardless of TT contents.

## 4. Context identity, incremental hashing and exact verification

**Position identity:** On candidate numeric reuse verify complete position state that can affect rules or evaluator output. Include piece arrangement, side to move, castling rights, effective legal-en-passant information, halfmove clock, and any evaluator-visible snapshot field (notably fullmove number and raw EP encoding, where relevant). Do not assume repetition equivalence alone makes evaluation inputs equivalent. Exact equality may compare canonical structs/fields, not padding bytes.

**History identity:** Preserve the M3/M4 `GameHistory` semantics, including its `complete()` bit and every currently accumulated `RepetitionSignature` occurrence, updated for each hypothetical make/undo. **Exact occurrence-count maps** over canonical repetition signatures—not their original chronological order—are sufficient for the rules tracked by M3/M4, provided current position, current halfmove clock and all other rule/evaluator-visible state also match. Verify exact equality of those count maps before numerical reuse; verify normalization of legal EP and intended-move claim eligibility. Do not let an incomplete recorded history masquerade as a complete one. Revisit this equivalence requirement if future rules depend on chronological path beyond counts.

**Fast indexing:** Maintain an incremental composite fingerprint of position and history multiset/count state (and history completeness/rule clocks) as legal moves are made/undone; updates should be proportional to the modified occurrence(s), not total history length. Lookups with no matching fingerprint avoid expensive exact comparisons. On candidate matches, compare exact state and canonical count evidence. Use memory-bounded stored representations (e.g., immutable/sharable verified history contexts where safe); never allocate unbounded per-entry snapshots or rebuild the entire history vector on every recursive node. Benchmarks must report hit-verification cost. Any colliding fingerprint must be rejected unless exact equality holds.

**Move hint identity:** Separate position-only hint lookup from history-sensitive numeric lookup. A hint from a different history, evaluator or remaining depth is permitted only after checking the candidate is a currently legal played move (or a currently eligible claim, represented separately); the hint itself carries no authoritative numerical assertion. Hints may change traversal order but cannot change canonical completed results.

## 5. Numeric compatibility, search horizon and evaluator behavior

Before reusing an EXACT/LOWER_BOUND/UPPER_BOUND value, require **all** of:

- Exact position and rule/history context from §4, including root-to-node hypothetical history, complete/incomplete flag, and evaluator-visible inputs.
- Stored remaining **nominal depth equals requested remaining depth**, including depth zero. Never use a depth-greater-or-equal shortcut; multiple depths may coexist when capacity allows.
- The exact M2 `EvaluationIdentity::fingerprint` and an explicit cache-compatible evaluator certification, with all evaluator inputs accounted for. A changed artifact, canonical configuration or relevant behavior state invalidates numerical reuse automatically. No fingerprint-only assumption that a random/stateful evaluator is cache-compatible.
- A compatible value-defining search policy/version: uniform-depth semantics, terminal/claim handling, score conventions, horizon behavior and any other policy affecting exact minimax values. Ordering toggles, MultiPV N and whether a known-exact value was obtained with alpha-beta need not invalidate a mathematically identical value; however TT bounds cannot induce pruning in exhaustive reference mode.
- Valid entry kind and mathematically sound alpha-beta window use as in §6.

For non-cache-compatible evaluators, forbid numerical TT reads/writes; validated legal ordering hints are permitted per D80. **Important limitation:** Stochastic/stateful evaluators can produce different results as call order changes. M5 cannot promise identical numerical outputs for such evaluators across different ordering strategies; determinism/equivalence acceptance checks apply to cache-compatible evaluators under identical declared inputs. A controller seeking strict experiment comparability should disable *all* TT interaction (including hints) for stochastic evaluators.

## 6. Entry kind, bound semantics, alpha-beta, MultiPV and PV proof

An entry may contain an exact value or a valid fail-high lower bound / fail-low upper bound. Classify against the **original incoming node alpha/beta window**, before any tightening by TT probes or local draw-claim baselines. Preserve inclusive/exclusive comparisons consistent with `SearchScore` and existing alpha-beta implementation; equality-sensitive ties must be refined rather than guessed from a bound.

- **EXACT** may replace subtree score computation only when §5 compatibility holds. It does not automatically prove the complete canonical PV. Check PV/decision evidence or re-search/refine as needed.
- **LOWER_BOUND** and **UPPER_BOUND** may tighten or close a compatible alpha-beta window only with a mathematically valid proof. Never return them as exact terminal/MultiPV scores or use them to declare a canonical tie.
- Before publishing an exact root MultiPV entry, establish its score and canonical PV through an exact chain or additional full-window searches/refinement. M4 evaluates every root decision to its exact score for ranking; M5 must preserve that behavior, including alternatives outside final top N when their value is needed to rank candidates.
- A compact best-decision hint is allowed. Following a stored played move requires legality and complete compatibility at each node; following a claim requires re-validation of current claim eligibility. A partial chain, incompatible context, missing entry, evicted continuation, bound-only step, or unresolved tie mandates re-search/refinement.
- A cached decision must not beat a canonically earlier equal-scoring alternative merely because it was visited first. Intended claim moves are **announced but not played**; never append them to played-move PV.
- The implementation must document safe entry insertion points after subtree outcomes are established; no speculative score/bound write while recursion is incomplete or during unwind.

## 7. Mate-distance normalization and safety

M3/M4 use **root-relative** `SearchScore::white_mate(d)` and `black_mate(d)` internally. TT entries instead use **node-relative** distances to make valid numerical reuse possible when the same position is reached at different plies.

For a proven mate score at current ply `p`, on insertion store `d_node = d_root - p` **only if** the subtraction is valid and represents a bound/value that can soundly be expressed relative to that node. On lookup reconstruct `d_root = d_node + p` with checked overflow. Handle WhiteMate and BlackMate symmetrically in distance transformation, without changing White-perspective score ordering. Finite scores are unchanged. **Do not cache** any transformed value/bound for which subtraction, addition or bound semantics is unsound (notably an inherited ancestor-relative window endpoint that does not express a valid node-local result). A numeric hint or re-search is preferable to an invalid normalization.

Tests must include the same position reached at distinct root plies, both mate colors, different distances and bound-classification boundary cases.

## 8. Draw claims and terminal states

Respect the existing precedence/order and all M3/M4 FIDE behavior: checkmate, stalemate, known dead positions, automatic fivefold/75-move rules and optional immediate/intended-move threefold/50-move claims. A TT entry representing such a result is reusable numerically only after exact §4/§5 verification and current rule legality re-check where necessary.

Maintain `ChessDecision` distinctions: `play`, `claim_draw`, `claim_draw_with_intended_move`, `terminal`. Intended move is a legal **unplayed** announcement; do not alter the current board/history during PV reconstruction. No cached played-move hint may hide an eligible claim that wins canonical tie-breaking. Terminal root behavior remains `TERMINAL`, completed depth zero, without fabricated PV or MultiPV.

## 9. Memory management, replacement and observability

The controller configures a fixed maximum **total TT-owned bytes**, counting bucket/entry storage, owned exact-context evidence and any retained auxiliary allocations; do not hide unbounded history verification data outside the budget. Define and report actual capacity allocation/rounding. Zero means disabled. `clear()` and `resize()` are allowed only when table idle; all operations must leave it valid. No fixed TTL. Table lifetime equals controller-instance lifetime; each entry survives until eviction, clear, resize or destruction.

Replacement is deterministic under identical operation sequences and considers recency and remaining depth (with no depth-monotonic correctness assumption, per D70). A bounded multi-entry/bucket design may keep several depths for a context. Do not prescribe a particular bucket count, scoring heuristic or eviction formula before profiling; document chosen one. Eviction is always correctness-neutral. Track per-invocation statistics distinguishing lookups, prefilter candidate hits, exact context/compatibility verified score hits, valid bound hits, hint-only hits, stores, replacements/evictions and verification misses as practical; specify precise counter meanings. Preserve M4's existing `SearchStatistics` fields and semantics, extending source-compatibly.

## 10. Cancellation, failure and exclusive use

Check cooperative cancellation at existing M4 boundaries even during cache probes/history verification and before expensive re-search. Cache-hit fast paths cannot bypass cancellation or terminal/evaluator usability checks. Fully established subtree exact results and valid bounds from an interrupted iteration may remain in TT; its **externally published** result still follows M4's last committed complete depth or unevaluated legal fallback. Do not store incomplete nodes. On evaluator failure or other SEARCH_ERROR, published progress/results remain invalidated by M4; after the search returns, the controller clears the attached TT (D78). Do not attempt transactional rollback or speculative partial recovery.

Search invocation is synchronous; only one active search per TT instance. The controller must await return before clearing, resizing, or reusing the instance. Native and WASM must compile under existing platform profiles without assuming browser Worker cancellation guarantees. No TT global mutable state or cross-controller sharing contract.

## 11. Correctness tests and adversarial fixtures

**Mandatory:**

1. Legacy M0–M4 tests green, C++ source compatibility and unchanged M2 C ABI; TT-off outputs unchanged.
2. TT on/off equivalence for best decision, structured exact score, full canonical PV/end/intended metadata and **every ranked MultiPV line** at same depth. Include White/Black, mate races, signed zero/ties, varying previous-PV ordering, varying MultiPV N, root claims, horizon claims and history completeness.
3. Exhaustive oracle ignores TT entirely even when a populated table is supplied; verify no TT reads/writes/hints via counters or instrumented table.
4. Cross-game warm-cache hits with compatible position/history; distinct histories with matching boards and intentionally adversarial halfmove clocks, fullmove/evaluator-visible fields, legal-EP normalization and incomplete histories; forced collisions and exact-verification rejection.
5. Equal-depth requirement, distinct-depth entries, evaluator fingerprint mismatch, search-policy mismatch, cache-compatible certification off, and move-hint legality/tie invariance.
6. Mate distance normalization across different root plies, both winners, score/bound transforms, subtraction/overflow rejection, and canonical PV after reuse.
7. Tight/fail-high/fail-low windows, original-window classification, EXACT vs bounds, draw-claim-induced window changes, root MultiPV full-window refinement and missing/evicted PV link fallback.
8. Strict byte capacity with variable history evidence sizes; zero/small capacity, collisions, replacement determinism, resize/clear, multiple searches/games, exclusive-use violations.
9. Cancellation before first depth and mid-iteration; show only completed subtree entries survive, last committed result/fallback unchanged. Evaluator failure after prior progress or cache writes, callback exception and other errors: controller harness clears supplied TT and retains independent tables.
10. Native debug/release/profile and pinned Emscripten/Node WASM build/test paths, including deterministic in-WASM cancellation fixtures. Do not misrepresent these as proof of live browser postMessage responsiveness.

Use an alpha-beta-disabled, TT-free exhaustive reference on tractable positions/depths. Any reproducible result mismatch is a correctness failure. If a cache optimization cannot meet invariants, reduce its hit rate or re-search rather than relax correctness.

## 12. Benchmarks and handoff evidence

Measure TT-off, TT-on cold, and TT-on warm under matched positions, evaluator build/config, effective history, nominal depth, MultiPV setting, ordering toggles, compiler/toolchain and platform. Report wall-clock time, visited nodes, evaluator calls, alpha-beta cutoffs, TT hit categories, stores, evictions and context-verification overhead. Benchmark both beneficial and adversarial workloads: repeated positions, no-transposition trees, expensive histories, forced collisions and small memory caps. State when warm caches were populated and how capacity was configured. No numerical speedup threshold is required for the first iteration; transparently document regressions and opportunities for improvement.

Provide `docs/m5-validation.md` with reproducible commands, test runs, CI links, representative measured values and limitations. Use existing CMake presets for native-debug, native-release, native-profile and pinned wasm-debug environment.

## 13. Implementation latitude, boundaries and review gate

Codex may choose TT public type spelling, bucket structure, incremental history-count map implementation, exact context verification representation, capacity rounding, replacement heuristic, PV-refinement strategy and extension fields as long as the above requirements are satisfied and performance costs are measured. The specification does not require an elaborate LRU structure or cryptographic context hash. Use test injection for collision scenarios.

**Out of scope:** disk-backed cache, global cache, simultaneous TT access, pondering execution, worker transport, search parallelization, evaluator C ABI change, opening book, quiescence, extensions, selective depth or new chess rules. Future pondering remains tracked under [GitHub issue #6](https://github.com/yctekmen-sudo/chess-engine-lab/issues/6); selective search concerns under [issue #5](https://github.com/yctekmen-sudo/chess-engine-lab/issues/5).

**Review gate:** This specification is approved. The separate `docs/codex-kickoff-m5.md` requires review and approval; both approved documents must be incorporated into `main` before Codex starts implementation on a feature branch. Implementation ends at a PR for explicit code review. Never self-merge.

## 14. Review attention: two deliberate implementation risks

1. **Efficient exact history verification:** An incremental fingerprint alone is insufficient for D69, while copying full history into every entry can nullify caching gains or violate D75. The implementation proposal must explain bounded exact occurrence-count evidence and realistic hit verification cost.
2. **PV/bound correctness:** Existing M4 obtains exact root MultiPV through independent full-window candidate searches and uses root-relative mate scores. Cached bounds and best-move hints must not shortcut that proof, especially under equality ties or different root plies. Fall back to exact re-search where necessary.