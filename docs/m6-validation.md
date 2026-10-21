# M6 implementation and validation

Base `2cfb47bebba9af7bf7a617ec63550b7e8769a73f`; branch
`m6/headless-controller`. The approved kickoff and D84–D117 specification are
unchanged. `m6-design-checkpoint.md` was committed as `d07235e` before implementation.
No M1–M5 headers, implementation semantics, ABI, or baseline tests were changed.
This delivery ends at an unmerged implementation PR. M7/M8 remain deferred.

## Reproduce

See `controller/README.md` for exact build/install/analyze/match/status/resume/export
commands. `controller/examples/configure.mjs` generates an executable-hash-bound
sample configuration from the current build; no hard-coded trusted digest.
The sample plays both colors against the ordinary UCI fixture in `test/mock.mjs`.
Its FEN is near the automatic 75-move boundary, so the complete games are short;
this is an integration smoke, not a playing-strength experiment.

Local environment: Linux 6.18.55 x86_64, GCC 14.2.0, CMake 3.31.6, Ninja
1.11.1.git.kitware.jobserver-1, Node v24.19.0, npm 11.9.0. CI pins Node 24.10.0.
Dependencies: chess.js 1.4.0 BSD-2-Clause; TypeScript 5.9.3; @types/node 24.10.0;
lockfile integrity and license inventory are committed. No runtime network needed.

```sh
cmake --preset native-debug && cmake --build --preset native-debug
ctest --preset native-debug
cmake --preset native-release && cmake --build --preset native-release
ctest --preset native-release
cmake --preset native-profile && cmake --build --preset native-profile
ctest --preset native-profile
cd controller
npm ci --ignore-scripts
npm run typecheck
npm run lint
npm test
```

The managed workspace requires `/workspace/.cloud-toolchain/bin` on PATH.
Local subprocess tests needed the execution tool's network permission because its
restricted sandbox interfered with Node subprocess pipes; the suite itself uses
local processes and files only. Initial restricted runs failed and were rerun with
working subprocess access. A differential test found raw EP loss in chess.js;
controller-owned raw EP preservation fixed the failure, with regression coverage.

## Native protocol and ownership

`engine/src/uci/main.cpp` is a separate native executable; the default native
material evaluator must be packaged beside it (or supplied as the first argv).
CMake introduces no threading into WASM/core/search libraries. One worker owns
active evaluator/table traversal; command changes require idle, cancelled workers
are joined before reuse, and isready remains responsive. The synchronous search
returns before failed-search table clearing; its active-table guard has released.
A fatal search clears its attached table and exits status 2 without bestmove.

| Input | Implemented behavior |
| --- | --- |
| uci, isready | Real supported option declarations, uciok/readyok; ready during search |
| position startpos moves | Complete replay history, transactional legality validation |
| position fen six fields moves | Incomplete history, full counters/raw EP preserved |
| ucinewgame | Idle barrier, retains compatible M5 caches |
| setoption | Idle-only Hash 0..256 MiB, Clear Hash, MultiPV 1..256, Evaluator, EvaluatorConfig, NumericTT, ValuePolicy |
| go depth | 1..64, final after specified completed depth |
| go movetime | 0..2147483647 ms, monotonic local cancellation |
| go wtime/btime/winc/binc | Both remaining clocks required; min(remaining, remaining/30 + increment/2) |
| go infinite | Holds final at terminal/depth-64 ceiling until stop |
| stop, quit, EOF | Cooperative cancellation and join; repeated stop harmless |
| ponder/searchmoves/nodes/mate/Chess960 or mixed go modes | Explicit rejection; no silently substituted semantics |

Finite M2 values are tagged raw **White-positive** diagnostics; no centipawn
conversion. Completed info depth/nodes/time/PV and displayed MultiPV only.
Mate scores use UCI side-to-move perspective and ceil(plies/2), including terminal
mate -0. No invented seldepth/NPS. Internal exact MultiPV=256 preserves strongest
PLAY, with a runtime root-candidate capacity check. A zero-budget/pre-depth-one
fallback is canonical, legal and explicitly unevaluated. Terminal gives 0000.

For trusted Chess Lab instances the final adjacent envelope is exactly:

```text
info string chesslab decision claim_draw
bestmove <strongest-evaluated-PLAY>
```

or `info string chesslab decision claim_draw_with_intended_move <uci>` immediately
before bestmove. An intended claim move is never played. Tests include intended
claim e1d1 with distinct strongest evaluated PLAY e1f1 at displayed MultiPV=1.
Provisional progress never survives a later evaluator/search failure as an action.

## Controller rules, sessions and clocks

`src/rules.ts` owns FIDE adjudication behind Rules, independently of engine results.
Legal chess.js moves support the board; no isGameOver/isDraw/isInsufficientMaterial
shortcut controls outcomes. Full FEN preserves raw EP independently; repetition
uses legally capturable EP, pieces, side and rights. Initial FEN, actual moves and
completeness reconstruct all evidence. Missing earlier history is not proof of no
earlier repetition. Live replay projections are checked at acceptance boundaries.
Mate/stalemate precede baseline certified dead cases, fivefold and 150 halfmoves;
100-halfmove/observed-threefold immediate and intended claims are optional.

`m6_rules_probe` cross-checks full FEN, legal moves, normalized signatures,
occurrences, claims, intended claims, dead cases, terminal precedence and invalid
FENs against real ChessCore, including pinned EP, castling, promotion, complete and
incomplete repetitions, 99/100/150 halfmoves and deterministic generated replay.
Dead-position recognition is the baseline's sufficient material cases, not a
universal reachability solver for every blocked legal position.

