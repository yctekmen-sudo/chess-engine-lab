#pragma once

#include "chess_lab/position_snapshot.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chess_lab {

// Opaque, adapter-owned representations. Virtual destruction permits ownership
// through our own types; no third-party layout is exposed. No concrete adapter
// exists in M0. These C++ interfaces are provisional, not a plugin ABI.
class Position {
public:
    virtual ~Position() = default;
protected:
    Position() = default;
    Position(const Position&) = delete;
    Position& operator=(const Position&) = delete;
};

class Move {
public:
    virtual ~Move() = default;
protected:
    Move() = default;
    Move(const Move&) = delete;
    Move& operator=(const Move&) = delete;
};

class UndoToken {
public:
    virtual ~UndoToken() = default;
protected:
    UndoToken() = default;
    UndoToken(const UndoToken&) = delete;
    UndoToken& operator=(const UndoToken&) = delete;
};

// Skeleton for M1. Objects must belong to the same adapter. Error reporting,
// validation, allocation/performance choices, reset, status, and hash/key APIs
// will be refined and tested there; these declarations implement no chess rules.
class ChessCore {
public:
    virtual ~ChessCore() = default;
    [[nodiscard]] virtual std::unique_ptr<Position> initial_position() const = 0;
    [[nodiscard]] virtual std::unique_ptr<Position> load_fen(std::string_view fen) const = 0;
    [[nodiscard]] virtual std::string export_fen(const Position& position) const = 0;
    [[nodiscard]] virtual std::unique_ptr<Position> clone(const Position& position) const = 0;
    [[nodiscard]] virtual std::vector<std::unique_ptr<Move>> legal_moves(const Position& position) const = 0;
    // Input move must be legal. Undo restores the exact preceding position state;
    // tokens are used once, in reverse make order, on their originating position.
    [[nodiscard]] virtual std::unique_ptr<UndoToken> make_move(Position& position, const Move& move) const = 0;
    virtual void undo_move(Position& position, std::unique_ptr<UndoToken> token) const = 0;
    [[nodiscard]] virtual std::string encode_move(const Position& position, const Move& move) const = 0;
    [[nodiscard]] virtual std::unique_ptr<Move> decode_move(const Position& position, std::string_view coordinate) const = 0;
    // A detached snapshot survives changes to/deletion of the source position.
    [[nodiscard]] virtual std::unique_ptr<const PositionSnapshot> snapshot(const Position& position) const = 0;
};

}
