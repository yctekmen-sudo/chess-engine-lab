#pragma once
// B4.1: bounded in-record text for the search layer's contiguous move lists.
// Valid UCI text is four or five bytes; the seven-byte payload also preserves
// the existing malformed short-text fixtures until validation, without heap use.
#include <algorithm>
#include <array>
#include <cstdint>
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

  friend bool operator==(const CompactUciText& a, const CompactUciText& b) noexcept {
    return a.view() == b.view();
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
    return a.view() < b.view();
  }
};
static_assert(sizeof(CompactUciText) == 8, "B4.1 compact move text must occupy eight bytes");
static_assert(std::is_nothrow_move_constructible_v<CompactUciText>);
static_assert(std::is_nothrow_move_assignable_v<CompactUciText>);
} // namespace chess_lab::detail
