#include <chess_lab/search.hpp>
#include "transposition_table_internal.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
#include <tuple>
using namespace chess_lab;
namespace d=chess_lab::detail;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static std::uint64_t old_mix(std::uint64_t x){
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
static auto native_squares(const RepetitionSignature& s){
    std::array<unsigned,64> a{};
    std::transform(s.pieces.begin(),s.pieces.end(),a.begin(),[](PieceCode p){return unsigned(p);});
    return a;
}
static std::uint64_t native_hash(const RepetitionSignature& s){
    std::uint64_t h=1469598103934665603ULL;
    for(auto p:native_squares(s))h=(h^p)*1099511628211ULL;
    h=old_mix(h^std::uint64_t(s.side));
    for(bool b:s.castling)h=old_mix(h^std::uint64_t(b));
    return old_mix(h^(s.legal_ep?*s.legal_ep:64));
}
static bool native_less(const RepetitionSignature& a,const RepetitionSignature& b){
    return std::tuple{native_squares(a),a.side,a.castling,a.legal_ep}<
           std::tuple{native_squares(b),b.side,b.castling,b.legal_ep};
}
static void verify_fen(const ChessCore& c,const Position& p){
    const auto fen=c.export_fen(p);const auto snap=c.snapshot(p);
    require(snap->schema_version()==position_snapshot_schema_version,"source schema");
    const std::string_view names="PNBRQKpnbrqk";
    int rank=7,file=0;
    auto expected=empty_piece_squares();
    for(char ch:fen){
        if(ch==' ')break;
        if(ch=='/'){--rank;file=0;}
        else if(ch>='1'&&ch<='8')file+=ch-'0';
        else {const auto idx=names.find(ch);require(idx!=std::string_view::npos,"unknown FEN code");
              expected.at(static_cast<std::size_t>(8*rank+file++))=static_cast<PieceCode>(idx);}
    }
    require(std::equal(expected.begin(),expected.end(),snap->squares().begin()),"FEN vs memcpy squares");
    const auto signature=repetition_signature(c,p);
    require(signature.pieces==expected,"repetition placement");
    require(d::signature_hash(signature)==native_hash(signature),"real native hash");
}
int main(){try{
    RepetitionSignature empty;
    require(std::all_of(empty.pieces.begin(),empty.pieces.end(),[](auto p){return p==PieceCode::empty;}),"default empty repetition");
    for(unsigned i=0;i<13;++i)for(unsigned j=0;j<13;++j){
        RepetitionSignature a,b;a.pieces[7]=static_cast<PieceCode>(i);b.pieces[7]=static_cast<PieceCode>(j);
        require(d::SignatureLess{}(a,b)==(i<j),"all native code pairs");
        require((a==b)==(i==j),"exact equality");
    }
    std::mt19937 rng(0x5111c);
    for(unsigned i=0;i<5000;++i){RepetitionSignature a,b;
        for(auto& p:a.pieces)p=static_cast<PieceCode>(rng()%13U);
        b=a;
        if(i%4!=0)b.pieces[rng()%64U]=static_cast<PieceCode>(rng()%13U);
        b.side=(i%2)?Side::white:Side::black;b.castling[i%4]=true;
        if(i%3==0)b.legal_ep=static_cast<std::uint8_t>(rng()%64U);
        require(d::signature_hash(a)==native_hash(a)&&d::signature_hash(b)==native_hash(b),"native hash");
        require(d::SignatureLess{}(a,b)==native_less(a,b)&&d::SignatureLess{}(b,a)==native_less(b,a),"native order");
    }
    auto c=create_chess_core();
    for(const char* fen:{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "7k/8/8/3pP3/8/8/8/7K w - d6 0 1",
            "1r5k/P7/8/8/8/8/8/7K w - - 0 1", "7k/8/8/8/8/8/p7/7K b - - 0 1"}){
        auto p=c->load_fen(fen);verify_fen(*c,*p);
        for(const auto& m:c->legal_moves(*p)){auto u=c->make_move(*p,*m);verify_fen(*c,*p);c->undo_move(*p,*u);verify_fen(*c,*p);}
    }
    for(int game=0;game<16;++game){auto p=c->initial_position();
        for(int ply=0;ply<80;++ply){verify_fen(*c,*p);auto legal=c->legal_moves(*p);if(legal.empty())break;
            auto u=c->make_move(*p,*legal[rng()%legal.size()]);(void)u;}
    }
    std::cout<<"S111D_ENCODING_HASH_ORDER_PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
