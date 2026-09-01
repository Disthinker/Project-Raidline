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
        "home-region-placeable-storage-content-58");
    const MapDefinition &frontierEnemyPopulation = registry.map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    EXPECT_EQ(
        frontierEnemyPopulation.proceduralOutdoor.minimumInitialEnemies,
        36U);
    EXPECT_EQ(
        frontierEnemyPopulation.proceduralOutdoor.maximumInitialEnemies,
        48U);
    EXPECT_FLOAT_EQ(
        frontierEnemyPopulation.proceduralOutdoor.minimumEnemySpawnDistance,
        1200.0F);
    ASSERT_EQ(registry.regionalOperations().baseSites.size(), 2U);
    EXPECT_EQ(registry.regionalOperations().maximumEstablishedOutposts, 2U);
    const RegionalBaseSiteDefinition &ashworks = registry.regionalBaseSite(
        RegionalBaseSiteDefinitionId{
            "regional_base_site.ashworks_logistics_yard"});
    EXPECT_EQ(ashworks.tier, RegionalBaseSiteTier::Mature);
    EXPECT_FALSE(ashworks.uniqueFeatureInitiallyRepaired);
    EXPECT_EQ(ashworks.uniqueFeatureRepairMaterialUnits, 15U);
    EXPECT_EQ(ashworks.uniqueFeatureRepairMinutes, 360U);
    EXPECT_EQ(ashworks.uniqueFeatureManufacturingDurationPercent, 75U);
    EXPECT_EQ(ashworks.dailyBaseThreatUnits, 3U);
    EXPECT_EQ(
        ashworks.perimeterSweepMapDefinitionId,
        MapDefinitionId{"map.raid.frontier_exchange"});
    EXPECT_TRUE(registry.map(ashworks.perimeterSweepMapDefinitionId)
                    .proceduralOutdoor.enabled);
    EXPECT_EQ(registry.map(ashworks.perimeterSweepMapDefinitionId)
                  .proceduralOutdoor.layoutVersion,
              4U);
    EXPECT_EQ(ashworks.perimeterSweepThreatReductionUnits, 40U);
    EXPECT_FALSE(ashworks.initiallyUnlocked);
    EXPECT_EQ(ashworks.migrationMinutes, 720U);
    EXPECT_EQ(ashworks.coreFacilitySlots, 4U);
    EXPECT_EQ(
        ashworks.clearanceMapDefinitionId,
        MapDefinitionId{"map.raid.industrial"});
    EXPECT_EQ(
        ashworks.outpostDefinitionId,
        RegionalOutpostDefinitionId{
            "regional_outpost.ashworks_logistics_yard"});
    const RegionalOutpostDefinition &relay = registry.regionalOutpost(
        RegionalOutpostDefinitionId{
            "regional_outpost.old_service_relay"});
    EXPECT_EQ(relay.safeShortcutOperations, 3U);
    EXPECT_EQ(
        relay.restorationMapDefinitionId,
        MapDefinitionId{"map.raid.riverside"});
    EXPECT_EQ(registry.map(MapDefinitionId{"map.v0.test"})
                  .recovery.serviceFee, 60U);
    EXPECT_EQ(registry.map(MapDefinitionId{"map.v0.test"})
                  .recovery.durationMinutes, 360U);
    EXPECT_EQ(registry.baseMorale().recoveryDaysFromLow, 2U);
    EXPECT_EQ(registry.baseMorale().lowManufacturingDurationPercent, 120U);
    EXPECT_EQ(registry.baseMorale().stableManufacturingDurationPercent, 100U);
    EXPECT_EQ(registry.baseMorale().highManufacturingDurationPercent, 90U);
    EXPECT_EQ(registry.baseMorale().eventCycleDays, 5U);
    ASSERT_EQ(registry.baseCommunityEvents().size(), 4U);
    EXPECT_EQ(
        registry.baseCommunityEvent(
            BaseCommunityEventDefinitionId{"base_event.shared_meal"})
            .moraleEffect,
        1);
    EXPECT_EQ(registry.gunsmithFullMaintenance().baseCost, 40U);
    EXPECT_EQ(
        registry.gunsmithFullMaintenance().currentDurabilityCostPerPoint,
        1U);
    EXPECT_EQ(
        registry.gunsmithFullMaintenance().maximumDurabilityCostPerPoint,
        2U);
    EXPECT_EQ(
        registry.playerBaseMedical().missingHealthCostPerPoint,
        3U);
    EXPECT_EQ(registry.playerBaseMedical().lightBleedingCost, 30U);
    EXPECT_EQ(registry.playerBaseMedical().heavyBleedingCost, 60U);
    EXPECT_EQ(registry.residentMedical().requiredContribution, 10U);
    EXPECT_EQ(registry.residentMedical().durationMinutes, 360U);
    EXPECT_EQ(registry.baseOperations().strainedBelowReserveDays, 3U);
    EXPECT_EQ(registry.baseOperations().supportedAtReserveDays, 7U);
    EXPECT_EQ(registry.basePriorityCycleMinutes(), 7200U);
    EXPECT_EQ(registry.maximumBaseConstructionMaterials(), 100U);
    ASSERT_EQ(registry.baseConstructionProjects().size(), 4U);
    EXPECT_EQ(registry.baseFacilities().size(), 5U);
    EXPECT_TRUE(registry.baseFacility(BaseFacilityDefinitionId{
        "base_facility.kitchen_water"}).requiredForMigration);
    EXPECT_FALSE(registry.baseFacility(BaseFacilityDefinitionId{
        "base_facility.kitchen_water"}).initiallyOwned);
    EXPECT_FALSE(registry.baseFacility(BaseFacilityDefinitionId{
        "base_facility.workshop"}).requiredForMigration);
    const BaseConstructionProjectDefinition &dormitory =
        registry.baseConstructionProject(
            BaseConstructionProjectDefinitionId{
                "base_construction.dormitory.level_2"});
    EXPECT_EQ(dormitory.target, BaseFacilityUpgradeTarget::Dormitory);
    EXPECT_EQ(dormitory.requiredLevel, 1U);
    EXPECT_EQ(dormitory.targetLevel, 2U);
    EXPECT_EQ(dormitory.materialCost, 4U);
    EXPECT_EQ(dormitory.workerCount, 3U);
    EXPECT_EQ(dormitory.durationMinutes, 360U);
    EXPECT_EQ(dormitory.bedCapacityAfter, 14U);
    EXPECT_EQ(registry.baseWorkforce().generalFallbackDurationPercent, 150U);
    EXPECT_EQ(registry.baseWorkforce().workshopLevel2DurationPercent, 85U);
    EXPECT_EQ(registry.baseWorkforce().medicalLevel2DurationPercent, 85U);
    ASSERT_EQ(registry.baseManufacturingRecipes().size(), 4U);
    const BaseManufacturingRecipeDefinition &manufacturing =
        registry.baseManufacturingRecipe(
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.weapon_maintenance_kit"});
    EXPECT_EQ(manufacturing.inputs.size(), 2U);
    EXPECT_EQ(
        manufacturing.outputItemDefinitionId,
        ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    EXPECT_EQ(manufacturing.workerCount, 1U);
    EXPECT_EQ(manufacturing.durationMinutes, 360U);
    const BaseManufacturingRecipeDefinition &rifleAmmunition =
        registry.baseManufacturingRecipe(
            BaseManufacturingRecipeDefinitionId{
                "base_manufacturing.ammunition_5_45x39_standard"});
    EXPECT_EQ(
        rifleAmmunition.outputItemDefinitionId,
        ItemDefinitionId{"item.ammunition.5_45x39_standard"});
    EXPECT_EQ(rifleAmmunition.outputQuantity, 60U);
    EXPECT_EQ(rifleAmmunition.durationMinutes, 180U);
    ASSERT_EQ(registry.basePriorities().size(), 3U);
    const BasePriorityDefinition &comfort = registry.basePriority(
        BasePriorityDefinitionId{"base_priority.comfort_cola"});
    EXPECT_EQ(comfort.requiredItemDefinitionId, alpha_content::lootCola);
    EXPECT_EQ(comfort.requiredQuantity, 1U);
    EXPECT_EQ(comfort.resourceReward, (BaseResourceBundle{0, 0, 12, 0}));
    ASSERT_EQ(registry.items().size(), 51U);
    ASSERT_EQ(registry.calibers().size(), 3U);
    EXPECT_EQ(
        registry.caliber(CaliberDefinitionId{"caliber.5_45x39"})
            .displayName,
        "5.45x39mm");
    EXPECT_TRUE(registry.ammunitionFitsMagazine(
        ItemDefinitionId{"item.ammunition.5_45x39_standard"},
        ItemDefinitionId{"item.magazine.5_45x39_30"}));
    EXPECT_TRUE(registry.ammunitionFitsMagazine(
        ItemDefinitionId{"item.ammunition.5_45x39_enhanced"},
        ItemDefinitionId{"item.magazine.5_45x39_30"}));
    EXPECT_FALSE(registry.ammunitionFitsMagazine(
        ItemDefinitionId{"item.ammunition.7_62x51_standard"},
        ItemDefinitionId{"item.magazine.5_45x39_30"}));
    EXPECT_TRUE(registry.magazineFitsWeapon(
        ItemDefinitionId{"item.magazine.7_62x51_20"},
        ItemDefinitionId{"item.weapon.dmr_7_62x51_service"}));
    EXPECT_FALSE(registry.magazineFitsWeapon(
        ItemDefinitionId{"item.magazine.7_62x51_100_box"},
        ItemDefinitionId{"item.weapon.dmr_7_62x51_service"}));
    const MapDefinition &frontierWithInterior = registry.map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_EQ(frontierWithInterior.interiors.size(), 2U);
    EXPECT_EQ(
        frontierWithInterior.interiors.front().id,
        RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"});
    EXPECT_EQ(
        frontierWithInterior.interiors.front().intelligencePrice,
        180U);
    EXPECT_EQ(
        &registry.raidInterior(
            RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"}),
        &frontierWithInterior.interiors.front());
    EXPECT_EQ(frontierWithInterior.interiors.front().enemies.size(), 2U);
    EXPECT_EQ(frontierWithInterior.interiors.front().lootSlots.size(), 3U);
    ASSERT_EQ(
        frontierWithInterior.interiors.front().exteriorPlacements.size(),
        6U);
    EXPECT_EQ(
        frontierWithInterior.interiors.front().exteriorPlacements.front()
            .entrance,
        frontierWithInterior.interiors.front().exteriorEntrance);
    EXPECT_FLOAT_EQ(
        frontierWithInterior.interiors.front().exteriorPlacements.front()
            .returnPoint.x,
        frontierWithInterior.interiors.front().exteriorReturn.x);
    EXPECT_FLOAT_EQ(
        frontierWithInterior.interiors.front().exteriorPlacements.front()
            .returnPoint.y,
        frontierWithInterior.interiors.front().exteriorReturn.y);
    const RaidInteriorDefinition &freightBay =
        frontierWithInterior.interiors[1];
    EXPECT_EQ(
        freightBay.id,
        RaidSpaceDefinitionId{
            "raid_space.frontier_exchange.freight_service_bay"});
    EXPECT_EQ(freightBay.displayName, "Freight Service Bay");
    EXPECT_EQ(freightBay.intelligencePrice, 220U);
    EXPECT_EQ(freightBay.worldSize.x, 1120.0F);
    EXPECT_EQ(freightBay.worldSize.y, 520.0F);
    EXPECT_EQ(freightBay.enemies.size(), 3U);
    EXPECT_EQ(freightBay.lootSlots.size(), 4U);
    EXPECT_EQ(freightBay.exteriorPlacements.size(), 6U);
    EXPECT_EQ(&registry.raidInterior(freightBay.id), &freightBay);
    EXPECT_NE(
        freightBay.worldSize.x,
        frontierWithInterior.interiors.front().worldSize.x);
    ASSERT_EQ(registry.lootTables().size(), 15U);
    const auto lootItemIds = [&](std::string_view tableId)
    {
        std::set<ItemDefinitionId> ids;
        for (const LootContentEntry &entry : registry.lootTable(
                 LootTableDefinitionId{std::string{tableId}}).entries)
        {
            ids.insert(entry.itemDefinitionId);
        }
        return ids;
    };
    const std::set<ItemDefinitionId> maintenanceLoot =
        lootItemIds("loot.frontier.maintenance_cache_v1");
    const std::set<ItemDefinitionId> roadsideLoot =
        lootItemIds("loot.frontier.roadside_salvage_v1");
    const std::set<ItemDefinitionId> securedLoot =
        lootItemIds("loot.frontier.secured_cargo_v1");
    const std::set<ItemDefinitionId> serviceLoot =
        lootItemIds("loot.frontier.service_supplies_v1");
    EXPECT_NE(maintenanceLoot, roadsideLoot);
    EXPECT_NE(securedLoot, serviceLoot);
    EXPECT_TRUE(maintenanceLoot.contains(
        ItemDefinitionId{"item.loot.industrial_fasteners"}));
    EXPECT_TRUE(roadsideLoot.contains(
        ItemDefinitionId{"item.loot.sealed_water"}));
    EXPECT_TRUE(securedLoot.contains(
        ItemDefinitionId{"item.loot.precision_components"}));
    EXPECT_TRUE(serviceLoot.contains(
        ItemDefinitionId{"item.loot.first_aid_stock"}));
    const std::set<ItemDefinitionId> serviceCrisisLoot =
        lootItemIds("loot.frontier.crisis.service_convoy_v1");
    const std::set<ItemDefinitionId> industrialCrisisLoot =
        lootItemIds("loot.frontier.crisis.industrial_breach_v1");
    const std::set<ItemDefinitionId> freightCrisisLoot =
        lootItemIds("loot.frontier.crisis.freight_lockdown_v1");
    EXPECT_TRUE(serviceCrisisLoot.contains(
        ItemDefinitionId{"item.ammunition.9mm_enhanced"}));
    EXPECT_TRUE(industrialCrisisLoot.contains(
        ItemDefinitionId{"item.ammunition.5_45x39_enhanced"}));
    EXPECT_TRUE(freightCrisisLoot.contains(
        ItemDefinitionId{"item.ammunition.7_62x51_enhanced"}));
    EXPECT_FALSE(serviceCrisisLoot.contains(
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}));
    EXPECT_FALSE(industrialCrisisLoot.contains(
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}));
    EXPECT_TRUE(freightCrisisLoot.contains(
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}));
    ASSERT_EQ(registry.enemyDeployments().size(), 13U);
    ASSERT_EQ(registry.maps().size(), 4U);

    std::set<MapDefinitionId> mapIds;
    std::set<EnemyDeploymentDefinitionId> raidDeploymentIds;
    std::set<std::uint32_t> outboundTravelMinutes;
    for (const MapDefinition &publishedMap : registry.maps())
    {
        EXPECT_FALSE(publishedMap.displayName.empty());
        EXPECT_FALSE(publishedMap.routeProfile.empty());
        EXPECT_FALSE(publishedMap.operationBriefing.difficulty.empty());
        EXPECT_FALSE(publishedMap.operationBriefing.warning.empty());
        EXPECT_GT(publishedMap.operationBriefing.price(
            RaidIntelligenceCategory::Transport), 0U);
        EXPECT_GT(publishedMap.operationBriefing.price(
            RaidIntelligenceCategory::Resource), 0U);
        EXPECT_GT(publishedMap.operationBriefing.price(
            RaidIntelligenceCategory::Enemy), 0U);
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
            publishedMap.id ==
                    MapDefinitionId{"map.raid.frontier_exchange"}
                ? 1200.0F
                : 180.0F);
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
        EXPECT_EQ(
            publishedMap.highRisk.activeEnemyCap,
            publishedMap.id ==
                    MapDefinitionId{"map.raid.frontier_exchange"}
                ? 48U
                : 8U);
        EXPECT_EQ(publishedMap.highRisk.pressureSpawns.size(), 4U);
        EXPECT_FLOAT_EQ(publishedMap.highRisk.activationDurationSeconds, 4.0F);
        EXPECT_EQ(publishedMap.highRisk.advancedLootSlots.size(), 2U);
        EXPECT_EQ(publishedMap.highRisk.advancedLootTableId,
                  LootTableDefinitionId{"loot.raid.high_risk_v1"});
        ASSERT_TRUE(publishedMap.rescue.has_value());
        EXPECT_EQ(
            publishedMap.rescue->subjectKind,
            RaidRescueSubjectKind::OrdinaryResidents);
        EXPECT_EQ(publishedMap.rescue->ordinaryResidentCount, 1U);
        EXPECT_FLOAT_EQ(
            publishedMap.rescue->interactionDurationSeconds,
            2.0F);
        EXPECT_TRUE(mapIds.insert(publishedMap.id).second);
        for (const EnemyDeploymentDefinitionId &deploymentId :
             publishedMap.raidEnemyDeploymentIds)
        {
            raidDeploymentIds.insert(deploymentId);
        }
    }
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.v0.test"}));
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.raid.riverside"}));
    EXPECT_TRUE(mapIds.contains(MapDefinitionId{"map.raid.industrial"}));
    EXPECT_TRUE(mapIds.contains(
        MapDefinitionId{"map.raid.frontier_exchange"}));
    const MapDefinition &frontier = registry.map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    EXPECT_TRUE(frontier.proceduralOutdoor.enabled);
    EXPECT_EQ(frontier.proceduralOutdoor.layoutVersion, 4U);
    EXPECT_EQ(frontier.proceduralOutdoor.columns, 320U);
    EXPECT_EQ(frontier.proceduralOutdoor.rows, 180U);
    EXPECT_EQ(frontier.proceduralOutdoor.districtColumns, 40U);
    EXPECT_EQ(frontier.proceduralOutdoor.districtRows, 20U);
    EXPECT_EQ(frontier.proceduralOutdoor.chunkSizeCells, 16U);
    EXPECT_EQ(frontier.proceduralOutdoor.minimumBranchRoads, 6U);
    EXPECT_EQ(frontier.proceduralOutdoor.maximumBranchRoads, 10U);
    EXPECT_EQ(frontier.proceduralOutdoor.minimumBlockers, 700U);
    EXPECT_EQ(frontier.proceduralOutdoor.maximumBlockers, 1100U);
    EXPECT_EQ(frontier.proceduralOutdoor.minimumDecorativeProps, 1200U);
    EXPECT_EQ(frontier.proceduralOutdoor.maximumDecorativeProps, 1800U);
    EXPECT_EQ(frontier.proceduralOutdoor.minimumRoadObstacles, 140U);
    EXPECT_EQ(frontier.proceduralOutdoor.maximumRoadObstacles, 220U);
    EXPECT_EQ(frontier.proceduralOutdoor.minimumPuddlePatches, 60U);
    EXPECT_EQ(frontier.proceduralOutdoor.maximumPuddlePatches, 100U);
    EXPECT_EQ(frontier.proceduralOutdoor.districtArchetypes.size(), 6U);
    EXPECT_EQ(frontier.proceduralOutdoor.landmarkTemplates.size(), 3U);
    ASSERT_EQ(frontier.proceduralOutdoor.resourcePointArchetypes.size(), 6U);
    ASSERT_EQ(frontier.highRisk.crises.size(), 3U);
    EXPECT_EQ(frontier.highRisk.crises[0].id,
              "crisis.frontier.road_convergence");
    EXPECT_EQ(frontier.highRisk.crises[0].pressureSpawnCount, 3U);
    EXPECT_EQ(frontier.highRisk.crises[1].id,
              "crisis.frontier.industrial_breach");
    EXPECT_EQ(frontier.highRisk.crises[1].waveSize, 4U);
    EXPECT_EQ(frontier.highRisk.crises[2].id,
              "crisis.frontier.freight_lockdown");
    EXPECT_EQ(
        frontier.highRisk.crises[2].advancedLootTableId,
        LootTableDefinitionId{
            "loot.frontier.crisis.freight_lockdown_v1"});
    const auto &securedCargo =
        frontier.proceduralOutdoor.resourcePointArchetypes[2];
    EXPECT_EQ(securedCargo.kind, RaidResourcePointKind::HighValue);
    EXPECT_EQ(securedCargo.minimumInstances, 3U);
    EXPECT_EQ(securedCargo.maximumInstances, 4U);
    EXPECT_EQ(securedCargo.capacity, 3U);
    EXPECT_EQ(securedCargo.riskTier, 3U);
    EXPECT_EQ(
        securedCargo.lootTableId,
        LootTableDefinitionId{"loot.frontier.secured_cargo_v1"});
    EXPECT_EQ(frontier.worldSize.x, 25600.0F);
    EXPECT_EQ(frontier.worldSize.y, 14400.0F);
    EXPECT_EQ(
        registry.map(MapDefinitionId{"map.raid.industrial"})
            .rescue->injuredResidentCount,
        1U);
    EXPECT_EQ(
        registry.map(MapDefinitionId{"map.v0.test"})
            .rescue->injuredResidentCount,
        0U);
    EXPECT_EQ(raidDeploymentIds.size(), 12U);
    EXPECT_EQ(outboundTravelMinutes,
              (std::set<std::uint32_t>{45U, 90U, 150U, 210U}));

    const ItemDefinition &ammunition = registry.item(
        ItemDefinitionId{"item.ammunition.9mm_basic"});
    EXPECT_EQ(ammunition.id, ItemId::Ammo9mm);
    const ItemDefinition &cola = registry.item(alpha_content::lootCola);
    ASSERT_TRUE(cola.baseContribution.has_value());
    EXPECT_EQ(
        *cola.baseContribution,
        (BaseResourceBundle{12, 0, 4, 0}));
    EXPECT_EQ(cola.baseConstructionMaterialValue, 0U);
    EXPECT_EQ(
        *registry.item(ItemDefinitionId{"item.loot.book_basic"})
             .baseContribution,
        (BaseResourceBundle{0, 0, 10, 0}));
    EXPECT_EQ(
        *registry.item(ItemDefinitionId{"item.loot.toilet_paper"})
             .baseContribution,
        (BaseResourceBundle{0, 3, 0, 0}));
    EXPECT_LT(
        registry.item(ItemDefinitionId{"item.loot.toilet_paper"})
            .baseContribution->hygiene,
        registry.item(alpha_content::medkit)
            .baseContribution->hygiene);
    EXPECT_EQ(
        registry.item(ItemDefinitionId{"item.loot.scrap_parts"})
            .baseConstructionMaterialValue,
        4U);
    EXPECT_EQ(
        registry.item(ItemDefinitionId{"item.loot.electronics"})
            .baseConstructionMaterialValue,
        4U);
    EXPECT_EQ(
        *registry.item(ItemDefinitionId{"item.loot.sealed_water"})
             .baseContribution,
        (BaseResourceBundle{10, 2, 0, 0}));
    EXPECT_EQ(
        *registry.item(ItemDefinitionId{"item.loot.compact_game_set"})
             .baseContribution,
        (BaseResourceBundle{0, 0, 18, 0}));
    EXPECT_EQ(
        registry.item(ItemDefinitionId{"item.loot.machine_tool_set"})
            .baseConstructionMaterialValue,
        5U);
    EXPECT_EQ(
        registry.item(ItemDefinitionId{"item.loot.precision_components"})
            .marketRecyclePrice,
        80U);
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
    EXPECT_EQ(
        map.raidLootTableId.value(),
        "loot.raid.greyline_supply_v1");
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

