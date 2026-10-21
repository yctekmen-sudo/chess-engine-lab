# Chess Engine Lab — Codex Kickoff for M7

**Status:** D118–D138 architecture approved; **this kickoff and accompanying M7 implementation specification are pending documentation-PR review/merge**. Do **not** implement until both `docs/m7-gui-spec-v1.0.md` and this file are merged on `main` and the user authorizes M7 implementation.
**Milestone:** M7 local desktop GUI / Node server; React + TypeScript + Vite; controller-authoritative interaction and durable games.
**Authoritative contracts:** `docs/m7-gui-spec-v1.0.md` (D118–D138), `docs/m6-controller-spec-v1.0.md` (D84–D117), `docs/evaluation-abi-v1.0.md`, M3–M5 specs and current code. More restrictive existing guarantees continue to apply.
**Delivery boundary:** One reviewed **implementation feature PR**, not merged without explicit instruction. No M8, cloud services, Electron, parallel experiment engine, new chess search features or unrestricted native engine execution.

## 1. Preflight, ownership and design checkpoint (mandatory)

1. Pull latest `origin/main` and record exact base commit. Assert the reviewed M7 spec **and** this kickoff are on `main`. If absent, modified, not yet approved, or inconsistent, **STOP**. Branch `m7/desktop-gui` (or suitably unique name) from that exact base, not from stale docs branch.
2. Inspect actual M6 code and native ABI before writing interfaces: `controller/src/{index,engine,game,rules,analysis,experiment,store,cli}.ts`; `engine/src/uci/main.cpp`; `engine/include/chess_lab/chess_eval_abi_v1.h` and evaluation host; build/CI/fixtures. Preserve controller/CLI and existing game/experiment identities. Do not assume the current `resumeExperiment` supports pause.
3. First commit or PR discussion must contain `docs/m7-design-checkpoint.md` with:
   - Exact process/package directory ownership and dependency arrows.
   - Typed JSON command/snapshot/state-event/search-progress contracts, revisions, ID deduplication durability and reconnection-gap handling.
   - Session-bootstrap flow, HTTP/WS origin and host checks, CSRF defense, file picker/path capabilities, launch/relaunch/quit behavior, limits and threat model.
   - Interactive game snapshot/journal layout, crash/checkpoint recovery, paused clocks, replay/auditable takeback handling, retention and schema migration.
   - Registry/executable SHA/effective evaluator options identity; trusted Chess Lab claims vs external UCI; no mutable active engine options.
   - Calibration descriptor JSON schema, version-negotiated *optional* ABI tail and native/WASM struct-size proof; raw score -> cp/side-to-move exact formatting; unsupported/malformed descriptor downgrade.
   - M6 experiment runner changes for safe boundary pause/stop, concurrency scheduler, immutable attempt/manifest and CLI compatibility.
   - Third-party versions/licenses (including react-chessboard), component accessibility, E2E runner/startup and pinned CI plan.
4. If delivering those contracts demands changing a frozen feature or weakening an M0–M6 safety guarantee, **STOP and request architectural review**. No unapproved substitutions.

## 2. Suggested implementation order (each step separately testable)

### A. Controller protocol and security substrate
- Extend TypeScript controller (keep `src/index.ts` reusable) with workspace snapshots, typed versioned validation, object revisions/generations and serialized idempotent commands. Durable accepted changes commit before ACK; reject replayed IDs with different payloads.
- Build local loopback HTTP and WebSocket adapter, secure session bootstrap, Host/Origin and CSRF enforcement, bounded JSON and streaming events. Committed revisions ordered, ephemeral progress independently session-bound. Reconnect via authoritative snapshot; recovery from gaps/reordered delivery is correct even across HTTP and WS boundaries.
- Add controlled filesystem actions and safe process launch. No arbitrary shell, uncontrolled RPC paths or secret leakage.
- Keep existing CLI functional without loading web server or React artifacts. Prove safety on two tabs, lost ACK/retry, stale game mutation, websocket interruption, restarts and malicious origins.

