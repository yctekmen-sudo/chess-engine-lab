#include <chess_lab/search.hpp>
#include <algorithm>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

using namespace chess_lab;
namespace {
using Clock = std::chrono::steady_clock;
std::mutex output;
void emit(const std::string &s) { std::lock_guard lock(output); std::cout << s << std::flush; }
std::vector<std::string> tokens(const std::string &s) {
  std::istringstream in(s); std::vector<std::string> v; std::string t;
  while (in >> t) { if(v.size() >= 8192) throw std::invalid_argument("too many tokens"); v.push_back(t); } return v;
}
std::uint64_t number(const std::string &s, std::uint64_t lo, std::uint64_t hi) {
  std::uint64_t n{}; auto r=std::from_chars(s.data(),s.data()+s.size(),n);
  if(r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || n<lo || n>hi) throw std::invalid_argument("integer outside bounds"); return n;
}
std::string joined(const std::vector<std::string>&v,std::size_t a,std::size_t b) {
  std::string s; for(;a<b;++a) { if(!s.empty())s+=' '; s+=v[a]; } return s;
}
std::string clean(std::string s) { for(auto &c:s) if(c=='\n'||c=='\r')c=' '; return s; }
std::string score(const SearchScore &s,bool white) {
  std::ostringstream o; o << std::setprecision(17);
  if(s.kind()==SearchScore::Kind::finite) o << "info string chesslab score raw white " << s.value() << '\n';
  else {
    bool won=(s.kind()==SearchScore::Kind::white_mate)==white;
    auto moves=(s.distance()+1)/2;
    o << "info score mate " << (won?"":"-") << moves << '\n';
  } return o.str();
}
struct Mode { std::optional<int> depth; std::optional<std::chrono::milliseconds> budget; bool infinite=false; };
class Adapter {
  std::unique_ptr<ChessCore> core=create_chess_core();
  std::unique_ptr<Position> root=core->initial_position();
  GameHistory history=GameHistory::starting_at(*core,*root,true);
  EvaluationSelection evaluation;
  TranspositionTable table{16*1024*1024};
  std::string module, config="{}";
  std::size_t multipv=1;
  bool certified=false;
  std::uint64_t policy=1;
  std::thread worker;
  std::atomic<bool> active=false;
  CancellationSource cancellation;
  std::mutex signal_mutex;
  std::condition_variable signal;
  void idle() {
    if(active.load())throw std::invalid_argument("command requires idle engine; stop first");
    if(worker.joinable())worker.join();
  }
  void identity() {
    const auto &i=evaluation.active().identity();
    const auto &ev=evaluation.active();
    if(!ev.calibration_json().empty())emit("info string chesslab evaluator calibration "+ev.calibration_digest()+" "+ev.calibration_json()+"\n");
    else if(ev.calibration_diagnostic()!="raw_only: optional capability absent")emit("info string chesslab evaluator calibration "+clean(ev.calibration_diagnostic())+"\n");
    emit("info string chesslab evaluator artifact_sha256 "+i.artifact_sha256+" fingerprint "+i.fingerprint+"\ninfo string chesslab evaluator config "+clean(i.canonical_configuration)+"\n");
  }
  void position(const std::vector<std::string>&v) {
    idle(); if(v.size()<2)throw std::invalid_argument("position missing root");
    bool complete=v[1]=="startpos"; std::size_t k=2;
    std::unique_ptr<Position> p;
    if(complete)p=core->initial_position();
    else if(v[1]=="fen" && v.size()>=8) {p=core->load_fen(joined(v,2,8));k=8;}
    else throw std::invalid_argument("expected startpos or six-field fen");
    auto h=GameHistory::starting_at(*core,*p,complete);
    if(k<v.size()) {
      if(v[k++]!="moves")throw std::invalid_argument("expected moves");
      for(;k<v.size();++k) { auto next=h.after_move(*core,*p,v[k]); auto m=core->decode_move(*p,v[k]); (void)core->make_move(*p,*m); h=std::move(next); }
    }
    root=std::move(p);history=std::move(h);
  }
  void option(const std::vector<std::string>&v) {
    idle(); if(v.size()<3 || v[1]!="name")throw std::invalid_argument("setoption name required");
    auto it=std::find(v.begin()+2,v.end(),"value");auto k=static_cast<std::size_t>(it-v.begin());
    auto name=joined(v,2,k);auto val=k<v.size()?joined(v,k+1,v.size()):"";
    if(name=="Clear Hash" && val.empty())table.clear();
    else if(name=="Hash")table.resize(number(val,0,256)*1024*1024);
    else if(name=="MultiPV")multipv=number(val,1,256);
    else if(name=="NumericTT") {if(val!="true" && val!="false")throw std::invalid_argument("expected true/false"); certified=val=="true";}
    else if(name=="ValuePolicy")policy=number(val,1,2147483647);
    else if(name=="Evaluator" || name=="EvaluatorConfig") {
      auto path=name=="Evaluator"?val:module, json=name=="EvaluatorConfig"?val:config;
      evaluation.replace(EvaluatorModule::native(path),json);module=std::move(path);config=std::move(json);identity();
    } else throw std::invalid_argument("unknown option or invalid button value");
  }
  Mode mode(const std::vector<std::string>&v) {
    Mode m;std::map<std::string,std::uint64_t> values;
    for(std::size_t k=1;k<v.size();++k) {
      auto key=v[k];
      if(key=="infinite") {if(m.infinite)throw std::invalid_argument("duplicate infinite");m.infinite=true;continue;}
      if(key!="depth" && key!="movetime" && key!="wtime" && key!="btime" && key!="winc" && key!="binc")throw std::invalid_argument("unsupported go control: "+key);
      if(++k==v.size() || values.contains(key))throw std::invalid_argument("missing or duplicate go value");
      values[key]=number(v[k],key=="depth"?1:0,key=="depth"?64:2147483647);
    }
    bool clocks=values.contains("wtime")||values.contains("btime")||values.contains("winc")||values.contains("binc");
    int modes=int(m.infinite)+int(values.contains("depth"))+int(values.contains("movetime"))+int(clocks);
    if(modes!=1)throw std::invalid_argument("exactly one go mode required");
    if(values.contains("depth"))m.depth=static_cast<int>(values["depth"]);
    if(values.contains("movetime"))m.budget=std::chrono::milliseconds(values["movetime"]);
    if(clocks) {
      if(!values.contains("wtime")||!values.contains("btime"))throw std::invalid_argument("both remaining clocks required");
      bool w=core->snapshot(*root)->side_to_move()==Side::white;
      auto remaining=values[w?"wtime":"btime"], inc=values[w?"winc":"binc"];
      m.budget=std::chrono::milliseconds(std::min(remaining,remaining/30+inc/2));
    } return m;
  }
  void search(const std::vector<std::string>&v) {
    idle();auto m=mode(v);
    auto p=core->clone(*root);auto h=history;
    if(core->legal_moves(*p).size()+1>256)throw std::runtime_error("root ranking exceeds exact capacity");
    cancellation=CancellationSource{};
    if(m.budget && m.budget->count()==0)cancellation.request();
    active=true;
    worker=std::thread([this,m,p=std::move(p),h=std::move(h)] {
      bool timer_done=false;std::thread timer;
      if(m.budget)timer=std::thread([&] {
        std::unique_lock lock(signal_mutex);
        if(!signal.wait_for(lock,*m.budget,[&]{return timer_done||cancellation.token().requested();}))cancellation.request();
      });
      auto finish_timer=[&] { {std::lock_guard lock(signal_mutex);timer_done=true;}signal.notify_all();if(timer.joinable())timer.join(); };
      try {
        bool white=core->snapshot(*p)->side_to_move()==Side::white;
        TranspositionTableUse use{&table,certified?EvaluatorCacheCompatibility::deterministic_position:EvaluatorCacheCompatibility::not_certified,policy};
        auto r=iterative_search(*core,*p,h,evaluation.active(),{m.depth,true,256,true},cancellation.token(),[&](CompletedDepth d){
          std::ostringstream o;
          for(std::size_t i=0;i<std::min(multipv,d.multipv.size());++i) {
            const auto &v=d.multipv[i];
            if(v.score.kind()==SearchScore::Kind::finite)o << score(v.score,white);
            o << "info depth " << d.depth << " time " << std::chrono::duration_cast<std::chrono::milliseconds>(d.statistics.elapsed).count() << " nodes " << d.statistics.visited_nodes << " multipv " << i+1;
            if(v.score.kind()==SearchScore::Kind::finite){if(auto cp=evaluation.active().centipawns(v.score.value()))o << " score cp " << (white?*cp:-*cp);}
            if(v.score.kind()!=SearchScore::Kind::finite) {
              bool won=(v.score.kind()==SearchScore::Kind::white_mate)==white;
              o << " score mate " << (won?"":"-") << (v.score.distance()+1)/2;
            }
            o << " pv";
            for(const auto &move:v.principal_variation)o << ' ' << move; o << '\n';
          }emit(o.str());
        },use);
        finish_timer();
        if(r.status==IterativeSearchStatus::evaluator_failed || r.status==IterativeSearchStatus::search_error)throw std::runtime_error(r.diagnostic);
        if(m.infinite && !cancellation.token().requested()) {
          emit("info string chesslab awaiting stop (terminal or depth ceiling)\n");
          std::unique_lock lock(signal_mutex);signal.wait(lock,[&]{return cancellation.token().requested();});
        }
        std::ostringstream o;
        if(r.terminal_decision) {
          if(r.terminal_score)o << score(*r.terminal_score,white);
          o << "bestmove 0000\n";
        } else if(r.completed) {
          const auto &d=*r.completed;
          auto play=std::find_if(d.multipv.begin(),d.multipv.end(),[](const auto&v){return v.decision.kind==DecisionKind::play;});
          if(play==d.multipv.end())throw std::runtime_error("no evaluated PLAY in complete ranking");
          const auto &tt=r.statistics.transposition;
          o << "info string chesslab metrics evaluator_calls " << r.statistics.evaluator_calls << " tt_lookups " << tt.lookups << " tt_exact_hits " << tt.exact_hits << '\n';
          if(d.best.decision.kind==DecisionKind::claim_draw)o << "info string chesslab decision claim_draw\n";
          else if(d.best.decision.kind==DecisionKind::claim_draw_with_intended_move)o << "info string chesslab decision claim_draw_with_intended_move " << d.best.decision.move << '\n';
          o << "bestmove " << play->decision.move << '\n';
        } else if(r.unevaluated_fallback && r.unevaluated_fallback->kind==DecisionKind::play) {
          o << "info string chesslab fallback unevaluated\nbestmove " << r.unevaluated_fallback->move << '\n';
        } else throw std::runtime_error("missing final decision");
        active=false;emit(o.str());
      } catch(const std::exception &e) {
        finish_timer();table.clear();emit("info string chesslab fatal "+clean(e.what())+"\n");std::exit(2);
      }
    });
  }
public:
  explicit Adapter(std::string default_module):module(std::move(default_module)) {evaluation.replace(EvaluatorModule::native(module),config);}
  void stop() {cancellation.request();signal.notify_all();if(worker.joinable())worker.join();active=false;}
  ~Adapter(){stop();}
  bool command(const std::string &line) {
    try {
      auto v=tokens(line);if(v.empty())return true;
      if(v[0]=="uci" && v.size()==1) {
        emit("id name Chess Lab M6\nid author Chess Engine Lab\noption name Hash type spin default 16 min 0 max 256\noption name Clear Hash type button\noption name MultiPV type spin default 1 min 1 max 256\noption name Evaluator type string default "+module+"\noption name EvaluatorConfig type string default {}\noption name NumericTT type check default false\noption name ValuePolicy type spin default 1 min 1 max 2147483647\n");identity();emit("uciok\n");
      } else if(v[0]=="isready" && v.size()==1)emit("readyok\n");
      else if(v[0]=="quit" && v.size()==1){stop();return false;}
      else if(v[0]=="stop" && v.size()==1)stop();
      else if(v[0]=="ucinewgame" && v.size()==1)idle();
      else if(v[0]=="position")position(v);
      else if(v[0]=="setoption")option(v);
      else if(v[0]=="go")search(v);
      else throw std::invalid_argument("unsupported or malformed command");
    } catch(const std::exception &e) {emit("info string chesslab rejected "+clean(e.what())+"\n");}
    return true;
  }
};
}
int main(int argc,char **argv) {
  try {
    auto path=argc>1?std::filesystem::path(argv[1]):std::filesystem::absolute(argv[0]).parent_path()/CHESS_LAB_MATERIAL_FILENAME;
    Adapter adapter(path.string());
    std::string line;bool oversized=false;char c;
    while(std::cin.get(c)) {
      if(c=='\n') {if(oversized)emit("info string chesslab rejected oversized command\n");else if(!adapter.command(line))break;line.clear();oversized=false;}
      else if(line.size()<65536)line+=c;else oversized=true;
    }
    adapter.stop();return 0;
  } catch(const std::exception &e){emit("info string chesslab fatal "+clean(e.what())+"\n");return 2;}
}
