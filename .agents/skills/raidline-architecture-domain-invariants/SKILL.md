---
name: raidline-architecture-domain-invariants
description: Design and review Project Raidline ownership, domain commands, stable IDs, persistence, inventory, settlement, shooting contracts, and C++ lifetime safety.
---

# Raidline Architecture and Domain Invariants

## Route and measure impact

Read the task's primary domain context and envelope. Before changing a shared definition such as AssetRecord, WeaponDefinition, ProfileState or HitResult, search consumers, persistence/content compatibility, tests and App projections. Use `python tools/raidline_governance.py impact --envelope <task.toml>`; do not limit review to compiler errors.

## Review ownership first

Identify the authoritative owner of every instance before reviewing behavior. An item instance has one location; action holds are explicit locations; death, interruption, deploy, and settlement must resolve every temporary location.

## Review command safety

- Queries do not mutate. Rejected commands preserve all participants.
- Multi-owner operations commit atomically and return a domain result suitable for App projection.
- Never retain `std::vector` references, iterators, or indexes across structural mutation.
- Definitions are immutable shared data; runtime instances are move-only and use stable IDs.
- UI, animation, names, and colors do not decide legality or semantic results.

## Review persistence and lifecycle

- Save versions, stable IDs, relationships, ID high-water marks, and idempotency keys.
- Never save pointers, UI cells, vector positions, or scene object addresses.
- Migrations are explicit and have round-trip, old-version, corrupt-main/backup, and repeated-settlement tests.
- Avoid global service locators, universal frameworks, and unconsumed future state.

## Review evidence

Run `python tools/raidline_governance.py architecture` and require focused invariant tests for success, rejection, capacity failure, duplicate ID, interruption, retry and idempotency. Authority changes also require persistence/migration and long-sequence labels. Treat compiler/static analysis as evidence, not proof of gameplay correctness.
