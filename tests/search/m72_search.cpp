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
// A single forced root edge isolates depth-zero behavior. Tree emits moves in
// reverse order, so canonical claim selection still requires initial preparation.
void leaf_ordering_regressions() {
  Checkpoint group("group M7.4a.1 horizon ordering");
  tree_values=false;constant=0;fail_at=0;eval_hook={};
  for(Side side:{Side::white,Side::black})for(int kind=0;kind<7;++kind) {
    const auto other=side==Side::white?Side::black:Side::white;
    Tree t;t.nodes={{side,{1}},{other,{2,3,4}},{side,{2}},{side,{3}},{side,{4}}};
    if(kind==1)t.nodes[2].clock=t.nodes[3].clock=100; // intended, canonical a1a2
    if(kind==2)t.nodes[1].clock=100;                // immediate claim
    if(kind==3){t.nodes[1].status=PositionStatus::checkmate;t.nodes[1].clock=150;}
    if(kind==4){t.nodes[1].status=PositionStatus::stalemate;t.nodes[1].clock=150;}
    if(kind==5)t.nodes[1].clock=150;
    if(kind==6)t.nodes[1].dead=true;
    values.assign(t.nodes.size(),0);
    auto root=t.initial_position();auto h=GameHistory::starting_at(t,*root,true);
    auto e=evaluator();const auto expected=oracle(t,0,1);
    ++search_count;auto fixed=search(t,*root,h,*e,{1,true});
    check(fixed.status==SearchStatus::complete,"fixed horizon result complete");
    same({*fixed.decision,*fixed.score,fixed.principal_variation,*fixed.pv_end,fixed.pv_intended_move},
         {expected.action,expected.score,expected.pv,expected.end,expected.intended});
    for(bool ordering:{false,true}) {
      ++search_count;auto ranked=legacy_iterative_search(t,*root,h,*e,{1,true,3,ordering});
      check(ranked.completed&&ranked.completed->multipv.size()==(kind==2||kind==3||kind==4||kind==5?2U:1U),"legacy MultiPV keeps root claim and PLAY alternatives");
      same(ranked.completed->best,{expected.action,expected.score,expected.pv,expected.end,expected.intended});
      // A deeper stored PLAY supplies a horizon hint but cannot supply a
      // depth-zero numeric score. Valid noncanonical and illegal hints differ
      // only in legal-membership/hint_hits accounting, never in the result.
      for(bool valid:{false,true})for(int mode:{1,2}) {
        TranspositionTable table(256*1024);
        TranspositionTableUse use{&table,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
        auto child=t.clone(*root);auto move=t.decode_move(*child,"a1a2");auto undo=t.make_move(*child,*move);
        auto signature=repetition_signature(t,*child);
        auto history=h.occurrences();history.push_back(signature);
        TranspositionStatistics seed;
        {detail::TTSession session(use,e->identity().fingerprint,h.complete(),seed,{});session.start(history);
         session.store(evaluation_snapshot(*t.snapshot(*child)),signature,2,SearchScore::finite(0),detail::TTBound::exact,
                       {DecisionKind::play,valid?"c1a2":"h1h2"},1);}
        t.undo_move(*child,*undo);
        for(int pass=0;pass<2;++pass) {
          ++search_count;auto r=iterative_search(t,*root,h,*e,{1,true,1,ordering},{},{},use);
          const auto end=kind==1?PVEnd::claim_draw_with_intended_move:kind==2?PVEnd::claim_draw:
                         kind>=3?PVEnd::terminal:PVEnd::horizon;
          const auto score=kind==3?(side==Side::white?SearchScore::white_mate(1):SearchScore::black_mate(1)):SearchScore::finite(0);
          check(r.status==IterativeSearchStatus::depth_reached&&r.completed,"horizon depth committed");
          same(r.completed->best,{ {DecisionKind::play,"a1a2"},score,{"a1a2"},end,kind==1?"a1a2":""});
          const auto &s=r.statistics;const auto &tt=s.transposition;
          check(s.visited_nodes==2&&tt.stores==1,"one actual horizon node/store; root remains PLAY-only");
          check(tt.lookups==(kind>=3?0U:1U)&&tt.hint_hits==(kind<3&&valid?1U:0U),"horizon hints validated and counted exactly, after terminal checks");
          check(s.evaluator_calls==(kind>=3||mode==2&&pass?0U:1U),"horizon evaluation only absent terminal or warm exact result");
          check(tt.exact_hits==(kind<3&&mode==2&&pass?1U:0U)&&tt.bound_hits==0&&tt.bound_cutoffs==0,"warm exact horizon/claim reuse and hint-only isolation");
          check(tt.candidate_hits==(kind>=3||mode==1?0U:pass?2U:1U)&&tt.context_rejections==(kind<3&&mode==2?1U:0U),"depth-mismatched hint never becomes numeric evidence");
          check(t.balance==0&&t.id(*root)==0&&h.matches_root(t,*root),"horizon board/history restoration");
        }
      }
    }
  }
  // The newly entered leaf itself completes a repetition. Its claim must be
  // processed at depth zero; PLAY-only policy belongs only to the actual root.
  for(Side side:{Side::white,Side::black})for(int kind=0;kind<3;++kind) {
    auto other=side==Side::white?Side::black:Side::white;
    Tree t;t.nodes={{side,{1}},{other,{0}}};auto root=t.initial_position();TreePosition start(kind==2?1:0);
    auto h=GameHistory::replay(t,start,std::vector<std::string>(kind==0?2:kind==1?4:7,"a1a2"),kind!=2);
    auto e=evaluator();const auto initial_balance=t.balance;
    for(bool ordering:{false,true})for(int mode:{0,1,2}) {
      TranspositionTable table(256*1024);TranspositionTableUse use;
      if(mode)use={&table,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
      for(int pass=0;pass<2;++pass) {
        ++search_count;auto r=iterative_search(t,*root,h,*e,{1,true,1,ordering},{},{},use);
        check(r.completed.has_value(),"repetition horizon completes");
        same(r.completed->best,{{DecisionKind::play,"a1a2"},SearchScore::finite(0),{"a1a2"},
             kind==0?PVEnd::claim_draw_with_intended_move:kind==1?PVEnd::claim_draw:PVEnd::terminal,kind==0?"a1a2":""});
        check(t.balance==initial_balance&&h.matches_root(t,*root),"actual repetition history restored");
      }
    }
  }
  // Genuine native clocks reach intended/immediate/automatic horizon rules.
  auto core=create_chess_core();
  for(Side side:{Side::white,Side::black})for(int clock:{98,99,149}) {
    auto root=core->load_fen(std::string("4k3/8/8/8/8/8/8/R3K3 ")+(side==Side::white?"w":"b")+" - - "+std::to_string(clock)+" 1");
    auto h=GameHistory::starting_at(*core,*root,true);auto e=evaluator();
    for(bool ordering:{false,true}) {
      ++search_count;auto r=iterative_search(*core,*root,h,*e,{1,true,1,ordering});
      check(r.completed&&r.completed->best.score==SearchScore::finite(0)&&r.completed->best.principal_variation.size()==1,"native zero-score canonical horizon PLAY");
      check(r.completed->best.pv_end==(clock==98?PVEnd::claim_draw_with_intended_move:clock==99?PVEnd::claim_draw:PVEnd::terminal),"native clock horizon claim/automatic precedence");
      auto leaf=core->clone(*root);auto move=core->decode_move(*leaf,r.completed->best.decision.move);auto undo=core->make_move(*leaf,*move);
      if(clock==98){std::vector<std::string> texts;for(auto &m:core->legal_moves(*leaf))texts.push_back(core->encode_move(*leaf,*m));std::sort(texts.begin(),texts.end());check(r.completed->best.pv_intended_move==texts.front(),"native intended claim remains canonical");}
    }
  }
  // Encoding validation remains in initial preparation for generic adapters.
  struct BadLeaf : Tree {
    bool promotion=false;
    std::string encode_move(const Position &p,const Move &m) const override {
      return id(p)==1?(promotion?"a1a2k":"a1a2"):Tree::encode_move(p,m);
    }
  };
  for(bool malformed:{false,true}) {
    BadLeaf t;t.promotion=malformed;t.nodes={{Side::white,{1}},{Side::black,{2,3}},{Side::white,{2}},{Side::white,{3}}};
    auto root=t.initial_position();auto h=GameHistory::starting_at(t,*root);auto e=evaluator();
    ++search_count;auto r=iterative_search(t,*root,h,*e,{1,true,1,true});
    check(r.status==IterativeSearchStatus::search_error&&!r.completed&&!r.unevaluated_fallback&&t.balance==0,"malformed/duplicate leaf encoding rejected with restoration");
  }
  for(int mode:{0,1,2})for(bool cancel:{false,true}) {
    Tree t;t.nodes={{Side::white,{1}},{Side::black,{1}}};auto root=t.initial_position();auto h=GameHistory::starting_at(t,*root);auto e=evaluator();
    TranspositionTable table(256*1024);TranspositionTableUse use;
    if(mode)use={&table,mode==2?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,1};
    CancellationSource stop;
    if(cancel)eval_hook=[&]{stop.request();};else fail_at=1;
    ++search_count;auto r=iterative_search(t,*root,h,*e,{1,true,1,true},stop.token(),{},use);
    eval_hook={};fail_at=0;
    check(r.status==(cancel?IterativeSearchStatus::cancelled:IterativeSearchStatus::evaluator_failed)&&!r.completed,"horizon cancellation/failure never commits an evaluated result");
    check(bool(r.unevaluated_fallback)==cancel&&t.balance==0&&h.matches_root(t,*root),"horizon fallback label and restoration");
  }
  group.finish();
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
  failures.finish();leaf_ordering_regressions();total.finish();
  std::cout<<"M7.2 single-PV exact references, TT cross-policy, draw history, cancellation and failure passed"<<std::endl;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
