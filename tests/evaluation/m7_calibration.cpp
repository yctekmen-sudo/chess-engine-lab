#include <chess_lab/evaluation.hpp>
#include <chess_lab/search.hpp>
#include "material.hpp"
#include <iostream>
#include <type_traits>
#include <cstring>
using namespace chess_lab;
static ChessEvalApi original;
static int mode=0;
static void check(bool b){if(!b)throw std::runtime_error("integer ABI contract check");}
template<class F> void rejects(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("expected rejection");}
static ChessEvalStatus CHESS_EVAL_CALL output(ChessEvalHandle h,const ChessEvalPosition *p,ChessEvalCentipawns *out,char *e,uint64_t n){
 if(mode==1){*out=CHESS_EVAL_CP_MAX+1;return 0;}if(mode==2){*out=CHESS_EVAL_CP_MIN-1;return 0;}if(mode==3)return 0;
 if(mode==4){*out=CHESS_EVAL_CP_MAX;return 0;}if(mode==5){*out=CHESS_EVAL_CP_MIN;return 0;}return original.evaluate(h,p,out,e,n);
}
static ChessEvalStatus CHESS_EVAL_CALL provider(uint64_t a,uint64_t b,void*o,uint64_t n,char*e,uint64_t en){auto status=chess_lab_material_provider(a,b,o,n,e,en);if(!status){original=*static_cast<ChessEvalApi*>(o);static_cast<ChessEvalApi*>(o)->evaluate=output;}return status;}
static ChessEvalStatus CHESS_EVAL_CALL legacy(uint64_t a,uint64_t,void*,uint64_t,char*e,uint64_t en){if(en)e[0]=0;check(a==2);return CHESS_EVAL_INCOMPATIBLE_ABI;}
int main(){try{
 static_assert(sizeof(SearchScore)==4);static_assert(sizeof(ChessEvalCentipawns)==4);static_assert(sizeof(ChessEvalApi)==72);
 auto core=create_chess_core();auto pos=core->load_fen("4k3/8/8/8/8/8/B7/4K3 w - - 0 1");auto snap=evaluation_snapshot(*core->snapshot(*pos));
 auto module=EvaluatorModule::provider(provider,std::string(64,'a'),"cp-fixture");auto good=module.create("{}");check(good->evaluate(snap)==325);check(good->identity().abi_major==2);
 for(int m:{1,2,3}){mode=m;auto bad=module.create("{}");rejects([&]{bad->evaluate(snap);});check(!bad->usable());rejects([&]{bad->evaluate(snap);});}
 mode=4;check(good->evaluate(snap)==CHESS_EVAL_CP_MAX);mode=5;check(good->evaluate(snap)==CHESS_EVAL_CP_MIN);mode=0;
 rejects([&]{EvaluatorModule::provider(legacy,std::string(64,'b'),"legacy");});
 auto alternate=module.create("{\"bishop_cp\":326}");check(alternate->evaluate(snap)==326);check(alternate->identity().fingerprint!=good->identity().fingerprint);
 for(auto c:{"{\"bishop_value\":3.25}","{\"bishop_cp\":3.25}","{\"bishop_cp\":16001}"})rejects([&]{module.create(c);});
 ChessEvalApi api{};check(chess_lab_material_provider(1,0,&api,sizeof api,nullptr,0)==CHESS_EVAL_INCOMPATIBLE_ABI);check(chess_lab_material_provider(2,0,&api,sizeof api,nullptr,0)==0);
 std::cout<<"integer centipawn ABI v2 range, quarantine, units and legacy rejection passed\n";return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
