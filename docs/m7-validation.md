# M7 validation and decision traceability

Implementation review deliverable for approved D118–D138. Base main is
`b057f5b89dacf6b812e61ccf2e344edfe86e7824` (documentation PR #13). The requested
existing branch `m7/desktop-gui` retains its two authorized status corrections;
`af6e3aa` records the required [design checkpoint](m7-design-checkpoint.md) before
implementation. Main was rechecked and remains at the approved documentation
merge. This implementation is not merged; M8 is outside this change.

## Environment and reproducible commands

Local validation: Linux x86_64, GCC 14.2.0, CMake 3.31.6, Ninja 1.11.1,
Node 24.19.0/npm 11.9.0, system Chromium. CI uses Ubuntu 24.04, Node 24.10.0,
Emscripten 4.0.10 at SDK commit `62a853cd3b3134398ce85cde8bb5cbb2ef0194cb`,
and Playwright 1.64.1's pinned Chromium. All npm dependencies are locked with
integrity hashes; runtime notices are retained with the UI assets and controller.

From repository root:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release
cmake --preset native-profile
cmake --build --preset native-profile
ctest --preset native-profile
npm --prefix controller ci --ignore-scripts
npm --prefix controller run typecheck
npm --prefix controller run lint
npm --prefix controller test
npm --prefix gui ci --ignore-scripts
npm --prefix gui run typecheck
npm --prefix gui run lint
npm --prefix gui test
npm --prefix gui run build
cd gui
npx playwright install --with-deps chromium
npm run e2e
```

Local system Chromium is used if available; set `CHROMIUM_EXECUTABLE` for another
explicit binary. CI always uses the Playwright-pinned browser unless explicitly
overridden. Tests isolate temporary workspace/experiment directories and ports;
no user records are modified. In the managed environment, Node IPC/socket tests
need the session's permitted network-enabled execution profile.

| Gate | Observed local result | Evidence |
| --- | --- | --- |
| Native Debug | 11/11 CTest passed | [Debug log](m7-evidence/native-debug.txt) |
| Native Release | 11/11 CTest passed | [Release log](m7-evidence/native-release.txt) |
| Native Profile | 11/11 CTest passed | [Profile log](m7-evidence/native-profile.txt) |
| Controller | Typecheck/lint and 71/71 full regression tests passed | [Node log](m7-evidence/controller.txt) |
| UI | Typecheck/lint/build and 10 Vitest/RTL tests passed | [UI log](m7-evidence/ui.txt) |
| Browser | 4 Playwright scenarios passed against real server/native engine | [Browser log](m7-evidence/browser.txt), [screenshot](m7-evidence/desktop.png) |
| WASM | Local SDK download blocked by environment HTTP 403; not claimed locally | Pinned GitHub Actions gate; review PR links exact head results |

CI preserves the existing native matrix, controller suite and WASM suite, and adds
`.github/workflows/gui.yml`. The shared WASM suite now also runs M7 C and C++
legacy72/extended80/calibration tests. Its status is determined by execution,
not by this workflow's presence. Exact implementation-head CI links/results are
recorded in [implementation PR #14](https://github.com/yctekmen-sudo/chess-engine-lab/pull/14)
once the jobs finish; its Checks tab always identifies the current head.

## What the tests establish

**Protocol/security/persistence.** Real loopback HTTP/WS tests exercise one-use
bootstrap, exact hostile Host/Origin rejection, CSRF, authenticated initial WS
snapshot, unauthenticated upgrade rejection, bounded JSON, stable command retries,
lost ACK, changed-ID-content rejection, stale revision/generation and restart
reconciliation. Gateway tests reject late/duplicate/root-mismatched progress,
ignore snapshot rollback, refresh revision gaps and diagnose incompatible versions.
Provisional PVs are replay-validated at the captured root; illegal PVs are omitted.
The UI hides old-root and failed-session results and disables board actions while
an authoritative legal-position projection is loading.
Repeated launcher uses the owner-only local channel and stores no launch/CSRF
secret in the runtime locator. Workspace state uses existing integrity envelopes,
writer locks, atomic rename and fsync. Injected write failures cannot acknowledge
or publish moves. A separate post-rename failure fixture proves that uncertain
storage freezes subsequent writes, safe close preserves the disk checkpoint and
restart resolves the same command exactly once.

**Games/clocks/documents.** M6 regression fixtures retain draw claims, history
completeness, legal proposal validation, technical failures, TT behavior and timeout
adjudication including uncertainty. Added virtual-clock tests prove timely receipt
survives validation after the deadline, exact paused restoration, audited takeback
clock restoration and duplicate Resume cannot reset a running clock. Analysis
branches never change official records. PGN tests cover legal SAN, branches,
comments, NAGs, headers, initial played prefixes, root annotations and atomic
malformed-input rejection. Import/export retains partial FEN history, including an explicitly imported
standard-position FEN using SetUp/FEN headers. Analysis history
navigation targets the displayed ancestor; branching before an imported played
prefix creates an independent document. Analysis export uses `Result "*"`; source
headers/records remain in its JSON sidecar rather than asserting an analysis tree
is a newly played result.

**Calibration/ABI.** Existing M2 C/C++ layout tests stay unchanged. M7 asserts old
72-byte prefix, new 80-byte table, tail at 72 on native/WASM and an old-capacity
canary. A native shared library compiled with a frozen v1.0 header rejects minor 1
and loads through fallback. Tests cover linear rounding/overflow, lookup anchors,
interpolation, sign/ordering/zero policy and out-of-domain raw-only, malformed
optional downgrade without evaluator failure, descriptor/config digests and raw
fingerprint separation. Real native transcripts check both White/Black root cp
polarity, retained raw diagnostics and mate bypass. All pre-existing M5 TT and
M6 evaluator-failure suites run unchanged. See [exact ABI mechanics](m7-calibration-abi.md).

**Registry/resources/experiments.** Explicit native trust precedes inspection;
SHA/effective options and evaluator/absolute argv-file artifacts are pinned and
rechecked. Job provenance records the actual started identity; calibration status uses its
final configuration generation rather than earlier handshake entries. Editing future
profiles cannot relabel active results. No capability is inferred for ordinary UCI
engines without MultiPV. Tests change a registered argv artifact and reject launch.
The scheduler rejects default experiment/interactive competition, records explicit
authorization, enforces four process slots and releases stopped leases. GUI run
control delegates to the M6 runner's optional `shouldContinue` boundary hook;
pause/stop tests preserve the first completed result byte-for-byte, resume the
remaining pair once and retain technical result categories. Execution metadata
is separate from immutable manifests/attempts. Existing M6 crash/lock/recovery and
real CLI integration tests remain in the full Node gate.

**Real browser/accessibility.** Playwright exercises human/human and real
human/engine play, takeback, registry/trust, analysis, PGN variation import,
refresh, shared tabs, browser close continuing the server, paused restart, explicit
promotion through the actual board dialog, FEN/PGN downloads, paired experiments
and per-game review. Offline snapshot failure disables mutations; reconnect
reconciles authority; Quit covers continue and terminate with paused final games.
RTL tests cover the board adapter, explicit promotion, keyboard move alternative,
history cursor, trust/engine-mode form constraints and truthful score labels.
Controls have accessible labels, textual status/errors and focusable dialog choices.

## Issues found and corrected during validation

- Preserving Game identity in transactional forks was necessary for M6 binding
  verification; virtual receipt-race and actual engine-reply fixtures cover it.
- Browser offline mode can leave an existing WebSocket open. Failed authoritative
  snapshot refresh now marks disconnection and reconnects; the browser test waits
  for the resume ACK before simulating network loss.
- An initial browser reply timeout occurred while three native builds ran
  concurrently. The scenario passed alone; its engine-reply budget is 15 seconds,
  with final independent browser execution passing all scenarios.
- Host spoofing is tested with raw Node HTTP because Node fetch normalizes Host.
- Experiment pause/stop payloads, job-bound calibration provenance, repeated Resume
  clock reset, exported played prefixes/annotations and post-rename persistence
  uncertainty were corrected and received regression coverage.

No approved architecture or frozen M0–M6 behavior was changed to resolve these
issues. Optional GUI/ABI capabilities remain additive. No implementation conflict
requiring an unapproved architecture decision was found.

## Decision-to-implementation traceability

Paths below are repository-relative. `m7-workspace` means
`controller/test/m7-workspace.test.mjs`; browser means
`gui/e2e/workspace.spec.ts`; UI means `gui/test/*.test.tsx`.

| Decision | Implementation | Validation |
| --- | --- | --- |
| D118 local authoritative desktop scope | `gui/`, `controller/src/workspace/`, `server/` | Browser four workspaces; preserved M6 tests |
| D119 versioned local HTTP/WS, CLI independence | `server/server.ts`, `workspace/protocol.ts`, lazy `cli.ts` import | Real HTTP/WS/security; existing CLI integration |
| D120 shared workspace, close/Quit, revisions | `workspace/service.ts`, `gui/src/api.ts`, Quit dialog | Two tabs, refresh, browser X, both Quit choices |
| D121 atomic persistence, paused restart | `service.ts`, additive `game.ts` checkpoints, M6 `store.ts` reuse | Lost ACK/restart, receipt clock, injected pre/post-rename failure |
| D122 React/TS/Vite local assets | `gui/package*.json`, Vite config, server static confinement | Locked install, typecheck/lint/build, offline browser |
| D123 board presentation adapter | `gui/src/Board.tsx` | RTL drop/promotion/keyboard; actual board promotion E2E |
| D124 four spaces, persisted layout | `gui/src/main.tsx`, `style.css`, settings commands | Browser navigation/refresh; settings validation |
| D125 casual modes/rules/clocks | `service.ts` game operations, M6 Game/FideRules | Virtual clocks, real human/engine, M6 claim/timeout/failure fixtures |
| D126 history, branches, audited takeback | `documents.ts`, service/game checkpoints, MoveHistory | Clock/audit and branch isolation; UI history, browser takeback |
| D127 truthful raw/calibrated cp/table | `engine/src/evaluation/host.cpp`, UCI adapter | Native/WASM calibration fixture, native polarity/mate transcripts |
| D128 optional compatible calibration ABI | `chess_eval_abi_v1.h`, evaluation host/accessors | Frozen v1.0 module, C/C++ 72/80/offset/canary, invalid downgrade |
| D129 immutable profile generations/identity | Registry/verify/job identity in `service.ts`, Engines panel | Trust, retry, changed argv rejection, real registration |
| D130 M6 experiment controls/review | M6 `shouldContinue`, service run state, Experiments panel | Pair pause/stop/resume, immutable result, browser review/export |
| D131 bounded FEN/PGN/annotations | `documents.ts`, scoped server export, browser uploads/downloads | Header/prefix/NAG/variation round-trip and invalid rejection |
| D132 resource isolation/consent | Service lease cap, authorization log, `m7-execution.json` | Conflict/consent/cap/release fixture |
| D133 ordered snapshots/progress/reconnect | Protocol, server event transport, Gateway | Stale/gap/version fixtures, multi-tab/offline/restart browser |
| D134 loopback/session/origin/file/process limits | Server Host/Origin/CSRF/WS checks, trust, scoped static/export | Host/Origin/auth/CSRF/oversize fixtures and source boundary review |
| D135 singleton launch/relaunch | `server/launcher.ts`, `cli.ts`, local launch socket | Repeated launcher, credential-free locator, Quit/browser-close E2E |
| D136 preserved and added CI | Native/controller/WASM workflows plus `gui.yml`, all test layers | Local results above; exact-head CI linked in review PR |
| D137 bounded structured failures/recovery | Fault/Problem, diagnostics, UI disconnect/alerts, write freeze | Persistence failures, protocol errors, registry change, offline UI |
| D138 approved freeze/review boundary | Base #13, checkpoint `af6e3aa`, one M7 implementation PR | No merge, M8 or search-feature expansion |

## Explicit limits

Linux/Chromium is exercised; Windows/macOS launch paths and other browsers are
unvalidated. This is a local application, not an OS sandbox for native modules or
protection against another process running as the same user. Artifacts must remain
immutable while loaded, as required by M2/M6. The recent authorization window is
bounded to 500 entries; full authorization metadata remains in non-evicted durable
command ACKs (10,000-command cap). Launch/final experiment sidecars and GUI JSON
exports include the applicable authorization records, including crash-time records
reconciled from the workspace.
Workspace command/size limits require a new workspace rather than unsafe ID eviction.
Browser X cannot guarantee a custom dialog and intentionally keeps work running.
WASM is a core/calibration CI correctness target, not a browser engine-process
implementation. Installer/Electron/mobile/cloud features remain deferred.


## PR #14 review follow-up

The review at `b69ceaa` requested Windows static-serving and registration fixes,
human-facing initial orientation, and native Windows smoke coverage.

- Static confinement now uses native `path.relative`, `path.sep` and
  `path.isAbsolute` rather than a Unix-only string prefix. Both the lexical
  candidate and final `realpath` must stay below the canonical asset root.
- The frontend requires a nonempty executable path and explicit trust; absolute
  path validation remains in the controller. A `C:\chess-lab\chess_lab_uci.exe`
  form regression exercises registration without a Unix-only path gate.
- Creating human/engine games atomically sets orientation toward the human.
  The GUI follows the committed orientation; manual flip remains available and
  persists until a new human/engine game chooses its initial orientation.
- `controller/test/m7-desktop-smoke.test.mjs` tests real HTTP `/` and `/assets/*`,
  asset paths with spaces and a root junction/symlink, sibling traversal and
  junction escape rejection, native executable registration with argv, relative
  path rejection, both human colors, manual flip and launcher IPC reconnect.
  It optionally serves the actual Vite build via `CHESS_LAB_GUI_ASSETS`.
- `.github/workflows/gui.yml` adds `windows-2022` / Node 24.10.0 smoke coverage
  using the same production controller and a real Node executable speaking UCI.
  This tests Windows process/path/named-pipe behavior without claiming a Windows
  C++ engine build, automatic browser opening, or full Windows browser E2E.

Local review results: 2/2 desktop smoke tests, 11/11 Vitest/RTL tests, 5/5 real
Chromium scenarios, controller and GUI static checks/build passed. Retained logs:
[desktop smoke](m7-evidence/review-smoke.txt), [UI](m7-evidence/review-ui.txt),
[browser](m7-evidence/review-browser.txt). Native/controller/WASM/GUI workflows
rerun on the pushed review commit; exact-head results and Windows status are
recorded in PR #14. Keep the PR unmerged for re-review.
