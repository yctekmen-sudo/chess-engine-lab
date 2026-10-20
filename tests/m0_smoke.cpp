#include "chess_lab/build_info.hpp"
#include "chess_lab/chess_core.hpp"

#include <concepts>
#include <iostream>
#include <type_traits>
#include <utility>

using namespace chess_lab;

// Contract compilation only: no fake rules adapter or evaluator implementation.
static_assert(std::is_abstract_v<ChessCore>);
static_assert(std::has_virtual_destructor_v<Position>);
static_assert(std::has_virtual_destructor_v<Move>);
static_assert(std::has_virtual_destructor_v<UndoToken>);
static_assert(!std::is_copy_constructible_v<Position>);
static_assert(!std::is_copy_constructible_v<Move>);
static_assert(!std::is_copy_constructible_v<UndoToken>);
static_assert(std::same_as<decltype(std::declval<const PositionSnapshot&>().squares()),
                           std::span<const PieceCode, 64>>);
static_assert(std::same_as<decltype(std::declval<const ChessCore&>().snapshot(
                              std::declval<const Position&>())),
                           std::unique_ptr<const PositionSnapshot>>);

int main() {
    // Explicit failure works in Release too (assert could be compiled out).
    if (project_version() != "0.1.0") {
        std::cerr << "Unexpected core library version: " << project_version() << '\n';
        return 1;
    }
    std::cout << "M0 scaffold linked and ran: " << project_version() << '\n';
    return 0;
}
