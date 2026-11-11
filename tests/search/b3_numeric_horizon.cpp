// B3a: cached numeric horizon leaves must preserve exact score/PV/TT results.
#include <chess_lab/search.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace chess_lab;
static void ensure(bool b,const char* why) { if(!b)throw std::runtime_error(why); }
static bool same(const SearchVariation& a, const SearchVariation& b) {
  return a.score==b.score && a.decision==b.decision &&
         a.principal_variation==b.principal_variation &&
         a.pv_end==b.pv_end && a.pv_intended_move==b.pv_intended_move;
}
int main(int argc,char** argv) try {
  ensure(argc==2,"expected native material evaluator path");
  auto core=create_chess_core();
  auto module=EvaluatorModule::native(argv[1]);
  auto evaluator=module.create("{}");
  std::size_t verified=0;
  for (auto fen : {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
      "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
      "4k3/8/8/8/8/8/8/R3K3 w - - 98 1",
      "4k3/8/8/8/8/8/8/R3K3 w - - 99 1",
      "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"}) {
    auto root=core->load_fen(fen);
    auto history=GameHistory::starting_at(*core,*root,true);
    std::vector<CompletedDepth> reference;
    auto run=[&](TranspositionTableUse use) {
      std::vector<CompletedDepth> observed;
      auto r=iterative_search(*core,*root,history,*evaluator,{3,true,1,true,true},{},
          [&](CompletedDepth d){observed.push_back(std::move(d));},use);
      ensure(r.status==IterativeSearchStatus::depth_reached && r.completed &&
             observed.size()==3,"incomplete iterative search");
      ensure(core->export_fen(*root)==fen && history.matches_root(*core,*root),
             "position/history changed");
      return observed;
    };
    reference=run({});
    TranspositionTable table(16*1024*1024);
    for (int i=0;i<2;++i) {
      auto observed=run({&table,EvaluatorCacheCompatibility::deterministic_position,1});
      for (std::size_t j=0;j<3;++j) {
        ensure(same(reference[j].best,observed[j].best),"numeric TT changed best/PV");
        ++verified;
      }
    }
    table.clear();
    auto hint=run({&table,EvaluatorCacheCompatibility::not_certified,1});
    for (std::size_t j=0;j<3;++j) {
      ensure(same(reference[j].best,hint[j].best),"hint TT changed best/PV");
      ++verified;
    }
  }
  ensure(verified>=54,"insufficient coverage");
  std::cout<<"b3_numeric_horizon: PASS "<<verified<<" depth records\n";
  return 0;
} catch(const std::exception& ex) {
  std::cerr<<"b3_numeric_horizon: FAIL "<<ex.what()<<"\n";
  return 1;
}
