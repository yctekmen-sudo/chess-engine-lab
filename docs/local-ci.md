# Local CI on Windows 11 and WSL2 Ubuntu 24.04

`scripts/ci/run-all.ps1` runs the current GitHub test coverage locally without
editing or disabling `.github/workflows/`. Run it in Windows PowerShell 5.1 from
the Windows checkout; PowerShell 7 was not available for local validation.
MSVC x64 is initialized through the latest
Visual Studio Build Tools installation (`vswhere` / `Enter-VsDevShell`) when needed.
No commit, fetch, pull, reset, repository copy or branch switch is performed.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/ci/run-all.ps1
# Explicit paths when the defaults differ:
./scripts/ci/run-all.ps1 -Mode Full -WslDistribution Ubuntu-24.04 `
  -LinuxRepository /home/your-user/chess-engine-lab `
  -EmsdkRoot /home/your-user/toolchains/emsdk
```

## Coverage and prerequisites

| Existing GitHub job | Local full-mode coverage |
| --- | --- |
| Native Ubuntu Debug/Release/Profile | Same configure/build/CTest presets, all 12 native suites in every configuration |
| WASM correctness | Emscripten 4.0.10, SDK Node 22.16.0, configure/build/verbose CTest; all 11 WASM suites including the existing bounded M7.2 matrix |
| M6 controller | Linux native-debug integration binaries; locked npm install, build/typecheck/lint and complete controller test suite |
| M7 GUI | Locked npm install, typecheck/lint/build, all component tests, pinned Playwright Chromium install and full Linux E2E suite against the native engine/controller |
| Windows smoke | Controller install/build, GUI install/test/build, the same Node-based static-assets/registration/orientation/IPC smoke test |
| Additional Windows validation | MSVC native Debug/Release/Profile CTest, controller typecheck/lint/full tests and GUI typecheck/lint |

CTest inventories are checked before execution so missing Node integration tests
or disabled suites cannot yield a full pass. Test summaries must also confirm
complete execution: skips, cancelled/todo tests, missing summaries and omitted
CTest inventory entries prevent acceptance even if the process exits zero.
Application coverage also has reviewed minimum counts in `suite.mjs`:
controller **107** per platform, GUI **14** per platform, Playwright **5**,
Windows desktop smoke **2**, and harness regressions **50** per platform.
Additional tests are allowed. Reductions block acceptance; changing these floors
requires an explicit source/documentation review. Reports retain both the floors
and observed counts, and the coordinator rechecks successful worker reports.
Native CTest retains the full M7.2
matrix; WASM retains its bounded matrix and existing test timeouts. The optional
full WASM matrix remains available as documented in [development.md](development.md).
No timeout, test assertion, engine behavior or evaluator ABI is changed.

Install these tools explicitly before running the harness:

- Windows: Node 24 with npm beside `node.exe`, Git, CMake 3.21+, Ninja, Visual
  Studio x64 C++ tools and a working WSL2 Ubuntu 24.04 distribution.
- Linux: Git, GCC, CMake 3.21+, Ninja; nvm at `$NVM_DIR` or `~/.nvm` containing
  Node **24.10.0**; a separately maintained checkout at `~/chess-engine-lab`.
- SDK: `~/toolchains/emsdk`, git revision
  `62a853cd3b3134398ce85cde8bb5cbb2ef0194cb`, activated Emscripten **4.0.10** and
  SDK Node **22.16.0**. The harness does not install/activate an SDK or install Node.

The Bash worker sources nvm explicitly in noninteractive shells. SDK activation
is isolated to subprocesses: native/application commands keep Node 24, while
WASM CMake receives the absolute SDK Node path. Inherited native engine/build
overrides and custom Chromium selection are cleared for each test child; reports
record the selected native build. `CI=1` selects the pinned Playwright browser.

Full mode runs `npm exec --no -- playwright install chromium` from `gui`, then
the existing E2E script. Runtime libraries must already be installed. If needed,
perform the one-time interactive Linux OS-dependency setup yourself:

```bash
cd ~/chess-engine-lab/gui
source ~/.nvm/nvm.sh
nvm use 24.10.0
npx playwright install --with-deps chromium
```

Unlike GitHub's disposable root-capable runner, local CI does not prompt for sudo
or change OS packages. A missing library/browser causes a recorded failing E2E or
install step, never a pass. Locked npm installs/browser downloads still require
network access unless their caches already contain the needed artifacts.
Linux application Node matches the hosted 24.10.0 pin. Windows accepts installed
Node 24 and records its exact version (24.22.0 in this validation), so its toolchain
does not exactly reproduce the hosted Windows Node 24.10.0 environment.

## Source identity and acceptance

Both checkouts are inspected before any suite runs. Different HEAD SHAs block
the combined run with exit 2; align the checkouts manually after protecting local
work. Branch names may differ. The worker reads the harness from the Windows
checkout's WSL mount while testing sources in the Linux checkout; it never copies
new/uncommitted scripts or sources into the other repository. Harness and workflow
SHA-256 values are recorded separately from the tested Git revision.

Each platform records staged/unstaged tracked modifications, the Git index,
all non-ignored untracked paths and working-content fingerprints before/after its
suites. Staged changes remain dirty even when working files match HEAD. Different line endings
across clean checkouts are allowed; fingerprints are compared within each
checkout, not across Windows and Linux. Both checkouts are inspected again after
both workers finish; harness hashes must match between workers. Any source mutation blocks
acceptance. Clean checkouts at the same SHA, all required gates passing and full
mode are necessary for an authoritative **passed** verdict. Dirty tracked files
or untracked files allow useful validation, but yield **non_authoritative**, not a
clean-commit milestone pass. This includes this harness's own uncommitted changes.
The report does not claim those changes were tested on Linux merely because HEADs
match; only the actual Linux working tree was tested there.

Git index `assume-unchanged` and `skip-worktree` flags are recorded explicitly.
Any flagged path makes full validation **non_authoritative**, including unchanged
flagged files and sparse checkouts: Git diff cannot establish their cleanliness.
Content fingerprints still detect changes during the run. Inspection never clears
flags or rewrites the user's index; optional Git index refresh writes are disabled.

## Quick mode

```powershell
./scripts/ci/run-all.ps1 -Mode Quick -Areas infrastructure
./scripts/ci/run-all.ps1 -Mode Quick -Areas native,controller
./scripts/ci/run-all.ps1 -Mode Quick -Areas gui,smoke
./scripts/ci/run-all.ps1 -Mode Quick -Areas wasm
```

Select affected areas explicitly: `infrastructure`, `native`, `wasm`, `controller`,
`gui`, `smoke`, `e2e`. The default quick selection is infrastructure self-tests.
Dependencies are included (for example native Debug for controller integration).
Release/Profile and unrelated gates are explicitly `not_selected` in the reports.
Quick GUI checks do not run Linux E2E unless `e2e` is selected. **Quick mode is
non-authoritative for milestone acceptance**, even when every selected check passes.

## Reports and failure handling

Outputs default to `work/ci/<UTC timestamp>-<unique suffix>/` in the Windows
checkout, including Linux logs written through the WSL mount. `-ReportRoot` can
select another Git-ignored directory. Existing logs are never replaced by a new
invocation. Builds remain in the preset's ignored `build/` directories; npm,
Playwright and test outputs use existing ignored directories/caches.

- `report.json` / `report.md`: combined verdict and both complete platform reports.
- `windows/report.json`, `linux/report.json`: host/toolchain identity, source
  snapshots, command arguments/cwd, start/end times, durations, exit codes,
  statuses, logs and discovered CTest suites.
- One full stdout/stderr log per executed step. Failure output is retained; a
  failed prerequisite marks dependent steps `blocked`, while independent checks
  continue. Missing tool/bootstrap reports are explicitly blocked.
- `windows-source.json`, `linux-source.json`: initial cross-check snapshots.
  `windows-final-source.json`, `linux-final-source.json`: final inspections after
  both workers finish. `coordinator.json` and `coordinator-*.log` retain worker
  invocation commands, exit codes, durations and bootstrap output/errors.
  `coordinator-error.json` records an orchestration/preflight blocker if one occurs.
- `worker-exits.json`: Windows and WSL worker process exits, persisted by the
  PowerShell coordinator immediately after each worker. Both exits must be zero
  for acceptance, independently of report contents. Missing/malformed exit evidence
  blocks acceptance; an abnormal nonzero exit with a success report fails with
  exit 1. An expected blocked worker report with exit 2 remains blocked.

Exit 0 means a clean full pass or selected quick checks passed (distinguish these
using `mode`, `verdict`, and `authoritative`). Exit 1 means failed checks; exit 2
means blocked/incomplete/non-authoritative full validation. Missing platforms,
missing gates, unavailable tools, interruption and mismatched sources cannot
produce a combined authoritative pass. Individual success is not a full-suite pass.

The local harness does not replace GitHub hosting controls, branch protection,
required remote statuses, action pinning or hosted OS-image evidence. Windows 11
is not Windows Server 2022; WSL2 is not a fresh GitHub VM; cached/incremental builds
are not clean-runner isolation. There is no macOS or Windows browser-gameplay
gate. Review workflow changes against this coverage mapping before relying on it.
GitHub workflows remain enabled until separately authorized changes are made.
The sequential local matrix has no GitHub job-level wall-clock ceiling; existing
CTest and test-runner deadlines remain unchanged. Executed results and local-only
evidence locations are recorded in [CURRENT_STATUS.md](CURRENT_STATUS.md#4-local-ci-infrastructure-and-m74-next-actions).

Harness regressions run with `node scripts/ci/suite.test.mjs` on both platforms.
They exercise negative exit codes, missing dependencies/gates/platforms, identity
mismatch, dirty/changed source detection and quick-mode qualification. No project
commit or publication is necessary to inspect local validation evidence.
