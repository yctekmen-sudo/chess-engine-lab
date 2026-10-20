# Chess Engine Lab — Architecture Specification v1.0

**Status:** APPROVED architecture baseline; implementation not started  
**Date:** 2026-10-19  
**Supersedes:** `chess-engine-architecture-v0.1.md`  
**Target platforms:** Windows, macOS, Linux, iOS, Android (standalone and offline for core chess functions)

> **Purpose.** Build a FIDE-compliant chess engine and experimentation platform in which the chess rules, static evaluator, and search policies have well-defined, independently replaceable interfaces. Support interactive chess, analysis, external engines, and reproducible automated matches, without requiring network access for core functionality.

## 1. Scope and guiding invariants

1. **Rules are authoritative.** Standard orthodox chess under FIDE rules, including legal moves and the distinction between claimable and automatic draws. No Chess960 requirement for v1.
2. **Evaluation is interchangeable.** `E(P; parameters)` is an unrestricted, **finite** real-valued score from White's perspective, computed from a standardized read-only current-position snapshot, with no access to move history.
3. **Search is independently interchangeable.** Minimax with alpha-beta, iterative deepening, optional ordering and caching, and configurable quiescence policies; the evaluation module does not implement search or adjudication.
4. **Only completed-depth results are authoritative.** A partially explored search iteration must not determine the played move, draw decision, or published final ranking.
5. **Portability is architectural.** One C++ search/rules/evaluation-contract codebase builds as native software and WebAssembly; platform-specific orchestration and plugin loading are adapters.
6. **Experiments are reproducible and auditable.** Every match/benchmark records its evaluation artifact, parameter values, search configuration, initial position, versions, and measurement conditions.
7. **Offline-first.** Engine search, local play and analysis function without internet on all target platforms. Mobile may limit which external engines/evaluation binaries can be installed.

### Outside initial scope

Online matchmaking, accounts, cloud computation, distributed search, custom chess variants, arbitrary downloaded executable plugins on iOS, multithreaded search, and calibrated Elo difficulty ratings. Architecture may permit these later.

## 2. Decision register — all 31 decisions

**Finalized** means previously agreed. **Experimental** means the goal is agreed but the algorithm or threshold remains intentionally open. This table preserves the numbering established during design discussions.

| # | Status | Decision and binding requirement |
|---|---|---|
| 1 | Finalized | Evaluator sees the current position only; history-dependent outcomes belong to search/controller. |
| 2 | Finalized | Static scores are unrestricted finite real values, positive for White; terminal outcomes conceptually ±∞ and draws 0. |
| 3 | Finalized | Main search is minimax with alpha-beta pruning. |
| 4 | Finalized | Iterative deepening stops at maximum completed nominal depth or hard time deadline, whichever comes first. |
| 5 | Finalized | Best Move (`MultiPV=1`) or ranked top `N` (`MultiPV`, normally 3); configurable selected rank. |
| 6 | Finalized | Nominal depth is in plies and may be exceeded by configured search extensions. |
| 7 | Finalized | Optional, memory-bounded transposition tables with validity and configuration safeguards. |
| 8 | Finalized | Modular, configurable move-ordering heuristics; no change in exact completed search value. |
| 9 | Experimental | Two-ply evaluation-driven instability probing and adaptive extension, with details tuned empirically. |
| 10 | Finalized | Three quiescence settings: NONE, TRADITIONAL, EVALUATION_DRIVEN. |
| 11 | Finalized | Decisions and final move rankings come only from the last **fully completed** iterative-deepening depth. |
| 12 | Finalized | Time limit is a hard wall-clock search budget, including quiescence/probing; cancellation latency should be small. |
| 13 | Finalized | Standalone UCI-compatible native engine; separate universal controller manages outside engines. |
| 14 | Finalized | Chess engine contains no graphical-interface code. |
| 15 | Finalized | Desktop UI is browser-based with a local backend. |
| 16 | Finalized | Evaluators are independently compiled and runtime-selectable; desktop loads shared native libraries. |
| 17 | Finalized | Evaluators expose module-specific parameter schemas/defaults and independent instances. |
| 18 | Finalized | Evaluator input is a versioned, read-only standardized position snapshot, not library-native objects. |
| 19 | Finalized by #21 | C++ core, native build, React/TypeScript frontend, Node.js/TypeScript desktop backend (see #21). |
| 20 | Finalized | iOS/Android applications run the engine locally and offline. |
| 21 | Finalized | C++20, CMake, Emscripten, React/TypeScript, Node.js/TypeScript, Capacitor; native desktop and mobile WASM targets. |
| 22 | Finalized, validation-gated | Start with Disservin/chess-library (MIT), wrapped in replaceable ChessCore adapter; validate native/WASM. |
| 23 | Finalized | Versioned C ABI for compiled evaluators; desktop dynamic loading; mobile ships bundled, runtime-selectable compiled evaluators. |
| 24 | **Deferred experiment** | Compare unconditional two-ply probes with conditional probes triggered by significant evaluation change at the depth boundary. Do **not** freeze metric/threshold yet. |
| 25 | Finalized | Strict FIDE draw handling; optional draw claims chosen when at least as favorable as continued play. |
| 26 | Finalized | Raw evaluator scores internally; only emit UCI centipawns when a meaningful conversion is explicitly defined; mates reported separately. |
| 27 | Finalized | Unified cancellation and terminal handling; deterministic legal fallback when depth 1 never completes. |
| 28 | Finalized | Foreground-only mobile searches; cancel on backgrounding; autosave and safely restore games. |
| 29 | Finalized | Headless, reproducible automated batch matches and benchmarking designed in from the beginning. |
| 30 | Finalized | Core returns a generalized chess Decision: play, claim draw, claim draw with intended move, or terminal outcome. |
| 31 | Finalized | Internal structured search score: finite, WhiteMate(distance in plies), BlackMate(distance in plies); exact winning/losing order. |

