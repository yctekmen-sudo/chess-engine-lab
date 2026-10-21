# M7 optional evaluator calibration

This documents the compatible extension implemented under D127–D128. The frozen
[evaluator ABI v1.0](evaluation-abi-v1.0.md) prefix and raw evaluation contract are
unchanged. No calibration is inferred from material values or module names.

## Negotiation and layout

`ChessEvalApi` stays 72 bytes. `ChessEvalApiV1_1` contains that prefix at offset 0
and an align-8 `ChessEvalGetConfigFn get_calibration_json` slot at offset 72; its
size is 80 bytes on native and wasm32. The function receives an evaluator instance
handle and uses the existing caller-owned JSON size-query/buffer convention.

The host requests major 1/minor 1 into a zero-initialized 80-byte table. It accepts
a legacy minor-0/72-byte response without touching the tail. A provider returning
`CHESS_EVAL_INCOMPATIBLE_ABI` is retried with minor 0 and exactly 72 bytes. A minor-1
provider must answer a minor-0/72-byte request with the original prefix only.
Inconsistent size/version pairs fail API negotiation. An absent optional slot
means raw-only. Legacy C/C++ layout tests remain unchanged; M7 tests assert both
layouts, an old-caller canary and a frozen-header native provider.

## Instance JSON descriptors

Descriptor JSON is limited to 64 KiB. Only these schema-1 shapes are accepted;
unknown fields, kinds, policies or invalid optional metadata disable cp display.

```json
{"schema_version":1,"target":"centipawns","kind":"linear","scale":100}
```

Scale must be finite and positive. It multiplies raw White-positive evaluation.

```json
{"schema_version":1,"target":"centipawns","kind":"lookup_table","interpolation":"linear","out_of_range":"raw_only","points":[[-3,-600],[-1,-100],[0,0],[1,100],[3,600]]}
```

Tables have 2–1024 finite `[raw, cp]` points, strictly increasing in both
coordinates, with an explicit `[0,0]` anchor and matching signs. Interpolate
linearly between adjacent anchors; outside the table domain retain raw-only.
There is no extrapolation or clamp. Mapping checks arithmetic finiteness and
signed UCI range ±2147483647, rounds half away from zero and checks again.

The host fetches metadata after creating/configuring an instance and validates
it separately from required evaluation functions. Canonical JSON and SHA-256
are exposed with evaluator artifact/effective-configuration identity. Malformed,
unsupported or failed optional metadata produces a raw-only diagnostic while a
usable raw evaluator stays usable. Different descriptors do not alter raw numeric
fingerprints, search inputs or TT certification; minor-0 identities retain their
original fingerprint construction.

## Presentation and provenance

The native UCI adapter retains `info string chesslab score raw white VALUE` and
emits standard `score cp N` only when conversion succeeds. Convert the raw White
score, round, then reverse polarity at Black-to-move roots. Mate remains its own
UCI score kind and bypasses calibration. Ordinary external UCI cp is labeled
engine-reported; it is never claimed as validated Chess Lab calibration.

UCI evaluator identity adds canonical calibration JSON, digest and effective
configuration provenance when present. Absence adds no fabricated calibration to
legacy identity. M7 analysis jobs retain their actual started engine identity and
trust provenance, so selecting/editing another profile cannot relabel old scores.

The included material provider remains minor 0/raw-only. Extended linear/table
providers are test fixtures; M7 does not add an arbitrary user-entered conversion
control that could misrepresent evaluator-owned calibration.
