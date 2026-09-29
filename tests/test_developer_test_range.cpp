#include "developer_test_range.h"
#include "developer_range_panel.h"
#include "game_flow.h"
#include "alpha_content_ids.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace {
const auto &content() { return publishedContentRegistry(); }
std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
}

TEST(DeveloperRangeTest, EveryPublishedWeaponAndAmmoCanEquipAValidatedDisposableKit)
{
    GameSession session;
    ASSERT_TRUE(session.startDeveloperRange());
    ASSERT_TRUE(session.developerRangeAtConsole());
    EXPECT_FALSE(session.profile().pendingRaid);
    for (const auto &weapon : developerRangeWeapons()) {
        const auto rounds = developerRangeAmmunition(weapon);
        ASSERT_FALSE(rounds.empty());
        for (const auto &ammo : rounds) {
            SCOPED_TRACE(std::string{weapon.value()} + " / " + std::string{ammo.value()});
            ASSERT_TRUE(session.grantDeveloperRangeLoadout(weapon, ammo, std::nullopt));
            EXPECT_TRUE(validateProfileState(session.profile(), content()).valid);
            ASSERT_TRUE(session.activeAlphaWeapon());
            const auto id = *session.activeAlphaWeapon();
            EXPECT_EQ(session.profile().assets.find(id)->chamberedRound->definitionId, ammo);
            const auto magazine = installedMagazine(session.profile(), id);
            ASSERT_TRUE(magazine);
            EXPECT_FALSE(session.profile().assets.find(*magazine)->magazineRounds.empty());
        }
    }
}

TEST(DeveloperRangeTest, IncompatibleKitAndFullBackpackRejectAtomically)
{
    GameSession session;
    ASSERT_TRUE(session.startDeveloperRange());
    ASSERT_TRUE(session.grantDeveloperRangeLoadout(alpha_content::rifle, alpha_content::ammunition, alpha_content::bodyArmor));
    auto before = profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.grantDeveloperRangeLoadout(alpha_content::rifle, alpha_content::medkit, std::nullopt));
    EXPECT_EQ(before, profileStateFingerprint(session.profile()));
    bool full{};
    for (int i = 0; i < 100; ++i) {
        before = profileStateFingerprint(session.profile());
        if (!session.grantDeveloperRangeItem(alpha_content::medkit)) {
            full = true;
            EXPECT_EQ(before, profileStateFingerprint(session.profile()));
            break;
        }
    }
    EXPECT_TRUE(full);
}

TEST(DeveloperRangeTest, FixedPadsReplaceTargetsWithNewIdentityAndRejectInvalidCounts)
{
    GameSession session;
    ASSERT_TRUE(session.startDeveloperRange());
    const EnemyCombatDefinitionId armored{"enemy.infected.armored"};
    for (std::size_t pad = 0; pad < 3; ++pad) {
        ASSERT_TRUE(session.resetDeveloperRangeEnemies(armored, pad, 3));
        ASSERT_EQ(session.world().enemies().size(), 3U);
        EXPECT_EQ(session.world().enemies()[0].position().x, kDeveloperRangePads[pad].x);
        EXPECT_EQ(session.world().enemies()[0].torsoArmor()->durability, 9U);
    }
    const auto oldId = session.world().enemies()[0].combatTargetId();
    ASSERT_TRUE(session.resetDeveloperRangeEnemies(ordinaryInfectedDefinitionId(), 0, 1));
    EXPECT_GT(session.world().enemies()[0].combatTargetId(), oldId);
    EXPECT_FALSE(session.world().enemies()[0].torsoArmor());
    EXPECT_FALSE(session.resetDeveloperRangeEnemies(armored, 3, 1));
    EXPECT_FALSE(session.resetDeveloperRangeEnemies(armored, 0, 6));
    ASSERT_EQ(session.world().enemies().size(), 1U);
    EXPECT_TRUE(session.resetDeveloperRangeEnemies(armored, 0, 0));
    EXPECT_TRUE(session.world().enemies().empty());
    GameplayWorld ordinary;
    EXPECT_FALSE(ordinary.resetDeveloperRangeEnemies(content().enemyCombatDefinition(armored), 0, 1));
}

