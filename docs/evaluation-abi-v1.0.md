# Chess Engine Lab — Evaluation ABI v1.0

**Status:** APPROVED — design Decisions 32–40; authoritative M2 ABI specification  
**Date:** 2026-10-20  
**Repository path:** `docs/evaluation-abi-v1.0.md`  
**Public ABI header:** `engine/include/chess_lab/chess_eval_abi_v1.h`  
**Authority:** supplements `docs/architecture-v1.0.md`; records design Decisions **32–40**.

> **Purpose.** Define a minimal, stable, independently implementable C-compatible boundary between the chess engine and a static position evaluator. The desktop engine must load an independently compiled native library; mobile/WebAssembly builds must select bundled evaluators via an equivalent function table. Evaluation stays independent of chess rules, history, search, and UI.

## 1. Approved principles and boundary

- **D32 — Entry point.** A native shared library exports exactly one mandatory symbol: `chess_evaluator_get_api`, which negotiates and fills a versioned function table. Extra internal symbols need not be exported.
- **D33 — Instances.** The engine owns opaque evaluator handles; a module owns the allocation and destruction of its instances. Parameters are immutable per instance. Internally mutable caches/precomputed data are permitted, but evaluation results must be deterministic for a given snapshot, parameters, build, and execution environment, regardless of call order or prior evaluations. Different instances may run concurrently; calls to the **same** instance are serialized. Bit-identical floating-point answers across different architectures are not promised.
- **D34 — Parameters.** Modules receive UTF-8 JSON once at instance creation, validated against a declared schema. No per-evaluation JSON parsing.
- **D35 — Snapshot.** Input is read-only current-position data, detached from the chess-rules implementation, with no move history, score, or adjudication. The originally proposed byte-per-square representation is **replaced for this ABI** by twelve canonical 64-bit bitboards (D40).
- **D36 — Versions.** Evaluation ABI and snapshot schema have independently negotiated major/minor revisions. Existing offsets and meanings cannot change within a major version. Reject incompatible requirements before instance creation.
- **D37 — Failures.** Evaluation errors/NaN/infinity invalidate **all** results of the affected search, including completed depths. Do not use those results, substitute another evaluator, or use a deterministic move fallback. That fallback applies only to otherwise valid, interrupted searches. Report the error; disable the failed instance until replacement. Search behavior is implemented in M3/M4, not M2.
- **D38 — Diagnostics.** Fixed 32-bit numeric statuses; optional UTF-8 diagnostics written to caller-owned buffers; no C++ exceptions or module-owned error-string pointers cross the ABI.
- **D39 — Identity.** Module metadata and a restricted JSON parameter schema; resolved effective parameter JSON; host-side canonicalization and module-artifact hashing for experiment identity.
- **D40 — Performance.** All *top-level ABI structure members* begin at offsets divisible by 8. Use naturally aligned, native-word-oriented representations where they benefit common microarchitectures. Memory compactness is not an overriding objective; performance claims require benchmarks. The C position record is 160 bytes and the v1.0 API table has six function pointers.

The approved `docs/architecture-v1.0.md` describes a 64-square, fixed-code *conceptual* snapshot. M1 also exposes an existing C++ `PositionSnapshot` with `squares()`. This document **refines the binary ABI**, not M1's C++ contract: the M2 bridge converts the C++ snapshot to the bitboard form below without exposing any Disservin chess-library types. This approved document governs ABI layout; the original architecture's conceptual current-position fields remain intact.

## 2. Supported languages, platforms, and alignment

The companion header is valid C11 and C++20 and includes only `<stdint.h>`. C++ symbols use `extern "C"`; no C++ templates, `std::string`, exceptions, RTTI, virtual objects, or standard-library ownership cross this boundary. Native builds use platform-specific dynamic-library loading (`LoadLibrary/GetProcAddress`, `dlopen/dlsym`, etc.) only in the loader adapter.

