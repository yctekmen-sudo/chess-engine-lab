#include "transposition_table_internal.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace chess_lab {
namespace {
constexpr std::uint32_t invalid = UINT32_MAX;
constexpr std::size_t ways = 4;
std::uint64_t mix(std::uint64_t x) {
  x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27; x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
std::uint64_t snapshot_hash(const ChessEvalPosition &p) {
  std::uint64_t h = mix(p.struct_size ^ p.schema_major ^ (p.schema_minor << 8));
  for (auto b : p.bitboards) h = mix(h ^ b);
  for (auto v : {p.side_to_move, p.castling_rights, p.en_passant_square,
                 p.halfmove_clock, p.fullmove_number}) h = mix(h ^ v);
  return h;
}
struct CompactDecision {
  DecisionKind kind = DecisionKind::play;
  char move[6]{};
  std::optional<TerminalOutcome> outcome;
  CompactDecision(const ChessDecision &d = {DecisionKind::play, {}})
      : kind(d.kind), outcome(d.terminal_outcome) {
    if (d.move.size() > 5) throw std::logic_error("noncanonical TT decision");
    std::copy(d.move.begin(), d.move.end(), move);
  }
  ChessDecision expand() const { return {kind, move, outcome}; }
};
struct Entry {
  bool used = false, complete = false;
  int depth = 0;
  std::uint64_t position_hash = 0, composite = 0, policy = 0, touched = 0;
  ChessEvalPosition snapshot{};
  RepetitionSignature signature;
  std::array<char, 64> evaluator{};
  std::optional<SearchScore> score;
  detail::TTBound bound = detail::TTBound::exact;
  CompactDecision decision;
  std::uint32_t tail = invalid;
};
struct HistoryNode {
  RepetitionSignature signature;
  std::uint32_t previous = 0, references = 0, next = 0;
};
struct Exclusive {
  std::atomic<bool> &flag;
  explicit Exclusive(std::atomic<bool> &f) : flag(f) {
    bool expected = false;
    if (!flag.compare_exchange_strong(expected, true))
      throw std::logic_error("transposition table is already active");
  }
  ~Exclusive() { flag.store(false); }
};
}
struct TranspositionTable::Impl {
  std::unique_ptr<std::byte[]> arena;
  Entry *entries = nullptr;
  HistoryNode *history = nullptr;
  std::uint32_t *history_index = nullptr;
  std::size_t buckets = 0, slots = 0, nodes = 0, allocated = 0, used = 0;
  std::uint32_t free_head = 0;
  std::uint64_t tick = 0, mask = UINT64_MAX;
  explicit Impl(std::size_t bucket_count) : buckets(bucket_count) {
    slots = buckets * ways; nodes = slots * 8;
    // Every component has at most alignof(Entry); explicit alignment below.
    auto offset = slots * sizeof(Entry);
    const auto history_offset = offset;
    offset += (nodes + 1) * sizeof(HistoryNode);
    const auto index_offset = offset;
    offset += nodes * sizeof(std::uint32_t);
    allocated = sizeof(Impl) + offset;
    arena = std::make_unique<std::byte[]>(offset);
    entries = reinterpret_cast<Entry *>(arena.get());
    history = reinterpret_cast<HistoryNode *>(arena.get() + history_offset);
    history_index = reinterpret_cast<std::uint32_t *>(arena.get() + index_offset);
    for (std::size_t i = 0; i < slots; ++i) std::construct_at(entries + i);
    for (std::size_t i = 0; i <= nodes; ++i) std::construct_at(history + i);
    reset();
  }
  void reset() noexcept {
    for (std::size_t i = 0; i < slots; ++i) entries[i] = Entry{};
    std::fill_n(history_index, nodes, 0);
    for (std::size_t i = 1; i <= nodes; ++i) {
      history[i].references = 0;
      history[i].next = i == nodes ? 0 : std::uint32_t(i + 1);
    }
    free_head = 1; used = 0; tick = 0;
  }
  std::size_t node_bucket(std::uint32_t parent, const RepetitionSignature &s) const {
    return mix(detail::signature_hash(s) ^ mix(parent)) % nodes;
  }
  void drop(std::uint32_t id) noexcept {
    while (id != 0 && id != invalid) {
      auto &n = history[id];
      if (--n.references) return;
      auto bucket = node_bucket(n.previous, n.signature);
      auto *link = &history_index[bucket];
      while (*link != id) link = &history[*link].next;
      *link = n.next;
      auto parent = n.previous;
      n.next = free_head; free_head = id;
      id = parent;
    }
  }
  void erase(std::size_t slot, TranspositionStatistics &stats, bool eviction = true) noexcept {
    auto &e = entries[slot];
    if (!e.used) return;
    drop(e.tail); e = Entry{}; --used;
    if (eviction) ++stats.evictions;
  }
  auto priority(std::size_t slot) const {
    const auto &e = entries[slot];
    return std::tuple(e.touched / 8, e.depth, e.touched, slot);
  }
  std::size_t victim(std::size_t begin, std::size_t end) const {
    auto best = begin;
    for (auto i = begin; i < end; ++i) {
      if (!entries[i].used) return i;
      if (priority(i) < priority(best)) best = i;
    }
    return best;
  }
};
TranspositionTable::TranspositionTable(std::size_t bytes) { resize(bytes); }
TranspositionTable::~TranspositionTable() = default;
void TranspositionTable::clear() {
  Exclusive idle(active_);
  if (impl_) impl_->reset();
  entries_.store(0);
}
void TranspositionTable::resize(std::size_t bytes) {
  Exclusive idle(active_);
  const std::size_t per_bucket = ways * sizeof(Entry) + ways * 8 *
      (sizeof(HistoryNode) + sizeof(std::uint32_t));
  const std::size_t overhead = sizeof(Impl) + sizeof(HistoryNode);
  std::unique_ptr<Impl> next;
  if (bytes >= overhead && (bytes - overhead) / per_bucket >= 1) {
    auto max_buckets = (bytes - overhead) / per_bucket;
    // Index types are fixed-width; avoid arithmetic overflow/unindexable pools.
    max_buckets = std::min(max_buckets, std::size_t((UINT32_MAX - 1) / (ways * 8)));
    std::size_t count = 1;
    while (count <= max_buckets / 2) count *= 2;
    next = std::make_unique<Impl>(count);
  }
  impl_.swap(next); // allocation failure leaves the original table intact
  capacity_.store(impl_ ? impl_->allocated : 0); entries_.store(0);
}
namespace detail {
std::uint64_t signature_hash(const RepetitionSignature &s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (auto p : s.pieces) h = (h ^ std::uint64_t(p)) * 1099511628211ULL;
  h = mix(h ^ std::uint64_t(s.side));
  for (bool b : s.castling) h = mix(h ^ std::uint64_t(b));
  return mix(h ^ (s.legal_ep ? *s.legal_ep : 64));
}
bool same_eval_state(const ChessEvalPosition &a, const ChessEvalPosition &b) {
  return a.struct_size == b.struct_size && a.schema_major == b.schema_major &&
    a.schema_minor == b.schema_minor &&
    std::equal(std::begin(a.bitboards), std::end(a.bitboards), std::begin(b.bitboards)) &&
    a.side_to_move == b.side_to_move && a.castling_rights == b.castling_rights &&
    a.en_passant_square == b.en_passant_square && a.halfmove_clock == b.halfmove_clock &&
    a.fullmove_number == b.fullmove_number;
}
TTBound classify_bound(SearchScore s, std::optional<SearchScore> alpha,
                       std::optional<SearchScore> beta) {
  if (alpha && s <= *alpha) return TTBound::upper;
  if (beta && s >= *beta) return TTBound::lower;
  return TTBound::exact;
}
std::optional<SearchScore> to_node_score(SearchScore s, std::uint64_t ply) {
  if (s.kind() == SearchScore::Kind::finite) return s;
  if (s.distance() < ply) return {};
  return s.kind() == SearchScore::Kind::white_mate
      ? SearchScore::white_mate(s.distance() - ply)
      : SearchScore::black_mate(s.distance() - ply);
}
std::optional<SearchScore> to_root_score(SearchScore s, std::uint64_t ply) {
  if (s.kind() == SearchScore::Kind::finite) return s;
  if (s.distance() > UINT64_MAX - ply) return {};
  return s.kind() == SearchScore::Kind::white_mate
      ? SearchScore::white_mate(s.distance() + ply)
      : SearchScore::black_mate(s.distance() + ply);
}
bool TTAccess::acquire(TranspositionTable &t) {
  bool expected = false;
  if (!t.active_.compare_exchange_strong(expected, true))
    throw std::logic_error("transposition table is already active");
  if (!t.impl_) { t.active_.store(false); return false; }
  return true;
}
void TTAccess::release(TranspositionTable &t) noexcept { t.active_.store(false); }
void TTAccess::force_collisions(TranspositionTable &t, bool enabled) {
  Exclusive idle(t.active_);
  if (t.impl_) { t.impl_->reset(); t.impl_->mask = enabled ? 0 : UINT64_MAX; }
  t.entries_.store(0);
}
std::uint32_t TTAccess::append(TranspositionTable &t, std::uint32_t parent,
    const RepetitionSignature &s, TranspositionStatistics &stats,
    const CancellationToken &token) {
  tt_poll(token);
  if (parent == invalid) return invalid;
  auto &p = *t.impl_; auto bucket = p.node_bucket(parent, s);
  for (auto id = p.history_index[bucket]; id; id = p.history[id].next) {
    tt_poll(token);
    auto &n = p.history[id];
    if (n.previous == parent && n.signature == s) { ++n.references; return id; }
  }
  while (!p.free_head && p.used) {
    tt_poll(token);
    std::size_t victim = p.slots;
    for (std::size_t i = 0; i < p.slots; ++i) {
      if ((i & 63) == 0) tt_poll(token);
      if (p.entries[i].used && (victim == p.slots || p.priority(i) < p.priority(victim))) victim = i;
    }
    p.erase(victim, stats); t.entries_.store(p.used);
  }
  if (!p.free_head) return invalid; // active search pins the whole pool
  auto id = p.free_head; auto &n = p.history[id]; p.free_head = n.next;
  n.signature = s; n.previous = parent; n.references = 1;
  n.next = p.history_index[bucket]; p.history_index[bucket] = id;
  if (parent) ++p.history[parent].references;
  return id;
}
void TTAccess::drop(TranspositionTable &t, std::uint32_t id) noexcept { t.impl_->drop(id); }
TTProbe TTAccess::probe(TranspositionTable &t, const ChessEvalPosition &snapshot,
    const RepetitionSignature &signature, OccurrenceCounts &counts,
    std::uint64_t hash, bool complete, int depth, std::string_view evaluator,
    std::uint64_t policy, bool numeric, std::uint64_t ply,
    TranspositionStatistics &stats, const CancellationToken &token) {
  tt_poll(token); ++stats.lookups;
  auto &p = *t.impl_;
  auto pos = signature_hash(signature) & p.mask;
  auto composite = mix(hash ^ snapshot_hash(snapshot) ^ mix(complete)) & p.mask;
  auto begin = (pos & (p.buckets - 1)) * ways;
  TTProbe result; std::uint64_t hint_recency = 0;
  for (auto i = begin; i < begin + ways; ++i) {
    tt_poll(token); auto &e = p.entries[i];
    if (!e.used || e.position_hash != pos) continue;
    if (e.signature == signature && e.decision.kind == DecisionKind::play &&
        e.decision.move[0] && e.touched >= hint_recency) {
      result.hint = e.decision.move; hint_recency = e.touched;
    }
    if (!numeric || !e.score || e.composite != composite) continue;
    ++stats.candidate_hits;
    const auto start = std::chrono::steady_clock::now();
    bool match = e.complete == complete && e.depth == depth && e.policy == policy &&
       evaluator.size() == e.evaluator.size() &&
       std::equal(evaluator.begin(), evaluator.end(), e.evaluator.begin()) &&
       e.signature == signature && same_eval_state(e.snapshot, snapshot);
    if (match) {
      // Reconstruct exact counts using scratch counters in the invocation's
      // existing count map. No allocation, retained validation object or
      // probabilistic equality test is needed on a candidate hit.
      for (auto &[key, count] : counts) { tt_poll(token); count.verified = 0; }
      for (auto id = e.tail; id != 0 && id != invalid; id = p.history[id].previous) {
        tt_poll(token); ++stats.verified_history_nodes;
        auto found = counts.find(p.history[id].signature);
        if (found == counts.end()) { match = false; break; }
        ++found->second.verified;
      }
      if (match) for (const auto &[key, count] : counts) {
        tt_poll(token);
        if (count.verified != count.total) { match = false; break; }
      }
    }
    stats.verification_elapsed += std::chrono::steady_clock::now() - start;
    auto score = match ? to_root_score(*e.score, ply) : std::nullopt;
    if (!score) { ++stats.context_rejections; continue; }
    if (!result.score || e.bound == TTBound::exact) {
      result.score = *score; result.bound = e.bound; result.decision = e.decision.expand();
      e.touched = ++p.tick;
      if (e.bound == TTBound::exact) ++stats.exact_hits; else ++stats.bound_hits;
    }
  }
  return result;
}
void TTAccess::store(TranspositionTable &t, const ChessEvalPosition &snapshot,
    const RepetitionSignature &signature, std::uint64_t hash, bool complete,
    int depth, std::string_view evaluator, std::uint64_t policy, std::uint32_t tail,
    bool numeric, SearchScore score, TTBound bound, const ChessDecision &decision,
    std::uint64_t ply, TranspositionStatistics &stats) {
  auto &p = *t.impl_;
  auto normalized = numeric && tail != invalid && evaluator.size() == 64
                      ? to_node_score(score, ply) : std::nullopt;
  auto pos = signature_hash(signature) & p.mask;
  auto composite = mix(hash ^ snapshot_hash(snapshot) ^ mix(complete)) & p.mask;
  auto begin = (pos & (p.buckets - 1)) * ways;
  auto slot = p.victim(begin, begin + ways);
  bool refresh = false;
  // Update an exactly identical representation; distinct depths can coexist.
  for (auto i = begin; i < begin + ways; ++i) {
    const auto &old = p.entries[i];
    if (old.used && old.tail == tail && old.depth == depth && old.policy == policy &&
        old.complete == complete && same_eval_state(old.snapshot, snapshot) &&
        evaluator.size() == 64 && std::equal(evaluator.begin(), evaluator.end(), old.evaluator.begin())) {
      slot = i; refresh = true; break;
    }
  }
  Entry next;
  next.used = true; next.complete = complete; next.depth = depth;
  next.position_hash = pos; next.composite = composite; next.policy = policy;
  next.touched = ++p.tick; next.snapshot = snapshot; next.signature = signature;
  next.score = normalized; next.bound = bound; next.decision = CompactDecision(decision);
  if (normalized) {
    std::copy(evaluator.begin(), evaluator.end(), next.evaluator.begin());
    next.tail = tail;
    if (tail) ++p.history[tail].references;
  } else ++stats.skipped_stores;
  if (p.entries[slot].used) p.erase(slot, stats, !refresh);
  p.entries[slot] = next; ++p.used; ++stats.stores; t.entries_.store(p.used);
}
TTSession::TTSession(TranspositionTableUse use, std::string_view evaluator,
    bool complete, TranspositionStatistics &stats, CancellationToken token)
    : use_(use), evaluator_(evaluator), complete_(complete),
      numeric_(use.evaluator == EvaluatorCacheCompatibility::deterministic_position),
      stats_(stats), cancellation_(std::move(token)) {
  if (use.table && TTAccess::acquire(*use.table)) table_ = use.table;
}
TTSession::~TTSession() {
  if (table_) { TTAccess::drop(*table_, tail_); TTAccess::release(*table_); }
}
void TTSession::increment(const RepetitionSignature &s) {
  auto &n = counts_[s].total;
  auto h = signature_hash(s);
  if (n) history_hash_ ^= mix(h ^ mix(n));
  ++n; history_hash_ ^= mix(h ^ mix(n));
}
void TTSession::decrement(const RepetitionSignature &s) noexcept {
  auto it = counts_.find(s); auto h = signature_hash(s);
  history_hash_ ^= mix(h ^ mix(it->second.total));
  if (--it->second.total) history_hash_ ^= mix(h ^ mix(it->second.total));
  else counts_.erase(it);
}
void TTSession::start(const std::vector<RepetitionSignature> &history) {
  if (!table_) return;
  for (const auto &s : history) {
    tt_poll(cancellation_); increment(s);
    if (numeric_) {
      auto next = TTAccess::append(*table_, tail_, s, stats_, cancellation_);
      TTAccess::drop(*table_, tail_); tail_ = next;
    }
  }
}
std::uint32_t TTSession::push(const RepetitionSignature &s) {
  if (!table_) return invalid;
  increment(s); auto old = tail_;
  try {
    if (numeric_) tail_ = TTAccess::append(*table_, old, s, stats_, cancellation_);
  } catch (...) { decrement(s); throw; }
  return old;
}
void TTSession::pop(const RepetitionSignature &s, std::uint32_t old) noexcept {
  if (!table_) return;
  if (numeric_) { TTAccess::drop(*table_, tail_); tail_ = old; }
  decrement(s);
}
std::size_t TTSession::count(const RepetitionSignature &s) const {
  auto it = counts_.find(s); return it == counts_.end() ? 0 : it->second.total;
}
TTProbe TTSession::probe(const ChessEvalPosition &p, const RepetitionSignature &s,
                        int depth, std::uint64_t ply) {
  if (!table_) return {};
  return TTAccess::probe(*table_, p, s, counts_, history_hash_, complete_, depth,
      evaluator_, use_.value_policy, numeric_, ply, stats_, cancellation_);
}
void TTSession::store(const ChessEvalPosition &p, const RepetitionSignature &s,
    int depth, SearchScore score, TTBound bound, const ChessDecision &action, std::uint64_t ply) {
  if (!table_) return;
  TTAccess::store(*table_, p, s, history_hash_, complete_, depth, evaluator_,
      use_.value_policy, tail_, numeric_, score, bound, action, ply, stats_);
}
} // namespace detail
} // namespace chess_lab
