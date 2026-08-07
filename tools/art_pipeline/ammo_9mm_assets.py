from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, cast

from PIL import Image, ImageChops, ImageDraw, ImageFont

from tools.art_pipeline.phase1_assets import (
    REPO_ROOT,
    checkerboard,
    derive_profile,
    normalize_to_canvas,
    save_png,
)

ASSET_ID = "item_ammo_9mm_basic_v1"
CONTRACT_PATH = REPO_ROOT / "art/specs/ammo_9mm_pack_v1.json"
WORK_ROOT = REPO_ROOT / "art/work/ammo_9mm_pack_v1"
SELECTION_PATH = WORK_ROOT / "selection.json"
SOURCE_DIR = REPO_ROOT / "assets/items/source"
INVENTORY_DIR = REPO_ROOT / "assets/items/inventory"
WORLD_DIR = REPO_ROOT / "assets/items/world"
REVIEW_ROOT = REPO_ROOT / "art/reviews/ammo_9mm_pack_v1"


def load_contract() -> dict[str, Any]:
    return cast(
        dict[str, Any],
        json.loads(CONTRACT_PATH.read_text(encoding="utf-8")),
    )


def load_selection(path: Path) -> tuple[dict[str, Path], dict[str, Any]]:
    data = cast(
        dict[str, Any],
        json.loads(path.read_text(encoding="utf-8")),
    )
    selected: dict[str, Path] = {}
    for asset_id, raw_path in data["selected_candidates"].items():
        candidate = Path(raw_path)
        if not candidate.is_absolute():
            candidate = REPO_ROOT / candidate
        if not candidate.exists():
            raise FileNotFoundError(f"Selected candidate does not exist: {candidate}")
        if WORK_ROOT not in candidate.parents:
            raise ValueError(f"Selected candidate is outside work package: {candidate}")
        selected[asset_id] = candidate
    return selected, data


def profile_paths(asset_id: str, spec: dict[str, Any]) -> dict[str, Path]:
    inventory_width, inventory_height = spec["inventory_size"]
    world_width, world_height = spec["world_size"]
    return {
        "master": SOURCE_DIR / f"{asset_id}.png",
        "inventory": (
            INVENTORY_DIR
            / f"{asset_id}_{inventory_width}x{inventory_height}.png"
        ),
        "world": WORLD_DIR / f"{asset_id}_{world_width}x{world_height}.png",
    }


def candidate_files() -> list[Path]:
    candidate_root = WORK_ROOT / "candidates" / ASSET_ID
    paths = [
        path
        for path in candidate_root.glob("*_rgba.png")
        if path.name.startswith(("candidate_", "repair_"))
    ]
    return sorted(paths)


