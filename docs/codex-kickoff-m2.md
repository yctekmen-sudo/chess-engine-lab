# Chess Engine Lab — Codex Kickoff for M2

**Status:** READY FOR IMPLEMENTATION. **Milestone:** M2 — Evaluation ABI and first interchangeable evaluator.
**Approved design:** `docs/evaluation-abi-v1.0.md` and `engine/include/chess_lab/chess_eval_abi_v1.h`.
**Additional references:** `docs/architecture-v1.0.md`, `docs/m1-contracts.md`, `docs/m1-validation.md`, `docs/development.md`.

## How to begin

1. Fetch the latest `origin/main`. Verify that merged M0 PR #1 and M1 PR #2 are present and that the **approved** ABI specification and header are present on `main`.
2. Create a **new branch `m2/evaluation-abi` based on latest `origin/main`**; do not begin from `m1/chess-core`. Report base commit and branch.
3. Treat the approved specification and header as **normative**. Do not silently change the ABI structures, field offsets, eight-byte alignment, pointer-table slots, version semantics, function signatures, or Decision 37 search-failure policy. If the contract proves technically impossible on an intended target, report the specific issue and stop for design review rather than silently revising the ABI.
4. Implement and test M2 **only**. Do not begin M3, implement search/minimax/quiescence, UCI, GUI, controller, or mobile application packaging.

## M2 implementation work

- Implement a fully validated adapter from M1's source-level read-only `PositionSnapshot` to the versioned, read-only ABI bitboard structure. Map the 12 fixed piece types, a1=bit 0 through h8=bit 63, side to move, castling, raw FEN en passant target, and counters. Keep Disservin types private. Verify all fields with positions covering castling, en passant, promotions, checks, and unusual clock counters.
- Establish the approved portable C header as the single source of ABI declarations. Add explicit C11 and C++20 compile-time layout assertions for the 160-byte snapshot and 72-byte function table, eight-byte struct and **top-level-member** alignment, and all offsets, on each tested native and WASM target.
- Implement a separately compiled **material-only evaluator module**, exported as a shared library on native platforms. Export the single required entry-point symbol `chess_evaluator_get_api`, with the complete negotiated function table, JSON metadata, restricted v1 parameter schema, schema validation, effective configuration, and independently owned instances. Use finite White-positive scores and no game history.
- Implement a native loader/host adapter that resolves the symbol, negotiates ABI/snapshot support, validates metadata/schema, enforces buffer/UTF-8/JSON limits, creates and replaces instances transactionally, calls evaluation, checks statuses and finite results, destroys every module-owned instance, and prevents premature unloading.
- Implement the **same** sample evaluator through a bundled-provider registry under Emscripten/WebAssembly, using the same function-table contract. WebAssembly needs no native dynamic loading or installation of external module binaries.
- Implement RFC 8785-compatible canonical effective-configuration identity and module-artifact SHA-256 tracking, recording the ABI, snapshot version and relevant build/provider identity for future experiment manifests. Don't invent a centipawn conversion for arbitrary evaluator units.
- Provide controlled invalid/incompatible evaluator fixtures and exhaustive error-path testing. Report module creation, ABI negotiation, invalid JSON/schema/parameter, invalid input snapshot, invalid pointers, undersized buffers, unknown status, NaN/infinity, error diagnostics, and failure cleanup.
- Validate that multiple instances have independent immutable configurations, that cache state cannot change results with evaluation call order, and that switching module/configuration signals invalidation of dependent cached evaluations. Do not implement an actual search transposition table in M2.
- Add native Debug/Release CTest coverage, dynamic-loader tests, and WASM golden correctness tests under pinned GitHub Actions runners. Keep the M0/M1 correctness suites passing. Include separate C and C++ ABI layout tests where supported.
- Benchmark representative snapshot conversion and material evaluations, report actual measured results and methodology; avoid speculative ABI changes based on expected CPU instruction behavior.

## Required acceptance report

Record in `docs/m2-validation.md`:
- Exact native and WASM configure/build/test commands, environments, and GitHub Actions links.
- Snapshot golden position test results and ABI layout verification for tested platforms.
- Native dynamic loading and independent-instance tests.
- Positive and negative module/JSON/error tests, including nonfinite-score rejection.
- Artifact/configuration fingerprint tests.
- Performance measurement methodology and results, with limitations.
- Any unsupported targets or incomplete validation; never claim browser/device validation from Node/WASM alone.
- Confirmation that the approved ABI header and contract were not modified without design approval.

**Important:** Decision 37 states that an evaluator error invalidates **all** completed and incomplete search results. M2 establishes reliable failure reporting and propagation, but the later M3/M4 search milestones implement and verify whole-search invalidation. Do not introduce an error fallback move.

## Finish condition

Commit and push work to `m2/evaluation-abi` and open a PR against `main` if possible. If PR creation fails due to credentials/network restrictions, provide the branch and commit SHA for a GitHub-side PR. **Do not merge and do not begin M3. Stop for ChatGPT architecture/code review.**
