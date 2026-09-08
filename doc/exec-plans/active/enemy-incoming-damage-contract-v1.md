# Enemy incoming damage facts v1

## Accepted dependency and narrow scope

User accepted #152 and requested continued framework refactoring. Explicit stacked
base a98090a, exact Windows/Ubuntu CI run 34188461419 succeeded; main 2ae898a.
Do not merge dependencies. Use existing stable CombatTargetId and damage domain.

## Audit and deliberate correction

Daily currently drops the consumed attack type, passes legacy damage (1/2), and
Session guesses torso/Scratch. Defense passes separate damage/type scalars and
Session reconstructs the meaning with a Scratch fallback. Raid emits a full
PlayerDamageObservation but duplicates the type-to-wound conversion.

Move that existing observation contract into a shared header, add source ID and
actual attack type, and produce it in all three real runtimes after a hit is
consumed. Session applies the observation, preserving its activity-specific random
stream, save/rollback and consequence. Keep scalar getters as derived compatibility
views for current consumers; no second authoritative damage state.

Intentional behavior fix: newly resolved Daily Scratch uses formal 12 torso damage,
Bite 18 head damage and Bite wound source, as already used in Raid/Defense, instead
of legacy 1/2 HP + guessed torso/Scratch. Armor still resolves in the domain.
Old HP/checkpoints are not rewritten; schema46/content60/rules29 unchanged.

## Preserved differences (not silently unified)

| Contract | Daily | Defense | Raid |
| --- | --- | --- | --- |
| Order | shot then enemies | shot then enemy substeps | enemy substeps then shot |
| Grab follow-up | confirm then next update Bite | confirm and consume Bite same substep | confirm and consume Bite same substep |
| Protection | skips consumption while protected | skips consumption while protected | consumes attack, filters damage while protected |
| Consequence | safe Base recovery | public soft failure | Raid failure/settlement |

Retain these timing/control policies and 0.25s interval. Characterize them explicitly.
No new contact resolver/God Object, Daily pathfinding, global ordering migration,
Session/Persistence redesign, art/audio/manifest or GDD changes.

## Verification / rollback

Real three-activity tests: Scratch/Bite facts, source ID, no duplicate hit, protection,
dead actors, and same-frame lethal-shot ordering. Domain mapping and armor/wound
tests plus existing saved Defense/Session and performance gates. Windows full build,
focused/full CTest, exact-head Windows/Ubuntu CI; user normal play last, no game launch.
Revert slice commit alone; no save transformation. Next review ordering only with
explicit behavioral evidence; do not automatically advance all remaining phases.

## Evidence

- Implemented shared value contract and factory; Base/Defense scalar compatibility
  getters now derive from one observation. Raid uses the same factory. Session's
  guessed Scratch fallback and Daily fixed torso/armor values removed.
- Windows Debug full incremental build passed, including header-dependent targets.
  Added 12 real-activity cases passed (0.53s); full CTest **1657/1657**, zero failures,
  **58.68s**. Existing navigation, save/rollback and mixed performance gates passed.
- Initial fixture failures were protected-zone placement (no eligible contact),
  corrected to a legal exposed perimeter arena; no production safety gate weakened.
- Exact-head Windows/Ubuntu CI recorded in stacked PR; user normal-play pending.
  No game launched, no merge, no art/audio or save schema change.
