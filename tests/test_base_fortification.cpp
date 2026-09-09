#include "base_defense_positions.h"
#include "base_facility_layout_domain.h"
#include "base_fortification_domain.h"
#include "base_fortification_serialization.h"
#include "base_migration_domain.h"
#include "game_flow.h"
#include "home_founding_types.h"
#include "raid_space_query.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <nlohmann/json.hpp>

namespace
{
const auto &content = publishedContentRegistry();
auto fresh()
{
    auto p = makeNewAlphaProfile("fortifications", content);
    p.baseConstruction.materialUnits = 100;
    return p;
}
auto build(ProfileState &p, std::string tx = "build")
{
    return executeFortificationCommand(p, content,
                                       BuildFortificationCommand{kWoodBarricadeDefinition},
                                       {p.revision, std::move(tx)});
}
DefenseSlotKey slot()
{
    return {RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"}, "",
            DefenseSide::West};
}
} // namespace

TEST(BaseFortificationTest, ConstructionConsumesPublicMaterialsNotInventoryAndAllocatesStableIds)
{
    auto p = fresh();
    const auto before = p;
    const auto hash = profileStateFingerprint(p);
    const auto plan =
        queryFortificationCommand(p, content, BuildFortificationCommand{kWoodBarricadeDefinition});
    EXPECT_EQ(profileStateFingerprint(p), hash);
    ASSERT_TRUE(plan.canCommit);
    EXPECT_EQ(plan.materialCost, 8U);
    auto r = build(p);
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(r.instance.value, 1U);
    EXPECT_EQ(p.baseFortifications.nextInstanceId, 2U);
    EXPECT_FALSE(p.baseFortifications.instances.at(r.instance).slot);
    EXPECT_EQ(p.baseFortifications.instances.at(r.instance).durability, 120U);
    EXPECT_EQ(p.baseConstruction.materialUnits, 92U);
    EXPECT_EQ(p.assets.nextAssetId(), before.assets.nextAssetId());
    EXPECT_EQ(p.currency, before.currency);
    const auto payload = [](const auto &v) {
        return nlohmann::json::parse(serializeProfileEnvelope(v, content.contentVersion()))
            .at("payload")
            .at("assets");
    };
    EXPECT_EQ(payload(p), payload(before));
    EXPECT_EQ(build(p, "build-second").instance.value, 2U);
}