TEST(ContentRegistryTest, BaseStorageCratePublishesPlaceableContainerContract)
{
    const ContentRegistry &registry = publishedContentRegistry();
    const ItemDefinitionId id{"item.container.base_storage_crate"};
    const ItemDefinition &crate = registry.item(id);

    EXPECT_EQ(crate.category, ItemCategory::Container);
    EXPECT_FALSE(crate.equipmentSlot.has_value());
    ASSERT_EQ(crate.containerCompartments.size(), 1U);
    EXPECT_EQ(crate.containerCompartments.front().width, 10U);
    EXPECT_EQ(crate.containerCompartments.front().height, 8U);
    ASSERT_TRUE(crate.basePlacement.has_value());
    EXPECT_FLOAT_EQ(crate.basePlacement->footprint.x, 160.0F);
    EXPECT_FLOAT_EQ(crate.basePlacement->footprint.y, 112.0F);
    EXPECT_TRUE(crate.basePlacement->parcelOnly);
    EXPECT_TRUE(crate.basePlacement->blocksMovement);
    EXPECT_EQ(crate.marketBuyPrice, 160U);
    EXPECT_EQ(crate.marketRecyclePrice, 40U);
    EXPECT_NE(
        std::find(
            registry.fixedSupplyItemIds().begin(),
            registry.fixedSupplyItemIds().end(),
            id),
        registry.fixedSupplyItemIds().end());
}

