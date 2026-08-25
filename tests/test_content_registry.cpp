#include <gtest/gtest.h>

#include <filesystem>
#include <set>
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

    EXPECT_EQ(
        registry.contentVersion(),
        "base-instant-gunsmith-content-18");
    EXPECT_EQ(registry.gunsmithFullMaintenance().baseCost, 40U);
    EXPECT_EQ(
        registry.gunsmithFullMaintenance().currentDurabilityCostPerPoint,
        1U);
    EXPECT_EQ(
        registry.gunsmithFullMaintenance().maximumDurabilityCostPerPoint,
        2U);
    EXPECT_EQ(registry.baseOperations().strainedBelowReserveDays, 3U);
    EXPECT_EQ(registry.baseOperations().supportedAtReserveDays, 7U);
    EXPECT_EQ(registry.basePriorityCycleMinutes(), 7200U);
    ASSERT_EQ(registry.basePriorities().size(), 3U);
    const BasePriorityDefinition &comfort = registry.basePriority(
        BasePriorityDefinitionId{"base_priority.comfort_cola"});
    EXPECT_EQ(comfort.requiredItemDefinitionId, alpha_content::lootCola);
    EXPECT_EQ(comfort.requiredQuantity, 1U);
    EXPECT_EQ(comfort.resourceReward, (BaseResourceBundle{0, 0, 12, 0}));
    ASSERT_EQ(registry.items().size(), 19U);
    ASSERT_EQ(registry.lootTables().size(), 3U);
    ASSERT_EQ(registry.enemyDeployments().size(), 10U);
    ASSERT_EQ(registry.maps().size(), 3U);

    std::set<MapDefinitionId> mapIds;
    std::set<EnemyDeploymentDefinitionId> raidDeploymentIds;
    std::set<std::uint32_t> outboundTravelMinutes;
    for (const MapDefinition &publishedMap : registry.maps())
    {
        EXPECT_FALSE(publishedMap.displayName.empty());
        EXPECT_FALSE(publishedMap.routeProfile.empty());
        EXPECT_GT(publishedMap.travel.outboundMinutes, 0U);
        EXPECT_GT(publishedMap.travel.returnMinutes, 0U);
        EXPECT_GE(publishedMap.travel.failureRegroupMinutes,
                  publishedMap.travel.returnMinutes);
        EXPECT_TRUE(outboundTravelMinutes.insert(
            publishedMap.travel.outboundMinutes).second);
        EXPECT_GT(publishedMap.backgroundTint.red, 0U);
        EXPECT_GT(publishedMap.backgroundTint.green, 0U);
        EXPECT_GT(publishedMap.backgroundTint.blue, 0U);
        EXPECT_EQ(publishedMap.spawnExtractionPairs.size(), 3U);
        EXPECT_EQ(publishedMap.raidEnemyDeploymentIds.size(), 3U);
        EXPECT_EQ(publishedMap.raidLootSlots.size(), 10U);
        EXPECT_TRUE(publishedMap.highRisk.enabled);
        EXPECT_FLOAT_EQ(
            publishedMap.highRisk.regularPhaseDurationSeconds,
            180.0F);
        EXPECT_FLOAT_EQ(
            publishedMap.highRisk.emergencyExtractionDurationSeconds,
            12.0F);
        EXPECT_FLOAT_EQ(
            publishedMap.highRisk.conditionalExtractionDurationSeconds,
            6.0F);
        EXPECT_EQ(
            publishedMap.highRisk.conditionalExtractionMaximumWeightGrams,
            22000U);
        EXPECT_EQ(publishedMap.highRisk.waveSize, 2U);
        EXPECT_EQ(publishedMap.highRisk.activeEnemyCap, 8U);
        EXPECT_EQ(publishedMap.highRisk.pressureSpawns.size(), 4U);
        EXPECT_FLOAT_EQ(publishedMap.highRisk.activationDurationSeconds, 4.0F);
        EXPECT_EQ(publishedMap.highRisk.advancedLootSlots.size(), 2U);
        EXPECT_EQ(publishedMap.highRisk.advancedLootTableId,
                  LootTableDefinitionId{"loot.raid.high_risk_v1"});
        EXPECT_TRUE(mapIds.insert(publishedMap.id).second);
        for (const EnemyDeploymentDefinitionId &deploymentId :
             publishedMap.raidEnemyDeploymentIds)
        {
            EXPECT_TRUE(raidDeploymentIds.insert(deploymentId).second);
        }
    }
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.v0.test"}));
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.raid.riverside"}));
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.raid.industrial"}));
    EXPECT_EQ(raidDeploymentIds.size(), 9U);
    EXPECT_EQ(outboundTravelMinutes,
              (std::set<std::uint32_t>{45U, 90U, 150U}));

    const ItemDefinition &ammunition = registry.item(
        ItemDefinitionId{"item.ammunition.9mm_basic"});
    EXPECT_EQ(ammunition.id, ItemId::Ammo9mm);
    const ItemDefinition &cola = registry.item(alpha_content::lootCola);
    ASSERT_TRUE(cola.baseContribution.has_value());
    EXPECT_EQ(
        *cola.baseContribution,
        (BaseResourceBundle{12, 0, 4, 0}));
    EXPECT_EQ(ammunition.maxStackSize, 60U);
    EXPECT_EQ(ammunition.unitWeightGrams, 12U);
    EXPECT_EQ(ammunition.marketBuyPrice, 1U);

    const ItemDefinition &rifle = registry.item(alpha_content::rifle);
    ASSERT_TRUE(rifle.weaponCondition.has_value());
    EXPECT_EQ(rifle.weaponCondition->maximumDurabilityCenti, 10000U);
    EXPECT_EQ(rifle.weaponCondition->wearPerSuccessfulShotCenti, 10U);
    ASSERT_EQ(rifle.weaponCondition->malfunctionWeights.size(), 1U);
    EXPECT_EQ(rifle.weaponCondition->malfunctionWeights.front().type,
              WeaponMalfunctionType::Stovepipe);
    ASSERT_TRUE(rifle.weaponUse.has_value());
    EXPECT_TRUE(rifle.weaponUse->automaticFire);
    EXPECT_EQ(rifle.weaponUse->recoilControl, 42U);
    EXPECT_EQ(rifle.weaponUse->stability, 66U);
    EXPECT_EQ(rifle.weaponUse->handlingSpeed, 46U);
    EXPECT_EQ(rifle.weaponUse->ergonomics, 100U);
    EXPECT_EQ(rifle.weaponUse->accuracy, 72U);
    EXPECT_EQ(rifle.weaponUse->baseDamage, 4);
    EXPECT_FLOAT_EQ(rifle.weaponUse->effectiveRange, 700.0F);
    EXPECT_FLOAT_EQ(rifle.weaponUse->maximumRange, 950.0F);
    EXPECT_FLOAT_EQ(rifle.weaponUse->logicalBallisticSpeed, 7200.0F);
    EXPECT_TRUE(itemCanEquipInSlot(
        rifle, EquipmentSlotKind::PrimaryWeapon));
    EXPECT_TRUE(itemCanEquipInSlot(
        rifle, EquipmentSlotKind::SecondaryWeapon));
    EXPECT_FALSE(itemCanEquipInSlot(
        rifle, EquipmentSlotKind::Sidearm));

    const ItemDefinition &pistol = registry.item(alpha_content::pistol);
    ASSERT_TRUE(pistol.weaponCondition.has_value());
    ASSERT_TRUE(pistol.weaponUse.has_value());
    EXPECT_FALSE(pistol.weaponUse->automaticFire);
    EXPECT_EQ(pistol.weaponUse->recoilControl, 55U);
    EXPECT_EQ(pistol.weaponUse->handlingSpeed, 82U);
    EXPECT_EQ(pistol.weaponUse->ergonomics, 100U);
    EXPECT_FLOAT_EQ(pistol.weaponUse->effectiveRange, 400.0F);
    EXPECT_FLOAT_EQ(pistol.weaponUse->maximumRange, 620.0F);
    EXPECT_FLOAT_EQ(pistol.weaponUse->logicalBallisticSpeed, 4200.0F);
    EXPECT_LT(
        deriveWeaponHandling(*pistol.weaponUse).switchDurationSeconds,
        deriveWeaponHandling(*rifle.weaponUse).switchDurationSeconds);
    EXPECT_TRUE(itemCanEquipInSlot(
        pistol, EquipmentSlotKind::Sidearm));
    EXPECT_FALSE(itemCanEquipInSlot(
        pistol, EquipmentSlotKind::PrimaryWeapon));
    EXPECT_EQ(
        pistol.compatibleMagazineDefinitionId,
        alpha_content::pistolMagazine);

    const ItemDefinition &pistolMagazine = registry.item(
        alpha_content::pistolMagazine);
    EXPECT_EQ(pistolMagazine.category, ItemCategory::Magazine);
    EXPECT_EQ(pistolMagazine.magazineCapacity, 15U);
    EXPECT_FALSE(pistolMagazine.visualAssetsPublished);

    const ItemDefinition &maintenance = registry.item(
        alpha_content::weaponMaintenanceKit);
    ASSERT_TRUE(maintenance.weaponMaintenance.has_value());
    EXPECT_EQ(maintenance.category, ItemCategory::Maintenance);
    EXPECT_EQ(maintenance.weaponMaintenance->capacityCenti, 2500U);
    EXPECT_EQ(maintenance.weaponMaintenance->raidActionDurationMs, 8000U);

    const ItemDefinition &armorMaintenance = registry.item(
        alpha_content::armorMaintenanceKit);
    ASSERT_TRUE(armorMaintenance.armorMaintenance.has_value());
    EXPECT_FALSE(armorMaintenance.weaponMaintenance.has_value());
    EXPECT_EQ(armorMaintenance.armorMaintenance->capacityCenti, 5000U);
    EXPECT_EQ(
        armorMaintenance.armorMaintenance->raidActionDurationMs, 6000U);
    EXPECT_EQ(
        armorMaintenance.armorMaintenance->softCostPerDurabilityCenti,
        100U);
    EXPECT_EQ(
        armorMaintenance.armorMaintenance->compositeCostPerDurabilityCenti,
        150U);

    const ItemDefinition &painkiller = registry.item(
        alpha_content::painkiller);
    ASSERT_TRUE(painkiller.medicalUse.has_value());
    EXPECT_TRUE(painkiller.medicalUse->slowMovement);

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
    EXPECT_EQ(helmet.armorProtection->material, ArmorMaterial::Composite);

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
    EXPECT_EQ(map.displayName, "Greyline Depot");
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
    ASSERT_EQ(map.ballisticBlockers.size(), 3U);
    EXPECT_EQ(map.ballisticBlockers.front().id, "central_barrier");
    EXPECT_FLOAT_EQ(
        map.ballisticBlockers.front().bounds.position.x,
        570.0F);
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

