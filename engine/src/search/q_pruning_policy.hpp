#pragma once
#include <chess_lab/evaluation.hpp>
namespace chess_lab::detail {
// Evaluator-specific quiescence registry. Thresholds are inclusive integer cp.
// Identity is a compatibility convention, not a security boundary.
// Unknown versions, configurations and ABI majors fail open (no SEE/delta pruning).
struct QPruningPolicy {
  unsigned id=0;
  bool see_enabled=false;
  int see_cutoff_cp=0;
  bool delta_enabled=false;
  int delta_margin_cp=0;
};
inline constexpr QPruningPolicy material_qpolicy{17,true,0,true,0};
inline constexpr QPruningPolicy material_pst_qpolicy{18,true,0,true,0};
inline QPruningPolicy q_pruning_policy(const EvaluationIdentity &id) {
  if(id.module_id=="org.chesslab.material" && id.abi_major==2 && id.module_version=="2.0.0" &&
      id.canonical_configuration==
      "{\"bishop_cp\":325,\"knight_cp\":300,\"pawn_cp\":100,\"queen_cp\":900,\"rook_cp\":500}")
    return material_qpolicy;
  if(id.module_id=="org.chesslab.experimental.e1.materialpst" && id.abi_major==2 &&
     id.module_version=="1.0.0" && id.canonical_configuration=="{}")
    return material_pst_qpolicy;
  return {};
}
} // namespace chess_lab::detail
