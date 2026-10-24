#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

#include "app.h"
#include "game_session.h"
#include "inventory_domain.h"
#include "ui_base_deployment_presentation.h"
#include "ui_raid_result_presentation.h"

struct RaidCredentialSessionTestAccess
{
    static auto &flow(App &app) { return app.gameFlow_; }
    static auto &session(App &app) { return app.gameSession_; }
    static void pause(App &app) { app.pauseMenu_.open(); }
    static void update(App &app) { app.update(0.0F); }
};

namespace
{
const MapDefinitionId frontier{"map.raid.frontier_exchange"};
const RaidExtractionPermissionDefinitionId permit{"raid_permission.frontier_exchange.freight_exit"};

struct TemporaryProfile
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-credential-session-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryProfile() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void position(GameSession &session, Vec2 center)
{
    const float half = session.world().player().size() * 0.5F;
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition(
        {center.x - half, center.y - half}));
}

void clearActiveEnemies(GameSession &session)
{
    for (const auto &enemy : session.world().enemies())
        static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(100000));
    session.update({}, 0);
}

void enterOffice(GameSession &session)
{
    ASSERT_TRUE(session.profile().pendingRaid);
    const auto &rooms = session.profile().pendingRaid->interiors;
    const auto office = std::find_if(rooms.begin(), rooms.end(), [](const auto &room) {
        return room.id == RaidSpaceDefinitionId{"raid_space.frontier_exchange.office"};
    });
    ASSERT_NE(office, rooms.end());
    const auto room = *office;
    const auto entrance = room.exteriorEntrance;
    clearActiveEnemies(session);
    position(session, {entrance.position.x + entrance.size.x * 0.5F,
        entrance.position.y + entrance.size.y * 0.5F});
    GameplayInput enter; enter.interactJustPressed = true;
    session.update(enter, 0);
    ASSERT_EQ(session.world().activeRaidSpaceId(), room.id);
    clearActiveEnemies(session);
    const auto point = session.profile().pendingRaid->credentialExtraction->definition.investigationPoint;
    position(session, {point.x - 40.0F, point.y});
    GameplayInput look; look.aimWorldPosition = point;
    session.update(look, 0);
    const auto currentAim = session.world().weaponAimWorldPosition();
    look.aimMotionDelta = Vec2{point.x - currentAim.x, point.y - currentAim.y};
    session.update(look, 1.0F / 60.0F);
    ASSERT_NEAR(session.world().weaponAimWorldPosition().x, point.x, 0.01F);
    ASSERT_NEAR(session.world().weaponAimWorldPosition().y, point.y, 0.01F);
    ASSERT_TRUE(session.world().credentialInvestigationInRange());
}

void investigate(GameSession &session)
{
    enterOffice(session);
    GameplayInput hold; hold.interactPressed = true;
    hold.aimWorldPosition = session.profile().pendingRaid->credentialExtraction->definition.investigationPoint;
    session.update(hold, 2.0F);
    ASSERT_TRUE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    EXPECT_FALSE(session.profile().raidExtractionPermissions.contains(permit));
    EXPECT_FALSE(session.world().credentialExtractionUnlocked());
}

void extractNormally(GameSession &session)
{
    const auto roomId = session.world().activeRaidSpaceId();
    const auto &rooms = session.profile().pendingRaid->interiors;
    const auto room = std::find_if(rooms.begin(), rooms.end(), [&](const auto &r) { return r.id == roomId; });
    if (room != rooms.end())
    {
        const auto exit = room->interiorExit;
        position(session, {exit.position.x + exit.size.x * 0.5F, exit.position.y + exit.size.y * 0.5F});
        GameplayInput leave; leave.interactJustPressed = true;
        session.update(leave, 0);
    }
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    const auto exit = session.world().extractionPoint().bounds();
    position(session, {exit.position.x + exit.size.x * 0.5F, exit.position.y + exit.size.y * 0.5F});
    session.update({}, 3.1F);
}
}