TEST(ContentRegistryTest, RejectsInvalidBasePriorityDefinitions)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"cycle_minutes\": 7200",
            "\"cycle_minutes\": 0")),
        ContentRegistryError);
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"required_item\": \"item.loot.cola_basic\"",
            "\"required_item\": \"item.loot.unknown\"")),
        ContentRegistryError);
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"resource_reward\": {\"morale\": 12}",
            "\"resource_reward\": {}")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsNonPositiveItemWeight)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"unit_weight_grams\": 400",
        "\"unit_weight_grams\": 0");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidBaseOperationsDefinition)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"strained_below_reserve_days\": 3",
            "\"strained_below_reserve_days\": 1")),
        ContentRegistryError);
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"supported_at_reserve_days\": 7",
            "\"supported_at_reserve_days\": 2")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsExcessiveGunsmithServiceUnitCost)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"maximum_durability_cost_per_point\": 2",
        "\"maximum_durability_cost_per_point\": 1001");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsZeroGunsmithServiceBaseCost)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"base_cost\": 40",
        "\"base_cost\": 0");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
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

TEST(ContentRegistryTest, RejectsInvalidWeaponReliabilityMultiplier)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"reliability_multiplier_basis_points\": 10000",
        "\"reliability_multiplier_basis_points\": 20000");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsDuplicateWeaponEquipmentSlot)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"equipment_slots\": [\"primary_weapon\", \"secondary_weapon\"]",
        "\"equipment_slots\": [\"primary_weapon\", \"primary_weapon\"]");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsWeaponUseOutsideConfiguredBounds)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"accuracy\": 58",
        "\"accuracy\": 101");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsNonPositiveLogicalBallisticSpeed)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"logical_ballistic_speed\": 4200.0",
        "\"logical_ballistic_speed\": 0.0");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsMaintenanceCapacityMismatch)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"capacity_centi\": 2500",
        "\"capacity_centi\": 2400");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnsupportedArmorMaterial)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"material\": \"composite\"",
        "\"material\": \"unknown\"");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsArmorMaintenanceWithInvertedMaximumLoss)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"raid_maximum_loss_basis_points\": 2000",
        "\"raid_maximum_loss_basis_points\": 500");
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

