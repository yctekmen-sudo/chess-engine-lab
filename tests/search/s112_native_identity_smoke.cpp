#include <chess_lab/chess_core.hpp>
#include "native_move_batch.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace chess_lab;
static void require(bool v){if(!v)throw std::runtime_error("S1.12 direct identity mismatch");}
int main(){try{
 auto c=create_chess_core(); unsigned checks=0;
 auto verify=[&](const Position& p){
   std::array<PieceCode,64> pieces; Side side; std::array<bool,4> rights;
   detail::NativeSearchMetadata m;
   detail::native_read_search_identity(*c,p,pieces,side,rights,m);
   auto ref=c->snapshot(p);
   require(std::equal(pieces.begin(),pieces.end(),ref->squares().begin()));
   require(side==ref->side_to_move() && m.raw_ep==ref->en_passant_square());
   require(m.half==ref->halfmove_clock() && m.full==ref->fullmove_number());
   require(rights==std::array<bool,4>{ref->can_castle_kingside(Side::white),ref->can_castle_queenside(Side::white),ref->can_castle_kingside(Side::black),ref->can_castle_queenside(Side::black)});
   ++checks;
 };
 for(const char* fen:{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1","r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 98 42","7k/8/8/3pP3/8/8/8/7K w - d6 0 1","1r5k/P7/8/8/8/8/8/7K w - - 0 1","7k/8/8/8/8/8/p7/7K b - - 0 1","k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"}){
    auto p=c->load_fen(fen);verify(*p);
    for(const auto& m:c->legal_moves(*p)){auto u=c->make_move(*p,*m);verify(*p);c->undo_move(*p,*u);verify(*p);}
 }
 std::mt19937 rng(0x5112);
 for(int game=0;game<32;++game){auto p=c->initial_position();verify(*p);
   for(int ply=0;ply<80;++ply){auto legal=c->legal_moves(*p);if(legal.empty())break;
     auto& m=legal[rng()%legal.size()]; auto text=c->encode_move(*p,*m);auto u=c->make_move(*p,*m);verify(*p);c->undo_move(*p,*u);verify(*p);
     auto fresh=c->decode_move(*p,text);auto next=c->make_move(*p,*fresh);(void)next;verify(*p);
   }
 }
 std::cout<<"S112_NATIVE_IDENTITY_PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
