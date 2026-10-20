# Chess Engine Lab — Codex Kickoff (M0 + M1)

**State:** Ready to begin implementation after Architecture Specification v1.0 approval (2026-10-19).  
**Authoritative reference:** `docs/architecture-v1.0.md` (copy the approved architecture document into the repository unchanged in substance).  
**Working approach:** Implement and validate one milestone at a time; do not begin M1 until M0 is accepted.

## Suggested repository

- Name: `chess-engine-lab` (change if preferred).
- Recommended visibility: **private initially**, with an explicit decision before publication.
- Main branch: `main`.
- Source-of-truth: GitHub repo; architecture under `docs/`.
- License: Do **not** assign a license to our original code by default. Record third-party license obligations separately. Preserve MIT notice for Disservin/chess-library if vendored.

## Paste into Codex — M0 task

> We are implementing **Chess Engine Lab**. Read `docs/architecture-v1.0.md` and treat it as the authoritative specification, approved on 2026-10-19. Proceed **only with M0: repository and contract scaffolding**. Do not implement minimax, quiescence, UCI, a GUI, chess rules, or evaluator algorithms yet.
>
> 
> **Stack:** C++20 with CMake; native desktop builds and Emscripten/WebAssembly build compatibility; TypeScript/React and Node.js later, not necessarily installed in M0.
>
> **M0 deliverables:**
> 1. Create a minimal, well-organized repo structure accommodating `engine/`, `evaluators/`, `controller/`, `gui/`, `tests/`, `docs/`, and `third_party/` (adapt names if strongly justified). Add sensible `.gitignore`, top-level `README.md`, and license inventory placeholder `THIRD_PARTY_LICENSES.md`.
> 2. Create a native C++20 CMake project with a minimal core library, test executable, and CTest smoke test. Compile successfully on the currently available development platform.
> 3. Add an Emscripten **build target or preset** for a minimal WebAssembly smoke test. If Emscripten is not installed or unavailable, do **not** claim that WASM tests passed: provide exact reproducible instructions, keep the target ready, and report the gap explicitly.
> 4. Define initial *conceptual/source-level* interfaces for `ChessCore`, opaque `Position`/`Move`/`UndoToken`, and a read-only evaluator position snapshot, without prematurely freezing the independently compiled C ABI layout. These should be skeleton contracts (comments/interfaces) sufficient to guide M1, not full implementations. Keep third-party chess-library types from appearing outside its future adapter.
> 5. Add developer instructions: native configure/build/test commands, prerequisites, build variants, and how to run the WASM smoke test when available.
> 6. Create or prepare baseline CI jobs for native compile and tests. Do not overbuild an untested cross-platform matrix; document what's actually verified.
> 
> **Constraints:** No network or cloud dependency at runtime; keep engine core free of Node.js, browser, OS process, and GUI dependencies. Preserve the ability to swap chess rules implementation. Do not implement undecided Decision 24 experimental quiescence details. Keep scope strictly at M0.
> 
> **Before marking complete:** execute available native compile/tests, report commands/results, enumerate new files, and list any outstanding issues or setup instructions. Stop after M0 and wait for review before starting M1.

## M0 acceptance checklist

- [ ] Approved `docs/architecture-v1.0.md` is checked in.
- [ ] Native C++20 CMake configure, build, and smoke test pass.
- [ ] WASM smoke build executes (or an explicitly reported toolchain blocker is documented; this means M0 portability is **not yet validated**).
- [ ] Third-party licensing expectations and dependency pinning policy documented.
- [ ] No GUI or search implementation introduced prematurely.
- [ ] Commit/PR includes clear file list and test evidence.

## M1 scope — after M0 acceptance

**Goal:** Deliver a replaceable orthodox chess core, initially backed by a pinned revision of [Disservin/chess-library](https://github.com/Disservin/chess-library) (MIT), and validate it on native and WebAssembly targets.

1. Add the pinned chess library behind our own `ChessCore` adapter. Do not expose library-specific types through core interfaces.
2. Support standard initial position, FEN load/export, legal move generation, make/unmake, check, and state inspection.
3. Provide stable, unambiguous move identifiers (UCI-style coordinate notation at external boundaries); handle castling, en passant, and all promotion pieces.
4. Generate the standardized read-only position snapshot needed by future evaluator modules (without fixing the complete M2 ABI prematurely).
5. Support board-position hashing and clarify distinction between transposition keys and FIDE repetition equivalence. Preserve game history separately.
6. Add position round-trips and reference perft tests. At a minimum, standard initial position expected perft counts are depth 1: 20, depth 2: 400, depth 3: 8,902, depth 4: 197,281. Add independently verified special-move test positions and expected counts.
7. Verify applying and undoing every legal move restores all chess state and hashes. Explicitly test check, mate, stalemate, castling rights, en-passant, and promotions.
8. Compile and run equivalent golden correctness tests in native and WASM environments. If the browser/Worker harness is not yet available, use a minimal WASM test runner.
9. Report acceptance results and stop before M2.

**Do not build in M1:** Evaluation DLLs, minimax, quiescence, MultiPV, UCI, match controller, GUI, or mobile packaging.

## Review gates and developer notes

- **Correctness first:** All moves must be legal and make/unmake reversible. Treat perft failures as blocking.
- **Portability gate:** A C++ library being portable *in principle* is not evidence that the selected pinned version has passed native/WASM tests.
- **Naming:** Keep engine internals platform independent; transport is a later adapter.
- **Experiment discipline:** Defaults and thresholds for Decision 24 are intentionally undecided. Don't silently choose them during unrelated milestones.
- **Conservative scope:** Prefer a small compiling vertical slice plus tests to a broad untested scaffold.

## Suggested first Codex message if repo is empty

> Create an M0-only scaffold for my private GitHub project `chess-engine-lab`, following the attached approved Architecture Specification v1.0 and the M0 task above. If you do not have permission to create or push to GitHub, work in the local checkout, show me the steps needed to connect/push, and do not claim a repository was created. Stop when M0 builds/tests are reported. Ask for review before M1.