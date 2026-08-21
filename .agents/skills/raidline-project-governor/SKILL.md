---
name: raidline-project-governor
description: Route Project Raidline tasks, classify risk, control scope/dependencies, and govern branches, evidence, commits, pushes, and pull requests.
---

# Raidline Project Governor

## Establish the smallest sufficient baseline

Read `AGENTS.md`, run `python tools/raidline_governance.py preflight --task "<request>"`, then load only the routed primary domain context and current task envelope/ExecPlan. Fetch and report branch, HEAD, `origin/main`, worktree state, dependency PRs, and relevant CI before writes. Load project state, roadmap, architecture or history only when the routed task actually needs them.

## Control scope and dependencies

- Create a short `*.task.toml` from the repository template. Map every change to its allowed paths, protected domains, risk, invariants and explicit exclusions.
- Treat an open dependency PR as a dependency, not as code to duplicate or silently cherry-pick.
- If implementation crosses the envelope, stop editing that area until the envelope and risk/evidence gate explicitly expand.
- Escalate only changes to product pillars, failure loss, business model, narrative direction, or material scope. Resolve ordinary architecture, numbers, and acceptance details within the active contract.
- Record GDD/code conflicts without editing the external GDD repository.

## Control delivery

- Use one focused `codex/...` branch from the accepted baseline and one Primary Writer for tracked business files.
- Require a self-contained ExecPlan, automated evidence, relevant manual acceptance, risk notes, and rollback before marking the slice complete.
- Prefer coherent commits with explicit contract changes. Push and open a reviewable PR; do not auto-merge, force-push, or rewrite shared history.
- Report verified, paused, pending, blocked, and deferred items separately.

## Close by evidence

Run postflight against the envelope. The closeout contains actual blast radius, build identity, layered tests, manual checks, deviations, known risks, commit, branch, PR, and dependency state. Transient PR/CI facts stay in the PR rather than being copied into stable project documents.