**Interpretation:** Decision 24 is intentionally **not** a finalized algorithm. Decisions 9–10 establish a pluggable experimental policy and a stable baseline, not proof that the proposed heuristic improves playing strength.

## 3. Technology and deployment

| Layer | Desktop | iOS / Android |
|---|---|---|
| GUI | React + TypeScript in browser | Same React GUI packaged via Capacitor |
| Game/experiment controller | Node.js + TypeScript, headless capable | Local app/controller layer; no Node.js server required |
| Engine transport | UCI subprocess; localhost HTTP/WebSocket for GUI | Direct Web Worker ↔ WebAssembly messages/API |
| Chess engine | Native C++20 executable/library | Same C++20 core compiled with Emscripten to WASM |
| Evaluation modules | Independently compiled `.dll` / `.dylib` / `.so` | Bundled precompiled evaluators selected through an internal registry |
| Build | CMake + native toolchains | CMake/Emscripten for WASM; Capacitor mobile builds |
| Internet | Not needed for core functions | Not needed after installation |

**Platform design constraint:** Neither `SearchEngine` nor `ChessCore` may require Node.js, browser APIs, OS processes, network services, or the mobile filesystem. These dependencies belong to a controller/transport or loader adapter. Native UCI is a protocol adapter, **not** the core engine API.

```text
                 React / TypeScript GUI
                   /               \
       Desktop browser           Capacitor app
                |                     |
      HTTP / WebSocket         Worker message API
                |                     |
    Node.js local controller   On-device controller
                |                     |
             UCI IO            C++ core as WASM
                |                     |
    Native UCI process         bundled evaluators
                \_____________________/
                          |
               Shared C++ engine API
                          |
       +------------------+------------------+
       |                  |                  |
 ChessCore adapter   Search & policies   Eval ABI / snapshot
       |                                     |
 Disservin library                   Evaluator implementation
```

## 4. Interfaces and ownership

### 4.1 ChessCore abstraction

Expose **our own** opaque `Position`, `Move`, and reversible `UndoToken` (or logically equivalent types). No Disservin types reach search, evaluation ABI, UCI, or GUI.

