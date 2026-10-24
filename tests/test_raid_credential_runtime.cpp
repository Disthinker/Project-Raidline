#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "gameplay_world.h"
#include "raid_map_generation.h"
#include "raid_lifecycle.h"
#include "raid_session.h"
#include "raid_tactical_map.h"

namespace
{
RaidSessionConfig credentialSessionConfig()
{
    return {0.0F, 3.0F, false, {true, 1.0F, 12.0F, 4.0F}, 6.0F};
}

RaidWorldConfig credentialWorldConfig(bool unlocked = false)
{
    RaidWorldConfig config;
    config.worldSize = {1280.0F, 720.0F};
    config.playerSpawn = {100.0F, 100.0F};
    config.extractionPoint = {{1000.0F, 560.0F}, {100.0F, 100.0F}};
    config.highRisk = {
        true, 1000.0F, {{800.0F, 40.0F}, {100.0F, 100.0F}}, 12.0F,
        9999.0F, 9999.0F, 1U, 1U,
        {EnemySpawn{{1200.0F, 650.0F}, {50.0F, 50.0F}, 3}},
        {{900.0F, 300.0F}, {100.0F, 100.0F}}, 2.0F,
        {{1000.0F, 350.0F}, {100.0F, 100.0F}}, 3U,
        {{700.0F, 500.0F}, {100.0F, 100.0F}}, 4.0F, 22000U};
    RaidInteriorWorldConfig office;
    office.id = RaidSpaceDefinitionId{"raid_space.test.credential_office"};
    office.displayName = "Credential office";
    office.worldSize = {800.0F, 600.0F};
    office.exteriorEntrance = {{80.0F, 80.0F}, {100.0F, 100.0F}};
    office.exteriorReturn = {200.0F, 100.0F};
    office.interiorSpawn = {200.0F, 200.0F};
    office.interiorExit = {{20.0F, 20.0F}, {60.0F, 60.0F}};
    config.interiors.push_back(office);
    RaidCredentialExtractionDefinition definition;
    definition.permissionId = RaidExtractionPermissionDefinitionId{
        "raid_permission.test.freight_exit"};
    definition.investigationPointId = "investigation.test.transit_documents";
    definition.investigationSpaceId = office.id;
    definition.investigationPoint = {265.0F, 225.0F};
    definition.extractionPoint = {{450.0F, 400.0F}, {100.0F, 100.0F}};
    config.credentialExtraction = RaidCredentialExtractionSnapshot{definition, unlocked};
    config.lightConeObservationEnabled = true;
    return config;
}

void enterOffice(GameplayWorld &world)
{
    GameplayInput enter;
    enter.interactJustPressed = true;
    enter.aimWorldPosition = Vec2{400.0F, 225.0F};
    world.update(enter, 0.0F);
    ASSERT_FALSE(world.inOutdoorRaidSpace());
}

GameplayInput holdInvestigation()
{
    GameplayInput input;
    input.interactPressed = true;
    input.aimWorldPosition = Vec2{400.0F, 225.0F};
    return input;
}

bool overlaps(ContentRect a, ContentRect b)
{
    return a.position.x < b.position.x + b.size.x &&
        a.position.x + a.size.x > b.position.x &&
        a.position.y < b.position.y + b.size.y &&
        a.position.y + a.size.y > b.position.y;
}

RaidMapGenerationAnchors credentialMapAnchors(const MapDefinition &map)
{
    RaidMapGenerationAnchors anchors;
    anchors.playerSpawn = map.spawnExtractionPairs.front().playerSpawn;
    anchors.extractionPoint = map.spawnExtractionPairs.front().extractionPoint;
    anchors.requests = {
        {std::string{kRaidAnchorPlayerSpawn}, RaidMapAnchorKind::PlayerSpawn, {50, 50}},
        {std::string{kRaidAnchorNormalExtraction}, RaidMapAnchorKind::NormalExtraction,
         anchors.extractionPoint.size},
        {std::string{kRaidAnchorEmergencyExtraction}, RaidMapAnchorKind::EmergencyExtraction,
         map.highRisk.emergencyExtractionPoint.size},
        {std::string{kRaidAnchorConditionalExtraction}, RaidMapAnchorKind::ConditionalExtraction,
         map.highRisk.conditionalExtractionPoint.size},
        {std::string{kRaidAnchorHighRiskControl}, RaidMapAnchorKind::HighRiskControl,
         map.highRisk.activationControlPoint.size},
        {std::string{kRaidAnchorAdvancedResource}, RaidMapAnchorKind::AdvancedResource,
         map.highRisk.advancedResourceArea.size},
        {std::string{kRaidAnchorRescue}, RaidMapAnchorKind::Rescue, {100, 100}},
    };
    const auto &deployment = publishedContentRegistry().enemyDeployment(map.raidEnemyDeploymentIds.front());
    for (std::size_t index = 0; index < deployment.enemies.size(); ++index)
        anchors.requests.push_back({raidIndexedAnchorId("enemy", index),
            RaidMapAnchorKind::Enemy, deployment.enemies[index].size});
    for (std::size_t index = 0; index < map.raidLootSlots.size(); ++index)
        anchors.requests.push_back({raidIndexedAnchorId("loot", index),
            RaidMapAnchorKind::Loot, {32, 32}});
    for (std::size_t index = 0; index < map.highRisk.pressureSpawns.size(); ++index)
        anchors.requests.push_back({raidIndexedAnchorId("pressure", index),
            RaidMapAnchorKind::PressureSpawn, map.highRisk.pressureSpawns[index].size});
    for (std::size_t index = 0; index < map.interiors.size(); ++index)
        anchors.requests.push_back({raidIndexedAnchorId("interior", index),
            RaidMapAnchorKind::InteriorEntrance, map.interiors[index].exteriorEntrance.size});
    anchors.requests.push_back({std::string{kRaidAnchorCredentialExtraction},
        RaidMapAnchorKind::CredentialExtraction, map.credentialExtraction->extractionPoint.size});
    return anchors;
}
}

