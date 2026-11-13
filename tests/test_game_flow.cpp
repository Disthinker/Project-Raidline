#include <gtest/gtest.h>

#include "game_flow.h"
#include "test_temp_directory.h"

#include <algorithm>

namespace
{
    GameplayInput moveLeftInput()
    {
        GameplayInput input{};
        input.moveLeft = true;
        return input;
    }

    GameplayInput moveDownInput()
    {
        GameplayInput input{};
        input.moveDown = true;
        return input;
    }

    void enterRaid(GameFlow &flow)
    {
        ASSERT_TRUE(flow.startGame());
        ASSERT_TRUE(flow.deploy());
        ASSERT_EQ(flow.state(), GameFlowState::Raid);
    }

    void pickUpDefaultMedkit(GameFlow &flow)
    {
        flow.update(moveLeftInput(), 0.5F);
        flow.update(moveDownInput(), 0.25F);

        GameplayInput interact{};
        interact.interactJustPressed = true;
        flow.update(interact, 0.0F);

        ASSERT_EQ(
            flow.gameSession()
                .world()
                .inventory()
                .placedItems()
                .size(),
            1U);
    }

    void extractFromDefaultMedkitPosition(
        GameFlow &flow)
    {
        flow.update(moveLeftInput(), 1.5F);
        flow.update(moveDownInput(), 0.45F);
        flow.update(GameplayInput{}, 3.0F);
    }
}

TEST(GameFlowTest, DefaultsToMainMenuWithPreparedRaidOne)
{
    const GameFlow flow;

    EXPECT_EQ(flow.state(), GameFlowState::MainMenu);
    EXPECT_FALSE(flow.isRaidScreen());
    EXPECT_EQ(flow.gameSession().raidNumber(), 1U);
    EXPECT_EQ(
        flow.gameSession().state(),
        GameSessionState::InRaid);
    EXPECT_EQ(flow.gameSession().stash().stackCount(), 0U);
}

TEST(GameFlowTest, NonRaidScreensFreezePreparedWorld)
{
    GameFlow flow;
    const Vec2 originalPosition =
        flow.gameSession().world().player().position();
    const float originalTime =
        flow.gameSession()
            .world()
            .raidSession()
            .raidTimeRemaining();
    const Vec2 originalEnemyPosition =
        flow.gameSession().world().enemies().front().position();
    const RaidSettlementState originalSettlement =
        flow.gameSession().settlement().state();

    GameplayInput input{};
    input.moveRight = true;
    input.firePressed = true;
    input.fireJustPressed = true;
    flow.update(input, 5.0F);

    EXPECT_EQ(flow.state(), GameFlowState::MainMenu);
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().player().position().x,
        originalPosition.x);
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().player().position().y,
        originalPosition.y);
    EXPECT_FLOAT_EQ(
        flow.gameSession()
            .world()
            .raidSession()
            .raidTimeRemaining(),
        originalTime);
    EXPECT_TRUE(
        flow.gameSession().world().logicalBallistics().empty());
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().enemies().front().position().x,
        originalEnemyPosition.x);
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().enemies().front().position().y,
        originalEnemyPosition.y);
    EXPECT_EQ(
        flow.gameSession().settlement().state(),
        originalSettlement);

    ASSERT_TRUE(flow.startGame());
    flow.update(input, 5.0F);
    EXPECT_EQ(flow.state(), GameFlowState::Base);
    EXPECT_FLOAT_EQ(
        flow.gameSession()
            .world()
            .raidSession()
            .raidTimeRemaining(),
        originalTime);
}

TEST(GameFlowTest, StartAndFirstDeployUsePreparedRaidExactlyOnce)
{
    GameFlow flow;
    GameplayWorld *const preparedWorld =
        &flow.gameSession().world();

    ASSERT_TRUE(flow.startGame());
    EXPECT_FALSE(flow.startGame());
    EXPECT_EQ(flow.state(), GameFlowState::Base);

    ASSERT_TRUE(flow.deploy());
    EXPECT_EQ(flow.state(), GameFlowState::Raid);
    EXPECT_EQ(&flow.gameSession().world(), preparedWorld);
    EXPECT_EQ(flow.gameSession().raidNumber(), 1U);
    EXPECT_EQ(flow.gameSession().world().player().health(), 3);
    EXPECT_FALSE(flow.deploy());
    EXPECT_FALSE(flow.returnToBase());
}