| Operation group | Required capabilities |
|---|---|
| Create/load | Initial position, load/export FEN, clone/reset |
| Legal moves | Enumerate all legal moves; correctly handle castling, promotions, en passant, checks |
| Traverse | Make move and undo to bit-for-bit equivalent state; support efficient recursive search |
| Inspect | Side to move, in-check, piece placement, rights, halfmove/fullmove counters |
| Encode | Convert moves to/from unambiguous UCI-style coordinate encoding; SAN/PGN handled by suitable presentation/controller layer |
| Status | Checkmate, stalemate, dead position support, legal repetition/clock metadata |
| Identity | Keys for cache lookup and rules-equivalent repetition, with **documented differences** |
| Snapshot | Convert to versioned evaluator position snapshot without exposing internal layout |

A chess **position state** comprises board squares, side to move, castling rights, en-passant availability, halfmove clock, and fullmove number. **Game history** separately records preceding moves/position occurrences and is required for several FIDE draw decisions. FEN alone is insufficient to reconstruct that history.

The chosen library is the initial implementation, not the specification of correct FIDE behavior. Perft, round-trip, special-move, and draw tests validate the adapter; native **and** WASM acceptance gates are required.

### 4.2 Evaluation ABI

Static contract:

\[
E(P;\theta)\in\mathbb{R},\qquad\text{finite, White-positive.}
\]

**Platform-neutral conceptual exports:** `getModuleInfo`, `getParameterSchema`, `createEvaluator(parameters)`, `evaluate(instance, readOnlySnapshot)`, `destroyEvaluator`. Exact C-compatible function signatures, layout/alignment, string ownership, memory ownership, failure codes, and ABI version constants become implementation-level interface tests before module coding.

- Snapshot: fixed-width piece codes for 64 squares, active side, castling and en-passant metadata, halfmove/fullmove counters, schema/version; **no previous-move list**. Current-state counters are provided for completeness but draw adjudication belongs outside evaluation.
- Modules cannot mutate the original position and cannot return NaN or ±∞; failures must be explicit. Do not allow C++ exceptions or compiler-specific objects to cross the C ABI.
- Independent evaluators may hold initialized tables and parameters. A module can be selected per engine instance; avoid swapping while a search is active. Module/parameter change invalidates evaluation-dependent caches.
- Module fingerprint includes artifact/hash or exact build identifier, ABI version, and canonical parameter serialization for experiments/cache identity.
- For mobile, runtime **selection** is supported, not unrestricted on-device installation of newly compiled code. All initially supported mobile evaluators ship inside the app.

### 4.3 Search and score types

Search uses a tagged internal value, not an arbitrary large mate constant:

- `Finite(real)` — ordered numerically; zero also represents terminal draw when applicable.
- `WhiteMate(plyDistance)` — above every finite value, with **shorter** mate preferred by White and longer mate preferred by Black.
- `BlackMate(plyDistance)` — below every finite value, with **shorter** mate preferred by Black and longer mate preferred by White.

All scores use **White perspective**. Mate distances are normalized when stored/read in transposition tables, so reuse at another search ply preserves the same meaning. Any game-theoretic draw is evaluated as 0; optional draw claims are actions, not forced terminalization.

**Search output, not just move output:**

```text
SearchResult
  status: COMPLETE | CANCELLED_WITH_COMMITTED_RESULT | NO_COMPLETED_DEPTH | TERMINAL
  completedDepth: integer
  rankedMoves: [ { move, structuredScore, principalVariation } ]
  selectedDecision: PLAY(move) | CLAIM_DRAW | CLAIM_DRAW_WITH_INTENDED_MOVE(move) | TERMINAL(outcome)
  timingAndNodeStatistics
  evaluationIdentityAndConfiguration
```

The representation above is **conceptual**, not committed wire code. Its key invariant is that **a chess decision can be a draw claim or terminal outcome**, while UCI `bestmove` supports reporting a move (or `0000` when none), requiring adapter policy for draw claims. Do not silently invent a nonstandard UCI claim command.

### 4.4 Controller and GUI

The **controller** owns the authoritative live game, history, clocks, move legality verification, draw-claim adjudication, adjudicated outcome, engine processes/instances, PGN/FEN exchange, experiment run records, and storage. Search uses its own legal copies/context.

