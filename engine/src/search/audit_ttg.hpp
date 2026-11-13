#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
namespace chess_lab::detail {
inline std::uint64_t audit_clock() noexcept {
 return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct AuditRow {
 std::uint64_t probes=0,store_attempts=0,stores=0,evictions=0,nonplay=0,unscored=0;
 std::uint64_t used_slots=0,position_slots=0,scored_slots=0,depth_wrong=0,depth_same=0;
 std::uint64_t depth_too_shallow=0,depth_deeper_eligible=0,deep_returns=0;
 std::uint64_t policy_wrong=0,compat_wrong=0,extras_wrong=0,evaluator_wrong=0,eligible=0;
 std::uint64_t exact_returns=0,bound_returns=0,claim_veto=0,pv_research=0,bound_noncutoff=0;
 std::uint64_t probe_ns=0,store_ns=0,preprobe_return_ns=0,prepare_return_ns=0;
};
struct DeepReturnEvent {
 int requested=0,source=0,ply=0,bound=0;
 std::uint64_t key=0, node_seq=0;
 int pv=0;
 int score_kind=0;
 double finite_score=0;
 int alpha_set=0,beta_set=0;
 int alpha_kind=0,beta_kind=0;
 double alpha_value=0,beta_value=0;
};
struct IterationDepthAudit {
 std::uint64_t selected=0,selected_exact=0,selected_bound=0;
 std::uint64_t nodes_seen=0,selected_pv=0,selected_nonpv=0;
 std::uint64_t probe_events=0,store_events=0,deep_touches=0,shadow_touches=0,deep_selected=0;
 std::uint64_t returns=0,returns_exact=0,returns_bound=0;
 std::uint64_t claim_veto=0,pv_research=0,bound_noncutoff=0;
 std::array<std::uint64_t,12> gap_returns{};
 std::array<std::uint64_t,12> requested_returns{};
 std::array<DeepReturnEvent,48> first_returns{};
 int saved=0;
};
struct Audit {
 bool enabled=false,heavy=false,trace=false,trace_nodes=false;
 int iteration=0;
 std::array<IterationDepthAudit,12> iterations{};
 std::array<AuditRow,12> depth{};
 std::uint64_t push_ns=0,pop_ns=0,count_ns=0,start_ns=0,hash_ns=0;
 std::uint64_t pushes=0,pops=0,counts=0,hashes=0;
 void reset() noexcept {depth={};iterations={};iteration=0;push_ns=pop_ns=count_ns=start_ns=hash_ns=pushes=pops=counts=hashes=0;}
};
extern Audit audit;
inline int bin(int depth) noexcept {return depth<0?0:depth>10?11:depth;}
struct Timer {
 std::uint64_t* sum;std::uint64_t since;
 explicit Timer(std::uint64_t *p) noexcept:sum(p),since(p?audit_clock():0){}
 ~Timer() noexcept {if(sum)*sum+=audit_clock()-since;}
};
}