TEST(ContentRegistryTest, BasePlacementRejectsInvalidCapabilities)
{
    const std::string conflictingOwnership = replaceFirst(
        publishedJsonCopy(),
        "\"base_placement\": {",
        "\"equipment_slot\": \"backpack\",\n      \"base_placement\": {");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(conflictingOwnership)),
        ContentRegistryError);

    const std::string badFootprint = replaceFirst(
        publishedJsonCopy(),
        "\"footprint\": {\"x\": 160, \"y\": 112}",
        "\"footprint\": {\"x\": 16, \"y\": 112}");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(badFootprint)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidRegionalBaseThreatRate)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"daily_base_threat_units\": 1",
        "\"daily_base_threat_units\": 0");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
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

TEST(ContentRegistryTest, RejectsReservedOutdoorInteriorIdentity)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "raid_space.frontier_exchange.office",
        "raid_space.outdoor");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        std::runtime_error);
}

TEST(ContentRegistryTest, RejectsUnsafeProceduralOutdoorBounds)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"minimum_blockers\": 700,",
        "\"minimum_blockers\": 1200,");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidMapOperationBriefing)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            R"("transport_price": 24)",
            R"("transport_price": 0)"))),
        std::runtime_error);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            R"("difficulty": "LOW")",
            R"("difficulty": "")"))),
        std::runtime_error);
}

