#include "material.hpp"
#include <algorithm>
#include <array>
#include <chess_lab/search.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
using namespace chess_lab;
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
ChessEvalStatus CHESS_EVAL_CALL controlled(ChessEvalHandle,
                                           const ChessEvalPosition *p,
                                           double *out, char *error,
                                           uint64_t cap) {
  if (cap)
    error[0] = 0;
  ++calls;
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
void scores() {
  using S = SearchScore;
  check(S::white_mate(UINT64_MAX) >
            S::finite(std::numeric_limits<double>::max()),
        "mate above finite");
  check(S::black_mate(UINT64_MAX) <
            S::finite(-std::numeric_limits<double>::max()),
        "mate below finite");
  check(S::white_mate(0) > S::white_mate(1) &&
            S::black_mate(0) < S::black_mate(1),
        "mate ordering");
  check(S::finite(-0.0) == S::finite(0.0), "signed zero");
  rejects([] { S::finite(NAN); });
  rejects([] { S::finite(INFINITY); });
  rejects([] { S::finite(-INFINITY); });
  rejects([] { S::white_mate(UINT64_MAX).one_ply_farther(); });
  rejects([] { S::black_mate(UINT64_MAX).one_ply_farther(); });
  check(S::white_mate(0).one_ply_farther() == S::white_mate(1),
        "mate propagation");
  rejects([] { S::finite(1).distance(); });
  rejects([] { S::white_mate(1).value(); });
}
void trees() {
  tree_values = true;
  fail_at = 0;
  unsigned random = 41249;
  auto next = [&] {
    random = random * 1664525U + 1013904223U;
    return random;
  };
  for (int trial = 0; trial < 160; ++trial) {
    Tree t;
    std::function<int(int, Side)> generate = [&](int d, Side side) {
      int id = t.nodes.size();
      t.nodes.push_back({side, {}});
      values.push_back(0);
      values[id] =
          std::array<double, 7>{-1e300, -7, -0.0, 0, 3, 7, 1e300}[next() % 7];
      auto kind = next() % 17;
      if (kind == 0 && id)
        t.nodes[id].status = PositionStatus::checkmate;
      else if (kind == 1 && id)
        t.nodes[id].status = PositionStatus::stalemate;
      else if (kind == 2 && id)
        t.nodes[id].dead = true;
      else if (kind == 3)
        t.nodes[id].clock = 150;
      else if (kind == 4)
        t.nodes[id].clock = 100;
      if (d)
        for (int i = 0; i < 3; ++i) {
          int child =
              generate(d - 1, side == Side::white ? Side::black : Side::white);
          t.nodes[id].children.push_back(child);
        }
      else
        t.nodes[id].children.push_back(
            id); // legal claim probes at horizon; depth never recurses here
      return id;
    };
    values.clear();
    generate(3, trial % 2 ? Side::white : Side::black);
    auto root = t.initial_position();
    auto h = GameHistory::starting_at(t, *root, true);
    auto expected = oracle(t, 0, 3);
    for (bool ab : {false, true}) {
      auto e = evaluator();
      auto r = search(t, *root, h, *e, {3, ab});
      check(r.score == expected.score && r.decision == expected.action,
            "tree oracle score AND action");
      check(r.principal_variation == expected.pv && r.pv_end == expected.end &&
                r.pv_intended_move == expected.intended,
            "exact principal branch");
      check(ab || r.statistics.alpha_beta_cutoffs == 0, "disabled cutoffs");
      check(t.balance == 0 && t.id(*root) == 0 && h.occurrences().size() == 1,
            "tree restoration");
      verify_pv(t, *root, r);
    }
  }
  std::cout << "160 independently enumerated deterministic trees, both "
               "sides/modes: passed\n";
  // Explicit faster victory / slower defeat, mirrored for both root sides.
  for (Side side : {Side::white, Side::black})
    for (bool winning : {false, true}) {
      Side other = side == Side::white ? Side::black : Side::white;
      Tree t;
      if (winning)
        t.nodes = {{side, {1, 2}},
                   {other, {}, PositionStatus::checkmate},
                   {other, {3}},
                   {side, {4}},
                   {other, {}, PositionStatus::checkmate}};
      else
        t.nodes = {{side, {1, 3}},
                   {other, {2}},
                   {side, {}, PositionStatus::checkmate},
                   {other, {4}},
                   {side, {5}},
                   {other, {6}},
                   {side, {}, PositionStatus::checkmate}};
      values.assign(t.nodes.size(), 0);
      auto p = t.initial_position();
      auto h = GameHistory::starting_at(t, *p);
      auto expected = oracle(t, 0, 4);
      check(expected.action.move == (winning ? "a1a2" : "b1a2"),
            "mate distance preference oracle");
      for (bool ab : {false, true}) {
        auto e = evaluator();
        auto r = search(t, *p, h, *e, {4, ab});
        check(r.score == expected.score && r.decision == expected.action &&
                  r.principal_variation == expected.pv,
              "mate distance preference search");
        check(t.balance == 0, "mate tree restored");
      }
    }
  tree_values = false;
}
SearchResult both(const ChessCore &c, const Position &p, const GameHistory &h,
                  int depth = 1) {
  auto before = c.export_fen(p);
  auto history = h.occurrences();
  auto a = evaluator(), b = evaluator();
  auto x = search(c, p, h, *a, {depth, false}),
       y = search(c, p, h, *b, {depth, true});
  check(x.score && y.score, "successful real search");
  check(x.evaluator_identity.fingerprint == a->identity().fingerprint &&
            x.options.nominal_depth == depth && !x.options.alpha_beta_enabled &&
            x.history_incomplete == !h.complete(),
        "identity/configuration provenance");
  check(x.status == y.status && x.score == y.score &&
            x.decision == y.decision &&
            x.principal_variation == y.principal_variation &&
            x.pv_end == y.pv_end && x.pv_intended_move == y.pv_intended_move,
        "real chess AB oracle agreement");
  check(x.statistics.alpha_beta_cutoffs == 0, "exhaustive no cutoffs");
  check(c.export_fen(p) == before && h.occurrences() == history,
        "real root/history preserved");
  verify_pv(c, p, x);
  return x;
}
// Separate real-chess reference for small positions without a claim threshold
// in reach. Clones each edge and increments mate distance on return;
// deliberately has neither production traversal nor alpha/beta windows.
Expected chess_oracle(const ChessCore &c, const Position &p, int depth) {
  auto status = c.status(p);
  auto s = c.snapshot(p);
  if (status == PositionStatus::checkmate)
    return {s->side_to_move() == Side::white ? SearchScore::black_mate(0)
                                             : SearchScore::white_mate(0),
            {DecisionKind::terminal,
             {},
             s->side_to_move() == Side::white ? TerminalOutcome::black_win
                                              : TerminalOutcome::white_win},
            {},
            PVEnd::terminal,
            {}};
  if (status == PositionStatus::stalemate || c.known_dead_position(p))
    return {SearchScore::finite(0),
            {DecisionKind::terminal, {}, TerminalOutcome::draw},
            {},
            PVEnd::terminal,
            {}};
  check(s->halfmove_clock() + depth < 99, "reference excludes draw thresholds");
  if (!depth)
    return {SearchScore::finite(constant),
            {DecisionKind::play, {}},
            {},
            PVEnd::horizon,
            {}};
  std::vector<std::string> moves;
  for (auto &m : c.legal_moves(p))
    moves.push_back(c.encode_move(p, *m));
  std::sort(moves.begin(), moves.end());
  std::optional<Expected> best;
  for (auto &text : moves) {
    auto child = c.clone(p);
    auto m = c.decode_move(*child, text);
    auto token = c.make_move(*child, *m);
    auto x = chess_oracle(c, *child, depth - 1);
    x.score = x.score.one_ply_farther();
    x.action = {DecisionKind::play, text};
    x.pv.insert(x.pv.begin(), text);
    if (!best || (s->side_to_move() == Side::white ? x.score > best->score
                                                   : x.score < best->score))
      best = x;
  }
  check(bool(best), "reference legal moves");
  return *best;
}
void real_oracle_cases() {
  auto c = create_chess_core();
  tree_values = false;
  fail_at = 0;
  for (auto fen :
       {"7k/8/5KQ1/8/8/8/8/8 w - - 0 1", "4k3/8/8/8/8/8/P7/4K3 b - - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"})
    for (double v : {-1e300, 0.0, 1e300}) {
      constant = v;
      auto p = c->load_fen(fen);
      auto h = GameHistory::starting_at(*c, *p);
      auto expected = chess_oracle(*c, *p, 2);
      auto r = both(*c, *p, h, 2);
      check(r.score == expected.score && r.decision == expected.action &&
                r.principal_variation == expected.pv &&
                r.pv_end == expected.end,
            "independent genuine chess oracle");
    }
  std::cout
      << "independent genuine chess reference, extreme finite scores: passed\n";
}
void real_rules() {
  auto c = create_chess_core();
  tree_values = false;
  fail_at = 0;
  constant = 42;
  auto run = [&](std::string fen, int depth = 1) {
    auto p = c->load_fen(fen);
    auto h = GameHistory::starting_at(*c, *p);
    return both(*c, *p, h, depth);
  };
  for (auto fen : {"7k/6Q1/5K2/8/8/8/8/8 b - - 150 1",
                   "7K/6q1/5k2/8/8/8/8/8 w - - 150 1"}) {
    auto r = run(fen);
    auto expected = fen[0] == '7' && fen[1] == 'k' ? TerminalOutcome::white_win
                                                   : TerminalOutcome::black_win;
    check(r.decision == ChessDecision{DecisionKind::terminal, {}, expected},
          "explicit checkmate winner payload");
    check(r.status == SearchStatus::terminal && r.completed_depth == 0 &&
              r.score->distance() == 0 && r.statistics.evaluator_calls == 0,
          "mate overrides 75 moves");
  }
  for (auto fen :
       {"7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", "4k3/8/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", "4k3/8/8/8/8/8/8/2N1K3 w - - 0 1"}) {
    auto r = run(fen);
    check(r.status == SearchStatus::terminal &&
              r.score == SearchScore::finite(0) &&
              r.statistics.evaluator_calls == 0,
          "stalemate/known dead");
    check(r.decision->terminal_outcome == TerminalOutcome::draw,
          "drawn root payload");
  }
  auto mate = run("7k/8/5KQ1/8/8/8/8/8 w - - 149 1");
  check(mate.score == SearchScore::white_mate(1) &&
            mate.pv_end == PVEnd::terminal,
        "depth zero mate precedence and distance");
  // Opponent at horizon can claim a draw but must compare against its static
  // continuation: Black keeps -42, selects zero over +42 (and on exact tie).
  for (double value : {-42.0, 0.0, 42.0}) {
    constant = value;
    auto r = run("4k3/8/8/8/8/8/8/R3K3 w - - 98 1");
    check(r.score == SearchScore::finite(std::min(value, 0.0)),
          "horizon intended claim vs static continuation");
    check((r.pv_end == PVEnd::claim_draw_with_intended_move) == (value >= 0),
          "horizon claim tie preference");
  }
  constant = -42;
  auto intended = run("4k3/8/8/8/8/8/8/R3K3 w - - 99 1");
  check(intended.decision->kind ==
                DecisionKind::claim_draw_with_intended_move &&
            intended.principal_variation.empty(),
        "50-move intended unplayed claim");
  check(intended.decision->move == "a1a2", "lexicographic intended claim");
  auto now = run("4k3/8/8/8/8/8/8/R3K3 w - - 100 1");
  check(now.status == SearchStatus::complete &&
            now.decision->kind == DecisionKind::claim_draw,
        "50 optional immediate");
  auto auto_draw = run("4k3/8/8/8/8/8/8/R3K3 w - - 150 1");
  check(auto_draw.status == SearchStatus::terminal &&
            auto_draw.statistics.evaluator_calls == 0,
        "75 automatic");
  check(auto_draw.decision->terminal_outcome == TerminalOutcome::draw,
        "75 move draw payload");
  constant = 42;
  auto win = run("4k3/8/8/8/8/8/P7/R3K3 w - - 100 1");
  check(win.decision->kind == DecisionKind::play &&
            win.score == SearchScore::finite(42),
        "decline immediate claim to win");
  constant = 0;
  auto tie = run("4k3/8/8/8/8/8/8/R3K3 w - - 100 1");
  check(tie.decision->kind == DecisionKind::claim_draw, "claim on exact tie");
  // Clock resets: authoritative ChessCore snapshots, then search across the
  // threshold. White can retain its favorable score only through a reset.
  constant = 10;
  for (auto fen : {"4k3/8/8/8/8/8/P7/4K3 w - - 99 1",
                   "4k3/8/8/8/8/8/r7/R3K3 w - - 99 1"}) {
    auto p = c->load_fen(fen);
    auto h = GameHistory::starting_at(*c, *p);
    auto r = both(*c, *p, h);
    check(r.score == SearchScore::finite(10) &&
              r.decision->kind == DecisionKind::play,
          "pawn/capture avoids opponent claim");
    auto m = c->decode_move(*p, r.decision->move);
    auto token = c->make_move(*p, *m);
    check(c->snapshot(*p)->halfmove_clock() == 0, "authoritative clock reset");
    c->undo_move(*p, *token);
  }
  // FIDE signatures: raw noncapturable and pinned EP disappear; legal EP and
  // castling rights distinguish otherwise equal piece placements.
  auto sig = [&](std::string_view fen) {
    auto p = c->load_fen(fen);
    return repetition_signature(*c, *p);
  };
  check(sig("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 2") ==
            sig("4k3/8/8/3p4/8/8/8/4K3 w - - 8 7"),
        "noncapturable EP normalized and clocks excluded");
  check(sig("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2") ==
            sig("k3r3/8/8/3pP3/8/8/8/4K3 w - - 0 2"),
        "pinned EP normalized");
  check(sig("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2") !=
            sig("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 2"),
        "legal EP retained");
  check(sig("4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1") !=
            sig("4k3/8/8/8/8/8/8/R3K2R w - - 0 1"),
        "castling rights retained");
  auto p = c->initial_position();
  auto start = c->clone(*p);
  auto h = GameHistory::starting_at(*c, *p, true);
  std::vector<std::string> cycle = {"g1f3", "g8f6", "f3g1", "f6g8"};
  std::vector<std::string> replay;
  constant = 0;
  for (int i = 0; i < 16; ++i) {
    auto text = cycle[i % 4];
    h = h.after_move(*c, *p, text);
    replay.push_back(text);
    auto m = c->decode_move(*p, text);
    auto token = c->make_move(*p, *m);
    if (i == 6) {
      auto r = both(*c, *p, h);
      check(r.decision ==
                    ChessDecision{DecisionKind::claim_draw_with_intended_move,
                                  "f6g8"} &&
                r.principal_variation.empty(),
            "third occurrence intended claim");
    }
    if (i == 7) {
      auto r = both(*c, *p, h);
      check(r.decision->kind == DecisionKind::claim_draw &&
                r.status == SearchStatus::complete,
            "exact third occurrence optional");
      auto imported = GameHistory::starting_at(*c, *p);
      auto fresh = both(*c, *p, imported);
      check(fresh.history_incomplete &&
                fresh.decision->kind == DecisionKind::play,
            "unknown prefix not invented");
      auto replayed = GameHistory::replay(*c, *start, replay, true);
      check(replayed.matches_root(*c, *p) &&
                replayed.occurrences() == h.occurrences(),
            "replay actual history");
    }
    if (i == 15) {
      auto r = both(*c, *p, h);
      check(r.status == SearchStatus::terminal &&
                r.score == SearchScore::finite(0),
            "exact fifth automatic");
      check(r.decision->terminal_outcome == TerminalOutcome::draw,
            "fivefold draw payload");
    }
  }
  auto imported = GameHistory::starting_at(*c, *p);
  auto changed =
      c->load_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  check(!imported.matches_root(*c, *changed),
        "clocks checked for root consistency");
  auto e = evaluator();
  check(search(*c, *changed, imported, *e).status == SearchStatus::search_error,
        "invalid history rejected");
  for (int depth : {-1, 0, 65, std::numeric_limits<int>::max()})
    check(search(*c, *p, imported, *e, {depth, true}).status ==
              SearchStatus::search_error,
          "invalid depth bounded");
  // Real nontrivial positions and Black perspective; depth 2/3 exhaustive
  // reference comparisons with legal PVs, finite scores outside mate sentinels.
  for (double v : {-1e300, 0.0, 1e300}) {
    constant = v;
    run("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        2);
    run("4k3/8/8/8/8/8/P7/4K3 b - - 0 1", 3);
  }
  // Depth-zero stalemate beats a hostile static value.
  Tree t;
  t.nodes = {{Side::white, {1}},
             {Side::black, {2}, PositionStatus::stalemate},
             {Side::white, {2}}};
  auto root = t.initial_position();
  auto hist = GameHistory::starting_at(t, *root);
  constant = 1e300;
  auto r = both(t, *root, hist);
  check(r.score == SearchScore::finite(0) && r.statistics.evaluator_calls == 0,
        "depth zero stalemate");
  std::cout << "real FIDE rules, repetition/EP/history, PV and fixed-depth "
               "cases: passed\n";
}
void failures() {
  Tree t;
  t.nodes = {{Side::white, {1, 2, 3}},
             {Side::black, {1}},
             {Side::black, {2}},
             {Side::black, {3}}};
  auto p = t.initial_position();
  auto h = GameHistory::starting_at(t, *p, true);
  tree_values = false;
  constant = 7;
  for (bool ab : {false, true})
    for (int mode = 0; mode < 5; ++mode) {
      fail_at = 2;
      fail_mode = mode;
      auto e = evaluator();
      auto r = search(t, *p, h, *e, {1, ab});
      check(r.status == SearchStatus::evaluator_failed && !r.decision &&
                !r.score && r.principal_variation.empty() && !r.pv_end &&
                r.completed_depth == 0 && !r.diagnostic.empty(),
            "entire search invalidated");
      check(calls == 2 && !e->usable() && t.balance == 0 && t.id(*p) == 0 &&
                h.occurrences().size() == 1,
            "late failure quarantine and restoration");
      r = search(t, *p, h, *e);
      check(r.status == SearchStatus::evaluator_failed && calls == 2,
            "quarantine no further ABI dispatch");
      fail_at = 0;
      auto replacement = evaluator();
      auto ok = search(t, *p, h, *replacement);
      check(ok.status == SearchStatus::complete, "replacement recovers");
    }
  auto e = evaluator();
  t.throw_at = 2;
  auto r = search(t, *p, h, *e);
  check(r.status == SearchStatus::search_error && e->usable() && !r.decision &&
            !r.score && t.balance == 0,
        "traversal error distinct and restored");
  t.throw_at = -1;
  // On cleanup failure the private clone is discarded and the traversal error
  // is reported; evaluator quarantine remains intact. No caller state is lost.
  t.undo_throw_at = 2;
  fail_at = 2;
  auto failing = evaluator();
  r = search(t, *p, h, *failing);
  check(r.status == SearchStatus::search_error && !r.score && !r.decision &&
            t.id(*p) == 0 && !failing->usable(),
        "undo failure cannot be hidden as evaluator failure");
  fail_at = 0;
  std::cout << "late ABI failures, quarantine/replacement and traversal "
               "errors: passed\n";
}
int main(int argc, char **argv) try {
  if (argc > 1)
    digest = argv[1];
  scores();
  trees();
  real_oracle_cases();
  real_rules();
  failures();
  std::cout << "M3 search passed\n";
} catch (const std::exception &e) {
  std::cerr << "M3 failure: " << e.what() << '\n';
  return 1;
}
