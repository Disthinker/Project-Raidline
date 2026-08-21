---
name: raidline-feature-slice-delivery
description: Implement one Project Raidline vertical product slice from domain contract through playable behavior and regression evidence.
---

# Raidline Feature Slice Delivery

## Fix the contract

Read the routed domain context, task envelope, active scope contract and ExecPlan. State the player outcome, owned state, commands, results, exclusions, dependency baseline, automated gate, manual gate and rollback before implementation. Do not default-load adjacent domains.

## Deliver vertically

- Start at domain values and ownership, then wire runtime composition, App projection, and visible interaction.
- Keep definitions separate from instances and use stable IDs and capabilities instead of display-name branches.
- Query before command; make multi-owner transitions atomic and non-mutating on failure.
- Add only abstractions with a current slice consumer.
- Preserve excluded systems as documented boundaries, not empty runtime frameworks.
- Prefer one larger player-result PR over a chain of architecture-only PRs when the migrations share the same accepted baseline and direct consumer. Preserve reviewability through domain/service/client commits and explicit rollback points.
- Run impact analysis before editing authoritative shared types; expand the envelope before crossing allowed paths or protected domains.

## Verify continuously

Run routed area tests after each domain change, rebuild affected targets after header/layout changes, then run Sentinel, the risk-selected Integration/migration/long-sequence gates, full CTest and exact-head CI. Put the consolidated visible player-flow checklist last for the user; do not launch the game on the user's behalf.

## Hand off

Run repository postflight and update the task/ExecPlan with actual evidence, unresolved risks and rollback. Update stable state/architecture/roadmap only when their owned accepted fact changed; keep transient PR evidence in the PR. Keep unrelated defects and future content out of the branch.
