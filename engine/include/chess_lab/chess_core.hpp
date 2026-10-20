#pragma once

#include "chess_lab/position_snapshot.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace chess_lab {

// Opaque, adapter-owned representations. Virtual destruction permits ownership
// through our own types; no third-party layout is exposed. Concrete storage
// stays private to each adapter. These interfaces are provisional, not a plugin ABI.
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

enum class PositionStatus { ongoing, checkmate, stalemate };

// Invalid FEN/moves, foreign objects and invalid undo usage throw
// std::invalid_argument. Counter exhaustion throws std::overflow_error.
// These source-level C++ interfaces are not the M2 evaluator ABI.
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
    virtual void undo_move(Position& position, UndoToken& token) const = 0;
    [[nodiscard]] virtual std::string encode_move(const Position& position, const Move& move) const = 0;
    [[nodiscard]] virtual std::unique_ptr<Move> decode_move(const Position& position, std::string_view coordinate) const = 0;
    virtual void reset(Position& position, std::string_view fen) const = 0;
    [[nodiscard]] virtual bool in_check(const Position& position) const = 0;
    [[nodiscard]] virtual PositionStatus status(const Position& position) const = 0;
    // Sufficient material-only dead-position cases; false is not proof of life.
    [[nodiscard]] virtual bool known_dead_position(const Position& position) const = 0;
    // Identity includes pieces, turn, rights and legally available en passant.
    // Neither key includes clocks or history; never a complete search-cache key.
    [[nodiscard]] virtual std::uint64_t position_key(const Position& position) const = 0;
    [[nodiscard]] virtual std::uint64_t repetition_key(const Position& position) const = 0;
    // A detached snapshot survives changes to/deletion of the source position.
    [[nodiscard]] virtual std::unique_ptr<const PositionSnapshot> snapshot(const Position& position) const = 0;
};

[[nodiscard]] std::unique_ptr<ChessCore> create_chess_core();

}
