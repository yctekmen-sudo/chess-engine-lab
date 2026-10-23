I'm continuing my **Chess Engine Lab (CELab)** project from a previous ChatGPT conversation.

Please reconstruct the project context before proposing any new work.

**Project repository:** `yctekmen-sudo/chess-engine-lab` (private GitHub repository).

Read these files from the `main` branch using the connected GitHub tools:
- `AGENTS.md`
- `docs/PROJECT_CONTEXT.md`
- `docs/CURRENT_STATUS.md`
- `docs/development.md`

Also retrieve relevant previous conversation context if available.

### Our established workflow

- We use this ChatGPT conversation for architecture, engineering decisions, planning, performance analysis, and reviewing results.
- Codex in VS Code performs source-code modifications, builds, and testing.
- You provide detailed, ready-to-paste Codex prompts when implementation is authorized.
- Do not commit, push, create pull requests, or merge without my explicit authorization for each action.
- Preserve correctness, exact search semantics, and established performance baselines.

### Current project status

1. **M7.3 is completed and merged.** PR #19 introduced prepared search nodes, dramatically reducing redundant legal move generation while preserving search results.
2. **Project handoff documentation is merged.** PR #20 added `PROJECT_CONTEXT.md` and `CURRENT_STATUS.md`. Main was at commit `bead04a` after these merges.
3. **M7.4 is planned but not started.** Its primary goal is reducing depth-zero move-preparation overhead and related string/allocation/sorting costs.
4. **We are currently migrating all GitHub Actions CI testing to my local Windows 11 PC using WSL2 Ubuntu 24.04.**
5. Local validation has already passed:
   - Linux native Debug, Release, and Profile
   - WebAssembly/Emscripten 4.0.10: 11/11 tests
   - Linux controller: 107/107 tests
   - GUI: 14/14 component tests
   - Playwright: 5/5 browser tests
   - Windows desktop smoke: 2/2 tests
6. **Codex is currently implementing a one-command local CI test runner** with consolidated reporting, Windows/WSL2 coordination, and full validation.
7. GitHub Actions should remain unchanged until the local runner is verified. I want to avoid additional GitHub Actions charges.

### What I want next

First, summarize your understanding of the project's architecture, milestones, performance optimizations, development workflow, and current CI migration status.

Identify anything that needs verification against the latest repository state.

Then continue helping me review Codex's local CI implementation, complete the migration, and eventually proceed to M7.4.

Do not start implementing anything until I request it.