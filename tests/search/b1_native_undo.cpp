// B1 experiment: private undo stack frames preserve public board semantics.
#include "native_undo_fastpath.hpp"
#include <chess_lab/search.hpp>
#include <stdexcept>
#include <string>
#include <iostream>
using namespace chess_lab;
static void require(bool b, const char* msg) { if(!b) throw std::runtime_error(msg); }
int main() try {
  auto c=create_chess_core();
  auto p=c->initial_position();
  auto initial=c->export_fen(*p);
  auto key=c->position_key(*p);
  auto first=c->decode_move(*p,"e2e4");
  detail::NativeUndoFrame f1;
  detail::make_native_search_move(*c,*p,*first,f1);
  auto after_first=c->export_fen(*p);
  require(after_first!=initial,"native move did not change position");
  auto second=c->decode_move(*p,"e7e5");
  detail::NativeUndoFrame f2;
  detail::make_native_search_move(*c,*p,*second,f2);
  // LIFO enforcement: failed out-of-order undo must leave child untouched.
  auto current=c->export_fen(*p);
  bool rejected=false;
  try {detail::undo_native_search_move(*c,*p,f1);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected && c->export_fen(*p)==current,"LIFO check failed");
  detail::undo_native_search_move(*c,*p,f2);
  require(c->export_fen(*p)==after_first,"second restore failed");
  detail::undo_native_search_move(*c,*p,f1);
  require(c->export_fen(*p)==initial && c->position_key(*p)==key,"root restore failed");
  rejected=false;
  try {detail::undo_native_search_move(*c,*p,f1);}catch(const std::invalid_argument&){rejected=true;}
  require(rejected && c->export_fen(*p)==initial,"reuse of undo frame accepted");
  // Same move with original public API must still work and restore identically.
  auto legacy=c->make_move(*p,*first);
  require(c->export_fen(*p)==after_first,"legacy transition differs");
  c->undo_move(*p,*legacy);
  require(c->export_fen(*p)==initial && c->position_key(*p)==key,"legacy restore differs");
  // Check native en-passant and castling using independently legal public moves.
  for (const auto& fen : {
     "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
     "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"}) {
    auto q=c->load_fen(fen);auto before=c->export_fen(*q);auto k=c->position_key(*q);
    auto list=c->legal_moves(*q);
    for (auto& mv: list) {
      detail::NativeUndoFrame u;
      detail::make_native_search_move(*c,*q,*mv,u);
      detail::undo_native_search_move(*c,*q,u);
      require(c->export_fen(*q)==before && c->position_key(*q)==k,"special-move restore");
    }
  }
  std::cout << "b1_native_undo: PASS\n";
  return 0;
} catch (const std::exception& e) {
  std::cerr << "b1_native_undo: FAIL: " << e.what() << "\n";
  return 1;
}
