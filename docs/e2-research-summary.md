# E2 evaluator research checkpoint (2026-11-27)

Exact merged engine baseline: PR #57 `30dae10bc3c761704876422aeda65a1463592fa6`.
Material remains the packaged default and computational performance reference.
C1 is selectable and is now the principal playing-strength reference for S3.
No learned PST or experimental pruning registry is approved for production.

## Experimental results (local-only archives)

- Q1: 10,041 quiet pairwise comparisons, 671 validation, 651 untouched test.
  Severe advanced-pawn coverage bias: ten rank-seven feature incidences.
- Q5: 10,007 unfiltered comparisons, 603 validation, 606 untouched test.
  76 rank-seven incidences; all eight squares covered. 9m48s total generation.
  Q1/Q5 differ in candidate sampling as well as quietness filtering.
- E2.1f: Q5 improves broad offline ranking, Q1 wins quiet validation. Some
  fitted PST magnitudes overwhelm material. The independent tests remain unused.
- E2.1g: hybrid B (Q5 pawn/knight, Q1 bishop/rook/queen/king, tapered king)
  is the leading offline hybrid. Selection used a small, repeatedly reused
  validation set. Strong offline accuracy is not proof of playing strength.
- E2.2a/b: efficient integer lookup implementation, native experimental ABI
  modules only. Initial fail-open pruning disadvantaged learned evaluators;
  E2.2b registered exact experimental identities with C1 thresholds (SEE <=0 cp,
  delta margin 0). All 1,320 benchmark searches completed. Policy reuse was
  experimental, not a production acceptance of the new identities.
- E2.3: 160 games at 100ms per move, 8 openings, reversed colors. All PST
  challengers including C1 scored 147 wins, 2 losses, 11 unresolved against
  material. Original PGN audit failed on CRLF/LF; audit-only repair verified
  all 160 games, 14,171 played moves and 243,722 PV moves without changing games.
- E2.3b: 288 games at 100ms, 16 openings, reversed colors. Learned challengers
  scored 51 wins, 14 draws, 178 losses, 45 unresolved against C1. Every match
  loses even if all unresolved games count as challenger wins. Hybrid B 0.25x
  was the leading learned reference (10W/1D/17L/4U). The audit passed.

These are small Windows standard Release development screens, not Full CI or
precise Elo measurements. Timing, selected openings and selective heuristics
limit generalization. User agreed to defer learned PST integration and further
training/scale investigation here; a separate Lichess-based research track is
addressing training architecture flaws. Consume its results only if independently
validated. Keep E2.2b source/coefficients and training archives local for reuse.

Evidence filenames and archive SHA-256:

- `CELab-E2.3-evidence-e23-20261127T024729Z-b12a38c3.zip`: `642e39309ae5953783ab62c4bc6e63cd68ecbccfa68c5d596c2a30dc6f4c5ed1` (711 entries verified; original failure retained).
- `CELab-E2.3-audit-repair-report.json`: PASS, references the exact original archive hash.
- `CELab-E2.3b-evidence-e23b-20261127T035225Z-647f69e2.zip`: `cb043684745d024438182a35643662bc2df159fc5de468e99664481774a56b45` (1223 entries verified).
- `CELab-E2.1e-final-quiet-results.zip`, `CELab-E2.1f-Q5-training-results.zip`, `CELab-E2.1g-hybrid-results.zip`, `CELab-E2.2b-C1-pruning-v1.zip`: retained experimental artifacts; not source changes in main.

Next: S3.1 late move reductions, independently of evaluator research. Material
for direct exact-main computational comparisons, C1 for playing strength.
Preserve legal PVs, mate/draw/cancellation and TT contracts; characterize
intentional selective-search score changes, and do not weaken reference tests.
The proposed later sequence LMP, futility, reverse futility and null move is
provisional. No authorization to commit, publish or merge follows from this note.