TEST(DeveloperRangeTest, RealShootingConsumesAmmoAndConsoleDistanceControlsGrants)
{
    GameSession session;
    ASSERT_TRUE(session.startDeveloperRange());
    ASSERT_TRUE(session.grantDeveloperRangeLoadout(alpha_content::rifle, alpha_content::ammunition, std::nullopt));
    ASSERT_TRUE(session.resetDeveloperRangeEnemies(ordinaryInfectedDefinitionId(), 0, 1));
    const auto weapon = *session.activeAlphaWeapon();
    const auto magazine = *installedMagazine(session.profile(), weapon);
    const auto rounds = session.profile().assets.find(magazine)->magazineRounds.size();
    GameplayInput input;
    input.aimWorldPosition = {750, 440};
    input.fireJustPressed = true;
    session.update(input, 0.016F);
    EXPECT_LT(session.profile().assets.find(magazine)->magazineRounds.size(), rounds);
    // Reset clears live flights before new stable targets are installed.
    ASSERT_TRUE(session.resetDeveloperRangeEnemies(ordinaryInfectedDefinitionId(), 0, 1));
    EXPECT_TRUE(session.world().logicalBallistics().empty());
    input = {};
    input.moveRight = true;
    for (int i = 0; i < 30; ++i) session.update(input, 0.1F);
    EXPECT_FALSE(session.developerRangeAtConsole());
    EXPECT_FALSE(session.grantDeveloperRangeItem(alpha_content::medkit));
    EXPECT_FALSE(session.resetDeveloperRangeEnemies(ordinaryInfectedDefinitionId(), 0, 1));
}

TEST(DeveloperRangeTest, ExitAndActiveQuitRestoreExactOriginalSessionAndSaveBytes)
{
    const auto path = std::filesystem::temp_directory_path() / ("raidline-range-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        GameFlow flow;
        flow.configurePersistence(path);
        ASSERT_TRUE(flow.startNewGame("range-isolation"));
        const auto before = profileStateFingerprint(flow.gameSession().profile());
        SaveRepository repository(path);
        const auto bytes = read(repository.primaryPath());
        auto *address = &flow.gameSession();
        auto *originalWorld = &flow.gameSession().world();
        for (bool quit : {false, true}) {
            ASSERT_TRUE(flow.enterDeveloperRange());
            EXPECT_EQ(&flow.gameSession(), address);
            EXPECT_FALSE(flow.gameSession().hasSavedProfile());
            ASSERT_TRUE(flow.gameSession().grantDeveloperRangeLoadout(alpha_content::rifle, alpha_content::ammunition, std::nullopt));
            if (quit) { GameplayInput input; input.quitRaidJustPressed = true; flow.update(input, 0.1F); }
            else EXPECT_TRUE(flow.leaveDeveloperRange());
            EXPECT_EQ(flow.state(), GameFlowState::Base);
            EXPECT_EQ(&flow.gameSession().world(), originalWorld);
            EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
            EXPECT_EQ(read(repository.primaryPath()), bytes);
        }
        ASSERT_TRUE(flow.enterDeveloperRange());
        ASSERT_TRUE(flow.returnToMainMenu());
        EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
        EXPECT_EQ(read(repository.primaryPath()), bytes);
    }
    std::filesystem::remove_all(path);
}

TEST(DeveloperRangeTest, RangeControlsHaveDisjointClickableBounds)
{
    for (int i = 0; i < 20; ++i) {
        const auto action = static_cast<RangePanelAction>(i);
        const auto bounds = rangePanelButton(action);
        EXPECT_EQ(rangePanelActionAt({bounds.x+2, bounds.y+2}), action);
    }
    EXPECT_FALSE(rangePanelActionAt({0, 0}));
    EXPECT_EQ(developerPanelActionAt({680, 120}, 25)->kind, DeveloperPanelActionKind::EnterTestRange);
}

TEST(DeveloperRangeTest, DeathReturnsToBaseWithoutAResultAndProductionRaidRejectsEntry)
{
    GameFlow flow;
    ASSERT_TRUE(flow.startNewGame("range-death"));
    const auto before = profileStateFingerprint(flow.gameSession().profile());
    ASSERT_TRUE(flow.enterDeveloperRange());
    ASSERT_TRUE(flow.gameSession().world().damagePlayer(1000));
    flow.update({}, 0.016F);
    EXPECT_EQ(flow.state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), before);
    EXPECT_FALSE(flow.gameSession().profile().lastRaidResult);
    ASSERT_TRUE(flow.deploy());
    const auto raid = profileStateFingerprint(flow.gameSession().profile());
    EXPECT_FALSE(flow.enterDeveloperRange());
    EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()), raid);
}
