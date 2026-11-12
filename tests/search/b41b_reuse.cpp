// B4.1b private per-ply move-array reuse regression.
// Include production search implementation exactly as j1_native_sort does;
// this exercises the actual PreparedNode/OrderedMove layout and helper calls.
#include <cstdlib>
#include <new>
#include <cstddef>
#include <cstdio>
static std::size_t allocation_attempts=0;
static std::size_t fail_allocation_size=0;
static std::size_t injected_allocation_failures=0;
// Measure exact move-vector reserve requests independently of ordinary
// C++ allocations, including MSVC Debug STL bookkeeping.
static std::size_t tracked_move_buffer_bytes=0;
static std::size_t tracked_move_buffer_allocations=0;
void* operator new(std::size_t n) {
  ++allocation_attempts;
  if (tracked_move_buffer_bytes && n==tracked_move_buffer_bytes)
    ++tracked_move_buffer_allocations;
  // Only inject failure into the requested contiguous move-vector buffer.
  // Failing the next arbitrary allocation can hit MSVC Debug STL bookkeeping.
  if (fail_allocation_size && n==fail_allocation_size) {
    fail_allocation_size=0;
    ++injected_allocation_failures;
    throw std::bad_alloc();
  }
  if (void* p=std::malloc(n?n:1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#include "../../engine/src/search/search.cpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace chess_lab;
namespace {
void check(bool yes, const char* why){ if(!yes)throw std::runtime_error(why); }
void check_same(const std::vector<OrderedMove>& a, const std::vector<OrderedMove>& b) {
  check(a.size()==b.size(),"reuse changed legal count");
  for(std::size_t i=0;i<a.size();++i) {
    const auto& x=a[i]; const auto& y=b[i];
    check(x.text==y.text && x.native_ordinal==y.native_ordinal &&
       x.pawn_move==y.pawn_move && x.capture==y.capture && x.legal_ep==y.legal_ep &&
       x.claim_threefold==y.claim_threefold && x.claim_fifty==y.claim_fifty,
       "reuse changed canonical move/facts/order");
  }
}
void one_position(const char* fen){
  auto core=create_chess_core();auto pos=core->load_fen(fen);
  auto snapshot=core->snapshot(*pos);
  auto expected=ordered_moves(*core,*pos,nullptr,snapshot.get());
  auto reference_batch=detail::prepare_native_move_batch(*core,*pos);
  auto expected_batch=batch_ordered_moves(*core,*pos,*reference_batch,nullptr,*snapshot);
  std::vector<OrderedMove> spare;
  spare.reserve(256); // Larger than every legal chess move list.
  auto* location=spare.data();auto capacity=spare.capacity();
  auto root=prepare_node(*core,*pos); // Parent owns its distinct allocation.
  auto parent_location=root.legal.data();
  for(int i=0;i<30;++i){
    std::optional<PreparedNode> prepared;
    {
      ReclaimPreparedMoves reclaim{prepared,spare};
      prepared.emplace(prepare_node(*core,*pos,nullptr,{},false,false,true,&spare));
      check(prepared->legal.data()==location && prepared->legal.capacity()==capacity,
            "child lost recycled contiguous allocation");
      check(spare.empty(),"spare should have donated its buffer");
      check_same(prepared->legal,expected_batch);
      check(root.legal.data()==parent_location,"recycling overwrote parent list");
      check_same(root.legal,expected);
    }
    check(spare.data()==location && spare.size()==0 && spare.capacity()==capacity,
          "prepared frame failed to return capacity");
  }
  // A cancelled prepare must return its allocation before unwinding.
  CancellationSource cancelled;cancelled.request();auto token=cancelled.token();
  bool was_cancelled=false;
  try{(void)prepare_node(*core,*pos,&token,{},false,false,true,&spare);}
  catch(const detail::SearchCancelled&){was_cancelled=true;}
  check(was_cancelled,"cancelled preparation not propagated");
  check(spare.data()==location && spare.empty(),"cancelled prepare lost spare capacity");
  // Deliberately trigger the fallible vector reserve in native batch preparation.
  // This targets the production operation rather than an arbitrary Debug-only
  // allocator/proxy allocation (whose failure can terminate a noexcept path).
  std::vector<OrderedMove> undersized;
  undersized.reserve(1);
  auto* undersized_location=undersized.data();
  auto undersized_capacity=undersized.capacity();
  check(expected_batch.size()>undersized_capacity,"reserve-failure fixture needs growth");
  std::fprintf(stderr,"B41B_CHECK before_targeted_reserve_failure count=%zu\n",expected_batch.size());
  std::fflush(stderr);
  const auto failures_before=injected_allocation_failures;
  const auto requested_bytes=expected_batch.size()*sizeof(OrderedMove);
  bool bad_alloc=false;
  fail_allocation_size=requested_bytes;
  try{(void)prepare_node(*core,*pos,nullptr,{},false,false,true,&undersized);}
  catch(const std::bad_alloc&){bad_alloc=true;}
  fail_allocation_size=0;
  check(bad_alloc,"targeted vector reserve failure not propagated");
  check(injected_allocation_failures==failures_before+1,"targeted vector buffer allocation was not reached");
  check(undersized.data()==undersized_location && undersized.capacity()==undersized_capacity &&
        undersized.empty(),"failed vector reserve did not restore original capacity");
  check(spare.data()==location && spare.empty(),"failed reserve disturbed independent spare");
  std::fprintf(stderr,"B41B_CHECK targeted_reserve_failure_complete bytes=%zu\n",requested_bytes);
  std::fflush(stderr);
  // An exception AFTER successful preparation must also unwind through the guard.
  bool thrown=false;
  try{
    std::optional<PreparedNode> prepared;
    ReclaimPreparedMoves reclaim{prepared,spare};
    prepared.emplace(prepare_node(*core,*pos,nullptr,{},false,false,true,&spare));
    throw std::runtime_error("intentional unwind");
  }catch(const std::runtime_error& e){thrown=std::string(e.what())=="intentional unwind";}
  check(thrown && spare.data()==location && spare.empty(),"exception path failed to recycle");
  // A certified claim-free leaf has no move materialization, yet must not
  // leak or strand a previously allocated move-list buffer.
  {
    std::span<const RepetitionSignature> empty_history{};
    std::optional<PreparedNode> prepared;
    {
      ReclaimPreparedMoves reclaim{prepared,spare};
      prepared.emplace(prepare_node(*core,*pos,nullptr,empty_history,false,false,false,&spare));
      if (!snapshot->en_passant_square() && snapshot->halfmove_clock()<99) {
        check(prepared->mode==PreparationMode::native_claim_free_leaf,
          "eligible claim-free leaf not recognized");
        check(prepared->legal.empty(),"claim-free leaf constructed move records");
      }
    }
    check(spare.data()==location && spare.empty(),"claim-free path lost buffer");
  }
  // Track the actual move-vector reserve allocations. Broad global-new
  // counts include Debug-only STL proxies, so retain those totals for
  // diagnosis but do not use them as an allocation-reuse correctness gate.
  constexpr int rounds=40;
  const auto requested_move_buffer_bytes=expected_batch.size()*sizeof(OrderedMove);
  check(requested_move_buffer_bytes>0 && expected_batch.size()<=spare.capacity(),
        "move-buffer allocation fixture invalid");
  const auto fresh_all_before=allocation_attempts;
  const auto fresh_move_before=tracked_move_buffer_allocations;
  tracked_move_buffer_bytes=requested_move_buffer_bytes;
  for(int i=0;i<rounds;++i){(void)prepare_node(*core,*pos,nullptr,{},false,false,true);}
  const auto fresh_all=allocation_attempts-fresh_all_before;
  const auto fresh_move=tracked_move_buffer_allocations-fresh_move_before;
  const auto reused_all_before=allocation_attempts;
  const auto reused_move_before=tracked_move_buffer_allocations;
  for(int i=0;i<rounds;++i){
    std::optional<PreparedNode> prepared;
    ReclaimPreparedMoves reclaim{prepared,spare};
    prepared.emplace(prepare_node(*core,*pos,nullptr,{},false,false,true,&spare));
  }
  const auto reused_all=allocation_attempts-reused_all_before;
  const auto reused_move=tracked_move_buffer_allocations-reused_move_before;
  tracked_move_buffer_bytes=0;
  std::fprintf(stderr,
    "B41B_ALLOC requested_bytes=%zu rounds=%d all_fresh=%zu all_reused=%zu move_fresh=%zu move_reused=%zu\n",
    requested_move_buffer_bytes,rounds,fresh_all,reused_all,fresh_move,reused_move);
  std::fflush(stderr);
  check(fresh_move>=rounds,"fresh preparation did not allocate expected move buffers");
  check(fresh_move>reused_move,"recycling did not eliminate tracked move-buffer allocations");
  check(spare.data()==location && spare.empty(),"allocation comparison changed storage owner");
  check(core->export_fen(*pos)==fen,"list recycling mutated board");
  std::cout<<"B4.1b fixture all_fresh="<<fresh_all<<" all_reused="<<reused_all
           <<" move_fresh="<<fresh_move<<" move_reused="<<reused_move
           <<" capacity="<<capacity<<"\n";
}
} // namespace
int main()try{
  static_assert(sizeof(OrderedMove)==24,"B4.1a compact record must remain intact");
  one_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  one_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  one_position("1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1");
  std::cout<<"b41b_reuse PASS\n";return 0;
}catch(const std::exception& e){fail_allocation_size=0;tracked_move_buffer_bytes=0;std::cerr<<"b41b_reuse FAIL: "<<e.what()<<"\n";return 1;}
