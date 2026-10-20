# Chess Engine Lab — Codex Kickoff for M3

**Status:** READY FOR IMPLEMENTATION — M3 only.  
**Milestone:** M3 — Fixed-depth reference search (minimax and alpha-beta).  
**Authoritative approved contract:** `docs/m3-search-spec-v1.0.md` (Decisions 41–48 including 42A).  
**Existing sources:** `docs/architecture-v1.0.md`, `docs/m1-contracts.md`, `docs/evaluation-abi-v1.0.md`, `docs/development.md`, `docs/m2-validation.md`, and current public C++ headers.

## Start and branch discipline

1. Fetch the latest `origin/main`, verify merged M0/M1/M2 and confirm the approved M3 specification has been placed in `docs/m3-search-spec-v1.0.md` on `main` **before starting implementation**. If the document is absent, stop and ask for it; do not infer the contract from this kickoff alone.
2. Create a new branch `m3/basic-search` from latest `origin/main`. Report base SHA and branch.
3. Inspect the actual `ChessCore`, `PositionSnapshot`, `Evaluator`, CMake, native and WASM tests before designing source interfaces. Existing M1 has **no** full draw/history API and has opaque move/undo tokens; M2 provides `evaluation_snapshot` and quarantining `Evaluator::evaluate`.
4. Implement **only M3**. Do not change the approved evaluator C ABI, change Disservin visibility, implement UCI/controller/UI, or begin M4/M5. If there is a genuine contract ambiguity or incompatibility, stop and report it instead of silently weakening an approved requirement.

## Implement

- Introduce an own-header C++20 search library (suggested `engine/include/chess_lab/search.hpp` and CMake `chess_search` target), exposed independent of third-party headers. Define a generalized chess decision and precise search status/result/options API. Return the evaluator identity, exact root structured score, depth, legal PV and measured statistics. No unit conversion to centipawns.
- Implement 24-byte/8-aligned tagged score semantics from D41: `Finite`, `WhiteMate`, `BlackMate`, with safe constructors, complete exact comparator, root/node-relative mate distances, propagated mate increments, and explicit unbounded alpha-beta endpoints. White always maximizes, Black minimizes; no negamax sign flips.
- Implement synchronous fixed-depth exhaustive minimax with optional alpha-beta, nominal depth ≥1 at nonterminal root, `Quiescence=NONE`, stable lexicographic legal-move order, first-best move ties, claim-on-tie and deterministic PV. Leave transposition tables, move-ordering heuristics, time limits and cancellation out.
- Add history construction and reversible search-local occurrence tracking for actual known pre-root positions plus hypothetical moves. Implement **exact** FIDE-equivalent repetition signatures, legal-en-passant normalization and collision verification; 64-bit M1 keys are only potential prefilters. Require root/history agreement, preserve the caller inputs and expose incomplete-prefix FEN history transparently.
- Apply checkmate/stalemate and sound dead-position detection before evaluator calls, including at depth zero; then automatic fivefold repetition and 75-move draws; then optional threefold/50-move claims including claims by *announced but not played* legal intended moves. Correctly compare claim zero against continuation at every node, including a static depth-zero leaf. Checkmate takes precedence over 75-move draw. Do not claim general FIDE dead-position completeness.
- On evaluator failure (including explicit status, invalid status, nonfinite value, quarantined instance), abort **the entire search** with `EVALUATOR_FAILED`, no chosen action/score/PV, no fallback or evaluator substitution; preserve original root/history. Ensure clean LIFO undo/history rollback. Use a separate error for invalid input/ChessCore traversal problems.
- Prepare a future-friendly place in decision provenance for optional root-only opening-book integration (D42A) without creating a book provider or book lookup in this milestone. A future book-selected action will have no synthetic search score.

## Required correctness tests

- Independent small-tree exhaustive oracle compared with alpha-beta on/off for exact score and **decision**, not merely score. Exercise both sides, mate distance, exact ties, claim preference, pathological finite magnitudes, and bound-only pruned alternatives. Verify PV correctness. Disabling pruning must result in zero cutoffs.
- Real chess cases: immediate and depth-zero checkmate/stalemate, sound known-dead configurations, 50/75 clock boundaries, pawn/capture resets, threefold/fivefold historical occurrences, legal/pinned/no-legal-capture en passant, differing castling rights, intended-move claims (threefold and 50-move), checkmate-over-75 precedence, and incomplete imported FEN history. Test deliberately colliding repetition hashes using a controlled fake adapter if needed.
- Controlled evaluator failure after prior root branches have finished; confirm all results discarded, instance quarantined, no fallback, unchanged source root/history, diagnostic present, and successful recovery only via evaluator replacement. Include non-evaluator traversal failure distinctly.
- Add native Debug/Release and Emscripten/WASM Node CTest targets and keep M0/M1/M2 suites intact. Reuse pinned workflow infrastructure and explicit `-fexceptions` behavior. Make no unverified portability claims.
- Benchmark no-pruning versus alpha-beta at equal fixed depths with identical evaluator/configuration. Record actual hardware/toolchain, FEN/history, repetitions/iterations, nodes, evaluator calls, cutoffs, elapsed time, and limitations; measure M1 make/undo overhead if helpful before proposing hot-path changes.

## Validation and handoff

Write `docs/m3-validation.md` recording actual build/CTest/CI commands and observed results, representative golden vectors and oracle comparisons, error/undo/history coverage, and real Release benchmark measurements. Confirm M1 public interface behavior and M2 C ABI stayed compatible. Flag known exceptions explicitly, particularly incomplete general dead-position recognition (D47) and unavailable pre-import history.

Build/test commands from the existing repository workflow:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug --output-on-failure
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
# On activated, pinned emsdk 4.0.10 toolchain:
cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug --verbose
```

Commit and push to `m3/basic-search`, open a PR against `main`, include observed CI links and test results. If PR creation fails, report branch and commit SHA for manual PR creation. **Do not merge, begin M4, or modify the agreed architecture without approval. Stop for ChatGPT code review.**