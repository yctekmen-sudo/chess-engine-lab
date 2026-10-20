#include "m4_fixtures.hpp"
#include "../../engine/src/search/transposition_table_internal.hpp"
#include <iomanip>
#include <sstream>
using namespace chess_lab::detail;
uint64_t checksum(const IterativeSearchResult&r,const std::vector<CompletedDepth>&events) {
  uint64_t hash=1469598103934665603ULL;
  auto consume=[&](const std::string&s){for(unsigned char c:s)hash=(hash^c)*1099511628211ULL;};
  auto line=[&](const SearchVariation&v){std::ostringstream s;s<<int(v.decision.kind)<<':'<<v.decision.move<<':'<<int(v.score.kind())<<':';
    if(v.score.kind()==SearchScore::Kind::finite)s<<std::hexfloat<<(v.score.value()==0?0.0:v.score.value());else s<<v.score.distance();
    s<<':'<<int(v.pv_end)<<':'<<v.pv_intended_move<<':';for(auto&m:v.principal_variation)s<<m<<',';consume(s.str());};
  consume(std::to_string(int(r.status)));
  for(auto&d:events){consume(std::to_string(d.depth));line(d.best);for(auto&v:d.multipv)line(v);}
  return hash;
}
int main(int argc,char**argv)try{
  if(argc<2||argc>9)throw std::invalid_argument("usage: m5_benchmark MODULE [depth=3] [iterations=3] [warmup=1] [bytes=1048576] [initial|pawn|tree|history|collision|small] [N=3] [PV-order=1]");
  int depth=argc>2?std::stoi(argv[2]):3,iterations=argc>3?std::stoi(argv[3]):3,warmup=argc>4?std::stoi(argv[4]):1;
  size_t budget=argc>5?std::stoull(argv[5]):1048576,n=argc>7?std::stoull(argv[7]):3;
  std::string workload=argc>6?argv[6]:"initial";bool order=argc>8?std::stoi(argv[8])!=0:true;
  if(depth<1||depth>5||iterations<1||warmup<0)throw std::invalid_argument("bounded benchmark depth 1..5; bad repetitions");
  std::unique_ptr<ChessCore>core;std::unique_ptr<Position>root;
  GameHistory history;std::string original_fen,replay;
  if(workload=="tree"||workload=="history"){
    auto t=std::make_unique<Tree>();int stem=workload=="history"?256:0;
    for(int i=0;i<stem;++i)t->nodes.push_back({i%2?Side::black:Side::white,{i+1}});
    std::function<int(int,Side)>generate=[&](int d,Side side){int id=t->nodes.size();t->nodes.push_back({side,{}});
      if(d)for(int i=0;i<3;++i){int child=generate(d-1,side==Side::white?Side::black:Side::white);t->nodes[id].children.push_back(child);}
      else t->nodes[id].children.push_back(id);return id;};
    generate(depth,Side::white);root=t->initial_position();history=GameHistory::starting_at(*t,*root,true);
    for(int i=0;i<stem;++i){if(i)replay+=',';replay+="a1a2";history=history.after_move(*t,*root,"a1a2");auto m=t->decode_move(*root,"a1a2");auto u=t->make_move(*root,*m);}
    original_fen="synthetic-tree:id=0;ternary-DFS;stem="+std::to_string(stem)+";generated-depth="+std::to_string(depth);core=std::move(t);
  }else{
    if(workload!="initial"&&workload!="pawn"&&workload!="collision"&&workload!="small")throw std::invalid_argument("unknown workload");
    original_fen=workload=="pawn"?"4k3/8/8/8/8/8/P7/4K3 b - - 0 1":"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    core=create_chess_core();root=core->load_fen(original_fen);history=GameHistory::starting_at(*core,*root);
  }
  if(workload=="small")budget=8192;
  auto module=EvaluatorModule::native(argv[1]);auto e=module.create("{}");TranspositionTable table(budget);
  if(workload=="collision")TTAccess::force_collisions(table,true);
  std::cout<<std::setprecision(12)<<"build="<<CHESS_LAB_BENCH_BUILD<<" compiler="<<CHESS_LAB_BENCH_COMPILER<<" platform="<<CHESS_LAB_BENCH_PLATFORM
    <<"\nworkload="<<workload<<" original="<<original_fen<<"\nroot="<<core->export_fen(*root)<<"\nreplay="<<replay
    <<"\nhistory_complete="<<history.complete()<<" occurrences="<<history.occurrences().size()
    <<"\nmodule="<<e->identity().module_id<<" provider="<<e->identity().provider<<"\nartifact="<<e->identity().artifact_sha256<<"\nfingerprint="<<e->identity().fingerprint
    <<"\nconfig="<<e->identity().canonical_configuration<<"\nmax_depth="<<depth<<" N="<<n<<" alpha_beta=1 previous_PV="<<order
    <<" warmup="<<warmup<<" iterations="<<iterations<<" requested_bytes="<<budget<<" allocated_bytes="<<table.capacity_bytes()<<" certified=1 policy=1\n";
  std::optional<uint64_t>expected;
  for(std::string state:{"off","cold","warm"}){
    auto invoke=[&]{std::vector<CompletedDepth>events;
      auto r=iterative_search(*core,*root,history,*e,{depth,true,n,order},{},[&](CompletedDepth d){events.push_back(std::move(d));},
           {state=="off"?nullptr:&table,EvaluatorCacheCompatibility::deterministic_position,1});
      if(r.status!=IterativeSearchStatus::depth_reached)throw std::runtime_error(r.diagnostic);
      auto hash=checksum(r,events);if(expected&&hash!=*expected)throw std::runtime_error("TT state changed exact result/PV checksum");expected=hash;
      return std::pair(std::move(r),hash);};
    table.clear();
    if(state=="warm"){auto populated=invoke();std::cout<<"warm_population_nodes="<<populated.first.statistics.visited_nodes<<" warm_population_entries="<<table.entry_count()<<'\n';}
    for(int i=-warmup;i<iterations;++i){if(state=="cold")table.clear();auto [r,hash]=invoke();if(i<0)continue;
      auto&s=r.statistics;auto&t=s.transposition;
      std::cout<<"state="<<state<<" run="<<i<<" nodes="<<s.visited_nodes<<" evals="<<s.evaluator_calls<<" cutoffs="<<s.alpha_beta_cutoffs<<" ms="<<s.elapsed.count()/1e6
        <<" probes="<<t.lookups<<" candidates="<<t.candidate_hits<<" exact="<<t.exact_hits<<" bounds="<<t.bound_hits<<" bound_cutoffs="<<t.bound_cutoffs<<" hints="<<t.hint_hits<<" rejects="<<t.context_rejections
        <<" stores="<<t.stores<<" evictions="<<t.evictions<<" skipped_numeric="<<t.skipped_stores<<" pv_researches="<<t.pv_researches<<" verification_nodes="<<t.verified_history_nodes
        <<" verification_ms="<<t.verification_elapsed.count()/1e6<<" retained_entries="<<table.entry_count()<<" checksum="<<hash<<'\n';
    }
  }
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
