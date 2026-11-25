# S2.0 integer centipawn validation — 2026-11-25

Exact pre-integration merged main: `0a23602e30ec0151200f5b874902b5c49d499e27` (PR #55). Both standard and optimized development experiments passed and were explicitly accepted. All 47 candidate source files match the two platforms and both experiments. Final release preparation changes documentation only; engine, tests, ABI, controller, GUI and build code retain the accepted bytes.

## Paired performance

43-position timing workloads, 236-position tactical/forced-mate corpus, and six horizon controls. NPS is aggregate nodes divided by summed per-position median wall times. TT-on and TT-off results are separated. All paired search node counts match; the nominal 16 MiB table has 65,536 entries in both builds. Optimized builds use Clang-cl 19.1.5 / Clang 18.1.3, effective O3, native CPU targeting, Full LTO and separately trained frontend PGO.

| Compiler | Workload | Main NPS | Candidate NPS | NPS change | Elapsed time saved | Nodes |
|---|---|---:|---:|---:|---:|---:|
| Windows MSVC | QS on, TT on | 1,872,062 | 1,945,266 | +3.91% | 3.76% | 3,620,143 |
| Windows MSVC | QS on, TT off | 1,964,320 | 2,061,772 | +4.96% | 4.73% | 4,729,187 |
| Windows MSVC | QS off, TT on | 2,285,367 | 2,390,728 | +4.61% | 4.41% | 4,116,309 |
| Windows MSVC | Tactical, TT on | 1,390,468 | 1,454,056 | +4.57% | 4.37% | 624,334 |
| Windows MSVC | Tactical, TT off | 1,464,615 | 1,518,604 | +3.69% | 3.56% | 644,797 |
| Windows MSVC | Horizon, TT on | 1,645,505 | 1,784,630 | +8.45% | 7.80% | 84,814 |
| Windows MSVC | Horizon, TT off | 1,854,872 | 1,904,776 | +2.69% | 2.62% | 102,208 |
| WSL GCC | QS on, TT on | 2,179,685 | 2,241,379 | +2.83% | 2.75% | 3,620,143 |
| WSL GCC | QS on, TT off | 2,304,344 | 2,370,402 | +2.87% | 2.79% | 4,729,187 |
| WSL GCC | QS off, TT on | 2,398,273 | 2,497,183 | +4.12% | 3.96% | 4,116,309 |
| WSL GCC | Tactical, TT on | 1,656,898 | 1,713,465 | +3.41% | 3.30% | 624,334 |
| WSL GCC | Tactical, TT off | 1,715,031 | 1,777,881 | +3.66% | 3.54% | 644,797 |
| WSL GCC | Horizon, TT on | 1,967,716 | 2,038,084 | +3.58% | 3.45% | 84,814 |
| WSL GCC | Horizon, TT off | 2,163,560 | 2,219,813 | +2.60% | 2.53% | 102,208 |
| Windows Clang | QS on, TT on | 3,009,988 | 3,100,155 | +3.00% | 2.91% | 3,620,143 |
| Windows Clang | QS on, TT off | 3,199,690 | 3,290,574 | +2.84% | 2.76% | 4,729,187 |
| Windows Clang | QS off, TT on | 3,759,808 | 3,959,149 | +5.30% | 5.03% | 4,116,309 |
| Windows Clang | Tactical, TT on | 2,014,458 | 2,047,457 | +1.64% | 1.61% | 624,334 |
| Windows Clang | Tactical, TT off | 2,100,917 | 2,188,209 | +4.15% | 3.99% | 644,797 |
| Windows Clang | Horizon, TT on | 2,655,978 | 2,770,083 | +4.30% | 4.12% | 84,814 |
| Windows Clang | Horizon, TT off | 2,839,742 | 2,948,037 | +3.81% | 3.67% | 102,208 |
| WSL Clang | QS on, TT on | 3,626,946 | 3,727,420 | +2.77% | 2.70% | 3,620,143 |
| WSL Clang | QS on, TT off | 3,908,060 | 4,043,872 | +3.48% | 3.36% | 4,729,187 |
| WSL Clang | QS off, TT on | 4,227,502 | 4,402,409 | +4.14% | 3.97% | 4,116,309 |
| WSL Clang | Tactical, TT on | 2,367,873 | 2,472,320 | +4.41% | 4.22% | 624,334 |
| WSL Clang | Tactical, TT off | 2,513,263 | 2,623,581 | +4.39% | 4.20% | 644,797 |
| WSL Clang | Horizon, TT on | 3,168,762 | 3,245,329 | +2.42% | 2.36% | 84,814 |
| WSL Clang | Horizon, TT off | 3,630,329 | 3,743,914 | +3.13% | 3.03% | 102,208 |

## Correctness and layout

Each experiment: 5,592 search observations, 2,796 paired searches and 10,140 completed-depth comparisons; zero normalized score, PV, move, node-count or reported search-metric differences. Optimized results also matched accepted standard results. 54,136 PV moves audited in each experiment. Native development tests 132/132 per experiment; standard controller 109/109 and GUI 15/15 on each platform. Actual ABI v1 rejection passed. Optimized PGO training: 480 searches in four independent builds. Depth-3 tactical matches 15/204 and exact mates 9/32 in both arms; no strength gain is claimed.

| Layout | Main | Candidate |
|---|---:|---:|
| SearchScore | 24 bytes | 4 bytes |
| Optional SearchScore | 32 bytes | 8 bytes |
| TT entry | 240 bytes | 216 bytes |
| TT allocation (excluding fixed metadata) | 15 MiB | 13.5 MiB |
| TT entries | 65,536 | 65,536 |

## Evidence provenance

- standard: `CELab-S2.0-centipawn-evidence-s20-20261125T150808Z-3a209c5d.zip`; SHA-256 `84db03af18859d8e2508c67289b3f90fcf233ab50981f2163e110837681e2c92`; 257 evidence-manifest entries and 93 kit hashes verified.
- optimized: `CELab-S2.0-COMP1-evidence-s20opt-20261125T160201Z-93a6eaa1.zip`; SHA-256 `6d77e7a8634f490e4a407fc63b9811d62f7948d5efddbe8f82c06e87b246640e`; 478 evidence-manifest entries and 118 kit hashes verified.

## Integration limitations

Independent PGO retraining may influence individual performance deltas. These results apply to the tested workload and default material values; arbitrary historical sub-centipawn configurations are not automatically equivalent. ABI v1 modules and legacy pawn-unit parameter keys are rejected; re-register and explicitly migrate evaluator settings. Historical persisted records must not be rewritten. Development excluded only `m76_policy_build`; authoritative Full CI must include it, WASM, native Debug/Release/Profile, controller, GUI, desktop/browser and harness. The historical approved reference is not replaced. Score-unit and ABI changes may intentionally alter historical fingerprints; review the actual differences after Full CI.
