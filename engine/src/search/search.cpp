#include "packed_pv.hpp"
#include "compact_uci_text.hpp"
#include "native_search_move.hpp"
#include "transposition_table_internal.hpp"
#include "audit_ttg.hpp"
#include "history_duplicates.hpp"
#include "repeated_claim_targets.hpp"
#include "persistent_ordering.hpp"
#include "../native_move_batch.hpp"
#include "../evaluation/native_eval_snapshot.hpp"
#include "../native_undo_fastpath.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <chess_lab/search.hpp>
#include <cmath>
#include <bit>
#include <span>
#include <limits>
#include <cstdlib>
#include <type_traits>

namespace chess_lab {
static_assert(std::is_trivially_copyable_v<SearchScore>);
// Native adapter identification; synthetic adapters retain custom status and clock semantics.
bool native_chess_core(const ChessCore &) noexcept;
std::size_t native_legal_move_count(const ChessCore &, const Position &);
bool native_has_any_legal_move(const ChessCore &, const Position &);
SearchScore SearchScore::finite(double v) {
  if (!std::isfinite(v))
    throw std::invalid_argument("nonfinite search score");
  return {Kind::finite, v, 0};
}
SearchScore SearchScore::white_mate(std::uint64_t d) {
  return {Kind::white_mate, 0, d};
}
SearchScore SearchScore::black_mate(std::uint64_t d) {
  return {Kind::black_mate, 0, d};
}
double SearchScore::value() const {
  if (kind_ != Kind::finite)
    throw std::logic_error("mate has no finite value");
  return value_;
}
std::uint64_t SearchScore::distance() const {
  if (kind_ == Kind::finite)
    throw std::logic_error("finite score has no mate distance");
  return distance_;
}
SearchScore SearchScore::one_ply_farther() const {
  if (kind_ == Kind::finite)
    return *this;
  if (distance_ == UINT64_MAX)
    throw std::overflow_error("mate distance overflow");
  return {kind_, 0, distance_ + 1};
}
std::strong_ordering
SearchScore::operator<=>(const SearchScore &b) const noexcept {
  auto rank = [](Kind k) {
    return k == Kind::black_mate ? 0 : k == Kind::finite ? 1 : 2;
  };
  if (kind_ != b.kind_)
    return rank(kind_) <=> rank(b.kind_);
  if (kind_ == Kind::white_mate)
    return b.distance_ <=> distance_;
  if (kind_ == Kind::black_mate)
    return distance_ <=> b.distance_;
  return value_ < b.value_   ? std::strong_ordering::less
         : value_ > b.value_ ? std::strong_ordering::greater
                             : std::strong_ordering::equal;
}
bool SearchScore::operator==(const SearchScore &b) const noexcept {
  return (*this <=> b) == 0;
}

namespace {
// T1.3a experimental controls. 0=unchanged, 1=defer ordering to after TT,
// 2=add native positive-depth batch materialization deferral.
int t13a_mode() noexcept {
  static const int mode = []() noexcept {
    const char *value = std::getenv("CELAB_T13A_MODE");
    return value && (value[0]=='0' || value[0]=='1' || value[0]=='2') && value[1]=='\0'
        ? int(value[0]-'0') : 2;
  }();
  return mode;
}
struct T13aCounts {
  std::uint64_t lazy_prepared=0, lazy_expanded=0, early_probes=0,
      early_exact_returns=0, early_bound_returns=0, early_nonreturns=0,
      deferred_order_nodes=0, order_skipped_returns=0;
};
thread_local T13aCounts t13a_counts;
struct OrderedMove {
  detail::SearchMoveIdentity text;
  std::unique_ptr<Move> move;
  bool pawn_move = false;
  bool capture = false;
  bool legal_ep = false;
  bool claim_threefold = false;
  bool claim_fifty = false;
  std::uint16_t native_ordinal = 0;
  const Move &executable() const {
    if (!move) throw std::logic_error("batch member requires certified materialization");
    return *move;
  }
};
static_assert(sizeof(OrderedMove) <= 24, "B4.1 search move records must remain compact");


// Accepted S1.7a generator: lazy tactical generation including quiet promotions.
constexpr unsigned s17a_mode() noexcept { return 3U; }
constexpr bool s17a_lazy() noexcept {return true;}
constexpr bool s17a_include_quiet_promotion() noexcept {return true;}
#if defined(CHESS_LAB_S1_DIAGNOSTICS) && CHESS_LAB_S1_DIAGNOSTICS
bool s17a_trace() noexcept {
  static const bool v=[]() noexcept {const char* e=std::getenv("CELAB_S17A_TRACE");
    return e && e[0]=='1' && e[1]=='\0';}();
  return v;
}
#else
constexpr bool s17a_trace() noexcept { return false; }
#endif

// Production S1: accepted policy is fixed; the process-local QS opt-out is
// retained for the fixed-depth compatibility oracle and controlled benchmarks.
// Process-local opt-out; default is selected by the supported CMake option.
#ifndef CHESS_LAB_QUIESCENCE_DEFAULT
#define CHESS_LAB_QUIESCENCE_DEFAULT 1
#endif
bool s11_quiescence_enabled() noexcept {
  static const bool enabled=[]() noexcept {
    const char *v=std::getenv("CELAB_S1_QUIESCENCE");
    if(v && v[0]=='0' && v[1]=='\0')return false;
    if(v && v[0]=='1' && v[1]=='\0')return true;
    return CHESS_LAB_QUIESCENCE_DEFAULT != 0;
  }();
  return enabled;
}
bool s11_see_order_enabled() noexcept { return s11_quiescence_enabled(); }
#if defined(CHESS_LAB_S1_DIAGNOSTICS) && CHESS_LAB_S1_DIAGNOSTICS
bool s11_trace_enabled() noexcept {
  static const bool enabled = [] { const char *v=std::getenv("CELAB_S11_TRACE");
    return v && v[0]=='1' && v[1]=='\0'; }();
  return enabled;
}
#else
constexpr bool s11_trace_enabled() noexcept { return false; }
#endif
// Values are material-evaluator DEFAULTS, in arbitrary units, scaled by 4.
// This is an ordering heuristic, not a second evaluator or centipawn score.
int s11_piece_kind(PieceCode p) noexcept {
  switch (p) {
    case PieceCode::white_pawn: case PieceCode::black_pawn: return 1;
    case PieceCode::white_knight: case PieceCode::black_knight: return 2;
    case PieceCode::white_bishop: case PieceCode::black_bishop: return 3;
    case PieceCode::white_rook: case PieceCode::black_rook: return 4;
    case PieceCode::white_queen: case PieceCode::black_queen: return 5;
    case PieceCode::white_king: case PieceCode::black_king: return 6;
    case PieceCode::empty: return 0;
  }
  return 0;
}
bool s11_white_piece(PieceCode p) noexcept {
  return p==PieceCode::white_pawn||p==PieceCode::white_knight||
    p==PieceCode::white_bishop||p==PieceCode::white_rook||
    p==PieceCode::white_queen||p==PieceCode::white_king;
}
int s11_value(PieceCode p) noexcept {
  constexpr int values[7]={0,4,12,13,20,36,400};
  return values[s11_piece_kind(p)];
}
PieceCode s11_promotion(bool white, char suffix) noexcept {
  switch(suffix) {
    case 'n': return white?PieceCode::white_knight:PieceCode::black_knight;
    case 'b': return white?PieceCode::white_bishop:PieceCode::black_bishop;
    case 'r': return white?PieceCode::white_rook:PieceCode::black_rook;
    default: return white?PieceCode::white_queen:PieceCode::black_queen;
  }
}
// Board-local attack and king-safety tests; no ChessCore adapter assumptions.
// Re-evaluate rays after each recapture to recognize x-ray discoveries.
bool s11_attacks(const std::array<PieceCode,64> &b, int from, int to) noexcept {
  const auto p=b[from];const int kind=s11_piece_kind(p);
  if (!kind || from==to) return false;
  const int dx=to%8-from%8,dy=to/8-from/8;
  const int ax=std::abs(dx),ay=std::abs(dy);
  if (kind==1) return ax==1 && dy==(s11_white_piece(p)?1:-1);
  if (kind==2) return (ax==1&&ay==2)||(ax==2&&ay==1);
  if (kind==6) return ax<=1&&ay<=1;
  const bool diag=ax==ay,straight=(dx==0||dy==0);
  if (!((kind==3&&diag)||(kind==4&&straight)||
        (kind==5&&(diag||straight)))) return false;
  const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
  int x=from%8+sx,y=from/8+sy;
  while (x!=to%8||y!=to/8) {
    if(b[y*8+x]!=PieceCode::empty)return false;
    x+=sx;y+=sy;
  }
  return true;
}
bool s11_attacked(const std::array<PieceCode,64> &b, int target,
                  bool by_white) noexcept {
  for (int i=0;i<64;++i)
    if(b[i]!=PieceCode::empty&&s11_white_piece(b[i])==by_white&&
       s11_attacks(b,i,target))return true;
  return false;
}
int s11_king_square(const std::array<PieceCode,64> &b,bool white) noexcept {
  const auto king=white?PieceCode::white_king:PieceCode::black_king;
  for(int i=0;i<64;++i)if(b[i]==king)return i;
  return -1;
}
// LVA SEE with legal recaptures: pinned movers and unsafe king captures are
// excluded, while discovered sliders and EP/promotion effects are modeled.
// As with standard LVA SEE, this is *not* a proof about all chess tactics.
// Trace-only SEE recapture-step instrumentation. This pointer is null
// during normal performance runs. Count the original candidate separately
// via s11_see_calls; this tracks subsequent hypothetical recaptures only.
thread_local std::uint64_t *s15_see_step_sink=nullptr;
int s11_recapture(std::array<PieceCode,64> &b,int target,bool white,
                  int remaining) noexcept {
  if(remaining==0 || s11_piece_kind(b[target])==6)return 0;
  int from=-1,attacker_value=99999;
  for(int i=0;i<64;++i) {
    const auto moving=b[i];
    if(moving==PieceCode::empty||s11_white_piece(moving)!=white||
       !s11_attacks(b,i,target))continue;
    const auto captured=b[target];
    const bool promoting=s11_piece_kind(moving)==1 &&
        target/8==(white?7:0);
    const auto placed=promoting?s11_promotion(white,'q'):moving;
    b[i]=PieceCode::empty;b[target]=placed;
    const int king=s11_king_square(b,white);
    const bool legal=king>=0 && !s11_attacked(b,king,!white);
    b[i]=moving;b[target]=captured;
    if(legal && s11_value(moving)<attacker_value) {
      attacker_value=s11_value(moving);from=i;
    }
  }
  if(from<0)return 0;
  if(s15_see_step_sink) ++*s15_see_step_sink;
  const auto moving=b[from],captured=b[target];
  const bool promoting=s11_piece_kind(moving)==1 &&
      target/8==(white?7:0);
  const auto placed=promoting?s11_promotion(white,'q'):moving;
  const int gain=s11_value(captured)+(promoting?s11_value(placed)-s11_value(moving):0);
  b[from]=PieceCode::empty;b[target]=placed;
  const int reply=s11_recapture(b,target,!white,remaining-1);
  b[from]=moving;b[target]=captured;
  return std::max(0,gain-reply);
}
int s11_see(const PositionSnapshot &snap,const OrderedMove &m) noexcept {
  std::array<PieceCode,64> b = empty_piece_squares();
  const auto squares=snap.squares();
  std::copy(squares.begin(),squares.end(),b.begin());
  const int from=static_cast<int>(m.text.from()),to=static_cast<int>(m.text.to());
  const auto moving=b[from];
  if(moving==PieceCode::empty)return -100000; // Defensive, never legal.
  const bool white=s11_white_piece(moving);
  const auto captured=m.legal_ep?(white?PieceCode::black_pawn:PieceCode::white_pawn):b[to];
  const bool promoting=m.text.size()==5;
  const auto placed=promoting?s11_promotion(white,m.text[4]):moving;
  const int gain=s11_value(captured)+(promoting?s11_value(placed)-s11_value(moving):0);
  if(m.legal_ep)b[to+(white?-8:8)]=PieceCode::empty;
  b[from]=PieceCode::empty;b[to]=placed;
  return gain-s11_recapture(b,to,!white,31);
}

// Optional scalar SEE/check/input differential verification.
bool s17c_verify() noexcept {
  static const bool on=[]() noexcept {
    const char *p=std::getenv("CELAB_S17C_VERIFY");
    return p && p[0]=='1' && p[1]=='\0';
  }();
  return on;
}
struct S17CBoard {
  std::array<PieceCode,64> b = empty_piece_squares();
  std::uint64_t sides[2]{}; // [0]=black, [1]=white, updated for each recapture
  int kings[2]{-1,-1};
  static unsigned color(bool white) noexcept {return white?1U:0U;}
  void initialize(std::span<const PieceCode> sq) noexcept {
    for(unsigned i=0;i<64;++i) {
      b[i]=sq[i];
      if(b[i]==PieceCode::empty)continue;
      const bool white=s11_white_piece(b[i]);
      sides[color(white)]|=std::uint64_t{1}<<i;
      if(s11_piece_kind(b[i])==6)kings[color(white)]=static_cast<int>(i);
    }
  }
  struct Undo {int from,to;PieceCode moving,captured;int oldKing,oldCapturedKing;};
  Undo push(int from,int to,PieceCode placed) noexcept {
    const auto moving=b[from], captured=b[to];
    const bool white=s11_white_piece(moving);
    const unsigned mine=color(white),theirs=color(!white);
    Undo u{from,to,moving,captured,kings[mine],kings[theirs]};
    sides[mine]&=~(std::uint64_t{1}<<from);
    sides[mine]|=std::uint64_t{1}<<to;
    if(captured!=PieceCode::empty && s11_white_piece(captured)!=white)
      sides[theirs]&=~(std::uint64_t{1}<<to);
    b[from]=PieceCode::empty;b[to]=placed;
    if(s11_piece_kind(moving)==6)kings[mine]=to;
    if(s11_piece_kind(captured)==6 && s11_white_piece(captured)!=white)kings[theirs]=-1;
    return u;
  }
  void pop(const Undo &u) noexcept {
    const bool white=s11_white_piece(u.moving);
    const unsigned mine=color(white),theirs=color(!white);
    b[u.from]=u.moving;b[u.to]=u.captured;
    sides[mine]|=std::uint64_t{1}<<u.from;
    sides[mine]&=~(std::uint64_t{1}<<u.to);
    if(u.captured!=PieceCode::empty && s11_white_piece(u.captured)!=white)
      sides[theirs]|=std::uint64_t{1}<<u.to;
    kings[mine]=u.oldKing;kings[theirs]=u.oldCapturedKing;
  }
  void remove(int square) noexcept {
    if(b[square]==PieceCode::empty)return;
    const bool white=s11_white_piece(b[square]);
    sides[color(white)]&=~(std::uint64_t{1}<<square);
    b[square]=PieceCode::empty;
  }
};
// S1.7d: target-centric native bitboards. The existing C3 board/king state
// remains the source of truth; piece-kind masks are updated for every push/pop.
// A square's eight rays are constant; only the nearest blocker can attack it.
// No vendored generator changes or dependencies on a particular CPU ISA.
constexpr std::uint64_t s17d_bit(int i) noexcept { return std::uint64_t{1}<<i; }
struct S17DRays {
  std::uint64_t rays[64][8]{}, knight[64]{}, king[64]{}, pawn[2][64]{};
  constexpr S17DRays() noexcept {
    constexpr int dx[8]={1,0,1,-1,-1,0,-1,1};
    constexpr int dy[8]={0,1,1,1,0,-1,-1,-1};
    // Directions 0..3 increase square index, 4..7 decrease it.
    for(int t=0;t<64;++t) {
      for(int d=0;d<8;++d)
        for(int x=t%8+dx[d],y=t/8+dy[d];x>=0&&x<8&&y>=0&&y<8;x+=dx[d],y+=dy[d])
          rays[t][d]|=s17d_bit(y*8+x);
      constexpr int nx[8]={1,2,2,1,-1,-2,-2,-1};
      constexpr int ny[8]={2,1,-1,-2,-2,-1,1,2};
      for(int d=0;d<8;++d) {
        const int x=t%8+nx[d],y=t/8+ny[d];
        if(x>=0&&x<8&&y>=0&&y<8)knight[t]|=s17d_bit(y*8+x);
        const int kx=t%8+dx[d],ky=t/8+dy[d];
        if(kx>=0&&kx<8&&ky>=0&&ky<8)king[t]|=s17d_bit(ky*8+kx);
      }
      for(int x=t%8-1;x<=t%8+1;x+=2)if(x>=0&&x<8) {
        if(t/8<7)pawn[0][t]|=s17d_bit(t+8-t%8+x);
        if(t/8>0)pawn[1][t]|=s17d_bit(t-8-t%8+x);
      }
    }
  }
};
constexpr S17DRays s17d_rays{};
struct S17DBoard : S17CBoard {
  std::uint64_t kinds[7]{}; // both colors, intersect with existing side masks
  void initialize(std::span<const PieceCode> sq) noexcept {
    for(int i=0;i<64;++i) {
      b[i]=sq[i];
      const int kind=s11_piece_kind(b[i]);
      if(!kind)continue;
      const unsigned side=color(s11_white_piece(b[i]));
      sides[side]|=s17d_bit(i);kinds[kind]|=s17d_bit(i);
      if(kind==6)kings[side]=i;
    }
  }
  Undo push(int from,int to,PieceCode placed) noexcept {
    const auto frombit=s17d_bit(from),tobit=s17d_bit(to);
    kinds[s11_piece_kind(b[from])]&=~frombit;
    if(b[to]!=PieceCode::empty)kinds[s11_piece_kind(b[to])]&=~tobit;
    kinds[s11_piece_kind(placed)]|=tobit;
    return S17CBoard::push(from,to,placed);
  }
  void pop(const Undo &u) noexcept {
    const auto frombit=s17d_bit(u.from),tobit=s17d_bit(u.to);
    kinds[s11_piece_kind(b[u.to])]&=~tobit;
    kinds[s11_piece_kind(u.moving)]|=frombit;
    if(u.captured!=PieceCode::empty)kinds[s11_piece_kind(u.captured)]|=tobit;
    S17CBoard::pop(u);
  }
  void remove(int square) noexcept {
    if(b[square]!=PieceCode::empty)kinds[s11_piece_kind(b[square])]&=~s17d_bit(square);
    S17CBoard::remove(square);
  }
};
template<class MaskState>
std::uint64_t s17d_attackers(const MaskState &state,int target,bool white) noexcept {
  const auto us=state.sides[S17CBoard::color(white)],occ=state.sides[0]|state.sides[1];
  const auto diag=us&(state.kinds[3]|state.kinds[5]);
  const auto straight=us&(state.kinds[4]|state.kinds[5]);
  std::uint64_t result=us&((s17d_rays.pawn[S17CBoard::color(white)][target]&state.kinds[1])|
      (s17d_rays.knight[target]&state.kinds[2])|(s17d_rays.king[target]&state.kinds[6]));
  for(int d=0;d<8;++d) {
    const auto sliders=(d%4>=2)?diag:straight;
    const auto ray=s17d_rays.rays[target][d];
    if(!(ray&sliders))continue;
    const auto blockers=ray&occ; // nonzero because sliders are occupied
    const int first=d<4?static_cast<int>(std::countr_zero(blockers)):
        63-static_cast<int>(std::countl_zero(blockers));
    result|=s17d_bit(first)&sliders;
  }
  return result;
}
struct S17EFullStages {
  const S17DBoard &s;
  std::uint64_t attackers;
  S17EFullStages(const S17DBoard &state,int t,bool white) noexcept:
    s(state),attackers(s17d_attackers(state,t,white)) {}
  template<int Kind> std::uint64_t get() const noexcept {
    /* E_STAGE */
    return attackers&s.kinds[Kind];
  }
};
template<unsigned Mode> int s17e_recapture(S17DBoard &,int,bool,int) noexcept;
// Static kind recursion is compiled away; no runtime sort or value comparisons.
template<unsigned Mode,int Kind=1,class Stages>
int s17e_choose(S17DBoard &s,Stages &g,int target,bool white,int remaining) noexcept {
  auto candidates=g.template get<Kind>();
  while(candidates) {
    const int from=int(std::countr_zero(candidates));candidates&=candidates-1;
    const auto moving=s.b[from],captured=s.b[target];
    const bool promoting=Kind==1 && target/8==(white?7:0);
    const auto placed=promoting?s11_promotion(white,'q'):moving;
    const int gain=s11_value(captured)+(promoting?s11_value(placed)-s11_value(moving):0);
    /* E_TRIAL */
    const auto undo=s.push(from,target,placed);
    const int king=s.kings[S17CBoard::color(white)];
    bool legal=false;
    if(king>=0) {
      /* E_SAFETY */
      legal=!s17d_attackers(s,king,!white);
    }
    if(legal) {
      /* E_SELECTED */
      const int reply=s17e_recapture<Mode>(s,target,!white,remaining-1);
      s.pop(undo);return std::max(0,gain-reply);
    }
    s.pop(undo);
  }
  if constexpr(Kind<6)return s17e_choose<Mode,Kind+1>(s,g,target,white,remaining);
  return 0;
}
template<unsigned Mode>
int s17e_recapture(S17DBoard &s,int target,bool white,int remaining) noexcept {
  /* E_RECAPTURE */
  if(remaining==0 || s11_piece_kind(s.b[target])==6)return 0;
  /* E_DISCOVERY */
  static_assert(Mode==5,"Only accepted E5 recapture is supported");
  S17EFullStages g(s,target,white);
  if(!g.attackers)return 0;
  return s17e_choose<Mode>(s,g,target,white,remaining);
}
template<unsigned Mode>
int s17e_optimized_see(const PositionSnapshot &snap,const OrderedMove &m) noexcept {
  S17DBoard s;s.initialize(snap.squares());
  const int from=int(m.text.from()),to=int(m.text.to());
  const auto moving=s.b[from];if(moving==PieceCode::empty)return -100000;
  const bool white=s11_white_piece(moving);
  const auto captured=m.legal_ep?(white?PieceCode::black_pawn:PieceCode::white_pawn):s.b[to];
  const bool promoting=m.text.size()==5;
  const auto placed=promoting?s11_promotion(white,m.text[4]):moving;
  const int gain=s11_value(captured)+(promoting?s11_value(placed)-s11_value(moving):0);
  if(m.legal_ep)s.remove(to+(white?-8:8));
  s.push(from,to,placed);
  return gain-s17e_recapture<Mode>(s,to,!white,31);
}


// S1.7f: a node-local scratchboard, constructed only after delta pruning
// has allowed the first SEE call. No child search shares this object.
bool s17f_same(const S17DBoard &a,const S17DBoard &b) noexcept {
  if(a.b!=b.b || a.sides[0]!=b.sides[0] || a.sides[1]!=b.sides[1] ||
     a.kings[0]!=b.kings[0] || a.kings[1]!=b.kings[1])return false;
  for(int i=0;i<7;++i)if(a.kinds[i]!=b.kinds[i])return false;
  return true;
}
int s17f_reused_see(S17DBoard &s,const OrderedMove &m) noexcept {
  const int from=int(m.text.from()),to=int(m.text.to());
  const auto moving=s.b[from];if(moving==PieceCode::empty)return -100000;
  const bool white=s11_white_piece(moving);
  const int ep_square=to+(white?-8:8);
  const auto captured=m.legal_ep?(white?PieceCode::black_pawn:PieceCode::white_pawn):s.b[to];
  const bool promoting=m.text.size()==5;
  const auto placed=promoting?s11_promotion(white,m.text[4]):moving;
  const int gain=s11_value(captured)+(promoting?s11_value(placed)-s11_value(moving):0);
  PieceCode ep_piece=PieceCode::empty;
  if(m.legal_ep){ep_piece=s.b[ep_square];s.remove(ep_square);}
  const auto undo=s.push(from,to,placed);
  const int result=gain-s17e_recapture<5>(s,to,!white,31);
  s.pop(undo);
  if(ep_piece!=PieceCode::empty){
    s.b[ep_square]=ep_piece;
    s.sides[S17CBoard::color(s11_white_piece(ep_piece))]|=s17d_bit(ep_square);
    s.kinds[s11_piece_kind(ep_piece)]|=s17d_bit(ep_square);
    if(s11_piece_kind(ep_piece)==6)s.kings[S17CBoard::color(s11_white_piece(ep_piece))]=ep_square;
  }
  return result;
}
// S1.7g.1: copy the existing snapshot's square array, not reconstruct masks.
bool s17g1_initialize(S17DBoard &state,const PositionSnapshot &snap,
                       const ChessCore &core,const Position &position) noexcept {
  detail::NativeSeeMasks masks;
  if(!detail::native_see_masks(core,position,masks))return false;
  const auto squares=snap.squares();
  std::copy(squares.begin(),squares.end(),state.b.begin());
  std::copy_n(masks.sides,2,state.sides);
  std::copy_n(masks.kinds,7,state.kinds);
  std::copy_n(masks.kings,2,state.kings);
  return true;
}
struct S17FNode {
  std::optional<S17DBoard> state;
  /* F_NODE_FIELDS */
  int see(const PositionSnapshot &snap,const OrderedMove &m,const ChessCore &core,const Position &position) noexcept {
    /* F_NODE_CALL */
    /* F_SCOPE */
    if(!state){
      /* F_INIT_START */
      state.emplace();
      const bool direct=s17g1_initialize(*state,snap,core,position);
      if(!direct)state->initialize(snap.squares());
      /* G1_COUNTS */
      if(s17c_verify()){
        S17DBoard expected;expected.initialize(snap.squares());
        if(!s17f_same(*state,expected)){
          std::fprintf(stderr,"CELAB_S17G1_INITIAL_STATE_MISMATCH\n");std::abort();
        }
      }
      /* F_INIT_END */
    }else { /* F_REUSE */ }
    const int result=s17f_reused_see(*state,m);
    if(s17c_verify()){
      S17DBoard fresh;fresh.initialize(snap.squares());
      const int e5=s17e_optimized_see<5>(snap,m),original=s11_see(snap,m);
      if(!s17f_same(*state,fresh)||result!=e5||result!=original){
        std::fprintf(stderr,"CELAB_S17F_SEE_OR_RESTORE_MISMATCH from=%u to=%u got=%d E5=%d S11=%d\n",unsigned(m.text.from()),unsigned(m.text.to()),result,e5,original);std::abort();
      }
    }
    return result;
  }
};


// Accepted nonpositive-SEE pruning, restricted to default material.
// ALL in-check evasions, promotions and EP captures remain unpruned.
// This is a tactical heuristic, not a correctness proof.
bool s12_prune_candidate(unsigned mode,unsigned /*qply*/,bool checked,
                         const OrderedMove &m,int see) noexcept {
  return mode==17 && !checked && m.capture && m.text.size()!=5 && !m.legal_ep && see<=0;
}

// Accepted delta-0 is restricted to the default material-v1 configuration.
// SEE ordering values use 4 units / pawn.
// The pre-SEE delta test is a heuristic, NOT a proof of a safe cutoff.
constexpr bool s16_delta_mode(unsigned mode) noexcept {return mode==17;}
constexpr double s16_delta_margin(unsigned) noexcept {return 0.0;}
bool s16_delta_reject(unsigned mode,bool checked,bool capture,bool ep,
                      bool promotion,bool white,double static_score,
                      double bound,double captured_value) noexcept {
  const double margin=s16_delta_margin(mode);
  if(!s16_delta_mode(mode) || checked || !capture || ep || promotion)return false;
  if(!std::isfinite(static_score)||!std::isfinite(bound)||
     !std::isfinite(captured_value))return false;
  // White-positive evaluation. Test only the side-to-move active bound.
  return white ? static_score+captured_value+margin<=bound :
                 static_score-captured_value-margin>=bound;
}
bool s16_default_material(const EvaluationIdentity &id) noexcept {
  return id.module_id=="org.chesslab.material" &&
      id.canonical_configuration==
      "{\"bishop_value\":3.25,\"knight_value\":3,\"pawn_value\":1,\"queen_value\":9,\"rook_value\":5}";
}

// Board-local postmove test: protect *every* capture that gives check,
// including discovered, slider, and knight checks. Never makes a real move.
bool s12_gives_check(const PositionSnapshot &snap,const OrderedMove &m) noexcept {
  std::array<PieceCode,64> b = empty_piece_squares();
  const auto squares=snap.squares();
  std::copy(squares.begin(),squares.end(),b.begin());
  const int from=static_cast<int>(m.text.from()),to=static_cast<int>(m.text.to());
  const auto moving=b[from];
  if(moving==PieceCode::empty)return true; // fail-open: do not prune
  const bool white=s11_white_piece(moving);
  if(m.legal_ep)b[to+(white?-8:8)]=PieceCode::empty;
  const auto placed=m.text.size()==5?s11_promotion(white,m.text[4]):moving;
  b[from]=PieceCode::empty;b[to]=placed;
  const int king=s11_king_square(b,!white);
  return king<0 || s11_attacked(b,king,white); // fail-open for bad snapshot
}

// S1.7h: root masks are immutable for the lifetime of the ranking loop.
// Only the hypothetical piece/occupancy masks are changed; no square copy,
// real make/undo, or vendor board escapes the private adapter.
bool s17h_mask_check(const detail::NativeSeeMasks &root,const PositionSnapshot &snap,
                     const OrderedMove &m) noexcept {
  const auto sq=snap.squares();
  const int from=int(m.text.from()),to=int(m.text.to());
  const auto moving=sq[from];
  if(moving==PieceCode::empty)return true;
  const bool white=s11_white_piece(moving);
  const unsigned side=S17CBoard::color(white),enemy=S17CBoard::color(!white);
  const int king=root.kings[enemy];
  if(king<0 || king==to || (m.legal_ep && king==to+(white?-8:8)))return true;
  auto masks=root;
  const auto erase=[&](int i) noexcept {
    const auto p=sq[i];if(p==PieceCode::empty)return;
    const auto bit=s17d_bit(i);
    masks.sides[S17CBoard::color(s11_white_piece(p))]&=~bit;
    masks.kinds[s11_piece_kind(p)]&=~bit;
  };
  if(m.legal_ep)erase(to+(white?-8:8));
  erase(from);erase(to);
  const auto placed=m.text.size()==5?s11_promotion(white,m.text[4]):moving;
  masks.sides[side]|=s17d_bit(to);
  masks.kinds[s11_piece_kind(placed)]|=s17d_bit(to);
  return s17d_attackers(masks,king,white)!=0;
}
// S1.10: direct native input on actual evaluation calls only.
ChessEvalPosition s110_eval_snapshot(const ChessCore &core,const Position &position,
                                    const PositionSnapshot &snap) {
  /* I_START */
  bool direct=false;
  auto value=detail::native_evaluation_snapshot(core,position,snap,direct,s17c_verify());
  /* I_END */
  return value;
}
struct S17HNode {
  // State 0: unqueried, 1: native masks ready, 2: unsupported adapter.
  unsigned status=0;
  detail::NativeSeeMasks root;
  bool check(const PositionSnapshot &snap,const OrderedMove &m,
             const ChessCore &core,const Position &position,unsigned reason) noexcept {
    (void)reason;
    bool result;
    if(status!=2){
      if(!status){status=detail::native_see_masks(core,position,root)?1U:2U; /* H_COPY */}
      else { /* H_REUSE */ }
    }
    if(status==1){
      /* H_NATIVE */
      result=s17h_mask_check(root,snap,m);
      if(s17c_verify() && result!=s12_gives_check(snap,m)){
        std::fprintf(stderr,"CELAB_S17H_CHECK_MISMATCH from=%u to=%u\n",unsigned(m.text.from()),unsigned(m.text.to()));std::abort();
      }
    }else{
      /* H_LEGACY */
      result=s12_gives_check(snap,m);
    }
    /* H_END */
    return result;
  }
};
// H2-C1.1 experimental: ONE capture and ONE promotion index, recorded during
// native move-fact preparation. Each compiled variant pays only for its policy.
struct H2C11Candidates {
  static constexpr std::uint16_t missing=UINT16_MAX;
  std::uint16_t first_promotion=missing;
  std::uint16_t chosen_capture=missing;
#if CELAB_H2C11_POLICY == 2 || CELAB_H2C11_POLICY == 3
  int best_capture_score=-100000;
#endif
};
#if CELAB_H2C11_POLICY == 2 || CELAB_H2C11_POLICY == 3
static int h2c11_piece_value(PieceCode p) noexcept {
  switch(p) {
    case PieceCode::white_pawn: case PieceCode::black_pawn: return 1;
    case PieceCode::white_knight: case PieceCode::black_knight: return 3;
    case PieceCode::white_bishop: case PieceCode::black_bishop: return 3;
    case PieceCode::white_rook: case PieceCode::black_rook: return 5;
    case PieceCode::white_queen: case PieceCode::black_queen: return 9;
    // Legal moves cannot capture kings, but handle unexpected facts safely.
    case PieceCode::white_king: case PieceCode::black_king: return 100;
    case PieceCode::empty: return 0;
  }
  return 0;
}
#endif

void ordered_moves_into(std::vector<OrderedMove> &result, const ChessCore &c, const Position &p,
                        const CancellationToken *token,
                        const PositionSnapshot *snapshot) {
  result.clear();
  {
    auto legal = c.legal_moves(p);
    if (token && !legal.empty()) detail::tt_poll(*token);
    result.reserve(legal.size());
    for (auto &m : legal) {
      if (token) detail::tt_poll(*token);
      result.push_back({c.encode_move(p, *m), std::move(m)});
    }
  }
    // Legal en-passant captures are already members of this list.
  if (snapshot) {
    const auto squares = snapshot->squares();
    for (auto &m : result) {
      const unsigned from = unsigned(m.text[0] - 'a') + 8U * unsigned(m.text[1] - '1');
      const unsigned to = unsigned(m.text[2] - 'a') + 8U * unsigned(m.text[3] - '1');
      const auto piece = squares[from];
      m.pawn_move = piece == PieceCode::white_pawn || piece == PieceCode::black_pawn;
      m.legal_ep = m.pawn_move && m.text[0] != m.text[2] &&
          squares[to] == PieceCode::empty &&
          snapshot->en_passant_square() == std::optional<std::uint8_t>(static_cast<std::uint8_t>(to));
      m.capture = squares[to] != PieceCode::empty || m.legal_ep;
    }
  }
  std::sort(result.begin(), result.end(),
             [](const auto &a, const auto &b) { return a.text < b.text; });
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto &s = result[i].text;
    auto square = [&](int n) {
      return s[n] >= 'a' && s[n] <= 'h' && s[n + 1] >= '1' && s[n + 1] <= '8';
    };
    if ((s.size() != 4 && s.size() != 5) || !square(0) || !square(2) ||
        (s.size() == 5 &&
         (s[4] != 'b' && s[4] != 'n' && s[4] != 'q' && s[4] != 'r')) ||
        (i && result[i - 1].text == s))
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
  }
  // B4.1b retains the contiguous allocation for the next sibling.
}
std::vector<OrderedMove> ordered_moves(const ChessCore &c, const Position &p,
                                       const CancellationToken *token = nullptr,
                                       const PositionSnapshot *snapshot = nullptr) {
  std::vector<OrderedMove> result;
  ordered_moves_into(result,c,p,token,snapshot);
  return result;
}
// Keep the bounded scratch frame out of recursive traversal even under /Ob2.
// This helper changes only native batch construction; eager/reset sorts stay put.
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void sort_native_batch_moves(std::vector<OrderedMove> &moves) {
  auto original_sort = [&] {
    std::sort(moves.begin(), moves.end(),
              [](const auto &a, const auto &b) { return a.text < b.text; });
  };
  const auto count = moves.size();
  if (count < 2) return;
  // An index occupies the low 16 bits. Do not truncate a larger list.
  if (count - 1 > std::numeric_limits<std::uint16_t>::max()) {
    original_sort();
    return;
  }
  for (const auto &move : moves)
    if ((move.text.size() != 4 && move.text.size() != 5) ||
        (move.text.size() == 5 && move.text[4] == '\0')) {
      original_sort();
      return; // The unchanged validation below rejects malformed text.
    }
  std::array<std::uint64_t, 128> local;
  std::vector<std::uint64_t> dynamic;
  if (count > local.size()) dynamic.resize(count); // Fallible, before record mutation.
  auto *entries = count <= local.size() ? local.data() : dynamic.data();
  for (std::size_t i = 0; i < count; ++i) {
    const auto &s = moves[i].text;
    std::uint64_t key = 0;
    for (std::size_t j = 0; j < 5; ++j)
      key = (key << 8) | (j < s.size() ? static_cast<unsigned char>(s[j]) : 0U);
    entries[i] = (key << 16) | static_cast<std::uint16_t>(i);
  }
  std::sort(entries, entries + count);
  // All fallible preparation is complete. Whole-record cycles retain owners,
  // facts, claims and original certificate ordinals. No polls or board mutation.
  static_assert(std::is_nothrow_move_constructible_v<OrderedMove>);
  static_assert(std::is_nothrow_move_assignable_v<OrderedMove>);
  constexpr std::uint64_t visited = std::uint64_t{1} << 63;
  auto permute = [&]() noexcept {
    for (std::size_t start = 0; start < count; ++start) {
      if (entries[start] & visited) continue;
      auto source = static_cast<std::uint16_t>(entries[start]);
      entries[start] |= visited;
      if (source == start) continue;
      OrderedMove saved = std::move(moves[start]);
      auto dest = start;
      while (source != start) {
        moves[dest] = std::move(moves[source]);
        dest = source;
        source = static_cast<std::uint16_t>(entries[dest]);
        entries[dest] |= visited;
      }
      moves[dest] = std::move(saved);
    }
  };
  permute();
}
// Shared with the implementation-including regression translation unit only.
void validate_native_batch_moves(const std::vector<OrderedMove> &result) {
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto &s = result[i].text;
    auto square = [&](int n) {
      return s[n] >= 'a' && s[n] <= 'h' && s[n + 1] >= '1' && s[n + 1] <= '8';
    };
    if ((s.size() != 4 && s.size() != 5) || !square(0) || !square(2) ||
        (s.size() == 5 && (s[4] != 'b' && s[4] != 'n' && s[4] != 'q' && s[4] != 'r')) ||
        (i && result[i - 1].text == s))
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
  }
}
// The generic eager path above stays the reference/fallback. Native horizon leaves
// keep the same text/fact/validation rules without allocating unused Mv owners.
void batch_ordered_moves_into(
    std::vector<OrderedMove> &result, const ChessCore &c, const Position &p,
    const detail::NativeMoveBatch &batch,
    const CancellationToken *token, const PositionSnapshot &snapshot,
    H2C11Candidates *h2c11 = nullptr) {
  result.clear();
  const auto count = detail::native_move_batch_size(batch);
  if (token && count) detail::tt_poll(*token);
  result.reserve(count);
  if (count) {
    if (token) detail::tt_poll(*token);
    const detail::NativeBatchEncoder encoder(c, p, batch);
    for (std::size_t i=0; i<count; ++i) {
      if (i && token) detail::tt_poll(*token);
      // Identity only: no UCI encoding, sorting-key creation, or record permutation.
      OrderedMove m{detail::SearchMoveIdentity::from_native(encoder.identity(i)), {}};
      m.native_ordinal = static_cast<std::uint16_t>(i);
      result.push_back(std::move(m));
    }
  }
  const auto squares=snapshot.squares();
  for (std::size_t h2c11_i=0;h2c11_i<result.size();++h2c11_i) {
    auto &m=result[h2c11_i];
    const auto id=m.text.native_move();
    const unsigned from=id.from(), to=id.uci_to();
    const auto piece=squares[from];
    m.pawn_move=piece==PieceCode::white_pawn || piece==PieceCode::black_pawn;
    m.legal_ep=m.pawn_move && (from%8 != to%8) &&
        squares[to]==PieceCode::empty &&
        snapshot.en_passant_square()==std::optional<std::uint8_t>(static_cast<std::uint8_t>(to));
    m.capture=squares[to]!=PieceCode::empty || m.legal_ep;
    // Reuse the existing generation/fact-annotation pass; no tactical scan.
    if (h2c11) {
      if (h2c11_i >= H2C11Candidates::missing)
        throw std::logic_error("H2-C1.1 native list exceeds index capacity");
      const auto index=static_cast<std::uint16_t>(h2c11_i);
      if (m.text.native_move().kind()==detail::NativeBinaryMove::promotion &&
          h2c11->first_promotion==H2C11Candidates::missing)
        h2c11->first_promotion=index;
      if (m.capture) {
#if CELAB_H2C11_POLICY == 1
        // First capture: no material scoring and no second list pass.
        if (h2c11->chosen_capture==H2C11Candidates::missing)
          h2c11->chosen_capture=index;
#elif CELAB_H2C11_POLICY == 2 || CELAB_H2C11_POLICY == 3
        const auto victim=m.legal_ep ? PieceCode::white_pawn : squares[to];
        const int captured=h2c11_piece_value(victim);
#if CELAB_H2C11_POLICY == 2
        const int priority=captured; // Most valuable victim.
#else
        const int priority=captured-h2c11_piece_value(piece); // Victim - attacker.
#endif
        if (priority>h2c11->best_capture_score) {
          h2c11->best_capture_score=priority;
          h2c11->chosen_capture=index;
        }
#else
#error "Unsupported H2-C1.1 policy"
#endif
      }
    }
  }
  // Native generator output is canonical by original ordinal. The vendored
  // library's legal generator already certifies uniqueness; no UCI text exists.
}
std::vector<OrderedMove> batch_ordered_moves(
    const ChessCore &c, const Position &p, const detail::NativeMoveBatch &batch,
    const CancellationToken *token, const PositionSnapshot &snapshot) {
  std::vector<OrderedMove> result;
  batch_ordered_moves_into(result,c,p,batch,token,snapshot);
  return result;
}
// Explicit restoration propagates undo failures as traversal errors, even when
// unwinding an evaluator failure. Never throw from a cleanup destructor.
template <class F>
auto with_move(const ChessCore &c, Position &p, const Move &m, F f) {
  if (native_chess_core(c)) {
    detail::NativeUndoFrame token; // Stack-owned; no per-edge Undo allocation.
    detail::make_native_search_move(c, p, m, token);
    using T = decltype(f());
    std::optional<T> result;
    try {
      result.emplace(f());
    } catch (...) {
      detail::undo_native_search_move(c, p, token);
      throw;
    }
    detail::undo_native_search_move(c, p, token);
    return std::move(*result);
  }
  auto token = c.make_move(p, m);
  using T = decltype(f());
  std::optional<T> result;
  try {
    result.emplace(f());
  } catch (...) {
    c.undo_move(p, *token);
    throw;
  }
  c.undo_move(p, *token);
  return std::move(*result);
}
// B2 experiment: native inner batches need no heap-owned executable move.
// The adapter checks the same owner/frame/ordinal certificate as materialization.
template <class F>
auto with_native_batch_move(const ChessCore &c, Position &p,
                            const detail::NativeMoveBatch &batch,
                            std::size_t ordinal, F f) {
  detail::NativeUndoFrame frame;
  detail::make_native_search_batch_move(c, p, batch, ordinal, frame);
  using T = decltype(f());
  std::optional<T> result;
  try {
    result.emplace(f());
  } catch (...) {
    detail::undo_native_search_move(c, p, frame);
    throw;
  }
  detail::undo_native_search_move(c, p, frame);
  return std::move(*result);
}

