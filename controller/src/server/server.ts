import {
  createServer,
  type IncomingMessage,
  type ServerResponse,
} from "node:http";
import { createServer as ipcServer } from "node:net";
import { randomBytes } from "node:crypto";
import { readFile, realpath, chmod, unlink } from "node:fs/promises";
import { resolve, join, extname } from "node:path";
import { WebSocketServer, WebSocket } from "ws";
import { Workspace } from "../workspace/service.js";
import { Fault, object, text, type Event } from "../workspace/protocol.js";
import { writeRecord, digest } from "../store.js";
export async function startServer(options: {
  directory: string;
  assets: string;
  takeover?: boolean;
  port?: number;
}) {
  const workspace = await Workspace.open(options.directory, {
    takeover: options.takeover ?? false,
  });
  const sessions = new Map<string, { csrf: string }>(),
    bootstrap = new Map<string, number>(),
    limits = new Map<string, { at: number; count: number }>();
  const cookieName = "chesslab_" + randomBytes(12).toString("hex");
  let origin = "";
  let shutdown: Promise<void> | undefined;
  const secret = () => randomBytes(32).toString("base64url");
  const credentials = (req: IncomingMessage) => {
    const sid = (req.headers.cookie ?? "")
      .split(";")
      .map((s) => s.trim())
      .find((s) => s.startsWith(cookieName + "="))
      ?.slice(cookieName.length + 1);
    return sid ? sessions.get(sid) : undefined;
  };
  const allowed = (req: IncomingMessage) =>
    req.headers.host === new URL(origin).host &&
    (!req.headers.origin || req.headers.origin === origin) &&
    !["cross-site", "same-site"].includes(
      req.headers["sec-fetch-site"] as string,
    );
  const json = (res: ServerResponse, status: number, data: unknown) => {
    res.writeHead(status, {
      "Content-Type": "application/json",
      "Cache-Control": "no-store",
    });
    res.end(JSON.stringify(data));
  };
  const body = async (req: IncomingMessage) => {
    if (req.headers["content-type"] !== "application/json")
      throw new Fault("CONTENT_TYPE", "application/json required");
    let bytes = 0;
    const chunks: Buffer[] = [];
    for await (const chunk of req) {
      bytes += chunk.length;
      if (bytes > 262144) throw new Fault("LIMIT", "Request exceeds 256 KiB");
      chunks.push(chunk);
    }
    return JSON.parse(Buffer.concat(chunks).toString("utf8")) as unknown;
  };
  const server = createServer(async (req, res) => {
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("Referrer-Policy", "no-referrer");
    res.setHeader(
      "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
    );
    try {
      if (!allowed(req)) throw new Fault("ORIGIN", "Host or Origin rejected");
      const url = new URL(req.url ?? "/", origin);
      if (url.search)
        throw new Fault(
          "VALIDATION",
          "Query credentials and generic path RPC are not supported",
        );
      const session = credentials(req),
        at = performance.now();
      if (url.pathname === "/api/v1/bootstrap" && req.method === "POST") {
        if (req.headers.origin !== origin)
          throw new Fault("ORIGIN", "Bootstrap requires same Origin");
        const key = req.socket.remoteAddress ?? "";
        const rate = limits.get(key);
        if (rate && at - rate.at < 60000 && rate.count >= 20)
          throw new Fault("LIMIT", "Bootstrap rate limit");
        limits.set(key, {
          at: rate && at - rate.at < 60000 ? rate.at : at,
          count: (rate && at - rate.at < 60000 ? rate.count : 0) + 1,
        });
        const b = object(await body(req)),
          code = text(b.code, 100);
        const expiry = bootstrap.get(code);
        bootstrap.delete(code);
        if (!expiry || expiry < at)
          throw new Fault("AUTH", "Invalid or expired one-time launch code");
        if (sessions.size >= 64) throw new Fault("LIMIT", "Session limit");
        const sid = secret(),
          csrf = secret();
        sessions.set(sid, { csrf });
        res.setHeader(
          "Set-Cookie",
          `${cookieName}=${sid}; HttpOnly; SameSite=Strict; Path=/`,
        );
        json(res, 200, { version: 1, csrf });
        return;
      }
      if (url.pathname.startsWith("/api/")) {
        if (!session)
          throw new Fault("AUTH", "Launch the workspace to authenticate");
        if (req.method === "GET" && url.pathname === "/api/v1/session") {
          json(res, 200, { version: 1, csrf: session.csrf });
          return;
        }
        if (req.method === "GET" && url.pathname === "/api/v1/snapshot") {
          json(res, 200, workspace.snapshot());
          return;
        }
        if (req.method === "POST") {
          if (
            req.headers.origin !== origin ||
            req.headers["x-chesslab-csrf"] !== session.csrf
          )
            throw new Fault("CSRF", "CSRF validation failed");
          const data = await body(req),
            receivedAt = performance.now();
          if (url.pathname === "/api/v1/command") {
            json(res, 200, await workspace.dispatch(data, receivedAt));
            return;
          }
          if (url.pathname === "/api/v1/command-status") {
            json(
              res,
              200,
              workspace.commandStatus(text(object(data).commandId, 100)),
            );
            return;
          }
          if (url.pathname === "/api/v1/position") {
            json(res, 200, workspace.position(data));
            return;
          }
          if (url.pathname === "/api/v1/export") {
            const b = object(data);
            json(
              res,
              200,
              await workspace.export(text(b.kind, 20), text(b.id, 100)),
            );
            return;
          }
          if (url.pathname === "/api/v1/quit") {
            const b = object(data);
            if (b.action === "continue") {
              json(res, 200, { continuing: true });
              return;
            }
            if (b.action !== "terminate")
              throw new Fault("VALIDATION", "Choose terminate or continue");
            json(res, 202, { status: "terminating_at_safe_boundary" });
            void close().catch((e) => workspace.emit("errorNotice", e));
            return;
          }
        }
        throw new Fault("NOT_FOUND", "Unknown API route");
      }
      if (req.method !== "GET") throw new Fault("NOT_FOUND", "Unknown route");
      const root = await realpath(options.assets),
        path = decodeURIComponent(url.pathname);
      if (path.includes("\0") || path.includes("\\"))
        throw new Fault("VALIDATION", "Invalid static path");
      const candidate = resolve(
        root,
        "." + (path === "/" ? "/index.html" : path),
      );
      if (!candidate.startsWith(root + "/"))
        throw new Fault("AUTH", "Static path outside GUI");
      const actual = await realpath(candidate);
      if (!actual.startsWith(root + "/"))
        throw new Fault("AUTH", "Static symlink outside GUI");
      const content = await readFile(actual);
      res.setHeader(
        "Content-Type",
        (
          {
            ".html": "text/html",
            ".js": "text/javascript",
            ".css": "text/css",
            ".svg": "image/svg+xml",
            ".json": "application/json",
          } as Record<string, string>
        )[extname(actual)] ?? "application/octet-stream",
      );
      res.end(content);
    } catch (e) {
      const code = e instanceof Fault ? e.code : "FAILURE";
      json(
        res,
        code === "AUTH" || code === "CSRF"
          ? 403
          : code === "NOT_FOUND"
            ? 404
            : 400,
        {
          error: {
            code,
            message: e instanceof Error ? e.message : "Request failed",
            severity: "failure",
          },
        },
      );
    }
  });
  server.maxConnections = 64;
  server.requestTimeout = 10000;
  server.headersTimeout = 5000;
  const ws = new WebSocketServer({
    noServer: true,
    maxPayload: 1024,
    handleProtocols: () => "chesslab.ws.v1",
  });
  server.on("upgrade", (req, socket, head) => {
    const session = credentials(req),
      protocols = (req.headers["sec-websocket-protocol"] ?? "")
        .split(",")
        .map((s) => s.trim());
    if (
      !allowed(req) ||
      req.headers.origin !== origin ||
      req.url !== "/api/v1/events" ||
      !session ||
      !protocols.includes("chesslab.ws.v1") ||
      !protocols.includes(session.csrf)
    ) {
      socket.write("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
      socket.destroy();
      return;
    }
    ws.handleUpgrade(req, socket, head, (client) => {
      client.on("message", () => client.close(1008, "Read-only event channel"));
      const s = workspace.snapshot();
      client.send(
        JSON.stringify({
          version: 1,
          type: "snapshot",
          revision: s.revision,
          snapshot: s,
        }),
      );
    });
  });
  const listener = (event: Event) => {
    const value = JSON.stringify(event);
    for (const client of ws.clients)
      if (client.readyState === WebSocket.OPEN) {
        if (client.bufferedAmount > 1024 * 1024)
          client.close(1013, "Refresh snapshot");
        else client.send(value);
      }
  };
  workspace.on("event", listener);
  await new Promise<void>((resolve) =>
    server.listen(options.port ?? 0, "127.0.0.1", resolve),
  );
  const address = server.address();
  if (!address || typeof address === "string")
    throw Error("No loopback address");
  origin = `http://127.0.0.1:${address.port}`;
  function launchUrl() {
    for (const [k, v] of bootstrap)
      if (v < performance.now()) bootstrap.delete(k);
    if (bootstrap.size >= 64) throw new Fault("LIMIT", "Launch limit");
    const code = secret();
    bootstrap.set(code, performance.now() + 60000);
    return origin + "/#launch=" + code;
  }
  const ipcPath =
    process.platform === "win32"
      ? "\\\\.\\pipe\\chesslab-" + digest(options.directory).slice(0, 24)
      : join(options.directory, "launch.sock");
  const ipc = ipcServer((socket) => {
    socket.setTimeout(1000, () => socket.destroy());
    socket.end(JSON.stringify({ url: launchUrl() }) + "\n");
  });
  try {
    if (options.takeover && process.platform !== "win32")
      await unlink(ipcPath).catch(() => {});
    await new Promise<void>((resolve, reject) => {
      ipc.once("error", reject);
      ipc.listen(ipcPath, resolve);
    });
    if (process.platform !== "win32") await chmod(ipcPath, 0o600);
    await writeRecord(
      join(options.directory, "runtime.json"),
      { pid: process.pid, origin, ipcPath },
      false,
    );
  } catch (e) {
    await workspace.close();
    server.close();
    throw e;
  }
  function close(): Promise<void> {
    return (shutdown ??= finishClose());
  }
  async function finishClose() {
    try {
      await workspace.close();
    } finally {
      workspace.off("event", listener);
      for (const client of ws.clients) {
        client.close(1001, "Controller terminated");
        const deadline = setTimeout(() => client.terminate(), 1000);
        deadline.unref();
        client.once("close", () => clearTimeout(deadline));
      }
      ws.close();
      ipc.close();
      const stopped = new Promise<void>((resolve) =>
        server.close(() => resolve()),
      );
      server.closeAllConnections();
      await stopped;
      sessions.clear();
      bootstrap.clear();
      await unlink(join(options.directory, "runtime.json")).catch(() => {});
    }
  }
  return { workspace, server, origin, launchUrl, close };
}
