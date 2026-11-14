# C1 + C2 + C3 integration: comparison to origin/main

**Status (2026-11-14): Direct committed-source A/B complete; 54-gate Full CI pending.**
The frozen `origin/main` commit, exact benchmarked PR implementation commit,
and Windows/WSL raw evidence are recorded below. Draft PR #49 remains unmerged.

## Accepted, exact source stack

- **C1:** packed 64-bit `CompactUciText` comparisons; avoid re-sorting
  canonical prepared move lists; direct promotion-byte checks.
- **C2-A:** checked `typeid` + `static_cast` for final native types,
  preserving invalid-object rejection.
- **C2-B:** bounded accumulator for evaluation snapshot bitboards, preserving
  validation and the evaluator C ABI.
- **C3-A:** side, castling, legal EP before the 64 squares.
- **C3-B:** C3-A ordering with `memcmp` of exactly the 64 live, uniquely
  represented `PieceCode` bytes; this **supersets** C3-A and is the single
  final implementation.

The five source blobs in the draft branch exactly match the C3-B experimental
overlay in `CELab-C3-evidence-c3952dd8.zip` (verified Git blob identities).
No rejected TT or malformed-move patch from Claude is included.

## Direct committed-head A/B vs origin/main — verified

The performance benchmark compared the **exact clean committed source revisions**:
- Reference `origin/main`: `072032c0d086dbe37a5b7afd796424b283f0ba53` (tree `dc1d2d03482fc8c1401ed3172c3c0b18a680bd23`).
- Accepted C1+C2+C3-B implementation: `fb25990f3b8a32114d916fcd3f67783fb0a3d604` (tree `92a9d2d70c0c0d855e5a41f9d0b5940cf298d2f6`).
- Evidence: `CELab-Cstack-direct-evidence-cs8c6589.zip`, 2026-11-14, externally archived; **1471/1471** SHA-256 manifest entries verified. Any later documentation-only PR commit does not modify the exact tested engine source.

Eleven comparable workloads (nine original + TwofoldReplay d4 and LongHistory16 d4), five TT modes, three interleaved **fresh-process** samples per variant/mode/workload on **each** platform: **660/660 observed benchmark runs**. Both platforms used separate clean exact-commit trees and a common platform-local material evaluator binary for baseline and candidate. Release binaries were uninstrumented; C1/C2/C3 optional experimental controls were not enabled.

### Direct performance, per-platform sum of eleven workload medians

**Positive time saved means the candidate is faster.** Each entry is the ratio of *sums of eleven workload medians*, not mean of percentage changes or a typical-game gain.

| TT mode | Windows baseline ms | Windows PR ms | Windows time saved | WSL baseline ms | WSL PR ms | WSL time saved |
|---|---:|---:|---:|---:|---:|---:|
| Off | 10,198.8 | 8,583.2 | **15.84%** | 8,772.8 | 7,043.8 | **19.71%** |
| Hint-only cold | 8,362.4 | 6,941.5 | **16.99%** | 7,100.9 | 5,701.4 | **19.71%** |
| Hint-only warm | 3,087.7 | 2,552.7 | **17.33%** | 2,611.9 | 2,010.9 | **23.01%** |
| Numeric cold | 7,137.0 | 6,003.9 | **15.88%** | 6,137.7 | 5,005.4 | **18.45%** |
| Numeric warm | 41.7 | 35.8 | **14.29%** | 35.9 | 28.7 | **19.81%** |

Effective NPS ratios at identical visited-node counts: Windows **1.188x** TT-off / **1.189x** numeric cold; WSL **1.246x** TT-off / **1.226x** numeric cold. These are aggregate ratios using summed medians, not instrumented per-node CPU profiles.

**All 55/55 per-workload TT-mode medians improved on Windows and all 55/55 on WSL**, including the earlier C2/C3 Perft5 cold-mode watchpoint. Windows Perft5 Checks d5 numeric-cold: **293.61 to 284.96 ms (2.95% reduction)**; hint-cold: **383.67 to 295.23 ms (23.05% reduction)**. WSL Perft5 numeric-cold: **259.48 to 211.16 ms (18.62% reduction)**; hint-cold: **295.78 to 239.83 ms (18.92% reduction)**. Three-run medians do not guarantee every run or host improves; Windows Perft5 cold samples retain variability.

