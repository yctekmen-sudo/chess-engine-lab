#include <chess_lab/search.hpp>
#include "native_move_batch.hpp"
#include "transposition_table_internal.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace chess_lab;
namespace d=chess_lab::detail;
static unsigned positions=0,ep_legal=0,ep_ignored=0,normalized=0;
static void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static std::string without_ep(const std::string& fen){
    std::istringstream input(fen);std::string a,b,c,ep,half,full;
    require(bool(input>>a>>b>>c>>ep>>half>>full),"FEN fields");
    return a+' '+b+' '+c+" - "+half+' '+full;
}
static std::uint64_t verify(const ChessCore& c,const Position& p){
    const auto sig=repetition_signature(c,p);
    const auto k=d::native_tt_repetition_key(c,p,sig.legal_ep);
    // The FEN parser independently removes illegal native EP; it computes
    // a fresh hash rather than relying on the live board's incremental state.
    const auto fen=c.export_fen(p);
    auto reload=c.load_fen(fen);
    require(repetition_signature(c,*reload)==sig,"reload repetition differs");
    require(k==c.position_key(*reload),"native normalized key differs from fresh parsed board");
    require(k==d::native_tt_repetition_key(c,*reload,sig.legal_ep),"equal signature, different key");
    const auto raw=c.snapshot(p)->en_passant_square();
    if(raw){
        auto noep=c.load_fen(without_ep(fen));auto no_sig=repetition_signature(c,*noep);
        const auto no_key=d::native_tt_repetition_key(c,*noep,no_sig.legal_ep);
        if(sig.legal_ep){++ep_legal;require(no_sig!=sig && no_key!=k,"legal EP lost from key");}
        else {++ep_ignored;require(no_sig==sig && no_key==k,"ineffective EP still affects key");}
    }
    if(k!=c.position_key(p))++normalized;
    ++positions;return k;
}
static void ep_case(const ChessCore& c,const char* fen,const char* move,bool legal,bool must_normalize){
    auto p=c.load_fen(fen);const auto before=verify(c,*p);
    auto m=c.decode_move(*p,move);auto u=c.make_move(*p,*m);
    auto sig=repetition_signature(c,*p);require(bool(sig.legal_ep)==legal,"EP fixture legality");
    const auto after=verify(c,*p);
    if(must_normalize) require(after!=c.position_key(*p),"pinned EP fixture did not exercise normalization");
    c.undo_move(*p,*u);require(verify(c,*p)==before,"EP undo key mismatch");
}
int main(){try{
    auto c=create_chess_core();
    ep_case(*c,"k3r3/3p4/8/4P3/8/8/8/4K3 b - - 0 1","d7d5",false,true);
    ep_case(*c,"7k/3p4/8/r3P2K/8/8/8/8 b - - 0 1","d7d5",false,true);
    ep_case(*c,"4k3/8/8/8/4p3/8/3P4/K3R3 w - - 0 1","d2d4",false,true);
    ep_case(*c,"7k/3p4/8/4P3/8/8/8/7K b - - 0 1","d7d5",true,false);
    ep_case(*c,"7k/8/8/8/4p3/8/3P4/7K w - - 0 1","d2d4",true,false);
    ep_case(*c,"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1","e2e4",false,false);
    for(const char* fen:{"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
          "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1","7k/8/8/3pP3/8/8/8/7K w - d6 0 1",
          "1r5k/P7/8/8/8/8/8/7K w - - 0 1","7k/8/8/8/8/8/p7/7K b - - 0 1"}){
        auto p=c->load_fen(fen);const auto before=verify(*c,*p);
        for(const auto& m:c->legal_moves(*p)){auto u=c->make_move(*p,*m);verify(*c,*p);c->undo_move(*p,*u);require(verify(*c,*p)==before,"special move undo");}
    }
    auto a=c->load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    auto b=c->load_fen("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1");
    auto side=c->load_fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    auto clocks=c->load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 99 50");
    require(verify(*c,*a)!=verify(*c,*b),"castling key component");
    require(verify(*c,*a)!=verify(*c,*side),"side key component");
    require(verify(*c,*a)==verify(*c,*clocks),"clocks must not alter repetition key");
    // Force all TT keys into one bucket, including their stored hash. Exact
    // signature comparison must prevent an unrelated native position hit.
    TranspositionTable table(256*1024); d::TTAccess::force_collisions(table,true);
    TranspositionStatistics stats;
    auto sa=repetition_signature(*c,*a),sb=repetition_signature(*c,*b);
    auto aa=c->snapshot(*a),bb=c->snapshot(*b);
    std::optional<std::uint64_t> ka=verify(*c,*a),kb=verify(*c,*b);
    const std::string fingerprint(64,'a');
    { d::TTSession session({&table,EvaluatorCacheCompatibility::history_independent_position,1},
                           fingerprint,true,stats,{});
      session.start({sa});
      session.store(sa,2,SearchScore::finite(17),d::TTBound::exact,ChessDecision{DecisionKind::play,"e1g1"},0,*aa,&ka);
      require(session.probe(sa,2,0,*aa,&ka).score==SearchScore::finite(17),"native key store/probe");
      require(!session.probe(sb,2,0,*bb,&kb).score,"collision yielded incompatible score");
    }
    std::mt19937 rng(0x5111e);
    for(int game=0;game<32;++game){auto p=c->initial_position();auto before=verify(*c,*p);
        std::vector<std::unique_ptr<UndoToken>> undo;std::vector<std::uint64_t> keys{before};
        for(int ply=0;ply<80;++ply){auto legal=c->legal_moves(*p);if(legal.empty())break;
            undo.push_back(c->make_move(*p,*legal[rng()%legal.size()]));keys.push_back(verify(*c,*p));}
        while(!undo.empty()){c->undo_move(*p,*undo.back());undo.pop_back();keys.pop_back();require(verify(*c,*p)==keys.back(),"random reverse unwind key");}
    }
    require(ep_legal>0 && ep_ignored>0 && normalized>=3,"EP normalization coverage missing");
    std::cout<<"S111E_NATIVE_KEY_PASS positions="<<positions<<" legal_ep="<<ep_legal<<" ignored_ep="<<ep_ignored<<" corrected_key="<<normalized<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
