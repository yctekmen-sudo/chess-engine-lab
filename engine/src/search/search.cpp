#include <algorithm>
#include <chess_lab/search.hpp>
#include <cmath>
#include <limits>
#include <type_traits>

namespace chess_lab {
static_assert(std::is_trivially_copyable_v<SearchScore>);
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
struct OrderedMove {
  std::string text;
  std::unique_ptr<Move> move;
};
std::vector<OrderedMove> ordered_moves(const ChessCore &c, const Position &p) {
  std::vector<OrderedMove> result;
  for (auto &m : c.legal_moves(p))
    result.push_back({c.encode_move(p, *m), std::move(m)});
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
  return result;
}
// Explicit restoration propagates undo failures as traversal errors, even when
// unwinding an evaluator failure. Never throw from a cleanup destructor.
template <class F>
auto with_move(const ChessCore &c, Position &p, const Move &m, F f) {
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
// All recursive values AND bounds use root-relative mate distances: a terminal
// at ply P is Mate(P). This is the order-preserving coordinate transform of
// node-relative Mate(0), avoiding lossy inverse bound transforms at Mate(0).
struct Line {
  SearchScore score;
  ChessDecision action;
  std::vector<std::string> pv;
  PVEnd end;
  std::string intended;
};
struct Traversal {
  const ChessCore &core;
  Position &position;
  Evaluator &evaluator;
  SearchOptions options;
  SearchStatistics &stats;
  std::vector<RepetitionSignature> history;
  std::size_t count(const RepetitionSignature &s) const {
    return std::count(history.begin(), history.end(), s);
  }
  template <class F> auto child(const Move &m, F f) {
    return with_move(core, position, m, [&] {
      history.push_back(repetition_signature(core, position));
      using T = decltype(f());
      std::optional<T> result;
      try {
        result.emplace(f());
      } catch (...) {
        history.pop_back();
        throw;
      }
      history.pop_back();
      return std::move(*result);
    });
  }
  Line node(int depth, std::uint64_t ply, std::optional<SearchScore> alpha,
            std::optional<SearchScore> beta) {
    ++stats.visited_nodes;
    auto status = core.status(position);
    auto s = core.snapshot(position);
    bool white = s->side_to_move() == Side::white;
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
    auto occurrences = count(history.back());
    if (occurrences >= 5 || s->halfmove_clock() >= 150)
      return terminal(SearchScore::finite(0), TerminalOutcome::draw);
    auto moves = ordered_moves(core, position);
    if (moves.empty())
      throw std::runtime_error("ongoing node has no legal moves");
    std::optional<Line> best;
    if (occurrences >= 3 || s->halfmove_clock() >= 100)
      best = Line{SearchScore::finite(0),
                  {DecisionKind::claim_draw, {}},
                  {},
                  PVEnd::claim_draw,
                  {}};
    else
      for (const auto &m : moves) {
        bool eligible = child(*m.move, [&] {
          return count(history.back()) >= 3 ||
                 core.snapshot(position)->halfmove_clock() >= 100;
        });
        if (eligible) {
          best = Line{SearchScore::finite(0),
                      {DecisionKind::claim_draw_with_intended_move, m.text},
                      {},
                      PVEnd::claim_draw_with_intended_move,
                      m.text};
          break;
        }
      }
    auto better = [&](SearchScore a, SearchScore b) {
      return white ? a > b : a < b;
    };
    if (depth == 0) {
      ++stats.evaluator_calls;
      auto score =
          SearchScore::finite(evaluator.evaluate(evaluation_snapshot(*s)));
      if (!best || better(score, best->score))
        return {score, {DecisionKind::play, {}}, {}, PVEnd::horizon, {}};
      return *best;
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
      return *best;
    }
    for (std::size_t i = 0; i < moves.size(); ++i) {
      const auto &m = moves[i];
      auto line =
          child(*m.move, [&] { return node(depth - 1, ply + 1, alpha, beta); });
      if (!best || better(line.score, best->score)) {
        line.action = {DecisionKind::play, m.text};
        line.pv.insert(line.pv.begin(), m.text);
        best = std::move(line);
      }
      if (update() && i + 1 < moves.size()) {
        ++stats.alpha_beta_cutoffs;
        break;
      }
    }
    return *best;
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
    auto line = t.node(options.nominal_depth, 0, {}, {});
    if (t.history != h.occurrences())
      throw std::runtime_error("unbalanced search history");
    r.status = line.action.kind == DecisionKind::terminal
                   ? SearchStatus::terminal
                   : SearchStatus::complete;
    r.completed_depth =
        r.status == SearchStatus::terminal ? 0 : options.nominal_depth;
    r.decision = std::move(line.action);
    r.score = line.score;
    r.principal_variation = std::move(line.pv);
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
} // namespace chess_lab
