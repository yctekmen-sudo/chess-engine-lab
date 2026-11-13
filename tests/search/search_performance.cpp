// Fixed initial-position workload. No exhaustive/MultiPV reference invocation.
#include <chess_lab/search.hpp>
#include <bit>
#include <cstdint>
#include <string_view>
#include <chrono>
#include <cstdlib>
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
extern "C" void celab_t12a5_set_policy(int) noexcept;
extern "C" void celab_t12a5_set_touch(int) noexcept;
extern "C" void celab_t12a5_reset() noexcept;
extern "C" void celab_t12a5_dump();
extern "C" void celab_t12a6_reset() noexcept;
extern "C" void celab_t12a6_dump();
extern "C" void celab_t12a7_reset() noexcept;
extern "C" void celab_t12a7_dump();
extern "C" int celab_t13a_config() noexcept;
extern "C" void celab_t13a_reset() noexcept;
extern "C" void celab_t13a_dump() noexcept;
extern "C" void celab_audit_enable(int);
extern "C" void celab_audit_reset();
extern "C" void celab_audit_dump();
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
// Stable FNV-1a over explicit little-endian fields. Do not serialize floating
// scores with iostreams: equivalent MSVC/GCC hexfloat spellings differ.
static void checksum_u64(std::uint64_t& h, std::uint64_t value) {
  for (int i=0;i<8;++i) {
    h=(h^static_cast<unsigned char>(value & 255U))*1099511628211ULL;
    value>>=8;
  }
}
static void checksum_text(std::uint64_t& h, std::string_view text) {
  checksum_u64(h, text.size());
  for (unsigned char c:text) h=(h^c)*1099511628211ULL;
}
std::uint64_t checksum(const std::vector<CompletedDepth>& depths) {
  std::uint64_t h=1469598103934665603ULL;
  for(const auto& d:depths) {
    const auto& v=d.best;
    checksum_u64(h,static_cast<std::uint64_t>(d.depth));
    checksum_u64(h,static_cast<std::uint64_t>(v.score.kind()));
    const bool finite=v.score.kind()==SearchScore::Kind::finite;
    const double normalized=finite && v.score.value()!=0.0 ? v.score.value() : 0.0;
    checksum_u64(h,finite?std::bit_cast<std::uint64_t>(normalized):0);
    checksum_u64(h,finite?0:v.score.distance());
    checksum_u64(h,static_cast<std::uint64_t>(v.decision.kind));
    checksum_text(h,v.decision.move);
    checksum_u64(h,v.decision.terminal_outcome.has_value()?1:0);
    checksum_u64(h,v.decision.terminal_outcome
        ?static_cast<std::uint64_t>(*v.decision.terminal_outcome):0);
    checksum_u64(h,static_cast<std::uint64_t>(v.pv_end));
    checksum_text(h,v.pv_intended_move);
    checksum_u64(h,v.principal_variation.size());
    for(const auto& m:v.principal_variation) checksum_text(h,m);
  }
  return h;
}

