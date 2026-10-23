# Chess Engine Lab — Project Context

**Document role:** Long-lived engineering handoff for ChatGPT, Codex, and collaborators.
**Updated:** 2026-10-22 (America/New_York).
**Repository:** private `yctekmen-sudo/chess-engine-lab` — <https://github.com/yctekmen-sudo/chess-engine-lab>.
**Authoritative sources:** the current repository code, approved specifications under `docs/`, and merged tests. This file is an orientation guide, not a substitute for the source or the approved specifications. Read the root [`AGENTS.md`](../AGENTS.md), [`README.md`](../README.md), and [`development.md`](development.md) before implementation. See [`CURRENT_STATUS.md`](CURRENT_STATUS.md) for moving milestones, benchmark results, and pending work.

## 1. Purpose and non-negotiable objectives

Chess Engine Lab is an offline-first, modular chess engine and experimentation environment. Its original approved architectural specification is [`docs/architecture-v1.0.md`](architecture-v1.0.md) (2026-10-19). Its goal is to implement orthodox, FIDE-compliant chess with replaceable chess rules, evaluator, and search policies, reproducible benchmarking, interactive play, and support for external UCI engines.

Priorities, in order:

1. **Rules correctness.** Legal play, castling, promotion, en passant, checkmate/stalemate, claimable threefold/50-move draws, automatic fivefold/75-move draws, and terminal precedence must remain correct. Position repetition includes legally effective en-passant availability and castling rights. Position history cannot generally be reconstructed from FEN alone.
2. **Exact reproducible search results.** Within the defined algorithm/search context, preserve the structured score, selected decision, canonical move tie-breaking, full principal variation (PV), mate distance, and PV termination—not just the first best move. A reproducible reference mismatch is a correctness failure, not an allowable optimization trade-off.
3. **Modularity.** Keep the rules adapter replaceable, search independent of the static evaluator, and evaluation modules independent of controller/UI.
4. **Controlled iteration and evidence.** Benchmark independent changes against clean, comparable Release baselines and run full correctness gates before proposing merges. Never assert performance improvements solely from expected complexity.
5. **Portability/offline operation.** C++20 engine builds natively and with Emscripten/WebAssembly; desktop controller and browser-based UI are separate layers. Mobile support is an architectural target, not proof of completed mobile implementation.

## 2. Architecture and source ownership

| Layer | Role | Principal locations |
| --- | --- | --- |
| Rules adapter | Opaque `Position`, `Move`, `UndoToken`; FEN, legal moves, make/undo, state, snapshots; currently wraps vendored Disservin/chess-library | `engine/include/chess_lab/chess_core.hpp`, `engine/src/chess_core.cpp` |
| Search | Minimax/alpha-beta, iterative deepening, MultiPV, previous-PV ordering, draw actions, exact PV/tie handling | `engine/include/chess_lab/search.hpp`, `engine/src/search/search.cpp` |
| Transposition table | Optional, bounded, context-validated hints and numerical results; exact history verification, compatible depth/evaluator/policy | `engine/src/search/transposition_table.cpp`, `docs/m5-transposition-table-spec-v1.0.md` |
| Evaluation | Replaceable current-position-only evaluator with versioned read-only C ABI; raw finite White-perspective score, material baseline | `engine/include/chess_lab/evaluation.hpp`, `engine/src/evaluation/`, `docs/evaluation-abi-v1.0.md` |
| Native protocol/controller | UCI process; Node.js/TypeScript owns authoritative game state, clocks, adjudication, sessions, experiment records | `engine/src/uci/`, `controller/`, `docs/m6-controller-spec-v1.0.md` |
| Presentation | React/TypeScript user interface, distinct from engine rules and search | `gui/`, `docs/m7-gui-spec-v1.0.md` |
| Validation and benchmarking | Native/WASM correctness suites, reference searches, automated benchmarks, release/profile/debug presets | `tests/`, `CMakeLists.txt`, `CMakePresets.json`, `.github/workflows/`, `docs/development.md` |

The Node.js 24 controller is authoritative for actual moves, clock rules, and adjudication; the GUI renders that state. Engine output is a validated proposal. Only a **fully completed iterative-deepening depth** supplies authoritative searched rankings; intermediate callbacks remain provisional until the invocation's final status. Terminal roots return at depth zero without an evaluated move ranking. Cancellation may leave a deterministic but *unevaluated* legal fallback before depth 1 completes; later search/evaluator errors invalidate provisional results. Importing the controller library does not start its authenticated loopback server or GUI.

`SearchScore` is structured: finite White-perspective score, WhiteMate(distance), or BlackMate(distance). Do not conflate terminal/mate results with ordinary numerical evaluations or assume unrestricted raw values represent centipawns.

