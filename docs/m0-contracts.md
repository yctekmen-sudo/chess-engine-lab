# M0 source-level contracts

These are scaffolding interfaces for M1 discussion, not stable binary exports.
They contain no rules implementation or third-party chess-library types.

- `Position`, `Move`, and `UndoToken` expose no representation. A future adapter
  owns concrete implementations behind these polymorphic types; public virtual
  destructors permit safe ownership through `std::unique_ptr`. Copying handles
  is disabled; `ChessCore::clone` describes explicit position cloning.
- `ChessCore` declares initial/FEN creation, export, cloning, legal move
  enumeration, make/undo, coordinate encoding/decoding, and detached snapshot
  creation. Moves use unambiguous UCI-style coordinates at boundaries, without
  introducing a UCI transport. No concrete factory or adapter exists yet.
- An adapter must accept only its own objects. Undo tokens are used once, in
  reverse order on their originating position, restoring exact position state.
  FEN and a position alone do not reconstruct game history.
- `PositionSnapshot` exposes only const accessors and a span of const piece
  codes for 64 squares, with side, castling/en-passant metadata, and counters.
  It is detached from mutable position storage. There is no move history,
  adjudication method, score, or evaluator algorithm. Snapshot producers and
  implementations are deferred; M0 never constructs a chess position.

M1 must refine error handling (including invalid FEN/moves and cross-adapter
misuse), reset/state/status APIs, in-check/dead-position support, cache versus
repetition identity, and efficient traversal/storage. Allocation choices in the
skeleton are provisional. Rules correctness is governed by the architecture,
not by these declarations or a chosen third-party implementation.

M2 must define the snapshot schema/version, C-compatible layout, sizes/alignment,
symbol names, ownership, error behavior, parameter/module contracts, and ABI
version checks before independently compiled evaluators. C++ virtual objects,
standard library containers, and exceptions must not cross that boundary. The
project version used by the smoke test is not a snapshot or ABI version.

Decision 24 quiescence metrics and thresholds remain undecided. No search,
evaluation, draw adjudication, transport, or GUI behavior is introduced by M0.
