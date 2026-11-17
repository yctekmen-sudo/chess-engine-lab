#pragma once
// C4.5 experimental private native binary move identity.
// This is an *identity*, never a certificate authorizing execution.
// The native chess-library's 16-bit move() value is retained unchanged.
// Neither text nor strings are stored in the identity.
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace chess_lab::detail {
struct NativeBinaryMove final {
    std::uint16_t bits = 0;
    static constexpr std::uint16_t kind_mask = 0xc000;
    static constexpr std::uint16_t promotion = 0x4000;
    static constexpr std::uint16_t en_passant = 0x8000;
    static constexpr std::uint16_t castling = 0xc000;
    static constexpr std::uint16_t no_move = 0;
    constexpr unsigned from() const noexcept { return (bits >> 6) & 63U; }
    constexpr unsigned raw_to() const noexcept { return bits & 63U; }
    constexpr unsigned kind() const noexcept { return bits & kind_mask; }
    constexpr unsigned promotion_index() const noexcept { return (bits >> 12) & 3U; }
    // Non-Chess960 UCI spells castling with king-to g/c; native chess-library
    // records the rook square for castling. This normalization is needed only
    // for external UCI or for the *existing* packed PV wire code.
    constexpr unsigned uci_to() const noexcept {
        return kind() == castling ? ((from() & ~7U) | (raw_to() > from() ? 6U : 2U)) : raw_to();
    }
    constexpr bool valid_shape() const noexcept {
        return bits != no_move && from() != raw_to() &&
            (kind() != castling || (from() / 8 == raw_to() / 8));
    }
    // Algebraic UCI lexical order, entirely in a 15-bit register key:
    // from(file,rank), to(file,rank), suffix(none,b,n,q,r).
    // This preserves the established canonical tie/PV contract without
    // generating or comparing UCI strings. Native generator order is not UCI order.
    constexpr std::uint16_t lexical_uci_key() const noexcept {
        const auto rank_square=[](unsigned sq) constexpr -> unsigned {
            return ((sq & 7U) << 3U) | (sq >> 3U);
        };
        // Native promotion codes N/B/R/Q = 0/1/2/3; UCI suffix lexical ranks
        // none=0, b=1, n=2, q=3, r=4. 0x70a has 3-bit fields 2,1,4,3.
        const unsigned suffix=kind()==promotion
            ? ((0x70aU >> (3U * promotion_index())) & 7U) : 0U;
        return static_cast<std::uint16_t>(
            (rank_square(from()) << 9U) | (rank_square(uci_to()) << 3U) | suffix);
    }
    // Existing PackedPV stores from:6, standard-UCI-to:6, promotion:3.
    // Native promotion bits represent N/B/R/Q as 0/1/2/3; PV uses 1/2/3/4.
    constexpr std::uint16_t pv_code() const noexcept {
        return static_cast<std::uint16_t>(from() | (uci_to() << 6) |
            ((kind() == promotion ? promotion_index() + 1U : 0U) << 12));
    }
    // Output boundary only; never call per generated move during search.
    std::string uci() const {
        if (!valid_shape()) throw std::invalid_argument("invalid native move identity");
        const unsigned f = from(), t = uci_to();
        std::array<char,5> buf{char('a' + f%8), char('1' + f/8),
                               char('a' + t%8), char('1' + t/8), '\0'};
        if (kind() == promotion) {
            buf[4] = "nbrq"[promotion_index()];
            return {buf.data(),5};
        }
        return {buf.data(),4};
    }
    friend constexpr bool operator==(NativeBinaryMove, NativeBinaryMove) = default;
};
static_assert(sizeof(NativeBinaryMove) == 2);
static_assert(std::is_trivially_copyable_v<NativeBinaryMove>);
} // namespace chess_lab::detail
