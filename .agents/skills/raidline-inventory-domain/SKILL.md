---
name: raidline-inventory-domain
description: Implement or review Project Raidline item, ground pickup, grid inventory, container, equipment, or stash behavior while preserving definition-versus-instance identity, move-only ownership, stable IDs, footprints, grid bounds, row-major placement, self-overlap, transactional failure, SDL separation, and keyboard-focus versus mouse-hover semantics. Use for any Raidline inventory-domain change.
---

# Raidline Inventory Domain

## Load the domain baseline

Read these sources before editing:

- `doc/architecture/INVARIANTS.md`
- `doc/project/CURRENT_STATE.md`
- the relevant active ExecPlan under `doc/exec-plans/active/`
- `src/item_definition.*`, `src/item_instance.*`, `src/ground_item.*`, and `src/grid_inventory.*`
- relevant tests and Week 14–16 DevLogs for historical rationale

Treat source and tests as current authority when a DevLog's branch or PR statement is stale.

## Preserve identity and ownership

- Keep `ItemDefinition` as shared static data and `ItemInstance` as unique runtime identity.
- Preserve move-only `ItemInstance`; ID `0` is invalid and moved-from instances stay explicitly invalid.
- Store stable `ItemInstanceId` across frames and mutations, never `std::vector` index, iterator, or element reference.
- Make ownership transfers explicit. A failed pickup, placement, move, or future cross-container transfer must not consume or duplicate an instance.

## Preserve grid transactions

- Keep positive grid dimensions, row-major cell mapping, in-bounds complete footprints, unique instance IDs, and non-overlap.
- Use `canPlace`/`canMove` as side-effect-free facts and `tryPlace`/`tryMove` as commit boundaries.
- Validate the entire operation before mutation; keep all observable state unchanged on failure.
- Allow a moving item to overlap its own old footprint and treat same-origin move as a successful no-op.
- Use actual `PlacedItem::origin` for a multi-cell item's top-left; do not infer origin from any occupied cell.

## Separate interaction concerns

- Keep SDL event structs, window coordinates, and rendering out of the core inventory model.
- Keep keyboard `focusedCell` separate from mouse `hoveredCell`.
- Convert screen position through panel-local coordinates and validated positive cell size before grid arithmetic.
- For dragging a multi-cell item, preserve `grabOffset = clickedCell - actualOrigin` and derive candidate origin from hover minus that offset.
- Use `canMove` for preview validity and `tryMove` only on an explicit valid release commit.
- Define click-versus-drag threshold, cancellation, Tab/Esc, panel-outside motion/release, failed release, and keyboard compatibility as state-machine contracts.

For Week 17, read `doc/exec-plans/active/week17-mouse-inventory-interaction.md`. Resolve the old global `class InventoryInteractionState` versus new global `enum class InventoryInteractionState` collision before CMake/App integration. Do not hide the conflict with include order or target separation.

## Test the failure surface

Cover every footprint edge, negative/outside coordinates, invalid IDs, missing items, other-item collision, self-overlap, same-origin, failed-state snapshots, multi-cell interior grabs, below/above drag threshold, outside release, cancel, and keyboard/mouse coexistence. Prove the mouse source is included by a dedicated CMake target before trusting green tests.

Keep rotation, multiple containers, equipment, quick transfer, context menus, search loot, stacking, weight, persistence, and broad `GameplayWorld` refactors out of a task unless explicitly scoped.
