#include "material.hpp"
#include <chess_lab/chess_core.hpp>
#include <chess_lab/evaluation.hpp>
#include <chrono>
#include <iostream>
using namespace chess_lab;
int main(int argc, char **argv) {
  auto core = create_chess_core();
  auto module =
      argc > 1 ? EvaluatorModule::native(argv[1])
               : EvaluatorModule::bundled("material-v1", std::string(64, 'a'));
  auto instance = module.create("{}");
  ChessEvalApi api{};
  if (chess_lab_material_provider(1, 0, &api, sizeof api, nullptr, 0) != 0)
    return 1;
  ChessEvalHandle raw = nullptr;
  if (api.create_evaluator("{}", 2, &raw, nullptr, 0) != 0)
    return 1;
  double checksum = 0;
  constexpr int iterations = 200000;
  for (auto fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                   "4k3/8/8/8/8/8/P7/4K3 w - - 0 1"}) {
    auto position = core->load_fen(fen);
    auto snapshot = core->snapshot(*position);
    auto p = evaluation_snapshot(*snapshot);
    for (int i = 0; i < 1000; ++i)
      checksum += instance->evaluate(p);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
      auto converted = evaluation_snapshot(*snapshot);
      checksum += double(converted.bitboards[0] & 255);
    }
    auto mid = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
      checksum += instance->evaluate(p);
    auto end = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
      double score = 0;
      if (api.evaluate(raw, &p, &score, nullptr, 0) != 0)
        return 1;
      checksum += score;
    }
    auto raw_end = std::chrono::steady_clock::now();
    std::cout
        << fen << "\nconversion ns/call="
        << std::chrono::duration<double, std::nano>(mid - start).count() /
               iterations
        << " host+ABI evaluation ns/call="
        << std::chrono::duration<double, std::nano>(end - mid).count() /
               iterations
        << " bundled raw ABI evaluation ns/call="
        << std::chrono::duration<double, std::nano>(raw_end - end).count() /
               iterations
        << '\n';
  }
  api.destroy_evaluator(raw);
  std::cout << "iterations=" << iterations << " checksum=" << checksum << '\n';
}
