# Gameplay timing CTest isolation

## Scope and baseline — 2026-09-09

User accepted and authorized merging #149–#157. All nine ordinary merges are
complete; main c3eb19c is tree-identical to accepted a68a82e. Post-merge local
Windows CTest passed 1697/1697 in 64.99s. The next gameplay proposal is docs-only
Draft #158; no fortification code is authorized by this CI repair.

Final-main CI run 34323812733 failed Windows test #551 only: the 100-enemy
navigation test measured one 61185us update against its unchanged 25000us guard.
Total simulation was 418ms against 1000ms; identity/fairness/workload assertions
passed. This is a real failed gate, not a gameplay pass or proven OS-only noise.
The same main run passed Ubuntu 1697/1697 in 71.00s; Windows was 1696/1697.

CTest metadata confirms four GameplayWorldPerformanceTest cases were parallel;
BaseDefensePerformanceTest already had RUN_SERIAL. Twenty isolated local runs
of the unchanged failing test passed (118–123ms totals; worst update 2118us).
This supports testing scheduler contention before touching production navigation;
it does not prove that every hosted overrun is external scheduling.

## Focused change / boundaries

Branch codex/ci-gameplay-timing-isolation starts at accepted origin/main c3eb19c,
not the open review branch. Apply RUN_SERIAL only to discovered
GameplayWorldPerformanceTest cases using Google's supported post-discovery
TEST_INCLUDE_FILES hook. Preserve CMake 3.15, all registered names, ordinary
parallel tests, production sources, test bodies, thresholds and save formats.
Do not add retries, remove cases or broaden into a performance framework.

## Gates and rollback

- Compare CTest JSON before/after: all four timing cases serial, ordinary cases
  remain parallel, 1697 unique names and commands unchanged.
- Reconfigure windows-debug, build affected targets, run timing cases repeatedly
  and full CTest; exact-head Windows/Ubuntu CI before handoff.
- A repeated isolated failure still blocks delivery and requires measured
  runtime diagnosis; do not loosen the 25ms guard or retry until green.
- No manual game launch/acceptance required for scheduling-only change; this
  does not grant acceptance to future fortifications. Ordinary revert is safe,
  with no schema/asset migration or gameplay consequences.
- Record final evidence in the PR; do not merge this new repair without explicit
  authorization. Main's initial failed CI remains visible in the evidence.

## Progress

- [x] Captured failed main run, per-case registration gap and 20 local baseline repeats.
- [x] Added bounded post-discovery scheduling, without changing assertions.
- [x] Reconfigured windows-debug and built all targets (unchanged binaries).
- [x] CTest JSON: 1697 names/commands identical; exactly four Gameplay timing
  cases plus the existing Defense timing case are serial; ordinary cases remain parallel.
- [x] Four timing cases repeated ten times each: 40/40 executions passed in 14.31s.
- [x] Full local Windows CTest: 1697/1697 in 69.75s; timing cases run after
  concurrent workload completes, with the original limits intact.
- [ ] Exact-head Windows/Ubuntu CI (final results recorded in the PR).