### B. Durable casual games, rules, clocks and analysis model
- Implement authoritative workspace/game save, object lifecycle, move commits, time controls, draw claims, resignation, player-turn binding and pause/resume. Persist latest committed state atomically and log takeback events. Game records reconstruct from start FEN and actual ordered moves, maintaining completeness.
- Restore paused clocks exactly after controller restart, with no search automatically running; inactive tabs never drive clocks. Implement auditable takeback as distinct operation; isolate analysis branch trees from official moves.
- Reuse `FideRules` and `Game` outcomes rather than relying on UI or `chess.js` convenience outcome predicates; retain M6 `adjudication_required` for uncertain timeout possibility.
- Keep engine-session management/technical failures/controller-side validation; stale completion cannot move a restored or replaced game.

### C. Engine registry and evaluator calibration
- Add persistent engine/evaluator profiles separately from live child processes; register by absolute path, validated argv/options and executable SHA-256; recheck identity before run. Changing a profile never mutates an in-flight search; report changes and failures.
- Extend evaluator API only compatibly: append optional calibration metadata function slot to M2 `ChessEvalApi` via negotiated minor version/struct size; old v1.0 providers load raw-only unchanged. Use existing caller-owned JSON buffer conventions, effective configuration anchoring, strict bounds/canonical descriptor hash; test native and bundled/WASM size/offset behavior.
- Implement linear and lookup-table interpolation converters in engine-side adapter/host. Exact rules: finite positive linear scale; for table strictly increasing raw and cp coordinates, explicit zero, sign/ordering preserving, no extrapolation; invalid descriptor -> raw-only diagnostic, not `EVALUATOR_FAILED`. Reject out-of-range/overflow outputs; round deterministically to permitted UCI integer score. Mate separate. Preserve engine search's raw White-positive scores and M5 TT compatibility checks.
- Emit truthful standard UCI `score cp` *only* with validated calibration; keep raw diagnostics and record descriptor provenance. Match UCI side-to-move polarity even when the evaluated side-to-move is Black. Beware that external UCI cp is self-reported, not calibrated by Chess Lab.

### D. Experiment run-control and resource scheduler
- Build GUI-facing scheduler using M6 experiment runner as engine, not a duplicate rules/outcome implementation. M6 `resumeExperiment` loops through all scheduled games today: refactor to expose cooperative boundary hooks and durable mutable run-state outside immutable manifest/schedule/terminal records.
- Pause requests finish currently running game; stop requests finish current game and prevent subsequent attempts; completed results never disappear. No fictitious terminal status for an interrupted attempt; crash recovery uses existing attempt recovery and lock/identity verification. Preserve `createExperiment`, `resumeExperiment`, `experimentStatus`, `exportExperiment` and existing CLI behavior.
- Default exclusive resource lease for experiments. Prompt and record user authorization before competing interactive analysis/game engine processes; bounded children/searches, no hidden engine-option scaling, release leases after all failures/shutdowns. Test stop/pause boundaries and deterministic pairing/counts.

### E. React GUI and launcher
- Build Vite SPA in `gui/` with typed API gateway, controller-owned snapshot projections, four workspaces (Play, Analyze, Experiments, Engines), layout persistence. Wrap `react-chessboard`; handle drag/click, promotions, board flip, legal hinting, move history, analysis branches and PV visualization.
- Play covers human-engine and local human-human. Engine-engine only through experiments. Analyze has start/stop, depth/budget/MultiPV and live completed-depth progress with raw, calibrated cp or mate labels; stale sessions never leak.
- Experiments expose setup, paired runs, progress, categorized outcome counts, pause/resume/stop, per-game review and PGN. Engines expose registration, option discovery, evaluator configuration, calibration status and identity changes.
- FEN/PGN import/export in controller with bounded parser/variation depth, atomic new-document semantics, correct partial-history handling and metadata sidecar as needed.
- Launcher (`chess-lab gui`/shortcut) starts or safely reconnects one workspace/controller, chooses free loopback port, opens default browser. In-app Quit offers terminate or continue; native browser X stays best effort and defaults to controller continuing. No background auto-close while active work exists.
- Accessibility: usable focus/keyboard alternatives for board actions and promotion, textual scores and clock state, non-color-only error messages, predictable dialogs. Persist only useful layout state.

