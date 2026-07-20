# named_firearms_pack_v1 Acceptance

## Outcome

The user-supplied QSZ92G, QBZ95-1, and M870 references have been converted into
the approved Project Raidline pixel language and published as three new,
versioned firearm identities. Existing generic pistol and rifle assets were not
overwritten.

## Production-task record

- Accepted production task:
  `019f7ec2-9935-7a30-b20f-715e8d57947a`
- Work package: `art/work/named_firearms_pack_v1_retry/`
- Successful built-in image-generation calls: 6
- Failed generation attempts: 0
- Repair rounds used: 0
- Replaced task:
  `019f7eb5-1aa3-7891-8ae3-af7541956e36`, archived after a stalled image-view
  tool failure and excluded from production evidence

## Art-control selections

| Asset | Selection | Reason |
|---|---|---|
| `item_pistol_qsz92g_v1` | candidate 01 | Black grip and richer control geometry remain closest to the supplied QSZ92G reference |
| `item_rifle_qbz95_1_v1` | candidate 01 | Carry handle, fore-end separation, and receiver structure best preserve the reference identity |
| `item_shotgun_m870_v1` | candidate 02 | Clearest barrel, magazine-tube, receiver, pump, and wooden-stock sequence |

## Runtime profiles

| Asset | Footprint | Master | Inventory | World |
|---|---:|---:|---:|---:|
| QSZ92G | `2x1` | `512x256` | `128x64` | `64x32` |
| QBZ95-1 | `4x2` | `1024x512` | `256x128` | `128x64` |
| M870 | `5x2` | `1280x512` | `320x128` | `160x64` |

All three may rotate at runtime. Their rotated footprints are `1x2`, `2x4`,
and `2x5`; no second bitmap is required.

## Acceptance evidence

- Three model identities remain recognizable after deterministic
  nearest-neighbor reduction.
- All formal profiles use muzzle-right orientation and the same identity across
  master, inventory, and world views.
- All profiles have exact dimensions, non-empty Alpha, transparent corners,
  and no opaque magenta residue.
- Inventory subjects retain at least six runtime pixels of safe padding.
- Grid footprints, rotation dimensions, and deterministic derivation checks
  pass.
- All three world pickups remain visible on the current test-map palette.

## Chroma note

The generator rendered a visually flat near-magenta key rather than exact
`#ff00ff`. Border auto-key sampling correctly removed the actual color. Raw
chroma renders were retained, while the approved RGBA outputs contain no
opaque near-magenta residue.
