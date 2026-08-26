# Base Resident Medical Treatment v1

This ExecPlan is a living delivery record for the first aggregated resident
injury and treatment loop. It follows `doc/exec-plans/PLANS.md` and is kept
current until the slice is accepted or superseded.

## Purpose

Make rescued injuries matter without introducing individual NPC simulation.
One published rescue can admit an injured ordinary resident. In Base, the
Medical facility previews and starts one treatment using whole quantities of
owned, Base-accessible items whose definitions the player explicitly assigned
to the Medical supply category. The resident recovers after world time passes.

Player treatment remains a separate currency-only, immediate service. Player
self-use remains an explicit medical-item action. This slice does not create a
second public medical inventory.

## Product contract and superseded material

The older external GDD describes a separate public medical reserve. The user's
newer explicit decision supersedes that implementation detail: every owned item
remains in the unified AssetRegistry and in its real Stash/equipment/container
location until an authorized consumer commits it. The external GDD remains
read-only; this discrepancy is recorded here rather than silently rewriting it.

V1 deliberately uses aggregated counts, one facility slot, a fixed contribution
target and a fixed duration from versioned content. It does not add resident
names, illness simulation, diagnosis, staff specialties, queues, cancellation,
random injury generation, formal art, audio, or manifest changes.

## Domain invariants

- `injuredResidents <= ordinaryResidents`; injured residents still need beds
  and food but are unavailable for construction work.
- Starting treatment is Base-only and requires no pending Raid, at least one
  untreated injured resident, no active resident treatment, and a non-overflowing
  Profile revision and service-job ID.
- Only definitions explicitly assigned to `BaseSupplyCategory::Medical` are
  candidates. Assets must be Base-accessible and non-empty containers are never
  consumed.
- Query and commit use the same deterministic order: higher contribution first,
  then stable AssetInstanceId. A stack may be partially reduced; each selected
  unit is consumed whole and the preview lists the exact quantities.
- Rejection, stale revision, invalid content, and persistence failure leave the
  Profile fingerprint, asset high-water mark, assignments and population
  unchanged.
- Supplies are consumed atomically when treatment starts. The active job owns no
  assets and therefore cannot duplicate or strand inventory.
- Completion is driven only by authoritative world minutes and is applied by
  Base simulation, rest, and Raid travel before validation/persistence.
- One Ashworks rescue admits one injured resident. The injury fact is frozen in
  the pending Raid snapshot and secured through the same idempotent rescue
  checkpoint as its ordinary-resident count.

## Implementation plan

1. Add versioned ResidentMedicalDefinition content and injured-resident facts to
   rescue definitions. Raise content to v24 and save schema to v16 with a v15
   migration that introduces no retroactive injury.
2. Add BaseResidentMedicalState and an active treatment job to ProfileState,
   validation, fingerprinting, pending-travel rollback data, and persistence.
3. Add pure query/execute/apply resident-medical domain functions, deterministic
   supply selection, rescue admission injury propagation, and healthy-worker
   projections.
4. Route treatment completion through all world-time consumers and expose a
   persisted GameSession command.
5. Extend the existing Medical facility page with resident status, exact supply
   preview, active progress, and a start action using text/geometry placeholders.
6. Cover domain, rescue, save migration/round-trip, GameSession save failure,
   localization, and world-time completion paths. Run focused tests, Windows
   Debug full build/CTest, push a Draft PR, and wait for exact-head CI.

## Acceptance

Automated evidence must prove the invariants above. Normal-play acceptance is
deferred to the user: secure the Ashworks resident, assign suitable owned items
to Medical supply, start treatment at the Medical facility, pass time by normal
Base play/rest or a Raid trip, and confirm recovery and persistence after restart.
Codex does not launch the game for this acceptance.

## Progress

- [x] PR #87 accepted and merged as `1be94bf`; new branch created from that
  accepted `origin/main`.
- [x] Contract frozen and old-GDD conflict recorded.
- [x] Domain, content, save and rescue changes implemented.
- [x] GameSession and Medical facility UI implemented.
- [x] Automated verification complete: Windows Debug full build and 973/973
  CTest pass; Codex did not launch the game.
- [x] Draft PR #88 open with exact-head Windows/Ubuntu CI green.
- [ ] User normal-play acceptance complete.

## Rollback

This slice is isolated on `codex/base-resident-medical-treatment-v1`. Before
merge, close the Draft PR and discard the branch. After merge, revert its merge
commit; schema v16 writes retain the new fields, while v15 migration defaults
them safely to zero/no active treatment. No resource or manifest rollback is
required.
