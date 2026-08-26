# Base Morale and Periodic Events v1

This ExecPlan is the living delivery record for the first formal resident
morale loop. It follows `doc/exec-plans/PLANS.md` and remains active until the
slice is accepted or superseded.

## Purpose

Make Base shortages, beds, five-day wishes and low-frequency community events
change a visible resident state and one existing production decision. This is
not a city simulation: the player receives a concise daily report, understands
why morale changed, and can choose the next Raid or Base action accordingly.

## Product contract

- Formal morale is a separate `Low / Stable / High` resident state. The legacy
  `baseResources.pool.morale` value remains Operations Support and cannot be
  interpreted as resident morale.
- Morale resolves only at the daily 00:00 boundary. A day can move at most one
  tier. Ration or bed shortages are negative reasons; supported operation
  recovers Low morale toward Stable; a fulfilled wish or positive community
  event can raise Stable morale to High.
- Wish fulfillment and missed wishes write reasons into the next daily ledger.
  They do not modify the morale tier immediately and cannot be repeated to
  farm multiple same-day changes.
- One versioned community event is active for each five-day world-time cycle.
  Selection uses a stable profile/cycle seed, is saved, and cannot be rerolled
  by opening a page or restarting. V1 events contribute one bounded positive
  or negative reason; they do not spawn quests, rewards, items or threats.
- Low, Stable and High morale affect only the duration frozen when a new
  manufacturing order starts: 120%, 100% and 90%. Existing orders never change
  duration retroactively.
- Sustained Low morale is displayed and persisted, but V1 does not remove
  residents, alter named NPCs, trigger rebellion, change combat difficulty or
  block Raid deployment.
- Allocation & Needs displays the formal tier, trend, last daily reasons,
  sustained-low duration, active event and next event rotation using existing
  bilingual text/geometry fallback.

## Architecture and persistence

1. Add versioned morale rules and community-event definitions with stable IDs.
2. Add `BaseMoraleState`, last-day ledger and current event snapshot to
   `ProfileState`; no item or scene object owns these facts.
3. Add a pure daily morale reducer and deterministic event synchronizer. Large
   time jumps use bounded summaries rather than iterating every elapsed day.
4. Include morale and event state in schema v18, Profile validation,
   fingerprinting and pending-Raid travel rollback snapshots.
5. Centralize world-time consumer order: daily needs, daily morale, wish/event
   rotation, construction, manufacturing and resident treatment.
6. Manufacturing query consumes only the current morale projection to freeze
   an adjusted duration; App never calculates productivity.

## Verification

- Content rejects duplicate/unknown event IDs, empty labels, invalid effects,
  invalid cycle duration and unsafe productivity percentages.
- Domain covers one-step daily movement, Low recovery, High qualification,
  shortage precedence, pending reasons, large catch-up, stable event selection
  and no immediate tier changes.
- Manufacturing covers 120/100/90 percent start duration and proves active jobs
  are unchanged by later morale changes.
- Persistence covers schema v18 round-trip, v17 migration, corrupt morale/event
  data, pending-Raid rollback and Profile fingerprint changes.
- GameSession covers Base simulation, rest and settled Raid world-time paths.
- Windows Debug full build, full CTest, exact-head Windows/Ubuntu CI, then one
  consolidated user normal-play acceptance. Codex does not launch the game.

## Progress

- [x] PR #89 accepted and normally merged as `194f910`; this branch was created
  from that clean `origin/main`.
- [x] GDD/current-code conflict checked: Operations Support remains legacy and
  is not reused as formal morale.
- [x] Domain, content and persistence implemented.
- [x] GameSession, manufacturing consumer and Allocation UI implemented.
- [ ] Automated verification complete locally: Windows Debug full build and
  1001/1001 CTest pass. Exact-head Windows/Ubuntu CI remains pending.
- [ ] User normal-play acceptance complete.

## Rollback

Before merge, close the Draft PR and discard
`codex/base-morale-periodic-events-v1`. After merge, revert its merge commit.
Schema v18 adds only morale/event state; v17 migration initializes Stable morale
and a deterministic current event. No art, audio or manifest rollback is
required.
