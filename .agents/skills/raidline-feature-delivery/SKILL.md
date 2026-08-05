---
name: raidline-feature-delivery
description: Deliver a Project Raidline gameplay feature, bug fix, or controlled refactor from repository audit through implementation, automated validation, review, documentation, and Chinese C++ teaching handoff. Use for requests to implement, fix, complete, integrate, or safely refactor Raidline code; do not use for read-only questions or art-only production.
---

# Raidline Feature Delivery

## Establish the baseline

1. Read `AGENTS.md`, `doc/project/CURRENT_STATE.md`, the relevant architecture and invariant documents, and any matching domain skill.
2. Inspect `git status`, the current branch, its upstream, the diff from the intended base, and recent commits. Treat all pre-existing changes as user-owned.
3. Trace the current execution path, tests, CMake targets, and historical DevLog evidence. Distinguish present source from compiled, tested, integrated behavior.
4. State the behavior contract, player-visible result, scope, and explicit non-goals before editing.

## Decide whether to plan

Read `doc/exec-plans/PLANS.md`. Create or update an active ExecPlan before implementation when the change spans core classes or systems, changes ownership, needs migration or persistence, introduces raid lifecycle or multi-container behavior, is a major refactor, or needs multiple stages.

Keep the plan live: record discoveries, decisions, test evidence, deviations, and remaining work as implementation proceeds.

## Deliver one coherent slice

1. Use `raidline-explorer` for independent read-only mapping when useful.
2. Have the primary thread decide contracts and boundaries.
3. Use only one `raidline-implementer` for overlapping source files.
4. Preserve SDL/input/rendering adaptation outside the testable gameplay or inventory model.
5. Add or update tests for success, boundary, invalid input, failed transaction, and regression behavior.
6. Add every new source and test to the relevant CMake targets. Confirm the test binary compiles the changed implementation rather than a similarly named older file.
7. Avoid unrelated cleanup, speculative abstractions, or future-week systems.

## Verify and review

1. Follow `$raidline-build-test-ci` for configure, build, focused tests, and full CTest.
2. Use `$raidline-cpp-safety-review` or `raidline-reviewer` for behavior, ownership, lifetime, invalidation, transaction, architecture, and test-gap review.
3. Fix actionable findings with the same implementation owner and repeat affected checks.
4. Run automatable manual validation. For visual or interactive checks, provide exact user steps and leave them `未验证` until performed.

## Close the task

Use `$raidline-task-closeout`. Update current-state, architecture, invariant, and active-plan documents only when the facts changed. Preserve DevLogs as history. Create the required Chinese C++ handoff under `doc/handoffs/completed/`.

Report the actual result, files, behavior contract, exact verification evidence, manual acceptance status, CI status, technical debt, and commit/PR recommendation. Never claim a skipped check passed.