TEST(RaidCredentialSessionTest, RealInvestigationSettlesThenReloadsAndUsesNewHighRiskRoute)
{
    TemporaryProfile temporary;
    GameSession session; session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.startNewProfile("credential-complete-loop"));
    ASSERT_TRUE(session.deployAlpha(9917, frontier));
    investigate(session);
    extractNormally(session);
    ASSERT_EQ(session.state(), GameSessionState::BetweenRaids);
    ASSERT_TRUE(session.profile().raidExtractionPermissions.contains(permit));
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_EQ(session.profile().lastRaidResult->unlockedExtractionPermissions,
        std::vector<RaidExtractionPermissionDefinitionId>{permit});

    GameSession resumed; resumed.configurePersistence(temporary.path);
    ASSERT_TRUE(resumed.continueProfile());
    ASSERT_TRUE(resumed.profile().raidExtractionPermissions.contains(permit));
    EXPECT_TRUE(resumed.profile().raidIntelligence.counts.empty());
    EXPECT_TRUE(resumed.profile().raidInteriorIntelligence.knownLayouts.empty());
    // Isolated fixture gear: verify the real service ignores the light-exit
    // limit for this route, without adding a gameplay grant or test button.
    auto &heavy = const_cast<ProfileState &>(resumed.profile());
    for (const auto &[id, slot] : std::vector<std::pair<const char *, EquipmentSlotKind>>{
        {"item.weapon.lmg_7_62x51_service", EquipmentSlotKind::PrimaryWeapon},
        {"item.protective_gear.body_armor_heavy", EquipmentSlotKind::BodyArmor},
        {"item.protective_gear.helmet_heavy", EquipmentSlotKind::Helmet}})
        static_cast<void>(heavy.assets.create(publishedContentRegistry().item(ItemDefinitionId{id}),
            EquippedAssetLocation{slot}));
    ASSERT_TRUE(validateProfileState(heavy, publishedContentRegistry()).valid);
    ASSERT_TRUE(resumed.deployAlpha(9918, frontier));
    EXPECT_GT(resumed.currentRaidCarriedWeightGrams(), 22000U);
    EXPECT_FALSE(resumed.conditionalExtractionEligible());
    EXPECT_TRUE(resumed.world().credentialExtractionUnlocked());
    ASSERT_TRUE(resumed.world().credentialExtractionPoint());
    clearActiveEnemies(resumed);
    const auto exit = resumed.world().credentialExtractionPoint()->bounds();
    position(resumed, {exit.position.x + exit.size.x * 0.5F, exit.position.y + exit.size.y * 0.5F});
    resumed.update({}, 0);
    EXPECT_EQ(resumed.world().raidSession().state(), RaidSessionState::InRaid);
    ASSERT_TRUE(resumed.world().triggerHighRiskForDeveloper());
    resumed.update({}, 5.9F);
    ASSERT_EQ(resumed.world().raidSession().extractionRoute(), RaidExtractionRoute::Credential);
    EXPECT_FALSE(resumed.world().raidSession().signalCalledLastUpdate());
    resumed.update({}, 0.2F);
    EXPECT_EQ(resumed.state(), GameSessionState::BetweenRaids);
    EXPECT_TRUE(resumed.profile().raidExtractionPermissions.contains(permit));
    ASSERT_TRUE(resumed.profile().lastRaidResult);
    EXPECT_TRUE(resumed.profile().lastRaidResult->unlockedExtractionPermissions.empty());
}

