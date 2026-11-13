#include "transposition_table_internal.hpp"
#include "audit_ttg.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <cstdlib>
#include <tuple>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <array>

namespace chess_lab {
namespace detail {
Audit audit;
struct T12A5Counters {
 std::uint64_t probes=0, exact_selections=0, returned=0, useful=0,
  touches=0, extra_touches=0, stores=0, updates=0, evictions=0,
  old_gen_evictions=0, useful_evictions=0, evicted_depth_sum=0,
  evicted_utility_sum=0, useful_reprobes=0;
 void reset() noexcept { *this=T12A5Counters{}; }
};
static T12A5Counters t12a5_counts;
struct T12A6Counters {
  std::uint64_t probes=0, scanned_probe_slots=0, occupied_probe_slots=0,
    position_probe_slots=0, stores=0, refreshed=0, nonfull_inserts=0,
    full_inserts=0, same_timestamp_ties=0, evict_useful=0;
  std::array<std::uint64_t,17> min_depth_ties{};
  void reset() noexcept { *this=T12A6Counters{}; }
};
static T12A6Counters t12a6_counts;
// T1.2a.7 store-path cost counters. Enabled only for audited runs.
struct T12A7Counters {
 std::uint64_t stores=0, victim_visits=0, match_visits=0,
   fused_visits=0, signature_checks=0, refreshes=0,
   empty_insertions=0, evictions=0, erase_skipped=0;
 void reset() noexcept { *this=T12A7Counters{}; }
};
static T12A7Counters t12a7_counts;


static int t12a5_replacement_override=-1, t12a5_touch_override=-1;
static int t12a5_policy() noexcept {
 if(t12a5_replacement_override>=0) return t12a5_replacement_override;
 static const int value=[]() noexcept {
  const char* v=std::getenv("CELAB_T12A5_POLICY");
  if(!v) return 1; const int n=std::atoi(v); return n>=0 && n<=5 ? n : 1;
 }();
 return value;
}
static int t12a5_touch_policy() noexcept {
 if(t12a5_touch_override>=0) return t12a5_touch_override;
 static const int value=[]() noexcept {
  const char* v=std::getenv("CELAB_T12A5_TOUCH");
  if(!v) return 0; const int n=std::atoi(v); return n>=0 && n<=3 ? n : 0;
 }();
 return value;
}
extern "C" void celab_t12a5_set_policy(int p) noexcept { t12a5_replacement_override=p; }
extern "C" void celab_t12a5_set_touch(int p) noexcept { t12a5_touch_override=p; }
extern "C" void celab_t12a5_reset() noexcept { t12a5_counts.reset(); }
extern "C" void celab_t12a6_reset() noexcept { t12a6_counts.reset(); }
extern "C" void celab_t12a7_reset() noexcept { t12a7_counts.reset(); }
extern "C" void celab_t12a5_dump() {
 const auto &c=t12a5_counts;
 std::cerr<<"CELAB_T12A5_COUNTS policy="<<t12a5_policy()<<" touch="<<t12a5_touch_policy()
  <<" probes="<<c.probes<<" selected="<<c.exact_selections<<" returned="<<c.returned
  <<" useful="<<c.useful<<" touches="<<c.touches<<" extra_touches="<<c.extra_touches
  <<" stores="<<c.stores<<" updates="<<c.updates<<" evictions="<<c.evictions
  <<" old_gen_evictions="<<c.old_gen_evictions<<" useful_evictions="<<c.useful_evictions
  <<" evicted_depth_sum="<<c.evicted_depth_sum<<" evicted_utility_sum="<<c.evicted_utility_sum
  <<" useful_reprobes="<<c.useful_reprobes<<'\n';
}
}
extern "C" void celab_audit_enable(int mode) { detail::audit.enabled=mode!=0;detail::audit.heavy=mode==2; }
extern "C" void celab_audit_reset() { detail::audit.reset(); }
extern "C" void celab_audit_dump() {
 const auto &a=detail::audit;
 std::cerr<<"CELAB_AUDIT_TOTAL push_ns="<<a.push_ns<<" pop_ns="<<a.pop_ns
   <<" count_ns="<<a.count_ns<<" start_ns="<<a.start_ns<<" hash_ns="<<a.hash_ns
   <<" pushes="<<a.pushes<<" pops="<<a.pops<<" counts="<<a.counts<<" hashes="<<a.hashes<<'\n';
 for(int d=0;d<12;d++) {
 const auto &r=a.depth[d];if(!r.probes&&!r.store_attempts&&!r.exact_returns&&!r.bound_returns)continue;
 std::cerr<<"CELAB_AUDIT_DEPTH depth="<<d<<" probes="<<r.probes
   <<" store_attempts="<<r.store_attempts<<" stores="<<r.stores
   <<" evictions="<<r.evictions<<" nonplay="<<r.nonplay<<" unscored="<<r.unscored
   <<" used_slots="<<r.used_slots<<" position_slots="<<r.position_slots
   <<" scored_slots="<<r.scored_slots<<" depth_wrong="<<r.depth_wrong
   <<" depth_same="<<r.depth_same<<" policy_wrong="<<r.policy_wrong
   <<" compat_wrong="<<r.compat_wrong<<" extras_wrong="<<r.extras_wrong
   <<" evaluator_wrong="<<r.evaluator_wrong<<" eligible="<<r.eligible
   <<" exact_returns="<<r.exact_returns<<" bound_returns="<<r.bound_returns
   <<" claim_veto="<<r.claim_veto<<" pv_research="<<r.pv_research
   <<" bound_noncutoff="<<r.bound_noncutoff<<" probe_ns="<<r.probe_ns
   <<" store_ns="<<r.store_ns<<" preprobe_return_ns="<<r.preprobe_return_ns
   <<" prepare_return_ns="<<r.prepare_return_ns<<'\n';
 }
}
namespace {
// Set once before any table is constructed; changing the environment during
// a process is intentionally unsupported. Identical Entry and Impl layouts.
std::size_t configured_ways() {
  const char* value=std::getenv("CELAB_T12A6_WAYS");
  if(!value || !*value) return 4;
  if(std::string_view(value)=="2") return 2;
  if(std::string_view(value)=="4") return 4;
  if(std::string_view(value)=="8") return 8;
  if(std::string_view(value)=="16") return 16;
  throw std::invalid_argument("CELAB_T12A6_WAYS must be 2, 4, 8 or 16");
}
const std::size_t ways = configured_ways();
// Select one store algorithm per process; A uses frozen T1.2a.5B source.
// 0 = historical two passes + erase, 1 = fused scan + erase,
// 2 = fused scan + direct overwrite, 3 = mode 2 + cheap-first matching.
int t12a7_mode() {
 const char* v = std::getenv("CELAB_T12A7_MODE");
 if(!v || !*v) return 3;
 if(std::string_view(v)=="0") return 0;
 if(std::string_view(v)=="1") return 1;
 if(std::string_view(v)=="2") return 2;
 if(std::string_view(v)=="3") return 3;
 throw std::invalid_argument("CELAB_T12A7_MODE must be 0, 1, 2, or 3");
}
const int store_mode = t12a7_mode();

std::uint64_t mix(std::uint64_t x) {
  x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27; x *= 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
// The existing 64-bit touched field packs the last access counter (48 bits),
// the current iterative-search generation (8 bits) and successful-return
// credit (8 bits). No Entry growth; all policies have the same TT capacity.
constexpr std::uint64_t RECENCY_MASK=(UINT64_C(1)<<48)-1;
std::uint64_t recency(std::uint64_t x) noexcept { return x & RECENCY_MASK; }
std::uint8_t generation(std::uint64_t x) noexcept { return std::uint8_t(x>>48); }
std::uint8_t utility(std::uint64_t x) noexcept { return std::uint8_t(x>>56); }
std::uint64_t meta(std::uint64_t tick, std::uint8_t gen, std::uint8_t useful) noexcept {
 return (tick & RECENCY_MASK) | (std::uint64_t(gen)<<48) | (std::uint64_t(useful)<<56);
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
// Deliberately no history, clock, full snapshot, history pool pointers, or
// verification metadata. Exact position signatures guard moves against
// collisions; position-dependent numeric scores are search estimates.
struct Entry {
  bool used = false;
  int depth = 0;
  std::uint64_t position_hash = 0, policy = 0, touched = 0;
  RepetitionSignature signature;
  std::array<char, 64> evaluator{};
  // Only full-snapshot evaluators use these three small ABI fields; they are
  // not draw-history evidence and require no persistent history bookkeeping.
  detail::EvaluationExtras extras{};
  std::uint8_t compatibility = 0;
  std::optional<SearchScore> score;
  detail::TTBound bound = detail::TTBound::exact;
  CompactDecision decision;
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
extern "C" void celab_t12a7_dump() {
 const auto &c=detail::t12a7_counts;
 std::cerr<<"CELAB_T12A7_COUNTS mode="<<store_mode<<" ways="<<ways
  <<" stores="<<c.stores<<" victim_visits="<<c.victim_visits
  <<" match_visits="<<c.match_visits<<" fused_visits="<<c.fused_visits
  <<" signature_checks="<<c.signature_checks
  <<" refreshes="<<c.refreshes<<" empty_insertions="<<c.empty_insertions
  <<" evictions="<<c.evictions<<" erase_skipped="<<c.erase_skipped<<'\n';
}
extern "C" void celab_t12a6_dump() {
  const auto &c=detail::t12a6_counts;
  std::cerr<<"CELAB_T12A6_COUNTS ways="<<ways
    <<" probes="<<c.probes
    <<" scanned_probe_slots="<<c.scanned_probe_slots
    <<" occupied_probe_slots="<<c.occupied_probe_slots
    <<" position_probe_slots="<<c.position_probe_slots
    <<" stores="<<c.stores<<" refreshed="<<c.refreshed
    <<" nonfull_inserts="<<c.nonfull_inserts
    <<" full_inserts="<<c.full_inserts
    <<" same_timestamp_ties="<<c.same_timestamp_ties
    <<" evict_useful="<<c.evict_useful;
  for(std::size_t n=1;n<=ways;++n)
    std::cerr<<" tie_"<<n<<"="<<c.min_depth_ties[n];
  std::cerr<<'\n';
}
struct TranspositionTable::Impl {
  std::size_t buckets = 0, slots = 0, allocated = 0, used = 0;
  std::uint64_t tick = 0, mask = UINT64_MAX;
  std::uint8_t search_generation=0;
  std::unique_ptr<Entry[]> entries;
  explicit Impl(std::size_t count) : buckets(count), slots(count * ways),
      allocated(sizeof(Impl) + slots * sizeof(Entry)),
      entries(std::make_unique<Entry[]>(slots)) {}
  void reset() noexcept {
    for (std::size_t i = 0; i < slots; ++i) entries[i] = Entry{};
    used = 0; tick = 0; search_generation=0;
  }
  void erase(std::size_t slot, TranspositionStatistics &stats, bool eviction = true) noexcept {
    auto &e = entries[slot];
    if (!e.used) return;
    const int old_depth=e.depth;
    e = Entry{};
    --used;
    if (eviction) ++stats.evictions;
    if(eviction && detail::audit.enabled) ++detail::audit.depth[detail::bin(old_depth)].evictions;
  }
  // Comparison returns true if index a is LESS worth retaining than b.
  // Mode 0 reproduces the original recency/8, depth, recency, slot tuple.
  bool evict_before(std::size_t a,std::size_t b) const noexcept {
    const auto &ea=entries[a], &eb=entries[b];
    const auto ra=recency(ea.touched), rb=recency(eb.touched);
    const int mode=detail::t12a5_policy();
    switch(mode) {
    case 1: return std::tuple(ea.depth,ra,a)<std::tuple(eb.depth,rb,b); // depth first
    case 2: return std::tuple(ra,a)<std::tuple(rb,b); // pure LRU
    case 3: { // mixed depth/age, age measured in 16-touch buckets and capped
      const auto sa=std::int64_t(ea.depth)*64-std::int64_t(std::min<std::uint64_t>((tick-ra)/16,2048));
      const auto sb=std::int64_t(eb.depth)*64-std::int64_t(std::min<std::uint64_t>((tick-rb)/16,2048));
      return std::tuple(sa,ra,a)<std::tuple(sb,rb,b);
    }
    case 4: return std::tuple(generation(ea.touched)==search_generation,ea.depth,ra,a)
                 <std::tuple(generation(eb.touched)==search_generation,eb.depth,rb,b);
    case 5: return std::tuple(utility(ea.touched),ea.depth,ra,a)
                 <std::tuple(utility(eb.touched),eb.depth,rb,b);
    default: return std::tuple(ra/8,ea.depth,ra,a)<std::tuple(rb/8,eb.depth,rb,b);
    }
  }
  void touch(Entry &e, bool extra=false) noexcept {
    if (tick >= RECENCY_MASK-1) { // unreachable in realistic benchmarks
      tick=1;
    } else ++tick;
    e.touched=meta(tick,search_generation,utility(e.touched));
    if(detail::audit.enabled) {
      ++detail::t12a5_counts.touches;
      if(extra) ++detail::t12a5_counts.extra_touches;
    }
  }
  std::size_t victim(std::size_t begin, std::size_t end) const {
    std::size_t best = begin;
    for (auto i = begin; i < end; ++i) {
      if(detail::audit.enabled) ++detail::t12a7_counts.victim_visits;
      if (!entries[i].used) return i;
      if (evict_before(i,best)) best = i;
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
  std::unique_ptr<Impl> next;
  const auto overhead = sizeof(Impl);
  if (bytes >= overhead && (bytes - overhead) / (ways * sizeof(Entry)) >= 1) {
    auto max_buckets = (bytes - overhead) / (ways * sizeof(Entry));
    std::size_t count = 1;
    while (count <= max_buckets / 2) count *= 2;
    next = std::make_unique<Impl>(count);
  }
  impl_.swap(next);
  capacity_.store(impl_ ? impl_->allocated : 0);
  entries_.store(0);
}
namespace detail {
// Keep the fixture API without doing any history verification or allocating
// an observer. The production entry contains no historical evidence to visit.
void TTAccess::observe_verification(TranspositionTable &t,
                                   void (*)(std::uint64_t) noexcept) {
  Exclusive idle(t.active_);
}
std::uint64_t signature_hash(const RepetitionSignature &s) {
  Timer timer(audit.enabled?&audit.hash_ns:nullptr);
  if(audit.enabled)++audit.hashes;
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
void TTAccess::begin_generation(TranspositionTable &t) noexcept {
  if(t.impl_) ++t.impl_->search_generation;
}
void TTAccess::reward(TranspositionTable &t,const TTProbe &hit) noexcept {
  if(!hit.score || hit.selected_slot==std::numeric_limits<std::size_t>::max() || !t.impl_) return;
  auto &p=*t.impl_;
  if(hit.selected_slot >= p.slots) return;
  auto &entry=p.entries[hit.selected_slot];
  if(!entry.used || !entry.score) return;
  const auto util=utility(entry.touched);
  entry.touched=meta(recency(entry.touched),generation(entry.touched),
                     util==255 ? util : std::uint8_t(util+1));
  if(detail::t12a5_touch_policy()==2) p.touch(entry);
  if(detail::audit.enabled) {
    ++detail::t12a5_counts.useful;
    ++detail::t12a5_counts.returned;
  }
}
TTProbe TTAccess::probe(TranspositionTable &t,
    const RepetitionSignature &signature, std::uint64_t signature_hash_value,
    int depth, std::string_view evaluator, std::uint64_t policy,
    EvaluatorCacheCompatibility compatibility,
    std::optional<EvaluationExtras> extras,
    bool numeric, std::uint64_t ply, TranspositionStatistics &stats,
    const CancellationToken &token) {
  tt_poll(token);
  ++stats.lookups;
  auto &row=audit.depth[bin(depth)];
  Timer timer(audit.enabled?&row.probe_ns:nullptr);
  if(audit.enabled)++row.probes;
  auto &p = *t.impl_;
  auto pos = signature_hash_value & p.mask;
  auto begin = (pos & (p.buckets - 1)) * ways;
  TTProbe result;
  std::uint64_t hint_recency = 0;
  bool shadow_done=false;
  if(detail::audit.enabled) {
    ++detail::t12a5_counts.probes;
    ++detail::t12a6_counts.probes;
  }
  for (auto i = begin; i < begin + ways; ++i) {
    tt_poll(token);
    auto &e = p.entries[i];
    if(audit.enabled) {
      ++detail::t12a6_counts.scanned_probe_slots;
      if(e.used) {
        ++detail::t12a6_counts.occupied_probe_slots;
        if(e.position_hash==pos && e.signature==signature)
          ++detail::t12a6_counts.position_probe_slots;
      }
    }
    if(audit.enabled && audit.heavy && e.used) {
      ++row.used_slots;
      if(e.position_hash==pos && e.signature==signature) {
        ++row.position_slots;
        if(e.score) {
          ++row.scored_slots;
          if(e.depth!=depth) ++row.depth_wrong;
          else {
            ++row.depth_same;
            if(e.policy!=policy)++row.policy_wrong;
            else if(e.compatibility!=static_cast<std::uint8_t>(compatibility))++row.compat_wrong;
            else if(compatibility==EvaluatorCacheCompatibility::deterministic_position &&
              (!extras || e.extras!=*extras))++row.extras_wrong;
            else if(evaluator.size()!=64 || !std::equal(evaluator.begin(),evaluator.end(),e.evaluator.begin()))
              ++row.evaluator_wrong;
            else ++row.eligible;
          }
        }
      }
    }
    if (!e.used || e.position_hash != pos || e.signature != signature) continue;
    const int touch_mode=detail::t12a5_touch_policy();
    if(touch_mode==1) p.touch(e,true); // position match, irrespective of score
    if(touch_mode==3 && !shadow_done && numeric && e.score && e.depth==depth+1 &&
       e.policy==policy && evaluator.size()==64 &&
       e.compatibility==static_cast<std::uint8_t>(compatibility) &&
       (compatibility!=EvaluatorCacheCompatibility::deterministic_position ||
        (extras && e.extras==*extras)) &&
       std::equal(evaluator.begin(),evaluator.end(),e.evaluator.begin()) &&
       to_root_score(*e.score,ply)) {
      p.touch(e,true); shadow_done=true; // one deeper entry, NO deeper numeric return
    }
    if (e.decision.kind == DecisionKind::play && e.decision.move[0] &&
        recency(e.touched) >= hint_recency) {
      result.hint = e.decision.move;
      hint_recency = recency(e.touched);
    }
    if (!numeric || !e.score) continue;
    ++stats.candidate_hits;
    if (e.depth != depth || e.policy != policy || evaluator.size() != 64 ||
        e.compatibility != static_cast<std::uint8_t>(compatibility) ||
        (compatibility == EvaluatorCacheCompatibility::deterministic_position &&
         (!extras || e.extras != *extras)) ||
        !std::equal(evaluator.begin(), evaluator.end(), e.evaluator.begin())) {
      ++stats.context_rejections;
      continue;
    }
    auto score = to_root_score(*e.score, ply);
    if (!score) { ++stats.context_rejections; continue; }
    if (!result.score || e.bound == TTBound::exact) {
      result.score = *score;
      result.bound = e.bound;
      result.decision = e.decision.expand();
      result.selected_slot=i;
      if(touch_mode==0 || touch_mode==3) p.touch(e); // original on selection
      if(detail::audit.enabled) {
        ++detail::t12a5_counts.exact_selections;
        if(utility(e.touched)>0) ++detail::t12a5_counts.useful_reprobes;
      }
      if (e.bound == TTBound::exact) ++stats.exact_hits;
      else ++stats.bound_hits;
    }
  }
  return result;
}
void TTAccess::store(TranspositionTable &t,
    const RepetitionSignature &signature, std::uint64_t signature_hash_value,
    int depth, std::string_view evaluator, std::uint64_t policy,
    EvaluatorCacheCompatibility compatibility,
    std::optional<EvaluationExtras> extras,
    bool numeric, SearchScore score, TTBound bound, const ChessDecision &decision,
    std::uint64_t ply, TranspositionStatistics &stats) {
  auto &row=audit.depth[bin(depth)];
  Timer timer(audit.enabled?&row.store_ns:nullptr);
  if(audit.enabled)++row.store_attempts;
  // Only a real PLAY continuation can provide history-independent move
  // information. Claims/terminal decisions require current DrawState.
  if (decision.move.size() > 5) throw std::logic_error("noncanonical TT decision");
  if (decision.kind != DecisionKind::play || decision.move.empty()) {
    if(audit.enabled)++row.nonplay;
    return;
  }
  auto normalized = numeric && evaluator.size() == 64 &&
      (compatibility != EvaluatorCacheCompatibility::deterministic_position || extras)
      ? to_node_score(score, ply) : std::nullopt;
  if(audit.enabled && !normalized)++row.unscored;
  auto &p = *t.impl_;
  auto pos = signature_hash_value & p.mask;
  auto begin = (pos & (p.buckets - 1)) * ways;
  const auto npos = std::numeric_limits<std::size_t>::max();
  std::size_t slot = npos;
  bool refresh = false;
  if(audit.enabled) ++detail::t12a7_counts.stores;
  // Exactly the historical key predicate, with optional cheap-first filtering.
  // Validates the full signature before declaring a same-key update.
  const auto same_store_key = [&](const Entry &old) {
    if(!old.used || old.position_hash != pos) return false;
    if(store_mode == 3 && (old.depth != depth || old.policy != policy)) return false;
    if(audit.enabled) ++detail::t12a7_counts.signature_checks;
    if(old.signature != signature || old.depth != depth || old.policy != policy) return false;
    return (!numeric && !old.score) || (numeric && old.score &&
      old.compatibility == static_cast<std::uint8_t>(compatibility) &&
      (compatibility != EvaluatorCacheCompatibility::deterministic_position ||
        (extras && old.extras == *extras)) &&
      evaluator.size() == 64 &&
      std::equal(evaluator.begin(), evaluator.end(), old.evaluator.begin()));
  };
  if(store_mode == 0) {
    slot = p.victim(begin, begin + ways);
    for(auto i=begin; i<begin+ways; ++i) {
      if(audit.enabled) ++detail::t12a7_counts.match_visits;
      if(same_store_key(p.entries[i])) { slot=i; refresh=true; break; }
    }
  } else {
    std::size_t empty = npos, victim = npos;
    // A match always wins, even when an empty slot precedes it. Search all
    // occupied entries until the first matching entry, as in the baseline.
    for(auto i=begin; i<begin+ways; ++i) {
      if(audit.enabled) ++detail::t12a7_counts.fused_visits;
      const auto &old=p.entries[i];
      if(!old.used) {
        if(empty==npos) empty=i;
        continue;
      }
      if(same_store_key(old)) { slot=i; refresh=true; break; }
      if(empty==npos && (victim==npos || p.evict_before(i,victim))) victim=i;
    }
    if(!refresh) slot = empty!=npos ? empty : victim;
    if(slot==npos) throw std::logic_error("TT fused insertion found no slot");
  }
  if(audit.enabled) {
    auto &c=detail::t12a7_counts;
    if(refresh) ++c.refreshes;
    else if(!p.entries[slot].used) ++c.empty_insertions;
    else ++c.evictions;
  }
  if(audit.enabled) {
    auto &c=detail::t12a6_counts;
    ++c.stores;
    if(refresh) ++c.refreshed;
    else {
      int min_depth=std::numeric_limits<int>::max();
      std::size_t ties=0, occupied=0;
      std::uint64_t oldest=std::numeric_limits<std::uint64_t>::max();
      for(auto i=begin;i<begin+ways;++i) {
        const auto &entry=p.entries[i];
        if(!entry.used) continue;
        ++occupied;
        if(entry.depth<min_depth) {
          min_depth=entry.depth; ties=1; oldest=recency(entry.touched);
        } else if(entry.depth==min_depth) {
          ++ties;
          if(recency(entry.touched)==oldest) ++c.same_timestamp_ties;
          oldest=std::min(oldest,recency(entry.touched));
        }
      }
      if(occupied==ways) {
        ++c.full_inserts;
        ++c.min_depth_ties[ties];
        if(utility(p.entries[slot].touched)>0) ++c.evict_useful;
      } else ++c.nonfull_inserts;
    }
  }
  Entry next;
  next.used = true;
  next.depth = depth;
  next.position_hash = pos;
  next.policy = policy;
  // A same-key refresh must preserve measured useful-return credit;
  // otherwise every cached cutoff followed by finish()->store() would
  // erase the signal before the replacement policy can observe it.
  const auto prior_utility=refresh ? utility(p.entries[slot].touched) : std::uint8_t{0};
  next.touched = meta(++p.tick,p.search_generation,prior_utility);
  next.signature = signature;
  next.score = normalized;
  next.bound = bound;
  next.decision = CompactDecision(decision);
  next.compatibility = normalized ? static_cast<std::uint8_t>(compatibility) : 0;
  if (normalized && extras) next.extras = *extras;
  if (normalized) std::copy(evaluator.begin(), evaluator.end(), next.evaluator.begin());
  else ++stats.skipped_stores;
  if(detail::audit.enabled) {
    ++detail::t12a5_counts.stores;
    if(refresh) ++detail::t12a5_counts.updates;
    if(p.entries[slot].used && !refresh) {
      const auto &old=p.entries[slot];
      ++detail::t12a5_counts.evictions;
      detail::t12a5_counts.evicted_depth_sum+=old.depth;
      detail::t12a5_counts.evicted_utility_sum+=utility(old.touched);
      if(utility(old.touched))++detail::t12a5_counts.useful_evictions;
      if(generation(old.touched)!=p.search_generation)++detail::t12a5_counts.old_gen_evictions;
    }
  }
  if(store_mode>=2) {
    // Never zero-fill a fully initialized Entry immediately before replacing
    // its complete payload. Preserve occupancy/eviction and per-depth audit.
    const bool was_used=p.entries[slot].used;
    if(was_used && !refresh) {
      ++stats.evictions;
      if(audit.enabled) ++audit.depth[bin(p.entries[slot].depth)].evictions;
    }
    p.entries[slot]=std::move(next);
    if(!was_used) ++p.used;
    if(audit.enabled && was_used) ++detail::t12a7_counts.erase_skipped;
  } else {
    if(p.entries[slot].used) p.erase(slot, stats, !refresh);
    p.entries[slot]=std::move(next);
    ++p.used;
  }
  ++stats.stores;
  if(audit.enabled)++row.stores;
  t.entries_.store(p.used);
}
TTSession::TTSession(TranspositionTableUse use, std::string_view evaluator,
    bool /*complete*/, TranspositionStatistics &stats, CancellationToken token)
    : use_(use), evaluator_(evaluator),
      numeric_(use.evaluator != EvaluatorCacheCompatibility::not_certified),
      compatibility_(use.evaluator),
      stats_(stats), cancellation_(std::move(token)) {
  if (use.table && TTAccess::acquire(*use.table)) table_ = use.table;
}
TTSession::~TTSession() {
  if (table_) TTAccess::release(*table_);
}
void TTSession::start(const std::vector<RepetitionSignature> &history) {
  Timer timer(audit.enabled ? &audit.start_ns : nullptr);
  // A TT session carries no draw counts. The active Traversal::history is
  // authoritative for chess rules; keep the original root-cancellation polls.
  if (!table_) return;
  TTAccess::begin_generation(*table_);
  for (const auto &signature : history) {
    (void)signature;
    tt_poll(cancellation_);
  }
}
TTProbe TTSession::probe_impl(const RepetitionSignature &s, int depth,
                              std::uint64_t ply,
                              std::optional<EvaluationExtras> extras,
                              std::optional<std::uint64_t> *node_hash) {
  if (!table_) return {};
  // A hash is owned by one search node, not by the recursive TT session.
  const auto hash = node_hash && node_hash->has_value()
      ? node_hash->value() : signature_hash(s);
  if (node_hash) *node_hash = hash;
  return TTAccess::probe(*table_, s, hash, depth,
      evaluator_, use_.value_policy, compatibility_, extras,
      numeric_, ply, stats_, cancellation_);
}
void TTSession::store_impl(const RepetitionSignature &s, int depth,
                           SearchScore score, TTBound bound,
                           const ChessDecision &action, std::uint64_t ply,
                           std::optional<EvaluationExtras> extras,
                           std::optional<std::uint64_t> *node_hash) {
  if (!table_) return;
  const auto hash = node_hash && node_hash->has_value()
      ? node_hash->value() : signature_hash(s);
  if (node_hash) *node_hash = hash;
  TTAccess::store(*table_, s, hash, depth,
      evaluator_, use_.value_policy, compatibility_, extras,
      numeric_, score, bound, action, ply, stats_);
}
TTProbe TTSession::probe(const RepetitionSignature &s, int depth,
                        std::uint64_t ply, const PositionSnapshot &snapshot,
                        std::optional<std::uint64_t> *node_hash) {
  return probe_impl(s, depth, ply,
      compatibility_ == EvaluatorCacheCompatibility::deterministic_position
      ? std::optional(evaluation_extras(snapshot)) : std::nullopt, node_hash);
}
void TTSession::store(const RepetitionSignature &s, int depth,
                      SearchScore score, TTBound bound,
                      const ChessDecision &action, std::uint64_t ply,
                      const PositionSnapshot &snapshot,
                      std::optional<std::uint64_t> *node_hash) {
  store_impl(s, depth, score, bound, action, ply,
      compatibility_ == EvaluatorCacheCompatibility::deterministic_position
      ? std::optional(evaluation_extras(snapshot)) : std::nullopt, node_hash);
}
TTProbe TTSession::probe(const ChessEvalPosition &snapshot,
                        const RepetitionSignature &s, int depth,
                        std::uint64_t ply) {
  return probe_impl(s, depth, ply,
      compatibility_ == EvaluatorCacheCompatibility::deterministic_position
      ? std::optional(evaluation_extras(snapshot)) : std::nullopt);
}
void TTSession::store(const ChessEvalPosition &snapshot,
                      const RepetitionSignature &s, int depth,
                      SearchScore score, TTBound bound,
                      const ChessDecision &action, std::uint64_t ply) {
  store_impl(s, depth, score, bound, action, ply,
      compatibility_ == EvaluatorCacheCompatibility::deterministic_position
      ? std::optional(evaluation_extras(snapshot)) : std::nullopt);
}
} // namespace detail
} // namespace chess_lab
