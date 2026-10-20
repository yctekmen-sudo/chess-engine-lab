#define CHESS_EVAL_DECLARE_NATIVE_ENTRYPOINT
#include <chess_lab/chess_eval_abi_v1.h>
extern "C" ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(
    uint64_t, uint64_t, void *, uint64_t, char *error, uint64_t capacity) {
  if (error && capacity)
    error[0] = 0;
  return CHESS_EVAL_INCOMPATIBLE_ABI;
}
