import type {
  Snapshot,
  Command,
  Ack,
  Event,
  Progress,
} from "../../controller/src/workspace/protocol";
export type { Snapshot, Progress };
export class Gateway {
  csrf = "";
  snapshot: Snapshot | null = null;
  connected = false;
  socket: WebSocket | null = null;
  listeners = new Set<() => void>();
  progress = new Map<string, Progress[]>();
  error = "";
  private stopped = false;
  private commands: Promise<unknown> = Promise.resolve();
  private retry: ReturnType<typeof setTimeout> | undefined;
  notify() {
    for (const l of this.listeners) l();
  }
  async post(path: string, data: unknown) {
    const response = await fetch("/api/v1/" + path, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Chesslab-Csrf": this.csrf,
      },
      body: JSON.stringify(data),
    });
    const value = await response.json();
    if (!response.ok)
      throw Error(value.error?.message ?? "Controller request failed");
    return value;
  }
  async connect() {
    try {
      const fragment = new URLSearchParams(location.hash.slice(1));
      const code = fragment.get("launch");
      history.replaceState(null, "", location.pathname);
      const auth = code
        ? await this.post("bootstrap", { code })
        : await (await fetch("/api/v1/session")).json();
      if (!auth.csrf)
        throw Error("Launch this workspace with chess-lab gui to authenticate");
      this.csrf = auth.csrf;
      await this.refresh();
      const unresolved = sessionStorage.getItem("chesslab.pending");
      if (unresolved) {
        const c = JSON.parse(unresolved);
        const ack = await this.post("command-status", {
          commandId: c.commandId,
        });
        if (ack) {
          sessionStorage.removeItem("chesslab.pending");
          if (!ack.accepted)
            this.error = ack.error?.message ?? "Previous command was rejected";
        } else
          this.error =
            "Previous command has no recorded outcome. Refresh and explicitly retry your action.";
      }
      const ws = new WebSocket(
        location.origin.replace("http", "ws") + "/api/v1/events",
        ["chesslab.ws.v1", this.csrf],
      );
      this.socket = ws;
      ws.onmessage = ({ data }) => this.receive(JSON.parse(data));
      ws.onopen = () => {
        this.connected = true;
        this.notify();
      };
      ws.onclose = () => {
        this.connected = false;
        this.notify();
        if (!this.stopped)
          this.retry = setTimeout(() => void this.connect(), 1000);
      };
      ws.onerror = () => ws.close();
    } catch (e) {
      this.error = String(e);
      this.connected = false;
      this.notify();
      if (!this.stopped)
        this.retry = setTimeout(() => void this.connect(), 1500);
    }
  }
  receive(event: Event) {
    if (event.version !== 1) {
      this.error = "Incompatible controller protocol";
      this.notify();
      return;
    }
    if (event.type === "progress") {
      const job =
        this.snapshot &&
        Object.values(this.snapshot.jobs).find(
          (j) =>
            j.session === event.session &&
            j.objectId === event.objectId &&
            j.root === event.root &&
            j.generation === event.generation,
        );
      if (!job || !["queued", "running"].includes(job.status)) return;
      const lines = this.progress.get(event.session) ?? [];
      if (lines.length && event.sequence <= lines.at(-1)!.sequence) return;
      lines.push(event);
      this.progress.set(event.session, lines.slice(-100));
      this.notify();
      return;
    }
    const old = this.snapshot?.revision ?? -1;
    if (event.revision < old) return;
    if (event.type === "state" && event.revision > old + 1) void this.refresh();
    this.snapshot = event.snapshot;
    this.notify();
  }
  async refresh() {
    try {
      const response = await fetch("/api/v1/snapshot");
      if (!response.ok)
        throw Error("Controller disconnected or session expired");
      const s = await response.json();
      if (!this.snapshot || s.revision >= this.snapshot.revision)
        this.snapshot = s;
      this.notify();
    } catch (e) {
      this.connected = false;
      this.socket?.close();
      this.notify();
      throw e;
    }
  }
  async command(
    operation: string,
    payload: Record<string, unknown> = {},
    objectId?: string,
  ) {
    const next = this.commands.then(() =>
      this.execute(operation, payload, objectId),
    );
    this.commands = next.catch(() => {});
    return next;
  }
  private async execute(
    operation: string,
    payload: Record<string, unknown> = {},
    objectId?: string,
  ) {
    if (!this.connected || !this.snapshot)
      throw Error("Reconnect before changing the workspace");
    const c: Command = {
      version: 1,
      commandId: crypto.randomUUID(),
      operation,
      payload,
      expectedRevision: this.snapshot.revision,
      ...(objectId ? { objectId } : {}),
      ...(objectId && this.snapshot.games[objectId]
        ? { expectedGeneration: this.snapshot.games[objectId].generation }
        : {}),
    };
    sessionStorage.setItem("chesslab.pending", JSON.stringify(c));
    let a: Ack;
    try {
      a = await this.post("command", c);
    } catch (e) {
      if (e instanceof TypeError) {
        this.connected = false;
        this.socket?.close();
        this.notify();
      }
      throw e;
    }
    sessionStorage.removeItem("chesslab.pending");
    await this.refresh();
    if (!a.accepted) throw Error(a.error?.message ?? "Command rejected");
    return a.result as { id?: string };
  }
  close() {
    this.stopped = true;
    if (this.retry) clearTimeout(this.retry);
    this.socket?.close();
  }
}
export function scoreLabel(
  line: string,
  provenance: "external" | "calibrated" = "external",
): string {
  const mate = /score mate (-?\d+)/.exec(line);
  if (mate) return `Mate ${mate[1]}`;
  const raw = /score raw white (\S+)/.exec(line);
  if (raw) return `Raw (White) ${raw[1]}`;
  const cp = /score cp (-?\d+)/.exec(line);
  if (cp)
    return `${provenance === "calibrated" ? "Calibrated" : "Engine-reported"} centipawns (side to move) ${cp[1]}`;
  return line;
}
