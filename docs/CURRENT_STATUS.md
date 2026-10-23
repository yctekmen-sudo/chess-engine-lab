# Chess Engine Lab — Current Status and Next Actions

**Verified migration checkpoint (2026-10-23 UTC):** [PR #21](https://github.com/yctekmen-sudo/chess-engine-lab/pull/21) is closed and merged into `main` at `bae9da7914a769bdffcabe31fc78c1f2e63e8af7`, verified through GitHub and fetched `origin/main`. The user reports manually disabling all four hosted workflows. Local Windows 11 + WSL2 Ubuntu 24.04 CI is the validation process; GitHub retains source control, PRs and reviews. The uncommitted `chore/retire-github-actions` cleanup retires the YAML definitions and makes their absence explicit in harness reports. No GitHub settings or branch-protection changes were made or reverified here.

**Prior authoritative validation:** at `6a0b3848f317c8f16b0f5fdee4878aa039d7b304`, all 54 gates passed, exit 0, with both checkouts clean and unchanged. The merge has the same tracked tree as that revision. This is prior evidence, not a new Full pass for the cleanup; see section 4 for provenance and current Quick validation.

**Earlier verification:** local CI infrastructure prepared and hardened 2026-10-22 from `main` at `bead04a312180d8ce3ba3c48980659da273487c7` (merged PR #20). The hardening rerun passed all 54 command gates, including 50 harness regressions per platform, with both worker exits zero; uncommitted Windows changes made that combined result non-authoritative. UTC report directories below cross into 2026-10-23. Historical profiling evidence remains attributed to its recorded M7.3 revision. This is a dated snapshot, not live data.

**Repository:** private [yctekmen-sudo/chess-engine-lab](https://github.com/yctekmen-sudo/chess-engine-lab).

**Stable context:** [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md). Read the root [AGENTS.md](../AGENTS.md) before implementation. ChatGPT supports architecture/design review; Codex implements only within the user's authorized scope.

## 1. Verified repository and PR checkpoint

| Item | Verified state |
| --- | --- |
| Cleanup baseline / fetched Windows `origin/main` | `bae9da7914a769bdffcabe31fc78c1f2e63e8af7`, merge of local CI PR #21; both cleanup branches start here. Existing local `main` refs remain at `bead04a312180d8ce3ba3c48980659da273487c7` (PR #20) |
| M7.2 | Merged; single-PV PLAY-only root search and controller root draw arbitration, including fixed-depth cancellation validation |
| M7.3 implementation | **Merged into `main`** at `f0a5affee25a333ff23df2343277e052a2c2ae81` |
| M7.3 PR | [#19: M7.3 (draft): Reuse prepared legal moves and annotate draw claims](https://github.com/yctekmen-sudo/chess-engine-lab/pull/19), **closed and merged**; “draft” remains in its historical title |
| Profiled M7.3 source / merged PR head | `54ec7d93c759e9ac7f6b8115fb5ef1985972863f`; measurements below remain attributed to this revision, not newly measured at the merge commit |
| PR #19 scope | Six files: `CMakeLists.txt`, `engine/src/chess_core.cpp`, `engine/src/search/search.cpp`, `docs/m7.3-search-node-design.md`, `tests/search/search_performance.cpp`, `tests/search/run_performance.mjs` |
| M7.3 PR-head CI | Native Debug/Release/Profile, WebAssembly correctness, M6 controller, and M7 GUI including Windows smoke: completed successfully; links below |
| Local M7.3 validation | Historical profiling-handoff log records native Release CTest **12/12 passed**, 0 failed, 4.68 s; newly executed local CI results are recorded in section 4 |
| Profiling investigation | **Completed and reviewed**; accepted with M7.3 per the user's handoff and PR #19's recorded correctness/performance acceptance notes; raw artifacts remain local |
| Next milestone | **M7.4 planned investigation**: lazy depth-zero move preparation, reduced move-allocation/encoding overhead, then TT bookkeeping; these are future proposals, not implemented improvements or measured speedups |
| Documentation handoff | [PR #20](https://github.com/yctekmen-sudo/chess-engine-lab/pull/20), merged into the local baseline above |
| Local CI implementation | [PR #21](https://github.com/yctekmen-sudo/chess-engine-lab/pull/21) merged at `bae9da7`; prior authoritative validation at its head `6a0b384` |
| Active infrastructure cleanup | `chore/retire-github-actions`: retire four hosted definitions, preserve harness fingerprints/acceptance and document completed local validation migration; uncommitted |

The successful PR-head CI results below belong to M7.3 PR #19 at `54ec7d9`; they are not proof of a pass at the merge commit or an updated PR #20 head. Check CI for the actual revision under review.

The approved [M7.3 design](m7.3-search-node-design.md) and implementation are now on `main`. Its original draft-era header is historical; the verified merge and PR #19's acceptance notes establish the completed milestone state. The preceding merged baseline was `17edf1e438370d8fc1dc17db66982b3fd0d5cf85` (PR #18 / M7.2).

Verified successful PR-head workflows:

- [Native Debug/Release/Profile, run 39823918573](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918573).
- [WebAssembly correctness, run 39823918638](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918638).
- [M6 headless controller, run 39823918536](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918536).
- [M7 GUI and Windows smoke, run 39823918527](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918527).

The push-triggered native workflow [39823909737](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823909737) also completed successfully on attempt 2. Its earlier stale/in-progress display is historical, not a current failing check. Success applies to the recorded M7.3 SHA; recheck after any new commit. The Windows smoke job does not build the native C++ engine or establish Windows browser-gameplay coverage; local native results are separate.

### Protect existing local work

Both checkouts now use `chore/retire-github-actions` from `bae9da7`. Original
`feature/local-ci-infrastructure` refs remain at `6a0b384`, with no reset or stash.
The pre-existing Windows status note was explicitly carried across the branch
switch and preserved separately under `work/ci/retire-actions-20261023T043737044Z/`.
Linux was clean before switching; the verified merge was fetched from the Windows
checkout's existing local remote after a stalled Linux HTTPS fetch was terminated.
The earlier `work/project-handoff/` documentation worktree is retained unchanged.
The harness itself never switches/resets/synchronizes checkouts.

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

## 4. Local CI infrastructure and M7.4 next actions

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

### M7.4 investigation directions

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

> Read `AGENTS.md`, `docs/PROJECT_CONTEXT.md` and `docs/CURRENT_STATUS.md`. Verify actual GitHub/main/PR/CI and local worktree/stash state. M7.3 is merged at `f0a5affee25a333ff23df2343277e052a2c2ae81`; its completed profiling has been reviewed. M7.4 will investigate lazy depth-zero preparation and lower move-allocation/encoding overhead, then TT bookkeeping. These directions are proposals, not implemented behavior or measured gains. Review the concrete design before coding. Preserve exact scores/PVs, rule/history semantics, evaluator ABI and the M7.2 root policy. Do not publish or merge without authorization for that action and scope.

Update this file when milestones conclude, profiling becomes available, or PR/CI states change. Include verification date/SHA, distinguish executed results from checked logs, user reports and CI, and label local-only evidence. Modify `PROJECT_CONTEXT.md` only for enduring architecture/workflow decisions. Documentation maintenance is not standing permission to commit, push, create PRs or merge.
