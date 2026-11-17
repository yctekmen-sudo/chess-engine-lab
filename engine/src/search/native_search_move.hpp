#pragma once
// Experimental CELab C4.5 private move identity. Native search data has no UCI
// characters; synthetic ChessCore adapters retain their bounded UCI payload.
// The native value is an identity, never a legal-execution certificate.
#include "compact_uci_text.hpp"
#include "native_binary_move.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <algorithm>
#include <stdexcept>

namespace chess_lab::detail {
struct SearchMoveIdentity final {
  std::array<char, 7> bytes{};
  std::uint8_t length = 0;
  static constexpr std::uint8_t native_marker = 255;
  SearchMoveIdentity() = default;
  SearchMoveIdentity(const CompactUciText& c) noexcept : bytes(c.bytes), length(c.length) {}
  SearchMoveIdentity(std::string_view s) { assign(s); }
  SearchMoveIdentity(const std::string& s) { assign(s); }
  SearchMoveIdentity(const char* s) { assign(s); }
  void assign(std::string_view s) {
    CompactUciText c{s}; bytes=c.bytes; length=c.length;
  }
  static SearchMoveIdentity from_native(std::uint16_t id) noexcept {
    SearchMoveIdentity m;
    m.bytes[0]=static_cast<char>(id & 255U);
    m.bytes[1]=static_cast<char>(id >> 8);
    m.length=native_marker;
    return m;
  }
  bool native() const noexcept { return length==native_marker; }
  std::uint16_t native_bits() const noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[0]) |
        (unsigned(static_cast<unsigned char>(bytes[1])) << 8));
  }
  NativeBinaryMove native_move() const noexcept { return {native_bits()}; }
  std::size_t size() const noexcept {
    return native() ? (native_move().kind()==NativeBinaryMove::promotion ? 5U : 4U) : length;
  }
  // Direct coordinate extraction; does not allocate or construct UCI text.
  unsigned from() const noexcept {
    if(native()) return native_move().from();
    return unsigned(bytes[0]-'a')+8U*unsigned(bytes[1]-'1');
  }
  unsigned to() const noexcept {
    if(native()) return native_move().uci_to();
    return unsigned(bytes[2]-'a')+8U*unsigned(bytes[3]-'1');
  }
  char operator[](std::size_t i) const noexcept {
    if(!native()) return bytes[i];
    const auto n=native_move();
    const auto sq=i<2?n.from():n.uci_to();
    switch (i) {
      case 0: case 2: return static_cast<char>('a'+sq%8);
      case 1: case 3: return static_cast<char>('1'+sq/8);
      case 4: return n.kind()==NativeBinaryMove::promotion ? "nbrq"[n.promotion_index()] : '\0';
      default: return '\0';
    }
  }
  // Text view is legal only for generic adapters; native users must inspect
  // native_bits()/coordinates or convert explicitly at an external boundary.
  std::string_view view() const noexcept {
    return native() ? std::string_view{} : std::string_view{bytes.data(),length};
  }
  std::string str() const {
    if (native()) return native_move().uci();
    return std::string(view());
  }
  bool starts_with(std::string_view prefix) const noexcept {
    if(prefix.size()>size())return false;
    for(std::size_t i=0;i<prefix.size();++i)if((*this)[i]!=prefix[i])return false;
    return true;
  }
  bool equals_uci(std::string_view s) const noexcept {
    if (s.size()!=size()) return false;
    for (std::size_t i=0;i<s.size();++i) if ((*this)[i]!=s[i]) return false;
    return true;
  }
  friend bool operator==(const SearchMoveIdentity& a, const SearchMoveIdentity& b) noexcept {
    if(a.native()&&b.native())return a.native_bits()==b.native_bits();
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(a[i]!=b[i])return false;
    return true;
  }
  friend bool operator<(const SearchMoveIdentity& a, const SearchMoveIdentity& b) noexcept {
    const auto n=std::min(a.size(),b.size());
    for(std::size_t i=0;i<n;++i)if(a[i]!=b[i])return (unsigned char)a[i]<(unsigned char)b[i];
    return a.size()<b.size();
  }
  friend bool operator==(const SearchMoveIdentity& a, std::string_view b) noexcept { return a.equals_uci(b); }
  friend bool operator==(const SearchMoveIdentity& a, const std::string& b) noexcept { return a.equals_uci(b); }
  friend bool operator==(const SearchMoveIdentity& a, const char* b) noexcept { return a.equals_uci(b); }
  // Distinguish native and synthetic move codes in private persistent ordering.
  // Generic keys occupy 24 bits; bit 24 denotes an authentic native identity.
  std::uint32_t ordering_code() const noexcept {
    if(native()) return 0x01000000U|native_bits();
    return 1U+((std::uint32_t(bytes[0]-'a')<<21)|
      (std::uint32_t(bytes[1]-'1')<<18)|(std::uint32_t(bytes[2]-'a')<<15)|
      (std::uint32_t(bytes[3]-'1')<<12)|(size()==5?std::uint32_t(bytes[4]):0));
  }
};
static_assert(sizeof(SearchMoveIdentity)==8);
static_assert(std::is_nothrow_move_constructible_v<SearchMoveIdentity>);
static_assert(std::is_nothrow_move_assignable_v<SearchMoveIdentity>);
} // namespace chess_lab::detail