All `ChessEvalPosition` and `ChessEvalApi` fields have **8-byte member alignment**. Each top-level struct itself has 8-byte alignment. Arrays have 8-byte-aligned bases and 8-byte elements. Function pointers occupy separately aligned **8-byte slots** in the table, even on wasm32 when a pointer may be 4 bytes. Each build must assert structure alignment, size, and every member offset; a platform not meeting the checks is unsupported rather than silently repacked. No `#pragma pack`.

**Caution:** the common member offsets specify source-compatible layout within each supported target, **not** binary interchangeability between native operating systems or arbitrary processor ABIs. The same compiled DLL is not portable between unrelated native platforms. Native code still uses each target's native calling convention, checked function-pointer types, and toolchain. The WebAssembly provider registry is linked into the app and does not install or dynamically load untrusted external mobile code.

## 3. Position snapshot schema 1.0

C declaration (normative; full header supplied separately):

```c
typedef struct ChessEvalPosition {
    CHESS_EVAL_ALIGN8 uint64_t struct_size;
    CHESS_EVAL_ALIGN8 uint64_t schema_major;
    CHESS_EVAL_ALIGN8 uint64_t schema_minor;
    CHESS_EVAL_ALIGN8 uint64_t bitboards[12];
    CHESS_EVAL_ALIGN8 uint64_t side_to_move;
    CHESS_EVAL_ALIGN8 uint64_t castling_rights;
    CHESS_EVAL_ALIGN8 uint64_t en_passant_square;
    CHESS_EVAL_ALIGN8 uint64_t halfmove_clock;
    CHESS_EVAL_ALIGN8 uint64_t fullmove_number;
} ChessEvalPosition;
```

### Exact offsets and meanings

| Offset | Size | Member | Contract |
|---:|---:|---|---|
| 0 | 8 | `struct_size` | At least 160 for snapshot 1.0; engine supplies true allocated size |
| 8 | 8 | `schema_major` | 1 |
| 16 | 8 | `schema_minor` | 0 initially |
| 24 | 96 | `bitboards[12]` | Order described below; twelve native `uint64_t` words |
| 120 | 8 | `side_to_move` | 0 White, 1 Black |
| 128 | 8 | `castling_rights` | bit 0 White K, bit 1 White Q, bit 2 Black K, bit 3 Black Q; all high bits zero |
| 136 | 8 | `en_passant_square` | 0–63 FEN target, `UINT64_MAX` if absent |
| 144 | 8 | `halfmove_clock` | Number of halfmoves since last pawn move/capture |
| 152 | 8 | `fullmove_number` | FIDE fullmove number, at least 1 |

**Total:** `sizeof(ChessEvalPosition) == 160`; `alignof/_Alignof(ChessEvalPosition) == 8`. Both the host and module validate before use.

Canonical bitboard order:

| Index | Piece | Index | Piece |
|---:|---|---:|---|
| 0 | White pawn | 6 | Black pawn |
| 1 | White knight | 7 | Black knight |
| 2 | White bishop | 8 | Black bishop |
| 3 | White rook | 9 | Black rook |
| 4 | White queen | 10 | Black queen |
| 5 | White king | 11 | Black king |

**Bit `n` means square `n`, with `a1=0, b1=1, …, h8=63`.** Bitboards must be pairwise disjoint. A valid orthodox chess position has exactly one king of each color. Pawns may not occupy the first or eighth rank. The host creates valid snapshots through `ChessCore`; modules may reject invalid input. The en-passant value is the **raw current-position FEN target**, even when no en-passant capture is legal; it is not an occurrence-history or repetition key. Current halfmove/fullmove counters are included but no module may perform historical draw adjudication.

A module must never modify or retain the snapshot pointer. The engine owns its storage, and the pointer is valid only for the duration of that call. M1 uses 32-bit counters; the bridge losslessly widens them to 64-bit values. Future snapshot minor revisions may append fields, but existing offsets, values, alignment, and semantic meanings must remain unchanged. A v1.0 module accesses only the first 160 bytes after checking `struct_size>=160` and compatible schema versions.

## 4. Status codes and diagnostics

