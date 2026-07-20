# Project Raidline Art Control Plane

This directory is the control plane for generated game art. Runtime code reads
published assets under `assets/`; it does not read candidates from `art/work/`.

## Phase 1 workflow

1. Read `ART_BIBLE.md`, `ASSET_MANIFEST.yaml`, and the relevant contract under
   `specs/`.
2. Generate two isolated candidates per item under the assigned
   `art/work/<package_id>/` directory.
3. Build the combined candidate sheet:

   ```powershell
   poetry run python tools/art_pipeline/phase1_assets.py contact-sheet
   ```

4. Record exactly one selected transparent candidate per item in
   `art/work/phase1_selection.json`.
5. Publish deterministic master, inventory, and world profiles:

   ```powershell
   poetry run python tools/art_pipeline/phase1_assets.py build
   poetry run python tools/art_pipeline/phase1_assets.py previews
   poetry run python tools/art_pipeline/phase1_assets.py validate
   ```

6. Update the manifest and progress report only after visual and technical QA
   pass.

## Ownership

- This main Codex task is the art-control task. It owns package planning,
  prompts, acceptance criteria, production-task creation, review, correction
  routing, final publication, manifest status, and batch reports. It does not
  perform image generation.
- Each coherent asset family is produced in a dedicated user-visible Codex
  task, such as `map_resources`, `character_layers`, or `weapon_assets`.
- Production tasks write prompts, raw renders, transparent candidates,
  recommendations, and `result.json` only inside their assigned
  `art/work/<package_id>/`.
- A rejected candidate is corrected by sending a precise repair prompt back to
  its existing production task. A new family starts in a new production task.
- After the art-control task has accepted and archived a package, it may start
  the next production task.
- Published versioned assets are immutable. A future redesign receives a new
  asset ID instead of overwriting an approved identity.

## Phase boundary

Phase 1 contains only item identity art, deterministic inventory/world
derivatives, previews, QA, and Week 17 calibration contracts. Character layers,
weapon attachments, procedural-map art, buildings, damage states, corpses, and
raster inventory chrome remain deferred.
