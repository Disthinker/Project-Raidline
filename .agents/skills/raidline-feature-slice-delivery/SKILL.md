---
name: raidline-feature-slice-delivery
description: Prepare concrete Raidline development or Bug plans, execute only approved versions, and report candidates for human acceptance.
---

# Feature Slice Delivery

Root AGENTS supplies the gates. Reuse the active task/ExecPlan and its actual state, plan version and human authorization source; new work follows [PLANS](../../../doc/exec-plans/PLANS.md). Read [short report formats](references/reports.md) only for the relevant plan, Bug or completion mode.

## Plan / Bug investigation

For new content, first obtain planner's correlated single task through project-governor. Inspect actual files, consumers and dependencies before proposing changes. A plan states player goal, scope/exclusions, checked paths and operations/reasons, behavior/contracts/assets, validation/user steps and rollback. Mark genuinely new files “拟新增”. Submit a versioned plan and wait; clarification/plan revision is not implementation approval. A clear human “change X then start” permits that revised scope without repeating the same question.

For every user Bug, including a small accepted-contract regression, diagnose first. Read calls/logs or run safe existing tests; isolate any temporary diagnostic modifications away from real code, saves and assets. Report reproduced/partial/unreproduced, confirmed cause versus hypothesis with paths/functions, minimal reasonable fix and size rationale, affected callers/contracts and regression/rollback. Wait for explicit approval of that version. Rejection permits revised analysis only; do not delegate the rejected fix. Explicit deferral keeps the Bug unresolved and excludes unaccepted work from later delivery.

## Approved implementation

Deliver the smallest complete player flow:
- Start from authoritative state and testable commands; wire runtime and read-only client projections.
- Use stable definitions/IDs and capabilities, not display-name branches. Preserve atomic rejection and asset ownership.
- Add abstractions only with a current consumer. Excluded future systems remain boundaries, not empty implementations.
- Keep directly dependent migrations in one coherent delivery with recoverable snapshots/checkpoints only as already authorized. Do not split ordinary internal work into repeated approval gates. Fix compilation/test/implementation failures within contract; renewed gameplay/public-interface/save/system/art-cost scope needs revised approval.

Run focused regression and required root checks; prepare normal-play steps without launching the game or inventing acceptance buttons. If pre-acceptance commit/push is unauthorized, bind local evidence to a content/tree fingerprint and build and state that exact remote CI has not run.

Report actual paths/behavior/deviations, candidate identity, exact checks (failed/skipped/queued separate), “前置→操作→预期”, correct build/entry/save/scenario, relevant unresolved issues and next suggestion. Record “待用户验收”; freeze that candidate. New material edits require a new identity and acceptance. Explicit current-candidate acceptance routes to project-governor delivery; automated checks do not advance that gate. Leave unrelated defects out of the accepted file set.
