#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace chess_lab {

enum class Side : std::uint8_t { white, black };
enum class PieceCode : std::uint8_t {
    empty,
    white_pawn, white_knight, white_bishop, white_rook, white_queen, white_king,
    black_pawn, black_knight, black_bishop, black_rook, black_queen, black_king
};

// Source-level read-only view of a detached current-position snapshot, owned by
// the caller. No history, adapter objects, evaluation, or adjudication here.
// Squares: a1=0, b1=1, ..., h8=63. No serialization/C ABI layout is committed.
class PositionSnapshot {
public:
    virtual ~PositionSnapshot() = default;
    [[nodiscard]] virtual std::span<const PieceCode, 64> squares() const noexcept = 0;
    [[nodiscard]] virtual Side side_to_move() const noexcept = 0;
    [[nodiscard]] virtual bool can_castle_kingside(Side side) const noexcept = 0;
    [[nodiscard]] virtual bool can_castle_queenside(Side side) const noexcept = 0;
    [[nodiscard]] virtual std::optional<std::uint8_t> en_passant_square() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t halfmove_clock() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t fullmove_number() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t schema_version() const noexcept = 0;
    // Concrete schema/version values and C export/ownership rules await M2.
};

}