TEST(ContentRegistryTest, RejectsInvalidBaseConstructionDefinition)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"material_cost\": 4",
            "\"material_cost\": 101"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"target_level\": 2",
            "\"target_level\": 3"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"base_construction_material\": 4",
            "\"base_construction_material\": 101"))),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInsufficientMigrationFacilitySlots)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"core_facility_slots\": 4",
            "\"core_facility_slots\": 3"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"required_for_migration\": false",
            "\"required_for_migration\": true"))),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsBaseSiteFeatureWithoutRepairConsumer)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"unique_feature_manufacturing_duration_percent\": 75",
        "\"unique_feature_manufacturing_duration_percent\": 100");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        std::runtime_error);
}

TEST(ContentRegistryTest, RejectsInvalidBaseManufacturingDefinition)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"worker_count\": 1",
            "\"worker_count\": 0"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"item\": \"item.loot.electronics\", \"quantity\": 1",
            "\"item\": \"item.loot.unknown\", \"quantity\": 1"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"id\": \"base_manufacturing.weapon_maintenance_kit\"",
            "\"id\": \"item.invalid_recipe\""))),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidBaseMoraleAndCommunityEvents)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"stable\": 100",
            "\"stable\": 99"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"event_cycle_days\": 5",
            "\"event_cycle_days\": 31"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"display_name\": \"Shared Meal\"",
            "\"display_name\": \"\""))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"morale_effect\": 1",
            "\"morale_effect\": 2"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"id\": \"base_event.restless_nights\"",
            "\"id\": \"base_event.shared_meal\""))),
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

