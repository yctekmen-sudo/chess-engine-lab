#include "material.hpp"
#include <chess_lab/evaluation.hpp>
#include <chess_lab/chess_eval_abi_v1.h>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace chess_lab;
static_assert(sizeof(ChessEvalApi)==72 && sizeof(ChessEvalApiV1_1)==80);
static_assert(offsetof(ChessEvalApiV1_1,get_calibration_json)==72);
std::string descriptor;
ChessEvalStatus CHESS_EVAL_CALL calibration(ChessEvalHandle,char *o,uint64_t n,uint64_t*r,char*e,uint64_t en){if(en)e[0]=0;*r=descriptor.size()+1;if(!o&&!n)return 0;if(n<*r)return CHESS_EVAL_BUFFER_TOO_SMALL;std::memcpy(o,descriptor.c_str(),*r);return 0;}
ChessEvalStatus CHESS_EVAL_CALL provider(uint64_t a,uint64_t b,void *o,uint64_t n,char*e,uint64_t en){auto s=chess_lab_material_provider(a,0,o,n,e,en);if(!s&&b>=1&&n>=80){auto *v=static_cast<ChessEvalApiV1_1*>(o);v->v1.struct_size=80;v->v1.abi_minor=1;v->get_calibration_json=calibration;}return s;}
void check(bool b){if(!b)throw std::runtime_error("calibration check failed");}
int main(){auto module=EvaluatorModule::provider(provider,std::string(64,'a'),"calibration-fixture");
 descriptor=R"({"schema_version":1,"target":"centipawns","kind":"linear","scale":100})";auto a=module.create("{}");check(a->centipawns(.005)==1);check(a->centipawns(-.005)==-1);check(!a->centipawns(1e300));check(!a->centipawns(-1e300));check(!a->centipawns(std::numeric_limits<double>::infinity()));check(!a->centipawns(std::numeric_limits<double>::quiet_NaN()));check(a->calibration_digest().size()==64);
 descriptor=R"({"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only","points":[[-3,-600],[-1,-100],[0,0],[1,100],[3,600]]})";auto table=module.create("{}");check(table->centipawns(2)==350);check(table->centipawns(-2)==-350);check(table->centipawns(3)==600);check(!table->centipawns(4));check(table->calibration_digest()!=a->calibration_digest());check(table->identity().fingerprint==a->identity().fingerprint);
 for(const auto &bad:{"{}",R"({"schema_version":1,"target":"centipawns","kind":"linear","scale":-1})",R"({"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only","points":[[-1,3],[0,0],[1,1]]})"}){descriptor=bad;auto x=module.create("{}");check(!x->centipawns(1));check(x->usable());}
 descriptor=R"({"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only","points":[[-1,-100],[1,100]]})";auto missing_zero=module.create("{}");check(!missing_zero->centipawns(0));check(missing_zero->usable());
 descriptor=R"({"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only","points":[[-1e308,-1e308],[0,0],[1e308,1e308]]})";auto extreme=module.create("{}");check(extreme->centipawns(-1)==-1);check(extreme->centipawns(1)==1);check(extreme->centipawns(0)==0);
 descriptor=std::string(65537,'x');auto oversized=module.create("{}");check(!oversized->centipawns(1));check(oversized->usable());
 ChessEvalApiV1_1 legacy_request{};legacy_request.get_calibration_json=calibration;
 check(provider(1,0,&legacy_request,72,nullptr,0)==0);check(legacy_request.v1.struct_size==72);check(legacy_request.get_calibration_json==calibration);
 auto configured=module.create("{\"pawn_value\":2}");check(configured->identity().fingerprint!=a->identity().fingerprint);
 auto old=EvaluatorModule::provider(chess_lab_material_provider,std::string(64,'b'),"old").create("{}");check(!old->centipawns(1));check(old->identity().abi_minor==0);std::cout<<"M7 legacy72/extended80 calibration native/WASM checks passed\n";
}
