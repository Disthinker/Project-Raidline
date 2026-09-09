# Combat runtime boundaries v1

## Acceptance and closeout — 2026-09-09

User accepted #156@f16544d in the next turn. Exact-head CI 34320466698 passed
Windows and Ubuntu (1697 tests each); this is integrated-tip acceptance, not a
retroactive claim that every historical head passed manual play. #149–#156
remain unmerged, with main at 2ae898a.

The planned closing review is complete in
[Gameplay framework closeout](../../architecture/GAMEPLAY_FRAMEWORK_CLOSEOUT.md):
actual before/after chain, ownership/policy matrix, 51 cross-activity contract
instances, remaining debt, cumulative diff and dependency delivery order.
Current Windows full rerun: 1697/1697 in 66.16s; unchanged-code build needs no work.
No new C++ defect justified another implementation slice. Code consolidation
can close; next gate is explicitly authorized dependency-chain delivery, not
automatic gameplay or Session/Persistence expansion. This follow-up only edits
documentation on `codex/gameplay-consolidation-closeout`, based on this PR.

Below is the original implementation record; its pending acceptance/CI text is
historical and superseded by this section.

## Baseline and scope

2026-09-09: user accepted #155@c0b0664 and requested the next planned gate.
Exact-head CI 34313046163 passed Windows and Ubuntu, 1687 tests each.
Clean stacked branch `codex/combat-runtime-boundaries-v1` explicitly depends on
`codex/combat-frame-order-v1`; main remains 2ae898a. No merge authorization.

Audit Daily -> Defense -> Daily, Base -> Raid -> Base, profile replacement,
and indoor/outdoor space changes. Fix only proven stale actor/flight/result
boundaries. Preserve active-space rosters, stable IDs, activity policy, the
accepted enemy-first order, and existing save/settlement semantics. No schema,
content or rules change (46/60/29), art/audio/manifest/GDD writes, new gameplay,
Daily pathfinding, manager/facade or Session/Persistence wholesale rewrite.

## Confirmed findings

- GameFlow reuses BaseWorld after loading another authoritative Profile. The
  same site and perimeter cycle can bypass actor import, retaining old health,
  position or retired IDs. Explicit profile replacement must invalidate that
  cache; ordinary same-cycle synchronization must still preserve live actors.
- Base shooting survives successful Deploy and player relocation, and may
  survive a perimeter cycle replacement with recycled local target IDs.
- Shooting's spatial clear removes flights/hits but not the last-shot flag.
  A completed transition must not expose an old accepted-shot fact again.
- A valid uninitialized shooting checkpoint previously returned without clearing
  a reused runtime's flights. It now clears spatial state while retaining the
  caller's configured weapon; active Defense still rejects uninitialized state.
- Defense preparation already strips prior Daily flights; same-event resume
  intentionally restores its own flights. Raid portals already clear shooting
  while retaining each space's roster. Characterize, do not rewrite those paths.

## Ownership and implementation gate

Runtime owns transient cleanup; Flow only notifies an accepted lifecycle
boundary. Profile replacement resets Base combat caches before importing the
loaded activity. Rejected Profile reads/saves, Deploy and checkpoint validation
must not clear the live runtime. An accepted Profile whose runtime cannot be
constructed still follows the existing blocked-recovery path, not an invented
global Flow/Profile transaction rollback.
Clear obsolete spatial shooting on successful departures/relocations and Daily
roster replacement, not per-frame, not on same-cycle synchronization, and not
while restoring the same Defense checkpoint. No target ID epoch or DTO needed.
Preserve all asset/ammo/health/reward/failure consequences.

## Tests / acceptance / rollback

1. Add failing real-adapter tests on unchanged production code for stale reload
   and flights, then record red evidence.
2. Exercise same-process vs fresh-process load, new Profile at same site/cycle,
   accepted/rejected Deploy, new vs same perimeter cycle, Daily/Defense return,
   same-event restore and rejected restore, and Raid portal isolation.
3. Build every affected Windows Debug target; focused/full CTest and exact-head
   Windows/Ubuntu CI. Agent does not launch the game.
4. User normally plays Base/perimeter, defends and continues a save, deploys,
   enters/exits an interior, then returns. No frame-perfect or debug-button gate.

One focused stacked PR; revert independently without save migration. After this
gate, publish actual boundary matrix and remaining debt. Do not automatically
start further navigation/session/persistence consolidation or new gameplay.

## Actual Before / After

| Boundary | Before | After / retained contract |
| --- | --- | --- |
| Successful new/continue Profile | sync site + relocate, possibly same-cycle cached actors | Flow notifies Base combat reset, then sync/relocate/import; Session invalidates only its configured Base weapon ID |
| Successful Base -> Raid | inactive Base retains old flights | clear Base spatial flights/results only after accepted Deploy; roster remains frozen |
| Raid -> Base / perimeter rescue | relocate/reanchor only | relocate/reanchor and clear obsolete spatial facts; do not regenerate roster |
| Daily cycle replacement | replace roster, retain flights aimed at reusable local IDs | clear spatial facts before replacement; same-cycle sync is unchanged |
| Daily -> Defense | preparation strips Daily flights | unchanged; candidate-only preparation, valid Defense checkpoint owns its flights |
| Defense -> Daily | release Defense and shooting projections | also clear old accepted-shot/removal/damage facts; preserve frozen Daily actors |
| Same Defense restore | validated complete checkpoint import | unchanged; shot/actor/attack/navigation state and resource snapshot continue together |
| Raid indoor/outdoor | separate rosters + clear spatial shooting | unchanged, new repeated-portal death/identity regression |

`BaseWorld::resetCombatForProfileLoad` owns transient reset; Flow does not erase
enemies itself. `clearSpatialCombatState` clears flights and frame facts, not the
roster or assets. Normal space changes preserve weapon setup and shot sequence.
An explicitly replaced Profile resets the shooting instance and reconfigures
the equipped weapon, even if the new Profile reuses the same asset ID. No new
identity system, serialization or universal runtime object.

## Evidence and progress

- On unchanged c0b0664 production, four new boundary tests failed in 0.61s:
  same-process new/continue, cycle replacement, relocation and accepted Deploy.
  A separate empty-checkpoint test then reproduced inherited flights (0.04s).
- Windows Debug affected-header full rebuild passed. Focused 149/149 passed
  (13.44s), including 10 added registered tests, real Flow/Session/World adapters,
  both rejection and acceptance, same-process/fresh-process Defense with an
  actual accepted shot, and existing serial performance thresholds unchanged.
- Full local CTest 1697/1697 passed (64.66s), no disabled tests or threshold
  relaxation. Exact-head CI will be recorded in the PR, not in a docs-only
  follow-up commit. No game launch; user normal-play acceptance pending.

## Remaining debt / next decision

The inspected remaining differences belong to real consumers: Daily patrol and
safe core, Defense waves/objectives/checkpoints, Raid spaces/high-risk/settlement.
There is no measured justification for combining their entire loops or save
policies into a CombatSpaceRuntime or GameSession manager. Daily navigation,
fixed-step migration and global Profile-Flow failure transactions remain outside
this slice, not silently claimed solved.

After normal-play acceptance, next is consolidation closeout: summarize the
shared contracts and explicit policy exceptions, inspect dependency PR order
and cumulative regression evidence, and report whether the refactor can close.
Do not create another architecture slice solely because the former broad plan
listed one. Merges still require authorization; new gameplay remains paused
until that closeout/product review.
