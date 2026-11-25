#include <chess_lab/chess_core.hpp>
#include "native_move_batch.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
using namespace chess_lab;
static void require(bool v){if(!v)throw std::runtime_error("S1.11a native snapshot mismatch");}
static void same(const PositionSnapshot &a,const PositionSnapshot &b){
 require(std::equal(a.squares().begin(),a.squares().end(),b.squares().begin()));
 require(a.side_to_move()==b.side_to_move() && a.en_passant_square()==b.en_passant_square());
 require(a.halfmove_clock()==b.halfmove_clock() && a.fullmove_number()==b.fullmove_number());
 require(a.schema_version()==b.schema_version());
 for(auto side:{Side::white,Side::black}){
  require(a.can_castle_kingside(side)==b.can_castle_kingside(side));
  require(a.can_castle_queenside(side)==b.can_castle_queenside(side));
 }
}
int main(){std::string context="initial setup";try{
 auto c=create_chess_core();auto initial=c->initial_position();auto slot=c->snapshot(*initial);auto addr=slot.get();
 unsigned checks=0;
 auto verify=[&](const Position& p){require(detail::native_refresh_search_snapshot(*c,p,slot));require(slot.get()==addr);same(*slot,*c->snapshot(p));++checks;};
 std::unique_ptr<const PositionSnapshot> empty;require(!detail::native_refresh_search_snapshot(*c,*initial,empty));
 for(const char* fen:{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1","r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1","7k/8/8/3pP3/8/8/8/7K w - d6 0 1","1r5k/P7/8/8/8/8/8/7K w - - 0 1","7k/8/8/8/8/8/p7/7K b - - 0 1"}){
  context=std::string("FEN: ")+fen;
  auto p=c->load_fen(fen);verify(*p);auto detached=c->snapshot(*p);auto before=c->clone(*p);
  for(const auto& m:c->legal_moves(*p)){auto u=c->make_move(*p,*m);verify(*p);same(*detached,*c->snapshot(*before));c->undo_move(*p,*u);verify(*p);same(*detached,*slot);}
 }
 std::mt19937 rng(0x5111a);
 for(int game=0;game<64;++game){context="random game "+std::to_string(game);auto p=c->initial_position();verify(*p);
  for(int ply=0;ply<80;++ply){context="random game "+std::to_string(game)+" ply "+std::to_string(ply);auto legal=c->legal_moves(*p);if(legal.empty())break;
   auto detached=c->snapshot(*p);auto& m=legal[rng()%legal.size()];auto text=c->encode_move(*p,*m);auto u=c->make_move(*p,*m);verify(*p);
   c->undo_move(*p,*u);verify(*p);same(*detached,*slot);auto fresh=c->decode_move(*p,text);auto next=c->make_move(*p,*fresh);(void)next;verify(*p);
  }
 }
 std::cout<<"S111A_NATIVE_SNAPSHOT_PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<context<<": "<<e.what()<<'\n';return 1;}}
