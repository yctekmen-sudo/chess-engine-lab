# S2.1 + E1-C1 pre-commit review

Exact pre-integration main: `f0145804513740993fd9b4543e7d29716327952a` (PR #56). Material remains the default; C1 is selectable. No S2.2 or S3 changes.

## Prior measured evidence (v3, not fresh post-extraction measurements)

| Workload | Standard Windows | Standard WSL | COMP1 Windows | COMP1 WSL | Node reduction |
|---|---:|---:|---:|---:|---:|
| Representative | 10.69% | 10.58% | 11.46% | 8.29% | 7.50% |
| Full corpus | 10.14% | 10.16% | 12.07% | 9.02% | 8.74% |
| TT off | 8.66% | 5.24% | 9.65% | 5.29% | 8.13% |
| QS off | 12.42% | 10.07% | 9.45% | 6.78% | 14.74% |

Percentages are elapsed-time reductions against exact main with material on both sides. WSL standard timings are historical v2 with v3 replay. COMP1 compares the supported per-revision training mixture (120 baseline, 240 candidate); no equal training-mixture claim. Full corpus: main 17,819,775 nodes, S2.1 material 16,261,982, C1 20,739,550. C1 solves 26/236 vs 24/236 for material at depth 5, but loses two solutions in the 43-position equal-time subset; no playing-strength superiority claimed.

Local-only evidence (not committed):
- `CELab-S21-E1C1-integration-evidence-s21ei3-20261125T230318Z-cd24cd8e.zip` SHA-256 `7aa8bb66ca08a9e869caf7be2e0b608af8d563fcae46a8326d19f36b43198d9e`.
- `CELab-S21-E1C1-COMP1-evidence-s21comp-20261125T231234Z-2265ba70.zip` SHA-256 `1a2a1c48026138b317f65ab6df92b5f9f1627fdd8e1aa5acae306153608a4f2a`.

COMP1: 2,004/2,004 exact standard-signature comparisons, 134 native CTests, 27,776 score checks, no cross-platform differences. Standard and COMP1 excluded m76_policy_build for development only. A missing profile warning in build_info.cpp occurred on both revisions. Full CI and historical reference review still required.

## PV/reference review

Material held constant: 236 positions, no score differences at any completed depth, no final best move differences, 37 changed completed-depth PV records across 23 positions, 22 changed final PVs. Prior independent endpoint review: 56/56 finite PV endpoints matched material score and 18/18 mate PVs passed mate-distance checks. This supports but does not prove equivalently optimal intermediate moves. First divergence never at ply 1. PVS traversal and TT reuse can alter tied continuations; explicit reference review remains required rather than silently approving a new baseline. C1 Arasan #8 had a previously investigated PVS-dependent 1 cp difference (-141 vs -140, same h6h5), associated with window-dependent heuristic delta pruning; no new pruning exception.

## PST isolation

Seven tables, 448 numbers, unchanged. Extracted into a private header; reinserting the original initializer reproduces the prior evaluator source byte-for-byte. Attribution and provisional wiki license notice are recorded in THIRD_PARTY_LICENSES.md; original-author grant and public-distribution terms remain unresolved. Private development only pending resolution. No new license imposed on unrelated CELab code. Fresh source and binary hashes will differ; prior timings are historical, not measured on the extracted source. Focused replay and committed Full CI are still required.
