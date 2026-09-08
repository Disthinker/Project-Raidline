# Navigation refresh selection contract v1

## Baseline and scope

User accepted #151 on 2026-09-08. Its exact head 47c696b passed both CI jobs
(run 34186994633). Main remains 2ae898a; #149/#150/#151 remain unmerged.
This explicitly stacked branch depends on #151, not an implicit main baseline.

Inspection found distinct policies, not two identical AI systems: Raid searches
eligible requests; Defense grants one rotating actor a turn before its cooldown
check. Preserve both. Extract only bounded, allocation-free live-actor selection
with stable CombatTargetId result. Cursor remains a scheduling offset in the
existing DTO, never a target identity. No new saved fields or version changes.

Daily remains direct steering. Do not change target policy, cooldowns, update
order, path algorithms, attack timing, spawn/waves, Session/save or consequences.
No God Object, new gameplay, art, audio or manifest work.

## Implementation and gates

- Shared pure selection over a read-only actor view, activity eligibility and scan budget.
- Raid passes all actors as scan budget; Defense passes one. Both keep one query
  maximum per substep and resolve the result by stable ID.
- Tests: empty/zero budget, wrap/large cursor, dead/ineligible, all blocked,
  fairness, removal/reordering, both legacy-policy traces.
- Existing real Raid/Defense pursuit, lifecycle and performance tests remain;
  Windows full build/CTest and exact-head Windows/Ubuntu CI required.
- User normal-play last: existing Raid pursuit and Defense approach/retarget
  remain unchanged after kills. Agent does not launch game.

## Rollback and next gate

Revert this slice alone; no saved-data transformation. Do not automatically
continue to combat ordering or Session/Persistence. Assess actual remaining
duplication after this contract; target-policy differences are intentional.

## Evidence

- User normal-play accepted; exact head a98090a Windows/Ubuntu CI run
  34188461419 succeeded. PR #152 remains unmerged. Next explicit stacked scope
  is incoming enemy damage facts, not all remaining architecture phases.

- Implemented shared selection and migrated both real consumers. Raid no longer
  carries a selected-for-refresh array flag; it compares the selected stable ID.
- Windows Debug full incremental build passed; full CTest **1645/1645**, zero
  failures, **55.63s**. Includes eight added selection contracts and unchanged
  real-activity navigation/lifecycle/checkpoint/performance regressions.
- Exact-head CI and user normal-play acceptance remain separate gates; final
  CI result is recorded in the stacked PR, not by a second docs-only commit.
- Before: Raid's local scan and Defense's local modulo each exported an array
  position. After: both call selectNavigationRefresh and consume CombatTargetId.
  Defense still applies cooldown after choosing its turn; Raid still filters
  eligibility before choosing. No path or target-policy code was merged.
- Remaining debt: refresh invalidation/goal choice differ by activity and are
  not demonstrated duplicate algorithms. Next assess actual frame/attack facts
  and same-frame ordering before proposing any behavioral convergence. Full
  Navigation/Combat/Session/Persistence consolidation is not approved by this slice.
