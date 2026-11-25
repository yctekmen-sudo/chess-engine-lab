#include "chess_lab/chess_core.hpp"
#include "native_move_batch.hpp"
#include "evaluation/native_eval_snapshot.hpp"
#include "native_undo_fastpath.hpp"
#include "chess.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <bit>
#include <charconv>
#include <limits>
#include <sstream>
#include <typeinfo>
#include <type_traits>
#include <stdexcept>

namespace chess_lab {
namespace {
[[noreturn]] void invalid() { throw std::invalid_argument("Invalid chess object, FEN, move or undo order"); }
// Exact-type dispatch is equivalent to dynamic_cast for final native types.
// Synthetic/mismatched objects must still be rejected by invalid().
template<class T, class B> const T& cast(const B& b) {
    static_assert(std::is_final_v<T>, "C2 exact-type dispatch requires final target");
    if (typeid(b) != typeid(T)) invalid();
    return static_cast<const T&>(b);
}
template<class T, class B> T& cast(B& b) {
    static_assert(std::is_final_v<T>, "C2 exact-type dispatch requires final target");
    if (typeid(b) != typeid(T)) invalid();
    return static_cast<T&>(b);
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
    std::array<PieceCode, 64> pieces = empty_piece_squares();
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
    std::uint32_t schema_version() const noexcept override { return position_snapshot_schema_version; }
};
// Prove the actual byte representation, not just enum ordinal equality.
static_assert(sizeof(chess::Piece)==1 && sizeof(PieceCode)==1);
static_assert(std::is_trivially_copyable_v<chess::Piece> && std::is_trivially_copyable_v<PieceCode>);
static_assert(std::has_unique_object_representations_v<chess::Piece> &&
              std::has_unique_object_representations_v<PieceCode>);
static_assert(int(chess::Piece{chess::Piece::WHITEPAWN})==int(PieceCode::white_pawn) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEPAWN})==
    std::bit_cast<std::uint8_t>(PieceCode::white_pawn));
static_assert(int(chess::Piece{chess::Piece::WHITEKNIGHT})==int(PieceCode::white_knight) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEKNIGHT})==
    std::bit_cast<std::uint8_t>(PieceCode::white_knight));
static_assert(int(chess::Piece{chess::Piece::WHITEBISHOP})==int(PieceCode::white_bishop) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEBISHOP})==
    std::bit_cast<std::uint8_t>(PieceCode::white_bishop));
static_assert(int(chess::Piece{chess::Piece::WHITEROOK})==int(PieceCode::white_rook) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEROOK})==
    std::bit_cast<std::uint8_t>(PieceCode::white_rook));
static_assert(int(chess::Piece{chess::Piece::WHITEQUEEN})==int(PieceCode::white_queen) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEQUEEN})==
    std::bit_cast<std::uint8_t>(PieceCode::white_queen));
static_assert(int(chess::Piece{chess::Piece::WHITEKING})==int(PieceCode::white_king) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::WHITEKING})==
    std::bit_cast<std::uint8_t>(PieceCode::white_king));
static_assert(int(chess::Piece{chess::Piece::BLACKPAWN})==int(PieceCode::black_pawn) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKPAWN})==
    std::bit_cast<std::uint8_t>(PieceCode::black_pawn));
static_assert(int(chess::Piece{chess::Piece::BLACKKNIGHT})==int(PieceCode::black_knight) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKKNIGHT})==
    std::bit_cast<std::uint8_t>(PieceCode::black_knight));
static_assert(int(chess::Piece{chess::Piece::BLACKBISHOP})==int(PieceCode::black_bishop) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKBISHOP})==
    std::bit_cast<std::uint8_t>(PieceCode::black_bishop));
static_assert(int(chess::Piece{chess::Piece::BLACKROOK})==int(PieceCode::black_rook) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKROOK})==
    std::bit_cast<std::uint8_t>(PieceCode::black_rook));
static_assert(int(chess::Piece{chess::Piece::BLACKQUEEN})==int(PieceCode::black_queen) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKQUEEN})==
    std::bit_cast<std::uint8_t>(PieceCode::black_queen));
