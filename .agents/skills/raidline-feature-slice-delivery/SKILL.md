---
name: raidline-feature-slice-delivery
description: Implement one Project Raidline vertical product slice from domain contract through playable behavior and regression evidence.
---

# Raidline Feature Slice Delivery

## Fix the contract

Read the active scope contract and ExecPlan. State the player outcome, owned state, commands, results, exclusions, dependency baseline, automated gate, manual gate, and rollback before implementation.

## Deliver vertically

- Start at domain values and ownership, then wire runtime composition, App projection, and visible interaction.
- Keep definitions separate from instances and use stable IDs and capabilities instead of display-name branches.
- Query before command; make multi-owner transitions atomic and non-mutating on failure.
- Add only abstractions with a current slice consumer.
- Preserve excluded systems as documented boundaries, not empty runtime frameworks.

## Verify continuously

Run focused tests after each domain change, rebuild affected targets after header/layout changes, then run full CTest. Perform the ExecPlan's visible player-flow checks before handoff.

## Hand off

Update current state and the ExecPlan with actual evidence, unresolved risks, and rollback. Keep unrelated defects and future content out of the branch.
