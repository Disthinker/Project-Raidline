# Enemy configuration and combat-feedback contract v1

## Scope / accepted dependency

2026-09-08: the user accepted Phase B's no-rewind/no-resurrection/dead-attack
result and explicitly authorized this next narrow slice. Branch
`codex/enemy-combat-contract-v1` is stacked on #150@2ff1a69 (accepted by user,
not merged); #149 remains its open dependency. Main remains 2ae898a. No merge.

Player outcome: newly generated ordinary infected use the same 12 HP definition
in Daily/Defense/Raid, and correctly aimed head hits have the same reticle
feedback everywhere. Ordinary hits never acquire a special X.

## Mechanism / policy

- ContentRegistry owns immutable, strongly typed enemy combat definitions.
  Published spawn sources reference `enemy.infected.basic`; load resolves values
  before each activity freezes its existing DTO. Activity still owns placement,
  waves, targets, consequences and save timing. No enemy instance ID change.
- Existing snapshots remain authoritative: old 3 HP perimeter cycles and old
  Defense wave health (including 100 HP) retain their exact current/max HP,
  positions and death tombstones. New cycle/event/deploy uses current content.
  Do not reinterpret injured snapshots or redraw/reseed pending Raid.
- Content v60 adds definitions/references; schema46 and rules29 stay unchanged.
  Explicitly accept content59 saves. Legacy numeric spawn definitions continue
  parsing; reject unknown profiles, duplicates, invalid HP and ambiguous sources.
- WorldShootingRuntime owns transient special-hit presentation alongside existing
  shot presentation. It consumes only HitResult, expires with simulation time,
  clears on spatial reset/restore, and is not persisted. Base and Raid expose
  the same read-only projection; App only draws it.
- No synthetic weak-point regions: ordinary infected currently support heads,
  while WeakPoint semantics remain supported only if produced by the domain.

## Delivery

1. Definition/reference loading + Daily and Defense generation injection.
2. Shared feedback projection + remove Raid-only App feedback accumulation.
3. Cross-activity hit/feedback contracts and content/old-snapshot/save regressions.
4. Windows Debug full build, focused/full CTest, exact-head Windows/Ubuntu CI.
5. User normal-play acceptance last; agent never launches game.

## Gates / deliberate behavior changes

- Newly generated Daily ordinary infected increase from 3 to 12 HP. This is an
  intentional balance correction, not pure refactoring. Existing Raid and new
  Defense HP stay 12. Missing Base special-hit feedback is a visible fix.
- Same weapon/target/aim/impact yields same damage/semantic/feedback in all three
  real runtime adapters; nonlethal, lethal, ordinary, head, wrong aim intent,
  feedback expiry/reset and death cleanup covered.
- Change one test content profile and prove Daily/Defense/Raid generation all
  consume it. Legacy snapshots remain unchanged across repeated restore/save.
- No new Profile copy/save in firing or feedback hot paths. Existing performance
  gates remain enabled, no threshold relaxation.

## Exclusions / rollback

No navigation algorithms, frame-order change, new enemies/weak points, full
Combat Pipeline, Session/Persistence redesign, art/audio/manifest/GDD edits.
Revert this slice's commits in reverse order without rewriting dependencies.
Keep a pre-v60 save backup for an older executable: schema is unchanged but
older loaders need not accept a newer content envelope. No destructive save work.

## Acceptance

In a new perimeter cycle, new Defense event and Raid, use the same weapon: torso
shots have comparable kill cost, correctly aimed head hits show the short marker,
ordinary hits do not. Continue moving after kills; survivors never rewind.
Open an existing injured/dead snapshot: no health refill or resurrection. If an
old event uses old HP, finish that event normally before comparing new enemies.

## Evidence

- Baseline: clean 2ff1a69; prior 1625 tests and exact-head CI belong to #150 only.
- Implemented: 82 published spawn references resolve through the shared definition;
  Daily/Defense creation consume that definition, while restore keeps frozen HP.
  WorldShootingRuntime now owns special-hit presentation; App's Raid-only timer
  and semantic accumulation were removed. No schema or activity-order change.
- Windows Debug full incremental build passed (all header-dependent targets rebuilt).
- Focused CTest: 155/155 passed. Full CTest: 1637/1637 passed; LastTest.log contains
  1637 passed records and no failed records (2026-09-08 12:21 local).
- Added 12 registered cases: content/reference validation, legacy compatibility,
  old injured/dead snapshots, real three-activity aim/fire/sweep semantics,
  lethal removal and feedback expiry/reset. Existing performance gates unchanged.
- Exact-head Windows/Ubuntu CI will be recorded in the stacked Draft PR. User
  normal-play acceptance remains pending; no game launched, no PR merged.
