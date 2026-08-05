---
name: raidline-art-pipeline
description: Control Project Raidline image, sprite, item art, animation, background, effect, raster UI, asset-review, deterministic-derivation, manifest, approval, and publishing workflows. Use only for art production or asset pipeline work; do not load for ordinary C++ feature work or code-rendered inventory interaction feedback.
---

# Raidline Art Pipeline

## Load the authoritative contract

Read `art/ART_BIBLE.md`, `art/ASSET_MANIFEST.yaml`, `art/PRODUCTION_TASK_PROTOCOL.md`, `art/README.md`, the relevant `art/specs/` Markdown and JSON contracts, and the applicable script under `tools/art_pipeline/`. Treat the manifest as current publication state and `art/work/` as unapproved production evidence.

## Enforce roles and write boundaries

- Keep art-control responsible for package scope, production brief, exact prompts, dedicated user-visible production tasks, review, revision requests, selection, publishing, manifest updates, QA, package closure, and starting downstream work.
- Do not have art-control generate images directly.
- Create one dedicated user-visible production task for each coherent asset family. Do not use internal subagents as the normal image-production unit.
- Have the production task use Codex's built-in image-generation workflow unless the user explicitly requests API or CLI generation.
- Restrict each production task to `art/work/<package_id>/`. Retain its exact rendered prompts, every candidate and transparent version, local QA, and `result.json`.
- Prevent production tasks from approving their own candidates, changing `art/ASSET_MANIFEST.yaml`, publishing into `assets/`, or starting downstream packages.
- Generate every distinct asset or candidate with a separate image-generation call.
- Let only art-control select, publish, update the manifest and QA state, and close the package after technical and visual review.

An unapproved `result.json` inside a production package is compatible with an accepted manifest entry: the production worker has no self-approval authority.

## Preserve asset identity

- Never overwrite an approved asset. Create a new versioned asset ID and path for every visual revision, even if a script could overwrite the old file.
- Publish only to the contract paths under `assets/items/source/`, `assets/items/inventory/`, and `assets/items/world/`.
- Derive inventory and world images deterministically with nearest-neighbor scaling from one approved identity master.
- Keep runtime code dependent only on published `assets/`, never candidates under `art/work/`.
- Treat collision and pickup bounds as gameplay data; never infer them from transparent canvas bounds.
- Use SDL rotation and swapped logical footprints rather than generating duplicate rotated bitmaps.

## Preserve visual and technical rules

- Use a light top-down three-quarter view, readable pixel silhouette, realistic proportions, upper-left light, compact internal shadow, and no cast shadow for isolated items.
- Favor black, charcoal, gray-green, military green, khaki, sand, aged steel, and restrained dark red.
- Reject neon blocks, cyberpunk, magic, futuristic ornament, exaggerated cartoon styling, watermarks, brands, and unrequested text.
- Use a generic medical plus rather than reproducing a protected Red Cross emblem.
- Require transparent RGBA PNG output derived from a removable flat key background.
- Keep generic firearms original and right-facing. Named-firearm identity is allowed only when the package explicitly declares user-provided references; remove brands, serials, inscriptions, and manufacturer labels.
- Keep inventory cells at 64×64, masters at four times runtime inventory pixels, at least six runtime pixels of inventory margin on every side, and world dimensions at one half of inventory dimensions.
- Preserve character layer canvas, pivot, footline, animation-sheet, and layer-order contracts in `art/specs/CHARACTER_LAYER_SPEC.md`; do not crop individual layers.

## Respect the current phase

The manifest records Phase 1 as complete for Weeks 13–16 item identities and Week 17 calibration contracts. Do not produce deferred character clothing, weapon attachments, procedural maps/buildings, corpses, limbs, damage overlays, projectile/VFX art, or large raster UI panels unless a later task explicitly reopens scope.

Keep inventory grids, placement highlights, selection rectangles, hover, and drag feedback code-rendered in SDL.

## Validate before approval

Run the relevant `tools/art_pipeline/` commands and `tests/test_phase1_assets.py`, but inspect command behavior first: build, preview, contact-sheet, and validation commands write generated or QA files. Confirm IDs, dimensions, alpha, transparent corners, safe margins, footprint sizing, rotation sizing, and pixel-exact nearest-neighbor derivation.

Treat script success as technical evidence only. Perform a separate visual review against the Art Bible and reference contract. Record known tooling risks: existing save routines can overwrite paths, basic candidate discovery may cross package boundaries for repeated IDs, named-firearm tooling is package-specific, and Python art tests are not currently in CTest/CI.
