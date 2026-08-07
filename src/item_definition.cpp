#include "item_definition.h"

#include <stdexcept>

namespace
{

    constexpr std::size_t toIndex(
        ItemId id) noexcept
    {
        return static_cast<std::size_t>(id);
    }

    constexpr ItemDefinitionCatalog
        kItemDefinitions{{
            {
                ItemId::Cola,
                "Cola",
                ItemCategory::Consumable,
                1,
                1,
                false,
                1,
                true,
                Vec2{32.0f, 32.0f},
                Vec2{48.0f, 48.0f},
                "items/inventory/"
                "item_cola_basic_v1_64x64.png",
                "items/world/"
                "item_cola_basic_v1_32x32.png",
            },
            {
                ItemId::Medkit,
                "Medical Kit",
                ItemCategory::Medical,
                2,
                2,
                false,
                1,
                true,
                Vec2{64.0f, 64.0f},
                Vec2{72.0f, 72.0f},
                "items/inventory/"
                "item_medkit_basic_v1_128x128.png",
                "items/world/"
                "item_medkit_basic_v1_64x64.png",
            },
            {
                ItemId::Pistol,
                "Pistol",
                ItemCategory::Weapon,
                2,
                1,
                true,
                1,
                true,
                Vec2{64.0f, 32.0f},
                Vec2{72.0f, 48.0f},
                "items/inventory/"
                "item_pistol_basic_v1_128x64.png",
                "items/world/"
                "item_pistol_basic_v1_64x32.png",
            },
            {
                ItemId::Rifle,
                "Rifle",
                ItemCategory::Weapon,
                4,
                2,
                true,
                1,
                true,
                Vec2{128.0f, 64.0f},
                Vec2{136.0f, 72.0f},
                "items/inventory/"
                "item_rifle_basic_v1_256x128.png",
                "items/world/"
                "item_rifle_basic_v1_128x64.png",
            },
            {
                ItemId::Ammo9mm,
                "9mm Ammunition",
                ItemCategory::Ammunition,
                1,
                1,
                false,
                60,
                true,
                Vec2{32.0f, 32.0f},
                Vec2{48.0f, 48.0f},
                "items/inventory/"
                "item_ammo_9mm_basic_v1_64x64.png",
                "items/world/"
                "item_ammo_9mm_basic_v1_32x32.png",
            },
        }};

    static_assert(
        kItemDefinitions.size() ==
        itemCount());

    static_assert(
        kItemDefinitions[toIndex(ItemId::Cola)].id ==
        ItemId::Cola);

    static_assert(
        kItemDefinitions[toIndex(ItemId::Medkit)].id ==
        ItemId::Medkit);

    static_assert(
        kItemDefinitions[toIndex(ItemId::Pistol)].id ==
        ItemId::Pistol);

    static_assert(
        kItemDefinitions[toIndex(ItemId::Rifle)].id ==
        ItemId::Rifle);

    static_assert(
        kItemDefinitions[toIndex(ItemId::Ammo9mm)].id ==
        ItemId::Ammo9mm);

} // namespace

const ItemDefinitionCatalog &
itemDefinitions() noexcept
{
    return kItemDefinitions;
}

const ItemDefinition &
itemDefinition(ItemId id)
{
    const std::size_t index =
        toIndex(id);

    if (index >= kItemDefinitions.size())
    {
        throw std::out_of_range{
            "ItemId does not refer to "
            "a valid item definition"};
    }

    return kItemDefinitions[index];
}

bool isValidItemOrientation(
    ItemOrientation orientation) noexcept
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
    case ItemOrientation::Degrees90:
    case ItemOrientation::Degrees180:
    case ItemOrientation::Degrees270:
        return true;
    }

    return false;
}

ItemOrientation rotatedClockwise(
    ItemOrientation orientation) noexcept
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
        return ItemOrientation::Degrees90;
    case ItemOrientation::Degrees90:
        return ItemOrientation::Degrees180;
    case ItemOrientation::Degrees180:
        return ItemOrientation::Degrees270;
    case ItemOrientation::Degrees270:
        return ItemOrientation::Degrees0;
    }

    return ItemOrientation::Degrees0;
}

bool canUseItemOrientation(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept
{
    if (!isValidItemOrientation(orientation))
    {
        return false;
    }

    return orientation == ItemOrientation::Degrees0 ||
           definition.canRotate;
}

InventoryFootprint inventoryFootprint(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept
{
    if (!canUseItemOrientation(definition, orientation))
    {
        return InventoryFootprint{};
    }

    if (orientation == ItemOrientation::Degrees90 ||
        orientation == ItemOrientation::Degrees270)
    {
        return InventoryFootprint{
            definition.inventoryHeightCells,
            definition.inventoryWidthCells};
    }

    return InventoryFootprint{
        definition.inventoryWidthCells,
        definition.inventoryHeightCells};
}

Vec2 orientedSize(
    Vec2 baseSize,
    ItemOrientation orientation) noexcept
{
    if (orientation == ItemOrientation::Degrees90 ||
        orientation == ItemOrientation::Degrees270)
    {
        return Vec2{baseSize.y, baseSize.x};
    }

    return baseSize;
}
