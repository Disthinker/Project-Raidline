# Project Raidline Codex Instructions

## Project

Project Raidline is a C++20/SDL3 2D game prototype moving toward a top-down pixel-art extraction experience. Treat the implemented code as the behavior baseline; the long-term product direction is not evidence that a feature already exists.

## Sources of truth

Use this order when facts disagree:

1. The current workspace, including user-owned uncommitted changes.
2. Current branch code, tests, CMake, CI, and asset manifests.
3. `main` and merged history.
4. Current documents under `doc/` and `art/`.
5. Historical DevLogs and prompt history.

Never discard, overwrite, or broadly reformat unrelated user changes. Check `git status` before and after work.

## Current stage and navigation

- Project summary: `doc/project/PROJECT_OVERVIEW.md`
- Verified current state: `doc/project/CURRENT_STATE.md`
- Milestones and candidate roadmap: `doc/project/ROADMAP.md`
- Architecture and ownership: `doc/architecture/ARCHITECTURE.md`
- Behavioral invariants: `doc/architecture/INVARIANTS.md`
- Build and test commands: `doc/engineering/BUILD_AND_TEST.md`
- Completion standard: `doc/engineering/DEFINITION_OF_DONE.md`
- Active plans: `doc/exec-plans/active/`

Week 17 mouse inventory interaction is complete with automated, commit-specific Windows/Ubuntu CI, and 9/9 real-window acceptance evidence on PR #31. Read its completed ExecPlan and `doc/project/KNOWN_ISSUES.md` before changing inventory interaction code.

## Architecture guardrails

- Keep SDL event translation and rendering in `App`/input adapters; keep core gameplay and inventory logic independently testable.
- Preserve the separation between shared `ItemDefinition` data and move-only, stable-ID `ItemInstance` ownership.
- Use pure queries such as `canMove` before transactional commands such as `tryMove`; failed commands must leave state unchanged.
- Do not retain `std::vector` references, iterators, or indices across mutations; retain stable IDs.
- Keep keyboard `focusedCell` distinct from mouse `hoveredCell`.
- Prefer a small closed slice over speculative ECS, scene, service, or cross-container abstractions.

## Workflows

Use an ExecPlan for multi-system changes, ownership changes, migrations, raid lifecycle work, multi-container transfer, persistence, major refactors, or work that needs several implementation stages. Follow `doc/exec-plans/PLANS.md`.

Use repository skills when their scope matches:

- `$raidline-feature-delivery`: feature, bug fix, or controlled refactor delivery.
- `$raidline-cpp-safety-review`: C++ ownership, lifetime, invalidation, state, transaction, compile, or link risk.
- `$raidline-build-test-ci`: configure/build/test/CI execution and diagnosis.
- `$raidline-task-closeout`: evidence-based task closure and teaching handoff.
- `$raidline-inventory-domain`: item, inventory, container, equipment, or stash work.
- `$raidline-art-pipeline`: image, sprite, animation, UI art, asset review, or publishing work.

Use project agents deliberately:

- `raidline-explorer`: read-only code and dependency mapping.
- `raidline-implementer`: the single normal source-writing worker for an approved scope.
- `raidline-reviewer`: read-only correctness and C++ safety review.
- `raidline-verifier`: build, test, static-check, and CI-log verification.
- `raidline-learning-analyst`: Chinese C++ teaching material from the final diff and failures.

Do not run multiple implementers against overlapping source files. The primary thread owns scope, decisions, integration, and user reporting.

## Validation and closeout

Run the smallest relevant target tests, then the full CTest suite. Consider both Windows and Ubuntu CI behavior. Never report an unexecuted check or manual acceptance item as passed.

Every completed feature task must satisfy `doc/engineering/DEFINITION_OF_DONE.md` and add a Chinese handoff under `doc/handoffs/completed/` using `doc/handoffs/CXX_TEACHING_HANDOFF_TEMPLATE.md`.

## Art asset hard boundaries

Before any art work, read `$raidline-art-pipeline`, `art/ART_BIBLE.md`, `art/ASSET_MANIFEST.yaml`, `art/PRODUCTION_TASK_PROTOCOL.md`, and the relevant `art/specs/` contract.

- The art-control task scopes packages, writes prompts, opens dedicated user-visible production tasks, reviews, requests revisions, publishes, updates the manifest, and closes packages; it does not generate images itself.
- Use one dedicated production task per coherent asset family. Internal subagents are not the normal image-production unit.
- A production task may write only inside its assigned `art/work/<package_id>/`; retain the exact prompt and every candidate.
- Production tasks must not approve their own work, update the manifest, publish into `assets/`, or start downstream packages.
- Generate every distinct candidate with a separate image-generation call.
- Never overwrite an approved asset. A visual revision requires a new versioned asset ID and path.
- Derive inventory and world variants deterministically from one approved identity master with nearest-neighbor scaling.
- Only art-control may approve, publish, update `art/ASSET_MANIFEST.yaml`, and close a package after technical QA and visual review.
- Runtime code reads published `assets/`, never `art/work/` candidates.
- Run the relevant `tools/art_pipeline/` validation before approval; scripts do not replace visual review.
- Phase 1 excludes character clothing layers, attachments, map/building production, corpses, limb/damage layers, projectile/VFX art, and large raster UI panels.
- Inventory grids, highlights, selection rectangles, hover, and drag feedback remain code-rendered SDL elements.
