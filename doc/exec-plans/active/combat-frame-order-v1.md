# Combat frame order v1

## Scope and baseline

2026-09-09: user accepted #154@b7937c3 and requested the next planned step.
Exact-head Windows/Ubuntu CI run 34298809360 passed (1673 tests each).
Clean explicit stack: `codex/combat-frame-order-v1` based on #154, PR base
`codex/enemy-contact-protection-v1`. Main is still 2ae898a. No merge authorized.
The accepted contact/lifecycle slice is retained, not reimplemented.

## Review and decision

Current real-adapter characterization proves: an active Scratch and a lethal
logical shot in the same frame produce contact in Raid, but not Daily/Defense.
This is a genuine result difference, not just duplicated code. Converge on
Raid's existing order: enemy movement/contact -> shot resolution/death cleanup.
Move only Daily and Defense shooting calls. Do not build an orchestration God
Object or extract the entire enemy loops merely to share two sequential calls.

Successful gunfire must still alert surviving enemies after shot resolution,
as Raid's existing Session notification does. Hearing is recorded this frame;
navigation/movement responds on the next enemy update. Never infer noise from a
raw fire button (dry/rejected/sprint-blocked input is not a shot). Keep each
activity's radius, exposure eligibility and target policy.

## Intentional changes and preserved boundaries

- Daily/Defense active contact is no longer retroactively canceled by a
  same-frame killing shot. A kill before the attack window still prevents it.
- Hits sample enemies after movement. Nonlethal impact slowdown begins affecting
  their following update, not their earlier movement in the same frame.
- Defense objective/breach retirement precedes shots; an already retired actor
  cannot also yield a kill. If enemy advance reaches a terminal defense result,
  skip shooting entirely; no new bullet/ammo/flash after the activity ended.
- Preserve existing substeps, attack quotas, protection, AI/nav policy, safe
  core, sounds/radii, death cleanup, stable IDs and Profile consequences.
- This contract orders simulation facts, not Profile mutations. Armor/health,
  rescue/failure and ammo transactions stay in existing Session consumers.
  Base still has no Raid stun runtime. No damage-first Session rewrite.
- No fixed-60-Hz migration, Daily pathfinding, new gameplay/art/audio/manifest,
  GDD writes, new save fields or schema/content/rules bump (46/60/29).

## Work and gates

1. Red same-frame contract on unchanged #154 production code; record results.
2. Minimal two-call-site migration and post-shot hearing; preserve terminal
   and stable-ID removal contracts, never use invalidated squad indices.
3. Three real activities: lethal/nonlethal/early kill, one death/source identity,
   next-frame dead actor inactivity and suppression. Defense: breach vs hit,
   terminal input, checkpoint/resume. Daily/Defense: accepted/no-shot hearing
   and response timing. Existing session/save, real shots and performance gates.
4. Windows Debug all affected targets, focused/full CTest, exact-head Windows
   and Ubuntu CI, then user normal play; agent never launches the game.

## Delivery / rollback / next decision

One focused stacked PR. Revert this slice without save migration; parent retains
all accepted lifetime and contact fixes. Record actual Before/After and evidence.
User plays perimeter, defense and Raid: normal pursuit/fire/grab/multi-enemy
contact, behind-wall gunfire, final defense outcome and save/continue. No debug
button or frame-perfect manual test is required; automation owns exact ties.

Stop after the slice. Assess remaining runtime boundary/duplication before
choosing further work; no automatic full Navigation/Combat/Session/Persistence
rewrite and no restart of new gameplay.

## Evidence

- Red on unchanged #154 production: the new same-frame contract failed in
  Daily and Defense (zero facts vs expected one), passed in Raid; 0.50s total.
- Minimal production changes limited to BaseWorld and BaseDefenseRuntime's
  implementation/private step signature. No Raid, Session or DTO edits.
- Windows Debug full affected-target build passed. Focused 95/95 passed in
  4.66s, including four new parameterized contracts in the three actual worlds,
  accepted/sprint-blocked hearing, and breach/terminal/resume behavior.
- Full local CTest 1687/1687 passed (73.65s), 14 new registered tests plus the
  explicitly migrated same-frame contract. Existing serial defense performance
  gate passed without threshold changes. Exact-head CI is recorded in the PR
  after completion, without a docs-only follow-up commit.
- User normal play pending; game not launched. No merge.

## Actual Before / After and residual responsibilities

| Activity | Before #154 | After |
| --- | --- | --- |
| Daily | shots/cleanup -> perception (including shot) -> steer/contact | perception/steer/contact -> shots/cleanup -> accepted shot hearing |
| Defense | spawn -> shots/cleanup -> substeps (perception/contact/breach) -> checkpoint | spawn -> same substeps -> if nonterminal shots/cleanup -> hearing -> checkpoint from current roster/squad |
| Raid | enemy substeps/contact -> shots/cleanup -> existing Session shot notification | unchanged |

Existing completed/inactive-frame guards remain. Defense now explicitly skips
dead actors while producing squad snapshots/advancing, rather than depending on
an earlier shot pass to remove them. Shooting's shared lifecycle still owns
death removal; the activity does not regain collection erase authority.

All saved state fields keep their meanings. Already frozen checks/actors are
not regenerated; resumed subsequent frames use the new stated ordering. The
existing within-frame Profile ammo/health transaction order is unchanged and
must not be advertised as a global damage-first transaction migration.

Further boundary review should examine actual activity-entry/exit and checkpoint
consumers, not merge update loops with distinct patrol/wave/objective policy.
Any proposed facade must remain composition-only and justify real consumers;
if no residual correctness/duplication warrants it, close consolidation instead.