`src/engine.ts` implements EngineTransport with bounded 64 KiB lines, 1 MiB
handshake output, 8 MiB search output, bounded stderr/transcript, strict protocol
states and argv spawn with shell:false. Every active result carries a local
session, process generation and game/root/history/side binding. One go at a time;
bestmove consumes exactly one session and a readyok barrier drains queued finals
before reuse. Duplicate/unsolicited/malformed finals poison the process. Shutdown sends quit with a bounded kill deadline. Stop
expires to kill; new activity requires a fresh explicit instance. Explicit kind
and matching configured executable SHA-256 authorize Chess Lab claims; name alone
never does. Conflicting/nonadjacent trusted extensions fail. Ordinary engine claim
spoofs are ignored and bestmove remains solely a PLAY proposal.

`src/game.ts` uses one injectable monotonic clock for receipts/deadlines. Legal
accepted receipt must be strictly before expiration; later validation charges
through that receipt instant. Invalid proposals never retroactively stop clocks.
Increment follows only accepted PLAY; claims consume time with no increment/move.
Stop grace is not playing-time subsidy. Analysis does not accept moves or spend a
player clock. Interactive failures pause; match failures default to separately
labeled technical forfeits or configured invalid attempts. Technical results use
PGN `*`, never fabricated checkmate/draw. Chess-only and forfeit standings differ.

Timeout proofs return CAN_MATE only with replayable cooperative mate witnesses;
CANNOT_MATE only for a bare mating-side king or verified dead material classes
(including all bishops on the same square color). Search is bounded at 3 plies /
1000 nodes. Exhaustion, KNN/K, opposing minors, self-blocking pieces and promotions
remain UNDETERMINED unless a witness/proof applies. Unknown flags produce
ADJUDICATION_REQUIRED, stop the automated run, preserve incomplete pairs, and never
create a chess win/draw. Tests cover proof-based timeout wins, draws and unknowns.

## Filesystem and experiments

`src/experiment.ts`, `src/store.ts`, and thin `src/cli.ts` implement deterministic
seeded color-swapped pairs, sequential execution, immutable versioned manifests/
schedules and stable game/attempt IDs. Executable, evaluator and argv-file hashes,
UCI identity/effective options/M2 diagnostics, initial history, search/clock/result
policies, seed, runtime/compiler/platform/controller revision are recorded. Missing
third-party metadata is unknown. Raw evaluator units are not compared across
providers; reproducible conditions do not imply identical wall-clock outcomes.

Every JSON record is `{schema:1,digest:<SHA256 canonical data>,data:<payload>}`.
`manifest.json`/`schedule.json`, `attempts/attempt-UUID/start.json`, finalized
`final.json` and official `games/game-ID.json` references are immutable. State and
summary are atomically replaceable projections. Temporary files are fsynced;
immutable publication uses exclusive hard links, projections use same-directory
rename, then directory fsync. Torn/integrity failures cannot count as results.

Writer locks have host/PID/token, exclusive creation and an explicit stale takeover
with a takeover guard and liveness check. Live/remote/invalid ownership is refused.
Unfinished attempts acquire immutable interrupted metadata; resume creates fresh
attempts from original openings/clocks/processes. Completed games do not rerun.
A crash after final publication but before official reference recovers that final
exactly once. Conflicting finals fail closed. PGN uses actually played SAN with
FEN/SetUp for custom starts; JSON sidecar retains claims, clocks, failures/proofs.
Explicit maxPlies work exhaustion is invalid, not a draw.

Durability guarantees are validated on Linux local filesystems. They do not assert
Windows, macOS, network-filesystem, browser or mobile correctness. An interrupted
initial manifest creation or crashed stale-lock takeover guard fails closed and
requires inspecting the directory; there is no automatic destructive repair.

## Measured evidence and remaining gates

- Native Debug/Release/Profile: each 9/9 CTests passed, preserving all M0–M5 suites
  and adding real UCI integration. Actual logs are in `m6-evidence/`.
- Typecheck/lint passed. Controller integration suite includes adversarial engines,
  native transcripts/failure/delayed evaluator, rules differential, virtual clocks,
  proof adjudication, paired real-native/mock CLI, immutable records, torn writes,
  locks, interrupted attempts and idempotent recovery. Test log/count recorded below.
- Actual sample analysis, complete paired match and repeated resume:
  `m6-evidence/sample-transcript.txt`. Exact runnable config generator is committed.
- Local pinned Emscripten install was attempted but proxy policy denied
  `storage.googleapis.com` with HTTP 403. No local WASM pass is claimed.
  The existing pinned Emscripten 4.0.10/SDK Node workflow is semantically unchanged.
- CI run links and observed final conclusions are appended after the implementation
  branch is pushed. No CI success is inferred from configuration or queued jobs.
- No external Stockfish download/smoke, Windows/macOS, real browser or mobile run
  was performed. General timeout/dead-position reachability is explicitly not
  claimed; uncertified timeout cases remain unresolved as required by D110.

Local final controller result: **42/42 tests passed** in 37.266 s;
`m6-evidence/controller-tests.txt`. `npm audit --omit=dev` reported zero known
runtime dependency vulnerabilities (`m6-evidence/runtime-dependency-audit.json`).
Native final CTest totals: Debug 48.32 s, Release 2.94 s, Profile 2.23 s; each 9/9.
These timings include concurrent local work and are evidence, not benchmarks.
