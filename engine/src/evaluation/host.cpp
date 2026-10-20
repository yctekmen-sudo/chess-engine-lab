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
template <class Call> Json fetch(Call call) {
  std::array<char, 4096> error{};
  uint64_t size = 0;
  auto status = call(nullptr, 0, &size, error.data(), error.size());
  check(status, error);
  require(size > 1 && size <= 1048576, "JSON output exceeds limit");
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
    check(entry(1, 0, &api, sizeof api, error.data(), error.size()), error);
    require(api.struct_size == 72 && api.abi_major == 1 && api.abi_minor == 0,
            "inconsistent ABI table");
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
  auto &id = p->id;
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
                   {"abi_minor", 0},
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