TEST(BaseFortificationTest, IdempotentRetryCannotSpendTwiceOrInventHistoricalReceipt)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    const auto before = profileStateFingerprint(p);
    auto r = executeFortificationCommand(
        p, content, BuildFortificationCommand{kWoodBarricadeDefinition}, {0, "build"});
    EXPECT_TRUE(r.succeeded);
    EXPECT_TRUE(r.alreadyCommitted);
    EXPECT_EQ(r.materialSpent, 0U);
    EXPECT_EQ(r.instance.value, 0U);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseFortificationTest, RejectionsKeepWholeProfileAndIdHighWaterUnchanged)
{
    auto p = fresh();
    p.baseConstruction.materialUnits = 7;
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(build(p).succeeded);
    EXPECT_FALSE(executeFortificationCommand(p, content,
                                             BuildFortificationCommand{kWoodBarricadeDefinition},
                                             {p.revision + 1, "stale"})
                     .succeeded);
    EXPECT_FALSE(executeFortificationCommand(p, content,
                                             BuildFortificationCommand{kWoodBarricadeDefinition},
                                             {p.revision, ""})
                     .succeeded);
    EXPECT_FALSE(executeFortificationCommand(
                     p, content,
                     BuildFortificationCommand{FortificationDefinitionId{"fortification.missing"}},
                     {p.revision, "unknown"})
                     .succeeded);
    EXPECT_FALSE(executeFortificationCommand(p, content, RepairFortificationCommand{{1}},
                                             {p.revision, "missing"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseFortificationTest, CapacityAndHighWaterOverflowFailClosed)
{
    auto p = fresh();
    p.baseFortifications.nextInstanceId = std::numeric_limits<std::uint64_t>::max();
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(build(p).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
    p = fresh();
    for (std::uint64_t id = 1; id <= 64; ++id)
        p.baseFortifications.instances.emplace(
            FortificationInstanceId{id}, FortificationRecord{kWoodBarricadeDefinition, 120, {}});
    p.baseFortifications.nextInstanceId = 65;
    const auto full = profileStateFingerprint(p);
    EXPECT_FALSE(build(p).succeeded);
    EXPECT_EQ(profileStateFingerprint(p), full);
}

TEST(BaseFortificationTest, RepairCeilingRoundingAndDestroyedRemnantsRemainOwned)
{
    for (const auto [health, cost] : {std::pair{119U, 1U}, {90U, 1U}, {89U, 2U}, {0U, 4U}})
    {
        auto p = fresh();
        auto built = build(p);
        ASSERT_TRUE(built.succeeded);
        p.baseFortifications.instances.at(built.instance).durability = health;
        const auto before = p.baseConstruction.materialUnits;
        const auto plan =
            queryFortificationCommand(p, content, RepairFortificationCommand{built.instance});
        ASSERT_TRUE(plan.canCommit);
        EXPECT_EQ(plan.materialCost, cost);
        const auto result = executeFortificationCommand(
            p, content, RepairFortificationCommand{built.instance}, {p.revision, "repair"});
        ASSERT_TRUE(result.succeeded) << result.message;
        EXPECT_EQ(before - p.baseConstruction.materialUnits, cost);
        EXPECT_EQ(p.baseFortifications.instances.at(built.instance).durability, 120U);
        EXPECT_EQ(p.baseFortifications.nextInstanceId, 2U);
        EXPECT_FALSE(
            queryFortificationCommand(p, content, RepairFortificationCommand{built.instance})
                .canCommit);
    }
}

TEST(BaseFortificationTest, WarningOnlyPermitsRepairActiveCombatPermitsNoCommands)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    p.baseFortifications.instances.at({1}).durability = 31;
    p.baseSiege.raidThreatUnits = 100;
    p.baseSiege.safeUntilWorldMinute = p.worldClock.elapsedWorldMinutes;
    ASSERT_TRUE(activateBaseSiegeWarningIfEligible(p));
    EXPECT_FALSE(
        queryFortificationCommand(p, content, BuildFortificationCommand{kWoodBarricadeDefinition})
            .canCommit);
    EXPECT_FALSE(queryFortificationCommand(p, content, StoreFortificationCommand{{1}}).canCommit);
    EXPECT_TRUE(queryFortificationCommand(p, content, RepairFortificationCommand{{1}}).canCommit);
    p.activeBaseDefense.emplace(); // rejection gate must run before interpreting the runtime DTO
    const auto before = profileStateFingerprint(p);
    EXPECT_FALSE(executeFortificationCommand(p, content, RepairFortificationCommand{{1}},
                                             {p.revision, "combat"})
                     .succeeded);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseFortificationTest, StoreAndMigrationReserveCannotRepairRefundOrDuplicate)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    auto &record = p.baseFortifications.instances.at({1});
    record.slot = slot();
    record.durability = 17;
    const auto materials = p.baseConstruction.materialUnits;
    ASSERT_TRUE(executeFortificationCommand(p, content, StoreFortificationCommand{{1}},
                                            {p.revision, "store"})
                    .succeeded);
    EXPECT_EQ(p.baseFortifications.instances.at({1}).durability, 17U);
    EXPECT_EQ(p.baseConstruction.materialUnits, materials);
    p.baseFortifications.instances.at({1}).slot = slot();
    storeAllBaseFortifications(p.baseFortifications);
    EXPECT_FALSE(p.baseFortifications.instances.at({1}).slot);
    EXPECT_EQ(p.baseFortifications.instances.at({1}).durability, 17U);
    EXPECT_EQ(p.baseFortifications.nextInstanceId, 2U);
}

TEST(BaseFortificationTest, ProfileValidationRejectsBadIdentitySlotsHealthAndReferences)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    const auto valid = p;
    p.baseFortifications.nextInstanceId = 1;
    EXPECT_FALSE(validateProfileState(p, content).valid);
    p = valid;
    p.baseFortifications.instances.at({1}).durability = 121;
    EXPECT_FALSE(validateProfileState(p, content).valid);
    p = valid;
    p.baseFortifications.instances.at({1}).definition =
        FortificationDefinitionId{"fortification.unknown"};
    EXPECT_FALSE(validateProfileState(p, content).valid);
    p = valid;
    p.baseFortifications.instances.at({1}).slot = slot();
    ASSERT_TRUE(build(p, "second").succeeded);
    p.baseFortifications.instances.at({2}).slot = slot();
    EXPECT_FALSE(validateProfileState(p, content).valid);
    p = valid;
    p.baseFortifications.instances.at({1}).slot = slot();
    p.baseFortifications.instances.at({1}).slot->plot = "missing";
    EXPECT_FALSE(validateProfileState(p, content).valid);
}

TEST(BaseFortificationTest, Schema47RoundTripKeepsRemnantIdentityAndSchema46MigrationIsEmpty)
{
    auto legacy = fresh();
    auto old = deserializeProfileEnvelope(
        serializeProfileEnvelope(legacy, "enemy-combat-contract-content-60", 46), content);
    ASSERT_TRUE(old.profile) << old.message;
    EXPECT_TRUE(old.profile->baseFortifications.instances.empty());
    EXPECT_EQ(old.profile->baseFortifications.nextInstanceId, 1U);
    ASSERT_TRUE(build(legacy).succeeded);
    legacy.baseFortifications.instances.at({1}).durability = 0;
    auto restored = deserializeProfileEnvelope(
        serializeProfileEnvelope(legacy, content.contentVersion()), content);
    ASSERT_TRUE(restored.profile) << restored.message;
    EXPECT_EQ(profileStateFingerprint(*restored.profile), profileStateFingerprint(legacy));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(legacy, content.contentVersion(), 46)),
                 std::invalid_argument);
}

