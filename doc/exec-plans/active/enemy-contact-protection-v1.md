# Enemy contact and damage protection v1

## Scope / dependency

2026-09-09: user requests execution of the staged consolidation plan. Explicit
stack on #153@d70f861, exact-head Windows/Ubuntu CI run 34247787567 passed.
#153 normal-play acceptance remains pending; continuation is not acceptance.
Main stays 2ae898a; no merge or history rewrite. New gameplay remains paused.

## Contact contract

Extract one narrow SDL-free resolver with three real consumers. It owns no
actors, navigation, Profile or save data. Inputs are one Enemy, target bounds,
activity eligibility, lazy LOS query and the existing protection timer. Output
is NoContact / Suppressed / Applied plus accepted damage, legacy damage and
control duration. Stable CombatTargetId remains the only source identity.

- Dead, ineligible, out-of-hitbox or occluded contact never consumes an attack.
- Valid Grab contact atomically becomes Bite and consumes its hit in that call.
- Every valid attack contact consumes at most once. During protection it is
  suppressed permanently, not queued until the interval ends; no damage/control
  fact or protection extension is produced.
- Accepted contact sets the existing 0.25s interval and emits the common damage
  observation. Raid's legacy three-HP adapter keeps its explicit legacy damage.
- LOS runs only after cheap opportunity/overlap checks; no extra whole-world
  scans, allocation, save or actor rebuild enters the contact hot path.

## Deliberate changes vs preserved policies

Changes: Daily requires LOS at contact (prevents through-wall hits), Daily Grab
now consumes Bite immediately, Daily/Defense consume suppressed contacts rather
than retrying them, and Raid suppressed Bite no longer applies control. These
are behavior corrections, not pure code motion, and require normal-play review.

Preserve: Daily/Defense shot-first vs Raid enemy-first; actor update/substep
timing, attack quota, AI/pathfinding, target/safe-zone policy, armor/wound
resolution, save/recovery and failure consequences. Base still has no player
control runtime; only Raid consumes an accepted control duration. No new Base
stun system. No schema/content/rules or art/audio/manifest/GDD changes.

## Gates and rollback

Pure resolver tests: Scratch/Bite/Grab, LOS laziness, wrong range, eligibility,
dead actor, same-hit replay, protected no damage/control/no extension, exact
expiry, checkpoint preservation and legacy damage.
Three real adapters: immediate Grab/Bite, protected contact consumed without
later damage, no wall contact, no dead contact, existing same-frame order.
Full build/focused/full CTest and exact-head Windows/Ubuntu CI. User normal play
last; agent does not launch the game. Revert this slice alone with no save
transformation. Do not automatically start frame-order migration or Session work.

## Evidence

Windows Debug configured and all affected targets rebuilt. Focused contact,
lifecycle, attack state, legacy Raid and defense regression: 89/89 passed
(4.44 seconds). Full CTest 1673/1673 passed (72.82 seconds), including the
existing serial defense performance gate. Exact-head Windows/Ubuntu CI will be
recorded in the PR, not by a second docs-only commit. Prior 1657 tests are
dependency evidence only. User normal play pending; no game launched.

## Actual before / after call chains

| Consumer | Before at #153 | After in this slice |
| --- | --- | --- |
| Daily | shooting/removal -> steer/collision -> protection gate -> overlap -> Grab confirm then next-frame Bite or consume hit -> damage observation | same shooting/movement -> shared contact with exposure + lazy static LOS -> accepted observation |
| Defense | shooting/removal -> existing substeps/navigation/breach -> protection/LOS/overlap -> Grab/Bite consume -> observation | same policy/substeps/breach -> shared contact -> accepted observation; earlier damage not erased by later suppression |
| Raid | enemy substep -> separate Grab and hit branches -> consume -> private damage/protection function -> control even if suppressed -> shooting | same enemy substep -> shared contact -> accepted formal observation or legacy HP -> accepted control only -> same shooting |

Changed production boundary: one header-only simulation mechanism, three call
sites, removal of Raid's duplicate private damage gate and timer constant; CMake
registers the header and resolver tests. No new runtime manager, registry,
Session member, snapshot DTO, allocation or global dispatch.

Added coverage: seven resolver tests plus three new contracts run through all
three real adapters (nine instances). Existing Grab characterization is
explicitly updated in all three adapters to the new immediate contract. Existing
same-frame lethal-shot characterization remains unchanged, so later order
migration cannot be disguised as this extraction.

## Consolidated normal-play review / next gate

After automated gates, user plays Daily perimeter, a defense encounter and Raid:
observe Scratch/Grab/Bite; overlapping attackers must not apply a burst of
damage or delayed protected hits; Raid protected Bite must not add a stun; wall
contact and Base safe core stay safe. Continue firing/killing and resuming a
defense to check no resurrection/rewind or stopped survivors. No debug buttons
are required. Base has no new stun; only its existing damage/rescue policy.

Stop after this slice. Review the retained same-frame kill/attack ordering and
its consequences before any reorder. Broader Navigation, CombatSpace facade,
Session or Persistence work requires an actual residual duplication/ownership
problem, not merely the next old-plan heading.
