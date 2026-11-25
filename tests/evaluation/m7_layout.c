#include <chess_lab/chess_eval_abi_v2.h>
#include <stddef.h>
_Static_assert(sizeof(ChessEvalApi)==72,"v2 table size");
_Static_assert(_Alignof(ChessEvalApi)==8,"v2 table alignment");
_Static_assert(offsetof(ChessEvalApi,evaluate)==56,"score slot");
_Static_assert(sizeof(ChessEvalCentipawns)==4,"centipawn size");
_Static_assert(CHESS_EVAL_ABI_MAJOR==2,"new incompatible ABI major");
int main(void){return 0;}
