#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace chess_lab {

enum class Side : std::uint8_t { white, black };
// Canonical CELab source codes match the pinned native Rules byte encoding.
// This is a CELab-owned type; the evaluator C ABI still uses twelve bitboards.
enum class PieceCode : std::uint8_t {
    white_pawn = 0, white_knight = 1, white_bishop = 2,
    white_rook = 3, white_queen = 4, white_king = 5,
    black_pawn = 6, black_knight = 7, black_bishop = 8,
    black_rook = 9, black_queen = 10, black_king = 11,
    empty = 12
};
// Source snapshot schema 2 changes the square discriminants, NOT the C ABI.
inline constexpr std::uint32_t position_snapshot_schema_version = 2;
// Value initialization of PieceCode now denotes a white pawn, not empty.
[[nodiscard]] constexpr std::array<PieceCode,64> empty_piece_squares() noexcept {
    std::array<PieceCode,64> result{PieceCode::empty};
    result.fill(PieceCode::empty);
    return result;
}

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
    // Source schema is independent of the evaluator C ABI schema.
};

}