The **GUI** renders board, move list, analysis/PV, current configuration, time, committed and provisional depth, nodes, evaluation chart, game clocks, experiments, and import/export. Analysis mode never auto-plays moves; playing mode applies the controller-validated decision only when appropriate.

## 5. Search algorithm — specified behavior

### 5.1 Main search and optimizations

- Nominal `maxDepth` is measured in **plies**. Iterate depths 1, 2, ... until depth limit or hard deadline.
- At nonterminal depth 0 with `Quiescence=NONE`, return `E(P)`; earlier terminal outcomes have precedence.
- White selects the maximum score; Black the minimum. Alpha-beta pruning must preserve the exact fixed-depth minimax result when the search completes.
- Modular move ordering (TT move, promising captures/promotions, cutoff/history heuristics) can change efficiency but not the exact fully completed search value.
- Optional memory-bounded transposition entries store hash, search depth, bound kind (exact/lower/upper), score, best move, and validity context. History-sensitive claim results must not be inappropriately reused. Identical board keys do **not** imply equivalent repetition history.
- First version: **single-threaded search**, with asynchronous execution in an independent native process or WASM Worker to keep the GUI responsive.

### 5.2 Root ranking and selection

- `MultiPV=1`: optimize for best move only; `selectedRank=1`.
- `MultiPV=N`: complete a defensible rank ordering of top `N` moves (normally 3), with comparable fully searched scores and per-move principal variations; no ranking built from incomplete alpha-beta bounds.
- The selected move rank is applied only **after** the iteration is committed. When fewer than `N` legal moves exist, return only available legal moves and constrain `selectedRank` accordingly.
- The controller may claim an available draw if doing so is **at least as good** as continuation according to the latest completed-depth assessment. Claimed draws are scored 0. Claims depending on intended moves require a corresponding legal intended move and independent claim verification.

### 5.3 Deadline, cancellation, completion

- `maxTime` is a hard **search compute budget** measured using a monotonic clock; applies to the main search, quiescence, and two-ply probes. Time-only, depth-only, or both are supported for appropriate modes.
- Stop promptly when the deadline expires or a stop/cancel request arrives. Minor OS/runtime/callback latency is acceptable, but no intentional finishing of an over-budget iteration.
- **Atomic commit:** only a *complete* search iteration replaces the committed result. Incomplete current-depth results may appear as *provisional diagnostics*, never as the final action/ranking.
- If cancellation occurs during depth 7 after depth 6 committed, use depth 6. If depth 1 never completes, choose an **implementation-independent deterministic legal fallback** selected before search, labeled **unevaluated**, not “best.” If no legal move exists, report the terminal game state.
- Work from interrupted iterations may leave individually valid transposition entries, but cannot directly become the final decision or ranked result.
- Cancellation and app-lifecycle events do not auto-play a saved result without the controller's explicit play-mode decision.

### 5.4 Three quiescence modes

| Mode | Boundary behavior | Status |
|---|---|---|
| `NONE` | Stop at nominal depth, evaluate statically if nonterminal | Baseline, required first |
| `TRADITIONAL` | Selective tactical continuations (e.g., captures, promotions, required responses to check), with safety limits | Required comparison policy |
| `EVALUATION_DRIVEN` | Use evaluation-change signals and two-ply minimax probes to detect unresolved boundary fluctuations, independent of hardcoded move categories | Experimental policy, formula deferred |

**Experimental concepts for evaluation-driven mode:**

- Unconditional probe: evaluate every nonterminal depth-boundary position through a complete two-ply minimax lookahead; computationally equivalent to adding at least two plies to much of the tree.
- Conditional probe: first look for a significant change in static score associated with the final move on an examined continuation at the nominal depth boundary; invoke two-ply probe only at flagged positions.
- Candidate metrics: static change `|E(P_d)-E(P_{d-1})|`; along-probe PV fluctuations; comparison between static and two-ply minimax value. Thresholds in evaluator units.
- If further extension is warranted, re-search with adversarial move choice (do not blindly follow a previously selected PV). Limit further extension by configurable plies/time and safe terminal/check handling.

