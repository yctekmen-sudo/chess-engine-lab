#pragma once
#include <chess_lab/chess_eval_abi_v2.h>
extern "C" ChessEvalStatus CHESS_EVAL_CALL
chess_lab_e1_provider(uint64_t major, uint64_t minor, void *out,
                      uint64_t n, char *e, uint64_t en);
