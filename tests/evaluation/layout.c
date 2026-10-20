#include <chess_lab/chess_eval_abi_v1.h>
#include <stddef.h>
#ifdef __cplusplus
#define ASSERT static_assert
#define ALIGN alignof
#else
#define ASSERT _Static_assert
#define ALIGN _Alignof
#endif
ASSERT(sizeof(ChessEvalStatus) == 4, "status width");
ASSERT(sizeof(ChessEvalPosition) == 160, "size");
ASSERT(ALIGN(ChessEvalPosition) == 8, "alignment");
ASSERT(offsetof(ChessEvalPosition, struct_size) == 0,
       "struct_size offset/alignment");
ASSERT(offsetof(ChessEvalPosition, schema_major) == 8,
       "schema_major offset/alignment");
ASSERT(offsetof(ChessEvalPosition, schema_minor) == 16,
       "schema_minor offset/alignment");
ASSERT(offsetof(ChessEvalPosition, bitboards) == 24,
       "bitboards offset/alignment");
ASSERT(offsetof(ChessEvalPosition, side_to_move) == 120,
       "side_to_move offset/alignment");
ASSERT(offsetof(ChessEvalPosition, castling_rights) == 128,
       "castling_rights offset/alignment");
ASSERT(offsetof(ChessEvalPosition, en_passant_square) == 136,
       "en_passant_square offset/alignment");
ASSERT(offsetof(ChessEvalPosition, halfmove_clock) == 144,
       "halfmove_clock offset/alignment");
ASSERT(offsetof(ChessEvalPosition, fullmove_number) == 152,
       "fullmove_number offset/alignment");
ASSERT(sizeof(ChessEvalApi) == 72, "size");
ASSERT(ALIGN(ChessEvalApi) == 8, "alignment");
ASSERT(offsetof(ChessEvalApi, struct_size) == 0,
       "struct_size offset/alignment");
ASSERT(offsetof(ChessEvalApi, abi_major) == 8, "abi_major offset/alignment");
ASSERT(offsetof(ChessEvalApi, abi_minor) == 16, "abi_minor offset/alignment");
ASSERT(offsetof(ChessEvalApi, get_module_info_json) == 24,
       "get_module_info_json offset/alignment");
ASSERT(offsetof(ChessEvalApi, get_parameter_schema_json) == 32,
       "get_parameter_schema_json offset/alignment");
ASSERT(offsetof(ChessEvalApi, create_evaluator) == 40,
       "create_evaluator offset/alignment");
ASSERT(offsetof(ChessEvalApi, get_effective_configuration_json) == 48,
       "get_effective_configuration_json offset/alignment");
ASSERT(offsetof(ChessEvalApi, evaluate) == 56, "evaluate offset/alignment");
ASSERT(offsetof(ChessEvalApi, destroy_evaluator) == 64,
       "destroy_evaluator offset/alignment");
int main(void) { return 0; }
