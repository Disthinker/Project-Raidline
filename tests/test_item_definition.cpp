#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

#include "item_definition.h"

namespace
{
    void expectSize(
        const Vec2 &actual,
        float expectedWidth,
        float expectedHeight)
    {
        EXPECT_FLOAT_EQ(actual.x, expectedWidth);
        EXPECT_FLOAT_EQ(actual.y, expectedHeight);
    }
}

TEST(
    ItemDefinitionTest,
    CatalogContainsFourDefinitions)
{
    EXPECT_EQ(itemCount(), 4u);
    EXPECT_EQ(itemDefinitions().size(), 4u);
}

TEST(
    ItemDefinitionTest,
    ColaMatchesDefinitionContract)
{
    const ItemDefinition &definition =
        itemDefinition(ItemId::Cola);

    EXPECT_EQ(definition.id, ItemId::Cola);
    EXPECT_EQ(
        definition.displayName,
        std::string_view{"Cola"});
    EXPECT_EQ(
        definition.category,
        ItemCategory::Consumable);

    EXPECT_EQ(definition.inventoryWidthCells, 1);
    EXPECT_EQ(definition.inventoryHeightCells, 1);

    expectSize(
        definition.worldRenderSize,
        32.0f,
        32.0f);

    expectSize(
        definition.pickupSize,
        48.0f,
        48.0f);

    EXPECT_EQ(
        definition.worldTexturePath,
        std::string_view{
            "items/world/"
            "item_cola_basic_v1_32x32.png"});
}

TEST(
    ItemDefinitionTest,
    MedkitMatchesDefinitionContract)
{
    const ItemDefinition &definition =
        itemDefinition(ItemId::Medkit);

    EXPECT_EQ(definition.id, ItemId::Medkit);
    EXPECT_EQ(
        definition.displayName,
        std::string_view{"Medical Kit"});
    EXPECT_EQ(
        definition.category,
        ItemCategory::Medical);

    EXPECT_EQ(definition.inventoryWidthCells, 2);
    EXPECT_EQ(definition.inventoryHeightCells, 2);

    expectSize(
        definition.worldRenderSize,
        64.0f,
        64.0f);

    expectSize(
        definition.pickupSize,
        72.0f,
        72.0f);
}

TEST(
    ItemDefinitionTest,
    PistolMatchesDefinitionContract)
{
    const ItemDefinition &definition =
        itemDefinition(ItemId::Pistol);

    EXPECT_EQ(definition.id, ItemId::Pistol);
    EXPECT_EQ(
        definition.category,
        ItemCategory::Weapon);

    EXPECT_EQ(definition.inventoryWidthCells, 2);
    EXPECT_EQ(definition.inventoryHeightCells, 1);

    expectSize(
        definition.worldRenderSize,
        64.0f,
        32.0f);

    expectSize(
        definition.pickupSize,
        72.0f,
        48.0f);
}

TEST(
    ItemDefinitionTest,
    RifleUsesManifestGridAndWorldSize)
{
    const ItemDefinition &definition =
        itemDefinition(ItemId::Rifle);

    EXPECT_EQ(definition.id, ItemId::Rifle);
    EXPECT_EQ(
        definition.category,
        ItemCategory::Weapon);

    EXPECT_EQ(definition.inventoryWidthCells, 4);
    EXPECT_EQ(definition.inventoryHeightCells, 2);

    expectSize(
        definition.worldRenderSize,
        128.0f,
        64.0f);

    expectSize(
        definition.pickupSize,
        136.0f,
        72.0f);
}

TEST(
    ItemDefinitionTest,
    EnumValuesMapToMatchingDefinitions)
{
    EXPECT_EQ(
        itemDefinition(ItemId::Cola).id,
        ItemId::Cola);

    EXPECT_EQ(
        itemDefinition(ItemId::Medkit).id,
        ItemId::Medkit);

    EXPECT_EQ(
        itemDefinition(ItemId::Pistol).id,
        ItemId::Pistol);

    EXPECT_EQ(
        itemDefinition(ItemId::Rifle).id,
        ItemId::Rifle);
}

TEST(
    ItemDefinitionTest,
    InventoryAndWorldSizesAreIndependent)
{
    const ItemDefinition &rifle =
        itemDefinition(ItemId::Rifle);

    // 4×2 是逻辑格子占用；
    // 128×64 是世界渲染像素尺寸。
    EXPECT_EQ(rifle.inventoryWidthCells, 4);
    EXPECT_EQ(rifle.inventoryHeightCells, 2);

    EXPECT_FLOAT_EQ(
        rifle.worldRenderSize.x,
        128.0f);

    EXPECT_FLOAT_EQ(
        rifle.worldRenderSize.y,
        64.0f);
}

TEST(
    ItemDefinitionTest,
    RejectsCountSentinel)
{
    EXPECT_THROW(
        itemDefinition(ItemId::Count),
        std::out_of_range);
}

TEST(
    ItemDefinitionTest,
    RejectsUnknownEnumValue)
{
    const ItemId invalidId =
        static_cast<ItemId>(999);

    EXPECT_THROW(
        itemDefinition(invalidId),
        std::out_of_range);
}