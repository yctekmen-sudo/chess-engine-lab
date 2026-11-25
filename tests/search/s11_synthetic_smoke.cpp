// Production CTest. Synthetic forwarding ChessCore deliberately uses the
// generic adapter path instead of native batching; preserve API portability.
#include <chess_lab/search.hpp>
#include "material.hpp"
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
using namespace chess_lab;

class ForwardCore final : public ChessCore {
 public:
  explicit ForwardCore(const ChessCore &under) : c(under) {}
  const ChessCore &c;
  std::unique_ptr<Position> initial_position() const override {return c.initial_position();}
  std::unique_ptr<Position> load_fen(std::string_view s) const override {return c.load_fen(s);}
  std::string export_fen(const Position &p) const override {return c.export_fen(p);}
  std::unique_ptr<Position> clone(const Position &p) const override {return c.clone(p);}
  std::vector<std::unique_ptr<Move>> legal_moves(const Position &p) const override {return c.legal_moves(p);}
  std::unique_ptr<UndoToken> make_move(Position &p,const Move &m) const override {return c.make_move(p,m);}
  void undo_move(Position &p,UndoToken &u) const override {c.undo_move(p,u);}
  std::string encode_move(const Position &p,const Move &m) const override {return c.encode_move(p,m);}
  std::unique_ptr<Move> decode_move(const Position &p,std::string_view s) const override {return c.decode_move(p,s);}
  void reset(Position &p,std::string_view s) const override {c.reset(p,s);}
  bool in_check(const Position &p) const override {return c.in_check(p);}
  PositionStatus status(const Position &p) const override {return c.status(p);}
  bool known_dead_position(const Position &p) const override {return c.known_dead_position(p);}
  std::uint64_t position_key(const Position &p) const override {return c.position_key(p);}
  std::uint64_t repetition_key(const Position &p) const override {return c.repetition_key(p);}
  std::unique_ptr<const PositionSnapshot> snapshot(const Position &p) const override {return c.snapshot(p);}
};
static void check(bool value,const char *why) {if(!value) throw std::runtime_error(why);}
static void run_case(const ChessCore &core,const std::string &fen,const std::string &config) {
  auto root=core.load_fen(fen);auto hist=GameHistory::starting_at(core,*root,false);
  const auto original=core.export_fen(*root);
  auto make_evaluator=[&] {
    return EvaluatorModule::provider(chess_lab_material_provider,std::string(64,'a'),"s11-synthetic-smoke").create(config);
  };
  auto first=make_evaluator(),second=make_evaluator();
  SearchOptions options;options.nominal_depth=2;options.alpha_beta_enabled=true;
  auto r=search(core,*root,hist,*first,options);
  auto rr=search(core,*root,hist,*second,options);
  check(r.status==SearchStatus::complete,"synthetic search must finish");
  check(r.score.has_value()&&r.decision.has_value(),"synthetic missing result");
  check(r.completed_depth==2,"synthetic missed requested depth");
  check(r.status==rr.status && r.score==rr.score && r.decision==rr.decision &&
        r.principal_variation==rr.principal_variation &&
        r.statistics.visited_nodes==rr.statistics.visited_nodes &&
        r.statistics.evaluator_calls==rr.statistics.evaluator_calls,
        "synthetic nondeterministic repeat");
  auto cursor=core.clone(*root);
  for(const auto &uci:r.principal_variation) {
    const auto available=core.legal_moves(*cursor);
    bool found=false;
    for(const auto &move:available)
      if(core.encode_move(*cursor,*move)==uci){auto undo=core.make_move(*cursor,*move);found=true;break;}
    check(found,"synthetic illegal PV move");
  }
  check(core.export_fen(*root)==original,"synthetic mutated root");
  check(hist.matches_root(core,*root),"synthetic mutated history");
}
// A queen can capture the undefended-looking rook, but a pawn recaptures.
// The depth-one static horizon prefers Qxd5; quiescence must reject it.
static void horizon_case(const ChessCore &core,const std::string &config,bool enabled) {
  auto root=core.load_fen("7k/8/2p5/3r4/8/8/8/K2Q4 w - - 0 1");
  auto hist=GameHistory::starting_at(core,*root,false);
  auto e=EvaluatorModule::provider(chess_lab_material_provider,std::string(64,'a'),"s1-horizon-smoke").create(config);
  SearchOptions options; options.nominal_depth=1;
  auto r=search(core,*root,hist,*e,options);
  check(r.status==SearchStatus::complete && r.decision.has_value(),"horizon search failed");
  check((r.decision->move=="d1d5")!=enabled,"QS on/off horizon contract");
}
int main(int argc,char **argv) {
 try {
  auto native=create_chess_core();ForwardCore f(*native);
  check(argc>=2 && (std::string(argv[argc-1])=="on" || std::string(argv[argc-1])=="off"),"expected QS mode argument");
  const bool enabled=std::string(argv[argc-1])=="on";
  for(const auto &config:std::vector<std::string>{"{}","{\"queen_cp\":800}"}) {
    horizon_case(*native,config,enabled);
    horizon_case(f,config,enabled);
  }
  for(const auto &fen:std::vector<std::string>{
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "4k3/8/8/8/8/8/4r3/4K3 w - - 0 1",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "4k3/P7/8/8/8/8/8/4K3 w - - 0 1"}) {
    run_case(f,fen,"{}");
    run_case(f,fen,"{\"queen_cp\":800}");
    run_case(*native,fen,"{\"queen_cp\":800}");
  }
  std::cout<<"S11_SYNTHETIC_SMOKE_PASS\n";
  return 0;
 }catch(const std::exception &e){std::cerr<<"S11_SYNTHETIC_SMOKE_FAIL "<<e.what()<<'\n';return 2;}
}
