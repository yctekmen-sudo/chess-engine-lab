#include "material.hpp"
#include "json.hpp"
#include "snapshot.hpp"
#include <array>
#include <cstring>
#include <new>
using namespace chess_lab::eval_detail;
namespace {
constexpr char metadata[] =
    R"({"module_id":"org.chesslab.material","display_name":"Material Evaluator","module_version":"1.0.0","description":"Material balance from the current board","parameter_schema_version":1,"required_snapshot_major":1,"minimum_snapshot_minor":0,"score_units":"arbitrary"})";
constexpr char parameter_schema[] =
    R"({"schema_version":1,"type":"object","properties":{"pawn_value":{"type":"number","default":1,"minimum":0,"maximum":1e100},"knight_value":{"type":"number","default":3,"minimum":0,"maximum":1e100},"bishop_value":{"type":"number","default":3.25,"minimum":0,"maximum":1e100},"rook_value":{"type":"number","default":5,"minimum":0,"maximum":1e100},"queen_value":{"type":"number","default":9,"minimum":0,"maximum":1e100}},"additionalProperties":false})";
struct Instance {
  std::array<double, 5> weights;
  std::string effective;
};
ChessEvalStatus result(ChessEvalStatus s, const char *message, char *e,
                       uint64_t n) noexcept {
  if (n && !e)
    return CHESS_EVAL_INVALID_ARGUMENT;
  if (n) {
    size_t len = std::min<uint64_t>(std::strlen(message), n - 1);
    std::memcpy(e, message, len);
    e[len] = 0;
  }
  return s;
}
ChessEvalStatus json(const std::string_view s, char *o, uint64_t n,
                     uint64_t *needed, char *e, uint64_t en) noexcept {
  if (!needed || (!o && n) || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid JSON buffer", e, en);
  *needed = s.size() + 1;
  if (!o && !n)
    return result(0, "", e, en);
  if (n < *needed)
    return result(CHESS_EVAL_BUFFER_TOO_SMALL, "JSON buffer too small", e, en);
  std::memcpy(o, s.data(), s.size());
  o[s.size()] = 0;
  return result(0, "", e, en);
}
ChessEvalStatus info(char *o, uint64_t n, uint64_t *r, char *e, uint64_t en) {
  return json(metadata, o, n, r, e, en);
}
ChessEvalStatus schema_json(char *o, uint64_t n, uint64_t *r, char *e,
                            uint64_t en) {
  return json(parameter_schema, o, n, r, e, en);
}
ChessEvalStatus create(const char *c, uint64_t n, ChessEvalHandle *out, char *e,
                       uint64_t en) noexcept {
  if (out)
    *out = nullptr;
  if (!out || !c || !n || n > 1048576 || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid creation arguments", e,
                  en);
  try {
    auto s = parse(parameter_schema);
    schema(s);
    auto config = resolve(s, parse(std::string_view(c, n)));
    auto instance = std::make_unique<Instance>();
    const char *names[] = {"pawn_value", "knight_value", "bishop_value",
                           "rook_value", "queen_value"};
    for (size_t i = 0; i < 5; ++i)
      instance->weights[i] = config[names[i]].get<double>();
    instance->effective = canonical(config);
    *out = instance.release();
    return result(0, "", e, en);
  } catch (const std::bad_alloc &) {
    return result(CHESS_EVAL_OUT_OF_MEMORY, "allocation failed", e, en);
  } catch (...) {
    return result(CHESS_EVAL_INVALID_CONFIGURATION,
                  "invalid material configuration", e, en);
  }
}
ChessEvalStatus config(ChessEvalHandle h, char *o, uint64_t n, uint64_t *r,
                       char *e, uint64_t en) noexcept {
  if (!h)
    return result(CHESS_EVAL_INVALID_ARGUMENT, "null instance", e, en);
  return json(static_cast<Instance *>(h)->effective, o, n, r, e, en);
}
ChessEvalStatus evaluate(ChessEvalHandle h, const ChessEvalPosition *p,
                         double *score, char *e, uint64_t en) noexcept {
  if (!h || !p || !score || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid evaluation arguments",
                  e, en);
  auto status = validate(*p);
  if (status)
    return result(status, "invalid snapshot", e, en);
  double v = 0;
  auto &weights = static_cast<Instance *>(h)->weights;
  for (size_t i = 0; i < 5; ++i)
    v += weights[i] *
         (std::popcount(p->bitboards[i]) - std::popcount(p->bitboards[i + 6]));
  if (!std::isfinite(v))
    return result(CHESS_EVAL_INTERNAL_ERROR, "nonfinite material result", e,
                  en);
  *score = v;
  return result(0, "", e, en);
}
void destroy(ChessEvalHandle h) noexcept { delete static_cast<Instance *>(h); }
} // namespace
extern "C" ChessEvalStatus CHESS_EVAL_CALL
chess_lab_material_provider(uint64_t major, uint64_t minor, void *out,
                            uint64_t n, char *e, uint64_t en) {
  (void)minor;
  if (!out || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid API buffer", e, en);
  if (major != 1)
    return result(CHESS_EVAL_INCOMPATIBLE_ABI, "unsupported ABI major", e, en);
  if (n < sizeof(ChessEvalApi))
    return result(CHESS_EVAL_BUFFER_TOO_SMALL, "API buffer too small", e, en);
  ChessEvalApi api{};
  api.struct_size = sizeof api;
  api.abi_major = 1;
  api.get_module_info_json = info;
  api.get_parameter_schema_json = schema_json;
  api.create_evaluator = create;
  api.get_effective_configuration_json = config;
  api.evaluate = evaluate;
  api.destroy_evaluator = destroy;
  std::memcpy(out, &api, sizeof api);
  return result(0, "", e, en);
}
#ifdef CHESS_LAB_MATERIAL_NATIVE
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL
chess_evaluator_get_api(uint64_t a, uint64_t b, void *o, uint64_t n, char *e,
                        uint64_t en) {
  return chess_lab_material_provider(a, b, o, n, e, en);
}
#endif
