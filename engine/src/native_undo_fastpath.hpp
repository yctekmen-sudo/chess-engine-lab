#pragma once
// B1 EXPERIMENT ONLY: private stack-owned native undo frame.
// It does not change ChessCore, UndoToken, evaluator ABI or public APIs.
#include "chess_lab/chess_core.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
namespace chess_lab::detail {
struct NativeUndoFrame {
    std::shared_ptr<const int> identity;
    std::uint16_t native_move = 0;
    std::optional<std::uint8_t> ep;
    std::uint32_t half = 0, full = 1;
    std::uint64_t id = 0, parent = 0;
    bool consumed = false;
};
void make_native_search_move(const ChessCore&, Position&, const Move&, NativeUndoFrame&);
void undo_native_search_move(const ChessCore&, Position&, NativeUndoFrame&);
struct NativeMoveBatch;
void make_native_search_batch_move(const ChessCore&, Position&,
                                   const NativeMoveBatch&, std::size_t,
                                   NativeUndoFrame&);
} // namespace chess_lab::detail