**Not yet decided:** exact trigger, full-versus-pruned probe, instability metric, stopping rule, probe cache reuse, and search-value semantics on a safety cutoff. These are **research questions**, not permission to implement arbitrary defaults silently. Any approach that always accepts a two-ply probe result even when 'stable' must be documented as a deeper effective baseline, not compared naively against `NONE` at nominal depth.

**Correctness:** In-check positions require legal evasions or recognition of mate; there is no legal “stand pat” assumption while in check. Evaluation-driven quiescence aims to repair immediate boundary instability, not to guarantee discovery of mate combinations beyond main depth.

## 6. FIDE game outcomes and decisions

- **Automatic draws:** stalemate, dead position, fivefold repetition, 75-move rule (subject to checkmate precedence), and other applicable FIDE adjudications.
- **Claimable draws:** threefold repetition and 50-move rule, including properly claimable intended-move cases.
- **Strategic policy:** if an eligible draw claim scores at least as favorably as continuation from the claiming side's perspective, claim it; otherwise continue. Claim is optional, **not** an automatic result at threefold/50 moves.
- Rules validation is the controller's responsibility for played games; the search applies corresponding rules over hypothetical history, independent of `E(P)`.
- Repetition equivalence includes player to move, piece placement, and applicable move possibilities (notably castling/en-passant rights). A transposition hash and a repetition key may have different criteria; historical context must not be lost.
- Null legal-move roots with terminal status are not passed to a normal fallback selection. Terminal outcome and explicit draw claims are not interchangeable with chess moves.

**UCI interoperability caveat:** Standard UCI does not encode all FIDE draw-claim actions as a `bestmove`. Keep the core Decision model richer than UCI and document specific UCI-facing policy prior to a production-compatible UCI release (e.g., let the host controller adjudicate). This is an adapter-level compatibility item, not a change to strict FIDE handling internally.

## 7. Score interoperability with UCI

- Internally, preserve raw evaluator values (White perspective) and structured mate outcome/distance.
- Standard UCI reports centipawn (`score cp`) or mate (`score mate`) scores in the engine-side perspective; adapter flips perspective if needed.
- An evaluator may declare an **explicit, meaningful** centipawn mapping. Do not infer pawn scale merely from arbitrary score magnitudes.
- If no mapping exists, send truthful nonstandard diagnostic information (e.g., UCI `info string` with raw score) rather than fabricated centipawns. External GUIs may then omit their evaluation bar.
- `bestmove` communicates the chosen move; MultiPV rankings are communicated with appropriate `info multipv` details where supported. The internal GUI displays true raw units and clearly labels them.

## 8. Mobile/offline and desktop operations

**Desktop:** Local Node.js controller can spawn native engine and third-party UCI processes. GUI uses local HTTP/WebSocket. Bind to localhost; validate permitted engine paths and origins, never expose an arbitrary process-launching endpoint to untrusted websites. Headless experiment batches continue if the browser closes while the backend remains running.

**Mobile:** React GUI packaged via Capacitor; C++ WASM engine in a Worker. Evaluators are precompiled and bundled. Support chess play, analysis, local matches, and configured search policies entirely offline. Do not assume arbitrary installed UCI executables can run in the app. Allow multiple bundled engine instances when feasible under device memory/performance constraints.

**Lifecycle:** When backgrounded, cancel ongoing search, stop active computation, persist full game move history and position, clocks/status, evaluation configuration, and settings. On resume, restore and restart analysis/search only when the controller determines it is appropriate. Preserve committed search results as diagnostics if valid, but never auto-play an interrupted move. Casual game clocks pause; a future competitive clock policy may differ.

**Persistence:** Autosave locally and allow PGN/FEN import/export. Do not depend on a user account. Initially do not persist active search trees or transposition table memory. Verify app lifecycle and storage behavior on real Android and iOS devices.

## 9. Experiment framework

Three supported experiment types: (1) fixed-position analysis comparison; (2) headless paired engine-vs-engine batches with swapped colors and controlled opening sets; (3) search performance benchmarks.

**Every run records:** engine executable/version/hash, rules adapter version, evaluator module binary/hash/ABI and parameters, search and quiescence options, TT/order settings, depth/time/node statistics, time control, opening positions, sides, random seed when applicable, platform/hardware, game outcome, PGN, and run date. Preserve a reproducible immutable per-run configuration manifest.

