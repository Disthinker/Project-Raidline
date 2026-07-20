from __future__ import annotations

import argparse
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

CONTRACT_PATH = REPO_ROOT / "art/specs/named_firearms_pack_v1.json"
WORK_ROOT = REPO_ROOT / "art/work/named_firearms_pack_v1_retry"
SELECTION_PATH = WORK_ROOT / "selection.json"
SOURCE_DIR = REPO_ROOT / "assets/items/source"
INVENTORY_DIR = REPO_ROOT / "assets/items/inventory"
WORLD_DIR = REPO_ROOT / "assets/items/world"
REVIEW_ROOT = REPO_ROOT / "art/reviews/named_firearms_pack_v1"


def load_contract() -> dict[str, Any]:
    return cast(
        dict[str, Any],
        json.loads(CONTRACT_PATH.read_text(encoding="utf-8")),
    )


def load_selection(path: Path) -> dict[str, Path]:
    data = json.loads(path.read_text(encoding="utf-8"))
    selected: dict[str, Path] = {}
    for asset_id, raw_path in data["selected_candidates"].items():
        candidate = Path(raw_path)
        if not candidate.is_absolute():
            candidate = REPO_ROOT / candidate
        if not candidate.exists():
            raise FileNotFoundError(f"Selected candidate does not exist: {candidate}")
        selected[asset_id] = candidate
    return selected


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


def candidate_files(asset_id: str) -> list[Path]:
    matches = []
    for path in WORK_ROOT.rglob("*.png"):
        lowered = path.name.lower()
        if (
            asset_id in path.as_posix()
            and "candidate" in lowered
            and ("rgba" in lowered or "alpha" in lowered)
        ):
            matches.append(path)
    return sorted(matches)


def make_contact_sheet() -> Path:
    contract = load_contract()
    assets = list(contract["assets"])
    cell_size = (600, 320)
    canvas = checkerboard((cell_size[0] * 2, cell_size[1] * len(assets)))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()

    for row, asset_id in enumerate(assets):
        candidates = candidate_files(asset_id)
        if len(candidates) != contract["candidate_count_per_asset"]:
            raise RuntimeError(
                f"Expected two transparent candidates for {asset_id}, "
                f"found {candidates}"
            )
        for column, candidate_path in enumerate(candidates):
            image = Image.open(candidate_path).convert("RGBA")
            bbox = image.getchannel("A").getbbox()
            if bbox is None:
                raise RuntimeError(f"Candidate is transparent: {candidate_path}")
            cropped = image.crop(bbox)
            scale = min(540 / cropped.width, 240 / cropped.height)
            preview_size = (
                max(1, round(cropped.width * scale)),
                max(1, round(cropped.height * scale)),
            )
            preview = cropped.resize(preview_size, Image.Resampling.NEAREST)
            left = column * cell_size[0] + (cell_size[0] - preview.width) // 2
            top = row * cell_size[1] + 48 + (240 - preview.height) // 2
            canvas.alpha_composite(preview, (left, top))
            draw.text(
                (column * cell_size[0] + 12, row * cell_size[1] + 12),
                f"{asset_id} / candidate {column + 1}",
                font=font,
                fill=(255, 255, 255, 255),
            )

    destination = REVIEW_ROOT / "contact_sheet.png"
    save_png(canvas.convert("RGB"), destination)
    print(destination)
    return destination


