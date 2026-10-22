# Chess Engine Lab — Current Status and Next Actions

**Verified:** 2026-10-22, 18:33 UTC (14:33 America/New_York). This is a dated snapshot, not live data.

**Repository:** private [yctekmen-sudo/chess-engine-lab](https://github.com/yctekmen-sudo/chess-engine-lab).

**Stable context:** [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md). Read the root [AGENTS.md](../AGENTS.md) before implementation. ChatGPT supports architecture/design review; Codex implements only within the user's authorized scope.

## 1. Verified repository and PR checkpoint

| Item | Verified state |
| --- | --- |
| Local and remote `main` | `17edf1e438370d8fc1dc17db66982b3fd0d5cf85`, merge of [PR #18](https://github.com/yctekmen-sudo/chess-engine-lab/pull/18) |
| M7.2 | Merged; single-PV PLAY-only root search and controller root draw arbitration, including fixed-depth cancellation validation |
| M7.3 implementation | On `feature/m7.3-prepared-search-nodes`; **not merged** |
| M7.3 PR | [#19: M7.3 (draft): Reuse prepared legal moves and annotate draw claims](https://github.com/yctekmen-sudo/chess-engine-lab/pull/19), open and **draft** |
| M7.3 local/remote head | `54ec7d93c759e9ac7f6b8115fb5ef1985972863f` |
| PR #19 scope | Six files: `CMakeLists.txt`, `engine/src/chess_core.cpp`, `engine/src/search/search.cpp`, `docs/m7.3-search-node-design.md`, `tests/search/search_performance.cpp`, `tests/search/run_performance.mjs` |
| PR-head CI | Native Debug/Release/Profile, WebAssembly correctness, M6 controller, and M7 GUI including Windows smoke: completed successfully; links below |
| Local M7.3 validation | Existing profiling-handoff log records native Release CTest **12/12 passed**, 0 failed, 4.68 s; this documentation task checked the log and did not rerun the suites |
| Profiling investigation | **Complete locally** at the M7.3 head above; report, nonempty counter/sample CSVs and raw evidence checked |
| Architecture and next optimizations | **Pending review with ChatGPT**; profiling completion is not implementation approval or merge approval |

The approved [M7.3 design at the verified head](https://github.com/yctekmen-sudo/chess-engine-lab/blob/54ec7d93c759e9ac7f6b8115fb5ef1985972863f/docs/m7.3-search-node-design.md) is on the feature branch, not `main`. PR #19's description still says validation is pending; the actual completed CI runs and local evidence below are more recent evidence. The description has not been edited by this documentation task.

Verified successful PR-head workflows:

- [Native Debug/Release/Profile, run 39823918573](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918573).
- [WebAssembly correctness, run 39823918638](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918638).
- [M6 headless controller, run 39823918536](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918536).
- [M7 GUI and Windows smoke, run 39823918527](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823918527).

The push-triggered native workflow [39823909737](https://github.com/yctekmen-sudo/chess-engine-lab/actions/runs/39823909737) also completed successfully on attempt 2. Its earlier stale/in-progress display is historical, not a current failing check. Success applies to the recorded M7.3 SHA; recheck after any new commit. The Windows smoke job does not build the native C++ engine or establish Windows browser-gameplay coverage; local native results are separate.

### Protect existing local work

The implementation checkout remains on the M7.3 branch. The handoff was prepared separately on `docs/project-handoff`, based on the verified `main`, in the ignored worktree `work/project-handoff/`; it does not contain M7.3 production changes.

At inspection, one stash existed: `stash@{0}`, object `3669920a80ede34c7943adfdcfc7c3dcd807b7fe`, message `On investigation/m7.2-search-performance: Local M7.3 work before branch switch`. Nine other untracked M7.2 investigation files remain under `docs/search-performance-*` and `tests/search/` (the old performance harness, counters, preparation/report scripts and Windows sampler). The two supplied handoff documents were also untracked. Do not infer that any of these files were committed or that the stash should be applied/dropped.

All 178 pre-existing tracked/untracked files outside the two handoff documents, and all 257 files under `work/m7.3/profiling/`, were hashed before documentation edits. Preserve their content, the stash and the implementation checkout. Inspect current status again before starting work; this inventory is a snapshot.

## 2. What M7.3 implements under review

Source inspection at `54ec7d9` confirms:

- `PreparedNode` owns a snapshot, legal-move list and repetition identity for one position encounter; iterative root preparation is reused.
- Legal EP is obtained from that prepared list rather than generating another list for repetition normalization. Evaluator snapshots retain raw EP.
- Native generated moves carry position/undo-frame certificates; encode/make avoid redundant legality generation when the certificate matches. External/foreign/stale inputs retain validation, and strict undo restores the matching parent frame.
- Native mate/stalemate classification uses the prepared list, consulting check state when empty. Synthetic adapters retain their independent status/clock semantics.
- Claim-aware nodes retain immediate and intended-claim metadata. A sound repeated-history-target filter avoids most speculative children. Intended fifty-move claims use halfmove/pawn/capture facts.
- M7.2 root PLAY-only policy, terminal precedence, canonical score/PV behavior, evaluator ABI and exact TT context requirements are preserved in the existing tests.

**Remaining design/review items:** rare intended-threefold candidates still prepare and discard speculative children; there is no general retained prepared-child cache. Historical repeated-target scanning remains pairwise, and ABI conversion is repeated for TT probe/store. These are observed implementation details, not newly approved optimization designs. Passing the tested matrices does not prove all untested histories or positions equivalent.

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

## 4. Pending review and next actions

1. **Review the completed report with ChatGPT.** Architectural conclusions, optimization priority and any new implementation scope remain pending. Local profiling completion is evidence, not approval to implement its recommendations.
2. Proposed candidates, **not implemented or newly approved**: defer horizon canonical text/order work and duplicate sorting; reduce move-list allocation/coordinate conversion; reduce repeated TT history hashing or validated snapshot conversions. Review exact-score/PV, claim/terminal, EP, certificate lifetime, cancellation/error and TT-history correctness gates before authorizing any change. The report lists detailed risks and regression requirements.
3. Review PR #19 against the approved M7.3 design, including rare intended-threefold cases, legal/pinned/raw EP, clock 99/100 boundaries, synthetic-adapter behavior and exact PV/TT policy isolation. Interpret tested equivalence within the tested scope.
4. If raw evidence is needed outside this machine, explicitly arrange a separate handoff or authorize publication. Do not commit ignored profiling artifacts or the existing untracked M7.2 work as part of documentation maintenance.
5. Merge PR #19 **only after separate explicit approval**. Update this file when its head, CI state or merge status changes; update the `main` SHA only after checking the actual remote.

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

`search_performance` and this runner exist on the M7.3 branch; they are absent from the recorded `main` and the documentation branch. Do not run search benchmarks from the documentation worktree or overwrite `work/m7.3/benchmark` / `profiling` without preserving existing results. No production benchmarks or suites were rerun for this documentation-only update.

## 6. New-session handoff and maintenance

> Read `AGENTS.md`, `docs/PROJECT_CONTEXT.md` and `docs/CURRENT_STATUS.md`. Verify actual GitHub/main/PR/CI and local worktree/stash state. M7.3 profiling is complete locally; review its final report and raw evidence before proposing the next optimization. Architectural conclusions and follow-up implementation are pending ChatGPT review. Preserve exact scores/PVs, rule/history semantics, evaluator ABI and the M7.2 root policy. Do not publish or merge without authorization for that action and scope.

Update this file when milestones conclude, profiling becomes available, or PR/CI states change. Include verification date/SHA, distinguish executed results from checked logs, user reports and CI, and label local-only evidence. Modify `PROJECT_CONTEXT.md` only for enduring architecture/workflow decisions. Documentation maintenance is not standing permission to commit, push, create PRs or merge.
