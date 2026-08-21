# Project Raidline delivery rules

## Mission and source priority

Project Raidline is an independent Windows PC game in production. Work is organized by playable slices, authoritative ownership, regression evidence and reviewable PRs; it is not a weekly teaching exercise.

Source priority:

1. Explicit user decisions and authorization boundaries.
2. Active scope contract, task envelope and ExecPlan.
3. Current code, tests, CMake/CI and reproducible evidence.
4. Accepted `origin/main`, stable architecture and invariants.
5. Historical plans, Week docs, DevLogs and chat summaries.

Record conflicts; do not silently rewrite the read-only external GDD from implementation details.

## Minimal task startup

Do not begin every task by reading the whole project. Start with:

1. Fetch and report branch, HEAD, `origin/main`, worktree and relevant open PR/dependency.
2. Run `python tools/raidline_governance.py preflight --task "<request>"` (add `--envelope <path> --run-sentinel` once the task boundary exists).
3. Read `doc/context/INDEX.md`, the one primary domain context returned by the router, and the current `*.task.toml`/ExecPlan.
4. Use `rg` for symbols, includes, callers and tests; load adjacent context only when dependency or failure evidence requires it. State why context expanded.

Do not default-load CURRENT_STATE, ROADMAP, all architecture docs, completed plans, DevLogs or art contracts. Historical context is on demand.

Every tracked change needs a short task envelope based on `doc/exec-plans/TASK_ENVELOPE_TEMPLATE.toml`. `cross-domain` and `authority` work, multi-step migrations and playable slices also require an ExecPlan. If implementation must leave the allowed paths or enter a protected domain, expand the envelope explicitly before editing further.

## Risk and evidence

- `local`: documentation, tooling or isolated stateless presentation. Run relevant checks and `git diff --check`; manually accept visible behavior.
- `domain`: one authoritative domain. Run its area label, Sentinel, full CTest and exact-head CI for C++.
- `cross-domain`: two or more layers/domains or shared build/client orchestration. Add ExecPlan, area tests, Sentinel, Integration, full CTest and CI; user performs consolidated normal-play acceptance when visible.
- `authority`: Profile, AssetRegistry, stable IDs, Save/schema/migration, Settlement, Content compatibility or atomic ownership. Also require persistence/migration, long-sequence, idempotency and failure-atomicity evidence.

Use `python tools/raidline_governance.py postflight --envelope <path> --base origin/main --run-tests` before push. Soft blast-radius warnings require a written reason or a split into independently complete slices; they are not line-count quotas.

## Architecture and domain rules

- `App` translates input and renders projections. It never owns assets, decides legality, infers hit semantics or synthesizes settlement.
- `GameSession` composes the persistent Profile and one mutually exclusive Base/Raid runtime. Worlds own spatial transient state, not long-term assets.
- One `AssetRegistry` owns every item instance. Stable IDs are monotonic; definitions are immutable; locations and relationships are explicit values.
- Query before command. Rejection preserves all participants, currency, revision and high-water marks; multi-owner changes commit atomically.
- Never retain vector references, pointers, iterators or indexes across structural mutation or transaction commit.
- `ShotCommand -> ShotResolution -> LogicalBallisticFlight -> HitResult` is authoritative. Production must not reintroduce a renderable/collidable Projectile entity or App-side hit inference.
- Domain and simulation remain SDL-free. Dependencies point `domain -> simulation -> services -> sdl_client` only in the consuming direction. Each production cpp has one build owner.
- Save stable IDs and namespaced DefinitionIds, never pointers, display names, UI cells or runtime indexes. Migrations are explicit and settlement is idempotent.
- Do not introduce ECS, service locators, universal event buses or future frameworks without a measured current consumer.

The machine guard and compact domain entry points live under `tools/raidline_governance.py` and `doc/context/domains/`.

## Scope and asset boundaries

The external Alpha scope contract remains the only Alpha range source. Long-term GDD ideas do not enter a slice unless its active contract includes them. Legacy 3 HP, 180-second timeout, V0 read-only Stash, infinite ammo and old RaidSettlement are historical adapters and must not gain consumers.

Formal Grab/Scratch/Bite art and all new formal art remain paused. Audio beyond the already authorized `assets/audio/v1` P0 bank is also paused. Without renewed authorization, do not generate candidates, write `art/work/`, publish assets or modify the art manifest.

## Git, collaboration and release control

- One coherent task uses one `codex/...` branch from the latest accepted dependency, normally `origin/main`. Do not use an open feature branch as an implicit base.
- One Primary Writer owns tracked business changes. Explorer and Reviewer are read-only; Verifier may write ignored build/report output but never tracked source. Do not run overlapping writers on GameSession, GameplayWorld, AssetRegistry, Persistence, CMake or shared headers.
- Preserve unrelated/user changes. Never force-push, rewrite shared history, auto-merge or merge without explicit authorization.
- Stage only confirmed paths. Keep commits coherent and open Draft PRs until automated gates and required user acceptance pass.
- Manual acceptance is last and is performed by the user through normal play. The development agent does not launch the game as a substitute.

## Documentation ownership

- `CURRENT_STATE.md`: accepted main capabilities only; no current branch, draft PR, transient CI or pending acceptance.
- `ARCHITECTURE.md`: stable architecture changes only. `INVARIANTS.md`: long-lived correctness contracts only.
- `ROADMAP.md`: product stage/order changes only. `KNOWN_ISSUES.md`: unresolved durable defects/decisions/debt only; resolved PR history belongs in completed plans/PRs.
- Active ExecPlan/task envelope: current task scope and evidence. Move accepted plans to `completed/`. PR: exact diff, exact-head evidence and transient delivery status.

Do not mechanically update every project document in every feature PR.

## Project skills

- `$raidline-project-governor`: preflight, context/risk/envelope, dependency and PR control.
- `$raidline-feature-slice-delivery`: one approved playable vertical slice.
- `$raidline-architecture-domain-invariants`: ownership, impact and architecture review.
- `$raidline-build-test-regression`: layered tests, builds, postflight and CI evidence.
- `$raidline-gameplay-acceptance`: user-executed real-window flow and defect classification.
- `$raidline-art-pipeline`: disabled unless the user explicitly reopens a named art package.

Use only the skills required by the routed task. Skills are bounded responsibilities, not a reason to create long-lived agents or load unrelated references.
