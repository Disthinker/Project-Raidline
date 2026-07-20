# named_firearms_pack_v1 Production Brief

You are the dedicated image-production task for three user-supplied firearm
identities. Work in:

`E:\WorkPlace\Projects\C\Project Raidline`

Read before acting:

- `AGENTS.md`
- `art/ART_BIBLE.md`
- `art/PRODUCTION_TASK_PROTOCOL.md`
- `art/specs/named_firearms_pack_v1.json`
- `art/references/weapons/named_firearms_pack_v1/REFERENCE_REGISTRY.json`
- every prompt in `.prompts/art/named_firearms_pack_v1/`
- the complete imagegen skill at
  `C:\Users\Administrator\.codex\skills\.system\imagegen\SKILL.md`

## Scope

Generate two pixel-art candidates for each asset, six successful built-in
`image_gen` calls total:

- `item_pistol_qsz92g_v1`
- `item_rifle_qbz95_1_v1`
- `item_shotgun_m870_v1`

Each input weapon image is an edit target and identity reference. Existing
approved Project Raidline item masters are style references only.

## Execution

1. Inspect every local edit target and style reference with `view_image`.
2. Execute each candidate prompt as one separate built-in `image_gen` edit.
3. Supply the target weapon first in `referenced_image_paths`, followed by the
   applicable approved weapon master, Player anchor, and test-map reference.
4. Copy each generated chroma render from `$CODEX_HOME/generated_images/` into
   `art/work/named_firearms_pack_v1/`.
5. Remove `#ff00ff` with the installed imagegen chroma helper using border
   auto-key, soft matte, thresholds 12/220, and despill.
6. Validate RGBA mode, non-empty Alpha, transparent corners, no opaque magenta,
   complete muzzle-right silhouette, and generous padding.
7. Copy the exact executed prompts into the work package.
8. Write `art/work/named_firearms_pack_v1/result.json` following
   `art/PRODUCTION_TASK_PROTOCOL.md`.

## Boundaries

- Write only inside `art/work/named_firearms_pack_v1/`.
- Do not update `art/ASSET_MANIFEST.yaml`.
- Do not publish into `assets/`.
- Do not derive inventory or world profiles.
- Do not approve your own candidates.
- Do not add attachments, hands, ammunition, muzzle effects, slings, UI frames,
  text, logos, serial numbers, or manufacturer markings.
- If an imagegen call fails before producing an image, retry the same prompt;
  record failed attempts separately from six successful outputs.

## Handoff

Return the six transparent candidate paths, exact prompt paths, technical QA,
one recommendation per asset, and known visual risks. Then wait for the
art-control task to accept or send one targeted repair instruction.
