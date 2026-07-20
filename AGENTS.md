# Project Raidline Codex Instructions

## Preserve existing work

- Treat uncommitted changes as user-owned. Do not revert, overwrite, or reformat unrelated files.
- Keep generated candidates separate from runtime assets until they pass validation.

## Art asset workflow

For game images, sprites, item art, animation, backgrounds, effects, or UI assets:

1. Read `art/ART_BIBLE.md`, `art/ASSET_MANIFEST.yaml`, and the relevant specification under `art/specs/`.
2. The art-control task does not call image generation itself. It scopes packages, writes prompts, creates dedicated user-visible Codex production tasks, reviews returned candidates, requests corrections, publishes approved assets, and starts the next package.
3. Create one dedicated production task per coherent asset family or package, such as map resources, character layers, weapons, or UI icons. Do not use internal subagents as the normal image-production unit.
4. The production task uses the built-in image generation workflow unless the user explicitly requests API/CLI generation.
5. Save the exact rendered prompt and all candidates under `art/work/<package_id>/`.
6. Never overwrite an approved asset. A visual revision requires a new versioned asset ID and path.
7. Production tasks may write only inside their assigned `art/work/<package_id>/`.
8. Only the art-control task may update the manifest, approve a candidate, publish into `assets/`, or close a production package.
9. Generate each distinct asset or candidate with a separate image-generation call.
10. Derive inventory and world-size variants deterministically from one approved identity master.
11. Run the relevant scripts under `tools/art_pipeline/` before marking an asset approved.

## Phase 1 boundaries

- Phase 1 supports Weeks 13-16: item identity art, world pickup art, inventory art, and Week 17 alignment contracts.
- Do not generate character clothing layers, weapon attachments, map tiles, buildings, corpses, limb fragments, damage overlays, projectile art, or large raster UI panels during Phase 1.
- Inventory grids, placement highlights, selection rectangles, and drag feedback remain code-rendered SDL elements.