TEST(BaseFortificationTest, JsonRejectsDuplicateIdsNegativeFractionalAndOverflowIntegers)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    const auto good = baseFortificationsJson(p.baseFortifications);
    auto bad = good;
    bad["instances"].push_back(bad["instances"][0]);
    EXPECT_THROW(static_cast<void>(parseBaseFortificationsJson(bad)), std::invalid_argument);
    for (const auto &value :
         {nlohmann::json(-1), nlohmann::json(1.5), nlohmann::json(4294967296ULL)})
    {
        bad = good;
        bad["instances"][0]["durability"] = value;
        EXPECT_THROW(static_cast<void>(parseBaseFortificationsJson(bad)), std::invalid_argument);
    }
    bad = good;
    bad["instances"][0]["slot"] = {{"site", slot().site.value()}, {"plot", ""}, {"side", 256}};
    EXPECT_THROW(static_cast<void>(parseBaseFortificationsJson(bad)), std::invalid_argument);
}

TEST(BaseFortificationTest, ActualMigrationStoresSameInstancesAndNeverCopiesToOldOutpost)
{
    auto p = fresh();
    ASSERT_TRUE(build(p).succeeded);
    p.baseFortifications.instances.at({1}).slot = slot();
    p.baseFortifications.instances.at({1}).durability = 0;
    const RegionalBaseSiteDefinitionId destination{"regional_base_site.ashworks_logistics_yard"};
    const RegionalOutpostDefinitionId outpost{"regional_outpost.ashworks_logistics_yard"};
    p.regionalOperations.baseSites.at(destination).unlocked = true;
    p.regionalOperations.outposts.at(outpost).unlocked = true;
    p.regionalOperations.outposts.at(outpost).established = true;
    p.baseConstruction.kitchenWaterLevel = 1;
    p.baseConstruction.facilities[BaseFacilityDefinitionId{"base_facility.kitchen_water"}] =
        BaseConstructionState::FacilityPlacement::Installed;
    initializeBaseFacilityLayouts(p, content);
    auto r = executeBaseMigration(p, content, {destination}, {p.revision, "migrate"});
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(p.baseFortifications.instances.size(), 1U);
    EXPECT_EQ(p.baseFortifications.nextInstanceId, 2U);
    EXPECT_EQ(p.baseFortifications.instances.at({1}).durability, 0U);
    EXPECT_FALSE(p.baseFortifications.instances.at({1}).slot);
    const auto before = profileStateFingerprint(p);
    EXPECT_TRUE(executeBaseMigration(p, content, {destination}, {0, "migrate"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p), before);
}

TEST(BaseFortificationTest, ServiceSaveFailureCommitsNeitherMaterialsNorInstance)
{
    struct TemporarySave
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("raidline-fortification-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporarySave()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } temporary;
    SaveRepository repository{temporary.path};
    auto original = fresh();
    ASSERT_TRUE(repository.save(original, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(temporary.path);
    ASSERT_TRUE(flow.continueGame());
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    const auto obstruction = temporary.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    const auto refused = flow.gameSession().executeBaseFortification(
        BuildFortificationCommand{kWoodBarricadeDefinition});
    EXPECT_FALSE(refused.succeeded);
    EXPECT_EQ(refused.materialSpent, 0U);
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    const auto accepted = flow.gameSession().executeBaseFortification(
        BuildFortificationCommand{kWoodBarricadeDefinition});
    ASSERT_TRUE(accepted.succeeded) << accepted.message;
    EXPECT_EQ(accepted.instance.value, 1U);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),
              profileStateFingerprint(flow.gameSession().profile()));
}

TEST(BaseFortificationTest, OldDefenseCheckpointAndWarningRemainRuleOneAfterMigration)
{
    GameFlow flow;
    ASSERT_TRUE(flow.startNewGame("old-fortification-warning", false));
    ASSERT_TRUE(flow.gameSession().triggerDeveloperBaseSiegeWarning());
    const auto warning = flow.gameSession().profile().baseSiege;
    const auto serializedWarning = serializeProfileEnvelope(flow.gameSession().profile(),
                                                            "enemy-combat-contract-content-60", 46);
    auto restoredWarning = deserializeProfileEnvelope(serializedWarning, content);
    ASSERT_TRUE(restoredWarning.profile) << restoredWarning.message;
    EXPECT_EQ(restoredWarning.profile->baseSiege, warning);
    ASSERT_TRUE(flow.gameSession().startBaseRealtimeDefense(flow.baseWorld()));
    const auto &old = *flow.gameSession().profile().activeBaseDefense;
    auto restored =
        deserializeProfileEnvelope(serializeProfileEnvelope(flow.gameSession().profile(),
                                                            "enemy-combat-contract-content-60", 46),
                                   content);
    ASSERT_TRUE(restored.profile) << restored.message;
    ASSERT_TRUE(restored.profile->activeBaseDefense);
    EXPECT_EQ(restored.profile->activeBaseDefense->rulesVersion, 1U);
    EXPECT_EQ(baseDefenseCheckpointHash(*restored.profile->activeBaseDefense),
              baseDefenseCheckpointHash(old));
    EXPECT_TRUE(restored.profile->baseFortifications.instances.empty());
}

TEST(BaseFortificationTest, ContentDefinitionsRejectMalformedAndDuplicateValues)
{
    const auto good = nlohmann::json::parse(publishedContentJson());
    auto bad = good;
    bad["fortifications"].push_back(bad["fortifications"][0]);
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
    for (const auto *key : {"maximum_durability", "build_material_units",
                            "repair_per_material_unit", "length", "depth"})
    {
        bad = good;
        bad["fortifications"][0][key] = 0;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())),
                     ContentRegistryError);
    }
    bad = good;
    bad.erase("fortifications");
    EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(bad.dump())), ContentRegistryError);
}

