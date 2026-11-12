#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <vector>

#include "alpha_content_ids.h"
#include "base_world.h"
#include "game_session.h"
#include "test_temp_directory.h"
#include "thrown_illumination_domain.h"
#include "thrown_illumination_runtime.h"

namespace
{
const RaidSpaceDefinitionId kTestSpace{"raid_space.flare_test"};
const ThrownIlluminationUseDefinition kUse;

AssetInstanceId storeMaterial(ProfileState &profile, const ContentRegistry &content,
    const ItemDefinitionId &id)
{
    const auto &definition = content.item(id);
    const auto fit = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        definition, ItemOrientation::Degrees0);
    if (!fit)
    {
        ADD_FAILURE() << "No space for material " << id.value();
        return 0;
    }
    return profile.assets.create(definition, StoredAssetLocation{ProfileContainerId::stash(), *fit});
}

void manufactureAndEquip(ProfileState &profile, AssetInstanceId &flare)
{
    const auto &content = publishedContentRegistry();
    ASSERT_NE(storeMaterial(profile, content, ItemDefinitionId{"item.loot.copper_wire_spool"}), 0U);
    ASSERT_NE(storeMaterial(profile, content, alpha_content::lootElectronics), 0U);
    const auto order = executeStartBaseManufacturing(profile, content,
        {kThrownIlluminationRecipeId}, {profile.revision, "manufacture-test-flare"});
    ASSERT_TRUE(order.succeeded) << order.message;
    ASSERT_TRUE(order.outputAssetId);
    static_cast<void>(advanceWorldClock(profile.worldClock, 90));
    ASSERT_TRUE(applyBaseManufacturingThrough(profile, content).completed);

    flare = *order.outputAssetId;
    ASSERT_NE(profile.assets.find(flare), nullptr);
    ASSERT_EQ(profile.assets.find(flare)->quantity, 2U);
    AssetInstanceId rig{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::chestRig)
        {
            rig = id;
            break;
        }
    }
    ASSERT_NE(rig, 0U);
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{rig, EquipmentSlotKind::ChestRig},
        {profile.revision, "equip-flare-rig"}).succeeded);
    const auto &pockets = content.item(alpha_content::chestRig).containerCompartments;
    for (std::uint32_t i = 0; i < pockets.size(); ++i)
    {
        if (pockets[i].pocketKind != ContainerPocketKind::General) continue;
        const auto compartment = ProfileContainerId::compartment(rig, i);
        const auto fit = findFirstProfileFit(profile, content, compartment,
            content.item(kThrownIlluminationItemId), ItemOrientation::Degrees0);
        if (!fit) continue;
        ASSERT_TRUE(executeInventory(profile, content,
            InventoryMoveCommand{flare, 0U, {compartment, *fit}},
            {profile.revision, "carry-test-flare"}).succeeded);
        return;
    }
    FAIL() << "No general compartment for the flare stack";
}
}

TEST(ThrownIlluminationTest, LandingUsesExistingFirstWallStopAndRangeCap)
{
    const auto index = RaidSpaceBlockerIndex::build(
        {1000.0F, 1000.0F}, std::vector<BallisticBlocker>{{1, {{250, 70}, {15, 60}}}});
    ASSERT_TRUE(index);
    const auto stopped = queryThrownIlluminationLanding({100, 100}, {800, 100}, kTestSpace, kUse, *index);
    ASSERT_TRUE(stopped);
    GrenadeUseDefinition existingThrow;
    existingThrow.range = kUse.maximumRange;
    const auto grenadeStop = queryGrenadeLanding(
        {100, 100}, {800, 100}, kTestSpace, existingThrow, *index);
    ASSERT_TRUE(grenadeStop);
    EXPECT_EQ(stopped->point.x, grenadeStop->point.x);
    EXPECT_EQ(stopped->point.y, grenadeStop->point.y);
    EXPECT_GT(stopped->point.x, 100.0F);
    EXPECT_LT(stopped->point.x, 250.0F);
    EXPECT_TRUE(index->hasLineOfSight(stopped->origin, stopped->point));
    const auto capped = queryThrownIlluminationLanding({100, 200}, {800, 200}, kTestSpace, kUse, *index);
    ASSERT_TRUE(capped);
    EXPECT_FLOAT_EQ(capped->point.x, 550.0F);
    EXPECT_FALSE(queryThrownIlluminationLanding({100, 100}, {101, 100}, kTestSpace, kUse, *index));
    EXPECT_FALSE(queryThrownIlluminationLanding({100, 100},
        {std::numeric_limits<float>::infinity(), 100}, kTestSpace, kUse, *index));
}