## 3. Important search and chess-rule decisions

- **Draw actions are distinct from playing chess moves.** Decisions include `play`, `claim_draw`, `claim_draw_with_intended_move`, and `terminal`. Claiming a draw has score zero but does **not** silently replace the normal evaluation of the associated playable move. A move announced for an intended claim remains unplayed as part of that action.
- **Terminal precedence matters.** Checkmate/stalemate, insufficient material, fivefold repetition and 75-move automatic draws are not interchangeable with optional threefold/50-move claims. Exact existing precedence and exceptions must be preserved.
- **Single-PV PLAY-only root (M7.2).** For default iterative `MultiPV=1` with `single_pv_play_only=true`, a nonterminal *actual root* selects a PLAY move, shares root alpha-beta bounds, and neither probes nor stores root TT entries because root optional-claim policy differs from ordinary descendants. Claim-aware descendants, legacy MultiPV/reference paths, and fixed-depth `search()` semantics remain intact.
- **Actual-game root draw arbitration belongs to the controller.** A trusted Chess Lab final-score envelope must pass session/search/depth/status validation before it can support an automatic claim. For fixed-depth searches, a completed result must reach the requested depth and a cancelled result is accepted only after a controller stop for that search; movetime, clock and infinite searches retain their own completion/cancellation semantics. A before-depth-one fallback has no exact final score. Analysis does not auto-claim; generic UCI engines do not acquire trusted Chess Lab score semantics.
- **TT numerical reuse is conditional.** A matching hash alone is insufficient. Reuse requires compatible snapshot, complete/incomplete history, exact repetition-context validation, identical remaining requested nominal depth, evaluator identity, and value policy. A legal move-order hint is distinct from a trusted numerical score.
- **Exact canonical PVs.** Previous-PV and TT hints may alter visit order without changing final exact decisions and canonical tie resolution. Promising bounded continuations may require full-window re-search. Do not remove this merely to increase speed.
- **Evaluator boundaries.** The evaluator receives a versioned read-only *current-position* snapshot with no game history; history-sensitive claims belong to search/controller. The evaluator ABI must not change incidentally during search optimization.
- **Unimplemented/deferred items are not requirements for the current milestone.** The approved architecture allows configurable quiescence, extensions, and later mobile variants, but these are not licenses to introduce selective-search heuristics into a correctness-preserving performance refactor.

## 4. Approved M7.3 design principles

See the approved [prepared-node design](m7.3-search-node-design.md). Consult `CURRENT_STATUS.md` for its exact revision and implementation checkpoint. These principles describe the approved design; they do not certify that every proposed implementation step or follow-up optimization is complete.

1. **One full legal-move set per search-node encounter.** Generate the set once, use it for terminal detection, encoding, canonical ordering, search, and all rule consumers for that position. Reordering the list must not change its membership or per-move metadata.
2. **En passant belongs to the legal list.** If an EP capture is legal it appears there. A raw EP target square alone does not affect repetition identity; normalize the repetition EP field by examining the existing list, without independently regenerating all moves. Executing EP still has its special board-update semantics.
3. **Draw eligibility is calculated once and retained.** Immediate claims are node metadata. Intended 50-move and threefold claims are flags on individual legal moves. Claims remain actions in their own right, each scored zero; PLAY scores are unaffected.
4. **Avoid eager child expansion.** Cheap, sound necessary conditions rule out most intended claims. Native 50-move intended claims can be identified using halfmove clock and pawn/capture facts. A new third repetition is possible only if a matching position occurred at least twice already. Rare cases must still verify exact resulting repetition identity.
5. **Position-safe trusted moves.** Internal moves already established legal may bypass redundant full-list validation, but external move inputs must retain strict validation. Respect opaque adapter objects, position generation, restoration, errors, and foreign-object protections. Synthetic fixture adapters can have nonstandard modeling rules; preserve their fixture contracts.
6. **Do not over-claim guarantees.** A current optimization may still reprepare rare hypothetical child positions, or count history less efficiently without a TT. Any such deviation remains a review/measurement item; the approved *intent* is not automatically evidence of complete implementation.

## 5. Test and benchmark requirements

The local acceptance workflow is documented in [local-ci.md](local-ci.md): one
Windows PowerShell entrypoint coordinates Windows/MSVC and WSL2 Ubuntu suites,
checking commit/working-tree identity and keeping application Node separate from
the pinned WASM SDK Node. Quick or dirty/incomplete runs are not authoritative
milestone passes. This local workflow does not grant permission to disable GitHub
workflows or change branch protection; such changes require separate approval.

For every substantive search optimization:

