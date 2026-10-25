#include "m4_fixtures.hpp"
#include "../../engine/src/search/transposition_table_internal.hpp"
#include "../../engine/src/native_move_batch.hpp"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>

// Adapter-private search capability; no public ChessCore/ABI extension.
namespace chess_lab {
std::size_t native_legal_move_count(const ChessCore &, const Position &);
}

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
// Force eager generic preparation for a semantic oracle over real native rules.
// This wrapper intentionally does NOT qualify as native; activation is measured
// separately in copied-source diagnostics, not inferred from this oracle.
struct EagerRules : ChessCore {
  const ChessCore &c;
  explicit EagerRules(const ChessCore &core):c(core){}
  std::unique_ptr<Position> initial_position()const override{return c.initial_position();}
  std::unique_ptr<Position> load_fen(std::string_view f)const override{return c.load_fen(f);}
  std::string export_fen(const Position &p)const override{return c.export_fen(p);}
  std::unique_ptr<Position> clone(const Position &p)const override{return c.clone(p);}
  std::vector<std::unique_ptr<Move>> legal_moves(const Position &p)const override{return c.legal_moves(p);}
  std::unique_ptr<UndoToken> make_move(Position &p,const Move &m)const override{return c.make_move(p,m);}
  void undo_move(Position &p,UndoToken &u)const override{c.undo_move(p,u);}
  std::string encode_move(const Position &p,const Move &m)const override{return c.encode_move(p,m);}
  std::unique_ptr<Move> decode_move(const Position &p,std::string_view m)const override{return c.decode_move(p,m);}
  void reset(Position &p,std::string_view f)const override{c.reset(p,f);}
  bool in_check(const Position &p)const override{return c.in_check(p);}
  PositionStatus status(const Position &p)const override{return c.status(p);}
  bool known_dead_position(const Position &p)const override{return c.known_dead_position(p);}
  std::uint64_t position_key(const Position &p)const override{return c.position_key(p);}
  std::uint64_t repetition_key(const Position &p)const override{return c.repetition_key(p);}
  std::unique_ptr<const PositionSnapshot> snapshot(const Position &p)const override{return c.snapshot(p);}
};
std::string deterministic_statistics(const SearchStatistics &s) {
  const auto &t=s.transposition;std::ostringstream o;
  o<<s.visited_nodes<<':'<<s.evaluator_calls<<':'<<s.alpha_beta_cutoffs<<':'
   <<t.lookups<<':'<<t.candidate_hits<<':'<<t.exact_hits<<':'<<t.bound_hits<<':'
   <<t.bound_cutoffs<<':'<<t.hint_hits<<':'<<t.context_rejections<<':'<<t.stores<<':'
   <<t.evictions<<':'<<t.pv_researches<<':'<<t.verified_history_nodes<<':'<<t.skipped_stores;
  return o.str();
}
std::uint64_t leaf_evidence_hash=1469598103934665603ULL,leaf_evidence_records=0;
void leaf_evidence(const std::string &s) {
  for(unsigned char c:s)leaf_evidence_hash=(leaf_evidence_hash^c)*1099511628211ULL;
  ++leaf_evidence_records;
  if(std::getenv("CHESS_LAB_M742_EVIDENCE"))std::cout<<"M7.4a.2 record "<<s<<std::endl;
}
void leaf_evidence(const SearchVariation &v,const SearchStatistics &s) {
  std::ostringstream o;o<<int(v.decision.kind)<<':'<<v.decision.move<<':'
    <<(v.decision.terminal_outcome?int(*v.decision.terminal_outcome):-1)<<':'<<int(v.score.kind())<<':';
  if(v.score.kind()==SearchScore::Kind::finite)o<<std::hexfloat<<(v.score.value()==0?0.0:v.score.value());else o<<v.score.distance();
  o<<':'<<int(v.pv_end)<<':'<<v.pv_intended_move<<':';for(const auto &m:v.principal_variation)o<<m<<',';
  o<<':'<<deterministic_statistics(s);leaf_evidence(o.str());
}
void native_leaf_case(const ChessCore &c,const Position &p,const GameHistory &h,bool compact=false,int depth=1) {
  EagerRules eager(c);const auto fen=c.export_fen(p);const auto occurrences=h.occurrences();
  Checkpoint fixture("M7.4a.2 native fen="+fen+" complete="+std::to_string(h.complete()));
  for(double value:{-2.0,0.0,2.0}) {
    if(compact&&value!=0)continue;constant=value;
    for(bool ab:{false,true}) {
      auto ref_eval=evaluator();++search_count;auto ref=search(eager,p,h,*ref_eval,{depth,ab});
      auto e=evaluator();++search_count;auto r=search(c,p,h,*e,{depth,ab});
      check(r.status==ref.status&&r.decision&&r.score&&r.pv_end,"native fixed leaf completes like eager rules");
      same({*r.decision,*r.score,r.principal_variation,*r.pv_end,r.pv_intended_move},
           {*ref.decision,*ref.score,ref.principal_variation,*ref.pv_end,ref.pv_intended_move});
      check(deterministic_statistics(r.statistics)==deterministic_statistics(ref.statistics),"fixed leaf node/eval/work counts unchanged");
      leaf_evidence({*r.decision,*r.score,r.principal_variation,*r.pv_end,r.pv_intended_move},r.statistics);
    }
    for(bool ordering:{false,true})for(int policy:{0,1,3}) {
      if(compact&&(ordering||policy==3))continue;
      IterativeSearchOptions options{depth,true,policy==3?3U:1U,ordering,policy!=0};
      auto re=evaluator();std::vector<CompletedDepth> reference;++search_count;
      auto ref=iterative_search(eager,p,h,*re,options,{},[&](CompletedDepth d){reference.push_back(std::move(d));});
      for(int mode:{0,1,2,3,4}) {
        // Full cache cases once per fixture; alpha-beta-off and disabled-table
        // controls prove eligibility depends on the effective session.
        if(mode&& (value!=0||!ordering||policy!=1||compact))continue;
        TranspositionTable table(mode==3?0:256*1024);TranspositionTableUse use;
        if(mode)use={&table,mode==1?EvaluatorCacheCompatibility::not_certified:EvaluatorCacheCompatibility::deterministic_position,1};
        auto actual_options=options;if(mode==4)actual_options.alpha_beta_enabled=false;
        for(int pass=0;pass<(mode==1||mode==2?2:1);++pass) {
          auto e=evaluator();int commits=0;++search_count;
          auto r=iterative_search(c,p,h,*e,actual_options,{},[&](CompletedDepth d){
            const auto &expected=reference.at(commits++);check(d.multipv.size()==expected.multipv.size(),"native exact rank count");
            for(std::size_t i=0;i<d.multipv.size();++i){same(d.multipv[i],expected.multipv[i]);leaf_evidence(d.multipv[i],d.statistics);}
            if(mode==0||mode==3)check(deterministic_statistics(d.statistics)==deterministic_statistics(expected.statistics),"uncached native/eager work prefix equality");
          },use);
          check(r.status==ref.status&&bool(r.completed)==bool(ref.completed)&&commits==int(reference.size()),"all native depths commit like eager reference");
          check(r.terminal_decision==ref.terminal_decision&&r.terminal_score==ref.terminal_score,"native root terminal precedence");
          check(r.provenance.history_incomplete==!h.complete(),"incomplete history provenance unchanged");
          if(r.terminal_decision)leaf_evidence({*r.terminal_decision,*r.terminal_score,{},PVEnd::terminal,{}},r.statistics);
          leaf_evidence("final:"+std::to_string(int(r.status))+":"+deterministic_statistics(r.statistics));
          check(c.export_fen(p)==fen&&h.occurrences()==occurrences&&h.matches_root(c,p),"native board/history restored");
        }
      }
    }
  }
  constant=0;
  fixture.finish();
}
void native_encoding_set(const ChessCore &c,Position &p,bool castling,bool promotion) {
  const auto fen=c.export_fen(p);std::vector<std::string> texts;
  for(auto &m:c.legal_moves(p)) {
    const auto text=c.encode_move(p,*m);
    auto square=[&](int i){return text[i]>='a'&&text[i]<='h'&&text[i+1]>='1'&&text[i+1]<='8';};
    check((text.size()==4||text.size()==5)&&square(0)&&square(2)&&
          (text.size()==4||std::string("bnqr").find(text[4])!=std::string::npos),"native generated encoding canonical");
    texts.push_back(text);auto u=c.make_move(p,*m);c.undo_move(p,*u);
    check(c.encode_move(p,*m)==text&&c.export_fen(p)==fen,"native generated certificate survives exact undo");
    auto decoded=c.decode_move(p,text);auto u2=c.make_move(p,*decoded);c.undo_move(p,*u2);
  }
  std::sort(texts.begin(),texts.end());check(std::adjacent_find(texts.begin(),texts.end())==texts.end(),"native generated encodings unique");
  if(castling){const auto from=c.snapshot(p)->side_to_move()==Side::white?"e1":"e8";for(const auto to:{'c','g'})check(std::find(texts.begin(),texts.end(),std::string(from)+to+from[1])!=texts.end(),"both native castling encodings");}
  if(promotion)for(char piece:std::string("bnqr"))check(std::count_if(texts.begin(),texts.end(),[&](const auto &t){return t.size()==5&&t[4]==piece;})==2,"all quiet/capture promotion encodings");
}
void guarded_leaf_regressions() {
  Checkpoint group("group M7.4a.2 guarded native leaves");tree_values=false;constant=0;fail_at=0;eval_hook={};
  auto c=create_chess_core();
  for(Side side:{Side::white,Side::black})for(int clock:{0,97,98,99,148,149,150}) {
    auto p=c->load_fen(std::string("4k3/8/8/8/8/8/8/R3K3 ")+(side==Side::white?"w":"b")+" - - "+std::to_string(clock)+" 1");
    auto h=GameHistory::starting_at(*c,*p,side==Side::white);native_leaf_case(*c,*p,h);
  }
  for(const auto fen:{
      "4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1", // child has legal raw EP
      "3k4/8/8/8/3p4/8/4P3/K2R4 w - - 0 1", // child EP is pinned
      "4k3/8/8/8/8/8/P7/4K3 w - - 0 1",     // irrelevant raw EP / clock zero
      "7k/8/5KQ1/8/8/8/8/8 w - - 0 1", "7K/8/5kq1/8/8/8/8/8 b - - 0 1",
      "4k3/8/8/8/8/8/r7/K7 w - - 0 1", "k7/R7/8/8/8/8/8/4K3 b - - 0 1",
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
      "1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1", "4k3/7P/8/8/8/8/p7/1R2K3 b - - 0 1"}) {
    std::cout<<"M7.4a.2 encoding fixture "<<fen<<std::endl;
    auto p=c->load_fen(fen);auto h=GameHistory::starting_at(*c,*p,true);
    const bool castle=std::string(fen).find("KQkq")!=std::string::npos;
    const bool promotion=std::string(fen).find("1r2k3")!=std::string::npos||std::string(fen).find("1R2K3")!=std::string::npos;
    native_encoding_set(*c,*p,castle,promotion);native_leaf_case(*c,*p,h);
  }
  const std::vector<std::string> cycle={"g1f3","g8f6","f3g1","f6g8"};
  for(bool complete:{false,true}) {
    auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,complete);
    for(int i=0;i<16;++i){auto text=cycle[i%4];h=h.after_move(*c,*p,text);auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);
      if(i==2||i==6||i==7||i==14||i==15)native_leaf_case(*c,*p,h,true);}
  }
  // Long repeated prefix without fivefold or a 150-halfmove interval: three
  // knight cycles, then a fresh pair of pawn moves, repeated across all files.
  {
    auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,false);
    auto play=[&](const std::string &s){h=h.after_move(*c,*p,s);auto m=c->decode_move(*p,s);auto u=c->make_move(*p,*m);};
    // Leave f-pawns until last: advancing them earlier blocks the knight cycle.
    for(char file:std::string("abcdeghf")){for(int j=0;j<12;++j)play(cycle[j%4]);play(std::string(1,file)+"2"+file+"3");play(std::string(1,file)+"7"+file+"6");}
    check(h.occurrences().size()==113,"long repeated history fixture");native_leaf_case(*c,*p,h,true);
  }
  // Deterministic legal walk retaining only distinct exact identities.
  {
    auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,true);unsigned seed=742;
    for(int ply=0;ply<48;++ply){auto moves=c->legal_moves(*p);seed=seed*1664525U+1013904223U;bool found=false;
      for(std::size_t i=0;i<moves.size();++i){auto &m=moves[(seed+i)%moves.size()];auto text=c->encode_move(*p,*m);auto u=c->make_move(*p,*m);
        const auto signature=repetition_signature(*c,*p);bool suitable=std::find(h.occurrences().begin(),h.occurrences().end(),signature)==h.occurrences().end()&&c->status(*p)==PositionStatus::ongoing&&!c->known_dead_position(*p);c->undo_move(*p,*u);
        if(suitable){h=h.after_move(*c,*p,text);auto chosen=c->decode_move(*p,text);auto undo=c->make_move(*p,*chosen);found=true;break;}}
      check(found,"distinct native history walk available");}
    check(h.occurrences().size()==49,"long distinct history fixture");native_leaf_case(*c,*p,h,true,2);
  }
  // Synthetic terminal status must not bypass early malformed/duplicate/throwing
  // encoding validation or turn an invalid preparation into a terminal result.
  struct InvalidTerminal : Tree {
    int kind=0;
    std::string encode_move(const Position &p,const Move &m)const override {
      if(id(p)!=1)return Tree::encode_move(p,m);
      if(kind==2)throw std::runtime_error("synthetic encoder failed");
      return kind==0?"a1a2":"a1a2k";
    }
  };
  for(int kind:{0,1,2})for(auto status:{PositionStatus::ongoing,PositionStatus::checkmate,PositionStatus::stalemate}) {
    InvalidTerminal t;t.kind=kind;t.nodes={{Side::white,{1}},{Side::black,{2,3},status},{Side::white,{2}},{Side::white,{3}}};
    auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);auto e=evaluator();++search_count;auto r=iterative_search(t,*p,h,*e,{1,true,1,true});
    check(r.status==IterativeSearchStatus::search_error&&!r.completed&&!r.terminal_decision&&!r.unevaluated_fallback&&r.statistics.visited_nodes==1&&r.statistics.evaluator_calls==0&&t.balance==0&&h.matches_root(t,*p),"generic encoding validation remains before terminal/node entry");
    leaf_evidence("invalid:"+std::to_string(kind)+":"+std::to_string(int(status))+":"+deterministic_statistics(r.statistics));
  }
  for(int mode:{0,1,2})for(bool cancel:{false,true}) {
    auto p=c->load_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");const auto fen=c->export_fen(*p);auto h=GameHistory::starting_at(*c,*p,true);auto e=evaluator();CancellationSource stop;
    TranspositionTable table(256*1024);TranspositionTableUse use;if(mode)use={&table,mode==1?EvaluatorCacheCompatibility::not_certified:EvaluatorCacheCompatibility::deterministic_position,1};
    if(cancel)eval_hook=[&]{stop.request();};else fail_at=1;
    ++search_count;auto r=iterative_search(*c,*p,h,*e,{2,true,1,true},stop.token(),{},use);eval_hook={};fail_at=0;
    check(r.status==(cancel?IterativeSearchStatus::cancelled:IterativeSearchStatus::evaluator_failed)&&!r.completed&&bool(r.unevaluated_fallback)==cancel&&c->export_fen(*p)==fen&&h.matches_root(*c,*p),"native leaf cancellation/evaluator failure restores root and preserves fallback labels");
    leaf_evidence("failure:"+std::to_string(mode)+":"+std::to_string(cancel)+":"+deterministic_statistics(r.statistics));
  }
  std::cout<<"M7.4a.2 deterministic evidence records="<<leaf_evidence_records<<" hash="<<leaf_evidence_hash<<std::endl;
  group.finish();
}
void native_count_regressions() {
  Checkpoint group("group M7.5b complete native leaf counts");
  auto c=create_chess_core();std::size_t positions=0;
  auto parity=[&](Position &p) {
    const auto fen=c->export_fen(p);const auto key=c->position_key(p);
    auto batch=detail::prepare_native_move_batch(*c,p);
    auto legal=c->legal_moves(p);
    check(detail::native_move_batch_size(*batch)==legal.size(),"batch complete count parity");
    std::vector<std::pair<std::string,std::size_t>> members;
    for(std::size_t i=0;i<detail::native_move_batch_size(*batch);++i)
      members.emplace_back(detail::encode_native_batch_member(*c,p,*batch,i),i);
    std::sort(members.begin(),members.end());
    std::vector<std::string> public_texts;
    for(const auto &move:legal)public_texts.push_back(c->encode_move(p,*move));
    std::sort(public_texts.begin(),public_texts.end());
    for(std::size_t i=0;i<members.size();++i)check(members[i].first==public_texts[i],"batch complete canonical UCI parity");
    check(native_legal_move_count(*c,p)==legal.size(),"private count equals complete public legal list");
    check(c->export_fen(p)==fen&&c->position_key(p)==key,"count leaves exact board unchanged");
    // Existing owners keep their position-frame certificates across counting.
    for(const auto &m:legal) {
      const auto text=c->encode_move(p,*m);
      const auto ordinal=std::find_if(members.begin(),members.end(),[&](const auto &v){return v.first==text;})->second;
      auto materialized=detail::materialize_native_batch_member(*c,p,*batch,ordinal);
      auto reference=c->make_move(p,*m);const auto child_fen=c->export_fen(p);c->undo_move(p,*reference);
      auto u=c->make_move(p,*materialized);
      check(c->export_fen(p)==child_fen,"batch materialized child exact board parity");
      bool rejected=false;try{detail::encode_native_batch_member(*c,p,*batch,ordinal);}catch(const std::invalid_argument&){rejected=true;}
      check(rejected,"parent batch rejected while child frame active");
      auto child_batch=detail::prepare_native_move_batch(*c,p);
      check(native_legal_move_count(*c,p)==c->legal_moves(p).size(),"child count parity");
      if(detail::native_move_batch_size(*child_batch)) {
        auto nested=detail::materialize_native_batch_member(*c,p,*child_batch,0);
        auto nested_token=c->make_move(p,*nested);c->undo_move(p,*nested_token);
        check(!detail::encode_native_batch_member(*c,p,*child_batch,0).empty(),"child batch survives nested strict undo");
      }
      c->undo_move(p,*u);
      check(c->encode_move(p,*m)==text&&c->export_fen(p)==fen,"count preserves undo and certificate validity");
      check(detail::encode_native_batch_member(*c,p,*batch,ordinal)==text,"batch original ordinal survives sorting and undo");
      rejected=false;try{detail::encode_native_batch_member(*c,p,*child_batch,0);}catch(const std::invalid_argument&){rejected=true;}
      check(rejected,"undone child batch cannot execute in parent");
      auto sibling=c->make_move(p,*materialized);
      rejected=false;try{detail::materialize_native_batch_member(*c,p,*child_batch,0);}catch(const std::invalid_argument&){rejected=true;}
      check(rejected,"old child batch rejected on sibling re-entry");c->undo_move(p,*sibling);
    }
    auto cloned=c->clone(p);
    check(native_legal_move_count(*c,*cloned)==legal.size(),"count accepts genuine cloned native position");
    bool rejected=false;try{detail::materialize_native_batch_member(*c,*cloned,*batch,0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"batch rejects cloned position identity");
    rejected=false;try{detail::encode_native_batch_member(*c,p,*batch,legal.size());}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"batch rejects out of range ordinal");
    detail::invalidate_native_move_batch(*batch);
    rejected=false;try{detail::encode_native_batch_member(*c,p,*batch,0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"invalidated batch never revives");
    ++positions;
  };
  for(const auto fen:{
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
      "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1",
      "7K/5k2/6q1/8/8/8/8/8 w - - 0 1", "7K/6q1/5k2/8/8/8/8/8 w - - 0 1",
      "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "4k3/8/8/8/8/8/r7/K7 w - - 0 1",
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
      "1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1", "4k3/7P/8/8/8/8/p7/1R2K3 b - - 0 1",
      "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2", "3k4/8/8/8/3pP3/8/8/K2R4 b - e3 0 1",
      "4k3/8/8/3p4/8/8/8/4K3 w - d6 0 2"}) {
    auto p=c->load_fen(fen);parity(*p);
  }
  for(Side side:{Side::white,Side::black})for(int clock:{98,99,100,149,150}) {
    auto p=c->load_fen(std::string("4k3/8/8/8/8/8/8/R3K3 ")+(side==Side::white?"w":"b")+" - - "+std::to_string(clock)+" 1");parity(*p);
    if(clock==100)native_leaf_case(*c,*p,GameHistory::starting_at(*c,*p,false),true);
  }
  // A valid repeatable middlegame with actual complete history, plus check-heavy
  // high-branching imported boards with explicitly incomplete histories.
  auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,true);
  for(const auto text:{"e2e4","e7e5","g1f3","b8c6","f1b5","a7a6","b5a4","g8f6","e1g1","f8e7","f1e1","b7b5","a4b3","d7d6","c2c3","e8g8"}) {
    h=h.after_move(*c,*p,text);auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);parity(*p);
  }
  native_leaf_case(*c,*p,h,true,2);
  for(const auto fen:{
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1"}) {
    auto tactical=c->load_fen(fen);native_leaf_case(*c,*tactical,GameHistory::starting_at(*c,*tactical,false));
  }
  Tree synthetic;synthetic.nodes={{Side::white,{0}}};auto foreign=synthetic.initial_position();
  bool rejected=false;try{native_legal_move_count(synthetic,*p);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"private capability rejects synthetic Core even with native Position");
  rejected=false;try{native_legal_move_count(*c,*foreign);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"private capability rejects foreign Position");
  rejected=false;try{detail::prepare_native_move_batch(synthetic,*p);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"batch generation rejects synthetic Core");
  auto native_batch=detail::prepare_native_move_batch(*c,*p);
  rejected=false;try{detail::encode_native_batch_member(synthetic,*p,*native_batch,0);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"batch description rejects foreign Core");
  rejected=false;try{detail::materialize_native_batch_member(*c,*foreign,*native_batch,0);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"batch materialization rejects foreign Position");
  const auto fen=c->export_fen(*p);c->reset(*p,fen);
  rejected=false;try{detail::encode_native_batch_member(*c,*p,*native_batch,0);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"reset invalidates batch lifetime even for identical FEN");
  detail::NativeMoveBatchOwner expired;
  {auto doomed=c->initial_position();expired=detail::prepare_native_move_batch(*c,*doomed);}
  auto replacement=c->initial_position();
  rejected=false;try{detail::encode_native_batch_member(*c,*replacement,*expired,0);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"destroyed position identity cannot be reused by a batch");
  std::cout<<"M7.5b count corpus positions="<<positions<<" deterministic records="<<leaf_evidence_records<<" hash="<<leaf_evidence_hash<<std::endl;
  group.finish();
}
void native_batch_regressions() {
  Checkpoint group("group M7.5c.2 native TT leaf batches");
  auto c=create_chess_core();auto p=c->initial_position();auto h=GameHistory::starting_at(*c,*p,true);
  const std::vector<std::string> cycle={"g1f3","g8f6","f3g1","f6g8"};
  for(int i=0;i<7;++i){auto text=cycle[i%4];h=h.after_move(*c,*p,text);auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);}
  // Non-compact cases exercise hint/numeric cold/warm, ignored/zero-capacity TT,
  // both root policies, MultiPV, previous-PV and all existing exact record fields.
  native_leaf_case(*c,*p,h);
  for(const auto fen:{"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
                      "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"}) {
    auto root=c->load_fen(fen);native_leaf_case(*c,*root,GameHistory::starting_at(*c,*root,false));
  }
  std::cout<<"M7.5c.2 batch deterministic evidence records="<<leaf_evidence_records<<" hash="<<leaf_evidence_hash<<std::endl;
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
  failures.finish();leaf_ordering_regressions();guarded_leaf_regressions();native_count_regressions();native_batch_regressions();total.finish();
  std::cout<<"M7.2 single-PV exact references, TT cross-policy, draw history, cancellation and failure passed"<<std::endl;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
