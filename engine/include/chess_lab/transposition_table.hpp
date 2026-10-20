#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace chess_lab {
namespace detail { struct TTAccess; class TTSession; }
// Controller-owned, single-active-invocation storage. clear/resize throw
// logic_error while active. Destroy only after the active search has returned.
class TranspositionTable {
public:
  explicit TranspositionTable(std::size_t maximum_bytes = 0);
  ~TranspositionTable();
  TranspositionTable(const TranspositionTable &) = delete;
  TranspositionTable &operator=(const TranspositionTable &) = delete;
  void clear();
  void resize(std::size_t maximum_bytes);
  // Actual enabled storage, including metadata; <= requested maximum.
  // Too-small budgets disable allocation (capacity=0). Allocator bookkeeping
  // and the caller's fixed-size handle are outside the owned allocation budget.
  std::size_t capacity_bytes() const noexcept { return capacity_.load(); }
  std::size_t entry_count() const noexcept { return entries_.load(); }
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::atomic<bool> active_{false};
  std::atomic<std::size_t> capacity_{0}, entries_{0};
  friend struct detail::TTAccess;
};
enum class EvaluatorCacheCompatibility {
  not_certified,
  deterministic_position // explicit controller certification, NOT inferred
};
struct TranspositionTableUse {
  TranspositionTable *table = nullptr;
  EvaluatorCacheCompatibility evaluator = EvaluatorCacheCompatibility::not_certified;
  // Namespace for value-defining policy/behavior beyond the engine's uniform
  // policy v1. Change when any certified behavior changes without fingerprint.
  std::uint64_t value_policy = 1;
};
struct TranspositionStatistics {
  std::uint64_t lookups = 0, candidate_hits = 0, exact_hits = 0,
      bound_hits = 0, bound_cutoffs = 0, hint_hits = 0, context_rejections = 0,
      stores = 0, evictions = 0, pv_researches = 0,
      verified_history_nodes = 0, skipped_stores = 0;
  std::chrono::nanoseconds verification_elapsed{};
};
} // namespace chess_lab
