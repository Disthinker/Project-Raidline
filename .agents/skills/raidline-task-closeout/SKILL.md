---
name: raidline-task-closeout
description: Close a completed Project Raidline task with evidence of scope, behavior, files, automated tests, manual acceptance, CI, documentation, technical debt, commit or PR readiness, and a Chinese C++ teaching handoff. Use at the end of every implemented feature, bug fix, controlled refactor, or engineering setup task.
---

# Raidline Task Closeout

## Reconcile the result

1. Re-read the request, final diff, `git status`, active ExecPlan, and `doc/engineering/DEFINITION_OF_DONE.md`.
2. Separate completed scope, deliberately excluded scope, discovered debt, and blocked or unverified work.
3. Confirm no unrelated or user-owned changes were overwritten.
4. Confirm every claimed runtime change is compiled, integrated, and exercised by the named evidence.

## Capture verification

Record exact configure/build commands, focused test result, full CTest result, standalone Python result when applicable, and commit-specific CI state. State `未执行` or `未验证` instead of inferring success.

For manual acceptance, record either the observed result and environment or an exact checklist the user still needs to run. Do not convert a checklist into a pass.

## Update durable records

- Update `doc/project/CURRENT_STATE.md` when the verified implementation state changed.
- Update architecture or invariants only when their canonical facts changed.
- Update the active ExecPlan's progress, decisions, outcomes, and residual work. Move it to `doc/exec-plans/completed/` only when the entire planned result is complete.
- Preserve historical `doc/DevLog_Week*.md`; do not rewrite old branch or CI statements as current truth.
- Update `doc/learning/CXX_LEARNING_LEDGER.md` with concise, evidence-based learning only.

## Create the teaching handoff

Create a Chinese Markdown report in `doc/handoffs/completed/` from `doc/handoffs/CXX_TEACHING_HANDOFF_TEMPLATE.md`. Base it on the real diff, tests, and errors. Cover execution path, design decisions, language and library features, ownership/lifetime, const/reference/value/pointer/move semantics, algorithms and complexity, state or transaction rules, failures and root causes, review questions, debt, locations, and a web-teaching prompt.

Tell the web teaching assistant not to modify project code, to explain before asking questions, to separate familiar/unstable/new knowledge, and to teach through this exact task rather than a detached textbook lecture.

## Deliver the closeout

Report completion, changed files, behavior contract, evidence, manual and CI status, debts, and a precise commit/PR recommendation. Do not commit, push, merge, or create a PR unless the task authorizes that external state change.
