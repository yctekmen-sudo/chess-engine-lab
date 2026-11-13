# T1 accepted transposition-table integration — B4 comparison

**Status:** Local isolated integration validation passed on 2026-11-13. Full 54-gate committed-revision CI and PR review are still required; this file does **not** record a production merge.

## Revision and accepted scope

- **Comparison baseline (`main`, B4):** `6e01eb17dc331261bc8fa823150f16a368920b97` (merged PR #47).
- **Candidate:** accepted T1.1a (cached repetition-signature hash), T1.1d.2 Variant C (no depth-zero TT), T1.1fH (hardened history-independent reuse), T1.1h Variant C (remove `DrawCounts`), T1.1i (per-node TT signature-hash reuse), T1.2a.5B (depth-preferred eviction), T1.2a.7 Mode 3 (efficient stores), and T1.3a B2 (deferred native move materialization and ordering).
- **Accepted defaults:** 4-way associativity; conventional TT entry layout; exact-depth numeric score matching; depth-preferred eviction; Mode 3 stores; T1.3a B2. Rejected variants, notably T1.3b, are excluded.
- **Source provenance:** The exact eight source/test overlays from `CELab-T1-integration-evidence-t1int-20261113T164656Z-49236.zip`; SHA-256 for every file is preserved in the publication kit's `validated-source-manifest.json` and the local archived evidence.

## Five-mode performance comparison (Windows Release)

| TT mode | B4 sum of medians (ms) | T1 sum of medians (ms) | Elapsed time saved | B4 nodes | T1 nodes | Nodes saved |
|---|---:|---:|---:|---:|---:|---:|
| Off | 10,028.5 | 10,252.3 | **-2.23%** | 11,514,442 | 11,514,442 | 0.00% |
| Hint-only cold | 11,203.6 | 8,313.2 | **25.80%** | 9,580,182 | 8,528,061 | 10.98% |
| Hint-only warm | 10,659.6 | 3,047.2 | **71.41%** | 9,160,011 | 2,525,873 | 72.43% |
| Numeric cold | 18,355.2 | 7,193.3 | **60.81%** | 11,020,793 | 7,829,596 | 28.96% |
| Numeric warm | 18,253.0 | 41.3 | **99.77%** | 11,004,019 | 39,580 | 99.64% |

- Benchmark coverage: nine position/depth/budget combinations, five TT modes, three fresh-process repetitions per variant, 270 measurements. The numbers are **sums of per-workload median times**, not statistically independent aggregate observations; the same workloads appear in every mode.
- Nine of nine workload comparisons in each mode reported the same final result checksum and best/PV. This does not imply every intermediate search decision is identical or prove playing strength.
- Numeric cold is about **2.55x faster** and 29% fewer nodes. Unlike same-position warm runs, it does not depend on a fully prepopulated TT.
- Numeric warm is about **442x faster** in this specific benchmark design because the same position was pre-searched and the TT was carried forward. Do not extrapolate this multiplier to full-game play or to unseen positions.
- **Known adverse result:** TT-off total elapsed time increased **2.23%**, with unchanged node counts. Must be documented in PR review and followed up with profiling; it does not represent a chess-correctness failure.

## Local validation provenance

- Windows B4: Release **23/23**, Debug **23/23** native tests passed.
- Windows accepted T1: Release **23/23**, Debug **23/23** native tests passed.
- WSL Ubuntu accepted T1: Release **23/23**, Debug **23/23** native tests passed.
- Determinism checks **90/90**; cross-version comparisons **45/45**; 270/270 measurements completed.
- Main checkout was unchanged in the local run. Builds and tests used detached isolated checkouts; timings were collected on Windows.
- Evidence ZIP: `CELab-T1-integration-evidence-t1int-20261113T164656Z-49236.zip` (retained outside Git; not a repository source artifact).

## Pre-merge gate

Run the existing model-free local Full CI from a **clean committed feature revision** on both Windows and WSL using `scripts/validation/full-ci.ps1 -PlanOnly` followed by `scripts/validation/full-ci.ps1`. All **54 gates** must execute, with preserved source identities and evidence. The existing workflow may appropriately report `reference_review_required` for changed engine/tests; review the delta and evidence instead of interpreting exit 2 as a pass or silently approving a reference.

No GitHub Actions CI should be added. No change to controller, GUI, WASM, or evaluator ABI is intended, but the authoritative full suite must check those layers before merge. Merge requires separate explicit approval.

## Future integration validation contract

Each future CELab integration candidate must include an explicit same-toolchain, same-workload performance comparison against the `main` revision at the beginning of integration, across TT-off, hint cold/warm and numeric cold/warm, reporting elapsed time, CPU time, nodes, NPS, evaluator calls, TT reuse and cutoffs, determinism and adverse workloads. Keep historical measurements and exact source hashes for longitudinal comparisons.