- Run `cmake --preset native-release`, `cmake --build --preset native-release`, `ctest --preset native-release --output-on-failure`, and appropriate Debug/Profile/WASM/GUI/controller CI gates.
- Compare the same initial position, full/comparable history, material evaluator and options, iterative single-PV PLAY-only root, previous-PV ordering, depth, TT off/cold/warm, and requested TT memory. Run multiple repetitions and quote medians from uninstrumented native **Release** binaries.
- Verify **exact** decisions, structured scores, complete PVs, claim/terminal actions and termination, not only visited-node counts or elapsed time. Test castling/EP (including pinned and irrelevant raw EP), fifty-move threshold transitions, repetition with complete and incomplete history, mate precedence, undo correctness, TT policy isolation, cancellation, and evaluator errors.
- Separate canonical wall-time measurements from instrumented event counters and sampled CPU profiles. Inclusive CPU percentages overlap and **must not be summed**. A counter CSV without observations is not a measured hotspot breakdown.
- Provide raw data, environment/toolchain details, SHA/commit IDs, command lines, baseline provenance, limitations, and any changed work/node counts.

Existing milestones have regression/oracle suites including `m3_search`, `m4_search`, `m5_search`, `m72_search`, native rules/evaluation tests, controller tests, and WASM checks. Check the repository for current target names and exact test instructions; do not infer gate success from configuration alone.

## 6. GitHub and collaboration workflow

- `main` is the merged source of truth. Use an isolated feature branch and **draft PR** for new changes. Do not merge without explicit user review/approval; do not silently modify `main`.
- The user prefers to **discuss architecture and approve the design in ChatGPT, then use Codex to implement it**. For future implementation requests, provide a ready-to-paste Codex prompt with scope, invariants, tests, profiling and deliverables. **Do not directly change the GitHub repository unless explicitly asked**.
- Codex should inspect the current repository and branch before coding, work incrementally, run appropriate checks, and report exactly what passed, failed, or was unavailable. Avoid declaring an unexecuted WASM/Windows test passed.
- Commit, push, PR creation, and merge each require explicit authorization for that action and scope, as specified in `AGENTS.md`. An authorized documentation PR must exclude unrelated profiling work; it does not authorize changing another PR or merging anything.
- Never discard user work: inspect `git status`, protect uncommitted tracked/untracked files, preserve stashes; do not `git reset --hard`, `git clean`, or `git stash pop` casually. The user works in Windows PowerShell, usually from `C:\cursor\chess-engine-lab`.
- Do not create unnecessary PR churn: docs-only handoff updates belong in a small focused changeset. Running benchmarks or reviewing profiler results does not, by itself, authorize a production implementation.

## 7. Document hierarchy and renewal

When facts conflict, use this order:

1. Latest **merged** source code and tests (implemented behavior on `main`, not proof of a particular deployment).
2. Explicitly approved architectural/specification decisions in `docs/`, noting any superseding decisions and their dates.
3. Current, open PR code and associated tests **for that branch**, clearly labeled unmerged.
4. `docs/CURRENT_STATUS.md` for last-known working status, measurements and pending actions.
5. This long-lived orientation document.
6. Prior ChatGPT conversation summaries and informal notes as supporting context.

Code and tests establish implemented behavior; approved specifications establish intended contracts. If they disagree, report the discrepancy rather than treating implementation as a silent amendment to the approved design. Verify the repository or ask the user for newer decisions if necessary.

**Update policy:** Revise this document only for enduring decisions, architecture or workflow changes. Update `docs/CURRENT_STATUS.md` at every significant milestone, PR transition, new benchmark, CI conclusion, or profiling handoff. Record the verification date, relevant SHA, evidence location and whether results were executed now, checked in existing local logs, reported by the user, or observed in CI. Keep moving SHAs and results in the status file. Identify ignored/local-only evidence explicitly so a fresh clone or ChatGPT session does not assume those artifacts are published. This maintenance happens during authorized work; it is not standing permission to commit, push, open PRs, or merge.

## 8. Starting a new ChatGPT conversation

Open a new conversation inside the **Chess Engine Lab** ChatGPT Project, attach or reference these two files, and use:

> We are continuing Chess Engine Lab. Read `PROJECT_CONTEXT.md` and `CURRENT_STATUS.md` as the handoff. Preserve the approved architecture, rule-correctness and exact-PV constraints, and my preference for Codex implementation prompts rather than direct repository edits. Verify the latest GitHub `main`, open PRs and CI before relying on status snapshots. Continue with the pending work listed in `CURRENT_STATUS.md`.

The ChatGPT Project and memory can improve context recovery, but should not be treated as exact copies of every earlier turn. These repository documents are intended as the explicit portable handoff.
