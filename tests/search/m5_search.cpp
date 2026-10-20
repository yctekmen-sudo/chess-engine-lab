#include "m4_fixtures.hpp"
#include "../../engine/src/search/transposition_table_internal.hpp"
#ifndef __EMSCRIPTEN__
#include <thread>
#endif
using namespace chess_lab::detail;
TranspositionTableUse certified(TranspositionTable &t, uint64_t policy=1) {
  return {&t,EvaluatorCacheCompatibility::deterministic_position,policy};
}
void equal_line(const SearchVariation&a,const SearchVariation&b) {
  check(a.decision==b.decision && a.score==b.score && a.principal_variation==b.principal_variation &&
        a.pv_end==b.pv_end && a.pv_intended_move==b.pv_intended_move,"TT exact decision/score/PV/end equality");
}
void equal_line(const SearchVariation&a,const Expected&b) {
  equal_line(a,{b.action,b.score,b.pv,b.end,b.intended});
}
void equal_result(const IterativeSearchResult&a,const IterativeSearchResult&b) {
  check(a.status==b.status && a.terminal_decision==b.terminal_decision && a.terminal_score==b.terminal_score &&
        a.unevaluated_fallback==b.unevaluated_fallback && bool(a.completed)==bool(b.completed),"TT final state equality");
  if(a.completed) {
    check(a.completed->depth==b.completed->depth && a.completed->multipv.size()==b.completed->multipv.size(),"TT depth/ranking count");
    equal_line(a.completed->best,b.completed->best);
    for(size_t i=0;i<a.completed->multipv.size();++i)equal_line(a.completed->multipv[i],b.completed->multipv[i]);
  }
}
void replay_line(const ChessCore&c,const Position&p,const SearchVariation&v) {
  SearchResult r;r.decision=v.decision;r.score=v.score;r.principal_variation=v.principal_variation;
  r.pv_end=v.pv_end;r.pv_intended_move=v.pv_intended_move;verify_pv(c,p,r);
}
std::vector<Expected> ranking(const Tree&t,int depth) {
  const auto&n=t.nodes[0];std::vector<Expected> out;
  if(n.clock>=100)out.push_back({SearchScore::finite(0),{DecisionKind::claim_draw,{}},{},PVEnd::claim_draw,{}});
  else for(size_t i=0;i<n.children.size();++i)if(t.nodes[n.children[i]].clock>=100) {
    auto text=std::string(1,char('a'+i))+"1a2";
    out.push_back({SearchScore::finite(0),{DecisionKind::claim_draw_with_intended_move,text},{},PVEnd::claim_draw_with_intended_move,text});break;
  }
  for(size_t i=0;i<n.children.size();++i) {
    auto x=oracle(t,n.children[i],depth-1);x.score=x.score.one_ply_farther();
    auto text=std::string(1,char('a'+i))+"1a2";x.action={DecisionKind::play,text};x.pv.insert(x.pv.begin(),text);out.push_back(x);
  }
  std::stable_sort(out.begin(),out.end(),[&](const auto&a,const auto&b){return n.side==Side::white?a.score>b.score:a.score<b.score;});
  return out;
}
void generated() {
  unsigned rng=3715;auto next=[&]{return rng=rng*1664525U+1013904223U;};
  tree_values=true;fail_at=0;
  uint64_t hits=0,bounds=0,closures=0;
  for(int trial=0;trial<24;++trial) {
    Tree t;values.clear();
    std::function<int(int,Side)> gen=[&](int depth,Side side) {
      int id=t.nodes.size();t.nodes.push_back({side,{}});
      values.push_back(std::array<double,7>{-1e300,-4,-0.0,0,2,7,1e300}[next()%7]);
      auto k=next()%29;
      if(id && k==0)t.nodes[id].status=PositionStatus::checkmate;
      if(id && k==1)t.nodes[id].status=PositionStatus::stalemate;
      if(id && k==2)t.nodes[id].dead=true;
      if(k==3)t.nodes[id].clock=100;
      if(id && k==4)t.nodes[id].clock=150;
      if(depth)for(int i=0;i<3;++i){int child=gen(depth-1,side==Side::white?Side::black:Side::white);t.nodes[id].children.push_back(child);}
      else t.nodes[id].children.push_back(id);
      return id;
    };
    gen(3,trial%2?Side::white:Side::black);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,trial%2);
    for(bool order:{false,true})for(size_t n:{1,3,8}) {
      TranspositionTable table(512*1024);auto e=evaluator();
      std::vector<CompletedDepth> snapshots;
      auto observe=[&](CompletedDepth d) {
        auto expected=ranking(t,d.depth);check(d.multipv.size()==std::min(n,expected.size()),"all distinct root candidates exact");
        for(size_t i=0;i<d.multipv.size();++i){equal_line(d.multipv[i],expected[i]);replay_line(t,*p,d.multipv[i]);}
        snapshots.push_back(std::move(d));
      };
      auto cold=iterative_search(t,*p,h,*e,{3,true,n,order},{},observe,certified(table));
      check(cold.status==IterativeSearchStatus::depth_reached && snapshots.size()==3,"cold iterative depths");
      auto warm=iterative_search(t,*p,h,*e,{3,true,n,order},{},observe,certified(table));equal_result(cold,warm);
      hits+=warm.statistics.transposition.exact_hits;bounds+=warm.statistics.transposition.bound_hits;closures+=warm.statistics.transposition.bound_cutoffs;
      auto off=iterative_search(t,*p,h,*e,{3,true,n,order});equal_result(off,warm);
      auto count=table.entry_count();
      auto ref=iterative_search(t,*p,h,*e,{3,false,n,order},{},{},certified(table));equal_result(ref,warm);
      check(ref.statistics.transposition.lookups==0 && ref.statistics.transposition.stores==0 && ref.statistics.transposition.hint_hits==0 && table.entry_count()==count,"exhaustive bypasses populated table entirely");
      for(auto&d:snapshots){auto expected=ranking(t,d.depth);for(size_t i=0;i<d.multipv.size();++i)equal_line(d.multipv[i],expected[i]);}
      check(t.balance==0 && t.id(*p)==0 && h.occurrences().size()==1,"TT make/undo/context restoration");
    }
  }
  check(hits>0 && bounds>0 && closures>0,"exact and bound cache paths exercised");tree_values=false;
  std::cout<<"24 generated trees, all intermediate/ranked lines, cold/warm/off/exhaustive, both sides and ordering passed\n";
}
// Direct private-layer fixtures isolate exact evidence, original windows and
// mate coordinates independently of whether a practical board yields a hit.
void context_and_bounds() {
  Tree t;t.nodes={{Side::white,{1}},{Side::black,{1}}};auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
  auto s=evaluation_snapshot(*t.snapshot(*p));auto sig=repetition_signature(t,*p);auto other=sig;other.pieces[32]=PieceCode::black_pawn;
  std::string fp(64,'a'),changed(64,'b');TranspositionTable table(256*1024);TranspositionStatistics stats;
  TTAccess::force_collisions(table,true);
  {
    TTSession session(certified(table),fp,true,stats,{});session.start(h.occurrences());
    for(int depth:{0,1,2})session.store(s,sig,depth,SearchScore::finite(depth+1),TTBound::exact,{DecisionKind::play,"a1a2"},0);
    for(int depth:{0,1,2})check(session.probe(s,sig,depth,0).score==SearchScore::finite(depth+1),"equal-depth entries coexist");
    check(!session.probe(s,sig,3,0).score,"deeper score is not reused at different depth");
  }
  auto reject=[&](std::vector<RepetitionSignature> history,ChessEvalPosition snapshot,bool complete,std::string_view id,uint64_t policy) {
    TTSession session(certified(table,policy),id,complete,stats,{});session.start(history);
    check(!session.probe(snapshot,sig,1,0).score,"colliding incompatible context rejected");
  };
  reject({sig,other},s,true,fp,1);reject({sig,sig},s,true,fp,1);
  reject({sig},s,false,fp,1);reject({sig},s,true,changed,1);reject({sig},s,true,fp,2);
  auto changed_snapshot=s;changed_snapshot.fullmove_number++;reject({sig},changed_snapshot,true,fp,1);
  changed_snapshot=s;changed_snapshot.halfmove_clock++;reject({sig},changed_snapshot,true,fp,1);
  changed_snapshot=s;changed_snapshot.en_passant_square=20;reject({sig},changed_snapshot,true,fp,1);
  {
    TTSession session({&table},fp,true,stats,{});session.start({sig});
    auto hit=session.probe(s,sig,1,0);check(!hit.score && hit.hint=="a1a2","uncertified evaluators receive hints only");
  }
  check(stats.context_rejections>0 && stats.verified_history_nodes>0,"collision verification observed");
  table.clear();
  {TTSession session(certified(table),fp,true,stats,{});session.start({sig,other,sig});session.store(s,sig,1,SearchScore::finite(7),TTBound::exact,{DecisionKind::play,"a1a2"},0);}
  {TTSession session(certified(table),fp,true,stats,{});session.start({other,sig,sig});check(session.probe(s,sig,1,0).score==SearchScore::finite(7),"canonical counts permit different chronological histories");}
  auto f=SearchScore::finite;
  check(classify_bound(f(0),f(0),f(3))==TTBound::upper && classify_bound(f(3),f(0),f(3))==TTBound::lower &&
        classify_bound(f(2),f(0),f(3))==TTBound::exact && classify_bound(f(0),{}, {})==TTBound::exact,"original-window inclusive bound classification");
  // The claim raises local alpha to zero; original alpha was -1, so zero is
  // exact at the original window rather than an invented fail-low.
  check(classify_bound(f(0),f(-1),f(1))==TTBound::exact,"classify before claim-induced window update");
  for(auto bound:{TTBound::exact,TTBound::lower,TTBound::upper})for(bool white:{false,true}) {
    table.clear();auto score=white?SearchScore::white_mate(9):SearchScore::black_mate(9);
    TTSession session(certified(table),fp,true,stats,{});session.start({sig});
    session.store(s,sig,1,score,bound,{DecisionKind::play,"a1a2"},4);
    auto hit=session.probe(s,sig,1,12);
    check(hit.score==(white?SearchScore::white_mate(17):SearchScore::black_mate(17)) && hit.bound==bound,"mate/bound normalization at distinct root plies");
  }
  check(!to_node_score(SearchScore::white_mate(2),3) && !to_node_score(SearchScore::black_mate(2),3),"ancestor-relative invalid subtraction refused");
  check(!to_root_score(SearchScore::white_mate(UINT64_MAX),1) && !to_root_score(SearchScore::black_mate(UINT64_MAX),1),"mate overflow refused");
  std::cout<<"exact count/snapshot/depth/evaluator/policy verification, forced collisions, original bounds and mate transforms passed\n";
}
void mate_races() {
  tree_values=true;
  for(Side side:{Side::white,Side::black})for(bool winning:{false,true}) {
    Side other=side==Side::white?Side::black:Side::white;Tree t;
    if(winning)t.nodes={{side,{1,2}},{other,{},PositionStatus::checkmate},{other,{3}},{side,{4}},{other,{},PositionStatus::checkmate}};
    else t.nodes={{side,{1,3}},{other,{2}},{side,{},PositionStatus::checkmate},{other,{4}},{side,{5}},{other,{6}},{side,{},PositionStatus::checkmate}};
    values.assign(t.nodes.size(),0);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);auto e=evaluator();
    TranspositionTable table(256*1024);auto ref=iterative_search(t,*p,h,*e,{4,false,3,false});
    for(bool order:{false,true})for(int pass=0;pass<2;++pass) {
      auto r=iterative_search(t,*p,h,*e,{4,true,3,order},{},{},certified(table));equal_result(r,ref);
      check(r.completed->best.decision.move==(winning?"a1a2":"b1a2"),"warm mate race faster win/slower defeat");
    }
  }
  tree_values=false;
}
void real_rules() {
  auto c=create_chess_core();tree_values=false;fail_at=0;
  for(auto fen:{"7k/8/5KQ1/8/8/8/8/8 w - - 149 1", "4k3/8/8/8/8/8/P7/4K3 b - - 0 1",
               "4k3/8/8/8/8/8/8/R3K3 w - - 99 1","4k3/8/8/8/8/8/8/R3K3 b - - 100 1",
               "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2","k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
               "7k/6Q1/5K2/8/8/8/8/8 b - - 150 1","7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",
               "4k3/8/8/8/8/8/8/4K3 w - - 0 1","4k3/8/8/8/8/8/8/R3K3 w - - 150 1"}) {
    auto p=c->load_fen(fen);auto h=GameHistory::starting_at(*c,*p);
    for(double v:{-1e300,-0.0,1e300}) {
      constant=v;auto e=evaluator();TranspositionTable table(1024*1024);
      for(bool order:{false,true}) {
        auto ref=iterative_search(*c,*p,h,*e,{2,false,256,order});
        for(int warm=0;warm<2;++warm) {
          auto r=iterative_search(*c,*p,h,*e,{2,true,256,order},{},{},certified(table));equal_result(r,ref);
          if(r.completed)for(auto&line:r.completed->multipv)replay_line(*c,*p,line);
        }
      }
    }
  }
  constant=0;auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,true);auto e=evaluator();TranspositionTable table(1024*1024);
  std::vector<std::string> cycle={"g1f3","g8f6","f3g1","f6g8"};
  for(int i=0;i<16;++i) {
    auto text=cycle[i%4];h=h.after_move(*c,*p,text);auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);
    if(i==6 || i==7 || i==15) {
      auto ref=iterative_search(*c,*p,h,*e,{1,false,256,false});
      auto r=iterative_search(*c,*p,h,*e,{1,true,256,true},{},{},certified(table));equal_result(r,ref);
      auto imported=GameHistory::starting_at(*c,*p);
      r=iterative_search(*c,*p,imported,*e,{1,true,256,true},{},{},certified(table));
      equal_result(r,iterative_search(*c,*p,imported,*e,{1,false,256,false}));
    }
  }
  // Same board, changed raw EP/fullmove/clock fields are separate evaluator inputs.
  for(auto fen:{"4k3/8/8/3p4/8/8/8/R3K3 w - d6 0 2","4k3/8/8/3p4/8/8/8/R3K3 w - - 0 3","4k3/8/8/3p4/8/8/8/R3K3 w - - 99 3"}) {
    auto q=c->load_fen(fen);auto history=GameHistory::starting_at(*c,*q);
    equal_result(iterative_search(*c,*q,history,*e,{1,true,3,true},{},{},certified(table)),iterative_search(*c,*q,history,*e,{1,false,3,false}));
  }
  std::cout<<"genuine chess all-line TT/oracle equivalence, rule/claim/history/evaluator-visible distinctions passed\n";
}
void small(Tree&t) {
  t.nodes={{Side::white,{1,2}},{Side::black,{3,4}},{Side::black,{5,6}},
           {Side::white,{7}},{Side::white,{8}},{Side::white,{9}},{Side::white,{10}}};
  for(int i=7;i<=10;++i)t.nodes.push_back({Side::black,{i}});
  values.assign(11,2);tree_values=false;constant=2;fail_at=0;eval_hook={};
}
void memory_and_lifetime() {
  Tree t;small(t);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);auto e=evaluator();
  auto ref=iterative_search(t,*p,h,*e,{3,false,3,false});
  for(size_t bytes:std::array<size_t,6>{0,1,64,4096,16384,131072}) {
    TranspositionTable table(bytes);check(table.capacity_bytes()<=bytes,"strict total owned allocation cap");
    auto cold=iterative_search(t,*p,h,*e,{3,true,3,true},{},{},certified(table));equal_result(cold,ref);
    auto warm=iterative_search(t,*p,h,*e,{3,true,3,true},{},{},certified(table));equal_result(warm,ref);
    if(!table.capacity_bytes())check(!warm.statistics.transposition.lookups && !table.entry_count(),"zero/tiny allocation is disabled");
    table.clear();check(!table.entry_count(),"clear is reusable");table.resize(8192);check(table.capacity_bytes()<=8192,"resize accounting");
    equal_result(iterative_search(t,*p,h,*e,{3,true,3,true},{},{},certified(table)),ref);
  }
  // Fixed pool pressure: exact evidence exceeds capacity, so only hints are
  // retained. No per-entry history allocation is allowed to escape the budget.
  TranspositionTable limited(16384);TranspositionStatistics stats;auto sig=repetition_signature(t,*p);
  auto snap=evaluation_snapshot(*t.snapshot(*p));std::string fp(64,'a');
  {TTSession session(certified(limited),fp,true,stats,{});session.start(std::vector<RepetitionSignature>(5000,sig));
   session.store(snap,sig,1,SearchScore::finite(2),TTBound::exact,{DecisionKind::play,"a1a2"},0);
   check(!session.probe(snap,sig,1,0).score,"oversized evidence declines numeric storage");}
  check(limited.capacity_bytes()<=16384 && stats.skipped_stores>0,"variable history cannot exceed pool");
  TranspositionTable first(32768),second(32768);TTAccess::force_collisions(first,true);TTAccess::force_collisions(second,true);
  auto a=iterative_search(t,*p,h,*e,{3,true,3,true},{},{},certified(first));
  auto b=iterative_search(t,*p,h,*e,{3,true,3,true},{},{},certified(second));equal_result(a,b);
  check(a.statistics.transposition.evictions==b.statistics.transposition.evictions && a.statistics.visited_nodes==b.statistics.visited_nodes &&
        first.entry_count()==second.entry_count() && a.statistics.transposition.evictions>0,"deterministic recency/depth replacement under collisions");
  auto e2=evaluator();bool rejected=false;
  auto outer=iterative_search(t,*p,h,*e,{1,true,1,true},{},[&](CompletedDepth){
    rejects([&]{first.clear();});rejects([&]{first.resize(32768);});
    auto inner=iterative_search(t,*p,h,*e2,{1,true,1,true},{},{},certified(first));
    rejected=inner.status==IterativeSearchStatus::search_error && inner.diagnostic.find("already active")!=std::string::npos;
    // Exhaustive reference still ignores even an actively attached table.
    auto oracle_result=iterative_search(t,*p,h,*e2,{1,false,1,false},{},{},certified(first));
    check(oracle_result.completed && oracle_result.statistics.transposition.lookups==0,"exhaustive bypass includes exclusive guard");
  },certified(first));
  check(rejected && outer.completed && first.entry_count()>0,"second active search rejected without touching owner table");first.clear();
