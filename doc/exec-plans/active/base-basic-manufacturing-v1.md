# Base Basic Manufacturing v1

This ExecPlan is the living delivery record for the first real-asset Base
manufacturing loop. It follows `doc/exec-plans/PLANS.md` and remains active
until the slice is accepted or superseded.

## Purpose

Turn Raid salvage into a reusable Raid supply without creating an abstract
crafting currency. The existing damaged workshop exposes one recipe: reserve
one Scrap Parts and one Damaged Electronics asset, commit one healthy ordinary
resident for six world hours, and produce one Weapon Maintenance Kit. The
player can rest, remain in Base, or complete a Raid while the order advances.

## Product contract

- The workshop is an existing damaged Base facility with one production slot;
  this slice does not add layout editing, facility construction, upgrades,
  professions, multiple queues, blueprints, quality, power, or automation.
- Recipe definitions are versioned content with stable IDs. V1 publishes one
  recipe and uses only existing item definitions and text/geometry fallback.
- Starting an order is Base-only, requires no pending Raid, one healthy worker,
  one exact Base-accessible asset for every recipe input, a fresh service-job
  ID, and a persistable Profile revision.
- Inputs keep their stable asset IDs and move to `BaseServiceAssetLocation`.
  They are not destroyed until completion. Supply-policy assignments remain
  intent attached to the item definition and are not silently changed.
- Cancellation is allowed only while processing. It first plans legal Stash
  cells for every reserved input. If all inputs cannot return, cancellation is
  rejected with zero mutation; success returns all inputs, releases the worker,
  and does not rewind world time.
- Starting the order reserves one real output identity under the workshop job.
  At completion, reserved inputs are destroyed exactly once and that output
  becomes usable. It enters the Stash when a legal cell exists; otherwise it
  remains in the workshop output area under the same job ID. A blocked output
  releases the worker and prevents a new order until collected.
- Collection moves the ready output into a legal Stash cell atomically. No
  output is discarded, converted to currency, placed on the ground, or created
  twice.
- Manufacturing time is authoritative world time. During a pending Raid,
  materialization is deferred until settlement so abnormal exit can restore
  the exact pre-Raid Profile without cloning the asset registry; success or
  ordinary failure applies the due completion once after the pending activity
  closes.

## Architecture and persistence

1. Add `BaseManufacturingRecipeDefinitionId` and versioned recipe definitions.
2. Add a single optional manufacturing order to `ProfileState`; the order owns
   exact input IDs, timing, worker commitment, and an optional ready output ID.
3. Extend service-asset validation so every `BaseServiceAssetLocation` belongs
   to exactly one typed job and every job-owned ID is unique.
4. Add pure query/execute/apply commands for start, cancel, and collect.
5. Advance the order through Base simulation, rest, and settled Raid travel.
6. Save schema v17/content v25 round-trips active and blocked-output orders;
   v16 migrates to no order without changing assets or ID high-water marks.

## Verification

- Content validation: duplicate/unknown recipe IDs, unknown inputs/outputs,
  invalid quantities, durations, workers, and unsupported stack contracts.
- Domain: deterministic input selection, reservation ownership, worker conflict,
  cancellation capacity failure, completion to Stash, blocked output,
  collection, replay, stale revision, save failure, and state fingerprint.
- Persistence/lifecycle: schema v17 round-trip, schema v16 migration, Base rest,
  real-time Base completion, Raid settlement completion, and abnormal rollback.
- BaseWorld/App: workshop collision/interaction, bilingual labels, one normal
  Start/Cancel/Collect control, exact requirements, progress, and output state.
- Windows Debug full build, full CTest, exact-head Windows/Ubuntu CI, then user
  normal-play acceptance. Codex does not launch the game.

## Progress

- [x] PR #88 accepted and merged as `987dc6b`; branch created from that clean
  `origin/main`.
- [x] Contract frozen and exclusions recorded.
- [x] Domain, content and persistence implemented.
- [x] GameSession, world time and workshop UI implemented.
- [x] Automated verification complete: Windows Debug full build and CTest
  988/988 pass; Codex did not launch the game.
- [x] Draft PR #89 open; code commit `aed2b1d` passed the scope gate and
  exact-head Windows/Ubuntu build and test jobs.
- [x] User normal-play acceptance complete; PR #89 was normally merged as
  `194f910` without deleting the branch or rewriting history.

## Rollback

Before merge, close the Draft PR and discard
`codex/base-basic-manufacturing-v1`. After merge, revert its merge commit.
Schema v17 adds only an optional order; v16 migration defaults it to empty.
No formal resource or manifest rollback is required.