**Comparison principles:**

- Compare algorithms at equal **nominal depth** *and* at equal **time budget**; interpret them differently.
- Static scores from unrelated evaluator scales cannot be compared directly without calibration.
- Alternate colors on paired openings to reduce opening/first-move bias.
- Clear caches between experiments when needed to avoid inter-run contamination; record warm/cold conditions.
- Track wins/draws/losses, uncertainty, move selection, depth completion, nodes, static evaluation calls, elapsed time, node rate, cache effectiveness, and memory.
- Consider fixed node-budget tests later for more deterministic comparisons; do not replace normal hard time/depth limits.
- Desktop controller must support unattended/headless batches from the initial architecture, even if the UI for experiment management comes later.

## 10. Configuration (conceptual, not finalized wire format)

```yaml
engine:
  rulesAdapter: disservin
  evaluator:
    module: material
    artifactId: <exact-build-id-or-hash>
    parameters: { pawn: 1.0, knight: 3.0, bishop: 3.2, rook: 5.0, queen: 9.0 }
  search:
    maxDepth: 8                  # optional; plies
    maxTimeMs: 5000             # optional; hard clock budget
    multiPV: 3                  # 1 = best-move-only
    selectedRank: 1             # 1..number of ranked legal moves
    quiescenceMode: NONE        # NONE | TRADITIONAL | EVALUATION_DRIVEN
    transpositionTable: { enabled: true, memoryMiB: 64 }
    moveOrdering: default
    experimentalInstability:    # only used when evaluation-driven is selected
      probePlies: 2
      trigger: undecided        # conditional/unconditional, to be tested
      threshold: undecided
      maxExtraPlies: undecided
```

This is illustrative and **not an approved algorithmic setting** for experimental quiescence. Validate parameter types and module compatibility on load; configuration changes that change score meaning invalidate caches. Distinguish engine-local options from per-match controller time controls.

## 11. Correctness and acceptance criteria

### 11.1 ChessCore (first critical gate)
- Perft test suite for opening and tactical reference positions.
- Every legal move make/unmake restores exact position state and keys.
- Castling, en passant, promotions, in-check behavior, checkmate, stalemate, dead positions.
- FEN import/export round-trip; game history preservation tested separately.
- Repetition key semantics and draw-claim/automatic-draw examples validated.
- Native and Emscripten/WASM builds pass equivalent golden test vectors.

### 11.2 Evaluation/ABI
- Independently compile and load a material-only module (native); bundle and invoke on WASM.
- Reject incompatible ABI, invalid parameter schema, NaN/±∞ outputs, and module initialization failures safely.
- Snapshot exactly matches chess position; no dependency on Disservin types.
- Switching module/parameters invalidates evaluation-dependent TT results.

### 11.3 Search and controller
- Minimax compared against small exact reference trees; alpha-beta on/off yields same completed exact values.
- Structured mate ordering and ply-distance normalization; terminal/draw outcome correctness.
- MultiPV top `N` versus exhaustive reference at manageable depths; rank selection after commit only.
- Deadline interruption during every search mode does not commit partial depth; preselected fallback is legal and deterministic.
- Search remains responsive to cancellation in native and WASM worker.
- UCI tests exercise initialization, position, go, stop, options, info, bestmove, and no-legal-move handling; draw-claim impedance explicitly tested.
- Headless matches reproduce configuration manifests and reliably export game results.

### 11.4 Experimental quiescence
- No-quiescence baseline produces the exact requested nominal depth (except terminal/cutoff).
- Traditional and evaluation-driven policies preserve rules and legal terminal handling.
- Benchmark extra-node cost and playing strength under equal time and equal nominal depth.
- Document false-positive and false-negative instability cases; do not assert guaranteed stabilization.

### 11.5 Platform and UI
- Desktop local backend is offline and secured to local use.
- Mobile app plays/analyzes without internet, remains responsive, and cancels/persists on backgrounding.
- Real-device mobile memory/CPU/battery tests; all required evaluator modules present in application package.
- GUI explicitly distinguishes committed depth from provisional search diagnostics.