int main(int argc,char** argv)try {
  if(argc!=7 && argc!=8 && argc!=9)throw std::invalid_argument("usage: search_performance MODULE DEPTH ITERATIONS WARMUP off|cold|warm|hintcold|hintwarm TT_BYTES [FEN] [ACTUAL_REPLAY_MOVES]");
  int depth=std::stoi(argv[2]),iterations=std::stoi(argv[3]),warmup=std::stoi(argv[4]);
  std::string mode=argv[5];auto budget=std::stoull(argv[6]);
  const char* prime_env=std::getenv("CELAB_T12A2_PRIME_DEPTH");
  int prime_depth=prime_env?std::stoi(prime_env):0;
  if(prime_depth<0 || prime_depth>10 ||
     (prime_depth && mode!="warm" && mode!="hintwarm"))
    throw std::invalid_argument("prime depth requires warm mode and is between 1 and 10");
  if(depth<1||depth>10||iterations<1||iterations>1000||warmup<0||warmup>100||
     (mode!="off"&&mode!="cold"&&mode!="warm"&&mode!="hintcold"&&mode!="hintwarm")||(mode!="off"&&budget<4096))throw std::invalid_argument("invalid workload arguments");
  auto env=std::getenv("CELAB_TT_AUDIT");
  int audit_mode=env&&std::string_view(env)=="nodes"?4:env&&std::string_view(env)=="trace"?3:env&&std::string_view(env)=="heavy"?2:env&&std::string_view(env)=="light"?1:0;
  celab_audit_enable(audit_mode);
  auto core=create_chess_core();auto root=argc>=8?core->load_fen(argv[7]):core->initial_position();
  auto history=GameHistory::starting_at(*core,*root,true);
  if(argc==9) {
    std::string replay=argv[8];
    for (std::size_t first=0;first<replay.size();) {
      auto last=replay.find(',',first);
      auto text=replay.substr(first,last==std::string::npos?last:last-first);
      if(text.empty())throw std::invalid_argument("empty actual replay move");
      history=history.after_move(*core,*root,text);
      auto move=core->decode_move(*root,text);
      auto token=core->make_move(*root,*move);
      if(last==std::string::npos)break;
      first=last+1;
    }
  }
  auto fen=core->export_fen(*root);
  auto module=EvaluatorModule::native(argv[1]);auto evaluator=module.create("{}");
  TranspositionTable table(mode=="off"?0:budget);
  std::cout<<std::setprecision(12)<<"build="<<CHESS_LAB_BENCH_BUILD<<" compiler="<<CHESS_LAB_BENCH_COMPILER
    <<" t13a_mode="<<celab_t13a_config()<<" mode="<<mode<<" depth="<<depth<<" prime_depth="<<prime_depth<<" iterations="<<iterations<<" warmup="<<warmup
    <<" requested_bytes="<<budget<<" allocated_bytes="<<table.capacity_bytes()
    <<" history_complete=1 multipv=1 play_only=1 previous_pv=1 alpha_beta=1\n"
    <<"fen="<<fen<<"\nartifact="<<evaluator->identity().artifact_sha256<<"\nfingerprint="<<evaluator->identity().fingerprint
    <<"\nconfig="<<evaluator->identity().canonical_configuration<<std::endl;
  std::optional<std::uint64_t> expected;
  auto invoke=[&](int run) {
    if(mode=="cold"||mode=="hintcold")table.clear(); // cache clearing is outside the timed interval
    std::vector<CompletedDepth> depths;depths.reserve(depth);
    if(audit_mode){celab_audit_reset();celab_t12a5_reset();celab_t12a6_reset();celab_t12a7_reset();celab_t13a_reset();}
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::reset();
#endif
    auto cpu=process_cpu_ms();auto start=std::chrono::steady_clock::now();
    auto r=iterative_search(*core,*root,history,*evaluator,{depth,true,1,true,true},{},
      [&](CompletedDepth d){
        depths.push_back(std::move(d));
        if(audit_mode>=3 && run>=0) {
          const auto &v=depths.back().best;
          const auto &st=depths.back().statistics;
          const auto prior=depths.size()>1 ? depths[depths.size()-2].statistics.visited_nodes : 0;
          const auto eval_prior=depths.size()>1 ? depths[depths.size()-2].statistics.evaluator_calls : 0;
          std::cerr<<"CELAB_ITER depth="<<depths.back().depth
            <<" nodes="<<st.visited_nodes<<" delta_nodes="<<(st.visited_nodes-prior)
            <<" evals="<<st.evaluator_calls<<" delta_evals="<<(st.evaluator_calls-eval_prior)
            <<" bounds="<<st.transposition.bound_cutoffs
            <<" checksum="<<checksum(depths)<<" best="<<v.decision.move
            <<" score_kind="<<int(v.score.kind());
          if(v.score.kind()==SearchScore::Kind::finite)std::cerr<<" score="<<v.score.value();
          else std::cerr<<" score_distance="<<v.score.distance();
          std::cerr<<" pv=";
          for(const auto &move:v.principal_variation)std::cerr<<move<<',';
          std::cerr<<'\n';
        }
      },
      {mode=="off"?nullptr:&table,mode.rfind("hint",0)==0?EvaluatorCacheCompatibility::not_certified:EvaluatorCacheCompatibility::history_independent_position,1});
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    double cpu_ms=process_cpu_ms()-cpu;
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::active=false;
#endif
    if(r.status!=IterativeSearchStatus::depth_reached||!r.completed||r.completed->depth!=depth||depths.size()!=std::size_t(depth))throw std::runtime_error("incomplete benchmark: "+r.diagnostic);
    if(core->export_fen(*root)!=fen||!history.matches_root(*core,*root))throw std::runtime_error("root/history changed");
    // Guard that every reported principal decision is legal in the actual root.
    const auto &decision=r.completed->best.decision;
    if(decision.kind==DecisionKind::play ||
       decision.kind==DecisionKind::claim_draw_with_intended_move) {
      const auto& text=decision.kind==DecisionKind::play ? decision.move :
                       r.completed->best.pv_intended_move;
      bool found=false;
      for(const auto &m:core->legal_moves(*root)) {
        if(core->encode_move(*root,*m)==text) {found=true;break;}
      }
      if(!found)throw std::runtime_error("illegal principal root move: "+text);
    }
    auto hash=checksum(depths);
    // A warm population run is allowed to differ from the measured search:
    // that difference is precisely the effect under investigation. Only
    // repeated RECORDED searches within this process require equality, and
    // the Python runner independently checks fresh-process repeatability.
    if(run<0)return;
    if(expected&&*expected!=hash)throw std::runtime_error("nonrepeatable measured score/PV");
    expected=hash;
    const auto& s=r.statistics;const auto& t=s.transposition;
    std::cout<<"run="<<run<<" ms="<<ms<<" cpu_ms="<<cpu_ms<<" nodes="<<s.visited_nodes
      <<" evals="<<s.evaluator_calls<<" cutoffs="<<s.alpha_beta_cutoffs<<" probes="<<t.lookups
      <<" candidates="<<t.candidate_hits<<" exact="<<t.exact_hits<<" bounds="<<t.bound_hits
      <<" bound_cutoffs="<<t.bound_cutoffs<<" hints="<<t.hint_hits<<" rejects="<<t.context_rejections
      <<" stores="<<t.stores<<" evictions="<<t.evictions<<" pv_researches="<<t.pv_researches
      <<" verification_nodes="<<t.verified_history_nodes<<" verification_ms="<<t.verification_elapsed.count()/1e6
      <<" retained="<<table.entry_count()<<" root_legal=1 checksum="<<hash<<" best="<<r.completed->best.decision.move<<" pv=";
    for(const auto& m:r.completed->best.principal_variation)std::cout<<m<<',';
    std::cout<<std::endl;
    if(audit_mode){celab_audit_dump();celab_t12a5_dump();celab_t12a6_dump();celab_t12a7_dump();celab_t13a_dump();}
#ifdef CHESS_LAB_PERF_DIAGNOSTICS
    perf_diag::report();
#endif
  };
  // A matched-prime control populates TT under EXACT depth before switching
  // to the experimental policy. No TT clear occurs between prime and probe.
  const char* canonical_prime_env=std::getenv("CELAB_T12A5_PRIME_POLICY");
  const bool matched_prime=canonical_prime_env &&
      std::string_view(canonical_prime_env)=="exact";
  if (canonical_prime_env && !matched_prime)
    throw std::invalid_argument("CELAB_T12A5_PRIME_POLICY must be exact or unset");
  std::cout<<"t12a5_prime="<<(matched_prime?"matched_exact":"native_policy")<<std::endl;
  if (matched_prime) { celab_t12a5_set_policy(0);celab_t12a5_set_touch(0); }
  // Do not log the untimed warm-population tree; diagnostics describe only
  // the measured search, which begins from the same prepopulated TT.
  if (audit_mode>=3) celab_audit_enable(0);
  if (prime_depth) {
    // Alternative, reproducible TT population state: a completed search to a
    // different depth in the SAME position with the SAME table and evaluator.
    // This is outside the measured interval and must not affect the reported
    // completed-depth checksum or accounting.
    auto seeded=iterative_search(*core,*root,history,*evaluator,
      {prime_depth,true,1,true,true},{},{},
      {mode=="off"?nullptr:&table,
       mode.rfind("hint",0)==0?EvaluatorCacheCompatibility::not_certified:
       EvaluatorCacheCompatibility::history_independent_position,1});
    if(seeded.status!=IterativeSearchStatus::depth_reached || !seeded.completed ||
       seeded.completed->depth!=prime_depth ||
       core->export_fen(*root)!=fen || !history.matches_root(*core,*root))
      throw std::runtime_error("TT prime search failed or altered root history");
  }
  // Default warm remains the original full-depth population search.
  // The default warm primer is also untimed. Restore trace after it.
  if (!prime_depth && (mode=="warm" || mode=="hintwarm")) {
    invoke(-1);
  }
  if (matched_prime) { celab_t12a5_set_policy(-1);celab_t12a5_set_touch(-1); }
  if (audit_mode>=3) celab_audit_enable(audit_mode);
  for(int i=-warmup;i<iterations;++i)invoke(i);
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}