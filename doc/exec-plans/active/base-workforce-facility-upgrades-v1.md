# Base Workforce and Facility Upgrades v1

This ExecPlan is the living delivery record for the first professional
workforce and facility-progression loop. It follows `doc/exec-plans/PLANS.md`
and remains active until the slice is accepted or superseded.

## Purpose

Turn the accepted aggregate resident count into a small set of useful choices:
which people staff the workshop and clinic, which Raid rescues add scarce
professional capability, and whether recovered materials and free labour are
committed to improve an existing facility. The result remains an aggregate
management layer, not a resident life simulation.

## Product contract

- Ordinary residents are partitioned into General, Medical, Engineering and
  Combat aggregate professions. Healthy and injured counts retain profession;
  totals cannot duplicate residents.
- Greyline adds General population, Riverside adds Medical population, and
  Ashworks adds injured Engineering population. Already committed rescues are
  deterministically reconstructed during schema migration.
- Workshop and clinic each expose one persistent staffing slot. The player can
  explicitly fill an empty slot with the matching healthy professional or use
  a healthy General resident as a fallback, clear an idle slot, or invoke a
  one-shot auto-fill. Auto-fill never steals construction labour or rewrites a
  later player choice continuously.
- Matching professionals run their facility at 100% duration. General fallback
  uses 150%. Inputs, output quantity, quality and failure risk do not change.
  Active manufacturing and treatment jobs freeze their worker qualification
  and duration when started.
- Existing Dormitory construction is generalized to typed facility targets.
  Workshop and clinic each gain one level-1 to level-2 upgrade. A single global
  construction slot remains; upgrades consume material units, world time and
  unassigned healthy labour while the old facility level remains usable.
- Level 2 reduces future workshop or clinic task duration to 85%. Existing jobs
  do not change retroactively. Upgrade cancellation refunds locked material and
  labour but not elapsed world time.
- Dormitory, Workshop and Medical pages use bilingual text/geometry fallback to
  display professions, staffing, levels, upgrade plans and frozen task speed.

## Ownership and persistence

1. `BasePopulationState` remains the aggregate population authority and gains
   profession totals plus injured-by-profession counts.
2. `BaseWorkforceState` owns persistent facility assignments. Construction and
   future activities consume the same available-population projection rather
   than maintaining independent worker counters.
3. Active manufacturing and resident-treatment snapshots save the profession
   used for their frozen duration. UI never guesses qualification from names.
4. `BaseConstructionState` owns current facility levels. Versioned construction
   definitions declare their target facility and linear level transition.
5. Schema v19 saves profession, staffing, facility level and pending-Raid
   rollback state. Schema v18 reconstructs professions and safe staffing from
   committed rescue facts and any active jobs.

## Explicit exclusions

- No named NPCs, resident list, relationships, skills, fatigue or equipment.
- No training queue, retraining, recruitment market or combat squad.
- No guard-post consumer, automatic defence, morale departure or rebellion.
- No second workshop/clinic queue, facility placement, construction slots,
  electricity, damage, maintenance or production quality.
- No separate public-medical inventory. The accepted user contract continues:
  resident treatment consumes only explicitly authorized, Base-accessible owned
  medical assets from the unified registry.
- No new art/audio production and no manifest changes.

## Verification

- Profession totals, injured pools, assignments and active jobs reject
  duplication, over-allocation and invalid enum values without mutation.
- Rescue admission and schema-v18 migration preserve population totals while
  deterministically assigning professions.
- Manual assignment, clearing, one-shot auto-fill, specialist preference and
  General fallback have focused query/command/idempotency coverage.
- Manufacturing and treatment cover 100%/150% staffing, level-2 85% scaling,
  frozen active durations and unchanged materials/output.
- Construction covers all three facility targets, one-slot exclusion, free
  labour checks, old-level availability, cancellation and exact completion.
- Schema v19 round-trip, corrupt data rejection, pending-Raid rollback and old
  version migration are required.
- Windows Debug full build, full CTest and exact-head Windows/Ubuntu CI precede
  one consolidated user normal-play acceptance. Codex does not launch the game.

## Progress

- [x] PR #90 accepted and normally merged as `1af0e56`; this branch was created
  from that clean `origin/main`.
- [x] GDD/current-code conflicts recorded: aggregate professionals are adopted,
  while the stale separate-medical-reserve wording remains superseded by the
  user's accepted unified-ownership authorization contract.
- [x] Profession population, staffing and rescue/migration implemented. New
  profiles begin with six General, one Medical and one Engineering resident;
  the published rescue profession is the only admission authority.
- [x] Facility consumers and linear upgrades implemented. Existing work freezes
  its duration while new level-2 work uses the published 85% duration.
- [x] Bilingual fallback UI and automated verification complete. Windows Debug
  builds and CTest passes 1010/1010; persistence tests cover staffing and
  Workshop completion across process sessions.
- [ ] Exact-head CI complete.
- [ ] User normal-play acceptance complete.

## Rollback

Before merge, close the Draft PR and discard
`codex/base-workforce-facility-upgrades-v1`. After merge, revert its merge
commit. Schema v19 migration is additive and does not move assets. Reverting
also reverts content v27 and the new typed upgrade definitions; no art, audio or
manifest rollback is required.
