#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "alpha_content_ids.h"
#include "content_registry.h"

namespace
{
    std::string replaceFirst(
        std::string text,
        std::string_view from,
        std::string_view to)
    {
        const std::size_t position = text.find(from);
        if (position == std::string::npos)
        {
            throw std::logic_error{"test fixture text was not found"};
        }
        text.replace(position, from.size(), to);
        return text;
    }

    std::string publishedJsonCopy()
    {
        return std::string{publishedContentJson()};
    }
}

static_assert(
    !std::is_constructible_v<
        LootTableDefinitionId,
        ItemDefinitionId>);
static_assert(
    std::is_same_v<
        decltype(std::declval<const ContentRegistry &>().items()),
        const std::vector<ItemDefinition> &>);

TEST(DefinitionIdTest, AcceptsStableNamespacedIdentifiers)
{
    const ItemDefinitionId id{"item.weapon.rifle_basic"};
    EXPECT_TRUE(id.valid());
    EXPECT_EQ(id.value(), "item.weapon.rifle_basic");
}

TEST(DefinitionIdTest, RejectsUnsafeOrUnnamespacedIdentifiers)
{
    EXPECT_THROW(ItemDefinitionId{""}, std::invalid_argument);
    EXPECT_THROW(ItemDefinitionId{"rifle"}, std::invalid_argument);
    EXPECT_THROW(ItemDefinitionId{"item..rifle"}, std::invalid_argument);
    EXPECT_THROW(ItemDefinitionId{"item.Rifle"}, std::invalid_argument);
    EXPECT_THROW(ItemDefinitionId{"item/rifle"}, std::invalid_argument);
    EXPECT_THROW(ItemDefinitionId{"item.rifle "}, std::invalid_argument);
}

TEST(ContentRegistryTest, PublishedRegistryPreservesCurrentContentContract)
{
    const ContentRegistry &registry = publishedContentRegistry();

    EXPECT_EQ(registry.contentVersion(), "survival-loadout-content-2");
    ASSERT_EQ(registry.items().size(), 16U);
    ASSERT_EQ(registry.lootTables().size(), 2U);
    ASSERT_EQ(registry.enemyDeployments().size(), 4U);
    ASSERT_EQ(registry.maps().size(), 1U);

    const ItemDefinition &ammunition = registry.item(
        ItemDefinitionId{"item.ammunition.9mm_basic"});
    EXPECT_EQ(ammunition.id, ItemId::Ammo9mm);
    EXPECT_EQ(ammunition.maxStackSize, 60U);
    EXPECT_EQ(ammunition.marketBuyPrice, 1U);

    const ItemDefinition &chestRig = registry.item(
        ItemDefinitionId{"item.container.chest_rig_small"});
    EXPECT_EQ(chestRig.id, ItemId::Count);
    EXPECT_EQ(chestRig.equipmentSlot, EquipmentSlotKind::ChestRig);
    ASSERT_EQ(chestRig.containerCompartments.size(), 4U);
    EXPECT_EQ(
        chestRig.containerCompartments.front().pocketKind,
        ContainerPocketKind::MagazineOnly);

    const ItemDefinition &helmet = registry.item(alpha_content::helmet);
    ASSERT_TRUE(helmet.armorProtection.has_value());
    EXPECT_EQ(helmet.equipmentSlot, EquipmentSlotKind::Helmet);
    EXPECT_EQ(helmet.armorProtection->coverage, HitRegion::Head);
    EXPECT_EQ(helmet.armorProtection->maximumDurability, 100U);

    const ItemDefinition &bodyArmor = registry.item(alpha_content::bodyArmor);
    ASSERT_TRUE(bodyArmor.armorProtection.has_value());
    EXPECT_EQ(bodyArmor.equipmentSlot, EquipmentSlotKind::BodyArmor);
    EXPECT_EQ(bodyArmor.armorProtection->coverage, HitRegion::Torso);

    const ItemDefinition &bandage = registry.item(alpha_content::bandage);
    ASSERT_TRUE(bandage.medicalUse.has_value());
    EXPECT_EQ(
        bandage.medicalUse->effect,
        MedicalItemEffect::StopLightBleeding);
    EXPECT_EQ(bandage.medicalUse->actionDurationMs, 2000U);

    const MapDefinition &map = defaultV0MapDefinition();
    EXPECT_EQ(map.id.value(), "map.v0.test");
    EXPECT_FLOAT_EQ(map.worldSize.x, 1280.0F);
    EXPECT_FLOAT_EQ(map.worldSize.y, 720.0F);
    EXPECT_FLOAT_EQ(map.playerSpawn.x, 640.0F);
    EXPECT_FLOAT_EQ(map.playerSpawn.y, 360.0F);
    EXPECT_EQ(map.spawnExtractionPairs.size(), 3U);
    EXPECT_EQ(map.raidEnemyDeploymentIds.size(), 3U);
    EXPECT_EQ(map.raidLootSlots.size(), 10U);
    EXPECT_EQ(map.raidLootTableId.value(), "loot.raid.alpha");
    EXPECT_EQ(map.defaultInventorySize.width, 10);
    EXPECT_EQ(map.defaultInventorySize.height, 6);
    EXPECT_EQ(map.groundItems.size(), 6U);
    EXPECT_FLOAT_EQ(map.storageCabinet.bounds.position.x, 960.0F);
    EXPECT_FLOAT_EQ(map.storageCabinet.bounds.position.y, 296.0F);
    EXPECT_EQ(map.storageCabinet.inventorySize.width, 6);
    EXPECT_EQ(map.storageCabinet.inventorySize.height, 6);
    EXPECT_FLOAT_EQ(map.extractionPoint.position.x, 64.0F);
    EXPECT_FLOAT_EQ(map.extractionPoint.position.y, 520.0F);
    EXPECT_FLOAT_EQ(map.raidRules.durationSeconds, 180.0F);
    EXPECT_FLOAT_EQ(map.raidRules.extractionDurationSeconds, 3.0F);

    const EnemyDeploymentDefinition &deployment =
        registry.enemyDeployment(map.enemyDeploymentId);
    ASSERT_EQ(deployment.enemies.size(), 3U);
    EXPECT_FLOAT_EQ(deployment.enemies[0].position.x, 600.0F);
    EXPECT_FLOAT_EQ(deployment.enemies[1].position.y, 500.0F);
    EXPECT_FLOAT_EQ(deployment.enemies[2].position.x, 930.0F);
}

