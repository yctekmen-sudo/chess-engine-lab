# Evaluator placeholder

M0 defines only the read-only source-level snapshot in
`engine/include/chess_lab/position_snapshot.hpp`. No evaluator is implemented.
The versioned C ABI, schema/version constants, ownership, error handling, and
independently compiled modules belong to M2. C++ virtual objects must never be
passed across that future ABI. Evaluation will be finite and White-positive;
history-dependent adjudication belongs outside evaluators.
