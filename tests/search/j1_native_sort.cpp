// Compile the private implementation in this test TU: no public/test API in production.
#include <cstdlib>
#include <cstdint>
#include <new>
static bool fail_scratch = false;
void *operator new(std::size_t n) {
  if (fail_scratch && n == 129 * sizeof(std::uint64_t)) throw std::bad_alloc();
  if (auto *p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
#include "../../engine/src/search/search.cpp"
#include <iostream>
#include <random>
using namespace chess_lab;
static std::size_t assertions = 0, cases = 0;
void check(bool ok, const char *why) {
  ++assertions;
  if (!ok) throw std::runtime_error(why);
}
struct OwnedMove final : Move { std::size_t id; explicit OwnedMove(std::size_t i) : id(i) {} };
std::vector<std::string> canonical() {
  std::vector<std::string> texts;
  for (char a='a';a<='h';++a) for (char b='1';b<='8';++b)
    for (char c='a';c<='h';++c) for (char d='1';d<='8';++d)
      for (char e : std::string(" bnqr")) {
        std::string s{a,b,c,d}; if (e!=' ') s+=e; texts.push_back(s);
      }
  std::sort(texts.begin(), texts.end()); return texts;
}
std::vector<OrderedMove> records(const std::vector<std::string> &texts) {
  std::vector<OrderedMove> moves; moves.reserve(texts.size());
  for (std::size_t i=0;i<texts.size();++i) {
    OrderedMove m{texts[i],std::make_unique<OwnedMove>(i)};
    m.native_ordinal=static_cast<std::uint16_t>(i);
    m.pawn_move=bool(i&1);m.capture=bool(i&2);m.legal_ep=bool(i&4);
    m.claim_threefold=bool(i&8);m.claim_fifty=bool(i&16);
    moves.push_back(std::move(m));
  }
  return moves;
}
void permutation_case(std::vector<std::string> texts) {
  ++cases; auto moves=records(texts); std::vector<const Move*> owners;
  for (const auto &m:moves) owners.push_back(m.move.get());
  auto expected=texts;std::sort(expected.begin(),expected.end());
  sort_native_batch_moves(moves);validate_native_batch_moves(moves);
  for (std::size_t i=0;i<moves.size();++i) {
    const auto &m=moves[i];const auto id=static_cast<const OwnedMove&>(*m.move).id;
    check(m.text==expected[i]&&m.text==texts[id],"exact canonical order and whole record text");
    check(m.move.get()==owners[id]&&m.native_ordinal==id,"owner and original ordinal");
    check(m.pawn_move==bool(id&1)&&m.capture==bool(id&2)&&m.legal_ep==bool(id&4)&&
      m.claim_threefold==bool(id&8)&&m.claim_fifty==bool(id&16),"all facts and claims move together");
  }
}
void reject_text(std::vector<std::string> texts) {
  ++cases;auto moves=records(texts);auto expected=texts;std::sort(expected.begin(),expected.end());
  sort_native_batch_moves(moves);
  for(std::size_t i=0;i<moves.size();++i)check(moves[i].text==expected[i],"malformed comparator order retained");
  bool rejected=false;try{validate_native_batch_moves(moves);}catch(const std::runtime_error&e){rejected=std::string(e.what())=="ambiguous/noncanonical legal move encoding";}
  check(rejected,"invalid or duplicate text must retain original rejection");
}
void native_case(const char *fen, bool castle=false, bool ep=false, bool promotion=false) {
  ++cases;auto c=create_chess_core();auto p=c->load_fen(fen);auto snapshot=c->snapshot(*p);
  auto batch=detail::prepare_native_move_batch(*c,*p);
  auto sorted=batch_ordered_moves(*c,*p,*batch,nullptr,*snapshot);
  auto eager=ordered_moves(*c,*p,nullptr,snapshot.get());
  check(sorted.size()==eager.size(),"complete native coverage");
  bool found_castle=false,found_ep=false;std::string promotions;
  for(std::size_t i=0;i<sorted.size();++i){auto &m=sorted[i];
    check(m.text==eager[i].text&&m.pawn_move==eager[i].pawn_move&&m.capture==eager[i].capture&&m.legal_ep==eager[i].legal_ep,"native batch exact lexical/fact parity");
    m.move=detail::materialize_native_batch_member(*c,*p,*batch,m.native_ordinal);
    check(c->encode_move(*p,m.executable())==m.text,"sorted ordinal materializes matching certificate");
    if(m.text=="e1g1"||m.text=="e1c1")found_castle=true;
    found_ep|=m.legal_ep;
    if(m.text.starts_with("a7a8")&&m.text.size()==5)promotions+=m.text[4];
    const auto before=c->export_fen(*p);auto u=c->make_move(*p,m.executable());
    bool rejected=false;try{detail::encode_native_batch_member(*c,*p,*batch,m.native_ordinal);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"parent member rejected in child frame");c->undo_move(*p,*u);
    check(c->export_fen(*p)==before&&c->encode_move(*p,m.executable())==m.text,"restored certificate and board");
  }
  if(castle)check(found_castle,"castling normalized to king destination");
  if(ep)check(found_ep,"legal EP metadata retained");
  if(promotion)check(promotions=="bnqr","all four promotions in canonical order");
  if(!sorted.empty()){
    auto clone=c->clone(*p);bool rejected=false;
    try{detail::materialize_native_batch_member(*c,*clone,*batch,sorted.front().native_ordinal);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"foreign position rejects sorted batch member");
    check(!detail::native_try_skip_claim_probe(*c,*clone,nullptr,0,sorted.front().move.get()),"foreign owner cannot use certificate fast path");
    // Public moves deliberately revalidate legal values on another native frame.
    check(c->encode_move(*clone,sorted.front().executable())==sorted.front().text,"public owner revalidation fallback retained");
    c->reset(*p,fen);rejected=false;
    try{detail::materialize_native_batch_member(*c,*p,*batch,sorted.front().native_ordinal);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"stale sorted batch member rejected after reset");
    check(!detail::native_try_skip_claim_probe(*c,*p,nullptr,0,sorted.front().move.get()),"stale owner cannot use certificate fast path");
    auto invalidated=detail::prepare_native_move_batch(*c,*p);detail::invalidate_native_move_batch(*invalidated);rejected=false;
    try{detail::materialize_native_batch_member(*c,*p,*invalidated,0);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"sorted ordinal cannot bypass invalidation");
  }
  CancellationSource stop;stop.request();auto token=stop.token();
  auto fresh=detail::prepare_native_move_batch(*c,*p);bool cancelled=false;
  if(detail::native_move_batch_size(*fresh)){
    try{batch_ordered_moves(*c,*p,*fresh,&token,*snapshot);}catch(const detail::SearchCancelled&){cancelled=true;}
    check(cancelled&&c->export_fen(*p)==fen,"batch cancellation preserves board before construction");
  }
}
int main() try {
  auto texts=canonical();std::mt19937 random(1703);
  for(std::size_t n:{0U,1U,2U,3U,127U,128U,129U,256U,257U,20480U}){
    auto subset=std::vector<std::string>(texts.begin(),texts.begin()+n);
    permutation_case(subset);std::reverse(subset.begin(),subset.end());permutation_case(subset);
    std::shuffle(subset.begin(),subset.end(),random);permutation_case(subset);
  }
  for(const auto &bad:std::vector<std::string>{"","a","a1a","a1a2xx","i1a2","a0a2","a1a9","a1a2k",std::string("a1a2\0",5),std::string("a1\0" "2",4),std::string("a1a2\xff",5)})reject_text({"h8h7",bad,"a1a2"});
  reject_text({"h8h7","a1a2","a1a2"});reject_text({"a7a8q","a7a8q"});
  // Explicitly reach both sides of the packed-index boundary without capping legal lists.
  for(std::size_t n:{65536U,65537U}){
    ++cases;std::vector<std::string> input;for(std::size_t i=0;i<n;++i)input.push_back(texts[(n-i)%texts.size()]);
    auto moves=records(input);auto expected=input;std::sort(expected.begin(),expected.end());sort_native_batch_moves(moves);
    std::vector<bool> seen(n);for(std::size_t i=0;i<n;++i){auto id=static_cast<const OwnedMove&>(*moves[i].move).id;
      check(moves[i].text==expected[i]&&moves[i].text==input[id]&&!seen[id],"index boundary preserves all records");seen[id]=true;}
  }
  ++cases;auto moves=records(std::vector<std::string>(texts.begin(),texts.begin()+129));
  std::reverse(moves.begin(),moves.end());std::vector<const Move*> owners;for(auto&m:moves)owners.push_back(m.move.get());
  bool failed=false;fail_scratch=true;try{sort_native_batch_moves(moves);}catch(const std::bad_alloc&){failed=true;}fail_scratch=false;
  check(failed,"checked dynamic scratch allocation propagates bad_alloc");
  for(std::size_t i=0;i<moves.size();++i)check(moves[i].move.get()==owners[i]&&moves[i].text==texts[128-i],"allocation failure occurs before record mutation");
  native_case("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  native_case("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",true);
  native_case("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",false,true);
  native_case("1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1",false,false,true);
  native_case("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1");
  std::cout<<"J1 native sort cases="<<cases<<" assertions="<<assertions<<" PASS\n";
} catch(const std::exception &e){fail_scratch=false;std::cerr<<e.what()<<'\n';return 1;}
