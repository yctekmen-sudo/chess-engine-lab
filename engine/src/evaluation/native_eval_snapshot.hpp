#pragma once
#include <chess_lab/chess_core.hpp>
#include <chess_lab/chess_eval_abi_v1.h>
namespace chess_lab::detail {
// Private native fill: returns false for non-native objects without writing output.
bool native_fill_evaluation_position(const ChessCore&, const Position&, ChessEvalPosition&) noexcept;
// Private, synchronous: snap must describe the current native position.
ChessEvalPosition native_evaluation_snapshot(const ChessCore&,const Position&,
    const PositionSnapshot&,bool& direct,bool verify);
}
