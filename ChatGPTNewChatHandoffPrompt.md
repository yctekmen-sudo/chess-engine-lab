# Chess Engine Lab — New ChatGPT Conversation Handoff

I'm continuing my **Chess Engine Lab (CELab)** project from a previous ChatGPT conversation.

Please reconstruct the project context before proposing any new work.

## 1. Repository and project documentation

**GitHub repository:** `yctekmen-sudo/chess-engine-lab` (private).

Use the connected GitHub tools to inspect the repository.

Read these files first:

- `AGENTS.md`
- `docs/PROJECT_CONTEXT.md`
- `docs/CURRENT_STATUS.md`
- `docs/development.md`
- `docs/local-ci.md` (if available)

**Important:** Do not assume `main` contains the latest work.

If an active feature branch contains work not yet merged into `main`, inspect that branch and its documentation as well. In particular, check `feature/local-ci-infrastructure` until its PR has been merged.

Check current Git history, branches, pull requests, and repository documentation before relying on the historical status below.

Also retrieve relevant context from previous ChatGPT conversations if available.

## 2. Our established development workflow

We deliberately separate architectural reasoning from implementation.

**ChatGPT (this conversation):**
- Architecture, engineering decisions, design discussions, and planning.
- Performance analysis and interpretation of experimental results.
- Reviewing Codex implementations and identifying correctness or performance risks.
- Providing detailed, ready-to-paste implementation prompts for Codex.

**Codex in VS Code:**
- Implements approved designs.
- Modifies repository files.
- Runs builds, correctness tests, performance benchmarks, and local CI.
- Prepares changes for review.

**GitHub:**
- Hosts our source repository, branches, pull requests, and project history.
- Is being retained for source control and review while GitHub-hosted CI is being replaced by local testing.

**Authorization rules:**
- Do not modify source code unless I request implementation.
- Do not commit, push, create a PR, or merge without my explicit authorization for that specific action.
- Approval to implement is not approval to publish or merge.
- Preserve unrelated files and user-authored changes.
- Preserve chess correctness, exact search semantics, deterministic experimental behavior, and established performance baselines.

## 3. Project architecture

Chess Engine Lab is intended to become a platform for meaningful, reproducible chess-engine experiments.

Its main components are:

- **Native chess engine:** C++20 chess rules, position representation, evaluation, and search.
- **Search subsystem:** Alpha-beta search, transposition tables, iterative deepening, MultiPV, cancellation, and history-aware draw handling.
- **Evaluator architecture:** Pluggable evaluation providers with a versioned C ABI.
- **WebAssembly:** Shared engine functionality compiled and tested with Emscripten.
- **Controller:** Node.js 24 / TypeScript, managing authoritative games, clocks, engines, experiments, persistence, and UCI subprocesses.
- **GUI:** React/Vite application supporting gameplay, analysis, engine registration, and experiments.
- **Testing:** Native CTest suites, controller integration tests, GUI component tests, and Playwright browser tests.

The repository documentation is authoritative for detailed contracts and implementation status.

## 4. Completed milestones

**M7.3 — Prepared Search Nodes**

- Completed and merged in PR #19.
- Eliminated a large amount of redundant legal-move generation.
- Preserved exact search decisions, scores, canonical principal variations, draw semantics, and search-node counts.
- Achieved approximately a ninefold speedup in the measured depth-six, transposition-table-disabled initial-position benchmark.

**Project handoff infrastructure**

- Completed and merged in PR #20.
- Introduced persistent architectural and status documentation.
- `AGENTS.md` directs Codex to consult and maintain these documents.

## 5. Current project status — October 23, 2026

The project is migrating away from GitHub-hosted CI to local validation on my Windows 11 computer using WSL2 Ubuntu 24.04.

**Local CI implementation:**

- Branch: `feature/local-ci-infrastructure`
- Validated commit: `2a1949f132646f1d157e912aca893bad6d87fdcb`
- Includes a one-command Windows/WSL2 validation harness.
- Includes reporting, source identity checks, failure propagation, coverage safeguards, and regression tests.

**Verified authoritative validation:**

- All 54 command gates passed.
- Windows and Linux validated the same clean, unchanged Git commit.
- Windows native Debug, Release, and Profile tests passed.
- Linux native Debug, Release, and Profile tests passed.
- WebAssembly/Emscripten 4.0.10 tests passed.
- Windows and Linux controller tests passed.
- Windows and Linux GUI tests passed.
- Linux Playwright browser tests passed.
- Windows desktop smoke tests passed.
- Local CI harness regression tests passed.
- Combined report: `Verdict: passed`.
- `Authoritative full pass: true`.
- Process exit code: `0`.

The validation evidence was recorded at:

`C:\cursor\chess-engine-lab\work\ci\20261023T031617205Z-d867a671`

This path refers to local-only evidence on my computer.

**At the time of this snapshot:**

- The local CI infrastructure was committed but not pushed.
- No infrastructure PR had been created.
- GitHub Actions workflows had not been disabled.
- The transition to local CI was technically validated but not yet administratively completed.

Treat all these statements as historical facts tied to the specified date and commit. Verify subsequent changes before proceeding.

## 6. Immediate next steps

Our current priorities are:

1. Finalize the local CI documentation and handoff prompts.
2. Review and commit any remaining documentation changes with explicit authorization.
3. Run authoritative local CI against the final committed revision.
4. Push the infrastructure branch and create a PR when authorized.
5. Review and merge the PR when explicitly approved.
6. Disable GitHub-hosted CI workflows and reconcile GitHub branch protection requirements.
7. Update the project documentation to reflect the completed migration.
8. Return to M7.4.

## 7. Planned engineering work

**M7.4 — Search performance optimization**

The next planned optimization focuses on lazy depth-zero move preparation and reducing unnecessary move encoding, string construction, allocation, and sorting overhead.

The objectives are improved search performance without changing externally observable search behavior or correctness.

Preserve the established M7.3 performance measurements as the comparison baseline.

M7.4 has not started as of this snapshot.

**Longer-term research direction**

Once Chess Engine Lab supports robust, meaningful experiments, I want to investigate neural-network-based chess engines.

Possible research includes:

- Classical versus neural-network evaluation with identical search algorithms.
- NNUE-style evaluation.
- Comparisons with neural-network/MCTS engines.
- Training our own neural evaluators.
- Measuring playing strength, computational cost, search efficiency, and statistical significance.

These are future research interests, not authorization to implement them now.

## 8. What I want from this new ChatGPT conversation

Before proposing or implementing anything:

1. Read the repository's current documentation and relevant active branches.
2. Recover pertinent earlier conversation context when available.
3. Summarize your understanding of the architecture, completed milestones, current state, and development workflow.
4. Distinguish historical snapshots from the latest verified repository state.
5. Identify any outstanding decisions, discrepancies, or issues requiring attention.
6. Recommend the next logical step based on the actual current project state.

Continue our established collaborative approach: **we reason and design together in ChatGPT; Codex implements and tests; we review results before integration.**

Do not start implementing anything until I explicitly request it.