`ChessEvalStatus` is `int32_t` with these exact values:

| Name | Value | Meaning |
|---|---:|---|
| `CHESS_EVAL_OK` | 0 | Success |
| `CHESS_EVAL_INVALID_ARGUMENT` | 1 | Bad pointer, size, or malformed call arguments |
| `CHESS_EVAL_INCOMPATIBLE_ABI` | 2 | Unsupported API major/minor requirements |
| `CHESS_EVAL_INCOMPATIBLE_SNAPSHOT` | 3 | Snapshot major/minor or structure size not supported |
| `CHESS_EVAL_INVALID_CONFIGURATION` | 4 | Malformed JSON, unknown/invalid parameter, or invalid parameter schema |
| `CHESS_EVAL_OUT_OF_MEMORY` | 5 | Resource allocation failure |
| `CHESS_EVAL_INTERNAL_ERROR` | 6 | Other module error |
| `CHESS_EVAL_BUFFER_TOO_SMALL` | 7 | Caller-provided output buffer cannot hold complete data |
| `CHESS_EVAL_INVALID_STATE` | 8 | Invalid evaluator state or lifecycle operation |

Unknown or malformed status values are host-detected **contract violations**, never success. For functions accepting `error, error_capacity`: `(NULL, 0)` opts out of diagnostics; otherwise `error` points to a writable caller-owned byte buffer. On any error, the module writes a valid NUL-terminated UTF-8 diagnostic if capacity permits. If too short, truncate on a UTF-8 character boundary and terminate. For `error_capacity > 0`, `error` must be non-NULL; the module should set the first byte to `\0` on successful calls. The host should allow up to 4 KiB of diagnostic text but must not rely on it to determine status. Diagnostic text is not a stable API.

No C++ exception may cross the function-pointer boundary. A C++ module must catch and translate its own exceptions. In-process native code can still crash or corrupt the host; ABI validation is not process isolation.

## 5. JSON metadata, schemas, and effective configuration

All JSON values use UTF-8 and must contain no duplicate object keys or nonfinite numbers. **Version 1 of our parameter schema uses a deliberately limited, documented JSON-Schema-like subset**; unsupported keywords or nested types are rejected rather than silently ignored.

### Required module metadata JSON

```json
{
  "module_id": "org.chesslab.material",
  "display_name": "Material Evaluator",
  "module_version": "1.0.0",
  "description": "Material balance from the current board",
  "parameter_schema_version": 1,
  "required_snapshot_major": 1,
  "minimum_snapshot_minor": 0,
  "score_units": "arbitrary"
}
```

`module_id` is a stable, globally distinguishable string (reverse-domain form recommended); `module_version` names the module's published algorithm revision. `description` and `display_name` are human-readable. `parameter_schema_version=1` selects the grammar below. `required_snapshot_major` and `minimum_snapshot_minor` must be checked before creation. `score_units` describes raw units **but is not an automatic centipawn conversion**. Extra optional metadata can be introduced under compatible revisions; engines ignore unfamiliar optional keys, but required fields must be present with the specified types. An actual calibrated centipawn mapping requires a separate explicit, meaningful documented declaration; the initial material sample does not claim one.

### Parameter-schema JSON — grammar v1

The root is an object with required members `schema_version:1`, `type:"object"`, `properties:{...}`, and `additionalProperties:false`; optional `required:[...]` lists mandatory keys. Individual properties are **flat scalar** types: `number`, `integer`, `boolean`, or `string`. Supported property keywords: `type`, `default`, `enum`, `minimum`, `maximum`, `minLength`, and `maxLength`, where relevant. Nested objects, arrays, conditional schemas, references, and unknown keywords are rejected in schema v1. `number` means finite binary64-representable JSON number; `integer` is an exactly representable integer within the interoperable range −(2^53−1) to +(2^53−1). Enumeration members must match the property's type. String length counts Unicode scalar values, and strings must be valid UTF-8. All bounds and defaults must be valid and internally consistent.

