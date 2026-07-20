from PIL import Image, ImageChops

from tools.art_pipeline.phase1_assets import (
    clean_transparent_rgb,
    derive_profile,
    normalize_to_canvas,
)


def test_normalize_to_canvas_preserves_safe_padding() -> None:
    source = Image.new("RGBA", (32, 16), (0, 0, 0, 0))
    for y in range(2, 14):
        for x in range(3, 29):
            source.putpixel((x, y), (80, 90, 70, 255))

    normalized = normalize_to_canvas(source, (128, 64), 12)
    bbox = normalized.getchannel("A").getbbox()

    assert normalized.size == (128, 64)
    assert bbox is not None
    assert bbox[0] >= 12
    assert bbox[1] >= 12
    assert normalized.width - bbox[2] >= 12
    assert normalized.height - bbox[3] >= 12


def test_derived_profile_is_deterministic_nearest_neighbor() -> None:
    master = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
    master.putpixel((2, 2), (20, 30, 40, 255))
    master.putpixel((3, 2), (80, 90, 100, 255))

    first = derive_profile(master, (4, 4))
    second = derive_profile(master, (4, 4))

    assert ImageChops.difference(first, second).getbbox() is None


def test_clean_transparent_rgb_zeroes_hidden_color() -> None:
    image = Image.new("RGBA", (2, 1), (0, 0, 0, 0))
    image.putpixel((0, 0), (255, 0, 255, 0))
    image.putpixel((1, 0), (30, 40, 50, 255))

    cleaned = clean_transparent_rgb(image)

    assert cleaned.getpixel((0, 0)) == (0, 0, 0, 0)
    assert cleaned.getpixel((1, 0)) == (30, 40, 50, 255)