TEST(GameFlowTest, DeathResultReturnsToBaseAndDeploysFreshRaid)
{
    GameFlow flow;
    enterRaid(flow);

    ASSERT_TRUE(
        flow.gameSession().world().damagePlayer(3));
    flow.update(GameplayInput{}, 0.0F);

    ASSERT_EQ(flow.state(), GameFlowState::RaidResult);
    ASSERT_EQ(
        flow.gameSession().settlement().state(),
        RaidSettlementState::PlayerDead);
    EXPECT_FALSE(flow.deploy());

    const Vec2 resultPosition =
        flow.gameSession().world().player().position();
    const float resultTime =
        flow.gameSession()
            .world()
            .raidSession()
            .raidTimeRemaining();
    GameplayInput resultInput{};
    resultInput.moveRight = true;
    resultInput.firePressed = true;
    resultInput.fireJustPressed = true;
    flow.update(resultInput, 5.0F);

    EXPECT_EQ(flow.state(), GameFlowState::RaidResult);
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().player().position().x,
        resultPosition.x);
    EXPECT_FLOAT_EQ(
        flow.gameSession().world().player().position().y,
        resultPosition.y);
    EXPECT_FLOAT_EQ(
        flow.gameSession()
            .world()
            .raidSession()
            .raidTimeRemaining(),
        resultTime);
    EXPECT_TRUE(
        flow.gameSession().world().logicalBallistics().empty());

    ASSERT_TRUE(flow.returnToBase());
    EXPECT_EQ(flow.state(), GameFlowState::Base);
    EXPECT_EQ(flow.gameSession().raidNumber(), 1U);

    ASSERT_TRUE(flow.deploy());
    EXPECT_EQ(flow.state(), GameFlowState::Raid);
    EXPECT_EQ(flow.gameSession().raidNumber(), 2U);
    EXPECT_EQ(flow.gameSession().world().player().health(), 3);
    EXPECT_TRUE(
        flow.gameSession()
            .world()
            .inventory()
            .placedItems()
            .empty());
}

TEST(GameFlowTest, ExtractionPersistsStashAcrossBaseAndNextRaid)
{
    GameFlow flow;
    enterRaid(flow);
    pickUpDefaultMedkit(flow);
    extractFromDefaultMedkitPosition(flow);

    ASSERT_EQ(flow.state(), GameFlowState::RaidResult);
    ASSERT_EQ(
        flow.gameSession().settlement().state(),
        RaidSettlementState::Extracted);
    ASSERT_EQ(flow.gameSession().stash().stackCount(), 1U);
    const ItemInstanceId storedId =
        flow.gameSession()
            .stash()
            .inventory()
            .placedItems()
            .front()
            .item.instanceId();

    ASSERT_TRUE(flow.returnToBase());
    flow.update(moveLeftInput(), 30.0F);
    EXPECT_EQ(flow.state(), GameFlowState::Base);
    EXPECT_EQ(flow.gameSession().stash().stackCount(), 1U);

    ASSERT_TRUE(flow.deploy());
    EXPECT_EQ(flow.gameSession().raidNumber(), 2U);
    EXPECT_EQ(flow.gameSession().stash().stackCount(), 1U);
    EXPECT_EQ(
        flow.gameSession()
            .stash()
            .inventory()
            .placedItems()
            .front()
            .item.instanceId(),
        storedId);
    EXPECT_GT(flow.gameSession().nextItemInstanceId(), storedId);
}

TEST(GameFlowTest, TimeoutEntersResultBeforeReturningToBase)
{
    GameFlow flow;
    enterRaid(flow);

    flow.update(GameplayInput{}, 180.0F);

    ASSERT_EQ(flow.state(), GameFlowState::RaidResult);
    EXPECT_EQ(
        flow.gameSession().settlement().state(),
        RaidSettlementState::RaidEnded);
    EXPECT_EQ(
        flow.gameSession().state(),
        GameSessionState::BetweenRaids);
    EXPECT_FALSE(flow.deploy());
    EXPECT_TRUE(flow.returnToBase());
    EXPECT_EQ(flow.state(), GameFlowState::Base);
}

