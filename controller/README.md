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