TEST(BaseFortificationTest, PublishedPlotsHaveStableCandidatesWithSpaceToPassBothSides)
{
    for (const auto &site : content.regionalOperations().baseSites)
    {
        std::vector<std::string> plots{""};
        for (const auto &plot : homePlotDefinitions())
            plots.emplace_back(plot.id);
        for (const auto &plot : plots)
        {
            SCOPED_TRACE(std::string{site.id.value()} + ":" + plot);
            const auto layout = plot.empty()
                                    ? generateHomeRegionLayout(site.id.value())
                                    : generateFoundingHomeRegionLayout(site.id.value(), plot);
            const auto positions = baseDefensePositionCandidates(
                layout, plot, *content.findFortification(kWoodBarricadeDefinition));
            const auto repeat = baseDefensePositionCandidates(
                layout, plot, *content.findFortification(kWoodBarricadeDefinition));
            std::size_t legal = 0;
            for (std::size_t i = 0; i < positions.size(); ++i)
            {
                const auto &position = positions[i];
                EXPECT_EQ(position.key, repeat[i].key);
                EXPECT_EQ(position.footprint, repeat[i].footprint);
                EXPECT_EQ(position.available, repeat[i].available);
                if (!position.available)
                    continue;
                ++legal;
                std::vector<BallisticBlocker> blockers{
                    {1, {position.footprint.position, position.footprint.size}}};
                const auto navigation =
                    RaidSpaceNavigationField::build({32, 48}, layout.worldSize, blockers, 0);
                ASSERT_TRUE(navigation);
                auto current = position.outsideApproach;
                for (unsigned step = 0;
                     step < 12 && std::hypot(current.x - position.insideApproach.x,
                                             current.y - position.insideApproach.y) > 1;
                     ++step)
                {
                    auto next = navigation->nextWaypoint(current, position.insideApproach, 0);
                    ASSERT_TRUE(next);
                    current = *next;
                }
                EXPECT_LT(std::hypot(current.x - position.insideApproach.x,
                                     current.y - position.insideApproach.y),
                          1);
                const std::vector<ContentRect> crate{position.footprint};
                EXPECT_FALSE(baseDefensePositionClear(position, crate));
                EXPECT_TRUE(baseDefensePositionClear(position, {}));
            }
            EXPECT_GE(legal, 1U);
        }
    }
}