TEST(ContentRegistryTest, RejectsInvalidPlayerBaseMedicalPricing)
{
    for (const std::pair<std::string_view, std::string_view> replacement : {
             std::pair{
                 std::string_view{"\"missing_health_cost_per_point\": 3"},
                 std::string_view{"\"missing_health_cost_per_point\": 0"}},
             std::pair{
                 std::string_view{"\"light_bleeding_cost\": 30"},
                 std::string_view{"\"light_bleeding_cost\": 10001"}},
             std::pair{
                 std::string_view{"\"heavy_bleeding_cost\": 60"},
                 std::string_view{"\"heavy_bleeding_cost\": 10"}}})
    {
        const std::string invalid = replaceFirst(
            publishedJsonCopy(), replacement.first, replacement.second);
        EXPECT_THROW(
            ContentRegistry::fromJson(invalid),
            ContentRegistryError);
    }
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

TEST(ContentRegistryTest, RejectsInvalidOutpostDisruptionThreshold)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"safe_shortcut_operations\": 3",
            "\"safe_shortcut_operations\": 0")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnknownOutpostRestorationMap)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"restoration_map_definition_id\": \"map.raid.riverside\"",
            "\"restoration_map_definition_id\": \"map.raid.missing\"")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnknownBaseSiteClearanceMap)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"clearance_map_definition_id\": \"map.raid.industrial\"",
            "\"clearance_map_definition_id\": \"map.raid.missing\"")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnknownBasePerimeterSweepMap)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"perimeter_sweep_map_definition_id\": \"map.v0.test\"",
            "\"perimeter_sweep_map_definition_id\": \"map.raid.missing\"")),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidProceduralResourcePointContracts)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "resource_point.frontier.secured_cargo",
            "resource_point.frontier.maintenance_cache"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"landmark_definition_id\": \"landmark.frontier.freight_exchange\"",
            "\"landmark_definition_id\": \"landmark.frontier.unknown\""))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"minimum_instances\": 5, \"maximum_instances\": 6",
            "\"minimum_instances\": 7, \"maximum_instances\": 6"))),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidProceduralInitialEnemyRange)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"minimum_initial_enemies\": 36",
        "\"minimum_initial_enemies\": 64");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsUnsafeEnemySpawnExclusionDistance)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"minimum_enemy_spawn_distance\": 1200",
        "\"minimum_enemy_spawn_distance\": 200");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsInvalidProceduralEncounterContracts)
{
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"minimum_groups\": 3, \"maximum_groups\": 4",
            "\"minimum_groups\": 5, \"maximum_groups\": 4"))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"kind\": \"ambush\"",
            "\"kind\": \"roaming_horde\""))),
        ContentRegistryError);
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"activation_distance\": 220",
            "\"activation_distance\": 0"))),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsIneffectiveBasePerimeterSweepReduction)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"perimeter_sweep_threat_reduction_units\": 40",
            "\"perimeter_sweep_threat_reduction_units\": 20")),
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

