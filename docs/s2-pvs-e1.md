# S2.1 PVS and E1-C1 production candidate

Baseline: merged PR #56 `f0145804513740993fd9b4543e7d29716327952a`.
Accepted experimental sources and the original E1 deep-puzzles evidence were
recovered in `CELab-accepted-stack-S21-E1C1-v1.zip`.

PVS is enabled for the native adapter, with alpha-beta, depth > 1, finite
incumbent and existing active-bound guards. A scout crossing the incumbent
is re-searched at the original window, and the pre-existing exact-PV refinement
remains. Synthetic adapters retain their prior behavior. The S2.1 delta-pruned
subset returns the appropriate conservative search-window bound instead of an
unsupported tighter fail-soft result. No S2.2 aspiration changes are included.

The native `material_pst_evaluator` shared library uses the exact accepted
E1-C evaluator implementation (including phase-tapered king tables), ABI v2,
module ID `org.chesslab.experimental.e1.materialpst`, version 1.0.0, configuration
`{}`. Its existing identity is retained intentionally. Select it by absolute
library path through the existing evaluator selection interface or native UCI
launcher. Material remains the native no-argument default, with NumericTT enabled by the
existing deterministic-position certification. Explicit module paths and replacements
still require explicit NumericTT certification. WASM `EvaluatorModule::bundled(sha)`
or `bundled("default", sha)` selects material; `bundled("material-v2", sha)` retains
the same material identity. `bundled("material-pst-v1", sha)` selects C1 explicitly.
No evaluator C ABI or score-unit change. Existing explicit registrations and material
weight configurations retain their meaning. The user selected material as the primary
S3 baseline after the v2 WSL measurements showed C1 increased search effort with
mixed tactical results. C1 is retained for secondary validation; no playing-strength
superiority is claimed.

The private registry resolves once per traversal: exact default material
identity (policy 17) and exact material+PST identity (policy 18) use SEE <= 0 cp
and delta margin 0 cp. Unknown identities, versions, ABI majors and configurations
use neither pruning rule. Registry matching is a compatibility convention, not
an authenticity guarantee. Removed the experimental C0/C2/C3/C4/C5 selectors;
PVS and delta-bound repair no longer require process-start switches. The optional
`CELAB_E1_QPOLICY_TRACE` diagnostic is retained. Neither the 5 cp diagnostic
margin nor the Arasan #8 capture exception is included. Window-dependent delta
pruning explains the accepted 1 cp PVS discrepancy in that position; this is
not a claim of exact equivalence across all heuristic search windows.

The material+PST module is built in standard native CMake builds and linked as
a separate static bundled provider without a duplicate dynamic ABI entry point.
COMP1 target scope includes both dynamic modules, and the independent training
corpus runs once with material default and once with explicitly selected C1.
Native static C1 bundled-provider objects are outside PGO scope. Standard and COMP1 paired validation passed on Windows/WSL for v3; see the
[pre-commit review](s21-e1c1-precommit-review.md). The table-only extraction
requires focused deterministic confirmation. Clean committed Full CI and
explicit reference review remain pending.
No commit, push, PR, or merge authorization follows from this document.
