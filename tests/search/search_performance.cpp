// Fixed initial-position workload. No exhaustive/MultiPV reference invocation.
#include <chess_lab/search.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
#include "performance_counters.hpp"
#endif
using namespace chess_lab;
double process_cpu_ms() {
#ifdef _WIN32
  FILETIME creation,exit,kernel,user;
  if(!GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user))throw std::runtime_error("GetProcessTimes failed");
  auto ticks=[](FILETIME t){return (std::uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
  return (ticks(kernel)+ticks(user))/10000.0;
#else
  return 1000.0*std::clock()/CLOCKS_PER_SEC;
#endif
}
std::uint64_t checksum(const std::vector<CompletedDepth>& depths) {
  std::uint64_t h=1469598103934665603ULL;
  for(const auto& d:depths) {
    const auto& v=d.best;std::ostringstream s;
    s<<d.depth<<':'<<int(v.decision.kind)<<':'<<v.decision.move<<':'<<int(v.score.kind())<<':';
    if(v.score.kind()==SearchScore::Kind::finite)s<<std::hexfloat<<(v.score.value()==0?0.0:v.score.value());else s<<v.score.distance();
    s<<':'<<int(v.pv_end)<<':'<<v.pv_intended_move<<':';
    for(const auto& m:v.principal_variation)s<<m<<',';
    for(unsigned char c:s.str())h=(h^c)*1099511628211ULL;
  }
  return h;
}
int main(int argc,char** argv)try {
  if(argc!=7)throw std::invalid_argument("usage: search_performance MODULE DEPTH ITERATIONS WARMUP off|cold|warm TT_BYTES");
  int depth=std::stoi(argv[2]),iterations=std::stoi(argv[3]),warmup=std::stoi(argv[4]);
  std::string mode=argv[5];auto budget=std::stoull(argv[6]);
  if(depth<1||depth>10||iterations<1||iterations>1000||warmup<0||warmup>100||
     (mode!="off"&&mode!="cold"&&mode!="warm")||(mode!="off"&&budget<4096))throw std::invalid_argument("invalid workload arguments");
  auto core=create_chess_core();auto root=core->initial_position();auto fen=core->export_fen(*root);
  auto history=GameHistory::starting_at(*core,*root,true);
  auto module=EvaluatorModule::native(argv[1]);auto evaluator=module.create("{}");
  TranspositionTable table(mode=="off"?0:budget);
  std::cout<<std::setprecision(12)<<"build="<<CHESS_LAB_BENCH_BUILD<<" compiler="<<CHESS_LAB_BENCH_COMPILER
    <<" mode="<<mode<<" depth="<<depth<<" iterations="<<iterations<<" warmup="<<warmup
    <<" requested_bytes="<<budget<<" allocated_bytes="<<table.capacity_bytes()
    <<" history_complete=1 multipv=1 play_only=1 previous_pv=1 alpha_beta=1\n"
    <<"fen="<<fen<<"\nartifact="<<evaluator->identity().artifact_sha256<<"\nfingerprint="<<evaluator->identity().fingerprint
    <<"\nconfig="<<evaluator->identity().canonical_configuration<<std::endl;
  std::optional<std::uint64_t> expected;
  auto invoke=[&](int run) {
    if(mode=="cold")table.clear(); // cache clearing is outside the timed interval
    std::vector<CompletedDepth> depths;depths.reserve(depth);
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::reset();
#endif
    auto cpu=process_cpu_ms();auto start=std::chrono::steady_clock::now();
    auto r=iterative_search(*core,*root,history,*evaluator,{depth,true,1,true,true},{},
      [&](CompletedDepth d){depths.push_back(std::move(d));},
      {mode=="off"?nullptr:&table,EvaluatorCacheCompatibility::deterministic_position,1});
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    double cpu_ms=process_cpu_ms()-cpu;
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::active=false;
#endif
    if(r.status!=IterativeSearchStatus::depth_reached||!r.completed||r.completed->depth!=depth||depths.size()!=std::size_t(depth))throw std::runtime_error("incomplete benchmark: "+r.diagnostic);
    if(core->export_fen(*root)!=fen||!history.matches_root(*core,*root))throw std::runtime_error("root/history changed");
    auto hash=checksum(depths);if(expected&&*expected!=hash)throw std::runtime_error("nonrepeatable exact score/PV");expected=hash;
    if(run<0)return;
    const auto& s=r.statistics;const auto& t=s.transposition;
    std::cout<<"run="<<run<<" ms="<<ms<<" cpu_ms="<<cpu_ms<<" nodes="<<s.visited_nodes
      <<" evals="<<s.evaluator_calls<<" cutoffs="<<s.alpha_beta_cutoffs<<" probes="<<t.lookups
      <<" candidates="<<t.candidate_hits<<" exact="<<t.exact_hits<<" bounds="<<t.bound_hits
      <<" bound_cutoffs="<<t.bound_cutoffs<<" hints="<<t.hint_hits<<" rejects="<<t.context_rejections
      <<" stores="<<t.stores<<" evictions="<<t.evictions<<" pv_researches="<<t.pv_researches
      <<" verification_nodes="<<t.verified_history_nodes<<" verification_ms="<<t.verification_elapsed.count()/1e6
      <<" retained="<<table.entry_count()<<" checksum="<<hash<<" best="<<r.completed->best.decision.move<<" pv=";
    for(const auto& m:r.completed->best.principal_variation)std::cout<<m<<',';
    std::cout<<std::endl;
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::report();
#endif
  };
  if(mode=="warm")invoke(-1); // explicit population even when warmup=0
  for(int i=-warmup;i<iterations;++i)invoke(i);
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}