Every optional property **must** declare a valid `default`. A required property has no implicit default. Unknown configuration keys are rejected; no implicit coercion (such as string `"3"` to number 3) or clamping is permitted. The engine may validate early for UX, but **the module must validate independently**. Configuration must be a JSON object even when empty (`{}`).

Example schema:

```json
{
  "schema_version": 1,
  "type": "object",
  "properties": {
    "pawn_value":   { "type": "number", "default": 1.0, "minimum": 0 },
    "knight_value": { "type": "number", "default": 3.0, "minimum": 0 },
    "bishop_value": { "type": "number", "default": 3.25, "minimum": 0 },
    "rook_value":   { "type": "number", "default": 5.0, "minimum": 0 },
    "queen_value":  { "type": "number", "default": 9.0, "minimum": 0 }
  },
  "additionalProperties": false
}
```

On successful creation, `get_effective_configuration_json(handle)` reports all parameters after defaults have been applied, with no omitted optional keys. The engine validates this effective configuration against the schema and canonicalizes it using **RFC 8785 / JSON Canonicalization Scheme** over interoperable I-JSON values. Whitespace and object-key order must not affect fingerprints. If effective configuration disagrees semantically with supplied values plus declared defaults, the host rejects the instance instead of silently accepting substitutions. Preserve original input separately if useful for diagnostics.

For reproducibility, an experiment manifest stores: module ID/version, effective canonical JSON, schema version, ABI major/minor, snapshot major/minor, actual evaluator artifact SHA-256 (or deterministic bundled-provider identity plus encompassing WASM artifact SHA-256), and platform/build identifiers. Changing evaluator identity or parameters invalidates evaluation-dependent caches. No raw evaluator score is assumed to be in centipawns.

## 6. Caller-owned JSON buffer convention

Three operations use UTF-8 output:

1. `get_module_info_json(output, capacity, required_bytes, error, error_capacity)`
2. `get_parameter_schema_json(output, capacity, required_bytes, error, error_capacity)`
3. `get_effective_configuration_json(handle, output, capacity, required_bytes, error, error_capacity)`

`required_bytes` is mandatory and includes one byte for the terminating NUL. The caller first passes `output=NULL, output_capacity=0` to obtain `required_bytes` with `CHESS_EVAL_OK`; this query never writes JSON. It then supplies a buffer of at least that size. A full write returns `CHESS_EVAL_OK`, writes complete UTF-8 JSON plus trailing NUL, and updates `required_bytes`. An undersized non-NULL buffer returns `CHESS_EVAL_BUFFER_TOO_SMALL` and writes **no partial JSON** (but must report the required size). A non-NULL buffer with capacity zero is treated as undersized. A NULL output with nonzero capacity is invalid. The host imposes a **1 MiB maximum** on metadata, schema, and effective configuration outputs, and rejects nonterminated, invalid UTF-8, malformed JSON, excessive sizes, or inconsistent reported lengths. Metadata, schema, and effective configuration are immutable for a given module/instance so the two-call size query is stable. Caller may retry after `BUFFER_TOO_SMALL`.

`create_evaluator` receives the bytes and explicit length of an input JSON object, not necessarily NUL-terminated. It requires non-NULL `configuration_json`, nonzero length, and non-NULL `out_handle`. It sets `*out_handle=NULL` before attempting initialization; on failure it leaves the handle null, releases partial resources, and preserves any previously active instance in the host. Instance handles are never passed between separately loaded modules.

`destroy_evaluator(handle)` is only called with a valid module-owned instance and must not throw. It is called after evaluations have stopped. The module cannot be unloaded until every instance and call has finished.

## 7. Evaluate semantics and high-frequency path

`evaluate(handle, position, out_score, error, error_capacity)` has no JSON, allocation, logging, system call, or synchronization **requirement** on the hot path; implementations may use caches and compute as needed. It returns `CHESS_EVAL_OK` and writes a finite IEEE-754 binary64 result into `*out_score` on success, where positive is favorable to White and negative to Black. It must not modify `position` or retain pointers to it or to output/error buffers.