TEST(GameFlowTest, BlockedSettlementCannotReachBaseOrDeploy)
{
    GameFlow flow{{1, 1}};
    enterRaid(flow);
    pickUpDefaultMedkit(flow);
    extractFromDefaultMedkitPosition(flow);

    ASSERT_EQ(flow.state(), GameFlowState::Raid);
    EXPECT_EQ(
        flow.gameSession().state(),
        GameSessionState::SettlementBlocked);
    EXPECT_EQ(
        flow.gameSession().settlement().state(),
        RaidSettlementState::Blocked);
    EXPECT_FALSE(flow.returnToBase());
    EXPECT_FALSE(flow.deploy());
    EXPECT_EQ(flow.gameSession().raidNumber(), 1U);
    EXPECT_EQ(
        flow.gameSession()
            .world()
            .inventory()
            .placedItems()
            .size(),
        1U);
}

TEST(GameFlowTest, StateNamesAreStableForDebugOutput)
{
    EXPECT_STREQ(
        gameFlowStateName(GameFlowState::MainMenu),
        "MainMenu");
    EXPECT_STREQ(
        gameFlowStateName(GameFlowState::Base),
        "Base");
    EXPECT_STREQ(
        gameFlowStateName(GameFlowState::Raid),
        "Raid");
    EXPECT_STREQ(
        gameFlowStateName(GameFlowState::RaidResult),
        "RaidResult");
}

TEST(GameFlowTest, BaseManagementCanOpenEveryFixedFacilityWithoutProximity)
{
    GameFlow flow;
    ASSERT_TRUE(flow.startGame());
    for (const BaseFacility &facility : flow.baseWorld().facilities())
    {
        if (!facility.active)
        {
            EXPECT_FALSE(flow.openBaseFacilityForManagement(facility.kind));
            continue;
        }
        ASSERT_TRUE(flow.openBaseFacilityForManagement(facility.kind));
        EXPECT_EQ(flow.activeBaseFacility(), facility.kind);
        EXPECT_FALSE(flow.openBaseFacilityForManagement(facility.kind));
        flow.closeBaseFacility();
    }

    ASSERT_TRUE(flow.deploy());
    EXPECT_FALSE(flow.openBaseFacilityForManagement(
        BaseFacilityKind::Workshop));
}

