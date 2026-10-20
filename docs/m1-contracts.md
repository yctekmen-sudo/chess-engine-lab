# M1 source-level chess core contracts

Architecture v1.0 remains authoritative. M0 declarations are refined here;
these C++ interfaces are not the independently compiled M2 evaluation ABI.

## Ownership, traversal and errors

create_chess_core() returns the Disservin implementation through ChessCore.
Position, Move and UndoToken remain opaque abstract own types. Public headers
and consumers require no upstream header, types, layout or include path.
An alternative adapter can implement the interface independently.

Move objects represent coordinate moves, reusable wherever legal in this
implementation. Encoding, decoding and making validate against current legal
moves; castling uses king destinations (e1g1/e1c1), promotions lowercase q/r/b/n.
Malformed, illegal and foreign implementation objects throw invalid_argument.
Objects produced by another instance of this same implementation are compatible.
No UCI protocol or SAN/PGN transport is implemented.

Each make returns a single-use token; undo takes a reference, so a rejected
out-of-order undo does not destroy a still-valid token. Tokens bind to the
originating position lifetime, reset generation and traversal branch, and must
be used in LIFO order. Reuse, wrong position, clone, reset or foreign tokens
throw without changing state. Destroying a token does not automatically undo.
Clones are independent and do not accept source tokens. Reset parses first and
leaves the original intact on invalid FEN. Traversal uses upstream incremental
make/unmake; moves and undo tokens currently use the provisional M0 allocation
model, with one token allocation per make. No search is implemented.

The FEN boundary requires six fields, eight complete ranks, one king per side,
valid piece/turn/orthodox rights, no back-rank pawns, nonadjacent kings, and
no check on the side that just moved. EP must describe a possible just-completed
double push with empty origin/target and zero halfmove clock. This is structural
and local legality validation, not proof that a position is historically reachable.
Fullmove starts at 1. Counters are uint32_t; overflow on make throws before
mutation. Upstream narrow clocks are not authoritative and never drive outcomes.
FEN output canonicalizes field spacing and castling order.

## State, identity and history

Full position state includes all 64 pieces, active side, rights, raw FEN EP
target, halfmove clock and fullmove number. Standard FEN retains an EP target
after any double pawn push even if no capture is legal. Own metadata preserves
that field while the library normalizes its internal EP state.

position_key and repetition_key currently return the same deterministic upstream
64-bit Zobrist identity: pieces, active side, castling rights, and EP file only
when a *legal* EP capture is available (including king safety). Clocks and raw
noncapturable EP targets are excluded. Equality of hashes is probabilistic,
not a collision-proof proof of position equality. This algorithm/pin is not a
persistent cross-version serialization contract.

A position key is only a board-state cache lookup component. It is not a complete
search-cache validity key: the halfmove clock, repetition history, evaluator and
search configuration can change results. Repetition equivalence does not include
the clocks, but threefold/fivefold outcomes require separate occurrence history.
FEN and clone do not reconstruct pre-import occurrences. ChessCore exposes no
claim/automatic repetition adjudication and does not use upstream repetition or
game-over helpers. The controller/search history context belongs to later milestones.

status reports only ongoing/checkmate/stalemate. known_dead_position recognizes
sufficient material cases (bare kings, king plus a lone minor, and limited
same-color-bishop cases). False is inconclusive; general blocked-position
deadness and FIDE draw adjudication remain outside this M1 implementation.

## Read-only snapshot

Snapshots are detached immutable objects surviving source mutation/destruction.
Source schema version 1 uses the existing fixed-width piece codes, a1=0 through
h8=63, active side, all four castling rights, raw FEN EP target and uint32 clocks.
There is no history or evaluator. This source schema number does not freeze
a C ABI layout, ownership convention, serialization format or module interface;
those remain M2 work.
