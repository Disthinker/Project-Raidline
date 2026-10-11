# Project Raidline

## Product and sources

Independent game, not a weekly teaching project. Target Windows PC, keyboard/mouse, offline single-player; keep core code portable for Linux build/test CI, not a simultaneous Linux release.

Follow explicit user decisions and authorization, then the active scope/ExecPlan. Use local code, tests and reproducible evidence to establish implementation facts, not to override product decisions. Distinguish accepted main, unmerged work, CI evidence and user acceptance.
- Start with `doc/project/CURRENT_STATE.md` for current work; consult the relevant active plan, `KNOWN_ISSUES.md` or `ROADMAP.md` only as needed.
- External GDD is read-only product input. Report conflicts; do not silently rewrite it.
- Alpha and Week plans are historical contracts, not current feature bans. Stage scope belongs in the active plan; do not restart old milestones.

## Three threads and approval · RL-COLLAB-20261011-v1

- Preserve the existing persistent threads: planner chooses the next content task and player criteria; main-controller inspects code, proposes, implements and integrates; art-controller produces/QA/hands off explicitly authorized packages. Main is the user's development, Bug and acceptance entry. External GDD stays read-only; planner cannot edit game code or approve delivery. Temporary agents are optional scoped workers, not replacement long-lived threads or extra approval layers.
- Restore state from CURRENT_STATE and the relevant task record first. “Continue/next” resumes an approved unfinished task or requests one next task from planner and prepares a plan. It never supplies missing plan approval, fix approval or human acceptance. An explicit skip/pause preserves unfinished work and isolates the next task.
- Before feature implementation, inspect real files, present a versioned concrete plan and wait for the user's explicit approval of that task/version. Plan revisions/discussion alone do not authorize execution. A clear “change X then execute” approves the revised scope; do not ask again for the same approval. Read-only investigation is allowed while waiting; no game edits, asset production or commits of a pending proposal.
- Every user-reported Bug, however small, starts diagnosis and a versioned fix plan, then waits for user approval. Isolate temporary diagnostic changes; do not write the fix to the real checkout before approval. A rejected plan cannot be executed via another thread. Within approved implementation, ordinary compiler/test failures may be corrected autonomously; material gameplay/interface/save/system/art-cost expansion needs renewed approval.
- Completion identifies the candidate commit/tree/build, actual checks and normal-use acceptance steps. Automated tests, planner replies, tool permissions or self-authored “approved” notes are not human approval/acceptance. Freeze the candidate while awaiting acceptance; material changes invalidate its previous acceptance. Track actual human instruction and task/version in the existing task record.
- Explicit human acceptance of the current candidate authorizes agreed closeout/commit/merge; do not habitually request a second merge approval. Verify candidate/diff/dependencies/target/checks and deliver only accepted scope. Pre-acceptance commit/push/PR requires already-existing explicit authorization; this maintenance and remote configuration grant none. Record local commit/merge, remote push and PR merge separately; use only an already-authorized remote strategy. A necessary unresolved gate means “accepted, delivery blocked”; retry valid authorization idempotently without bypassing checks. After delivery sync roles as needed, suggest but do not start the next task.
- Use the real thread mapping and native transport in [THREADS](doc/coordination/THREADS.md). Replies must match request/task/version/baseline; messages say “主控协调请求”, never impersonate human approval. Handoff files are evidence, not wakeups. Do not drive a running thread through multiple entry points. Roles and on-demand skill routes are linked there; plan/report/Bug formats are linked from [PLANS](doc/exec-plans/PLANS.md).

## Working and delivery

