#ifdef M7_LEGACY
#include "legacy_abi_v1.h"
#else
#include <chess_lab/chess_eval_abi_v1.h>
#endif
#include <cstdlib>
#include <cstring>
extern "C" ChessEvalStatus CHESS_EVAL_CALL chess_lab_material_provider(uint64_t,uint64_t,void*,uint64_t,char*,uint64_t);
#ifndef M7_LEGACY
ChessEvalStatus CHESS_EVAL_CALL calibration(ChessEvalHandle,char*o,uint64_t n,uint64_t*r,char*e,uint64_t en){if(en)e[0]=0;const auto *value=std::getenv("CHESS_LAB_TEST_CALIBRATION");if(!value)value="{\"schema_version\":1,\"target\":\"centipawns\",\"kind\":\"linear\",\"scale\":100}";*r=std::strlen(value)+1;if(!o&&!n)return 0;if(n<*r)return CHESS_EVAL_BUFFER_TOO_SMALL;std::memcpy(o,value,*r);return 0;}
#endif
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(uint64_t a,uint64_t b,void*o,uint64_t n,char*e,uint64_t en){
#ifdef M7_LEGACY
 if(b>0){if(en)e[0]=0;return CHESS_EVAL_INCOMPATIBLE_ABI;}
#endif
 auto s=chess_lab_material_provider(a,0,o,n,e,en);
#ifndef M7_LEGACY
 if(!s&&b>=1&&n>=80){auto*v=static_cast<ChessEvalApiV1_1*>(o);v->v1.struct_size=80;v->v1.abi_minor=1;v->get_calibration_json=calibration;}
#endif
 return s;
}
