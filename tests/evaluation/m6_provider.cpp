#include "material.hpp"
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
namespace {
ChessEvalApi original;
int calls=0;
ChessEvalStatus CHESS_EVAL_CALL evaluate(ChessEvalHandle h,const ChessEvalPosition *p,double *out,char *e,uint64_t n) {
  ++calls;
  const char *delay=std::getenv("CHESS_LAB_TEST_EVAL_DELAY_MS");
  if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(delay)));
  const char *fail=std::getenv("CHESS_LAB_TEST_EVAL_FAIL_AT");
  if(fail&&calls>=std::atoi(fail))return CHESS_EVAL_INTERNAL_ERROR;
  return original.evaluate(h,p,out,e,n);
}
}
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(uint64_t a,uint64_t b,void *o,uint64_t n,char *e,uint64_t en) {
  auto status=chess_lab_material_provider(a,b,o,n,e,en);
  if(!status){original=*static_cast<ChessEvalApi*>(o);static_cast<ChessEvalApi*>(o)->evaluate=evaluate;}
  return status;
}
