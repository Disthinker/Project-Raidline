# Phase 1 Week 13-16 Acceptance

## Outcome

Phase 1 is accepted for the Week 14-16 item loop. The published asset set
contains four immutable item identities and deterministic inventory and world
profiles. Week 13 continues to use the existing enemy art and code-rendered
particles.

## Selected identities

| Asset | Selected candidate | Reason |
|---|---|---|
| `item_cola_basic_v1` | consumables `candidate_a` | Strongest compact can silhouette and muted red pickup contrast |
| `item_pistol_basic_v1` | weapons `candidate_02` | Clean generic silhouette, readable trigger guard, muzzle right |
| `item_rifle_basic_v1` | weapons `candidate_02` | Clear muzzle, rail, magazine, receiver, and stock zones |
| `item_medkit_basic_v1` | consumables `candidate_b` | Broad 2x2 silhouette and ordinary green medical mark |

The one allowed directed repair round was not used because every selected
candidate met the visual contract without revision.

## Deliverables

- 8 generated candidates and their exact prompts under `art/work/`.
- 4 approved RGBA identity masters under `assets/items/source/`.
- 4 deterministic inventory textures under `assets/items/inventory/`.
- 4 deterministic world textures under `assets/items/world/`.
- Combined candidate contact sheet.
- Actual-scale inventory preview, including `1x2` pistol and `2x4` rifle
  rotation examples.
- Test-map world pickup preview.
- Machine-readable and Markdown technical QA reports.
- Week 17 character canvas, pivot, baseline, layer-order, and weapon-socket
  contracts.

## Acceptance evidence

- Every PNG is readable, has the exact contract dimensions, and contains Alpha.
- All four corners are transparent with hidden transparent RGB normalized to
  zero.
- Every inventory subject keeps at least six runtime pixels of safe padding.
- Inventory and world profiles are nearest-neighbor derivatives of the same
  approved master.
- Inventory canvas dimensions equal the configured grid footprint at
  `64x64` pixels per cell.
- SDL rotation requires no second image: pistol becomes `1x2`; rifle becomes
  `2x4`.
- All four world profiles remain distinguishable on the current test-map
  palette.

## Runtime integration boundary

The art contract is ready for `ItemDefinition`, ground pickup, inventory
placement, move, rotate, drop, and ownership-transfer integration. First-fit,
overlap prevention, pickup/drop behavior, collision bounds, and texture loading
remain Week 14-16 runtime tests because those gameplay systems do not yet exist
in this branch. Collision and pickup bounds must remain separate from the
transparent image canvas.

## Deferred by design

No character layers, weapon attachments, map tiles, buildings, loot
containers, damage overlays, limb fragments, corpses, projectile art, muzzle
effects, shell casings, health UI, score UI, or raster inventory chrome were
created.
