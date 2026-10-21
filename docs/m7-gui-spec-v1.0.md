# Chess Engine Lab — M7 Desktop GUI and Local Controller Specification v1.0

**Status:** Architecture APPROVED (D118–D137 individually, D138 architecture freeze). This is a proposed implementation specification for review; **not yet approved or merged**. Implementation must wait for this spec and the M7 kickoff to be reviewed and merged on `main`.
**Milestone:** M7 desktop browser GUI, local Node controller/server, pluggable evaluator calibration display, interactive games, analysis and experiments.
**Baseline:** M6 merged on `main` at `b9c2ebd22019981ea9acc5665be8ccd39e21a5f3` when drafting. Authoritative older contracts: `docs/m6-controller-spec-v1.0.md`, `docs/evaluation-abi-v1.0.md`, M3–M5 specifications, C++ and TypeScript code. The implementation branch must re-check the current `main` SHA before any change.
**Change rule:** User approval was per-decision for M7. D138 freezes feature-level architecture. Implementer may resolve compatible mechanical details, but any material behavioral conflict, new scope, or breach of M0–M6 guarantees requires explicit user review.

## 1. Approved decision ledger

| Decision | Binding M7 requirement |
| --- | --- |
| **D118** | Local/offline desktop React/TypeScript GUI for play, analysis, evaluator configuration, experiments, history and FEN/PGN. M6 controller authoritative; native UCI subprocesses; M8 mobile deferred. |
| **D119** | One loopback Node HTTP + WebSocket server with typed versioned contracts, controller authority, reconnection and authenticated local sessions with Origin validation. CLI remains independent. |
| **D120** | Controller-owned in-memory workspace shared across tabs; survives refresh. Browser close does not automatically kill controller. In-app Close/Quit offers terminate vs continue in background; browser X cannot guarantee a custom dialog. Safe graceful shutdown; revision/idempotency. |
| **D121** | Automatic atomic local game/workspace persistence. Restart restores latest committed game with clocks paused; never resumes searches automatically; revalidate engine identity. |
| **D122** | React + TypeScript + Vite SPA served as static assets by local Node controller, no production cloud/web-server dependency. |
| **D123** | `react-chessboard` behind a presentation-only adapter. Drag/click moves, promotion choices, rotation and annotations; controller validates all committed moves. |
| **D124** | Play, Analyze, Experiments, Engines top-level workspaces; shared board-centric Play/Analyze, dedicated management panels, resizable/collapsible and persisted layout. |
| **D125** | Casual Human vs Engine and local Human vs Human in Play; Engine vs Engine through Experiments. Standard/FEN roots, Fischer or untimed, authoritative rules/clocks/outcomes. Analysis never alters official game. |
| **D126** | Move-history browsing and persistent branching analysis; separate from actual played moves. Casual auditable takebacks restore clock snapshot, invalidate old search sessions; experiments immutable. |
| **D127** | Raw White-positive evaluator scores remain search inputs. Optional evaluator-owned calibration descriptor (linear or lookup-table/linear interpolation) lets UCI adapter emit honest centipawns; raw retained. No uncalibrated conversion or extrapolation, mate separate. |
| **D128** | Backward-compatible, versioned instance-specific calibration capability; host validates, binds to effective configuration/identity, tracks provenance. Invalid optional calibration disables cp presentation rather than search. |
| **D129** | Persistent registered engine definitions/configs separate from running instances; controller option discovery, identity checking and subprocess management. Changes never rewrite active searches/immutable experiments. |
| **D130** | GUI for creating, watching, pausing at safe game boundaries, resuming/stopping M6 experiments, per-game review/PGN export; immutable schedule and results, technical failures separate from chess outcomes. |
| **D131** | FEN and PGN import/export with variations/compatible annotations. Controller parses/validates, protects current games, tracks history completeness and clock semantics. |
| **D132** | Controller scheduler coordinates resource use. Experiment isolation by default; interactive competition requires explicit authorization and recording; bounded subprocess/resources. |
| **D133** | Complete authoritative snapshots plus ordered, versioned state events over WebSocket. Command IDs/expected revisions; session/root-bound search progress; reconnect reconciliation. |
| **D134** | Loopback binding; authenticated browser sessions, strict Host/Origin/WebSocket validation, constrained file/subprocess actions, no cloud/accounts. Native engine executables remain untrusted and not OS-sandboxed. |
| **D135** | `chess-lab gui` or lightweight shortcut launches/reconnects one local controller and opens default browser. No Electron; installers deferred. |
| **D136** | Vitest/React Testing Library/Playwright plus real server/engine contract tests; all existing M0–M6 native/Node/WASM CI gates preserved. |
| **D137** | Structured severities and error codes, bounded local diagnostic history, explicit recoveries, no silent fallback and no false success. |
| **D138** | Freeze architecture D118–D137; documentation PR first, then approved Codex kickoff / implementation PR. Material gaps return for decision rather than being silently resolved. |