TEST(GameFlowTest, GeneratorPhysicallyDeploysInEveryPublishedSiteAndPlot)
{
    const auto &content = publishedContentRegistry();
    for (const auto &site : content.regionalOperations().baseSites)
    {
        std::vector<std::string> plots{""};
        if (site.id == kFoundingRegion)
            for (const auto &plot : homePlotDefinitions())
                plots.emplace_back(plot.id);
        for (const auto &plot : plots)
        {
            SCOPED_TRACE(std::string{site.id.value()} + ":" + plot);
            raidline::test::TemporaryDirectory temporary;
            temporary.claim("raidline-generator-", "physical-placement");
            ProfileState profile = makeNewAlphaProfile("generator-placement", content);
            profile.regionalOperations.baseSites.at(site.id).unlocked = true;
            if (site.outpostDefinitionId)
                profile.regionalOperations.outposts.at(*site.outpostDefinitionId).unlocked = true;
            profile.regionalOperations.activeBaseNodeId = site.nodeId;
            profile.regionalOperations.technologyCore.baseSiteDefinitionId = site.id;
            if (!plot.empty())
            {
                profile.homeFounding.layoutVersion = 2U;
                profile.homeFounding.plots[site.id] = plot;
            }
            profile.baseConstruction.facilities[kBaseGeneratorFacilityId] =
                BaseConstructionState::FacilityPlacement::Reserve;
            profile.baseConstruction.facilityReserveStartedWorldMinutes[kBaseGeneratorFacilityId] =
                profile.worldClock.elapsedWorldMinutes;
            initializeBaseFacilityLayouts(profile, content);
            const HomeRegionLayout layout = plot.empty()
                ? generateHomeRegionLayout(site.id.value())
                : generateFoundingHomeRegionLayout(site.id.value(), plot);
            const auto perimeter = ensureHomePerimeterSnapshot(profile, content,
                {site.id, layout.worldSize, layout.baseParcel, layout.movementBlockers},
                {profile.revision, "seed-perimeter"});
            ASSERT_TRUE(perimeter.succeeded) << perimeter.message;
            const Vec2 center{layout.baseParcel.position.x + 1040.0F,
                              layout.baseParcel.position.y + 320.0F};
            const auto ground = profile.assets.create(
                content.item(ItemDefinitionId{"item.loot.cola_basic"}),
                BaseGroundAssetLocation{site.id, center});
            ASSERT_TRUE(SaveRepository{temporary.path()}.save(profile, content.contentVersion()).succeeded);

            GameFlow flow;
            flow.configurePersistence(temporary.path());
            ASSERT_TRUE(flow.continueGame()) << flow.gameSession().persistenceMessage();
            const BaseWorld &world = flow.baseWorld();
            const auto generator = std::find_if(world.facilities().begin(), world.facilities().end(),
                [](const BaseFacility &facility) { return facility.kind == BaseFacilityKind::Generator; });
            ASSERT_NE(generator, world.facilities().end());
            const Vec2 origin = world.baseParcel().position;
            EXPECT_FALSE(generator->active);
            EXPECT_FALSE(flow.openBaseFacilityForManagement(BaseFacilityKind::Generator));
            EXPECT_FALSE(flow.queryInstallBaseFacilityAt(BaseFacilityKind::Generator, origin).canCommit);
            EXPECT_FALSE(flow.queryInstallBaseFacilityAt(BaseFacilityKind::Generator, center).canCommit);
            ASSERT_TRUE(profile.assets.erase(ground));
            ASSERT_TRUE(SaveRepository{temporary.path()}.save(profile, content.contentVersion()).succeeded);
            GameFlow clearFlow;
            clearFlow.configurePersistence(temporary.path());
            ASSERT_TRUE(clearFlow.continueGame()) << clearFlow.gameSession().persistenceMessage();
            const BaseWorld &clearWorld = clearFlow.baseWorld();
            const auto preview = clearFlow.queryInstallBaseFacilityAt(BaseFacilityKind::Generator, center);
            ASSERT_TRUE(preview.canCommit) << preview.message;
            const auto installed = clearFlow.installBaseFacilityAt(
                BaseFacilityKind::Generator, center, "place-generator");
            ASSERT_TRUE(installed.succeeded) << installed.message;
            const auto deployed = std::find_if(clearWorld.facilities().begin(), clearWorld.facilities().end(),
                [](const BaseFacility &facility) { return facility.kind == BaseFacilityKind::Generator; });
            ASSERT_NE(deployed, clearWorld.facilities().end());
            EXPECT_TRUE(deployed->active);
            EXPECT_FLOAT_EQ(deployed->bounds.size.x, 180.0F);
            EXPECT_FLOAT_EQ(deployed->bounds.size.y, 120.0F);
            EXPECT_FLOAT_EQ(deployed->bounds.position.x + 90.0F, center.x);
            EXPECT_FLOAT_EQ(deployed->bounds.position.y + 60.0F, center.y);

            const Vec2 entrance = baseFacilityAccessGeometry(*deployed).entrancePoint;
            const Vec2 size = clearWorld.playerSize();
            ASSERT_TRUE(clearFlow.baseWorld().restorePlayerBody(
                {entrance.x - size.x * 0.5F, entrance.y + 20.0F - size.y * 0.5F},
                {0.0F, -1.0F}, 0.0F, 0.0F));
            EXPECT_EQ(clearWorld.interactableFacility(), BaseFacilityKind::Generator);
            BaseInput input{};
            input.interactJustPressed = true;
            clearFlow.updateBase(input, 0.0F);
            EXPECT_EQ(clearFlow.activeBaseFacility(), BaseFacilityKind::Generator);
        }
    }
}

TEST(GameFlowTest, BaseAndRaidCanReturnToMainMenuWithoutSettlement)
{
    GameFlow base;
    ASSERT_TRUE(base.startGame());
    EXPECT_TRUE(base.returnToMainMenu());
    EXPECT_EQ(base.state(), GameFlowState::MainMenu);
    EXPECT_FALSE(base.returnToMainMenu());

    GameFlow raid;
    enterRaid(raid);
    const RaidSettlementState settlementBefore =
        raid.gameSession().settlement().state();
    EXPECT_TRUE(raid.returnToMainMenu());
    EXPECT_EQ(raid.state(), GameFlowState::MainMenu);
    EXPECT_EQ(raid.gameSession().settlement().state(), settlementBefore);
    EXPECT_EQ(raid.gameSession().state(), GameSessionState::InRaid);
}
