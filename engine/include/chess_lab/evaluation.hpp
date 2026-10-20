#pragma once
#include <chess_lab/chess_eval_abi_v1.h>
#include <chess_lab/position_snapshot.hpp>
#include <memory>
#include <stdexcept>
#include <string>
namespace chess_lab {
// Conversion validates all source discriminants and the resulting ABI fields.
ChessEvalPosition evaluation_snapshot(const PositionSnapshot &source);
class EvaluationError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};
struct EvaluationIdentity {
  std::string module_id, module_version, canonical_configuration,
      artifact_sha256;
  std::string provider, build;
  uint64_t abi_major = 1, abi_minor = 0, snapshot_major = 1, snapshot_minor = 0;
  std::string fingerprint;
};
class Evaluator;
class EvaluatorModule {
public:
  static EvaluatorModule native(const std::string &path);
  // WASM callers provide the encompassing .wasm artifact digest from packaging.
  static EvaluatorModule bundled(const std::string &provider,
                                 const std::string &wasm_sha256);
  // Also permits independently linked providers and controlled contract
  // fixtures.
  static EvaluatorModule provider(ChessEvalGetApiFn entry,
                                  const std::string &artifact_sha256,
                                  const std::string &provider_id);
  std::unique_ptr<Evaluator> create(const std::string &json) const;

private:
  struct State;
  std::shared_ptr<State> state_;
  explicit EvaluatorModule(std::shared_ptr<State> state)
      : state_(std::move(state)) {}
  friend class Evaluator;
};
class Evaluator {
public:
  ~Evaluator();
  Evaluator(const Evaluator &) = delete;
  Evaluator &operator=(const Evaluator &) = delete;
  double evaluate(const ChessEvalPosition &position);
  const EvaluationIdentity &identity() const noexcept;
  bool usable() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit Evaluator(std::unique_ptr<Impl> impl);
  friend class EvaluatorModule;
};
// The owner serializes replacement with searches. Successful replacements emit
// a generation signal for downstream cache owners; failed replacements preserve
// it.
class EvaluationSelection {
public:
  void replace(const EvaluatorModule &module, const std::string &json);
  Evaluator &active();
  uint64_t generation() const noexcept { return generation_; }

private:
  std::unique_ptr<Evaluator> active_;
  uint64_t generation_ = 0;
};
std::string evaluator_artifact_sha256(const std::string &path);
} // namespace chess_lab
