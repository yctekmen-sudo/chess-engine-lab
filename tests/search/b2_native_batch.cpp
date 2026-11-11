// B2 private adapter experiment: certified native batch moves execute without
// heap materialization and preserve the public adapter's board transitions.
#include "native_move_batch.hpp"
#include "native_undo_fastpath.hpp"
#include <chess_lab/search.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace chess_lab;
static void require(bool good, const char* message) {
  if (!good) throw std::runtime_error(message);
}
int main() try {
  auto c = create_chess_core();
  unsigned exercised = 0;
  for (const char* fen : {
       "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
       "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
       "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1",
       "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
       "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
       "1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1"}) {
    auto p = c->load_fen(fen);
    const std::string original = c->export_fen(*p);
    const auto key = c->position_key(*p);
    auto batch = detail::prepare_native_move_batch(*c, *p);
    const auto n = detail::native_move_batch_size(*batch);
    for (std::size_t i = 0; i < n; ++i) {
      const auto text = detail::encode_native_batch_member(*c, *p, *batch, i);
      auto public_move = c->decode_move(*p, text);
      auto public_token = c->make_move(*p, *public_move);
      const auto expected_fen = c->export_fen(*p);
      const auto expected_key = c->position_key(*p);
      c->undo_move(*p, *public_token);
      require(c->export_fen(*p) == original && c->position_key(*p) == key,
              "public adapter failed to restore initial position");
      detail::NativeUndoFrame frame;
      detail::make_native_search_batch_move(*c, *p, *batch, i, frame);
      require(c->export_fen(*p) == expected_fen && c->position_key(*p) == expected_key,
              "B2 native batch transition differs from public adapter");
      detail::undo_native_search_move(*c, *p, frame);
      require(c->export_fen(*p) == original && c->position_key(*p) == key,
              "B2 native batch undo did not restore board state");
      ++exercised;
    }
    // Invalid ordinal, invalidated batch and stale position must be rejected.
    bool rejected = false;
    try {
      detail::NativeUndoFrame frame;
      detail::make_native_search_batch_move(*c, *p, *batch, n, frame);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "out-of-range native batch member accepted");
    detail::invalidate_native_move_batch(*batch);
    rejected = false;
    try {
      detail::NativeUndoFrame frame;
      detail::make_native_search_batch_move(*c, *p, *batch, 0, frame);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalidated native batch accepted");
    require(c->export_fen(*p) == original && c->position_key(*p) == key,
            "rejected native batch affected source board");
  }
  require(exercised >= 20, "insufficient native batch fixture coverage");
  std::cout << "b2_native_batch: PASS (" << exercised << " moves)\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "b2_native_batch: FAIL: " << error.what() << '\n';
  return 1;
}