TEST(RaidCredentialRuntimeTest, RouteRequiresHighRiskAndIndependentSixSeconds)
{
    RaidSession session{credentialSessionConfig()};
    ASSERT_TRUE(session.start());
    session.update(0.5F, false, false, false, false, true);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    session.update(0.5F, false, false, false, false, true);
    ASSERT_EQ(session.phase(), RaidPhase::HighRisk);
    session.update(5.99F, false, false, false, false, true);
    EXPECT_EQ(session.extractionRoute(), RaidExtractionRoute::Credential);
    EXPECT_EQ(session.state(), RaidSessionState::Extracting);
    EXPECT_FLOAT_EQ(session.extractionDuration(), 6.0F);
    EXPECT_FALSE(session.signalCalledLastUpdate());
    session.update(0.02F, false, false, false, false, true);
    EXPECT_EQ(session.state(), RaidSessionState::Extracted);
    EXPECT_FALSE(session.signalCalledLastUpdate());
}

TEST(RaidCredentialRuntimeTest, LeaveControlAndDeathResetCredentialRoute)
{
    RaidSession session{credentialSessionConfig()};
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(session.triggerHighRisk());
    session.update(3.0F, false, false, false, false, true);
    session.update(0.0F, false, false, false, false, false);
    EXPECT_FLOAT_EQ(session.extractionProgress(), 0.0F);
    session.update(2.0F, false, false, false, false, true);
    ASSERT_TRUE(session.interruptCredentialExtraction());
    EXPECT_FLOAT_EQ(session.extractionTimeElapsed(), 0.0F);
    session.update(1.0F, false, false, false, false, true);
    ASSERT_TRUE(session.markPlayerDead());
    session.update(20.0F, false, false, false, true, true);
    EXPECT_EQ(session.state(), RaidSessionState::PlayerDead);
    EXPECT_EQ(session.extractionRoute(), RaidExtractionRoute::None);
}

