#pragma once
// C4.5 private search/TT action. Separating this type keeps the native move
// identity independent of public chess_lab/search.hpp for PV and ordering TUs.
// The public boundary still expands to a ChessDecision with canonical UCI.
#include <chess_lab/search.hpp>
#include "native_search_move.hpp"
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
namespace chess_lab::detail {
// Private recursive/TT action. Only terminal publication and external result
// boundaries expand this identity to ChessDecision text.
struct PrivateDecision final {
  DecisionKind kind=DecisionKind::play;
  SearchMoveIdentity move{};
  std::optional<TerminalOutcome> terminal_outcome{};
  PrivateDecision() = default;
  PrivateDecision(DecisionKind k, SearchMoveIdentity m = {},
                  std::optional<TerminalOutcome> outcome = {})
    : kind(k), move(std::move(m)), terminal_outcome(outcome) {}
  static SearchMoveIdentity public_move(const ChessDecision &d) {
    if (d.move.size()>5)throw std::logic_error("noncanonical TT decision");
    return SearchMoveIdentity(d.move);
  }
  PrivateDecision(const ChessDecision &d)
    : kind(d.kind), move(public_move(d)), terminal_outcome(d.terminal_outcome) {}
  ChessDecision expand() const {return {kind, move.str(), terminal_outcome};}
  bool operator==(const PrivateDecision&) const = default;
};
} // namespace chess_lab::detail
