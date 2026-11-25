#ifdef M7_LEGACY
#include "legacy_abi_v1.h"
#include <cstring>
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(uint64_t a,uint64_t,void*o,uint64_t n,char*e,uint64_t en){
 if(en)e[0]=0;if(a!=1)return CHESS_EVAL_INCOMPATIBLE_ABI;if(n<sizeof(ChessEvalApi))return CHESS_EVAL_BUFFER_TOO_SMALL;
 // Independent frozen v1 table layout; the v2 host must not call its slots.
 ChessEvalApi api{};api.struct_size=sizeof api;api.abi_major=1;std::memcpy(o,&api,sizeof api);return 0;
}
#else
#include "material.hpp"
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(uint64_t a,uint64_t b,void*o,uint64_t n,char*e,uint64_t en){return chess_lab_material_provider(a,b,o,n,e,en);}
#endif
