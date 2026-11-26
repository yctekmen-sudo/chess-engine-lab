#include <chess_lab/chess_eval_abi_v2.h>
#include <algorithm>
#include "json.hpp"
#include "snapshot.hpp"
#include <array>
#include <cstring>
#include <new>
using namespace chess_lab::eval_detail;
namespace {
#include "material_pst_tables.hpp"
#if E1_MATERIAL
constexpr char metadata[] = R"({"module_id":"org.chesslab.experimental.e1.materialpst","display_name":"E1-C Material and PST","module_version":"1.0.0","description":"Experimental material and piece square tables","parameter_schema_version":1,"required_snapshot_major":1,"minimum_snapshot_minor":0,"score_units":"centipawns"})";
constexpr int material[5]={100,300,325,500,900};
#else
constexpr char metadata[] = R"({"module_id":"org.chesslab.experimental.e1.pst","display_name":"E1-B PST only","module_version":"1.0.0","description":"Experimental piece square tables only","parameter_schema_version":1,"required_snapshot_major":1,"minimum_snapshot_minor":0,"score_units":"centipawns"})";
constexpr int material[5]={0,0,0,0,0};
#endif
constexpr char parameter_schema[] = R"({"schema_version":1,"type":"object","properties":{},"additionalProperties":false})";
struct Instance { std::string effective; };
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
    instance->effective = canonical(config);
    *out = instance.release();
    return result(0, "", e, en);
  } catch (const std::bad_alloc &) {
    return result(CHESS_EVAL_OUT_OF_MEMORY, "allocation failed", e, en);
  } catch (...) {
    return result(CHESS_EVAL_INVALID_CONFIGURATION,
                  "invalid E1 configuration", e, en);
  }
}
ChessEvalStatus config(ChessEvalHandle h, char *o, uint64_t n, uint64_t *r,
                       char *e, uint64_t en) noexcept {
  if (!h)
    return result(CHESS_EVAL_INVALID_ARGUMENT, "null instance", e, en);
  return json(static_cast<Instance *>(h)->effective, o, n, r, e, en);
}
ChessEvalStatus evaluate(ChessEvalHandle h, const ChessEvalPosition *p,
                         ChessEvalCentipawns *score, char *e, uint64_t en) noexcept {
  if (!h || !p || !score || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid evaluation arguments",
                  e, en);
  auto status = validate(*p);
  if (status)
    return result(status, "invalid snapshot", e, en);
  // Accumulate directly from the read-only ABI bitboards. No move generation,
  // board copies, dynamic allocation or side-to-move dependence.
  int v=0, phase=0;
  constexpr int phase_weight[5]={0,1,1,2,4};
  for (int i=0;i<5;++i) {
    auto w=p->bitboards[i], b=p->bitboards[i+6];
    phase += phase_weight[i]*(std::popcount(w)+std::popcount(b));
    while(w) {const auto sq=std::countr_zero(w); w &= w-1; v += material[i]+pst[i][sq^56];}
    while(b) {const auto sq=std::countr_zero(b); b &= b-1; v -= material[i]+pst[i][sq];}
  }
  phase=std::min(24,phase);
  const auto wk=std::countr_zero(p->bitboards[5])^56;
  const auto bk=std::countr_zero(p->bitboards[11]);
  // Taper only the king table; use the signed difference before division
  // to ensure exact color-reflection antisymmetry under integer rounding.
  v += (phase*(pst[5][wk]-pst[5][bk])+(24-phase)*(pst[6][wk]-pst[6][bk]))/24;
  if (v < CHESS_EVAL_CP_MIN || v > CHESS_EVAL_CP_MAX)
    return result(CHESS_EVAL_INTERNAL_ERROR, "E1 result outside centipawn range", e,
                  en);
  *score = v;
  return result(0, "", e, en);
}
void destroy(ChessEvalHandle h) noexcept { delete static_cast<Instance *>(h); }
} // namespace
extern "C" ChessEvalStatus CHESS_EVAL_CALL
chess_lab_e1_provider(uint64_t major, uint64_t minor, void *out,
                            uint64_t n, char *e, uint64_t en) {
  (void)minor;
  if (!out || (!e && en))
    return result(CHESS_EVAL_INVALID_ARGUMENT, "invalid API buffer", e, en);
  if (major != CHESS_EVAL_ABI_MAJOR)
    return result(CHESS_EVAL_INCOMPATIBLE_ABI, "unsupported ABI major", e, en);
  if (n < sizeof(ChessEvalApi))
    return result(CHESS_EVAL_BUFFER_TOO_SMALL, "API buffer too small", e, en);
  ChessEvalApi api{};
  api.struct_size = sizeof api;
  api.abi_major = CHESS_EVAL_ABI_MAJOR;
  api.get_module_info_json = info;
  api.get_parameter_schema_json = schema_json;
  api.create_evaluator = create;
  api.get_effective_configuration_json = config;
  api.evaluate = evaluate;
  api.destroy_evaluator = destroy;
  std::memcpy(out, &api, sizeof api);
  return result(0, "", e, en);
}
#if !defined(CHESS_LAB_E1_BUNDLED)
extern "C" CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL
chess_evaluator_get_api(uint64_t a, uint64_t b, void *o, uint64_t n, char *e,
                        uint64_t en) {
  return chess_lab_e1_provider(a, b, o, n, e, en);
}
#endif