TEST(BaseFortificationTest, PublishedSitesAndPlotsKeepARealDefenseApproachWithAllCandidatesSolid)
{
    for (const auto &site : content.regionalOperations().baseSites)
    {
        std::vector<std::string> plots{""};
        for (const auto &plot : homePlotDefinitions())
            plots.emplace_back(plot.id);
        for (const auto &plot : plots)
        {
            SCOPED_TRACE(std::string{site.id.value()} + ":" + plot);
            BaseWorld world;
            world.configureSite(site.id.value(), {}, plot);
            const auto candidates = baseDefensePositionCandidates(
                world.layout(), plot, *content.findFortification(kWoodBarricadeDefinition));
            std::vector<ContentRect> obstacles;
            for (const auto &candidate : candidates)
                if (candidate.available)
                    obstacles.push_back(candidate.footprint);
            world.configureGroundBlockers(
                obstacles); // test geometry injection only; not a production ownership path
            for (const auto seed : {17ULL, 91ULL, 318ULL})
            {
                SCOPED_TRACE(seed);
                BaseDefenseSnapshot input;
                input.eventId = "positions-proof:1";
                input.siegeSequence = 1;
                input.siteDefinitionId = site.id.value();
                input.plotId = plot;
                input.seed = seed;
                input.frozenPopulation = 8;
                input.frozenMoraleTier = 1;
                const auto plan = world.prepareBaseDefenseSnapshot(
                    input, content.enemyCombatDefinition(ordinaryInfectedDefinitionId()));
                ASSERT_TRUE(plan)
                    << "All-candidate geometry blocks existing real defense preparation";
                EXPECT_EQ(plan->wavePlans.size(), 3U);
                EXPECT_FALSE(plan->coreDefenseZones.empty());
                EXPECT_LE(plan->coreDefenseZones.size(), 2U);
            }
        }
    }
}