TEST(RaidCredentialSessionTest, RealMenuInterruptionResetsInvestigationAndDoesNotGrantAnything)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("credential-menu"));
    ASSERT_TRUE(session.deployAlpha(9917, frontier));
    enterOffice(session);
    GameplayInput hold; hold.interactPressed = true;
    session.update(hold, 1);
    EXPECT_GT(session.world().credentialInvestigationProgress(), 0.0F);
    hold.inventoryOpen = true;
    session.update(hold, 3);
    EXPECT_FLOAT_EQ(session.world().credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    hold.inventoryOpen = false;
    session.update(hold, 1);
    EXPECT_FALSE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    session.update(hold, 1);
    EXPECT_TRUE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    EXPECT_FALSE(session.profile().raidExtractionPermissions.contains(permit));
}

TEST(RaidCredentialSessionTest, InvestigatedButAbnormalExitRestoresExactPreRaidProfile)
{
    TemporaryProfile temporary;
    GameSession session; session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.startNewProfile("credential-abnormal"));
    const auto before = profileStateFingerprint(session.profile());
    ASSERT_TRUE(session.deployAlpha(9917, frontier));
    investigate(session);
    GameSession reopened; reopened.configurePersistence(temporary.path);
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), before);
    EXPECT_FALSE(reopened.profile().pendingRaid);
    EXPECT_FALSE(reopened.profile().raidExtractionPermissions.contains(permit));
}

TEST(RaidCredentialSessionTest, InvestigatedThenDeathOrActiveQuitNeverRegisters)
{
    for (const bool death : {false, true})
    {
        GameSession session;
        ASSERT_TRUE(session.startNewProfile(death ? "credential-death" : "credential-quit"));
        ASSERT_TRUE(session.deployAlpha(9917, frontier));
        investigate(session);
        GameplayInput input;
        if (death) EXPECT_TRUE(const_cast<GameplayWorld &>(session.world()).damagePlayer(10000));
        else input.quitRaidJustPressed = true;
        session.update(input, 0);
        EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
        EXPECT_FALSE(session.profile().raidExtractionPermissions.contains(permit));
        ASSERT_TRUE(session.profile().lastRaidResult);
        EXPECT_TRUE(session.profile().lastRaidResult->unlockedExtractionPermissions.empty());
    }
}

TEST(RaidCredentialSessionTest, SaveFailureRetainsPendingFactAndRetryRegistersExactlyOnce)
{
    TemporaryProfile temporary;
    GameSession session; session.configurePersistence(temporary.path);
    ASSERT_TRUE(session.startNewProfile("credential-save-failure"));
    ASSERT_TRUE(session.deployAlpha(9917, frontier));
    investigate(session);
    const auto blocking = temporary.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blocking));
    extractNormally(session);
    ASSERT_EQ(session.state(), GameSessionState::SettlementBlocked);
    EXPECT_FALSE(session.profile().raidExtractionPermissions.contains(permit));
    ASSERT_TRUE(session.profile().pendingRaid);
    EXPECT_TRUE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    const auto beforeRetry = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(profileStateFingerprint(session.profile()), beforeRetry);
    ASSERT_TRUE(std::filesystem::remove(blocking));
    session.update({}, 0);
    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    ASSERT_TRUE(session.profile().raidExtractionPermissions.contains(permit));
    const auto after = profileStateFingerprint(session.profile());
    session.update({}, 0);
    EXPECT_EQ(profileStateFingerprint(session.profile()), after);
}

