from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, cast

from PIL import Image, ImageChops, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = REPO_ROOT / "art/specs/phase1_item_contracts.json"
SELECTION_PATH = REPO_ROOT / "art/work/phase1_selection.json"
SOURCE_DIR = REPO_ROOT / "assets/items/source"
INVENTORY_DIR = REPO_ROOT / "assets/items/inventory"
WORLD_DIR = REPO_ROOT / "assets/items/world"
REVIEW_DIR = REPO_ROOT / "art/reviews"
CALIBRATION_DIR = REPO_ROOT / "art/references/calibration"


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


def clean_transparent_rgb(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    if pixels is None:
        raise RuntimeError("Could not access image pixels")
    for y in range(rgba.height):
        for x in range(rgba.width):
            red, green, blue, alpha = cast(
                tuple[int, int, int, int],
                pixels[x, y],
            )
            if alpha == 0:
                pixels[x, y] = (0, 0, 0, 0)
            else:
                pixels[x, y] = (red, green, blue, alpha)
    return rgba


def normalize_to_canvas(
    image: Image.Image,
    canvas_size: tuple[int, int],
    safe_padding: int,
) -> Image.Image:
    rgba = clean_transparent_rgb(image)
    bbox = rgba.getchannel("A").getbbox()
    if bbox is None:
        raise ValueError("Candidate is fully transparent")

    cropped = rgba.crop(bbox)
    available_width = canvas_size[0] - safe_padding * 2
    available_height = canvas_size[1] - safe_padding * 2
    if available_width <= 0 or available_height <= 0:
        raise ValueError("Safe padding leaves no usable canvas")

    scale = min(
        available_width / cropped.width,
        available_height / cropped.height,
    )
    resized_size = (
        max(1, round(cropped.width * scale)),
        max(1, round(cropped.height * scale)),
    )
    resized = cropped.resize(resized_size, Image.Resampling.NEAREST)
    resized = clean_transparent_rgb(resized)

    canvas = Image.new("RGBA", canvas_size, (0, 0, 0, 0))
    offset = (
        (canvas_size[0] - resized.width) // 2,
        (canvas_size[1] - resized.height) // 2,
    )
    canvas.alpha_composite(resized, offset)
    return clean_transparent_rgb(canvas)


def derive_profile(master: Image.Image, size: tuple[int, int]) -> Image.Image:
    return clean_transparent_rgb(master.resize(size, Image.Resampling.NEAREST))


def save_png(image: Image.Image, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination, optimize=True)


def profile_paths(asset_id: str, spec: dict[str, Any]) -> dict[str, Path]:
    inventory_width, inventory_height = spec["inventory_size"]
    world_width, world_height = spec["world_size"]
    return {
        "master": SOURCE_DIR / f"{asset_id}.png",
        "inventory": INVENTORY_DIR
        / f"{asset_id}_{inventory_width}x{inventory_height}.png",
        "world": WORLD_DIR / f"{asset_id}_{world_width}x{world_height}.png",
    }


def build_assets(selection_path: Path) -> None:
    contract = load_contract()
    selected = load_selection(selection_path)
    expected_ids = set(contract["assets"])
    if set(selected) != expected_ids:
        raise ValueError(
            "Selection IDs do not match contract IDs: "
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


def checkerboard(size: tuple[int, int], tile: int = 16) -> Image.Image:
    image = Image.new("RGBA", size, (43, 47, 45, 255))
    draw = ImageDraw.Draw(image)
    for y in range(0, size[1], tile):
        for x in range(0, size[0], tile):
            if (x // tile + y // tile) % 2:
                draw.rectangle(
                    (x, y, x + tile - 1, y + tile - 1),
                    fill=(59, 64, 61, 255),
                )
    return image


def make_calibration_templates() -> None:
    CALIBRATION_DIR.mkdir(parents=True, exist_ok=True)
    font = ImageFont.load_default()

    character = Image.new("RGBA", (256, 320), (0, 0, 0, 0))
    draw = ImageDraw.Draw(character)
    draw.line((128, 0, 128, 319), fill=(70, 170, 255, 180), width=1)
    draw.line((0, 300, 255, 300), fill=(255, 190, 40, 255), width=2)
    draw.ellipse((124, 296, 132, 304), outline=(255, 255, 255, 255), width=1)
    draw.text((134, 286), "pivot 128,300", font=font, fill=(255, 255, 255, 255))
    draw.rectangle((0, 0, 255, 319), outline=(255, 255, 255, 160), width=1)
    save_png(
        character,
        CALIBRATION_DIR / "character_alignment_template_256x320.png",
    )

    grid = Image.new("RGBA", (640, 384), (31, 34, 33, 255))
    grid_draw = ImageDraw.Draw(grid)
    for x in range(0, 641, 64):
        grid_draw.line((x, 0, x, 384), fill=(110, 118, 113, 255), width=1)
    for y in range(0, 385, 64):
        grid_draw.line((0, y, 640, y), fill=(110, 118, 113, 255), width=1)
    save_png(grid, CALIBRATION_DIR / "inventory_grid_template_10x6_64px.png")
    print(f"wrote calibration templates to {CALIBRATION_DIR}")


def candidate_files(asset_id: str) -> list[Path]:
    matches = []
    for path in (REPO_ROOT / "art/work").rglob("*.png"):
        lowered = path.name.lower()
        if (
            asset_id in path.as_posix()
            and "candidate" in lowered
            and ("alpha" in lowered or "rgba" in lowered)
        ):
            matches.append(path)
    return sorted(matches)


def make_contact_sheet() -> None:
    contract = load_contract()
    assets = list(contract["assets"])
    cell_size = (420, 300)
    canvas = checkerboard((cell_size[0] * 2, cell_size[1] * len(assets)))
    draw = ImageDraw.Draw(canvas)
    font = ImageFont.load_default()

    for row, asset_id in enumerate(assets):
        candidates = candidate_files(asset_id)
        if len(candidates) < 2:
            raise RuntimeError(
                "Expected at least two alpha candidates for "
                f"{asset_id}, found {candidates}"
            )
        for column, candidate_path in enumerate(candidates[:2]):
            image = Image.open(candidate_path).convert("RGBA")
            bbox = image.getchannel("A").getbbox()
            if bbox is None:
                raise RuntimeError(f"Candidate is fully transparent: {candidate_path}")
            cropped = image.crop(bbox)
            scale = min(350 / cropped.width, 230 / cropped.height)
            size = (
                max(1, round(cropped.width * scale)),
                max(1, round(cropped.height * scale)),
            )
            preview = cropped.resize(size, Image.Resampling.NEAREST)
            left = column * cell_size[0] + (cell_size[0] - size[0]) // 2
            top = row * cell_size[1] + 44 + (230 - size[1]) // 2
            canvas.alpha_composite(preview, (left, top))
            label = f"{asset_id} / candidate {column + 1}"
            draw.text(
                (column * cell_size[0] + 12, row * cell_size[1] + 12),
                label,
                font=font,
                fill=(255, 255, 255, 255),
            )

    destination = REVIEW_DIR / "contact_sheets/phase1_item_candidates.png"
    save_png(canvas.convert("RGB"), destination)
    print(destination)


def make_inventory_preview(contract: dict[str, Any]) -> Path:
    grid_size = (10, 6)
    cell = contract["inventory_cell_size"]
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
        ("item_cola_basic_v1", (0, 0), False),
        ("item_pistol_basic_v1", (1, 0), False),
        ("item_medkit_basic_v1", (4, 0), False),
        ("item_rifle_basic_v1", (0, 3), False),
        ("item_pistol_basic_v1", (7, 0), True),
        ("item_rifle_basic_v1", (7, 2), True),
    ]
    for asset_id, grid_position, rotated in placements:
        spec = contract["assets"][asset_id]
        image = Image.open(profile_paths(asset_id, spec)["inventory"]).convert("RGBA")
        if rotated:
            image = image.transpose(Image.Transpose.ROTATE_90)
        canvas.alpha_composite(
            image,
            (grid_position[0] * cell, grid_position[1] * cell),
        )

    destination = REVIEW_DIR / "previews/phase1_inventory_layout.png"
    save_png(canvas, destination)
    return destination


def make_world_preview(contract: dict[str, Any]) -> Path:
    map_path = (
        REPO_ROOT
        / "assets/backgrounds/project_raidline_test_map_1280x720.png"
    )
    canvas = Image.open(map_path).convert("RGBA")
    positions = {
        "item_cola_basic_v1": (360, 300),
        "item_pistol_basic_v1": (500, 390),
        "item_rifle_basic_v1": (700, 450),
        "item_medkit_basic_v1": (930, 320),
    }
    for asset_id, position in positions.items():
        spec = contract["assets"][asset_id]
        image = Image.open(profile_paths(asset_id, spec)["world"]).convert("RGBA")
        canvas.alpha_composite(image, position)

    destination = REVIEW_DIR / "previews/phase1_world_pickups.png"
    save_png(canvas, destination)
    return destination


def validate_assets() -> dict[str, Any]:
    contract = load_contract()
    safe_padding = contract["inventory_safe_padding"]
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
        for name, image in images.items():
            alpha = image.getchannel("A")
            bbox = alpha.getbbox()
            corners = [
                alpha.getpixel((0, 0)),
                alpha.getpixel((image.width - 1, 0)),
                alpha.getpixel((0, image.height - 1)),
                alpha.getpixel((image.width - 1, image.height - 1)),
            ]
            checks[name] = {
                "path": str(paths[name].relative_to(REPO_ROOT)),
                "size": list(image.size),
                "expected_size": list(expected_sizes[name]),
                "size_ok": image.size == expected_sizes[name],
                "alpha_bbox": list(bbox) if bbox else None,
                "transparent_corners_ok": corners == [0, 0, 0, 0],
            }
            if (
                image.size != expected_sizes[name]
                or bbox is None
                or corners != [0, 0, 0, 0]
            ):
                report["status"] = "failed"

        inventory_bbox = images["inventory"].getchannel("A").getbbox()
        if inventory_bbox is None:
            padding_ok = False
        else:
            padding_ok = (
                inventory_bbox[0] >= safe_padding
                and inventory_bbox[1] >= safe_padding
                and images["inventory"].width - inventory_bbox[2] >= safe_padding
                and images["inventory"].height - inventory_bbox[3] >= safe_padding
            )
        checks["inventory"]["safe_padding_ok"] = padding_ok
        if not padding_ok:
            report["status"] = "failed"

        footprint_width, footprint_height = spec["grid_footprint"]
        footprint_size_ok = images["inventory"].size == (
            footprint_width * contract["inventory_cell_size"],
            footprint_height * contract["inventory_cell_size"],
        )
        checks["grid_footprint_matches_inventory"] = footprint_size_ok
        if not footprint_size_ok:
            report["status"] = "failed"

        if spec["can_rotate"]:
            rotated_inventory = images["inventory"].transpose(
                Image.Transpose.ROTATE_90
            )
            rotated_size = (
                footprint_height * contract["inventory_cell_size"],
                footprint_width * contract["inventory_cell_size"],
            )
            rotation_ok = rotated_inventory.size == rotated_size
            checks["rotation"] = {
                "enabled": True,
                "rotated_footprint": [footprint_height, footprint_width],
                "rotated_size": list(rotated_inventory.size),
                "expected_size": list(rotated_size),
                "size_ok": rotation_ok,
            }
            if not rotation_ok:
                report["status"] = "failed"
        else:
            checks["rotation"] = {
                "enabled": False,
                "rotated_footprint": None,
                "size_ok": True,
            }

        expected_inventory = derive_profile(
            images["master"],
            tuple(spec["inventory_size"]),
        )
        expected_world = derive_profile(images["master"], tuple(spec["world_size"]))
        identity_ok = (
            ImageChops.difference(
                images["inventory"],
                expected_inventory,
            ).getbbox()
            is None
            and ImageChops.difference(images["world"], expected_world).getbbox()
            is None
        )
        checks["deterministic_identity_ok"] = identity_ok
        if not identity_ok:
            report["status"] = "failed"

        report["assets"][asset_id] = checks

    qa_dir = REVIEW_DIR / "batch_reports"
    qa_dir.mkdir(parents=True, exist_ok=True)
    json_path = qa_dir / "phase1_week13_16_qa.json"
    json_path.write_text(
        json.dumps(report, indent=2, ensure_ascii=False),
        encoding="utf-8",
    )

    markdown_lines = [
        "# Phase 1 Week 13-16 Asset QA",
        "",
        f"- Overall status: `{report['status']}`",
        "- Visual review remains a separate human/agent gate.",
        "",
    ]
    for asset_id, checks in report["assets"].items():
        transparent_corners_ok = (
            checks["inventory"]["transparent_corners_ok"]
            and checks["world"]["transparent_corners_ok"]
        )
        markdown_lines.extend(
            [
                f"## {asset_id}",
                "",
                f"- master size: `{checks['master']['size_ok']}`",
                f"- inventory size: `{checks['inventory']['size_ok']}`",
                f"- world size: `{checks['world']['size_ok']}`",
                f"- transparent corners: `{transparent_corners_ok}`",
                f"- inventory safe padding: `{checks['inventory']['safe_padding_ok']}`",
                "- footprint matches inventory canvas: "
                f"`{checks['grid_footprint_matches_inventory']}`",
                f"- rotation contract: `{checks['rotation']['size_ok']}`",
                f"- deterministic identity: `{checks['deterministic_identity_ok']}`",
                "",
            ]
        )
    markdown_path = qa_dir / "phase1_week13_16_qa.md"
    markdown_path.write_text("\n".join(markdown_lines), encoding="utf-8")
    print(json_path)
    print(markdown_path)
    return report


def make_previews() -> None:
    contract = load_contract()
    inventory = make_inventory_preview(contract)
    world = make_world_preview(contract)
    print(inventory)
    print(world)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build and validate Phase 1 item art")
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("calibration")
    subparsers.add_parser("contact-sheet")
    subparsers.add_parser("previews")
    subparsers.add_parser("validate")

    build = subparsers.add_parser("build")
    build.add_argument(
        "--selection",
        type=Path,
        default=SELECTION_PATH,
    )
    return parser


def main() -> None:
    args = build_parser().parse_args()
    if args.command == "calibration":
        make_calibration_templates()
    elif args.command == "contact-sheet":
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