TEST(ThrownIlluminationTest, FlightAndLitDurationAdvanceAndRestoreAsOneEffect)
{
    const ThrownIlluminationLanding landing{{100, 100}, {200, 100}, kTestSpace, kUse, "test-flare"};
    ThrownIlluminationRuntime runtime;
    ASSERT_TRUE(runtime.start(landing));
    EXPECT_FALSE(runtime.start(landing));
    runtime.advance(0.0F);
    ASSERT_TRUE(runtime.flight());
    EXPECT_EQ(runtime.flight()->elapsed, 0.0);
    runtime.advance(0.20F);
    ASSERT_TRUE(runtime.flight());
    const auto inFlight = runtime.checkpoint();

    ThrownIlluminationRuntime restored;
    ASSERT_TRUE(restored.restore(inFlight));
    restored.advance(0.30F);
    EXPECT_FALSE(restored.flight());
    ASSERT_TRUE(restored.source());
    EXPECT_NEAR(restored.source()->elapsed, 0.15, 0.00001);
    EXPECT_EQ(restored.source()->landing.space, kTestSpace);
    EXPECT_FALSE(restored.start(landing));
    restored.markNightVisionGlareTriggered();
    const auto lit = restored.checkpoint();

    auto invalid = lit;
    invalid.flight = inFlight.flight;
    EXPECT_FALSE(restored.restore(invalid));
    EXPECT_TRUE(restored.source()->nightVisionGlareTriggered);
    invalid = lit;
    invalid.source->elapsed = kUse.durationSeconds;
    EXPECT_FALSE(restored.restore(invalid));

    ThrownIlluminationRuntime resumed;
    ASSERT_TRUE(resumed.restore(lit));
    ASSERT_TRUE(resumed.source()->nightVisionGlareTriggered);
    resumed.advance(19.8F);
    EXPECT_TRUE(resumed.active());
    resumed.advance(0.1F);
    EXPECT_FALSE(resumed.active());
    EXPECT_TRUE(resumed.expiredLastAdvance());
    resumed.advance(0.0F);
    EXPECT_FALSE(resumed.expiredLastAdvance());
    EXPECT_TRUE(resumed.start(landing));
}