## 2. Authority, deliverables and repository boundaries

Production layout (anticipated):

~~~text
engine/                         C++ chess core, evaluator host, search, UCI
controller/src/                 reusable headless M6 library
controller/src/server/          M7 HTTP/WS application adapter
controller/src/workspace/       M7 game/analysis/registry/experiment orchestration
controller/src/calibration/     validated score descriptor interpretation where needed
gui/                            React/TypeScript/Vite SPA, typed client, tests
docs/m7-gui-spec-v1.0.md        this contract
docs/codex-kickoff-m7.md        implementation gate/checklist
~~~

Keep actual dependency edges `GUI -> typed transport -> controller library -> managed UCI subprocesses`. Browser must not import `chess.js` as rules authority; it may display legal destinations transmitted by controller. Never add a native Node binding. Existing `controller/src/cli.ts` CLI continues to work and must not require an HTTP server.

M6 current state confirmed during drafting: `controller/src/engine.ts` provides `ManagedEngine`, safe argv spawn and UCI session handling; `controller/src/game.ts` is authoritative for game outcomes/clocks; `controller/src/analysis.ts` manages one-position analysis; `controller/src/experiment.ts` has synchronous-through-the-schedule async `resumeExperiment`, status/export, immutable attempts; `controller/src/store.ts` implements atomic records and writer locks. The M6 runner **does not yet have** GUI pause/resume/stop boundaries; those are M7 implementation work, not preexisting APIs. M6's `engine/src/uci/main.cpp` currently emits finite arbitrary scores as `info string chesslab score raw white ...`; it does not yet emit calibrated `score cp`.

## 3. Workspace objects, commands, projections and persistence

Authoritative workspace belongs to one local controller process; multi-tab clients view the same objects. Objects include `workspaceId`, `workspaceRevision`, `games`, `activeGameId`, `analysisDocuments` with variation trees, registered engine profiles, experiment references/status, settings/layout and current runtime activity. Each game stores immutable `initialFen`, ordered actual UCI moves, `historyComplete`, explicit outcome/claims, player assignments, time control and replay-auditable clock snapshots/events. Analysis variation nodes carry parent, UCI move, annotations and cursor independent of the official game. No GUI-computed board mutation is authoritative.

Mutation flow: validate client credential, request size/shape, command name, `commandId`, relevant object ID and expected revision/generation; serialize per relevant object; validate chess rules/outcomes and permissions; atomically commit durable state **before** reporting command success or broadcasting its committed revision. Return either one definitive ACK with new revision or a typed non-success (stale revision, validation, authentication, unavailable, engine failure). Retries of the same `commandId` with identical content return the same recorded outcome even if the first response was lost; same ID/different payload is rejected. Scope retained deduplication to meaningful workspace lifetime/persistence; do not promise infinite retention. Log/replay identity prevents double-accepted moves or duplicate destructive actions after process restart.

