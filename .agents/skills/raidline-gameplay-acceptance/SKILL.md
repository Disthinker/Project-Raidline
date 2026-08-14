---
name: raidline-gameplay-acceptance
description: Plan and execute Project Raidline real-window player-flow acceptance, capture defects, and separate product judgments from implementation regressions.
---

# Raidline Gameplay Playtest and Acceptance

## Build an executable checklist

Derive checks from the active ExecPlan. Each check states initial state, player inputs, visible expected outcome, authoritative state expected after the action, and failure evidence to capture.

## Test player flows

- Use the exact built revision and record window/build identity.
- Cover the complete vertical path, not isolated screens only.
- Include cancellation, capacity failure, repeated input, death/quit, return flow, and a second run when relevant.
- Do not infer domain correctness solely from visuals; pair manual findings with state/test evidence.

## Classify findings

- Regression: violates an accepted contract.
- Product issue: the contract works but is confusing, tedious, or not fun.
- Scope request: requires an excluded system or materially expands the slice.
- Presentation debt: behavior is readable with fallback but lacks authorized final art/audio.

Only product-pillar, loss-model, business-model, narrative, or major-scope findings require user direction. Ordinary defects return to the active slice.
