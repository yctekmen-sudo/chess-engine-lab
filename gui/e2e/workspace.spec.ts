import { test, expect } from "@playwright/test";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
// Compiled controller is the real server used in production, no HTTP mock.
const serverModule = await import("../../controller/dist/server/server.js");
let directory: string,
  server: Awaited<ReturnType<typeof serverModule.startServer>>;
test.beforeEach(async () => {
  directory = await mkdtemp(join(tmpdir(), "m7-browser-"));
  server = await serverModule.startServer({
    directory,
    assets: resolve("dist"),
  });
});
test.afterEach(async () => {
  await server.close();
  await rm(directory, { recursive: true, force: true });
});
test("real browser play, engine registration, analysis, imports, refresh and Quit continue", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto(server.launchUrl());
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByRole("button", { name: "Create game", exact: true }).click();
  await expect(page.getByRole("button", { name: "Resume play" })).toBeVisible();
  await page.getByRole("button", { name: "Resume play" }).click();
  await page.getByLabel("Coordinate move").fill("e2e4");
  await page.getByRole("button", { name: "Play move", exact: true }).click();
  await expect(page.locator(".history")).toContainText("e2e4");
  await page.getByRole("button", { name: "Take back", exact: true }).click();
  await expect(page.locator(".history")).not.toContainText("e2e4");
  await page.getByRole("button", { name: /Engines/ }).click();
  await page
    .getByLabel("Absolute executable path")
    .fill(resolve("../build/native-debug/chess_lab_uci"));
  await page.getByLabel("Extension provenance").selectOption("chesslab");
  await page.getByLabel("I trust this executable").check();
  await page.getByRole("button", { name: "Inspect and save profile" }).click();
  await expect(page.getByText("Generation 1", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: /Play/ }).first().click();
  await page.getByLabel("Black player").selectOption({ label: "Chess Lab" });
  await page.getByRole("button", { name: "Create game", exact: true }).click();
  await page.getByRole("button", { name: "Resume play" }).click();
  await page.getByLabel("Coordinate move").fill("e2e4");
  await page.getByRole("button", { name: "Play move", exact: true }).click();
  await expect
    .poll(
      () =>
        server.workspace.snapshot().games[
          server.workspace.snapshot().activeGameId!
        ].checkpoint.record.moves.length,
      { timeout: 15000 },
    )
    .toBe(2);
  await page
    .getByRole("button", { name: /Analyze/ })
    .first()
    .click();
  await page.getByRole("button", { name: "New analysis", exact: true }).click();
  await page
    .getByLabel("Registered engine")
    .selectOption({ label: "Chess Lab" });
  await page
    .getByRole("button", { name: "Start analysis", exact: true })
    .click();
  await expect(page.locator(".progress")).toContainText("completed", {
    timeout: 15000,
  });
  await expect(page.locator(".progress")).toContainText("Provisional");
  await page.getByLabel("PGN with variations").fill("1. e4 (1. d4 d5) e5 *");
  await page.getByRole("button", { name: "Import PGN", exact: true }).click();
  await expect(page.locator(".history")).toContainText("e7e5");
  await expect(page.getByLabel("Variation cursor")).toBeVisible();
  await page.reload();
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByRole("button", { name: "Quit workspace" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page.getByRole("button", { name: "Continue in background" }).click();
  await expect(page.getByRole("dialog")).not.toBeVisible();
  expect(errors).toEqual([]);
  await expect(
    page.locator('[data-square="e4"] [data-piece="wP"]'),
  ).toBeVisible();
  await page.screenshot({
    path: "../work/m7-gui-playwright.png",
    fullPage: true,
  });
});
test("two tabs share state and restart restores a paused game", async ({
  page,
  context,
}) => {
  await page.goto(server.launchUrl());
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByRole("button", { name: "Create game", exact: true }).click();
  await page.getByRole("button", { name: "Resume play" }).click();
  const tab = await context.newPage();
  await tab.goto(server.origin);
  await expect(tab.getByText("Local controller connected")).toBeVisible();
  await tab.getByLabel("Coordinate move").fill("d2d4");
  await tab.getByRole("button", { name: "Play move", exact: true }).click();
  await expect(page.locator(".history")).toContainText("d2d4");
  await tab.close();
  await page.close();
  expect(
    server.workspace.snapshot().games[server.workspace.snapshot().activeGameId!]
      .checkpoint.record.moves,
  ).toEqual(["d2d4"]);
  await server.close();
  server = await serverModule.startServer({
    directory,
    assets: resolve("dist"),
  });
  const restored = await context.newPage();
  await restored.goto(server.launchUrl());
  await expect(
    restored.getByRole("button", { name: "Resume play" }),
  ).toBeVisible();
  await expect(restored.locator(".history")).toContainText("d2d4");
});
test("promotion, FEN export and real paired experiment workspace", async ({
  page,
}) => {
  await page.goto(server.launchUrl());
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByLabel("Starting FEN").fill("4k3/P7/8/8/8/8/7p/4K3 w - - 0 1");
  await page.getByRole("button", { name: "Create game", exact: true }).click();
  await page.getByRole("button", { name: "Resume play" }).click();
  // Board click promotion uses the presentation adapter and explicit dialog.
  await page.locator('[data-square="a7"]').click();
  await page.locator('[data-square="a8"]').click();
  await expect(
    page.getByRole("dialog", { name: "Choose promotion" }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Knight", exact: true }).click();
  await expect(page.locator(".history")).toContainText("a7a8n");
  const fenDownload = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export FEN", exact: true }).click();
  expect((await fenDownload).suggestedFilename()).toBe("chess-lab.fen");
  await page.getByRole("button", { name: /Engines/ }).click();
  await page
    .getByLabel("Absolute executable path")
    .fill(resolve("../build/native-debug/chess_lab_uci"));
  await page.getByLabel("I trust this executable").check();
  await page.getByRole("button", { name: "Inspect and save profile" }).click();
  await expect(page.getByText("Generation 1", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: /Experiments/ }).click();
  await page.getByLabel("Engine A").selectOption({ label: "Chess Lab" });
  await page.getByLabel("Engine B").selectOption({ label: "Chess Lab" });
  await page.getByLabel("Match conditions JSON").fill(
    JSON.stringify({
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
    }),
  );
  await page.getByRole("button", { name: "Create paired experiment" }).click();
  await page.getByRole("button", { name: "Start / resume" }).click();
  await expect(page.getByText("completed", { exact: true })).toBeVisible({
    timeout: 20000,
  });
  await page.getByRole("button", { name: "Review games" }).click();
  await expect(
    page.getByRole("button", { name: "Open played game in Analyze" }),
  ).toHaveCount(2);
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export PGN + metadata" }).click();
  expect((await download).suggestedFilename()).toBe("chess-lab.pgn");
});
test("websocket disconnect disables mutations, reconnects, and Quit terminates safely", async ({
  page,
}) => {
  await page.goto(server.launchUrl());
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByRole("button", { name: "Create game", exact: true }).click();
  await page.getByRole("button", { name: "Resume play" }).click();
  await expect(page.getByRole("button", { name: "Pause game" })).toBeVisible();
  await page.context().setOffline(true);
  await expect(page.getByText("Disconnected — reconnecting")).toBeVisible({
    timeout: 10000,
  });
  await expect(page.getByRole("button", { name: "Pause game" })).toBeDisabled();
  await page.context().setOffline(false);
  await expect(page.getByText("Local controller connected")).toBeVisible({
    timeout: 15000,
  });
  await page.getByLabel("Coordinate move").fill("e2e4");
  await page.getByRole("button", { name: "Play move", exact: true }).click();
  await expect(page.locator(".history")).toContainText("e2e4");
  await page.getByRole("button", { name: "Quit workspace" }).click();
  await page.getByRole("button", { name: "Terminate controller" }).click();
  await expect.poll(() => server.server.listening).toBe(false);
  expect(
    server.workspace.snapshot().games[server.workspace.snapshot().activeGameId!]
      .checkpoint.paused,
  ).toBe(true);
});

test("new human-engine games face either human color and preserve manual flip", async ({
  page,
}) => {
  await page.goto(server.launchUrl());
  await expect(page.getByText("Local controller connected")).toBeVisible();
  await page.getByRole("button", { name: /Engines/ }).click();
  await page
    .getByLabel("Absolute executable path")
    .fill(resolve("../build/native-debug/chess_lab_uci"));
  await page.getByLabel("I trust this executable").check();
  await page.getByRole("button", { name: "Inspect and save profile" }).click();
  await expect(page.getByText("Generation 1", { exact: true })).toBeVisible();
  await page.getByRole("button", { name: /Play/ }).first().click();
  const firstSquare = page.locator("[data-square]").first(),
    create = page.getByRole("button", { name: "Create game", exact: true }),
    flip = page.getByRole("button", { name: "Flip board" });
  await page.getByLabel("White player").selectOption({ label: "Chess Lab" });
  await create.click();
  await expect(firstSquare).toHaveAttribute("data-square", "h1");
  await flip.click();
  await expect(firstSquare).toHaveAttribute("data-square", "a8");
  await create.click();
  await expect(firstSquare).toHaveAttribute("data-square", "h1");
  await page.getByLabel("White player").selectOption("human");
  await page.getByLabel("Black player").selectOption({ label: "Chess Lab" });
  await create.click();
  await expect(firstSquare).toHaveAttribute("data-square", "a8");
  await flip.click();
  await expect(firstSquare).toHaveAttribute("data-square", "h1");
  await create.click();
  await expect(firstSquare).toHaveAttribute("data-square", "a8");
});