bool same_snapshot(const ChessEvalPosition &a, const ChessEvalPosition &b) {
  // Compare fields, never padding bytes.
  return std::equal(std::begin(a.bitboards), std::end(a.bitboards),
                    std::begin(b.bitboards)) &&
         a.side_to_move == b.side_to_move &&
         a.castling_rights == b.castling_rights &&
         a.en_passant_square == b.en_passant_square &&
         a.halfmove_clock == b.halfmove_clock &&
         a.fullmove_number == b.fullmove_number;
}
} // namespace
RepetitionSignature repetition_signature(const ChessCore &c,
                                         const Position &p) {
  auto s = c.snapshot(p);
  RepetitionSignature r;
  std::copy(s->squares().begin(), s->squares().end(), r.pieces.begin());
  r.side = s->side_to_move();
  r.castling = {s->can_castle_kingside(Side::white),
                s->can_castle_queenside(Side::white),
                s->can_castle_kingside(Side::black),
                s->can_castle_queenside(Side::black)};
  if (auto ep = s->en_passant_square()) {
    for (const auto &m : ordered_moves(c, p)) {
      auto from = unsigned(m.text[0] - 'a') + 8U * unsigned(m.text[1] - '1');
      auto to = unsigned(m.text[2] - 'a') + 8U * unsigned(m.text[3] - '1');
      auto piece = r.pieces[from];
      if (to == *ep && m.text[0] != m.text[2] &&
          r.pieces[to] == PieceCode::empty &&
          (piece == PieceCode::white_pawn || piece == PieceCode::black_pawn)) {
        r.legal_ep = ep;
        break;
      }
    }
  }
  return r;
}
GameHistory GameHistory::starting_at(const ChessCore &c, const Position &p,
                                     bool complete) {
  GameHistory h;
  h.occurrences_.push_back(repetition_signature(c, p));
  h.last_ = evaluation_snapshot(*c.snapshot(p));
  h.complete_ = complete;
  return h;
}
bool GameHistory::matches_root(const ChessCore &c, const Position &p) const {
  return !occurrences_.empty() &&
         same_snapshot(last_, evaluation_snapshot(*c.snapshot(p))) &&
         occurrences_.back() == repetition_signature(c, p);
}
GameHistory GameHistory::after_move(const ChessCore &c, const Position &current,
                                    std::string_view text) const {
  if (!matches_root(c, current))
    throw std::invalid_argument("history does not end at current position");
  auto p = c.clone(current);
  auto m = c.decode_move(*p, text);
  auto token = c.make_move(*p, *m);
  auto h = *this;
  h.occurrences_.push_back(repetition_signature(c, *p));
  h.last_ = evaluation_snapshot(*c.snapshot(*p));
  return h;
}
GameHistory GameHistory::replay(const ChessCore &c, const Position &start,
                                const std::vector<std::string> &moves,
                                bool complete) {
  auto p = c.clone(start);
  auto h = starting_at(c, *p, complete);
  for (const auto &text : moves) {
    h = h.after_move(c, *p, text);
    auto m = c.decode_move(*p, text);
    auto token = c.make_move(*p, *m);
  }
  return h;
}
namespace {
// A prepared node owns one legal list and its position-specific facts.
enum class PreparationMode { eager, native_claim_free_leaf, native_horizon_batch, native_inner_batch, native_inner_deferred };
enum class PreparationPurpose { eager, search_horizon, search_inner };
struct PreparedNode {
  std::uint64_t audit_prepare_ns=0;
  std::unique_ptr<const PositionSnapshot> snapshot;
  std::vector<OrderedMove> legal;
  // Exact count for deferred inner batches; 0/1 availability for claim-free leaves.
  std::size_t native_legal_count = 0;
  PreparationMode mode = PreparationMode::eager;
  RepetitionSignature repetition;
  detail::NativeSearchMetadata native_meta{};
  bool claims_ready = false;
  bool claim_now_threefold = false;
  bool claim_now_fifty = false;
  detail::NativeMoveBatchOwner batch;
  H2C11Candidates h2c11{};
  std::size_t legal_count() const {
    return (mode == PreparationMode::native_claim_free_leaf ||
            mode == PreparationMode::native_inner_deferred) ? native_legal_count : legal.size();
  }
};
// Stack-only compatibility adapter for existing snapshot consumers. It is
// created after the node reaches its destination, never stored in the node,
// never points to the live board, and is never returned outside traversal.
class PreparedView final : public PositionSnapshot {
    const PreparedNode& n;
public:
    explicit PreparedView(const PreparedNode& node) noexcept : n(node) {}
    std::span<const PieceCode,64> squares() const noexcept override {
        return n.snapshot ? n.snapshot->squares() : std::span<const PieceCode,64>(n.repetition.pieces);
    }
    Side side_to_move() const noexcept override {
        return n.snapshot ? n.snapshot->side_to_move() : n.repetition.side;
    }
    bool can_castle_kingside(Side s) const noexcept override {
        return n.snapshot ? n.snapshot->can_castle_kingside(s) : n.repetition.castling[s==Side::white?0:2];
    }
    bool can_castle_queenside(Side s) const noexcept override {
        return n.snapshot ? n.snapshot->can_castle_queenside(s) : n.repetition.castling[s==Side::white?1:3];
    }
    std::optional<std::uint8_t> en_passant_square() const noexcept override {
        return n.snapshot ? n.snapshot->en_passant_square() : n.native_meta.raw_ep;
    }
    std::uint32_t halfmove_clock() const noexcept override {
        return n.snapshot ? n.snapshot->halfmove_clock() : n.native_meta.half;
    }
    std::uint32_t fullmove_number() const noexcept override {
        return n.snapshot ? n.snapshot->fullmove_number() : n.native_meta.full;
    }
    std::uint32_t schema_version() const noexcept override {
        return n.snapshot ? n.snapshot->schema_version() : position_snapshot_schema_version;
    }
};
RepetitionSignature snapshot_repetition(const PositionSnapshot &s) {
  RepetitionSignature r;
  std::copy(s.squares().begin(), s.squares().end(), r.pieces.begin());
  r.side = s.side_to_move();
  r.castling = {s.can_castle_kingside(Side::white),
                s.can_castle_queenside(Side::white),
                s.can_castle_kingside(Side::black),
                s.can_castle_queenside(Side::black)};
  return r;
}
bool history_contains(std::span<const RepetitionSignature> history,
                      const RepetitionSignature &child,
                      const CancellationToken *token) {
  if (token) detail::tt_poll(*token);
  for (const auto &entry : history) {
    if (token) detail::tt_poll(*token);
    if (entry == child) return true;
  }
  return false;
}
// S1.11a: retain a native snapshot allocation per child recursion slot.
using S111SnapshotOwner=std::unique_ptr<const PositionSnapshot>;
S111SnapshotOwner s111_snapshot(const ChessCore &c,const Position &p,
                                S111SnapshotOwner *spare) {
  /* J_START */
  const bool reused=spare && detail::native_refresh_search_snapshot(c,p,*spare);
  auto result=reused?std::move(*spare):c.snapshot(p);
  /* J_END */
  if(reused && s17c_verify()) {
    const auto ref=c.snapshot(p);
    const auto &a=*result;const auto &b=*ref;
    bool same=std::equal(a.squares().begin(),a.squares().end(),b.squares().begin()) &&
       a.side_to_move()==b.side_to_move() && a.en_passant_square()==b.en_passant_square() &&
       a.halfmove_clock()==b.halfmove_clock() && a.fullmove_number()==b.fullmove_number() &&
       a.schema_version()==b.schema_version();
    for(auto side:{Side::white,Side::black})same=same &&
      a.can_castle_kingside(side)==b.can_castle_kingside(side) &&
      a.can_castle_queenside(side)==b.can_castle_queenside(side);
    if(!same)throw std::logic_error("S1.11a recycled snapshot mismatch");
  }
  return result;
}
// Only recursive native frames in the accepted mode use direct identity.
// Root, generic, and other experimental modes retain detached snapshots.
void s112_prepare_identity(PreparedNode& node,const ChessCore& c,const Position& p,
                           S111SnapshotOwner* spare) {
    if (spare && native_chess_core(c)) {
        /* S112_START */
        detail::native_read_search_identity(c,p,node.repetition.pieces,
            node.repetition.side,node.repetition.castling,node.native_meta);
        /* S112_END */
    } else {
        node.snapshot=s111_snapshot(c,p,spare);
    }
}
PreparedNode prepare_node(const ChessCore &c, const Position &p,
                          const CancellationToken *token = nullptr,
                          std::optional<std::span<const RepetitionSignature>> leaf_history = {},
                          bool tt_search_horizon = false,
                          bool parent_has_duplicate = false,
                          bool native_search_inner = false,
                          std::vector<OrderedMove> *recycled = nullptr,
                          S111SnapshotOwner *snapshot_spare = nullptr) {
  PreparedNode node;
  if (recycled) {
    assert(recycled->empty());
    node.legal.swap(*recycled); // Take capacity, no copy or allocation.
  }
  try {
  s112_prepare_identity(node,c,p,snapshot_spare);
  if (leaf_history && native_chess_core(c) &&
      !PreparedView(node).en_passant_square() && PreparedView(node).halfmove_clock() < 99) {
    if(node.snapshot) node.repetition = snapshot_repetition(*node.snapshot);
    if (token) detail::tt_poll(*token);
    if (!parent_has_duplicate && !history_contains(*leaf_history, node.repetition, token)) {
      // C4.2: certified claim-free horizon needs existence, not an exact
      // count. Pinned native library's ONE_MOVE_ONLY generator returns after
      // finding its first legal move. 0/1 is a presence sentinel used only
      // for terminal classification, never for deferred batch expansion.
      if (token) detail::tt_poll(*token);
      node.native_legal_count = native_has_any_legal_move(c, p) ? 1U : 0U;
      if (token) detail::tt_poll(*token); // Including zero-move terminals.
      // Poll once after positive availability; the faster ONE_MOVE_ONLY
      // generation does not provide per-member callbacks. The original
      // full-list path remains responsible for per-member polling elsewhere.
      if (node.native_legal_count && token) detail::tt_poll(*token);
      node.mode = PreparationMode::native_claim_free_leaf;
      return node;
    }
  }
  const bool native_search_horizon = tt_search_horizon || leaf_history.has_value();
  if (native_chess_core(c)) { // Include iterative root in native generator order.
    if (token) detail::tt_poll(*token);
    node.batch = detail::prepare_native_move_batch(c, p);
    if (token) detail::tt_poll(*token); // Including zero-legal terminal positions.
    batch_ordered_moves_into(node.legal, c, p, *node.batch, token, PreparedView(node), &node.h2c11);
    node.mode = native_search_horizon ? PreparationMode::native_horizon_batch
                                  : PreparationMode::native_inner_batch;
  } else {
    ordered_moves_into(node.legal, c, p, token, node.snapshot.get());
  }
  if(node.snapshot) node.repetition = snapshot_repetition(*node.snapshot);
  auto &r = node.repetition;
  if (auto ep = PreparedView(node).en_passant_square())
    for (const auto &move : node.legal)
      if (move.legal_ep) { r.legal_ep = ep; break; }
  return node;
  } catch (...) {
    // A failed prepare must not lose the retained capacity. No board
    // state, TT state, or cancellation boundary is modified here.
    if (recycled) { node.legal.clear(); recycled->swap(node.legal); }
    throw;
  }
}
// Prepare a native positive-depth node without encoding, sorting, or
// materializing its moves. The complete native legal batch is retained so
// there is no second native move-generation pass after a TT miss.
PreparedNode prepare_deferred_inner(const ChessCore &c, const Position &p,
                                   const CancellationToken *token,
                                   std::vector<OrderedMove> *recycled,
                                   S111SnapshotOwner *snapshot_spare = nullptr) {
  PreparedNode node;
  if (recycled) {
    assert(recycled->empty());
    node.legal.swap(*recycled);
  }
  try {
    s112_prepare_identity(node,c,p,snapshot_spare);
    if (token) detail::tt_poll(*token);
    node.batch = detail::prepare_native_move_batch(c, p);
    if (token) detail::tt_poll(*token);
    node.native_legal_count = detail::native_move_batch_size(*node.batch);
    if(node.snapshot) node.repetition = snapshot_repetition(*node.snapshot);
    if (auto ep = PreparedView(node).en_passant_square(); ep &&
        detail::native_has_legal_en_passant(c, p))
      node.repetition.legal_ep = ep;
    node.mode = PreparationMode::native_inner_deferred;
    ++t13a_counts.lazy_prepared;
    return node;
  } catch (...) {
    if (recycled) { node.legal.clear(); recycled->swap(node.legal); }
    throw;
  }
}
void expand_deferred_inner(PreparedNode &node, const ChessCore &c,
                           const Position &p, const CancellationToken *token) {
  assert(node.mode == PreparationMode::native_inner_deferred && node.batch);
  try {
    batch_ordered_moves_into(node.legal,c,p,*node.batch,token,PreparedView(node),&node.h2c11);
    if (node.legal.size() != node.native_legal_count)
      throw std::logic_error("T1.3a deferred native batch count drift");
    // The native legality helper must agree with the original batch-derived
    // repetition signature, including rare legal en-passant transitions.
    auto expected=snapshot_repetition(PreparedView(node));
    if (auto ep=PreparedView(node).en_passant_square())
      for (const auto &m:node.legal)
        if (m.legal_ep) { expected.legal_ep=ep; break; }
    if (!(expected == node.repetition))
      throw std::logic_error("T1.3a deferred repetition mismatch");
    node.mode=PreparationMode::native_inner_batch;
    ++t13a_counts.lazy_expanded;
  } catch (...) {
    detail::invalidate_native_move_batch(*node.batch);
    throw;
  }
}
// All recursive values AND bounds use root-relative mate distances: a terminal
// at ply P is Mate(P). This is the order-preserving coordinate transform of
// node-relative Mate(0), avoiding lossy inverse bound transforms at Mate(0).
struct Line {
  SearchScore score;
  detail::PrivateDecision action;
  detail::PackedPV pv;
  PVEnd end;
  std::string intended;
};
bool canonical_before(const detail::PrivateDecision &a, const detail::PrivateDecision &b) {
  auto rank=[](DecisionKind k){return k==DecisionKind::claim_draw ? 0
         : k==DecisionKind::claim_draw_with_intended_move ? 1 : 2;};
  return rank(a.kind)!=rank(b.kind) ? rank(a.kind)<rank(b.kind) : a.move<b.move;
}
bool canonical_before(const ChessDecision &a, const ChessDecision &b) {
  auto rank = [](DecisionKind k) {
    return k == DecisionKind::claim_draw ? 0
         : k == DecisionKind::claim_draw_with_intended_move ? 1 : 2;
  };
  return rank(a.kind) != rank(b.kind) ? rank(a.kind) < rank(b.kind)
                                     : a.move < b.move;
}
struct ReclaimPreparedMoves {
  std::optional<PreparedNode> &prepared;
  std::vector<OrderedMove> &spare;
  S111SnapshotOwner *snapshot_spare=nullptr;
  ~ReclaimPreparedMoves() noexcept {
    if (!prepared) return;
    prepared->legal.clear(); // Destroy owners before reusing capacity.
    spare.swap(prepared->legal);
    if(snapshot_spare) *snapshot_spare=std::move(prepared->snapshot);
  }
};
struct Traversal {
  const ChessCore &core;
  Position &position;
  Evaluator &evaluator;
  SearchOptions options;
  SearchStatistics &stats;
  std::vector<RepetitionSignature> history;
#if defined(CELAB_H2_POLICY) && CELAB_H2_POLICY >= 1 && CELAB_H2_POLICY <= 4
  // Place experimental fields after the six positional aggregate-initialized
  // Traversal members. Every new field is self-initialized.
  std::array<detail::SearchMoveIdentity,65> h2_killer{};
  std::array<bool,65> h2_killer_valid{};
  static bool h2_promotion(const OrderedMove &m) noexcept {
    return m.text.native() ? m.text.native_move().kind()==detail::NativeBinaryMove::promotion
                           : m.text.size()==5;
  }
#endif