static_assert(int(chess::Piece{chess::Piece::BLACKKING})==int(PieceCode::black_king) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::BLACKKING})==
    std::bit_cast<std::uint8_t>(PieceCode::black_king));
static_assert(int(chess::Piece{chess::Piece::NONE})==int(PieceCode::empty) &&
    std::bit_cast<std::uint8_t>(chess::Piece{chess::Piece::NONE})==
    std::bit_cast<std::uint8_t>(PieceCode::empty));
void fill_native_snapshot(const Pos& p, Snap& s) {
        const auto& native=p.board.celab_native_squares();
        static_assert(sizeof(native)==64 && sizeof(s.pieces)==64);
        std::memcpy(s.pieces.data(),native.data(),sizeof(s.pieces));
#if defined(CELAB_S111C_VERIFY)
        // Independent scalar reads from the original square accessor.
        for (int i=0;i<64;++i) {
            const auto piece=p.board.at(chess::Square(i));
            if (s.pieces[i]!=static_cast<PieceCode>(int(piece)))
                throw std::logic_error("S1.11c native byte copy mismatch");
        }
#endif
        s.side=p.board.sideToMove()==chess::Color::WHITE?Side::white:Side::black;
        auto cr=p.board.castlingRights();
        using CS=chess::Board::CastlingRights::Side;
        s.rights={cr.has(chess::Color::WHITE,CS::KING_SIDE),cr.has(chess::Color::WHITE,CS::QUEEN_SIDE),
                   cr.has(chess::Color::BLACK,CS::KING_SIDE),cr.has(chess::Color::BLACK,CS::QUEEN_SIDE)};
        s.meta=p.meta;
}
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

// One native board-transition implementation serves both the public UndoToken
// path and the stack-owned search path. Inspect before the fallible Undo
// allocation or any mutation; preserve the same counter/exception boundaries.
struct NativeMoveFacts {
    bool pawn, zero, black;
};
NativeMoveFacts inspect_native_transition(const Pos& p, chess::Move move) {
    const bool pawn=p.board.at<chess::PieceType>(move.from())==chess::PieceType::PAWN;
    const bool zero=pawn||p.board.isCapture(move);
    const bool black=p.board.sideToMove()==chess::Color::BLACK;
    if ((!zero && p.meta.half==UINT32_MAX) || (black && p.meta.full==UINT32_MAX) || p.serial==UINT64_MAX)
        throw std::overflow_error("Chess state counter exhausted");
    return {pawn,zero,black};
}
void commit_native_transition(Pos& p, chess::Move move, NativeMoveFacts facts,
                              std::uint64_t next_id) {
    p.board.makeMove(move);
    p.meta.ep.reset();
    if (facts.pawn && std::abs(move.from().index()-move.to().index())==16)
        p.meta.ep=static_cast<std::uint8_t>((move.from().index()+move.to().index())/2);
    p.meta.half=facts.zero?0:p.meta.half+1;
    p.meta.full+=facts.black;
    p.serial=next_id; p.top=next_id;
}
void restore_native_transition(Pos& p, chess::Move move,
                               const Metadata& saved, std::uint64_t parent) {
    p.board.unmakeMove(move);
    p.meta=saved; p.top=parent;
}

