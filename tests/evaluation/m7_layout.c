#include <chess_lab/chess_eval_abi_v1.h>
#include <stddef.h>
_Static_assert(sizeof(ChessEvalApi)==72,"legacy remains 72");
_Static_assert(sizeof(ChessEvalApiV1_1)==80,"minor1 extends to 80");
_Static_assert(_Alignof(ChessEvalApiV1_1)==8,"minor1 alignment");
_Static_assert(offsetof(ChessEvalApiV1_1,v1)==0,"unchanged prefix");
_Static_assert(offsetof(ChessEvalApiV1_1,get_calibration_json)==72,"optional padded pointer slot");
int main(void){return 0;}