TEST(RaidCredentialSessionTest, ResultAndBriefingShowPermanentProgressWithoutGrantingIntelligence)
{
    const auto &content = publishedContentRegistry();
    auto profile = makeNewAlphaProfile("credential-display", content);
    const auto initial = profileStateFingerprint(profile);
    auto p = projectUiDeployment(profile, content, UiLanguage::SimplifiedChinese,
        frontier, {}, {}, 0, {});
    ASSERT_TRUE(p.credentialUnlocked);
    EXPECT_FALSE(*p.credentialUnlocked);
    EXPECT_EQ(profileStateFingerprint(profile), initial);
    profile.raidExtractionPermissions.insert(permit);
    const auto next = projectUiDeployment(profile, content, UiLanguage::SimplifiedChinese,
        frontier, {}, {}, 0, {});
    EXPECT_TRUE(*next.credentialUnlocked);
    EXPECT_NE(p.contextKey, next.contextKey);
    EXPECT_TRUE(profile.raidIntelligence.counts.empty());
    EXPECT_TRUE(profile.raidInteriorIntelligence.knownLayouts.empty());
    LastRaidResult result;
    result.settlementId = "credential-ui-result";
    result.outcome = RaidResultOutcome::Extracted;
    result.unlockedExtractionPermissions = {permit};
    const auto display = projectUiRaidResult(profile.profileId, result, content,
        UiLanguage::SimplifiedChinese, {}, false);
    EXPECT_NE(std::find(display.summaryLines.begin(), display.summaryLines.end(),
        "边境交换站通行许可已登记"), display.summaryLines.end());
    for (const auto *key : {"TRANSIT DOCUMENTS | HOLD F 2s", "CREDENTIAL EXIT | PERMISSION REQUIRED",
        "CREDENTIAL EXIT | WAIT FOR HIGH RISK", "CREDENTIAL EXIT | 6s | NO WEIGHT LIMIT"})
        EXPECT_NE(localizeUiText(UiLanguage::SimplifiedChinese, key), key);
}

TEST(RaidCredentialSessionTest, LooseBatteryKeepsFPickupPriorityAtDocumentOverlap)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("credential-f-priority"));
    auto &initial = const_cast<ProfileState &>(session.profile());
    const auto bag = std::find_if(initial.assets.records().begin(), initial.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == ItemDefinitionId{"item.container.backpack_small"}; });
    ASSERT_NE(bag, initial.assets.records().end());
    ASSERT_TRUE(executeInventory(initial, publishedContentRegistry(),
        InventoryEquipCommand{bag->first, EquipmentSlotKind::Backpack},
        {initial.revision, "credential-priority-equip"}).succeeded);
    ASSERT_TRUE(session.deployAlpha(9917, frontier));
    enterOffice(session);
    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto snapshot = *profile.pendingRaid->credentialExtraction;
    const auto id = profile.assets.create(publishedContentRegistry().item(
        ItemDefinitionId{"item.battery.universal"}), RaidDroppedBatteryLocation{
        profile.pendingRaid->raidId, snapshot.definition.investigationSpaceId,
        snapshot.definition.investigationPoint});
    ASSERT_TRUE(validateProfileState(profile, publishedContentRegistry()).valid);
    GameplayInput pickup; pickup.interactJustPressed = pickup.interactPressed = true;
    session.update(pickup, 0.25F);
    ASSERT_TRUE(session.profile().assets.find(id));
    EXPECT_TRUE(assetIsCarried(session.profile(), id)) << session.persistenceMessage();
    EXPECT_FLOAT_EQ(session.world().credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
}

TEST(RaidCredentialSessionTest, RealAppPauseCancelsPartialInvestigationBeforeReturning)
{
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
    TemporaryProfile temporary;
    auto app = std::make_unique<App>();
    auto &flow = RaidCredentialSessionTestAccess::flow(*app);
    flow.configurePersistence(temporary.path);
    ASSERT_TRUE(flow.startNewGame("credential-real-pause"));
    ASSERT_TRUE(flow.deploy(frontier));
    auto &session = RaidCredentialSessionTestAccess::session(*app);
    enterOffice(session);
    GameplayInput hold; hold.interactPressed = true;
    session.update(hold, 1.8F);
    ASSERT_GT(session.world().credentialInvestigationProgress(), 0.5F);
    RaidCredentialSessionTestAccess::pause(*app);
    RaidCredentialSessionTestAccess::update(*app);
    EXPECT_FLOAT_EQ(session.world().credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(session.profile().pendingRaid->credentialExtraction->investigationCompleted);
    app.reset();
    SDL_Quit();
}
