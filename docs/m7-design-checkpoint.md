# M7 implementation design checkpoint

Preflight main: `b057f5b89dacf6b812e61ccf2e344edfe86e7824` (merged PR #13).
Working branch: existing `m7/desktop-gui`, initial head
`480b898` including two user-authorized documentation status corrections.
Main is an ancestor; these corrections are retained as requested. Approved M7,
M6, evaluator ABI, actual controller/host/UCI, fixtures and workflows inspected.
No implementation changes precede this checkpoint.

## Ownership and order

A: `controller/src/workspace/protocol.ts` shared versioned JSON types/validation;
`workspace/service.ts` authoritative serialized state and durable command outcomes;
`server/` loopback HTTP/WS/session adapter. B: durable casual games and independent
analysis trees using M6 Game/FideRules, clock checkpoints and auditable takeback.
C: registry and optional evaluator calibration. D: runner boundary hooks plus leases
and external run-control sidecar. E: React/Vite GUI and local launcher.
Dependency arrows: GUI -> JSON gateway -> workspace -> M6 library -> UCI processes.
C++ core/search never import transport/UI; CLI lazily loads GUI launcher only for
`gui`. No Electron, backend service, mobile, new search or rules replacement.

## Typed protocol, durable state and reconnect

`/api/v1`: command `{version:1,commandId,operation,objectId?,expectedRevision,
expectedGeneration?,payload}`; ACK `{version:1,commandId,accepted,revision,result? ,
error?:{code,message,severity}}`. IDs are bounded safe strings. All mutations
serialize globally (simple multi-tab total order), validate expected workspace
revision and game generation, clone state, apply, integrity-write/fsync atomic
checkpoint, then ACK/broadcast. Same ID/content returns stored outcome; changed
payload conflicts. Accepted command records and rejected definitive outcomes are
retained for the workspace lifetime, capped at 10000; reaching capacity rejects
new commands instead of evicting IDs and allowing duplicate actions. No infinite
retention claim. Runtime requests ACK as queued, not falsely as completed work.

Snapshot includes schema/version, workspace ID/revision, games, active selection,
analysis documents/nodes, engine profiles/config generations, experiments, jobs,
layout/settings and bounded structured diagnostics. State events carry revision
and full authoritative projection; progress has separate session/root/generation/
sequence/provisional fields. Subscribe sends a snapshot immediately; GUI accepts
only newer committed revisions, detects gaps and fetches snapshot; HTTP ACKs never
optimistically mutate board state. Restart rotates credentials, restores clocks
paused, terminates volatile job sessions and requires explicit restart. No search
resumes merely because an old start command was acknowledged.

Workspace `state.json` uses M6 checksummed schema envelope, atomic replace/fsync,
exclusive writer lock and explicit stale-lock takeover. Casual snapshots store
record/time/outcome/remaining clocks, generation, assignments, audit and clock
history. Every move commits before ACK. Takeback retains removed moves/audit,
restores recorded clock checkpoint, pauses and increments generation. Analysis
nodes are separate parent/move/comment/NAG records; official history immutable to
analysis. Bounded document/journal sizes; unknown schema/corruption fails closed.
Import creates new document atomically; no implicit overwrite. PGN parser bounded
by bytes/tokens/variation depth, reconstructs SAN with legal rules and separate
variations/comments; arbitrary FEN history stays incomplete.

## Security and launcher

Bind 127.0.0.1 ephemeral port; exact numeric Host and same origin only. No wildcard
CORS. Static serving restricted to built GUI directory; CSP, no token logging.
Launcher publishes owner-only runtime locator with port/PID (no secrets) and
uses a protected local launch channel/one-time bootstrap capability to reopen.
Browser bootstrap secret only in fragment, immediately removed with history API;
POST exchange requires same Origin/Host and JSON. Session cookie is HttpOnly,
SameSite=Strict; CSRF token returned in memory, required on mutations. WS requires
cookie, exact Origin/Host and versioned subprotocol; no state-changing WS RPC.
Restart rotates credentials. Launch/bootstrap credentials never enter workspace
records, query logs or durable diagnostics. Request/WS size and rate limits.
Filesystem operations are workspace-ID/scoped exports, bounded browser upload/
download; native absolute executable registration requires explicit trust consent,
sha validation, shell:false and recheck before launch. No generic path RPC.
Launcher singleton uses writer lock plus runtime discovery; OS browser opening
uses fixed platform command/argv. Browser X leaves server alive. Quit continue
keeps running; terminate cancels interactive searches, checkpoints paused games,
requests experiment boundary stop then closes processes/server and releases lock.

## Calibration compatibility proof and presentation

Keep public legacy `ChessEvalApi` exactly 72 bytes, offsets 0..64 unchanged.
Define minor-1 extension table as that prefix plus align-8 instance JSON function
at offset 72, total 80 bytes on native and wasm32 (pointer slot padded to 8).
Host zero-initializes 80-byte table, requests (1,1); accepts an old provider's
72-byte/minor-0 response without reading tail. Incompatible-version response
retries (1,0) with exactly 72 bytes. Minor-1 providers must answer old callers with
72-byte/minor-0 prefix, never overrun capacity. Existing layout tests remain valid;
new C/C++/WASM asserts prove extension layout and old-provider fixtures.

Optional instance JSON fetched only after effective config validation, bounded
64 KiB / 1024 points. Strict schema_version=1 target=centipawns; linear finite
scale>0; table increasing raw/cp, explicit zero, sign-preserving, linear segments,
raw_only outside domain. Canonical JSON SHA bound to artifact/effective config in
presentation provenance. Invalid/unknown/missing descriptor returns raw-only
with diagnostic, never quarantines valid raw evaluator. No dynamic display code.
Conversion uses checked finite arithmetic, no extrapolation, signed UCI integer
range +/-2147483647, rounding half away from zero then side-to-move polarity.
Mate bypasses mapping. Search/TT still consume raw scores; v1.0 fingerprints and
numeric compatibility certification are unchanged. Native UCI emits calibrated cp
only with valid descriptor, retaining raw diagnostics and provenance. Default
material provider remains raw-only; explicit calibration fixture proves behavior.
New experiment identity/sidecar may capture calibration without rewriting old
manifest or completed results.

## Registry, experiments and scheduler

Profiles pin executable/argv/evaluator artifact hashes, reported/effective options,
trust kind and configuration generation; profile edits affect future sessions only.
Every launch rechecks artifacts; identity change needs explicit re-registration.
Option discovery is a bounded managed process, subject to resource lease.

M6 resume gains optional boundary callbacks and interruption signal; default CLI
behavior unchanged. Pause/stop sidecar is separate from immutable manifest and
attempts. Boundary request finishes current game then prevents next attempt;
status distinguishes requested/paused/stopped/completed/failed. Shutdown waits for
boundary unless explicit process interruption records an interrupted attempt with
no fake final. One experiment lease, bounded interactive leases; default conflict
rejects with actionable pause-or-authorize choice. Explicit competition consent is
recorded in workspace and experiment execution sidecar, never silently changes
Hash/Threads or conditions. Release leases on completion/failure/shutdown.

## UI, dependencies and verification

Pin React/React DOM 19.2.0 (MIT), Vite 7.3.7 (MIT), react-chessboard 5.12.1 (MIT),
TypeScript 5.9.3 (Apache-2.0), ws 8.18.3 (MIT), Vitest 4.0.18 (MIT), React Testing
Library 16.3.0 (MIT), Playwright 1.64.1 (Apache-2.0); lockfiles and notices.
Four workspaces; board adapter with drag/click, keyboard coordinate form, explicit
promotion dialog, controller legal hints, flip, history cursor, variation/PV view.
Resizable/collapsible persisted panels. Scores label raw/calibrated/engine-reported
cp/mate and provisional status. Structured errors, disconnect state, accessible
labels/focus and trust/quit dialogs. No UI chess outcome authority.

Tests in dependency order: durable idempotency/lost ACK/restart/two tabs; hostile
Host/Origin/CSRF/WS/oversize/static traversal; clocks/pause/takeback/stale sessions;
registry changes and resource contention; native/WASM legacy+extended calibration,
rounding/table bounds/polarity/failure isolation; M6 boundary pause/stop/recovery;
Vitest/RTL board/promotion/score/history/forms; Playwright real Chromium+server+
native engine for four workspaces/import/export/reconnect/paused restart/quit.
Local Chromium available at /usr/bin/chromium; CI installs pinned Playwright
browser. Preserve native Debug/Release/Profile, all M0–M6 Node and pinned WASM gates.
Validation records actual commands and failures, decision/file/test traceability.
Any incompatible ABI, weakened persistence/auth/chess guarantee or additional
feature requirement stops for user architecture review. No such conflict found
at checkpoint. Finish at an unmerged implementation PR for review.
