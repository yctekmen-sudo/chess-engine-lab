# M6 headless controller

Node 24, TypeScript library and CLI; local UCI subprocesses, local records, no server.
Build native binaries from the repository root, then install pinned dependencies:

```sh
cmake --preset native-debug
cmake --build --preset native-debug
cd controller
npm ci --ignore-scripts
npm run build
npm run typecheck
npm run lint
npm test
```

From the repository root, generate a runnable sample with explicit trusted native
executable hash, then analyze and run a complete color-swapped pair:

```sh
mkdir -p work
node controller/examples/configure.mjs > work/m6-sample.json
node controller/dist/cli.js analyze work/m6-sample.json
node controller/dist/cli.js match work/m6-sample.json work/m6-batch
node controller/dist/cli.js experiment status work/m6-batch
node controller/dist/cli.js experiment resume work/m6-batch
node controller/dist/cli.js experiment export work/m6-batch work/m6-export
```

JSON stdout; actionable JSON errors on stderr and nonzero exit. `match` creates a
new immutable experiment; `resume` is explicit and never reruns completed games.
Use `experiment resume DIRECTORY --takeover-stale-lock` only for a verifiably dead
same-host owner. Existing/live/corrupt/remote-host locks fail closed. Linux fsync,
hard-link atomic publication and local filesystem semantics are required for the
validated durability guarantees. No database or implicit process restart.

`src/index.ts` exports `Rules`/`FideRules`, `Game`, `EngineTransport`,
`ManagedEngine`, `analyze`, config/schedule/experiment/status/resume/export APIs.
`Game` and `Rules` do not depend on Node process transport. All `info` progress is
provisional. Only final session/root-bound proposals can be accepted by `Game`;
analysis does not accept them. `ManagedEngine.stop()` resolves the old search or
kills its process after grace; it never reassigns its stream to a new search.
`close()` must be called in `finally`. Engine failures pause interactive games;
`Game.fail(reason, policy)` explicitly records automated technical/invalid results.

Fischer time is `{kind:"fischer",initialMs,incrementMs}`; untimed is
`{kind:"untimed"}`. Match conditions distinguish `limit:{kind:"depth",depth}`
from `limit:{kind:"movetime",ms}`. `searchCapMs` limits measured search work
independently of the game deadline; its stop grace never extends playing time.
`maxPlies` is an explicit work cap producing an invalid attempt, never a draw.

Timeout feasibility is conservative: bare king and proven dead material classes
can prove impossibility; legal cooperative mate witnesses prove possibility.
Bounded proof search (3 plies / 1000 nodes) can return `ADJUDICATION_REQUIRED`.
That stops an automated run as an unresolved disposition, with no fabricated
chess result. It is not a general mate-possibility solver.

Full protocol, rule coverage, persistence schema, evidence and limits:
[`docs/m6-validation.md`](../docs/m6-validation.md).

## PR #12 review corrections

Startup failures are charged to the actual failing engine/color, independent of
whose turn the opening specifies. Technical-forfeit standings credit its opponent;
the invalid-game policy records the same attribution without points. Neutral
orchestration/identity failures are invalid attempts with no loser and no points.
`Game.fail(reason, policy, loser)` accepts explicit attribution; `Game.invalidate`
records neutral failures. Completed chess outcomes remain immutable.

CLI `analyze` uses the config's `searchCapMs` (default 60000 ms) as a finite search
watchdog, including fixed-depth analysis. For movetime the earlier of that cap and
the requested movetime sends stop. A cooperative engine may return its legal final
proposal; an engine that ignores stop fails after `stopGraceMs` and is killed.
Handshake/readiness deadlines are separately bounded. This policy does not imply
that the requested depth completed: retained `info` remains provisional and an
unevaluated fallback stays labeled by the native engine.

The library supports externally cancelled infinite analysis:

