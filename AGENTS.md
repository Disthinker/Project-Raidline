# Project Raidline delivery rules

## Mission and product scope

Project Raidline is an independent game in production, not a weekly teaching exercise. Work is organized by playable product slices, ownership boundaries, regression evidence, and reviewable PRs.

The shipping architecture targets Windows PC, keyboard-and-mouse-first, offline single-player. Keep SDL-independent code portable enough for Linux build/test CI, but do not introduce networking, console certification, public modding, or cross-platform release frameworks without a new product decision and an active consumer.

For Core Extraction Alpha, the external GDD repository is read-only product input. Its `05_Core_Extraction_Alpha_首阶段功能规格.md` is the only first-stage scope contract. Other GDD files describe long-term direction; a feature enters Alpha only when the scope contract includes it.

Source priority:

1. Explicit user product decisions and authorization boundaries.
2. The active stage scope contract and active ExecPlan.
3. Current branch code, tests, and reproducible runtime evidence.
4. `origin/main`, accepted PRs, architecture invariants, and current-state docs.
5. Historical Week plans, handoffs, learning notes, and candidate designs.

When sources conflict, record the conflict and follow the higher source. Do not silently rewrite the GDD from the code repository.

## Git and release control

- Fetch before starting a task and report branch, HEAD, `origin/main`, worktree state, dependency PRs, and relevant CI.
- One product slice or focused defect uses one `codex/...` branch. Start from the latest accepted dependency baseline, normally `origin/main`.
- Never use another open feature branch as an implicit base. Do not copy an open fix into a second branch merely to unblock unrelated work.
- Do not mix unrelated work, rewrite shared history, force-push, or merge a PR without explicit authorization.
- Keep commits coherent and reviewable. Push and open a PR only after the scoped evidence gate passes.
- Historical Week documents remain evidence; new product milestones are not organized by Week number.

## Architecture and domain rules

- `App` translates input and renders read-only projections. It must not own assets, decide inventory legality, infer hit semantics, or synthesize settlement results.
- `GameFlow` owns screen-level transitions. `GameSession` is the composition root for persistent profile state and the mutually exclusive `BaseWorld` or `GameplayWorld` runtime.
- `BaseWorld` owns only base-space transient state. `GameplayWorld` owns only one Raid runtime. Neither owns the authoritative long-term asset registry.
- One `AssetRegistry` is the authority for every unique item instance. Locations are stable domain values such as Stash, equipment, container, weapon, action hold, ground, or settlement transit.
- Keep definitions immutable and shared; keep instances move-only with stable IDs. Save IDs and relationships, never pointers, vector indexes, UI cells, or display names.
- Query before command. A rejected command leaves every participant unchanged. Multi-owner changes commit atomically.
- Do not retain references, pointers, or iterators into `std::vector` across erase, insertion, reallocation, move, or transaction commit.
- UI consumes domain results. Ordinary hit, headshot, weak point, armor, action, economy, deploy, and settlement facts must never be guessed from animations, colors, collision labels, or display names.
- Core Extraction Alpha establishes `ShotCommand -> ShotResolution -> HitResult`. The current `Projectile` path is a temporary adapter only; weapon, damage, persistence, and App projections may not require that entity type.
- Stable IDs are monotonic within their identity domain. Save formats are versioned, migrations are explicit, and Raid deploy/settlement uses a unique idempotency key.
- Do not introduce an ECS, service locator, universal event bus, or framework without a current consumer and a measured need.
- The target modular monolith uses `raidline_domain`, `raidline_simulation`, `raidline_services`, and `raidline_sdl_client`; introduce each target only through the accepted architecture migration sequence and keep domain/simulation SDL-free.
- Content definitions migrate to versioned JSON with stable namespaced IDs; persistence saves those IDs, never runtime indexes. Do not add hot reload or a public mod API as part of Alpha.

## Core Extraction Alpha scope guard

The active scope includes only the features enumerated in the Alpha scope contract. In particular:

- The active equipment slots are primary weapon, chest rig, and backpack.
- The first Raid has no hard time failure. The legacy 180-second timeout is scheduled for removal behind an explicit lifecycle contract.
- Player health migrates from the V0 value to 100 HP only in the planned health/medical slice.
- One fixed map is represented by data definitions and a frozen per-Raid snapshot.
- Failure means full loss for death, active quit, or abnormal quit; settlement is idempotent.
- Formal attack animations, new art/audio production, multi-map work, high-risk phase, armor, hit regions, penetration, durability, faults, complex injuries, quests, crafting, base building, population, and story content are excluded.

## Build, test, and evidence

- Use the repository `windows-debug` preset, Ninja, Debug, x64-windows, the configured Visual Studio Developer Shell, and UTF-8 code page.
- Any source or CMake change requires an incremental build and relevant automated tests. Header or class-layout changes require rebuilding every affected target.
- If MSVC/GTest reports `gtest_ar_` stack corruption, inspect Ninja header dependencies and rebuild affected targets; never treat stale binaries as evidence.
- Run focused tests during implementation and the full registered CTest suite before push. C++-affecting PRs must pass exact-head Windows and Ubuntu CI.
- Manual acceptance proves visible player behavior only. Record build identity, steps, expected result, actual result, and deviations.
- A task closes with scope, changed contracts, automated evidence, manual evidence when relevant, risks, rollback, commit, push, and PR. Teaching handoffs and learning ledgers are optional historical material, not completion gates.

## Art boundary

- Official Grab/Scratch/Bite art and all other new formal art/audio production are paused.
- Without renewed user authorization, do not generate candidates, write `art/work/`, publish runtime assets, or modify the art manifest.
- Runtime code may use already approved assets and code-rendered fallback presentation. Only an explicitly reopened art package may use `$raidline-art-pipeline`.

## On-demand project skills

- `$raidline-project-governor`: dependency, branch, scope, release, and PR control.
- `$raidline-feature-slice-delivery`: deliver one playable vertical slice.
- `$raidline-architecture-domain-invariants`: ownership, commands, IDs, persistence, and C++ safety review.
- `$raidline-build-test-regression`: build, automated regression, CI, and evidence.
- `$raidline-gameplay-acceptance`: executable player-flow acceptance and defect capture.
- `$raidline-art-pipeline`: disabled unless the user explicitly reopens a named art package.

Use these as bounded responsibilities, not as a reason to create long-lived agent threads. The primary developer remains accountable for integrating their evidence.
