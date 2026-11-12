// B4.4: exact canonical move round-trips; inline capacity; ownership and
// output conversion; exception safety; no allocation on recursive prepend.
#include "packed_pv.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t attempts=0, cases=0;
void verify(bool ok,const char *reason) { ++cases; if(!ok)throw std::runtime_error(reason); }
}
void *operator new(std::size_t n) { ++attempts; if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p,std::size_t) noexcept { std::free(p); }
void *operator new[](std::size_t n) { ++attempts; if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete[](void *p,std::size_t) noexcept { std::free(p); }

int main() try {
  using chess_lab::detail::PackedPV;
  static_assert(sizeof(PackedPV)<=136);
  verify(PackedPV::max_size()==64,"max PV length");
  auto square=[](int i){ return std::string{char('a'+i%8),char('1'+i/8)}; };
  // Every legal square pair and all four promotion suffixes have a lossless
  // packed round-trip. The original 4- or 5-byte spelling must be exact.
  for(int from=0;from<64;++from)for(int to=0;to<64;++to)
    for(char promotion:std::string(" nbrq")) {
      auto text=square(from)+square(to);
      if(promotion!=' ')text.push_back(promotion);
      PackedPV pv;pv.prepend(text);
      auto result=pv.expand();
      verify(result.size()==1&&result[0]==text,"exhaustive UCI round-trip");
    }
  PackedPV pv;
  const auto alloc_before=attempts;
  for(int i=0;i<64;++i) {
    constexpr std::string_view moves[]={"e2e4","e7e5","a7a8q","e1g1","e5d6","b8a6","a2a1n"};
    pv.prepend(moves[i%std::size(moves)]);
  }
  verify(attempts==alloc_before,"inline PV prepend must not allocate");
  verify(pv.size()==64,"64-ply capacity");
  auto copy=pv;
  auto moved=std::move(copy);
  verify(moved.expand()==pv.expand(),"copy and move retain exact order");
  const auto snapshot=pv.expand();
  bool rejected=false;
  try { pv.prepend("a1a2"); } catch(const std::length_error&) { rejected=true; }
  verify(rejected && pv.expand()==snapshot,"overflow preserves existing PV");
  for(const auto& bad:std::vector<std::string>{"","a1","a1a","a1a2k","i1a2","a0a2","a1a9","a1a2nn","h8h7Q"}) {
    PackedPV candidate;candidate.prepend("g1f3");
    rejected=false;
    try {candidate.prepend(bad);}catch(const std::runtime_error&){rejected=true;}
    verify(rejected&&candidate.expand()==std::vector<std::string>{"g1f3"},"malformed input atomic rejection");
  }
  PackedPV empty;
  verify(empty.empty()&&empty.expand().empty(),"empty terminal/horizon PV");
  // Matched small-PV synthetic allocation fixture. This demonstrates the
  // mechanism; it is NOT a full-search allocation profile or a timing claim.
  constexpr std::string_view example[]={"e2e4","e7e5","g1f3","b8c6","f1b5","a7a6"};
  const auto baseline=attempts;
  for(unsigned trial=0;trial<128;++trial) {
    std::vector<std::string> old;
    for(auto text:example)old.insert(old.begin(),std::string(text));
    verify(old.size()==std::size(example),"reference PV size");
  }
  const auto legacy_allocations=attempts-baseline;
  const auto before_packed=attempts;
  for(unsigned trial=0;trial<128;++trial) {
    PackedPV current;
    for(auto text:example)current.prepend(text);
    verify(current.size()==std::size(example),"packed PV size");
  }
  const auto packed_allocations=attempts-before_packed;
  verify(legacy_allocations>0 && packed_allocations==0,
         "recursive PV fixture eliminates heap allocation calls");
  std::cout << "b44_pv PASS checks="<<cases<<" bytes="<<sizeof(PackedPV)
            <<" max_depth="<<PackedPV::max_size()
            <<" legacy_fixture_allocations="<<legacy_allocations
            <<" packed_fixture_allocations="<<packed_allocations<<"\n";
} catch(const std::exception& ex) {
  std::cerr<<"b44_pv FAIL: "<<ex.what()<<'\n'; return 1;
}
