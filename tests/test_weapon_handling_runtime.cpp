#include "weapon_handling_fixture.h"
#include "test_temp_directory.h"
#include <gtest/gtest.h>
#include <array>
#include <fstream>

TEST(WeaponHandlingRuntimeTest, ThreeRoundBurstConsumesRealShotsAndHeldDoesNotRestart)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon, WeaponFireMode::ThreeRoundBurst}, "burst-mode").succeeded);
    session.update({}, 0); // Both fire sources released after the mode change.
    const auto before = magazineRoundCount(session.profile(), ids.oldMagazine);
    GameplayInput fire; fire.firePressed = fire.fireJustPressed = true;
    session.update(fire, 0); ASSERT_TRUE(session.world().shotFiredLastUpdate());
    fire.fireJustPressed = false;
    session.update(fire, 0.2F); ASSERT_TRUE(session.world().shotFiredLastUpdate());
    session.update(fire, 0.2F); ASSERT_TRUE(session.world().shotFiredLastUpdate());
    session.update(fire, 0.2F); EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), before - 3U);
    session.update({}, 0.2F);
    fire.fireJustPressed = true; session.update(fire, 0); EXPECT_TRUE(session.world().shotFiredLastUpdate());
    session.update({}, 0.2F); session.update(fire = GameplayInput{}, 0.2F);
    // A click now completes its three-shot group after the button releases.
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), before - 6U);
}

TEST(WeaponHandlingRuntimeTest, ModeSwitchPreservesCadenceSpreadRandomAndRecoilAndRequiresRelease)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    GameplayInput fire; fire.firePressed = fire.fireJustPressed = true;
    session.update(fire, 0); ASSERT_TRUE(session.world().shotFiredLastUpdate());
    const auto before = WeaponHandlingTestAccess::shooting(session.world());
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon, WeaponFireMode::SemiAutomatic}, "mode-preserve").succeeded);
    const auto after = WeaponHandlingTestAccess::shooting(session.world());
    EXPECT_EQ(after.fireState, before.fireState);
    EXPECT_EQ(after.spreadRandomState, before.spreadRandomState);
    EXPECT_EQ(after.aimVectors, before.aimVectors);
    EXPECT_EQ(after.recoilRandomState, before.recoilRandomState);
    GameplayInput change = fire; change.cycleFireModeJustPressed = true;
    session.update(change, 0);
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    change.fireJustPressed = change.cycleFireModeJustPressed = false;
    session.update(change, 0.2F); EXPECT_FALSE(session.world().shotFiredLastUpdate());
    session.update({}, 0); session.update(fire, 0); EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(session.weaponFireMode(ids.weapon), WeaponFireMode::ThreeRoundBurst);
}

TEST(WeaponHandlingRuntimeTest, PhysicalStagesCommitAtBoundariesAndInterruptionNeverReinstallsOldMagazine)
{
    for (float stop : {0.199F, 0.2F, 0.599F, 0.6F, 1.199F, 1.2F, 1.599F, 1.6F})
    {
        SCOPED_TRACE(stop); GameSession session; const auto ids = prepareHandlingSession(session);
        ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine));
        session.update({}, stop);
        const auto installed = installedMagazine(session.profile(), ids.weapon);
        if (stop < 0.6F) EXPECT_EQ(installed, ids.oldMagazine);
        else if (stop < 1.2F) EXPECT_FALSE(installed);
        else EXPECT_EQ(installed, ids.spareMagazine);
        GameplayInput sprint; sprint.sprint = true;
        session.update(sprint, 1.0F); // This frame must not submit later stages.
        EXPECT_FALSE(session.raidActionState().active());
        EXPECT_EQ(installedMagazine(session.profile(), ids.weapon), installed);
        EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), 9U);
        EXPECT_EQ(magazineRoundCount(session.profile(), ids.spareMagazine), 20U);
        for (const auto &[id, asset] : session.profile().assets.records())
        { static_cast<void>(id); EXPECT_FALSE(std::holds_alternative<RaidReloadHeldMagazineLocation>(asset.location)); }
        EXPECT_TRUE(validateProfileState(session.profile(), publishedContentRegistry()).valid);
    }
}

TEST(WeaponHandlingRuntimeTest, EmergencyUpgradePreservesElapsedAndDroppedIdentityAndCanPickup)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine));
    session.update({}, 0.22F);
    ASSERT_TRUE(session.upgradeAlphaReloadEmergency(ids.weapon));
    EXPECT_FLOAT_EQ(std::get<ReloadRaidAction>(*session.raidActionState().active()).elapsedSeconds, 0.22F);
    session.update({}, 0.13F);
    const auto *drop = std::get_if<RaidDroppedMagazineLocation>(&session.profile().assets.find(ids.oldMagazine)->location);
    ASSERT_NE(drop, nullptr); const auto frozen = *drop;
    EXPECT_EQ(session.droppedRaidMagazineIds(), std::vector<AssetInstanceId>{ids.oldMagazine});
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), 9U);
    session.update({}, 0.85F);
    EXPECT_FALSE(session.raidActionState().active());
    EXPECT_EQ(installedMagazine(session.profile(), ids.weapon), ids.spareMagazine);
    EXPECT_EQ(std::get<RaidDroppedMagazineLocation>(session.profile().assets.find(ids.oldMagazine)->location), frozen);
    GameplayInput pickup; pickup.interactJustPressed = true;
    session.update(pickup, 0);
    EXPECT_TRUE(assetIsCarried(session.profile(), ids.oldMagazine));
    EXPECT_TRUE(session.droppedRaidMagazineIds().empty());
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), 9U);
}

