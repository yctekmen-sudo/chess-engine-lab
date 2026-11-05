#pragma once
#include <chess_lab/search.hpp>
#include <algorithm>
#include <span>
#include <vector>

namespace chess_lab::detail {
// Native-only necessary conditions. Effective EP is deliberately left to the
// existing exact hypothetical verification. References live only while the
// annotation pass leaves actual history unchanged.
class RepeatedClaimTargets {
  struct Target {
    const RepetitionSignature *signature;
    std::uint64_t changed;
    unsigned pieces;
  };
  std::vector<Target> targets_;
  unsigned parent_pieces_ = 0;
  static unsigned piece_count(const RepetitionSignature &s) {
    return unsigned(std::count_if(s.pieces.begin(), s.pieces.end(),
        [](auto p) { return p != PieceCode::empty; }));
  }
public:
  RepeatedClaimTargets() = default;
  template<class Poll>
  RepeatedClaimTargets(std::span<const RepetitionSignature> history,
                       const RepetitionSignature &parent, Poll poll) {
    poll();
    parent_pieces_ = piece_count(parent);
    targets_.reserve(history.size());
    for (std::size_t i = 0; i < history.size(); ++i) {
      poll();
      bool earlier = false;
      for (std::size_t j = 0; j < i; ++j) {
        poll();
        if (history[i] == history[j]) { earlier = true; break; }
      }
      if (earlier) continue;
      bool repeated = false;
      for (std::size_t j = i + 1; j < history.size(); ++j) {
        poll();
        if (history[i] == history[j]) { repeated = true; break; }
      }
      if (!repeated || history[i].side == parent.side) continue;
      std::uint64_t changed = 0;
      for (unsigned square = 0; square < 64; ++square) {
        if (square % 8 == 0) poll();
        if (parent.pieces[square] != history[i].pieces[square])
          changed |= std::uint64_t{1} << square;
      }
      targets_.push_back({&history[i], changed, piece_count(history[i])});
    }
  }
  template<class Poll>
  bool possible(const RepetitionSignature &parent, std::string_view text,
                bool legal_ep, bool capture, Poll poll) const {
    poll();
    // text/facts came from this prepared native legal list, never external UCI.
    auto square = [&](unsigned n) {
      return unsigned(text[n] - 'a') + 8U * unsigned(text[n + 1] - '1');
    };
    const auto from = square(0), to = square(2);
    const auto moving = parent.pieces[from];
    auto result = moving;
    const bool white = parent.side == Side::white;
    if (text.size() == 5) {
      const unsigned type = text[4] == 'n' ? 2 : text[4] == 'b' ? 3
                          : text[4] == 'r' ? 4 : 5;
      result = PieceCode(type + (white ? 0 : 6));
    }
    std::array<unsigned, 4> squares{};
    std::array<PieceCode, 4> values{};
    unsigned size = 0;
    std::uint64_t changed = 0;
    auto add = [&](unsigned s, PieceCode p) {
      squares[size] = s; values[size++] = p;
      changed |= std::uint64_t{1} << s;
    };
    add(from, PieceCode::empty); add(to, result);
    if (legal_ep) add(unsigned(int(to) + (white ? -8 : 8)), PieceCode::empty);
    auto rights = parent.castling;
    const bool king = moving == PieceCode::white_king || moving == PieceCode::black_king;
    if (king) {
      rights[white ? 0 : 2] = false; rights[white ? 1 : 3] = false;
      if (int(to) - int(from) == 2 || int(from) - int(to) == 2) {
        const unsigned base = white ? 0 : 56;
        const bool kingside = to > from;
        add(base + (kingside ? 7 : 0), PieceCode::empty);
        add(base + (kingside ? 5 : 3), white ? PieceCode::white_rook : PieceCode::black_rook);
      }
    }
    const unsigned rook_home[] = {7, 0, 63, 56};
    for (unsigned i = 0; i < 4; ++i) {
      const auto rook = i < 2 ? PieceCode::white_rook : PieceCode::black_rook;
      if ((from == rook_home[i] && moving == rook) ||
          (to == rook_home[i] && parent.pieces[to] == rook)) rights[i] = false;
    }
    for (const auto &target : targets_) {
      poll();
      const auto &s = *target.signature;
      if (s.castling != rights || target.pieces != parent_pieces_ - unsigned(capture) ||
          target.changed != changed) continue;
      bool match = true;
      for (unsigned i = 0; i < size; ++i)
        if (s.pieces[squares[i]] != values[i]) { match = false; break; }
      if (match) return true;
    }
    return false;
  }
};
} // namespace chess_lab::detail
