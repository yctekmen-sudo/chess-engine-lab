#include <chess_lab/chess_core.hpp>
#include "native_move_batch.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <random>
using namespace chess_lab;
static unsigned checked=0;
static void verify(const ChessCore& core,const Position& pos){
    detail::NativeSeeMasks got;
    if(!detail::native_see_masks(core,pos,got))throw std::runtime_error("Native adapter rejected native board");
    auto snap=core.snapshot(pos);
    detail::NativeSeeMasks expected{};expected.kings[0]=expected.kings[1]=-1;
    const auto sq=snap->squares();
    for(int i=0;i<64;++i){
        const int pc=static_cast<int>(sq[i]);if(pc==12)continue;
        const int side=pc<6?1:0,kind=pc%6+1;
        const std::uint64_t bit=std::uint64_t{1}<<i;
        expected.sides[side]|=bit;expected.kinds[kind]|=bit;
        if(kind==6)expected.kings[side]=i;
    }
    for(int i=0;i<2;++i)if(got.sides[i]!=expected.sides[i]||got.kings[i]!=expected.kings[i])throw std::runtime_error("Side/king mismatch");
    for(int i=0;i<7;++i)if(got.kinds[i]!=expected.kinds[i])throw std::runtime_error("Piece mask mismatch");
    ++checked;
}
int main(){
 try{
    auto core=create_chess_core();std::mt19937 rng(0x51761);
    for(const char* fen:{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "7k/8/8/3pP3/8/8/8/7K w - d6 0 1", "1r5k/P7/8/8/8/8/8/7K w - - 0 1", "7k/8/8/8/8/8/p7/7K b - - 0 1"}){
      auto p=core->load_fen(fen);verify(*core,*p);
      auto legal=core->legal_moves(*p);
      for(const auto& m:legal){auto u=core->make_move(*p,*m);verify(*core,*p);core->undo_move(*p,*u);verify(*core,*p);}
    }
    for(int game=0;game<64;++game){auto p=core->initial_position();verify(*core,*p);
      for(int ply=0;ply<80;++ply){auto legal=core->legal_moves(*p);if(legal.empty())break;
        auto& m=legal[rng()%legal.size()];const auto text=core->encode_move(*p,*m);auto u=core->make_move(*p,*m);verify(*core,*p);
        core->undo_move(*p,*u);verify(*core,*p);auto fresh=core->decode_move(*p,text);auto next=core->make_move(*p,*fresh);(void)next;verify(*core,*p);
      }
    }
    std::cout<<"S17G1_NATIVE_MASKS_PASS positions="<<checked<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
