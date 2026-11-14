# C1 + C2 + C3 integration: comparison to origin/main

**Status (2026-11-14): DRAFT / pre-integration direct A/B not yet performed.**
The `origin/main` reference is frozen at
`072032c0d086dbe37a5b7afd796424b283f0ba53` (merged T1, tree
`dc1d2d03482fc8c1401ed3172c3c0b18a680bd23`).
This document is the draft PR performance comparison; **do not treat the
indicative cumulative percentages below as directly measured integration
speedups**. The final head needs same-host direct A/B before merge.

## Accepted, exact source stack

- **C1:** packed 64-bit `CompactUciText` comparisons; avoid re-sorting
  canonical prepared move lists; direct promotion-byte checks.
- **C2-A:** checked `typeid` + `static_cast` for final native types,
  preserving invalid-object rejection.
- **C2-B:** bounded accumulator for evaluation snapshot bitboards, preserving
  validation and the evaluator C ABI.
- **C3-A:** side, castling, legal EP before the 64 squares.
- **C3-B:** C3-A ordering with `memcmp` of exactly the 64 live, uniquely
  represented `PieceCode` bytes; this **supersets** C3-A and is the single
  final implementation.

The five source blobs in the draft branch exactly match the C3-B experimental
overlay in `CELab-C3-evidence-c3952dd8.zip` (verified Git blob identities).
No rejected TT or malformed-move patch from Claude is included.

## Preliminary origin/main comparison: chained A/B ratios (NOT direct A/B)

Each stage used the same nine base workloads, five TT policies, and three
fresh-process runs per workload/mode/variant on a common platform, measuring
**sums of nine per-workload median elapsed times**. Ratios are measured *within*
each stage: T1 -> C1; C1 -> C1+C2; C1+C2 -> C1+C2+C3-B.
The displayed percentages multiply those three independent ratios:
`100 * (1 - R_C1 * R_C2 * R_C3B)`.

Because these were **different sessions** and not a single contemporaneous
T1-vs-final-stack run, hardware conditions, compiler/linker and run-to-run
variance can bias this calculation. It is a directional, **inferred comparison**,
not the required authoritative integration benchmark.

| TT mode | Windows inferred time saved vs origin/main | WSL Linux inferred time saved vs origin/main |
|---|---:|---:|
| Off | ~16.55% | ~18.42% |
| Hint-only cold | ~16.71% | ~20.82% |
| Hint-only warm | ~19.11% | ~26.35% |
| Numeric cold | ~15.05% | ~20.64% |
| Numeric warm | ~18.33% | ~23.58% |

The nine common workloads are Initial d7, Italian d5, Kiwipete d6
(8 MiB and 64 MiB), Perft5 Checks d5, Perft6 Middlegame d5,
QueenMate d5, Sicilian d5, and ThreefoldReplay d3.
C3 also tested TwofoldReplay d4 and LongHistory16 d4, which are
**excluded from these nine-workload inferred cumulative ratios**.

The warm numeric mode is a synthetic same-root prepopulated TT and is *not*
a representative gameplay throughput estimate. Positive values represent lower
elapsed time; no claim is made of improved playing strength or node reduction.

## Measured incremental performance and audit provenance

| Exploratory stage | Windows TT-off saving | Windows numeric-cold saving | Linux TT-off saving | Linux numeric-cold saving |
|---|---:|---:|---:|---:|
| C1 combined vs T1 | 8.35% | 7.68% | 9.15% | 8.84% |
| C2 combined vs C1 | 6.44% | 5.00% | 5.29% | 8.34% |
| C3-B vs C2 (11 workloads) | 2.68% | 3.15% | 5.21% | 5.04% |

Evidence (raw logs, manifest hashes, source checksums, tests and
per-workload medians): `CELab-C1-evidence-c169e35d(4).zip`,
`CELab-C2-evidence-c252a530(1).zip` and
`CELab-C3-evidence-c3952dd8.zip`. They are external archived experimental
evidence, not GitHub CI attachments.

- C1 exploratory Windows/WSL native and 945/945 benchmark observations passed.
- C2 exploratory Windows/WSL native and 945/945 benchmark observations passed.
- C3 exploratory Windows/WSL native and 990/990 benchmark observations passed.
- All stage-local comparable results preserved best move, PV checksum, nodes,
  evaluator calls and TT statistics. No final-stack-vs-main full direct
  comparison has yet been observed.
- **Adverse case**: Windows Perft5 Checks hint-cold C3-B vs C2 was ~10.22%
  slower by three-run median (295.03 -> 325.18 ms) while Linux improved.
  C2 Windows Perft5 hint-cold and numeric-cold also regressed on their
  stage-local medians; retain these explicit watchpoints.

## Mandatory direct integration benchmark and release gate

Before marking this draft PR ready for review/merge:

1. Check out the **exact frozen `origin/main` commit** and **the exact
   committed PR head** in separate, clean, short-path source trees. Confirm
   HEAD/tree, source/patch hashes, toolchain, build type and evaluator binary.
2. Run fresh native Release builds and complete relevant CTest suites on both
   Windows 11 and WSL Ubuntu 24.04. Reuse the **same evaluator binary** between
   baseline and candidate on each platform.
3. Run directly paired/interleaved **all five TT modes**: off, hint-only
   cold/warm, numeric cold/warm, with at least the original nine workloads,
   plus the two additional history controls. Three or more independent
   process runs per variant/workload/mode, raw wall/CPU time and effective NPS.
4. Require equal nodes, evaluator calls, full PV/checksum, legal best moves,
   and relevant TT metrics; publish per-position medians/negative outliers,
   particularly Windows Perft5 cold modes. Retain complete raw logs and source
   manifests; do not aggregate sub-millisecond warm timing as gameplay speed.
5. After separately authorized **clean committed Full CI**, run the existing
   model-free `scripts/validation/full-ci.ps1 -PlanOnly` and then one 54-gate
   `scripts/validation/full-ci.ps1` invocation. Review any
   `reference_review_required` outcome rather than automatically replacing
   the historical reference. No GitHub-hosted CI or unapproved merge.

No direct benchmark result or Full CI status is claimed by this document.
