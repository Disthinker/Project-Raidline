#include "base_fortification_placement.h"
#include "game_flow.h"
#include "home_founding_types.h"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

namespace
{
const auto &content = publishedContentRegistry();
struct Fixture
{
    ProfileState profile = makeNewAlphaProfile("placement-proof", content);
    BaseWorld world;
    Fixture()
    {
        profile.baseConstruction.materialUnits = 100;
        auto receipt = executeFortificationCommand(
            profile, content, BuildFortificationCommand{kWoodBarricadeDefinition},
            {profile.revision, "build"});
        if (!receipt.succeeded)
            throw std::runtime_error(receipt.message);
    }
    auto candidates() const
    {
        return baseDefensePositionCandidates(world.layout(), world.plotId(),
                                             *content.findFortification(kWoodBarricadeDefinition));
    }
    auto first() const
    {
        const auto values = candidates();
        for (const auto &position : values)
            if (position.available)
                return InstallFortificationCommand{{1}, position.key};
        throw std::runtime_error("No published fixed position");
    }
    FortificationReceipt install(const InstallFortificationCommand &command,
                                 std::string tx = "install")
    {
        return executeFortificationPlacement(profile, content, world, command,
                                             {profile.revision, std::move(tx)});
    }
    void ground(Vec2 position, std::string definition = "item.loot.cola_basic")
    {
        static_cast<void>(profile.assets.create(
            content.item(ItemDefinitionId{std::move(definition)}),
            BaseGroundAssetLocation{profile.regionalOperations.technologyCore.baseSiteDefinitionId,
                                    position}));
    }
};
} // namespace

TEST(BaseFortificationPlacementTest, PublishedSitesPlotsProvePlayerFacilitiesAndRealDefense)
{
    for (const auto &site : content.regionalOperations().baseSites)
    {
        std::vector<std::string> plots{""};
        for (const auto &plot : homePlotDefinitions())
            plots.emplace_back(plot.id);
        for (const auto &plot : plots)
        {
            Fixture f;
            f.profile.regionalOperations.technologyCore.baseSiteDefinitionId = site.id;
            f.profile.homeFounding.plots.clear();
            if (!plot.empty())
                f.profile.homeFounding.plots[site.id] = plot;
            f.world.configureSite(site.id.value(), {}, plot);
            std::size_t legal = 0;
            for (const auto &position : f.candidates())
            {
                if (!position.available)
                    continue;
                SCOPED_TRACE(std::string{site.id.value()} + ":" + plot + ":" +
                             std::to_string(static_cast<int>(position.key.side)));
                const auto before = profileStateFingerprint(f.profile);
                const auto plan =
                    queryFortificationPlacement(f.profile, content, f.world, {{1}, position.key});
                EXPECT_EQ(profileStateFingerprint(f.profile), before);
                EXPECT_TRUE(plan.canCommit) << plan.message;
                if (plan.canCommit)
                {
                    ++legal;
                    EXPECT_GE(plan.verifiedPlayerDestinations, 9U);
                    EXPECT_GE(plan.verifiedDefenseApproaches, 1U);
                    EXPECT_LE(plan.verifiedDefenseApproaches, 2U);
                    EXPECT_EQ(plan.footprint, position.footprint);
                }
            }
            EXPECT_GE(legal, 1U);
        }
    }
}

