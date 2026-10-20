# M0 validation and change inventory

M0 scaffolding is implemented for review. M1 has not started. The approved
architecture and kickoff documents are unchanged. No chess rules, evaluator,
search, UCI, GUI, or experimental quiescence algorithm has been implemented.

## Results

Local platform: Linux x86-64, GCC 14.2.0, CMake 3.31.6, Ninja 1.11.1.

| Check | Result |
| --- | --- |
| `cmake --preset native-debug` | Passed |
| `cmake --build --preset native-debug` | Passed: static library and smoke executable |
| `ctest --preset native-debug` | Passed: 1/1 (`m0_smoke`) |
| `cmake --preset native-release` | Passed |
| `cmake --build --preset native-release` | Passed: static library and smoke executable |
| `ctest --preset native-release` | Passed: 1/1 (`m0_smoke`) |
| External `/tmp/chess-engine-lab-build` configure/build/CTest | Passed: 1/1 using development guide commands |
| Separate `-DBUILD_TESTING=OFF` configure/build | Passed: library only; tests intentionally not built/run |
| Emscripten SDK 4.0.10 installation | Blocked: HTTP 403 fetching SDK Node archive from `storage.googleapis.com` |
| WASM build/execution | Not run: no installed Emscripten compiler; portability is unvalidated |
| GitHub Actions | Workflow prepared; remote run not observed |

Smoke coverage: public contracts compile, opaque ownership/destruction and
snapshot constness are checked at compile time, and a linked core function
returns the expected project version at runtime. Release uses an explicit
failure return, so `NDEBUG` cannot remove that check. These results demonstrate
the M0 build pipeline, not chess correctness or a stable evaluator ABI.

The SDK download failure is an environment network restriction, not a native
build failure. The pinned WASM setup and rerun commands are in
`docs/development.md`. Permit `storage.googleapis.com` in environment network
settings before retrying. Windows/macOS/mobile and browser/Worker behavior
remain untested; no cross-platform CI results are claimed.

## Change inventory

Modified:

- `README.md`: project state, quick start, layout, scope and documentation links.

Added:

- `.gitignore`: generated builds and local configuration exclusions.
- `CMakeLists.txt`: C++20 static core, linked smoke target, native/Node CTest paths.
- `CMakePresets.json`: native Debug/Release and Emscripten Debug presets.
- `.github/workflows/native.yml`: Ubuntu native configure/build/CTest baseline.
- `engine/include/chess_lab/build_info.hpp`: core metadata declaration.
- `engine/src/build_info.cpp`: minimal linkable core metadata implementation.
- `engine/include/chess_lab/chess_core.hpp`: opaque types and abstract M1 contract.
- `engine/include/chess_lab/position_snapshot.hpp`: read-only source snapshot contract.
- `tests/m0_smoke.cpp`: contract checks and linked runtime smoke executable.
- `docs/development.md`: prerequisites, variants, commands, WASM blocker and CI scope.
- `docs/m0-contracts.md`: ownership, provisional interfaces, M1/M2 boundaries.
- `docs/m0-validation.md`: this evidence and file inventory.
- `THIRD_PARTY_LICENSES.md`: license inventory placeholder; original code unlicensed.
- `third_party/README.md`: immutable dependency pinning and notice policy.
- `evaluators/README.md`: M2 placeholder and evaluator boundary.
- `controller/README.md`: M6 placeholder and controller responsibilities.
- `gui/README.md`: M7/M8 placeholder.

Generated native artifacts are under ignored `build/` or `/tmp`; the attempted
SDK installation is outside the repository. No third-party chess library was
downloaded or added. Review M0 before authorizing M1.
