#include <chess_lab/search.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace chess_lab;
using nlohmann::json;
int main() {
  auto c=create_chess_core();std::string line;
  while(std::getline(std::cin,line))try {
    auto j=json::parse(line);auto p=c->load_fen(j.at("initialFen").get<std::string>());
    auto h=GameHistory::starting_at(*c,*p,j.at("complete").get<bool>());
    for(const auto &v:j.at("moves")) {
      auto text=v.get<std::string>();auto next=h.after_move(*c,*p,text);
      auto m=c->decode_move(*p,text);(void)c->make_move(*p,*m);h=std::move(next);
    }
    auto sig=repetition_signature(*c,*p);std::size_t count=std::count(h.occurrences().begin(),h.occurrences().end(),sig);
    auto snap=c->snapshot(*p);auto fen=c->export_fen(*p);std::istringstream in(fen);std::string placement,side,rights,ep;in>>placement>>side>>rights>>ep;
    ep="-";if(sig.legal_ep){ep=std::string(1,char('a'+*sig.legal_ep%8))+std::string(1,char('1'+*sig.legal_ep/8));}
    std::vector<std::string> legal;for(auto &m:c->legal_moves(*p))legal.push_back(c->encode_move(*p,*m));std::sort(legal.begin(),legal.end());
    auto status=c->status(*p);bool dead=c->known_dead_position(*p);std::string terminal="ongoing";
    if(status==PositionStatus::checkmate)terminal="checkmate";
    else if(status==PositionStatus::stalemate)terminal="stalemate";
    else if(dead)terminal="known dead position";
    else if(count>=5)terminal="fivefold repetition";
    else if(snap->halfmove_clock()>=150)terminal="75 move rule";
    bool immediate=terminal=="ongoing"&&(count>=3||snap->halfmove_clock()>=100);std::vector<std::string> intended;
    if(terminal=="ongoing")for(const auto &text:legal) {
      auto next_h=h.after_move(*c,*p,text);auto child=c->clone(*p);auto m=c->decode_move(*child,text);(void)c->make_move(*child,*m);
      auto s=repetition_signature(*c,*child);auto n=std::count(next_h.occurrences().begin(),next_h.occurrences().end(),s);
      if(n>=3||c->snapshot(*child)->halfmove_clock()>=100)intended.push_back(text);
    }
    std::cout<<json{{"fen",fen},{"signature",placement+" "+side+" "+rights+" "+ep},{"legal",legal},{"occurrences",count},{"dead",dead},{"terminal",terminal},{"claim",immediate},{"intended",intended}}.dump()<<std::endl;
  }catch(const std::exception &e){std::cout<<json{{"error",e.what()}}.dump()<<std::endl;}
}
