// Compile the private implementation in this test TU: no public/test API in production.
#include <cstdlib>
#include <cstdint>
#include <new>
static bool fail_scratch = false;
static std::size_t fail_size = 0;
static std::size_t allocation_attempts = 0;
void *operator new(std::size_t n) {
  ++allocation_attempts;
  if (fail_size && n == fail_size) { fail_size = 0; throw std::bad_alloc(); }
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
struct ForeignPosition final : Position {};
// A synthetic wrapper with native public behavior must still fail native binding.
struct ForeignCore final : ChessCore {
  const ChessCore &c;
  explicit ForeignCore(const ChessCore &core):c(core){}
  std::unique_ptr<Position> initial_position()const override{return c.initial_position();}
  std::unique_ptr<Position> load_fen(std::string_view f)const override{return c.load_fen(f);}
  std::string export_fen(const Position &p)const override{return c.export_fen(p);}
  std::unique_ptr<Position> clone(const Position &p)const override{return c.clone(p);}
  std::vector<std::unique_ptr<Move>> legal_moves(const Position &p)const override{return c.legal_moves(p);}
  std::unique_ptr<UndoToken> make_move(Position &p,const Move &m)const override{return c.make_move(p,m);}
  void undo_move(Position &p,UndoToken &u)const override{c.undo_move(p,u);}
  std::string encode_move(const Position &p,const Move &m)const override{return c.encode_move(p,m);}
  std::unique_ptr<Move> decode_move(const Position &p,std::string_view m)const override{return c.decode_move(p,m);}
  void reset(Position &p,std::string_view f)const override{c.reset(p,f);}
  bool in_check(const Position &p)const override{return c.in_check(p);}
  PositionStatus status(const Position &p)const override{return c.status(p);}
  bool known_dead_position(const Position &p)const override{return c.known_dead_position(p);}
  std::uint64_t position_key(const Position &p)const override{return c.position_key(p);}
  std::uint64_t repetition_key(const Position &p)const override{return c.repetition_key(p);}
  std::unique_ptr<const PositionSnapshot> snapshot(const Position &p)const override{return c.snapshot(p);}
};
template<class F> std::string rejected(F f) {
  try { f(); } catch (const std::invalid_argument &e) { return e.what(); }
  throw std::runtime_error("expected native certificate/type rejection");
}
void encoder_guards() {
  ++cases;
  static_assert(!std::is_copy_constructible_v<detail::NativeBatchEncoder>);
  static_assert(!std::is_move_constructible_v<detail::NativeBatchEncoder>);
  auto c=create_chess_core(); auto p=c->initial_position();
  auto batch=detail::prepare_native_move_batch(*c,*p);
  const auto count=detail::native_move_batch_size(*batch);
  const auto message=rejected([&]{detail::encode_native_batch_member(*c,*p,*batch,count);});
  auto rejects=[&](const ChessCore &core,const Position &pos,const detail::NativeMoveBatch &b,std::size_t i){
    check(rejected([&]{detail::encode_native_batch_member(core,pos,b,i);})==message,"checked rejection message");
    check(rejected([&]{detail::NativeBatchEncoder e(core,pos,b);(void)e.encode(i);})==message,"bound rejection matches reference");
  };
  ForeignCore foreign(*c); ForeignPosition position;
  rejects(foreign,*p,*batch,0); rejects(*c,position,*batch,0);
  rejects(foreign,position,*batch,count); rejects(*c,*p,*batch,count);
  auto clone=c->clone(*p);rejects(*c,*clone,*batch,0);
  auto m=detail::materialize_native_batch_member(*c,*p,*batch,0);
  auto undo=c->make_move(*p,*m);rejects(*c,*p,*batch,0);
  c->undo_move(*p,*undo);
  { detail::NativeBatchEncoder e(*c,*p,*batch);
    check(e.encode(0)==detail::encode_native_batch_member(*c,*p,*batch,0),"strict undo restores original frame"); }
  check(rejected([&]{c->undo_move(*p,*undo);})==message,"consumed undo rejected");
  c->reset(*p,c->export_fen(*p)); rejects(*c,*p,*batch,0);
  batch=detail::prepare_native_move_batch(*c,*p);
  // Adversarial invalidation between members must be checked again, not cached.
  { detail::NativeBatchEncoder e(*c,*p,*batch);(void)e.encode(0);
    detail::invalidate_native_move_batch(*batch);
    check(rejected([&]{(void)e.encode(1);})==message,"per-member invalidation retained"); }
  rejects(*c,*p,*batch,0);
  // A destroyed owner's shared identity cannot authenticate a fresh position.
  batch=detail::prepare_native_move_batch(*c,*p);
  p.reset();p=c->initial_position();rejects(*c,*p,*batch,0);
  batch=detail::prepare_native_move_batch(*c,*p);auto snapshot=c->snapshot(*p);
  const auto fen=c->export_fen(*p);
  // Target the reserve itself: Debug STL may allocate a container proxy in
  // vector's noexcept default constructor before reserve is reached.
  bool failed=false;fail_size=count*sizeof(OrderedMove);
  try{(void)batch_ordered_moves(*c,*p,*batch,nullptr,*snapshot);}catch(const std::bad_alloc&){failed=true;}
  fail_size=0;
  check(failed&&c->export_fen(*p)==fen,"reserve failure restores root");
  check(batch_ordered_moves(*c,*p,*batch,nullptr,*snapshot).size()==count,"construction immediately recovers after allocation failure");
  // Binding allocates nothing. Encoding adds no allocations over the checked
  // reference, including STL Debug implementations with string proxies.
  auto before=allocation_attempts;
  { detail::NativeBatchEncoder e(*c,*p,*batch);
    check(allocation_attempts==before,"allocation-free binding");
    for(std::size_t i=0;i<count;++i)(void)e.encode(i); }
  const auto bound_allocations=allocation_attempts-before;
  before=allocation_attempts;
  for(std::size_t i=0;i<count;++i)(void)detail::encode_native_batch_member(*c,*p,*batch,i);
  check(allocation_attempts-before==bound_allocations,"no additional per-member allocation over reference");
}
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
  { detail::NativeBatchEncoder encoder(*c,*p,*batch);
    for(std::size_t i=0;i<detail::native_move_batch_size(*batch);++i)
      check(encoder.encode(i)==detail::encode_native_batch_member(*c,*p,*batch,i),"bound full UCI matches checked reference"); }
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
  encoder_guards();
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
