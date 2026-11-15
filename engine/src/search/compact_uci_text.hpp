#pragma once
// B4.1: bounded in-record text for the search layer's contiguous move lists.
// Valid UCI text is four or five bytes; the seven-byte payload also preserves
// the existing malformed short-text fixtures until validation, without heap use.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace chess_lab::detail {
struct CompactUciText final {
  std::array<char, 7> bytes{};
  std::uint8_t length = 0;

  CompactUciText() noexcept = default;
  CompactUciText(std::string_view s) { assign(s); }
  CompactUciText(const std::string& s) { assign(s); }
  CompactUciText(const char* s) { assign(std::string_view(s)); }

  void assign(std::string_view s) {
    if (s.size() > bytes.size())
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
    bytes.fill('\0');
    std::copy(s.begin(), s.end(), bytes.begin());
    length = static_cast<std::uint8_t>(s.size());
  }
  std::size_t size() const noexcept { return length; }
  char operator[](std::size_t i) const noexcept { return bytes[i]; }
  std::string_view view() const noexcept { return {bytes.data(), length}; }
  std::string str() const { return std::string(view()); }
  bool starts_with(std::string_view prefix) const noexcept { return view().starts_with(prefix); }
  operator std::string_view() const noexcept { return view(); }

  // Eight initialized bytes: seven zero-filled chars and the final length.
  // The unchanged assign() invariant is essential for raw-word equality.
  std::uint64_t raw_word() const noexcept {
    std::uint64_t word;
    static_assert(sizeof(word) == sizeof(CompactUciText));
    static_assert(std::is_trivially_copyable_v<CompactUciText>);
    std::memcpy(&word, this, sizeof(word));
    return word;
  }
  // Unsigned lexical key, including prefix and embedded-NUL ordering.
  // Native little-endian machines require byte reversal; big-endian do not.
  std::uint64_t order_key() const noexcept {
    std::uint64_t word = raw_word();
    if constexpr (std::endian::native == std::endian::little) {
#if defined(_MSC_VER)
      return _byteswap_uint64(word);
#elif defined(__GNUC__) || defined(__clang__)
      return __builtin_bswap64(word);
#else
      return ((word & 0x00000000000000ffULL) << 56) |
             ((word & 0x000000000000ff00ULL) << 40) |
             ((word & 0x0000000000ff0000ULL) << 24) |
             ((word & 0x00000000ff000000ULL) << 8) |
             ((word & 0x000000ff00000000ULL) >> 8) |
             ((word & 0x0000ff0000000000ULL) >> 24) |
             ((word & 0x00ff000000000000ULL) >> 40) |
             ((word & 0xff00000000000000ULL) >> 56);
#endif
    }
    return word;
  }

  friend bool operator==(const CompactUciText& a, const CompactUciText& b) noexcept {
    return a.raw_word() == b.raw_word();
  }
  friend bool operator==(const CompactUciText& a, const std::string& b) noexcept {
    return a.view() == b;
  }
  friend bool operator==(const CompactUciText& a, const char* b) noexcept {
    return a.view() == std::string_view(b);
  }
  friend bool operator==(const CompactUciText& a, std::string_view b) noexcept {
    return a.view() == b;
  }
  friend bool operator<(const CompactUciText& a, const CompactUciText& b) noexcept {
    return a.order_key() < b.order_key();
  }
};
static_assert(sizeof(CompactUciText) == 8, "B4.1 compact move text must occupy eight bytes");
static_assert(std::is_nothrow_move_constructible_v<CompactUciText>);
static_assert(std::is_nothrow_move_assignable_v<CompactUciText>);
} // namespace chess_lab::detail
