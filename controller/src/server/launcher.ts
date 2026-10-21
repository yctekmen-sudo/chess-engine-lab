import { createConnection } from "node:net";
import { spawn } from "node:child_process";
import { resolve, join } from "node:path";
import { readRecord, exists } from "../store.js";
import { startServer } from "./server.js";
function own(server: Awaited<ReturnType<typeof startServer>>) {
  const stop = () => {
    void server
      .close()
      .catch((e) =>
        console.error(
          "Safe shutdown failed:",
          e instanceof Error ? e.message : "controller failure",
        ),
      );
  };
  process.once("SIGINT", stop);
  process.once("SIGTERM", stop);
  return server;
}
export async function launch(
  directory: string,
  assets: string,
  options: { openBrowser?: boolean; takeover?: boolean } = {},
) {
  directory = resolve(directory);
  let url: string;
  if (await exists(join(directory, "runtime.json"))) {
    const info = await readRecord<{
      pid: number;
      origin: string;
      ipcPath: string;
    }>(join(directory, "runtime.json"));
    try {
      process.kill(info.pid, 0);
      url = await new Promise<string>((resolve, reject) => {
        const socket = createConnection(info.ipcPath);
        let text = "";
        socket.setTimeout(2000, () => {
          socket.destroy();
          reject(Error("Existing controller did not respond"));
        });
        socket.on("data", (b) => {
          text += b;
          if (text.length > 2048) socket.destroy();
        });
        socket.on("end", () => {
          try {
            const result = JSON.parse(text);
            if (!result.url.startsWith(info.origin + "/#launch="))
              throw Error("Invalid controller response");
            resolve(result.url);
          } catch (e) {
            reject(e);
          }
        });
        socket.on("error", reject);
      });
    } catch (e) {
      if (!options.takeover)
        throw Error(
          "Controller locator exists; verify stale owner and use --takeover-stale-lock. " +
            String(e),
        );
      const server = own(
        await startServer({ directory, assets, takeover: true }),
      );
      url = server.launchUrl();
    }
  } else {
    const server = own(
      await startServer({
        directory,
        assets,
        takeover: options.takeover ?? false,
      }),
    );
    url = server.launchUrl();
  }
  if (options.openBrowser !== false) {
    const cmd =
        process.platform === "darwin"
          ? "open"
          : process.platform === "win32"
            ? "rundll32"
            : "xdg-open",
      args =
        process.platform === "win32"
          ? ["url.dll,FileProtocolHandler", url]
          : [url];
    const child = spawn(cmd, args, { shell: false, stdio: "ignore" });
    child.on("error", () =>
      console.error(
        "Browser could not be opened. Run chess-lab gui again after configuring your default browser.",
      ),
    );
    child.unref();
  }
  return { origin: new URL(url).origin };
}
