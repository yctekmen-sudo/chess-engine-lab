# Local paired-game playing-strength validation

This optional developer tool coordinates the existing M6/M7 controller's
`createExperiment`, `resumeExperiment`, `experimentStatus`, `exportExperiment`
and durable records. It does not modify engine, evaluator, controller runtime,
GUI, persistence schemas or the 54-gate CI policy. See [controller guide](../controller/README.md)
for controller adjudication and diagnostics contracts. Use a clean, fingerprinted
Release build for each arm. The controller's game rules and authoritative clocks
are unchanged; this driver uses untimed games with equal per-search limits.

## Configure and run

From the repository root, install the pinned controller dependencies and build
with Node 24 (`npm --prefix controller ci --ignore-scripts`, then
`npm --prefix controller run build`). Build the engines independently.
Create `work/strength-config.json` (example below; replace all paths and source
revision declarations with the actual ones). Paths are absolute, with Windows
backslashes JSON-escaped. Linux binaries use their Linux paths. `sourceRevision`
is a required provenance **declaration**, not an assertion that the driver can
recover a source tree from a binary. Include any relevant binary dependencies
not discoverable through argv or the effective Evaluator option in `artifacts`.

```json
{
  "reference": {
    "id": "C1",
    "sourceRevision": "30dae10bc3c761704876422aeda65a1463592fa6",
    "engine": {"path": "C:/cursor/chess-engine-lab/build/native-release/chess_lab_uci.exe", "kind": "chesslab", "args": ["C:/cursor/chess-engine-lab/build/native-release/material_pst_evaluator.dll"], "options": {"Hash": "16", "MultiPV": "1", "NumericTT": "true"}}
  },
  "challengers": [{
    "id": "candidate",
    "sourceRevision": "REPLACE_WITH_CANDIDATE_SOURCE_REVISION",
    "engine": {"path": "C:/candidate/build/native-release/chess_lab_uci.exe", "kind": "chesslab", "args": ["C:/candidate/build/native-release/material_pst_evaluator.dll"], "options": {"Hash": "16", "MultiPV": "1", "NumericTT": "true"}}
  }],
  "match": {"seed": 13, "limit": {"kind": "movetime", "ms": 100}, "maxPlies": 200, "searchCapMs": 60000, "searchEvidence": {"maxEntries": 512, "maxBytes": 4194304}}
}
```

The default opening set is the 16 E2.3b lines in
`controller/tools/playing-strength-openings.json`. Override `openings` with
an array of `{moves:[...], initialFen?, complete?}` records. For a custom FEN,
explicitly declare whether history is complete. Opening moves are UCI; each
opening is validated with the controller's rules. Both colors are scheduled
from every opening. Exact duplicate history records are rejected; opening
balance and transpositional correlation require separate review. One may use
`limit:{kind:"depth",depth:4}` for deterministic controls instead of movetime.
Optional bounded `uciDiagnostics` uses the existing controller settings; no
unbounded trace collection is enabled by default.

```
node controller/tools/playing-strength.mjs run work/strength-config.json work/strength-run
node controller/tools/playing-strength.mjs status work/strength-run
node controller/tools/playing-strength.mjs export work/strength-run work/strength-export
```

To pause after a bounded number of **new** games, add `--max-games 8` or
`--minutes 30` to `run`. The time limit is checked between games, not a hard
in-game deadline. Repeat `run` with the same configuration and directory to
resume. The immutable tournament plan records binary SHA-256 hashes, explicitly
listed artifacts, absolute argv/evaluator paths, engine options, full openings,
controller commit/build-file hashes, driver hash, Node/platform and inherited
`CELAB_*` environment. The controller independently verifies executable,
effective option and artifact identity on match resume. It preserves completed
games; interrupted attempts remain evidence and restart from their original
opening. Resume rejects changed inputs. Use `--takeover-stale-lock` only after
verifying dead same-host ownership, as with the existing controller. A crash
during initial creation leaving only a schedule fails closed; investigate before
recovery. Do not delete evidence to make a resume succeed.

The serial schedule runs one challenger match at a time. No parallel games,
per-arm environment overrides, automatic retries of completed failures,
evaluation-based adjudication, automatic source compilation or source-control
operations. For a search experiment use separate compiled binaries/configurations
or existing UCI options. Record toolchain and build-source provenance separately.

## Evidence and interpretation

- Each match has controller checksummed records, color-reversed schedule, and
  bounded search evidence. Export replays official game histories through
  `FideRules` (chess.js) and writes separate PGNs and raw JSON sidecars plus
  `summary.json`. The controller's PGN headers use engine-reported names;
  match directory and sidecar identify the challenger and reference.
- Only chess outcomes award points. A work-cap invalid attempt or unresolved
  adjudication is not a draw; technical/invalid failures remain separate.
  Pending games are separate. Score bounds pessimistically/optimistically assign
  all non-chess outcomes zero/one point. The driver returns nonzero for recorded
  technical/invalid failures; review partial or capped results explicitly.
- Elo is the logistic transformation of the observed complete chess score;
  no finite estimate at 0%/100% or if any game is unresolved/pending/failed.
  A descriptive approximate 95% opening-pair t interval is provided only for
  fully resolved schedules with nonzero pair-score variance and at least two
  pairs. It is not a calibrated confidence guarantee for handpicked/correlated
  openings, repeated model selection or a sequential stopping rule.
- Per-move search summary uses the controller's existing reported counters and
  measured time, with explicit evidence truncation fields. This is **not** the
  E2.3 completed-depth-only parser or full PV legality audit. For selective
  search, separately run deterministic fixed-depth score/PV/legality gates and
  tactical/forced-mate tests; do not substitute tournament records for them.
- Historical E2.3 used automatic threefold/50-move claims and a custom referee;
  the production controller has trusted score-based optional claims and
  automatic draw rules. The controller may send stop at movetime. Results from
  this tool are not strictly comparable to old E2.3 games; rerun both arms with
  this tool for a fair comparison. Equal-time games are not deterministic.

## Validation

Run `node --test controller/tools/playing-strength.test.mjs`. A local integration
smoke should use the same clean baseline engine binaries, pause after one game,
resume, check the first official record/attempt hashes, export, and confirm that
work caps are not draws. Keep this optional tournament tooling outside routine
Full CI; no production engine behavior changes are intended. Every engine
integration still requires direct performance comparison to exact pre-integration
main plus the existing Windows/WSL Full CI and explicit publication approvals.