Durable game storage is **separate** from M6 immutable experiment storage. Reuse validated durable atomic-write/integrity/locking patterns from `store.ts` rather than rewriting old experiment records. Use versioned schemas, one writer per workspace, integrity checks, deterministic recovery and explicit migration/rejection for unknown schema versions. Persist game snapshots at every accepted mutation and persist settings/registry changes before ACK. Prefer bounded, checkpointed event journal and/or append-safe events with integrity checks for takeback auditing; never append to an experiment's completed results. A corrupt newest checkpoint must be diagnosed, not treated as a valid fresh game; last verified committed state may be recovered with explicit notice. No tokens in durable records.

Restored clocks are paused without retroactively consuming downtime; resume requires an explicit command. Engine process state, transposition table, searches, volatile stream revisions and local credentials do not survive restart. Registered executable/evaluator artifacts are freshly identified before launch, changes require confirmation and cannot silently resume immutable experiments. Graceful shutdown stops accepting new mutations, finishes or explicitly interrupts active work under relevant policies, commits state, closes managed processes, releases locks and stops server; do not mark incomplete attempts as completed.

## 4. API, state revisions and WebSocket lifecycle

Use one versioned typed JSON protocol (for example `/api/v1` and `chesslab.ws.v1`) with shared server/client TypeScript types plus runtime input validation. Schema sample (illustrative names; implementation checkpoint fixes field list and validation constraints):

~~~json
{
  "commandId": "uuid",
  "operation": "game.move",
  "objectId": "game-uuid",
  "expectedRevision": 42,
  "expectedGeneration": 7,
  "payload": {"move": "e2e4"}
}
~~~

On mutation response, include `commandId`, accepted/rejected status, authoritative new revision or typed error code and context. Read-only snapshot covers relevant objects and current controller revision. Version handshake rejects incompatible clients; do not silently reinterpret unknown fields. Suitable typed operations: workspace snapshot/settings, game create/select/move/resign/claim/takeback/pause/resume, history cursor/analysis branch, engine registry/inspect, analysis start/stop, experiment create/start/status/pause/resume/stop/export, import/export, controller quit. Names are illustrative, not license for unbounded generic filesystem/process RPC.

Committed state-change events use strictly increasing workspace revisions with object identity/generation. Ephemeral search-progress streams carry session ID, root FEN/history fingerprint, position generation, sequence and provisional flag; they never impersonate committed state or create authoritative move events. GUI discards unknown/late session events, detects missing committed revisions, and refreshes an authoritative snapshot on gaps/reconnect. Do not rely on clocks or arrival order across HTTP/WebSocket channels to linearize commands.

Support browser refresh and multiple tabs. Startup obtains authenticated snapshot and subscribes/reconciles without gaps (server may implement snapshot-plus-subscribe handshake or sequence cursor/replay). Offline browser sends no optimistic authoritative commits and does not claim success for unresolved requests. On reconnect, safely query command status by ID when needed; no blind reissue of non-idempotent actions. Visible disconnect and error affordances.

## 5. Frontend: Play / Analyze / Experiments / Engines

React + TypeScript + Vite SPA compiled into local static assets and served by Node; `react-chessboard` wrapped by internal board adapter with pinned tested dependency and license. UI layout has four top-level workspaces. Play and Analyze share chessboard, history and board navigation; experiments/registry have dedicated screens. Persist relevant layout preferences. The frontend state store is a projection of controller snapshots plus explicitly temporary visual state.

**Play.** Human vs Engine or local Human vs Human; registered engine/color selection and settings; optional valid initial FEN; untimed or Fischer clocks; all promotions explicitly chosen; drag/drop and click-to-move; last move, legal-square hints from controller, board rotation. Turn ownership enforced server-side. Move commits on ACK; optimistic animation must roll back on failure. Resign/claims/outcomes from controller, not UI heuristics. History navigation is read-only. In-app Quit has Terminate controller / Continue in background choice, with warning on active work. Closing browser chrome X leaves controller running; browser-level beforeunload prompt is best-effort only and never presented as guaranteed custom popup.

