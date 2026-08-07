#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vec2.h"

// 标识“物品定义的种类”。
// Count 不是物品，仅用于得到静态定义表的大小。
enum class ItemId
{
    Cola,
    Medkit,
    Pistol,
    Rifle,
    Ammo9mm,
    Count
};

enum class ItemCategory
{
    Consumable,
    Medical,
    Weapon,
    Ammunition
};

enum class ItemOrientation
{
    Degrees0,
    Degrees90,
    Degrees180,
    Degrees270
};

struct InventoryFootprint
{
    int width{};
    int height{};

    friend bool operator==(
        const InventoryFootprint &,
        const InventoryFootprint &) = default;
};

// 一种物品的共享静态数据。
// 它不代表世界中某一个具体物品，也不拥有 Texture。
struct ItemDefinition
{
    ItemId id{};
    std::string_view displayName{};
    ItemCategory category{};

    int inventoryWidthCells{};
    int inventoryHeightCells{};
    bool canRotate{};
    std::uint32_t maxStackSize{1};

    // A logical definition may land before its approved art package. Such
    // entries must not be spawned by production content or loaded as textures.
    bool visualAssetsPublished{true};

    Vec2 worldRenderSize{};
    Vec2 pickupSize{};

    // 相对于运行时 assets/ 目录的路径。
    std::string_view inventoryTexturePath{};
    std::string_view worldTexturePath{};
};

constexpr std::size_t itemCount() noexcept
{
    return static_cast<std::size_t>(
        ItemId::Count);
}

using ItemDefinitionCatalog =
    std::array<ItemDefinition, itemCount()>;

// 返回完整静态目录，不复制数据。
[[nodiscard]]
const ItemDefinitionCatalog &
itemDefinitions() noexcept;

// 根据稳定 ItemId 查询定义。
// ItemId::Count 或其他非法枚举值会抛出 std::out_of_range。
[[nodiscard]]
const ItemDefinition &
itemDefinition(ItemId id);

[[nodiscard]]
bool isValidItemOrientation(
    ItemOrientation orientation) noexcept;

[[nodiscard]]
ItemOrientation rotatedClockwise(
    ItemOrientation orientation) noexcept;

[[nodiscard]]
bool canUseItemOrientation(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept;

[[nodiscard]]
InventoryFootprint inventoryFootprint(
    const ItemDefinition &definition,
    ItemOrientation orientation) noexcept;

[[nodiscard]]
Vec2 orientedSize(
    Vec2 baseSize,
    ItemOrientation orientation) noexcept;
