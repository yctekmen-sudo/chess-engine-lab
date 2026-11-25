#pragma once

#include <chess_lab/chess_core.hpp>
#include "search/compact_uci_text.hpp" // Native search-private in-record UCI.
#include <cstddef>
#include <array>

// Adapter-private capabilities. No native chess-library types or executable
// values escape the adapter, and none of these are public ChessCore methods.
namespace chess_lab::detail {
struct NativeMoveBatch;
struct NativeMoveBatchDeleter {
  void operator()(NativeMoveBatch *) const noexcept;
};
using NativeMoveBatchOwner =
    std::unique_ptr<NativeMoveBatch, NativeMoveBatchDeleter>;

// S1.7g.1: plain adapter-private data, no vendor types escape.
// S1.12: private native node identity. No board pointer is retained.
struct NativeSearchMetadata {
    std::optional<std::uint8_t> raw_ep;
    std::uint32_t half=0, full=1;
};
void native_read_search_identity(const ChessCore&, const Position&,
    std::array<PieceCode,64>&, Side&, std::array<bool,4>&, NativeSearchMetadata&);
struct NativeSeeMasks {
    std::uint64_t sides[2]; // black, white
    std::uint64_t kinds[7]; // empty (zero), pawn, knight, bishop, rook, queen, king
    int kings[2];
};
// Private search-only pool: caller must have unique ownership and no borrowers.
// Only a native Snap originally allocated non-const by Core::snapshot is refreshed.
bool native_refresh_search_snapshot(const ChessCore &,const Position &,
    std::unique_ptr<const PositionSnapshot> &);
bool native_see_masks(const ChessCore &, const Position &, NativeSeeMasks &) noexcept;
NativeMoveBatchOwner prepare_native_move_batch(const ChessCore &, const Position &);
// S1.7a isolated native quiescence generator; use only on certified claim-free leaves.
NativeMoveBatchOwner prepare_native_qsearch_batch(const ChessCore &, const Position &, bool in_check, bool include_quiet_promotions);
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
  // Avoid constructing temporary std::string in search's native-batch path.
  // Keep encode() unchanged for independent reference/certificate tests.
  CompactUciText encode_compact(std::size_t ordinal) const;
  // Return raw 16-bit native identity with the full per-member certificate check.
  std::uint16_t identity(std::size_t ordinal) const;

private:
  const Position &position_;
  const NativeMoveBatch &batch_;
};
std::unique_ptr<Move> materialize_native_batch_member(
    const ChessCore &, const Position &, const NativeMoveBatch &, std::size_t ordinal);
void invalidate_native_move_batch(NativeMoveBatch &) noexcept;
// Exact effective EP availability without constructing a legal-move list.
bool native_has_legal_en_passant(const ChessCore &, const Position &);
// Native search-only TT key; exact equality still uses RepetitionSignature.
// legal_ep must describe the current live position, not an ancestor.
std::uint64_t native_tt_repetition_key(const ChessCore &,const Position &,
                                      std::optional<std::uint8_t> legal_ep);
// Check a table hint without encoding/sorting all legal move texts.
// Native adapter only, same canonical UCI spelling as ordered moves.
bool native_has_legal_uci_hint(const ChessCore &, const Position &,
                               std::string_view hint);
// Check native TT hints without encoding UCI. Identity is not a certificate.
bool native_has_legal_move_identity(const ChessCore &, const Position &,
                                    std::uint16_t identity);
// Omit a proven claim-ineligible make/undo while retaining its serial advance.
// False requires the original exact path (counter boundary or stale certificate).
// A batch member needs no boxed Move; eager members use their existing owner.
bool native_try_skip_claim_probe(const ChessCore &, Position &,
    const NativeMoveBatch *, std::size_t ordinal, const Move *eager);
} // namespace chess_lab::detail
