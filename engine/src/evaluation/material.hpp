#pragma once
#include <chess_lab/chess_eval_abi_v1.h>
extern "C" ChessEvalStatus CHESS_EVAL_CALL chess_lab_material_provider(
    uint64_t, uint64_t, void *, uint64_t, char *, uint64_t);