TEST(ContentRegistryTest, RejectsInvalidMapRecoveryQuote)
{
    EXPECT_THROW(
        ContentRegistry::fromJson(replaceFirst(
            publishedJsonCopy(),
            "\"recovery\": {\"service_fee\": 60, \"duration_minutes\": 360}",
            "\"recovery\": {\"service_fee\": 0, \"duration_minutes\": 360}")),
        std::runtime_error);
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

TEST(ContentRegistryTest, RejectsUnsupportedRescueSubjectKind)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"subject_kind\": \"ordinary_residents\"",
        "\"subject_kind\": \"named_npc\"");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsDuplicateRescueDefinitionId)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "rescue.ordinary.riverside_checkpoint",
        "rescue.ordinary.greyline_depot");

    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(invalid)),
                 ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsRescuePointOverlappingHighRiskRegion)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"transfer_point\": {\"position\": {\"x\": 40, \"y\": 290}, \"size\": {\"x\": 200, \"y\": 120}}",
        "\"transfer_point\": {\"position\": {\"x\": 300, \"y\": 300}, \"size\": {\"x\": 170, \"y\": 130}}");

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

TEST(ContentRegistryTest, RejectsDuplicateHighRiskCrisisIdentity)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "crisis.frontier.industrial_breach",
        "crisis.frontier.road_convergence");

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

TEST(ContentRegistryTest, RejectsDuplicateRaidExteriorPlacementId)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"id\": \"north_exchange\"",
        "\"id\": \"east_gate\"");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsOverlappingPortalsAcrossInteriorDefinitions)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"id\": \"northwest_service\", \"entrance\": {\"position\": {\"x\": 180, \"y\": 280}, \"size\": {\"x\": 120, \"y\": 70}}",
        "{\"id\": \"northwest_service\", \"entrance\": {\"position\": {\"x\": 2220, \"y\": 380}, \"size\": {\"x\": 120, \"y\": 80}}");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, RejectsNonPositiveInteriorIntelligencePrice)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"intelligence_price\": 180",
        "\"intelligence_price\": 0");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
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

