#pragma once
// M7.6 P1 PRIVATE experimental controls. No public search/UCI option.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>
namespace chess_lab::detail {
struct OrderingConfig {
  int last_ply = -1; // disabled; root=0
  std::size_t budget = 4 * 1024 * 1024;
  bool best_only = false;
  bool time_maintenance = false; // diagnostics only
};
struct OrderingStatistics {
  std::uint64_t nodes=0, edges=0, peak_nodes=0, peak_edges=0;
  std::uint64_t allocated_bytes=0, peak_used_bytes=0, allocations=0;
  std::uint64_t hits=0, misses=0, reused_moves=0, reused_siblings=0;
  std::uint64_t budget_fallbacks=0, exact_refinements=0, maintenance_ns=0;
  std::uint64_t committed_iterations=0;
};
struct OrderingControl { OrderingConfig config; OrderingStatistics *statistics; };
inline thread_local const OrderingControl *ordering_control = nullptr;
class OrderingScope {
  OrderingControl control_; const OrderingControl *old_;
public:
  explicit OrderingScope(OrderingConfig c, OrderingStatistics *s=nullptr)
      : control_{c,s}, old_(ordering_control) { ordering_control=&control_; }
  ~OrderingScope() { ordering_control=old_; }
  OrderingScope(const OrderingScope&)=delete;
  OrderingScope& operator=(const OrderingScope&)=delete;
};
class OrderingTree {
public:
  using Index=std::uint32_t;
  static constexpr Index invalid=UINT32_MAX;
private:
  struct Node { Index begin=0, count=0; };
  struct Edge { std::uint32_t move=0; Index child=invalid; std::uint32_t priority=0; };
  struct Bank { std::vector<Node> nodes; std::vector<Edge> edges; };
  Bank committed_, pending_;
  OrderingConfig config_;
  OrderingStatistics stats_;
  OrderingStatistics *output_=nullptr;
  std::size_t node_cap_=0, edge_cap_=0;
  bool enabled_=false;
  struct Measure {
    OrderingTree &tree;
    std::chrono::steady_clock::time_point start{};
    explicit Measure(OrderingTree &t):tree(t) {
      if(tree.config_.time_maintenance)start=std::chrono::steady_clock::now();
    }
    ~Measure(){if(tree.config_.time_maintenance)tree.stats_.maintenance_ns+=
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
  };
  static std::uint32_t code(std::string_view s) noexcept {
    return (std::uint32_t(s[0]-'a')<<21)|(std::uint32_t(s[1]-'1')<<18)|
           (std::uint32_t(s[2]-'a')<<15)|(std::uint32_t(s[3]-'1')<<12)|
           (s.size()==5?std::uint32_t(s[4]):0);
  }
  void memory() noexcept {
    stats_.allocated_bytes=sizeof(*this)+
      (committed_.nodes.capacity()+pending_.nodes.capacity())*sizeof(Node)+
      (committed_.edges.capacity()+pending_.edges.capacity())*sizeof(Edge);
    const auto n=committed_.nodes.size()+pending_.nodes.size();
    const auto e=committed_.edges.size()+pending_.edges.size();
    stats_.peak_nodes=std::max<std::uint64_t>(stats_.peak_nodes,n);
    stats_.peak_edges=std::max<std::uint64_t>(stats_.peak_edges,e);
    stats_.peak_used_bytes=std::max<std::uint64_t>(stats_.peak_used_bytes,sizeof(*this)+n*sizeof(Node)+e*sizeof(Edge));
  }
public:
  explicit OrderingTree(const OrderingControl *control) {
    if(!control)return;
    config_=control->config;output_=control->statistics;
    if(config_.last_ply<0)return;
    if(config_.last_ply>4 || config_.budget<=sizeof(*this)+128){++stats_.budget_fallbacks;return;}
    const auto bank=(config_.budget-sizeof(*this))/2;
    node_cap_=bank/(8*sizeof(Node));edge_cap_=(bank-node_cap_*sizeof(Node))/sizeof(Edge);
    if(!node_cap_||!edge_cap_){++stats_.budget_fallbacks;return;}
    try {
      for(auto *b:{&committed_,&pending_}) {
        b->nodes.reserve(node_cap_);++stats_.allocations;
        b->edges.reserve(edge_cap_);++stats_.allocations;
      }
      memory();
      if(stats_.allocated_bytes>config_.budget)throw std::bad_alloc{};
      committed_.nodes.push_back({});enabled_=true;memory();
    } catch(const std::bad_alloc&) {
      committed_=Bank{};pending_=Bank{};++stats_.budget_fallbacks;memory();
    }
  }
  ~OrderingTree() {if(output_)*output_=stats_;}
  bool enabled() const noexcept {return enabled_;}
  bool can_reorder(Index i) const noexcept {
    return enabled_&&i<committed_.nodes.size()&&committed_.nodes[i].count!=0;
  }
  void begin() {
    Measure measure(*this);
    pending_.nodes.assign(committed_.nodes.begin(),committed_.nodes.end());
    pending_.edges.assign(committed_.edges.begin(),committed_.edges.end());
    for(auto &e:pending_.edges)e.priority=0;
    memory();
  }
  void commit() noexcept {
    Measure measure(*this);std::swap(committed_,pending_);
    ++stats_.committed_iterations;stats_.nodes=committed_.nodes.size();stats_.edges=committed_.edges.size();memory();
  }
  template<class Moves> void prepare(Index i,const Moves &moves) {
    Measure measure(*this);
    if(i==invalid)return;
    auto &n=pending_.nodes[i];if(n.count)return;
    if(moves.size()>edge_cap_-pending_.edges.size()) {++stats_.budget_fallbacks;return;}
    n.begin=static_cast<Index>(pending_.edges.size());n.count=static_cast<Index>(moves.size());
    for(const auto &m:moves)pending_.edges.push_back({code(m.text),invalid,0});
    memory();
  }
  Index child(Index i,int ply,std::string_view move) {
    Measure measure(*this);
    if(i==invalid||ply>=config_.last_ply)return invalid;
    const auto n=pending_.nodes[i];
    for(Index j=n.begin;j<n.begin+n.count;++j)if(pending_.edges[j].move==code(move)) {
      if(pending_.edges[j].child==invalid) {
        if(pending_.nodes.size()==node_cap_){++stats_.budget_fallbacks;return invalid;}
        const auto id=static_cast<Index>(pending_.nodes.size());
        pending_.nodes.push_back({});pending_.edges[j].child=id;memory();
      }
      return pending_.edges[j].child;
    }
    return invalid;
  }
  template<class Moves,class Poll> void apply(Index i,Moves &moves,Poll poll) {
    Measure measure(*this);
    if(!can_reorder(i)){++stats_.misses;return;}
    const auto n=committed_.nodes[i];auto dest=moves.begin();std::size_t used=0;
    for(Index j=n.begin;j<n.begin+n.count;++j) {
      poll();const auto &edge=committed_.edges[j];if(!edge.priority)continue;
      auto found=std::find_if(dest,moves.end(),[&](const auto&m){return code(m.text)==edge.move;});
      if(found==moves.end())continue;
      std::rotate(dest,found,found+1);++dest;++used;
      if(config_.best_only)break;
    }
    if(used) {++stats_.hits;stats_.reused_moves+=used;if(used>1)stats_.reused_siblings+=used-1;}
    else ++stats_.misses;
  }
  void observe(Index i,std::string_view move,unsigned category,std::size_t sequence) {
    Measure measure(*this);if(i==invalid)return;const auto n=pending_.nodes[i];
    for(Index j=n.begin;j<n.begin+n.count;++j)if(pending_.edges[j].move==code(move)) {
      // Only exact improvements/cutoffs use chronology. Other groups canonical.
      pending_.edges[j].priority=(category<<24)|
          ((category>=3)?static_cast<std::uint32_t>(std::min<std::size_t>(sequence,0xffffff)):0);
      return;
    }
  }
  void seal(Index i) {
    Measure measure(*this);if(i==invalid)return;const auto n=pending_.nodes[i];
    std::sort(pending_.edges.begin()+n.begin,pending_.edges.begin()+n.begin+n.count,[](const Edge&a,const Edge&b){
      return a.priority!=b.priority?a.priority>b.priority:a.move<b.move;});
  }
  void refinement() noexcept {++stats_.exact_refinements;}
};
struct OrderingFrame {
  OrderingTree::Index &index;int &ply;OrderingTree::Index old;int old_ply;
  OrderingFrame(OrderingTree *tree,OrderingTree::Index &i,int &p,std::string_view move)
      :index(i),ply(p),old(i),old_ply(p) {
    if(tree){index=tree->child(i,p,move);++ply;}
  }
  ~OrderingFrame(){index=old;ply=old_ply;}
};
} // namespace chess_lab::detail