#ifndef __EMSCRIPTEN__
  // A true second execution context also observes the exclusive guard.
  {TTSession held(certified(first),fp,true,stats,{});std::atomic<bool> denied=false;
   std::thread other([&]{try{TTSession competing(certified(first),fp,true,stats,{});}catch(const std::logic_error&){denied=true;}});other.join();check(denied,"native simultaneous table use denied");}
#endif
  std::cout<<"bounded pools/oversized evidence, zero/resize/clear, deterministic eviction and exclusive use passed\n";
}
struct Owner {
  TranspositionTable table{256*1024};bool cleared=false;size_t retained_before_clear=0;
  IterativeSearchResult run(const ChessCore&c,const Position&p,const GameHistory&h,Evaluator&e,
                           IterativeSearchOptions o, CancellationToken token={},SearchProgress callback={}) {
    auto r=iterative_search(c,p,h,e,o,token,std::move(callback),certified(table));
    if(r.status==IterativeSearchStatus::evaluator_failed || r.status==IterativeSearchStatus::search_error) {
      retained_before_clear=table.entry_count();table.clear();cleared=true;
    }
    return r;
  }
};
CancellationSource *verification_cancellation = nullptr;
std::uint64_t verification_cancel_at = 0;
void cancel_inside_verification(std::uint64_t visited) noexcept {
  if (visited >= verification_cancel_at) verification_cancellation->request();
}
void cancellation_inside_tt_verification() {
  Tree t, subtree;small(subtree);
  constexpr int stem = 256;
  for (int i=0;i<stem;++i)t.nodes.push_back({i%2?Side::black:Side::white,{i+1}});
  for (auto node:subtree.nodes) {
    for (auto &child:node.children)child+=stem;
    t.nodes.push_back(std::move(node));
  }
  auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
  for (int i=0;i<stem;++i) {
    h=h.after_move(t,*p,"a1a2");auto m=t.decode_move(*p,"a1a2");auto u=t.make_move(*p,*m);
  }
  const auto balance=t.balance;auto e=evaluator();Owner owner;
  owner.table.resize(1024*1024);
  auto reference=iterative_search(t,*p,h,*e,{2,false,3,false});
  auto depth_one=iterative_search(t,*p,h,*e,{1,false,3,false});
  equal_result(owner.run(t,*p,h,*e,{2,true,3,false}),reference);
  const auto retained=owner.table.entry_count();check(retained>0,"prime completed TT entries");
  for (bool after_commit:{false,true}) {
    CancellationSource source;verification_cancellation=&source;
    verification_cancel_at=after_commit?UINT64_MAX:32;
    TTAccess::observe_verification(owner.table,cancel_inside_verification);
    int events=0;
    auto r=owner.run(t,*p,h,*e,{2,true,3,false},source.token(),[&](CompletedDepth d) {
      ++events;check(d.depth==1,"only first depth committed before TT cancellation");
      verification_cancel_at=d.statistics.transposition.verified_history_nodes+32;
    });
    TTAccess::observe_verification(owner.table,nullptr);
    check(r.status==IterativeSearchStatus::cancelled && !owner.cleared && e->usable(),
          "TT history cancellation is normal cancellation, never owner-clearing search error");
    check(r.statistics.transposition.verified_history_nodes>=32 && source.token().requested(),
          "request originated inside long-history candidate verification");
    if (after_commit) {
      check(events==1 && r.completed && r.completed->depth==1 && !r.unevaluated_fallback,
            "TT cancellation retains only committed depth");
      equal_line(r.completed->best,depth_one.completed->best);
      check(r.completed->multipv.size()==depth_one.completed->multipv.size(),"committed ranking size retained");
      for(size_t i=0;i<r.completed->multipv.size();++i)
        equal_line(r.completed->multipv[i],depth_one.completed->multipv[i]);
    } else check(events==0 && !r.completed && r.unevaluated_fallback==ChessDecision{DecisionKind::play,"a1a2"},
                 "TT cancellation before commit retains canonical unevaluated fallback");
    check(t.balance==balance && t.id(*p)==stem && h.matches_root(t,*p) && h.occurrences().size()==257,
          "TT cancellation restores every make/undo and caller history");
    check(owner.table.entry_count()>=retained,"normal TT cancellation retains completed cached entries");
    equal_result(owner.run(t,*p,h,*e,{2,true,3,false}),reference);
  }
  verification_cancellation=nullptr;
  std::cout<<"long-history TT verification cancellation retains fallback/committed depth, cache and LIFO state passed\n";
}
void cancellation_and_errors() {
  Tree t;small(t);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);auto e=evaluator();Owner owner,unrelated;
  auto reference=iterative_search(t,*p,h,*e,{2,false,3,false});
  check(unrelated.run(t,*p,h,*e,{1,true,3,true}).completed.has_value(),"independent owner primed");auto untouched=unrelated.table.entry_count();
  CancellationSource before;before.request();
  auto r=owner.run(t,*p,h,*e,{2,true,3,true},before.token());
  check(r.status==IterativeSearchStatus::cancelled && r.unevaluated_fallback==ChessDecision{DecisionKind::play,"a1a2"} && !r.completed &&
        !owner.table.entry_count() && !owner.cleared,"predepth cancellation no speculative entries");
  CancellationSource inside;calls=0;eval_hook=[&]{if(calls==2)inside.request();};
  r=owner.run(t,*p,h,*e,{2,true,3,true},inside.token());eval_hook={};
  check(r.status==IterativeSearchStatus::cancelled && r.unevaluated_fallback && !r.completed && owner.table.entry_count()>0 && t.balance==0 &&
        !owner.cleared,"cancelled iteration keeps only completed leaf writes");
  equal_result(owner.run(t,*p,h,*e,{2,true,3,true}),reference);
  owner.table.clear();CancellationSource later;int events=0;
  r=owner.run(t,*p,h,*e,{3,true,3,true},later.token(),[&](CompletedDepth){++events;eval_hook=[&]{later.request();};});eval_hook={};
  check(r.status==IterativeSearchStatus::cancelled && r.completed->depth==1 && events==1 && owner.table.entry_count()>0 && t.balance==0,"cancelled later depth retains committed result/cache");
  owner.table.clear();fail_at=0;e=evaluator();
  r=owner.run(t,*p,h,*e,{3,true,3,true},{},[&](CompletedDepth){fail_at=calls+1;});
  check(r.status==IterativeSearchStatus::evaluator_failed && !r.completed && !r.unevaluated_fallback && !e->usable() &&
        owner.cleared && owner.retained_before_clear>0 && !owner.table.entry_count() && t.balance==0,"owner clears after late provider failure, engine retained writes until return");
  check(unrelated.table.entry_count()==untouched,"failure clearing leaves independent owner untouched");
  auto dispatched=calls;
  r=owner.run(t,*p,h,*e,{2,true,3,true});check(r.status==IterativeSearchStatus::evaluator_failed && calls==dispatched,"quarantine cannot be bypassed by cache");
  fail_at=0;e=evaluator();owner.cleared=false;
  r=owner.run(t,*p,h,*e,{2,true,3,true},{},[](CompletedDepth){throw EvaluationError("callback failure");});
  check(r.status==IterativeSearchStatus::search_error && !r.completed && e->usable() && owner.cleared && owner.retained_before_clear>0 && !owner.table.entry_count(),"callback exception is search error; owner clears after release");
  owner.cleared=false;
  r=owner.run(t,*p,h,*e,{2,true,3,true},{},[&](CompletedDepth){t.throw_at=2;});t.throw_at=-1;
  check(r.status==IterativeSearchStatus::search_error && !r.completed && e->usable() && owner.cleared && owner.retained_before_clear>0 && t.balance==0,"traversal error controller clears, root unwinds");
  check(unrelated.table.entry_count()==untouched && t.id(*p)==0 && h.matches_root(t,*p),"all failures isolate caller and unrelated tables");
  std::cout<<"cancellation commit/write boundaries and controller-owned failure clearing/quarantine passed\n";
}
void ordered_trap_and_certification() {
  Tree t;t.nodes={{Side::white,{1,2}},{Side::black,{3,4}},{Side::black,{5,6}},
     {Side::white,{7,8}},{Side::white,{9,10}},{Side::white,{11,12}},{Side::white,{13,14}}};
  for(int i=7;i<15;++i)t.nodes.push_back({Side::black,{i}});
  values.assign(15,0);values[1]=-3;values[2]=7;values[3]=8;values[4]=-2;values[5]=9;values[6]=6;tree_values=true;
  auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);
  for(double hidden:{0.0,4.0}) {
    values[12]=hidden;TranspositionTable table(16384);auto e=evaluator();
    auto ref=iterative_search(t,*p,h,*e,{3,false,8,false});
    for(int pass=0;pass<4;++pass)equal_result(iterative_search(t,*p,h,*e,{3,true,8,bool(pass%2)},{},{},certified(table)),ref);
    // Force missing/evicted links and repeated noncanonical hints.
    table.resize(16384);TTAccess::force_collisions(table,true);
    for(int pass=0;pass<2;++pass)equal_result(iterative_search(t,*p,h,*e,{3,true,8,true},{},{},certified(table)),ref);
  }
  tree_values=false;Tree simple;small(simple);auto root=simple.initial_position();auto history=GameHistory::starting_at(simple,*root);
  TranspositionTable table(256*1024);auto e=evaluator();
  auto r=iterative_search(simple,*root,history,*e,{1,true,3,false},{},{},certified(table));check(r.completed.has_value(),"prime numeric entries");
  constant=9;calls=0;
  auto not_certified=iterative_search(simple,*root,history,*e,{1,true,3,false},{},{},{&table});
  check(not_certified.statistics.transposition.exact_hits==0 && not_certified.statistics.transposition.bound_hits==0 && calls>0 &&
        not_certified.completed->best.score==SearchScore::finite(9),"certification off forbids stale numeric reads/writes");
  // An incompatible real M2 identity receives no old score, without a clear.
  auto changed=EvaluatorModule::provider(provider,std::string(64,'b'),"search-fixture-other").create("{}");calls=0;
  auto new_id=iterative_search(simple,*root,history,*changed,{1,true,3,false},{},{},certified(table));
  check(calls>0 && new_id.completed->best.score==SearchScore::finite(9),"changed evaluator fingerprint cannot inherit numeric score");
  // Inject a position-matched but illegal hint through the private fixture.
  // No numeric assertion is stored or trusted for an uncertified provider.
  table.clear();auto child=simple.clone(*root);auto move=simple.decode_move(*child,"a1a2");auto token=simple.make_move(*child,*move);
  auto child_history=history.after_move(simple,*root,"a1a2");TranspositionStatistics stats;
  {TTSession session({&table},changed->identity().fingerprint,false,stats,{});session.start(child_history.occurrences());
   session.store(evaluation_snapshot(*simple.snapshot(*child)),repetition_signature(simple,*child),1,
                 SearchScore::finite(0),TTBound::exact,{DecisionKind::play,"h1h8"},0);}
  auto expected=iterative_search(simple,*root,history,*changed,{2,false,3,false});
  auto after=iterative_search(simple,*root,history,*changed,{2,true,3,false},{},{},{&table});equal_result(after,expected);
  simple.undo_move(*child,*token);
  std::cout<<"ordered tie/bound traps, evicted PV re-search, certification/fingerprint and legal hints passed\n";
}
int main(int argc,char**argv) try {
  if(argc>1)digest=argv[1];
  generated();context_and_bounds();mate_races();real_rules();memory_and_lifetime();cancellation_inside_tt_verification();cancellation_and_errors();ordered_trap_and_certification();
  std::cout<<"M5 transposition table passed\n";
} catch(const std::exception&e){std::cerr<<"M5 failure: "<<e.what()<<'\n';return 1;}
