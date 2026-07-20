#pragma once

#include <cstddef>

#include "vec2.h"

enum class ItemId
{
    Cola,
    Medkit,
    Pistol,
    Rifle,
    Count
};

enum class ItemCategory
{
    Consumable,
    Medical,
    Weapon
};

struct ItemDefinition
{
    ItemId id{};
    const char *name{};
    ItemCategory category{};
    int height{};
    int width{};
    Vec2 displaySize{};
    const char *iconPath{};
};

constexpr std::size_t itemCount() noexcept
{
    return static_cast<std::size_t>(ItemId::Count);
}

const ItemDefinition &itemDefinition(ItemId id);