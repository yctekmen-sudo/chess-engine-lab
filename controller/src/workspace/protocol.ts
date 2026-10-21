import type { GameCheckpoint } from "../game.js";
import type { EngineConfig, EngineIdentity, SearchLimit } from "../engine.js";
import type { RecordState } from "../rules.js";
export const VERSION = 1;
export type Problem = {
  code: string;
  message: string;
  severity: "information" | "warning" | "failure";
};
export type Command = {
  version: 1;
  commandId: string;
  operation: string;
  objectId?: string;
  expectedRevision: number;
  expectedGeneration?: number;
  payload: Record<string, unknown>;
};
export type Ack = {
  version: 1;
  commandId: string;
  accepted: boolean;
  revision: number;
  result?: unknown;
  error?: Problem;
};
export type Casual = {
  id: string;
  generation: number;
  checkpoint: GameCheckpoint;
  players: { w: string; b: string };
  audit: unknown[];
  history: GameCheckpoint[];
};
export type Node = {
  id: string;
  parent: string | null;
  move: string | null;
  record: RecordState;
  comment: string;
  nags: string[];
};
export type AnalysisDocument = {
  headers: Record<string, string>;
  id: string;
  cursor: string;
  nodes: Record<string, Node>;
};
export type Profile = {
  id: string;
  label: string;
  generation: number;
  config: EngineConfig;
  identity: EngineIdentity;
  artifacts: Record<string, string>;
};
export type Job = {
  id: string;
  kind: "analysis" | "play" | "experiment";
  status: string;
  session: string;
  objectId: string;
  root: string;
  generation: number;
  identity?: EngineIdentity;
  trusted?: boolean;
  result?: unknown;
  error?: Problem;
};
export type ExperimentRef = {
  id: string;
  status: string;
  summary?: unknown;
  config: unknown;
  share: boolean;
  artifacts: Record<string, string>;
};
export type Snapshot = {
  version: 1;
  id: string;
  revision: number;
  activeGameId: string | null;
  games: Record<string, Casual>;
  analyses: Record<string, AnalysisDocument>;
  profiles: Record<string, Profile>;
  experiments: Record<string, ExperimentRef>;
  jobs: Record<string, Job>;
  settings: Record<string, unknown>;
  diagnostics: Problem[];
};
export type Progress = {
  version: 1;
  type: "progress";
  session: string;
  objectId: string;
  root: string;
  generation: number;
  sequence: number;
  provisional: true;
  line: string;
};
export type Event =
  | {
      version: 1;
      type: "snapshot" | "state";
      revision: number;
      snapshot: Snapshot;
    }
  | Progress;
export type Stored = {
  snapshot: Snapshot;
  commands: Record<string, { hash: string; ack: Ack }>;
};
export type AnalysisRequest = {
  profile: string;
  limit: SearchLimit;
  multiPV: number;
  allowCompetition: boolean;
};
export class Fault extends Error {
  constructor(
    readonly code: string,
    message: string,
  ) {
    super(message);
  }
}
export function object(v: unknown): Record<string, unknown> {
  if (!v || typeof v !== "object" || Array.isArray(v))
    throw new Fault("VALIDATION", "Expected an object");
  return v as Record<string, unknown>;
}
export function text(v: unknown, max = 65536): string {
  if (typeof v !== "string" || v.length > max)
    throw new Fault("VALIDATION", "Expected bounded text");
  return v;
}
export function integer(v: unknown, min = 0, max = 2147483647): number {
  if (typeof v !== "number" || !Number.isSafeInteger(v) || v < min || v > max)
    throw new Fault("VALIDATION", "Expected bounded integer");
  return v;
}
export function identifier(v: unknown): string {
  const s = text(v, 100);
  if (!/^[a-zA-Z0-9_-]+$/.test(s))
    throw new Fault("VALIDATION", "Invalid identifier");
  return s;
}
export function command(v: unknown): Command {
  const o = object(v);
  if (
    Object.keys(o).some(
      (k) =>
        ![
          "version",
          "commandId",
          "operation",
          "objectId",
          "expectedRevision",
          "expectedGeneration",
          "payload",
        ].includes(k),
    )
  )
    throw new Fault("VERSION", "Unknown command field");
  if (o.version !== 1)
    throw new Fault("VERSION", "Protocol version 1 required");
  return {
    version: 1,
    commandId: identifier(o.commandId),
    operation: text(o.operation, 80),
    expectedRevision: integer(o.expectedRevision),
    payload: structuredClone(object(o.payload)),
    ...(o.objectId !== undefined ? { objectId: identifier(o.objectId) } : {}),
    ...(o.expectedGeneration !== undefined
      ? { expectedGeneration: integer(o.expectedGeneration) }
      : {}),
  };
}
