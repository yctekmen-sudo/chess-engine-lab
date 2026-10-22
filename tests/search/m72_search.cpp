#include "m4_fixtures.hpp"
#include "../../engine/src/search/transposition_table_internal.hpp"
#include <chrono>
#include <iomanip>

bool ci_matrix=false;
std::uint64_t search_count=0;
struct Checkpoint {
  std::string name;
  std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
  std::uint64_t searches=search_count;
  explicit Checkpoint(std::string label):name(std::move(label)) {
    std::cout<<"M7.2 begin "<<name<<std::endl;
  }
  // Explicit completion only: exceptions must never print a successful end.
  void finish() const {
    std::cout<<"M7.2 end "<<name<<" seconds="<<std::fixed<<std::setprecision(3)
      <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()
      <<" searches="<<search_count-searches<<std::endl;
  }
};

void same(const SearchVariation &a,const SearchVariation &b) {
  check(a.decision==b.decision && a.score==b.score && a.principal_variation==b.principal_variation &&
        a.pv_end==b.pv_end && a.pv_intended_move==b.pv_intended_move,"single-PV exact PLAY/score/canonical PV/termination");
}
void compare(const ChessCore &c,const Position &p,const GameHistory &h,int depth,bool ci_tt=false) {
  Checkpoint comparison("compare depth="+std::to_string(depth));
  auto reference_evaluator=evaluator();std::vector<CompletedDepth> reference;
  Checkpoint reference_time("legacy exhaustive reference");++search_count;
  auto ref=legacy_iterative_search(c,p,h,*reference_evaluator,{depth,false,256,false},{},[&](CompletedDepth d){reference.push_back(d);});
  reference_time.finish();
  auto verify=[&](Evaluator &e,bool ordering,TranspositionTableUse use) {
    int commits=0;++search_count;
    auto r=iterative_search(c,p,h,e,{depth,true,1,ordering},{},[&](CompletedDepth d){
      const auto &rank=reference.at(commits++).multipv;
      auto play=std::find_if(rank.begin(),rank.end(),[](const auto &v){return v.decision.kind==DecisionKind::play;});
      check(play!=rank.end()&&d.multipv.size()==1,"one PLAY, every completed depth");same(d.best,*play);
    },use);
    if(ref.terminal_decision)check(r.terminal_decision==ref.terminal_decision && r.terminal_score==ref.terminal_score && !commits,"terminal precedence");
    else check(r.completed&&commits==depth,"all optimized depths committed");
    return r;
  };
  auto reverse=[&](Evaluator &e,bool ordering,TranspositionTableUse use) {
    ++search_count;
    auto again=legacy_iterative_search(c,p,h,e,{depth,true,256,ordering},{},{},use);
    if(ref.completed)for(size_t i=0;i<ref.completed->multipv.size();++i)same(again.completed->multipv[i],ref.completed->multipv[i]);
  };
  if(ci_matrix) {
    // Expensive real boards need exact all-depth equivalence and both ordering
    // modes, not the full Cartesian product. Small representatives exercise TT.
    for(bool ordering:{false,true}) {
      auto e=evaluator();verify(*e,ordering,{});
      if(ci_tt)for(int mode:{1,2}) {
        Checkpoint cache("CI TT ordering="+std::to_string(ordering)+" mode="+std::to_string(mode));
        TranspositionTable table(1024*1024);
        TranspositionTableUse use{&table,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
        auto cold=verify(*e,ordering,use);
        auto warm=verify(*e,ordering,use);
        check(cold.statistics.transposition.lookups>0&&warm.statistics.transposition.lookups>0,"CI cold/warm cache exercised");
        if(mode==1)check(!warm.statistics.transposition.exact_hits&&!warm.statistics.transposition.bound_hits,"hint-only cannot use numeric hits");
        else check(warm.statistics.transposition.exact_hits+warm.statistics.transposition.bound_hits>0,"certified warm numeric hits exercised");
        reverse(*e,ordering,use); // optimized -> legacy
        verify(*e,ordering,use);  // legacy -> optimized
        reverse(*e,ordering,use);
        cache.finish();
      }
    }
    comparison.finish();return;
  }
  // Preserve the complete original native matrix, including its shared-table
  // lifetime and repeated passes. --matrix=full selects it on WASM too.
  TranspositionTable table(1024*1024);
  for(bool ordering:{false,true})for(int mode:{0,1,2})for(int pass=0;pass<2;++pass) {
    Checkpoint config("full ordering="+std::to_string(ordering)+" mode="+std::to_string(mode)+" pass="+std::to_string(pass));
    auto e=evaluator();TranspositionTableUse use;
    if(mode)use={&table,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
    // Warm ordinary descendants under legacy ranking, then reuse in both
    // directions. Root-policy separation is tested by explicit injection below.
    if(mode){++search_count;legacy_iterative_search(c,p,h,*e,{depth,true,256,ordering},{},{},use);}
    verify(*e,ordering,use);
    // Verify using the same table again under legacy semantics, in reverse.
    if(mode)reverse(*e,ordering,use);
    config.finish();
  }
  comparison.finish();
}
int main(int argc,char **argv)try {
  for(int i=1;i<argc;++i) {
    std::string arg=argv[i];
    if(arg=="--matrix=ci")ci_matrix=true;
    else if(arg=="--matrix=full")ci_matrix=false;
    else if(arg.size()==64&&arg.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos)digest=arg;
    else throw std::invalid_argument("usage: m72_search [WASM SHA-256] [--matrix=ci|--matrix=full]");
  }
  Checkpoint total(ci_matrix?"total matrix=ci":"total matrix=full");
  fail_at=0;constant=0;
  auto c=create_chess_core();
  Checkpoint real("group real boards");int case_number=0;
  for(auto fen:{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1", "4k3/7P/8/8/8/8/p7/4K3 b - - 0 1", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
    "7k/8/5KQ1/8/8/8/8/8 w - - 149 1", "7K/8/5kq1/8/8/8/8/8 b - - 149 1",
    "4k3/8/8/8/8/8/8/R3K3 b - - 99 1", "4k3/8/8/8/8/8/8/R3K3 b - - 100 1",
    "4k3/8/8/8/8/8/8/R3K3 w - - 150 1", "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",
    "7k/6Q1/5K2/8/8/8/8/8 b - - 150 1", "7K/6q1/5k2/8/8/8/8/8 w - - 150 1"}) {
    auto p=c->load_fen(fen);auto h=GameHistory::starting_at(*c,*p);
    Checkpoint fixture("real case="+std::to_string(++case_number)+" fen="+fen);
    for(double value:{-3.0,0.0,3.0}){
      if(ci_matrix&&value!=0)continue;
      Checkpoint score("value="+std::to_string(value));constant=value;
      compare(*c,*p,h,2,ci_matrix&&(case_number==5||case_number==8));score.finish();
    }
    fixture.finish();
  }
  real.finish();Checkpoint repetitions("group actual repetitions");
  auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,true);
  const std::vector<std::string> cycle={"g1f3","g8f6","f3g1","f6g8"};
  for(int i=0;i<16;++i){auto text=cycle[i%4];h=h.after_move(*c,*p,text);auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);if(i==6||i==7||i==15){Checkpoint fixture("actual ply="+std::to_string(i+1));compare(*c,*p,h,2);fixture.finish();}}
  repetitions.finish();Checkpoint generated("group generated trees");
  // Generated unequal/equal values and mate traps exercise root and internal
  // prior-PV reordering against the legacy exhaustive traversal at every depth.
  unsigned rng=9721;tree_values=true;
  for(int trial=0;trial<(ci_matrix?6:30);++trial){
    Checkpoint fixture("generated trial="+std::to_string(trial));
    Tree t;values.clear();std::function<int(int,Side)> gen=[&](int d,Side side){
      int id=int(t.nodes.size());t.nodes.push_back({side,{}});rng=rng*1664525U+1013904223U;values.push_back(int(rng%7)-3);
      if(id&&rng%19==0)t.nodes[id].status=PositionStatus::checkmate;
      if(rng%13==0)t.nodes[id].clock=100;
      if(d)for(int i=0;i<3;++i){int child=gen(d-1,side==Side::white?Side::black:Side::white);t.nodes[id].children.push_back(child);}
      else t.nodes[id].children.push_back(id);return id;};
    gen(3,trial%2?Side::white:Side::black);auto root=t.initial_position();auto hist=GameHistory::starting_at(t,*root);
    compare(t,*root,hist,3,ci_matrix);check(t.balance==0,"generated restoration");fixture.finish();
  }
  generated.finish();Checkpoint mates("group mate races");
  for(Side side:{Side::white,Side::black})for(bool winning:{false,true}){
    Side other=side==Side::white?Side::black:Side::white;Tree t;
    if(winning)t.nodes={{side,{1,2}},{other,{},PositionStatus::checkmate},{other,{3}},{side,{4}},{other,{},PositionStatus::checkmate}};
    else t.nodes={{side,{1,3}},{other,{2}},{side,{},PositionStatus::checkmate},{other,{4}},{side,{5}},{other,{6}},{side,{},PositionStatus::checkmate}};
    values.assign(t.nodes.size(),0);auto root=t.initial_position();auto history=GameHistory::starting_at(t,*root);compare(t,*root,history,4);
  }
  mates.finish();Checkpoint isolation("group root TT isolation");
  tree_values=false;constant=0;
  {
    constant=-3;auto claim_root=c->load_fen("4k3/8/8/8/8/8/8/R3K3 w - - 100 1");
    auto history=GameHistory::starting_at(*c,*claim_root);auto provider=evaluator();
    TranspositionTable table(1024*1024);TranspositionTableUse use{&table,EvaluatorCacheCompatibility::deterministic_position,1};
    TranspositionStatistics statistics;
    {
      detail::TTSession session(use,provider->identity().fingerprint,history.complete(),statistics,{});session.start(history.occurrences());
      session.store(evaluation_snapshot(*c->snapshot(*claim_root)),history.occurrences().back(),1,
        SearchScore::finite(0),detail::TTBound::exact,{DecisionKind::claim_draw,{}},0);
    }
    ++search_count;auto r=iterative_search(*c,*claim_root,history,*provider,{1,true,1,true},{},{},use);
    check(r.completed&&r.completed->best.decision.kind==DecisionKind::play&&r.completed->best.score==SearchScore::finite(-3),"incompatible ordinary-root numeric claim entry ignored");
    constant=0;
  }
  isolation.finish();Checkpoint failures("group cancellation and evaluator error");
  auto e=evaluator();auto root=c->initial_position();auto hist=GameHistory::starting_at(*c,*root,true);
  CancellationSource before;before.request();++search_count;auto cancelled=iterative_search(*c,*root,hist,*e,{3,true,1,true},before.token());
  check(cancelled.unevaluated_fallback&&!cancelled.completed,"unevaluated early fallback");
  CancellationSource after;++search_count;auto retained=iterative_search(*c,*root,hist,*e,{3,true,1,true},after.token(),[&](CompletedDepth){after.request();});
  check(retained.completed&&retained.completed->depth==1&&retained.status==IterativeSearchStatus::cancelled,"committed depth retained on cancellation");
  ++search_count;auto failed=iterative_search(*c,*root,hist,*e,{3,true,1,true},{},[&](CompletedDepth){fail_at=calls+1;});
  check(failed.status==IterativeSearchStatus::evaluator_failed&&!failed.completed&&!failed.unevaluated_fallback,"late failure invalidates provisional depths");
  failures.finish();total.finish();
  std::cout<<"M7.2 single-PV exact references, TT cross-policy, draw history, cancellation and failure passed"<<std::endl;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
