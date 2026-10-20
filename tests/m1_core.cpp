#include "chess_lab/chess_core.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <set>
#include <array>
#include <algorithm>

using namespace chess_lab;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void rejects(const std::function<void()>& f) {
    try { f(); } catch(const std::invalid_argument&) { return; }
    throw std::runtime_error("Expected invalid_argument");
}
bool equal(const PositionSnapshot& a,const PositionSnapshot& b) {
    return std::equal(a.squares().begin(),a.squares().end(),b.squares().begin()) &&
        a.side_to_move()==b.side_to_move() && a.en_passant_square()==b.en_passant_square() &&
        a.halfmove_clock()==b.halfmove_clock() && a.fullmove_number()==b.fullmove_number() &&
        a.schema_version()==b.schema_version() &&
        a.can_castle_kingside(Side::white)==b.can_castle_kingside(Side::white) &&
        a.can_castle_kingside(Side::black)==b.can_castle_kingside(Side::black) &&
        a.can_castle_queenside(Side::white)==b.can_castle_queenside(Side::white) &&
        a.can_castle_queenside(Side::black)==b.can_castle_queenside(Side::black);
}
std::uint64_t perft(ChessCore& c,Position& p,int depth) {
    if (!depth) return 1;
    auto fen=c.export_fen(p); auto key=c.position_key(p); auto rep=c.repetition_key(p);
    auto snap=c.snapshot(p);
    std::uint64_t count=0;
    for (auto& m:c.legal_moves(p)) {
        auto u=c.make_move(p,*m); count+=perft(c,p,depth-1); c.undo_move(p,*u);
        check(c.export_fen(p)==fen && c.position_key(p)==key && c.repetition_key(p)==rep,
              "Perft undo FEN/key restoration");
        check(equal(*snap,*c.snapshot(p)),"Perft undo snapshot restoration");
    }
    return count;
}
bool has(ChessCore& c,Position& p,const std::string& code) {
    for(auto& m:c.legal_moves(p)) if(c.encode_move(p,*m)==code) return true;
    return false;
}
void play(ChessCore& c,Position& p,std::string_view move) {
    auto m=c.decode_move(p,move); auto u=c.make_move(p,*m);
}
struct ForeignPosition:Position {};
struct ForeignMove:Move {};
struct ForeignUndo:UndoToken {};
int main() {
try {
    auto core=create_chess_core(); auto& c=*core;
    const char* initial="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    struct Vector { const char* name; const char* fen; std::array<std::uint64_t,4> counts; };
    // Chess Programming Wiki Perft Results, standard positions 1 through 6.
    const Vector vectors[]={
        {"initial",initial,{20,400,8902,197281}},
        {"kiwipete","r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",{48,2039,97862,0}},
        {"rook-pawn EP","8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",{14,191,2812,43238}},
        {"promotions/castling","r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",{6,264,9467,0}},
        {"promotion/check","rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",{44,1486,62379,0}},
        {"middlegame","r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",{46,2079,89890,0}}
    };
    for (const auto& v:vectors) {
        auto p=c.load_fen(v.fen);
        check(c.export_fen(*p)==v.fen,"FEN round trip");
        for(int d=1;d<=4;++d) if(v.counts[d-1]) {
            auto n=perft(c,*p,d);
            std::cout<<v.name<<" depth "<<d<<": "<<n<<'\n';
            check(n==v.counts[d-1],"Perft mismatch");
        }
    }
    auto p=c.initial_position(); auto s=c.snapshot(*p);
    check(c.export_fen(*p)==initial && s->schema_version()==1,"Initial/schema");
    check(s->squares()[0]==PieceCode::white_rook && s->squares()[4]==PieceCode::white_king &&
          s->squares()[60]==PieceCode::black_king && s->squares()[63]==PieceCode::black_rook,"Piece codes");
    auto original=c.position_key(*p);
    play(c,*p,"e2e4");
    check(c.export_fen(*p)=="rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1","Double push metadata");
    check(s->squares()[12]==PieceCode::white_pawn && !s->en_passant_square(),"Detached snapshot");
    auto noep=c.load_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    check(c.position_key(*p)==c.position_key(*noep),"Nonlegal EP excluded from identity");
    c.reset(*p,initial);
    for(auto code:{"g1f3","g8f6","f3g1","f6g8"}) play(c,*p,code);
    check(c.position_key(*p)==original && c.snapshot(*p)->halfmove_clock()==4,"History separate from identity");
    auto copy=c.clone(*p); check(c.export_fen(*copy)==c.export_fen(*p),"Clone");
    c.reset(*copy,initial); check(c.snapshot(*p)->halfmove_clock()==4,"Clone independent");

    auto castle=c.load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    check(has(c,*castle,"e1g1") && has(c,*castle,"e1c1"),"Castling generation");
    play(c,*castle,"e1g1");
    auto cs=c.snapshot(*castle);
    check(cs->squares()[6]==PieceCode::white_king && cs->squares()[5]==PieceCode::white_rook &&
          !cs->can_castle_kingside(Side::white) && !cs->can_castle_queenside(Side::white),"Castling result");
    auto blackcastle=c.load_fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    check(has(c,*blackcastle,"e8g8") && has(c,*blackcastle,"e8c8"),"Black castling");
    play(c,*blackcastle,"e8c8");
    check(c.snapshot(*blackcastle)->squares()[58]==PieceCode::black_king &&
          c.snapshot(*blackcastle)->squares()[59]==PieceCode::black_rook,"Black castling result");
    auto checkedcastle=c.load_fen("r3k2r/8/8/8/8/4r3/8/R3K2R w KQkq - 0 1");
    check(!has(c,*checkedcastle,"e1g1") && !has(c,*checkedcastle,"e1c1"),"No castling in check");
    auto attacked=c.load_fen("r3k2r/8/8/8/8/5r2/8/R3K2R w KQkq - 0 1");
    check(!has(c,*attacked,"e1g1") && has(c,*attacked,"e1c1"),"Castle through check");
    auto rights=c.load_fen("r3k2r/8/8/8/8/8/8/R3K2R w Qkq - 0 1");
    auto allrights=c.load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    check(c.position_key(*rights)!=c.position_key(*allrights),"Rights identity");
    play(c,*allrights,"a1a8");
    check(!c.snapshot(*allrights)->can_castle_queenside(Side::black),"Captured rook rights");

    auto ep=c.load_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    auto epnone=c.load_fen("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 2");
    check(c.repetition_key(*ep)!=c.repetition_key(*epnone) && has(c,*ep,"e5d6"),"Legal EP identity");
    play(c,*ep,"e5d6");
    check(c.snapshot(*ep)->squares()[35]==PieceCode::empty &&
          c.snapshot(*ep)->squares()[43]==PieceCode::white_pawn,"EP capture");
    auto pinned=c.load_fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
    auto pinnednone=c.load_fen("k3r3/8/8/3pP3/8/8/8/4K3 w - - 0 2");
    check(!has(c,*pinned,"e5d6") && c.repetition_key(*pinned)==c.repetition_key(*pinnednone),"Pinned EP");
    for (auto side:{"w","b"}) {
        auto promo=c.load_fen(std::string(side)=="w"?"4k3/P7/8/8/8/8/8/4K3 w - - 0 1":
                                                       "4k3/8/8/8/8/8/p7/4K3 b - - 0 1");
        for(char suffix:std::string("qrbn")) {
            auto code=(std::string(side)=="w"?"a7a8":"a2a1")+std::string(1,suffix);
            auto m=c.decode_move(*promo,code); auto before=c.export_fen(*promo);
            auto u=c.make_move(*promo,*m);
            auto sq=std::string(side)=="w"?56:0;
            check(c.snapshot(*promo)->squares()[sq]!=PieceCode::empty,"Promotion placement");
            c.undo_move(*promo,*u); check(c.export_fen(*promo)==before,"Promotion undo");
        }
    }
    auto mate=c.load_fen("7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    check(c.in_check(*mate) && c.status(*mate)==PositionStatus::checkmate,"Checkmate");
    auto stale=c.load_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    check(!c.in_check(*stale) && c.status(*stale)==PositionStatus::stalemate,"Stalemate");
    auto evasion=c.load_fen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
    check(c.in_check(*evasion) && c.status(*evasion)==PositionStatus::ongoing,"Check with evasions");
    auto dead=c.load_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    check(c.known_dead_position(*dead) && !c.known_dead_position(*p),"Known dead material");

    c.reset(*p,initial);
    auto m=c.decode_move(*p,"e2e4"); auto u=c.make_move(*p,*m);
    auto n=c.decode_move(*p,"e7e5"); auto v=c.make_move(*p,*n);
    rejects([&]{c.undo_move(*p,*u);});
    auto cloned=c.clone(*p); rejects([&]{c.undo_move(*cloned,*v);});
    c.undo_move(*p,*v); c.undo_move(*p,*u);
    rejects([&]{c.undo_move(*p,*u);});
    check(c.export_fen(*p)==initial,"Nested undo");
    auto t=c.make_move(*p,*m); c.reset(*p,initial);
    rejects([&]{c.undo_move(*p,*t);});
    rejects([&]{auto unused=c.decode_move(*p,"e2e5");});
    rejects([&]{auto unused=c.decode_move(*p,"E2E4");});
    ForeignPosition fp; ForeignMove fm; ForeignUndo fu;
    rejects([&]{auto unused=c.export_fen(fp);});
    rejects([&]{auto unused=c.make_move(*p,fm);});
    rejects([&]{c.undo_move(*p,fu);});
    for(auto fen:{"", "8/8/8/8/8/8/8/8 w - - 0 1",
         "9/8/8/8/8/8/8/K6k w - - 0 1", "4k3/8/8/8/8/8/8/4K3 x - - 0 1",
         "4k3/8/8/8/8/8/8/4K3 w K - 0 1", "4k3/8/8/8/8/8/8/4K3 w - e6 0 1",
         "4k3/8/8/8/8/8/8/4K3 w - - -1 1", "4k3/8/8/8/8/8/8/4K3 w - - 0 0",
         "4k3/8/8/8/8/8/8/4K3 w - - 0 1 junk"}) {
        rejects([&]{auto unused=c.load_fen(fen);});
        rejects([&]{c.reset(*p,fen);}); check(c.export_fen(*p)==initial,"Reset atomicity");
    }
    auto turn=c.load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
    check(c.position_key(*turn)!=original,"Turn identity");
    // A move object is revalidated at application time.
    play(c,*p,"e2e4");
    rejects([&]{auto unused=c.make_move(*p,*m);});
    auto promoCapture=c.load_fen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    for(char suffix:std::string("qrbn")) check(has(c,*promoCapture,"a7b8"+std::string(1,suffix)),"Capture promotions");
    // A consumed token must not accept a new branch at the same depth.
    c.reset(*p,initial);
    auto branch=c.make_move(*p,*m);
    c.undo_move(*p,*branch);
    auto branch2=c.make_move(*p,*m);
    rejects([&]{c.undo_move(*p,*branch);});
    c.undo_move(*p,*branch2);
    auto clocks=c.load_fen("4k3/8/8/8/8/8/8/4K2R w - - 300 70000");
    auto cm=c.decode_move(*clocks,"h1h2"); auto cu=c.make_move(*clocks,*cm);
    check(c.snapshot(*clocks)->halfmove_clock()==301 && c.snapshot(*clocks)->fullmove_number()==70000,"Wide clocks");
    c.undo_move(*clocks,*cu);
    check(c.export_fen(*clocks)=="4k3/8/8/8/8/8/8/4K2R w - - 300 70000","Wide clock restoration");
    std::cout<<"M1 correctness checks passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