TEST(ContentRegistryTest, LegacyAdapterCoversEveryCurrentItemExactly)
{
    for (std::size_t index = 0; index < itemCount(); ++index)
    {
        const ItemId legacyId = static_cast<ItemId>(index);
        const ItemDefinitionId &stableId =
            legacyItemDefinitionId(legacyId);
        ASSERT_EQ(legacyItemId(stableId), legacyId);
        EXPECT_EQ(itemDefinition(legacyId).definitionId, stableId);
    }

    EXPECT_THROW(
        legacyItemDefinitionId(ItemId::Count),
        std::out_of_range);
}

TEST(ContentRegistryTest, RejectsUnsupportedSchema)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"schema_version\": 1",
        "\"schema_version\": 2");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsDuplicatePublishedResource)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"published_resources\": [",
        "\"published_resources\": [\n"
        "    \"backgrounds/project_raidline_test_map_1280x720.png\",");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsDuplicateOrMismatchedItemIdentity)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"id\": \"item.medical.medkit_basic\"",
        "\"id\": \"item.loot.cola_basic\"");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidItemFootprint)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"inventory\": {\"width\": 1, \"height\": 1",
        "\"inventory\": {\"width\": 0, \"height\": 1");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsFixedSupplyOutsideAlphaRecycleBaseline)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"market_buy_price\": 300,\"market_recycle_price\": 75",
        "\"market_buy_price\": 300,\"market_recycle_price\": 76");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnsupportedMedicalEffect)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"effect\": \"stop_light_bleeding\"",
        "\"effect\": \"cure_everything\"");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnknownLootItemReference)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"item\": \"item.loot.cola_basic\", \"weight\": 24",
        "{\"item\": \"item.loot.unknown\", \"weight\": 24");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidLootQuantity)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"maximum_quantity\": 30",
        "\"maximum_quantity\": 61");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnknownMapDefinitionReference)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"enemy_deployment\": \"enemy_deployment.v0.default\"",
        "\"enemy_deployment\": \"enemy_deployment.v0.missing\"");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsEmptyEnemyDeployment)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"enemies\": [",
        "\"enemies\": [], \"ignored_enemies\": [");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsDisconnectedOrOutOfBoundsMapAnchor)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"player_spawn\": {\"x\": 640, \"y\": 360}",
        "\"player_spawn\": {\"x\": 1400, \"y\": 360}");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsMissingPublishedResourceReference)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "backgrounds/project_raidline_test_map_1280x720.png",
        "backgrounds/missing.png");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, PublishedResourceFilesExist)
{
    const std::filesystem::path assetRoot{
        RAIDLINE_SOURCE_ASSET_DIR};
    for (const std::string &resource :
         publishedContentRegistry().publishedResources())
    {
        EXPECT_TRUE(std::filesystem::is_regular_file(
            assetRoot / resource))
            << resource;
    }
}

TEST(ContentRegistryTest, UnknownStrongIdLookupFailsExplicitly)
{
    EXPECT_THROW(
        publishedContentRegistry().item(
            ItemDefinitionId{"item.unknown.placeholder"}),
        std::out_of_range);
}