**Search semantics:** 330 paired raw benchmark observations per platform, zero mismatches for visited nodes, evaluator calls, alpha-beta/TT counters, best move, PV and checksum (660 observations total, 330 paired pairs). No observed search-node reductions; improvements reflect faster identical search work. Reference and candidate evaluator fingerprints and artifacts matched *within* each platform; platform-specific artifacts differ appropriately.

**Local native validation:** baseline and candidate both passed 23/23 native tests in Release and Debug on Windows and WSL: **184/184 native CTest executions**. All standalone C1 packed-key, C2 dispatch/snapshot, and C3 repetition-signature parity checks passed on both platform variants (12/12 executions). These checks include malformed/key text controls, 30,000 synthetic board fixtures and 100,000 randomized repetition pairs. Clean exact source SHA/tree checks matched on both platforms.

**Caveats:** Synthetic numeric warm reuses a pre-populated identical root and should never be extrapolated to full-game performance. Windows process CPU time is coarse, especially sub-millisecond warm runs; the principal comparable measure is wall elapsed time. Eleven chosen search workloads cannot establish playing-strength gains, generalize to every time control or eliminate host variance.

## Measured incremental performance and audit provenance

| Exploratory stage | Windows TT-off saving | Windows numeric-cold saving | Linux TT-off saving | Linux numeric-cold saving |
|---|---:|---:|---:|---:|
| C1 combined vs T1 | 8.35% | 7.68% | 9.15% | 8.84% |
| C2 combined vs C1 | 6.44% | 5.00% | 5.29% | 8.34% |
| C3-B vs C2 (11 workloads) | 2.68% | 3.15% | 5.21% | 5.04% |

Evidence (raw logs, manifest hashes, source checksums, tests and
per-workload medians): `CELab-C1-evidence-c169e35d(4).zip`,
`CELab-C2-evidence-c252a530(1).zip` and
`CELab-C3-evidence-c3952dd8.zip`. They are external archived experimental
evidence, not GitHub CI attachments.

- C1 exploratory Windows/WSL native and 945/945 benchmark observations passed.
- C2 exploratory Windows/WSL native and 945/945 benchmark observations passed.
- C3 exploratory Windows/WSL native and 990/990 benchmark observations passed.
- All stage-local comparable results preserved best move, PV checksum, nodes,
  evaluator calls and TT statistics. The subsequent direct committed-head A/B
  above supersedes any inferred cumulative percentage.
- **Historical stage-local adverse case (superseded by direct comparison): Windows Perft5 Checks hint-cold C3-B vs C2 was ~10.22%
  slower by three-run median (295.03 -> 325.18 ms) while Linux improved.
  C2 Windows Perft5 hint-cold and numeric-cold also regressed on their
  stage-local medians; retain these explicit watchpoints.

## Remaining release gate — authoritative Full CI

The **direct integrated A/B requirement is completed and verified** on the
clean committed code revision identified above. It does **not** substitute for
full-system integration qualification, and the documentation-only update made
after benchmarking requires use of the final clean PR head in Full CI.

- [x] Direct Windows/WSL baseline-vs-PR-source A/B, 11 workloads, five modes, repeated runs, complete logs/hashes and adverse-outlier review.
- [x] Native Release and Debug CTest 23/23 for both revisions on each platform, plus targeted C1/C2/C3 parity tests.
- [ ] Obtain separate user approval for clean-commit model-free Full CI; run
  `scripts/validation/full-ci.ps1 -PlanOnly`, then the existing **54-gate**
  `scripts/validation/full-ci.ps1` exactly once. Validate Windows+WSL,
  GUI/controller, WASM, deterministic records, source/evaluator fingerprints,
  and all reference exceptions.
- [ ] Independently review CI results, including any
  `reference_review_required` (exit 2); do not silently rewrite the sealed
  historical reference or add GitHub-hosted CI.
- [ ] Obtain separate user approval for merge. Draft status remains.

**No authoritative Full CI or playing-strength claim is made here.**
