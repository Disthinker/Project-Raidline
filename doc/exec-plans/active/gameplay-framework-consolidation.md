# Enemy Lifecycle consolidation — Phase A/B

## Current status override — 2026-09-09

Phase A/B (#150) and the subsequently authorized narrow slices #151–#156 are
implemented. User accepted integrated #156@f16544d; exact-head Windows/Ubuntu CI
passed. The mandatory Phase B pause was followed by individual decisions, not
blanket authorization for the former broad phases. The closing review is now
complete: [actual contracts, policies, debt and delivery gates](../../architecture/GAMEPLAY_FRAMEWORK_CLOSEOUT.md).
Code consolidation can close; the dependency chain remains unmerged and new
gameplay remains paused. All pending statements below describe their historical
slice, not today's integrated build. No further architecture phase is automatic.

## Scope and baseline

2026-09-08: user pauses new gameplay. Implement only characterization and Enemy
lifecycle consolidation, then STOP and assess actual remaining debt. Baseline
`d43bab05b21e7950d0c2e46613c6c94dd4e709c5`; main `2ae898a`; dependency Draft
PR #149 is NOT accepted. Work lives on `codex/gameplay-framework-consolidation`,
with a stacked PR targeting #149's branch. No merge/force-push. Existing baseline:
1606 tests; this is not evidence for new code.

Reuse Enemy/CombatTargetId, shooting, collision, content and save DTOs. Consolidate
authority over registration, death, removal and attached Nav/Target cleanup.
Keep spawn, destination, waves/patrol, rewards, failure and save policies local.
Do not change frame order, introduce Daily pathfinding, new gameplay, art/audio,
Manifest, schema, ECS, event bus, or a universal manager. GameSession consumes
profile-relevant facts; it does not clean up transient actor collections.

## Architecture debt / baseline matrix

Debt: Gameplay Lifecycle / Activity Orchestration Architecture Debt. Shared
algorithms have independently wired callers. Daily ignores the shooting removal
result; Session updates only surviving IDs; configureHomePerimeter then rebuilds
all actors on count mismatch. This can rewind survivors and resurrect kills.
The parallel spawn array also loses alignment after deletion.

| Lifecycle | Daily/Base | Defense | Raid |
| --- | --- | --- | --- |
| Spawn / owner | perimeter snapshot / BaseWorld | frozen waves / BaseDefenseRuntime | deploy + space snapshots / GameplayWorld |
| Update | local loop, after shooting | local substeps, after shooting | local substeps, before shooting |
| Navigation | direct steering, collision | budgeted paths + checkpoint waypoints | budgeted paths + parallel navigation array |
| Destination | player or spawn | exposed player or wave objective | player, last known, patrol/guard |
| Damage/death | shared hit resolver | shared hit resolver | shared hit resolver |
| Death consumption | discarded | before/after ID difference | removed vector indices |
| Removal | resolver erases enemies | resolver + breach erase | resolver erases enemies |
| Associated cleanup | spawn array omitted | reservations/checkpoints independently pruned | navigation/encounter arrays manually erased |
| Consequence | perimeter health snapshot | killed/breached wave IDs | kill score |
| Session/save | updates found IDs only | resumable consistent checkpoint | existing Raid/pending/settlement policy |
| Exit/restore | count-triggered reconstruction | explicit checkpoint restore | activity/space transition |

Pure mechanism: identity, alive/dead transition, one death fact, removal and
attached-state lifetime. Policy: the destination, spawn timing, consequences and
save timing. Existing damage numbers are not rebalanced in this work.

## Steps and gates

- [x] A: reproduce baseline bug with real Base shooting/configuration; characterize
  Raid and Defense; add reusable three-consumer lifecycle tests. Record red evidence.
- [x] B: private authoritative collection, stable-ID results, single removal path
  including navigation/target attachments; migrate all three existing consumers.
- [x] Verify nonlethal/lethal/multiple hits/single death, first-middle-last removal,
  survivor identity/position, no dead attack/nav/target, no accidental respawn.
- [x] Full Windows Debug build, focused and full CTest, exact-head Windows/Ubuntu CI (#150: run 34177559722; later integration evidence in closeout).
- [x] STOP after B: actual before/after calls, code/test statistics, remaining debt; later work proceeded only as separately accepted narrow slices.

Reproduction tests first run red locally; normal committed gates must be green,
not permanently disabled or weakened. Header/layout changes rebuild all affected
targets. Preserve navigation query and existing <25ms simulation/performance gates.
No full-profile copy or synchronous save is added to the shot hot path.

## Delivery and rollback

Keep characterization, lifecycle migration and final evidence in coherent commits.
Revert dependent commits in reverse order without rewriting shared history. No save
version change is planned. Preserve #149's schema-v46 compatibility boundary.
Only relevant Known Issues, this plan and current-state pointers are updated.

User normal-play gate (agent does not launch the game): kill multiple moving
perimeter enemies and verify no rewind; defend and save/resume the same event;
Raid combat, obstacle pursuit and interior transition, then extract. Safety core,
soft failure and asset policies must remain unchanged. New gameplay stays paused
until structural bug fix, common contract, full tests/CI and user acceptance pass.

## Evidence

- Branch created from exact `d43bab0`; worktree was clean.
- Phase A red evidence: `BaseWorldTest.LethalPerimeterShotDoesNotRebuildSurvivorsFromStaleSnapshot`
  compiled against unchanged production baseline. A real shot removed ID 11;
  repeating same-cycle configuration restored size 2 instead of expected 1.
  Test failed in 34ms at the resurrection assertion, not at shot setup.
- Initial B focused pass: 138 existing/added BaseWorld, GameplayWorld,
  BaseDefenseRuntime and HitResolution tests, 4.86s. The red reproduction now
  passes. Not yet a final full-suite or CI result.
- Final local Windows Debug: all affected targets rebuilt; **1625/1625 CTest,
  zero failures, 54.60s**. Baseline 1606 retained; 19 added: 12 parameterized
  actual-activity contracts, 4 ownership/restore/transition tests, 2 real temporary-directory
  Session save/rejection tests, and the original real-shot Base reproduction.
- Contract fixtures arrange deterministic actors and logical flights through
  narrow test friendship, then call actual Daily/Defense/Raid updates. They do
  not initialize SDL windows/audio or touch player saves. Tiny-step contracts
  isolate lifetime/position effects; existing full-step navigation, shooting,
  Defense continuation and mixed performance tests remain enabled.
- Unchanged Defense stress: 16 enemies/1000 blockers/1000 assets, 45 shots,
  12 inventory operations, 14 damage operations, 7 medical operations.
  Checkpoint copy P95/P99 **1.0099/2.5836ms**, simulation max **0.5889ms**,
  no-SDL main-iteration P95/P99/max **8.8091/11.2854/13.8123ms**. This is not
  a rendered FPS or shipping hardware guarantee. No threshold relaxation.
- CI is recorded on the stacked PR at its exact head. Existing workflow only
  automatically triggers PRs targeting main, so dispatch that unchanged workflow
  explicitly for the integration branch; verify run head SHA and both jobs.

## Phase B actual Before / After

Before: three callers passed mutable enemy vectors to shooting. Hit resolution
erased the vector and exported indices; Daily dropped the result, Raid erased
parallel arrays, Defense inferred kills from set differences and separately
erased breached actors. All were exposed to shape-change omissions.

After:

```text
Daily / Defense / Raid (existing spawn and frame-order policies)
  -> WorldShootingRuntime::advanceShots(EnemyLifecycle&)
  -> resolveShotHits (damage + stable target IDs)
  -> EnemyLifecycle::removeDead
       -> remove bound attachment by CombatTargetId
       -> compact private actor vector
       -> invalidate squad membership reservations
  -> stable EnemyRemovalFact values -> activity consequences
       Daily: Session records existing DTO health=0
       Defense: killed IDs advance existing wave outcome
       Raid: existing shot kill score
```

Defense objective/breach requests use `removeForObjective` on the same lifecycle,
with a distinct reason. Explicit restore imports existing tombstones, not new
death consequences. No new ID domain, schema, content or rules version.

| Responsibility | Before | After |
| --- | --- | --- |
| Actor collection mutation | consumer vectors + shared resolver + Defense erase | private EnemyLifecycle collection; consumers submit spawn/removal requests |
| Dead cleanup | caller-dependent | one mandatory path inside shared resolver |
| Spawn/nav/target attachment lifetime | 5 parallel array declarations across Daily and Raid; Defense uses DTO as nav cache | EnemyRoster<State> owns ID-bound attachments and removes them automatically |
| Raid attachment access helpers | four vector getters | removed; state(id) |
| Defense death inference | before/after ID scan | explicit removal facts |
| Attack membership | caller cleanup / implicit next decide | invalidated by shared lifecycle on structural removal |
| Daily reload | same-cycle count mismatch rebuilds all | explicit reset/new cycle only; dead DTO records not spawned |
| Session | misses removed IDs | 9-line consumption of Profile-relevant death facts, no transient cleanup |
| Spawn/target/waves/reward/failure/save policy | each activity | still each activity |

The new shared lifecycle header is 154 lines (including comments/formatting);
no CombatSpaceRuntime, manager framework or new runtime translation unit was
introduced. Changed production-file and insertion/deletion totals are measured
from the final PR diff, not asserted as a deduplication percentage. Insertions
include migrated call sites; deletions include adapters/comments, not all are
"duplicate algorithms removed".

## Mandatory pause and remaining debt

Final ownership checks prohibit base-only assignment (which could omit typed
attachments). Starting a new Defense activity clears only the candidate's old
Daily logical flights: local target IDs must not alias targets in another
activity. The live Daily state is unchanged if preparation is rejected, and
same-event checkpoint restore still retains its own flights. This explicit
transition cleanup has a real-adapter regression test; it does not change
simulation order, navigation algorithms or the save schema.

Phase B implementation is finished. **Do not start Phase C or new gameplay.**
Full CI and user normal-play gates remain separate from local automation.

Remaining real duplication: Raid and Defense still independently refresh/issue
navigation queries and run enemy updates. Daily intentionally retains direct
steering. Frame order remains Raid enemy-first, Daily/Defense shot-first. Those
are not silently unified in this lifetime PR. Save trigger/recovery policies
remain distinct and are not evidence that all persistence code must be merged.

Recommended next decision AFTER user verification: inspect whether a narrow
navigation scheduling helper is warranted for the two existing path consumers;
treat Daily pathfinding and frame-order convergence as separately accepted
behavior changes. Do not automatically approve Combat/Session/Persistence phases.

Future Rescue can reuse this registry, death/target cleanup and contract harness
without writing deletion glue. It would still need to reuse/select an existing
update orchestration; this phase does NOT claim a universal shared combat pipeline
or zero integration cost for a new activity.
