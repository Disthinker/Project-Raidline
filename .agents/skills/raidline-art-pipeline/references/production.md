# Authorized art production and publication

Read only for a reopened production package, not routine asset lookup. Before action, read the relevant Art Bible rules, manifest entries, PRODUCTION_TASK_PROTOCOL and package Markdown/JSON specs; art/README.md is the tooling index, not a mandatory full-document bundle. Inspect the script entry point before running it.

## Enforce roles and write boundaries

- Keep the existing art-control thread responsible for package scope, brief/prompts, authorized worker coordination, review/repair, selection, publishing, manifest/QA and package closure. Start downstream work only under its own valid authorization.
- Do not have art-control generate images directly.
- Use a bounded temporary production worker when delegation is needed and authorized. A new user-visible persistent task requires the human to explicitly request one; do not create replacement long-lived roles. Keep each coherent package's owner/output boundary explicit.
- Have the production task use Codex's built-in image-generation workflow unless the user explicitly requests API or CLI generation.
- Restrict each production task to `art/work/<package_id>/`. Retain its exact rendered prompts, every candidate and transparent version, local QA, and `result.json`.
- Prevent production tasks from approving their own candidates, changing `art/ASSET_MANIFEST.yaml`, publishing into `assets/`, or starting downstream packages.
- Generate every distinct asset or candidate with a separate image-generation call.
- Let the explicitly assigned sole writer (default art-control) select/publish/update manifest and QA only within package authorization, after technical and visual review. A handoff to main must name paths/phase and stop the previous writer; main owns code integration. Closing an asset package cannot supply human acceptance of the integrated game candidate.

An unapproved `result.json` inside a production package is compatible with an accepted manifest entry: the production worker has no self-approval authority.

## Preserve asset identity

- Never overwrite an approved asset. Create a new versioned asset ID and path for every visual revision, even if a script could overwrite the old file.
- For Phase 1 items, publish only to the contract paths under `assets/items/source/`, `assets/items/inventory/`, and `assets/items/world/`. Other explicitly authorized families use their own spec paths.
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

## Package scope

Use the manifest for actual publication state. Completed Phase 1/Week packages are historical evidence, not a mandate to restart them. Character clothing, weapon attachments, map/building art, corpses, limbs, damage overlays, projectile/VFX art and large raster UI panels require explicit package authorization.

Keep inventory grids, placement highlights, selection rectangles, hover, and drag feedback code-rendered in SDL.

## Validate before approval

Run the relevant `tools/art_pipeline/` commands and `tests/test_phase1_assets.py`, but inspect command behavior first: build, preview, contact-sheet, and validation commands write generated or QA files. Confirm IDs, dimensions, alpha, transparent corners, safe margins, footprint sizing, rotation sizing, and pixel-exact nearest-neighbor derivation.

Treat script success as technical evidence only. Perform a separate visual review against the Art Bible and reference contract. Record known tooling risks: existing save routines can overwrite paths, basic candidate discovery may cross package boundaries for repeated IDs, named-firearm tooling is package-specific, and Python art tests are not currently in CTest/CI.
