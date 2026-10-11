---
name: raidline-architecture-domain-invariants
description: Review Raidline ownership, Enemy lifecycle, transactions or save-contract changes; not presentation-only edits.
---

# Architecture and Domain Invariants

Trace the changed execution path and authoritative owners. Read the affected sections of doc/architecture/INVARIANTS.md; consult ARCHITECTURE.md for composition. Root AGENTS.md supplies shared guardrails.

Check the contracts actually touched:
- Asset/location changes: exactly one owner; temporary action locations resolve on interruption/death; queries do not mutate and rejection preserves all participants.
- Enemy lifecycle: stable existing Enemy/CombatTarget identity; one death fact; removed/dead actors cannot attack, navigate or remain targets. Exercise real Daily/Defense/Raid adapters for shared lifecycle changes, including same-frame hits and first/middle/last removal with survivors unchanged.
- Runtime composition: keep shared mechanism separate from spawn/target/wave/reward/save policy. Do not move transient internals into GameSession or an all-purpose facade.
- C++ lifetime: no dangling vector references across structural mutation or commit; verify ownership/move and compiled consumers.
- Persistence changes only: explicit schema/ID mapping, high-water validation, old/frozen snapshots, round trip, corrupt-main/backup recovery and repeated settlement. Do not upgrade schema simply to tidy architecture.

Add focused success and failure tests for the changed contract (capacity, interruption, retry, duplicate identity as applicable). Report actual before/after call paths, intentional behavior changes and validation gaps; compilation alone is not behavioral proof.
