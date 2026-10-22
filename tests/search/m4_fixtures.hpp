// M4 test-only fixtures copied from the unchanged M3 regression suite.
// The exhaustive oracle shares no production traversal or bounds logic.
#pragma once
#include "material.hpp"
#include <algorithm>
#include <array>
#include <chess_lab/search.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace chess_lab;
// Keep M4/M5's full historical root-claim/ranking assertions intact. M7.2 tests
// exercise the new default against these exact references independently.
inline IterativeSearchResult legacy_iterative_search(
    const ChessCore &c, const Position &p, const GameHistory &h, Evaluator &e,
    IterativeSearchOptions options = {}, CancellationToken token = {},
    SearchProgress progress = {}, TranspositionTableUse use = {}) {
  options.single_pv_play_only = false;
  return iterative_search(c, p, h, e, options, token, progress, use);
}
void check(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
  try {
    f();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("expected rejection");
}
std::string digest(64, 'a');
ChessEvalApi original;
std::vector<double> values;
bool tree_values = false;
double constant = 0;
int calls = 0, fail_at = 0, fail_mode = 0;
std::function<void()> eval_hook;
ChessEvalStatus CHESS_EVAL_CALL controlled(ChessEvalHandle,
                                           const ChessEvalPosition *p,
                                           double *out, char *error,
                                           uint64_t cap) {
  if (cap)
    error[0] = 0;
  ++calls;
  if (eval_hook) eval_hook();
  if (fail_at && calls >= fail_at) {
    if (fail_mode == 0)
      return CHESS_EVAL_INTERNAL_ERROR;
    if (fail_mode == 1)
      return -123;
    *out = fail_mode == 2   ? std::numeric_limits<double>::quiet_NaN()
           : fail_mode == 3 ? INFINITY
                            : -INFINITY;
    return 0;
  }
  if (tree_values && p->fullmove_number > values.size())
    return CHESS_EVAL_INTERNAL_ERROR;
  *out = tree_values ? values[p->fullmove_number - 1] : constant;
  return 0;
}
ChessEvalStatus CHESS_EVAL_CALL provider(uint64_t a, uint64_t b, void *out,
                                         uint64_t n, char *e, uint64_t en) {
  auto status = chess_lab_material_provider(a, b, out, n, e, en);
  if (status == 0) {
    original = *static_cast<ChessEvalApi *>(out);
    static_cast<ChessEvalApi *>(out)->evaluate = controlled;
  }
  return status;
}
auto evaluator() {
  calls = 0;
  return EvaluatorModule::provider(provider, digest, "search-fixture")
      .create("{}");
}
// Synthetic tree adapter. IDs use exact distinct pawn patterns, while every
// supplied hash intentionally collides. Edges are adapter-legal coordinate
// moves.
struct Node {
  Side side = Side::white;
  std::vector<int> children;
  PositionStatus status = PositionStatus::ongoing;
  uint32_t clock = 0;
  bool dead = false;
};
struct TreePosition : Position {
  int id;
  explicit TreePosition(int i) : id(i) {}
};
struct TreeMove : Move {
  int child;
  std::string text;
  TreeMove(int c, std::string s) : child(c), text(std::move(s)) {}
};
struct TreeUndo : UndoToken {
  int from, to;
  TreeUndo(int a, int b) : from(a), to(b) {}
};
struct TreeSnapshot : PositionSnapshot {
  std::array<PieceCode, 64> pieces{};
  Node node;
  int id;
  TreeSnapshot(Node n, int i) : node(std::move(n)), id(i) {
    pieces[4] = PieceCode::white_king;
    pieces[60] = PieceCode::black_king;
    pieces[8] = PieceCode::white_pawn;
    for (int bit = 0; bit < 16; ++bit)
      if ((i + 1) & (1 << bit))
        pieces[16 + bit] = PieceCode::white_pawn;
  }
  std::span<const PieceCode, 64> squares() const noexcept override {
    return pieces;
  }
  Side side_to_move() const noexcept override { return node.side; }
  bool can_castle_kingside(Side) const noexcept override { return false; }
  bool can_castle_queenside(Side) const noexcept override { return false; }
  std::optional<uint8_t> en_passant_square() const noexcept override {
    return {};
  }
  uint32_t halfmove_clock() const noexcept override { return node.clock; }
  uint32_t fullmove_number() const noexcept override { return id + 1; }
  uint32_t schema_version() const noexcept override { return 1; }
};
struct Tree : ChessCore {
  std::vector<Node> nodes;
  mutable int balance = 0;
  int throw_at = -1, undo_throw_at = -1;
  int id(const Position &p) const {
    return dynamic_cast<const TreePosition &>(p).id;
  }
  std::unique_ptr<Position> initial_position() const override {
    return std::make_unique<TreePosition>(0);
  }
  std::unique_ptr<Position> load_fen(std::string_view) const override {
    return initial_position();
  }
  std::string export_fen(const Position &p) const override {
    return std::to_string(id(p));
  }
  std::unique_ptr<Position> clone(const Position &p) const override {
    return std::make_unique<TreePosition>(id(p));
  }
  std::vector<std::unique_ptr<Move>>
  legal_moves(const Position &p) const override {
    if (id(p) == throw_at)
      throw std::runtime_error("injected traversal failure");
    std::vector<std::unique_ptr<Move>> r;
    const auto &kids = nodes[id(p)].children;
    for (int i = int(kids.size()) - 1; i >= 0; --i)
      r.push_back(std::make_unique<TreeMove>(
          kids[i], std::string(1, char('a' + i)) + "1a2"));
    return r;
  }
  std::unique_ptr<UndoToken> make_move(Position &p,
                                       const Move &m) const override {
    auto &pos = dynamic_cast<TreePosition &>(p);
    auto &move = dynamic_cast<const TreeMove &>(m);
    auto u = std::make_unique<TreeUndo>(pos.id, move.child);
    pos.id = move.child;
    ++balance;
    return u;
  }
  void undo_move(Position &p, UndoToken &token) const override {
    auto &pos = dynamic_cast<TreePosition &>(p);
    auto &u = dynamic_cast<TreeUndo &>(token);
    check(pos.id == u.to, "LIFO undo");
    if (pos.id == undo_throw_at && fail_at && calls >= fail_at)
      throw std::runtime_error("injected undo failure");
    pos.id = u.from;
    --balance;
  }
  std::string encode_move(const Position &, const Move &m) const override {
    return dynamic_cast<const TreeMove &>(m).text;
  }
  std::unique_ptr<Move> decode_move(const Position &p,
                                    std::string_view s) const override {
    for (auto &m : legal_moves(p))
      if (encode_move(p, *m) == s)
        return std::move(m);
    throw std::invalid_argument("illegal tree move");
  }
  void reset(Position &, std::string_view) const override {
    throw std::runtime_error("unused");
  }
  bool in_check(const Position &p) const override {
    return status(p) == PositionStatus::checkmate;
  }
  PositionStatus status(const Position &p) const override {
    return nodes[id(p)].status;
  }
  bool known_dead_position(const Position &p) const override {
    return nodes[id(p)].dead;
  }
  uint64_t position_key(const Position &) const override { return 0; }
  uint64_t repetition_key(const Position &) const override { return 0; }
  std::unique_ptr<const PositionSnapshot>
  snapshot(const Position &p) const override {
    return std::make_unique<TreeSnapshot>(nodes[id(p)], id(p));
  }
};
struct Expected {
  SearchScore score;
  ChessDecision action;
  std::vector<std::string> pv;
  PVEnd end;
  std::string intended;
};
// Independent exhaustive oracle: no search bounds, history code, ordering
// helper, or recursive implementation shared with production. Mate distances
// increment at each return rather than using production's root-relative
// transformation.
Expected oracle(const Tree &t, int id, int depth) {
  const auto &n = t.nodes[id];
  if (n.status == PositionStatus::checkmate)
    return {n.side == Side::white ? SearchScore::black_mate(0)
                                  : SearchScore::white_mate(0),
            {DecisionKind::terminal,
             {},
             n.side == Side::white ? TerminalOutcome::black_win
                                   : TerminalOutcome::white_win},
            {},
            PVEnd::terminal,
            {}};
  if (n.status == PositionStatus::stalemate || n.dead || n.clock >= 150)
    return {SearchScore::finite(0),
            {DecisionKind::terminal, {}, TerminalOutcome::draw},
            {},
            PVEnd::terminal,
            {}};
  std::vector<Expected> candidates;
  if (n.clock >= 100)
    candidates.push_back({SearchScore::finite(0),
                          {DecisionKind::claim_draw, {}},
                          {},
                          PVEnd::claim_draw,
                          {}});
  else
    for (size_t i = 0; i < n.children.size(); ++i)
      if (t.nodes[n.children[i]].clock >= 100) {
        auto m = std::string(1, char('a' + i)) + "1a2";
        candidates.push_back({SearchScore::finite(0),
                              {DecisionKind::claim_draw_with_intended_move, m},
                              {},
                              PVEnd::claim_draw_with_intended_move,
                              m});
        break;
      }
  if (depth == 0)
    candidates.push_back({SearchScore::finite(values[id]),
                          {DecisionKind::play, {}},
                          {},
                          PVEnd::horizon,
                          {}});
  else
    for (size_t i = 0; i < n.children.size(); ++i) {
      auto line = oracle(t, n.children[i], depth - 1);
      line.score = line.score.one_ply_farther();
      auto m = std::string(1, char('a' + i)) + "1a2";
      line.action = {DecisionKind::play, m};
      line.pv.insert(line.pv.begin(), m);
      candidates.push_back(std::move(line));
    }
  check(!candidates.empty(), "oracle valid tree");
  auto best = candidates.front();
  for (const auto &candidate : candidates)
    if (n.side == Side::white ? candidate.score > best.score
                              : candidate.score < best.score)
      best = candidate;
  return best;
}
void verify_pv(const ChessCore &c, const Position &root,
               const SearchResult &r) {
  check(r.decision->terminal_outcome.has_value() ==
            (r.decision->kind == DecisionKind::terminal),
        "terminal outcome present exactly for terminal decisions");
  auto p = c.clone(root);
  std::vector<std::unique_ptr<UndoToken>> undo;
  for (const auto &s : r.principal_variation) {
    auto m = c.decode_move(*p, s);
    undo.push_back(c.make_move(*p, *m));
  }
  if (r.decision->kind == DecisionKind::play)
    check(!r.principal_variation.empty() &&
              r.principal_variation.front() == r.decision->move,
          "root PV decision");
  if (r.pv_end == PVEnd::terminal) {
    check(c.status(*p) != PositionStatus::ongoing ||
              c.known_dead_position(*p) ||
              c.snapshot(*p)->halfmove_clock() >= 150 ||
              r.score == SearchScore::finite(0),
          "PV terminal");
  }
  if (r.pv_end == PVEnd::claim_draw_with_intended_move) {
    auto m = c.decode_move(*p, r.pv_intended_move);
    check(bool(m), "legal unplayed intended move");
  }
  for (auto i = undo.rbegin(); i != undo.rend(); ++i)
    c.undo_move(*p, **i);
}