TEST(RaidCredentialRuntimeTest, DisabledCredentialContractCannotSelectRoute)
{
    auto config = credentialSessionConfig();
    config.credentialExtractionDurationSeconds = 0.0F;
    RaidSession session{config};
    ASSERT_TRUE(session.start());
    ASSERT_TRUE(session.triggerHighRisk());
    session.update(10.0F, false, false, false, false, true);
    EXPECT_EQ(session.state(), RaidSessionState::InRaid);
    EXPECT_FALSE(session.credentialExtractionOpen());
    config.credentialExtractionDurationSeconds =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_THROW(static_cast<void>(RaidSession{config}), std::invalid_argument);
}

TEST(RaidCredentialRuntimeTest, FullInvestigationMintsOneFactAndAcknowledgesOnce)
{
    GameplayWorld world{credentialWorldConfig()};
    EXPECT_FALSE(world.credentialInvestigationInRange());
    enterOffice(world);
    ASSERT_TRUE(world.credentialInvestigationInRange());
    world.update(holdInvestigation(), 1.0F);
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.5F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
    world.update(holdInvestigation(), 1.0F);
    const auto fact = world.pendingCredentialInvestigation();
    ASSERT_TRUE(fact);
    EXPECT_EQ(fact->spaceId, world.activeRaidSpaceId());
    EXPECT_EQ(fact->investigationPointId, "investigation.test.transit_documents");
    EXPECT_FALSE(world.credentialExtractionUnlocked());
    world.acknowledgeCredentialInvestigation();
    world.acknowledgeCredentialInvestigation();
    world.update(holdInvestigation(), 4.0F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
    EXPECT_FALSE(world.credentialInvestigationInRange());
    EXPECT_TRUE(world.credentialExtractionSnapshot()->investigationCompleted);
    EXPECT_FALSE(world.credentialExtractionUnlocked());
}

TEST(RaidCredentialRuntimeTest, ReleasingHoldResetsRatherThanAccumulatingProgress)
{
    GameplayWorld world{credentialWorldConfig()};
    enterOffice(world);
    world.update(holdInvestigation(), 1.0F);
    world.update({}, 0.0F);
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
    world.update(holdInvestigation(), 1.1F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
}

TEST(RaidCredentialRuntimeTest, PortalFrameAndLeavingRadiusCannotAccumulateHold)
{
    GameplayWorld world{credentialWorldConfig()};
    auto enter = holdInvestigation();
    enter.interactJustPressed = true;
    world.update(enter, 1.0F);
    ASSERT_FALSE(world.inOutdoorRaidSpace());
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
    world.update(holdInvestigation(), 1.0F);
    ASSERT_GT(world.credentialInvestigationProgress(), 0.0F);
    auto leave = holdInvestigation();
    leave.moveDown = true;
    world.update(leave, 1.0F);
    EXPECT_FALSE(world.credentialInvestigationInRange());
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
}

TEST(RaidCredentialRuntimeTest, MenuModalFocusAndArbitrationInterruptInvestigation)
{
    for (int gate = 0; gate < 4; ++gate)
    {
        GameplayWorld world{credentialWorldConfig()};
        enterOffice(world);
        world.update(holdInvestigation(), 1.0F);
        auto blocked = holdInvestigation();
        blocked.inventoryOpen = gate == 0;
        blocked.throwableModal = gate == 1;
        blocked.freezeObservationDirection = gate == 2;
        blocked.credentialInvestigationAllowed = gate != 3;
        world.update(blocked, 1.0F);
        EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F) << gate;
        EXPECT_FALSE(world.pendingCredentialInvestigation()) << gate;
    }
}

TEST(RaidCredentialRuntimeTest, ControlThatExpiresThisFrameStillInterruptsHold)
{
    GameplayWorld world{credentialWorldConfig()};
    enterOffice(world);
    world.update(holdInvestigation(), 1.0F);
    ASSERT_TRUE(const_cast<Player &>(world.player()).applyControl(0.01F));
    world.update(holdInvestigation(), 1.0F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
}

TEST(RaidCredentialRuntimeTest, WrongSpaceDistanceAndBlockedSightCannotInvestigate)
{
    auto config = credentialWorldConfig();
    GameplayWorld outside{config};
    outside.update(holdInvestigation(), 3.0F);
    EXPECT_FALSE(outside.pendingCredentialInvestigation());

    config.interiors.front().interiorSpawn = {80.0F, 200.0F};
    GameplayWorld distant{config};
    enterOffice(distant);
    distant.update(holdInvestigation(), 3.0F);
    EXPECT_FALSE(distant.credentialInvestigationInRange());
    EXPECT_FALSE(distant.pendingCredentialInvestigation());

    config = credentialWorldConfig();
    config.interiors.front().ballisticBlockers.push_back(
        BallisticBlocker{1, Rect{{244.0F, 190.0F}, {10.0F, 80.0F}}});
    GameplayWorld blocked{config};
    enterOffice(blocked);
    blocked.update(holdInvestigation(), 3.0F);
    EXPECT_FALSE(blocked.credentialInvestigationInRange());
    EXPECT_FALSE(blocked.pendingCredentialInvestigation());
}

TEST(RaidCredentialRuntimeTest, LookingAwayInterruptsVisibleInvestigation)
{
    GameplayWorld world{credentialWorldConfig()};
    enterOffice(world);
    world.update(holdInvestigation(), 1.0F);
    auto away = holdInvestigation();
    // Keep the absolute anchor fixed and supply actual device motion. Legacy
    // absolute samples do not snap an already rebased, window-anchored reticle.
    const Vec2 actual = world.weaponAimWorldPosition();
    away.aimMotionDelta = Vec2{100.0F - actual.x, 225.0F - actual.y};
    world.update(away, 1.0F);
    ASSERT_LT(world.weaponAimDirection().x, 0.0F);
    ASSERT_FALSE(world.credentialInvestigationInRange());
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(world.pendingCredentialInvestigation());
}

TEST(RaidCredentialRuntimeTest, RejectedFactCanBeCancelledWithoutCompletingSnapshot)
{
    GameplayWorld world{credentialWorldConfig()};
    enterOffice(world);
    world.update(holdInvestigation(), 2.0F);
    ASSERT_TRUE(world.pendingCredentialInvestigation());
    world.cancelCredentialInvestigationInteraction();
    EXPECT_FALSE(world.pendingCredentialInvestigation());
    EXPECT_FALSE(world.credentialExtractionSnapshot()->investigationCompleted);
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
    world.update(holdInvestigation(), 2.0F);
    ASSERT_TRUE(world.pendingCredentialInvestigation());
    ASSERT_TRUE(world.markPlayerDead());
    EXPECT_FALSE(world.pendingCredentialInvestigation());
    EXPECT_FLOAT_EQ(world.credentialInvestigationProgress(), 0.0F);
}

TEST(RaidCredentialRuntimeTest, FrozenPermissionRequiredAndModalGateRestartsSixSeconds)
{
    auto config = credentialWorldConfig(true);
    config.playerSpawn = {460.0F, 410.0F};
    config.highRisk.regularPhaseDurationSeconds = 0.1F;
    GameplayWorld world{config};
    world.update({}, 0.1F);
    world.update({}, 3.0F);
    ASSERT_EQ(world.raidSession().extractionRoute(), RaidExtractionRoute::Credential);
    EXPECT_FALSE(world.raidSession().signalCalledLastUpdate());
    GameplayInput blocked;
    blocked.inventoryOpen = true;
    world.update(blocked, 0.0F);
    EXPECT_FLOAT_EQ(world.raidSession().extractionTimeElapsed(), 0.0F);
    world.update({}, 5.99F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::Extracting);
    world.update({}, 0.02F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::Extracted);

    config.credentialExtraction->unlockedAtDeploy = false;
    config.credentialExtraction->investigationCompleted = true;
    GameplayWorld locked{config};
    locked.update({}, 0.1F);
    locked.update({}, 8.0F);
    EXPECT_EQ(locked.raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FALSE(locked.credentialExtractionUnlocked());
}

TEST(RaidCredentialRuntimeTest, CredentialMapUsesDiscoveryOrTransportWithoutGrantingPermission)
{
    const ContentRect exit{{850.0F, 430.0F}, {80.0F, 60.0F}};
    RaidTacticalMapState map;
    map.configure({960, 540}, {}, {{20, 20}, {80, 60}}, {}, {}, {}, {}, {}, {}, exit);
    EXPECT_FALSE(map.extractionVisible(RaidMapExtractionKind::Credential));
    map.revealAround({890, 460});
    EXPECT_TRUE(map.extractionVisible(RaidMapExtractionKind::Credential));

    RaidIntelligenceLoadout transport;
    transport.set(RaidIntelligenceCategory::Transport, true);
    map.configure({960, 540}, transport, {{20, 20}, {80, 60}}, {}, {}, {}, {}, {}, {}, exit);
    EXPECT_TRUE(map.extractionVisible(RaidMapExtractionKind::Credential));
    map.configure({960, 540}, transport, {{20, 20}, {80, 60}}, {}, {}, {}, {});
    EXPECT_FALSE(map.extractionVisible(RaidMapExtractionKind::Credential));
}

TEST(RaidCredentialRuntimeTest, ExpiringControlStillCancelsCredentialExitThisFrame)
{
    auto config = credentialWorldConfig(true);
    config.playerSpawn = {460.0F, 410.0F};
    config.highRisk.regularPhaseDurationSeconds = 0.1F;
    GameplayWorld world{config};
    world.update({}, 0.1F);
    world.update({}, 3.0F);
    ASSERT_EQ(world.raidSession().extractionRoute(), RaidExtractionRoute::Credential);
    ASSERT_TRUE(const_cast<Player &>(world.player()).applyControl(0.01F));
    world.update({}, 0.1F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(world.raidSession().extractionTimeElapsed(), 0.0F);
    world.update({}, 5.99F);
    EXPECT_EQ(world.raidSession().state(), RaidSessionState::Extracting);
}

TEST(RaidCredentialRuntimeTest, PauseHookInterruptsCredentialWithoutSimulationAdvance)
{
    GameplayWorld investigation{credentialWorldConfig()};
    enterOffice(investigation);
    investigation.update(holdInvestigation(), 1.0F);
    ASSERT_GT(investigation.credentialInvestigationProgress(), 0.0F);
    investigation.interruptCredentialInteraction();
    EXPECT_FLOAT_EQ(investigation.credentialInvestigationProgress(), 0.0F);
    EXPECT_FALSE(investigation.pendingCredentialInvestigation());

    auto config = credentialWorldConfig(true);
    config.playerSpawn = {460.0F, 410.0F};
    config.highRisk.regularPhaseDurationSeconds = 0.1F;
    GameplayWorld extraction{config};
    extraction.update({}, 0.1F);
    extraction.update({}, 3.0F);
    ASSERT_EQ(extraction.raidSession().extractionRoute(), RaidExtractionRoute::Credential);
    extraction.interruptCredentialInteraction();
    EXPECT_EQ(extraction.raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(extraction.raidSession().extractionTimeElapsed(), 0.0F);

    config.playerSpawn = {1010.0F, 570.0F};
    config.highRisk.regularPhaseDurationSeconds = 1000.0F;
    GameplayWorld normal{config};
    normal.update({}, 1.0F);
    ASSERT_EQ(normal.raidSession().extractionRoute(), RaidExtractionRoute::Normal);
    normal.interruptCredentialInteraction();
    EXPECT_EQ(normal.raidSession().extractionRoute(), RaidExtractionRoute::Normal);
    EXPECT_FLOAT_EQ(normal.raidSession().extractionTimeElapsed(), 1.0F);
}

TEST(RaidCredentialRuntimeTest, CredentialMapAnchorsAreDeterministicReachableAndSeparate)
{
    const auto &map = publishedContentRegistry().map(
        MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_TRUE(map.credentialExtraction);
    const auto anchors = credentialMapAnchors(map);
    for (const auto seed : std::array<std::uint64_t, 3>{7U, 31U, 89U})
    {
        const auto layout = generateRaidMapLayout(map, seed, anchors);
        EXPECT_EQ(layout, generateRaidMapLayout(map, seed, anchors));
        EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors));
        const auto *exit = findRaidAnchorPlacement(layout, kRaidAnchorCredentialExtraction);
        ASSERT_NE(exit, nullptr);
        const auto district = std::find_if(layout.districts.begin(), layout.districts.end(),
            [&](const auto &candidate) { return candidate.instanceId == exit->districtInstanceId; });
        ASSERT_NE(district, layout.districts.end());
        EXPECT_TRUE(district->kind == RaidDistrictKind::Highway ||
            district->kind == RaidDistrictKind::Logistics);
        for (const auto &placement : layout.anchorPlacements)
            if (placement.id != exit->id) EXPECT_FALSE(overlaps(exit->bounds, placement.bounds));
        for (const auto &blocker : layout.ballisticBlockers)
            EXPECT_FALSE(overlaps(exit->bounds, blocker));
    }
}

TEST(RaidCredentialRuntimeTest, CredentialMapFallbackRetainsFrozenReachableExit)
{
    auto map = publishedContentRegistry().map(MapDefinitionId{"map.raid.frontier_exchange"});
    ASSERT_TRUE(map.credentialExtraction);
    const auto anchors = credentialMapAnchors(map);
    map.proceduralOutdoor.maximumAttempts = 0U;
    const auto layout = generateRaidMapLayout(map, 11U, anchors);
    ASSERT_TRUE(layout.usedFallback);
    ASSERT_NE(findRaidAnchorPlacement(layout, kRaidAnchorCredentialExtraction), nullptr);
    EXPECT_TRUE(raidMapLayoutConnectsAnchors(map, layout, anchors));
}

TEST(RaidCredentialRuntimeTest, FullDeployKeepsCredentialClearOfLandmarksWithoutMovingOldAnchors)
{
    const auto &content = publishedContentRegistry();
    auto legacyJson = nlohmann::json::parse(publishedContentJson());
    for (auto &map : legacyJson["maps"]) map.erase("credential_extraction");
    const auto legacyContent = ContentRegistry::fromJson(legacyJson.dump());
    const MapDefinitionId frontier{"map.raid.frontier_exchange"};
    // Seed 41 selected a logistics landmark structure before the dedicated
    // collider exclusion. Exercise real encounter/resource requests as well
    // as the simpler direct generator fixtures above.
    for (const auto seed : std::array<std::uint64_t, 12>{
             1U, 2U, 8U, 17U, 31U, 41U, 64U, 89U, 128U, 9917U, 7712100U, 880031U})
    {
        SCOPED_TRACE(seed);
        auto current = makeNewAlphaProfile("credential-placement-preservation", content);
        auto legacy = makeNewAlphaProfile("credential-placement-preservation", legacyContent);
        const DeployCommand command{"credential-placement-raid", "credential-placement-settlement",
            seed, frontier};
        const auto deployed = executeDeploy(current, content, command,
            CommandContext{current.revision, "credential-placement-deploy"});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        const auto oldDeployed = executeDeploy(legacy, legacyContent, command,
            CommandContext{legacy.revision, "credential-placement-deploy"});
        ASSERT_TRUE(oldDeployed.succeeded) << oldDeployed.message;
        ASSERT_TRUE(current.pendingRaid);
        ASSERT_TRUE(legacy.pendingRaid);
        const auto &layout = current.pendingRaid->spatialLayout;
        const auto *exit = findRaidAnchorPlacement(layout, kRaidAnchorCredentialExtraction);
        ASSERT_NE(exit, nullptr);
        for (const auto &landmark : layout.landmarks)
            for (const auto &structure : landmark.structures)
                EXPECT_FALSE(overlaps(exit->bounds, structure));
        for (const auto &blocker : layout.ballisticBlockers)
            EXPECT_FALSE(overlaps(exit->bounds, blocker));
        for (const auto &oldAnchor : legacy.pendingRaid->spatialLayout.anchorPlacements)
        {
            const auto *retained = findRaidAnchorPlacement(layout, oldAnchor.id);
            ASSERT_NE(retained, nullptr);
            EXPECT_EQ(retained->bounds, oldAnchor.bounds);
            EXPECT_FALSE(overlaps(exit->bounds, oldAnchor.bounds));
        }
        EXPECT_EQ(current.pendingRaid->loot, legacy.pendingRaid->loot);
        EXPECT_EQ(current.pendingRaid->enemies, legacy.pendingRaid->enemies);
        EXPECT_TRUE(validateProfileState(current, content).valid);
    }
}
