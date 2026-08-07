# ammo_9mm_pack_v1 Production Brief

You are the dedicated image-production task for one Project Raidline item
identity. Work in:

`E:\WorkPlace\Projects\C\RaidLine`

Read completely before acting:

- `AGENTS.md`
- `art/ART_BIBLE.md`
- `art/PRODUCTION_TASK_PROTOCOL.md`
- `art/specs/ITEM_ART_SPEC.md`
- `art/specs/ammo_9mm_pack_v1.json`
- both prompts in `.prompts/art/ammo_9mm_pack_v1/`
- the complete imagegen skill at
  `C:\Users\25113\.codex\skills\.system\imagegen\SKILL.md`

Inspect these approved references with `view_image` before generation:

- `assets/items/source/item_cola_basic_v1.png` — approved 1x1 item pixel-density and padding reference
- `assets/items/source/item_medkit_basic_v1.png` — approved container-like item material and outline reference
- `assets/characters/protagonist_left_minimal_256x320.png` — immutable project pixel-style anchor
- `assets/backgrounds/project_raidline_test_map_1280x720.png` — palette and runtime contrast reference

## Scope

Generate exactly two distinct candidates for:

- `item_ammo_9mm_basic_v1`

Execute two separate successful built-in `image_gen` calls, one per frozen
prompt. The visual identity is one compact, unbranded, worn olive/gray-green
9mm ammunition carton with its attached lid slightly open and a few brass
cartridge tips visible inside. It is a 1x1, non-rotating item whose eventual
profiles will be 256x256 master, 64x64 inventory and 32x32 world.

## Execution

1. Inspect every approved reference listed above with `view_image`.
2. Execute each candidate prompt as one separate built-in `image_gen` call.
3. Preserve each raw generated chroma render unchanged.
4. Copy outputs and exact executed prompts into:

   `art/work/ammo_9mm_pack_v1/`

5. Remove the sampled flat magenta border with the imagegen chroma helper.
   Read its local usage first, then use border auto-key, soft matte,
   transparent threshold 12, opaque threshold 220, and despill.
6. Validate RGBA mode, non-empty Alpha, four transparent corners, no opaque or
   near-opaque magenta, a complete single-box silhouette, generous padding,
   and no extra object, loose cartridge, text, brand, weapon, hand or shadow.
7. Copy the exact prompts actually executed into the package `prompts/`
   directory and record SHA-256 values.
8. Write `art/work/ammo_9mm_pack_v1/result.json` following
   `art/PRODUCTION_TASK_PROTOCOL.md`, including both candidates, technical QA,
   one recommendation, known risks and scope confirmation.

Use this package layout:

```text
art/work/ammo_9mm_pack_v1/
  prompts/
    item_ammo_9mm_basic_v1_candidate_01.txt
    item_ammo_9mm_basic_v1_candidate_02.txt
  candidates/item_ammo_9mm_basic_v1/
    candidate_01_chroma.png
    candidate_01_rgba.png
    candidate_02_chroma.png
    candidate_02_rgba.png
  result.json
```

## Boundaries

- Write only inside `art/work/ammo_9mm_pack_v1/`.
- Do not modify source code, tests, documentation, `.prompts/`, contracts,
  `art/ASSET_MANIFEST.yaml`, reviews, or any existing work package.
- Do not publish into `assets/`.
- Do not derive the 256x256, 64x64 or 32x32 final profiles.
- Do not approve your own candidates.
- Do not start downstream work.
- Generate every distinct candidate with a separate image-generation call.
- If a call fails before producing an image, retry the same frozen prompt and
  record the failed attempt separately from the two successful calls.

## Handoff

Return the two transparent candidate paths, exact prompt paths, technical QA,
your recommendation and known visual risks. Then stop and wait for art-control
review or one targeted repair instruction.