**Takeback.** Only casual games; explicit controller command and durable audit event. Human/engine default takes back the most recent exchange when applicable; at edge cases use a legal well-defined rollback target. Restore recorded clock snapshot, pause/restart as appropriate, increment game generation and cancel/replace old search before allowing another engine move. Do not falsify a historical game record: retained audit states which actual moves were undone. Experiments have no takeback.

**Analyze.** Inspect arbitrary legal positions and game-history cursors without altering committed game record. Separate persistent branching variations (including comments/annotations), navigation, PV selection. Controls for start/stop, depth/time/infinite (only with explicit stop/cancellation), MultiPV, evaluator and settings; completed-depth progress only. Show depth, nodes, legal PVs, mate and raw/validly-calibrated cp, origin and session; provisional output cannot drive game actions. External engines' UCI `score cp` is *engine-reported*, not independently certified comparable to another engine's calibration. Switching position/evaluator invalidates old analysis session display.

**Experiments.** GUI calls controller's immutable M6 schedule, runner/status and export model. Defaults remain color-swapped paired sequential games, recorded seed, original FEN, precise engine identities and fixed-depth vs clock-limited conditions. Show game-level progress and explicit chess W/D/L, technical forfeits, invalids, unresolved/timeout adjudication separately. Stop/pause requests are cooperative at a safe completed-game boundary by default, and current active game continues until that boundary unless an explicit separate failure/cancellation policy is approved. Controller adds run-control state **outside** immutable manifest/schedule; distinguish `pause_requested`, `paused`, `stop_requested`, `stopped`, `completed` and `failed` where appropriate. Resume preserves M6 recovery: completed games never rerun, interrupted attempts remain documented, unfinished scheduled games restart from original opening with new attempt. Do not claim M6's original `resumeExperiment` already implements these controls. Preserve M6 CLI compatibility and immutable record schema where feasible. If users authorize resource-sharing with interactive sessions, record actual concurrency policy/execution provenance separately from the frozen experimental conditions.

**Engines.** Persistent registry of executable path/argv/kind and pinned SHA-256, friendly label, supported UCI options/effective settings, evaluator native module path/artifact identity and effective JSON; validate every setup before launch; no arbitrary shell. Engine definitions are not processes. Live edits create new configuration generation for subsequent sessions, never silently alter active game/experiment process. Distinguish untrusted external UCI claims from explicitly configured trusted Chess Lab extensions. Evaluator failures follow M2–M6 quarantine and fail rather than silent fallback.

## 6. Calibration ABI extension and UCI presentation (D127–D128)

Current M2 public header `engine/include/chess_lab/chess_eval_abi_v1.h` uses ABI major 1/minor 0 and a size-tagged `ChessEvalApi` struct with the existing JSON buffer convention. **Do not change existing field offsets or ABI major; do not require existing modules to recompile.** Introduce an optional minor-version capability by appending a function-pointer slot to the tail of the size-tagged API, negotiated through the existing `ChessEvalGetApiFn`. The function obtains *instance-specific* immutable descriptor JSON after configuration normalization; an evaluator without this slot is `raw_only`. A host should negotiate the extension when available, otherwise fall back to the original contract and must never read an absent tail. The implementation checkpoint must show a native + WASM ABI layout/struct-size compatibility proof (including 8-byte pointer slots) and old-v1-module fixture. If current version negotiation cannot provide this safely, stop for architectural review rather than breaking v1.

Versioned descriptor examples:

~~~json
{"schema_version":1,"target":"centipawns","kind":"linear","scale":100.0}
~~~

~~~json
{"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only",
 "points":[[-3.0,-600],[-2.0,-300],[-1.0,-100],[0.0,0],[1.0,100],[2.0,300],[3.0,600]]}
