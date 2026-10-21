import { EventEmitter } from "node:events";
import { randomUUID } from "node:crypto";
import { join, isAbsolute } from "node:path";
import { readFile, stat } from "node:fs/promises";
import {
  Game,
  monotonic,
  type TimeControl,
  type MonotonicClock,
} from "../game.js";
import { FideRules, DEFAULT_POSITION, type RecordState } from "../rules.js";
import {
  ManagedEngine,
  executableIdentity,
  type EngineConfig,
  type SearchLimit,
} from "../engine.js";
import {
  createExperiment,
  resumeExperiment,
  exportExperiment,
  validateConfig,
} from "../experiment.js";
import {
  writeRecord,
  readRecord,
  exists,
  writerLock,
  digest,
} from "../store.js";
import {
  command,
  object,
  text,
  integer,
  identifier,
  Fault,
  type Ack,
  type Snapshot,
  type Stored,
  type Event,
  type Job,
  type Profile,
  type Casual,
} from "./protocol.js";
import { document, addNode, importPgn, exportPgn } from "./documents.js";
export class Workspace extends EventEmitter {
  private state!: Stored;
  private tail: Promise<unknown> = Promise.resolve();
  private release!: () => Promise<void>;
  private games = new Map<string, Game>();
  private tasks = new Map<
    string,
    { abort: AbortController; done: Promise<void> }
  >();
  private leases = new Map<
    string,
    { kind: string; share: boolean; count: number }
  >();
  private timer: NodeJS.Timeout | undefined;
  private closing = false;
  private shutdown: Promise<void> | undefined;
  private runtimeFailure: ReturnType<typeof problem> | undefined;
  private constructor(
    readonly directory: string,
    readonly clock: MonotonicClock,
  ) {
    super();
    this.on("errorNotice", (e) => {
      if (!this.runtimeFailure) {
        this.runtimeFailure = problem(e);
        this.emitState();
      }
    });
  }
  static async open(
    directory: string,
    options: { takeover?: boolean; clock?: MonotonicClock } = {},
  ): Promise<Workspace> {
    const w = new Workspace(directory, options.clock ?? monotonic);
    w.release = await writerLock(directory, options.takeover ?? false);
    try {
      const path = join(directory, "state.json");
      if (await exists(path)) {
        w.state = await readRecord<Stored>(path);
        if (w.state.snapshot.version !== 1)
          throw Error("Unsupported workspace version");
        for (const g of Object.values(w.state.snapshot.games)) {
          const live = Game.restore(g.checkpoint, w.clock);
          w.games.set(g.id, live);
          g.checkpoint = live.checkpoint();
          g.generation++;
        }
        for (const j of Object.values(w.state.snapshot.jobs))
          if (["queued", "running", "stopping"].includes(j.status))
            j.status = "interrupted";
        for (const e of Object.values(w.state.snapshot.experiments))
          if (
            ["running", "pause_requested", "stop_requested"].includes(e.status)
          )
            e.status = "paused";
        w.state.snapshot.revision++;
      } else
        w.state = {
          snapshot: {
            version: 1,
            id: randomUUID(),
            revision: 0,
            activeGameId: null,
            games: {},
            analyses: {},
            profiles: {},
            experiments: {},
            jobs: {},
            settings: {},
            diagnostics: [],
          },
          commands: {},
        };
      await w.persist(w.state);
      w.timer = setInterval(
        () => void w.tick().catch((e) => w.emit("errorNotice", String(e))),
        100,
      );
      w.timer.unref();
      return w;
    } catch (e) {
      await w.release();
      throw e;
    }
  }
  snapshot(): Snapshot {
    const s = structuredClone(this.state.snapshot);
    for (const [id, r] of this.games)
      if (s.games[id]) s.games[id]!.checkpoint = r.checkpoint();
    if (this.runtimeFailure)
      s.diagnostics.push({
        ...this.runtimeFailure,
        code: "PERSISTENCE_FAILURE",
        message:
          "Controller cannot safely persist work. Restart after fixing storage: " +
          this.runtimeFailure.message,
      });
    return s;
  }
  position(value: unknown) {
    const r = new FideRules(readPosition({ record: value }));
    return {
      fen: r.fen(),
      legal: r.legal(),
      claim: r.claim(),
      outcome: r.terminal() ?? null,
    };
  }
  commandStatus(id: string) {
    return structuredClone(this.state.commands[identifier(id)]?.ack ?? null);
  }
  private async persist(value: Stored) {
    if (this.runtimeFailure)
      throw new Fault(
        "PERSISTENCE_FAILURE",
        "Workspace requires restart after storage failure",
      );
    if (JSON.stringify(value).length > 16 * 1024 * 1024)
      throw new Fault("LIMIT", "Workspace size limit");
    try {
      await writeRecord(join(this.directory, "state.json"), value, false);
    } catch (e) {
      this.emit("errorNotice", e);
      throw e;
    }
  }
  private serial<T>(fn: () => Promise<T>): Promise<T> {
    const p = this.tail.then(fn);
    this.tail = p.catch(() => {});
    return p;
  }
  private emitState() {
    this.emit("event", {
      version: 1,
      type: "state",
      revision: this.state.snapshot.revision,
      snapshot: this.snapshot(),
    } satisfies Event);
  }
  private async update(fn: (s: Snapshot) => void) {
    await this.serial(async () => {
      if (this.runtimeFailure)
        throw new Fault(
          "PERSISTENCE_FAILURE",
          "Workspace requires restart after storage failure",
        );
      const next = structuredClone(this.state);
      fn(next.snapshot);
      next.snapshot.revision++;
      await this.persist(next);
      this.state = next;
      this.emitState();
    });
  }
  private lease(id: string, kind: string, share = false, count = 1) {
    if ([...this.leases.values()].reduce((n, l) => n + l.count, 0) + count > 4)
      throw new Fault("RESOURCE_BUSY", "Four engine-process slots are in use");
    if (
      [...this.leases.values()].some(
        (l) => kind === "experiment" || l.kind === "experiment",
      ) &&
      !share
    )
      throw new Fault(
        "RESOURCE_BUSY",
        "Experiment isolation: pause at a game boundary or explicitly authorize competition",
      );
    this.leases.set(id, { kind, share, count });
  }
  private busy(id: string) {
    return Object.values(this.state.snapshot.jobs).some(
      (j) =>
        j.objectId === id &&
        ["queued", "running", "stopping"].includes(j.status),
    );
  }
  private cancel(id: string) {
    for (const [key, t] of this.tasks)
      if (
        this.state.snapshot.jobs[key]?.objectId === id &&
        ["queued", "running", "stopping"].includes(
          this.state.snapshot.jobs[key]!.status,
        )
      )
        t.abort.abort();
  }
  private profile(s: Snapshot, id: unknown): Profile {
    const p = s.profiles[identifier(id)];
    if (!p) throw new Fault("NOT_FOUND", "Register an engine first");
    return p;
  }
  private async verify(p: Profile) {
    const actual = await executableIdentity(p.config);
    if (actual.sha256 !== p.identity.sha256 || actual.path !== p.identity.path)
      throw new Fault(
        "IDENTITY_CHANGED",
        "Executable changed; re-register and confirm trust",
      );
    for (const [path, hash] of Object.entries(p.artifacts)) {
      if ((await executableHash(path)) !== hash)
        throw new Fault(
          "IDENTITY_CHANGED",
          "Evaluator artifact changed; re-register",
        );
    }
  }
  async dispatch(raw: unknown, receivedAt = this.clock.now()): Promise<Ack> {
    const c = command(raw);
    return this.serial(async () => {
      if (this.closing)
        throw new Fault("CLOSING", "Controller is shutting down");
      if (this.runtimeFailure)
        throw new Fault(
          "PERSISTENCE_FAILURE",
          "Fix storage and restart the controller before changing the workspace",
        );
      const hash = digest(c),
        old = this.state.commands[c.commandId];
      if (old) {
        if (old.hash !== hash)
          throw new Fault(
            "COMMAND_CONFLICT",
            "Command ID reused with different content",
          );
        return structuredClone(old.ack);
      }
      if (Object.keys(this.state.commands).length >= 10000)
        throw new Fault(
          "LIMIT",
          "Workspace command retention limit; create a new workspace",
        );
      const next = structuredClone(this.state),
        s = next.snapshot,
        after: Array<() => void> = [],
        live = new Map(this.games);
      let ack: Ack;
      try {
        if (c.expectedRevision !== s.revision)
          throw new Fault(
            "STALE_REVISION",
            "Refresh the authoritative snapshot",
          );
        const p = c.payload,
          id = c.objectId;
        const fields: Record<string, string[]> = {
          "workspace.settings": [
            "tab",
            "flipped",
            "collapsed",
            "activeAnalysis",
            "panelWidth",
          ],
          "game.create": ["record", "fen", "time", "players"],
          "game.select": [],
          "game.move": ["move"],
          "game.claim": ["move"],
          "game.pause": [],
          "game.resume": [],
          "game.resign": ["side"],
          "game.takeback": [],
          "analysis.create": ["record", "fen", "pgn"],
          "analysis.branch": ["node", "move"],
          "analysis.cursor": ["node"],
          "analysis.annotate": ["node", "comment", "nags"],
          "analysis.start": ["profile", "limit", "multiPV", "allowCompetition"],
          "analysis.stop": [],
          "engine.register": ["label", "trust", "config", "allowCompetition"],
          "experiment.create": ["white", "black", "config"],
          "experiment.start": ["allowCompetition"],
          "experiment.resume": ["allowCompetition"],
          "experiment.pause": [],
          "experiment.stop": [],
        };
        if (
          fields[c.operation] &&
          Object.keys(p).some((k) => !fields[c.operation]!.includes(k))
        )
          throw new Fault("VALIDATION", "Unknown payload field");
        const game = (): { g: Casual; r: Game } => {
          const g = id ? s.games[id] : undefined;
          if (!g) throw new Fault("NOT_FOUND", "Game missing");
          if (c.expectedGeneration !== g.generation)
            throw new Fault("STALE_GENERATION", "Game generation changed");
          return { g, r: live.get(g.id)!.fork() };
        };
        const commitGame = (g: Casual, r: Game) => {
          g.checkpoint = r.checkpoint();
          g.generation++;
          live.set(g.id, r);
        };
        let result: unknown = {};
        switch (c.operation) {
          case "workspace.settings": {
            if (JSON.stringify(p).length > 4096)
              throw new Fault("LIMIT", "Layout settings limit");
            if (
              p.tab !== undefined &&
              !["Play", "Analyze", "Experiments", "Engines"].includes(
                String(p.tab),
              )
            )
              throw new Fault("VALIDATION", "Unknown workspace tab");
            for (const key of ["flipped", "collapsed"])
              if (p[key] !== undefined && typeof p[key] !== "boolean")
                throw new Fault(
                  "VALIDATION",
                  "Expected boolean layout setting",
                );
            if (p.panelWidth !== undefined) integer(p.panelWidth, 280, 500);
            if (p.activeAnalysis !== undefined && p.activeAnalysis !== "")
              identifier(p.activeAnalysis);
            s.settings = { ...s.settings, ...p };
            break;
          }
          case "game.create": {
            const record = readPosition(p),
              time = (p.time ?? { kind: "untimed" }) as TimeControl,
              players = object(p.players ?? { w: "human", b: "human" }),
              w = text(players.w, 100),
              b = text(players.b, 100);
            if (w !== "human") this.profile(s, w);
            if (b !== "human") this.profile(s, b);
            if (w !== "human" && b !== "human")
              throw new Fault(
                "VALIDATION",
                "Engine versus engine belongs in Experiments",
              );
            if (time.kind !== "untimed" && time.kind !== "fischer")
              throw new Fault("VALIDATION", "Unsupported clock");
            const r = new Game(record, time, this.clock);
            r.pause();
            const gid = randomUUID();
            const checkpoint = r.checkpoint();
            s.games[gid] = {
              id: gid,
              generation: 0,
              checkpoint,
              players: { w, b },
              audit: [],
              history: [checkpoint],
            };
            live.set(gid, r);
            s.activeGameId = gid;
            if (w === "human" && b !== "human") s.settings.flipped = false;
            else if (w !== "human" && b === "human") s.settings.flipped = true;
            result = { id: gid };
            break;
          }
          case "game.select":
            if (!id || !s.games[id])
              throw new Fault("NOT_FOUND", "Game missing");
            else {
              s.activeGameId = id;
              break;
            }
          case "game.move":
          case "game.claim": {
            const { g, r } = game();
            if (g.checkpoint.paused)
              throw new Fault("PAUSED", "Resume the game first");
            if (g.players[r.side()] !== "human")
              throw new Fault(
                "TURN",
                "This turn belongs to the registered engine",
              );
            const legal = new FideRules(r.record()).legal(),
              move =
                c.operation === "game.move"
                  ? text(p.move, 5)
                  : p.move
                    ? text(p.move, 5)
                    : undefined;
            const session = randomUUID(),
              binding = r.begin(session),
              decision =
                c.operation === "game.move"
                  ? { kind: "play" as const, move: move! }
                  : move
                    ? { kind: "claim_draw_with_intended_move" as const, move }
                    : { kind: "claim_draw" as const };
            r.accept({
              session,
              binding,
              decision,
              bestmove: c.operation === "game.move" ? move! : legal[0]!,
              receivedAt,
            });
            commitGame(g, r);
            g.history.push(g.checkpoint);
            after.push(() => this.autoPlay(g.id));
            break;
          }
          case "game.pause": {
            const { g, r } = game();
            r.pause();
            commitGame(g, r);
            after.push(() => this.cancel(g.id));
            break;
          }
          case "game.resume": {
            const { g, r } = game();
            if (this.busy(g.id))
              throw new Fault(
                "RESOURCE_BUSY",
                "Wait for the old search to stop",
              );
            r.resume();
            commitGame(g, r);
            after.push(() => this.autoPlay(g.id));
            break;
          }
          case "game.resign": {
            const { g, r } = game();
            if (p.side !== "w" && p.side !== "b")
              throw new Fault("VALIDATION", "Choose White or Black to resign");
            const side = p.side;
            if (g.players[side] !== "human")
              throw new Fault("TURN", "Only a local human can resign");
            r.resign(side);
            commitGame(g, r);
            after.push(() => this.cancel(g.id));
            break;
          }
          case "game.takeback": {
            const { g, r } = game();
            r.pause();
            const steps =
                g.players.w !== "human" || g.players.b !== "human" ? 2 : 1,
              target = Math.max(0, g.history.length - 1 - steps),
              cp = g.history[target]!;
            g.audit.push({
              kind: "takeback",
              removed: g.checkpoint.record.moves.slice(cp.record.moves.length),
              from: g.checkpoint.record,
              to: cp.record,
              at: new Date().toISOString(),
            });
            const restored = Game.restore(cp, this.clock);
            g.history = g.history.slice(0, target + 1);
            commitGame(g, restored);
            after.push(() => this.cancel(g.id));
            break;
          }
          case "analysis.create": {
            const d =
              p.pgn !== undefined
                ? importPgn(text(p.pgn, 262144))
                : document(readPosition(p));
            s.analyses[d.id] = d;
            result = { id: d.id };
            break;
          }
          case "analysis.branch":
          case "analysis.cursor":
          case "analysis.annotate": {
            const d = id ? s.analyses[id] : undefined;
            if (!d) throw new Fault("NOT_FOUND", "Analysis document missing");
            const node = identifier(p.node);
            if (!d.nodes[node])
              throw new Fault("NOT_FOUND", "Variation missing");
            if (c.operation === "analysis.branch")
              addNode(d, node, text(p.move, 5));
            else if (c.operation === "analysis.cursor") d.cursor = node;
            else {
              d.nodes[node]!.comment = text(p.comment, 4096);
              d.nodes[node]!.nags = Array.isArray(p.nags)
                ? p.nags.map((x) => {
                    const n = text(x, 8);
                    if (!/^\$\d+$/.test(n))
                      throw new Fault("VALIDATION", "Invalid annotation");
                    return n;
                  })
                : d.nodes[node]!.nags;
            }
            after.push(() => this.cancel(d.id));
            break;
          }
          case "engine.register": {
            if (p.trust !== true)
              throw new Fault(
                "TRUST_REQUIRED",
                "Native executables run with your OS user rights. Confirm trust first.",
              );
            const config = structuredClone(object(p.config)) as EngineConfig;
            if (
              Object.keys(config).some(
                (k) =>
                  ![
                    "path",
                    "args",
                    "kind",
                    "sha256",
                    "options",
                    "handshakeMs",
                    "stopGraceMs",
                  ].includes(k),
              )
            )
              throw new Fault(
                "VALIDATION",
                "Unknown engine configuration field",
              );
            if (
              config.kind !== undefined &&
              config.kind !== "uci" &&
              config.kind !== "chesslab"
            )
              throw new Fault("VALIDATION", "Unknown engine kind");
            if (
              config.options &&
              Object.values(object(config.options)).some(
                (v) => typeof v !== "string",
              )
            )
              throw new Fault("VALIDATION", "UCI options must be strings");
            const actual = await executableIdentity({ ...config, kind: "uci" });
            config.sha256 = actual.sha256;
            const lease = randomUUID();
            this.lease(lease, "inspect", p.allowCompetition === true);
            let identity;
            try {
              const engine = new ManagedEngine(config);
              try {
                identity = await engine.start();
              } finally {
                await engine.close();
              }
            } finally {
              this.leases.delete(lease);
            }
            const pid = id ?? randomUUID(),
              old = s.profiles[pid],
              artifacts: Record<string, string> = {};
            const evaluator = identity.effective.Evaluator;
            if (evaluator && isAbsolute(evaluator))
              artifacts[evaluator] = await executableHash(evaluator);
            for (const path of config.args ?? [])
              if (isAbsolute(path)) {
                try {
                  if ((await stat(path)).isFile())
                    artifacts[path] = await executableHash(path);
                } catch {
                  throw new Fault(
                    "IDENTITY_CHANGED",
                    "Configured argv artifact is missing",
                  );
                }
              }
            s.profiles[pid] = {
              id: pid,
              label: text(p.label, 100),
              generation: (old?.generation ?? 0) + 1,
              config,
              identity,
              artifacts,
            };
            result = { id: pid };
            break;
          }
          case "analysis.start": {
            const d = id ? s.analyses[id] : undefined;
            if (!d) throw new Fault("NOT_FOUND", "Analysis missing");
            if (this.busy(d.id))
              throw new Fault(
                "RESOURCE_BUSY",
                "Stop the active analysis first",
              );
            const profile = this.profile(s, p.profile);
            await this.verify(profile);
            const limit = readLimit(p.limit),
              mpv = integer(p.multiPV ?? 1, 1, 256),
              config = structuredClone(profile.config);
            if (profile.identity.options.MultiPV)
              config.options = { ...config.options, MultiPV: String(mpv) };
            else if (mpv !== 1)
              throw new Fault(
                "CAPABILITY",
                "This engine does not advertise MultiPV",
              );
            const jid = randomUUID(),
              root = new FideRules(d.nodes[d.cursor]!.record).fen();
            this.lease(jid, "analysis", p.allowCompetition === true);
            if (p.allowCompetition === true)
              s.diagnostics.push({
                code: "COMPETITION_AUTHORIZED",
                severity: "warning",
                message:
                  "Interactive competition with experiments explicitly authorized",
              });
            s.jobs[jid] = {
              id: jid,
              kind: "analysis",
              status: "queued",
              session: randomUUID(),
              objectId: d.id,
              root,
              generation: s.revision + 1,
              trusted: profile.config.kind === "chesslab",
            };
            after.push(() =>
              this.launchAnalysis(
                jid,
                config,
                d.nodes[d.cursor]!.record,
                limit,
              ),
            );
            result = { id: jid };
            break;
          }
          case "analysis.stop":
            if (!id) throw new Fault("NOT_FOUND", "Analysis missing");
            else {
              for (const j of Object.values(s.jobs))
                if (
                  j.objectId === id &&
                  ["queued", "running"].includes(j.status)
                )
                  j.status = "stopping";
              after.push(() => this.cancel(id));
              break;
            }
          case "experiment.create": {
            const a = this.profile(s, p.white),
              b = this.profile(s, p.black);
            await this.verify(a);
            await this.verify(b);
            const eid = randomUUID();
            s.experiments[eid] = {
              id: eid,
              status: "new",
              config: validateConfig({
                ...object(p.config),
                engines: [a.config, b.config],
              }),
              share: false,
              artifacts: { ...a.artifacts, ...b.artifacts },
            };
            result = { id: eid };
            break;
          }
          case "experiment.start":
          case "experiment.resume": {
            const e = id ? s.experiments[id] : undefined;
            if (!e) throw new Fault("NOT_FOUND", "Experiment missing");
            if (this.busy(e.id))
              throw new Fault("RESOURCE_BUSY", "Experiment is active");
            const jid = randomUUID();
            this.lease(jid, "experiment", p.allowCompetition === true, 2);
            e.share = p.allowCompetition === true;
            e.status = "running";
            s.jobs[jid] = {
              id: jid,
              kind: "experiment",
              status: "queued",
              session: randomUUID(),
              objectId: e.id,
              root: "",
              generation: s.revision + 1,
            };
            after.push(() => this.launchExperiment(jid, e.id));
            result = { id: jid };
            break;
          }
          case "experiment.pause":
          case "experiment.stop": {
            const e = id ? s.experiments[id] : undefined;
            if (!e) throw new Fault("NOT_FOUND", "Experiment missing");
            if (!this.busy(e.id))
              throw new Fault("NOT_RUNNING", "Experiment is not running");
            e.status =
              c.operation === "experiment.pause"
                ? "pause_requested"
                : "stop_requested";
            break;
          }
          default:
            throw new Fault("OPERATION", "Unknown operation");
        }
        if (
          id &&
          [
            "game.pause",
            "game.takeback",
            "game.resign",
            "analysis.branch",
            "analysis.cursor",
            "analysis.annotate",
          ].includes(c.operation)
        )
          for (const job of Object.values(s.jobs))
            if (
              job.objectId === id &&
              ["queued", "running"].includes(job.status)
            )
              job.status = "stopping";
        if (p.allowCompetition === true) {
          const authorization = {
            command: c.commandId,
            operation: c.operation,
            objectId: id ?? null,
            revision: s.revision + 1,
            allowCompetition: true,
            activeExperiments: Object.values(s.experiments)
              .filter((e) =>
                ["running", "pause_requested", "stop_requested"].includes(
                  e.status,
                ),
              )
              .map((e) => e.id),
          };
          s.settings.concurrencyAuthorizations = [
            ...((s.settings.concurrencyAuthorizations ?? []) as unknown[]),
            authorization,
          ].slice(-500);
          // Full authorization remains with its non-evicted durable command ACK.
          result = { ...object(result), authorization };
        }
        s.diagnostics = s.diagnostics.slice(-100);
        s.revision++;
        ack = {
          version: 1,
          commandId: c.commandId,
          accepted: true,
          revision: s.revision,
          result,
        };
      } catch (e) {
        ack = {
          version: 1,
          commandId: c.commandId,
          accepted: false,
          revision: this.state.snapshot.revision,
          error: problem(e),
        };
        next.snapshot = this.snapshot();
      }
      next.commands[c.commandId] = { hash, ack };
      try {
        await this.persist(next);
      } catch (e) {
        this.emit("errorNotice", e);
        for (const id of Object.keys(s.jobs))
          if (!this.state.snapshot.jobs[id]) this.leases.delete(id);
        throw e;
      }
      this.state = next;
      if (ack.accepted) {
        this.games = live;
        this.emitState();
        for (const fn of after) queueMicrotask(fn);
      }
      return ack;
    });
  }
  private run(id: string, fn: (signal: AbortSignal) => Promise<void>) {
    const abort = new AbortController();
    const done = (async () => {
      try {
        await fn(abort.signal);
      } catch (e) {
        await this.update((s) => {
          const j = s.jobs[id];
          if (j) {
            j.status = abort.signal.aborted ? "stopped" : "failed";
            j.error = problem(e);
          }
          if (j?.kind === "experiment" && s.experiments[j.objectId])
            s.experiments[j.objectId]!.status = "failed";
          s.diagnostics.push(problem(e));
          s.diagnostics = s.diagnostics.slice(-100);
        });
      } finally {
        this.leases.delete(id);
        this.tasks.delete(id);
      }
    })();
    this.tasks.set(id, { abort, done });
    void done.catch((e) => this.emit("errorNotice", String(e)));
  }
  private progress(job: Job, record: RecordState) {
    let sequence = 0;
    return ({ line }: { line: string }) => {
      const pv = /\spv\s+(.+)$/.exec(line);
      if (pv) {
        try {
          const moves = pv[1]!.trim().split(/\s+/);
          if (moves.length > 256) throw Error("PV length");
          const rules = new FideRules(record);
          for (const move of moves) rules.play(move);
        } catch {
          line = line.slice(0, pv.index) + " [illegal provisional PV omitted]";
        }
      }
      this.emit("event", {
        version: 1,
        type: "progress",
        session: job.session,
        objectId: job.objectId,
        root: job.root,
        generation: job.generation,
        sequence: ++sequence,
        provisional: true,
        line,
      } satisfies Event);
    };
  }
  private launchAnalysis(
    id: string,
    config: EngineConfig,
    record: RecordState,
    limit: SearchLimit,
  ) {
    this.run(id, async (signal) => {
      const job = this.state.snapshot.jobs[id]!,
        engine = new ManagedEngine(config, this.clock);
      const abort = () => engine.stop();
      signal.addEventListener("abort", abort);
      try {
        const identity = await engine.start();
        if (signal.aborted) return;
        await this.update((s) => {
          s.jobs[id]!.status = "running";
          s.jobs[id]!.identity = identity;
        });
        const result = await engine.search({
          session: job.session,
          binding: new Game(record).binding(),
          record,
          limit,
          ...(limit.kind === "infinite"
            ? {}
            : { stopAfterMs: limit.kind === "movetime" ? limit.ms : 60000 }),
          onProgress: this.progress(job, record),
        });
        if (!signal.aborted) {
          const rules = new FideRules(record),
            terminal = rules.terminal();
          if (
            terminal
              ? result.bestmove !== "0000"
              : !rules.legal().includes(result.bestmove)
          )
            throw new Fault(
              "ILLEGAL_PROPOSAL",
              "Engine returned an illegal analysis action",
            );
          if (
            result.decision.kind !== "play" &&
            !rules.claim(
              result.decision.kind === "claim_draw_with_intended_move"
                ? result.decision.move
                : undefined,
            )
          )
            throw new Fault("INVALID_CLAIM", "Unverified analysis claim");
          await this.update((s) => {
            s.jobs[id]!.status = "completed";
            s.jobs[id]!.result = result;
          });
        }
      } finally {
        signal.removeEventListener("abort", abort);
        await engine.close();
        if (signal.aborted)
          await this.update((s) => {
            s.jobs[id]!.status = "stopped";
          });
      }
    });
  }
  private autoPlay(id: string) {
    void this.serial(async () => {
      const g = this.state.snapshot.games[id];
      if (
        this.closing ||
        this.runtimeFailure ||
        !g ||
        g.checkpoint.paused ||
        g.checkpoint.outcome ||
        this.busy(id)
      )
        return;
      const r = this.games.get(id)!,
        pid = g.players[r.side()];
      if (pid === "human") return;
      const profile = this.profile(this.state.snapshot, pid),
        jid = randomUUID();
      try {
        this.lease(jid, "play");
        await this.verify(profile);
      } catch (e) {
        this.leases.delete(jid);
        await this.pauseFailure(id, e);
        return;
      }
      const next = structuredClone(this.state),
        session = randomUUID();
      next.snapshot.jobs[jid] = {
        id: jid,
        kind: "play",
        status: "queued",
        session,
        objectId: id,
        root: r.fen(),
        generation: g.generation,
      };
      next.snapshot.revision++;
      try {
        await this.persist(next);
      } catch (e) {
        this.leases.delete(jid);
        throw e;
      }
      this.state = next;
      this.emitState();
      queueMicrotask(() =>
        this.run(jid, async (signal) => {
          const engine = new ManagedEngine(profile.config, this.clock),
            abort = () => engine.stop();
          signal.addEventListener("abort", abort);
          try {
            await engine.start();
            if (signal.aborted) return;
            const initial = this.state.snapshot.games[id]!;
            if (initial.generation !== g.generation) return;
            const source = this.games.get(id)!,
              binding = source.begin(session);
            await this.update((s) => {
              s.jobs[jid]!.status = "running";
            });
            const result = await engine.search({
              session,
              binding,
              record: source.record(),
              limit:
                source.time.kind === "fischer"
                  ? { kind: "clock", ...source.uciClock() }
                  : { kind: "depth", depth: 2 },
              stopAfterMs: Math.min(60000, source.available()),
              onProgress: this.progress(
                this.state.snapshot.jobs[jid]!,
                source.record(),
              ),
            });
            await this.serial(async () => {
              const current = this.state.snapshot.games[id]!;
              if (signal.aborted || current.generation !== g.generation) return;
              const played = source.fork();
              played.accept(result);
              const n = structuredClone(this.state);
              n.snapshot.games[id]!.checkpoint = played.checkpoint();
              n.snapshot.games[id]!.generation++;
              n.snapshot.games[id]!.history.push(played.checkpoint());
              n.snapshot.jobs[jid]!.status = "completed";
              n.snapshot.revision++;
              await this.persist(n);
              this.games.set(id, played);
              this.state = n;
              this.emitState();
            });
          } catch (e) {
            if (
              !signal.aborted &&
              this.state.snapshot.games[id]?.generation === g.generation
            )
              await this.serial(() => this.pauseFailure(id, e));
            throw e;
          } finally {
            signal.removeEventListener("abort", abort);
            await engine.close();
            if (signal.aborted)
              await this.update((s) => {
                s.jobs[jid]!.status = "stopped";
              });
          }
        }),
      );
    }).catch((e) => this.emit("errorNotice", String(e)));
  }
  private async pauseFailure(id: string, e: unknown) {
    const r = this.games.get(id)?.fork();
    if (!r) return;
    if (r.available() <= 0) r.flag();
    else {
      r.pause();
      r.fail(String(e));
    }
    const n = structuredClone(this.state);
    n.snapshot.games[id]!.checkpoint = r.checkpoint();
    n.snapshot.games[id]!.generation++;
    n.snapshot.diagnostics.push(problem(e));
    n.snapshot.revision++;
    await this.persist(n);
    this.games.set(id, r);
    this.state = n;
    this.emitState();
  }
  private launchExperiment(jid: string, id: string) {
    this.run(jid, async (signal) => {
      const path = join(this.directory, "experiments", id),
        ref = this.state.snapshot.experiments[id]!;
      for (const [artifact, hash] of Object.entries(ref.artifacts))
        if ((await executableHash(artifact)) !== hash)
          throw new Fault(
            "IDENTITY_CHANGED",
            "Experiment artifact changed; restore the pinned artifact or create a new experiment",
          );
      for (const config of (ref.config as { engines: EngineConfig[] }).engines)
        await executableIdentity(config);
      if (!(await exists(join(path, "manifest.json"))))
        await createExperiment(path, ref.config);
      await writeRecord(
        join(path, "m7-execution.json"),
        {
          share: ref.share,
          workspace: this.state.snapshot.id,
          authorizations: this.authorizations(id),
        },
        false,
      );
      await this.update((s) => {
        s.jobs[jid]!.status = "running";
      });
      let sequence = 0;
      const summary = await resumeExperiment(path, {
        shouldContinue: () =>
          !signal.aborted &&
          !["pause_requested", "stop_requested"].includes(
            this.state.snapshot.experiments[id]!.status,
          ),
        onProgress: (value) => {
          this.emit("event", {
            version: 1,
            type: "progress",
            session: this.state.snapshot.jobs[jid]!.session,
            objectId: id,
            root: "",
            generation: this.state.snapshot.jobs[jid]!.generation,
            sequence: ++sequence,
            provisional: true,
            line: JSON.stringify(value),
          } satisfies Event);
        },
      });
      await writeRecord(
        join(path, "m7-execution.json"),
        {
          share: ref.share,
          workspace: this.state.snapshot.id,
          authorizations: this.authorizations(id),
        },
        false,
      );
      await this.update((s) => {
        const e = s.experiments[id]!;
        e.summary = summary;
        e.status =
          summary.completed === summary.scheduled
            ? "completed"
            : e.status === "stop_requested" || signal.aborted
              ? "stopped"
              : "paused";
        s.jobs[jid]!.status = "completed";
      });
    });
  }
  private authorizations(id: string) {
    return Object.values(this.state.commands).flatMap((c) => {
      const authorization = (
        c.ack.result as
          | {
              authorization?: {
                objectId: string | null;
                activeExperiments: string[];
              };
            }
          | undefined
      )?.authorization;
      return c.ack.accepted &&
        authorization &&
        (authorization.objectId === id ||
          authorization.activeExperiments.includes(id))
        ? [authorization]
        : [];
    });
  }
  async export(kind: string, id: string): Promise<unknown> {
    const s = this.state.snapshot;
    if (kind === "experiment") {
      identifier(id);
      if (!s.experiments[id])
        throw new Fault("NOT_FOUND", "Experiment missing");
      const path = join(this.directory, "experiments", id),
        exported = await exportExperiment(path);
      const execution = await readRecord<Record<string, unknown>>(
        join(path, "m7-execution.json"),
      );
      return {
        ...exported,
        sidecar: {
          ...object(exported.sidecar),
          m7: { ...execution, authorizations: this.authorizations(id) },
        },
      };
    }
    if (kind === "analysis") {
      const d = s.analyses[id];
      if (!d) throw new Fault("NOT_FOUND", "Document missing");
      return { pgn: exportPgn(d), document: d };
    }
    const g = s.games[id];
    if (!g) throw new Fault("NOT_FOUND", "Game missing");
    const r = new FideRules(g.checkpoint.record);
    return {
      fen: r.fen(),
      pgn: r.pgn(
        g.checkpoint.outcome?.kind === "chess"
          ? g.checkpoint.outcome
          : undefined,
        g.checkpoint.record.complete
          ? {}
          : { SetUp: "1", FEN: g.checkpoint.record.initialFen },
      ),
      sidecar: g,
    };
  }
  private async tick() {
    if (this.closing || this.runtimeFailure) return;
    await this.serial(async () => {
      for (const [id, r] of this.games) {
        const g = this.state.snapshot.games[id]!;
        if (
          !g.checkpoint.paused &&
          !g.checkpoint.outcome &&
          !this.busy(id) &&
          r.available() <= 0
        ) {
          const n = structuredClone(this.state),
            copy = r.fork();
          copy.flag();
          n.snapshot.games[id]!.checkpoint = copy.checkpoint();
          n.snapshot.games[id]!.generation++;
          n.snapshot.revision++;
          await this.persist(n);
          this.games.set(id, copy);
          this.state = n;
          this.emitState();
        }
      }
    });
  }
  close(): Promise<void> {
    return (this.shutdown ??= this.finishClose());
  }
  private async finishClose() {
    this.closing = true;
    if (this.timer) clearInterval(this.timer);
    for (const [id, t] of this.tasks) {
      if (
        this.runtimeFailure ||
        this.state.snapshot.jobs[id]?.kind !== "experiment"
      )
        t.abort.abort();
      else
        try {
          await this.update((s) => {
            s.experiments[s.jobs[id]!.objectId]!.status = "stop_requested";
          });
        } catch (e) {
          this.emit("errorNotice", e);
          t.abort.abort();
        }
    }
    const outcomes = await Promise.allSettled(
      [...this.tasks.values()].map((t) => t.done),
    );
    for (const outcome of outcomes)
      if (outcome.status === "rejected")
        this.emit("errorNotice", outcome.reason);
    try {
      await this.serial(async () => {
        // A failed fsync may follow a successful rename. Never overwrite that
        // uncertain checkpoint with older memory; restart reads its verified data.
        if (this.runtimeFailure) {
          for (const r of this.games.values()) r.pause();
          return;
        }
        const next = structuredClone(this.state);
        for (const [id, r] of this.games) {
          r.pause();
          next.snapshot.games[id]!.checkpoint = r.checkpoint();
          next.snapshot.games[id]!.generation++;
        }
        next.snapshot.revision++;
        await this.persist(next);
        this.state = next;
      });
    } finally {
      await this.release();
    }
  }
}
function problem(e: unknown) {
  return {
    code: e instanceof Fault ? e.code : "FAILURE",
    message: String(e instanceof Error ? e.message : e).slice(0, 2000),
    severity: "failure" as const,
  };
}
function readPosition(p: Record<string, unknown>): RecordState {
  const r = p.record
    ? object(p.record)
    : {
        initialFen: p.fen ?? DEFAULT_POSITION,
        moves: [],
        complete: p.fen ? false : true,
      };
  if (
    typeof r.complete !== "boolean" ||
    !Array.isArray(r.moves) ||
    r.moves.length > 4096 ||
    r.moves.some((m) => typeof m !== "string")
  )
    throw new Fault("VALIDATION", "Invalid game record");
  return new FideRules({
    initialFen: text(r.initialFen, 200),
    moves: r.moves as string[],
    complete: r.complete,
  }).record();
}
function readLimit(v: unknown): SearchLimit {
  const l = object(v);
  if (l.kind === "depth")
    return { kind: "depth", depth: integer(l.depth, 1, 64) };
  if (l.kind === "movetime")
    return { kind: "movetime", ms: integer(l.ms, 0, 600000) };
  if (l.kind === "infinite") return { kind: "infinite" };
  throw new Fault("VALIDATION", "Invalid analysis limit");
}
async function executableHash(path: string) {
  if (!(await stat(path)).isFile())
    throw new Fault("VALIDATION", "Artifact must be a file");
  return (await import("node:crypto"))
    .createHash("sha256")
    .update(await readFile(path))
    .digest("hex");
}