TEST(ContentRegistryTest, ContentBetaWeaponsExposeStableCaliberAndTierContracts)
{
    const ContentRegistry &registry = publishedContentRegistry();
    const std::array<ItemDefinitionId, 4> newWeapons{
        ItemDefinitionId{"item.weapon.carbine_5_45_compact"},
        ItemDefinitionId{"item.weapon.rifle_5_45_service"},
        ItemDefinitionId{"item.weapon.dmr_7_62x51_service"},
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"}};
    for (const ItemDefinitionId &weaponId : newWeapons)
    {
        const ItemDefinition &weapon = registry.item(weaponId);
        EXPECT_EQ(weapon.category, ItemCategory::Weapon);
        EXPECT_TRUE(weapon.weaponUse.has_value());
        EXPECT_TRUE(weapon.weaponCondition.has_value());
        EXPECT_FALSE(weapon.compatibleMagazineDefinitionIds.empty());
        EXPECT_FALSE(weapon.visualAssetsPublished);
    }

    const ItemDefinition &standard = registry.item(
        ItemDefinitionId{"item.ammunition.7_62x51_standard"});
    const ItemDefinition &enhanced = registry.item(
        ItemDefinitionId{"item.ammunition.7_62x51_enhanced"});
    ASSERT_TRUE(standard.ammunitionUse.has_value());
    ASSERT_TRUE(enhanced.ammunitionUse.has_value());
    EXPECT_EQ(standard.ammunitionUse->tier, AmmunitionTier::Standard);
    EXPECT_EQ(enhanced.ammunitionUse->tier, AmmunitionTier::Enhanced);
    EXPECT_EQ(
        standard.ammunitionUse->caliberDefinitionId,
        enhanced.ammunitionUse->caliberDefinitionId);
    EXPECT_GT(
        enhanced.ammunitionUse->penetration,
        standard.ammunitionUse->penetration);
}

TEST(ContentRegistryTest, ContentBetaLoadoutGearDefinesThreeTradeoffTiers)
{
    const ContentRegistry &registry = publishedContentRegistry();
    const ItemDefinition &scoutHelmet = registry.item(
        ItemDefinitionId{"item.protective_gear.helmet_scout"});
    const ItemDefinition &basicHelmet = registry.item(alpha_content::helmet);
    const ItemDefinition &heavyHelmet = registry.item(
        ItemDefinitionId{"item.protective_gear.helmet_heavy"});
    ASSERT_TRUE(scoutHelmet.armorProtection.has_value());
    ASSERT_TRUE(basicHelmet.armorProtection.has_value());
    ASSERT_TRUE(heavyHelmet.armorProtection.has_value());
    EXPECT_LT(
        scoutHelmet.armorProtection->protectionRequirement,
        basicHelmet.armorProtection->protectionRequirement);
    EXPECT_LT(
        basicHelmet.armorProtection->protectionRequirement,
        heavyHelmet.armorProtection->protectionRequirement);
    EXPECT_LT(scoutHelmet.unitWeightGrams, basicHelmet.unitWeightGrams);
    EXPECT_LT(basicHelmet.unitWeightGrams, heavyHelmet.unitWeightGrams);
    EXPECT_EQ(heavyHelmet.marketBuyPrice, 0U);
    EXPECT_EQ(
        heavyHelmet.armorProtection->material,
        ArmorMaterial::Metal);

    const ItemDefinition &smallRig = registry.item(alpha_content::chestRig);
    const ItemDefinition &patrolRig = registry.item(
        ItemDefinitionId{"item.container.chest_rig_patrol"});
    const ItemDefinition &assaultRig = registry.item(
        ItemDefinitionId{"item.container.chest_rig_assault"});
    const auto capacity = [](const ItemDefinition &definition)
    {
        std::uint32_t total{};
        for (const ContainerCompartmentDefinition &compartment :
             definition.containerCompartments)
        {
            total += static_cast<std::uint32_t>(
                compartment.width * compartment.height);
        }
        return total;
    };
    EXPECT_LT(capacity(smallRig), capacity(patrolRig));
    EXPECT_LT(capacity(patrolRig), capacity(assaultRig));
    EXPECT_LT(smallRig.unitWeightGrams, patrolRig.unitWeightGrams);
    EXPECT_LT(patrolRig.unitWeightGrams, assaultRig.unitWeightGrams);

    const ItemDefinition &smallBackpack = registry.item(alpha_content::backpack);
    const ItemDefinition &fieldBackpack = registry.item(
        ItemDefinitionId{"item.container.backpack_field"});
    const ItemDefinition &expeditionBackpack = registry.item(
        ItemDefinitionId{"item.container.backpack_expedition"});
    EXPECT_EQ(smallBackpack.containerCompartments.size(), 1U);
    EXPECT_EQ(fieldBackpack.containerCompartments.size(), 1U);
    EXPECT_EQ(expeditionBackpack.containerCompartments.size(), 1U);
    EXPECT_LT(capacity(smallBackpack), capacity(fieldBackpack));
    EXPECT_LT(capacity(fieldBackpack), capacity(expeditionBackpack));

    const std::uint64_t lightGearWeight =
        scoutHelmet.unitWeightGrams +
        registry.item(ItemDefinitionId{
            "item.protective_gear.body_armor_light"}).unitWeightGrams +
        smallRig.unitWeightGrams + smallBackpack.unitWeightGrams;
    const std::uint64_t balancedGearWeight =
        basicHelmet.unitWeightGrams +
        registry.item(alpha_content::bodyArmor).unitWeightGrams +
        patrolRig.unitWeightGrams + fieldBackpack.unitWeightGrams;
    const std::uint64_t heavyGearWeight =
        heavyHelmet.unitWeightGrams +
        registry.item(ItemDefinitionId{
            "item.protective_gear.body_armor_heavy"}).unitWeightGrams +
        assaultRig.unitWeightGrams + expeditionBackpack.unitWeightGrams;
    EXPECT_LT(lightGearWeight, balancedGearWeight);
    EXPECT_LT(balancedGearWeight, 22000U);
    EXPECT_GT(heavyGearWeight, 22000U);
}

TEST(ContentRegistryTest, ContentBetaRejectsOversizedContainerCompartment)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"width\": 7, \"height\": 5, \"pocket\": \"general\"}",
        "{\"width\": 13, \"height\": 5, \"pocket\": \"general\"}");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, ContentBetaLoadoutArchetypesRejectBadReferences)
{
    const std::string unknownWeapon = replaceFirst(
        publishedJsonCopy(),
        "\"item.weapon.pistol_basic\", \"item.weapon.rifle_basic\"",
        "\"item.weapon.missing\", \"item.weapon.rifle_basic\"");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(unknownWeapon)),
        ContentRegistryError);

    const std::string wrongSlot = replaceFirst(
        publishedJsonCopy(),
        "\"body_armor\": [\"item.protective_gear.body_armor_basic\", \"item.protective_gear.body_armor_light\"]",
        "\"body_armor\": [\"item.protective_gear.helmet_scout\"]");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(wrongSlot)),
        ContentRegistryError);

    const std::string missingMap = replaceFirst(
        publishedJsonCopy(),
        "\"recommended_map\": \"map.raid.riverside\"",
        "\"recommended_map\": \"map.raid.missing\"");
    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(missingMap)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, PublishedContentRejectsUnknownAmmunitionCaliber)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "\"caliber\": \"caliber.5_45x39\", \"tier\": \"standard\"",
        "\"caliber\": \"caliber.unknown\", \"tier\": \"standard\"");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, ContentBetaReleasedItemsHaveSourceUseAndSink)
{
    const ContentRegistry &registry = publishedContentRegistry();
    const std::array<ItemDefinitionId, 20> released{
        ItemDefinitionId{"item.ammunition.9mm_enhanced"},
        ItemDefinitionId{"item.ammunition.5_45x39_standard"},
        ItemDefinitionId{"item.ammunition.5_45x39_enhanced"},
        ItemDefinitionId{"item.ammunition.7_62x51_standard"},
        ItemDefinitionId{"item.ammunition.7_62x51_enhanced"},
        ItemDefinitionId{"item.magazine.5_45x39_30"},
        ItemDefinitionId{"item.magazine.7_62x51_20"},
        ItemDefinitionId{"item.magazine.7_62x51_100_box"},
        ItemDefinitionId{"item.weapon.carbine_5_45_compact"},
        ItemDefinitionId{"item.weapon.rifle_5_45_service"},
        ItemDefinitionId{"item.weapon.dmr_7_62x51_service"},
        ItemDefinitionId{"item.weapon.lmg_7_62x51_service"},
        ItemDefinitionId{"item.protective_gear.helmet_scout"},
        ItemDefinitionId{"item.protective_gear.helmet_heavy"},
        ItemDefinitionId{"item.protective_gear.body_armor_light"},
        ItemDefinitionId{"item.protective_gear.body_armor_heavy"},
        ItemDefinitionId{"item.container.chest_rig_patrol"},
        ItemDefinitionId{"item.container.chest_rig_assault"},
        ItemDefinitionId{"item.container.backpack_field"},
        ItemDefinitionId{"item.container.backpack_expedition"}};

    for (const ItemDefinitionId &itemId : released)
    {
        const ItemDefinition &item = registry.item(itemId);
        const bool hasLootSource = std::any_of(
            registry.lootTables().begin(),
            registry.lootTables().end(),
            [&itemId](const LootTableDefinition &table)
            {
                return std::any_of(
                    table.entries.begin(),
                    table.entries.end(),
                    [&itemId](const LootContentEntry &entry)
                    {
                        return entry.itemDefinitionId == itemId;
                    });
            });
        EXPECT_TRUE(item.marketBuyPrice > 0U || hasLootSource)
            << itemId.value();
        EXPECT_GT(item.marketRecyclePrice, 0U) << itemId.value();
        if (item.category == ItemCategory::Ammunition)
        {
            EXPECT_TRUE(std::any_of(
                registry.items().begin(),
                registry.items().end(),
                [&registry, &itemId](const ItemDefinition &candidate)
                {
                    return candidate.category == ItemCategory::Magazine &&
                        registry.ammunitionFitsMagazine(
                            itemId, candidate.definitionId);
                })) << itemId.value();
        }
        else if (item.category == ItemCategory::Magazine)
        {
            EXPECT_TRUE(std::any_of(
                registry.items().begin(),
                registry.items().end(),
                [&registry, &itemId](const ItemDefinition &candidate)
                {
                    return candidate.category == ItemCategory::Weapon &&
                        registry.magazineFitsWeapon(
                            itemId, candidate.definitionId);
                })) << itemId.value();
        }
        else if (item.category == ItemCategory::ProtectiveGear ||
                 item.category == ItemCategory::Container)
        {
            ASSERT_TRUE(item.equipmentSlot.has_value()) << itemId.value();
            EXPECT_TRUE(itemCanEquipInSlot(item, *item.equipmentSlot))
                << itemId.value();
        }
    }
}