```ts
const cancellation = new AbortController();
const pending = analyze(engine, record, {kind: 'infinite'}, {
  signal: cancellation.signal,
  onProgress: progress => console.log(progress),
});
// The caller decides when to stop, e.g. an application's cancel action:
cancellation.abort();
await pending; // rejects with name === 'AbortError'; subprocess is disposed
```

Infinite `analyze` requires an AbortSignal and has no automatic finite watchdog.
Cancellation sends stop and uses the same bounded grace/kill path. An already
aborted signal does not launch an engine. The CLI intentionally exposes finite
analysis; infinite activity is an explicitly managed library operation.

For ordinary UCI live clock play, choose `limit: {kind: 'clock'}` together with
Fischer `time`. Each search sends `go wtime W btime B winc I binc I`, taking W/B
from the controller's live clock projection, flooring to milliseconds and clamping
to the adapter's 0..2147483647 range. Both increments are transmitted. The
controller still stops at the earlier of its authoritative game deadline and
`searchCapMs`; engine time allocation and stop grace cannot extend playing time.
Fixed-depth and movetime comparison modes remain explicit alternatives. Clock
mode is rejected for untimed matches and for the CLI's single-position analysis.
Direct `ManagedEngine` clock requests require an explicit `stopAfterMs` budget.

## M7.1 independent experiment limits and diagnostics

The shared `limit` remains required and keeps its existing meaning. Optionally
add `engineLimits: [limitForA, limitForB]` to override it for paired experiments.
Index 0 is `engines[0]` (Engine A); index 1 is `engines[1]` (Engine B), regardless
of which engine plays White. Both elements are required when the array is present.
Depth is an integer 1..64; movetime is an integer 0..2147483647 milliseconds.
Depth/movetime may be mixed. `{ "kind": "clock" }` may also be mixed with either,
and requires Fischer `time`. The shared limit must still be valid. Fischer clocks,
authoritative deadlines, `searchCapMs`, stop grace and adjudication are unchanged.
CLI `analyze` continues to use the shared limit; `engineLimits` applies to matches.

For example, Chess Lab depth 8 versus Stockfish depth 3 on Windows:

```json
{
  "engines": [
    { "path": "C:\\cursor\\chess-engine-lab\\build\\native-debug\\chess_lab_uci.exe" },
    { "path": "C:\\engines\\stockfish\\stockfish-windows-x86-64-avx2.exe" }
  ],
  "seed": 1,
  "limit": { "kind": "depth", "depth": 1 },
  "engineLimits": [
    { "kind": "depth", "depth": 8 },
    { "kind": "depth", "depth": 3 }
  ],
  "time": { "kind": "untimed" },
  "searchCapMs": 60000,
  "maxPlies": 300,
  "uciDiagnostics": { "maxEntries": 4096, "maxBytes": 1048576 }
}
```

Replace paths with installed absolute executables (including `.exe` on Windows).
This example uses ordinary UCI trust. For Chess Lab draw extensions, set Engine A's
`kind` to `chesslab` and supply its actual executable `sha256`, as in the existing
sample generator. Stockfish's filename depends on the installed build.
Save as `work/depth-comparison.json`, then run
`node controller/dist/cli.js match work/depth-comparison.json work/depth-comparison`
and export with
`node controller/dist/cli.js experiment export work/depth-comparison work/depth-comparison-export`.
Depth 8 is a request: the watchdog may interrupt before that depth completes.

