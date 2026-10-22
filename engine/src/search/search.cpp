#include "transposition_table_internal.hpp"
#include <algorithm>
#include <chess_lab/search.hpp>
#include <cmath>
#include <limits>
#include <type_traits>

namespace chess_lab {
static_assert(std::is_trivially_copyable_v<SearchScore>);
// Native adapter identification; synthetic adapters retain custom status and clock semantics.
bool native_chess_core(const ChessCore &) noexcept;
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
  bool pawn_move = false;
  bool capture = false;
  bool legal_ep = false;
  bool claim_threefold = false;
  bool claim_fifty = false;
};
std::vector<OrderedMove> ordered_moves(const ChessCore &c, const Position &p,
                                       const CancellationToken *token = nullptr,
                                       const PositionSnapshot *snapshot = nullptr) {
  std::vector<OrderedMove> result;
  for (auto &m : c.legal_moves(p)) {
    if (token) detail::tt_poll(*token);
    result.push_back({c.encode_move(p, *m), std::move(m)});
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
// A prepared node owns one legal list and its position-specific facts.
struct PreparedNode {
  std::unique_ptr<const PositionSnapshot> snapshot;
  std::vector<OrderedMove> legal;
  RepetitionSignature repetition;
  bool claims_ready = false;
};
PreparedNode prepare_node(const ChessCore &c, const Position &p,
                          const CancellationToken *token = nullptr) {
  PreparedNode node;
  node.snapshot = c.snapshot(p);
  node.legal = ordered_moves(c, p, token, node.snapshot.get());
  auto &r = node.repetition;
  std::copy(node.snapshot->squares().begin(), node.snapshot->squares().end(), r.pieces.begin());
  r.side = node.snapshot->side_to_move();
  r.castling = {node.snapshot->can_castle_kingside(Side::white),
                node.snapshot->can_castle_queenside(Side::white),
                node.snapshot->can_castle_kingside(Side::black),
                node.snapshot->can_castle_queenside(Side::black)};
  if (auto ep = node.snapshot->en_passant_square())
    for (const auto &move : node.legal)
      if (move.legal_ep) { r.legal_ep = ep; break; }
  return node;
}
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
bool canonical_before(const ChessDecision &a, const ChessDecision &b) {
  auto rank = [](DecisionKind k) {
    return k == DecisionKind::claim_draw ? 0
         : k == DecisionKind::claim_draw_with_intended_move ? 1 : 2;
  };
  return rank(a.kind) != rank(b.kind) ? rank(a.kind) < rank(b.kind)
                                     : a.move < b.move;
}
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
  void poll() const {
    detail::tt_poll(cancellation);
  }
  std::vector<OrderedMove> &moves(PreparedNode &prepared, bool hints) {
    poll();
    auto &result = prepared.legal;
    // Iterative-deepening root preparation is reused with canonical reset.
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.text < b.text; });
    if (hints) prefer_previous(result);
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
    if (cache) return cache->count(s);
    std::size_t n = 0;
    for (const auto &entry : history) {
      poll();
      n += entry == s;
    }
    return n;
  }
  template <class F> auto child(const OrderedMove &m, F f) {
    poll();
    const bool track_path = previous != nullptr;
    return with_move(core, position, *m.move, [&] {
      if (track_path) path.push_back(m.text); // Already encoded.
      std::optional<PreparedNode> prepared;
      try {
        prepared.emplace(prepare_node(core, position, &cancellation));
        history.push_back(prepared->repetition);
      } catch (...) {
        if (track_path) path.pop_back();
        throw;
      }
      std::uint32_t old_tail = 0;
      try {
        if (cache) old_tail = cache->push(history.back());
      } catch (...) {
        history.pop_back();
        if (track_path) path.pop_back();
        throw;
      }
      using T = decltype(f(*prepared));
      std::optional<T> result;
      try {
        result.emplace(f(*prepared));
      } catch (...) {
        if (cache) cache->pop(history.back(), old_tail);
        history.pop_back();
        if (track_path) path.pop_back();
        throw;
      }
      if (cache) cache->pop(history.back(), old_tail);
      history.pop_back();
      if (track_path) path.pop_back();
      return std::move(*result);
    });
  }
  std::optional<Line> terminal(std::uint64_t ply, const PositionSnapshot &s) {
    auto status = core.status(position);
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
    auto occurrences = count(history.back());
    if (occurrences >= 5 || s.halfmove_clock() >= 150)
      return terminal(SearchScore::finite(0), TerminalOutcome::draw);
    return {};
  }
  void annotate_claims(PreparedNode &prepared) {
    if (prepared.claims_ready || !native_chess_core(core)) return;
    prepared.claims_ready = true;
    const auto &s = *prepared.snapshot;
    if (count(history.back()) >= 3 || s.halfmove_clock() >= 100) return;
    // For an intended threefold claim, the destination must already occur
    // twice. Avoid all hypothetical child work when this is impossible.
    bool repeated_target = false;
    for (std::size_t i = 0; i < history.size() && !repeated_target; ++i)
      for (std::size_t j = 0; j < i; ++j)
        if (history[i] == history[j]) { repeated_target = true; break; }
    for (auto &m : prepared.legal) {
      poll();
      m.claim_fifty = s.halfmove_clock() == 99 && !m.pawn_move && !m.capture;
      if (repeated_target) {
        // Read-only lookahead. Never insert a hypothetical occurrence into
        // the actual history or the table's history-interning structure.
        m.claim_threefold = with_move(core, position, *m.move, [&] {
          auto next = prepare_node(core, position, &cancellation);
          return count(next.repetition) >= 2;
        });
      }
    }
  }
  std::optional<Line> claim(const PositionSnapshot &s,
                            const std::vector<OrderedMove> &canonical_moves) {
    poll();
    auto occurrences = count(history.back());
    std::optional<Line> best;
    if (occurrences >= 3 || s.halfmove_clock() >= 100)
      best = Line{SearchScore::finite(0),
                  {DecisionKind::claim_draw, {}},
                  {},
                  PVEnd::claim_draw,
                  {}};
    else
      for (const auto &m : canonical_moves) {
        poll();
        // Preserve synthetic-adapter custom clock behavior: only the native
        // rules adapter derives intended claims from prepared metadata.
        bool eligible = native_chess_core(core)
            ? (m.claim_threefold || m.claim_fifty)
            : child(m, [&](PreparedNode &next) {
                return count(history.back()) >= 3 ||
                       next.snapshot->halfmove_clock() >= 100;
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
    // Outside the previous-PV prefix all descendants retain M3's canonical
    // order, so a full-window traversal already supplies its canonical PV.
    // Only a subtree that can actually receive a hint needs tie refinement.
    if (exact && !cache) {
      const auto *pv = previous ? &previous->best.principal_variation : nullptr;
      exact = pv && path.size() < pv->size() &&
              std::equal(path.begin(), path.end(), pv->begin());
    }
    ++stats.visited_nodes;
    const auto original_alpha = alpha, original_beta = beta;
    const auto &s = prepared.snapshot;
    auto finish = [&](Line line) {
      if (cache && !root_play_only) {
        poll();
        cache->store(evaluation_snapshot(*s), history.back(), depth, line.score,
          detail::classify_bound(line.score, original_alpha, original_beta), line.action, ply);
      }
      return line;
    };
    auto &moves_here = moves(prepared, false);
    if (auto end = terminal(ply, *s))
      return finish(std::move(*end));
    if (moves_here.empty())
      throw std::runtime_error("ongoing node has no legal moves");
    if (!root_play_only) annotate_claims(prepared);
    auto best = root_play_only ? std::optional<Line>{} : claim(*s, moves_here);
    prefer_previous(moves_here);
    if (cache && !root_play_only) {
      if (!evaluator.usable()) throw EvaluationError("evaluator is quarantined");
      poll();
      auto hit = cache->probe(evaluation_snapshot(*s), history.back(), depth, ply);
      if (!hit.hint.empty()) {
        auto found = std::find_if(moves.begin(), moves.end(), [&](const auto &m) { return m.text == hit.hint; });
        if (found != moves.end()) {
          std::rotate(moves.begin(), found, found + 1);
          ++stats.transposition.hint_hits;
        }
      }
      poll();
      if (hit.score) {
        if (hit.bound == detail::TTBound::exact) {
          if (best && hit.decision == best->action)
            return finish(*best); // exact zero, claim eligibility revalidated
          if (!exact || (depth == 0 && hit.decision && hit.decision->kind == DecisionKind::play))
            return finish({*hit.score, hit.decision.value(), {}, PVEnd::horizon, {}});
          ++stats.transposition.pv_researches;
        } else if (!exact &&
            ((hit.bound == detail::TTBound::lower && beta && *hit.score >= *beta) ||
             (hit.bound == detail::TTBound::upper && alpha && *hit.score <= *alpha))) {
          ++stats.alpha_beta_cutoffs;
          ++stats.transposition.bound_cutoffs;
          return finish({*hit.score, hit.decision.value(), {}, PVEnd::horizon, {}});
        }
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
      const auto &m = moves_here[i];
      poll();
      bool direct_exact = exact && (!options.alpha_beta_enabled || (!alpha && !beta));
      auto line = child(m, [&](PreparedNode &next) {
        return node(depth - 1, ply + 1, next, alpha, beta, direct_exact);
      });
      ChessDecision action{DecisionKind::play, m.text};
      auto competitive = [&] {
        return !best || better(line.score, best->score) ||
               (line.score == best->score && canonical_before(action, best->action));
      };
      if (competitive()) {
        if (exact && !direct_exact)
          line = child(m, [&](PreparedNode &next) {
            return node(depth - 1, ply + 1, next, {}, {}, true);
          });
        if (competitive()) {
          line.action = std::move(action);
          line.pv.insert(line.pv.begin(), m.text);
          best = std::move(line);
        }
      }
      if (update() && i + 1 < moves_here.size()) {
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
        table_use.evaluator == EvaluatorCacheCompatibility::deterministic_position;
    r.provenance.tt_value_policy = table_use.value_policy;
    auto p = c.clone(root);
    Traversal t{c, *p, e, {1, options.alpha_beta_enabled}, r.statistics,
                h.occurrences()};
    // Validation/terminal detection and safe fallback enumeration precede
    // attaching the observer: a cancelled terminal never becomes a fallback.
    auto root_prepared = prepare_node(c, *p);
    const auto &snapshot = root_prepared.snapshot;
    if (auto line = t.terminal(0, *snapshot)) {
      ++r.statistics.visited_nodes;
      r.status = IterativeSearchStatus::terminal;
      r.terminal_decision = line->action;
      r.terminal_score = line->score;
    } else {
      auto &legal = root_prepared.legal;
      if (legal.empty())
        throw std::runtime_error("ongoing root has no legal moves");
      fallback = ChessDecision{DecisionKind::play, legal.front().text};
      t.cancellation = std::move(cancellation);
      t.poll();
      if (session) {
        session->start(h.occurrences());
        t.cache = &*session;
      }
      auto variation = [](Line line) {
        return SearchVariation{std::move(line.action), line.score,
             std::move(line.pv), line.end, std::move(line.intended)};
      };
      for (int depth = 1; depth <= options.max_depth.value_or(64); ++depth) {
        t.poll();
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
        if (auto claim = t.claim(*snapshot, legal))
          ranking.push_back(variation(std::move(*claim)));
        for (const auto &m : t.moves(root_prepared, true)) {
          t.poll();
          // Independent full-window search: every candidate is exact. The
          // shared traversal refines internal ordering ties before publishing.
          auto line = t.child(m, [&](PreparedNode &next) {
            return t.node(depth - 1, 1, next, {}, {}, true);
          });
          line.action = {DecisionKind::play, m.text};
          line.pv.insert(line.pv.begin(), m.text);
          ranking.push_back(variation(std::move(line)));
        }
        }
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
} // namespace chess_lab
