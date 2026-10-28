#pragma once
// One representation is compiled into the shared traversal; no runtime dispatch.
#if defined(CHESS_LAB_ORDERING_P1)
#include "categorical_ordering.hpp"
#else
// NONE creates no tree and allocates no retention memory.
#include "compact_ordering.hpp"
#endif
namespace chess_lab::detail {
inline constexpr OrderingConfig production_ordering_config{3, 128 * 1024 * 1024, false, false};
#if defined(CHESS_LAB_ORDERING_P1)
inline constexpr const char *compiled_ordering_policy = "P1";
#elif defined(CHESS_LAB_ORDERING_NONE)
inline constexpr const char *compiled_ordering_policy = "NONE";
#else
inline constexpr const char *compiled_ordering_policy = "B2";
#endif
}