Omit `uciDiagnostics` to disable protocol capture (the default); `{}` enables
the defaults above. Limits apply **per attempt across both engine processes**:
1..65536 entries, 1..8388608 bytes. The byte budget counts serialized entries
including attribution. Capture retains an ordered prefix; it never evicts older
commands to imply an uninterrupted trace. `entries`, `observedEntries`,
`droppedEntries`, `retainedBytes` and `truncated` describe its coverage.
`lifecycleComplete` is false for checkpoints, failed/forced process shutdowns,
unterminated output or an observer failure. Observer exceptions cannot fail normal
chess operations, but their sticky `observerFailures` count prevents the engine
and exported capture from claiming complete evidence, even if later observation
succeeds. `complete` requires lifecycle completion, no truncation and zero
observer failures.
Captured traffic consists of submitted stdin commands and complete stdout lines
from the attempt's handshake through quit, including options, position, go, stop,
readiness and responses. Preflight identity inspections and stderr are outside
this trace. An attempted write is not proof the engine consumed that command.
Entries carry observation sequence, monotonic timestamp, process generation,
scheduled game, attempt, engine index and color. Search traffic additionally
carries session and absolute replay ply; setup and idle shutdown have no search attribution.
These are controller bindings under the existing UCI stream barriers, not IDs
echoed by the engine. Ordering records controller observations across processes.

Each new attempt's `searches` records per-move `requested`, engine/color/ply/session,
`reported` depth/nodes/timeMs/nps, `measuredElapsedMs`, `stopRequested`, `status`
and whether the proposal was `accepted`. Requested depth and engine-reported depth
are separate. Measurement runs from search submission through bestmove receipt
(or failure/disposal), excluding the subsequent readiness barrier. Counters are
the latest reported values per field; malformed/negative/unsafe integer values
make that field unavailable. Omitted fields remain absent until reported;
zero is retained only when actually reported. `info string` and PV payloads are
not parsed as counters. Four recent standard info lines of at most 512 characters
are retained per search; `infoDropped` and each line's `truncated` flag label loss.
Fuller raw evidence is available when protocol capture is enabled and fits its limits.

Per-move search detail is also bounded **per attempt across both engines**, even
with protocol capture disabled. By default `searches` retains an ordered prefix
of at most 256 records and 262144 serialized entry bytes. Optional JSON
`searchEvidence: { "maxEntries": 256, "maxBytes": 262144 }` selects budgets
(1..4096 records, 1..4194304 bytes). This does not change `maxPlies`, engine limits,
clocks or game records. Once either budget fills, no subsequent raw search detail
is retained or serialized into the collector; per-engine aggregate counters,
elapsed totals and reported-field sample counts/ranges continue across **every
observed search**, including omitted records. These aggregates do not reconstruct
the missing per-move detail or raw UCI lines.

Each checkpoint/final includes `searchEvidence`: budgets, `retainedBytes`,
`retainedSearches`, `observedSearches`, `droppedSearches`, `truncated`, first/last
dropped session/engine/color/ply, and the aggregate `summary`. `complete` describes
per-search record coverage and requires a finalized attempt with zero omitted
records; checkpoints always have `lifecycleComplete: false` and `complete: false`.
Raw info loss remains separately labeled within retained records. Exported
`searchSummary` uses the aggregates for all observed searches and additionally
reports retained/dropped counts and attempts with truncated search detail.
Earlier M7.1 array-only attempts remain readable and are summarized from their
existing records; M6 attempts without statistics remain explicitly unavailable.

Retained evidence, aggregate state and session-to-ply bookkeeping use bounded
memory independent of `maxPlies`. Checkpoints still rewrite the bounded prefix
plus fixed-size summaries, so added evidence serialization is linear in the
number of searches after saturation, rather than repeatedly growing with the
whole search history. The authoritative move/event history still grows normally;
the evidence budget does not truncate it or make total game storage constant.

`completed` means bestmove plus a successful session barrier, `interrupted` means
a controller stop/disposal, and `failed` means a transport/protocol failure. A
completed exchange does not certify reaching the requested depth, and an
interrupted search can still yield a legally accepted move. Engine-reported time
is distinct from measured elapsed time and authoritative playing time. Statistics
and scores are not normalized or certified comparable between engines.