  std::uint64_t s11_qnodes=0,s11_see_calls=0,s11_see_ns=0,
      s11_standpat_cutoffs=0,s11_search_cutoffs=0,s11_sorted_nodes=0,
      s11_searched=0,s11_captures=0,s11_promotions=0,s11_ep=0,
      s11_evasions=0,s11_emergency=0,s11_see_negative=0,
      s11_see_zero=0,s11_see_positive=0;
  std::array<std::uint64_t,65> s11_qhist{};
  std::uint64_t s12_candidates=0,s12_pruned=0,s12_check_protected=0;
  std::uint64_t s15_zero_pruned=0,s15_check_pruned=0,
    s15_ep_pruned=0,s15_see_recaptures=0;
  std::uint64_t s16_delta_tested=0,s16_delta_pruned=0,
    s16_delta_check_protected=0,s16_positive_pruned=0,
    s16_top_pruned=0,s16_positive_check_protected=0;
  std::uint64_t s17a_stage5=0,s17a_lazy_expansions=0,
    s17a_claim_fallbacks=0,s17a_generated=0,
    s17a_quiet_promo_generated=0,s17a_capture_promo_generated=0,
    s17a_quiet_promo_eligible=0,s17a_capture_promo_eligible=0,
    s17a_quiet_promo_searched=0,s17a_capture_promo_searched=0,
    s17a_excluded_quiet_promo=0;
  CancellationToken cancellation{};
  const CompletedDepth *previous = nullptr;
  std::vector<detail::SearchMoveIdentity> path{};
  detail::TTSession *cache = nullptr;
  // Each recursive ply owns exactly one currently active prepared list.
  // Retain capacity by ply between siblings; no globally shared storage.
  std::size_t root_history_size = history.size();
  std::array<std::vector<OrderedMove>,65> recycled_moves{};
  std::array<S111SnapshotOwner,65> recycled_snapshots{};
  bool has_duplicate = detail::initial_history_has_duplicates(history);
  detail::OrderingTree *ordering = nullptr;
  detail::OrderingTree::Index ordering_node = 0;
  int ordering_ply = 0;
  void poll() const {
    detail::tt_poll(cancellation);
  }
  std::vector<OrderedMove> &moves(PreparedNode &prepared, bool hints) {
    poll();
    auto &result = prepared.legal;
    // Iterative-deepening root preparation is reused with canonical reset.
    // Fresh lists remain canonical; previously ordered nodes may not.
    // C4.6a isolated experiment: native generator/hint/PV order is already
    // deterministic for identical histories. Do not re-sort native moves into
    // historical lexical UCI order at each visit. Keep generic adapter policy.
    if (!result.empty() && result.front().text.native()) {
      // No canonical sorting, sortedness scan, or UCI-text materialization.
      // Previous-PV, persistent-ordering and TT hints still apply below.
    } else {
      const auto by_text=[](const auto &a,const auto &b){return a.text<b.text;};
      if (!std::is_sorted(result.begin(),result.end(),by_text))
        std::sort(result.begin(),result.end(),by_text);
    }
    if (hints) {
      if (ordering) {
        ordering->prepare(ordering_node,result);
        ordering->apply(ordering_node,result,[&]{poll();});
      }
      prefer_previous(result);
    }
    return result;
  }
  void prefer_previous(std::vector<OrderedMove> &result) {
    if (previous) {
      std::vector<std::string> preferred;
      if (path.empty()) {
        for (const auto &v : previous->multipv)
          if (v.decision.kind == DecisionKind::play)
            preferred.push_back(v.decision.move);
      } else {
        const auto &pv = previous->best.principal_variation;
        if (path.size() < pv.size() &&
            std::equal(path.begin(), path.end(), pv.begin(),
                [](const auto &a,const auto &b){return a==b;}))
          preferred.push_back(pv[path.size()]);
      }
      auto dest = result.begin();
      for (const auto &text : preferred) {
        poll();
        auto found = std::find_if(dest, result.end(),
                                  [&](const auto &m) { return m.text == text; });
        if (found != result.end()) {
          std::rotate(dest, found, found + 1);
          ++dest;
        }
      }
    }
  }
  std::size_t count(const RepetitionSignature &s) const {
    poll();
    detail::Timer count_timer(detail::audit.enabled ? &detail::audit.count_ns : nullptr);
    if (detail::audit.enabled) ++detail::audit.counts;
    std::size_t n = 0;
    for (const auto &entry : history) {
      poll();
      n += entry == s;
    }
    return n;
  }
  void invalidate_inner_batch(PreparedNode &parent) noexcept {
    if (parent.mode != PreparationMode::native_inner_batch) return;
    detail::invalidate_native_move_batch(*parent.batch);
    for (auto &move : parent.legal) move.move.reset();
  }
  const Move &executable(PreparedNode &parent, OrderedMove &move) {
    if (parent.mode != PreparationMode::native_inner_batch) return move.executable();
    poll();
    if (!move.move) {
      move.move = detail::materialize_native_batch_member(
          core, position, *parent.batch, move.native_ordinal);
      poll();
    }
    return move.executable();
  }
  template <class F> auto child(PreparedNode &parent, OrderedMove &m, F f,
                                PreparationPurpose purpose = PreparationPurpose::eager) {
    poll();
    const bool track_path = previous != nullptr;
    try {
    auto visit = [&] {
      // A child inherits the parent flag until terminal() observes its count.
      // Synthetic claim-only lookahead never consumes the flag or descends.
      detail::RestoreDuplicateFlag duplicate_frame(has_duplicate);
#if defined(CHESS_LAB_ORDERING_B2)
      detail::OrderingFrame ordering_frame(ordering,ordering_node,ordering_ply,m.text,purpose==PreparationPurpose::search_inner);
#else
      detail::OrderingFrame ordering_frame(ordering,ordering_node,ordering_ply,m.text);
#endif
      if (track_path) path.push_back(m.text); // Binary for native, no UCI conversion.
      if (history.size() < root_history_size ||
          history.size() - root_history_size >= recycled_moves.size())
        throw std::logic_error("search recursion exceeds move-storage frames");
      auto &spare = recycled_moves[history.size() - root_history_size];
      std::optional<PreparedNode> prepared;
      auto *snapshot_spare=native_chess_core(core)
          ? &recycled_snapshots[history.size()-root_history_size] : nullptr;
      ReclaimPreparedMoves reclaim{prepared,spare,snapshot_spare};
      try {
        std::optional<std::span<const RepetitionSignature>> leaf_history;
        if (purpose == PreparationPurpose::search_horizon)
          leaf_history.emplace(history);
        const auto preparation_start=detail::audit.enabled?detail::audit_clock():0;
        if (t13a_mode() == 2 && purpose == PreparationPurpose::search_inner &&
            cache && cache->numeric() && native_chess_core(core))
          prepared.emplace(prepare_deferred_inner(core,position,&cancellation,&spare,snapshot_spare));
        else
          prepared.emplace(prepare_node(core, position, &cancellation, leaf_history,
              purpose == PreparationPurpose::search_horizon && cache, has_duplicate,
              purpose == PreparationPurpose::search_inner, &spare,snapshot_spare));
        if(detail::audit.enabled)prepared->audit_prepare_ns=detail::audit_clock()-preparation_start;
        history.push_back(prepared->repetition);
      } catch (...) {
        if (track_path) path.pop_back();
        throw;
      }
      using T = decltype(f(*prepared));
      std::optional<T> result;
      try {
        result.emplace(f(*prepared));
      } catch (...) {
        history.pop_back();
        if (track_path) path.pop_back();
        throw;
      }
      history.pop_back();
      if (track_path) path.pop_back();
      return std::move(*result);
    };
    if (parent.mode == PreparationMode::native_inner_batch) {
      // Match the two cancellation boundaries around original materialization.
      poll();
      poll();
      return with_native_batch_move(core, position, *parent.batch, m.native_ordinal, visit);
    }
    return with_move(core, position, executable(parent, m), visit);
    } catch (...) {
      invalidate_inner_batch(parent);
      throw;
    }
  }
  std::optional<Line> terminal(std::uint64_t ply, const PreparedNode &prepared) {
    const PreparedView s(prepared);
    // This adapter's legal list has already established terminal move
    // availability. Synthetic test adapters retain their independent status.
    auto status = native_chess_core(core)
        ? (prepared.legal_count() == 0
              ? (core.in_check(position) ? PositionStatus::checkmate
                                         : PositionStatus::stalemate)
              : PositionStatus::ongoing)
        : core.status(position);
    bool white = s.side_to_move() == Side::white;
    auto terminal = [](SearchScore score, TerminalOutcome outcome) {
      return Line{score,
                  {DecisionKind::terminal, {}, outcome},
                  {},
                  PVEnd::terminal,
                  {}};
    };
    if (status == PositionStatus::checkmate)
      return terminal(
          white ? SearchScore::black_mate(ply) : SearchScore::white_mate(ply),
          white ? TerminalOutcome::black_win : TerminalOutcome::white_win);
    if (status == PositionStatus::stalemate ||
        core.known_dead_position(position))
      return terminal(SearchScore::finite(0), TerminalOutcome::draw);
    const auto occurrences = [&] {
      if (prepared.mode == PreparationMode::native_claim_free_leaf) {
        poll(); // Preserve cancellation at the original counting boundary.
        return std::size_t{1}; // Certified absent before this child was pushed.
      }
      return count(history.back());
    }();
    has_duplicate = has_duplicate || occurrences >= 2;
    // Earlier mate/stalemate/dead returns cannot annotate or descend.
    if (occurrences >= 5 || s.halfmove_clock() >= 150)
      return terminal(SearchScore::finite(0), TerminalOutcome::draw);
    return {};
  }
  void annotate_claims(PreparedNode &prepared) {
    if (prepared.claims_ready || !native_chess_core(core)) return;
    prepared.claims_ready = true;
    const PreparedView s(prepared);
    prepared.claim_now_threefold = count(history.back()) >= 3;
    prepared.claim_now_fifty = s.halfmove_clock() >= 100;
    if (prepared.claim_now_threefold || prepared.claim_now_fifty) return;
    // Distinct supplied history and this clock exclude all native claims.
    if (!has_duplicate && s.halfmove_clock() < 99) return;
    // For an intended threefold claim, the destination must already occur
    // twice. Avoid all hypothetical child work when this is impossible.
    const bool repeated_target = has_duplicate;
    std::optional<detail::RepeatedClaimTargets> targets;
    if (repeated_target)
      targets.emplace(history, prepared.repetition, [&] { poll(); });
    for (auto &m : prepared.legal) {
      poll();
      m.claim_fifty = s.halfmove_clock() == 99 && !m.pawn_move && !m.capture;
      if (repeated_target) {
        if (!targets->possible(prepared.repetition, m.text, m.legal_ep, m.capture,
                               [&] { poll(); }) &&
            detail::native_try_skip_claim_probe(core, position, prepared.batch.get(),
                                               m.native_ordinal, m.move.get()))
          continue;
        // Read-only lookahead. Never insert a hypothetical occurrence into
        // the actual history or the table's history-interning structure.
        std::unique_ptr<Move> materialized;
        const bool batched = prepared.mode == PreparationMode::native_horizon_batch;
        if (batched) {
          poll();
          materialized = detail::materialize_native_batch_member(
              core, position, *prepared.batch, m.native_ordinal);
          poll();
        }
        try {
          m.claim_threefold = with_move(core, position,
              batched ? *materialized : executable(prepared, m), [&] {
            // This once-per-prepared-node pass retains eligibility on the
            // move through every later reorder. Hypothetical identities do
            // not require child move lists or affect actual history/TT state.
            auto snapshot = core.snapshot(position);
            auto signature = snapshot_repetition(*snapshot);
            poll();
            if (auto ep = snapshot->en_passant_square(); ep &&
                detail::native_has_legal_en_passant(core, position))
              signature.legal_ep = ep;
            poll();
            return count(signature) >= 2;
          });
        } catch (...) {
          if (batched) detail::invalidate_native_move_batch(*prepared.batch);
          invalidate_inner_batch(prepared);
          throw;
        }
      }
    }
  }
  std::optional<Line> claim(PreparedNode &prepared) {
    poll();
    const PreparedView s(prepared);
    if (native_chess_core(core) && !has_duplicate && s.halfmove_clock() < 99)
      return {};
    auto &canonical_moves = prepared.legal;
    auto occurrences = native_chess_core(core)
        ? (prepared.claim_now_threefold ? 3U : 0U) : count(history.back());
    std::optional<Line> best;
    if (occurrences >= 3 || (native_chess_core(core)
                                 ? prepared.claim_now_fifty
                                 : s.halfmove_clock() >= 100))
      best = Line{SearchScore::finite(0),
                  {DecisionKind::claim_draw, {}},
                  {},
                  PVEnd::claim_draw,
                  {}};
    else
      for (auto &m : canonical_moves) {
        poll();
        // Preserve synthetic-adapter custom clock behavior: only the native
        // rules adapter derives intended claims from prepared metadata.
        bool eligible = native_chess_core(core)
            ? (m.claim_threefold || m.claim_fifty)
            : child(prepared, m, [&](PreparedNode &next) {
                return count(history.back()) >= 3 ||
                       PreparedView(next).halfmove_clock() >= 100;
              });
        if (eligible) {
          // Native generator order governs traversal, but the established
          // public tie-break among eligible draw claims remains lexical.
          if (!best || m.text < best->action.move)
            best = Line{SearchScore::finite(0),
                        {DecisionKind::claim_draw_with_intended_move, m.text.str()},
                        {},
                        PVEnd::claim_draw_with_intended_move,
                        m.text.str()};
          // Preserve generic adapters' exact first-eligible claim behavior.
          if (!native_chess_core(core)) break;
        }
      }
    return best;
  }

