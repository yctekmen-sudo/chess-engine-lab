import { useEffect, useState, type CSSProperties } from "react";
import { createRoot } from "react-dom/client";
import { Gateway, scoreLabel, type Snapshot } from "./api";
import { Board, MoveHistory } from "./Board";
import "./style.css";
const api = new Gateway();
const tabs = ["Play", "Analyze", "Experiments", "Engines"] as const;
type Tab = (typeof tabs)[number];
export function App() {
  const [, render] = useState(0),
    [tab, setTab] = useState<Tab>("Play"),
    [selectedAnalysis, setAnalysis] = useState(""),
    [fen, setFen] = useState(""),
    [flipped, setFlipped] = useState(false),
    [error, setError] = useState(""),
    [busy, setBusy] = useState(false),
    [quit, setQuit] = useState(false),
    [position, setPosition] = useState({
      fen: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      legal: [] as string[],
      claim: false,
      source: "",
    }),
    [cursor, setCursor] = useState<number | null>(null),
    [profile, setProfile] = useState(""),
    [depth, setDepth] = useState("2"),
    [mode, setMode] = useState("depth"),
    [multi, setMulti] = useState("1"),
    [budget, setBudget] = useState("1000"),
    [textImport, setImport] = useState(""),
    [share, setShare] = useState(false),
    [rightCollapsed, setCollapsed] = useState(false),
    [panelWidth, setPanelWidth] = useState(350);
  const s = api.snapshot,
    g = s?.activeGameId ? s.games[s.activeGameId] : null,
    d = s?.analyses[selectedAnalysis],
    node = d?.nodes[d.cursor];
  const record = tab === "Analyze" ? node?.record : g?.checkpoint.record;
  const displayed =
    record && cursor !== null
      ? { ...record, moves: record.moves.slice(0, cursor) }
      : record;
  const positionReady = position.source === JSON.stringify(displayed);
  useEffect(() => {
    const change = () => render((n) => n + 1);
    api.listeners.add(change);
    void api.connect();
    const tick = setInterval(() => {
      if (api.connected) void api.refresh().catch(() => {});
    }, 1000);
    return () => {
      clearInterval(tick);
      api.listeners.delete(change);
      api.close();
    };
  }, []);
  useEffect(() => {
    if (!s) return;
    setTab((s.settings.tab as Tab) || "Play");
    setAnalysis((s.settings.activeAnalysis as string) || "");
    setFlipped(Boolean(s.settings.flipped));
    setCollapsed(Boolean(s.settings.collapsed));
    setPanelWidth(Number(s.settings.panelWidth ?? 350));
  }, [s?.id]);
  useEffect(() => {
    if (!displayed) return;
    let active = true;
    void api
      .post("position", displayed)
      .then((p) => {
        if (active) setPosition({ ...p, source: JSON.stringify(displayed) });
      })
      .catch((e) => setError(String(e)));
    return () => {
      active = false;
    };
  }, [JSON.stringify(displayed)]);
  async function action(fn: () => Promise<unknown>) {
    setBusy(true);
    setError("");
    try {
      await fn();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  }
  const command = (op: string, p: Record<string, unknown> = {}, id?: string) =>
    action(() => api.command(op, p, id));
  const layout = (values: Record<string, unknown>) =>
    void api
      .command("workspace.settings", values)
      .catch((e) => setError(String(e)));
  const switchTab = (t: Tab) => {
    setTab(t);
    setCursor(null);
    layout({ tab: t });
  };
  const profiles = Object.values(s?.profiles ?? {}),
    jobs = Object.values(s?.jobs ?? {}),
    live = jobs.filter((j) =>
      ["queued", "running", "stopping"].includes(j.status),
    );
  async function download(kind: string, id: string) {
    const value = await api.post("export", { kind, id });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(
      new Blob([value.pgn ?? JSON.stringify(value)], { type: "text/plain" }),
    );
    a.download = "chess-lab.pgn";
    a.click();
    URL.revokeObjectURL(a.href);
    const side = document.createElement("a");
    side.href = URL.createObjectURL(
      new Blob([JSON.stringify(value, null, 2)], { type: "application/json" }),
    );
    side.download = "chess-lab-sidecar.json";
    side.click();
    URL.revokeObjectURL(side.href);
  }
  return (
    <div className="app">
      <aside className="rail">
        <div className="brand">
          <span className="brand-icon">♞</span>
          <div>
            CHESS ENGINE<span>LAB / DESKTOP</span>
          </div>
        </div>
        <nav aria-label="Workspace">
          {tabs.map((t, i) => (
            <button
              className={tab === t ? "active" : ""}
              key={t}
              onClick={() => switchTab(t)}
            >
              <span>{["♙", "⌕", "◫", "⚙"][i]}</span>
              {t}
              <small>0{i + 1}</small>
            </button>
          ))}
        </nav>
        <div className="rail-footer">
          <span className={"dot " + (api.connected ? "online" : "")} />
          {api.connected
            ? "Local controller connected"
            : "Disconnected — reconnecting"}
          <p>LOCAL · OFFLINE · YOUR WORKSPACE</p>
          <button onClick={() => setQuit(true)}>Quit workspace ↗</button>
        </div>
      </aside>
      <main>
        <header>
          <div>
            <p className="eyebrow">CHESS ENGINE LAB / {tab.toUpperCase()}</p>
            <h1>
              {tab === "Play"
                ? "Your next move."
                : tab === "Analyze"
                  ? "Explore the position."
                  : tab === "Experiments"
                    ? "Test an idea."
                    : "Know your engine."}
            </h1>
          </div>
          <div className="header-actions">
            <span className="pill">Workspace {s?.id.slice(0, 6) ?? "—"}</span>
            <span className="pill">{live.length} active</span>
          </div>
        </header>
        {(error || api.error) && (
          <div role="alert" className="error">
            {error || api.error}
            <button
              onClick={() => {
                setError("");
                api.error = "";
              }}
            >
              Dismiss
            </button>
          </div>
        )}
        {s?.diagnostics.slice(-2).map((d, i) => (
          <div
            key={i}
            role={d.severity === "failure" ? "alert" : "status"}
            className={d.severity === "failure" ? "error" : "notice"}
          >
            <strong>
              {d.severity.toUpperCase()} · {d.code}
            </strong>{" "}
            {d.message}
          </div>
        ))}
        {!api.connected && (
          <div role="status" className="notice">
            Connection lost. Board changes are disabled until the authoritative
            workspace reconnects.
          </div>
        )}
        {(tab === "Play" || tab === "Analyze") && (
          <>
            <div className="toolbar">
              <label>
                {tab === "Play" ? "Game" : "Analysis document"}
                <select
                  value={
                    tab === "Play" ? (s?.activeGameId ?? "") : selectedAnalysis
                  }
                  onChange={(e) => {
                    setCursor(null);
                    if (tab === "Play")
                      void command("game.select", {}, e.target.value);
                    else {
                      setAnalysis(e.target.value);
                      layout({ activeAnalysis: e.target.value });
                    }
                  }}
                >
                  <option value="">Choose a document</option>
                  {Object.values(
                    tab === "Play" ? (s?.games ?? {}) : (s?.analyses ?? {}),
                  ).map((x) => (
                    <option key={x.id} value={x.id}>
                      {tab === "Play" ? "Casual game" : "Analysis"} ·{" "}
                      {x.id.slice(0, 6)}
                    </option>
                  ))}
                </select>
              </label>
              <button
                onClick={() => {
                  setFlipped(!flipped);
                  layout({ flipped: !flipped });
                }}
              >
                Flip board ⇄
              </button>
              <button
                onClick={() => {
                  setCollapsed(!rightCollapsed);
                  layout({ collapsed: !rightCollapsed });
                }}
              >
                {rightCollapsed ? "Show panels" : "Hide panels"}
              </button>
              <label className="panel-width">
                Panel width
                <input
                  aria-label="Panel width"
                  type="range"
                  min="280"
                  max="500"
                  value={panelWidth}
                  onChange={(e) => setPanelWidth(Number(e.target.value))}
                  onPointerUp={() => layout({ panelWidth })}
                  onKeyUp={() => layout({ panelWidth })}
                />
              </label>
              <button
                disabled={!record}
                onClick={() =>
                  void action(() =>
                    download(
                      tab === "Play" ? "game" : "analysis",
                      tab === "Play" ? g!.id : d!.id,
                    ),
                  )
                }
              >
                Export PGN + metadata
              </button>
              <button
                disabled={!record || !positionReady}
                onClick={() => saveFile("chess-lab.fen", position.fen)}
              >
                Export FEN
              </button>
            </div>
            <div
              className={"board-layout " + (rightCollapsed ? "collapsed" : "")}
              style={{ "--panel-width": panelWidth + "px" } as CSSProperties}
            >
              <section className="board-panel">
                <div className="player">
                  <span className="avatar">♚</span>
                  <div>
                    <strong>
                      {g?.players.b === "human"
                        ? "Black · Human"
                        : (s?.profiles[g?.players.b ?? ""]?.label ?? "Black")}
                    </strong>
                    <small>
                      {g?.checkpoint.paused
                        ? "Paused"
                        : "Authoritative game clock"}
                    </small>
                  </div>
                  <b className="clock">
                    {clock(tab === "Play" ? g?.checkpoint.remaining.b : null)}
                  </b>
                </div>
                {record ? (
                  <Board
                    lastMove={displayed?.moves.at(-1)}
                    fen={position.fen}
                    legal={position.legal}
                    flipped={flipped}
                    disabled={
                      !api.connected ||
                      !positionReady ||
                      busy ||
                      (tab === "Play" &&
                        (g?.checkpoint.paused ||
                          Boolean(g?.checkpoint.outcome) ||
                          cursor !== null ||
                          g?.players[newTurn(g.checkpoint.record)] !== "human"))
                    }
                    onMove={(move) => {
                      if (tab === "Play")
                        void command("game.move", { move }, g!.id);
                      else
                        void action(async () => {
                          let target = d!;
                          if (cursor !== null) {
                            const a = await api.command("analysis.create", {
                              record: displayed,
                            });
                            target = api.snapshot!.analyses[a.id!];
                            setAnalysis(target.id);
                            layout({ activeAnalysis: target.id });
                            setCursor(null);
                          }
                          await api.command(
                            "analysis.branch",
                            { node: target.cursor, move },
                            target.id,
                          );
                        });
                    }}
                  />
                ) : (
                  <div className="empty-board">
                    <span>♙</span>
                    <h2>A fresh position awaits.</h2>
                    <p>Create a game or import a position to begin.</p>
                  </div>
                )}
                <div className="player">
                  <span className="avatar light">♔</span>
                  <div>
                    <strong>
                      {g?.players.w === "human"
                        ? "White · Human"
                        : (s?.profiles[g?.players.w ?? ""]?.label ?? "White")}
                    </strong>
                    <small>
                      {tab === "Analyze"
                        ? "Analysis never changes played moves"
                        : (g?.checkpoint.outcome?.reason ?? "Casual play")}
                    </small>
                  </div>
                  <b className="clock">
                    {clock(tab === "Play" ? g?.checkpoint.remaining.w : null)}
                  </b>
                </div>
                {tab === "Play" && g && (
                  <div className="game-actions">
                    <button
                      className="primary"
                      disabled={
                        busy || !api.connected || Boolean(g.checkpoint.outcome)
                      }
                      onClick={() =>
                        void command(
                          g.checkpoint.paused ? "game.resume" : "game.pause",
                          {},
                          g.id,
                        )
                      }
                    >
                      {g.checkpoint.paused ? "Resume play" : "Pause game"}
                    </button>
                    <button
                      disabled={!g.checkpoint.record.moves.length}
                      onClick={() => void command("game.takeback", {}, g.id)}
                    >
                      Take back
                    </button>
                    <button
                      onClick={() =>
                        void command(
                          "game.resign",
                          {
                            side:
                              g.players[newTurn(g.checkpoint.record)] ===
                              "human"
                                ? newTurn(g.checkpoint.record)
                                : newTurn(g.checkpoint.record) === "w"
                                  ? "b"
                                  : "w",
                          },
                          g.id,
                        )
                      }
                    >
                      Resign
                    </button>
                    <button
                      disabled={!position.claim}
                      onClick={() => void command("game.claim", {}, g.id)}
                    >
                      Claim draw
                    </button>
                    <input
                      aria-label="Intended draw claim move"
                      placeholder="Intended claim e.g. g1f3"
                      id="claim-move"
                      maxLength={5}
                    />
                    <button
                      onClick={() =>
                        void command(
                          "game.claim",
                          {
                            move: (
                              document.getElementById(
                                "claim-move",
                              ) as HTMLInputElement
                            ).value,
                          },
                          g.id,
                        )
                      }
                    >
                      Claim with intended move
                    </button>
                  </div>
                )}
              </section>
              {!rightCollapsed && (
                <aside className="panels">
                  <section className="panel">
                    <div className="panel-title">
                      <h2>
                        {tab === "Play" ? "New game" : "Position / import"}
                      </h2>
                      <span>01</span>
                    </div>
                    <label>
                      Starting FEN
                      <textarea
                        value={fen}
                        onChange={(e) => setFen(e.target.value)}
                        placeholder="Leave blank for the standard position"
                        rows={2}
                      />
                    </label>
                    {tab === "Play" ? (
                      <NewGame
                        profiles={profiles}
                        onCreate={(payload) =>
                          void command("game.create", {
                            ...payload,
                            ...(fen ? { fen } : {}),
                          })
                        }
                      />
                    ) : (
                      <>
                        <button
                          className="primary"
                          onClick={() =>
                            void action(async () => {
                              const a = await api.command(
                                "analysis.create",
                                fen ? { fen } : {},
                              );
                              setAnalysis(a.id!);
                              layout({ activeAnalysis: a.id! });
                              setCursor(null);
                            })
                          }
                        >
                          New analysis
                        </button>
                        <label>
                          PGN with variations
                          <textarea
                            rows={3}
                            value={textImport}
                            onChange={(e) => setImport(e.target.value)}
                            placeholder="1. e4 (1. d4 d5) e5"
                          />
                        </label>
                        <button
                          onClick={() =>
                            void action(async () => {
                              const a = await api.command("analysis.create", {
                                pgn: textImport,
                              });
                              setAnalysis(a.id!);
                              layout({ activeAnalysis: a.id! });
                            })
                          }
                        >
                          Import PGN
                        </button>
                      </>
                    )}
                    <label className="upload">
                      Import PGN file
                      <input
                        type="file"
                        accept=".pgn,text/plain"
                        onChange={(e) =>
                          void action(async () => {
                            const f = e.target.files?.[0];
                            if (!f) return;
                            if (f.size > 262144)
                              throw Error("PGN exceeds 256 KiB");
                            const a = await api.command("analysis.create", {
                              pgn: await f.text(),
                            });
                            setAnalysis(a.id!);
                            layout({ activeAnalysis: a.id! });
                            switchTab("Analyze");
                          })
                        }
                      />
                    </label>
                  </section>
                  <section className="panel">
                    <div className="panel-title">
                      <h2>Move history</h2>
                      <span>{record?.moves.length ?? 0} plies</span>
                    </div>
                    <MoveHistory
                      moves={record?.moves ?? []}
                      cursor={cursor}
                      onNavigate={(i) => {
                        if (tab === "Analyze" && d && i !== null) {
                          let n = d.nodes[d.cursor];
                          while (n.parent && n.record.moves.length > i)
                            n = d.nodes[n.parent];
                          if (n.record.moves.length > i) setCursor(i);
                          else {
                            setCursor(null);
                            void command(
                              "analysis.cursor",
                              { node: n.id },
                              d.id,
                            );
                          }
                        } else setCursor(i);
                      }}
                    />
                    {tab === "Analyze" && d && (
                      <>
                        <label>
                          Variation
                          <select
                            aria-label="Variation cursor"
                            value={d.cursor}
                            onChange={(e) => {
                              setCursor(null);
                              void command(
                                "analysis.cursor",
                                { node: e.target.value },
                                d.id,
                              );
                            }}
                          >
                            {Object.values(d.nodes).map((n) => (
                              <option key={n.id} value={n.id}>
                                {n.record.moves.join(" ") || "Root"}
                              </option>
                            ))}
                          </select>
                        </label>
                        <label>
                          Comment
                          <textarea
                            key={node?.id}
                            defaultValue={node?.comment}
                            onBlur={(e) =>
                              void command(
                                "analysis.annotate",
                                { node: d.cursor, comment: e.target.value },
                                d.id,
                              )
                            }
                          />
                        </label>
                      </>
                    )}
                  </section>
                  <section className="panel">
                    <div className="panel-title">
                      <h2>Engine analysis</h2>
                      <span>02</span>
                    </div>
                    <label>
                      Registered engine
                      <select
                        value={profile}
                        onChange={(e) => setProfile(e.target.value)}
                      >
                        <option value="">Select engine</option>
                        {profiles.map((p) => (
                          <option value={p.id} key={p.id}>
                            {p.label}
                          </option>
                        ))}
                      </select>
                    </label>
                    <div className="form-row">
                      <label>
                        Mode
                        <select
                          value={mode}
                          onChange={(e) => setMode(e.target.value)}
                        >
                          <option value="depth">Fixed depth</option>
                          <option value="movetime">Time budget</option>
                          <option value="infinite">Until stopped</option>
                        </select>
                      </label>
                      <label>
                        {mode === "movetime" ? "Milliseconds" : "Depth"}
                        <input
                          type="number"
                          min="1"
                          max={mode === "depth" ? 64 : 600000}
                          value={mode === "movetime" ? budget : depth}
                          onChange={(e) =>
                            mode === "movetime"
                              ? setBudget(e.target.value)
                              : setDepth(e.target.value)
                          }
                        />
                      </label>
                      <label>
                        MultiPV
                        <input
                          type="number"
                          min="1"
                          max="256"
                          value={multi}
                          onChange={(e) => setMulti(e.target.value)}
                        />
                      </label>
                    </div>
                    <label className="checkbox">
                      <input
                        type="checkbox"
                        checked={share}
                        onChange={(e) => setShare(e.target.checked)}
                      />
                      Authorize competition with experiment workloads
                    </label>
                    <div className="form-row">
                      <button
                        className="primary"
                        disabled={!profile || busy || !record}
                        onClick={() =>
                          void action(async () => {
                            let id = d?.id;
                            if (tab === "Play" || !id || cursor !== null) {
                              const a = await api.command("analysis.create", {
                                record: displayed,
                              });
                              id = a.id!;
                              setAnalysis(id);
                              layout({ activeAnalysis: id });
                              switchTab("Analyze");
                            }
                            await api.command(
                              "analysis.start",
                              {
                                profile,
                                limit:
                                  mode === "infinite"
                                    ? { kind: "infinite" }
                                    : mode === "depth"
                                      ? { kind: "depth", depth: Number(depth) }
                                      : {
                                          kind: "movetime",
                                          ms: Number(budget),
                                        },
                                multiPV: Number(multi),
                                allowCompetition: share,
                              },
                              id,
                            );
                          })
                        }
                      >
                        Start analysis
                      </button>
                      <button
                        disabled={!d}
                        onClick={() => void command("analysis.stop", {}, d!.id)}
                      >
                        Stop
                      </button>
                    </div>
                    <div className="progress">
                      {jobs
                        .filter(
                          (j) =>
                            j.objectId === d?.id &&
                            cursor === null &&
                            positionReady &&
                            j.root === position.fen &&
                            !["stopping", "stopped", "interrupted"].includes(
                              j.status,
                            ),
                        )
                        .slice(-1)
                        .map((j) => (
                          <div key={j.id}>
                            <strong>
                              {j.status} · session {j.session.slice(0, 6)} ·{" "}
                              {j.identity?.id.join(" · ")}
                            </strong>
                            {j.error && <p role="alert">{j.error.message}</p>}
                            {(j.status === "failed"
                              ? []
                              : (api.progress.get(j.session) ?? [])
                            )
                              .slice(-8)
                              .map((p) => (
                                <p key={p.sequence}>
                                  {scoreLabel(
                                    p.line,
                                    j.trusted &&
                                      String(
                                        JSON.stringify(j.identity?.m2),
                                      ).match(/calibration [a-f0-9]{64} /)
                                      ? "calibrated"
                                      : "external",
                                  )}
                                  <code className="pv-line">{p.line}</code>
                                  <small>Provisional · {p.root}</small>
                                  {/ pv /.test(p.line) && (
                                    <button
                                      onClick={() =>
                                        void action(async () => {
                                          const pv = p.line
                                            .split(" pv ")[1]
                                            .trim()
                                            .split(" ");
                                          let parent = d!.cursor;
                                          for (const move of pv) {
                                            await api.command(
                                              "analysis.branch",
                                              { node: parent, move },
                                              d!.id,
                                            );
                                            parent =
                                              api.snapshot!.analyses[d!.id]
                                                .cursor;
                                          }
                                        })
                                      }
                                    >
                                      Explore PV as variation
                                    </button>
                                  )}
                                </p>
                              ))}
                          </div>
                        ))}
                    </div>
                  </section>
                </aside>
              )}
            </div>
          </>
        )}
        {tab === "Engines" && <EnginePanel snapshot={s} command={command} />}
        {tab === "Experiments" && (
          <Experiments
            snapshot={s}
            command={command}
            download={(id) => action(() => download("experiment", id))}
            review={(record) =>
              void action(async () => {
                const a = await api.command("analysis.create", { record });
                setAnalysis(a.id!);
                layout({ activeAnalysis: a.id! });
                switchTab("Analyze");
              })
            }
          />
        )}
        <footer>
          <span>
            Controller authority · {s?.revision ?? 0} committed revisions
          </span>
          <span>Raw scores stay raw. Calibrated scores carry provenance.</span>
        </footer>
      </main>
      {quit && (
        <div className="dialog-backdrop">
          <section
            role="dialog"
            aria-label="Quit workspace"
            aria-modal="true"
            className="dialog"
          >
            <h2>Leave your workspace?</h2>
            <p>
              Closing the browser keeps the controller running. Terminate pauses
              games and stops experiments after their current game.
            </p>
            {live.length > 0 && (
              <p>{live.length} active workload(s) will be stopped safely.</p>
            )}
            <button
              autoFocus
              onClick={() => {
                void action(() => api.post("quit", { action: "continue" }));
                setQuit(false);
              }}
            >
              Continue in background
            </button>
            <button
              onClick={() =>
                void action(async () => {
                  await api.post("quit", { action: "terminate" });
                  api.close();
                  setQuit(false);
                })
              }
            >
              Terminate controller
            </button>
            <button onClick={() => setQuit(false)}>Cancel</button>
          </section>
        </div>
      )}
    </div>
  );
}
function clock(n: number | null | undefined) {
  return n === null || n === undefined
    ? "—"
    : `${Math.floor(n / 60000)
        .toString()
        .padStart(
          2,
          "0",
        )}:${Math.floor(n / 1000) % 60 < 10 ? "0" : ""}${Math.floor(n / 1000) % 60}`;
}
function newTurn(r: { initialFen: string; moves: readonly string[] }) {
  const white = r.initialFen.split(" ")[1] === "w";
  return (white ? r.moves.length % 2 === 0 : r.moves.length % 2 !== 0)
    ? "w"
    : "b";
}
export function NewGame({
  profiles,
  onCreate,
}: {
  profiles: Array<{ id: string; label: string }>;
  onCreate: (p: Record<string, unknown>) => void;
}) {
  const [white, setWhite] = useState("human"),
    [black, setBlack] = useState("human"),
    [timed, setTimed] = useState(false),
    [minutes, setMinutes] = useState(5),
    [increment, setIncrement] = useState(2);
  return (
    <>
      <div className="form-row">
        {[
          ["White", white, setWhite],
          ["Black", black, setBlack],
        ].map(([label, value, change]) => (
          <label key={label as string}>
            {label as string}
            <select
              aria-label={label + " player"}
              value={value as string}
              onChange={(e) => (change as (s: string) => void)(e.target.value)}
            >
              <option value="human">Local human</option>
              {profiles.map((p) => (
                <option value={p.id} key={p.id}>
                  {p.label}
                </option>
              ))}
            </select>
          </label>
        ))}
      </div>
      <label className="checkbox">
        <input
          type="checkbox"
          checked={timed}
          onChange={(e) => setTimed(e.target.checked)}
        />
        Fischer clock
      </label>
      {timed && (
        <div className="form-row">
          <label>
            Minutes
            <input
              type="number"
              min="0"
              value={minutes}
              onChange={(e) => setMinutes(Number(e.target.value))}
            />
          </label>
          <label>
            Increment seconds
            <input
              type="number"
              min="0"
              value={increment}
              onChange={(e) => setIncrement(Number(e.target.value))}
            />
          </label>
        </div>
      )}
      <button
        className="primary"
        disabled={white !== "human" && black !== "human"}
        onClick={() =>
          onCreate({
            players: { w: white, b: black },
            time: timed
              ? {
                  kind: "fischer",
                  initialMs: minutes * 60000,
                  incrementMs: increment * 1000,
                }
              : { kind: "untimed" },
          })
        }
      >
        Create game
      </button>
    </>
  );
}
export function EnginePanel({
  snapshot: s,
  command,
}: {
  snapshot: Snapshot | null;
  command: (op: string, p: Record<string, unknown>, id?: string) => unknown;
}) {
  const [label, setLabel] = useState("Chess Lab"),
    [path, setPath] = useState(""),
    [args, setArgs] = useState("[]"),
    [options, setOptions] = useState("{}"),
    [kind, setKind] = useState("uci"),
    [trust, setTrust] = useState(false),
    [edit, setEdit] = useState<string | undefined>();
  return (
    <div className="management">
      <section className="panel">
        <p className="eyebrow">LOCAL EXECUTABLES</p>
        <h2>{edit ? "New profile generation" : "Register an engine"}</h2>
        <p>
          Native engines and evaluator modules run with your OS user
          permissions. Register only executables you trust.
        </p>
        <label>
          Friendly name
          <input value={label} onChange={(e) => setLabel(e.target.value)} />
        </label>
        <label>
          Absolute executable path
          <input
            value={path}
            onChange={(e) => setPath(e.target.value)}
            placeholder="/path/to/chess_lab_uci"
          />
        </label>
        <label>
          Argv JSON
          <textarea value={args} onChange={(e) => setArgs(e.target.value)} />
        </label>
        <label>
          UCI options / evaluator configuration JSON
          <textarea
            rows={5}
            value={options}
            onChange={(e) => setOptions(e.target.value)}
            placeholder={
              '{"Hash":"16","Evaluator":"/path/module.so","EvaluatorConfig":"{}"}'
            }
          />
        </label>
        <label>
          Extension provenance
          <select value={kind} onChange={(e) => setKind(e.target.value)}>
            <option value="uci">External UCI · claims untrusted</option>
            <option value="chesslab">Trusted Chess Lab executable</option>
          </select>
        </label>
        <label className="checkbox">
          <input
            type="checkbox"
            checked={trust}
            onChange={(e) => setTrust(e.target.checked)}
          />
          I trust this executable and configured native modules.
        </label>
        <button
          className="primary"
          disabled={!trust || !path.startsWith("/") || !label}
          onClick={() => {
            try {
              void command(
                "engine.register",
                {
                  label,
                  trust,
                  config: {
                    path,
                    args: JSON.parse(args),
                    options: JSON.parse(options),
                    kind,
                  },
                },
                edit,
              );
            } catch {
              alert("Argv and options must be valid JSON");
            }
          }}
        >
          Inspect and save profile
        </button>
      </section>
      <section className="engine-list">
        {Object.values(s?.profiles ?? {}).map((p) => (
          <article className="panel" key={p.id}>
            <span className="pill">Generation {p.generation}</span>
            <h2>{p.label}</h2>
            <p>{p.identity.id.join(" · ")}</p>
            <code>{p.config.path}</code>
            <details>
              <summary>Identity, options and calibration</summary>
              <pre>{JSON.stringify(p.identity, null, 2)}</pre>
            </details>
            <p className="calibration-status">
              {p.config.kind === "chesslab" &&
              /calibration [a-f0-9]{64}/.test(JSON.stringify(p.identity.m2))
                ? "Validated evaluator calibration · descriptor provenance recorded"
                : p.config.kind === "chesslab"
                  ? "Raw only · no validated calibration"
                  : "External UCI · cp values are engine-reported"}
            </p>
            <p>
              SHA-256 <code>{p.identity.sha256}</code>
            </p>
            <button
              onClick={() => {
                setEdit(p.id);
                setPath(p.config.path);
                setLabel(p.label);
                setArgs(JSON.stringify(p.config.args ?? []));
                setOptions(JSON.stringify(p.config.options ?? {}));
                setKind(p.config.kind ?? "uci");
                setTrust(false);
              }}
            >
              Edit future sessions
            </button>
          </article>
        ))}
      </section>
    </div>
  );
}
function Experiments({
  snapshot: s,
  command,
  download,
  review,
}: {
  snapshot: Snapshot | null;
  command: (op: string, p: Record<string, unknown>, id?: string) => unknown;
  download: (id: string) => unknown;
  review: (r: unknown) => unknown;
}) {
  const [a, setA] = useState(""),
    [b, setB] = useState(""),
    [json, setJson] = useState(
      '{"seed":1,"limit":{"kind":"depth","depth":1},"time":{"kind":"untimed"},"maxPlies":300,"searchCapMs":60000}',
    ),
    [competition, setCompetition] = useState(false),
    [reviewData, setReview] = useState<any>(null);
  return (
    <div className="management">
      <section className="panel">
        <p className="eyebrow">REPRODUCIBLE BY DESIGN</p>
        <h2>A paired comparison</h2>
        <p>
          Each opening is played with colors swapped. Conditions and engine
          identities are frozen in the manifest.
        </p>
        {[
          ["Engine A", a, setA],
          ["Engine B", b, setB],
        ].map(([label, value, change]) => (
          <label key={label as string}>
            {label as string}
            <select
              value={value as string}
              onChange={(e) => (change as (s: string) => void)(e.target.value)}
            >
              <option value="">Select engine</option>
              {Object.values(s?.profiles ?? {}).map((p) => (
                <option key={p.id} value={p.id}>
                  {p.label}
                </option>
              ))}
            </select>
          </label>
        ))}
        <label>
          Match conditions JSON
          <textarea
            rows={9}
            value={json}
            onChange={(e) => setJson(e.target.value)}
          />
        </label>
        <button
          className="primary"
          disabled={!a || !b}
          onClick={() => {
            try {
              void command("experiment.create", {
                white: a,
                black: b,
                config: JSON.parse(json),
              });
            } catch {
              alert("Match conditions must be valid JSON");
            }
          }}
        >
          Create paired experiment
        </button>
        <label className="checkbox">
          <input
            type="checkbox"
            checked={competition}
            onChange={(e) => setCompetition(e.target.checked)}
          />
          Explicitly authorize resource competition
        </label>
      </section>
      <section>
        {Object.values(s?.experiments ?? {}).map((e) => (
          <article className="panel" key={e.id}>
            <div className="panel-title">
              <h2>Experiment {e.id.slice(0, 6)}</h2>
              <span className="pill">{e.status}</span>
            </div>
            <div className="game-actions">
              {[
                ["Start / resume", "experiment.resume"],
                ["Pause after game", "experiment.pause"],
                ["Stop after game", "experiment.stop"],
              ].map(([label, op]) => (
                <button
                  key={op}
                  onClick={() =>
                    void command(
                      op,
                      op === "experiment.resume"
                        ? { allowCompetition: competition }
                        : {},
                      e.id,
                    )
                  }
                >
                  {label}
                </button>
              ))}
              <button onClick={() => void download(e.id)}>
                Export PGN + metadata
              </button>
              <button
                onClick={() =>
                  void api
                    .post("export", { kind: "experiment", id: e.id })
                    .then(setReview)
                }
              >
                Review games
              </button>
            </div>
            {Object.values(s?.jobs ?? {})
              .filter((j) => j.objectId === e.id)
              .slice(-1)
              .map((j) => (
                <div className="progress" key={j.id}>
                  {j.error && <p role="alert">{j.error.message}</p>}
                  {(api.progress.get(j.session) ?? []).slice(-1).map((p) => (
                    <p key={p.sequence}>
                      Current game progress · {p.line}
                      <small>
                        Provisional until the M6 terminal record commits
                      </small>
                    </p>
                  ))}
                </div>
              ))}
            {e.summary ? (
              <pre>{JSON.stringify(e.summary, null, 2)}</pre>
            ) : (
              <p>Awaiting its first completed game.</p>
            )}
          </article>
        ))}
        {reviewData?.sidecar?.attempts?.map((a: any) => (
          <article className="panel" key={a.id}>
            <h3>
              {a.game} · {a.outcome.kind}
            </h3>
            <p>{a.outcome.reason}</p>
            <button onClick={() => review(a.record)}>
              Open played game in Analyze
            </button>
          </article>
        ))}
      </section>
    </div>
  );
}
function saveFile(name: string, content: string) {
  const url = URL.createObjectURL(new Blob([content], { type: "text/plain" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}
const root = document.getElementById("root");
if (root) createRoot(root).render(<App />);
