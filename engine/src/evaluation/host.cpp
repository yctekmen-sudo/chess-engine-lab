#include "json.hpp"
#include "material.hpp"
#include "snapshot.hpp"
#include <array>
#include <chess_lab/evaluation.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <picosha2/picosha2.h>
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif
namespace chess_lab {
using namespace eval_detail;
namespace {
std::string digest(const std::string &bytes) {
  return picosha2::hash256_hex_string(bytes);
}
void check(ChessEvalStatus s, const std::array<char, 4096> &e) {
  // An empty diagnostic is already terminated and valid UTF-8. Only the known
  // success status may bypass the full diagnostic validation below.
  if (s == CHESS_EVAL_OK && e.front() == '\0')
    return;
  require(std::memchr(e.data(), 0, e.size()), "unterminated diagnostic");
  // JSON escaping validates the entire UTF-8 diagnostic without interpreting
  // it.
  auto text = std::string(e.data());
  (void)Json(text).dump();
  if (s < 0 || s > 8)
    throw EvaluationError("unknown evaluator status");
  if (s != 0)
    throw EvaluationError("evaluator status " + std::to_string(s) + ": " +
                          text);
  require(text.empty(), "diagnostic must be empty on success");
}
template <class Call> Json fetch(Call call, uint64_t limit=1048576) {
  std::array<char, 4096> error{};
  uint64_t size = 0;
  auto status = call(nullptr, 0, &size, error.data(), error.size());
  check(status, error);
  require(size > 1 && size <= limit, "JSON output exceeds limit");
  std::vector<char> buffer(size, '\x7f');
  uint64_t again = 0;
  error.fill(0);
  status = call(buffer.data(), size, &again, error.data(), error.size());
  check(status, error);
  require(again == size && buffer.back() == 0 &&
              std::strlen(buffer.data()) == size - 1,
          "inconsistent JSON output length");
  return parse(std::string_view(buffer.data(), size - 1));
}
void digest_valid(const std::string &d) {
  require(d.size() == 64 && d.find_first_not_of("0123456789abcdef") == d.npos,
          "artifact SHA-256 required");
}
} // namespace
struct EvaluatorModule::State {
  ChessEvalApi api{};
  ChessEvalGetConfigFn calibration=nullptr;
  Json metadata, parameters;
  std::string artifact, provider;
#ifndef __EMSCRIPTEN__
#ifdef _WIN32
  HMODULE library = nullptr;
  ~State() {
    if (library)
      FreeLibrary(library);
  }
#else
  void *library = nullptr;
  ~State() {
    if (library)
      dlclose(library);
  }
#endif
#endif
  void negotiate(ChessEvalGetApiFn entry) {
    require(entry, "missing evaluator entry point");
    std::array<char, 4096> error{};
    ChessEvalApiV1_1 extended{};
    auto status=entry(1,1,&extended,sizeof extended,error.data(),error.size());
    if(status==CHESS_EVAL_INCOMPATIBLE_ABI){error.fill(0);check(entry(1,0,&api,sizeof api,error.data(),error.size()),error);}
    else {check(status,error);api=extended.v1;}
    require(api.abi_major==1 && ((api.struct_size==72 && api.abi_minor==0)||(api.struct_size==80 && api.abi_minor==1)),"inconsistent ABI table");
    if(api.struct_size==80)calibration=extended.get_calibration_json;
    require(api.get_module_info_json && api.get_parameter_schema_json &&
                api.create_evaluator && api.get_effective_configuration_json &&
                api.evaluate && api.destroy_evaluator,
            "null API function");
    metadata = fetch(api.get_module_info_json);
    require(metadata.is_object(), "metadata object");
    for (auto key : {"module_id", "display_name", "module_version",
                     "description", "score_units"})
      require(metadata.at(key).is_string() &&
                  !metadata.at(key).get<std::string>().empty(),
              "invalid metadata string");
    for (auto key : {"parameter_schema_version", "required_snapshot_major",
                     "minimum_snapshot_minor"})
      require(metadata.at(key).is_number_integer() &&
                  metadata.at(key).get<double>() >= 0,
              "invalid metadata version");
    require(metadata["parameter_schema_version"] == 1,
            "unsupported schema version");
    require(metadata["required_snapshot_major"] == 1 &&
                metadata["minimum_snapshot_minor"] == 0,
            "incompatible snapshot requirements");
    parameters = fetch(api.get_parameter_schema_json);
    schema(parameters);
  }
};
struct Evaluator::Impl {
  std::shared_ptr<EvaluatorModule::State> module;
  ChessEvalHandle handle = nullptr;
  EvaluationIdentity id;
  mutable std::mutex mutex;
  bool usable = true;
  std::string calibration,calibration_hash,calibration_error="raw_only: optional capability absent";
  Json descriptor;
  ~Impl() {
    if (handle)
      module->api.destroy_evaluator(handle);
  }
};
std::string evaluator_artifact_sha256(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  require(bool(stream), "cannot read evaluator artifact");
  picosha2::hash256_one_by_one hash;
  // Keep bulk I/O off the stack: wasm32's default stack is only 64 KiB.
  std::vector<char> data(65536);
  while (stream) {
    stream.read(data.data(), data.size());
    hash.process(data.begin(), data.begin() + stream.gcount());
  }
  require(stream.eof(), "artifact read error");
  hash.finish();
  return picosha2::get_hash_hex_string(hash);
}
EvaluatorModule EvaluatorModule::provider(ChessEvalGetApiFn entry,
                                          const std::string &artifact,
                                          const std::string &name) {
  digest_valid(artifact);
  require(!name.empty(), "provider identity required");
  auto s = std::make_shared<State>();
  s->artifact = artifact;
  s->provider = name;
  s->negotiate(entry);
  return EvaluatorModule(s);
}
EvaluatorModule EvaluatorModule::bundled(const std::string &name,
                                         const std::string &wasm) {
  require(name == "material-v1", "unknown bundled evaluator provider");
  return provider(chess_lab_material_provider, wasm, "bundled:material-v1");
}
EvaluatorModule EvaluatorModule::native(const std::string &path) {
#ifdef __EMSCRIPTEN__
  (void)path;
  throw EvaluationError("native evaluator loading unavailable in WebAssembly");
#else
  auto s = std::make_shared<State>();
  // Use the identical resolved file for hashing and loading, even if the
  // caller supplies a basename that dlopen would otherwise search elsewhere.
  const auto artifact_path = std::filesystem::canonical(path).string();
  s->artifact = evaluator_artifact_sha256(artifact_path);
  s->provider = "native";
#ifdef _WIN32
  s->library = LoadLibraryA(artifact_path.c_str());
  require(s->library, "cannot load evaluator library");
  auto entry = reinterpret_cast<ChessEvalGetApiFn>(
      GetProcAddress(s->library, "chess_evaluator_get_api"));
#else
  s->library = dlopen(artifact_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  require(s->library, "cannot load evaluator library");
  auto entry = reinterpret_cast<ChessEvalGetApiFn>(
      dlsym(s->library, "chess_evaluator_get_api"));
#endif
  s->negotiate(entry);
  return EvaluatorModule(s);
#endif
}
std::unique_ptr<Evaluator>
EvaluatorModule::create(const std::string &json) const {
  auto expected = resolve(state_->parameters, parse(json));
  auto p = std::make_unique<Evaluator::Impl>();
  p->module = state_;
  std::array<char, 4096> e{};
  auto status = state_->api.create_evaluator(json.data(), json.size(),
                                             &p->handle, e.data(), e.size());
  // Even a contract-violating provider that returns failure with a live handle
  // has that handle destroyed by Impl's guard before releasing the module.
  check(status, e);
  require(p->handle, "successful creation returned null handle");
  auto effective =
      fetch([&](char *o, uint64_t n, uint64_t *r, char *err, uint64_t en) {
        return state_->api.get_effective_configuration_json(p->handle, o, n, r,
                                                            err, en);
      });
  auto resolved = resolve(state_->parameters, effective);
  require(effective.size() == state_->parameters["properties"].size() &&
              canonical(effective) == canonical(resolved) &&
              canonical(effective) == canonical(expected),
          "effective configuration disagrees with requested values/defaults");
  if(state_->calibration)try {
    auto descriptor=fetch([&](char*o,uint64_t n,uint64_t*r,char*e,uint64_t en){return state_->calibration(p->handle,o,n,r,e,en);},65536);
    require(descriptor.is_object() && descriptor.at("schema_version")==1 && descriptor.at("target")=="centipawns","unknown calibration schema/target");
    const auto kind=descriptor.at("kind");
    if(kind=="linear"){
      require(descriptor.size()==4 && descriptor.at("scale").is_number(),"invalid linear descriptor");
      const auto scale=descriptor.at("scale").get<double>();require(std::isfinite(scale)&&scale>0,"scale must be finite positive");
    }else if(kind=="lookup_table"){
      require(descriptor.size()==6 && descriptor.at("interpolation")=="linear" && descriptor.at("out_of_range")=="raw_only","invalid table policy");
      const auto &points=descriptor.at("points");require(points.is_array()&&points.size()>=2&&points.size()<=1024,"invalid point count");
      double previous_raw=-INFINITY,previous_cp=-INFINITY;bool zero=false;
      for(const auto &point:points){require(point.is_array()&&point.size()==2&&point[0].is_number()&&point[1].is_number(),"invalid point");
        const auto x=point[0].get<double>(),y=point[1].get<double>();require(std::isfinite(x)&&std::isfinite(y)&&x>previous_raw&&y>previous_cp,"nonfinite or nonmonotonic table");
        require((x==0&&y==0)||(x<0&&y<0)||(x>0&&y>0),"sign/zero mismatch");zero|=x==0&&y==0;previous_raw=x;previous_cp=y;
      }require(zero,"missing zero anchor");
    }else throw EvaluationError("unknown calibration kind");
    p->descriptor=descriptor;p->calibration=canonical(descriptor);p->calibration_hash=digest(p->calibration);p->calibration_error.clear();
  }catch(const std::exception &e){p->calibration_error=std::string("raw_only: ")+e.what();}
  auto &id = p->id;
  id.abi_minor=state_->api.abi_minor;
  id.module_id = state_->metadata["module_id"];
  id.module_version = state_->metadata["module_version"];
  id.artifact_sha256 = state_->artifact;
  id.provider = state_->provider;
  id.canonical_configuration = canonical(effective);
#ifdef __EMSCRIPTEN__
  id.build = "wasm32;";
#else
  id.build = "native;pointer-bits=" + std::to_string(sizeof(void *) * 8) + ";";
#endif
#ifdef _MSC_VER
  id.build += "compiler=msvc-" + std::to_string(_MSC_FULL_VER);
#else
  id.build += std::string("compiler=") + __VERSION__;
#endif
#ifdef __linux__
  id.build += ";os=linux";
#elif defined(_WIN32)
  id.build += ";os=windows";
#elif defined(__APPLE__)
  id.build += ";os=apple";
#endif
#if defined(__x86_64__) || defined(_M_X64)
  id.build += ";arch=x86_64";
#elif defined(__aarch64__)
  id.build += ";arch=aarch64";
#endif
  id.build += ";engine=" CHESS_LAB_VERSION ";mode=" CHESS_LAB_BUILD_TYPE;
  Json identity = {{"module_id", id.module_id},
                   {"module_version", id.module_version},
                   {"effective", effective},
                   {"artifact_sha256", id.artifact_sha256},
                   {"provider", id.provider},
                   {"build", id.build},
                   {"abi_major", 1},
                   {"abi_minor", id.abi_minor},
                   {"snapshot_major", 1},
                   {"snapshot_minor", 0},
                   {"parameter_schema_version", 1}};
  id.fingerprint = digest(canonical(identity));
  return std::unique_ptr<Evaluator>(new Evaluator(std::move(p)));
}
Evaluator::Evaluator(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
Evaluator::~Evaluator() = default;
const EvaluationIdentity &Evaluator::identity() const noexcept {
  return impl_->id;
}
const std::string &Evaluator::calibration_json() const noexcept{return impl_->calibration;}
const std::string &Evaluator::calibration_digest() const noexcept{return impl_->calibration_hash;}
const std::string &Evaluator::calibration_diagnostic() const noexcept{return impl_->calibration_error;}
std::optional<int> Evaluator::centipawns(double raw) const noexcept {
  if(impl_->calibration.empty()||!std::isfinite(raw))return {};
  try {
    const auto &d=impl_->descriptor;long double cp;
    if(d.at("kind")=="linear")cp=static_cast<long double>(raw)*d.at("scale").get<double>();
    else {
      const auto &p=d.at("points");if(raw<p.front()[0].get<double>()||raw>p.back()[0].get<double>())return {};
      std::size_t i=1;while(i+1<p.size()&&raw>p[i][0].get<double>())++i;
      long double x0=p[i-1][0].get<double>(),x1=p[i][0].get<double>(),y0=p[i-1][1].get<double>(),y1=p[i][1].get<double>();
      // Interpolate from the nearer anchor to avoid cancellation near zero.
      const long double left=static_cast<long double>(raw)-x0,right=x1-static_cast<long double>(raw);
      cp=left<=right?y0+left/(x1-x0)*(y1-y0):y1-right/(x1-x0)*(y1-y0);
    }
    if(!std::isfinite(cp)||cp<-2147483647.L||cp>2147483647.L)return {};
    auto rounded=std::round(cp);if(rounded<-2147483647.L||rounded>2147483647.L)return {};return static_cast<int>(rounded);
  }catch(...){return {};}
}
bool Evaluator::usable() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->usable;
}
double Evaluator::evaluate(const ChessEvalPosition &p) {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->usable)
    throw EvaluationError("evaluator quarantined until replacement");
  try {
    require(validate(p) == 0, "invalid host snapshot");
    double score = std::numeric_limits<double>::quiet_NaN();
    std::array<char, 4096> e{};
    auto status = impl_->module->api.evaluate(impl_->handle, &p, &score,
                                              e.data(), e.size());
    check(status, e);
    require(std::isfinite(score), "nonfinite evaluator score");
    return score;
  } catch (const std::exception &e) {
    impl_->usable = false;
    throw EvaluationError(e.what());
  }
}
void EvaluationSelection::replace(const EvaluatorModule &module,
                                  const std::string &json) {
  auto next = module.create(json);
  active_ = std::move(next);
  ++generation_;
}
Evaluator &EvaluationSelection::active() {
  if (!active_)
    throw EvaluationError("no evaluator selected");
  return *active_;
}
} // namespace chess_lab
