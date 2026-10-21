// Runs on Linux and native Windows: real persistence, HTTP, subprocess and IPC.
import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, mkdir, writeFile, symlink, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve, isAbsolute } from "node:path";
import { randomUUID } from "node:crypto";
import { startServer } from "../dist/server/server.js";
import { launch } from "../dist/server/launcher.js";
async function setup(t) {
  const root = await mkdtemp(join(tmpdir(), "m7-desktop-"));
  let server;
  t.after(async () => {
    try {
      await server?.close();
    } finally {
      await rm(root, { recursive: true, force: true });
    }
  });
  const assets = join(root, "GUI with spaces"),
    outside = join(root, "GUI with spaces-escape");
  await mkdir(join(assets, "assets"), { recursive: true });
  await mkdir(outside);
  await writeFile(join(assets, "index.html"), "<main>Desktop smoke</main>");
  await writeFile(
    join(assets, "assets", "app.js"),
    "window.localDesktop=true;",
  );
  await writeFile(join(outside, "secret.js"), "outside GUI root");
  await symlink(outside, join(assets, "escape"), "junction");
  const alias = join(root, "GUI link");
  await symlink(assets, alias, "junction");
  const directory = join(root, "workspace");
  server = await startServer({ directory, assets: alias });
  return { root, assets: alias, directory, server };
}
test("desktop static serving accepts native root/assets paths and rejects traversal and junction escapes", async (t) => {
  const { server } = await setup(t),
    get = (path) => fetch(server.origin + path);
  const index = await get("/");
  assert.equal(index.status, 200);
  assert.match(await index.text(), /Desktop smoke/);
  const asset = await get("/assets/app.js");
  assert.equal(asset.status, 200);
  assert.equal(asset.headers.get("content-type"), "text/javascript");
  assert.match(await asset.text(), /localDesktop/);
  for (const path of [
    "/escape/secret.js",
    "/%2e%2e%2fGUI%20with%20spaces-escape/secret.js",
  ]) {
    const response = await get(path);
    assert.equal(response.status, 403);
    assert.equal((await response.json()).error.code, "AUTH");
  }
});
test("desktop registers the native executable, rejects relative paths, orients toward either human and reconnects its launcher", async (t) => {
  const { root, assets, directory, server } = await setup(t);
  assert(isAbsolute(process.execPath));
  const script = join(root, "UCI fixture with spaces.mjs");
  await writeFile(
    script,
    `import {createInterface} from 'node:readline';
createInterface({input:process.stdin}).on('line',line=>{
 if(line==='uci')console.log('id name Desktop native-process smoke\\nuciok');
 if(line==='isready')console.log('readyok');
 if(line==='quit')process.exit(0);
});`,
  );
  const code = new URL(server.launchUrl()).hash.slice(8),
    headers = { Origin: server.origin, "Content-Type": "application/json" };
  const auth = await fetch(server.origin + "/api/v1/bootstrap", {
    method: "POST",
    headers,
    body: JSON.stringify({ code }),
  });
  assert.equal(auth.status, 200);
  const { csrf } = await auth.json();
  headers.Cookie = auth.headers.get("set-cookie").split(";")[0];
  headers["X-Chesslab-Csrf"] = csrf;
  const command = async (operation, payload) => {
    const snapshot = await (
      await fetch(server.origin + "/api/v1/snapshot", { headers })
    ).json();
    const response = await fetch(server.origin + "/api/v1/command", {
      method: "POST",
      headers,
      body: JSON.stringify({
        version: 1,
        commandId: randomUUID(),
        operation,
        payload,
        expectedRevision: snapshot.revision,
      }),
    });
    assert.equal(response.status, 200);
    return response.json();
  };
  const rejected = await command("engine.register", {
    trust: true,
    label: "relative",
    config: { path: "engine.exe" },
  });
  assert.equal(rejected.accepted, false);
  assert.match(rejected.error.message, /absolute local path/);
  const registered = await command("engine.register", {
    trust: true,
    label: "Desktop smoke",
    config: { path: process.execPath, args: [script] },
  });
  assert.equal(registered.accepted, true, JSON.stringify(registered.error));
  const id = registered.result.id,
    profile = server.workspace.snapshot().profiles[id];
  assert(isAbsolute(profile.identity.path));
  assert.match(profile.identity.sha256, /^[a-f0-9]{64}$/);
  assert(profile.identity.id.includes("name Desktop native-process smoke"));
  assert(
    (await command("game.create", { players: { w: id, b: "human" } })).accepted,
  );
  assert.equal(server.workspace.snapshot().settings.flipped, true);
  assert((await command("workspace.settings", { flipped: false })).accepted);
  assert.equal(server.workspace.snapshot().settings.flipped, false);
  assert(
    (await command("game.create", { players: { w: id, b: "human" } })).accepted,
  );
  assert.equal(server.workspace.snapshot().settings.flipped, true);
  assert(
    (await command("game.create", { players: { w: "human", b: id } })).accepted,
  );
  assert.equal(server.workspace.snapshot().settings.flipped, false);
  assert((await command("workspace.settings", { flipped: true })).accepted);
  assert.equal(server.workspace.snapshot().settings.flipped, true);
  const revision = server.workspace.snapshot().revision;
  assert.deepEqual(
    await launch(resolve(directory), assets, { openBrowser: false }),
    { origin: server.origin },
  );
  assert.equal(server.workspace.snapshot().revision, revision);
  if (process.env.CHESS_LAB_GUI_ASSETS) {
    const built = await startServer({
      directory: join(root, "built workspace"),
      assets: resolve(process.env.CHESS_LAB_GUI_ASSETS),
    });
    try {
      const html = await (await fetch(built.origin)).text();
      assert.match(html, /id="root"/);
      const src = /src="([^"]+\.js)"/.exec(html)?.[1];
      assert(src);
      assert.equal((await fetch(built.origin + src)).status, 200);
    } finally {
      await built.close();
    }
  }
});