- Inspect branch, HEAD and worktree before edits; preserve existing changes. Fetch and check `origin/main`, dependency PRs and CI before choosing a baseline or performing release work, not repeatedly for every local check.
- Use one coherent slice/fix per `codex/...` branch from its accepted dependency, normally latest `origin/main`. Do not implicitly build on, copy or cherry-pick another open feature. Keep directly dependent migrations together with reviewable rollback points.
- Once the task plan is approved, complete authorized implementation and relevant checks without repeated approval for internal steps. Ask for product-pillar, failure-loss, business-model, narrative or material scope changes.
- No unrelated edits, shared-history rewriting, force push or merge without explicit authorization. Commit/push/PR actions must be within the requested delivery scope; local maintenance alone does not authorize them.
- Reuse the active ExecPlan; consult `doc/exec-plans/PLANS.md` for new work. A small Bug plan may live in the existing task record, but its user approval gate still applies. Skills and agents are on-demand tools, not a mandatory review pipeline.
- Close with what changed, actual verification, remaining risks and a concrete next step/player goal. Include revision/PR/rollback details when delivering code; never label automated checks as human acceptance. Do not create another revision just to copy CI status into docs.

## Architecture invariants

Use `doc/architecture/INVARIANTS.md` for affected contracts and `ARCHITECTURE.md` for ownership/call paths.
- App maps input and renders projections; legality, damage, hit regions and settlement come from domain results, not UI names, colors or animation.
- Keep the modular monolith: `raidline_domain`, `raidline_simulation`, `raidline_services`, `raidline_sdl_client`; domain/simulation stay SDL-free. Do not add ECS, service locator, global event bus or speculative framework.
- One AssetRegistry owns each asset instance and its explicit location. Definitions are immutable/shared; instances use stable IDs and established move-only ownership. Queries are non-mutating; rejected commands preserve all participants; multi-owner changes commit atomically.
- Use stable IDs across Enemy/Combat and asset boundaries, not vector indexes. Never retain vector references/iterators across structural mutation or transaction replacement.
- Share Enemy/Nav/Combat lifecycle mechanisms, not activity policies. Daily, Defense and Raid keep spawn, targets, waves, rewards, loss and save policies. Runtime owns transient cleanup; GameSession consumes activity/profile/settlement facts, not every internal event. A facade is not a new all-purpose owner.
- Preserve `ShotCommand -> ShotResolution -> HitResult`: short logical flight records, not renderable/collidable projectile entities.
- Persist stable namespaced IDs, relationships, ID high-water marks and versioned DTOs, never pointers/UI cells/display names/runtime indexes. Migrations are explicit; deploy/settlement remain idempotent. Do not change schema without a data-contract need.
- Preserve current death/active-quit loss and pre-Raid Profile recovery on abnormal exit; the latter remains a documented long-term design conflict, not permission to redesign failure policy. No public mod/hot-reload API without a consumer and product decision.

## Validation

- C++/CMake changes: use the repository `windows-debug` preset (Ninja, Debug, x64-windows), x64 VS Developer Shell and UTF-8. Commands/setup are in `doc/engineering/BUILD_AND_TEST.md`.
- Incrementally build and run focused tests; header/class-layout changes rebuild every affected target. For `gtest_ar_` stack corruption, inspect Ninja dependencies and stale objects rather than treating old binaries as evidence.
- Full registered CTest is required before code push; C++-affecting PRs need exact-head Windows/Ubuntu CI. Docs/instruction-only maintenance uses format, references and relevant configuration checks, not an unrelated full C++ cycle.
- Give the user one normal-play acceptance checklist after automation/CI. Do not launch the game on their behalf or add single-step test buttons as gameplay substitutes.

## Art and audio

Check the latest package authorization. Formal game art, Grab/Scratch/Bite animations and new audio outside the authorized `assets/audio/v1` P0 bank have no renewed production/publication authorization at this maintenance baseline. Reopen a package only through an explicitly approved task plan; neither this role definition nor an old pause record overrides newer human instructions. Existing approved assets and code/text fallbacks are allowed. Catalog indexing has separate authorization and grants no game production/publication authority. Assign one writer per official directory/manifest before work; main owns code integration. Details: on-demand `raidline-art-pipeline`.