~~~

Descriptor is canonicalized, hashed, bounded in bytes/number of points, finite-only and checked *after effective evaluator configuration*. `linear` requires finite strictly positive scale, implicit zero-to-zero. `lookup_table` requires at least two points; strictly increasing raw abscissae, strictly increasing mapped cp ordinates, explicit zero->zero anchor, and sign preservation across domain. Interpolation is linear between adjacent points only; no extrapolation, clamping, or invented conversion. Values outside the declared table domain report raw-only; emitted cp must fit supported bounded UCI integer range and use documented deterministic rounding. Unknown schema/kind, missing capability, nonfinite, malformed, nonmonotonic or inconsistent metadata disables cp **for that evaluator instance** with diagnostic; search can continue with valid raw evaluations. Static evaluate() itself retains strict `EVALUATOR_FAILED` handling and must not be masked by a calibration failure.

Search still uses original White-positive M2 scores and retains all M5 TT numeric-compatibility rules. Only after a committed completed depth does adapter transform finite evaluation values with the active descriptor, convert to UCI side-to-move polarity and emit `info ... score cp N`. Valid proven mate scores continue emitting `score mate`, never mapped to cp. Keep raw diagnostics/provenance available even when cp exists. No cross-engine equality or probabilistic win chance is implied by centipawn display.

Calibration is presentation-only, and instance configuration+evaluator artifact SHA+descriptor version/digest must be recorded for any experiment/reporting outputs that use calibrated display. Do not rewrite historical M6 immutable manifests; define compatible new optional sidecars/new-record fields for M7 runs. Ensure reconfiguration/identity changes invalidate cached calibration and display projections. Avoid executing evaluator-supplied code at display time; adapter interprets only standardized descriptors.

## 7. FEN/PGN, chess semantics and experimental integrity

Controller uses existing `FideRules`/`Game` for validation and adjudication; never replace with `chess.js.isGameOver()`. FEN import creates a new analysis/game object by default, complete historical repetition is not inferred from FEN. PGN parses SAN headers, moves and compatible comments/NAGs/variations under bounded size/depth/time; preserve actual move mainline and variation tree separately. For standard original-root PGN with fully supplied moves, record completeness correctly; for arbitrary SetUp FEN, preceding repetition history is unknown. Malformed move/illegal position causes atomic import rejection rather than partial mutation of active game. Export standards-compliant PGN/FEN and optional sidecar for calibration/technical results; retain mate/draw claim and metadata semantics. Parsed text never executes code or launches engines.

Game adjudication and timeouts remain exactly M6 (including `UNDETERMINED` timeout-mate possibility and adjudication required). GUI must not infer a decisive result from uncertain proof. Clock charge uses authoritative monotonic time during live process only. No engine-supplied provisional info may commit a move.

## 8. Local server security, launcher and operational limits

Server binds loopback only (`127.0.0.1` and/or `::1`), dynamically chosen port, permitted Host and Origin allowlist, DNS-rebinding defense, anti-CSRF controls for credentialed actions and authenticated HTTP/WebSocket requests. Launcher starts or connects one server for one workspace, distributes a one-time or similarly secure local bootstrap credential to the browser, and opens an authenticated page without placing reusable secrets in logs or long-lived URLs. Rotate/reject invalid credentials across controller restarts; keep secrets out of persisted workspace and diagnostics. No wildcard CORS, unrestricted static filesystem serving or direct path-to-command RPC. Strict size limits, object validation, filename/path authorization and safe `spawn(...,{shell:false})`. Browser X close is not detectable with guaranteed custom UI; controller remains running by default. Idle-shutdown is optional and never interrupts work; no fixed timeout is approved.

Engine subprocesses operate with local OS user rights; GUI API safeguards do not sandbox untrusted executables. Show trust warning before registering/running new native executables or modules. Don't claim remote multi-user access. Keep a bounded structured diagnostics stream/history (redacted credentials); classify information/warning/failure; no fabricated success and explicit retry/restart actions.

