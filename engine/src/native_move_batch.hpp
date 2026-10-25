#pragma once

#include <chess_lab/chess_core.hpp>
#include <cstddef>

// Adapter-private capabilities. No native chess-library types or executable
// values escape the adapter, and none of these are public ChessCore methods.
namespace chess_lab::detail {
struct NativeMoveBatch;
struct NativeMoveBatchDeleter {
  void operator()(NativeMoveBatch *) const noexcept;
};
using NativeMoveBatchOwner =
    std::unique_ptr<NativeMoveBatch, NativeMoveBatchDeleter>;

NativeMoveBatchOwner prepare_native_move_batch(const ChessCore &, const Position &);
std::size_t native_move_batch_size(const NativeMoveBatch &) noexcept;
std::string encode_native_batch_member(const ChessCore &, const Position &,
                                       const NativeMoveBatch &, std::size_t ordinal);
std::unique_ptr<Move> materialize_native_batch_member(
    const ChessCore &, const Position &, const NativeMoveBatch &, std::size_t ordinal);
void invalidate_native_move_batch(NativeMoveBatch &) noexcept;
} // namespace chess_lab::detail
