# Third-party dependency policy

M1 vendors Disservin/chess-library behind our own adapter. See
[chess-library/README.md](chess-library/README.md) for the immutable pin.
Configure, build and runtime require no dependency downloads.

For each dependency, pin a full immutable commit (and archive checksum when
using an archive), record its origin/revision/license in
`THIRD_PARTY_LICENSES.md`, and retain all required notices. Do not track mutable
branches or silently update pins. Validate the selected revision on native and
WASM before claiming portability. Prefer vendoring reviewed sources or an
explicit preparation step so configure/build and runtime can remain offline.
