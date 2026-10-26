#pragma once
#include "transposition_table_internal.hpp"
#include <span>

namespace chess_lab::detail {
// Imported root only. Descendants reuse terminal()'s existing occurrence count.
inline bool initial_history_has_duplicates(std::span<const RepetitionSignature> history) {
  for (std::size_t i = 0; i < history.size(); ++i)
    for (std::size_t j = 0; j < i; ++j)
      if (history[i] == history[j]) return true;
  return false;
}
struct RestoreDuplicateFlag {
  bool &state;
  const bool parent;
  explicit RestoreDuplicateFlag(bool &value) noexcept : state(value), parent(value) {}
  ~RestoreDuplicateFlag() noexcept { state = parent; }
  RestoreDuplicateFlag(const RestoreDuplicateFlag &) = delete;
  RestoreDuplicateFlag &operator=(const RestoreDuplicateFlag &) = delete;
};
} // namespace chess_lab::detail