TEST(WeaponHandlingRuntimeTest, EmptyChamberNeedsExplicitHalfSecondActionAndInterruptDoesNotTakeRound)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    WeaponHandlingTestAccess::profile(session).assets.findMutable(ids.weapon)->chamberedRound.reset();
    const auto count = magazineRoundCount(session.profile(), ids.oldMagazine);
    GameplayInput fire; fire.firePressed = fire.fireJustPressed = true;
    session.update(fire, 0);
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_FALSE(session.profile().assets.find(ids.weapon)->chamberedRound);
    session.update(handlingReloadIntent(ids.weapon), 0);
    ASSERT_TRUE(std::holds_alternative<ChamberRaidAction>(*session.raidActionState().active()));
    session.update({}, 0.499F);
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), count);
    GameplayInput interrupt; interrupt.inventoryOpen = true; session.update(interrupt, 0.1F);
    EXPECT_FALSE(session.profile().assets.find(ids.weapon)->chamberedRound);
    ASSERT_TRUE(session.startAlphaChamber(ids.weapon)); session.update({}, 0.5F);
    EXPECT_TRUE(session.profile().assets.find(ids.weapon)->chamberedRound);
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), count - 1U);
}

TEST(WeaponHandlingRuntimeTest, SameFrameReleaseAndEmergencyPressUseFirstReleaseAge)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    auto input = handlingReloadIntent(ids.weapon, RaidReloadInputKind::Automatic, std::nullopt, 0.10F);
    input.raidReloadIntents.push_back({RaidReloadInputKind::UpgradeEmergency, ids.weapon, std::nullopt, 0.01F,
        input.raidReloadIntents.front().gestureId, 0.09});
    session.update(input, 0.5F);
    const auto &action = std::get<ReloadRaidAction>(*session.raidActionState().active());
    EXPECT_FLOAT_EQ(action.elapsedSeconds, 0.10F); EXPECT_EQ(action.plan.kind, RaidReloadKind::Emergency);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(session.profile().assets.find(ids.spareMagazine)->location));
}

TEST(WeaponHandlingRuntimeTest, DeathAndQuitRetainBothHeldMagazinesWithWeaponRatherThanDropping)
{
    for (auto outcome : {RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit})
    {
        GameSession session; const auto ids = prepareHandlingSession(session);
        ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine)); session.update({}, 0.6F);
        ASSERT_FALSE(installedMagazine(session.profile(), ids.weapon));
        ASSERT_TRUE(WeaponHandlingTestAccess::settle(session, outcome)) << session.persistenceMessage();
        EXPECT_TRUE(std::holds_alternative<LostRaidAssetLocation>(session.profile().assets.find(ids.weapon)->location));
        for (auto id : {ids.oldMagazine, ids.spareMagazine})
        {
            const auto *retained = std::get_if<RetainedRaidMagazineLocation>(&session.profile().assets.find(id)->location);
            ASSERT_NE(retained, nullptr); EXPECT_EQ(retained->weaponAssetId, ids.weapon);
        }
        EXPECT_TRUE(validateProfileState(session.profile(), publishedContentRegistry()).valid);
    }
}

TEST(WeaponHandlingRuntimeTest, FailedLossSavePreservesHeldOwnershipAndRetrySettlesExactlyOnce)
{
    raidline::test::TemporaryDirectory directory;
    directory.claim("raidline-weapon-handling-", "failed-loss-save");
    GameSession session; const auto ids = prepareHandlingSession(session);
    const auto blocked = directory.path() / "blocked-repository";
    { std::ofstream obstruction(blocked); obstruction << "not a directory"; }
    session.configurePersistence(blocked);
    ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine)); session.update({}, 0.6F);
    const auto before = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.activeQuitAlphaRaid());
    EXPECT_EQ(profileStateFingerprint(session.profile()), before);
    ASSERT_TRUE(session.raidActionState().active());
    for (auto id : {ids.oldMagazine, ids.spareMagazine})
        EXPECT_TRUE(std::holds_alternative<RaidReloadHeldMagazineLocation>(session.profile().assets.find(id)->location));
    ASSERT_TRUE(std::filesystem::remove(blocked));
    ASSERT_TRUE(std::filesystem::create_directory(blocked));
    ASSERT_TRUE(session.activeQuitAlphaRaid()) << session.persistenceMessage();
    const auto settled = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.activeQuitAlphaRaid());
    EXPECT_EQ(profileStateFingerprint(session.profile()), settled);
    EXPECT_EQ(session.profile().lostRaidRecords.size(), 1U);
}

