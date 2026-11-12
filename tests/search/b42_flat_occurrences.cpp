// B4.2: exact regression for contiguous repetition count map.
// Confirms sorted iteration, duplicate handling, erase, and 512-key history.
#include "transposition_table_internal.hpp"
#include <map>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <cstdint>
#include <type_traits>
using namespace chess_lab;
namespace d=chess_lab::detail;
static void require(bool b,const char *reason){if(!b)throw std::runtime_error(reason);}
static RepetitionSignature key(unsigned i) {
  RepetitionSignature s{};
  for(unsigned bit=0;bit<16;++bit)
    if(i & (1u<<bit))s.pieces[bit]=PieceCode::white_pawn;
  s.legal_ep=(i%7==0) ? std::optional<std::uint8_t>(std::uint8_t(i%64)) : std::nullopt;
  return s;
}
static void equivalent(const d::OccurrenceCounts &v,
                const std::map<RepetitionSignature,d::OccurrenceCount,d::SignatureLess> &m) {
  require(v.size()==m.size(),"size mismatch");
  auto a=v.begin();auto b=m.begin();
  for(;a!=v.end();++a,++b) {
    require(b!=m.end(),"iterator range mismatch");
    require(a->first==b->first,"sorted key mismatch");
    require(a->second.total==b->second.total && a->second.verified==b->second.verified,
        "counter mismatch");
  }
}
int main()try{
  static_assert(std::is_move_constructible_v<d::OccurrenceCounts>);
  d::OccurrenceCounts flat;
  std::map<RepetitionSignature,d::OccurrenceCount,d::SignatureLess> ref;
  require(flat.size()==0 && flat.begin()==flat.end(),"empty map");
  unsigned checks=0;
  // Deterministic shuffled order, repeated updates and deletion. 512 entries
  // exercise growth and shifts well above the usual five-element size.
  for(unsigned i=0;i<512;++i){
    unsigned value=(i*137U)%512U;
    auto k=key(value);
    flat[k].total += 1;ref[k].total += 1;
    if (i%13==0)equivalent(flat,ref);
    ++checks;
  }
  equivalent(flat,ref);
  for(unsigned turn=0;turn<5000;++turn){
    unsigned value=(turn*113U+47U)%512U;
    auto k=key(value);
    if (turn%5==0){
      auto a=flat.find(k);auto b=ref.find(k);
      require(a!=flat.end()&&b!=ref.end(),"missing key");
      flat.erase(a);ref.erase(b);
    } else {
      flat[k].total+=3;ref[k].total+=3;
      flat[k].verified=turn%17;ref[k].verified=turn%17;
    }
    if (turn%41==0)equivalent(flat,ref);
    ++checks;
  }
  equivalent(flat,ref);
  for (unsigned i=0;i<512;++i){
    auto k=key(i);auto a=flat.find(k);auto b=ref.find(k);
    require((a==flat.end())==(b==ref.end()),"find mismatch");
  }
  // Unknown keys must not alias an existing entry and must never be inserted by find.
  for (unsigned i=512;i<530;++i){
    auto k=key(i);auto before=flat.size();
    require(flat.find(k)==flat.end() && flat.size()==before,"false hit / find inserted");
  }
  // Very short histories are the intended hot workload.
  d::OccurrenceCounts small;
  for(unsigned i=0;i<7;++i)small[key(i)].total=10+i;
  for(unsigned i=0;i<7;++i)require(small.find(key(i))->second.total==10+i,"small lookup");
  std::cout<<"b42_flat_occurrences PASS checks="<<checks
           <<" entries="<<flat.size()<<" capacity="<<flat.capacity()<<"\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"b42_flat_occurrences FAIL: "<<e.what()<<"\n";return 1;}
