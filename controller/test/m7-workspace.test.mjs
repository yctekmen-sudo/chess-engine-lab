import test from "node:test";
import { request } from "node:http";
import assert from "node:assert/strict";
import { mkdtemp, rm, mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { randomUUID } from "node:crypto";
import { WebSocket } from "ws";
import { Workspace } from "../dist/workspace/service.js";
import { startServer } from "../dist/server/server.js";
import { Game } from "../dist/game.js";
import { DEFAULT_POSITION } from "../dist/rules.js";
import { document, importPgn, exportPgn } from "../dist/workspace/documents.js";
import { resumeExperiment, createExperiment } from "../dist/experiment.js";
async function folder(t) {
  const p = await mkdtemp(join(tmpdir(), "m7-"));
  t.after(() => rm(p, { recursive: true, force: true }));
  return p;
}
const cmd = (w, operation, payload = {}, objectId, generation) => ({
  version: 1,
  commandId: randomUUID(),
  operation,
  payload,
  expectedRevision: w.snapshot().revision,
  ...(objectId
    ? {
        objectId,
        expectedGeneration:
          generation ?? w.snapshot().games[objectId]?.generation,
      }
    : {}),
});
test("durable accepted IDs, lost ACK, stale tabs and paused restart", async (t) => {
  const dir = await folder(t);
  let w = await Workspace.open(dir);
  const c = cmd(w, "game.create"),
    a = await w.dispatch(c);
  assert(a.accepted);
  assert.deepEqual(await w.dispatch(c), a);
  await assert.rejects(
    w.dispatch({ ...c, payload: { fen: DEFAULT_POSITION } }),
    /different content/,
  );
  const id = a.result.id;
  assert((await w.dispatch(cmd(w, "game.resume", {}, id))).accepted);
  const move = cmd(w, "game.move", { move: "e2e4" }, id);
  const ack = await w.dispatch(move);
  assert(ack.accepted);
  assert(!(await w.dispatch({ ...move, commandId: randomUUID() })).accepted);
  await w.close();
  w = await Workspace.open(dir);
  try {
    assert.deepEqual(await w.dispatch(move), ack);
    assert.equal(w.snapshot().games[id].checkpoint.record.moves.length, 1);
    assert(w.snapshot().games[id].checkpoint.paused);
  } finally {
    await w.close();
  }
});
test("takeback audit restores clocks and variations never alter official moves", async (t) => {
  let time = 0;
  const w = await Workspace.open(await folder(t), {
    clock: { now: () => time },
  });
  try {
    let a = await w.dispatch(
      cmd(w, "game.create", {
        time: { kind: "fischer", initialMs: 1000, incrementMs: 10 },
      }),
    );
    const id = a.result.id;
    await w.dispatch(cmd(w, "game.resume", {}, id));
    time = 100;
    await w.dispatch(cmd(w, "game.move", { move: "e2e4" }, id));
    assert.equal(w.snapshot().games[id].checkpoint.remaining.w, 910);
    await w.dispatch(cmd(w, "game.takeback", {}, id));
    const g = w.snapshot().games[id];
    assert.equal(g.checkpoint.record.moves.length, 0);
    assert(g.checkpoint.paused);
    assert.equal(g.checkpoint.remaining.w, 1000);
    assert.equal(g.audit[0].removed[0], "e2e4");
    a = await w.dispatch(cmd(w, "analysis.create"));
    const d = w.snapshot().analyses[a.result.id];
    await w.dispatch(
      cmd(w, "analysis.branch", { node: d.cursor, move: "d2d4" }, d.id),
    );
    assert.equal(w.snapshot().games[id].checkpoint.record.moves.length, 0);
  } finally {
    await w.close();
  }
});
test("PGN branching, comments and illegal atomic rejection", () => {
  const d = importPgn(
    '[Event "fixture"]\n1. e4 {center} (1. d4 d5) e5 2. Nf3 *',
  );
  assert.equal(Object.keys(d.nodes).length, 6);
  assert(Object.values(d.nodes).some((n) => n.comment === "center"));
  const again = importPgn(exportPgn(d));
  assert.equal(Object.keys(again.nodes).length, 6);
  assert.throws(() => importPgn("1. e5 *"));
  assert.throws(() => importPgn("(".repeat(18)));
});
test("loopback bootstrap, CSRF, hostile origins, WS snapshot and bounded bodies", async (t) => {
  const dir = await folder(t),
    assets = join(dir, "assets");
  await mkdir(assets);
  await writeFile(join(assets, "index.html"), "test");
  const s = await startServer({ directory: join(dir, "workspace"), assets });
  try {
    const origin = s.origin,
      code = new URL(s.launchUrl()).hash.slice(8);
    let r = await fetch(origin + "/api/v1/bootstrap", {
      method: "POST",
      headers: {
        Origin: "https://evil.test",
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ code }),
    });
    assert.equal(r.status, 400);
    r = await fetch(origin + "/api/v1/bootstrap", {
      method: "POST",
      headers: { Origin: origin, "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });
    assert(r.ok);
    const cookie = r.headers.get("set-cookie").split(";")[0],
      { csrf } = await r.json();
    r = await fetch(origin + "/api/v1/command", {
      method: "POST",
      headers: {
        Origin: origin,
        Cookie: cookie,
        "Content-Type": "application/json",
      },
      body: "{}",
    });
    assert.equal(r.status, 403);
    r = await fetch(origin + "/api/v1/snapshot", {
      headers: { Cookie: cookie, Origin: "https://evil.test" },
    });
    assert.equal(r.status, 400);
    const ws = new WebSocket(
      origin.replace("http:", "ws:") + "/api/v1/events",
      ["chesslab.ws.v1", csrf],
      { headers: { Cookie: cookie, Origin: origin } },
    );
    const initial = await new Promise((resolve, reject) => {
      ws.once("message", (b) => resolve(JSON.parse(b)));
      ws.once("error", reject);
    });
    assert.equal(initial.type, "snapshot");
    ws.close();
    r = await fetch(origin + "/api/v1/command", {
      method: "POST",
      headers: {
        Origin: origin,
        Cookie: cookie,
        "X-Chesslab-Csrf": csrf,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({
        version: 1,
        commandId: randomUUID(),
        operation: "game.create",
        expectedRevision: 0,
        payload: {},
      }),
    });
    assert((await r.json()).accepted);
    r = await fetch(origin + "/api/v1/bootstrap", {
      method: "POST",
      headers: { Origin: origin, "Content-Type": "application/json" },
      body: JSON.stringify({ code }),
    });
    assert.equal(r.status, 403);
  } finally {
    await s.close();
  }
});
test("Game checkpoint preserves M6 receipt race and restart pauses clock", () => {
  let now = 0;
  const g = new Game(
    { initialFen: DEFAULT_POSITION, moves: [], complete: true },
    { kind: "fischer", initialMs: 100, incrementMs: 10 },
    { now: () => now },
  );
  const session = randomUUID(),
    binding = g.begin(session);
  now = 120;
  const fork = g.fork();
  fork.accept({
    session,
    binding,
    receivedAt: 99,
    bestmove: "e2e4",
    decision: { kind: "play", move: "e2e4" },
  });
  assert.equal(fork.clocks().w, 11);
  const restored = Game.restore(fork.checkpoint(), { now: () => 10000 });
  assert(restored.checkpoint().paused);
  assert.equal(restored.clocks().b, 100);
});
test("M6 optional boundary hook pauses after a completed game and resume deduplicates", async (t) => {
  const dir = await folder(t),
    engine = resolve(
      import.meta.dirname,
      "../../build/native-debug/chess_lab_uci",
    );
  const cfg = {
    engines: [{ path: engine }, { path: engine }],
    openings: [
      {
        initialFen: "7k/8/5KQ1/8/8/8/8/8 w - - 149 1",
        moves: [],
        complete: false,
      },
    ],
    seed: 1,
    limit: { kind: "depth", depth: 1 },
    time: { kind: "untimed" },
    maxPlies: 3,
    searchCapMs: 1000,
  };
  await createExperiment(dir, cfg);
  let calls = 0;
  const partial = await resumeExperiment(dir, {
    shouldContinue: () => ++calls === 1,
  });
  assert.equal(partial.completed, 1);
  const full = await resumeExperiment(dir);
  assert.equal(full.completed, 2);
  assert.equal(full.attempts.length, 2);
});
test("server rejects oversized JSON, fake Host and unauthenticated websocket", async (t) => {
  const root = await folder(t),
    assets = join(root, "assets");
  await mkdir(assets);
  await writeFile(join(assets, "index.html"), "test");
  const s = await startServer({ directory: join(root, "ws"), assets });
  try {
    assert.equal(
      await new Promise((resolve, reject) => {
        const req = request(
          s.origin,
          { headers: { Host: "evil.test" } },
          (res) => {
            res.resume();
            resolve(res.statusCode);
          },
        );
        req.on("error", reject);
        req.end();
      }),
      400,
    );
    const forbidden = await new Promise((resolve) => {
      const client = new WebSocket(
        s.origin.replace("http", "ws") + "/api/v1/events",
        ["chesslab.ws.v1"],
        { headers: { Origin: "https://evil.test" } },
      );
      client.on("unexpected-response", (_req, res) => {
        resolve(res.statusCode);
        client.terminate();
      });
      client.on("error", () => {});
    });
    assert.equal(forbidden, 403);
    const r = await fetch(s.origin + "/api/v1/bootstrap", {
      method: "POST",
      headers: { Origin: s.origin, "Content-Type": "application/json" },
      body: "x".repeat(262145),
    });
    assert.equal(r.status, 400);
  } finally {
    await s.close();
  }
});
test("engine registry trust, stable command retry, identity change and stopped analysis", async (t) => {
  const dir = await folder(t),
    w = await Workspace.open(dir);
  try {
    const config = {
      path: resolve(
        import.meta.dirname,
        "../../build/native-debug/chess_lab_uci",
      ),
      kind: "chesslab",
    };
    let a = await w.dispatch(
      cmd(w, "engine.register", { label: "trusted", config }),
    );
    assert.equal(a.error.code, "TRUST_REQUIRED");
    const c = cmd(w, "engine.register", {
      label: "trusted",
      trust: true,
      config,
    });
    a = await w.dispatch(c);
    assert(a.accepted);
    assert.deepEqual(await w.dispatch(c), a);
    const profile = a.result.id;
    const d = await w.dispatch(cmd(w, "analysis.create"));
    const id = d.result.id;
    const start = await w.dispatch(
      cmd(
        w,
        "analysis.start",
        { profile, limit: { kind: "infinite" }, multiPV: 2 },
        id,
      ),
    );
    assert(start.accepted);
    await new Promise((r) => setTimeout(r, 50));
    let stop = await w.dispatch(cmd(w, "analysis.stop", {}, id));
    if (!stop.accepted)
      stop = await w.dispatch(cmd(w, "analysis.stop", {}, id));
    assert(stop.accepted);
    const until = Date.now() + 5000;
    while (
      !Object.values(w.snapshot().jobs).some((j) => j.status === "stopped") &&
      Date.now() < until
    )
      await new Promise((r) => setTimeout(r, 10));
    assert(
      Object.values(w.snapshot().jobs).some((j) => j.status === "stopped"),
    );
  } finally {
    await w.close();
  }
});
test("failed durable write cannot acknowledge or publish a move", async (t) => {
  const w = await Workspace.open(await folder(t));
  try {
    const create = await w.dispatch(cmd(w, "game.create"));
    const id = create.result.id;
    await w.dispatch(cmd(w, "game.resume", {}, id));
    const move = cmd(w, "game.move", { move: "e2e4" }, id),
      before = w.snapshot(),
      persist = w.persist;
    w.persist = async () => {
      throw Error("injected fsync failure");
    };
    await assert.rejects(w.dispatch(move), /fsync failure/);
    assert.deepEqual(w.snapshot().games[id].checkpoint.record.moves, []);
    assert.equal(w.snapshot().revision, before.revision);
    w.persist = persist;
    await assert.rejects(w.dispatch(move), /Fix storage/);
    assert(
      w.snapshot().diagnostics.some((p) => p.code === "PERSISTENCE_FAILURE"),
    );
  } finally {
    await w.close();
  }
});
test("changed registered argv artifact requires explicit re-registration", async (t) => {
  const dir = await folder(t),
    script = join(dir, "fixture.mjs");
  await writeFile(
    script,
    "import{createInterface}from'node:readline';createInterface({input:process.stdin}).on('line',s=>{if(s==='uci')console.log('id name fixture\\nuciok');if(s==='isready')console.log('readyok');if(s==='quit')process.exit();});",
  );
  const w = await Workspace.open(join(dir, "workspace"));
  try {
    const profile = await w.dispatch(
      cmd(w, "engine.register", {
        label: "fixture",
        trust: true,
        config: { path: process.execPath, args: [script] },
      }),
    );
    assert(profile.accepted);
    await writeFile(script, "changed");
    const d = await w.dispatch(cmd(w, "analysis.create"));
    const start = await w.dispatch(
      cmd(
        w,
        "analysis.start",
        { profile: profile.result.id, limit: { kind: "depth", depth: 1 } },
        d.result.id,
      ),
    );
    assert.equal(start.error.code, "IDENTITY_CHANGED");
    assert.equal(Object.keys(w.snapshot().jobs).length, 0);
  } finally {
    await w.close();
  }
});
test("workspace layout cannot forge scheduler authorization records", async (t) => {
  const w = await Workspace.open(await folder(t));
  try {
    const a = await w.dispatch(
      cmd(w, "workspace.settings", {
        concurrencyAuthorizations: [{ fake: true }],
      }),
    );
    assert(!a.accepted);
    assert.equal(a.error.code, "VALIDATION");
    assert.equal(w.snapshot().settings.concurrencyAuthorizations, undefined);
  } finally {
    await w.close();
  }
});
async function waitFor(predicate) {
  const end = Date.now() + 15000;
  while (!predicate()) {
    if (Date.now() > end) throw Error("Timed out waiting for workspace state");
    await new Promise((r) => setTimeout(r, 10));
  }
}
async function fresh(w, op, p = {}, id) {
  for (let n = 0; n < 10; n++) {
    const a = await w.dispatch(cmd(w, op, p, id));
    if (a.error?.code !== "STALE_REVISION") return a;
  }
  throw Error("Workspace remained busy");
}
test("scheduler isolates experiments, records explicit competition, caps children and releases stopped leases", async (t) => {
  const w = await Workspace.open(await folder(t));
  try {
    const p = await fresh(w, "engine.register", {
        label: "fixture",
        trust: true,
        config: {
          path: resolve(
            import.meta.dirname,
            "../../build/native-debug/chess_lab_uci",
          ),
          kind: "chesslab",
        },
      }),
      profile = p.result.id,
      d = await fresh(w, "analysis.create"),
      id = d.result.id;
    const e = await fresh(w, "experiment.create", {
      white: profile,
      black: profile,
      config: {
        seed: 1,
        limit: { kind: "depth", depth: 1 },
        time: { kind: "untimed" },
        maxPlies: 3,
        searchCapMs: 1000,
        openings: [
          {
            initialFen: "7k/8/5KQ1/8/8/8/8/8 w - - 149 1",
            moves: [],
            complete: false,
          },
        ],
      },
    });
    assert(e.accepted);
    const start = await fresh(
      w,
      "analysis.start",
      { profile, limit: { kind: "infinite" } },
      id,
    );
    assert(start.accepted);
    const blocked = await fresh(w, "experiment.resume", {}, e.result.id);
    assert.equal(blocked.error.code, "RESOURCE_BUSY");
    const shared = await fresh(
      w,
      "experiment.resume",
      { allowCompetition: true },
      e.result.id,
    );
    assert(shared.accepted);
    assert.equal(w.snapshot().settings.concurrencyAuthorizations.length, 1);
    await waitFor(
      () => w.snapshot().experiments[e.result.id].status === "completed",
    );
    assert.equal(w.snapshot().experiments[e.result.id].summary.completed, 2);
    assert.equal(shared.result.authorization.allowCompetition, true);
    // The bounded recent-layout window is not the integrity record.
    w.state.snapshot.settings.concurrencyAuthorizations = [];
    const exported = await w.export("experiment", e.result.id);
    assert(
      exported.sidecar.m7.authorizations.some(
        (a) => a.command === shared.commandId,
      ),
    );
    assert.equal(exported.sidecar.m7.share, true);
    await fresh(w, "analysis.stop", {}, id);
    await waitFor(
      () => w.snapshot().jobs[start.result.id].status === "stopped",
    );
    await waitFor(() => w.leases.size === 0);
    const a = await fresh(
      w,
      "analysis.start",
      { profile, limit: { kind: "depth", depth: 1 } },
      id,
    );
    assert(a.accepted);
    await waitFor(() => w.snapshot().jobs[a.result.id].status === "completed");
    assert(w.snapshot().jobs[a.result.id].identity);
    await waitFor(() => w.leases.size === 0);
    w.lease("test", "analysis", true, 4);
    assert.throws(() => w.lease("excess", "analysis", true), /Four engine/);
    w.leases.delete("test");
  } finally {
    await w.close();
  }
});
test("workspace pause and stop retain completed paired results at M6 boundaries", async (t) => {
  const w = await Workspace.open(await folder(t));
  try {
    const profile = (
      await fresh(w, "engine.register", {
        label: "fixture",
        trust: true,
        config: {
          path: resolve(
            import.meta.dirname,
            "../../build/native-debug/chess_lab_uci",
          ),
        },
      })
    ).result.id;
    const id = (
      await fresh(w, "experiment.create", {
        white: profile,
        black: profile,
        config: {
          seed: 1,
          limit: { kind: "depth", depth: 1 },
          time: { kind: "untimed" },
          maxPlies: 4,
          searchCapMs: 1000,
        },
      })
    ).result.id;
    let request;
    const listener = (e) => {
      if (e.type === "progress" && e.objectId === id && !request)
        request = fresh(w, "experiment.pause", {}, id);
    };
    w.on("event", listener);
    assert((await fresh(w, "experiment.resume", {}, id)).accepted);
    await waitFor(() => w.snapshot().experiments[id].status === "paused");
    w.off("event", listener);
    assert((await request).accepted);
    assert.equal(w.snapshot().experiments[id].summary.completed, 1);
    const first = (await w.export("experiment", id)).sidecar.attempts[0];
    assert((await fresh(w, "experiment.resume", {}, id)).accepted);
    assert((await fresh(w, "experiment.stop", {}, id)).accepted);
    await waitFor(() =>
      ["stopped", "completed"].includes(w.snapshot().experiments[id].status),
    );
    const attempts = (await w.export("experiment", id)).sidecar.attempts;
    assert.deepEqual(
      attempts.find((a) => a.id === first.id),
      first,
    );
    assert((await fresh(w, "experiment.resume", {}, id)).accepted);
    await waitFor(() => w.snapshot().experiments[id].status === "completed");
    assert.equal(w.snapshot().experiments[id].summary.completed, 2);
  } finally {
    await w.close();
  }
});
test("repeated launcher uses the protected local channel without another controller", async (t) => {
  const root = await folder(t),
    assets = join(root, "assets");
  await mkdir(assets);
  await writeFile(join(assets, "index.html"), "test");
  const s = await startServer({ directory: join(root, "workspace"), assets });
  try {
    const { launch } = await import("../dist/server/launcher.js");
    const before = s.workspace.snapshot().revision;
    const result = await launch(join(root, "workspace"), assets, {
      openBrowser: false,
    });
    assert.equal(result.origin, s.origin);
    assert.equal(s.workspace.snapshot().revision, before);
    const { readFile } = await import("node:fs/promises");
    const locator = await readFile(
      join(root, "workspace", "runtime.json"),
      "utf8",
    );
    assert(!locator.includes("launch="));
    assert(!locator.includes("csrf"));
  } finally {
    await s.close();
  }
});
test("duplicate resume cannot reset a running clock, and background persistence errors become visible", async (t) => {
  let now = 0;
  const w = await Workspace.open(await folder(t), {
    clock: { now: () => now },
  });
  try {
    const id = (
      await fresh(w, "game.create", {
        time: { kind: "fischer", initialMs: 1000, incrementMs: 0 },
      })
    ).result.id;
    await fresh(w, "game.resume", {}, id);
    now = 100;
    assert(!(await fresh(w, "game.resume", {}, id)).accepted);
    assert.equal(w.snapshot().games[id].checkpoint.remaining.w, 900);
    w.emit("errorNotice", Error("injected disk error"));
    assert(
      w.snapshot().diagnostics.some((p) => p.code === "PERSISTENCE_FAILURE"),
    );
    await assert.rejects(
      w.dispatch(cmd(w, "game.pause", {}, id)),
      /Fix storage/,
    );
  } finally {
    await w.close();
  }
});
test("analysis PGN retains its played prefix, headers, root comments and annotations", () => {
  const d = document({
    initialFen: DEFAULT_POSITION,
    moves: ["e2e4", "e7e5"],
    complete: true,
  });
  d.nodes[d.cursor].comment = "known played prefix";
  const result = importPgn(exportPgn(d));
  assert.deepEqual(result.nodes[result.cursor].record.moves, ["e2e4", "e7e5"]);
  assert(
    Object.values(result.nodes).some(
      (n) => n.comment === "known played prefix",
    ),
  );
  const imported = importPgn(
    '[Event "Original"]\n{root} 1. e4!? (1. d4 $1) e5 *',
  );
  const exported = exportPgn(imported);
  assert(exported.includes('[Event "Original"]'));
  assert(exported.includes("{root}"));
  assert(exported.includes("$5"));
  assert(exported.includes("$1"));
  assert.throws(() => importPgn("1. e4 {unclosed"));
});

test("post-rename write uncertainty freezes memory and restart reconciles the durable command once", async (t) => {
  const directory = await folder(t);
  let w = await Workspace.open(directory);
  const id = (await fresh(w, "game.create")).result.id;
  await fresh(w, "game.resume", {}, id);
  const move = cmd(w, "game.move", { move: "e2e4" }, id),
    persist = w.persist.bind(w);
  w.persist = async (value) => {
    await persist(value);
    throw Error("injected post-rename fsync error");
  };
  await assert.rejects(w.dispatch(move), /post-rename/);
  assert.equal(w.snapshot().games[id].checkpoint.record.moves.length, 0);
  await assert.rejects(
    w.dispatch(cmd(w, "game.move", { move: "d2d4" }, id)),
    /Fix storage/,
  );
  await w.close();
  w = await Workspace.open(directory);
  try {
    assert.equal(w.snapshot().games[id].checkpoint.record.moves.length, 1);
    assert(w.snapshot().games[id].checkpoint.paused);
    const ack = await w.dispatch(move);
    assert(ack.accepted);
    assert.equal(w.snapshot().games[id].checkpoint.record.moves.length, 1);
  } finally {
    await w.close();
  }
});
