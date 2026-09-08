# Enemy Lifecycle consolidation — Phase A/B

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

- [ ] A: reproduce baseline bug with real Base shooting/configuration; characterize
  Raid and Defense; add reusable three-consumer lifecycle tests. Record red evidence.
- [ ] B: private authoritative collection, stable-ID results, single removal path
  including navigation/target attachments; migrate all three existing consumers.
- [ ] Verify nonlethal/lethal/multiple hits/single death, first-middle-last removal,
  survivor identity/position, no dead attack/nav/target, no accidental respawn.
- [ ] Full Windows Debug build, focused and full CTest, exact-head Windows/Ubuntu CI.
- [ ] STOP after B: actual before/after calls, code/test statistics, remaining debt.

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