  // S1.0 reconstruction: tactical leaf stability, NOT a second main search.
  Line s11_quiesce(std::uint64_t ply, PreparedNode &prepared,
                   std::optional<SearchScore> alpha,
                   std::optional<SearchScore> beta,unsigned qply) {
    poll();
    ++stats.visited_nodes;
    ++s11_qnodes;
    if(qply<s11_qhist.size())++s11_qhist[qply];
    if(auto end=terminal(ply,prepared))return std::move(*end);
    if(prepared.legal_count()==0)
      throw std::runtime_error("S1 ongoing node without legal moves");
    const PreparedView s(prepared);
    const bool white=s.side_to_move()==Side::white;
    const bool checked=core.in_check(position);
    if(!native_chess_core(core)||prepared.mode!=PreparationMode::native_claim_free_leaf)
      annotate_claims(prepared);
    std::optional<Line> best=claim(prepared);
    // Material-unit pruning is certified only for the accepted default material
    // identity/configuration. Other evaluators still receive SEE ordering and
    // all tactical continuations/evasions, without material-unit pruning.
    const unsigned s16_mode=s16_default_material(evaluator.identity()) ? 17U : 0U;
    std::optional<double> s16_static;
    auto better=[&](SearchScore a,SearchScore b){return white?a>b:a<b;};
    auto update=[&] {
      if(!options.alpha_beta_enabled||!best)return false;
      if(white) {if(!alpha||best->score>*alpha)alpha=best->score;}
      else {if(!beta||best->score<*beta)beta=best->score;}
      return alpha&&beta&&*beta<=*alpha;
    };
    // qply>=64 is the emergency static approximation, including if in check.
    // Ordinary in-check nodes NEVER stand-pat (nor omit quiet evasions).
    if(!checked || ply>=64) {
      poll();++stats.evaluator_calls;
      const double raw=evaluator.evaluate(s110_eval_snapshot(core,position,s));
      if(s16_delta_mode(s16_mode))s16_static=raw;
      auto score=SearchScore::finite(raw);
      if(!best||better(score,best->score))
        best=Line{score,{DecisionKind::play,{}},{},PVEnd::horizon,{}};
      if(ply>=64)++s11_emergency;
      if(update()) {++stats.alpha_beta_cutoffs;++s11_standpat_cutoffs;return *best;}
    }
    if(ply>=64 || (!checked&&qply>=8)) return *best;
    // S1.7a: only after ALL terminal/standpat/claim/depth exits, create
    // search candidates. A certified claim-free native leaf used ONE_MOVE_ONLY
    // up to this point.  In-check nodes need all legal evasions; in ordinary
    // nodes either CAPTURE or TACTICAL generation is sufficient.
    if(s17a_trace())++s17a_stage5;
    if(s17a_lazy() && prepared.mode==PreparationMode::native_claim_free_leaf) {
      prepared.batch=detail::prepare_native_qsearch_batch(
          core,position,checked,s17a_include_quiet_promotion());
      batch_ordered_moves_into(prepared.legal,core,position,*prepared.batch,
          &cancellation,s,nullptr);
      // Existing binary batch execution avoids heap Move construction.
      prepared.mode=PreparationMode::native_inner_batch;
      if(s17a_trace())++s17a_lazy_expansions;
    } else if(s17a_lazy() &&
              prepared.mode==PreparationMode::native_horizon_batch) {
      // Claim-/EP-sensitive horizon nodes already have a FULL native batch,
      // with valid native ordinals but NO materialized Move objects. Their
      // terminal/claim handling has completed above. The recursive QS child
      // executor accepts native_inner_batch; leaving native_horizon_batch
      // here would wrongly enter executable(parent,m) and throw.
      if(!prepared.batch)
        throw std::logic_error("S1.7a native horizon fallback missing batch");
      prepared.mode=PreparationMode::native_inner_batch;
      if(s17a_trace())++s17a_claim_fallbacks;
    } else if(s17a_lazy() && s17a_trace()) {
      // Synthetic adapters still use the old eager path with boxed Move.
      ++s17a_claim_fallbacks;
    }
    // Candidate ordering remains the same as filtering full legal moves;
    // checked nodes keep ALL evasions (including quiet promotions).
    std::vector<std::size_t> eligible;
    eligible.reserve(prepared.legal.size());
    for(std::size_t i=0;i<prepared.legal.size();++i) {
      const auto &m=prepared.legal[i];
      const bool promotion=m.text.size()==5;
      if(s17a_trace()) {
        ++s17a_generated;
        if(promotion) {
          if(m.capture)++s17a_capture_promo_generated;
          else ++s17a_quiet_promo_generated;
        }
      }
      const bool allowed=checked || m.capture ||
          (promotion && s17a_include_quiet_promotion());
      if(allowed) {
        eligible.push_back(i);
        if(s17a_trace() && promotion) {
          if(m.capture)++s17a_capture_promo_eligible;
          else ++s17a_quiet_promo_eligible;
        }
      } else if(s17a_trace() && promotion && !m.capture) {
        ++s17a_excluded_quiet_promo;
      }
    }
    // Index-only sorting preserves PreparedNode native certificates and
    // original list order for exact SEE ties. Quiet evasions always survive.
    if(s11_see_order_enabled() && (eligible.size()>1 || s16_mode!=0)) {
      struct Entry {std::size_t i;int priority;};
      S17FNode see_node;
      S17HNode check_node;
      std::vector<Entry> rank;
      rank.reserve(eligible.size());
      for(auto i:eligible) {
        poll();const auto &m=prepared.legal[i];
        int priority=-100001; // Quiet evasion stays behind tactical moves.
        if(m.capture||m.text.size()==5) {
          // Delta pruning is deliberately BEFORE the potentially expensive
          // SEE calculation. No check evasion, promotion, EP, or checking
          // capture is discarded by this new rule.
          if(s16_delta_mode(s16_mode) && s16_static) {
            const auto &active_bound=white?alpha:beta;
            if(active_bound && active_bound->kind()==SearchScore::Kind::finite &&
               s16_delta_reject(s16_mode,checked,m.capture,m.legal_ep,
                 m.text.size()==5,white,*s16_static,active_bound->value(),
                 0.25*s11_value(s.squares()[m.text.to()]))) {
              ++s16_delta_tested;
              if(check_node.check(s,m,core,position,0))++s16_delta_check_protected;
              else {++s16_delta_pruned;continue;}
            }
          }
          auto t0=s11_trace_enabled()?std::chrono::steady_clock::now():
                 std::chrono::steady_clock::time_point{};
          if(s11_trace_enabled())s15_see_step_sink=&s15_see_recaptures;
          priority=see_node.see(s,m,core,position);
          s15_see_step_sink=nullptr;
          ++s11_see_calls;
          if(priority<0)++s11_see_negative;
          else if(priority==0)++s11_see_zero;
          else ++s11_see_positive;
          if(s11_trace_enabled())s11_see_ns+=static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                 std::chrono::steady_clock::now()-t0).count());
        }
        if(s12_prune_candidate(s16_mode,qply,checked,m,priority)) {
          ++s12_candidates;
          // Accepted SEE pruning does not protect checking captures; the
          // delta rule above does. Classify pruned checks only in diagnostics.
          {
            if(priority==0) ++s15_zero_pruned;
            if(m.legal_ep) ++s15_ep_pruned;
            if(s11_trace_enabled() &&
               check_node.check(s,m,core,position,1)) ++s15_check_pruned;
            ++s12_pruned;
            continue;
          }
        }
        rank.push_back({i,priority});
      }
      std::stable_sort(rank.begin(),rank.end(),[](const auto &a,const auto &b){
        return a.priority>b.priority;});

