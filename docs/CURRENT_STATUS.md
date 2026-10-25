# Chess Engine Lab — Current Status and Next Actions

**Latest handoff (2026-10-24 UTC):** M7.4a.2 merged through PR #24 at `f3aaee0a284b206a347786a8e5418c3773654a40`. M7.5a profiling is complete, reviewed and accepted. ChatGPT approved the original M7.5b implementation, tests and controlled performance evidence. Windows is on `feature/m7.5b-native-leaf-count` at that merged baseline, with three approved implementation/test changes and the two documentation updates uncommitted; the index is empty. Linux is clean on `main` at the same baseline. M7.5b has **no authoritative 54-gate Full CI yet**; commit and publication require separate authorization. Section 4 records the measured benefits, unresolved TT uncertainty and local-only provenance. The dated pre-commit checkpoints below are historical, not current branch or acceptance status.

**Earlier M7.4a.2 pre-commit review checkpoint (2026-10-24 UTC):** ChatGPT completed M7.4a.2 and TT investigation review and decided to retain the original guarded native leaf implementation unchanged. Windows is on `feature/m7.4a2-guarded-leaf`, base `1d461dfab38e2f105ecb67529192c34b880f3efc`, with the source/test patch uncommitted. The isolated in-place signature variation was not adopted. Section 4 records exact correctness/work equivalence, repeated workload-specific TT-off gains and local-only evidence. **TT-enabled neutrality has not been demonstrated; regression root cause remains unresolved.** This documentation handoff reruns no suites/benchmarks and claims no authoritative Full CI pass. Commit, synchronization and acceptance validation need separate authorization.

**Earlier M7.4a.1 validation checkpoint (2026-10-23 UTC):** M7.4a.1 depth-zero ordering remains uncommitted on `feature/m7.4a1-leaf-ordering` at base `b04a198f543aad2a1eb9d9475db23779b010ca71`. ChatGPT independently approved the production/regression diff; controlled performance validation is now complete. Interleaved ABBA/BAAB measurements support a repeatable local improvement for depth-six TT-off/cold and depth-five off/cold/warm; depth-six warm remains inconclusive. Section 4 records the paired results and limitations. Earlier native/WASM correctness results remain historical validation, not a new Full CI pass. No M7.4a.2 deferred encoding or TT bookkeeping change is included.

