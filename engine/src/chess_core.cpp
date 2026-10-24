#include "chess_lab/chess_core.hpp"
#include "chess.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace chess_lab {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("Invalid chess object, FEN, move or undo order"); }
template<class T, class B> const T& cast(const B& b) {
    auto p = dynamic_cast<const T*>(&b);
    if (!p) invalid();
    return *p;
}
template<class T, class B> T& cast(B& b) {
    auto p = dynamic_cast<T*>(&b);
    if (!p) invalid();
    return *p;
}
struct Metadata {
    std::optional<std::uint8_t> ep;
    std::uint32_t half = 0, full = 1;
};
struct Pos final : Position {
    chess::Board board;
    Metadata meta;
    // Unique lifetime identity prevents pointer reuse accepting old tokens.
    std::shared_ptr<const int> identity = std::make_shared<const int>(0);
    std::uint64_t serial = 0, top = 0;
};
struct Mv final : Move {
    chess::Move value;
    // A legal generator-issued certificate is valid only for this position
    // lifetime and undo-stack frame. A strict undo restores that exact board.
    std::shared_ptr<const int> owner;
    std::uint64_t top = 0;
    explicit Mv(chess::Move m) : value(m) {}
    Mv(chess::Move m, const Pos& p) : value(m), owner(p.identity), top(p.top) {}
};
bool certified(const Pos& p, const Mv& m) {
    return m.owner && m.owner == p.identity && m.top == p.top;
}
struct Undo final : UndoToken {
    std::shared_ptr<const int> identity;
    chess::Move move;
    Metadata meta;
    std::uint64_t id, parent;
    bool consumed = false;
};
struct Snap final : PositionSnapshot {
    std::array<PieceCode, 64> pieces{};
    Side side{};
    std::array<bool, 4> rights{};
    Metadata meta;
    std::span<const PieceCode, 64> squares() const noexcept override { return pieces; }
    Side side_to_move() const noexcept override { return side; }
    bool can_castle_kingside(Side s) const noexcept override { return rights[s == Side::white ? 0 : 2]; }
    bool can_castle_queenside(Side s) const noexcept override { return rights[s == Side::white ? 1 : 3]; }
    std::optional<std::uint8_t> en_passant_square() const noexcept override { return meta.ep; }
    std::uint32_t halfmove_clock() const noexcept override { return meta.half; }
    std::uint32_t fullmove_number() const noexcept override { return meta.full; }
    std::uint32_t schema_version() const noexcept override { return 1; }
};
std::uint32_t number(const std::string& s, bool positive) {
    std::uint32_t n;
    auto [end, ec] = std::from_chars(s.data(), s.data()+s.size(), n);
    if (ec != std::errc{} || end != s.data()+s.size() || (positive && n == 0)) invalid();
    return n;
}
// Validate before calling upstream's permissive parser (which assumes kings).
std::unique_ptr<Pos> parse(std::string_view fen) {
    std::istringstream input{std::string(fen)};
    std::array<std::string, 6> f;
    for (auto& field : f) if (!(input >> field)) invalid();
    std::string extra;
    if (input >> extra) invalid();
    std::array<char,64> squares{};
    int rank=7, file=0, wk=0, bk=0;
    for (char c : f[0]) {
        if (c == '/') { if (file != 8 || rank == 0) invalid(); --rank; file=0; }
        else if (c >= '1' && c <= '8') { file += c-'0'; if (file>8) invalid(); }
        else {
            if (std::string_view("PNBRQKpnbrqk").find(c) == std::string_view::npos || file>=8) invalid();
            if ((c=='P'||c=='p') && (rank==0||rank==7)) invalid();
            squares[rank*8+file++] = c;
            wk += c=='K'; bk += c=='k';
        }
    }
    if (rank!=0 || file!=8 || wk!=1 || bk!=1 || (f[1]!="w" && f[1]!="b")) invalid();
    std::string seen;
    if (f[2]!="-") for (char c:f[2]) {
        auto index=std::string_view("KQkq").find(c);
        if (index==std::string_view::npos || seen.find(c)!=std::string::npos) invalid();
        seen+=c;
        const int rook[]={7,0,63,56};
        if (squares[index<2?4:60] != (index<2?'K':'k') ||
            squares[rook[index]] != (index<2?'R':'r')) invalid();
    }
    auto p=std::make_unique<Pos>();
    p->meta.half=number(f[4],false); p->meta.full=number(f[5],true);
    if (f[3]!="-") {
        if (f[3].size()!=2 || f[3][0]<'a'||f[3][0]>'h' || f[3][1]!=(f[1]=="w"?'6':'3')) invalid();
        int ep=(f[3][1]-'1')*8+f[3][0]-'a';
        int pawn=ep+(f[1]=="w"?-8:8), origin=ep+(f[1]=="w"?8:-8);
        if (squares[ep] || squares[origin] || squares[pawn]!=(f[1]=="w"?'p':'P')) invalid();
        if (p->meta.half!=0) invalid();
        p->meta.ep=static_cast<std::uint8_t>(ep);
    }
    // Upstream counters are narrow; own metadata is authoritative.
    std::string normalized=f[0]+" "+f[1]+" "+f[2]+" "+f[3]+" 0 1";
    if (!p->board.setFen(normalized)) invalid();
    auto white=p->board.kingSq(chess::Color::WHITE);
    auto black=p->board.kingSq(chess::Color::BLACK);
    if (std::abs(white.index()/8-black.index()/8)<=1 &&
        std::abs(white.index()%8-black.index()%8)<=1) invalid();
    // The side that just moved cannot have left its own king attacked.
    if (p->board.isAttacked(p->board.kingSq(~p->board.sideToMove()), p->board.sideToMove())) invalid();
    return p;
}
chess::Movelist moves(const Pos& p) {
    chess::Movelist result;
    chess::movegen::legalmoves(result,p.board);
    return result;
}
void require_legal(const Pos& p, chess::Move m) {
    auto list=moves(p);
    for (auto legal:list) if (legal==m) return;
    invalid();
}
class Core final : public ChessCore {
public:
    std::unique_ptr<Position> initial_position() const override { return load_fen(chess::constants::STARTPOS); }
    std::unique_ptr<Position> load_fen(std::string_view fen) const override { return parse(fen); }
    std::string export_fen(const Position& position) const override {
        const auto& p=cast<Pos>(position);
        std::istringstream in(p.board.getFen());
        std::string board,side,rights,ep;
        in>>board>>side>>rights>>ep;
        ep=p.meta.ep ? std::string{char('a'+*p.meta.ep%8),char('1'+*p.meta.ep/8)} : "-";
        return board+" "+side+" "+rights+" "+ep+" "+std::to_string(p.meta.half)+" "+std::to_string(p.meta.full);
    }
    std::unique_ptr<Position> clone(const Position& position) const override {
        const auto& p=cast<Pos>(position);
        auto result=std::make_unique<Pos>();
        result->board=p.board; result->meta=p.meta;
        return result;
    }
    void reset(Position& position,std::string_view fen) const override {
        auto& p=cast<Pos>(position); auto fresh=parse(fen);
        p.board=std::move(fresh->board); p.meta=fresh->meta;
        p.identity=std::move(fresh->identity); p.serial=0; p.top=0;
    }
    std::vector<std::unique_ptr<Move>> legal_moves(const Position& position) const override {
        std::vector<std::unique_ptr<Move>> result;
        const auto& p=cast<Pos>(position);
        auto list=moves(p);
        for (auto m:list) result.push_back(std::make_unique<Mv>(m, p));
        return result;
    }
    std::unique_ptr<UndoToken> make_move(Position& position,const Move& move) const override {
        auto& p=cast<Pos>(position); auto m=cast<Mv>(move).value;
        if (!certified(p,cast<Mv>(move))) require_legal(p,m);
        bool pawn=p.board.at<chess::PieceType>(m.from())==chess::PieceType::PAWN;
        bool zero=pawn||p.board.isCapture(m);
        bool black=p.board.sideToMove()==chess::Color::BLACK;
        if ((!zero && p.meta.half==UINT32_MAX) || (black && p.meta.full==UINT32_MAX) || p.serial==UINT64_MAX)
            throw std::overflow_error("Chess state counter exhausted");
        auto u=std::make_unique<Undo>();
        u->identity=p.identity; u->move=m; u->meta=p.meta; u->id=p.serial+1; u->parent=p.top;
        p.board.makeMove(m);
        p.meta.ep.reset();
        if (pawn && std::abs(m.from().index()-m.to().index())==16)
            p.meta.ep=static_cast<std::uint8_t>((m.from().index()+m.to().index())/2);
        p.meta.half=zero?0:p.meta.half+1;
        p.meta.full+=black;
        p.serial=u->id; p.top=u->id;
        return u;
    }
    void undo_move(Position& position,UndoToken& token) const override {
        auto& p=cast<Pos>(position); auto& u=cast<Undo>(token);
        if (u.consumed || u.identity!=p.identity || p.top!=u.id) invalid();
        p.board.unmakeMove(u.move); p.meta=u.meta; p.top=u.parent; u.consumed=true;
    }
    std::string encode_move(const Position& position,const Move& move) const override {
        const auto& p=cast<Pos>(position); const auto& supplied=cast<Mv>(move);
        if (!certified(p,supplied)) require_legal(p,supplied.value);
        return chess::uci::moveToUci(supplied.value);
    }
    std::unique_ptr<Move> decode_move(const Position& position,std::string_view coordinate) const override {
        for (auto m:moves(cast<Pos>(position)))
            if (chess::uci::moveToUci(m)==coordinate) return std::make_unique<Mv>(m);
        invalid();
    }
    bool in_check(const Position& p) const override { return cast<Pos>(p).board.inCheck(); }
    PositionStatus status(const Position& position) const override {
        const auto& p=cast<Pos>(position);
        if (!moves(p).empty()) return PositionStatus::ongoing;
        return p.board.inCheck()?PositionStatus::checkmate:PositionStatus::stalemate;
    }
    bool known_dead_position(const Position& p) const override { return cast<Pos>(p).board.isInsufficientMaterial(); }
    std::uint64_t position_key(const Position& p) const override { return cast<Pos>(p).board.hash(); }
    std::uint64_t repetition_key(const Position& p) const override { return position_key(p); }
    std::unique_ptr<const PositionSnapshot> snapshot(const Position& position) const override {
        const auto& p=cast<Pos>(position); auto s=std::make_unique<Snap>();
        for (int i=0;i<64;++i) {
            auto piece=p.board.at(chess::Square(i));
            s->pieces[i]=piece==chess::Piece::NONE?PieceCode::empty:static_cast<PieceCode>(int(piece)+1);
        }
        s->side=p.board.sideToMove()==chess::Color::WHITE?Side::white:Side::black;
        auto cr=p.board.castlingRights();
        using CS=chess::Board::CastlingRights::Side;
        s->rights={cr.has(chess::Color::WHITE,CS::KING_SIDE),cr.has(chess::Color::WHITE,CS::QUEEN_SIDE),
                   cr.has(chess::Color::BLACK,CS::KING_SIDE),cr.has(chess::Color::BLACK,CS::QUEEN_SIDE)};
        s->meta=p.meta;
        return s;
    }
};
}
std::unique_ptr<ChessCore> create_chess_core() { return std::make_unique<Core>(); }
// Search-internal adapter dispatch; synthetic ChessCore fixtures retain their
// existing status and clock-transition semantics.
bool native_chess_core(const ChessCore& c) noexcept { return dynamic_cast<const Core*>(&c)!=nullptr; }
// Private search capability: complete generation without boxing unused moves.
// Neither native move values nor certificates escape the current position frame.
std::size_t native_legal_move_count(const ChessCore& c, const Position& p) {
    (void)cast<Core>(c);
    return moves(cast<Pos>(p)).size();
}
}
