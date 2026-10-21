import { useState } from "react";
import { Chessboard } from "react-chessboard";
export function Board({
  fen,
  legal,
  onMove,
  disabled = false,
  flipped = false,
  lastMove,
}: {
  fen: string;
  legal: string[];
  onMove: (move: string) => void;
  disabled?: boolean;
  flipped?: boolean;
  lastMove?: string;
}) {
  const [from, setFrom] = useState(""),
    [promotion, setPromotion] = useState<string | null>(null),
    [coordinate, setCoordinate] = useState("");
  function select(a: string, b: string) {
    const moves = legal.filter((m) => m.startsWith(a + b));
    if (!moves.length || disabled) return false;
    if (moves.some((m) => m.length === 5)) setPromotion(a + b);
    else onMove(a + b);
    setFrom("");
    return true;
  }
  const highlights = Object.fromEntries(
    (from ? legal.filter((m) => m.startsWith(from)) : []).map((m) => [
      m.slice(2, 4),
      { background: "radial-gradient(circle, #bf9858 18%, transparent 20%)" },
    ]),
  );
  return (
    <div className="board-wrapper">
      <Chessboard
        options={{
          id: "chesslab-board",
          position: fen,
          boardOrientation: flipped ? "black" : "white",
          allowDragging: !disabled,
          onPieceDrop: ({ sourceSquare, targetSquare }) =>
            targetSquare ? select(sourceSquare, targetSquare) : false,
          onSquareClick: ({ square }) => {
            if (from && select(from, square)) return;
            setFrom(square);
          },
          squareStyles: {
            ...(lastMove
              ? Object.fromEntries(
                  [lastMove.slice(0, 2), lastMove.slice(2, 4)].map((s) => [
                    s,
                    { backgroundColor: "#bf985855" },
                  ]),
                )
              : {}),
            ...highlights,
          },
          darkSquareStyle: { backgroundColor: "#759486" },
          lightSquareStyle: { backgroundColor: "#ebe6d8" },
          boardStyle: { borderRadius: 6 },
        }}
      />
      <form
        className="coordinate"
        onSubmit={(e) => {
          e.preventDefault();
          const m = coordinate.trim();
          if (legal.includes(m) && !disabled) {
            onMove(m);
            setCoordinate("");
          }
        }}
      >
        <label>
          Keyboard move
          <input
            aria-label="Coordinate move"
            placeholder="e2e4"
            value={coordinate}
            onChange={(e) => setCoordinate(e.target.value)}
            maxLength={5}
          />
        </label>
        <button disabled={disabled || !legal.includes(coordinate)}>
          Play move
        </button>
      </form>
      {promotion && (
        <div className="dialog-backdrop">
          <section
            role="dialog"
            aria-modal="true"
            aria-label="Choose promotion"
            className="dialog"
          >
            <h2>Promote pawn</h2>
            <p>Choose the piece for this move.</p>
            {[
              ["q", "Queen"],
              ["r", "Rook"],
              ["b", "Bishop"],
              ["n", "Knight"],
            ].map(([p, label]) => (
              <button
                key={p}
                autoFocus={p === "q"}
                onClick={() => {
                  onMove(promotion + p);
                  setPromotion(null);
                }}
              >
                {label}
              </button>
            ))}
            <button onClick={() => setPromotion(null)}>Cancel</button>
          </section>
        </div>
      )}
    </div>
  );
}

export function MoveHistory({
  moves,
  cursor,
  onNavigate,
}: {
  moves: readonly string[];
  cursor: number | null;
  onNavigate: (cursor: number | null) => void;
}) {
  return (
    <div className="history">
      <button
        className={cursor === 0 ? "selected" : ""}
        onClick={() => onNavigate(0)}
      >
        Start
      </button>
      {moves.map((m, i) => (
        <button
          key={i}
          className={cursor === i + 1 ? "selected" : ""}
          onClick={() => onNavigate(i + 1)}
        >
          {i % 2 === 0 ? `${Math.floor(i / 2) + 1}. ` : ""}
          {m}
        </button>
      ))}
      <button onClick={() => onNavigate(null)}>Live position</button>
    </div>
  );
}
