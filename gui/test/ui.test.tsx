import { describe, it, expect, vi } from "vitest";
import { render, screen, fireEvent } from "@testing-library/react";
vi.mock("react-chessboard", () => ({
  Chessboard: ({ options }: any) => (
    <button
      onClick={() =>
        options.onPieceDrop({ sourceSquare: "a7", targetSquare: "a8" })
      }
    >
      Drop pawn
    </button>
  ),
}));
import { Board, MoveHistory } from "../src/Board";
import { Gateway, scoreLabel } from "../src/api";
describe("board presentation and explicit promotion", () => {
  it("requires a promotion choice and sends only the selected move", () => {
    const onMove = vi.fn();
    render(
      <Board
        fen="8/P7/8/8/8/8/8/8 w - - 0 1"
        legal={["a7a8q", "a7a8r", "a7a8b", "a7a8n"]}
        onMove={onMove}
      />,
    );
    fireEvent.click(screen.getByText("Drop pawn"));
    expect(onMove).not.toHaveBeenCalled();
    expect(screen.getByRole("dialog")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Knight"));
    expect(onMove).toHaveBeenCalledWith("a7a8n");
  });
  it("offers keyboard moves and disables disconnected actions", () => {
    const onMove = vi.fn();
    const { rerender } = render(
      <Board fen="start" legal={["e2e4"]} onMove={onMove} />,
    );
    fireEvent.change(screen.getByLabelText("Coordinate move"), {
      target: { value: "e2e4" },
    });
    fireEvent.click(screen.getByText("Play move"));
    expect(onMove).toHaveBeenCalledWith("e2e4");
    rerender(<Board fen="start" legal={["e2e4"]} disabled onMove={onMove} />);
    expect(screen.getByText("Play move")).toBeDisabled();
  });
});
it("labels mate/raw/cp truthfully", () => {
  expect(scoreLabel("info score mate -2")).toBe("Mate -2");
  expect(scoreLabel("info string chesslab score raw white 1.5")).toContain(
    "Raw (White)",
  );
  expect(scoreLabel("info depth 2 score cp 35 pv e2e4")).toContain(
    "side to move",
  );
});
it("ignores stale/duplicate progress and snapshot rollback", () => {
  const g = new Gateway();
  g.snapshot = {
    version: 1,
    revision: 5,
    jobs: {
      x: {
        session: "s",
        objectId: "d",
        root: "fen",
        generation: 7,
        status: "running",
      },
    },
  } as any;
  g.receive({
    version: 1,
    type: "progress",
    session: "old",
    objectId: "d",
    root: "fen",
    generation: 7,
    sequence: 1,
    provisional: true,
    line: "old",
  });
  expect(g.progress.size).toBe(0);
  const event = {
    version: 1,
    type: "progress",
    session: "s",
    objectId: "d",
    root: "fen",
    generation: 7,
    sequence: 1,
    provisional: true,
    line: "committed depth",
  } as const;
  g.receive(event);
  g.receive(event);
  expect(g.progress.get("s")?.length).toBe(1);
  g.receive({
    version: 1,
    type: "state",
    revision: 4,
    snapshot: { revision: 4 } as any,
  });
  expect(g.snapshot!.revision).toBe(5);
});
it("reconciles revision gaps and rejects incompatible event versions", async () => {
  const g = new Gateway();
  g.snapshot = { revision: 2, jobs: {} } as any;
  const refresh = vi.spyOn(g, "refresh").mockResolvedValue();
  g.receive({
    version: 1,
    type: "state",
    revision: 4,
    snapshot: { revision: 4, jobs: {} } as any,
  });
  expect(refresh).toHaveBeenCalledOnce();
  g.receive({
    version: 2,
    type: "state",
    revision: 5,
    snapshot: { revision: 5 },
  } as any);
  expect(g.snapshot!.revision).toBe(4);
  expect(g.error).toContain("Incompatible");
});
it("retains calibrated provenance independently from profile selection", () => {
  expect(scoreLabel("info score cp -35", "calibrated")).toContain("Calibrated");
  expect(scoreLabel("info score cp -35", "external")).toContain(
    "Engine-reported",
  );
});

it("history navigation emits only a presentation cursor", () => {
  const navigate = vi.fn();
  render(
    <MoveHistory
      moves={["e2e4", "e7e5"]}
      cursor={null}
      onNavigate={navigate}
    />,
  );
  fireEvent.click(screen.getByText("1. e2e4"));
  expect(navigate).toHaveBeenLastCalledWith(1);
  fireEvent.click(screen.getByText("Start"));
  expect(navigate).toHaveBeenLastCalledWith(0);
  fireEvent.click(screen.getByText("Live position"));
  expect(navigate).toHaveBeenLastCalledWith(null);
});