## 12. Implementation milestones for Codex

**M0 — repository and contracts.** CMake/TypeScript workspace skeleton; design document; shared interface tests; pinned dependencies/licenses; CI strategy. Do not implement full GUI yet.

**M1 — chess core & portability.** Disservin adapter, opaque own types, FEN, legal move generation, make/unmake, initial position snapshot, perft and special-rule tests; prove native and **WASM** builds early. This is the first coding milestone.

**M2 — evaluation ABI.** Versioned C ABI, dynamically loaded native sample material evaluator, pre-bundled WASM equivalent, parameters, ABI/error tests.

**M3 — exact basic search.** Minimax and alpha-beta, `NONE` quiescence, structured score and FIDE terminal/draw context, reference-tree tests.

**M4 — full search behavior.** Iterative deepening, hard deadline, atomic depth commit, deterministic fallback, Best Move/MultiPV, rank selection, cancellation tests.

**M5 — search optimizations/experiments.** Optional move ordering, transposition tables, Traditional and experimental Evaluation-driven policy. Benchmark every option against baselines; don't hardcode Decision 24's still-open heuristic without a documented experiment proposal.

**M6 — interoperability/headless controller.** UCI native adapter, Node.js controller, external-engine management, PGN, headless paired-match runner, immutable manifests.

**M7 — desktop GUI.** React board/analysis/history/evaluation graphs/settings and experiment UI. GUI does not own authoritative game state.

**M8 — packaged mobile.** Capacitor offline app, WASM Worker, bundled evaluators, autosave/resume lifecycle, real-device performance/correctness tests.

**Milestone discipline:** Each milestone is independently testable and only considered complete when its stated acceptance criteria pass. Preserve portability tests throughout development; do not defer WASM validation until M8.

## 13. Items deliberately left for implementation design or experimentation

These **do not block** beginning M0/M1, but must be resolved before the relevant code is finalized:

1. **Decision 24 research:** conditional probe trigger/metric, threshold, extension stopping/safety semantics, and probe cost. Keep it a separately configurable experiment.
2. **ABI exact layout:** symbol names, struct sizes/alignment, ownership/exception/error conventions, snapshot revision strategy. Must precede M2.
3. **History-aware TT implementation:** safe cache reuse with repetition/50-move claims and mate distances. Must precede M5.
4. **UCI draw-claim bridging:** internal claims vs standard `bestmove` protocol; clearly define which party is responsible in external-controller play. Must precede full UCI compatibility release.
5. **Offline persistence schema and time controls:** precise saved-state versioning, clock pause/resume policy. Must precede mobile acceptance.
6. **Platform toolchain matrix:** pinned versions, supported architectures, build/test environments, practical WASM runtime performance. Validate in M0/M1.
7. **Experiment inference:** exact rating/uncertainty estimators and stopping criteria. Can follow basic headless batches.

## 14. Source and license references (non-normative links)

- [FIDE Handbook — Laws of Chess (2023 edition)](https://handbook.fide.com/chapter/E012023) — consult effective FIDE edition before shipping; test claimable/automatic outcomes.
- [Disservin/chess-library](https://github.com/Disservin/chess-library) — initial C++ rules implementation, MIT licensed; pin tested revision and preserve license notice.
- [Disservin example / make-unmake and perft](https://disservin.github.io/chess-library/pages/examples.html).
- [Emscripten dynamic linking guide](https://emscripten.org/docs/compiling/Dynamic-Linking.html) — later optional mobile/WASM module-loading techniques.
- [Apple App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/) — relevant to app-contained executable functionality.
- [Capacitor application lifecycle documentation](https://capacitorjs.com/docs/apis/app).

**Licensing:** Third-party components must be documented in a `THIRD_PARTY_LICENSES` inventory; prefer MIT/BSD/Apache-style terms compatible with intended commercial/private distribution. License choice is separate from code correctness.

---

**Approval state:** **Approved by project owner on 2026-10-19.** Architecture v1.0 is the authoritative implementation baseline. M0 repository setup and subsequent Codex milestones may proceed. Decision 24 remains explicitly experimental; approval does not authorize selecting an untested heuristic as final.