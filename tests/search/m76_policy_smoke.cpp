#include "m4_fixtures.hpp"
#include "../../engine/src/search/persistent_ordering.hpp"
#include <string_view>
using namespace chess_lab::detail;

void equal(const std::vector<CompletedDepth>& a, const std::vector<CompletedDepth>& b) {
  check(a.size()==b.size(), "policy completed-depth count");
  for(std::size_t d=0;d<a.size();++d) {
    check(a[d].multipv.size()==b[d].multipv.size(), "policy MultiPV count");
    for(std::size_t i=0;i<a[d].multipv.size();++i) {
      const auto &x=a[d].multipv[i], &y=b[d].multipv[i];
      check(x.decision==y.decision && x.score==y.score &&
        x.principal_variation==y.principal_variation && x.pv_end==y.pv_end &&
        x.pv_intended_move==y.pv_intended_move, "policy exact public result");
    }
    check(a[d].statistics.visited_nodes==b[d].statistics.visited_nodes &&
      a[d].statistics.evaluator_calls==b[d].statistics.evaluator_calls &&
      a[d].statistics.alpha_beta_cutoffs==b[d].statistics.alpha_beta_cutoffs,
      "normal entry point matches explicit selected policy work");
  }
}

int main() try {
  auto core=create_chess_core();auto root=core->initial_position();
  auto history=GameHistory::starting_at(*core,*root,true);
  auto fen=core->export_fen(*root);
  for(std::size_t multi:{1U,3U}) {
    std::vector<CompletedDepth> normal, explicit_policy;
    auto e=evaluator();
    // No private scope: this is how UCI/controller/WASM call the public API.
    auto result=iterative_search(*core,*root,history,*e,{3,true,multi,true,multi==1},{},
      [&](CompletedDepth d){normal.push_back(std::move(d));});
    check(result.status==IterativeSearchStatus::depth_reached, "normal default policy completes");
    OrderingStatistics stats;
    {OrderingScope selected(production_ordering_config,&stats);
      result=iterative_search(*core,*root,history,*e,{3,true,multi,true,multi==1},{},
        [&](CompletedDepth d){explicit_policy.push_back(std::move(d));});}
    equal(normal,explicit_policy);
    if(std::string_view(compiled_ordering_policy)=="NONE")
      check(stats.allocations==0 && stats.committed_iterations==0, "NONE never allocates/activates");
    else {
      check(stats.committed_iterations==3 && stats.budget_fallbacks==0 && stats.hits>0,
        "selected policy active with complete retention");
      if(std::string_view(compiled_ordering_policy)=="B2")
        check(stats.allocated_bytes<1024*1024, "B2 does not allocate P1 arena");
    }
    check(core->export_fen(*root)==fen && history.matches_root(*core,*root), "policy restores root/history");
  }
  std::cout<<"compiled_ordering_policy="<<compiled_ordering_policy
    <<" retained_plies=0..3 retention_ceiling=134217728 normal_entry_point=passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