// Match the vendored non-Chess960 UCI conversion without coordinate strings.
std::string native_move_to_uci(chess::Move move) {
    const auto from = move.from();
    auto to = move.to();
    // Native castling targets the rook square; UCI targets the king destination.
    if (move.typeOf() == chess::Move::CASTLING)
        to = chess::Square(to > from ? chess::File::FILE_G : chess::File::FILE_C,
                           from.rank());
    char text[5] = {char('a' + from.index() % 8), char('1' + from.index() / 8),
                    char('a' + to.index() % 8), char('1' + to.index() / 8), 0};
    const bool promotion = move.typeOf() == chess::Move::PROMOTION;
    if (promotion) text[4] = "pnbrqk"[int(move.promotionType())];
    return std::string(text, promotion ? 5 : 4);
}
// C4.3: identical native castling/promotion normalization, written straight
// into the search-private eight-byte UCI record without an intermediate string.
// CompactUciText's remaining bytes start zeroed, retaining its raw-word
// equality and lexical-order invariants.
detail::CompactUciText native_move_to_compact_uci(chess::Move move) {
    const auto from = move.from();
    auto to = move.to();
    if (move.typeOf() == chess::Move::CASTLING)
        to = chess::Square(to > from ? chess::File::FILE_G : chess::File::FILE_C,
                           from.rank());
    detail::CompactUciText text;
    text.bytes[0] = char('a' + from.index() % 8);
    text.bytes[1] = char('1' + from.index() / 8);
    text.bytes[2] = char('a' + to.index() % 8);
    text.bytes[3] = char('1' + to.index() / 8);
    const bool promotion = move.typeOf() == chess::Move::PROMOTION;
    if (promotion) text.bytes[4] = "pnbrqk"[int(move.promotionType())];
    text.length = static_cast<std::uint8_t>(promotion ? 5 : 4);
    return text;
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
        result.reserve(list.size());
        for (auto m:list) result.push_back(std::make_unique<Mv>(m, p));
        return result;
    }
    std::unique_ptr<UndoToken> make_move(Position& position,const Move& move) const override {
        auto& p=cast<Pos>(position); const auto& supplied=cast<Mv>(move);
        const auto m=supplied.value;
        if (!certified(p,supplied)) require_legal(p,m);
        const auto facts=inspect_native_transition(p,m);
        auto u=std::make_unique<Undo>();
        u->identity=p.identity; u->move=m; u->meta=p.meta; u->id=p.serial+1; u->parent=p.top;
        commit_native_transition(p,m,facts,u->id);
        return u;
    }
    void undo_move(Position& position,UndoToken& token) const override {
        auto& p=cast<Pos>(position); auto& u=cast<Undo>(token);
        if (u.consumed || u.identity!=p.identity || p.top!=u.id) invalid();
        restore_native_transition(p,u.move,u.meta,u.parent);
        u.consumed=true;
    }
    std::string encode_move(const Position& position,const Move& move) const override {
        const auto& p=cast<Pos>(position); const auto& supplied=cast<Mv>(move);
        if (!certified(p,supplied)) require_legal(p,supplied.value);
        return native_move_to_uci(supplied.value);
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
        fill_native_snapshot(p,*s);
        return s;
    }
};
}
std::unique_ptr<ChessCore> create_chess_core() { return std::make_unique<Core>(); }
// Search-internal adapter dispatch; synthetic ChessCore fixtures retain their
// existing status and clock-transition semantics.
bool native_chess_core(const ChessCore& c) noexcept {
    static_assert(std::is_final_v<Core>, "C2 native dispatch requires final Core");
    return typeid(c) == typeid(Core);
}
// Private search capability: complete generation without boxing unused moves.
// Neither native move values nor certificates escape the current position frame.
std::size_t native_legal_move_count(const ChessCore& c, const Position& p) {
    (void)cast<Core>(c);
    return moves(cast<Pos>(p)).size();
}
// C4.2 isolated leaf experiment. Use the pinned upstream generator's
// ONE_MOVE_ONLY path. No full legal list or exact count is needed for
// certified claim-free horizon terminal classification.
bool native_has_any_legal_move(const ChessCore& c, const Position& p) {
    (void)cast<Core>(c);
    return chess::movegen::anylegalmoves(cast<Pos>(p).board);
}
namespace detail {

// Search-private fast path shares the same transition and restoration helpers
// as public Core::make_move/undo_move. Only token ownership differs.
void make_native_search_move(const ChessCore& core, Position& position,
                             const Move& move, NativeUndoFrame& frame) {
    (void)cast<Core>(core);
    auto& p=cast<Pos>(position);
    const auto& supplied=cast<Mv>(move);
    auto m=supplied.value;
    if (!certified(p,supplied)) require_legal(p,m);
    const auto facts=inspect_native_transition(p,m);
    frame.identity=p.identity; frame.native_move=m.move();
    frame.ep=p.meta.ep; frame.half=p.meta.half; frame.full=p.meta.full;
    frame.id=p.serial+1; frame.parent=p.top; frame.consumed=false;
    commit_native_transition(p,m,facts,frame.id);
}
void undo_native_search_move(const ChessCore& core, Position& position, NativeUndoFrame& frame) {
    (void)cast<Core>(core);
    auto& p=cast<Pos>(position);
    if (frame.consumed || frame.identity!=p.identity || p.top!=frame.id) invalid();
    restore_native_transition(p,chess::Move(frame.native_move),
                              Metadata{frame.ep,frame.half,frame.full},frame.parent);
    frame.consumed=true;
}

// B3a: lazy canonical hint check for count-only numeric-TT horizon nodes.
// The full legal-move generator remains the authoritative membership test.
// No heap-owned Move objects, native batch or sorted text list is constructed.
bool native_has_legal_uci_hint(const ChessCore& core, const Position& position,
                               std::string_view hint) {
    (void)cast<Core>(core);
    const auto& p = cast<Pos>(position);
    for (auto m : moves(p))
        if (native_move_to_uci(m) == hint) return true;
    return false;
}

// Verify the raw TT hint against the current position's legal move list.
// Never interpret this raw identity as an executable move certificate.
bool native_has_legal_move_identity(const ChessCore& core, const Position& position,
                                    std::uint16_t identity) {
    (void)cast<Core>(core);
    const auto& p=cast<Pos>(position);
    for (auto m:moves(p)) if (m.move()==identity) return true;
    return false;
}
// S1.11e: callers supply the exact repetition EP of this live frame.
std::uint64_t native_tt_repetition_key(const ChessCore& core,const Position& position,
                                      std::optional<std::uint8_t> legal_ep) {
    (void)cast<Core>(core);
    const auto& p=cast<Pos>(position);
    const auto ep=legal_ep ? chess::Square(*legal_ep) : chess::Square(chess::Square::NO_SQ);
#if defined(CELAB_S111E_VERIFY)
    if(p.board.hash()!=p.board.zobrist())
        throw std::logic_error("S1.11e incremental Zobrist mismatch");
    // Independent complete legal-list oracle when raw EP exists. No production
    // regeneration, and no change to the make/unmake or public key contracts.
    std::optional<std::uint8_t> expected;
    if(p.meta.ep) for(auto m:moves(p))
        if(m.typeOf()==chess::Move::ENPASSANT) expected=p.meta.ep;
    if(expected!=legal_ep)
        throw std::logic_error("S1.11e effective EP mismatch");
    // Rebuild through the existing validated FEN parser. It normalizes EP on
    // load independently of the normal (pseudo-legal) makeMove path.
    if(p.meta.ep) {
        auto reload=core.load_fen(core.export_fen(position));
        if(p.board.celab_repetition_hash(ep)!=core.position_key(*reload))
            throw std::logic_error("S1.11e normalized key mismatch");
    }
#endif
    return p.board.celab_repetition_hash(ep);
}
bool native_has_legal_en_passant(const ChessCore& core, const Position& position) {
    (void)cast<Core>(core);
    const auto& p = cast<Pos>(position);
    if (!p.meta.ep) return false;
    const auto ep = chess::Square(*p.meta.ep);
    const bool white = p.board.sideToMove() == chess::Color::WHITE;
    for (int file_delta : {-1, 1}) {
        const int from = int(*p.meta.ep) + (white ? -8 : 8) + file_delta;
        if (from < 0 || from >= 64 || std::abs(from % 8 - int(*p.meta.ep) % 8) != 1)
            continue;
        const auto square = chess::Square(from);
        if (p.board.at(square) != chess::Piece(chess::PieceType::PAWN, p.board.sideToMove()))
            continue;
        // Single-move legality checks pins, check evasion and exposed sliders;
        // it does not invoke the complete legal-move generator.
        if (p.board.isLegal(chess::Move::make<chess::Move::ENPASSANT>(square, ep)))
            return true;
    }
    return false;
}
struct NativeMoveBatch {
    std::shared_ptr<const int> owner;
    std::uint64_t top = 0;
    std::array<std::uint16_t, chess::constants::MAX_MOVES> identities{};
    std::size_t size = 0;
    bool invalidated = false;
};
void NativeMoveBatchDeleter::operator()(NativeMoveBatch* batch) const noexcept {
    delete batch;
}
// S1.7a: Compact, legally generated tactical batch. The original complete
// generator remains available and is unchanged. The source move list is a
// fixed-capacity chess::Movelist; only native uint16 identities are retained.
NativeMoveBatchOwner prepare_native_qsearch_batch(
    const ChessCore& core, const Position& position,
    bool checked, bool include_quiet_promotions) {
    (void)cast<Core>(core);
    const auto& p=cast<Pos>(position);
    NativeMoveBatchOwner batch(new NativeMoveBatch);
    batch->owner=p.identity;
    batch->top=p.top;
    chess::Movelist legal;
    if(checked)chess::movegen::legalmoves(legal,p.board);
    else if(include_quiet_promotions)
        chess::movegen::legalmoves<chess::movegen::MoveGenType::TACTICAL>(legal,p.board);
    else
        chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(legal,p.board);
    for(auto move:legal)batch->identities.at(batch->size++)=move.move();
    return batch;
}
// Populate the final repetition storage directly, without a detached Snap.
void native_read_search_identity(const ChessCore& core,const Position& position,
    std::array<PieceCode,64>& pieces, Side& side, std::array<bool,4>& rights,
    NativeSearchMetadata& metadata) {
    (void)cast<Core>(core);
    const auto& p=cast<Pos>(position);
    const auto& native=p.board.celab_native_squares();
    static_assert(sizeof(native)==64 && sizeof(pieces)==64);
    std::memcpy(pieces.data(),native.data(),sizeof(pieces));
    side=p.board.sideToMove()==chess::Color::WHITE?Side::white:Side::black;
    const auto cr=p.board.castlingRights();
    using CS=chess::Board::CastlingRights::Side;
    rights={cr.has(chess::Color::WHITE,CS::KING_SIDE),cr.has(chess::Color::WHITE,CS::QUEEN_SIDE),
            cr.has(chess::Color::BLACK,CS::KING_SIDE),cr.has(chess::Color::BLACK,CS::QUEEN_SIDE)};
    metadata={p.meta.ep,p.meta.half,p.meta.full};
#if defined(CELAB_S112_VERIFY)
    const auto ref=core.snapshot(position);
    if (!std::equal(pieces.begin(),pieces.end(),ref->squares().begin()) ||
        side!=ref->side_to_move() || metadata.raw_ep!=ref->en_passant_square() ||
        metadata.half!=ref->halfmove_clock() || metadata.full!=ref->fullmove_number() ||
        rights!=std::array<bool,4>{ref->can_castle_kingside(Side::white),
            ref->can_castle_queenside(Side::white),ref->can_castle_kingside(Side::black),
            ref->can_castle_queenside(Side::black)})
        throw std::logic_error("S1.12 direct identity mismatch");
#endif
}
bool native_refresh_search_snapshot(const ChessCore& core,const Position& position,
    std::unique_ptr<const PositionSnapshot>& owner) {
    if (!owner || typeid(core)!=typeid(Core) || typeid(position)!=typeid(Pos) ||
        typeid(*owner)!=typeid(Snap)) return false;
    // Core::snapshot allocates a mutable Snap, then transfers const ownership.
    // This private operation requires unique ownership with no live readers.
    fill_native_snapshot(static_cast<const Pos&>(position),
        const_cast<Snap&>(static_cast<const Snap&>(*owner)));
    return true;
}
// S1.13: Populate the stable evaluator ABI directly from the live native frame.
// No SEE king-square queries, intermediate masks or virtual snapshot reads.
bool native_fill_evaluation_position(const ChessCore& core, const Position& position,
                                     ChessEvalPosition& out) noexcept {
    if (typeid(core) != typeid(Core) || typeid(position) != typeid(Pos)) return false;
    const auto& p = static_cast<const Pos&>(position);
    const auto& board = p.board;
    out.struct_size = sizeof out;
    out.schema_major = CHESS_EVAL_SNAPSHOT_MAJOR;
    out.schema_minor = CHESS_EVAL_SNAPSHOT_MINOR;
    const auto white = board.us(chess::Color::WHITE);
    const auto black = board.us(chess::Color::BLACK);
    const chess::PieceType kinds[] = {chess::PieceType::PAWN, chess::PieceType::KNIGHT,
        chess::PieceType::BISHOP, chess::PieceType::ROOK, chess::PieceType::QUEEN,
        chess::PieceType::KING};
    for (unsigned i = 0; i < 6; ++i) {
        const auto mask = board.pieces(kinds[i]);
        out.bitboards[i] = (mask & white).getBits();
        out.bitboards[i+6] = (mask & black).getBits();
    }
    out.side_to_move = board.sideToMove() == chess::Color::BLACK;
    const auto cr = board.castlingRights();
    using CS = chess::Board::CastlingRights::Side;
    out.castling_rights =
        static_cast<std::uint64_t>(cr.has(chess::Color::WHITE, CS::KING_SIDE)) |
        (static_cast<std::uint64_t>(cr.has(chess::Color::WHITE, CS::QUEEN_SIDE)) << 1) |
        (static_cast<std::uint64_t>(cr.has(chess::Color::BLACK, CS::KING_SIDE)) << 2) |
        (static_cast<std::uint64_t>(cr.has(chess::Color::BLACK, CS::QUEEN_SIDE)) << 3);
    // Board EP can be normalized; the evaluator contract requires raw FEN EP.
    out.en_passant_square = p.meta.ep ? *p.meta.ep : CHESS_EVAL_EP_NONE;
    out.halfmove_clock = p.meta.half;
    out.fullmove_number = p.meta.full;
    return true;
}
bool native_see_masks(const ChessCore& core, const Position& position,
                      NativeSeeMasks& masks) noexcept {
    if (typeid(core) != typeid(Core) || typeid(position) != typeid(Pos)) return false;
    const auto& board=static_cast<const Pos&>(position).board;
    masks.sides[0]=board.us(chess::Color::BLACK).getBits();
    masks.sides[1]=board.us(chess::Color::WHITE).getBits();
    masks.kinds[0]=0;
    masks.kinds[1]=board.pieces(chess::PieceType::PAWN).getBits();
    masks.kinds[2]=board.pieces(chess::PieceType::KNIGHT).getBits();
    masks.kinds[3]=board.pieces(chess::PieceType::BISHOP).getBits();
    masks.kinds[4]=board.pieces(chess::PieceType::ROOK).getBits();
    masks.kinds[5]=board.pieces(chess::PieceType::QUEEN).getBits();
    masks.kinds[6]=board.pieces(chess::PieceType::KING).getBits();
    masks.kings[0]=board.kingSq(chess::Color::BLACK).index();
    masks.kings[1]=board.kingSq(chess::Color::WHITE).index();
    return true;
}
NativeMoveBatchOwner prepare_native_move_batch(const ChessCore& core,
                                               const Position& position) {
    (void)cast<Core>(core);
    const auto& p = cast<Pos>(position);
    NativeMoveBatchOwner batch(new NativeMoveBatch);
    batch->owner = p.identity;
    batch->top = p.top;
    const auto legal = moves(p); // Complete existing generator, exactly once.
    for (auto move : legal) batch->identities.at(batch->size++) = move.move();
    return batch;
}
std::size_t native_move_batch_size(const NativeMoveBatch& batch) noexcept {
    return batch.size;
}
bool native_try_skip_claim_probe(const ChessCore& core, Position& position,
                                const NativeMoveBatch* batch, std::size_t ordinal,
                                const Move* eager) {
    (void)cast<Core>(core);
    auto& p = cast<Pos>(position);
    chess::Move move;
    if (batch) {
        if (batch->invalidated || batch->owner != p.identity || batch->top != p.top ||
            ordinal >= batch->size) return false;
        move = chess::Move(batch->identities[ordinal]);
    } else {
        static_assert(std::is_final_v<Mv>, "C2 eager move requires final Mv");
        const auto* m = eager && typeid(*eager) == typeid(Mv)
            ? static_cast<const Mv*>(eager) : nullptr;
        if (!m || !certified(p, *m)) return false;
        move = m->value;
    }
    const bool zero = p.board.at<chess::PieceType>(move.from()) == chess::PieceType::PAWN ||
                      p.board.isCapture(move);
    if ((!zero && p.meta.half == UINT32_MAX) ||
        (p.board.sideToMove() == chess::Color::BLACK && p.meta.full == UINT32_MAX) ||
        p.serial == UINT64_MAX) return false;
    // make_move followed by strict undo restores top/meta/board, but not serial.
    // Preserve that progression so removing probes cannot postpone exhaustion.
    ++p.serial;
    return true;
}
// Batch ordinals can never fall through to public stale-move revalidation.
static chess::Move checked_batch_member(const ChessCore& core, const Position& position,
                                const NativeMoveBatch& batch, std::size_t ordinal) {
    (void)cast<Core>(core);
    const auto& p = cast<Pos>(position);
    if (batch.invalidated || batch.owner != p.identity || batch.top != p.top ||
        ordinal >= batch.size) invalid();
    return chess::Move(batch.identities[ordinal]);
}
// B2 experiment: inner-batch identity is bound to its originating board frame.
// A stack-local certified Mv replaces a heap-owned materialized Mv. The existing
// B1 state transition and validation are intentionally reused unchanged.
void make_native_search_batch_move(const ChessCore& core, Position& position,
                                   const NativeMoveBatch& batch, std::size_t ordinal,
                                   NativeUndoFrame& frame) {
    const chess::Move move = checked_batch_member(core, position, batch, ordinal);
    const auto& p = cast<Pos>(position);
    const Mv certified_move(move, p);
    make_native_search_move(core, position, certified_move, frame);
}

std::string encode_native_batch_member(const ChessCore& core,
                                       const Position& position,
                                       const NativeMoveBatch& batch,
                                       std::size_t ordinal) {
    return native_move_to_uci(checked_batch_member(core, position, batch, ordinal));
}
NativeBatchEncoder::NativeBatchEncoder(const ChessCore& core, const Position& position,
                                       const NativeMoveBatch& batch)
    : position_(position), batch_(batch) {
    (void)cast<Core>(core);
    (void)cast<Pos>(position);
}
std::string NativeBatchEncoder::encode(std::size_t ordinal) const {
    // The constructor proved the final native type. No type-erased/native move
    // values escape; do not replace these per-member certificate checks.
    const auto& p = static_cast<const Pos&>(position_);
    if (batch_.invalidated || batch_.owner != p.identity || batch_.top != p.top ||
        ordinal >= batch_.size) invalid();
    return native_move_to_uci(chess::Move(batch_.identities[ordinal]));
}
CompactUciText NativeBatchEncoder::encode_compact(std::size_t ordinal) const {
    // The binding proved the final native type. Every member still receives
    // the same owner/frame/invalidation/ordinal certificate checks as encode().
    const auto& p = static_cast<const Pos&>(position_);
    if (batch_.invalidated || batch_.owner != p.identity || batch_.top != p.top ||
        ordinal >= batch_.size) invalid();
    return native_move_to_compact_uci(chess::Move(batch_.identities[ordinal]));
}
std::uint16_t NativeBatchEncoder::identity(std::size_t ordinal) const {
    const auto& p = static_cast<const Pos&>(position_);
    if (batch_.invalidated || batch_.owner != p.identity || batch_.top != p.top ||
        ordinal >= batch_.size) invalid();
    return batch_.identities[ordinal];
}
std::unique_ptr<Move> materialize_native_batch_member(
    const ChessCore& core, const Position& position, const NativeMoveBatch& batch,
    std::size_t ordinal) {
    const auto move = checked_batch_member(core, position, batch, ordinal);
    return std::make_unique<Mv>(move, cast<Pos>(position));
}
void invalidate_native_move_batch(NativeMoveBatch& batch) noexcept {
    batch.invalidated = true;
}
} // namespace detail
}
