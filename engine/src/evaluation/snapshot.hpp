#pragma once
#include <bit>
#include <chess_lab/chess_eval_abi_v1.h>
namespace chess_lab::eval_detail {
inline ChessEvalStatus validate(const ChessEvalPosition &p) noexcept {
  if (p.struct_size < 160 || p.schema_major != 1)
    return CHESS_EVAL_INCOMPATIBLE_SNAPSHOT;
  if (p.side_to_move > 1 || p.castling_rights > 15 ||
      (p.en_passant_square != CHESS_EVAL_EP_NONE &&
       (p.en_passant_square > 63 ||
        (p.en_passant_square / 8 != 2 && p.en_passant_square / 8 != 5))) ||
      p.fullmove_number == 0)
    return CHESS_EVAL_INVALID_ARGUMENT;
  uint64_t occupied = 0;
  for (auto b : p.bitboards) {
    if (occupied & b)
      return CHESS_EVAL_INVALID_ARGUMENT;
    occupied |= b;
  }
  if (std::popcount(p.bitboards[5]) != 1 ||
      std::popcount(p.bitboards[11]) != 1 ||
      ((p.bitboards[0] | p.bitboards[6]) & 0xff000000000000ffULL))
    return CHESS_EVAL_INVALID_ARGUMENT;
  return CHESS_EVAL_OK;
}
} // namespace chess_lab::eval_detail
