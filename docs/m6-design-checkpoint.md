# M6 design checkpoint

Base: `2cfb47bebba9af7bf7a617ec63550b7e8769a73f` (approved M6 documents merged in PR #11).
Checkpoint recorded before implementation. Git transport's configured proxy was
unreachable; the exact missing Git objects were recovered through the connected
GitHub API and verified against their original SHA-1 identities. No base changes.

## Native boundary

The native-only executable owns the evaluator selection and M5 table. One worker
owns their active use; the command thread owns replacement after cancellation and
join. `isready` uses only the output mutex and never joins. A fresh cancellation
source and detached root/history belong to each search. A condition-variable
timer requests cancellation for local time budgets. Output blocks serialize
committed info and the final adjacent extension/bestmove envelope. Search failures
clear the attached table after return, mark the process unhealthy and exit without
bestmove. EOF/quit stop and join. Existing core/search/evaluator APIs stay intact.

Commands: uci/isready, idle-only setoption/ucinewgame/position, go depth 1..64,
infinite, movetime and wtime/btime/winc/binc, stop/quit. Unsupported semantic go
tokens (ponder/searchmoves/nodes/mate etc.) reject the entire command. Position
and option changes are transactional. Hash, Clear Hash, MultiPV, Evaluator,
EvaluatorConfig, NumericTT and ValuePolicy are advertised with bounded validation.
The default evaluator is the native material module beside the executable.

Clock allocation: max(0, min(remaining, remaining/30 + increment/2)), bounded
to a signed 32-bit millisecond input range. The controller independently owns
game deadlines. Infinite retains the result at depth 64 until stop; finite depth
finishes at its depth. Timed modes may finish on terminal or depth cap.

Request 256 exact root rankings internally, check legal root count plus claim
fits that limit at runtime, and select the first PLAY in ranked order. Display
only configured MultiPV. Claims announce an unplayed move; the extension directly
precedes bestmove. Before depth one use the canonical legal fallback and explicitly
mark it unevaluated. Terminal gives 0000. Finite scores remain White-positive raw
tagged diagnostics, never cp. Mate scores convert winner and ply distance to UCI
side-to-move moves. No provisional info can authorize execution after failure.

## Controller and rules

Node 24, TypeScript 5.9.3 and chess.js 1.4.0 (BSD-2-Clause), exact lockfile.
An engine transport owns STARTING/HANDSHAKE/READY/SEARCHING/STOPPING/FAILED/CLOSED,
bounded lines/stderr, timeout waits, shell:false argv and disposal. Only one active
session. Completion consumes exactly one bestmove; duplicate/unsolicited finals
poison the process. A readyok barrier after completion drains queued messages before
another search. An unresolved stop kills that process. Generations/session/root
tokens stay local. Extension trust requires explicit Chess Lab kind and configured
matching executable SHA-256; self-reported name never grants trust. A recognized
final extension must be unique and directly adjacent to bestmove.

The transport captures monotonic receipt time before parsing. Game validation
checks session/root/generation and legality before charging through that timestamp;
receipt at deadline is late. Invalid candidates never stop the clock. Claims add
no increment and no move. Legal accepted moves debit elapsed then add increment;
opponent starts after validation. Stop grace is separate from remaining game time.
Analysis returns proposals without changing any game.

Rules wraps chess.js board/legal operations only. Preserve raw EP in replay FEN,
normalize repetition EP by actual legal en-passant captures (including pinned EP).
Track initial FEN, actually played UCI moves, explicit completeness and signatures.
Rebuild at acceptance boundaries. Mate/stalemate precede independently implemented
known dead cases, fivefold and 150 halfmoves. Claims use observed counts >=3 or
100 halfmoves, optionally after an intended legal move, without committing it.
Incomplete evidence can establish thresholds but cannot establish their absence.
Cross-check legal moves, full FEN, signatures, dead cases and claims with a native
line-oriented ChessCore probe, not JS bindings.

Timeout proof coverage: CANNOT_MATE for bare king, and certified global dead
material classes (bare kings; lone bishop/knight versus bare king; bishops only
all on one square color). CAN_MATE requires an actual legal cooperative sequence
ending with checkmate by the specified side, discovered by bounded exhaustive
search and returned as a replayable witness. Search exhaustion of the configured
bound is UNDETERMINED, never an impossibility proof. KNN/K, opposing minors,
self-blocking pieces, promotions and longer cooperative mates remain unknown
unless a witness is found. Unknown produces ADJUDICATION_REQUIRED and stops an
automated run with separately recorded disposition. No material heuristic proves
possibility. This conservative coverage is permitted by D110, not a rule change.

## API, persistence and verification

Typed async modules expose Rules, Game, managed engine, analyze, sequential paired
experiments, status/resume/export. Thin CLI validates JSON and delegates. Observable
progress is provisional; accepted decisions come only from final proposals.
Manifest/schedule freeze seeded color pairs, conditions, executable/config hashes,
reported effective options, runtime and revision. Unknown third-party M2 data is
explicit. Chess, technical forfeit, invalid and unresolved accounting are separate.

Storage uses versioned canonical JSON envelopes with SHA-256 integrity, fsync and
same-directory atomic rename; exclusive immutable final publication and a writer
lock. Stable game/attempt IDs, immutable manifest/schedule/finals and replaceable
summary. Interrupted attempts remain recorded; explicit stale-lock takeover checks
owner liveness. Resume verifies identities/config and original schedule, restarts
unfinished games with fresh engines/clocks from original FEN, never reruns completed
games. PGN contains only played SAN and genuine chess results; sidecar carries
claims, technical events and unresolved adjudication.

Gates: real UCI pipe transcripts and evaluator failure fixture; adversarial Node
engine fixtures for fragmentation, huge lines, duplicate/late output, spoofing,
crash/handshake/stop hangs; ChessCore differential; virtual-clock receipt boundary
and increment tests; paired real-native/mock CLI match; durable corruption/lock/
interruption/idempotent resume checks. Preserve native Debug/Release/Profile and
pinned Emscripten 4.0.10 tests; add Linux Node typecheck/lint/tests/integration CI.
Validation documentation will distinguish actual local runs, observed CI and
unverified platforms. No M7/M8 or implementation merge.
