# Deterministic local validation

## Default Full CI entry point

The maintained user-facing command, from a committed clean CELab checkout in
a regular Windows PowerShell terminal, is:

```powershell
.\scripts\validation\full-ci.ps1 -PlanOnly
.\scripts\validation\full-ci.ps1
```

The first line checks source identity, reference availability and Windows/WSL
toolchains **without creating clones or executing CI**. The second discovers or
creates isolated clean Windows/WSL Git clones for the selected commit, runs the
original Full coordinator once through `run.py --execute-full`, and writes
a unique evidence package beneath `work/validation-efficiency/routine/`.
No Codex calls, network fetches, repair operations or automatic retries occur.

For a Windows linked worktree, WSL Git cannot follow the Windows `gitdir:` path
in its `.git` file. The Linux clone therefore uses the isolated Windows
validation clone as its origin, after verifying that clone's exact selected
commit/tree and clean state. This origin must have an ordinary `.git` directory;
an existing linked-worktree destination is refused. No common-repository HEAD
or branch is assumed to contain the selected revision, including a detached
commit absent from common refs. Ordinary source checkouts keep their direct
Linux clone origin. Missing/invalid source Git metadata fails closed through
the metadata and Git identity checks. `-PlanOnly` reports `linuxCloneOrigin`
without creating the Windows origin, Linux checkout or evidence directory.

Each candidate and reference is independently checked against its own committed
Git tree and clean before/after/final source states. Whole-tree tracked/index
fingerprints are compared strictly for the **same commit**, but are not compared
across **different commits**. Instead, a Git source-delta inventory requires
review when any path outside `AGENTS.md`, `docs/`, and `scripts/validation/` changed.
Engine, controller, GUI, tests, fixtures, or existing CI policy changes require
review even if all seven deterministic output sets happen to match.
Evaluator configuration, toolchain and deterministic fingerprints remain strict.
Passing validation with documentation/tooling-only changes and identical
deterministic evidence can be accepted without inventing a new baseline.
Other mismatches yield exit 2 (`reference_review_required`), not permission
to update the accepted reference automatically. Missing evidence, dirty source,
skipped tests and nonzero worker/coordinator exits remain failures.
See [local CI](../../docs/local-ci.md). The underlying runner still has a
plan-only default for advanced callers; it has not changed its CI policy.

This optional wrapper reads the existing CI policy; it does not replace it.
`scripts/ci/run-all.ps1`, its 54-gate matrix, fixtures, test floors and verdict
rules remain unchanged. Python 3.11+ (standard library), application Node 24,
Git, Windows PowerShell and the established Windows/WSL toolchains are required.
The analyzer itself can read archived evidence without starting WSL or CI.

Keep these tools in a development/tooling checkout. Run validation against
separate clean Windows and WSL checkouts at the supplied production commit/tree.
Uncommitted tooling in a validation checkout prevents authoritative acceptance.

## Analyze existing evidence

From an ordinary PowerShell terminal, set paths to the existing local artifacts:

```powershell
$tools = 'C:/cursor/chess-engine-lab/work/validation-efficiency/approach-b1-20261106T160512Z/source/scripts/validation'
$base = 'C:/cursor/chess-engine-lab/work/validation-efficiency'
$commit = 'bcfe5082d67a5bfc78f6c763f47897673633ee09'
$tree = '934b5733357ce1c6274c6a3a0fb0fdb12ea508f1'
python "$tools/analyze.py" `
  --evidence-root "$base/codex-baseline-a2-20261106T152634Z" `
  --reference-root "$base/production-readiness-20261106T064606Z" `
  --source-repo "$base/codex-baseline-a2-20261106T152634Z/windows-ci" `
  --expected-commit $commit --expected-tree $tree `
  --out "$base/a2-offline-review"
