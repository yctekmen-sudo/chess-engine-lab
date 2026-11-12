#pragma once
#include <chess_lab/search.hpp>
#include <map>
#include <algorithm>
#include <vector>
#include <utility>
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
// B4.2: small repetition maps live in a single contiguous buffer.
// At the observed ~5 entries a sequential lookup avoids tree pointer chasing.
// Insertion keeps SignatureLess order so numeric TT history verification
// visits the same signatures as the original std::map.
class OccurrenceCounts {
public:
  using value_type = std::pair<RepetitionSignature, OccurrenceCount>;
  using container_type = std::vector<value_type>;
  using iterator = container_type::iterator;
  using const_iterator = container_type::const_iterator;
private:
  container_type entries_;
public:
  iterator begin() noexcept { return entries_.begin(); }
  iterator end() noexcept { return entries_.end(); }
  const_iterator begin() const noexcept { return entries_.begin(); }
  const_iterator end() const noexcept { return entries_.end(); }
  std::size_t size() const noexcept { return entries_.size(); }
  std::size_t capacity() const noexcept { return entries_.capacity(); }
  iterator find(const RepetitionSignature &s) noexcept {
    return std::find_if(entries_.begin(), entries_.end(),
                        [&](const value_type &v) { return v.first == s; });
  }
  const_iterator find(const RepetitionSignature &s) const noexcept {
    return std::find_if(entries_.begin(), entries_.end(),
                        [&](const value_type &v) { return v.first == s; });
  }
  OccurrenceCount &operator[](const RepetitionSignature &s) {
    if (auto found = find(s); found != end()) return found->second;
    // Keep the tree's comparator-defined iteration order; only new keys shift.
    const auto insertion = std::lower_bound(entries_.begin(), entries_.end(), s,
       [](const value_type &v, const RepetitionSignature &key) {
         return SignatureLess{}(v.first,key);
       });
    return entries_.insert(insertion,value_type{s,{}})->second;
  }
  void erase(iterator it) noexcept { entries_.erase(it); }
};
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