TEST(BaseFortificationPlacementTest, InstallMoveStorePreservesHealthCostIdsAndRoundTrip)
{
    Fixture f;
    const auto command = f.first();
    f.profile.baseFortifications.instances.at({1}).durability = 17;
    const auto before = f.profile;
    const auto r = f.install(command);
    ASSERT_TRUE(r.succeeded) << r.message;
    EXPECT_EQ(r.materialSpent, 0U);
    EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).slot, command.slot);
    EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).durability, 17U);
    EXPECT_EQ(f.profile.baseConstruction.materialUnits, before.baseConstruction.materialUnits);
    EXPECT_EQ(f.profile.assets.nextAssetId(), before.assets.nextAssetId());
    EXPECT_EQ(f.profile.baseFortifications.nextInstanceId,
              before.baseFortifications.nextInstanceId);
    EXPECT_EQ(f.profile.currency, before.currency);
    const auto accepted = profileStateFingerprint(f.profile);
    auto duplicate =
        executeFortificationPlacement(f.profile, content, f.world, command, {0, "install"});
    EXPECT_TRUE(duplicate.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(f.profile), accepted);
    for (const auto &next : f.candidates())
    {
        if (!next.available || next.key == command.slot)
            continue;
        ASSERT_TRUE(f.install({{1}, next.key}, "move").succeeded);
        EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).slot, next.key);
        EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).durability, 17U);
        break;
    }
    auto restored = deserializeProfileEnvelope(
        serializeProfileEnvelope(f.profile, content.contentVersion()), content);
    ASSERT_TRUE(restored.profile) << restored.message;
    EXPECT_EQ(profileStateFingerprint(*restored.profile), profileStateFingerprint(f.profile));
    ASSERT_TRUE(executeFortificationCommand(f.profile, content, StoreFortificationCommand{{1}},
                                            {f.profile.revision, "store"})
                    .succeeded);
    EXPECT_FALSE(f.profile.baseFortifications.instances.at({1}).slot);
    EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).durability, 17U);
}

TEST(BaseFortificationPlacementTest,
     DuplicateSlotStaleRevisionAndWrongIdentitiesRefuseWithoutMutation)
{
    Fixture f;
    auto command = f.first();
    ASSERT_TRUE(f.install(command).succeeded);
    ASSERT_TRUE(executeFortificationCommand(f.profile, content,
                                            BuildFortificationCommand{kWoodBarricadeDefinition},
                                            {f.profile.revision, "build2"})
                    .succeeded);
    const auto before = profileStateFingerprint(f.profile);
    const auto collision =
        queryFortificationPlacement(f.profile, content, f.world, {{2}, command.slot});
    EXPECT_EQ(collision.placementFailure, FortificationPlacementFailure::OccupiedSlot);
    EXPECT_FALSE(f.install({{2}, command.slot}, "duplicate-slot").succeeded);
    EXPECT_FALSE(executeFortificationPlacement(f.profile, content, f.world, command,
                                               {f.profile.revision - 1, "stale"})
                     .succeeded);
    EXPECT_FALSE(executeFortificationPlacement(f.profile, content, f.world, command,
                                               {f.profile.revision, ""})
                     .succeeded);
    command.instance = {999};
    EXPECT_FALSE(f.install(command, "missing").succeeded);
    command.instance = {1};
    command.slot.plot = "not-the-current-plot";
    EXPECT_FALSE(f.install(command, "plot").succeeded);
    command = f.first();
    command.slot.side = static_cast<DefenseSide>(99);
    EXPECT_FALSE(f.install(command, "side").succeeded);
    command = f.first();
    command.slot.site = RegionalBaseSiteDefinitionId{"regional_base_site.ashworks_logistics_yard"};
    EXPECT_FALSE(f.install(command, "site").succeeded);
    EXPECT_EQ(profileStateFingerprint(f.profile), before);
}

TEST(BaseFortificationPlacementTest, PreviewDoesNotAuthorizePlacementOverNewGroundLoot)
{
    Fixture f;
    const auto command = f.first();
    const auto preview = queryFortificationPlacement(f.profile, content, f.world, command);
    ASSERT_TRUE(preview.canCommit) << preview.message;
    const auto r = preview.footprint;
    f.ground({r.position.x + r.size.x / 2, r.position.y + r.size.y / 2});
    const auto before = profileStateFingerprint(f.profile);
    const auto changed = queryFortificationPlacement(f.profile, content, f.world, command);
    EXPECT_EQ(changed.placementFailure, FortificationPlacementFailure::OccupiedClearance);
    EXPECT_FALSE(f.install(command).succeeded);
    EXPECT_EQ(profileStateFingerprint(f.profile), before);
}

