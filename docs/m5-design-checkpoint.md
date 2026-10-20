# M5 pre-implementation design checkpoint

Base: approved main `f643bf116d4c2d997173dd46efbee9f3d379d0e7`.
Branch: `m5/transposition-table`. Scope: Decisions 67–83 only.

- Public C++ `TranspositionTable(bytes)` is noncopyable; idle-only `clear()` and
  `resize(bytes)`, actual `capacity_bytes()` and entry reporting. A per-instance
  atomic exclusive guard rejects second active use and mutation while active.
- Append defaulted `TranspositionTableUse` to `iterative_search`, containing
  table pointer, conservative `EvaluatorCacheCompatibility` certification, and
  an explicit value-policy namespace. Existing M3/M4 callers and the C ABI stay
  unchanged. Exhaustive mode bypasses all table access, including hints/guard.
- One fixed allocation contains entries, interned history-prefix nodes and their
  index/free-list storage. Metadata allocation is included in enabled capacity;
  tiny budgets disable storage without allocating. No retained strings, vectors,
  unbounded history maps, or hidden context caches. Prefix nodes share history
  evidence across depths/entries/searches and are reference-counted. Entry
  eviction releases unreachable prefixes. If pinned history exhausts capacity,
  decline numeric caching rather than exceed budget.
- The invocation maintains an exact occurrence-count map plus an incremental
  commutative fingerprint. Each make/undo changes one count/hash contribution.
  A fingerprint candidate must pass complete evaluator-visible snapshot,
  normalized signature, completeness, evaluator fingerprint, policy and equal
  remaining-depth checks. Only candidate hits reconstruct stored prefix evidence
  into a transient count map for exact comparison, with cancellation polling.
  Verification is O(recorded occurrences log distinct signatures); no-hit key
  maintenance is O(log distinct signatures), without full-history rescans.
- Bucket replacement chooses deterministically by coarse recency, remaining
  depth, precise recency and slot index. Multiple depths can coexist. Memory
  pressure on the shared history pool may evict globally using the same rule.
- Store only completed node outcomes. Classify EXACT/LOWER/UPPER against the
  original incoming alpha/beta window, before local claims/probes. Mate storage
  subtracts current ply; lookup adds it, checked in both directions for either
  winner. Decline invalid ancestor-relative bounds/overflow.
- Exact root candidates remain independent full-window searches. A numeric hit
  is not proof of a canonical PV. Revalidate terminal/claim legality; leaf hits
  can prove an empty endpoint. Internal played lines lacking a complete proof
  are re-searched. Bound-only returns stay in score-only traversal; promising
  moves and canonical earlier ties are refined before publication. TT hints are
  validated against current legal plays and never decide ties by visitation.
- Extend statistics with lookups, prefilter candidates, verified exact/bound
  hits, used hints, rejects, stores, evictions, PV re-searches and history evidence
  inspected/verification time. Preserve existing counter meanings.
- Cancellation retains only completed subtree writes, with unchanged depth
  publication. On search/evaluator error, search reports/invalidate as before;
  the controller-style owner clears only its attached table after return.

Expected costs: constant bounded arena overhead, incremental count-map/hash
maintenance, reference-counted prefix interning, candidate-hit exact verification
and conservative PV re-search. Warm numeric leaf hits should reduce evaluator
calls; exact-history requirements can eliminate many ordinary board-only
transpositions. No speedup target or weakened PV/history equivalence is assumed.

Implementation refinement: candidate verification reconstructs stored counts into
scratch counters in the invocation's already maintained count-map nodes. This
keeps O(H log U + U) verification while eliminating a separate temporary map
allocation. The table's owned memory remains the fixed bounded arena/metadata.
