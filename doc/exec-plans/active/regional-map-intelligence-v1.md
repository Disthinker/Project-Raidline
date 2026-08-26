# Regional Operations and Raid Intelligence v1

This ExecPlan is the living delivery record for the first regional-operation
decision and consumable Raid-intelligence loop. It follows
`doc/exec-plans/PLANS.md` and remains active until accepted or superseded.

## Purpose

Replace the Raid Gate's bare map carousel with a real preparation choice. All
three accepted fixed maps remain available, but the player may spend currency
to acquire map-specific Transport, Resource and Enemy intelligence, commit a
chosen briefing when deploying, and use a non-pausing tactical map in the Raid.
The slice validates the long-term information contract without starting the
procedural-map, merchant, outpost or migration systems.

## Product contract

- Every published Raid map declares a fixed difficulty warning and prices for
  three intelligence categories: Transport Map, Resource Manifest and Enemy
  Dossier. These values are development balance, not a formal merchant economy.
- Purchased briefings live in a persistent, slotless intelligence archive keyed
  by `MapDefinitionId` and category. They are knowledge charges, never item
  instances and never enter `AssetRegistry` or grid inventory.
- The Raid Gate acts as a temporary survivor-network purchase and briefing
  consumer. Clicking an empty category purchases one charge and selects it;
  clicking an owned category toggles it for the next deployment.
- Deploy atomically validates and consumes every selected charge, then freezes
  the selected information permissions into `PendingRaidSnapshot`. A rejection
  changes neither currency, counts, revision nor the pending activity.
- Death, active quit and successful extraction keep the information consumed.
  Closing the process during an unresolved Raid continues to restore the exact
  pre-Raid persisted Profile, including the charges, under the accepted
  abnormal-exit rollback contract.
- Pressing `M` during an active Raid opens a tactical-map overlay. The world
  continues to simulate; the player may move at 45% speed but cannot sprint,
  fire, aim, reload, use medical/actions, interact, switch weapons or open the
  inventory until the map closes.
- Exploration reveals a bounded grid around the player for the current Raid.
  The map always shows the player and explored terrain. Unknown extraction
  points become discovered when approached and remain visible for this Raid.
- Transport Map shows the current frozen extraction routes and their live
  availability/conditions. Resource Manifest shows only a coarse region around
  the frozen advanced-resource area. Enemy Dossier shows only coarse circles
  around the initial deployment snapshot, never current enemy positions.
- The overlay and operation board use bilingual text and code-rendered geometry
  placeholders. No preview image or formal map art is introduced.

## Ownership and persistence

1. `ProfileState::raidIntelligence` owns archive counts. Content owns price and
   map compatibility. `App` owns only the transient next-deployment selection.
2. `RaidIntelligenceLoadout` is a value copied into `DeployCommand` and
   `PendingRaidSnapshot`; the deploy domain is the only consumption authority.
3. `RaidTacticalMapState` is SDL-free simulation state for explored cells and
   per-Raid discoveries. It never enters the Profile or cross-Raid save.
4. UI consumes query plans, frozen loadout and tactical projections. It does not
   infer extraction availability, enemy movement or Loot from rendered colors.
5. Schema v20 stores archive counts, frozen Raid loadout and legacy rollback
   data. Schema v19 migrates to an empty archive and no selected briefing.

## Explicit exclusions

- No procedural generation, independent interiors or formal map images.
- No Raid or Base intelligence merchant NPC, random merchant stock, raw
  intelligence Loot, permanent building interiors or trader interruption.
- No chalk drawing, custom pins, text annotations or cross-Raid map memory.
- No outposts, route graph, base migration, waiting-for-departure scheduling,
  vehicle logistics or night-vision gameplay.
- No dynamic enemy tracking, Loot tracking, automatic route guidance or pause.
- No new art/audio production and no manifest changes.

## Verification

- Content rejects invalid difficulty, prices and duplicate map IDs.
- Purchase query/command covers success, insufficient currency, overflow,
  idempotency, stale revision and save failure with zero mutation.
- Deploy covers exact selected-charge consumption, wrong-map isolation,
  repeated command, rejection and pending-Raid rollback.
- Schema v20 round-trips all categories and frozen loadout; schema v19 migration,
  malformed counts and corrupt relationships are rejected or recovered.
- Tactical-map tests cover reveal bounds, discovery persistence, category
  visibility, live extraction state projection and no live-enemy tracking.
- Input tests cover `M`, modal arbitration, slow movement and blocked actions.
- Windows Debug full build, full CTest and exact-head Windows/Ubuntu CI precede
  one consolidated user normal-play acceptance. Codex does not launch the game.

## Progress

- [x] PR #91 accepted and normally merged as `12a2fa6`; this branch was created
  from that clean `origin/main`.
- [x] GDD conflict review complete: long-term consumable categories are kept,
  while the temporary Base survivor-network purchase surface does not freeze a
  formal intelligence merchant or preview-image unlock rule.
- [x] Content, archive, commands, deploy consumption and schema v20 complete.
- [x] Tactical map, exploration fog and three information projections complete.
- [x] Bilingual operation board, focused regression and documentation complete.
- [x] Windows Debug full build and complete CTest 1026/1026 pass at final source state.
- [x] PR #92 implementation commit `cdce5d0` Windows/Ubuntu CI complete.
- [ ] User normal-play acceptance complete.

## Rollback

Before merge, close the Draft PR and discard
`codex/regional-map-intelligence-v1`. After merge, revert its merge commit.
Schema v20 adds knowledge counts but never moves or destroys owned assets; a
revert also removes content v28 and the tactical-map runtime without requiring
an art, audio or manifest rollback.