TEST(ContentRegistryTest, RejectsZeroRaidTravelTime)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"outbound_minutes\": 45",
        "\"outbound_minutes\": 0");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsFailureRegroupShorterThanReturnTravel)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"failure_regroup_minutes\": 90",
        "\"failure_regroup_minutes\": 30");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsOutOfRangeMapBackgroundTint)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"background_tint\": {\"red\": 255",
        "\"background_tint\": {\"red\": 256");
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

TEST(ContentRegistryTest, RejectsEnemySpawnInsideBallisticBlocker)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"id\": \"central_barrier\", \"bounds\": {\"position\": {\"x\": 570, \"y\": 390}, \"size\": {\"x\": 140, \"y\": 36}}}",
        "{\"id\": \"central_barrier\", \"bounds\": {\"position\": {\"x\": 650, \"y\": 330}, \"size\": {\"x\": 50, \"y\": 50}}}");
    EXPECT_THROW(
        ContentRegistry::fromJson(invalid),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsHighRiskWaveLargerThanActiveCap)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"active_enemy_cap\": 8",
        "\"active_enemy_cap\": 1");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsHighRiskCapBelowInitialDeployment)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"active_enemy_cap\": 8",
        "\"active_enemy_cap\": 3");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsHighRiskEmergencyExtractionOutsideMap)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"emergency_extraction_point\": {\"position\": {\"x\": 300, \"y\": 300}",
        "\"emergency_extraction_point\": {\"position\": {\"x\": 1260, \"y\": 700}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsConditionalExtractionOutsideMap)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"conditional_extraction_point\": {\"position\": {\"x\": 380, \"y\": 60}",
        "\"conditional_extraction_point\": {\"position\": {\"x\": 1260, \"y\": 700}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsConditionalExtractionOverlappingSignal)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"conditional_extraction_point\": {\"position\": {\"x\": 380, \"y\": 60}",
        "\"conditional_extraction_point\": {\"position\": {\"x\": 300, \"y\": 300}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(
    ContentRegistryTest,
    RejectsHighRiskControlPointInsideBlocker)
{
    const std::string invalid =
        replaceFirst(publishedJsonCopy(),
                     "\"activation_control_point\": {\"position\": {\"x\": "
                     "760, \"y\": 90}",
                     "\"activation_control_point\": {\"position\": {\"x\": "
                     "590, \"y\": 395}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsOverlappingHighRiskInteractionRegions)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"emergency_extraction_point\": {\"position\": {\"x\": 300, \"y\": 300}",
        "\"emergency_extraction_point\": {\"position\": {\"x\": 820, \"y\": 440}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsNormalExtractionOverlappingHighRiskRegion)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"id\": \"west_to_east\", \"player_spawn\": {\"x\": 80, \"y\": 330}, \"extraction_point\": {\"position\": {\"x\": 1080, \"y\": 260}",
        "{\"id\": \"west_to_east\", \"player_spawn\": {\"x\": 80, \"y\": 330}, \"extraction_point\": {\"position\": {\"x\": 820, \"y\": 440}");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(
    ContentRegistryTest,
    RejectsHighRiskAdvancedLootOutsideResourceArea)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"id\": \"depot_cache_1\", \"position\": {\"x\": 860, \"y\": 480}}",
        "{\"id\": \"depot_cache_1\", \"position\": {\"x\": 200, \"y\": 200}}");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
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
