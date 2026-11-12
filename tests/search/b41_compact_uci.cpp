// B4.1: compact, cache-friendly move-text regression, independent of chess engine.
#include "compact_uci_text.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
using chess_lab::detail::CompactUciText;
namespace {
struct Record { CompactUciText text; std::unique_ptr<int> owner; bool pawn=false,capture=false,ep=false,three=false,fifty=false; std::uint16_t ordinal=0; };
static_assert(sizeof(Record)<=24,"B4.1 record must fit at least two per typical 64-byte cache line");
void check(bool b,const char* why){ if(!b) throw std::runtime_error(why); }
}
int main() try {
  check(sizeof(CompactUciText)==8,"unexpected inline text size");
  check(sizeof(Record)==24,"unexpected dense move-record layout");
  std::vector<std::string> inputs={"a7a8b","a7a8n","a7a8q","a7a8r","a1a2","h8h7","e1c1","e1g1","e2e4"};
  for(char a='a';a<='h';++a)for(char b='1';b<='8';++b)
    for(char c='a';c<='h';++c)for(char d='1';d<='8';++d) {
      std::string s{a,b,c,d};inputs.push_back(s);
      for(char e : std::string("bnqr")){auto x=s;x+=e;inputs.push_back(x);}
    }
  auto expected=inputs;std::sort(expected.begin(),expected.end());
  std::mt19937 rng(1703);std::shuffle(inputs.begin(),inputs.end(),rng);
  std::vector<Record> moves;moves.reserve(inputs.size());
  for(std::size_t i=0;i<inputs.size();++i){
    Record r{inputs[i],std::make_unique<int>(static_cast<int>(i))};
    r.ordinal=static_cast<std::uint16_t>(i);
    r.pawn=bool(i&1);r.capture=bool(i&2);r.ep=bool(i&4);r.three=bool(i&8);r.fifty=bool(i&16);
    moves.push_back(std::move(r));
  }
  std::sort(moves.begin(),moves.end(),[](const auto& a,const auto& b){return a.text<b.text;});
  for(std::size_t i=0;i<moves.size();++i){
    auto& m=moves[i]; auto id=*m.owner;
    check(m.text==expected[i] && m.text==inputs[id],"canonical lexical comparison mismatch");
    check(m.ordinal==static_cast<std::uint16_t>(id) && m.pawn==bool(id&1) && m.capture==bool(id&2) &&
      m.ep==bool(id&4) && m.three==bool(id&8) && m.fifty==bool(id&16),"record ownership/flags corrupted");
    std::string_view view=m.text;
    check(view==inputs[id] && m.text.str()==inputs[id] && inputs[id]==m.text,"string boundary changed");
  }
  for(auto bad: {std::string(""),std::string("a"),std::string("a1a"),std::string("a1a2xx"),
                 std::string("a1a2\0",5),std::string("a1\0" "2",4),std::string("a1a2\xff",5)}){
    CompactUciText value{bad};
    check(value.str()==bad && value.size()==bad.size(),"malformed short text lost before validation");
  }
  CompactUciText x{"a7a8q"};check(x.starts_with("a7a8") && x[4]=='q',"promotion encoding changed");
  CompactUciText y{std::string("a1a2")};check(y=="a1a2" && "a1a2"==y,"symmetric comparison failed");
  bool reject=false;
  try{CompactUciText too_long{"a1a2excess"};(void)too_long;}
  catch(const std::runtime_error& e){reject=std::string(e.what())=="ambiguous/noncanonical legal move encoding";}
  check(reject,"unrepresentable invalid text was not rejected");
  std::cout<<"b41_compact_uci PASS records="<<moves.size()<<" record_bytes="<<sizeof(Record)
           <<" text_bytes="<<sizeof(CompactUciText)<<'\n';
  return 0;
}catch(const std::exception& e){std::cerr<<"b41_compact_uci FAIL: "<<e.what()<<'\n';return 1;}
