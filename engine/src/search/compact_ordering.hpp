#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <new>
#include <string_view>
#include <vector>
namespace chess_lab::detail {
struct OrderingConfig {int last_ply=-1;std::size_t budget=4*1024*1024;bool best_only=false;bool time_maintenance=false;};
struct OrderingStatistics {
  std::uint64_t nodes=0,edges=0,peak_nodes=0,peak_edges=0,allocated_bytes=0,peak_used_bytes=0,allocations=0;
  std::uint64_t hits=0,misses=0,reused_moves=0,reused_siblings=0,budget_fallbacks=0,exact_refinements=0,maintenance_ns=0,committed_iterations=0;
  std::uint64_t peak_allocated_bytes=0,copied_bytes=0,transient_records_peak=0,lookup_checks=0,rotations=0;
  std::uint64_t exact_evidence=0,lower_evidence=0,upper_evidence=0,unavailable_evidence=0,score_budget_fallbacks=0;
};
struct OrderingControl {OrderingConfig config;OrderingStatistics *statistics;};
inline thread_local const OrderingControl *ordering_control=nullptr;
class OrderingScope {OrderingControl control_;const OrderingControl *old_;
public:explicit OrderingScope(OrderingConfig c,OrderingStatistics*s=nullptr):control_{c,s},old_(ordering_control){ordering_control=&control_;}
~OrderingScope(){ordering_control=old_;}OrderingScope(const OrderingScope&)=delete;OrderingScope&operator=(const OrderingScope&)=delete;};

// B2: committed state has one selected move and only actual retained path links.
class OrderingTree {
public:using Index=std::uint32_t;static constexpr Index invalid=UINT32_MAX;
private:
  static constexpr std::uint32_t active_bit=0x80000000U,move_mask=0x00ffffffU;
  struct Node{std::uint32_t incoming=0,best=0;Index first=invalid,next=invalid;};
  struct Observation{std::uint32_t move=0,priority=0;Index next=invalid;};
  std::vector<Node> committed_,pending_;
  std::vector<Index> heads_; // provisional observations only, never committed
  std::vector<Observation> observations_;
  OrderingConfig config_;OrderingStatistics stats_;OrderingStatistics *output_=nullptr;
  bool enabled_=false,iteration_=false;
  struct Measure{OrderingTree&t;std::chrono::steady_clock::time_point start{};
    explicit Measure(OrderingTree&x):t(x){if(t.config_.time_maintenance)start=std::chrono::steady_clock::now();}
    ~Measure(){if(t.config_.time_maintenance)t.stats_.maintenance_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}};
  static std::uint32_t code(std::string_view s)noexcept{return 1U+((std::uint32_t(s[0]-'a')<<21)|(std::uint32_t(s[1]-'1')<<18)|(std::uint32_t(s[2]-'a')<<15)|(std::uint32_t(s[3]-'1')<<12)|(s.size()==5?std::uint32_t(s[4]):0));}
  std::size_t reserved()const noexcept{return sizeof(*this)+(committed_.capacity()+pending_.capacity())*sizeof(Node)+heads_.capacity()*sizeof(Index)+observations_.capacity()*sizeof(Observation);}
  void memory()noexcept{
    stats_.allocated_bytes=reserved();stats_.peak_allocated_bytes=std::max<std::uint64_t>(stats_.peak_allocated_bytes,reserved());
    auto n=committed_.size()+pending_.size();auto e=observations_.size();
    stats_.peak_nodes=std::max<std::uint64_t>(stats_.peak_nodes,n);stats_.peak_edges=std::max<std::uint64_t>(stats_.peak_edges,n>1?n-2:0);
    stats_.transient_records_peak=std::max<std::uint64_t>(stats_.transient_records_peak,e);
    stats_.peak_used_bytes=std::max<std::uint64_t>(stats_.peak_used_bytes,sizeof(*this)+n*sizeof(Node)+heads_.size()*sizeof(Index)+e*sizeof(Observation));
  }
  template<class T>bool grow(std::vector<T>&v,std::size_t need){
    if(need<=v.capacity())return true;
    const auto bytes=reserved();if(bytes>=config_.budget)return false;
    auto maximum=(config_.budget-bytes)/sizeof(T); // includes old buffer while allocating new
    if(need>maximum||need>UINT32_MAX)return false;
    auto goal=std::min(maximum,std::max(need,std::max<std::size_t>(8,v.capacity()*2)));
    try{const auto before=bytes;v.reserve(goal);++stats_.allocations;
      stats_.peak_allocated_bytes=std::max<std::uint64_t>(stats_.peak_allocated_bytes,before+v.capacity()*sizeof(T));memory();
      if(stats_.peak_allocated_bytes>config_.budget){iteration_=false;return false;}
      return true;
    }catch(const std::bad_alloc&){return false;}
  }
public:
  explicit OrderingTree(const OrderingControl*c){if(!c)return;config_=c->config;output_=c->statistics;
    if(config_.last_ply<0)return;
    if(config_.last_ply>4||config_.budget<sizeof(*this)+128){++stats_.budget_fallbacks;return;}
    if(!grow(committed_,1)){++stats_.budget_fallbacks;return;}committed_.push_back({});enabled_=true;memory();}
  ~OrderingTree(){if(output_)*output_=stats_;}
  bool enabled()const noexcept{return enabled_;}
  bool can_reorder(Index i)const noexcept{return iteration_&&i<committed_.size()&&(committed_[i].incoming&active_bit);}
  void begin(){Measure timer(*this);iteration_=true;observations_.clear();heads_.clear();pending_.clear();
    if(!grow(pending_,committed_.size())||!grow(heads_,committed_.size())){++stats_.budget_fallbacks;iteration_=false;return;}
    pending_.assign(committed_.begin(),committed_.end());heads_.assign(pending_.size(),invalid);
    for(auto &n:pending_)n.best=0;
#ifdef CHESS_LAB_P1B_DIAGNOSTICS
    stats_.copied_bytes+=committed_.size()*sizeof(Node);
#endif
    memory();}
  void commit()noexcept{Measure timer(*this);++stats_.committed_iterations;
    if(iteration_){committed_.swap(pending_);stats_.nodes=committed_.size();stats_.edges=committed_.empty()?0:committed_.size()-1;}
    observations_.clear();heads_.clear();memory();}
  template<class Moves>void prepare(Index i,const Moves&){Measure timer(*this);if(iteration_&&i!=invalid)pending_[i].incoming|=active_bit;}
  Index child(Index i,int ply,std::string_view move,bool actual_positive=true){Measure timer(*this);
    if(!iteration_||!actual_positive||i==invalid||ply>=config_.last_ply)return invalid;
    const auto key=code(move);
    for(auto j=pending_[i].first;j!=invalid;j=pending_[j].next){
#ifdef CHESS_LAB_P1B_DIAGNOSTICS
      ++stats_.lookup_checks;
#endif
      if((pending_[j].incoming&move_mask)==key)return j;
    }
    if(!grow(pending_,pending_.size()+1)||!grow(heads_,pending_.size()+1)){++stats_.budget_fallbacks;return invalid;}
    const auto id=static_cast<Index>(pending_.size());const auto next=pending_[i].first;
    pending_.push_back({key,0,invalid,next});heads_.push_back(invalid);pending_[i].first=id;memory();return id;
  }
  template<class Moves,class Poll>void apply(Index i,Moves&moves,Poll poll){Measure timer(*this);
    if(!can_reorder(i)||!committed_[i].best){++stats_.misses;return;}
    const auto key=committed_[i].best;auto found=moves.end();
    for(auto it=moves.begin();it!=moves.end();++it){poll();
#ifdef CHESS_LAB_P1B_DIAGNOSTICS
      ++stats_.lookup_checks;
#endif
      if(code(it->text)==key){found=it;break;}}
    if(found==moves.end()){++stats_.misses;return;}
#ifdef CHESS_LAB_P1B_DIAGNOSTICS
    if(found!=moves.begin())++stats_.rotations;
#endif
    std::rotate(moves.begin(),found,found+1);++stats_.hits;++stats_.reused_moves;
  }
  void observe(Index i,std::string_view move,unsigned category,std::size_t sequence){Measure timer(*this);
    if(!iteration_||i==invalid)return;
    const auto key=code(move);const auto priority=(category<<24)|((category>=3)?static_cast<std::uint32_t>(std::min<std::size_t>(sequence,0xffffff)):0);
    for(auto j=heads_[i];j!=invalid;j=observations_[j].next){
#ifdef CHESS_LAB_P1B_DIAGNOSTICS
      ++stats_.lookup_checks;
#endif
      if(observations_[j].move==key){observations_[j].priority=priority;return;}}
    if(!grow(observations_,observations_.size()+1)){++stats_.budget_fallbacks;return;}
    const auto id=static_cast<Index>(observations_.size());observations_.push_back({key,priority,heads_[i]});heads_[i]=id;memory();
  }
  void seal(Index i){Measure timer(*this);if(!iteration_||i==invalid)return;
    std::uint32_t priority=0,best=0;
    for(auto j=heads_[i];j!=invalid;j=observations_[j].next){auto&e=observations_[j];if(e.priority>priority||(e.priority==priority&&e.move<best)){priority=e.priority;best=e.move;}}
    pending_[i].best=best;
  }
  void refinement()noexcept{++stats_.exact_refinements;}
};
struct OrderingFrame{OrderingTree::Index&index;int&ply;OrderingTree::Index old;int old_ply;
  OrderingFrame(OrderingTree*t,OrderingTree::Index&i,int&p,std::string_view move,bool actual_positive=true):index(i),ply(p),old(i),old_ply(p){if(t){index=t->child(i,p,move,actual_positive);++ply;}}
  ~OrderingFrame(){index=old;ply=old_ply;}};
} // namespace chess_lab::detail
