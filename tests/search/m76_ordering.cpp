#include "m4_fixtures.hpp"
#include "../../engine/src/search/persistent_ordering.hpp"
#include <sstream>
using namespace chess_lab::detail;
std::uint64_t searches=0,commits=0;
void same(const SearchVariation&a,const SearchVariation&b) {
  check(a.decision==b.decision&&a.score==b.score&&a.principal_variation==b.principal_variation&&
    a.pv_end==b.pv_end&&a.pv_intended_move==b.pv_intended_move,"P1 exact score/decision/full canonical PV/end");
}
std::vector<SearchVariation> independent(const Tree&t,int depth,bool play_only,std::size_t count) {
  std::vector<SearchVariation> all;
  const auto &n=t.nodes[0];
  if(!play_only) {
    auto claim=oracle(t,0,0);
    // Enumerate root claims independently of whether static evaluation wins.
    if(n.clock>=100)all.push_back({{DecisionKind::claim_draw,{}},SearchScore::finite(0),{},PVEnd::claim_draw,{}});
    else for(std::size_t i=0;i<n.children.size();++i)if(t.nodes[n.children[i]].clock>=100) {
      const auto move=std::string(1,char('a'+i))+"1a2";
      all.push_back({{DecisionKind::claim_draw_with_intended_move,move},SearchScore::finite(0),{},PVEnd::claim_draw_with_intended_move,move});break;
    }
  }
  for(std::size_t i=0;i<n.children.size();++i) {
    auto x=oracle(t,n.children[i],depth-1);auto move=std::string(1,char('a'+i))+"1a2";
    x.pv.insert(x.pv.begin(),move);all.push_back({{DecisionKind::play,move},x.score.one_ply_farther(),x.pv,x.end,x.intended});
  }
  std::sort(all.begin(),all.end(),[&](const auto&a,const auto&b){
    if(a.score!=b.score)return n.side==Side::white?a.score>b.score:a.score<b.score;
    auto rank=[](DecisionKind k){return k==DecisionKind::claim_draw?0:k==DecisionKind::claim_draw_with_intended_move?1:2;};
    return rank(a.decision.kind)!=rank(b.decision.kind)?rank(a.decision.kind)<rank(b.decision.kind):a.decision.move<b.decision.move;
  });
  all.erase(all.begin()+std::min(count,all.size()),all.end());return all;
}
Tree generated(unsigned seed,Side side,int shape) {
  Tree t;std::function<int(int,Side)> add=[&](int depth,Side turn) {
    const int id=int(t.nodes.size());t.nodes.push_back({turn,{}});seed=seed*1664525U+1013904223U;
    if(depth==0){t.nodes[id].children={id};return id;}
    for(int k=0;k<3;++k){const int child=add(depth-1,turn==Side::white?Side::black:Side::white);t.nodes[id].children.push_back(child);}
    if(id&&shape==1&&id%11==0)t.nodes[id].clock=100;
    if(id&&shape==2&&id%17==0){t.nodes[id].status=PositionStatus::checkmate;t.nodes[id].clock=150;}
    if(id&&shape==3&&id%19==0)t.nodes[id].status=PositionStatus::stalemate;
    return id;
  };add(4,side);values.resize(t.nodes.size());
  for(auto &v:values){seed=seed*1664525U+1013904223U;v=shape==0?0:double(int(seed%7)-3);if(shape==4)v*=1e250;}
  return t;
}
void trees() {
  tree_values=true;
  for(Side side:{Side::white,Side::black})for(int shape=0;shape<5;++shape)for(unsigned seed=1;seed<=3;++seed) {
    auto t=generated(seed,side,shape);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
    for(int retained:{-1,0,2,3,4})for(bool previous:{false,true})for(int mode:{0,1,2})for(std::size_t multi:{1U,3U}) {
      const bool play_only=multi==1;TranspositionTable table(64*1024);OrderingStatistics metrics;
      OrderingScope scope({retained,256*1024,false,false},&metrics);auto e=evaluator();int seen=0;++searches;
      auto x=iterative_search(t,*p,h,*e,{4,true,multi,previous,play_only},{},[&](CompletedDepth d){
        auto expected=independent(t,d.depth,play_only,multi);check(d.multipv.size()==expected.size(),"P1 exact rank count");
        for(std::size_t i=0;i<expected.size();++i)same(d.multipv[i],expected[i]);++seen;++commits;
      },{mode?&table:nullptr,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1});
      check(x.status==IterativeSearchStatus::depth_reached&&seen==4&&t.balance==0&&t.export_fen(*p)=="0","P1 synthetic completion/restoration");
    }
  }
  tree_values=false;constant=0;
}
// Deliberate off-PV-prefix trap: depth4 learns c,c,c continuations;
// depth5 is all-zero and must publish a,a,a,a,a, including below old PV.
void off_prefix_ties() {
  tree_values=true;Tree t;std::vector<int> path;
  std::function<int(int,Side)> add=[&](int ply,Side side) {
    const int id=int(t.nodes.size());t.nodes.push_back({side,{}});values.push_back(0);
    if(ply==4) {
      double v=path[1]==2?-5:5;
      if(path[2]!=2)v-=1;
      v+=2-path[3];values[id]=v;
    }
    if(ply==5){t.nodes[id].children={id};return id;}
    for(int k=0;k<3;++k){path.push_back(k);auto child=add(ply+1,side==Side::white?Side::black:Side::white);path.pop_back();t.nodes[id].children.push_back(child);}
    return id;
  };
  values.clear();add(0,Side::white);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
  for(int retained:{3,4})for(bool prev:{false,true})for(int mode:{0,1,2}) {
    auto e=evaluator();TranspositionTable table(262144);OrderingScope scope({retained,1048576});++searches;
    auto x=iterative_search(t,*p,h,*e,{5,true,1,prev,true},{},[&](CompletedDepth d){same(d.best,independent(t,d.depth,true,1).front());++commits;},
      {mode?&table:nullptr,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1});
    check(x.completed&&x.completed->best.principal_variation==std::vector<std::string>(5,"a1a2")&&t.balance==0,"off-prefix arbitrary ordering exact canonical PV");
  }
  tree_values=false;
}
void native() {
  auto c=create_chess_core();
  const std::vector<std::string> fens={
    "start", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "4k3/P7/8/8/8/8/7p/4K3 w - - 99 1",
    "7k/5Q2/6K1/8/8/8/8/8 b - - 150 1",
    "7k/6Q1/6K1/8/8/8/8/8 b - - 150 1",
    "4k3/8/8/8/8/8/R7/4K3 w - - 100 1",
    "4k3/8/8/8/8/8/R7/4K3 b - - 149 1"};
  for(const auto &fen:fens)for(bool complete:{false,true}) {
    auto p=fen=="start"?c->initial_position():c->load_fen(fen);auto h=GameHistory::starting_at(*c,*p,complete);
    if(fen=="start") {const std::vector<std::string> moves={"g1f3","g8f6","f3g1","f6g8","g1f3","g8f6","f3g1"};h=GameHistory::replay(*c,*p,moves,complete);for(auto &m:moves){auto v=c->decode_move(*p,m);auto u=c->make_move(*p,*v);}}
    const auto before=c->export_fen(*p);
    for(bool legacy:{false,true}) {
      const std::size_t count=legacy?3:1;auto e=evaluator();std::vector<CompletedDepth> reference;
      {OrderingScope disabled({-1});++searches;iterative_search(*c,*p,h,*e,{2,false,count,false,!legacy},{},[&](CompletedDepth d){reference.push_back(d);});}
      for(int retained:{0,2,4})for(bool prev:{false,true})for(int mode:{0,1,2})for(int pass:{0,1}) {
        TranspositionTable table(256*1024);auto use=TranspositionTableUse{mode?&table:nullptr,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
        OrderingStatistics stats;OrderingScope scope({retained,pass?1U:256U*1024U},&stats);auto ev=evaluator();int seen=0;
        if(pass&&mode){++searches;iterative_search(*c,*p,h,*ev,{2,true,count,prev,!legacy},{},{},use);}
        ++searches;auto x=iterative_search(*c,*p,h,*ev,{2,true,count,prev,!legacy},{},[&](CompletedDepth d){
          const auto &v=reference.at(seen++);check(d.multipv.size()==v.multipv.size(),"native ranking size");
          for(std::size_t i=0;i<v.multipv.size();++i)same(d.multipv[i],v.multipv[i]);++commits;
        },use);
        check(x.status==IterativeSearchStatus::terminal||x.status==IterativeSearchStatus::depth_reached,"P1 native completion");
        check(c->export_fen(*p)==before&&h.matches_root(*c,*p),"P1 native history/root restoration");
        if(pass&&x.completed)check(stats.budget_fallbacks>0,"tiny memory budget fallback");
      }
    }
  }
}
void lifecycle() {
  tree_values=true;auto t=generated(811,Side::black,4);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
  for(int retained:{0,3})for(int mode:{0,1,2})for(int target:{1,4,20,80})for(bool cancel:{false,true}) {
    auto e=evaluator();CancellationSource stop;fail_at=cancel?0:target;calls=0;eval_hook=[&]{if(cancel&&calls>=target)stop.request();};
    OrderingStatistics metrics;OrderingScope scope({retained,256*1024,false,true},&metrics);TranspositionTable table(65536);++searches;
    auto x=iterative_search(t,*p,h,*e,{4,true,1,true,true},stop.token(),{},
      {mode?&table:nullptr,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1});
    check(t.balance==0&&t.export_fen(*p)=="0","P1 fault restores caller position");
    if(calls<target)check(x.status==IterativeSearchStatus::depth_reached&&x.completed&&metrics.committed_iterations==4,"unreached injection is complete control");
    else if(cancel){check(x.status==IterativeSearchStatus::cancelled,"P1 cancellation");check(metrics.committed_iterations==std::uint64_t(x.completed?x.completed->depth:0),"partial iteration cannot publish ordering");}
    else check(x.status==IterativeSearchStatus::evaluator_failed&&!x.completed&&!x.unevaluated_fallback,"P1 failure invalidates prior results");
    if(x.completed)same(x.completed->best,independent(t,x.completed->depth,true,1).front());
    std::cout<<"P1 fault ply="<<retained<<" mode="<<mode<<" target="<<target<<" cancel="<<cancel
      <<" calls="<<calls<<" triggered="<<(calls>=target)<<" status="<<int(x.status)
      <<" committed="<<metrics.committed_iterations<<" balance="<<t.balance<<"\n";
    fail_at=0;eval_hook={};
  }
  for(bool throwing:{false,true}) {
    auto e=evaluator();CancellationSource stop;OrderingStatistics metrics;OrderingScope scope({3,262144},&metrics);++searches;
    auto x=iterative_search(t,*p,h,*e,{4,true,1,true,true},stop.token(),[&](CompletedDepth d){if(d.depth==2){if(throwing)throw std::runtime_error("progress fault");stop.request();}});
    check(t.balance==0&&metrics.committed_iterations==2,"callback boundary ordering committed atomically");
    check(throwing?x.status==IterativeSearchStatus::search_error&&!x.completed:x.status==IterativeSearchStatus::cancelled&&x.completed&&x.completed->depth==2,"callback cancellation/error semantics");
  }
  {auto e=evaluator();OrderingStatistics metrics;OrderingScope scope({3,65536},&metrics);
    t.throw_at=1;++searches;auto x=iterative_search(t,*p,h,*e,{4,true,1,true,true});t.throw_at=-1;
    check(x.status==IterativeSearchStatus::search_error&&!x.completed&&t.balance==0,"P1 rules exception restores frame");}
  // No invocation-local ordering survives into another invocation or fixed search.
  auto e=evaluator();auto a=iterative_search(t,*p,h,*e,{3,true,1,true,true});
  {OrderingScope enabled({4,65536});auto b=iterative_search(t,*p,h,*e,{3,true,1,true,true});same(a.completed->best,b.completed->best);}
  auto b=iterative_search(t,*p,h,*e,{3,true,1,true,true});same(a.completed->best,b.completed->best);
  check(a.statistics.visited_nodes==b.statistics.visited_nodes&&a.statistics.evaluator_calls==b.statistics.evaluator_calls,"P1 disabled work unchanged across invocations");
  tree_values=false;
}
void arena() {
  struct M{std::string text;};std::vector<M> moves={{"a1a2"},{"b1a2"},{"c1a2"},{"d1a2"}};
  for(bool best_only:{false,true}) {
    OrderingStatistics stats;
    {OrderingControl control{{3,4096,best_only,false},&stats};OrderingTree tree(&control);tree.begin();tree.prepare(0,moves);
     tree.observe(0,"b1a2",3,1);tree.observe(0,"c1a2",3,2);tree.observe(0,"d1a2",1,3);tree.seal(0);tree.commit();
     tree.begin();auto current=moves;tree.apply(0,current,[]{});check(current[0].text=="c1a2","latest exact improvement first");
     #if defined(CHESS_LAB_ORDERING_B2)
     check(current[1].text=="a1a2","B2 applies only one selected move with canonical fallback");
#else
     check(current[1].text==(best_only?"a1a2":"b1a2"),"P1 tests multiple siblings beyond best only");
#endif
     current={{"a1a2"},{"b1a2"},{"e1a2"}};tree.apply(0,current,[]{});check(current.size()==3,"missing retained/legal new fallback preserved");
    }
#if defined(CHESS_LAB_ORDERING_B2)
    check(stats.allocated_bytes<=4096&&stats.peak_used_bytes<=4096&&stats.peak_allocated_bytes<=4096&&stats.allocations>0,"B2 dynamic/transient memory bound");
    check(stats.reused_siblings==0,"B2 never applies extra sibling priorities");
#else
    check(stats.allocated_bytes<=4096&&stats.peak_used_bytes<=4096&&stats.allocations==4,"P1 actual two-bank memory bound");
    if(!best_only)check(stats.reused_siblings>0,"extra siblings reused");
#endif
    OrderingStatistics exhausted;
    {OrderingControl c{{4,1024,false,false},&exhausted};OrderingTree tree(&c);tree.begin();
      std::vector<M> many;for(char f='a';f<='h';++f)for(char rank='1';rank<='8';++rank)many.push_back({std::string{f,rank,'a','1'}});
      tree.prepare(0,many);
#if defined(CHESS_LAB_ORDERING_B2)
      for(const auto&m:many){auto id=tree.child(0,0,m.text,true);if(id!=OrderingTree::invalid)tree.prepare(id,moves);tree.observe(0,m.text,2,1);}
#endif
      tree.commit();}
#if defined(CHESS_LAB_ORDERING_B2)
    check(exhausted.budget_fallbacks>0&&exhausted.peak_allocated_bytes<=1024,"B2 node/observation exhaustion remains bounded");
#else
    check(exhausted.budget_fallbacks>0&&exhausted.edges==0&&exhausted.allocated_bytes<=1024,"edge capacity exhaustion preserves fallback");
#endif
  }
}

void refinement_policy_tests(){
  struct M{std::string text;};std::vector<M> moves={{"a1a2"},{"b1a2"},{"c1a2"},{"d1a2"}};
#if defined(CHESS_LAB_ORDERING_B2)
  OrderingStatistics stats;
  {OrderingControl c{{3,65536,false,false},&stats};OrderingTree tree(&c);tree.begin();tree.prepare(0,moves);
   check(tree.child(0,0,"a1a2",false)==OrderingTree::invalid,"B2 horizon/claim lookahead creates no empty nodes");
   auto child=tree.child(0,0,"b1a2",true);check(child!=OrderingTree::invalid,"B2 actual positive child linked");tree.prepare(child,moves);
   tree.observe(child,"a1a2",3,1);tree.observe(child,"b1a2",4,2);tree.observe(child,"b1a2",1,3);tree.seal(child);
   tree.observe(0,"b1a2",3,1);tree.seal(0);tree.commit();tree.begin();auto samechild=tree.child(0,0,"b1a2",true);
   check(samechild==child,"B2 child ID survives bank swap/growth");auto ordered=moves;tree.apply(child,ordered,[]{});
   check(ordered[0].text=="a1a2","B2 best selector preserves per-move priority downgrades");tree.commit();
  }
  check(stats.nodes==2&&stats.edges==1&&stats.peak_allocated_bytes<=65536,"B2 compact structural links, no legal edge list");
#endif
}
int main()try{refinement_policy_tests();arena();trees();off_prefix_ties();native();lifecycle();std::cout<<"P1 passed searches="<<searches<<" commits="<<commits<<"\n";}
catch(const std::exception&e){std::cerr<<"P1 failure: "<<e.what()<<"\n";return 1;}
