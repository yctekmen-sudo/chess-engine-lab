#pragma once
#include <chess_lab/search.hpp>
#include <map>
#include <string_view>
#include <tuple>

namespace chess_lab::detail {
struct SearchCancelled {};
inline void tt_poll(const CancellationToken &t) {
  if (t.requested()) throw SearchCancelled{};
}
struct SignatureLess {
  bool operator()(const RepetitionSignature &a, const RepetitionSignature &b) const {
    return std::tie(a.pieces, a.side, a.castling, a.legal_ep) <
           std::tie(b.pieces, b.side, b.castling, b.legal_ep);
  }
};
struct OccurrenceCount {
  std::uint64_t total = 0, verified = 0;
};
using OccurrenceCounts = std::map<RepetitionSignature, OccurrenceCount, SignatureLess>;
enum class TTBound { exact, lower, upper };
TTBound classify_bound(SearchScore, std::optional<SearchScore> original_alpha,
                       std::optional<SearchScore> original_beta);
std::optional<SearchScore> to_node_score(SearchScore, std::uint64_t ply);
std::optional<SearchScore> to_root_score(SearchScore, std::uint64_t ply);
std::uint64_t signature_hash(const RepetitionSignature &);
bool same_eval_state(const ChessEvalPosition &, const ChessEvalPosition &);
struct TTProbe {
  std::optional<SearchScore> score;
  TTBound bound = TTBound::exact;
  std::optional<ChessDecision> decision;
  std::string hint;
};
// Private access also supports deliberate collision/bound fixtures, without
// exposing injectable hashing or raw entry mutation in the public API.
struct TTAccess {
  // Private deterministic fixture seam, per table and configured only while idle.
  // The observer runs inside exact history verification before its next poll.
  static void observe_verification(TranspositionTable &, void (*)(std::uint64_t) noexcept);
  static bool acquire(TranspositionTable &);
  static void release(TranspositionTable &) noexcept;
  static void force_collisions(TranspositionTable &, bool);
  static std::uint32_t append(TranspositionTable &, std::uint32_t,
                              const RepetitionSignature &, TranspositionStatistics &,
                              const CancellationToken &);
  static void drop(TranspositionTable &, std::uint32_t) noexcept;
  static TTProbe probe(TranspositionTable &, const ChessEvalPosition &,
                       const RepetitionSignature &, OccurrenceCounts &,
                       std::uint64_t history_hash, bool complete, int depth,
                       std::string_view evaluator, std::uint64_t policy,
                       bool numeric, std::uint64_t ply, TranspositionStatistics &,
                       const CancellationToken &);
  static void store(TranspositionTable &, const ChessEvalPosition &,
                     const RepetitionSignature &, std::uint64_t history_hash,
                     bool complete, int depth, std::string_view evaluator,
                     std::uint64_t policy, std::uint32_t tail, bool numeric,
                     SearchScore score, TTBound, const ChessDecision &,
                     std::uint64_t ply, TranspositionStatistics &);
};
class TTSession {
public:
  static constexpr std::uint32_t invalid = UINT32_MAX;
  TTSession(TranspositionTableUse use, std::string_view evaluator, bool complete,
            TranspositionStatistics &stats, CancellationToken cancellation);
  ~TTSession();
  TTSession(const TTSession &) = delete;
  bool numeric() const noexcept { return numeric_; }
  bool enabled() const noexcept { return table_ != nullptr; }
  void start(const std::vector<RepetitionSignature> &);
  std::uint32_t push(const RepetitionSignature &);
  void pop(const RepetitionSignature &, std::uint32_t old_tail) noexcept;
  std::size_t count(const RepetitionSignature &) const;
  TTProbe probe(const ChessEvalPosition &, const RepetitionSignature &, int depth,
                std::uint64_t ply);
  void store(const ChessEvalPosition &, const RepetitionSignature &, int depth,
             SearchScore, TTBound, const ChessDecision &, std::uint64_t ply);
private:
  void increment(const RepetitionSignature &);
  void decrement(const RepetitionSignature &) noexcept;
  TranspositionTable *table_ = nullptr;
  TranspositionTableUse use_;
  std::string_view evaluator_;
  bool complete_, numeric_;
  TranspositionStatistics &stats_;
  CancellationToken cancellation_;
  OccurrenceCounts counts_;
  std::uint64_t history_hash_ = 0;
  std::uint32_t tail_ = 0;
};
} // namespace chess_lab::detail