TEST(BaseFortificationPlacementTest, DamagedRemnantStillOwnsSlotAndReservesRepairableFullFootprint)
{
    Fixture f;
    auto command = f.first();
    f.profile.baseFortifications.instances.at({1}).durability = 0;
    ASSERT_TRUE(f.install(command).succeeded);
    ASSERT_TRUE(executeFortificationCommand(f.profile, content,
                                            BuildFortificationCommand{kWoodBarricadeDefinition},
                                            {f.profile.revision, "build2"})
                    .succeeded);
    EXPECT_EQ(queryFortificationPlacement(f.profile, content, f.world, {{2}, command.slot})
                  .placementFailure,
              FortificationPlacementFailure::OccupiedSlot);
    EXPECT_EQ(f.profile.baseFortifications.instances.at({1}).durability, 0U);
}

TEST(BaseFortificationPlacementTest, MultipleInstalledRemnantsKeepEveryRequiredPassageLegal)
{
    Fixture f;
    std::uint64_t id = 1;
    for (const auto &position : f.candidates())
    {
        if (!position.available)
            continue;
        if (id != 1)
            ASSERT_TRUE(executeFortificationCommand(
                            f.profile, content, BuildFortificationCommand{kWoodBarricadeDefinition},
                            {f.profile.revision, "build:" + std::to_string(id)})
                            .succeeded);
        f.profile.baseFortifications.instances.at({id}).durability = 0;
        const InstallFortificationCommand command{{id}, position.key};
        const auto plan = queryFortificationPlacement(f.profile, content, f.world, command);
        ASSERT_TRUE(plan.canCommit) << plan.message;
        ASSERT_TRUE(f.install(command, "install:" + std::to_string(id)).succeeded);
        ++id;
    }
    ASSERT_GT(id, 2U);
    EXPECT_EQ(f.profile.baseFortifications.instances.size(), id - 1);
    for (const auto &[key, record] : f.profile.baseFortifications.instances)
    {
        EXPECT_EQ(record.durability, 0U);
        ASSERT_TRUE(record.slot);
        EXPECT_TRUE(queryFortificationPlacement(f.profile, content, f.world, {key, *record.slot})
                        .canCommit);
    }
}

TEST(BaseFortificationPlacementTest, WarningAndActiveDefenseFreezeInstallation)
{
    Fixture f;
    const auto command = f.first();
    f.profile.baseSiege.warningActive = true;
    const auto before = profileStateFingerprint(f.profile);
    EXPECT_EQ(queryFortificationPlacement(f.profile, content, f.world, command).placementFailure,
              FortificationPlacementFailure::ActivityLocked);
    EXPECT_FALSE(f.install(command).succeeded);
    EXPECT_EQ(profileStateFingerprint(f.profile), before);
    f.profile.baseSiege.warningActive = false;
    f.profile.activeBaseDefense.emplace();
    EXPECT_FALSE(f.install(command).succeeded);
}

TEST(BaseFortificationPlacementTest, CurrentFacilityGeometryIsRecheckedAfterPreview)
{
    Fixture f;
    const auto command = f.first();
    const auto preview = queryFortificationPlacement(f.profile, content, f.world, command);
    ASSERT_TRUE(preview.canCommit);
    const auto r = preview.footprint;
    f.world.configureSite(f.world.siteDefinitionId(),
                          {{BaseFacilityKind::Storage,
                            {r.position.x + r.size.x / 2, r.position.y + r.size.y / 2},
                            true}});
    const auto before = profileStateFingerprint(f.profile);
    EXPECT_EQ(queryFortificationPlacement(f.profile, content, f.world, command).placementFailure,
              FortificationPlacementFailure::OccupiedClearance);
    EXPECT_FALSE(f.install(command).succeeded);
    EXPECT_EQ(profileStateFingerprint(f.profile), before);
}

TEST(BaseFortificationPlacementTest,
     GlobalRouteProofRejectsRemoteSealedFacilityDespiteLocalClearance)
{
    Fixture f;
    const auto command = f.first();
    // Coincident fixed facilities make a required entrance unreachable, far
    // from the proposed slot. A local overlap-only query would accept it.
    const auto &storage = f.world.facilities()[0];
    const Vec2 p{storage.bounds.position.x + storage.bounds.size.x / 2,
                 storage.bounds.position.y + storage.bounds.size.y + 50};
    f.world.configureSite(f.world.siteDefinitionId(), {{BaseFacilityKind::Medical, p, true}});
    const auto before = profileStateFingerprint(f.profile);
    const auto plan = queryFortificationPlacement(f.profile, content, f.world, command);
    EXPECT_FALSE(plan.canCommit);
    EXPECT_EQ(plan.placementFailure, FortificationPlacementFailure::PlayerOrFacilityUnreachable)
        << plan.message;
    EXPECT_EQ(profileStateFingerprint(f.profile), before);
}

