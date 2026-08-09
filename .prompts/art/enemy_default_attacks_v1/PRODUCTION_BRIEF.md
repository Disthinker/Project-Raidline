# enemy_default_attacks_v1 Production Brief

You are the dedicated image-production task for one coherent Project Raidline
character-animation family. Work in:

`E:\WorkPlace\Projects\C\RaidLine`

Read completely before acting:

- `AGENTS.md`
- `art/ART_BIBLE.md`
- `art/PRODUCTION_TASK_PROTOCOL.md`
- `art/specs/CHARACTER_LAYER_SPEC.md`
- `art/specs/enemy_default_attacks_v1.json`
- all six frozen prompts in `.prompts/art/enemy_default_attacks_v1/`
- the complete imagegen skill at
  `C:\Users\25113\.codex\skills\.system\imagegen\SKILL.md`

Inspect this immutable identity and layout reference at original detail before
generation:

- `assets/characters/enemy/default/enemy_default_move_horizontal_6f_1536x640.png`

## Scope

Generate exactly two distinct candidates for each of these three attack sheets:

- `enemy_default_attack_grab`
- `enemy_default_attack_scratch`
- `enemy_default_attack_bite`

Execute six separate successful built-in image-generation calls, one per frozen
prompt. Every call must use the immutable movement sheet as the sole character
identity reference. This is one coherent family: all six candidates must retain
the exact same infected identity, camera, pixel density, palette, frame canvas,
pivot and footline.

Each transparent candidate is one 1536x640 PNG arranged as six 256x320 columns
by two rows. Row 0 faces left and row 1 faces right. The two rows must be
independently posed; do not produce one row by runtime or post-production
mirroring. Preserve complete hair, hands and boots inside every frame. No frame
labels, separators, guides, shadows, props or background are allowed.

## Execution

1. Inspect the immutable reference with `view_image` at original detail.
2. Execute each frozen prompt as one separate built-in image-generation call,
   passing the immutable movement sheet through `referenced_image_paths`.
3. Preserve each raw generated output unchanged.
4. Copy outputs and exact executed prompts only into:

   `art/work/enemy_default_attacks_v1/`

5. Produce an RGBA candidate for each call. Deterministic canvas normalization
   may only establish the contract's exact 1536x640 size and six-by-two cell
   boundaries; it may not redraw, mirror, interpolate, invent or repair frames.
   Use nearest-neighbor handling for pixel content. If the generated poses do
   not already satisfy the grid, record the candidate as failed visual QA.
6. Validate exact dimensions, RGBA mode, non-empty Alpha, four transparent
   corners, no opaque background, six populated cells per row, uncropped head
   and feet, pivot/footline drift, independent left/right poses and readability
   at 64x80 per frame.
7. Build contact sheets and 64x80 frame-strip previews for art-control review,
   but do not publish them into runtime assets or reviews.
8. Copy the exact prompts actually executed into the package `prompts/`
   directory and record SHA-256 values.
9. Write `art/work/enemy_default_attacks_v1/result.json` following
   `art/PRODUCTION_TASK_PROTOCOL.md`. Record every candidate, raw path, RGBA
   path, technical QA, visual deviations, one recommendation per action and
   known risks.

Use this package layout:

```text
art/work/enemy_default_attacks_v1/
  prompts/
    enemy_default_attack_grab_candidate_01.txt
    enemy_default_attack_grab_candidate_02.txt
    enemy_default_attack_scratch_candidate_01.txt
    enemy_default_attack_scratch_candidate_02.txt
    enemy_default_attack_bite_candidate_01.txt
    enemy_default_attack_bite_candidate_02.txt
  candidates/
    enemy_default_attack_grab/candidate_01_raw.png
    enemy_default_attack_grab/candidate_01_rgba.png
    ...
  previews/
    contact_sheet.png
    enemy_default_attack_grab_candidate_01_64x80_strip.png
    ...
  result.json
```

## Boundaries

- Write only inside `art/work/enemy_default_attacks_v1/`.
- Do not modify source code, tests, documentation, `.prompts/`, contracts,
  `art/ASSET_MANIFEST.yaml`, reviews or any existing work package.
- Do not publish into `assets/` and do not overwrite the movement fallback.
- Do not approve your own candidates.
- Do not add new enemy identities, clothing, damage layers, gore, props,
  shadows, effects or gameplay rules.
- Do not create a Bite chase/start animation; Bite is a close Grab follow-up.
- Generate every distinct candidate with a separate image-generation call.
- If a call fails before producing an image, retry the same frozen prompt and
  record the failed attempt separately from the six successful calls.
- Stop after the six candidates. Only art-control may authorize one targeted
  repair round.

## Handoff

Return all six transparent candidate paths, exact prompt paths, contact sheet,
64x80 previews, technical QA, one recommendation per action and known visual
risks. Then stop and wait for art-control review or one targeted repair request.
