import test from "node:test";
import assert from "node:assert/strict";
import { execFile, spawn } from "node:child_process";
import { copyFile, mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { basename, dirname, join } from "node:path";
import { createInterface } from "node:readline";
import { promisify } from "node:util";
import { nativeEngine, sharedLibrary } from "./native-paths.mjs";
async function transcript(t, kind, fen, descriptor) {
  const child = spawn(
    nativeEngine,
    [sharedLibrary(`m7_${kind}_evaluator`, process.env.CHESS_LAB_BUILD)],
    {
      env: {
        ...process.env,
        ...(descriptor ? { CHESS_LAB_TEST_CALIBRATION: descriptor } : {}),
      },
      stdio: "pipe",
    },
  );
  const lines = [];
  let finish;
  const complete = new Promise((r, j) => {
    finish = r;
    child.on("error", j);
    child.on("exit", (code) => {
      if (code) j(Error("exit " + code));
    });
  });
  const timer = setTimeout(() => {
    child.kill("SIGKILL");
    finish();
  }, 5000);
  createInterface({ input: child.stdout }).on("line", (s) => {
    lines.push(s);
    if (s === "uciok") child.stdin.write(`position fen ${fen}\ngo depth 1\n`);
    if (s.startsWith("bestmove ")) {
      finish();
      child.stdin.write("quit\n");
    }
  });
  t.after(() => {
    clearTimeout(timer);
    child.kill("SIGKILL");
  });
  child.stdin.write("uci\n");
  await complete;
  assert(lines.some((l) => l.startsWith("bestmove ")));
  return lines;
}
test("native frozen v1.0 module stays raw-only after minor negotiation fallback", async (t) => {
  const lines = await transcript(t, "legacy", "7k/8/8/8/8/8/q7/4K3 w - - 0 1");
  assert(lines.some((l) => l.includes("score raw white")));
  assert(!lines.some((l) => l.includes("score cp")));
});
test("native calibrated cp has correct White/Black polarity and raw provenance", async (t) => {
  for (const [fen, cp] of [
    ["7k/8/8/8/8/8/q7/4K3 w - - 0 1", "-900"],
    ["7k/8/8/8/8/8/Q7/4K3 b - - 0 1", "-900"],
  ]) {
    const lines = await transcript(t, "calibrated", fen);
    assert(lines.some((l) => l.includes(" score cp " + cp)));
    assert(lines.some((l) => l.includes("score raw white")));
    assert(lines.some((l) => /evaluator calibration [a-f0-9]{64}/.test(l)));
  }
});
test("malformed optional calibration downgrades without evaluator failure; mate bypasses cp", async (t) => {
  const malformed = await transcript(
    t,
    "calibrated",
    "7k/8/8/8/8/8/q7/4K3 w - - 0 1",
    '{"schema_version":1,"target":"centipawns","kind":"linear","scale":-1}',
  );
  assert(malformed.some((l) => l.includes("raw_only")));
  assert(!malformed.some((l) => l.includes("score cp")));
  const mate = await transcript(
    t,
    "calibrated",
    "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1",
  );
  assert(mate.includes("info score mate -0"));
  assert(!mate.some((l) => l.includes("score cp")));
});
test("calibration fixtures honor CHESS_LAB_BUILD with UCI in a separate directory", async (t) => {
  const directory = await mkdtemp(join(tmpdir(), "m7-calibration-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const stagedEngine = join(directory, basename(nativeEngine));
  await copyFile(nativeEngine, stagedEngine);
  await promisify(execFile)(process.execPath, [
    "--test",
    "--test-isolation=none",
    "--test-name-pattern=^native frozen v1\\.0 module stays raw-only after minor negotiation fallback$",
    import.meta.filename,
  ], {
    env: {
      ...process.env,
      CHESS_LAB_BUILD: process.env.CHESS_LAB_BUILD ?? dirname(nativeEngine),
      CHESS_LAB_UCI: stagedEngine,
    },
    timeout: 15000,
  });
});