Resource scheduler maintains controller-owned leases for engine processes and searches with configured bounded limits and no silent option modification. Default experiment isolation is exclusive against unrelated engine workloads, even if multiple GUI tabs open. Requests for interactive analysis during an experiment may request safe pause at next game boundary; optional explicitly authorized concurrent use is recorded. Deadlocks/cancellation/crashes must release leases. Do not silently alter `threads`/`hash` engine options to fit capacity.

## 9. Tests, CI and evidence

- **Unit/UI (Vitest + React Testing Library):** board adapter, click/drag, promotions, variation navigation, takeback controls, calibrated/raw/mate displays, error and disconnected states, workspace switching and layout persistence.
- **Typed transport integration:** malformed input limits, auth, Host/Origin/WebSocket/CSRF, command-id retries and conflicting payloads, stale revisions/generations, interleaved tabs, event revision gaps, reconnect without lost mutations, startup/shutdown, safe file access.
- **Chess/end-to-end (Playwright):** real browser->local server->controller->native engine, human-engine game, human-human, promotion, claim, clock/timeouts, missed deadline, analysis/MultiPV and PV navigation, imported PGN/FEN, takebacks with audit/clock restoration, experiment monitoring, exports, refresh, browser X vs in-app Quit, restart with paused clocks.
- **Calibration:** old M2 v1 evaluator works as raw-only; new minor-capable native/WASM fixture; valid linear/lookup interpolation, boundary/out-of-range behavior, large values, rounding, malformed/unknown descriptor, per-config change and digest, mate bypass, search failure strictness, UCI perspective both sides. Native-only capability must not break WASM CI.
- **Experiment/scheduling:** immutable pair assignments and manifests, pause at game boundary, resume after crash, no duplicate terminal result, technical vs chess outcome counts, contention authorization metadata, stale lock handling, bounded subprocess resource cleanup, M6 CLI unchanged.
- **Baseline gates:** Node 24 pinned install/build/typecheck/lint/test, native Debug/Release/Profile CMake/CTest and pinned Emscripten WASM CI; retain existing end-to-end UCI mock/adversarial tests. E2E CI uses headless browser with deterministic fixtures and actual engine smoke when practical; maintain test artifact/log links in PR.
- **Accessibility/error:** keyboard controls/focus and promotion selection, readable result/score labels, no color-only signals, meaningful error messages and non-lying success states.

No test may rely on wall-clock timing for the exact ordering of racing clock deadlines; use injectable monotonic clocks and deterministic event scheduling for those fixtures. Record actual commands, revisions, SHA identities and repro steps for any failures. Test browser close limitations honestly, not with an impossible custom-X promise.

## 10. Delivery boundary and unresolved implementation checks

M7 implementation deliverable: production local GUI, controller API/server, desktop launcher, persistent casual-game workspace and registered engines, analysis/variation/score calibration, experiment dashboard + pause-safe scheduler, imports/exports, automated tests and documentation. M8 mobile, remote multiplayer, cloud sync/accounts, installers, Electron, operating-system sandbox, new chess-search algorithms, distributed/parallel match execution, opening-book/rating research and unapproved new calibration forms remain out of scope.

Before coding, prepare a design checkpoint with exact typed HTTP/WS schemas; mutable vs immutable on-disk formats; one-writer lock and idempotency-recovery horizon; security/bootstrap/session construction; calibrator ABI minor negotiation/wrapper/protocol; M6 runner refactor plan with lifecycle-safe pause/stop; clock+takeback replay; external-UCI truthful score handling; library versions/licenses and CI fixtures. Review any conflict with established M6 semantics rather than hiding it. A documentation-only PR for this spec and kickoff is reviewed first. Implementation follows in a **separate unmerged PR**, not directly on `main`.