def build_assets(selection_path: Path) -> None:
    contract = load_contract()
    selected = load_selection(selection_path)
    expected_ids = set(contract["assets"])
    if set(selected) != expected_ids:
        raise ValueError(
            "Selection IDs do not match the contract: "
            f"expected={sorted(expected_ids)} actual={sorted(selected)}"
        )

    safe_padding = (
        contract["inventory_safe_padding"] * contract["source_scale"]
    )
    for asset_id, spec in contract["assets"].items():
        candidate = Image.open(selected[asset_id]).convert("RGBA")
        master = normalize_to_canvas(
            candidate,
            tuple(spec["master_size"]),
            safe_padding,
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
    grid_size = (12, 7)
    canvas = Image.new(
        "RGBA",
        (grid_size[0] * cell, grid_size[1] * cell),
        (31, 34, 33, 255),
    )
    draw = ImageDraw.Draw(canvas)
    for x in range(grid_size[0] + 1):
        draw.line(
            (x * cell, 0, x * cell, canvas.height),
            fill=(110, 118, 113, 255),
            width=1,
        )
    for y in range(grid_size[1] + 1):
        draw.line(
            (0, y * cell, canvas.width, y * cell),
            fill=(110, 118, 113, 255),
            width=1,
        )

    placements = [
        ("item_pistol_qsz92g_v1", (0, 0), False),
        ("item_rifle_qbz95_1_v1", (0, 2), False),
        ("item_shotgun_m870_v1", (0, 5), False),
        ("item_pistol_qsz92g_v1", (6, 0), True),
        ("item_rifle_qbz95_1_v1", (8, 0), True),
        ("item_shotgun_m870_v1", (10, 0), True),
    ]
    for asset_id, position, rotated in placements:
        spec = contract["assets"][asset_id]
        image = Image.open(profile_paths(asset_id, spec)["inventory"]).convert(
            "RGBA"
        )
        if rotated:
            image = image.transpose(Image.Transpose.ROTATE_90)
        canvas.alpha_composite(image, (position[0] * cell, position[1] * cell))

    destination = REVIEW_ROOT / "inventory_preview.png"
    save_png(canvas, destination)
    return destination


def make_world_preview(contract: dict[str, Any]) -> Path:
    map_path = (
        REPO_ROOT
        / "assets/backgrounds/project_raidline_test_map_1280x720.png"
    )
    canvas = Image.open(map_path).convert("RGBA")
    positions = {
        "item_pistol_qsz92g_v1": (380, 330),
        "item_rifle_qbz95_1_v1": (610, 430),
        "item_shotgun_m870_v1": (850, 300),
    }
    for asset_id, position in positions.items():
        spec = contract["assets"][asset_id]
        image = Image.open(profile_paths(asset_id, spec)["world"]).convert("RGBA")
        canvas.alpha_composite(image, position)

    destination = REVIEW_ROOT / "world_preview.png"
    save_png(canvas, destination)
    return destination


def make_previews() -> None:
    contract = load_contract()
    print(make_inventory_preview(contract))
    print(make_world_preview(contract))


def validate_assets() -> dict[str, Any]:
    contract = load_contract()
    safe_padding = contract["inventory_safe_padding"]
    cell = contract["inventory_cell_size"]
    report: dict[str, Any] = {"status": "passed", "assets": {}}

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
            profile_ok = (
                image.size == expected_sizes[profile]
                and bbox is not None
                and corners == [0, 0, 0, 0]
            )
            checks[profile] = {
                "path": str(paths[profile].relative_to(REPO_ROOT)),
                "size": list(image.size),
                "expected_size": list(expected_sizes[profile]),
                "alpha_bbox": list(bbox) if bbox else None,
                "transparent_corners": corners,
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
        rotated = images["inventory"].transpose(Image.Transpose.ROTATE_90)
        rotation_ok = rotated.size == (
            footprint_height * cell,
            footprint_width * cell,
        )
        inventory_expected = derive_profile(
            images["master"],
            tuple(spec["inventory_size"]),
        )
        world_expected = derive_profile(images["master"], tuple(spec["world_size"]))
        identity_ok = (
            ImageChops.difference(
                images["inventory"],
                inventory_expected,
            ).getbbox()
            is None
            and ImageChops.difference(
                images["world"],
                world_expected,
            ).getbbox()
            is None
        )
        checks["safe_padding_ok"] = padding_ok
        checks["footprint_ok"] = footprint_ok
        checks["rotation"] = {
            "rotated_footprint": [footprint_height, footprint_width],
            "size_ok": rotation_ok,
        }
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
    markdown = [
        "# named_firearms_pack_v1 QA",
        "",
        f"- Overall status: `{report['status']}`",
        "",
    ]
    for asset_id, checks in report["assets"].items():
        markdown.extend(
            [
                f"## {asset_id}",
                "",
                f"- master profile: `{checks['master']['passed']}`",
                f"- inventory profile: `{checks['inventory']['passed']}`",
                f"- world profile: `{checks['world']['passed']}`",
                f"- safe padding: `{checks['safe_padding_ok']}`",
                f"- grid footprint: `{checks['footprint_ok']}`",
                f"- rotation contract: `{checks['rotation']['size_ok']}`",
                f"- deterministic identity: `{checks['deterministic_identity_ok']}`",
                "",
            ]
        )
    markdown_path = REVIEW_ROOT / "qa.md"
    markdown_path.write_text("\n".join(markdown), encoding="utf-8")
    print(json_path)
    print(markdown_path)
    return report


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Publish and validate named firearm assets"
    )
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
        make_contact_sheet()
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