A module must not return NaN or infinity, and must not encode mate or draw adjudication as static scores. The host checks that returned status is known and, on success, checks `isfinite(*out_score)`. On nonzero status, the host disregards `*out_score` completely. This is *not* a promise that the ABI can catch arbitrary native crashes.

**Search failure rule (Decision 37):** a reported evaluation error, invalid score, unknown status, or discovered ABI contract violation invalidates **all** completed and in-progress results of the affected search. Search stops with a distinct `EVALUATOR_FAILED` outcome, no decision/move from that search, no fallback move, no silent replacement evaluator, and an explicit diagnostic for GUI/controller/experiments. Mark the instance unusable until destroyed and replaced. This rule is specified now and implemented/tested in the search milestone; M2 tests can verify failure propagation without implementing search.

## 8. Function table and entry point

Exact typedefs, declarations, calling-convention annotations, and alignment macros are supplied in the companion `chess_eval_abi_v1.h`. The v1.0 table is:

```c
typedef struct ChessEvalApi {
    CHESS_EVAL_ALIGN8 uint64_t struct_size;
    CHESS_EVAL_ALIGN8 uint64_t abi_major;
    CHESS_EVAL_ALIGN8 uint64_t abi_minor;
    CHESS_EVAL_ALIGN8 ChessEvalGetJsonFn get_module_info_json;
    CHESS_EVAL_ALIGN8 ChessEvalGetJsonFn get_parameter_schema_json;
    CHESS_EVAL_ALIGN8 ChessEvalCreateFn create_evaluator;
    CHESS_EVAL_ALIGN8 ChessEvalGetConfigFn get_effective_configuration_json;
    CHESS_EVAL_ALIGN8 ChessEvalEvaluateFn evaluate;
    CHESS_EVAL_ALIGN8 ChessEvalDestroyFn destroy_evaluator;
} ChessEvalApi;
```

Exact offsets in bytes: `struct_size` 0; `abi_major` 8; `abi_minor` 16; pointers at 24, 32, 40, 48, 56, and 64. **`sizeof(ChessEvalApi)==72`, alignment 8** for supported native and WASM targets. Each function pointer has its declared type; none may be null after successful negotiation. The function table is immutable after negotiation.

The mandatory native export is `chess_evaluator_get_api`, having this signature:

```c
ChessEvalStatus chess_evaluator_get_api(
    uint64_t requested_major,
    uint64_t requested_minor,
    void* out_api,
    uint64_t out_api_capacity,
    char* error,
    uint64_t error_capacity);
```

The host zero-initializes an aligned `ChessEvalApi` table, requests ABI `(1,0)`, and supplies capacity 72. The module requires `requested_major==1` and `out_api_capacity>=72`, writes at most the negotiated capacity, populates all six functions, and sets `struct_size=72`, `abi_major=1`, `abi_minor=0` for this version. It must not overrun the supplied memory. A future module may negotiate a compatible lower minor version and fill only the supported prefix; adding functions may only append to the table, never reorder existing entries. A future host must tolerate a v1.0 module if its supported version range includes 1.0; a module requiring a new major cannot be loaded by an ABI-major-1 host. The host rejects inconsistent size/version responses.

Before `create_evaluator`, the host queries module metadata and schema, confirms the module requires a supported snapshot major/minor, validates schema grammar, and verifies all required function pointers. On each evaluation, the module checks snapshot schema, `struct_size>=160`, and supported field values; the engine checks them when building the snapshot. Future minor schemas append fields only; older modules ignore new trailing fields. A newer module needing a higher minimum snapshot minor is rejected before creation if the host only provides schema 1.0.

For mobile/WASM, an application registry supplies uniquely named bundled providers of the same **`ChessEvalGetApiFn` function-pointer type**; the native symbol name need not be exported by each bundled provider, avoiding symbol collisions. No unrestricted runtime installation of evaluator binaries is required.

## 9. Loader lifecycle, errors, and resource boundaries

