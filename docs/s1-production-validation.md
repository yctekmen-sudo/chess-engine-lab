# S1 production validation and performance — 2026-11-24 (America/New_York)

Accepted local production evidence: `CELab-S1-production-evidence-s1prod-20261125T042518Z-6e77aa73.zip` (UTC timestamp 2026-11-25). Archive SHA-256: `61a57f3af0f82530f53e8d25ff405c28b04d6ea3e963f8d84cd12827f89f01c9`.
Baseline: `84ea6ba22ffc52a5a4c42e5218c969052c0e99cb` (exact pre-S1 merged main).
This is isolated native validation of the exported production v2 source, not a committed-revision Full CI result. The user accepted the candidate. The commit preparation updates documentation and removes exactly one trailing LF from `scripts/performance/targeting.py` to satisfy `git diff --cached --check`. All other non-documentation files are retained byte-for-byte. The targeting script has identical Python AST and meaningful tokens; no executable statement or compiler flag changes. The original and repaired SHA-256 hashes are e924014ddd3c277b2eb1b79cad06f4cd6fed0fb272e4c04473502b59937873a4 and e8ca69682cf5b2f907b37f1e4d04893ba011a105fc691ac7ca08e8d1cb9cabdc, respectively. Full CI and explicit cross-revision reference review remain mandatory.

## Direct paired measurements

43 positions, five repetitions, aggregate NPS = summed nodes / summed per-position median wall time. TT-on and TT-off measured separately. Generic = MSVC (Windows), GCC (WSL); optimized = Clang-cl 19.1.5 / Clang 18.1.3, native O3, Full LTO, independently trained PGO profiles for each revision. Main uses its static horizon. Production QS-on uses the actual default. S1.13 is the accepted pre-consolidation reference. Same-platform and same-configuration baseline comparisons are the primary results.

| Platform/config | TT | Main NPS | S1.13 QS off | Production QS off | S1.13 QS on | Production QS on | QS-off NPS vs main | QS-on time saved vs main |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| windows generic | on | 1,795,557 | 2,209,909 | 2,228,665 | 1,742,179 | 1,809,867 | +24.12% | 12.78% |
| windows generic | off | 1,916,399 | 2,354,244 | 2,389,495 | 1,775,553 | 1,841,432 | +24.69% | 15.15% |
| windows optimized | on | 2,787,676 | 3,749,671 | 3,758,444 | 2,895,367 | 2,981,050 | +34.82% | 17.79% |
| windows optimized | off | 3,210,849 | 4,270,471 | 4,295,235 | 3,084,795 | 3,127,965 | +33.77% | 16.31% |
| linux generic | on | 1,925,823 | 2,350,100 | 2,342,904 | 2,114,936 | 2,139,379 | +21.66% | 20.86% |
| linux generic | off | 2,139,945 | 2,615,388 | 2,619,108 | 2,226,800 | 2,259,862 | +22.39% | 22.79% |
| linux optimized | on | 2,997,897 | 3,941,941 | 3,973,879 | 3,355,167 | 3,445,557 | +32.56% | 23.51% |
| linux optimized | off | 3,574,327 | 4,769,501 | 4,798,048 | 3,754,054 | 3,838,917 | +34.24% | 24.09% |

With TT enabled, node totals are 4,117,821 (main), 4,116,309 (QS off), 3,620,143 (QS on). With TT disabled they are 5,800,382, 5,800,382 and 4,729,187 respectively. QS changes the search tree; NPS is not a strength or equivalent-work metric. The production QS-on elapsed-time comparisons are therefore important. No claim of universal node reduction or playing-strength gain is made.

## Validation and caveats

- Windows and WSL production status PASS; 192 native tests per platform (main 23, S1.13 39, production 34, each for two compiler configurations). Policy-build tests restored; default QS and opt-out controls and evaluator fallback checked.
- 20,660 uninstrumented benchmark/corpus searches, 256 additional verification searches, 600 PGO training searches. Frozen trace parity and PV legality checks passed in the reviewed evidence. 834 manifest entries and 73 kit hashes verified; all 36 exported candidate files identical across both platforms.
- Depth-3 tactical expectations: main/QS-off 16/204, QS-on 15/204; exact mates 8/32 vs 9/32. The dedicated horizon regression passes; these shallow tests do not demonstrate aggregate playing-strength improvement.
- Non-default evaluators use the conservative fallback without material-dependent pruning; public evaluator ABI remains unchanged. Native optimized builds are opt-in and CPU-specific. PGO retraining may affect code outside the changed source; regenerate profiles after relevant changes.
- Full CI (including native Debug/Profile, WASM, controller, GUI and reference comparison) remains pending. Do not silently accept changed historical fingerprints, reset the approved reference, publish or merge without authorization.

## COMP1 disposition

Clang, native CPU targeting, effective O3, Full LTO and frontend instrumentation PGO accepted for local performance builds. O2 rejected; ThinLTO superseded by Full LTO. COMP1b assembly study is explanatory, not a proof of dynamic instruction costs. COMP2 platform-gap study deferred. Portable standard builds retain their compiler settings.