```

`--report-dir` can select an original coordinator directory explicitly. Without
it, the analyzer locates exactly one `windows-ci/work/ci/*/report.json` inside
the evidence package. A1's synthetic packaging summary is never substituted for
a missing original coordinator report. `--reference-report` can similarly select
the approved reference's original report directory. The source repository supplies
Git objects and the unchanged exported CI policy, not a fresh test result.
Both report directories must be inside their respective sealed evidence packages;
external reports must first be copied and included in the integrity inventory.
The runner does this while retaining the original report path separately.

The analyzer reads standalone worker reports, final source snapshots and worker
exit records, cross-checks their embedded copies, and replays exported `plan`,
`combinedVerdict` and `testEvidence` functions through `ci-contract.mjs`.
All 54 required logs must exist. CTest inventories are checked against both
the existing plan and approved inventory. No tests execute during analysis.
Original coordinator invocation records and logs must also be present; a recorded
nonzero coordinator or wrapper process exit overrides a copied passing report.

Outputs are `analysis.json` and a concise `report.md` in a new directory.
Exit 0 means `authoritative_pass`; all other classifications exit 1:
`validation_failed`, `non_authoritative`, `evidence_incomplete`, or
`fingerprint_mismatch`. All discrepancies remain recorded; primary classification
priority is process/test failure, fingerprint mismatch, incomplete evidence,
then non-authoritative state. A missing/zero-gate report cannot pass.

## Fingerprints and integrity

The original report schemas and A2 sidecar names are reused: `full-ci-summary.json`,
`fingerprints.json`, `full-extra/`, `deterministic-test-fingerprints.json`, and
`evidence-manifest.json`. Every consumed evidence file must have matching size and
SHA-256 in the package's existing manifest. The manifest is an integrity record,
not a cryptographic signature or protection against deliberate resealing.

Source commits and Git trees are checked against actual Git objects. Source and
evaluator Git blobs, shared Windows source byte hashes, within-platform working
fingerprints, harness hashes, host/toolchain identities, and all seven deterministic
sets must match the approved reference. Additional recorded source blobs are
verified against the frozen Git tree. The 48-blob premerge inventory is extended
by A2's recording of `repeated_claim_targets.hpp`; its presence is not a code change.

The existing A2 extraction keeps printed perft, M7.2 fixture/group search counts,
ordering retention and P1 search/commit records, removing only elapsed `seconds=`.
Records are regenerated from each original `LastTest.log`, then compared to the
reference. A2's CRLF normalized files are checked against their original byte
hashes and compared by lines; new packages write UTF-8 LF. Stored success flags
and record counts alone do not establish agreement. Original logs remain intact.

Report timestamps/durations, probe durations, absolute invocation paths, build
binary hashes and CMake cache bytes vary with a run/build location. Binary hashes
are retained as observations, not required to match across builds; binary inventory
must match after separator normalization. A2 omitted the premerge package's nested
policy caches/`ordering-policy.txt`; those runtime configuration inventory differences
are reported explicitly. Evaluator *source* identity remains a required match.
Toolchain version changes produce a fingerprint mismatch, not a silent waiver.

## Runner: planning and future B2 execution

The runner defaults to **planning only**. It creates an exclusive evidence package
and records arguments without preflight or coordinator execution:

```powershell
python "$tools/run.py" `
  --windows-repo 'C:/path/to/clean/windows-validation' `
  --linux-repo '/home/user/work/clean-linux-validation' `
  --expected-commit $commit --expected-tree $tree `
  --reference-root "$base/production-readiness-20261106T064606Z" `
  --emsdk-root '/home/user/toolchains/emsdk' `
  --out "$base/approach-b2-plan"
```

After separate B2 authorization, use a fresh output directory and add
`--execute-full`. This is the only execution switch. It enables one invocation:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1 -Mode Full
```

The wrapper appends the isolated Linux checkout and existing distro/Node/SDK
options. It never passes `-ReportRoot`; original reports stay under the validation
checkout's default `work/ci/<run-id>/`. Preflight checks exact clean Windows/WSL
identity, unflagged indexes, the unchanged policy, approved reference, ignored
default reporting path, Windows tools/MSVC availability, Linux application Node
and the existing worker's pinned SDK checks. No source synchronization, repair,
package installation, extra tests, automatic retry, or source modification occurs.
Dependency downloads/runtime tests remain the coordinator's responsibility.

Command, original report path, independent timing, exits and errors are retained.
The runner copies original reports without rewriting them, collects existing CTest
logs/build fingerprints, and calls the analyzer. Exceptions and missing reports
stop the run. Existing output directories are refused. No second invocation occurs,
even when packaging/analysis fails. Interrupt cleanup targets only its owned process
tree. The complete live path has not been executed during B1.

Optional `--telemetry-collector <existing telemetry.py> --rate-cli <codex.exe>`
uses the tested collector's read-only allowance RPC before/after validation.
Without them, allowance is explicitly unavailable. No Codex session/model is
started. `modelCalls: 0` records the explicit model-free execution path; missing
implementation, routine-token and anomaly-investigation telemetry is null, not an
invented zero. Implementation-session tokens and any later anomaly investigation
remain separate from routine B2 execution and shared allowance observations.

## Focused B1 tests

Deterministic workflow and comparison regressions (no archived evidence or WSL
required; temporary local Git repositories test detached linked-worktree commits):

```powershell
python -m unittest discover -s scripts/validation -p test_workflow.py -v
python -m unittest discover -s scripts/validation -p test_cross_revision.py -v
```

The workflow regressions cover origin selection, unchanged plan-mode files,
missing/malformed pointers, exact commit/tree and cleanliness checks, and two
local clone stages with a selected commit absent from the common repository's
refs. Their Linux stage adapts WSL argv to local Git; real WSL cloning remains a
separate integration check, and neither check is Full CI qualification.

Captured-evidence regressions:

```powershell
$env:CELAB_VALIDATION_EVIDENCE = $base
$env:PYTHONDONTWRITEBYTECODE = '1'
python "$tools/test_validation.py"
```

Tests require the original A1/A2/premerge packages and make temporary mutation
copies, including paths with spaces. They fail explicitly if fixtures are missing.
All runner execution tests inject a fake backend and captured A2 evidence.
They cannot invoke the real coordinator. This suite does not execute Full CI,
application/engine tests, or benchmarks. No source artifacts are published by it.
