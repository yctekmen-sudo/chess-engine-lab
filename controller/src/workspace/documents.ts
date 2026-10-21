import { randomUUID } from "node:crypto";
import { Chess } from "chess.js";
import {
  FideRules,
  DEFAULT_POSITION,
  uci,
  parseMove,
  type RecordState,
} from "../rules.js";
import { Fault, type AnalysisDocument, type Node } from "./protocol.js";
export function document(record: RecordState): AnalysisDocument {
  const id = randomUUID(),
    root = randomUUID();
  new FideRules(record);
  return {
    id,
    headers: {},
    cursor: root,
    nodes: {
      [root]: {
        id: root,
        parent: null,
        move: null,
        record,
        comment: "",
        nags: [],
      },
    },
  };
}
export function addNode(
  doc: AnalysisDocument,
  parent: string,
  move: string,
  comment = "",
): Node {
  const p = doc.nodes[parent];
  if (!p) throw new Fault("NOT_FOUND", "Variation parent missing");
  if (Object.keys(doc.nodes).length >= 4096)
    throw new Fault("LIMIT", "Variation limit");
  const r = new FideRules(p.record);
  r.play(move);
  const id = randomUUID();
  const n = { id, parent, move, record: r.record(), comment, nags: [] };
  doc.nodes[id] = n;
  doc.cursor = id;
  return n;
}
export function importPgn(input: string): AnalysisDocument {
  if (input.length > 262144) throw new Fault("LIMIT", "PGN exceeds 256 KiB");
  const started = performance.now();
  const headers: Record<string, string> = {};
  const body = input.replace(
    /\[(\w+)\s+"((?:\\.|[^"\\])*)"\]/g,
    (_, key, value) => {
      headers[key] = value.replace(/\\(["\\])/g, "$1");
      return "";
    },
  );
  const fen = headers.FEN ?? DEFAULT_POSITION;
  const d = document({ initialFen: fen, moves: [], complete: !headers.FEN });
  d.headers = headers;
  if (/[{}]/.test(body.replace(/\{[^}]*\}|;[^\n]*/g, "")))
    throw new Fault("VALIDATION", "Unclosed PGN comment");
  const tokens = body.match(/\{[^}]*\}|;[^\n]*|\(|\)|\$\d+|[^\s(){}]+/g) ?? [];
  if (tokens.length > 12000) throw new Fault("LIMIT", "Too many PGN tokens");
  let current = d.cursor;
  const stack: string[] = [];
  for (let token of tokens) {
    if (performance.now() - started > 2000)
      throw new Fault("LIMIT", "PGN parser time budget exceeded");
    if (token === "(") {
      if (stack.length >= 16) throw new Fault("LIMIT", "Variation depth limit");
      stack.push(current);
      current = d.nodes[current]!.parent ?? current;
      continue;
    }
    if (token === ")") {
      if (!stack.length)
        throw new Fault("VALIDATION", "Unmatched PGN variation");
      current = stack.pop()!;
      continue;
    }
    if (token.startsWith("{") || token.startsWith(";")) {
      d.nodes[current]!.comment += token.startsWith("{")
        ? token.slice(1, -1)
        : token.slice(1);
      continue;
    }
    if (token.startsWith("$")) {
      d.nodes[current]!.nags.push(token);
      continue;
    }
    token = token.replace(/^\d+\.(?:\.\.)?/, "");
    if (
      !token ||
      /^\.+$/.test(token) ||
      ["1-0", "0-1", "1/2-1/2", "*"].includes(token)
    )
      continue;
    const b = new Chess(new FideRules(d.nodes[current]!.record).fen());
    const glyph = /([!?]{1,2})$/.exec(token)?.[1];
    const move = b.move(glyph ? token.slice(0, -glyph.length) : token);
    current = addNode(d, current, uci(move)).id;
    if (glyph)
      d.nodes[current]!.nags.push(
        (
          {
            "!": "$1",
            "?": "$2",
            "!!": "$3",
            "??": "$4",
            "!?": "$5",
            "?!": "$6",
          } as Record<string, string>
        )[glyph]!,
      );
  }
  if (stack.length) throw new Fault("VALIDATION", "Unclosed PGN variation");
  d.cursor = current;
  return d;
}
export function exportPgn(doc: AnalysisDocument): string {
  const root = Object.values(doc.nodes).find((n) => n.parent === null)!;
  const headers = Object.entries(doc.headers ?? {})
    .filter(([key]) => !["Result", "FEN", "SetUp"].includes(key))
    .map(
      ([key, value]) =>
        "[" +
        key +
        ' "' +
        value.replace(/\\/g, "\\\\").replace(/"/g, '\\"') +
        '"]\n',
    )
    .join("");
  const head =
    headers +
    (root.record.initialFen === DEFAULT_POSITION
      ? ""
      : '[SetUp "1"]\n[FEN "' + root.record.initialFen + '"]\n');
  const children = (id: string) =>
    Object.values(doc.nodes).filter((n) => n.parent === id);
  function line(n: Node): string {
    const p = doc.nodes[n.parent!]!,
      b = new Chess(new FideRules(p.record).fen()),
      fields = b.fen().split(" "),
      prefix = fields[5] + (b.turn() === "w" ? "." : "...");
    const san = b.move(parseMove(n.move!)).san;
    return (
      prefix +
      " " +
      san +
      (n.nags.length ? " " + n.nags.join(" ") : "") +
      (n.comment ? " {" + n.comment.replace(/[{}]/g, "") + "}" : "")
    );
  }
  function continuation(id: string): string {
    const cs = children(id);
    if (!cs.length) return "";
    return (
      line(cs[0]!) +
      cs
        .slice(1)
        .map((n) => " (" + line(n) + " " + continuation(n.id) + ")")
        .join("") +
      " " +
      continuation(cs[0]!.id)
    );
  }
  const board = new Chess(root.record.initialFen),
    prefix: string[] = [];
  for (const move of root.record.moves) {
    const fields = board.fen().split(" "),
      number = fields[5] + (board.turn() === "w" ? "." : "...");
    prefix.push(number + " " + board.move(parseMove(move)).san);
  }
  const annotation =
    (root.nags.length ? " " + root.nags.join(" ") : "") +
    (root.comment ? " {" + root.comment.replace(/[{}]/g, "") + "}" : "");
  return (
    head +
    '[Result "*"]\n\n' +
    prefix.join(" ") +
    annotation +
    " " +
    continuation(root.id) +
    " *"
  );
}