TEST(WeaponHandlingRuntimeTest, PortalTransferResolvesHeldMagazinesInThePreviousSpace)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    const auto &map = publishedContentRegistry().map(MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_FALSE(map.interiors.empty());
    auto arena = handlingArena();
    RaidInteriorWorldConfig interior;
    interior.id = map.interiors.front().id; interior.displayName = "weapon-handling-test-room";
    interior.worldSize = map.interiors.front().worldSize;
    interior.exteriorEntrance = {{1180, 760}, {100, 100}};
    interior.exteriorReturn = {1200, 784}; interior.interiorSpawn = {100, 100};
    interior.interiorExit = {{80, 80}, {100, 100}};
    arena.interiors.push_back(interior);
    WeaponHandlingTestAccess::world(session, arena);
    ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine)); session.update({}, 0.6F);
    ASSERT_FALSE(installedMagazine(session.profile(), ids.weapon));
    GameplayInput enter; enter.interactJustPressed = true;
    session.update(enter, 0);
    EXPECT_TRUE(session.world().spaceTransitionedLastUpdate());
    EXPECT_EQ(session.world().activeRaidSpaceId(), interior.id);
    EXPECT_FALSE(session.raidActionState().active());
    EXPECT_FALSE(installedMagazine(session.profile(), ids.weapon));
    for (auto id : {ids.oldMagazine, ids.spareMagazine})
        EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(session.profile().assets.find(id)->location));
    EXPECT_TRUE(validateProfileState(session.profile(), publishedContentRegistry()).valid);
}

TEST(WeaponHandlingRuntimeTest, ExistingDeathIntentPreventsThisFramesUncommittedTakeStage)
{
    GameSession session; const auto ids = prepareHandlingSession(session);
    ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine)); session.update({}, 0.199F);
    WeaponHandlingTestAccess::dead(session);
    session.update({}, 0.5F);
    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_EQ(installedMagazine(session.profile(), ids.weapon), ids.oldMagazine);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(session.profile().assets.find(ids.spareMagazine)->location));
    EXPECT_FALSE(std::holds_alternative<RetainedRaidMagazineLocation>(session.profile().assets.find(ids.spareMagazine)->location));
    EXPECT_EQ(magazineRoundCount(session.profile(), ids.spareMagazine), 20U);
}

TEST(WeaponHandlingRuntimeTest, ReloadFireInterruptionResumesHeldAutomaticWithoutLeavingPendingBurst)
{
    for (auto mode : {WeaponFireMode::Automatic, WeaponFireMode::ThreeRoundBurst})
    {
        GameSession session; const auto ids = prepareHandlingSession(session);
        ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon, mode}, "interrupt-fire-mode").succeeded);
        session.update({}, 0);
        ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine)); session.update({}, 0.2F);
        GameplayInput fire; fire.firePressed = fire.fireJustPressed = true;
        session.update(fire, 0);
        ASSERT_FALSE(session.raidActionState().active());
        EXPECT_FALSE(session.world().shotFiredLastUpdate());
        const auto rounds = magazineRoundCount(session.profile(), ids.oldMagazine);
        fire.fireJustPressed = false; session.update(fire, 0.2F);
        EXPECT_EQ(session.world().shotFiredLastUpdate(), mode == WeaponFireMode::Automatic);
        EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine),
            rounds - (mode == WeaponFireMode::Automatic ? 1U : 0U));
    }
}

TEST(WeaponHandlingRuntimeTest, HeldMeleeAndRecoveryEndingThisFrameDoNotBlockFreshTriggerOrReload)
{
    for (auto mode : {WeaponFireMode::SemiAutomatic, WeaponFireMode::ThreeRoundBurst, WeaponFireMode::Automatic})
    {
        GameSession session; const auto ids = prepareHandlingSession(session);
        ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon, mode}, "recovery-fire-mode").succeeded);
        session.update({}, 0);
        ASSERT_TRUE(WeaponHandlingTestAccess::restoreMeleeRecovery(session.world(), 0.05F));
        const auto before = magazineRoundCount(session.profile(), ids.oldMagazine);
        GameplayInput fire; fire.meleePressed = true;
        fire.firePressed = fire.fireJustPressed = true;
        session.update(fire, 0.05F);
        EXPECT_FLOAT_EQ(session.world().melee().recoverySeconds(), 0);
        ASSERT_TRUE(session.world().shotFiredLastUpdate());
        EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), before - 1U);
        GameplayInput released; released.meleePressed = true; session.update(released, 0.2F);
        auto reload = handlingReloadIntent(ids.weapon); reload.meleePressed = true;
        session.update(reload, 0);
        ASSERT_TRUE(session.raidActionState().active());
        session.update(released, 0.2F);
        ASSERT_TRUE(session.raidActionState().active());
        EXPECT_TRUE(std::holds_alternative<RaidReloadHeldMagazineLocation>(session.profile().assets.find(ids.spareMagazine)->location));
    }
}