**Earlier investigation checkpoint (2026-10-23 UTC):** fetched `origin/main` was `b04a198f543aad2a1eb9d9475db23779b010ca71`, the verified merge of [PR #22](https://github.com/yctekmen-sudo/chess-engine-lab/pull/22). Local CI infrastructure and hosted-workflow retirement are merged. The M7.4 baseline/design investigation completed on `investigation/m7.4-lazy-leaf` before production changes. ChatGPT subsequently approved that investigation and authorized the narrowly scoped M7.4a.1 implementation below; deferred encoding and further changes remain separate proposals.

**Earlier migration checkpoint (2026-10-23 UTC):** [PR #21](https://github.com/yctekmen-sudo/chess-engine-lab/pull/21) merged at `bae9da7914a769bdffcabe31fc78c1f2e63e8af7`. The user reported manually disabling all four hosted workflows. Local Windows 11 + WSL2 Ubuntu 24.04 CI is the validation process; GitHub retains source control, PRs and reviews. PR #22 subsequently retired the definitions and made their absence explicit in harness reports. No GitHub settings or branch-protection changes were made or reverified during this investigation.

**Prior authoritative validation:** at `6a0b3848f317c8f16b0f5fdee4878aa039d7b304`, all 54 gates passed, exit 0, with both checkouts clean and unchanged; PR #21's merge `bae9da7` has the same tracked tree. Retirement head `9b5ece5` subsequently passed the full matrix as recorded below. These are prior CI results, not a new Full run at the M7.4 investigation revision.

**Earlier verification:** local CI infrastructure prepared and hardened 2026-10-22 from `main` at `bead04a312180d8ce3ba3c48980659da273487c7` (merged PR #20). The hardening rerun passed all 54 command gates, including 50 harness regressions per platform, with both worker exits zero; uncommitted Windows changes made that combined result non-authoritative. UTC report directories below cross into 2026-10-23. Historical profiling evidence remains attributed to its recorded M7.3 revision. This is a dated snapshot, not live data.

**Repository:** private [yctekmen-sudo/chess-engine-lab](https://github.com/yctekmen-sudo/chess-engine-lab).

**Stable context:** [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md). Read the root [AGENTS.md](../AGENTS.md) before implementation. ChatGPT supports architecture/design review; Codex implements only within the user's authorized scope.

## 1. Repository checkpoint: M7.5b, 2026-10-24 UTC

The active Windows/Linux states were inspected read-only for this documentation
handoff. GitHub PR #24 was reverified merged on 2026-10-24 at 19:37:10 UTC;
earlier PR/CI rows retain their original provenance. Tests and benchmarks below
are checked existing evidence, not newly executed validation.

| Item | Verified state |
| --- | --- |
| M7.4a.2 integration / M7.5b baseline | [PR #24](https://github.com/yctekmen-sudo/chess-engine-lab/pull/24) merged 2026-10-24; `f3aaee0a284b206a347786a8e5418c3773654a40`, current Windows feature HEAD and clean Linux `main` |
| M7.2 | Merged; single-PV PLAY-only root search and controller root draw arbitration, including fixed-depth cancellation validation |
| M7.3 implementation | **Merged into `main`** at `f0a5affee25a333ff23df2343277e052a2c2ae81` |
| M7.3 PR | [#19: M7.3 (draft): Reuse prepared legal moves and annotate draw claims](https://github.com/yctekmen-sudo/chess-engine-lab/pull/19), **closed and merged**; “draft” remains in its historical title |
| Profiled M7.3 source / merged PR head | `54ec7d93c759e9ac7f6b8115fb5ef1985972863f`; measurements below remain attributed to this revision, not newly measured at the merge commit |
| PR #19 scope | Six files: `CMakeLists.txt`, `engine/src/chess_core.cpp`, `engine/src/search/search.cpp`, `docs/m7.3-search-node-design.md`, `tests/search/search_performance.cpp`, `tests/search/run_performance.mjs` |
| M7.3 PR-head CI | Native Debug/Release/Profile, WebAssembly correctness, M6 controller, and M7 GUI including Windows smoke: completed successfully; links below |
| Local M7.3 validation | Historical profiling-handoff log records native Release CTest **12/12 passed**, 0 failed, 4.68 s; newly executed local CI results are recorded in section 4 |
| Profiling investigation | **Completed and reviewed**; accepted with M7.3 per the user's handoff and PR #19's recorded correctness/performance acceptance notes; raw artifacts remain local |
| Next action | **M7.5b source/test and performance review approved**; final five-file documentation handoff. Commit, clean aligned Full CI and publication remain pending applicable separate authorization; TT-enabled neutrality remains unproven |
| Documentation handoff | [PR #20](https://github.com/yctekmen-sudo/chess-engine-lab/pull/20), merged into the local baseline above |
| Local CI implementation | [PR #21](https://github.com/yctekmen-sudo/chess-engine-lab/pull/21) merged at `bae9da7`; prior authoritative validation at its head `6a0b384` |
| Infrastructure retirement | PR #22 merged at `b04a198`; authoritative Full local CI at its head `9b5ece5f527fb154a669485e863f9b047cd6a9ef` passed 54/54 gates, exit 0, 441.429 s; local evidence `work/ci/20261023T051529887Z-ec9f96f3/` |
| Approved investigation | `investigation/m7.4-lazy-leaf` at `b04a198`: fresh five-repetition Release baseline and first-step design reviewed and approved by ChatGPT |
| Active implementation | `feature/m7.5b-native-leaf-count` at `f3aaee0`, dirty: three approved implementation/test files plus `docs/development.md` and this status file; no commit/publication authorized |
| M7.5a investigation | Completed, reviewed and accepted; measurements remain attributed to its separate frozen baseline session |
| M7.5b acceptance | Focused native/WASM correctness and controlled crossover complete; no authoritative Full CI on an M7.5b commit yet |

The successful PR-head CI results below belong to M7.3 PR #19 at `54ec7d9`; they are not proof of a pass at the merge commit or an updated PR #20 head. Check CI for the actual revision under review.

The approved [M7.3 design](m7.3-search-node-design.md) and implementation are now on `main`. Its original draft-era header is historical; the verified merge and PR #19's acceptance notes establish the completed milestone state. The preceding merged baseline was `17edf1e438370d8fc1dc17db66982b3fd0d5cf85` (PR #18 / M7.2).

Verified successful PR-head workflows:

- [Native Debug/Release/Profile, run 39823918573](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918573).
- [WebAssembly correctness, run 39823918638](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918638).
- [M6 headless controller, run 39823918536](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918536).
- [M7 GUI and Windows smoke, run 39823918527](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918527).

The push-triggered native workflow [39823909737](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823909737) also completed successfully on attempt 2. Its earlier stale/in-progress display is historical, not a current failing check. Success applies to the recorded M7.3 SHA; recheck after any new commit. The Windows smoke job does not build the native C++ engine or establish Windows browser-gameplay coverage; local native results are separate.

### Protect existing local work

At the earlier M7.4a.2 pre-commit handoff, preserve the original source/test patch and all
ignored profiling, benchmark and CI artifacts. The TT investigation verified
3,743 historical files unchanged; preserve its audit and manifest too. Linux
was then clean on `main` at `1d461df`. Those checkout states are historical.

For the current M7.5b handoff, preserve all **5,555** previously verified historical
files, the **213** frozen local artifacts and the complete original ignored
`work/m7.5b/implementation-20261024T210345Z/` directory. Linux is now clean on
`main` at `f3aaee0`; this task does not modify it. Its stale local `origin/main`
tracking ref remains at `1d461df` (status reports ahead 2); no fetch or sync was
performed. Existing branches, stashes, remotes and worktrees are preserved.
The paragraphs below record earlier checkout states as history.

At the earlier retirement-preparation checkpoint, both checkouts used
`chore/retire-github-actions` from `bae9da7`. Original
`feature/local-ci-infrastructure` refs remain at `6a0b384`, with no reset or stash.
The pre-existing Windows status note was explicitly carried across the branch
switch and preserved separately under `work/ci/retire-actions-20261023T043737044Z/`.
Linux was clean before switching; the verified merge was fetched from the Windows
checkout's existing local remote after a stalled Linux HTTPS fetch was terminated.
The earlier `work/project-handoff/` documentation worktree is retained unchanged.
The harness itself never switches/resets/synchronizes checkouts.

At the M7.4 investigation's start, Windows was clean on `main` at `b04a198`; a new
`investigation/m7.4-lazy-leaf` branch was created there. Linux remains clean on
`chore/retire-github-actions` at `9b5ece5`, unchanged by this investigation. Existing
worktrees/stashes and all 257 M7.3 profiling files were preserved. The only tracked
investigation edit is this dated status update, written after clean-source timing.

The M7.4a.1 implementation explicitly carried that existing status work onto its
new feature branch and preserved a copy in its ignored evidence directory. Linux
remains unchanged. The implementation's tracked inventory is search.cpp,
m72_search.cpp, development.md and this status file; older profiling/baseline
artifacts remain intact.

Earlier handoff notes recorded stash object `3669920a80ede34c7943adfdcfc7c3dcd807b7fe`
and untracked M7.2 investigation files. At this infrastructure task's start,
`git status` was clean and `git stash list` was empty. Those old inventories are
historical; inspect actual status before attempting any restoration. No stash or
existing profiling artifact was modified by this task.

The earlier documentation handoff verified 178 unrelated files and 257 profiling
artifacts unchanged. Preserve existing evidence; those historical checks do not
replace current source snapshots in each new local CI report.

## 2. What merged M7.3 implements

Source inspection at `54ec7d9` confirms:

- `PreparedNode` owns a snapshot, legal-move list and repetition identity for one position encounter; iterative root preparation is reused.
- Legal EP is obtained from that prepared list rather than generating another list for repetition normalization. Evaluator snapshots retain raw EP.
- Native generated moves carry position/undo-frame certificates; encode/make avoid redundant legality generation when the certificate matches. External/foreign/stale inputs retain validation, and strict undo restores the matching parent frame.
- Native mate/stalemate classification uses the prepared list, consulting check state when empty. Synthetic adapters retain their independent status/clock semantics.
- Claim-aware nodes retain immediate and intended-claim metadata. A sound repeated-history-target filter avoids most speculative children. Intended fifty-move claims use halfmove/pawn/capture facts.
- M7.2 root PLAY-only policy, terminal precedence, canonical score/PV behavior, evaluator ABI and exact TT context requirements are preserved in the existing tests.

**Known follow-up costs:** rare intended-threefold candidates still prepare and discard speculative children; there is no general retained prepared-child cache. Historical repeated-target scanning remains pairwise, and ABI conversion is repeated for TT probe/store. These are observed details of the accepted M7.3 baseline, not implementations of M7.4 proposals. Passing the tested matrices does not prove all untested histories or positions equivalent.

## 3. Completed profiling: provenance and performance

The investigation changed diagnostic infrastructure only. Canonical runs use uninstrumented native **Release**; copied sources supply separate operation counters; separate optimized Profile binaries/PDBs supply samples. Runs were sequential, with no competing benchmark/build/test. All artifacts are retained locally under `work/m7.3/profiling/`, which Git ignores; **they are not included in this documentation PR or in PR #19**. A fresh clone or ChatGPT session needs those artifacts supplied separately to inspect raw data.

Evidence locations, relative to the implementation checkout:

| Local artifact | Contents checked |
| --- | --- |
| `work/m7.3/profiling/performance-investigation.md` | Final interpretation, methodology, limitations, future candidates and correctness gates |
| `work/m7.3/profiling/results.csv` | 54 measured Release/Profile rows; elapsed/CPU time, nodes, evaluations, TT statistics, exact result checksums and final PVs |
| `work/m7.3/profiling/counters.csv` | Actual counters for 12 diagnostic invocations, including warm-table population and ordinary/speculative preparation attribution; not header-only |
| `work/m7.3/profiling/sampling.csv` | Exclusive/self, overlapping inclusive and exclusive owner-category samples; primary 20 ms sampling pass |
| `work/m7.3/profiling/cpu-breakdown.json` | TT-off/cold category percentages, estimated Profile CPU ms/search, nominal sampling uncertainty and batch ranges |
| `work/m7.3/profiling/environment.json` | Toolchain, OS/CPU, compiler flags and artifact/PDB SHA-256 identities |
| `work/m7.3/profiling/diagnostics/results.json`, `samples-lowoverhead/results.json`, `samples/results.json` | Raw diagnostic/sample result attribution and checks; original higher-rate sampling retained separately |
| `work/m7.3/profiling/tool-validation.json`, `ctest-release.log` | 145 validation checks, including 13 negative cases; local native Release 12/12 pass |
| `work/m7.3/profiling/reproduce.ps1`, diagnostic sources and scripts | Serial reproduction commands, checked instrumentation anchors, source hashes and sampling policy |

Workload: initial FEN with complete starting history, iterative depths 4/5/6, MultiPV=1 PLAY-only root, previous-PV ordering and alpha-beta enabled, default material evaluator, requested numeric TT budget 8 MiB. Actual table allocation is 4,522,172 bytes / 4,096 entries. Canonical M7.3 runs use one warmup plus three measured repetitions; warm mode also explicitly populates the table. Historical M7.2 depth-six timings used zero additional warmups. Table modes are independent; cold clears outside timing.

Windows x64 10.0.26200, Intel Core i7-14700F, MSVC 19.44.35229.0, CMake 4.4.4, Ninja 1.13.2, Node 24.22.0. The desktop was not isolated or pinned to a CPU/frequency. Release and Profile have different inlining/link settings; sampled Profile milliseconds are not canonical Release timings.

| Depth | M7.2 Release off, historical | Earlier M7.3 Release off | Repeated M7.3 Release off | Repeated cold | Repeated warm |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 | 99.330 ms | 9.366 ms | **9.060 ms** | 11.140 ms | 1.113 ms |
| 5 | 1,580.016 ms | 145.757 ms | **151.720 ms** | 150.702 ms | 151.096 ms |
| 6 | 17,862.088 ms | 1,944.673 ms | **1,953.221 ms** | 2,295.142 ms | 2,288.014 ms |

These are medians of three runs. Repeated M7.3 medians differ from the supplied M7.3 baseline by -3.28% to +5.56% across workloads. Earlier cold/warm depth-six medians were 2,174.328 / 2,182.477 ms. The new TT-off speedup over historical M7.2 is about **9.14x**; cold/warm are approximately **17.5% / 17.1% slower than current TT off** in this opening/material workload, not universally.

Depth-six off visits **381,938** nodes and evaluates **328,475** times in both M7.2 and M7.3. Cold visits **355,879** nodes and evaluates **301,184** times. All completed-depth checksum/PV comparisons match across canonical, counter and sampled runs. Checksums for depths 4/5/6 are `14153966329163974846`, `12170758485630949694`, `6361699534327624292`; they include exact structured scores, decisions, PV endings/intended claims and complete PVs. The depth-six PV remains `a2a3 b7b6 a1a2 c8a6 a2a1 a6e2`.

Release executable SHA-256: `263e83ff39451a8a5fe87e762402740653a30fa35fbc766d5162a8339b9e95f7`.

Release material SHA-256: `4b9c491bb12441fcc450a21d4437466ecb3024273df032d41201438d89dfabd8`.

These match the supplied M7.3 baseline. Baseline evidence is local `work/m7.3/benchmark/results.json` / `results.csv`; historical M7.2 evidence is in the preserved, untracked `docs/search-performance-*` files. Source/artifact SHA-256 identity and result consistency checksums serve different purposes.

### Significant observed operation reductions

| Depth-six operation | M7.2 off, historical | M7.3 off | M7.2 cold, historical | M7.3 cold |
| --- | ---: | ---: | ---: | ---: |
| Full underlying legal generations | 88,751,792 | **385,132** | 82,963,166 | **356,926** |
| `require_legal` validations | 85,734,948 | **0** | 80,142,285 | **0** |
| Move encodings | 75,912,579 | 9,507,358 | 71,006,204 | 8,802,266 |
| Speculative intended-claim children | 9,440,437 | **3,199** | 8,780,208 | **1,052** |
| Snapshot allocations | 19,644,748 | 385,135 | 18,272,172 | 356,929 |
| TT history push/pop, each | 0 | 0 | 9,136,081 | 355,873 |
| Evaluator ABI conversions | 328,477 | 328,477 | 1,012,932 | 1,012,932 |

Every actual full generation is attributed to one preparation. Generations per visited node are **1.008362614 off** / **1.002942011 cold**: ordinary children plus exceptional speculative children plus one prepared root, with that root reused across six iterations. No extra generation comes from status, encode/make legality checks or independent EP normalization in this measured search. No eligible intended claim was found by these exceptional opening probes; that is not a rule that intended claims never occur.

**Depth zero remains significant.** Off has 328,475 leaf entries; cold has 302,740, of which 301,184 evaluate. Preparation, legal-list encoding/sorting and required rule metadata occur before recursive TT probing or leaf evaluation. Leaf preparation encodes 8,210,720 / 7,558,366 moves. This is measured work and inspected control flow; the benefit and correctness of deferring it have not yet been established.

### Measured CPU breakdown and limits

Primary sampling used two batches of 20 depth-six searches per TT state at a nominal 20 ms wait: **4,459 off / 4,983 cold samples**, zero unwalkable stacks, about 4.6–4.7% suspended wall time. All 80 primary sampled searches, 40 retained higher-rate searches and 12 counter invocations were independently checked against uninstrumented results. Existing tool-validation evidence records **145 passing checks**, including 13 intentional failure cases; this documentation task read the evidence and did not rerun profiling.

Each owner sample is assigned once to the nearest recognized operation on its ordered stack. These exclusive categories sum to 100%; inclusive paths overlap and must not be added. CPU milliseconds below are estimates from the sampled Profile main-thread CPU, **not Release elapsed time**.

| Exclusive owner category | TT off | Estimated Profile CPU ms/search | Cold TT | Estimated Profile CPU ms/search |
| --- | ---: | ---: | ---: | ---: |
| Legal generation and move-list construction | **68.27%** | 1,489.9 | **58.58%** | 1,398.3 |
| Make/undo | 1.86% | 40.6 | 1.87% | 44.6 |
| Snapshots/conversion | 3.41% | 74.4 | 5.56% | 132.7 |
| Repetition/claim preparation | 7.29% | 159.1 | 4.27% | 102.0 |
| TT/history | 0% | 0 | **14.69%** | 350.7 |
| Evaluation | 1.95% | 42.6 | 0.96% | 23.0 |
| Other/unattributed | **17.22%** | 375.9 | **14.07%** | 335.8 |

The largest legal-list category has nominal sampling uncertainty about +/-1.37 percentage points; systematic error from inlining, scheduling and attribution is not captured by that estimate. Full batch ranges and uncertainty are in `cpu-breakdown.json` and the report. Unknown subcomponents remain unmeasured. An earlier 3 ms sampling pass paused the target for roughly 24–25% of wall time; it is retained for sensitivity comparison, not pooled into the primary breakdown.

Notable self-sample groups (off/cold): heap allocation/free exports **21.8% / 18.9%**, coordinate-string conversion **8.2% / 6.9%**, move sorting **8.2% / 6.6%**, `ordered_moves` body **4.8% / 3.8%**, and cold TT hashing plus history tuple comparison **7.8%**. These are components of the categories above, not additional time. OS export-only symbol attribution is approximate. Underlying movegen stack unions are only about **2.56% / 2.83%**, versus much larger list materialization/encoding/sorting paths. Exact TT history verification is about 1.4 ms in the canonical cold run; broader history/hash bookkeeping is the larger observed TT cost.

The historical M7.2 `Traversal::claim` share of about 84.7% was **inclusive**, overlapping and measured before M7.3. It is not a present-day hotspot share.

## 4. Local CI and performance milestone checkpoints

**Historical local CI implementation snapshot:** the following infrastructure
task and its validation are retained with their original provenance. The current
M7.5b documentation-only handoff is recorded later in this section.

The current authorized implementation task is local CI infrastructure, not search
optimization. See [local-ci.md](local-ci.md) for the new entrypoint, coverage map,
source-identity rules and reports. Initial inspection found Windows and WSL clean
at the same baseline SHA. Subsequent uncommitted infrastructure/documentation work
must be disclosed in validation; it cannot produce a clean-commit acceptance pass.
The existing M7.3 profiling artifacts and measurements remain unchanged.

The following initial implementation results precede the review hardening below.
They remain historical evidence, including the then-current 31 harness checks.
Executed on Windows 11 x64 / PowerShell 5.1 / MSVC 19.44.35229 / Node 24.22.0 /
CMake 4.4.4 / Ninja 1.13.2, and WSL2 Ubuntu 24.04 / GCC 13.3.0 / Node 24.10.0 /
CMake 3.28.3 / Ninja 1.11.1. WASM used pinned emsdk 4.0.10 and SDK Node 22.16.0.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Full
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Quick -Areas infrastructure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Quick -Areas wasm
```

| Executed check | Windows | WSL Ubuntu |
| --- | --- | --- |
| Native Debug CTest | 12/12, 92.413 s | 12/12, 35.692 s |
| Native Release CTest | 12/12, 4.610 s | 12/12, 2.765 s |
| Native Profile CTest | 12/12, 4.868 s | 12/12, 2.944 s |
| Controller locked install/build/typecheck/lint/test | Passed; 107/107 tests | Passed; 107/107 tests |
| GUI locked install/typecheck/lint/build/test | Passed; 14/14 tests | Passed; 14/14 tests |
| Desktop static-assets/registration/orientation/IPC smoke | 2/2 | Not a Linux gate |
| Playwright Chromium installation / E2E | Not a Windows gate | Passed; 5/5 E2E tests |
| WASM configure/build/CTest | Not a Windows gate | 11/11, 52.700 s |
| Harness regression checks | 31/31 | 31/31 |

Full evidence: ignored, local-only
`work/ci/20261022T211452435Z-61e84ee1/report.json` and `report.md`, with every
command, tool version, duration, test count and full step log. **54/54 gates
passed**, zero failed/blocked/skipped; recorded combined elapsed time **433.424 s**.
Both checkouts retained the same SHA and unchanged working contents/index during
the run. Windows was dirty; Linux was clean. The honest combined verdict was
**`non_authoritative`, exit 2**, not a clean-commit acceptance pass.

Subsequent reporting/source-fingerprint refinements (MSVC's stderr banner,
BOM-free missing-Node reports with the SHA, and dangling-link handling) were
revalidated with infrastructure quick mode on both platforms, **31/31 each**;
evidence: `work/ci/20261022T212256800Z-fbe0d11f/`. The full command matrix and
production sources were unchanged. The isolated WASM quick run also passed
11/11 in 53.529 s (`work/ci/20261022T211243420Z-3882a169/`). Quick results never
constitute milestone acceptance.

`node --check scripts/ci/suite.mjs`, PowerShell AST parsing, and
`wsl.exe -d Ubuntu-24.04 --exec bash -n /mnt/c/cursor/chess-engine-lab/scripts/ci/run-linux.sh`
passed. An additional Linux Node 24 fixture check
(`work/ci/verify-symlink.mjs`) confirmed that changing a dangling symlink invalidates
the source fingerprint. Final diff/whitespace review includes all six new files
as well as the three modified documentation files.

Intentional negative runs returned blocked exit 2 for missing Linux Node
(`-LinuxNodeVersion 99.0.0`, `work/ci/20261022T211437425Z-1a37bea8/`), missing SDK
(`-Mode Quick -Areas wasm -EmsdkRoot /nonexistent/chess-lab-ci-sdk`,
`work/ci/20261022T211040003Z-0529c5ed/`), and Windows Node removed from the child
process PATH (`work/ci/20261022T212352343Z-6e8b63e4/`); no tools were uninstalled.
Harness regressions also cover different commits, index/working-tree mutations,
missing gates/platforms, skipped tests and nonzero process exits.

Earlier attempts are retained: `20261022T210118532Z-63d5e424` found an inventory
parser bug caused by SDK stderr; `20261022T211117607Z-fafedd12` was deliberately
interrupted to correct Windows CTest summary parsing. Neither is reported as a
pass. Both bugs were fixed before the completed full run above. No engine/test
assertions, existing timeouts or bounded WASM selections were changed.

The checkouts were not synchronized, pulled or reset. Three user-authored,
untracked handoff-prompt files remain outside this change. Existing profiling
artifacts, stashes and the separate documentation worktree were preserved.
There is no environmental blocker in the completed local matrix. It used
installed OS libraries and existing build/browser caches; this does not establish
fresh-machine provisioning, hosted Windows Server 2022, macOS or remote required
statuses. GitHub workflows and protection rules remain unchanged. Review and
separate publication authorization are still required.

### Local CI review hardening: completed 2026-10-22

Three findings were corrected in `scripts/ci/suite.mjs`, `suite.test.mjs` and
`run-all.ps1`; guidance is updated in `local-ci.md` and this status file.

- Reviewed minimum counts are controller 107 and GUI 14 per platform,
  Playwright 5, Windows smoke 2, and harness 50 per platform (31 existing plus
  19 new regression cases). Additions remain allowed. Both summary parsing and
  combined acceptance reject reduced/missing coverage; reports include the floors.
- Snapshots record `assume-unchanged` and `skip-worktree` flags. Any flagged path
  prevents authoritative full acceptance, even if unchanged. Content and flag
  changes remain detectable during validation. Snapshot Git reads disable optional
  index writes; temporary-repository regressions verified index bytes unchanged
  on Windows and Linux, including modified files concealed from Git diff.
- The coordinator persists `worker-exits.json` immediately after each worker.
  Both exits must be zero for acceptance, independent of successful report files.
  Missing/malformed exits block acceptance. Abnormal exits fail; an exit-2 worker
  with an explicitly blocked report retains blocked semantics.

Executed commands:

```powershell
node scripts/ci/suite.test.mjs
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Quick -Areas infrastructure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Full
```

The full rerun used the same Windows/WSL toolchains and same unchanged baseline
SHA as above. All **54/54 command gates passed**, zero failed/blocked/skipped,
with worker exits **Windows 0 / WSL 0**. Recorded combined duration: **443.871 s**.

| Test coverage | Windows | WSL Ubuntu |
| --- | --- | --- |
| Native Debug/Release/Profile | 12/12 each; 92.599 / 4.768 / 4.979 s | 12/12 each; 36.016 / 2.878 / 3.066 s |
| Controller build/typecheck/lint/test | Passed; 107/107 | Passed; 107/107 |
| GUI typecheck/lint/build/test | Passed; 14/14 | Passed; 14/14 |
| Desktop smoke | 2/2 | Not a Linux gate |
| Chromium E2E | Not a Windows gate | 5/5 |
| WASM configure/build/CTest | Not a Windows gate | 11/11; 52.625 s |
| Harness regressions | 50/50 | 50/50 |

Ignored, local-only evidence:

- Full rerun: `work/ci/20261023T025538833Z-1f33b1b7/report.json` / `report.md`.
  This includes required coverage floors, observed counts, both zero worker exits,
  source/index/flag snapshots, tool versions and complete command logs.
- Quick infrastructure: `work/ci/20261023T025436275Z-9e4e27e6/`, exit 0,
  `quick_pass`, with 50/50 regressions per platform.
- Negative coordinator fixtures:
  `work/ci/hardening-20261022/worker-exit-negatives/{windows,linux}/`.
  The actual PowerShell invocation/persistence helpers ran a controlled Windows
  Node worker and a WSL Node worker. Each wrote a synthetic successful report,
  then exited **7**; the helpers captured **7**, and the actual combiner returned
  **failed, exit 1, authoritative=false**. Reproduction scripts are retained in
  that ignored hardening directory. These are deliberate negative fixtures, not
  failed application tests or real clean-commit passes.

The full combined verdict is **non_authoritative, exit 2** because Windows remains
dirty/untracked; Linux is clean. Both source snapshots stayed unchanged during
the run, neither had special index flags, and no checkouts were synchronized.
Authoritative end-to-end acceptance still requires clean checkouts at the same
commit. The conservative flag policy also excludes unchanged flagged/sparse
checkouts. There were no environmental blockers in this rerun.

Node syntax and PowerShell AST parsing passed. Final whitespace/diff review covers
new files as well as tracked changes. Profiling evidence and prior review/log
artifacts are preserved; production code, existing test commands/assertions,
timeouts, the bounded WASM matrix and GitHub Actions are unchanged. Nothing has
been staged, committed, pushed, published as a PR or merged.

### Historical publication checkpoint: 2026-10-23 UTC, before PR #21 merge

At this earlier checkpoint, two commits were pushed to `origin/feature/local-ci-infrastructure`: the hardened
runner (`2a1949f`) and approved handoff-document update (`6a0b384`). PR #21 is open
against `main`; it has not been merged and auto-merge was not enabled.

The approved handoff document was preserved. The WSL feature checkout was safely
fast-forwarded to `6a0b3848f317c8f16b0f5fdee4878aa039d7b304` before the final full
run. Both checkouts were clean and unchanged at that same SHA. All 54 gates passed
in 435.208 s; both workers and the overall process exited 0. The combined report
records `passed` and authoritative `true`. Evidence remains local-only under
`work/ci/20261023T034049466Z-19715372/` (`report.md`, `report.json`, and full logs).

This post-publication status update remains local and uncommitted; the published
PR head is still exactly the validated SHA. Hosted workflow outcomes are separate
from the verified local result. Disabling GitHub Actions, changing protection
rules, committing this note, or merging requires separate authorization.

### Workflow retirement: 2026-10-23 UTC

PR #21's merge is verified at `bae9da7`. The user reports that all four hosted
workflows were manually disabled. This cleanup removes the four YAML definitions;
their historical contents remain in Git, without duplicate archives. GitHub
settings and branch protection are outside this cleanup's authorization.

The only runtime dependency found was unconditional workflow hashing in
`scripts/ci/suite.mjs`. Reports now record deterministic ordered path/status
evidence for all four historical definitions: `absent`, or `present` with SHA-256.
Unexpected read failures still block validation. Mandatory harness hashes, source
identity, coverage floors, worker exits and Full/Quick acceptance remain intact.
Four added regressions cover complete absence, retained hashes/order, partial
retirement and unexpected read errors; the reviewed harness floor is now 54.

Executed cleanup validation:

```powershell
node scripts/ci/suite.test.mjs
node --check scripts/ci/suite.mjs
node --check scripts/ci/suite.test.mjs
wsl.exe -d Ubuntu-24.04 --exec bash -n /mnt/c/cursor/chess-engine-lab/scripts/ci/run-linux.sh
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Quick -Areas infrastructure
git diff --check
```

Standalone Windows regressions passed **54/54**, exit 0. Both JavaScript syntax
checks, Bash syntax and PowerShell AST parsing passed, exit 0. Quick infrastructure
passed **54/54 regressions on each platform**, with worker exits **0 / 0** and
overall exit **0**, verdict **`quick_pass`**, authoritative **false**. The two
selected gates passed; the other 52 gates were explicitly `not_selected`.
Combined worker duration was **5.010 s** (Windows harness 2.451 s, Linux 0.396 s).
Local-only evidence: `work/ci/20261023T044451237Z-3aa7e217/report.json` and
`report.md`, plus platform reports and complete harness logs. Both reports record
all four historical definitions as `absent` and retain four mandatory harness
hashes. Both HEADs are `bae9da7`; both working trees are dirty, with exactly the
four YAML deletions in Linux. Linux ran the revised harness through the Windows
mount; the other Windows documentation edits were not copied into Linux.

No new authoritative Full pass is claimed: the source tree is uncommitted. After
a separately authorized commit, align both clean checkouts to that exact commit
before Full-mode acceptance. Production behavior and profiling data are unchanged;
the profiling section above and sampled report/results/counter hashes were
verified unchanged. Final whitespace review passed. No commit, push, PR, merge or
GitHub-settings change was made.

### M7.4 baseline and lazy-leaf design investigation: 2026-10-23 UTC

Investigation completed against fetched merged `main` at `b04a198`. Engine,
vendored rules, CMake and benchmark/runner sources are unchanged from profiled
M7.3 `54ec7d9`. A fresh uninstrumented Windows/MSVC Release build used depths
4/5/6, TT off/cold/warm, default material evaluator, single-PV PLAY-only root,
alpha-beta/previous-PV ordering, 8 MiB TT request, serial execution, **five measured
repetitions and one warmup** per configuration. Warm explicitly populates first.

| Depth | Off median ms | Cold median ms | Warm median ms |
| --- | ---: | ---: | ---: |
| 4 | 9.562 | 10.982 | 1.215 |
| 5 | 147.896 | 148.233 | 150.883 |
| 6 | 2,089.650 | 2,235.407 | 2,302.892 |

All 45 measured rows match historical checksums/best moves/PVs; all 27 overlapping
records match deterministic work statistics. An untimed read-only inspector matched
225 complete-depth decision/hexadecimal-score/PV/end records across modes and all
45 final work records. Depth-six nodes/evals remain 381,938/328,475 off and
355,879/301,184 cold/warm. All **12/12 fresh Release CTest suites passed**, exit 0,
5.36 s. Timing snapshots were clean and unchanged; this is a performance baseline,
not a newly executed Full local CI pass. No Linux/Profile/WASM timings were taken.

Environment matches reported M7.3 OS/CPU/compiler versions, but the build directory,
whole-artifact hashes and repetition count differ. Executable/material `.text`
hashes match the prior binaries; PE timestamps and `.rdata` differ. Desktop
scheduling/thermal state was not isolated. Timing changes are observations, not
measured M7.4 speedups. Two metadata setup attempts stalled before timing; bounded
Node collection resolved setup. Historical profiling files were hash-verified
unchanged; no old output was overwritten.

Local-only report and evidence: `work/m7.4/baseline-20261023T152516Z/`:
`investigation.md`, `baseline-summary.csv`, `canonical/results.json` / `results.csv`,
`validation.json`, `completed-depths.json`, toolchain/artifact/source evidence and
logs. Fresh benchmark SHA-256:
`2cd6e91808e5ebf34a556afa6790c8a6de2b65825bc38fac4518ae97f0f4fde1`;
material SHA-256:
`424e3fb4ac9081ceeea489638858d861d0ec9583829432fb742b54526b273b51`.
These ignored artifacts must be supplied separately for review outside this machine.

**Proposed smallest first implementation:** skip redundant depth-zero sorting and
unused previous-PV/TT rotations, preserving canonical claims, legal TT hint
validation/counts, probe/store/bound semantics and all result/work statistics.
No prepared-node representation or adapter change is needed. A subsequent
increment would defer text/facts/order only for actual native depth-zero children
with no active TT, no raw EP, halfmove clock <99 and no repeated
target in the active history including the just-entered child. Retain full legal
generation, snapshot, terminal precedence, exact repetition identity, cancellation
and restoration. Reuse the raw list on fallback; keep roots, speculative lookahead,
synthetic adapters and TT/EP/claim-sensitive leaves eager. The historical counters
suggest 266,606–266,741 eligible depth-six off leaves; this is a derived opportunity,
not measured savings. TT-enabled laziness and TT bookkeeping remain later proposals.
Review the detailed proof/fixtures before authorizing production implementation.

### M7.4a.1 depth-zero ordering: 2026-10-23 UTC

This is the earlier implementation/grouped-timing checkpoint. The controlled
crossover below supplies the subsequent performance assessment; retain these
original measurements and limitations as provenance.

Implemented only within `Traversal::node()`: depth-zero nodes reuse the initial
canonical list without another sort, retain its cancellation boundary, skip
previous-PV ordering and skip TT move rotation. Legal hint lookup/membership and
`hint_hits` remain unchanged. Positive-depth/root ordering, initial move encoding/
validation, terminal/claim/evaluation and TT probe/store behavior are retained.

The existing M7.2 suite adds **358 focused searches** covering both colors,
fixed/iterative exact results, horizon claims/terminals, canonical ties, previous-PV
on/off, legacy/MultiPV/root policy, valid/invalid leaf hints and wrong-depth numeric
entries, TT hint/numeric cold/warm, actual repetitions, native clocks, synthetic
encoding failures, cancellation and evaluator failures. New assertions passed
against the unchanged search first. One initial fixture mistakenly compared its
make/undo count against zero after replay setup; the corrected test compares with
the post-setup count, and the failed log is preserved. No engine defect was found.

Changed-source validation, all exit 0:

- Native M3/M4/M5/M7.2: **4/4**, 3.73 s; complete Release CTest **12/12**, 4.88 s.
- Pinned emsdk `62a853cd3b3134398ce85cde8bb5cbb2ef0194cb`, Emscripten 4.0.10,
  SDK Node 22.16.0: affected WASM suites **4/4**, 33.07 s; complete WASM **11/11**,
  52.02 s, built from the modified Windows source through WSL without changing
  the Linux checkout. M7.2 totals are 600 bounded / 2,566 native; timeout remains
  300 s. The added group took 0.170 s WASM / 0.007 s native Release.
- All **45 candidate benchmark rows**, plus **90** frozen control rows, match
  baseline checksums, final PVs and deterministic work fields. Untimed differential
  inspection directly matches **225 completed-depth exact results** and **225
  cumulative counter prefixes**, including every deterministic TT field.

Serial uninstrumented Release, five measurements plus one warmup, same material/
initial position/TT budget/options as the approved baseline:

| Depth | Candidate off ms | Candidate cold ms | Candidate warm ms |
| --- | ---: | ---: | ---: |
| 4 | 9.082 | 10.560 | 1.120 |
| 5 | 139.296 | 134.946 | 137.586 |
| 6 | 2,173.709 | 2,422.264 | 2,480.441 |

Depth-five medians are roughly 4–6% lower than both same-session frozen controls.
Depth-four off is slightly slower than controls; warm changes direction.
Depth-six results overlap broad control distributions: its warm median is +0.80%
versus the first control and -3.46% versus the ending control. Against the earlier
approved baseline, candidate depth-six times are **4.02% / 8.36% / 7.71% higher**.
These mixed observations and uncontrolled desktop/grouped run order make overall
performance **inconclusive**; no stable overall speedup or attributable regression
is established. Keep this limitation visible during review.

Ignored local evidence: `work/m7.4a1/validation-20261023T163313Z/`, especially
`report.md`, `comparison.json` / `comparison.csv`, all raw configuration logs,
completed-depth comparisons and `m7.4a1-source-tests.patch`. All 403 files from
M7.3 profiling and the approved M7.4 baseline were verified unchanged. Existing
status work is preserved in `CURRENT_STATUS-before.md`. Only tracked documentation changed
after measurements; source identity was unchanged throughout timing.

This dirty implementation is **not authoritative Full CI acceptance**. Native
Debug/Profile and the full controller/GUI matrix were not rerun; no coordinated
Quick pass is claimed. Full local CI requires a separately authorized commit and
aligned clean checkouts. No source changes were made to chase timing results.
No commit, push, PR, merge or GitHub-settings change was performed.

### M7.4a.1 controlled crossover: 2026-10-23 UTC

After independent review approved the production/regression diff, the frozen
baseline and candidate Release artifacts ran **144 fresh-process invocations**:
six four-run blocks per configuration, three ABBA and three BAAB. Configuration
order rotated between rounds. Each invocation retained the unchanged benchmark's
one warmup, one measured search, initial position/full history, material evaluator,
single-PV PLAY-only options and 8 MiB TT request. Warm TT had explicit population
before warmup; cold clearing remained outside timing. No observation was excluded.

The paired estimate is the median of six within-block geometric B/A time ratios
(two adjacent A/B pairs per block); percentages below express time reduction,
not comparisons with historical unpaired medians.

| Depth / TT | Median paired time reduction | Faster blocks | Block B/A range |
| --- | ---: | ---: | ---: |
| 5 / off | 3.69% | 6/6 | 0.9336–0.9745 |
| 5 / cold | 3.88% | 6/6 | 0.9551–0.9743 |
| 5 / warm | 3.31% | 6/6 | 0.9503–0.9680 |
| 6 / off (primary) | 4.68% | 6/6 | 0.8645–0.9796 |
| 6 / cold | 3.85% | 6/6 | 0.9546–0.9816 |
| 6 / warm | 2.54% | 4/6 | 0.9567–1.0300 |

Depth-six off/cold also had lower measured CPU time in all six blocks. Depth-five
CPU measurements are coarse (15.625 ms units); cold's median CPU ratio was 1.0541
despite consistent wall-time reductions. Depth-six warm reversed direction in
two blocks, so its apparent median benefit is **inconclusive**. A 2,603.041 ms
baseline depth-six off observation with 2,531.25 ms CPU remains included. Neither
thermal state nor hybrid-core residency was measured; desktop/OS effects remain
possible. These results support retaining this separate optimization for review,
not a universal speedup or an attributable regression in warm TT.

All **144** invocations matched the established completed-depth checksums, final
PVs, nodes, evaluations and emitted deterministic TT counters. Direct completed-
depth inspection also passed: each frozen companion produced **45 records / 225
exact completed-depth results and counter prefixes**, matching the established
reference. These untimed results are retained alongside the timing results;
the canonical executable exposes checksum evidence rather than raw per-depth
scores. Executable/DLL hashes were checked before and after every invocation.
Production code, regression tests and the canonical benchmark were not edited.

New ignored evidence: `work/m7.4a1/ab-20261023T204000Z/`, including `report.md`,
`timings.csv`, `observations.jsonl`, `blocks.csv`, `summary.csv`, `artifacts.json`,
all raw logs, the predeclared schedule and a complete four-file review patch.
The timed crossover took **398.784 s**, exit 0. All **646** existing files across
M7.3 profiling, the M7.4 baseline and prior M7.4a.1 validation were verified
unchanged. No commit/publication occurred; this dirty-tree benchmark investigation
does not claim authoritative Full CI acceptance or new Linux/WASM performance.

### M7.4a.2 guarded native leaf preparation: 2026-10-24 UTC

**Historical pre-commit snapshot:** the implementation and measurements below
are retained unchanged. The subsequent completion checkpoint records PR #24 and
committed Full CI; the uncommitted acceptance statements in this snapshot do not
describe the current M7.5b branch.

**Reviewed decision:** retain the original implementation unchanged on
`feature/m7.4a2-guarded-leaf`, base
`1d461dfab38e2f105ecb67529192c34b880f3efc`. The source/test patch for
`engine/src/search/search.cpp` and `tests/search/m72_search.cpp` has SHA-256
`a23441a795db11d9aa19699529f9f6258b4fb5456b318f9ff699d88b35c33e47`.
The experimental in-place signature variation was not adopted.

Genuine depth-zero native search children, including exact-PV re-search, may
retain the complete legal set as raw move owners only with no effective TT
session, no raw EP square, child clock below 99, and pairwise-distinct exact
prospective history. Before physical history push, the guard checks
`duplicate(H + [r]) = duplicate(H) || contains(H, r)` with cancellation polling. Active hint-only/
numeric TT, EP-sensitive, claim-sensitive and synthetic-adapter cases use eager
preparation. Roots and positive-depth ordering remain eager. Legal generation,
snapshots, terminal precedence, draw claims, evaluation, ownership/restoration,
external interfaces, evaluator ABI and deterministic search behavior are preserved.

Recorded implementation validation, all final exits 0; these are checked existing
results, **not suites rerun for this documentation handoff**:

| Check | Passed | Elapsed |
| --- | ---: | ---: |
| Native Release | 12/12 | 6.17 s |
| Native Debug | 12/12 | 127.95 s |
| Native Profile | 12/12 | 8.57 s |
| Complete pinned WASM, Emscripten 4.0.10 / SDK Node 22.16.0 | 11/11 | 81.55 s |

The 1,461 new regression searches preserve earlier matrices: 4,027 native full
and 2,061 bounded WASM searches. Frozen/candidate comparisons match 1,703 direct
structured differential records **per platform**, including exact decisions,
scores, full PVs/endings/intended moves and deterministic counters. Native
canonical inspection matches 225 completed-depth results and 225 cumulative work
prefixes from 45 invocations, including skipped stores. All canonical checksums
and deterministic work remain identical. Separate copied-source diagnostics passed
2,000 cancellation injections, checking board/history/path restoration and
subsequent usable searches; those hooks are not production APIs.

Measured work for one initial-position iterative depth-six TT-off invocation:

| Quantity | Recorded count |
| --- | ---: |
| Raw leaf preparations | 266,606 |
| Eliminated move encodings | 6,703,200 |
| Eliminated initial sorts | 266,606 |
| Actual legal generations, unchanged | 385,132 |
| Visited nodes, unchanged | 381,938 |
| Evaluator calls, unchanged | 328,475 |

The original 216-invocation serial Release crossover used six ABBA/BAAB blocks
per depth/TT configuration, fresh processes, one warmup, initial position,
complete history, material evaluator, iterative single-PV PLAY-only root and an
8 MiB TT request. Warm tables had explicit population; cold clearing was outside
timing. Estimates use the median of six within-block geometric candidate/baseline
ratios, not historical unpaired medians. Instrumented counters are separate from
canonical timing, and no observations or outliers were excluded.

| TT-off depth | Median paired time reduction | Faster blocks |
| --- | ---: | ---: |
| 4 | 29.61% | 6/6 |
| 5 | 41.38% | 6/6 |
| 6 | 40.18% | 6/6 |

That experiment also observed depth-five TT-cold 0.64% slower in all six blocks
and depth-six TT-warm 2.77% slower in five of six. Follow-up independent balanced
experiments (54 invocations each) used normal evaluator identity/ABI checks with
a common frozen DLL, then compared an isolated in-place signature variation.
The retained original candidate's depth-six TT-off reductions were 42.35% and
44.04%, each favoring it in all six blocks. TT-enabled directions were mixed:
warm was 0.56% faster then 3.59% slower; cold was 0.87% slower then 0.54% faster.
TT-enabled searches remained exclusively eager and all result/work checks matched.
Common-DLL controls did not establish an evaluator-artifact cause. The in-place
variation's favorable warm median favored it in only three of six blocks and
did not establish a consistent fix.

**TT-enabled neutrality has not been demonstrated; the root cause of the observed
regressions remains unresolved.** Additional eager-path storage and a temporary
signature copy were confirmed in MSVC assembly; their timing contributions and
code-layout/OS effects remain hypotheses. These are single-machine Windows/MSVC,
initial-position, workload-specific findings, not universal engine speedups.
Thermal state, core residency and desktop scheduling were not isolated.

Valid 49-entry distinct and 113-entry repeated histories were replayed at depths
1/2 in 96 balanced TT-off invocations with the frozen material DLL. Direct
completed-depth results and deterministic work matched.

| History / depth | Baseline median ms | Candidate median ms | Median paired candidate/baseline |
| --- | ---: | ---: | ---: |
| Distinct / 1 | 0.39820 | 0.23905 | 0.60971 |
| Distinct / 2 | 1.55205 | 1.05115 | 0.67969 |
| Repeated / 1 | 2.40030 | 2.37155 | 0.99465 |
| Repeated / 2 | 9.15105 | 9.23310 | 1.00150 |

The distinct-history guard replaces much of the pre-existing pairwise eager claim
scan; its entire quadratic cost is not newly added work. Repeated histories reject
early and retain existing intended-claim lookahead. No pathological incremental
cost was demonstrated, so no evidence supports a fallback threshold and none was
added. Longer distinct histories remain unmeasured; quadratic worst-case behavior
and these short fixture timings remain limitations.

Ignored local-only evidence, unavailable from a fresh clone without a separate
artifact handoff:

- `work/m7.4a2/investigation-20261024T014138Z/`: approved design, reference values
  and frozen baseline binaries.
- `work/m7.4a2/implementation-20261024T050939Z/`: `report.md`, `differential.json`,
  diagnostics/cancellation records, native/WASM logs, original `ab/timings.csv`,
  `ab/artifacts.json` and original source/test review patch.
- `work/m7.4a2/tt-investigation-20261024T055228Z/`: `report.md`, `results.json`,
  `common-dll/` and `inplace-ab/` raw timings/hashes, `long-history/`, rejected
  variation and `retained-two-file-review.patch`. Its `final-audit.json` records
  3,743 historical files unchanged; preserve that audit and its manifest.

The rejected variation also passed affected native and pinned WASM suites 4/4
each; those checks do not substitute for acceptance of a new production variant.
Neither investigation nor this documentation handoff is authoritative Full CI.
A separately authorized commit and clean, identical Windows/Linux checkouts are
required before the complete 54-gate acceptance run. No commit, push, PR or merge
is authorized by this handoff.

### M7.4a.2 completion and M7.5a accepted investigation: 2026-10-24 UTC

[PR #24](https://github.com/yctekmen-sudo/chess-engine-lab/pull/24) merged M7.4a.2
into main at `f3aaee0a284b206a347786a8e5418c3773654a40` on 2026-10-24.
Its approved feature commit `8b68e241717a8e205d86904fc2d26f9077dca76f` passed
authoritative Full local CI: **54/54** command gates, Windows **24/24**, Linux
**30/30**, worker/coordinator exits **0/0/0**, verdict `passed` and authoritative
full pass `true`, with clean aligned checkouts. Checked local-only evidence:
`work/ci/20261024T185729687Z-1e5cefff/report.md` and `report.json`, plus
`work/m7.4a2/publication-20261024T185446Z/`. These are historical M7.4a.2 results,
not M7.5b acceptance. M7.4a.2's unresolved TT-performance uncertainty remains
historical and unresolved; merging did not demonstrate TT-enabled neutrality.

**M7.5a is complete, reviewed and accepted.** Its separate baseline session at
`f3aaee0` measured initial-position depth-six uninstrumented native Release
TT-off throughput of **348,774 NPS**. Sampling of TT-off execution classified
**60.86%** of sampled CPU stacks under the exclusive move/list owner category;
the **41.76%** inclusive heap-path stack union overlaps those owner categories
and must not be added to them. Evaluator stacks accounted for **2.20%**.
Provenance: `work/m7.5a/investigation-20261024T202503Z/report.md`,
`throughput.csv`, `cpu-breakdown.json`, `artifacts.json` and
`canonical-release/results.json`. These single-machine Windows/MSVC,
material-evaluator measurements describe that workload/session; sampling
categories are not independent elapsed-time savings estimates.

### M7.5b approved native leaf count handoff: 2026-10-24 UTC

The original implementation replaces unused native move-wrapper allocation with
an **adapter-private complete native legal-count path**, only at the previously
guarded depth-zero leaves. Full legal generation remains required. TT-enabled,
EP-sensitive, claim-sensitive and synthetic-adapter cases retain eager handling.
No public ChessCore, evaluator ABI, controller, GUI or dependency changes are
included. The original source/test patch remains approved and unchanged; no
experimental variant is adopted.

At frozen initial-position depth six with TT off, **6,703,200 unused native move
wrappers** are avoided across 266,606 guarded leaves. Actual full legal-generation
calls remain **385,132**, visited nodes **381,938** and evaluations **328,475**.
Decisions, exact scores, complete PVs/endings, claims and deterministic statistics
match the frozen merged M7.4a.2 baseline. Instrumented counters are separate from
uninstrumented Release timings; fewer public boxing calls are not fewer actual
legal generations.

Checked existing correctness evidence, not rerun for this documentation task:

- **132 additional regression searches**, with a **43-position** complete-count
  corpus, retaining all earlier assertions and matrices.
- Native **Release / Debug / Profile 12/12 each**; complete pinned Emscripten
  **4.0.10 / SDK Node 22.16.0 WASM 11/11**, all final exits 0.
- **1,855 direct structured differential records**, **225 completed-depth
  comparisons** and corresponding deterministic counter prefixes. Native/WASM
  records agree after lossless hexadecimal-float spelling normalization, with
  no numerical tolerance; the earlier 1,703 records per target remain intact.
- **2,000 diagnostic cancellation injections**, plus targeted helper/evaluator
  failures and restoration checks; no diagnostic hooks enter public interfaces.

The controlled **408-invocation** crossover compares the frozen merged baseline
against the original candidate with the same frozen evaluator DLL through normal
identity/ABI checks. It covers 17 configurations, each with six four-run blocks
(three ABBA, three BAAB), rotating configuration order, equivalent warmup and
warm-TT population, and cold clearing outside timing. All raw observations,
including outliers, and exact result/work checks are retained. Reductions below
are medians of contemporaneous paired block ratios, not historical unpaired
median comparisons.

| TT-off workload | Median paired time reduction | Faster blocks |
| --- | ---: | ---: |
| Initial position, depth 4 | 22.36% | 6/6 |
| Initial position, depth 5 | 29.75% | 6/6 |
| Initial position, depth 6 | 31.71% | 6/6 |
| Tactical, depth 4 | 39.43% | 6/6 |
| Middlegame, depth 4 | 42.84% | 6/6 |

Descriptive initial-position depth-six TT-off throughput in **this crossover**
was approximately **320K baseline versus 467K candidate NPS**. It is distinct
from the separate M7.5a baseline session's 348,774 NPS; do not use that historical
value to attribute the paired improvement. **TT-enabled neutrality remains
unproven:** depth-six TT-warm was **0.89% slower**, with **four of six blocks
favoring the baseline**. TT-enabled count-path activations are zero; the cause
of small timing differences is unresolved. These are single-machine,
workload-specific findings without thermal/frequency/core-affinity or desktop
isolation, not universal speedups or playing-strength claims.

Original ignored, local-only evidence (not available through GitHub or a fresh
clone): `work/m7.5b/implementation-20261024T210345Z/`. Preserve its `report.md`,
`final-audit.json`, `review-identity.json`, `artifacts.json`,
`diagnostic-results.json`, `differential.json`, `wasm-differential.json`,
cancellation/native/WASM logs, and `ab/` raw timings, block summaries and artifact
identities. Do not overwrite its frozen binaries or earlier M7.3/M7.4 evidence.
The implementation audit verified **5,555 historical files** and **213 frozen
local artifacts** unchanged; this documentation handoff rechecks preservation.

**Acceptance:** ChatGPT approved the original source/tests and controlled
performance evidence. The three implementation/test files and two documentation
updates remain **uncommitted**, index empty. **No authoritative 54-gate Full CI
has run for M7.5b.** Linux remains clean on `main` at the merged baseline.
Commit and publication require separate authorization; aligned clean-commit Full
CI remains pending. No benchmark or correctness suite was rerun for this
documentation-only handoff.

### M7.4 investigation directions (earlier planning snapshot)

The following pre-implementation directions are retained as historical context.
The reviewed M7.4a.1 implementation and its measured scope are recorded above;
deferred encoding/allocation and TT bookkeeping still require separate approval.

M7.3's architectural optimization and completed profiling investigation have been reviewed and accepted, followed by PR #19's merge. M7.4 is the next investigation direction; its concrete implementation design, correctness proof and expected benefit still require review. No M7.4 code or new speedup is established by this documentation update.

1. **Investigate lazy depth-zero move preparation first.** Determine which move text, canonical ordering, hint rotation or metadata work can safely be deferred at the horizon. Full legal availability, mate/stalemate and automatic-terminal precedence, legal EP identity, optional draw actions and exact TT/PV obligations remain required. The measured leaf work motivates this proposal; its saving has not been measured.
2. **Investigate reduced move-allocation and encoding overhead.** Assess compact internal move/fact/text representation, appropriate vector reservation and avoiding repeated coordinate conversion. Preserve adapter/public interfaces, certificates and make/undo restoration. The measured allocation/encoding/sorting costs identify an opportunity, not a validated replacement design.
3. **Subsequently investigate TT bookkeeping.** After measuring the earlier changes independently, review repeated signature hashing, occurrence counts, persistent-history operations and ABI conversions. Retain bounded storage, exact collision/history verification, evaluator/depth/policy isolation and cancellation behavior; do not attribute the whole cold-TT penalty to the small exact-verification path.
4. If raw evidence is needed outside this machine, explicitly arrange a separate handoff or authorize publication. Do not commit ignored profiling artifacts or the existing untracked M7.2 work as part of documentation maintenance.
5. Agree the M7.4 design and scoped implementation request with ChatGPT before coding. Review exact-score/PV, claim/terminal, EP, clock 99/100, synthetic-adapter, certificate lifetime, cancellation/error and TT-history regression gates. Preserve the M7.3 evidence and benchmark each proposed change against the same uninstrumented baseline. Local CI implementation is separate; do not commit, push, create a PR, disable GitHub workflows or change protection rules without separate authorization.

## 5. Recovery and reproduction

From the implementation checkout, inspect before acting:

```powershell
git status --short --branch
git branch -vv
git worktree list
git stash list
```

For reproduction, read `work/m7.3/profiling/reproduce.ps1` and the report first. Its generated outputs overwrite files in that profiling directory; preserve the current evidence before rerunning. Use a fresh output directory for an independent canonical comparison, after building the intended M7.3 revision:

```powershell
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
node tests/search/run_performance.mjs build/native-release work/m7.3/recheck-YYYYMMDD 4,5,6 3 1
```

`search_performance` and this runner are now available on `main` and on the updated documentation branch through the M7.3 merge. For comparable runs, explicitly choose and record the intended source revision, build directory and evaluator artifact. Use a dedicated implementation/benchmark worktree rather than running benchmarks as part of documentation maintenance, and do not overwrite `work/m7.3/benchmark` / `profiling` without preserving existing results. Profiling and performance benchmarks were not rerun for the local CI task; current correctness-suite results are recorded in section 4.

## 6. New-session handoff and maintenance

> Read `AGENTS.md`, `docs/PROJECT_CONTEXT.md` and `docs/CURRENT_STATUS.md`. Verify actual GitHub/main/PR/CI and local worktree/stash state. M7.4a.2 merged through PR #24 at `f3aaee0a284b206a347786a8e5418c3773654a40`; its approved feature commit passed authoritative 54-gate Full CI. M7.5a profiling is complete, reviewed and accepted. ChatGPT approved M7.5b original source/tests and controlled evidence on `feature/m7.5b-native-leaf-count` at that merged baseline; the five-file handoff remains uncommitted, index empty, with no authoritative M7.5b Full CI yet. Linux is clean on main at the same baseline. Section 4 records exact work equivalence and paired workload-specific TT-off gains; TT-enabled neutrality remains unproven. Preserve the approved three-file patch and all ignored evidence. Commit, clean aligned Full CI and publication need applicable separate authorization. Preserve exact scores/PVs, rule/history semantics, evaluator ABI and the M7.2 root policy.

Update this file when milestones conclude, profiling becomes available, or PR/CI states change. Include verification date/SHA, distinguish executed results from checked logs, user reports and CI, and label local-only evidence. Modify `PROJECT_CONTEXT.md` only for enduring architecture/workflow decisions. Documentation maintenance is not standing permission to commit, push, create PRs or merge.
