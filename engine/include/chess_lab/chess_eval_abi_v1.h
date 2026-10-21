#ifndef CHESS_EVAL_ABI_V1_H
#define CHESS_EVAL_ABI_V1_H

/* Chess Engine Lab — Evaluation ABI v1.0, approved public contract.
 * C11-compatible; also valid C++20. No third-party chess types are exposed.
 * A native module exports chess_evaluator_get_api; a bundled WASM provider
 * registers a function of type ChessEvalGetApiFn instead.
 */

#include <stdint.h>

#if defined(__cplusplus)
#  define CHESS_EVAL_ALIGN8 alignas(8)
#elif defined(_MSC_VER)
#  define CHESS_EVAL_ALIGN8 __declspec(align(8))
#else
#  define CHESS_EVAL_ALIGN8 _Alignas(8)
#endif

#if defined(_WIN32)
#  define CHESS_EVAL_CALL __cdecl
#  define CHESS_EVAL_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#  define CHESS_EVAL_CALL
#  define CHESS_EVAL_EXPORT __attribute__((visibility("default")))
#else
#  define CHESS_EVAL_CALL
#  define CHESS_EVAL_EXPORT
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define CHESS_EVAL_ABI_MAJOR ((uint64_t)1)
#define CHESS_EVAL_ABI_MINOR ((uint64_t)0)
#define CHESS_EVAL_SNAPSHOT_MAJOR ((uint64_t)1)
#define CHESS_EVAL_SNAPSHOT_MINOR ((uint64_t)0)
#define CHESS_EVAL_EP_NONE UINT64_MAX

/* Stable signed 32-bit status, NOT a compiler-dependent enum. */
typedef int32_t ChessEvalStatus;
#define CHESS_EVAL_OK                    ((ChessEvalStatus)0)
#define CHESS_EVAL_INVALID_ARGUMENT      ((ChessEvalStatus)1)
#define CHESS_EVAL_INCOMPATIBLE_ABI       ((ChessEvalStatus)2)
#define CHESS_EVAL_INCOMPATIBLE_SNAPSHOT  ((ChessEvalStatus)3)
#define CHESS_EVAL_INVALID_CONFIGURATION ((ChessEvalStatus)4)
#define CHESS_EVAL_OUT_OF_MEMORY         ((ChessEvalStatus)5)
#define CHESS_EVAL_INTERNAL_ERROR        ((ChessEvalStatus)6)
#define CHESS_EVAL_BUFFER_TOO_SMALL      ((ChessEvalStatus)7)
#define CHESS_EVAL_INVALID_STATE         ((ChessEvalStatus)8)

/* Bitboard index: WP WN WB WR WQ WK BP BN BB BR BQ BK.
 * Square index: a1=0, b1=1, ..., h8=63.
 * Castling mask bits: 0 WK, 1 WQ, 2 BK, 3 BQ.
 * EP square: raw FEN target (0..63), or CHESS_EVAL_EP_NONE.
 * Snapshot fields are read-only throughout evaluate(); a module must not
 * retain this pointer beyond the call. All reserved bit values are invalid.
 */
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

typedef void* ChessEvalHandle;

/* In a get-json call, output==NULL and capacity==0 queries required_bytes
 * (including the trailing NUL). Output is UTF-8 JSON, null terminated.
 * On insufficient capacity return BUFFER_TOO_SMALL with required_bytes set,
 * and do not write a truncated JSON object.
 */
typedef ChessEvalStatus (CHESS_EVAL_CALL *ChessEvalGetJsonFn)(
    char* output, uint64_t output_capacity, uint64_t* required_bytes,
    char* error, uint64_t error_capacity);

typedef ChessEvalStatus (CHESS_EVAL_CALL *ChessEvalCreateFn)(
    const char* configuration_json, uint64_t configuration_length,
    ChessEvalHandle* out_handle, char* error, uint64_t error_capacity);

typedef ChessEvalStatus (CHESS_EVAL_CALL *ChessEvalGetConfigFn)(
    ChessEvalHandle handle, char* output, uint64_t output_capacity,
    uint64_t* required_bytes, char* error, uint64_t error_capacity);

typedef ChessEvalStatus (CHESS_EVAL_CALL *ChessEvalEvaluateFn)(
    ChessEvalHandle handle, const ChessEvalPosition* position,
    double* out_score, char* error, uint64_t error_capacity);

typedef void (CHESS_EVAL_CALL *ChessEvalDestroyFn)(ChessEvalHandle handle);

/* Function pointers each occupy an ABI slot beginning at a multiple of 8.
 * On wasm32, the 32-bit pointer gets trailing padding in its 8-byte slot.
 * A platform failing the layout assertions is unsupported, not re-packed.
 */
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

/* ABI minor 1: compatible optional tail. The legacy 72-byte type above stays
 * source/binary compatible; the extended wire table appends one padded slot.
 * Negotiate requested_minor=1, capacity=80. A v1.0 caller always gets prefix 72.
 */
#define CHESS_EVAL_CALIBRATION_MINOR ((uint64_t)1)
typedef struct ChessEvalApiV1_1 {
    ChessEvalApi v1;
    CHESS_EVAL_ALIGN8 ChessEvalGetConfigFn get_calibration_json;
} ChessEvalApiV1_1;

typedef ChessEvalStatus (CHESS_EVAL_CALL *ChessEvalGetApiFn)(
    uint64_t requested_major, uint64_t requested_minor,
    void* out_api, uint64_t out_api_capacity,
    char* error, uint64_t error_capacity);

/* Define only when building a native shared-library evaluator. */
#if defined(CHESS_EVAL_DECLARE_NATIVE_ENTRYPOINT)
CHESS_EVAL_EXPORT ChessEvalStatus CHESS_EVAL_CALL chess_evaluator_get_api(
    uint64_t requested_major, uint64_t requested_minor,
    void* out_api, uint64_t out_api_capacity,
    char* error, uint64_t error_capacity);
#endif

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* CHESS_EVAL_ABI_V1_H */