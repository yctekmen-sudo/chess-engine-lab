#include "packed_pv.hpp"
#include "compact_uci_text.hpp"
#include "transposition_table_internal.hpp"
#include "audit_ttg.hpp"
#include "history_duplicates.hpp"
#include "repeated_claim_targets.hpp"
#include "persistent_ordering.hpp"
#include "../native_move_batch.hpp"
#include "../native_undo_fastpath.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <chess_lab/search.hpp>
#include <cmath>
#include <limits>
#include <type_traits>

namespace chess_lab {
static_assert(std::is_trivially_copyable_v<SearchScore>);
// Native adapter identification; synthetic adapters retain custom status and clock semantics.
bool native_chess_core(const ChessCore &) noexcept;
std::size_t native_legal_move_count(const ChessCore &, const Position &);
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
  detail::CompactUciText text;
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
         std::string("bnqr").find(s[4]) == std::string::npos) ||
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
        (s.size() == 5 && std::string("bnqr").find(s[4]) == std::string::npos) ||
        (i && result[i - 1].text == s))
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
  }
}
// The generic eager path above stays the reference/fallback. Native horizon leaves
// keep the same text/fact/validation rules without allocating unused Mv owners.
void batch_ordered_moves_into(
    std::vector<OrderedMove> &result, const ChessCore &c, const Position &p,
    const detail::NativeMoveBatch &batch,
    const CancellationToken *token, const PositionSnapshot &snapshot) {
  result.clear();
  const auto count = detail::native_move_batch_size(batch);
  if (token && count) detail::tt_poll(*token);
  result.reserve(count);
  if (count) {
    // Preserve reserve/first-member poll/type-check ordering, including the
    // empty-list path. This binding ends before facts, sorting or traversal.
    if (token) detail::tt_poll(*token);
    const detail::NativeBatchEncoder encoder(c, p, batch);
    for (std::size_t i = 0; i < count; ++i) {
      if (i && token) detail::tt_poll(*token);
      OrderedMove move{encoder.encode(i), {}};
      move.native_ordinal = static_cast<std::uint16_t>(i);
      result.push_back(std::move(move));
    }
  }
  const auto squares = snapshot.squares();
  for (auto &m : result) {
    const unsigned from = unsigned(m.text[0] - 'a') + 8U * unsigned(m.text[1] - '1');
    const unsigned to = unsigned(m.text[2] - 'a') + 8U * unsigned(m.text[3] - '1');
    const auto piece = squares[from];
    m.pawn_move = piece == PieceCode::white_pawn || piece == PieceCode::black_pawn;
    m.legal_ep = m.pawn_move && m.text[0] != m.text[2] &&
        squares[to] == PieceCode::empty &&
        snapshot.en_passant_square() == std::optional<std::uint8_t>(static_cast<std::uint8_t>(to));
    m.capture = squares[to] != PieceCode::empty || m.legal_ep;
  }
  sort_native_batch_moves(result);
  validate_native_batch_moves(result);
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
  std::size_t native_legal_count = 0;
  PreparationMode mode = PreparationMode::eager;
  RepetitionSignature repetition;
  bool claims_ready = false;
  bool claim_now_threefold = false;
  bool claim_now_fifty = false;
  detail::NativeMoveBatchOwner batch;
  std::size_t legal_count() const {
    return (mode == PreparationMode::native_claim_free_leaf ||
            mode == PreparationMode::native_inner_deferred) ? native_legal_count : legal.size();
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
PreparedNode prepare_node(const ChessCore &c, const Position &p,
                          const CancellationToken *token = nullptr,
                          std::optional<std::span<const RepetitionSignature>> leaf_history = {},
                          bool tt_search_horizon = false,
                          bool parent_has_duplicate = false,
                          bool native_search_inner = false,
                          std::vector<OrderedMove> *recycled = nullptr) {
  PreparedNode node;
  if (recycled) {
    assert(recycled->empty());
    node.legal.swap(*recycled); // Take capacity, no copy or allocation.
  }
  try {
  node.snapshot = c.snapshot(p);
  if (leaf_history && native_chess_core(c) &&
      !node.snapshot->en_passant_square() && node.snapshot->halfmove_clock() < 99) {
    node.repetition = snapshot_repetition(*node.snapshot);
    if (token) detail::tt_poll(*token);
    if (!parent_has_duplicate && !history_contains(*leaf_history, node.repetition, token)) {
      // No move/text consumer is reachable in this certified position frame.
      // Generate the complete set, retaining its count without move owners.
      if (token) detail::tt_poll(*token);
      node.native_legal_count = native_legal_move_count(c, p);
      if (token) detail::tt_poll(*token); // Also covers zero-move terminals.
      for (std::size_t i = 0; i < node.native_legal_count; ++i)
        if (token) detail::tt_poll(*token);
      node.mode = PreparationMode::native_claim_free_leaf;
      return node;
    }
  }
  const bool native_search_horizon = tt_search_horizon || leaf_history.has_value();
  if ((native_search_horizon || native_search_inner) && native_chess_core(c)) {
    if (token) detail::tt_poll(*token);
    node.batch = detail::prepare_native_move_batch(c, p);
    if (token) detail::tt_poll(*token); // Including zero-legal terminal positions.
    batch_ordered_moves_into(node.legal, c, p, *node.batch, token, *node.snapshot);
    node.mode = native_search_horizon ? PreparationMode::native_horizon_batch
                                  : PreparationMode::native_inner_batch;
  } else {
    ordered_moves_into(node.legal, c, p, token, node.snapshot.get());
  }
  node.repetition = snapshot_repetition(*node.snapshot);
  auto &r = node.repetition;
  if (auto ep = node.snapshot->en_passant_square())
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
                                   std::vector<OrderedMove> *recycled) {
  PreparedNode node;
  if (recycled) {
    assert(recycled->empty());
    node.legal.swap(*recycled);
  }
  try {
    node.snapshot = c.snapshot(p);
    if (token) detail::tt_poll(*token);
    node.batch = detail::prepare_native_move_batch(c, p);
    if (token) detail::tt_poll(*token);
    node.native_legal_count = detail::native_move_batch_size(*node.batch);
    node.repetition = snapshot_repetition(*node.snapshot);
    if (auto ep = node.snapshot->en_passant_square(); ep &&
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
    batch_ordered_moves_into(node.legal,c,p,*node.batch,token,*node.snapshot);
    if (node.legal.size() != node.native_legal_count)
      throw std::logic_error("T1.3a deferred native batch count drift");
    // The native legality helper must agree with the original batch-derived
    // repetition signature, including rare legal en-passant transitions.
    auto expected=snapshot_repetition(*node.snapshot);
    if (auto ep=node.snapshot->en_passant_square())
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
  ChessDecision action;
  detail::PackedPV pv;
  PVEnd end;
  std::string intended;
};
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
  ~ReclaimPreparedMoves() noexcept {
    if (!prepared) return;
    prepared->legal.clear(); // Destroy owners before reusing capacity.
    spare.swap(prepared->legal);
  }
};
struct Traversal {
  const ChessCore &core;
  Position &position;
  Evaluator &evaluator;
  SearchOptions options;
  SearchStatistics &stats;
  std::vector<RepetitionSignature> history;
  CancellationToken cancellation{};
  const CompletedDepth *previous = nullptr;
  std::vector<std::string> path{};
  detail::TTSession *cache = nullptr;
  // Each recursive ply owns exactly one currently active prepared list.
  // Retain capacity by ply between siblings; no globally shared storage.
  std::size_t root_history_size = history.size();
  std::array<std::vector<OrderedMove>,65> recycled_moves{};
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
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.text < b.text; });
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
            std::equal(path.begin(), path.end(), pv.begin()))
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
      if (track_path) path.push_back(m.text.str()); // Already encoded.
      if (history.size() < root_history_size ||
          history.size() - root_history_size >= recycled_moves.size())
        throw std::logic_error("search recursion exceeds move-storage frames");
      auto &spare = recycled_moves[history.size() - root_history_size];
      std::optional<PreparedNode> prepared;
      ReclaimPreparedMoves reclaim{prepared,spare};
      try {
        std::optional<std::span<const RepetitionSignature>> leaf_history;
        if (purpose == PreparationPurpose::search_horizon)
          leaf_history.emplace(history);
        const auto preparation_start=detail::audit.enabled?detail::audit_clock():0;
        if (t13a_mode() == 2 && purpose == PreparationPurpose::search_inner &&
            cache && cache->numeric() && native_chess_core(core))
          prepared.emplace(prepare_deferred_inner(core,position,&cancellation,&spare));
        else
          prepared.emplace(prepare_node(core, position, &cancellation, leaf_history,
              purpose == PreparationPurpose::search_horizon && cache, has_duplicate,
              purpose == PreparationPurpose::search_inner, &spare));
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
    const auto &s = *prepared.snapshot;
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
    const auto &s = *prepared.snapshot;
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
    const auto &s = *prepared.snapshot;
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
                       next.snapshot->halfmove_clock() >= 100;
              });
        if (eligible) {
          best = Line{SearchScore::finite(0),
                      {DecisionKind::claim_draw_with_intended_move, m.text.str()},
                      {},
                      PVEnd::claim_draw_with_intended_move,
                      m.text.str()};
          break;
        }
      }
    return best;
  }
  // exact=true is used only with an unbounded window. An ordered child can
  // return a bound on equality. Re-search promising/canonically earlier ties
  // with a full window and exact PV before letting them become publishable.
  Line node(int depth, std::uint64_t ply, PreparedNode &prepared,
            std::optional<SearchScore> alpha, std::optional<SearchScore> beta,
            bool exact = false,
            bool root_play_only = false) {
    poll();
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
    const auto &s = prepared.snapshot;
    // This cache is scoped to a single node; children cannot overwrite it.
    // A TT hash is computed only if this node actually probes or stores.
    std::optional<std::uint64_t> node_tt_hash;
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
          line.action, ply, *s, &node_tt_hash);
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
        preprobe.emplace(cache->probe(history.back(),depth,ply,*s,&node_tt_hash));
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
            if (!preprobe->hint.empty() &&
                detail::native_has_legal_uci_hint(core,position,preprobe->hint))
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
            return finish({*preprobe->score, preprobe->decision.value(), {}, PVEnd::horizon, {}});
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
    std::string deferred_hint;
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
          cache->probe(history.back(), depth, ply, *s, &node_tt_hash);
      if (!hit.hint.empty()) {
        if (raw_leaf) {
          // The table may still report a move hint for a previously searched
          // position. Preserve the exact legal-hint statistic without constructing
          // a complete boxed/sorted/encoded batch. Depth zero never reorders.
          if (detail::native_has_legal_uci_hint(core, position, hit.hint))
            ++stats.transposition.hint_hits;
        } else {
          auto found = std::find_if(moves_here.begin(), moves_here.end(), [&](const auto &m) { return m.text == hit.hint; });
          if (found != moves_here.end()) {
            if (defer_order) deferred_hint=hit.hint;
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
          if (best && hit.decision == best->action)
            return tt_finish(*best); // exact zero, claim eligibility revalidated
          if (!exact || (depth == 0 && hit.decision && hit.decision->kind == DecisionKind::play)) {
            audit_return(false);
            cache->reward(hit); // actual numeric TT return, not just candidate hit
            return tt_finish({*hit.score, hit.decision.value(), {}, PVEnd::horizon, {}});
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
          return tt_finish({*hit.score, hit.decision.value(), {}, PVEnd::horizon, {}});
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
      if (!deferred_hint.empty()) {
        auto found=std::find_if(moves_here.begin(),moves_here.end(),
            [&](const auto &m) { return m.text == deferred_hint; });
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
          SearchScore::finite(evaluator.evaluate(evaluation_snapshot(*s)));
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
    for (std::size_t i = 0; i < moves_here.size(); ++i) {
      auto &m = moves_here[i];
      poll();
      const auto child_alpha = alpha, child_beta = beta;
      bool refined = false;
      bool direct_exact = exact && (!options.alpha_beta_enabled || (!alpha && !beta));
      auto line = child(prepared, m, [&](PreparedNode &next) {
        return node(depth - 1, ply + 1, next, alpha, beta, direct_exact);
      }, depth == 1 ? PreparationPurpose::search_horizon : PreparationPurpose::search_inner);
      ChessDecision action{DecisionKind::play, m.text.str()};
      auto competitive = [&] {
        return !best || better(line.score, best->score) ||
               (line.score == best->score && canonical_before(action, best->action));
      };
      bool improved = false;
      if (competitive()) {
        if (exact && !direct_exact) {
          refined = true;
          if (ordering) ordering->refinement();
          line = child(prepared, m, [&](PreparedNode &next) {
            return node(depth - 1, ply + 1, next, {}, {}, true);
          }, depth == 1 ? PreparationPurpose::search_horizon : PreparationPurpose::search_inner);
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
      if (update() && i + 1 < moves_here.size()) {
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
    r.decision = std::move(line.action);
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
      r.terminal_decision = line->action;
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
        return SearchVariation{std::move(line.action), line.score,
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
          }, depth == 1 ? PreparationPurpose::search_horizon : PreparationPurpose::search_inner);
          if (t.ordering) {
            const bool improving = std::all_of(ranking.begin(),ranking.end(),[&](const auto &v){
              return snapshot->side_to_move()==Side::white?line.score>v.score:line.score<v.score;});
            t.ordering->observe(t.ordering_node,m.text,improving?3:2,ranking.size()+1);
          }
          line.action = {DecisionKind::play, m.text.str()};
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
