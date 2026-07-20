# Phase 1 Item Art Contract

## Runtime contract

```cpp
struct ItemArtDefinition
{
    std::string_view artId;
    int inventoryWidthCells;
    int inventoryHeightCells;
    bool canRotate;
    std::string_view inventoryTexturePath;
    std::string_view worldTexturePath;
    int worldDrawWidth;
    int worldDrawHeight;
};
```

The item collision and pickup bounds are gameplay data. They must not be inferred from the transparent image canvas.

## Profiles

- Inventory cell: `64x64` runtime pixels.
- Source master: four source pixels per inventory pixel.
- Inventory safe padding: at least six runtime pixels, equivalent to 24 source pixels.
- World pickup: one-half of the inventory canvas in both dimensions.
- Inventory rotation uses SDL rendering and swaps the logical footprint. It does not require a second bitmap.

## Paths

```text
assets/items/source/<asset_id>.png
assets/items/inventory/<asset_id>_<width>x<height>.png
assets/items/world/<asset_id>_<width>x<height>.png
```

## Phase 1 assets

| ID | Footprint | Master | Inventory | World | Rotate |
|---|---:|---:|---:|---:|---:|
| `item_cola_basic_v1` | 1x1 | 256x256 | 64x64 | 32x32 | no |
| `item_pistol_basic_v1` | 2x1 | 512x256 | 128x64 | 64x32 | yes |
| `item_rifle_basic_v1` | 4x2 | 1024x512 | 256x128 | 128x64 | yes |
| `item_medkit_basic_v1` | 2x2 | 512x512 | 128x128 | 64x64 | no |

