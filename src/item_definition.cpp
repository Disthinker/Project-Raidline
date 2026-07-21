#include "item_definition.h"

#include <stdexcept>

namespace
{
    constexpr std::size_t toIndex(ItemId id) noexcept
    {
        return static_cast<std::size_t>(id);
    }

    constexpr ItemDefinitionCatalog kItemDefinitions{{
        {
            ItemId::Cola,
            "Cola",
            ItemCategory::Consumable,
            1,
            1,
            Vec2{32.0f, 32.0f},
            Vec2{48.0f, 48.0f},
            "items/world/item_cola_basic_v1_32x32.png",
        },
        {
            ItemId::Medkit,
            "Medical Kit",
            ItemCategory::Medical,
            2,
            2,
            Vec2{64.0f, 64.0f},
            Vec2{72.0f, 72.0f},
            "items/world/item_medkit_basic_v1_64x64.png",
        },
        {
            ItemId::Pistol,
            "Pistol",
            ItemCategory::Weapon,
            2,
            1,
            Vec2{64.0f, 32.0f},
            Vec2{72.0f, 48.0f},
            "items/world/item_pistol_basic_v1_64x32.png",
        },
        {
            ItemId::Rifle,
            "Rifle",
            ItemCategory::Weapon,
            4,
            2,
            Vec2{128.0f, 64.0f},
            Vec2{136.0f, 72.0f},
            "items/world/item_rifle_basic_v1_128x64.png",
        },
    }};

    static_assert(
        kItemDefinitions.size() ==
        itemCount());

    // 防止未来修改枚举顺序或数组顺序后，
    // ItemId 到数组下标的映射悄悄失效。
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
}

const ItemDefinitionCatalog &
itemDefinitions() noexcept
{
    return kItemDefinitions;
}

const ItemDefinition &
itemDefinition(ItemId id)
{
    const std::size_t index = toIndex(id);

    if (index >= kItemDefinitions.size())
    {
        throw std::out_of_range{
            "ItemId does not refer to a valid item definition"};
    }

    return kItemDefinitions[index];
}