#pragma once
#include <array>
#include <atomic>
#include <chess_lab/chess_core.hpp>
#include <chess_lab/evaluation.hpp>
#include <chess_lab/transposition_table.hpp>
#include <chrono>
#include <compare>
#include <functional>
#include <optional>

namespace chess_lab {
// Source-level value, not a wire ABI. All scores are White-perspective.
class alignas(8) SearchScore {
public:
  enum class Kind : std::uint64_t { finite, white_mate, black_mate };
  static SearchScore finite(double);
  static SearchScore white_mate(std::uint64_t);
  static SearchScore black_mate(std::uint64_t);
  Kind kind() const noexcept { return kind_; }
  double value() const;                // throws for a mate
  std::uint64_t distance() const;      // throws for a finite value
  SearchScore one_ply_farther() const; // checked increment
  std::strong_ordering operator<=>(const SearchScore &) const noexcept;
  bool operator==(const SearchScore &) const noexcept;

private:
  SearchScore(Kind k, double v, std::uint64_t d)
      : kind_(k), value_(v), distance_(d) {}
  Kind kind_;
  double value_;
  std::uint64_t distance_;
};
static_assert(sizeof(SearchScore) == 24 && alignof(SearchScore) == 8);

// Exact repetition evidence; raw EP is normalized through legal move
// generation.
struct RepetitionSignature {
  std::array<PieceCode, 64> pieces{};
  Side side{};
  std::array<bool, 4> castling{};
  std::optional<std::uint8_t> legal_ep;
  bool operator==(const RepetitionSignature &) const = default;
};
RepetitionSignature repetition_signature(const ChessCore &, const Position &);

// Immutable by API. Helpers accept only actual legal moves. The final full
// snapshot (including raw EP and clocks) must agree with the search root.
class GameHistory {
public:
  static GameHistory starting_at(const ChessCore &, const Position &,
                                 bool complete = false);
  static GameHistory replay(const ChessCore &, const Position &start,
                            const std::vector<std::string> &moves,
                            bool complete = false);
  GameHistory after_move(const ChessCore &, const Position &current,
                         std::string_view move) const;
  bool matches_root(const ChessCore &, const Position &) const;
  bool complete() const noexcept { return complete_; }
  const std::vector<RepetitionSignature> &occurrences() const noexcept {
    return occurrences_;
  }

private:
  std::vector<RepetitionSignature> occurrences_;
  ChessEvalPosition last_{};
  bool complete_ = false;
};
enum class DecisionKind {
  play,
  claim_draw,
  claim_draw_with_intended_move,
  terminal
};
enum class TerminalOutcome { white_win, black_win, draw };
struct ChessDecision {
  DecisionKind kind;
  // Played move for play; announced, UNPLAYED move for an intended claim.
  std::string move;
  // Present exactly for terminal decisions, never for a play or draw claim.
  std::optional<TerminalOutcome> terminal_outcome{};
  bool operator==(const ChessDecision &) const = default;
};
enum class DecisionSource { search, opening_book }; // book integration deferred
struct SearchOptions {
  // Bounded to [1,64] to reject unreasonable recursive stack requests.
  int nominal_depth = 1;
  bool alpha_beta_enabled = true;
};
enum class SearchStatus { complete, terminal, evaluator_failed, search_error };
enum class PVEnd {
  horizon,
  terminal,
  claim_draw,
  claim_draw_with_intended_move
};
struct SearchStatistics {
  // Recursive node entries (including terminals); prospective claim probes do
  // not count as nodes. Calls count attempted host evaluations. Cutoffs count
  // nodes where a closed alpha/beta window avoids remaining traversal.
  std::uint64_t visited_nodes = 0, evaluator_calls = 0, alpha_beta_cutoffs = 0;
  // Whole invocation, including validation/cloning and failure cleanup.
  std::chrono::nanoseconds elapsed{};
  TranspositionStatistics transposition;
};
struct SearchResult {
  SearchStatus status = SearchStatus::search_error;
  DecisionSource source = DecisionSource::search;
  std::optional<ChessDecision> decision;
  std::optional<SearchScore> score;
  int completed_depth = 0;
  std::vector<std::string> principal_variation;
  std::optional<PVEnd> pv_end;
  std::string pv_intended_move;
  SearchStatistics statistics;
  EvaluationIdentity evaluator_identity;
  SearchOptions options;
  bool history_incomplete = true;
  std::string diagnostic;
};
SearchResult search(const ChessCore &, const Position &, const GameHistory &,
                    Evaluator &, SearchOptions = {});

// One source per invocation. Copies share a one-way signal; there is no reset.
// Tokens keep the state alive even after the source has been destroyed. An
// absent token never cancels. Native requests are thread-safe; WASM transport
// (e.g. shared memory) is the controller's responsibility.
class CancellationToken {
public:
  CancellationToken() = default;
  bool requested() const noexcept {
    return state_ && state_->load(std::memory_order_relaxed);
  }
private:
  std::shared_ptr<std::atomic<bool>> state_;
  explicit CancellationToken(std::shared_ptr<std::atomic<bool>> s)
      : state_(std::move(s)) {}
  friend class CancellationSource;
};
class CancellationSource {
public:
  CancellationSource() : state_(std::make_shared<std::atomic<bool>>(false)) {}
  CancellationToken token() const { return CancellationToken(state_); }
  void request() const noexcept { state_->store(true, std::memory_order_relaxed); }
private:
  std::shared_ptr<std::atomic<bool>> state_;
};
struct IterativeSearchOptions {
  std::optional<int> max_depth;
  bool alpha_beta_enabled = true;
  // Bounded to [1,256]; returned size also cannot exceed legal decisions.
  std::size_t multipv_count = 1;
  bool previous_pv_ordering_enabled = true;
  // MultiPV=1 searches PLAY only at the actual root, sharing root bounds.
  // Descendant claims are unchanged. False retains the M4 exact-ranking path
  // (including root claims), for legacy/reference callers. MultiPV>1 is exact.
  bool single_pv_play_only = true;
};
enum class IterativeSearchStatus {
  depth_reached, cancelled, depth_limit_reached, terminal,
  evaluator_failed, search_error
};
struct SearchVariation {
  ChessDecision decision;
  SearchScore score;
  std::vector<std::string> principal_variation;
  PVEnd pv_end;
  std::string pv_intended_move;
};
struct SearchProvenance {
  EvaluationIdentity evaluator_identity;
  IterativeSearchOptions options;
  // Exact snapshot and occurrence evidence, not a collision-prone cache key.
  ChessEvalPosition root{};
  std::vector<RepetitionSignature> history;
  bool history_incomplete = true;
  bool tt_numeric_certified = false;
  std::uint64_t tt_value_policy = 1;
};
struct CompletedDepth {
  int depth;
  SearchVariation best;
  std::vector<SearchVariation> multipv;
  SearchStatistics statistics; // cumulative through this commit
  SearchProvenance provenance;
};
struct IterativeSearchResult {
  IterativeSearchStatus status = IterativeSearchStatus::search_error;
  std::optional<CompletedDepth> completed;
  // Present ONLY on cancellation before depth one; explicitly unevaluated.
  std::optional<ChessDecision> unevaluated_fallback;
  // Present ONLY for a terminal root, at depth zero, without PV/ranking.
  std::optional<ChessDecision> terminal_decision;
  std::optional<SearchScore> terminal_score;
  SearchStatistics statistics; // includes any abandoned iteration
  SearchProvenance provenance;
  std::string diagnostic;
};
// Synchronous, single-threaded traversal. Callback runs on the calling thread
// after commit, receives a detached value, may cancel, and must not reenter on
// the same active resources. ALL progress is provisional: a later failure
// invalidates it. Root/history are immutable; the owner serializes evaluator
// use/replacement and handles session IDs, workers, clocks and stale results.
using SearchProgress = std::function<void(CompletedDepth)>;
IterativeSearchResult iterative_search(
    const ChessCore &, const Position &, const GameHistory &, Evaluator &,
    IterativeSearchOptions = {}, CancellationToken = {}, SearchProgress = {},
    TranspositionTableUse = {});
} // namespace chess_lab