1. **Load** the native module or select bundled provider; identify its exact artifact/build.
2. **Negotiate** ABI table and major/minor versions; check member offsets/sizes and mandatory pointers.
3. **Fetch/validate** metadata and parameter schema; check snapshot compatibility; enforce JSON buffer limits.
4. **Create** a new instance from validated JSON; fetch and validate effective configuration. Only after full success atomically select it for future searches, retaining any prior valid instance until replacement is ready.
5. **Evaluate** read-only snapshots. Different instances may be evaluated concurrently; serialize calls to one instance. No evaluator receives previous-move, repetition, controller, or search context.
6. **Stop** all relevant uses before changing instance/module; invalidate evaluation-dependent caches when changed.
7. **Destroy** every instance with the matching module's destroy function; only then **unload** native module.

An evaluator creation/configuration failure does not disturb the prior valid instance. Failure during evaluation quarantines that instance and invalidates the affected search as specified above. A process crash in the evaluator is outside guarantees of this in-process ABI; process isolation could be designed separately later.

## 10. M2 deliverables and acceptance gates

M2 is **ABI and sample evaluator only**. Do not implement minimax, iterative deepening, quiescence, UCI, controller, GUI, mobile packaging, or unresolved Decision 24 behavior.

**Required deliverables:**

1. Commit the approved spec and portable public C header, including C11 and C++20 layout assertions for both structs and all fields.
2. Implement the M1-`PositionSnapshot` to ABI bitboard bridge. Test initial position and complex FENs, castling and EP metadata, move counters, and non-overlapping bitboards; no Disservin types appear in the ABI/public evaluator headers.
3. Build an independently compiled shared native **material-only evaluator** as a DLL/dylib/so where supported. Implement metadata, parameter schema, defaults, effective JSON, creation, evaluation, destruction, and all error contracts.
4. Build a native loader/host that negotiates the table, validates metadata/schema, creates independent instances, switches them transactionally, evaluates snapshots, destroys instances, and unloads modules safely. Include one intentionally invalid/incompatible sample module or test fixture to prove rejection behavior.
5. Bundle the **same material evaluation algorithm** in WebAssembly via a registry/provider function table, and run equivalent golden vectors under the pinned Emscripten/Node CI configuration established in M1. Native CI remains green.
6. Test missing/wrong symbol, ABI mismatch, snapshot mismatch, insufficient buffers, null pointers, invalid schema, invalid JSON and bounds, unknown parameter, failed creation, invalid/unknown status, NaN/infinity, and error diagnostics.
7. Test numerical material scores (initial position zero, controlled material imbalances with both sides to move, promotions, parameter variations); verify scores do not depend on evaluation order, previous calls, or independent instance caches.
8. Test module artifact digest + canonical effective configuration identity; reject metadata sizes beyond limits; verify module cannot unload with live handles. Test that switching evaluator/parameters causes a cache invalidation signal (without implementing M3/M5 search/TT).
9. Report **actual** native and WASM test execution. Do not claim browser/mobile device portability from Node/WASM alone. Document toolchain settings (especially Emscripten C++ exception handling), build commands, and failures.
10. Stop at a reviewable M2 PR on a new branch based on latest `origin/main`. Do not merge or begin M3.

**Performance acceptance:** The per-evaluation ABI call is binary, not JSON-based. Benchmark snapshot conversion vs evaluator computation on representative native positions; report instruction/performance observations, not presumed benefits. Use correctness-first optimization; don't silently widen/repack fields or change the ABI based only on an unmeasured performance hypothesis.

## 11. Design approval record

- [x] Approved: 160-byte bitboard snapshot and 72-byte function table, including wasm32 padded function-pointer slots.
- [x] Approved: the restricted flat parameter schema v1 grammar and exact buffer/status behavior.
- [x] Approved: module metadata and effective-configuration/fingerprint rules.
- [x] Approved: minor-version negotiation semantics and exact ABI entry point.
- [x] Confirmed: this **supplements**, without silently rewriting, the approved architecture and M1 code.
- [x] Approved for publication in GitHub and for subsequent strictly M2-only Codex implementation.
