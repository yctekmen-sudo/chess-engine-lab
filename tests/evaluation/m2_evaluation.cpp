#include "json.hpp"
#include "material.hpp"
#include <array>
#include <bit>
#include <chess_lab/chess_core.hpp>
#include <chess_lab/evaluation.hpp>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
using namespace chess_lab;
using namespace chess_lab::eval_detail;
void test(bool b, const char *m) {
  if (!b)
    throw std::runtime_error(m);
}
void rejects(const std::function<void()> &fn) {
  try {
    fn();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error("expected rejection");
}
std::string sha(64, 'a');
struct SourceFixture : PositionSnapshot {
  std::array<PieceCode, 64> board{};
  Side turn = Side::white;
  uint32_t version = 1, full = 1;
  std::optional<uint8_t> ep;
  SourceFixture() {
    board[4] = PieceCode::white_king;
    board[60] = PieceCode::black_king;
  }
  std::span<const PieceCode, 64> squares() const noexcept override {
    return board;
  }
  Side side_to_move() const noexcept override { return turn; }
  bool can_castle_kingside(Side) const noexcept override { return false; }
  bool can_castle_queenside(Side) const noexcept override { return false; }
  std::optional<uint8_t> en_passant_square() const noexcept override {
    return ep;
  }
  uint32_t halfmove_clock() const noexcept override { return UINT32_MAX; }
  uint32_t fullmove_number() const noexcept override { return full; }
  uint32_t schema_version() const noexcept override { return version; }
};
void source_validation() {
  SourceFixture s;
  test(evaluation_snapshot(s).halfmove_clock == UINT32_MAX,
       "lossless source counter");
  s.version = 2;
  rejects([&] { evaluation_snapshot(s); });
  s.version = 1;
  s.turn = static_cast<Side>(2);
  rejects([&] { evaluation_snapshot(s); });
  s.turn = Side::white;
  s.board[0] = static_cast<PieceCode>(13);
  rejects([&] { evaluation_snapshot(s); });
  s.board[0] = PieceCode::empty;
  s.full = 0;
  rejects([&] { evaluation_snapshot(s); });
  s.full = 1;
  s.ep = 64;
  rejects([&] { evaluation_snapshot(s); });
  s.ep.reset();
  s.board[60] = PieceCode::empty;
  rejects([&] { evaluation_snapshot(s); });
}
ChessEvalApi original{};
std::string mode, custom_json;
int live = 0, calls = 0;
ChessEvalStatus text_output(std::string text, char *o, uint64_t n, uint64_t *r,
                            char *e, uint64_t en) {
  if (en)
    e[0] = 0;
  *r = text.size() + 1;
  if (!o)
    return 0;
  if (n < *r)
    return 7;
  std::memcpy(o, text.c_str(), *r);
  return 0;
}
ChessEvalStatus metadata(char *o, uint64_t n, uint64_t *r, char *e,
                         uint64_t en) {
  if (mode == "huge") {
    *r = 1048577;
    return 0;
  }
  if (mode == "unterminated" && o) {
    std::memset(o, 'x', n);
    *r = n;
    return 0;
  }
  if (mode == "length" && o) {
    auto s = original.get_module_info_json(o, n, r, e, en);
    --*r;
    return s;
  }
  if (mode == "utf8")
    return text_output(std::string("{\"bad\":\"") + char(0xff) + "\"}", o, n, r,
                       e, en);
  if (mode == "json")
    return text_output("not json", o, n, r, e, en);
  if (mode == "metadata")
    return text_output("{}", o, n, r, e, en);
  if (mode == "snapshot")
    return text_output(
        R"({"module_id":"test","display_name":"test","description":"test","module_version":"1","parameter_schema_version":1,"required_snapshot_major":1,"minimum_snapshot_minor":1,"score_units":"arbitrary"})",
        o, n, r, e, en);
  return original.get_module_info_json(o, n, r, e, en);
}
ChessEvalStatus parameter_schema(char *o, uint64_t n, uint64_t *r, char *e,
                                 uint64_t en) {
  if (mode == "schema")
    return text_output(custom_json, o, n, r, e, en);
  return original.get_parameter_schema_json(o, n, r, e, en);
}
ChessEvalStatus create(const char *c, uint64_t n, ChessEvalHandle *h, char *e,
                       uint64_t en) {
  if (mode == "null_handle") {
    *h = nullptr;
    return 0;
  }
  if (mode == "oom") {
    *h = nullptr;
    return 5;
  }
  auto s = original.create_evaluator(c, n, h, e, en);
  if (*h)
    ++live;
  if (mode == "create_failure")
    return 6;
  return s;
}
void destroy(ChessEvalHandle h) {
  --live;
  original.destroy_evaluator(h);
}
ChessEvalStatus config(ChessEvalHandle h, char *o, uint64_t n, uint64_t *r,
                       char *e, uint64_t en) {
  if (mode == "effective")
    return text_output("{}", o, n, r, e, en);
  if (mode == "substitute")
    return text_output(
        R"({"pawn_value":2,"knight_value":3,"bishop_value":3.25,"rook_value":5,"queen_value":9})",
        o, n, r, e, en);
  return original.get_effective_configuration_json(h, o, n, r, e, en);
}
ChessEvalStatus evaluate(ChessEvalHandle h, const ChessEvalPosition *p,
                         double *s, char *e, uint64_t en) {
  ++calls;
  *s = 0; // Diagnostic violations must fail independently of finite-score checks.
  if (mode == "unknown")
    return 99;
  if (mode == "negative_status")
    return -1;
  if (mode == "error" || mode == "success_diagnostic") {
    std::strcpy(e, "fixture failure");
    return mode == "error" ? CHESS_EVAL_INTERNAL_ERROR : CHESS_EVAL_OK;
  }
  if (mode == "bad_diagnostic" || mode == "success_unterminated") {
    std::memset(e, 'x', en);
    return mode == "bad_diagnostic" ? CHESS_EVAL_INTERNAL_ERROR : CHESS_EVAL_OK;
  }
  if (mode == "utf8_diagnostic" || mode == "success_invalid_utf8") {
    e[0] = char(0xff);
    e[1] = 0;
    return mode == "utf8_diagnostic" ? CHESS_EVAL_INTERNAL_ERROR : CHESS_EVAL_OK;
  }
  if (mode == "nan" || mode == "inf" || mode == "-inf") {
    *s = mode == "nan"   ? std::numeric_limits<double>::quiet_NaN()
         : mode == "inf" ? std::numeric_limits<double>::infinity()
                         : -std::numeric_limits<double>::infinity();
    return 0;
  }
  return original.evaluate(h, p, s, e, en);
}
ChessEvalStatus fixture(uint64_t a, uint64_t b, void *out, uint64_t n, char *e,
                        uint64_t en) {
  auto status = chess_lab_material_provider(a, b, out, n, e, en);
  auto &api = *static_cast<ChessEvalApi *>(out);
  if (mode == "abi")
    api.abi_major = 2;
  if (mode == "minor")
    api.abi_minor = 1;
  if (mode == "table_size")
    api.struct_size = 80;
  if (mode == "pointer")
    api.evaluate = nullptr;
  else
    api.evaluate = evaluate;
  api.get_module_info_json = metadata;
  api.get_parameter_schema_json = parameter_schema;
  api.create_evaluator = create;
  api.destroy_evaluator = destroy;
  api.get_effective_configuration_json = config;
  return status;
}
void golden(const EvaluatorModule &module) {
  auto core = create_chess_core();
  auto instance = module.create("{}");
  struct Vector {
    const char *fen;
    double score;
  };
  const Vector vectors[] = {
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0},
      {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
       0},
      {"4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 99", 0},
      {"4k3/8/8/4p3/8/8/8/4K3 w - e6 0 17", -1},
      {"4k3/8/8/8/8/8/P7/4K3 w - - 4294967295 4294967295", 1},
      {"4k3/8/8/8/8/8/P7/4K3 b - - 270 70000", 1},
      {"Q3k3/8/8/8/8/8/8/4K3 b - - 0 1", 9},
      {"4k3/8/8/8/8/8/8/q3K3 w - - 0 1", -9}};
  for (auto &v : vectors) {
    auto pos = core->load_fen(v.fen);
    auto src = core->snapshot(*pos);
    auto p = evaluation_snapshot(*src);
    std::istringstream in(v.fen);
    std::string board, turn, rights, ep;
    uint64_t half, full;
    in >> board >> turn >> rights >> ep >> half >> full;
    std::array<uint64_t, 12> boards{};
    int rank = 7, file = 0;
    std::string codes = "PNBRQKpnbrqk";
    for (char c : board) {
      if (c == '/') {
        --rank;
        file = 0;
      } else if (c >= '1' && c <= '8')
        file += c - '0';
      else {
        boards[codes.find(c)] |= uint64_t{1} << (8 * rank + file++);
      }
    }
    for (int i = 0; i < 12; ++i)
      test(p.bitboards[i] == boards[i], "golden bitboard mismatch");
    uint64_t mask = 0;
    for (size_t i = 0; i < 4; ++i)
      if (rights.find(std::string("KQkq")[i]) != rights.npos)
        mask |= 1u << i;
    test(p.struct_size == 160 && p.schema_major == 1 && p.schema_minor == 0,
         "snapshot header");
    test(p.side_to_move == (turn == "b") && p.castling_rights == mask &&
             p.halfmove_clock == half && p.fullmove_number == full,
         "golden metadata");
    test(p.en_passant_square ==
             (ep == "-" ? UINT64_MAX
                        : uint64_t((ep[1] - '1') * 8 + ep[0] - 'a')),
         "raw FEN EP");
    auto copy = p;
    test(instance->evaluate(p) == v.score, "golden material score");
    test(std::memcmp(&copy, &p, sizeof p) == 0, "read-only snapshot");
    std::cout << "golden " << v.fen << " score=" << v.score << '\n';
  }
  // Promotions through M1, both colors, all four piece choices.
  for (auto fen :
       {"4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "4k3/8/8/8/8/8/p7/4K3 b - - 0 1"}) {
    for (char promotion : std::string("qnrb")) {
      auto pos = core->load_fen(fen);
      bool white = std::string(fen).find(" w ") != std::string::npos;
      auto move = core->decode_move(*pos, std::string(white ? "a7a8" : "a2a1") +
                                              promotion);
      auto undo = core->make_move(*pos, *move);
      auto p = evaluation_snapshot(*core->snapshot(*pos));
      double expected = promotion == 'q'   ? 9
                        : promotion == 'r' ? 5
                        : promotion == 'n' ? 3
                                           : 3.25;
      test(instance->evaluate(p) == (white ? expected : -expected),
           "promotion score");
    }
  }
  auto pos = core->load_fen("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");
  auto p = evaluation_snapshot(*core->snapshot(*pos));
  auto other = module.create("{\"pawn_value\":2}");
  test(other->evaluate(p) == 2 && instance->evaluate(p) == 1,
       "independent configuration");
  for (int i = 0; i < 100; ++i) {
    test(other->evaluate(p) == 2 && instance->evaluate(p) == 1,
         "call-order independence");
  }
  auto same = module.create("{\"queen_value\":9.0,\"pawn_value\":1e0}");
  test(same->identity().fingerprint == instance->identity().fingerprint,
       "canonical defaults identity");
  test(other->identity().fingerprint != instance->identity().fingerprint,
       "parameter identity change");
  EvaluationSelection selection;
  selection.replace(module, "{}");
  auto old = selection.active().identity().fingerprint;
  rejects([&] { selection.replace(module, "{\"unknown\":1}"); });
  test(selection.generation() == 1 &&
           selection.active().identity().fingerprint == old,
       "transactional failed replacement");
  selection.replace(module, "{\"pawn_value\":2}");
  test(selection.generation() == 2 && selection.active().evaluate(p) == 2,
       "cache invalidation generation");
  selection.replace(module, "{}");
  test(selection.generation() == 3, "module reselection invalidation");
  for (auto json :
       {"[]", "null", "{", "{\"pawn_value\":-1}", "{\"pawn_value\":\"2\"}",
        "{\"pawn_value\":true}", "{\"pawn_value\":1e101}",
        "{\"pawn_value\":1,\"pawn_value\":2}", "{\"pawn_value\":1e400}"})
    rejects([&] { module.create(json); });
  std::cout << "golden, independent instances, immutable configuration, "
               "transaction and identity checks passed\n";
}
void raw_contract() {
  ChessEvalApi api{};
  char e[32];
  test(chess_lab_material_provider(1, 0, &api, 72, e, sizeof e) == 0 &&
           e[0] == 0,
       "entry point");
  original = api;
  test(chess_lab_material_provider(2, 0, &api, 72, e, sizeof e) == 2,
       "ABI major rejection");
  test(chess_lab_material_provider(1, 0, nullptr, 72, e, sizeof e) == 1,
       "null API");
  test(chess_lab_material_provider(1, 0, &api, 71, e, sizeof e) == 7,
       "short API");
  test(chess_lab_material_provider(1, 99, &api, 72, e, sizeof e) == 0 &&
           api.abi_minor == 0,
       "minor downgrade negotiation");
  uint64_t n = 0;
  test(api.get_module_info_json(nullptr, 0, &n, e, sizeof e) == 0 && n > 1,
       "query");
  std::vector<char> small(n - 1, 'z');
  auto before = small;
  test(api.get_module_info_json(small.data(), small.size(), &n, e, sizeof e) ==
               7 &&
           small == before,
       "no truncated JSON");
  test(api.get_module_info_json(nullptr, 1, &n, e, sizeof e) == 1,
       "null JSON output");
  test(api.get_module_info_json(nullptr, 0, nullptr, e, sizeof e) == 1,
       "null required length");
  test(api.get_module_info_json(nullptr, 0, &n, nullptr, 1) == 1,
       "invalid diagnostic pointer");
  ChessEvalHandle h = reinterpret_cast<void *>(1);
  test(api.create_evaluator(nullptr, 0, &h, e, sizeof e) == 1 && !h,
       "failed create nulls output");
  test(api.create_evaluator("{}", 2, nullptr, e, sizeof e) == 1,
       "null handle output");
  test(api.create_evaluator("{", 1, &h, e, 1) == 4 && !h && e[0] == 0,
       "tiny error buffer terminated");
  test(api.create_evaluator("{}", 2, &h, nullptr, 0) == 0 && h,
       "opt-out diagnostics");
  double score = 0;
  ChessEvalPosition p{};
  test(api.evaluate(h, nullptr, &score, e, sizeof e) == 1, "null snapshot");
  test(api.evaluate(nullptr, &p, &score, e, sizeof e) == 1, "null instance");
  test(api.evaluate(h, &p, nullptr, e, sizeof e) == 1, "null score");
  test(api.get_effective_configuration_json(nullptr, nullptr, 0, &n, e,
                                            sizeof e) == 1,
       "null effective handle");
  auto core = create_chess_core();
  p = evaluation_snapshot(*core->snapshot(*core->initial_position()));
  auto invalid = [&](auto mutate, ChessEvalStatus expected) {
    auto q = p;
    mutate(q);
    test(api.evaluate(h, &q, &score, e, sizeof e) == expected,
         "invalid snapshot status");
  };
  invalid([](auto &q) { q.struct_size = 159; }, 3);
  invalid([](auto &q) { q.schema_major = 2; }, 3);
  invalid([](auto &q) { q.side_to_move = 2; }, 1);
  invalid([](auto &q) { q.castling_rights = 16; }, 1);
  invalid([](auto &q) { q.fullmove_number = 0; }, 1);
  invalid([](auto &q) { q.en_passant_square = 64; }, 1);
  invalid([](auto &q) { q.en_passant_square = 0; }, 1);
  invalid([](auto &q) { q.bitboards[0] |= 1; }, 1);
  invalid([](auto &q) { q.bitboards[5] = 0; }, 1);
  invalid([](auto &q) { q.bitboards[0] |= q.bitboards[6]; }, 1);
  auto newer = p;
  newer.schema_minor = 1;
  test(api.evaluate(h, &newer, &score, e, sizeof e) == 0,
       "compatible appended snapshot minor");
  api.destroy_evaluator(h);
  // Independent module-side validation, bypassing host's early checks.
  for (auto config :
       {"[]", "{\"unknown\":0}", "{\"pawn_value\":false}",
        "{\"pawn_value\":-1}", "{\"pawn_value\":1,\"pawn_value\":2}"}) {
    h = reinterpret_cast<void *>(1);
    test(api.create_evaluator(config, std::strlen(config), &h, e, sizeof e) ==
                 4 &&
             !h && e[0],
         "module configuration rejection");
  }
  std::cout << "raw ABI argument, buffer, status, snapshot and configuration "
               "checks passed\n";
}
void hostile() {
  for (auto m :
       {"abi", "minor", "table_size", "pointer", "huge", "unterminated",
        "length", "utf8", "json", "metadata", "snapshot"}) {
    mode = m;
    rejects([&] { EvaluatorModule::provider(fixture, sha, "test"); });
    test(live == 0, "no creation before negotiation validation");
  }
  for (auto m :
       {"null_handle", "oom", "create_failure", "effective", "substitute"}) {
    mode = m;
    auto module = EvaluatorModule::provider(fixture, sha, "test");
    rejects([&] { module.create("{}"); });
    test(live == 0, "failed creation cleanup");
  }
  mode = "";
  auto module = EvaluatorModule::provider(fixture, sha, "test");
  auto core = create_chess_core();
  auto p = evaluation_snapshot(*core->snapshot(*core->initial_position()));
  for (auto m : {"unknown", "negative_status", "error", "bad_diagnostic",
                 "utf8_diagnostic", "success_diagnostic", "success_unterminated",
                 "success_invalid_utf8", "nan", "inf", "-inf"}) {
    mode = m;
    {
      auto instance = module.create("{}");
      calls = 0;
      rejects([&] { instance->evaluate(p); });
      test(!instance->usable(), "failure quarantine");
      rejects([&] { instance->evaluate(p); });
      test(calls == 1, "quarantined provider not called");
    }
    test(live == 0, "failed evaluation cleanup");
  }
  mode = "error";
  auto instance = module.create("{}");
  try {
    instance->evaluate(p);
  } catch (const EvaluationError &e) {
    test(std::string(e.what()).find("fixture failure") != std::string::npos,
         "diagnostic propagated");
  }
  instance.reset();
  mode = "schema";
  for (
      auto s :
      {R"({"schema_version":1,"type":"object","properties":{"x":{"type":"object","default":{}}},"additionalProperties":false})",
       R"({"schema_version":1,"type":"object","properties":{"x":{"type":"number"}},"additionalProperties":false})",
       R"({"schema_version":1,"type":"object","properties":{},"additionalProperties":false,"$ref":"x"})",
       R"({"schema_version":1,"type":"object","properties":{"x":{"type":"number","default":1,"minimum":2}},"additionalProperties":false})"}) {
    custom_json = s;
    rejects([&] { EvaluatorModule::provider(fixture, sha, "test"); });
  }
  mode = "";
  test(live == 0, "all fixture instances destroyed");
  std::cout << "hostile provider negotiation, JSON, creation, finite score and "
               "quarantine checks passed\n";
}
void json_tests() {
  // RFC 8785 Appendix B: exact binary64 inputs, independent decimal oracle.
  const std::pair<uint64_t, const char *> numeric_vectors[] = {
      {0x0000000000000001ULL, "5e-324"},
      {0x8000000000000001ULL, "-5e-324"},
      {0x7fefffffffffffffULL, "1.7976931348623157e+308"},
      {0x4340000000000000ULL, "9007199254740992"},
      {0x4430000000000000ULL, "295147905179352830000"},
      {0x44b52d02c7e14af5ULL, "9.999999999999997e+22"},
      {0x44b52d02c7e14af6ULL, "1e+23"},
      {0x44b52d02c7e14af7ULL, "1.0000000000000001e+23"},
      {0x444b1ae4d6e2ef4eULL, "999999999999999700000"},
      {0x444b1ae4d6e2ef4fULL, "999999999999999900000"},
      {0x444b1ae4d6e2ef50ULL, "1e+21"},
      {0x3eb0c6f7a0b5ed8cULL, "9.999999999999997e-7"},
      {0x3eb0c6f7a0b5ed8dULL, "0.000001"},
      {0x41b3de4355555553ULL, "333333333.3333332"},
      {0x41b3de4355555554ULL, "333333333.33333325"},
      {0x41b3de4355555555ULL, "333333333.3333333"},
      {0x41b3de4355555556ULL, "333333333.3333334"},
      {0x41b3de4355555557ULL, "333333333.33333343"},
      {0xbecbf647612f3696ULL, "-0.0000033333333333333333"},
      {0x43143ff3c1cb0959ULL, "1424953923781206.2"}};
  for (auto [bits, expected] : numeric_vectors)
    test(number(std::bit_cast<double>(bits)) == expected, "RFC8785 Appendix B");
  test(number(1000000000000000128.0) == "1000000000000000100",
       "JCS significant digits before fixed formatting");
  rejects([] { parse(std::string("{}\0bad", 6)); });
  rejects([] { parse(std::string(1048577, ' ')); });
  rejects([] { parse(std::string(66, '[') + "0" + std::string(66, ']')); });

  test(canonical(
           parse(R"({"z":-0.0,"a":1e-6,"b":1e21,"c":333333333.33333329})")) ==
           R"({"a":0.000001,"b":1e+21,"c":333333333.3333333,"z":0})",
       "JCS numeric vector");
  test(
      canonical(parse(R"({"\ue000":1,"\ud83d\ude00":2,"a":"\u000f\n\\\""})")) ==
          std::string("{\"a\":\"\\u000f\\n\\\\\\\"\",\"😀\":2,\"\":1}"),
      "JCS UTF-16 key order and escaping");
  for (auto s :
       {"{\"a\":1,\"a\":2}", "{\"x\":NaN}", "{\"x\":1e400}", "\"\\ud800\""})
    rejects([&] { parse(s); });
  auto s = parse(
      R"({"schema_version":1,"type":"object","properties":{"n":{"type":"integer","minimum":0,"maximum":10},"b":{"type":"boolean","default":false},"s":{"type":"string","default":"😀","minLength":1,"maxLength":1,"enum":["😀","a"]}},"required":["n"],"additionalProperties":false})");
  schema(s);
  test(resolve(s, parse("{\"n\":1}"))["s"] == "😀", "Unicode scalar length");
  for (auto config : {"{}", "{\"n\":1.5}", "{\"n\":11}", "{\"n\":1,\"b\":0}",
                      "{\"n\":1,\"s\":\"aa\"}", "{\"n\":1,\"s\":\"b\"}"})
    rejects([&] { resolve(s, parse(config)); });

  auto base = parse(
      R"({"schema_version":1,"type":"object","properties":{"x":{"type":"integer","default":1}},"additionalProperties":false})");
  for (auto property :
       {R"({"type":"integer","default":9007199254740992})",
        R"({"type":"number","default":1,"minimum":3,"maximum":2})",
        R"({"type":"number","default":1,"enum":[1,1]})",
        R"({"type":"boolean","default":false,"minimum":0})",
        R"({"type":"string","default":"x","minLength":2,"maxLength":1})",
        R"({"type":"number","default":1,"enum":["1"]})",
        R"({"type":"number","default":1,"unknown":true})"}) {
    auto bad = base;
    bad["properties"]["x"] = parse(property);
    rejects([&] { schema(bad); });
  }
  auto bad = base;
  bad["required"] = Json::array({"missing"});
  rejects([&] { schema(bad); });
  bad = base;
  bad["required"] = Json::array({"x", "x"});
  rejects([&] { schema(bad); });
  auto a = EvaluatorModule::bundled("material-v1", sha).create("{}");
  auto changed = EvaluatorModule::bundled("material-v1", std::string(64, 'b'))
                     .create("{}");
  test(a->identity().fingerprint != changed->identity().fingerprint,
       "artifact change identity");
  std::ofstream("m2_hash_vector.tmp", std::ios::binary) << "abc";
  test(evaluator_artifact_sha256("m2_hash_vector.tmp") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
       "SHA-256 published abc vector");
  std::remove("m2_hash_vector.tmp");
  rejects([&] { EvaluatorModule::bundled("missing", sha); });
  rejects([&] { EvaluatorModule::bundled("material-v1", "bad"); });
  std::cout
      << "restricted schema, JCS and artifact fingerprint checks passed\n";
}
int main(int argc, char **argv) {
  try {

#ifdef __EMSCRIPTEN__
    test(argc == 2, "encompassing WASM digest required");
    sha = argv[1];
#endif
    source_validation();
    raw_contract();
    json_tests();
    hostile();
    auto module = EvaluatorModule::bundled("material-v1", sha);
    golden(module);
    // Owning instance survives destruction of module facade.
    std::unique_ptr<Evaluator> survivor;
    {
      auto temp = EvaluatorModule::provider(fixture, sha, "lifetime");
      survivor = temp.create("{}");
    }
    survivor.reset();
    test(live == 0, "module lifetime guard");
#ifndef __EMSCRIPTEN__
    test(argc == 4, "native module fixture paths required");
    auto native = EvaluatorModule::native(argv[1]);
    golden(native);
    rejects([&] { EvaluatorModule::native("/missing/evaluator.so"); });
    rejects([&] { EvaluatorModule::native(argv[2]); });
    rejects([&] { EvaluatorModule::native(argv[3]); });
    {
      auto temp = EvaluatorModule::native(argv[1]);
      survivor = temp.create("{}");
    }
    auto core = create_chess_core();
    test(survivor->evaluate(evaluation_snapshot(
             *core->snapshot(*core->initial_position()))) == 0,
         "native module stays loaded with live instance");
    survivor.reset();
    test(native.create("{}")->identity().artifact_sha256 ==
             evaluator_artifact_sha256(argv[1]),
         "loaded artifact digest");
#else
    (void)argc;
    (void)argv;
    rejects([&] { EvaluatorModule::native("external.so"); });
#endif
    std::cout << "M2 all correctness/error checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
