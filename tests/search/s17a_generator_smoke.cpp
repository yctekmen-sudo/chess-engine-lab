// S1.7a isolated private test: exact legal generator membership and order.
// Compile against the cloned, locally overlaid pinned chess.hpp only.
#include "chess.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

using chess::Move;
using chess::Movelist;
using chess::Board;
using MT = chess::movegen::MoveGenType;

static void check(const std::string &fen, bool requireQuietPromo=false,
                  bool requireCapturePromo=false, bool requireEp=false) {
    Board b(fen);
    Movelist all, captures, tactical;
    chess::movegen::legalmoves(all,b);
    chess::movegen::legalmoves<MT::CAPTURE>(captures,b);
    chess::movegen::legalmoves<MT::TACTICAL>(tactical,b);
    std::vector<std::uint16_t> wantC, wantT, gotC, gotT;
    unsigned quietPromo=0,capturePromo=0,ep=0;
    for(auto m:all){
        bool c=b.isCapture(m), p=m.typeOf()==Move::PROMOTION;
        if(c)wantC.push_back(m.move());
        if(c||p)wantT.push_back(m.move());
        if(p&&!c)++quietPromo;
        if(p&&c)++capturePromo;
        if(m.typeOf()==Move::ENPASSANT)++ep;
    }
    for(auto m:captures)gotC.push_back(m.move());
    for(auto m:tactical)gotT.push_back(m.move());
    if(wantC!=gotC||wantT!=gotT) {
        throw std::runtime_error("Generator membership/order mismatch: "+fen);
    }
    if(requireQuietPromo && !quietPromo)throw std::runtime_error("No quiet promotions: "+fen);
    if(requireCapturePromo && !capturePromo)throw std::runtime_error("No capture promotions: "+fen);
    if(requireEp && !ep)throw std::runtime_error("No en passant: "+fen);
}
int main() {
    try {
        check("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        check("7k/P7/8/8/8/8/8/7K w - - 0 1",true);
        check("1r5k/P7/8/8/8/8/8/7K w - - 0 1",true,true);
        check("7k/8/8/8/8/8/p7/7K b - - 0 1",true);
        check("7k/8/8/3pP3/8/8/8/7K w - d6 0 1",false,false,true);
        check("7k/8/8/8/8/8/8/5r1K w - - 0 1");
        std::cout<<"S17A_GENERATOR_EQUIVALENCE_PASS\n";
        return 0;
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}
}
