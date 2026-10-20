#include "m4_fixtures.hpp"
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

SearchResult legacy_line(const SearchVariation &v) {
  SearchResult r;
  r.decision = v.decision; r.score = v.score;
  r.principal_variation = v.principal_variation;
  r.pv_end = v.pv_end; r.pv_intended_move = v.pv_intended_move;
  return r;
}
void equal(const SearchVariation &v, const Expected &x) {
  check(v.decision == x.action && v.score == x.score &&
        v.principal_variation == x.pv && v.pv_end == x.end &&
        v.pv_intended_move == x.intended, "exact action/score/PV/end oracle");
}
void equal(const SearchVariation &v, const SearchResult &x) {
  check(v.decision == x.decision && v.score == x.score &&
        v.principal_variation == x.principal_variation && v.pv_end == x.pv_end &&
        v.pv_intended_move == x.pv_intended_move, "M3 action/score/PV/end compatibility");
}
std::vector<Expected> tree_ranking(const Tree &t, int depth) {
  std::vector<Expected> out;
  const auto &n = t.nodes[0];
  if (n.clock >= 100)
    out.push_back({SearchScore::finite(0), {DecisionKind::claim_draw, {}}, {}, PVEnd::claim_draw, {}});
  else
    for (size_t i = 0; i < n.children.size(); ++i)
      if (t.nodes[n.children[i]].clock >= 100) {
        auto m = std::string(1, char('a' + i)) + "1a2";
        out.push_back({SearchScore::finite(0), {DecisionKind::claim_draw_with_intended_move, m}, {}, PVEnd::claim_draw_with_intended_move, m});
        break;
      }
  for (size_t i = 0; i < n.children.size(); ++i) {
    auto x = oracle(t, n.children[i], depth - 1);
    x.score = x.score.one_ply_farther();
    auto m = std::string(1, char('a' + i)) + "1a2";
    x.action = {DecisionKind::play, m}; x.pv.insert(x.pv.begin(), m);
    out.push_back(std::move(x));
  }
  // Stable sort preserves independently enumerated claim/move tie order.
  std::stable_sort(out.begin(), out.end(), [&](const auto &a, const auto &b) {
    return n.side == Side::white ? a.score > b.score : a.score < b.score;
  });
  return out;
}
void generated() {
  tree_values = true; fail_at = 0;
  unsigned rng = 72912;
  auto next = [&] { return rng = rng * 1664525U + 1013904223U; };
  bool reordered = false;
  for (int trial = 0; trial < 80; ++trial) {
    Tree t; values.clear();
    std::function<int(int, Side)> gen = [&](int depth, Side side) {
      int id = int(t.nodes.size());
      t.nodes.push_back({side, {}});
      values.push_back(std::array<double, 7>{-1e300, -3, -0.0, 0, 2, 4, 1e300}[next() % 7]);
      unsigned kind = next() % 23;
      if (id && kind == 0) t.nodes[id].status = PositionStatus::checkmate;
      if (id && kind == 1) t.nodes[id].status = PositionStatus::stalemate;
      if (id && kind == 2) t.nodes[id].dead = true;
      if (kind == 3) t.nodes[id].clock = 100;
      if (id && kind == 4) t.nodes[id].clock = 150;
      if (depth)
        for (int i = 0; i < 3; ++i) {
          int child = gen(depth - 1, side == Side::white ? Side::black : Side::white);
          t.nodes[id].children.push_back(child);
        }
      else t.nodes[id].children.push_back(id);
      return id;
    };
    gen(3, trial % 2 ? Side::white : Side::black);
    auto p = t.initial_position(); auto h = GameHistory::starting_at(t, *p, trial % 2);
    for (bool ab : {false, true}) for (bool order : {false, true})
      for (size_t n : {1, 2, 3, 8}) {
        auto e = evaluator();
        std::vector<CompletedDepth> events;
        auto r = iterative_search(t, *p, h, *e, {3, ab, n, order}, {}, [&](CompletedDepth s) {
          auto expected = tree_ranking(t, s.depth);
          check(s.multipv.size() == std::min(n, expected.size()), "exact distinct candidate count");
          equal(s.best, expected[0]);
          for (size_t j = 0; j < s.multipv.size(); ++j) {
            equal(s.multipv[j], expected[j]); verify_pv(t, *p, legacy_line(s.multipv[j]));
          }
          if (!events.empty()) check(s.statistics.visited_nodes > events.back().statistics.visited_nodes, "cumulative nodes");
          check(s.provenance.evaluator_identity.fingerprint == e->identity().fingerprint &&
                s.provenance.history == h.occurrences() && s.provenance.history_incomplete == !h.complete(), "snapshot provenance");
          reordered |= s.best.decision.kind == DecisionKind::play && s.best.decision.move != "a1a2";
          events.push_back(std::move(s));
        });
        check(r.status == IterativeSearchStatus::depth_reached && r.completed && events.size() == 3, "three atomic depths");
        for (const auto &s : events) {
          equal(s.best, tree_ranking(t, s.depth)[0]);
          auto ref=evaluator();equal(s.best,search(t,*p,h,*ref,{s.depth,ab}));
        }
        equal(r.completed->best, tree_ranking(t, 3)[0]);
        check(t.balance == 0 && t.id(*p) == 0 && h.occurrences().size() == 1, "generated LIFO restoration");
        check(ab || !r.statistics.alpha_beta_cutoffs, "no pruning when disabled");
      }
  }
  check(reordered, "noncanonical prior PV hints exercised");
  tree_values = false;
  std::cout << "80 generated trees, all depths/ranks/PVs, both sides and ordering/pruning modes passed\n";
}
// Explicit bound trap: depth-one prefers b; later equal branches must select
// a again, including at internal PV positions after prior-PV ordering.
void tie_trap() {
  Tree t;
  t.nodes = {{Side::white,{1,2}}, {Side::black,{3,4}}, {Side::black,{5,6}},
             {Side::white,{7,8}}, {Side::white,{9,10}}, {Side::white,{11,12}},
             {Side::white,{13,14}}};
  for (int i = 7; i < 15; ++i) t.nodes.push_back({Side::black,{i}});
  values.assign(15,0); values[1]=-3; values[2]=7;
  values[3]=8; values[4]=-2; values[5]=6; values[6]=9;
  tree_values=true;
  auto p=t.initial_position(); auto h=GameHistory::starting_at(t,*p);
  for (bool order : {false,true}) {
    auto e=evaluator();
    auto r=iterative_search(t,*p,h,*e,{3,true,8,order},{},[&](CompletedDepth d) {
      auto expected=tree_ranking(t,d.depth);
      for(size_t i=0;i<expected.size();++i) equal(d.multipv[i],expected[i]);
    });
    check(r.completed->best.decision.move=="a1a2" &&
          r.completed->best.principal_variation==std::vector<std::string>({"a1a2","a1a2","a1a2"}), "ordered bound/tie canonical PV");
  }
  tree_values=false;
}
void mate_distances() {
  tree_values=true;
  for(Side side:{Side::white,Side::black}) for(bool winning:{false,true}) {
    Side other=side==Side::white?Side::black:Side::white;
    Tree t;
    if(winning) t.nodes={{side,{1,2}},{other,{},PositionStatus::checkmate},
      {other,{3}},{side,{4}},{other,{},PositionStatus::checkmate}};
    else t.nodes={{side,{1,3}},{other,{2}},{side,{},PositionStatus::checkmate},
      {other,{4}},{side,{5}},{other,{6}},{side,{},PositionStatus::checkmate}};
    values.assign(t.nodes.size(),0);
    auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);
    for(bool ab:{false,true})for(bool order:{false,true}) {
      auto e=evaluator();
      auto r=iterative_search(t,*p,h,*e,{4,ab,3,order},{},[&](CompletedDepth d){
        auto expected=tree_ranking(t,d.depth);
        for(size_t i=0;i<expected.size();++i)equal(d.multipv[i],expected[i]);
      });
      check(r.completed->best.decision.move==(winning?"a1a2":"b1a2"),"rank faster victory / slower defeat for each side");
      check(t.balance==0,"mate-distance LIFO restoration");
    }
  }
  tree_values=false;
}
// Independent clone-per-edge real-chess oracle, exact history and canonical
// decisions. It never uses search(), traversal, windows or production ranking.
std::vector<Expected> real_candidates(const ChessCore &, const Position &, const GameHistory &, int);
Expected real_reference(const ChessCore &c, const Position &p, const GameHistory &h, int depth) {
  auto s=c.snapshot(p); auto status=c.status(p);
  auto sig=repetition_signature(c,p);
  auto count=std::count(h.occurrences().begin(),h.occurrences().end(),sig);
  if(status==PositionStatus::checkmate) {
    bool w=s->side_to_move()==Side::white;
    return {w?SearchScore::black_mate(0):SearchScore::white_mate(0),
      {DecisionKind::terminal,{},w?TerminalOutcome::black_win:TerminalOutcome::white_win},{},PVEnd::terminal,{}};
  }
  if(status==PositionStatus::stalemate || c.known_dead_position(p) || count>=5 || s->halfmove_clock()>=150)
    return {SearchScore::finite(0),{DecisionKind::terminal,{},TerminalOutcome::draw},{},PVEnd::terminal,{}};
  return real_candidates(c,p,h,depth).front();
}
std::vector<Expected> real_candidates(const ChessCore &c, const Position &p, const GameHistory &h, int depth) {
  auto s=c.snapshot(p); auto sig=repetition_signature(c,p);
  auto count=std::count(h.occurrences().begin(),h.occurrences().end(),sig);
  std::vector<std::string> moves;
  for (auto &m:c.legal_moves(p)) moves.push_back(c.encode_move(p,*m));
  std::sort(moves.begin(),moves.end());
  std::vector<Expected> out;
  if(count>=3 || s->halfmove_clock()>=100)
    out.push_back({SearchScore::finite(0),{DecisionKind::claim_draw,{}},{},PVEnd::claim_draw,{}});
  else for(auto &m:moves) {
    auto q=c.clone(p); auto next=h.after_move(c,p,m);
    auto move=c.decode_move(*q,m); auto u=c.make_move(*q,*move);
    auto signature=repetition_signature(c,*q);
    if(std::count(next.occurrences().begin(),next.occurrences().end(),signature)>=3 || c.snapshot(*q)->halfmove_clock()>=100) {
      out.push_back({SearchScore::finite(0),{DecisionKind::claim_draw_with_intended_move,m},{},PVEnd::claim_draw_with_intended_move,m}); break;
    }
  }
  if(!depth) out.push_back({SearchScore::finite(constant),{DecisionKind::play,{}},{},PVEnd::horizon,{}});
  else for(auto &m:moves) {
    auto q=c.clone(p); auto next=h.after_move(c,p,m);
    auto move=c.decode_move(*q,m); auto u=c.make_move(*q,*move);
    auto x=real_reference(c,*q,next,depth-1); x.score=x.score.one_ply_farther();
    x.action={DecisionKind::play,m}; x.pv.insert(x.pv.begin(),m); out.push_back(std::move(x));
  }
  std::stable_sort(out.begin(),out.end(),[&](const auto&a,const auto&b){return s->side_to_move()==Side::white?a.score>b.score:a.score<b.score;});
  return out;
}
void real_case(const ChessCore &c,const Position&p,const GameHistory&h,int max=2) {
  auto fen=c.export_fen(p); auto occurrences=h.occurrences();
  for(bool ab:{false,true}) for(bool order:{false,true}) {
    auto e=evaluator();
    std::vector<CompletedDepth> snapshots;
    auto r=iterative_search(c,p,h,*e,{max,ab,256,order},{},[&](CompletedDepth d){
      auto expected=real_candidates(c,p,h,d.depth);
      check(d.multipv.size()==expected.size(),"all exact real root decisions");
      for(size_t i=0;i<expected.size();++i) {equal(d.multipv[i],expected[i]);verify_pv(c,p,legacy_line(d.multipv[i]));}
      snapshots.push_back(std::move(d));
    });
    check(r.status==IterativeSearchStatus::depth_reached && r.completed,"real iterations");
    for(auto &d:snapshots){auto ref=evaluator();equal(d.best,search(c,p,h,*ref,{d.depth,ab}));}
    // N=1, ordering off, same final and all intermediate depths as M3.
    snapshots.clear();
    auto one=iterative_search(c,p,h,*e,{max,ab,1,false},{},[&](CompletedDepth d){snapshots.push_back(std::move(d));});
    for(auto &d:snapshots){auto ref=evaluator();equal(d.best,search(c,p,h,*ref,{d.depth,ab}));}
    equal(one.completed->best,legacy_line(r.completed->best));
    check(c.export_fen(p)==fen && h.occurrences()==occurrences,"real immutable root/history");
  }
}
void real_rules_m4() {
  auto c=create_chess_core(); fail_at=0;
  for(auto fen:{"7k/8/5KQ1/8/8/8/8/8 w - - 149 1", "4k3/8/8/8/8/8/P7/4K3 b - - 0 1",
                "4k3/8/8/8/8/8/8/R3K3 w - - 99 1", "4k3/8/8/8/8/8/8/R3K3 b - - 100 1"}) {
    auto p=c->load_fen(fen); auto h=GameHistory::starting_at(*c,*p);
    for(double v:{-1e300,-0.0,1e300}) {constant=v;real_case(*c,*p,h);}
  }
  constant=0;
  for(auto fen:{"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
               "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"}) {
    auto ep=c->load_fen(fen);auto history=GameHistory::starting_at(*c,*ep);
    real_case(*c,*ep,history,1);
  }
  auto p=c->initial_position(); auto h=GameHistory::starting_at(*c,*p,true);
  real_case(*c,*p,h,1);
  std::vector<std::string> cycle={"g1f3","g8f6","f3g1","f6g8"};
  for(int i=0;i<16;++i) {
    const auto &text=cycle[i%4]; h=h.after_move(*c,*p,text);
    auto m=c->decode_move(*p,text);auto u=c->make_move(*p,*m);
    if(i==6 || i==7) real_case(*c,*p,h,1);
  }
  auto e=evaluator(); CancellationSource source;source.request();
  auto five=iterative_search(*c,*p,h,*e,{2,true,3,true},source.token());
  check(five.status==IterativeSearchStatus::terminal && five.terminal_decision->terminal_outcome==TerminalOutcome::draw &&
        !five.completed && !five.unevaluated_fallback,"actual fivefold terminal precedes cancellation");
  for(auto fen:{"7k/6Q1/5K2/8/8/8/8/8 b - - 150 1","7K/6q1/5k2/8/8/8/8/8 w - - 150 1",
               "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1","4k3/8/8/8/8/8/8/4K3 w - - 0 1",
               "4k3/8/8/8/8/8/8/R3K3 w - - 150 1"}) {
    auto q=c->load_fen(fen);auto hist=GameHistory::starting_at(*c,*q);
    auto ref=real_reference(*c,*q,hist,0);
    auto r=iterative_search(*c,*q,hist,*e,{},source.token(),[](CompletedDepth){throw std::runtime_error("terminal must not publish");});
    check(r.status==IterativeSearchStatus::terminal && r.terminal_decision==ref.action && r.terminal_score==ref.score &&
          !r.completed && !r.unevaluated_fallback,"terminal score/outcome depth zero");
  }
  // Hypothetical opponent move carries a separately advanced matching history.
  auto real=c->initial_position();auto hist=GameHistory::starting_at(*c,*real,true);
  auto hypothetical=c->clone(*real);auto next=hist.after_move(*c,*real,"e2e4");
  auto m=c->decode_move(*hypothetical,"e2e4");auto u=c->make_move(*hypothetical,*m);
  auto r=iterative_search(*c,*hypothetical,next,*e,{1,true,2,true});
  check(r.provenance.root.side_to_move==1 && c->snapshot(*real)->side_to_move()==Side::white &&
        next.matches_root(*c,*hypothetical) && hist.matches_root(*c,*real),"hypothetical provenance without turn override");
  std::cout<<"real chess MultiPV, mate/clock precedence, intended/current claims, exact history and hypothetical root passed\n";
}
struct ProbeTree : Tree {
  mutable size_t makes=0, undos=0;
  std::function<void(size_t)> on_make, on_undo;
  bool broken_undo=false, unknown_error=false;
  std::unique_ptr<UndoToken> make_move(Position &p,const Move&m) const override {
    auto u=Tree::make_move(p,m); ++makes;
    if(on_make) on_make(makes);
    return u;
  }
  void undo_move(Position&p,UndoToken&u) const override {
    if(broken_undo) throw std::runtime_error("independent undo failure");
    Tree::undo_move(p,u); ++undos;
    if(on_undo) on_undo(undos);
  }
  std::vector<std::unique_ptr<Move>> legal_moves(const Position&p) const override {
    if(unknown_error) throw 23;
    return Tree::legal_moves(p);
  }
};
void small_tree(Tree &t) {
  t.nodes={{Side::white,{1,2}}, {Side::black,{3,4}}, {Side::black,{5,6}},
           {Side::white,{7}}, {Side::white,{8}}, {Side::white,{9}}, {Side::white,{10}}};
  for(int i=7;i<=10;++i)t.nodes.push_back({Side::black,{i}});
  values.assign(11,2);tree_values=false;constant=2;fail_at=0;eval_hook={};
}
void empty_error(const IterativeSearchResult&r,IterativeSearchStatus status) {
  check(r.status==status && !r.completed && !r.unevaluated_fallback && !r.terminal_decision && !r.terminal_score &&
        !r.diagnostic.empty(),"failure invalidates all final playable values");
}
void fallback_check(const IterativeSearchResult&r) {
  check(r.status==IterativeSearchStatus::cancelled && !r.completed &&
        r.unevaluated_fallback==ChessDecision{DecisionKind::play,"a1a2"} &&
        !r.terminal_decision && !r.terminal_score,"one explicitly unevaluated canonical legal fallback");
}
void cancellation_failures() {
  ProbeTree t;small_tree(t);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p,true);
  auto e=evaluator();
  CancellationToken expired;
  {CancellationSource source;expired=source.token();source.request();}
  fallback_check(iterative_search(t,*p,h,*e,{},expired));
  // Fresh/default tokens do not inherit a previous invocation's cancellation.
  check(iterative_search(t,*p,h,*e,{1,true,1,true}).status==IterativeSearchStatus::depth_reached,"token isolation");
  CancellationSource probe;
  t.on_make=[&](size_t){probe.request();};
  auto before=calls;
  fallback_check(iterative_search(t,*p,h,*e,{},probe.token()));
  check(calls==before && t.balance==0,"cancellation in prospective claim probe restores LIFO");
  t.on_make={};
  CancellationSource recursive;
  eval_hook=[&]{recursive.request();};
  fallback_check(iterative_search(t,*p,h,*e,{3,true,3,true},recursive.token()));
  check(t.balance==0,"recursive abort restoration");eval_hook={};
  CancellationSource late;std::vector<CompletedDepth> events;
  auto r=iterative_search(t,*p,h,*e,{3,true,3,true},late.token(),[&](CompletedDepth d){
    events.push_back(std::move(d));eval_hook=[&]{late.request();};
  });
  eval_hook={};
  check(r.status==IterativeSearchStatus::cancelled && r.completed->depth==1 && events.size()==1 &&
        !r.unevaluated_fallback && r.statistics.evaluator_calls>r.completed->statistics.evaluator_calls && t.balance==0,"later partial iteration discarded, cumulative abandoned work");
  equal(events[0].best,legacy_line(r.completed->best));
  CancellationSource callback;
  r=iterative_search(t,*p,h,*e,{1,true,3,true},callback.token(),[&](CompletedDepth d){
    d.best.principal_variation.clear();d.multipv.clear();callback.request();
  });
  check(r.status==IterativeSearchStatus::cancelled && r.completed->depth==1 && !r.completed->multipv.empty() &&
        !r.completed->best.principal_variation.empty(),"callback cancel after commit and detached copy");
  // Count a dry depth-one traversal, then signal on the final undo just before
  // publication. This depth must be discarded despite all candidates finishing.
  t.undos=0;r=iterative_search(t,*p,h,*e,{1,true,3,false});size_t last=t.undos;
  CancellationSource boundary;t.undos=0;t.on_undo=[&](size_t n){if(n==last)boundary.request();};
  int published=0;
  fallback_check(iterative_search(t,*p,h,*e,{1,true,3,false},boundary.token(),[&](CompletedDepth){++published;}));
  check(!published && t.balance==0,"precommit cancellation discards entire ranking");t.on_undo={};
  // Late ABI failure invalidates earlier provisional depths and quarantines.
  int progress=0;
  r=iterative_search(t,*p,h,*e,{3,true,3,true},{},[&](CompletedDepth){++progress;fail_at=calls+1;});
  empty_error(r,IterativeSearchStatus::evaluator_failed);
  check(progress==1 && !e->usable() && t.balance==0,"late failure quarantine and restoration");
  before=calls;
  empty_error(iterative_search(t,*p,h,*e),IterativeSearchStatus::evaluator_failed);
  check(calls==before,"quarantined provider never dispatched again");
  fail_at=0;e=evaluator();
  for(int kind:{0,1,2}) {
    r=iterative_search(t,*p,h,*e,{3,true,3,true},{},[&](CompletedDepth){
      if(kind==0)throw std::runtime_error("callback failure");
      if(kind==1)throw EvaluationError("callback-owned error, not provider failure");
      throw 5;
    });
    empty_error(r,IterativeSearchStatus::search_error);
    check(e->usable() && t.balance==0,"callback error distinct from evaluator quarantine");
  }
  r=iterative_search(t,*p,h,*e,{2,true,3,true},{},[&](CompletedDepth){t.throw_at=2;});
  empty_error(r,IterativeSearchStatus::search_error);check(t.balance==0 && e->usable(),"late core traversal failure unwinds");t.throw_at=-1;
  t.unknown_error=true;empty_error(iterative_search(t,*p,h,*e),IterativeSearchStatus::search_error);t.unknown_error=false;
  t.broken_undo=true;empty_error(iterative_search(t,*p,h,*e),IterativeSearchStatus::search_error);t.broken_undo=false;
  check(t.id(*p)==0 && h.matches_root(t,*p) && e->usable(),"undo failure discards private clone, preserves caller root/history");
  // Account for the deliberately failed private-clone undo before next tests.
  t.balance=0;
  for(int d:{0,65,-1})empty_error(iterative_search(t,*p,h,*e,{d,true,1,true}),IterativeSearchStatus::search_error);
  for(size_t n:std::array<size_t,3>{0,257,size_t(-1)})empty_error(iterative_search(t,*p,h,*e,{1,true,n,true}),IterativeSearchStatus::search_error);
  auto other=t.clone(*p);auto m=t.decode_move(*other,"a1a2");auto u=t.make_move(*other,*m);
  auto mismatch=iterative_search(t,*other,h,*e);empty_error(mismatch,IterativeSearchStatus::search_error);
  check(mismatch.diagnostic.find("history/root mismatch")!=std::string::npos,"explicit mismatch diagnostic");t.undo_move(*other,*u);
  std::cout<<"deterministic cancellation/progress boundaries, late errors, quarantine, lifetime and cleanup passed\n";
}
void limit() {
  Tree t;
  for(int i=0;i<66;++i)t.nodes.push_back({i%2?Side::black:Side::white,{std::min(i+1,65)}});
  auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);auto e=evaluator();
  int count=0;
  auto r=iterative_search(t,*p,h,*e,{}, {},[&](CompletedDepth d){check(d.depth==++count,"consecutive depth commits to cap");});
  check(r.status==IterativeSearchStatus::depth_limit_reached && count==64 && r.completed->depth==64 && t.balance==0,"cheap generated 64-ply safety cap");
  auto bounded=iterative_search(t,*p,h,*e,{64,true,1,true});
  check(bounded.status==IterativeSearchStatus::depth_reached && bounded.completed->depth==64,"explicit depth 64 termination distinct");
}
#ifndef __EMSCRIPTEN__
void native_thread() {
  Tree t;small_tree(t);auto p=t.initial_position();auto h=GameHistory::starting_at(t,*p);auto e=evaluator();
  CancellationSource source;
  std::atomic<bool> entered=false, timed_out=false;
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  std::thread canceller([&]{
    while(!entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    if(!entered.load(std::memory_order_acquire))timed_out=true;
    source.request();
  });
  eval_hook=[&]{
    entered.store(true,std::memory_order_release);
    while(!source.token().requested() && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  };
  auto r=iterative_search(t,*p,h,*e,{3,true,3,true},source.token());
  canceller.join();eval_hook={};fallback_check(r);
  check(!timed_out && entered && t.balance==0,"real second thread requests cancellation during evaluation");
  const auto caller=std::this_thread::get_id();
  r=iterative_search(t,*p,h,*e,{1,true,1,true},{},[&](CompletedDepth){check(std::this_thread::get_id()==caller,"callback thread affinity");});
  check(r.completed.has_value(),"synchronous callback");
  std::cout<<"native cross-thread atomic cancellation and callback affinity passed\n";
}
#endif
int main(int argc,char **argv) try {
  if(argc>1)digest=argv[1];
  generated();tie_trap();mate_distances();real_rules_m4();cancellation_failures();limit();
#ifndef __EMSCRIPTEN__
  native_thread();
#endif
  std::cout<<"M4 iterative search passed\n";
} catch(const std::exception &e) {std::cerr<<"M4 failure: "<<e.what()<<'\n';return 1;}