Evidence shares the existing checksummed, fsynced incremental state and immutable
final attempt records; no separate logging write can acknowledge a game early.
After a crash, only the latest durable move checkpoint is available; active-search
and shutdown traffic since that checkpoint may be lost. Export adds
`incompleteAttempts` (unfinished or explicitly interrupted, always labeled partial)
and per-engine `searchSummary` sample counts/ranges and elapsed totals for official
attempts. It retains the existing `manifest`, `scheduled`, `attempts` and PGN.
Old manifests/results resume and export without fabricated historical statistics;
summary coverage counts distinguish attempts lacking search evidence. Corrupt
evidence fails integrity checks. Resume still verifies identities/artifacts and
never rewrites completed attempts, manifests or schedules.

## M7.2 single-PV final scores and controller claims

Chess Lab's default `MultiPV=1` now searches the strongest PLAY at the actual
root, without competing with an optional root draw claim. Hypothetical descendant
claims and automatic draws retain their normal search semantics. `MultiPV>1`
keeps exact legacy root ranking, including legacy claim recommendations.

For single-PV completed depths, the native adapter emits exactly one final envelope
immediately before the corresponding `bestmove`, in one serialized output block:

```text
info string chesslab final_score v1 search 1 depth 3 status completed bestmove e2e4 score finite 0
bestmove e2e4
```

The `search` counter starts at 1 per process and advances for every accepted `go`,
including searches with unevaluated fallbacks. `depth` is the last fully completed
depth (1..64); `status` is `completed` or `cancelled`. Score is `finite VALUE`
(finite White-perspective evaluator units), `white_mate PLIES` or `black_mate PLIES`
(positive root-relative mate distance, at most completed depth). This envelope
does not calibrate raw values to centipawns. Progress `info depth`, `score cp`,
raw-score diagnostics and MultiPV scores remain provisional.

Only a SHA-verified engine explicitly registered/configured as `kind: "chesslab"`
grants this contract authority. `ManagedEngine` verifies the active search counter,
unique adjacent envelope, identical bestmove, depth/status/value constraints and
existing readiness barrier. A completed fixed-depth envelope must reach the
requested depth. A cancelled fixed-depth envelope requires a controller stop
request for that same search; only then may it retain an earlier completed depth.
An unsolicited fixed-depth cancellation fails the protocol before claim arbitration,
even if its depth equals the target. Movetime, clock and infinite searches retain
their existing engine-local cancellation/completion semantics.
Malformed, duplicate, conflicting, stale or nonadjacent trusted envelopes fail
the session. Generic engines' extensions remain nonauthoritative. An absent final
envelope confers no score-based claim authority; older trusted adjacent draw-claim
extensions retain their original interpretation and independent validation.

In play/experiments, the controller uses `FideRules` and the recorded history:
when the final PLAY score is unfavorable or exactly zero for the mover, take an
available claim. Prefer an immediate claim, otherwise the lexicographically first
eligible UCI intended move. A favorable score plays the engine's selected move.
`Game.accept()` still independently validates the final session/root binding,
bestmove legality, claim and monotonic receipt deadline. Claims do not play their
intended move or earn an increment. No playing-time/stop-grace policy changes.

`Proposal.finalSearch` optionally preserves the structured score, counter, depth,
status and contract. Library and GUI analysis set `claimPolicy: "none"`, returning
the strongest PLAY and score without automatic root-claim arbitration or playing
any move. Other `ManagedEngine.search` calls default to controller arbitration
when a trusted final envelope is available. Legacy claim extensions remain
visible in older-engine analysis as before. Terminal roots emit `bestmove 0000`
without a new envelope; independent terminal validation applies. Cancellation
before depth one emits an explicitly unevaluated fallback with no final score
and cannot trigger automatic score-based claims. Cancellation after a committed
depth can use that final committed score. Later evaluator/search failures emit
no final score or successful bestmove and invalidate earlier progress.

See [M7.2 validation and benchmarks](../docs/m7.2-validation.md).