## 3. Acceptance tests and CI gates

Build tests alongside each unit of work; do not defer until final PR:

| Area | Minimum acceptance |
| --- | --- |
| UI unit | Vitest + React Testing Library: board wrapper, promotion, history navigation, variation isolation, failure and score labels, form validation. |
| Protocol | Snapshot/event ordering, WebSocket late/duplicate/gap handling, optimistic concurrency rejection, idempotency across ACK loss/restart where promised, multiple tabs, bad origins/hosts/auth/CSRF, oversized inputs. |
| Clock/game | Monotonic race fixtures; acceptance deadline vs received time; draw claim, resignation, timeout uncertainty, takeback audit, stopped search leakage, paused-clock restoration. |
| Calibration | Backward-compatible old evaluator, optional ABI minor native/WASM, linear/table anchors/interpolation, malformed/nonfinite/overflow, raw-only fallback, mate bypass, sign on White/Black roots, artifact/config/digest changes. |
| Experiments | GUI -> M6 runner; sequential pair identity, progress, boundary pause/resume/stop, technical failures vs chess results, immutable completed records, crash recovery and stale writer-lock rules. |
| Launch/security | Local server bootstrap, repeated launcher, shutdown/continue, malicious browser origin and URL/Host attempts, no secret logging, scoped path operations and subprocess argv. |
| Browser E2E | Playwright real browser/server/native engine: play, analyze/MultiPV, FEN/PGN, experiment, tab refresh, lost controller connection, restart paused clocks, close vs Quit dialog. |
| Regression | Node 24 `npm ci --ignore-scripts` + TypeScript typecheck/lint/tests; native Debug/Release/Profile and CTest; pinned Emscripten/WASM jobs; existing M6 CLI real engine smoke + mock UCI adversarial fixtures. |

Create/extend CI workflows without removing prior gates; record environment and commands in `docs/m7-validation.md`. Pin all dependencies and preserve third-party license notices. End-to-end tests should use isolated temporary workspace directories, engine fixtures and ephemeral ports; no interference with user records.

## 4. Required implementation PR artifacts

- `docs/m7-design-checkpoint.md` with actual preflight commit, API and ABI diagrams/contracts and explicit risk/compatibility checklist.
- New server/workspace/registry/calibration code, `gui/` assets and tests, launcher, relevant README/development documentation, lockfiles and CI jobs.
- `docs/m7-validation.md` with reproducible exact commands, tested platforms, native/WASM status, browser E2E evidence, security/calibration/experiment fixtures and unresolved limitations.
- Implementation PR description mapping each approved D118–D137 to files/tests and highlighting any semantic divergences; a traceability matrix is preferred.
- No implementation push to `main`, no automatic merge. Request review after CI reports.

## 5. Mandatory stop conditions

Stop and report rather than silently inventing behavior if:
- The new evaluator metadata cannot be negotiated without invalidating M2 v1 API layout/native+WASM compatibility.
- Interactive experiment pause would silently rewrite old immutable attempt/manifest records or misclassify an unfinished game as completed.
- Idempotent command replay/reconnection can result in duplicate moves, game overwrite, or falsely acknowledged persistence.
- Authentication/session bootstrap cannot resist malicious-webpage requests to a loopback controller.
- The resource scheduler must change engine search results, TT/evaluator error semantics, clock deadlines, or M6 CLI behavior.
- An unapproved new backend, OS sandbox, cloud service, Electron packaging or M8 feature appears necessary.

**This kickoff is a plan for Codex, not permission to start until the user-reviewed documentation is merged and implementation is expressly authorized.**
