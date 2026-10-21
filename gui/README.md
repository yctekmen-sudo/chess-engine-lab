# Chess Engine Lab desktop GUI (M7)

A local React/Vite application served by the Node controller. The controller owns
rules, clocks, games, analysis trees, engine processes and experiment records.
The native engine/core and existing CLI remain usable independently.

## Build and launch

From the repository root, with Node 24, CMake/Ninja and a C++20 compiler:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
npm --prefix controller ci --ignore-scripts
npm --prefix controller run build
npm --prefix gui ci --ignore-scripts
npm --prefix gui run build
node controller/dist/cli.js gui work/my-workspace
```

This opens the default browser on an authenticated ephemeral loopback port.
Running the command again reconnects the same workspace through its protected
local launch channel. Different workspace directories are separate workspaces.
Browser refresh and multiple tabs share controller state. Browser X leaves the
controller running. **Quit workspace** offers Continue in background or Terminate
controller; termination pauses games and waits for the current experiment game.
SIGINT/SIGTERM also request safe shutdown. A stale writer lock requires verified
dead ownership and the explicit `--takeover-stale-lock` flag; never delete a live
workspace lock. Restart restores committed records with clocks paused and jobs
interrupted, requiring explicit Resume/Start.

## Workspaces

- **Play:** human/human or human/engine, standard/FEN positions, untimed or Fischer
  clocks, click/drag and keyboard coordinate moves, explicit promotions, draw
  claims, resignation and audited takeback. New human/engine games initially face
  the human's color; manual flip remains available. Move-history browsing is read-only.
- **Analyze:** independent persistent variations/comments, PGN/FEN imports,
  depth/time/infinite analysis, MultiPV when advertised, Stop and PV branching.
  Progress remains provisional; raw, calibrated cp, external cp and mate labels
  preserve provenance. A running job pins its actual engine identity/options.
- **Engines:** enter an absolute executable path, argv JSON and UCI options JSON;
  explicitly confirm trust before inspection. Register the built engine at the
  absolute `build/native-debug/chess_lab_uci` path. Select trusted Chess Lab
  provenance only for an executable you have identified. Profiles capture SHA-256,
  discovered/effective options, evaluator identity and calibration. Editing creates
  a future-session generation; changed executable/evaluator/absolute argv-file
  artifacts require re-registration. Native modules execute with your OS rights.
- **Experiments:** choose registered engines and M6 match-conditions JSON (see
  `controller/README.md`), then start/pause/resume/stop paired runs. Pause/stop finish
  the current game. Review completed records in Analyze, or export PGN plus JSON
  metadata. The M6 runner owns immutable schedules, attempts and adjudication.
  M7.1 adds **Use independent engine limits**: select depth, movetime in ms or
  Fischer clock separately for Engine A and Engine B. Limits follow engine identity
  through the color swap. Controls and the JSON `engineLimits` array stay in sync;
  switching independent limits off restores the shared JSON `limit`. For Chess Lab
  depth 8 versus Stockfish depth 3, select those profiles as A/B, enable independent
  limits and enter 8/3. Clock mode requires Fischer `time` in JSON. Optional
  **Capture bounded UCI protocol evidence** is off by default; its limits can be
  adjusted through `uciDiagnostics` in JSON. Search statistics and enabled traces
  are included in the metadata export, with explicit missing/truncated/incomplete
  evidence. See the [configuration and evidence format](../controller/README.md#m71-independent-experiment-limits-and-diagnostics).

FEN and PGN export use browser downloads; PGN exports include a JSON metadata
sidecar. PGN upload parses into a new analysis document and never overwrites a
played game. Branches, comments and numeric annotation glyphs round-trip;
unsupported PGN syntax fails visibly. An imported FEN has incomplete history.

Experiment resources are isolated by default. Interactive engine work conflicts
until the experiment pauses or you explicitly authorize and record competition.
Four process slots are available; an experiment reserves two. Full authorization
records are retained in durable command outcomes and exported experiment JSON,
separate from the recent UI window and immutable M6 manifests. Limits never
silently change Hash, Threads, clocks, search limits or experimental conditions.
Play engine turns default to depth 2 when untimed; timed games use their Fischer
clock with a 60-second maximum per-search watchdog. Analysis depth searches also
have a 60-second watchdog. Infinite analysis requires explicit Stop.

## Security and storage

The server accepts exact loopback Host/Origin, one-use bootstrap, HttpOnly
SameSite cookies and CSRF-protected mutations; WebSocket events require the same
session and versioned subprotocol. Bootstrap fragments are removed immediately.
No cloud, accounts, shell strings or generic filesystem RPC. Local processes
running as the same OS user and explicitly trusted native engines are outside
the malicious-webpage threat boundary; this is not an OS sandbox.

`state.json` is checksummed, atomically replaced and fsynced under a single writer
lock. It stores game clocks/history/audits, analysis, profiles, job outcomes,
experiment references, layout and command outcomes. No browser credentials are
persisted. Limits: 10,000 durable command IDs (no eviction), 16 MiB workspace,
256 KiB imports/JSON requests, 4,096 analysis nodes, variation depth 16, two-second
PGN parse budget, 64 sessions and bounded diagnostic/progress history. Reaching a
retention limit requires starting a new workspace; corruption/unknown schema fails
closed rather than silently recreating records. Back up directories while stopped.

## Verify

```sh
npm --prefix controller test
npm --prefix gui run lint
npm --prefix gui test
npm --prefix gui run build
cd gui
npx playwright install --with-deps chromium
npm run e2e
```

E2E uses real temporary workspaces/server/native engine, ephemeral ports and
Chromium. Locally an installed `/usr/bin/chromium` is supported; CI uses the
browser pinned by Playwright. `CHROMIUM_EXECUTABLE` explicitly overrides it.
See [validation](../docs/m7-validation.md) and the
[design checkpoint](../docs/m7-design-checkpoint.md). Linux/Chromium is validated;
M7.1 local Windows x64 validation also covers native Debug build/CTest and all
five browser scenarios using installed Chrome through `CHROMIUM_EXECUTABLE`;
see [M7.1 results](../docs/m7.1-validation.md). Automatic browser opening, macOS packaging
and other browser compatibility remain unvalidated. Installers,
Electron, M8/mobile and additional engine search features are outside M7.
