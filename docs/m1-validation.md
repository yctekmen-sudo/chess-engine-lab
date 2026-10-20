# M1 validation — 2026-10-20

## Branch provenance and scope

Fetched GitHub main before changes. The starting commit is
8d956d4fb70d50022248e7d2c1cd229e9902c28a, whose message is
"Merge pull request #1 from yctekmen-sudo/m0/project-setup" (M0).
Created m1/chess-core directly from that origin/main in a clean worktree.
The original checkout's uncommitted files were preserved. No M0 branch was used
as the M1 base. No M2, evaluator, search, quiescence, UCI protocol, controller
or GUI implementation is included.

## Dependency and boundaries

Vendored unmodified upstream chess.hpp and MIT LICENSE at immutable commit
53e6a841dcda7059a2af363d85f785ef1817304a. Checksums and origin are recorded in
third_party/chess-library/README.md. Only the adapter implementation includes
the upstream header; public interfaces and tests use our own types. Builds and
runtime are offline.

## Native results

Linux, GCC 14.2.0, CMake 3.31.6, Ninja 1.11.1.
All commands run from the M1 worktree with the cloud toolchain on PATH:

    cmake --preset native-debug
    cmake --build --preset native-debug
    ctest --preset native-debug

    cmake --preset native-release
    cmake --build --preset native-release
    ctest --preset native-release

Both modes pass 2/2 tests: m0_smoke and m1_core. Latest runs: Debug 15.49 s,
Release 0.71 s. Tests use explicit runtime checks, active with NDEBUG.
Timing is observational, not a benchmark.

Reference vectors: [Chess Programming Wiki, Perft Results](https://www.chessprogramming.org/Perft_Results),
positions 1–6. Upstream's tests/perft.cpp uses these position families at deeper depths.
All listed counts matched:

| Position | Depth 1 | Depth 2 | Depth 3 | Depth 4 |
| --- | ---: | ---: | ---: | ---: |
| Initial | 20 | 400 | 8,902 | 197,281 |
| Kiwipete | 48 | 2,039 | 97,862 | not run |
| Rook/pawn endgame (EP) | 14 | 191 | 2,812 | 43,238 |
| Promotions/castling (position 4) | 6 | 264 | 9,467 | not run |
| Promotion/check (position 5) | 44 | 1,486 | 62,379 | not run |
| Middlegame (position 6) | 46 | 2,079 | 89,890 | not run |

At every traversed edge, undo verifies complete exported FEN, every snapshot
field and both position/repetition keys. Golden FENs are in tests/m1_core.cpp.

Additional checks cover:

- Initial state/piece codes, detached immutable snapshots, independent clones,
  reset and invalid-reset atomicity.
- Both castling sides/colors, rook placement, rights removal after castling or
  rook capture, forbidden castling in/through check.
- Legal EP and captured-pawn removal, pinned illegal EP, raw double-push FEN
  preservation, legal/nonlegal EP identity differences.
- All four promotion choices for both colors and capture promotions.
- Check with legal evasions, checkmate, stalemate and known dead material.
- Turn/castling identity differences, clock/history-independent identity after
  a reversible knight cycle, and clocks beyond upstream's narrow storage.
- Move validation, foreign objects, stale moves, single-use/LIFO undo,
  wrong-position/clone/reset tokens and traversal-branch reuse.

## Sanitizer results

Configured and built without compiler warnings:

    cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wpedantic" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build/sanitize --parallel 2

The initial CTest run failed both executables at exit because LeakSanitizer
reported that it cannot operate under ptrace in this environment. This was an
instrumentation limitation, not a successful leak check. Retried with only
leak detection disabled:

    ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/sanitize --output-on-failure

Passed 2/2 in 44.06 s, with address and undefined-behavior instrumentation
enabled and no reported errors. Memory leak validation is unclaimed.

## WASM attempt: blocked, not validated

Retried the existing pinned emsdk 4.0.10 installation:

    cd /workspace/emsdk-m0
    ./emsdk install 4.0.10

Exit 1:

    Error: Downloading URL 'https://storage.googleapis.com/webassembly/emscripten-releases-builds/deps/node-v22.16.0-linux-x64.tar.xz':
    <urlopen error Tunnel connection failed: 403 Forbidden>
    error: installation failed!

The enforced environment HTTP host list excludes storage.googleapis.com.
This is consistent with the SDK tunnel denial. No proxy bypass or TLS/verification
relaxation was attempted.

Attempted configuration explicitly:

    EMSDK=/workspace/emsdk-m0 cmake --preset wasm-debug

Exit 1: missing
/workspace/emsdk-m0/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake.
No Emscripten compiler was installed, so compilation and execution could not run.
CMake now registers the same m0_smoke and m1_core sources for native and Node/WASM
runners. Follow docs/development.md after the environment permits the SDK host;
run configure, build and CTest, and record actual results before closing this gate.

## Acceptance status

Native M1 correctness passes. **M1 portability acceptance remains open** until
equivalent WASM golden tests pass. Windows/macOS, browser/Worker, and real-device
mobile validation are also unclaimed. Material-only dead-position recognition
is intentionally conservative; full history-dependent FIDE adjudication is later
work. Stop for review; do not merge or begin M2.
