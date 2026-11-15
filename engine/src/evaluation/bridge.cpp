#include "snapshot.hpp"
#include <chess_lab/evaluation.hpp>
namespace chess_lab {
ChessEvalPosition evaluation_snapshot(const PositionSnapshot &s) {
  if (s.schema_version() != 1)
    throw EvaluationError("unsupported source snapshot schema");
  ChessEvalPosition p{};
  p.struct_size = sizeof p;
  p.schema_major = 1;
  const auto squares = s.squares();
  // Slot zero collects empty squares; masked indices also keep malformed
  // piece codes memory-safe until the original error is reported.
  uint64_t acc[16] = {};
  unsigned invalid = 0;
  for (size_t i = 0; i < 64; ++i) {
    const auto code = static_cast<unsigned>(squares[i]);
    invalid |= static_cast<unsigned>(code > 12);
    acc[code & 15U] |= uint64_t{1} << i;
  }
  if (invalid)
    throw EvaluationError("invalid source piece");
  for (unsigned code = 1; code <= 12; ++code)
    p.bitboards[code - 1] = acc[code];
  if (s.side_to_move() != Side::white && s.side_to_move() != Side::black)
    throw EvaluationError("invalid source side");
  p.side_to_move = s.side_to_move() == Side::black;
  p.castling_rights = s.can_castle_kingside(Side::white) |
                      (s.can_castle_queenside(Side::white) << 1) |
                      (s.can_castle_kingside(Side::black) << 2) |
                      (s.can_castle_queenside(Side::black) << 3);
  p.en_passant_square =
      s.en_passant_square() ? *s.en_passant_square() : CHESS_EVAL_EP_NONE;
  p.halfmove_clock = s.halfmove_clock();
  p.fullmove_number = s.fullmove_number();
  if (eval_detail::validate(p) != CHESS_EVAL_OK)
    throw EvaluationError("invalid source snapshot");
  return p;
}
} // namespace chess_lab
