#pragma once
// B4.4 private PV representation: inlined contiguous 16-bit canonical UCI
// codes; at the external result boundary, expand once to std::vector<string>.
// No dynamic memory, pointers or string objects in recursive PV fragments.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace chess_lab::detail {
class PackedPV final {
  static constexpr std::size_t maximum = 64;
  std::array<std::uint16_t,maximum> codes_{};
  std::uint8_t length_ = 0;
  static unsigned square(std::string_view s, std::size_t offset) {
    if (s[offset] < 'a' || s[offset] > 'h' ||
        s[offset+1] < '1' || s[offset+1] > '8')
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
    return unsigned(s[offset]-'a') + 8U*unsigned(s[offset+1]-'1');
  }
  static std::uint16_t encode(std::string_view s) {
    if (s.size() != 4 && s.size() != 5)
      throw std::runtime_error("ambiguous/noncanonical legal move encoding");
    auto from=square(s,0), to=square(s,2);
    unsigned promotion=0;
    if (s.size()==5) {
      switch (s[4]) {
      case 'n': promotion=1; break;
      case 'b': promotion=2; break;
      case 'r': promotion=3; break;
      case 'q': promotion=4; break;
      default: throw std::runtime_error("ambiguous/noncanonical legal move encoding");
      }
    }
    return static_cast<std::uint16_t>(from | (to<<6) | (promotion<<12));
  }
  static std::string decode(std::uint16_t code) {
    const unsigned from=code & 63U, to=(code>>6)&63U, promo=(code>>12)&7U;
    char chars[5]{char('a'+from%8),char('1'+from/8),
                  char('a'+to%8),char('1'+to/8),0};
    if (promo) {
      if (promo>4) throw std::logic_error("corrupt packed principal variation");
      chars[4]="\0nbrq"[promo];
    }
    return std::string(chars, promo?5:4);
  }
public:
  static constexpr std::size_t max_size() noexcept { return maximum; }
  std::size_t size() const noexcept { return length_; }
  bool empty() const noexcept { return length_==0; }
  void prepend(std::string_view s) {
    // Check all failure conditions before mutating the current PV.
    const auto code=encode(s);
    if (length_==maximum) throw std::length_error("principal variation exceeds 64 plies");
    std::move_backward(codes_.begin(),codes_.begin()+length_,codes_.begin()+length_+1);
    codes_[0]=code;
    ++length_;
  }
  std::vector<std::string> expand() const {
    std::vector<std::string> output;
    output.reserve(length_);
    for (unsigned i=0;i<length_;++i) output.push_back(decode(codes_[i]));
    return output;
  }
};
static_assert(sizeof(PackedPV)<=136, "B4.4 PV exceeds compact inline size budget");
static_assert(std::is_nothrow_move_constructible_v<PackedPV>);
static_assert(std::is_nothrow_move_assignable_v<PackedPV>);
} // namespace chess_lab::detail