TEST(BaseFortificationPlacementTest, RealSessionSaveFailureLeavesSlotAndRuntimeUnchanged)
{
    struct TemporarySave
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("raidline-placement-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporarySave()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } temporary;
    Fixture f;
    SaveRepository repository{temporary.path};
    ASSERT_TRUE(repository.save(f.profile, content.contentVersion()).succeeded);
    GameFlow flow;
    flow.configurePersistence(temporary.path);
    ASSERT_TRUE(flow.continueGame());
    auto &session = flow.gameSession();
    flow.updateBase({}, 0.000001F);
    ASSERT_TRUE(session.checkpointWorldClock());
    const auto enemies = flow.baseWorld().perimeterEnemySnapshots();
    ASSERT_FALSE(enemies.empty());
    const auto command = f.first();
    const auto plan = session.queryBaseFortificationPlacement(flow.baseWorld(), command);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    const auto before = profileStateFingerprint(session.profile());
    const auto obstruction = temporary.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    const auto refused = session.installBaseFortification(flow.baseWorld(), command, plan.revision);
    EXPECT_FALSE(refused.succeeded);
    EXPECT_TRUE(flow.baseWorld().fortifications().empty());
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    EXPECT_EQ(flow.baseWorld().perimeterEnemySnapshots(), enemies);
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    const auto accepted =
        session.installBaseFortification(flow.baseWorld(), command, plan.revision);
    ASSERT_TRUE(accepted.succeeded) << accepted.message;
    EXPECT_EQ(flow.baseWorld().perimeterEnemySnapshots(), enemies);
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(session.profile()));
    EXPECT_EQ(loaded.profile->baseFortifications.instances.at({1}).slot, command.slot);
    // Production session projects only the committed owner on the next update.
    flow.updateBase({}, 0.0F);
    ASSERT_EQ(flow.baseWorld().fortifications().size(), 1U);
    EXPECT_EQ(flow.baseWorld().fortifications()[0].id, FortificationInstanceId{1});
    EXPECT_EQ(flow.baseWorld().perimeterEnemySnapshots(), enemies);
    ASSERT_TRUE(session.executeBaseFortification(StoreFortificationCommand{{1}}).succeeded);
    flow.updateBase({}, 0.0F);
    EXPECT_TRUE(flow.baseWorld().fortifications().empty());
    EXPECT_EQ(flow.baseWorld().perimeterEnemySnapshots(), enemies);
}

TEST(BaseFortificationPlacementTest, RejectedPreviewPreservesRealEnemyIdentityAndPosition)
{
    GameFlow flow;
    ASSERT_TRUE(flow.startNewGame("placement-runtime-boundary", false));
    flow.updateBase({}, 0.000001F);
    const auto before = flow.baseWorld().perimeterEnemySnapshots();
    ASSERT_FALSE(before.empty());
    const auto &enemy = flow.baseWorld().perimeterEnemies().front();
    const auto *address = &enemy;
    const auto id = enemy.combatTargetId();
    Fixture f;
    const auto rejected =
        flow.gameSession().queryBaseFortificationPlacement(flow.baseWorld(), f.first());
    EXPECT_FALSE(rejected.canCommit);
    EXPECT_EQ(rejected.placementFailure, FortificationPlacementFailure::MissingInstance);
    EXPECT_EQ(flow.baseWorld().perimeterEnemySnapshots(), before);
    EXPECT_EQ(&flow.baseWorld().perimeterEnemies().front(), address);
    EXPECT_EQ(flow.baseWorld().perimeterEnemies().front().combatTargetId(), id);
    EXPECT_FALSE(flow.baseWorld().baseDefenseActive());
}
