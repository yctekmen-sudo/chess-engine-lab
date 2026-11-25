#include "snapshot.hpp"
#include "native_eval_snapshot.hpp"
#include "../native_move_batch.hpp"
#include <chess_lab/evaluation.hpp>
namespace chess_lab {
ChessEvalPosition evaluation_snapshot(const PositionSnapshot &s) {
  if (s.schema_version() != position_snapshot_schema_version)
    throw EvaluationError("unsupported source snapshot schema");
  ChessEvalPosition p{};
  p.struct_size = sizeof p;
  p.schema_major = 1;
  const auto squares = s.squares();
  // Slot twelve collects empty squares; masked indices also keep malformed
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
  for (unsigned code = 0; code < 12; ++code)
    p.bitboards[code] = acc[code];
  if (s.side_to_move() != Side::white && s.side_to_move() != Side::black)
    throw EvaluationError("invalid source side");
  p.side_to_move = s.side_to_move() == Side::black;
  // Cast bool flags to an integer before bitwise arithmetic (MSVC C4805).
  p.castling_rights =
      static_cast<uint64_t>(s.can_castle_kingside(Side::white)) |
      (static_cast<uint64_t>(s.can_castle_queenside(Side::white)) << 1) |
      (static_cast<uint64_t>(s.can_castle_kingside(Side::black)) << 2) |
      (static_cast<uint64_t>(s.can_castle_queenside(Side::black)) << 3);
  p.en_passant_square =
      s.en_passant_square() ? *s.en_passant_square() : CHESS_EVAL_EP_NONE;
  p.halfmove_clock = s.halfmove_clock();
  p.fullmove_number = s.fullmove_number();
  if (eval_detail::validate(p) != CHESS_EVAL_OK)
    throw EvaluationError("invalid source snapshot");
  return p;
}

namespace detail {
ChessEvalPosition native_evaluation_snapshot(const ChessCore &core,const Position &position,
    const PositionSnapshot &s,bool &direct,bool verify) {
  ChessEvalPosition p;
  direct=native_fill_evaluation_position(core,position,p);
  if(!direct)return evaluation_snapshot(s);
  if (eval_detail::validate(p) != CHESS_EVAL_OK)
    throw EvaluationError("invalid source snapshot");
  if(verify){
    const auto ref=evaluation_snapshot(s);
    bool same=p.struct_size==ref.struct_size && p.schema_major==ref.schema_major &&
      p.schema_minor==ref.schema_minor && p.side_to_move==ref.side_to_move &&
      p.castling_rights==ref.castling_rights && p.en_passant_square==ref.en_passant_square &&
      p.halfmove_clock==ref.halfmove_clock && p.fullmove_number==ref.fullmove_number;
    for(unsigned i=0;i<12;++i)same= same && p.bitboards[i]==ref.bitboards[i];
    if(!same)throw EvaluationError("S1.10 direct evaluation snapshot mismatch");
  }
  return p;
}
} // namespace detail
} // namespace chess_lab
