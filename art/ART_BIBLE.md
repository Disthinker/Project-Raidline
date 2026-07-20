# Project Raidline Art Bible

## Project direction

Project Raidline is a 2D, lightly top-down pixel-art extraction survival game set within six months of a disaster. Art must favor gameplay readability over decorative detail.

## Approved character anchor

The existing protagonist static sprite and six-frame left/right movement sheet are immutable character identity and style anchors:

- source frame: `256x320`
- runtime draw size: `64x80`
- movement sheet: `1536x640`, six columns by two rows
- source foot baseline: `y = 300`
- source pivot: `(128, 300)`
- runtime pivot: `(32, 75)`

The existing default enemy and test map are auxiliary palette and mood references, not immutable archetype approvals.

## Shared visual rules

- Readable pixel-art rendering grounded in realistic object proportions.
- Lightly top-down three-quarter presentation; do not use a flat front elevation for world items.
- Muted black, charcoal, gray-green, military green, khaki, sand, worn steel, and restrained dark red.
- Consistent upper-left lighting and compact shadows inside the object; isolated item assets have no cast shadow.
- Crisp silhouette and controlled highlights that remain readable after nearest-neighbor downscaling.
- No neon color fields, cyberpunk, magic, futuristic ornament, exaggerated cartoon proportions, watermarks, trademarks, or unrequested text.

## Phase 1 item rules

- One approved identity master is the source of inventory and world variants.
- Inventory safe padding is at least six runtime pixels on every side.
- Pistol and rifle are horizontal with the muzzle pointing right.
- Firearms are original generic designs, not replicas of named commercial models.
- The rifle keeps visually distinct muzzle, top rail, magazine, receiver, and stock zones for the later attachment system, but Phase 1 does not split these into layers.
- The medical kit uses a generic white or green medical plus motif. It must not reproduce the protected official Red Cross emblem.
- Transparent assets are generated on a flat removable chroma-key background and delivered as RGBA PNG.

## Named firearm exception

The generic-firearm restriction above applies to `*_basic_v1` identities.
User-supplied named-firearm references may be converted into separate versioned
assets when the package explicitly declares them as identity targets.

- Preserve the reference model's gameplay-recognizable silhouette and major
  structures.
- Remove brand logos, serial numbers, engraved text, proof marks, and small
  manufacturer labels from the rendered asset.
- Do not overwrite or silently repurpose an approved generic identity.
- Named firearms still use the shared pixel language, lighting, chroma-key,
  safe-padding, orientation, and deterministic-derivative rules.
