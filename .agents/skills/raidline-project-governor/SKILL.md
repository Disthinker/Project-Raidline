---
name: raidline-project-governor
description: Coordinate Raidline next-task selection, restore approval state, and close accepted deliveries through the existing three threads.
---

# Project Governor

Use root AGENTS.md for human gates. Read CURRENT_STATE and the relevant ExecPlan before choosing any action. Use [real thread map and transport](../../../doc/coordination/THREADS.md) when coordinating, not same-name custom workers.

1. Identify task/state/plan version/candidate and actual human instruction. Resume an approved unfinished task inside scope. Waiting for plan/fix approval or human acceptance remains waiting after “continue”; explain the missing gate. Explicit pause/skip preserves true state and isolates later work.
2. With no approved active task, or an explicit next-task request, send latest accepted facts/issues/dependencies to the existing planner. Validate the correlated reply, take its single task, inspect real code, then use feature-slice-delivery to prepare the plan. Planner advice never approves implementation. Art queries before approval are read-only; no art need means no art call.
3. Before baseline/release actions, fetch and verify branch, HEAD, worktree, accepted main, dependency PR and exact-head CI. Do not copy an unaccepted dependency into a delivery. Ordinary choices inside an approved contract are autonomous; material changes return to approval. External GDD stays read-only.
4. Explicit acceptance of the unchanged current candidate authorizes agreed closeout/commit/merge without another routine merge question. Verify the exact accepted file set/tree, target and necessary tests/dependencies. Honor existing pre-acceptance checkpoint restrictions; at this baseline there is no standing authorization for this maintenance's commit/push/PR. Configured origin is not remote authorization. Record local commit/merge and remote push/PR merge separately; preserve the authorized strategy and unrelated dirt.
5. Required CI/dependency/conflict/permission problems leave “accepted, delivery blocked”; never bypass, claim merged or materially change the accepted candidate. Retry only still-valid authorization. Consult existing commit/PR/task evidence before repeating delivery. Sync planner/necessary art after actual delivery, report identifiers/evidence and suggest the next player result; do not start it. No teaching ledger or extra CI-only commit.

Keep the small state/version/approval fields in the existing task record using [PLANS](../../../doc/exec-plans/PLANS.md). A coordination tool receipt is not the target's result. Keep wrong-version replies as history; do not use them to advance state. If native transport is unavailable, report that limitation and stop dependent selection instead of inventing planner output.
