---
name: raidline-gameplay-acceptance
description: Prepare Raidline user playtests or triage gameplay feedback without launching the game; not automated builds or feature explanations.
---

# Gameplay Acceptance

Use the active plan and exact built revision to give the user one consolidated normal-play flow after automation. State starting conditions, ordinary controls and visible outcomes. Include cancellation, capacity, repeated input, death/quit or a second run only where the changed behavior needs them. Do not require special single-step buttons or launch the executable on the user's behalf.

When feedback arrives, pair visible evidence with the affected contract/tests. Distinguish:
- Regression: accepted contract broken; investigate and propose the minimal fix, then wait for the user's fix-plan approval.
- Product issue: works but is confusing, tedious or not fun.
- Scope expansion: needs excluded systems or a materially different product decision.
- Presentation debt: usable fallback without authorized final art/audio.

Every user Bug requires the feature-slice-delivery diagnosis/plan/approval flow before real code changes, regardless of size. Ordinary failures found while implementing an already approved plan can be corrected within contract. Record human acceptance separately from CI/tests and bind it to the unchanged candidate. Waiting acceptance stays waiting after a vague “continue”; explicit acceptance of this candidate triggers agreed delivery via project-governor without a habitual second merge question. Report gaps honestly; screenshots alone do not establish domain correctness.
