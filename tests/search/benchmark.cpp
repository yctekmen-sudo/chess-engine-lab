#include <chess_lab/search.hpp>
#include <iomanip>
#include <iostream>
using namespace chess_lab;
int main(int argc, char **argv) try {
  if (argc < 2 || argc > 7)
    throw std::invalid_argument(
        "usage: m3_benchmark MODULE [depth=3] [iterations=3] [warmup=1] [FEN] "
        "[replay-comma-separated-moves]");
  int depth = argc > 2 ? std::stoi(argv[2]) : 3;
  int iterations = argc > 3 ? std::stoi(argv[3]) : 3;
  int warmup = argc > 4 ? std::stoi(argv[4]) : 1;
  if (iterations < 1 || warmup < 0)
    throw std::invalid_argument("invalid iteration counts");
  std::string fen =
      argc > 5 ? argv[5]
               : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto c = create_chess_core();
  auto p = c->load_fen(fen);
  auto h = GameHistory::starting_at(*c, *p, false);
  std::string moves = argc > 6 ? argv[6] : "";
  for (std::size_t from = 0; from < moves.size();) {
    auto end = moves.find(',', from);
    auto text = moves.substr(from, end == std::string::npos ? end : end - from);
    h = h.after_move(*c, *p, text);
    auto m = c->decode_move(*p, text);
    auto u = c->make_move(*p, *m);
    if (end == std::string::npos)
      break;
    from = end + 1;
  }
  auto module = EvaluatorModule::native(argv[1]);
  auto e = module.create("{}");
  std::cout << std::setprecision(12) << "build=" << CHESS_LAB_BENCH_BUILD
            << " compiler=" << CHESS_LAB_BENCH_COMPILER
            << " platform=" << CHESS_LAB_BENCH_PLATFORM << "\nstart_fen=" << fen
            << "\nreplay=" << moves << " history_complete=" << h.complete()
            << " occurrences=" << h.occurrences().size()
            << "\nroot_fen=" << c->export_fen(*p)
            << "\nfingerprint=" << e->identity().fingerprint
            << "\nconfig=" << e->identity().canonical_configuration
            << "\ndepth=" << depth << " warmup=" << warmup
            << " iterations=" << iterations << '\n';
  std::uint64_t checksum = 1469598103934665603ULL;
  for (bool ab : {false, true})
    for (int i = -warmup; i < iterations; ++i) {
      auto r = search(*c, *p, h, *e, {depth, ab});
      if (!r.score || !r.decision)
        throw std::runtime_error(r.diagnostic);
      std::string consumed =
          std::to_string(int(r.decision->kind)) + r.decision->move;
      for (auto &m : r.principal_variation)
        consumed += m;
      consumed += std::to_string(int(r.score->kind()));
      consumed += r.score->kind() == SearchScore::Kind::finite
                      ? std::to_string(r.score->value())
                      : std::to_string(r.score->distance());
      for (unsigned char b : consumed)
        checksum = (checksum ^ b) * 1099511628211ULL;
      if (i >= 0)
        std::cout << "ab=" << ab << " run=" << i
                  << " nodes=" << r.statistics.visited_nodes
                  << " evaluations=" << r.statistics.evaluator_calls
                  << " cutoffs=" << r.statistics.alpha_beta_cutoffs
                  << " ms=" << r.statistics.elapsed.count() / 1e6
                  << " decision=" << int(r.decision->kind) << ':'
                  << r.decision->move << " checksum=" << checksum << '\n';
    }
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
