#include <chess_lab/search.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace chess_lab;
namespace {
void mix(std::uint64_t &hash, const std::string &s) {
  for (unsigned char b : s) hash = (hash ^ b) * 1099511628211ULL;
}
std::uint64_t checksum(const std::vector<CompletedDepth> &events,
                       const IterativeSearchResult &r) {
  std::uint64_t hash = 1469598103934665603ULL;
  auto line = [&](const SearchVariation &v) {
    std::ostringstream s;
    s << int(v.decision.kind) << ':' << v.decision.move << ':'
      << (v.decision.terminal_outcome ? int(*v.decision.terminal_outcome) : -1)
      << ':' << int(v.score.kind()) << ':';
    if (v.score.kind() == SearchScore::Kind::finite)
      s << std::hexfloat << (v.score.value() == 0 ? 0.0 : v.score.value());
    else s << v.score.distance();
    s << ':' << int(v.pv_end) << ':' << v.pv_intended_move << ':';
    for (const auto &m : v.principal_variation) s << m << ',';
    mix(hash, s.str());
  };
  mix(hash, std::to_string(int(r.status)));
  for (const auto &d : events) {
    mix(hash, std::to_string(d.depth)); line(d.best);
    for (const auto &v : d.multipv) line(v);
  }
  if (r.terminal_decision)
    line({*r.terminal_decision, *r.terminal_score, {}, PVEnd::terminal, {}});
  return hash;
}
}
int main(int argc, char **argv) try {
  if (argc < 2 || argc > 9)
    throw std::invalid_argument("usage: m4_benchmark MODULE [depth=3] [iterations=3] [warmup=1] [FEN] [replay-comma-moves] [MultiPV=3] [alpha-beta=1]");
  const int depth = argc > 2 ? std::stoi(argv[2]) : 3;
  const int iterations = argc > 3 ? std::stoi(argv[3]) : 3;
  const int warmup = argc > 4 ? std::stoi(argv[4]) : 1;
  const size_t n = argc > 7 ? std::stoull(argv[7]) : 3;
  const bool ab = argc > 8 ? std::stoi(argv[8]) != 0 : true;
  if (iterations < 1 || warmup < 0) throw std::invalid_argument("bad repetition counts");
  const std::string fen = argc > 5 ? argv[5] : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  const std::string replay = argc > 6 ? argv[6] : "";
  auto c = create_chess_core(); auto p = c->load_fen(fen);
  auto h = GameHistory::starting_at(*c, *p, false);
  for (size_t from = 0; from < replay.size();) {
    auto end = replay.find(',', from);
    auto text = replay.substr(from, end == std::string::npos ? end : end - from);
    h = h.after_move(*c, *p, text);
    auto m = c->decode_move(*p, text); auto u = c->make_move(*p, *m);
    if (end == std::string::npos) break;
    from = end + 1;
  }
  auto module = EvaluatorModule::native(argv[1]); auto e = module.create("{}");
  std::cout << std::setprecision(12) << "build=" << CHESS_LAB_BENCH_BUILD
    << " compiler=" << CHESS_LAB_BENCH_COMPILER << " platform=" << CHESS_LAB_BENCH_PLATFORM
    << "\nstart_fen=" << fen << "\nreplay=" << replay
    << " history_complete=" << h.complete() << " occurrences=" << h.occurrences().size()
    << "\nroot_fen=" << c->export_fen(*p)
    << "\nprovider=" << e->identity().provider << " module=" << e->identity().module_id
    << "\nartifact_sha256=" << e->identity().artifact_sha256
    << "\nfingerprint=" << e->identity().fingerprint
    << "\nconfig=" << e->identity().canonical_configuration
    << "\nmax_depth=" << depth << " multipv=" << n << " ab=" << ab
    << " warmup=" << warmup << " iterations=" << iterations << '\n';
  std::optional<std::uint64_t> reference;
  for (bool order : {false, true})
    for (int i = -warmup; i < iterations; ++i) {
      std::vector<CompletedDepth> events;
      auto r = iterative_search(*c, *p, h, *e, {depth, ab, n, order}, {},
                               [&](CompletedDepth d) { events.push_back(std::move(d)); });
      if (r.status != IterativeSearchStatus::depth_reached && r.status != IterativeSearchStatus::terminal)
        throw std::runtime_error(r.diagnostic);
      auto hash = checksum(events, r);
      if (reference && *reference != hash) throw std::runtime_error("ordering changed exact decisions/ranking/PVs");
      reference = hash;
      if (i < 0) continue;
      SearchStatistics prior;
      for (const auto &d : events) {
        std::cout << "order=" << order << " run=" << i << " depth=" << d.depth
          << " iteration_nodes=" << d.statistics.visited_nodes - prior.visited_nodes
          << " cumulative_nodes=" << d.statistics.visited_nodes
          << " iteration_evaluations=" << d.statistics.evaluator_calls - prior.evaluator_calls
          << " cumulative_evaluations=" << d.statistics.evaluator_calls
          << " cumulative_cutoffs=" << d.statistics.alpha_beta_cutoffs
          << " cumulative_ms=" << d.statistics.elapsed.count()/1e6 << '\n';
        prior = d.statistics;
      }
      std::cout << "order=" << order << " run=" << i << " status=" << int(r.status)
        << " nodes=" << r.statistics.visited_nodes << " evaluations=" << r.statistics.evaluator_calls
        << " cutoffs=" << r.statistics.alpha_beta_cutoffs
        << " ms=" << r.statistics.elapsed.count()/1e6 << " checksum=" << hash;
      if (r.completed) std::cout << " decision=" << int(r.completed->best.decision.kind) << ':' << r.completed->best.decision.move;
      std::cout << '\n';
    }
  auto fixed = search(*c, *p, h, *e, {depth, ab});
  if (!fixed.score) throw std::runtime_error(fixed.diagnostic);
  std::cout << "m3_reference depth=" << depth << " nodes=" << fixed.statistics.visited_nodes
    << " evaluations=" << fixed.statistics.evaluator_calls << " cutoffs=" << fixed.statistics.alpha_beta_cutoffs
    << " ms=" << fixed.statistics.elapsed.count()/1e6 << " decision=" << int(fixed.decision->kind) << ':' << fixed.decision->move << '\n';
} catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