TEST(ContentRegistryTest,
     FixedSupplyIsDerivedFromPurchasableDefinitionsAndExcludesHighTierItems)
{
    const ContentRegistry &registry = publishedContentRegistry();
    const std::set<ItemDefinitionId> fixedSupply{
        registry.fixedSupplyItemIds().begin(),
        registry.fixedSupplyItemIds().end()};

    for (const ItemDefinition &item : registry.items())
    {
        EXPECT_EQ(
            fixedSupply.contains(item.definitionId),
            item.marketBuyPrice > 0U)
            << item.definitionId.value();
    }
    for (const ItemDefinitionId &excluded : {
             ItemDefinitionId{"item.ammunition.9mm_enhanced"},
             ItemDefinitionId{"item.ammunition.5_45x39_enhanced"},
             ItemDefinitionId{"item.ammunition.7_62x51_enhanced"},
             ItemDefinitionId{"item.weapon.dmr_7_62x51_service"},
             ItemDefinitionId{"item.weapon.lmg_7_62x51_service"},
             ItemDefinitionId{"item.protective_gear.body_armor_heavy"},
             ItemDefinitionId{"item.container.backpack_expedition"}})
    {
        EXPECT_FALSE(fixedSupply.contains(excluded)) << excluded.value();
    }
}

TEST(ContentRegistryTest, EveryPublishedItemHasARealSourceAndConsumer)
{
    const ContentRegistry &registry = publishedContentRegistry();
    for (const ItemDefinition &item : registry.items())
    {
        const bool lootSource = std::any_of(
            registry.lootTables().begin(),
            registry.lootTables().end(),
            [&item](const LootTableDefinition &table)
            {
                return std::any_of(
                    table.entries.begin(), table.entries.end(),
                    [&item](const LootContentEntry &entry)
                    {
                        return entry.itemDefinitionId ==
                            item.definitionId;
                    });
            });
        const bool manufacturingSource = std::any_of(
            registry.baseManufacturingRecipes().begin(),
            registry.baseManufacturingRecipes().end(),
            [&item](const BaseManufacturingRecipeDefinition &recipe)
            {
                return recipe.outputItemDefinitionId ==
                    item.definitionId;
            });
        EXPECT_TRUE(
            item.marketBuyPrice > 0U || lootSource ||
            manufacturingSource)
            << item.definitionId.value();

        const bool manufacturingConsumer = std::any_of(
            registry.baseManufacturingRecipes().begin(),
            registry.baseManufacturingRecipes().end(),
            [&item](const BaseManufacturingRecipeDefinition &recipe)
            {
                return std::any_of(
                    recipe.inputs.begin(), recipe.inputs.end(),
                    [&item](const BaseManufacturingInputDefinition &input)
                    {
                        return input.itemDefinitionId ==
                            item.definitionId;
                    });
            });
        const bool typedGameplayConsumer =
            item.category != ItemCategory::Loot ||
            item.baseContribution.has_value() ||
            item.baseConstructionMaterialValue > 0U;
        EXPECT_TRUE(
            typedGameplayConsumer || manufacturingConsumer ||
            item.marketRecyclePrice > 0U)
            << item.definitionId.value();
    }
}

TEST(ContentRegistryTest, BaseManufacturingRejectsRecycleValueCreation)
{
    const std::string invalid = replaceFirst(
        publishedJsonCopy(),
        "{\"item\": \"item.loot.precision_components\", \"quantity\": 1}",
        "{\"item\": \"item.loot.scrap_parts\", \"quantity\": 1}");

    EXPECT_THROW(
        static_cast<void>(ContentRegistry::fromJson(invalid)),
        ContentRegistryError);
}

TEST(ContentRegistryTest, FixedMapsPublishDistinctLootRoles)
{
    const ContentRegistry &registry = publishedContentRegistry();
    EXPECT_EQ(
        registry.map(MapDefinitionId{"map.v0.test"}).raidLootTableId,
        LootTableDefinitionId{"loot.raid.greyline_supply_v1"});
    EXPECT_EQ(
        registry.map(MapDefinitionId{"map.raid.riverside"})
            .raidLootTableId,
        LootTableDefinitionId{"loot.raid.riverside_supply_v1"});
    EXPECT_EQ(
        registry.map(MapDefinitionId{"map.raid.industrial"})
            .raidLootTableId,
        LootTableDefinitionId{"loot.raid.ashworks_industrial_v1"});
}