def relative(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def make_contact_sheet() -> Path:
    contract = load_contract()
    selected, _ = load_selection(SELECTION_PATH)
    candidates = candidate_files()
    expected_count = (
        contract["candidates_per_asset"] + contract["maximum_repair_rounds"]
    )
    if len(candidates) != expected_count:
        raise RuntimeError(
            f"Expected {expected_count} transparent candidates, found {candidates}"
        )

    cell_width, cell_height = 420, 360
    canvas = checkerboard((cell_width * len(candidates), cell_height))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()
    selected_path = selected[ASSET_ID].resolve()

    for column, candidate_path in enumerate(candidates):
        image = Image.open(candidate_path).convert("RGBA")
        bbox = image.getchannel("A").getbbox()
        if bbox is None:
            raise RuntimeError(f"Candidate is fully transparent: {candidate_path}")
        cropped = image.crop(bbox)
        scale = min(360 / cropped.width, 270 / cropped.height)
        preview_size = (
            max(1, round(cropped.width * scale)),
            max(1, round(cropped.height * scale)),
        )
        preview = cropped.resize(preview_size, Image.Resampling.NEAREST)
        left = column * cell_width + (cell_width - preview.width) // 2
        top = 52 + (270 - preview.height) // 2
        canvas.alpha_composite(preview, (left, top))

        is_selected = candidate_path.resolve() == selected_path
        label = candidate_path.stem.removesuffix("_rgba")
        if is_selected:
            label += " / SELECTED"
            draw.rectangle(
                (
                    column * cell_width + 3,
                    3,
                    (column + 1) * cell_width - 4,
                    cell_height - 4,
                ),
                outline=(118, 220, 142, 255),
                width=4,
            )
        draw.text(
            (column * cell_width + 14, 18),
            label,
            font=font,
            fill=(255, 255, 255, 255),
        )

    destination = REVIEW_ROOT / "contact_sheet.png"
    save_png(canvas.convert("RGB"), destination)
    return destination


def build_assets(selection_path: Path) -> None:
    contract = load_contract()
    selected, _ = load_selection(selection_path)
    expected_ids = set(contract["assets"])
    if set(selected) != expected_ids:
        raise ValueError(
            "Selection IDs do not match the contract: "
            f"expected={sorted(expected_ids)} actual={sorted(selected)}"
        )

    source_padding = (
        contract["inventory_safe_padding"] * contract["source_scale"]
    )
    for asset_id, spec in contract["assets"].items():
        candidate = Image.open(selected[asset_id]).convert("RGBA")
        master = normalize_to_canvas(
            candidate,
            tuple(spec["master_size"]),
            source_padding,
        )
        inventory = derive_profile(master, tuple(spec["inventory_size"]))
        world = derive_profile(master, tuple(spec["world_size"]))
        paths = profile_paths(asset_id, spec)
        save_png(master, paths["master"])
        save_png(inventory, paths["inventory"])
        save_png(world, paths["world"])
        print(f"built {asset_id}: {paths}")


def make_inventory_preview(contract: dict[str, Any]) -> Path:
    cell = contract["inventory_cell_size"]
    canvas = Image.new("RGBA", (cell * 5, cell * 3), (31, 34, 33, 255))
    draw = ImageDraw.Draw(canvas)
    for x in range(6):
        draw.line((x * cell, 0, x * cell, canvas.height), fill=(110, 118, 113, 255))
    for y in range(4):
        draw.line((0, y * cell, canvas.width, y * cell), fill=(110, 118, 113, 255))

    spec = contract["assets"][ASSET_ID]
    image = Image.open(profile_paths(ASSET_ID, spec)["inventory"]).convert("RGBA")
    canvas.alpha_composite(image, (cell * 2, cell))
    draw.rectangle((cell * 3 - 23, cell * 2 - 18, cell * 3 - 4, cell * 2 - 4), fill=(8, 10, 12, 220))
    draw.text((cell * 3 - 20, cell * 2 - 16), "60", fill=(235, 238, 240, 255))

    destination = REVIEW_ROOT / "inventory_preview.png"
    save_png(canvas, destination)
    return destination


def make_world_preview(contract: dict[str, Any]) -> Path:
    map_path = REPO_ROOT / "assets/backgrounds/project_raidline_test_map_1280x720.png"
    canvas = Image.open(map_path).convert("RGBA")
    spec = contract["assets"][ASSET_ID]
    image = Image.open(profile_paths(ASSET_ID, spec)["world"]).convert("RGBA")
    canvas.alpha_composite(image, (624, 390))

    destination = REVIEW_ROOT / "world_preview.png"
    save_png(canvas, destination)
    return destination


def make_previews() -> None:
    contract = load_contract()
    print(make_inventory_preview(contract))
    print(make_world_preview(contract))


def magenta_pixels(image: Image.Image) -> int:
    count = 0
    for red, green, blue, alpha in image.convert("RGBA").getdata():
        if alpha >= 220 and red >= 220 and blue >= 220 and green <= 40:
            count += 1
    return count


def hidden_rgb_is_clean(image: Image.Image) -> bool:
    return all(
        alpha != 0 or (red, green, blue) == (0, 0, 0)
        for red, green, blue, alpha in image.convert("RGBA").getdata()
    )


def validate_assets() -> dict[str, Any]:
    contract = load_contract()
    selected, selection_data = load_selection(SELECTION_PATH)
    safe_padding = contract["inventory_safe_padding"]
    cell = contract["inventory_cell_size"]
    report: dict[str, Any] = {
        "package_id": contract["package_id"],
        "status": "passed",
        "selection": {
            "path": relative(selected[ASSET_ID]),
            "sha256": sha256(selected[ASSET_ID]),
            "accepted_deviations": selection_data.get("accepted_deviations", []),
        },
        "assets": {},
    }

    for asset_id, spec in contract["assets"].items():
        paths = profile_paths(asset_id, spec)
        images = {
            name: Image.open(path).convert("RGBA")
            for name, path in paths.items()
        }
        expected_sizes = {
            "master": tuple(spec["master_size"]),
            "inventory": tuple(spec["inventory_size"]),
            "world": tuple(spec["world_size"]),
        }
        checks: dict[str, Any] = {}
        for profile, image in images.items():
            alpha = image.getchannel("A")
            bbox = alpha.getbbox()
            corners = [
                alpha.getpixel((0, 0)),
                alpha.getpixel((image.width - 1, 0)),
                alpha.getpixel((0, image.height - 1)),
                alpha.getpixel((image.width - 1, image.height - 1)),
            ]
            residual_magenta = magenta_pixels(image)
            hidden_rgb_clean = hidden_rgb_is_clean(image)
            profile_ok = (
                image.size == expected_sizes[profile]
                and bbox is not None
                and corners == [0, 0, 0, 0]
                and residual_magenta == 0
                and hidden_rgb_clean
            )
            checks[profile] = {
                "path": relative(paths[profile]),
                "sha256": sha256(paths[profile]),
                "size": list(image.size),
                "expected_size": list(expected_sizes[profile]),
                "alpha_bbox": list(bbox) if bbox else None,
                "transparent_corners": corners,
                "opaque_near_magenta_pixels": residual_magenta,
                "transparent_rgb_clean": hidden_rgb_clean,
                "passed": profile_ok,
            }
            if not profile_ok:
                report["status"] = "failed"

        inventory_bbox = images["inventory"].getchannel("A").getbbox()
        padding_ok = (
            inventory_bbox is not None
            and inventory_bbox[0] >= safe_padding
            and inventory_bbox[1] >= safe_padding
            and images["inventory"].width - inventory_bbox[2] >= safe_padding
            and images["inventory"].height - inventory_bbox[3] >= safe_padding
        )
        footprint_width, footprint_height = spec["grid_footprint"]
        footprint_ok = images["inventory"].size == (
            footprint_width * cell,
            footprint_height * cell,
        )
        rotation_ok = spec["can_rotate"] is False
        identity_ok = (
            ImageChops.difference(
                images["inventory"],
                derive_profile(images["master"], tuple(spec["inventory_size"])),
            ).getbbox()
            is None
            and ImageChops.difference(
                images["world"],
                derive_profile(images["master"], tuple(spec["world_size"])),
            ).getbbox()
            is None
        )
        checks["safe_padding_ok"] = padding_ok
        checks["footprint_ok"] = footprint_ok
        checks["non_rotatable_contract_ok"] = rotation_ok
        checks["deterministic_identity_ok"] = identity_ok
        if not all((padding_ok, footprint_ok, rotation_ok, identity_ok)):
            report["status"] = "failed"
        report["assets"][asset_id] = checks

    REVIEW_ROOT.mkdir(parents=True, exist_ok=True)
    json_path = REVIEW_ROOT / "qa.json"
    json_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )
    checks = report["assets"][ASSET_ID]
    markdown = [
        "# ammo_9mm_pack_v1 QA",
        "",
        f"- Overall status: `{report['status']}`",
        f"- Selected source: `{report['selection']['path']}`",
        f"- master profile: `{checks['master']['passed']}`",
        f"- inventory profile: `{checks['inventory']['passed']}`",
        f"- world profile: `{checks['world']['passed']}`",
        f"- safe padding: `{checks['safe_padding_ok']}`",
        f"- grid footprint: `{checks['footprint_ok']}`",
        f"- non-rotatable contract: `{checks['non_rotatable_contract_ok']}`",
        f"- deterministic identity: `{checks['deterministic_identity_ok']}`",
        "",
    ]
    markdown_path = REVIEW_ROOT / "qa.md"
    markdown_path.write_text("\n".join(markdown), encoding="utf-8")
    print(json_path)
    print(markdown_path)
    return report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Publish and validate 9mm ammo art")
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("contact-sheet")
    subparsers.add_parser("previews")
    subparsers.add_parser("validate")
    build = subparsers.add_parser("build")
    build.add_argument("--selection", type=Path, default=SELECTION_PATH)
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.command == "contact-sheet":
        print(make_contact_sheet())
    elif args.command == "build":
        build_assets(args.selection)
    elif args.command == "previews":
        make_previews()
    elif args.command == "validate":
        report = validate_assets()
        if report["status"] != "passed":
            raise SystemExit(1)


if __name__ == "__main__":
    main()
