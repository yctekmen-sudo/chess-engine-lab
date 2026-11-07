# Approach C1 — Focused development validation (draft)

**Development prototype.** C1 is the development feedback loop, **not**
Approach B production qualification. It delegates project build, CTest, application test, floor, and dependency
commands to the existing `scripts/ci/suite.mjs` **Windows Quick** worker,
while running affected Python orchestration regressions as supplemental checks.
It does not modify the Quick policy or invoke the full coordinator.

## Installing this draft locally

Copy this directory to `scripts/dev/` in a CELab working checkout. No existing
repository files need to change. The directory may be untracked during C1
qualification. Do not commit/push/publish without separate approval.

On Windows, in the CELab repository root:

```powershell
# Read-only plan, automatic selection for uncommitted/staged/untracked changes
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev/focused.ps1

# Explicitly run selected Windows Quick gates once
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev/focused.ps1 -Execute

# Include committed changes on a feature branch as well as working edits
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev/focused.ps1 -BaseRef main -Execute

# Explicitly choose from existing Windows Quick areas
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/dev/focused.ps1 -Areas 'native,controller' -Execute
```

Use `-BaseRef` only with a **local ancestor commit**. C1 never pulls, fetches,
creates commits, changes branches, updates the index, or synchronizes WSL.

The default base is `HEAD`: it includes staged, unstaged and non-ignored
untracked files, but NOT already committed feature-branch changes. When the
selected base contains no changes, use explicit `-Areas` instead. Unknown
paths stop automatic selection; an explicit override records the unclassified
paths and any omitted inferred areas in the result, rather than silently
pretending to test them.

## Scope and guarantees

- This first C1 slice is **Windows only**; engine/CMake changes record pending
  Linux native, controller and WASM checks; controller changes record Linux
  controller checks; GUI changes record Linux GUI and E2E checks; and CI-policy
  changes record the full Linux coverage gap. Those checks are never silently claimed as covered.
- Windows Quick policy is consumed from `scripts/ci/suite.mjs` and its actual
  selected-step inventory is independently checked. Existing floors and
  smoke/controller/native dependencies remain unchanged.
- The worker inspects the working source before and after tests. A changed Git
  revision, working source, or changed-file selection invalidates acceptance.
  Non-ignored new files are included; assume-unchanged and skip-worktree flags
  block automatic planning.
- Plan mode is read-only and launches **zero** workers. Execute mode invokes
  **one** Windows Quick worker. No retry, Full CI, GitHub CI, model invocation,
  WSL source synchronization or reference-promotion occurs.
- Local Python regressions are executed for changes under `scripts/dev/` or
  `scripts/validation/`, separately from the unchanged Windows Quick worker.
  Reviewed minimums of 40 C1, 22 workflow and 9 cross-revision tests prevent
  accidental coverage reductions from producing a pass. The captured-evidence
  regression suite remains an explicit review dependency.
- A successful result is **development_pass** or **development_partial_pass**,
  always `authoritative: false`, never production acceptance. Full Windows/WSL
  production qualification remains with Approach B.
- If Windows MSVC x64 is unavailable, any selected native Debug gate will be
  **blocked** by the existing worker, not skipped or marked passed.
- Evidence is written under the ignored `work/dev-validation/<unique run id>/`
  path: `plan.json`, `summary.json`, `summary.md`, `coordinator.log`, plus the original
  Windows worker's JSON/Markdown and per-step logs. Existing evidence is not
  overwritten. For no-op plan mode nothing is written.
- Open `summary.md` for the human-readable verdict, selected Windows gates,
  passed test counts, separate Python regression results, coverage gaps,
  failures and log links. The same run's `summary.json` remains the machine-
  readable record; neither report promotes development checks to Full CI.
- Exit 0 means all selected **Windows** gates passed, even when an explicit
  `coveragePending` list remains; read `status` and `coveragePending` to understand
  the coverage boundary. Exit 1 means selected gates failed; exit 2 means
  blocked, invalid evidence, or unsafe planning.

## Offline tests

```powershell
python -m unittest discover -s scripts/dev -p test_focused.py -v
```

These are offline synthetic tests using temporary Git repositories and a fake
Windows worker; they never run real native builds, tests, Linux or Full CI.
Physical Windows PowerShell/Node 24 focused execution remains a separate
acceptance requirement before depending on C1 for development.

## Next slices

The human-readable C1 summary is the first reporting improvement.
C2 may expand concise failure digests and usage accounting. A future
C1/C2 extension may support a Linux Quick worker against an explicitly
verified equivalent source tree, but must not run against a stale WSL checkout
or silently synchronize uncommitted edits. Production Full CI remains separate.
