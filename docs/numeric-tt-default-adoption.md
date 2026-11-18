# Numeric TT default for the packaged material evaluator

**Integration candidate**, sourced from post-PR #52 `main` at
`47aef862bd9c92b230fb8701d0a200088915e029`.
Do not label this change merged or authoritative until committed and validated.

## Behavior and trust boundary

The native UCI executable loads its adjacent `material_evaluator` library by
its packaged default path when invoked without an evaluator argument. This
bundled material-v1 evaluator is deterministic on the evaluator ABI snapshot,
so the UCI adapter implicitly enables `EvaluatorCacheCompatibility::deterministic_position`
in that **specific launch configuration**. This compatibility level includes
halfmove/fullmove clocks and raw en-passant data in the TT reuse requirements;
it is **not** the stronger `history_independent_position` optimization.

A nondefault evaluator path, even if it points to the same material provider,
starts **hint-only**. Changing `Evaluator` through UCI resets implicit numeric
certification to hint-only; an explicit later `NumericTT=true` remains a caller
assertion of evaluator compatibility. `NumericTT=false` explicitly opts out.
Changing only `EvaluatorConfig` does not change the provider identity; the
existing evaluator fingerprint continues to namespace numeric entries.

The default executable-adjacent file is trusted by distribution/packaging. This
change **does not cryptographically authenticate the file** and must not be
interpreted as automatic certification of arbitrary external evaluators. The
owner must provide a trusted packaged material artifact. If deployment cannot
guarantee this, choose an explicitly supplied module path and leave numeric
reuse disabled unless compatibility is certified.

This is a **native UCI startup-policy change only**. It does not alter the
public C++ search default (`TranspositionTableUse` without an explicit policy),
TT keying, search implementation, evaluator C ABI, binary layout, WASM build or
controller protocol. A change in default may change search nodes and PVs
relative to the former hint-only default, but same-mode output should be
identical to the original code.

## Prior screening evidence (not integration acceptance)

`CELab-numeric-default-evidence-20261118T000502Z-4fdbf55a.zip` is a local-only
post-PR52 study. Its actual UCI experiments found **16.10% Windows / 15.04%
WSL cold-search time saved** relative to the older hint-only default, across
16 test positions and three paired runs, with 14.65% fewer nodes. No
completed-session score mismatches were observed. Same-mode comparisons
matched search work and PVs. Warm same-position results are artificial reuse
and must not be extrapolated to normal game play; a few cold positions regressed.
The C++ benchmark's numeric settings use `history_independent_position` rather
than the actual UCI's `deterministic_position`, so only actual UCI runs measure
this default-policy effect. No Elo or tournament-strength claim is supported.

## Acceptance and reproduction gates

The integration validation kit must check the **complete** candidate overlay
(including the permanent native UCI regression tests and this documentation),
not just the earlier standalone experiment. Verify source hashes and direct
same-host A/B against exact pre-integration `main` on Windows and WSL, with
all five independent benchmark TT modes and the real UCI defaults, raw timings,
per-position regressions, deterministic completed-depth scores and PVs.
Permanent UCI tests must verify actual numeric TT exact hits for packaged
startup, effective explicit opt-out, external startup hint-only, and reset upon
evaluator replacement followed by explicit opt-in.

This isolated-candidate test does **not** substitute for authoritative local
Full CI. After separately approved commit(s) on a feature branch, run the
54-gate Windows/WSL/controller/GUI/WASM Full CI against the clean committed
candidate; review reference differences before a separately approved push,
PR or merge. Do not change the reference baseline automatically.
