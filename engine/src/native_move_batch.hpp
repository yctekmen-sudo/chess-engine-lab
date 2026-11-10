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
// Stack-only binding for synchronous batch construction. The caller must not
// make/undo, recurse or destroy the position while this binding is in scope.
// Runtime types are checked at binding; every encode still checks provenance.
class NativeBatchEncoder final {
public:
  NativeBatchEncoder(const ChessCore &, const Position &, const NativeMoveBatch &);
  NativeBatchEncoder(const NativeBatchEncoder &) = delete;
  NativeBatchEncoder &operator=(const NativeBatchEncoder &) = delete;
  std::string encode(std::size_t ordinal) const;

private:
  const Position &position_;
  const NativeMoveBatch &batch_;
};
std::unique_ptr<Move> materialize_native_batch_member(
    const ChessCore &, const Position &, const NativeMoveBatch &, std::size_t ordinal);
void invalidate_native_move_batch(NativeMoveBatch &) noexcept;
// Exact effective EP availability without constructing a legal-move list.
bool native_has_legal_en_passant(const ChessCore &, const Position &);
// Check a table hint without encoding/sorting all legal move texts.
// Native adapter only, same canonical UCI spelling as ordered moves.
bool native_has_legal_uci_hint(const ChessCore &, const Position &,
                               std::string_view hint);
// Omit a proven claim-ineligible make/undo while retaining its serial advance.
// False requires the original exact path (counter boundary or stale certificate).
// A batch member needs no boxed Move; eager members use their existing owner.
bool native_try_skip_claim_probe(const ChessCore &, Position &,
    const NativeMoveBatch *, std::size_t ordinal, const Move *eager);
} // namespace chess_lab::detail
