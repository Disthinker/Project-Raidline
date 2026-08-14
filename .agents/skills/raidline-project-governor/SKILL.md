---
name: raidline-project-governor
description: Control Project Raidline scope, dependencies, branches, release evidence, commits, pushes, and pull requests for product delivery.
---

# Raidline Project Governor

## Establish the delivery baseline

Read `AGENTS.md`, the active scope contract, `doc/project/CURRENT_STATE.md`, `doc/project/KNOWN_ISSUES.md`, `doc/project/ROADMAP.md`, and the active ExecPlan. Fetch and report branch, HEAD, `origin/main`, worktree state, dependency PRs, and CI before writes.

## Control scope and dependencies

- Map every requested change to the active product slice and its explicit exclusions.
- Treat an open dependency PR as a dependency, not as code to duplicate or silently cherry-pick.
- Escalate only changes to product pillars, failure loss, business model, narrative direction, or material scope. Resolve ordinary architecture, numbers, and acceptance details within the active contract.
- Record GDD/code conflicts without editing the external GDD repository.

## Control delivery

- Use one focused `codex/...` branch from the accepted baseline.
- Require a self-contained ExecPlan, automated evidence, relevant manual acceptance, risk notes, and rollback before marking the slice complete.
- Prefer coherent commits with explicit contract changes. Push and open a reviewable PR; do not auto-merge, force-push, or rewrite shared history.
- Report verified, paused, pending, blocked, and deferred items separately.

## Close by evidence

The closeout contains build identity, tests, manual checks, deviations, known risks, commit, branch, PR, and dependency state. Teaching summaries and learning ledgers are not required.
