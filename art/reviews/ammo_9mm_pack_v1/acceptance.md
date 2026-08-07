# ammo_9mm_pack_v1 Acceptance

## Outcome

The approved 9mm ammunition identity is one compact, worn olive-drab carton
with an attached open lid and contained brass pistol ammunition. It is
published as a non-rotatable `1x1` item with a maximum stack size of 60.

## Production-task record

- Accepted production task: `019fdb3a-add1-7ab3-a67e-8cd0ad4bc009`
- Work package: `art/work/ammo_9mm_pack_v1/`
- Successful built-in image-generation calls: 3
- Initial candidate calls: 2
- Repair rounds used: 1 of 1
- Failed generation attempts: 0

## Art-control selection

`candidate_01_rgba.png` is approved. It keeps the clearest cartridge
side-profile and ammunition-box reading after deterministic reduction to
`64x64` and `32x32`.

Candidate 02 met the exact-four request, but its circular brass forms read more
like cartridge bases or primers at small size. The sole repair output preserved
the preferred side profile but produced two cartridges instead of the requested
four and failed its repair objective.

The selected candidate also does not present exactly four cartridges. This is
an accepted visual deviation: the authoritative asset contract requires only
"a few brass cartridge tips visible inside", while runtime recognition is the
higher-priority acceptance criterion.

## Runtime profiles

| Footprint | Rotation | Maximum stack | Master | Inventory | World |
|---:|---:|---:|---:|---:|---:|
| `1x1` | disabled | `60` | `256x256` | `64x64` | `32x32` |

## Acceptance evidence

- The carton and contained ammunition remain recognizable at both runtime sizes.
- Master, inventory, and world profiles share one deterministic identity.
- All profiles have exact dimensions, non-empty Alpha, transparent corners,
  clean hidden RGB, and no opaque near-magenta residue.
- The inventory profile retains at least six pixels of safe padding.
- The `1x1`, non-rotatable contract matches the C++ item definition.
- Inventory and map-composite review previews are stored beside this report.

## Chroma note

The generator produced a visually flat near-magenta key rather than a
mathematically uniform `#ff00ff`. The unchanged chroma originals remain in the
work package. Border auto-key removal produced clean transparent candidates.
