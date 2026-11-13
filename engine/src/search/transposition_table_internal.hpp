#pragma once
#include <limits>
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
// Draw eligibility is maintained by Traversal::history; no duplicate
// count vector or TT-adjacent push/pop state is needed.
// PositionSnapshot clocks are uint32_t; store only exact ABI-relevant fields
// that can differ between equal repetition signatures. 12 bytes, not a full
// historical context or evaluator snapshot.
struct EvaluationExtras {
  std::uint32_t halfmove_clock = 0, fullmove_number = 0;
  std::uint8_t raw_ep = 64;
  bool operator==(const EvaluationExtras &) const = default;
};
inline EvaluationExtras evaluation_extras(const ChessEvalPosition &s) {
  if (s.halfmove_clock > UINT32_MAX || s.fullmove_number > UINT32_MAX ||
      (s.en_passant_square != CHESS_EVAL_EP_NONE && s.en_passant_square > 63))
    throw std::invalid_argument("TT evaluator snapshot outside chess ABI range");
  return {static_cast<std::uint32_t>(s.halfmove_clock),
          static_cast<std::uint32_t>(s.fullmove_number),
          static_cast<std::uint8_t>(s.en_passant_square == CHESS_EVAL_EP_NONE
                                    ? 64 : s.en_passant_square)};
}
inline EvaluationExtras evaluation_extras(const PositionSnapshot &s) noexcept {
  return {s.halfmove_clock(), s.fullmove_number(),
          static_cast<std::uint8_t>(s.en_passant_square().value_or(64))};
}
// An optional claim is a locally attainable result of zero. A TT return
// cannot cause the claimant to accept an immediately worse estimate.
inline bool cached_score_worse_than_claim(SearchScore score, Side side) {
  auto draw = SearchScore::finite(0);
  return side == Side::white ? score < draw : score > draw;
}
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
  std::size_t selected_slot=std::numeric_limits<std::size_t>::max(); // reward only a score actually returned by search
};
// Private access also supports deliberate collision/bound fixtures, without
// exposing injectable hashing or raw entry mutation in the public API.
struct TTAccess {
  static void observe_verification(TranspositionTable &, void (*)(std::uint64_t) noexcept);
  static bool acquire(TranspositionTable &);
  static void release(TranspositionTable &) noexcept;
  static void force_collisions(TranspositionTable &, bool);
  static void begin_generation(TranspositionTable &) noexcept;
  static void reward(TranspositionTable &,const TTProbe &) noexcept;
  static TTProbe probe(TranspositionTable &, const RepetitionSignature &,
                       std::uint64_t signature_hash_value, int depth,
                       std::string_view evaluator, std::uint64_t policy,
                       EvaluatorCacheCompatibility compatibility,
                       std::optional<EvaluationExtras> extras,
                       bool numeric, std::uint64_t ply, TranspositionStatistics &,
                       const CancellationToken &);
  static void store(TranspositionTable &, const RepetitionSignature &,
                    std::uint64_t signature_hash_value, int depth,
                    std::string_view evaluator, std::uint64_t policy,
                    EvaluatorCacheCompatibility compatibility,
                    std::optional<EvaluationExtras> extras,
                    bool numeric, SearchScore score, TTBound,
                    const ChessDecision &, std::uint64_t ply,
                    TranspositionStatistics &);
};
class TTSession {
public:
  TTSession(TranspositionTableUse use, std::string_view evaluator, bool complete,
            TranspositionStatistics &stats, CancellationToken cancellation);
  ~TTSession();
  TTSession(const TTSession &) = delete;
  bool numeric() const noexcept { return numeric_; }
  bool enabled() const noexcept { return table_ != nullptr; }
  void start(const std::vector<RepetitionSignature> &);
  void reward(const TTProbe &hit) noexcept {
    if(table_) TTAccess::reward(*table_,hit);
  }
  // Optional node-local cache avoids rehashing on the probe/store pair.
  // Never stored in TTSession: recursive search shares one session.
  TTProbe probe(const RepetitionSignature &, int depth, std::uint64_t ply,
                const PositionSnapshot &,
                std::optional<std::uint64_t> *node_hash = nullptr);
  void store(const RepetitionSignature &, int depth,
             SearchScore, TTBound, const ChessDecision &, std::uint64_t ply,
             const PositionSnapshot &,
             std::optional<std::uint64_t> *node_hash = nullptr);
  // Private direct-fixture overloads: ABI extras matter only for deterministic
  // complete-snapshot evaluators, never for position-only certification.
  TTProbe probe(const ChessEvalPosition &s, const RepetitionSignature &r,
                int depth, std::uint64_t ply);
  void store(const ChessEvalPosition &s, const RepetitionSignature &r, int depth,
             SearchScore score, TTBound bound, const ChessDecision &action,
             std::uint64_t ply);
private:
  TranspositionTable *table_ = nullptr;
  TranspositionTableUse use_;
  std::string_view evaluator_;
  bool numeric_;
  EvaluatorCacheCompatibility compatibility_;
  TTProbe probe_impl(const RepetitionSignature &, int, std::uint64_t,
                     std::optional<EvaluationExtras>,
                     std::optional<std::uint64_t> *node_hash = nullptr);
  void store_impl(const RepetitionSignature &, int, SearchScore, TTBound,
                  const ChessDecision &, std::uint64_t,
                  std::optional<EvaluationExtras>,
                  std::optional<std::uint64_t> *node_hash = nullptr);
  TranspositionStatistics &stats_;
  CancellationToken cancellation_;
};
} // namespace chess_lab::detail