      eligible.resize(rank.size());
      for(std::size_t j=0;j<rank.size();++j)eligible[j]=rank[j].i;
      ++s11_sorted_nodes;
    }
    for(std::size_t j=0;j<eligible.size();++j) {
      poll();auto &m=prepared.legal[eligible[j]];
      ++s11_searched;
      if(m.capture)++s11_captures;
      if(m.text.size()==5) {
        ++s11_promotions;
        if(s17a_trace()) {
          if(m.capture)++s17a_capture_promo_searched;
          else ++s17a_quiet_promo_searched;
        }
      }
      if(m.legal_ep)++s11_ep;
      if(checked)++s11_evasions;
      auto line=child(prepared,m,[&](PreparedNode &next) {
        return s11_quiesce(ply+1,next,alpha,beta,qply+1);
      },s17a_lazy()?PreparationPurpose::search_horizon:PreparationPurpose::eager);
      if(!best||better(line.score,best->score)||
         (line.score==best->score&&!m.text.native()&&
           canonical_before({DecisionKind::play,m.text},best->action))) {
        line.action={DecisionKind::play,m.text};
        line.pv.prepend(m.text);
        best=std::move(line);
      }
      if(update()&&j+1<eligible.size()) {
        ++stats.alpha_beta_cutoffs;++s11_search_cutoffs;break;
      }
    }
    if(!best)throw std::logic_error("S1 qsearch lacks standpat/evasions/claim");
    return *best;
  }
  // exact=true is used only with an unbounded window. An ordered child can
  // return a bound on equality. Re-search promising/canonically earlier ties
  // with a full window and exact PV before letting them become publishable.
  Line node(int depth, std::uint64_t ply, PreparedNode &prepared,
            std::optional<SearchScore> alpha, std::optional<SearchScore> beta,
            bool exact = false,
            bool root_play_only = false) {
    poll();
    if(depth==0 && s11_quiescence_enabled())
      return s11_quiesce(ply,prepared,alpha,beta,0);
    const auto audit_node_start=detail::audit.enabled?detail::audit_clock():0;
    std::uint64_t audit_preprobe_ns=0;
    // Outside the previous-PV prefix all descendants retain M3's canonical
    // order, so a full-window traversal already supplies its canonical PV.
    // Only a subtree that can actually receive a hint needs tie refinement.
    if (exact && !cache && (!ordering || !ordering->can_reorder(ordering_node))) {
      const auto *pv = previous ? &previous->best.principal_variation : nullptr;
      exact = pv && path.size() < pv->size() &&
              std::equal(path.begin(), path.end(), pv->begin());
    }
    ++stats.visited_nodes;
    const bool raw_leaf = prepared.mode == PreparationMode::native_claim_free_leaf;
    assert(!raw_leaf || (depth == 0 && native_chess_core(core)));
    assert(prepared.mode != PreparationMode::native_horizon_batch ||
           (depth == 0 && native_chess_core(core) && prepared.batch));
    assert(prepared.mode != PreparationMode::native_inner_batch ||
           (depth > 0 && native_chess_core(core) && prepared.batch));
    assert(prepared.mode != PreparationMode::native_inner_deferred ||
           (depth > 0 && native_chess_core(core) && prepared.batch));
    const auto original_alpha = alpha, original_beta = beta;
    const PreparedView view(prepared);
    const auto *s = &view;
    // This cache is scoped to a single node; children cannot overwrite it.
    // A TT hash is computed only if this node actually probes or stores.
    std::optional<std::uint64_t> node_tt_hash;
    // Compute only on a real TT operation, once per node. At each call the
    // current board is this frame's board (children have been undone).
    // Synthetic adapters retain TTSession's exact-signature hash fallback.
    auto tt_hash_for_node = [&]() -> std::optional<std::uint64_t>* {
      if (!node_tt_hash && cache->enabled() && native_chess_core(core)) {
        detail::Timer timer(detail::audit.enabled?&detail::audit.hash_ns:nullptr);
        if(detail::audit.enabled)++detail::audit.hashes;
        node_tt_hash = detail::native_tt_repetition_key(core,position,history.back().legal_ep);
      }
      return &node_tt_hash;
    };
    auto finish = [&](Line line) {
      if (ordering && depth > 0) ordering->seal(ordering_node);
      if (cache && !root_play_only && raw_leaf && !cache->numeric()) poll(); // Preserve hint-only skipped store boundary.
      // T1.1d.2 C: depth-zero numeric scores are not written to the TT.
      // Keep existing positive-depth and hint-only storage semantics.
      if (cache && !root_play_only && (!raw_leaf || cache->numeric()) &&
          (!cache->numeric() || depth > 0)) {
        poll();
        cache->store(history.back(), depth, line.score,
          detail::classify_bound(line.score, original_alpha, original_beta),
          line.action, ply, *s, tt_hash_for_node());
      }
      return line;
    };
    // On native inner nodes mode 2 tests the terminal/draw guard before
    // expanding the already-generated native batch. Only a provably
    // claim-free node may return directly from the early numeric TT probe.
    bool terminal_checked = false;
    std::optional<detail::TTProbe> preprobe;
    if (prepared.mode == PreparationMode::native_inner_deferred) {
      if (auto end = terminal(ply,prepared)) return finish(std::move(*end));
      terminal_checked = true;
      if (prepared.legal_count() == 0)
        throw std::runtime_error("ongoing node has no legal moves");
      if (!root_play_only && cache && cache->numeric() &&
          !has_duplicate && s->halfmove_clock() < 99) {
        if (!evaluator.usable()) throw EvaluationError("evaluator is quarantined");
        poll();
        if(detail::audit.enabled)audit_preprobe_ns=detail::audit_clock()-audit_node_start;
        preprobe.emplace(cache->probe(history.back(),depth,ply,*s,tt_hash_for_node()));
        ++t13a_counts.early_probes;
        poll();
        if (preprobe->score && !exact) {
          const bool exact_return=preprobe->bound==detail::TTBound::exact;
          const bool bound_return=!exact_return &&
            ((preprobe->bound==detail::TTBound::lower && beta && *preprobe->score >= *beta) ||
             (preprobe->bound==detail::TTBound::upper && alpha && *preprobe->score <= *alpha));
          if (exact_return || bound_return) {
            // Terminal adjudication and actual repetition-count updates already
            // happened above; available claims have been proven absent.
            // C4.5: the private TT can now retain a native 16-bit hint.
            // An early numeric return must account for that hint just as the
            // ordinary TT probe path does. Do not materialize a UCI string.
            const bool legal_preprobe_hint = preprobe->native_hint
                ? (native_chess_core(core) && detail::native_has_legal_move_identity(
                       core, position, preprobe->native_hint->native_bits()))
                : (!preprobe->hint.empty() &&
                   detail::native_has_legal_uci_hint(core, position, preprobe->hint));
            if (legal_preprobe_hint)
              ++stats.transposition.hint_hits;
            if (ordering) ordering->prepare(ordering_node,prepared.legal);
            if (bound_return) {
              ++stats.alpha_beta_cutoffs;
              ++stats.transposition.bound_cutoffs;
              ++t13a_counts.early_bound_returns;
            } else ++t13a_counts.early_exact_returns;
            if (detail::audit.enabled) {
              auto &row=detail::audit.depth[detail::bin(depth)];
              if (bound_return) ++row.bound_returns; else ++row.exact_returns;
              row.preprobe_return_ns+=audit_preprobe_ns;
              row.prepare_return_ns+=prepared.audit_prepare_ns;
            }
            cache->reward(*preprobe);
            return finish({*preprobe->score, preprobe->internal_decision.value(), {}, PVEnd::horizon, {}});
          }
        }
        ++t13a_counts.early_nonreturns;
      }
      expand_deferred_inner(prepared,core,position,&cancellation);
    }
    const bool defer_order = t13a_mode() != 0 && depth > 0 &&
        cache && cache->numeric() && !root_play_only;
    if (defer_order) ++t13a_counts.deferred_order_nodes;
    // Leaves are canonical already; positive-depth ordering can be postponed
    // until a numeric hit is known to be unusable.
    if (depth == 0) poll();
    auto &moves_here = depth == 0 ? prepared.legal :
        (defer_order ? prepared.legal : moves(prepared, false));
    if (!terminal_checked) {
      if (auto end = terminal(ply, prepared)) return finish(std::move(*end));
    }
    if (prepared.legal_count() == 0)
      throw std::runtime_error("ongoing node has no legal moves");
    if (!root_play_only && !raw_leaf) annotate_claims(prepared);
    auto best = root_play_only || raw_leaf ? std::optional<Line>{} : claim(prepared);
    if (depth > 0 && !defer_order) {
      if (ordering) {
        ordering->prepare(ordering_node,moves_here);
        ordering->apply(ordering_node,moves_here,[&]{poll();});
      }
      prefer_previous(moves_here);
    }
    std::optional<detail::SearchMoveIdentity> deferred_hint;
#if defined(CELAB_H2_POLICY) && CELAB_H2_POLICY >= 1 && CELAB_H2_POLICY <= 4
    bool h2_legal_tt_hint=false;
#endif
    auto tt_finish=[&](Line line) {
      if (defer_order) {
        ++t13a_counts.order_skipped_returns;
        if (ordering) ordering->prepare(ordering_node,moves_here);
      }
      return finish(std::move(line));
    };
    if (raw_leaf && cache && !root_play_only && !cache->numeric()) {
      if (!evaluator.usable()) throw EvaluationError("evaluator is quarantined");
      poll();
    }
    // T1.1d.2: depth-zero numeric TT probes cannot avoid recursive search.
    // Hint-only and all positive-depth lookup behavior remain unchanged.
    if (cache && !root_play_only && (!raw_leaf || cache->numeric()) &&
        (!cache->numeric() || depth > 0)) {
      if (!evaluator.usable()) throw EvaluationError("evaluator is quarantined");
      poll();
      if(detail::audit.enabled)audit_preprobe_ns=detail::audit_clock()-audit_node_start;
      auto hit = preprobe ? std::move(*preprobe) :
          cache->probe(history.back(), depth, ply, *s, tt_hash_for_node());
      if (!hit.hint.empty() || hit.native_hint) {
        if (raw_leaf) {
          // The table may still report a move hint for a previously searched
          // position. Preserve the exact legal-hint statistic without constructing
          // a complete boxed/sorted/encoded batch. Depth zero never reorders.
          if (hit.native_hint
              ? (native_chess_core(core) && detail::native_has_legal_move_identity(
                    core,position,hit.native_hint->native_bits()))
              : detail::native_has_legal_uci_hint(core, position, hit.hint))
            ++stats.transposition.hint_hits;
        } else {
          auto found = std::find_if(moves_here.begin(), moves_here.end(), [&](const auto &m) {
            return hit.native_hint ? m.text == *hit.native_hint : m.text == hit.hint;
          });
          if (found != moves_here.end()) {
#if defined(CELAB_H2_POLICY) && CELAB_H2_POLICY >= 1 && CELAB_H2_POLICY <= 4
            h2_legal_tt_hint=true;
#endif
            if (defer_order) deferred_hint=hit.native_hint
                ? *hit.native_hint : detail::SearchMoveIdentity(hit.hint);
            else if (depth > 0) std::rotate(moves_here.begin(), found, found + 1);
            ++stats.transposition.hint_hits;
          }
        }
      }
      poll();
      if (hit.score) {
        // Current, legally available draw claims take precedence over a
        // history-independent losing estimate. This applies to both exact
        // scores and alpha-beta bounds, and never trusts a cached claim action.
        const bool claim_dominates = best &&
          detail::cached_score_worse_than_claim(*hit.score, s->side_to_move());
        auto &audit_row=detail::audit.depth[detail::bin(depth)];
        auto audit_return=[&](bool bound) {
          if(!detail::audit.enabled)return;
          if(bound)++audit_row.bound_returns;else ++audit_row.exact_returns;
          audit_row.preprobe_return_ns+=audit_preprobe_ns;
          audit_row.prepare_return_ns+=prepared.audit_prepare_ns;
        };
        if(detail::audit.enabled && claim_dominates)++audit_row.claim_veto;
        if (hit.bound == detail::TTBound::exact) {
          if (claim_dominates) return tt_finish(*best);
          if (best && hit.internal_decision == std::optional<detail::PrivateDecision>(best->action))
            return tt_finish(*best); // exact zero, claim eligibility revalidated
          if (!exact || (depth == 0 && hit.internal_decision && hit.internal_decision->kind == DecisionKind::play)) {
            audit_return(false);
            cache->reward(hit); // actual numeric TT return, not just candidate hit
            return tt_finish({*hit.score, hit.internal_decision.value(), {}, PVEnd::horizon, {}});
          }
          if(detail::audit.enabled)++audit_row.pv_research;
          ++stats.transposition.pv_researches;
        } else if (!exact && !claim_dominates &&
            ((hit.bound == detail::TTBound::lower && beta && *hit.score >= *beta) ||
             (hit.bound == detail::TTBound::upper && alpha && *hit.score <= *alpha))) {
          ++stats.alpha_beta_cutoffs;
          ++stats.transposition.bound_cutoffs;
          audit_return(true);
          cache->reward(hit); // bound genuinely cut off the node
          return tt_finish({*hit.score, hit.internal_decision.value(), {}, PVEnd::horizon, {}});
        } else if(hit.bound!=detail::TTBound::exact && detail::audit.enabled && !claim_dominates) {
          ++audit_row.bound_noncutoff;
        }
      }
    }
    if (defer_order) {
      moves(prepared,false);
      if (ordering) {
        ordering->prepare(ordering_node,moves_here);
        ordering->apply(ordering_node,moves_here,[&]{poll();});
      }
      prefer_previous(moves_here);
      if (deferred_hint) {
        auto found=std::find_if(moves_here.begin(),moves_here.end(),
            [&](const auto &m) { return m.text == *deferred_hint; });
        if (found!=moves_here.end())
          std::rotate(moves_here.begin(),found,found+1);
      }
    }
    bool white = s->side_to_move() == Side::white;
    auto better = [&](SearchScore a, SearchScore b) {
      return white ? a > b : a < b;
    };
    if (depth == 0) {
      poll();
      ++stats.evaluator_calls;
      auto score =
          SearchScore::finite(evaluator.evaluate(s110_eval_snapshot(core,position,*s)));
      if (!best || better(score, best->score))
        return finish({score, {DecisionKind::play, {}}, {}, PVEnd::horizon, {}});
      return finish(*best);
    }
    auto update = [&] {
      if (!options.alpha_beta_enabled || !best)
        return false;
      if (white) {
        if (!alpha || best->score > *alpha)
          alpha = best->score;
      } else {
        if (!beta || best->score < *beta)
          beta = best->score;
      }
      return alpha && beta && *beta <= *alpha;
    };
    if (update()) {
      ++stats.alpha_beta_cutoffs;
      return finish(*best);
    }
#if defined(CELAB_H2_POLICY) && CELAB_H2_POLICY >= 1 && CELAB_H2_POLICY <= 4
    // The new heuristic runs only AFTER B2/previous-PV/TT ordering. Treat an
    // eligible B2/PV node as hinted even if the heuristic could potentially
    // find another move; this deliberately favors safety over coverage.
    // Do not ever touch synthetic-adapter move order or depth-zero leaves.
    const bool h2_active_depth = (CELAB_H2_POLICY==2 || CELAB_H2_POLICY==4) || depth==1;
    const bool h2_prev_pv = previous && (path.empty() ?
      !previous->multipv.empty() :
      (path.size()<previous->best.principal_variation.size() &&
       std::equal(path.begin(),path.end(),previous->best.principal_variation.begin())));
    const bool h2_existing_hint = h2_legal_tt_hint || h2_prev_pv ||
      (ordering && ordering->can_reorder(ordering_node));
    if (h2_active_depth && !h2_existing_hint && moves_here.size()>1 &&
        moves_here.front().text.native()) {
      std::size_t candidate=moves_here.size();
#if CELAB_H2_POLICY == 1 || CELAB_H2_POLICY == 2
      if (ply<h2_killer_valid.size() && h2_killer_valid[ply]) {
        for(std::size_t k=0;k<moves_here.size();++k) {
          const auto &m=moves_here[k];
          if (!m.capture && !h2_promotion(m) && m.text==h2_killer[ply]) {
            candidate=k;
            break;
          }
        }
      }
#else
      // At unhinted native nodes no PV/TT/B2 reorder has invalidated the
      // generation-time indexes. A single cached lookup replaces the scan.
      const auto &h2c11=prepared.h2c11;
      const auto index=h2c11.first_promotion!=H2C11Candidates::missing
          ? h2c11.first_promotion : h2c11.chosen_capture;
      if (index!=H2C11Candidates::missing) {
        if (index>=moves_here.size())
          throw std::logic_error("H2-C1.1 cached move index is out of range");
        candidate=index;
      }
#endif
      // Rotate a single candidate to the first slot. All other moves retain
      // native relative ordering; unlike a swap, only the selected move moves.
      if (candidate>0 && candidate<moves_here.size())
        std::rotate(moves_here.begin(),moves_here.begin()+candidate,
                    moves_here.begin()+candidate+1);
    }
#endif
    for (std::size_t i = 0; i < moves_here.size(); ++i) {
      auto &m = moves_here[i];
      poll();
      const auto child_alpha = alpha, child_beta = beta;
      bool refined = false;
      bool direct_exact = exact && (!options.alpha_beta_enabled || (!alpha && !beta));
      auto line = child(prepared, m, [&](PreparedNode &next) {
        return node(depth - 1, ply + 1, next, alpha, beta, direct_exact);
      }, depth == 1 ? (s11_quiescence_enabled() ? (s17a_lazy() ? PreparationPurpose::search_horizon : PreparationPurpose::eager) : PreparationPurpose::search_horizon) : PreparationPurpose::search_inner);
      detail::PrivateDecision action{DecisionKind::play,m.text};
      // C4.6b experiment: native first-encounter tie policy. An alpha-beta
      // bound equal to the incumbent does not certify that a later lexical
      // predecessor has the same exact minimax score. Never replace on equal
      // scores for native moves; preserve synthetic canonical tie semantics.
      auto competitive = [&] {
        return !best || better(line.score, best->score) ||
               (line.score == best->score && !m.text.native() &&
                canonical_before(action, best->action));
      };
      bool improved = false;
      if (competitive()) {
        if (exact && !direct_exact) {
          refined = true;
          if (ordering) ordering->refinement();
          line = child(prepared, m, [&](PreparedNode &next) {
            return node(depth - 1, ply + 1, next, {}, {}, true);
          }, depth == 1 ? (s11_quiescence_enabled() ? (s17a_lazy() ? PreparationPurpose::search_horizon : PreparationPurpose::eager) : PreparationPurpose::search_horizon) : PreparationPurpose::search_inner);
        }
        if (competitive()) {
          line.action = std::move(action);
          line.pv.prepend(m.text);
          best = std::move(line);
          improved = true;
        }
      }
      if (ordering) {
        const bool score_exact = !options.alpha_beta_enabled || refined ||
          detail::classify_bound(line.score,child_alpha,child_beta)==detail::TTBound::exact;
        ordering->observe(ordering_node,m.text,score_exact?(improved?3:2):1,i+1);
      }
      const bool h2_should_cut = update() && i + 1 < moves_here.size();
#if defined(CELAB_H2_POLICY) && (CELAB_H2_POLICY == 1 || CELAB_H2_POLICY == 2)
      // Learn from ANY real quiet cutoff, including at hinted nodes, but do
      // not promote a learned move over a stronger PV/TT/B2 hint.
      if (h2_should_cut && ply<h2_killer_valid.size() &&
          m.text.native() && !m.capture && !h2_promotion(m)) {
        h2_killer[ply]=m.text;
        h2_killer_valid[ply]=true;
      }
#endif
      if (h2_should_cut) {
        if (ordering) {
          ordering->observe(ordering_node,m.text,4,i+1);
        }
        ++stats.alpha_beta_cutoffs;
        break;
      }
    }
    return finish(*best);
  }
};
} // namespace
SearchResult search(const ChessCore &c, const Position &root,
                    const GameHistory &h, Evaluator &e, SearchOptions options) {
  auto start = std::chrono::steady_clock::now();
  SearchResult r;
  r.options = options;
  r.history_incomplete = !h.complete();
  try {
    r.evaluator_identity = e.identity();
    if (options.nominal_depth < 1 || options.nominal_depth > 64)
      throw std::invalid_argument("nominal depth must be in [1,64]");
    if (!h.matches_root(c, root))
      throw std::invalid_argument("history/root mismatch");
    if (!e.usable())
      throw EvaluationError("evaluator is quarantined");
    auto p = c.clone(root);
    Traversal t{c, *p, e, options, r.statistics, h.occurrences()};
    auto prepared = prepare_node(c, *p);
    auto line = t.node(options.nominal_depth, 0, prepared, {}, {});
    if (t.history != h.occurrences())
      throw std::runtime_error("unbalanced search history");
    r.status = line.action.kind == DecisionKind::terminal
                   ? SearchStatus::terminal
                   : SearchStatus::complete;
    r.completed_depth =
        r.status == SearchStatus::terminal ? 0 : options.nominal_depth;
    r.decision = line.action.expand();
    r.score = line.score;
    r.principal_variation = line.pv.expand();
    r.pv_end = line.end;
    r.pv_intended_move = std::move(line.intended);
  } catch (const EvaluationError &ex) {
    r.status = SearchStatus::evaluator_failed;
    r.diagnostic = ex.what();
  } catch (const std::exception &ex) {
    r.status = SearchStatus::search_error;
    r.diagnostic = ex.what();
  } catch (...) {
    r.status = SearchStatus::search_error;
    r.diagnostic = "unknown traversal exception";
  }
  if (r.status == SearchStatus::search_error ||
      r.status == SearchStatus::evaluator_failed) {
    r.decision.reset();
    r.score.reset();
    r.principal_variation.clear();
    r.pv_end.reset();
    r.pv_intended_move.clear();
    r.completed_depth = 0;
  }
  r.statistics.elapsed = std::chrono::steady_clock::now() - start;
  return r;
}
IterativeSearchResult iterative_search(
    const ChessCore &c, const Position &root, const GameHistory &h, Evaluator &e,
    IterativeSearchOptions options, CancellationToken cancellation,
    SearchProgress progress, TranspositionTableUse table_use) {
  const auto start = std::chrono::steady_clock::now();
  IterativeSearchResult r;
  r.provenance.options = options;
  r.provenance.history_incomplete = !h.complete();
  std::optional<ChessDecision> fallback;
  auto elapsed = [&] {
    r.statistics.elapsed = std::chrono::steady_clock::now() - start;
  };
  try {
    r.provenance.evaluator_identity = e.identity();
    if (options.max_depth && (*options.max_depth < 1 || *options.max_depth > 64))
      throw std::invalid_argument("max depth must be in [1,64]");
    if (options.multipv_count < 1 || options.multipv_count > 256)
      throw std::invalid_argument("MultiPV count must be in [1,256]");
    if (!h.matches_root(c, root))
      throw std::invalid_argument("history/root mismatch");
    r.provenance.root = evaluation_snapshot(*c.snapshot(root));
    r.provenance.history = h.occurrences();
    if (!e.usable())
      throw EvaluationError("evaluator is quarantined");
    std::optional<detail::TTSession> session;
    if (options.alpha_beta_enabled && table_use.table) {
      session.emplace(table_use, e.identity().fingerprint, h.complete(),
                      r.statistics.transposition, cancellation);
      if (!session->enabled()) session.reset();
    }
    r.provenance.tt_numeric_certified = session &&
        table_use.evaluator != EvaluatorCacheCompatibility::not_certified;
    r.provenance.tt_value_policy = table_use.value_policy;
    // Compile-time selection changes only the representation. Normal callers need
    // no experimental activation scope; each iterative invocation owns its tree.
#if defined(CHESS_LAB_ORDERING_NONE)
    detail::OrderingTree ordering(nullptr);
#else
    const detail::OrderingControl defaults{detail::production_ordering_config, nullptr};
    detail::OrderingTree ordering(detail::ordering_control ? detail::ordering_control : &defaults);
#endif
    auto p = c.clone(root);
    Traversal t{c, *p, e, {1, options.alpha_beta_enabled}, r.statistics,
                h.occurrences()};
    if (ordering.enabled()) t.ordering = &ordering;
    // Validation/terminal detection and safe fallback enumeration precede
    // attaching the observer: a cancelled terminal never becomes a fallback.
    auto root_prepared = prepare_node(c, *p);
    const auto &snapshot = root_prepared.snapshot;
    if (auto line = t.terminal(0, root_prepared)) {
      ++r.statistics.visited_nodes;
      r.status = IterativeSearchStatus::terminal;
      r.terminal_decision = line->action.expand();
      r.terminal_score = line->score;
    } else {
      auto &legal = root_prepared.legal;
      if (legal.empty())
        throw std::runtime_error("ongoing root has no legal moves");
      fallback = ChessDecision{DecisionKind::play, legal.front().text.str()};
      t.cancellation = std::move(cancellation);
      t.poll();
      if (session) {
        session->start(h.occurrences());
        t.cache = &*session;
      }
      auto variation = [](Line line) {
        return SearchVariation{line.action.expand(), line.score,
             line.pv.expand(), line.end, std::move(line.intended)};
      };
      for (int depth = 1; depth <= options.max_depth.value_or(64); ++depth) {
        t.poll();
        if (t.ordering) t.ordering->begin();
        t.options.nominal_depth = depth;
        t.previous = options.previous_pv_ordering_enabled && r.completed
                         ? &*r.completed : nullptr;
        std::vector<SearchVariation> ranking;
        if (options.multipv_count == 1 && options.single_pv_play_only) {
          // Reuse exact traversal/tie refinement, sharing the incumbent window
          // across root candidates. Never probe/store this root: its optional
          // claim policy differs from ordinary (including cached) descendants.
          ranking.push_back(variation(t.node(depth, 0, root_prepared, {}, {}, true, true)));
        } else {
        ++r.statistics.visited_nodes; // legacy independent exact root ranking
        t.moves(root_prepared, false); // canonical intended claim order
        t.annotate_claims(root_prepared);
        if (auto claim = t.claim(root_prepared))
          ranking.push_back(variation(std::move(*claim)));
        auto &root_moves=t.moves(root_prepared, true);
        for (auto &m : root_moves) {
          t.poll();
          // Independent full-window search: every candidate is exact. The
          // shared traversal refines internal ordering ties before publishing.
          auto line = t.child(root_prepared, m, [&](PreparedNode &next) {
            return t.node(depth - 1, 1, next, {}, {}, true);
          }, depth == 1 ? (s11_quiescence_enabled() ? (s17a_lazy() ? PreparationPurpose::search_horizon : PreparationPurpose::eager) : PreparationPurpose::search_horizon) : PreparationPurpose::search_inner);
          if (t.ordering) {
            const bool improving = std::all_of(ranking.begin(),ranking.end(),[&](const auto &v){
              return snapshot->side_to_move()==Side::white?line.score>v.score:line.score<v.score;});
            t.ordering->observe(t.ordering_node,m.text,improving?3:2,ranking.size()+1);
          }
          line.action = {DecisionKind::play, m.text};
          line.pv.prepend(m.text);
          ranking.push_back(variation(std::move(line)));
        }
        }
        if (t.ordering) t.ordering->seal(t.ordering_node);
        bool white = snapshot->side_to_move() == Side::white;
        std::sort(ranking.begin(), ranking.end(), [&](const auto &a, const auto &b) {
          if (a.score != b.score)
            return white ? a.score > b.score : a.score < b.score;
          return canonical_before(a.decision, b.decision);
        });
        ranking.erase(ranking.begin() + std::min(options.multipv_count, ranking.size()),
                      ranking.end());
        if (t.history != h.occurrences() || !t.path.empty())
          throw std::runtime_error("unbalanced search history/path");
        elapsed();
        CompletedDepth next{depth, ranking.front(), std::move(ranking),
                            r.statistics, r.provenance};
        // Linearization point: last observation before moving the complete
        // detached snapshot into committed storage. A later request retains it.
        t.poll();
        t.previous = nullptr;
        if (t.ordering) t.ordering->commit();
        r.completed = std::move(next);
        if (s11_trace_enabled()) {
          std::fprintf(stderr,"CELAB_S11_DEPTH depth=%d mode=%d see=%d prune=%u total=%llu q=%llu see_calls=%llu see_ns=%llu sorted=%llu searched=%llu captures=%llu promotions=%llu ep=%llu evasions=%llu standpat_cut=%llu q_cut=%llu emergency=%llu negative=%llu zero=%llu positive=%llu s12_candidates=%llu s12_pruned=%llu s12_check_protected=%llu s15_zero_pruned=%llu s15_check_pruned=%llu s15_ep_pruned=%llu s15_see_recaptures=%llu s16_delta_tested=%llu s16_delta_pruned=%llu s16_delta_check_protected=%llu s16_positive_pruned=%llu s16_top_pruned=%llu s16_positive_check_protected=%llu\n",
            depth,int(s11_quiescence_enabled()),int(s11_see_order_enabled()),
            unsigned(s11_quiescence_enabled() && s16_default_material(e.identity()) ? 17U : 0U),
            static_cast<unsigned long long>(r.statistics.visited_nodes),
            static_cast<unsigned long long>(t.s11_qnodes),
            static_cast<unsigned long long>(t.s11_see_calls),
            static_cast<unsigned long long>(t.s11_see_ns),
            static_cast<unsigned long long>(t.s11_sorted_nodes),
            static_cast<unsigned long long>(t.s11_searched),
            static_cast<unsigned long long>(t.s11_captures),
            static_cast<unsigned long long>(t.s11_promotions),
            static_cast<unsigned long long>(t.s11_ep),
            static_cast<unsigned long long>(t.s11_evasions),
            static_cast<unsigned long long>(t.s11_standpat_cutoffs),
            static_cast<unsigned long long>(t.s11_search_cutoffs),
            static_cast<unsigned long long>(t.s11_emergency),
            static_cast<unsigned long long>(t.s11_see_negative),
            static_cast<unsigned long long>(t.s11_see_zero),
            static_cast<unsigned long long>(t.s11_see_positive),
            static_cast<unsigned long long>(t.s12_candidates),
            static_cast<unsigned long long>(t.s12_pruned),
            static_cast<unsigned long long>(t.s12_check_protected),
            static_cast<unsigned long long>(t.s15_zero_pruned),
            static_cast<unsigned long long>(t.s15_check_pruned),
            static_cast<unsigned long long>(t.s15_ep_pruned),
            static_cast<unsigned long long>(t.s15_see_recaptures),
            static_cast<unsigned long long>(t.s16_delta_tested),
            static_cast<unsigned long long>(t.s16_delta_pruned),
            static_cast<unsigned long long>(t.s16_delta_check_protected),
            static_cast<unsigned long long>(t.s16_positive_pruned),
            static_cast<unsigned long long>(t.s16_top_pruned),
            static_cast<unsigned long long>(t.s16_positive_check_protected));
          if(s17a_trace())std::fprintf(stderr,
            "CELAB_S17A_DEPTH depth=%d mode=%u stage5=%llu expansions=%llu fallback=%llu generated=%llu quiet_promos_generated=%llu capture_promos_generated=%llu quiet_promos_eligible=%llu capture_promos_eligible=%llu quiet_promos_searched=%llu capture_promos_searched=%llu quiet_promos_excluded=%llu\n",
            depth,s17a_mode(),
            static_cast<unsigned long long>(t.s17a_stage5),
            static_cast<unsigned long long>(t.s17a_lazy_expansions),
            static_cast<unsigned long long>(t.s17a_claim_fallbacks),
            static_cast<unsigned long long>(t.s17a_generated),
            static_cast<unsigned long long>(t.s17a_quiet_promo_generated),
            static_cast<unsigned long long>(t.s17a_capture_promo_generated),
            static_cast<unsigned long long>(t.s17a_quiet_promo_eligible),
            static_cast<unsigned long long>(t.s17a_capture_promo_eligible),
            static_cast<unsigned long long>(t.s17a_quiet_promo_searched),
            static_cast<unsigned long long>(t.s17a_capture_promo_searched),
            static_cast<unsigned long long>(t.s17a_excluded_quiet_promo));
          std::fprintf(stderr,"CELAB_S11_HIST depth=%d qply=",depth);
          for(std::size_t q=0;q<t.s11_qhist.size();++q)
            std::fprintf(stderr,"%s%llu",q?",":"",
              static_cast<unsigned long long>(t.s11_qhist[q]));
          std::fprintf(stderr,"\n");
        }
        if (progress) {
          try {
            progress(*r.completed); // value-copy: no alias to engine state
          } catch (const std::exception &ex) {
            throw std::runtime_error(std::string("progress callback: ") + ex.what());
          } catch (...) {
            throw std::runtime_error("unknown progress callback exception");
          }
        }
        t.poll(); // includes requests made by the just-completed callback
        r.status = options.max_depth ? IterativeSearchStatus::depth_reached
                                    : IterativeSearchStatus::depth_limit_reached;
      }
    }
  // Traversal and TT intern/probe polls must share this cancellation boundary.
  } catch (const detail::SearchCancelled &) {
    r.status = IterativeSearchStatus::cancelled;
    if (!r.completed)
      r.unevaluated_fallback = std::move(fallback);
  } catch (const EvaluationError &ex) {
    r.status = IterativeSearchStatus::evaluator_failed;
    r.diagnostic = ex.what();
  } catch (const std::exception &ex) {
    r.status = IterativeSearchStatus::search_error;
    r.diagnostic = ex.what();
  } catch (...) {
    r.status = IterativeSearchStatus::search_error;
    r.diagnostic = "unknown iterative search exception";
  }
  if (r.status == IterativeSearchStatus::evaluator_failed ||
      r.status == IterativeSearchStatus::search_error) {
    r.completed.reset();
    r.unevaluated_fallback.reset();
    r.terminal_decision.reset();
    r.terminal_score.reset();
  }
  elapsed();
  return r;
}
// Benchmark-only instrumentation accessors (not part of public chess API).
extern "C" int celab_t13a_config() noexcept { return t13a_mode(); }
extern "C" void celab_t13a_reset() noexcept { t13a_counts={}; }
extern "C" void celab_t13a_dump() noexcept {
  std::fprintf(stderr,"CELAB_T13A_COUNTS mode=%d lazy_prepared=%llu lazy_expanded=%llu early_probes=%llu early_exact_returns=%llu early_bound_returns=%llu early_nonreturns=%llu deferred_order_nodes=%llu order_skipped_returns=%llu\n",
    t13a_mode(),static_cast<unsigned long long>(t13a_counts.lazy_prepared),
    static_cast<unsigned long long>(t13a_counts.lazy_expanded),
    static_cast<unsigned long long>(t13a_counts.early_probes),
    static_cast<unsigned long long>(t13a_counts.early_exact_returns),
    static_cast<unsigned long long>(t13a_counts.early_bound_returns),
    static_cast<unsigned long long>(t13a_counts.early_nonreturns),
    static_cast<unsigned long long>(t13a_counts.deferred_order_nodes),
    static_cast<unsigned long long>(t13a_counts.order_skipped_returns));
}
} // namespace chess_lab