TEST(ThrownIlluminationTest, CraftedStackUsesOrdinaryRaidDispatchAndOneLiveEffect)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("test-raid-flare", content);
    profile.tutorial = TutorialProgress::Complete;
    AssetInstanceId flare{};
    ASSERT_NO_FATAL_FAILURE(manufactureAndEquip(profile, flare));
    const auto inStash = storeMaterial(profile, content, kThrownIlluminationItemId);
    ASSERT_NE(inStash, 0U);
    EXPECT_FALSE(thrownIlluminationInChestRig(profile, content, inStash));

    raidline::test::TemporaryDirectory save;
    save.claim("raidline-thrown-illumination-", "ordinary-raid");
    ASSERT_TRUE(SaveRepository(save.path()).save(profile, content.contentVersion()).succeeded);
    GameSession session;
    session.configurePersistence(save.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    ASSERT_TRUE(session.deployAlpha(90819U)) << session.persistenceMessage();
    const auto center = session.playerGameplayContext().playerCenter;
    const auto before = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.throwThrowable(flare, center, {session.profile().revision, "too-close"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);

    std::optional<Vec2> aim;
    for (const Vec2 delta : std::array{Vec2{100, 0}, Vec2{-100, 0}, Vec2{0, 100}, Vec2{0, -100}})
    {
        const Vec2 point{center.x + delta.x, center.y + delta.y};
        if (session.world().previewThrownIllumination(point, kUse))
        {
            aim = point;
            break;
        }
    }
    ASSERT_TRUE(aim);
    const CommandContext first{session.profile().revision, "throw-test-flare"};
    EXPECT_FALSE(session.throwThrowable(inStash, *aim, first).succeeded);
    const auto thrown = session.throwThrowable(flare, *aim, first);
    ASSERT_TRUE(thrown.succeeded) << thrown.message;
    ASSERT_TRUE(session.world().thrownIllumination().flight());
    EXPECT_EQ(session.world().thrownIllumination().flight()->landing.effectIdentity, first.transactionId);
    EXPECT_EQ(session.profile().assets.find(flare)->quantity, 1U);
    EXPECT_FALSE(session.canSelectThrowable());
    EXPECT_FALSE(session.canSelectNoiseDecoy());
    EXPECT_TRUE(session.throwThrowable(flare, *aim, first).alreadyCommitted);

    session.update({}, kUse.flightSeconds);
    ASSERT_TRUE(session.world().thrownIllumination().source());
    EXPECT_TRUE(session.canSelectThrowable());
    const auto lit = profileStateFingerprint(session.profile());
    const auto second = session.throwThrowable(flare, *aim,
        {session.profile().revision, "throw-second-while-lit"});
    EXPECT_FALSE(second.succeeded);
    EXPECT_EQ(second.message, "ILLUMINATION ALREADY ACTIVE");
    EXPECT_EQ(profileStateFingerprint(session.profile()), lit);

    GameSession reopened;
    reopened.configurePersistence(save.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    EXPECT_FALSE(reopened.profile().pendingRaid);
    ASSERT_NE(reopened.profile().assets.find(flare), nullptr);
    EXPECT_EQ(reopened.profile().assets.find(flare)->quantity, 2U);
    EXPECT_FALSE(reopened.world().thrownIllumination().active());
}

TEST(ThrownIlluminationTest, BaseCoreRejectionAndCheckpointRetryPreserveTheSameThrow)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("test-base-flare", content);
    AssetInstanceId flare{};
    ASSERT_NO_FATAL_FAILURE(manufactureAndEquip(profile, flare));
    raidline::test::TemporaryDirectory save;
    save.claim("raidline-thrown-illumination-", "base-save-retry");
    SaveRepository repository{save.path()};
    ASSERT_TRUE(repository.save(profile, content.contentVersion()).succeeded);

    BaseWorld world;
    GameSession session;
    session.configurePersistence(save.path());
    ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
    static_cast<void>(session.updateBaseWorld(world, {}, 0.0F));
    ASSERT_TRUE(session.checkpointWorldClock()) << session.persistenceMessage();
    const auto durableBefore = repository.load(content);
    ASSERT_TRUE(durableBefore.profile) << durableBefore.message;
    ASSERT_NE(durableBefore.profile->assets.find(flare), nullptr);
    EXPECT_EQ(durableBefore.profile->assets.find(flare)->quantity, 2U);
    const auto durableBeforeFingerprint = profileStateFingerprint(*durableBefore.profile);
    ASSERT_EQ(world.playerSafetyZone(), HomeRegionSafetyZone::SafeCore);
    const auto core = session.playerGameplayContext().playerCenter;
    const auto before = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.throwThrowable(flare, {core.x + 120.0F, core.y},
        {session.profile().revision, "core-flare-rejected"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);

    const auto parcel = world.baseParcel();
    ASSERT_TRUE(world.restorePlayerBody({parcel.position.x - 180.0F,
        parcel.position.y + parcel.size.y * 0.5F}, {-1, 0}, 0.0F, 0.0F));
    const auto origin = session.playerGameplayContext().playerCenter;
    const Vec2 outside{origin.x - 150.0F, origin.y};
    const Vec2 inside{parcel.position.x + 25.0F, origin.y};
    EXPECT_FALSE(session.throwThrowable(flare, inside,
        {session.profile().revision, "into-core-rejected"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);

    const CommandContext throwCommand{session.profile().revision, "outside-flare"};
    ASSERT_TRUE(session.throwThrowable(flare, outside, throwCommand).succeeded) << session.persistenceMessage();
    ASSERT_TRUE(world.thrownIllumination().flight());
    EXPECT_EQ(session.profile().assets.find(flare)->quantity, 1U);
    static_cast<void>(session.updateBaseWorld(world, {}, .5F));
    ASSERT_TRUE(world.thrownIllumination().source());
    const double elapsed = world.thrownIllumination().source()->elapsed;

    const auto obstruction = save.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    EXPECT_FALSE(session.checkpointWorldClock());
    const auto unchanged = repository.load(content);
    ASSERT_TRUE(unchanged.profile) << unchanged.message;
    EXPECT_EQ(unchanged.status, durableBefore.status);
    EXPECT_EQ(profileStateFingerprint(*unchanged.profile), durableBeforeFingerprint);
    EXPECT_FALSE(unchanged.profile->committedTransactions.contains(throwCommand.transactionId));
    const auto stagedFingerprint = profileStateFingerprint(session.profile());
    EXPECT_TRUE(session.throwThrowable(flare, outside, throwCommand).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(session.profile()), stagedFingerprint);
    ASSERT_NE(session.profile().assets.find(flare), nullptr);
    EXPECT_EQ(session.profile().assets.find(flare)->quantity, 1U);
    ASSERT_TRUE(world.thrownIllumination().source());
    EXPECT_DOUBLE_EQ(world.thrownIllumination().source()->elapsed, elapsed);

    ASSERT_TRUE(std::filesystem::remove(obstruction));
    ASSERT_TRUE(session.checkpointWorldClock()) << session.persistenceMessage();
    const auto saved = repository.load(content);
    ASSERT_TRUE(saved.profile) << saved.message;
    EXPECT_EQ(saved.status, durableBefore.status);
    EXPECT_EQ(profileStateFingerprint(*saved.profile), stagedFingerprint);
    BaseWorld restoredWorld;
    GameSession restored;
    restored.configurePersistence(save.path());
    ASSERT_TRUE(restored.continueProfile()) << restored.persistenceMessage();
    ASSERT_TRUE(restored.restoreBasePlayerRuntime(restoredWorld));
    ASSERT_TRUE(restoredWorld.thrownIllumination().source());
    EXPECT_EQ(restoredWorld.thrownIllumination().source()->landing.effectIdentity, throwCommand.transactionId);
    EXPECT_DOUBLE_EQ(restoredWorld.thrownIllumination().source()->elapsed, elapsed);
    ASSERT_NE(restored.profile().assets.find(flare), nullptr);
    EXPECT_EQ(restored.profile().assets.find(flare)->quantity, 1U);
    EXPECT_TRUE(restored.throwThrowable(flare, outside, throwCommand).alreadyCommitted);
    restoredWorld.configureSite("regional_base_site.ashworks_logistics_yard");
    EXPECT_FALSE(restoredWorld.thrownIllumination().active());
}
