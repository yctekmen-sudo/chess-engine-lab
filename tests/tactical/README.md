# CELab S0 — Versioned Tactical Benchmark Suite

**Status:** test-data and benchmark infrastructure only; no search-algorithm, evaluator,
TT, UCI protocol or GitHub CI changes. All native engine runs remain local on
Windows 11 and WSL Ubuntu 24.04. Publication of this directory in a feature
branch is not authority to merge or to claim full production validation.

## Corpus (236 records in two independently frozen cohorts)

| Source | Frozen selection | Scoring contract | File |
|---|---:|---|---|
| Arasan 2026 | 197 best-move EPD + 3 avoid-move EPD | Published-move agreement | `corpus/arasan2026.epd` |
| Lichess official examples | 4 puzzles | Apply opponent setup move, then evaluate first solution move | `corpus/lichess-official-samples.csv` |
| MateTrack | 4 each mate-in-1 through mate-in-8 (32) | Mate detection / correct distance / legal checkmating PV | `corpus/matetrack-selected.epd` |

The first two sources form the original 204-position S0 cohort. **MateTrack's
32-position challenge cohort is separate**; do not pool its results with the 204
published-move accuracy denominator. There is no claim of 236 distinct FENs
across cohorts without a cross-corpus deduplication audit. In future, extend the
suite toward a 2,000-case stratified corpus with independent adjudication.

### Pinned source identities and licenses

- CELab baseline: `f92a170deee6d9e56b173101ad50aca6c619ab6d`, merged PR #53.
- Arasan: [MIT-licensed source](https://github.com/jdart1/arasan-chess/blob/0cd818b74208e96314f16a65f91d51449a095e0f/tests/arasan2026.epd), pinned commit `0cd818b74208e96314f16a65f91d51449a095e0f`,
  expected Git blob `29fa318d960c73dc511eb4589c32ae9595f4b9ba`. See `licenses/ARASAN-MIT.txt`.
- Lichess: [official CC0 puzzle dataset](https://database.lichess.org/#puzzles), sample IDs `00sHx`, `00sJ9`, `00sJb`, `00sO1`.
- MateTrack: [GPL-3.0 source](https://github.com/vondele/matetrack/blob/d280222d630df9c21d4c2f1e87dcb62ffc50e48b/matetrackpv.epd), pinned commit `d280222d630df9c21d4c2f1e87dcb62ffc50e48b`, Git blob `1790ce63d7e9d74a2227b3a95d6b30d9ab2dbc90`. The selected EPD's
  SHA-256 is `79bdb4ac882aab713096b6b59a5bc07e9c3b4ba61c17f82d1ae606fd40fcdfb3`.
  **This GPL-3.0 license applies to the copied MateTrack dataset.** Keep the
  attribution and `licenses/MATETRACK-GPL-3.0.txt` with that material; review
  licensing before further redistribution or moving it into differently licensed code.

## Running the original 204-position baseline locally

Run from any Windows PowerShell session with local Git access, Node 24,
CMake/Ninja, MSVC x64, Python, npm and WSL Ubuntu-24.04 set up as for CELab.
The scripts create **new isolated source clones** and do not change the active
worktree or merge/push anything. The baseline hash is pinned by design.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    .\tests\tactical\scripts\run.ps1 -Limit 20 -Depth 4

# Full original corpus (previously run at depth 5):
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    .\tests\tactical\scripts\run.ps1 -Depth 5 -Repetitions 1
```

The `-ArasanPath` option can supply a verified replacement source copy.
Otherwise the runner uses **the pinned EPD in this repo**, never downloading a
mutable dataset. `-SkipWsl` is diagnostic only. Full cross-platform review
requires both platforms. Results are written to Downloads under
`CELab-S0-baseline-evidence-*.zip` with hashes and source identities.

## Running the 32 forced-mate challenge cases locally

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    .\tests\tactical\scripts\run-mates.ps1 `
    -MateFrom 1 -MateTo 8 -Depth 3 -TimeoutSeconds 30
```

The baseline is **not required to solve mate-in-8 at depth 15**. Its depth-3
smoke run only confirms the corpus and mate scoring: 4/4 mate-in-1 at depth 1,
4/4 mate-in-2 at depth 3, and no mate scores for mate-in-3..8. Future S1+
candidates must use equal work/time comparisons and report the first completed
depth, node count, correct mate distance and whether the PV reaches checkmate.
The published PV is only a representative checkmating line, not proof of a
forced win against all possible defensive moves. Other winning first moves are
not automatically mistakes.

**Current runner scope:** Both PowerShell entry points are intentionally pinned
**baseline-only**. They do **not** run arbitrary candidate commits. Before S1
candidate evaluation, add explicit SHA-matched candidate/baseline paired runs
and a node-budget search option or repeated equal-time methodology. Do not
pass candidate output through a baseline-only parser and claim comparison.

## Validated S0 observations (historical evidence, not new CI results)

From `CELab-S0-baseline-evidence-20261118T175512Z-d3d3403f.zip`:
204 positions, depth 5, 1,020 completed-depth records per platform, semantic
differences 0. At depth 5, Arasan best 14/197 (7.1%), avoid 3/3, Lichess 4/4.

From `CELab-S0-mates-evidence-20261118T183037Z-f04b33b6.zip`:
32 positions, depth 3, all completed per platform, 0 timeouts and 0 semantic
differences; all reported PVs legal; correct 8/8 mates within horizon. The
run was a single trial, so timing variability and cross-run repeatability
were not established. These are historical evidence from baseline source
`f92a170` before repo integration; they do **not** establish that this exact
feature-branch integration has passed Full 54-gate validation.

## Local content validation

From this directory, with Node and Python installed:

```powershell
node --check scripts/core.mjs
node --check scripts/freeze.mjs
node --check scripts/bench.mjs
node --check scripts/freeze-mates.mjs
node --check scripts/bench-mates.mjs
node scripts/selftest.mjs
node scripts/selftest-mates.mjs
python -m py_compile scripts/analyze.py scripts/analyze-mates.py scripts/seal.py
```

The case validators `freeze.mjs` and `freeze-mates.mjs` require the tested
`chess.js` package from the chosen *clean* CELab source checkout and verify
all published sample lines and position legality when run.

## Rules for S1 and subsequent changes

- Preserve frozen corpus identity and recorded source license/provenance.
- Keep **mate discovery**, **correct mate distance**, **PV checkmate**, and
  **published first-move match** as separate metrics.
- Report results by depth and by cohort; keep all raw JSONL records.
- Every integration includes a direct **merged-baseline performance comparison**
  on Windows and WSL, with time, nodes and evaluation counts; do not substitute
  identical fixed-depth node counts for equal-budget strength evidence.
- Validate clean/source-identical local Windows+WSL builds; no hosted GitHub CI.
- Engine changes, repository commit, PR and merge each require appropriate review.
