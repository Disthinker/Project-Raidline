#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "alpha_content_ids.h"
#include "game_session.h"

namespace
{
const MapDefinitionId kFrontier{"map.raid.frontier_exchange"};
const RaidSpaceDefinitionId kFreightRoom{"raid_space.frontier_exchange.freight_service_bay"};
constexpr float kStepSeconds{0.125F};

enum class WedgeStorage { None, Carried, Warehouse };

class RaidFreightRuntimeTest : public testing::Test
{
protected:
    void deploy(bool carryKey, int additionalWarehouseStacks = 0,
                WedgeStorage wedgeStorage = WedgeStorage::None)
    {
        ASSERT_TRUE(session.startNewProfile("freight-runtime"));
        auto &profile = const_cast<ProfileState &>(session.profile());
        const auto &content = publishedContentRegistry();
        const auto foundBag = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
        ASSERT_NE(foundBag, profile.assets.records().end());
        bag = foundBag->first;
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{bag, EquipmentSlotKind::Backpack},
            {profile.revision, "equip-freight-test-bag"}).succeeded);

        const auto keyDestination = carryKey ? ProfileContainerId::compartment(bag, 0)
                                             : ProfileContainerId::stash();
        const auto &keyDefinition = content.item(kFreightCrateKeyId);
        const auto fit = findFirstProfileFit(profile, content, keyDestination, keyDefinition, ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        key = profile.assets.create(keyDefinition, StoredAssetLocation{keyDestination, *fit});

        if (wedgeStorage != WedgeStorage::None)
        {
            const auto destination = wedgeStorage == WedgeStorage::Carried
                ? ProfileContainerId::compartment(bag, 0) : ProfileContainerId::stash();
            const auto &definition = content.item(kFreightBreachWedgesId);
            for (auto &wedge : wedges)
            {
                const auto cell = findFirstProfileFit(profile, content, destination,
                    definition, ItemOrientation::Degrees0);
                ASSERT_TRUE(cell);
                wedge = profile.assets.create(definition, StoredAssetLocation{destination, *cell});
            }
        }

        const auto warehouse = ProfileContainerId::stash();
        const auto &ammunition = content.item(alpha_content::ammunition);
        for (int index = 0; index < additionalWarehouseStacks; ++index)
        {
            const auto origin = findFirstProfileFit(profile, content, warehouse, ammunition, ItemOrientation::Degrees0);
            ASSERT_TRUE(origin);
            static_cast<void>(profile.assets.create(ammunition, StoredAssetLocation{warehouse, *origin}));
        }

        ASSERT_TRUE(session.deployAlpha(9917, kFrontier)) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid);
        const auto &raid = *session.profile().pendingRaid;
        ASSERT_TRUE(raid.sealedFreightCrate);
        crate = raid.sealedFreightCrate->containerAssetId;
        ASSERT_TRUE(session.isRaidFreightCrate(crate));
        EXPECT_FALSE(session.raidFreightUnlock());
        const auto interior = std::find_if(raid.interiors.begin(), raid.interiors.end(),
            [](const auto &room) { return room.id == kFreightRoom; });
        ASSERT_NE(interior, raid.interiors.end());
        const auto &portal = interior->exteriorEntrance;
        ASSERT_NO_FATAL_FAILURE(move({portal.position.x + portal.size.x * 0.5F,
                                      portal.position.y + portal.size.y * 0.5F}));
        GameplayInput enter;
        enter.interactJustPressed = true;
        session.update(enter, 0.0F);
        ASSERT_EQ(session.world().activeRaidSpaceId(), kFreightRoom);

        // Keep damage and corpse selection from changing the timed gesture.
        for (const auto &enemy : session.world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        session.update({}, 0.0F);
        const auto *root = session.profile().assets.find(crate);
        ASSERT_NE(root, nullptr);
        cratePosition = std::get<RaidSceneContainerAssetLocation>(root->location).position;
        ASSERT_NO_FATAL_FAILURE(moveNearCrate());
        const auto projection = session.raidFreightUnlock();
        ASSERT_TRUE(projection);
        EXPECT_EQ(projection->containerAssetId, crate);
        EXPECT_EQ(projection->hasKey, carryKey);
        EXPECT_FLOAT_EQ(projection->progress, 0.0F);
    }

    void move(Vec2 center)
    {
        auto &player = const_cast<Player &>(session.world().player());
        const float half = player.size() * 0.5F;
        ASSERT_TRUE(player.setPosition({center.x - half, center.y - half}));
    }

    void moveNearCrate()
    {
        ASSERT_NO_FATAL_FAILURE(move({cratePosition.x - 50.0F, cratePosition.y}));
        session.update(aimAtCrate(), 0.0F);
    }

    GameplayInput aimAtCrate() const
    {
        GameplayInput input;
        input.aimWorldPosition = cratePosition;
        input.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
        return input;
    }

    void hold(unsigned frames, bool freshPress)
    {
        auto input = aimAtCrate();
        input.interactPressed = true;
        input.interactJustPressed = freshPress;
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            session.update(input, kStepSeconds);
            input.interactJustPressed = false;
        }
    }

    bool unlocked() const
    {
        return session.profile().pendingRaid->sealedFreightCrate->unlocked;
    }

    float progress() const
    {
        const auto projection = session.raidFreightUnlock();
        EXPECT_TRUE(projection);
        return projection ? projection->progress : -1.0F;
    }

    GameplayInput aimAtBreachDoor() const
    {
        auto input = aimAtCrate();
        input.aimWorldPosition = Vec2{740.0F, 260.0F};
        return input;
    }

    void holdBreach(unsigned frames, bool freshPress)
    {
        auto input = aimAtBreachDoor();
        input.interactPressed = true;
        input.interactJustPressed = freshPress;
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            session.update(input, kStepSeconds);
            input.interactJustPressed = false;
        }
    }

    float breachProgress() const
    {
        const auto projection = session.raidFreightBreach();
        EXPECT_TRUE(projection);
        return projection ? projection->progress : -1.0F;
    }

    GameSession session;
    AssetInstanceId bag{};
    AssetInstanceId crate{};
    AssetInstanceId key{};
    std::array<AssetInstanceId, 2> wedges{};
    Vec2 cratePosition{};
};

class FrontierRoadBreachTest : public testing::Test
{
protected:
    void deploy(unsigned carriedWedges, bool warehouseWedge = false)
    {
        ASSERT_TRUE(session.startNewProfile("frontier-road-breach"));
        auto &profile = const_cast<ProfileState &>(session.profile());
        const auto &content = publishedContentRegistry();
        const auto bag = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
        ASSERT_NE(bag, profile.assets.records().end());
        const auto bagId = bag->first;
        ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{bagId, EquipmentSlotKind::Backpack},
            {profile.revision, "equip-road-test-bag"}).succeeded);
        const auto &wedge = content.item(kFreightBreachWedgesId);
        for (unsigned index = 0; index < carriedWedges + static_cast<unsigned>(warehouseWedge); ++index)
        {
            const auto target = index < carriedWedges
                ? ProfileContainerId::compartment(bagId, 0) : ProfileContainerId::stash();
            const auto cell = findFirstProfileFit(profile, content, target, wedge, ItemOrientation::Degrees0);
            ASSERT_TRUE(cell);
            wedges.push_back(profile.assets.create(wedge, StoredAssetLocation{target, *cell}));
        }
        ASSERT_TRUE(session.deployAlpha(2U, kFrontier)) << session.persistenceMessage();
        ASSERT_TRUE(session.profile().pendingRaid->highRiskCrisis);
        ASSERT_TRUE(session.profile().pendingRaid->highRiskCrisis->roadBlockade);
        blockade = *session.profile().pendingRaid->highRiskCrisis->roadBlockade;
    }

    static Vec2 center(const auto &rect)
    {
        return {rect.position.x + rect.size.x * 0.5F, rect.position.y + rect.size.y * 0.5F};
    }

    void move(Vec2 center)
    {
        auto &player = const_cast<Player &>(session.world().player());
        const float half = player.size() * 0.5F;
        ASSERT_TRUE(player.setPosition({center.x - half, center.y - half}));
    }

    void tick(GameplayInput input = {}, float seconds = kStepSeconds)
    {
        for (const auto &enemy : session.world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        session.update(input, seconds);
    }

    GameplayInput aim(Vec2 point) const
    {
        GameplayInput input;
        input.aimWorldPosition = point;
        input.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
        return input;
    }

    void closeRoad()
    {
        const auto control = session.world().highRiskControlPoint();
        ASSERT_TRUE(control);
        ASSERT_NO_FATAL_FAILURE(move(center(*control)));
        auto input = aim(center(*control));
        input.interactPressed = input.interactJustPressed = true;
        for (unsigned frame = 0; frame < 100 && session.world().raidSession().phase() == RaidPhase::Regular; ++frame)
        {
            tick(input);
            input.interactJustPressed = false;
        }
        ASSERT_EQ(session.world().raidSession().phase(), RaidPhase::HighRisk);
        ASSERT_TRUE(session.world().roadBlockade());
        EXPECT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Warning);
        for (unsigned frame = 0; frame < 40; ++frame)
            tick();
        ASSERT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Closed);
    }

    void approach()
    {
        const auto middle = center(blockade.bounds);
        for (const auto endpoint : {blockade.routeEntry, blockade.routeExit})
        {
            const Vec2 toward{endpoint.x - middle.x, endpoint.y - middle.y};
            const float length = std::hypot(toward.x, toward.y);
            ASSERT_GT(length, 0.0F);
            const Vec2 direction{toward.x / length, toward.y / length};
            const float radius = std::min(
                direction.x != 0.0F ? blockade.bounds.size.x / (2.0F * std::abs(direction.x)) : length,
                direction.y != 0.0F ? blockade.bounds.size.y / (2.0F * std::abs(direction.y)) : length);
            ASSERT_NO_FATAL_FAILURE(move({middle.x + direction.x * (radius + 48.0F),
                                          middle.y + direction.y * (radius + 48.0F)}));
            tick(aim(middle), 0.0F);
            if (session.raidRoadBlockadeBreach())
                return;
        }
        FAIL() << "Neither route endpoint exposes a usable roadblock face";
    }

    void hold(unsigned frames, bool freshPress)
    {
        auto input = aim(center(blockade.bounds));
        input.interactPressed = true;
        input.interactJustPressed = freshPress;
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            tick(input);
            input.interactJustPressed = false;
        }
    }

    GameSession session;
    RaidRoadBlockadeSnapshot blockade;
    std::vector<AssetInstanceId> wedges;
};

TEST_F(FrontierRoadBreachTest, FreshThreeSecondGestureConsumesExactlyOneCarriedWedge)
{
    ASSERT_NO_FATAL_FAILURE(deploy(2));
    ASSERT_NO_FATAL_FAILURE(closeRoad());
    ASSERT_NO_FATAL_FAILURE(approach());
    ASSERT_TRUE(session.raidRoadBlockadeBreach()->hasTool);

    hold(12, true);
    ASSERT_FLOAT_EQ(session.raidRoadBlockadeBreach()->progress, 0.5F);
    tick(aim(center(blockade.bounds)), 0.0F);
    hold(30, false);
    EXPECT_FLOAT_EQ(session.raidRoadBlockadeBreach()->progress, 0.0F);

    hold(12, true);
    auto modal = aim(center(blockade.bounds));
    modal.interactPressed = true;
    modal.freezeObservationDirection = true;
    tick(modal);
    hold(30, false);
    EXPECT_FLOAT_EQ(session.raidRoadBlockadeBreach()->progress, 0.0F);
    ASSERT_TRUE(session.profile().assets.find(wedges[0]));
    ASSERT_TRUE(session.profile().assets.find(wedges[1]));

    hold(23, true);
    ASSERT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Closed);
    hold(1, false);
    ASSERT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Breached);
    hold(30, true);
    const auto remaining = std::count_if(wedges.begin(), wedges.end(), [&](AssetInstanceId id) {
        return session.profile().assets.find(id) != nullptr;
    });
    EXPECT_EQ(remaining, 1);
}

TEST_F(FrontierRoadBreachTest, WarehouseWedgeCannotOpenRoadAndSignalExitStillWorks)
{
    ASSERT_NO_FATAL_FAILURE(deploy(0, true));
    ASSERT_NO_FATAL_FAILURE(closeRoad());
    ASSERT_NO_FATAL_FAILURE(approach());
    EXPECT_FALSE(session.raidRoadBlockadeBreach()->hasTool);
    hold(30, true);
    EXPECT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Closed);
    EXPECT_TRUE(session.profile().assets.find(wedges.front()));

    const auto &crisis = *session.profile().pendingRaid->highRiskCrisis;
    const auto &loot = session.profile().pendingRaid->loot;
    const auto salvage = std::find_if(loot.begin(), loot.end(), [&](const auto &drop) {
        return drop.requiresHighRisk && drop.resourcePointInstanceId == crisis.resourcePointInstanceId &&
            drop.definitionId == kBlockadeSalvageItemId;
    });
    ASSERT_NE(salvage, loot.end());
    const auto salvageId = salvage->assetId;
    ASSERT_NO_FATAL_FAILURE(move({salvage->position.x - 6.0F, salvage->position.y}));
    auto input = aim(salvage->position);
    tick(input, 0.0F);
    EXPECT_TRUE(session.raidLootAccessible(*salvage));
    input.interactJustPressed = input.interactPressed = true;
    tick(input, 0.0F);
    ASSERT_TRUE(assetIsCarried(session.profile(), salvageId)) << session.persistenceMessage();

    const auto signal = session.world().emergencyExtractionPoint();
    ASSERT_TRUE(signal);
    ASSERT_NO_FATAL_FAILURE(move(center(signal->bounds())));
    tick({}, 0.0F);
    ASSERT_TRUE(session.canCallSignalExtraction());
    tick(input);
    for (unsigned frame = 0; frame < 200 && session.state() == GameSessionState::InRaid; ++frame)
        tick();
    ASSERT_TRUE(session.profile().lastRaidResult) << session.persistenceMessage();
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_FALSE(session.profile().lastRaidResult->fuelExtractionPaid);
    ASSERT_TRUE(session.profile().assets.find(salvageId));
}

TEST_F(RaidFreightRuntimeTest, WarehouseKeyDoesNotGrantAccessOrStartHold)
{
    ASSERT_NO_FATAL_FAILURE(deploy(false));
    hold(20, true);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 0.0F);
    EXPECT_FALSE(session.canOpenRaidSceneContainer(crate));
    EXPECT_FALSE(session.nearbyRaidSceneContainer());
    EXPECT_FALSE(session.takeOpenedRaidFreightCrate());
}

TEST_F(RaidFreightRuntimeTest, FreshTwoSecondHoldOpensOnceAndDoesNotConsumeKey)
{
    ASSERT_NO_FATAL_FAILURE(deploy(true));
    EXPECT_FALSE(session.nearbyRaidSceneContainer());
    const auto compartment = ProfileContainerId::compartment(crate, 0);
    const auto contents = assetsInContainer(session.profile(), compartment);
    ASSERT_EQ(contents.size(), 1U);
    const auto *material = contents.front();
    EXPECT_EQ(material->definitionId, ItemDefinitionId{"item.loot.precision_components"});
    const auto carried = ProfileContainerId::compartment(bag, 0);
    const auto &content = publishedContentRegistry();
    const auto fit = findFirstProfileFit(session.profile(), content, carried,
        content.item(material->definitionId), material->orientation, material->instanceId);
    ASSERT_TRUE(fit);
    const InventoryCommand take = InventoryMoveCommand{material->instanceId, 0, {carried, *fit}};
    EXPECT_FALSE(session.queryRaidSceneContainerInventory(crate, take).canCommit);
    EXPECT_FALSE(session.executeRaidSceneContainerInventory(crate, take, "take-locked").succeeded);

    hold(8, true);
    EXPECT_FLOAT_EQ(progress(), 0.5F);
    session.update(aimAtCrate(), 0.0F);
    EXPECT_FLOAT_EQ(progress(), 0.0F);
    hold(16, false);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 0.0F);

    session.update(aimAtCrate(), 0.0F);
    hold(8, true);
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).applyControl(0.01F));
    hold(16, false);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 0.0F);

    session.update(aimAtCrate(), 0.0F);
    hold(15, true);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 15.0F / 16.0F);
    hold(1, false);
    EXPECT_TRUE(unlocked());
    EXPECT_EQ(session.takeOpenedRaidFreightCrate(), crate);
    EXPECT_FALSE(session.takeOpenedRaidFreightCrate());
    EXPECT_TRUE(assetIsCarried(session.profile(), key));
    EXPECT_EQ(session.nearbyRaidSceneContainer(), crate);
    EXPECT_TRUE(session.queryRaidSceneContainerInventory(crate, take).canCommit);
    EXPECT_TRUE(session.executeRaidSceneContainerInventory(crate, take, "take-unlocked").succeeded);
    EXPECT_TRUE(assetsInContainer(session.profile(), compartment).empty());
    EXPECT_EQ(session.nearbyRaidSceneContainer(), crate);
}

TEST_F(RaidFreightRuntimeTest, MovingAwayAndModalCancellationRequireNewPress)
{
    ASSERT_NO_FATAL_FAILURE(deploy(true));
    hold(8, true);
    EXPECT_FLOAT_EQ(progress(), 0.5F);
    ASSERT_NO_FATAL_FAILURE(move({cratePosition.x - 100.0F, cratePosition.y}));
    hold(8, false);
    EXPECT_FALSE(session.raidFreightUnlock());

    ASSERT_NO_FATAL_FAILURE(move({cratePosition.x - 50.0F, cratePosition.y}));
    hold(16, false);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 0.0F);

    session.update(aimAtCrate(), 0.0F);
    hold(8, true);
    EXPECT_FLOAT_EQ(progress(), 0.5F);
    auto modal = aimAtCrate();
    modal.inventoryOpen = modal.freezeObservationDirection = true;
    session.cancelRaidFreightUnlock();
    session.update(modal, kStepSeconds);
    hold(16, false);
    EXPECT_FALSE(unlocked());
    EXPECT_FLOAT_EQ(progress(), 0.0F);

    session.update(aimAtCrate(), 0.0F);
    hold(16, true);
    EXPECT_TRUE(unlocked());
    EXPECT_EQ(session.takeOpenedRaidFreightCrate(), crate);
}

TEST_F(RaidFreightRuntimeTest, BreachWarehouseToolsCannotOpenTheDoor)
{
    ASSERT_NO_FATAL_FAILURE(deploy(false, 0, WedgeStorage::Warehouse));
    ASSERT_TRUE(session.profile().pendingRaid->freightBreachEnabled);
    ASSERT_NO_FATAL_FAILURE(move({640.0F, 260.0F}));
    session.update(aimAtBreachDoor(), 0.0F);
    const auto projection = session.raidFreightBreach();
    ASSERT_TRUE(projection);
    EXPECT_FALSE(projection->hasTool);

    holdBreach(16, true);
    EXPECT_FLOAT_EQ(breachProgress(), 0.0F);
    EXPECT_TRUE(session.world().nearbyFreightBreachDoor());
    for (const auto id : wedges)
        EXPECT_NE(session.profile().assets.find(id), nullptr);
}

TEST_F(RaidFreightRuntimeTest, BreachFromEastConsumesOneToolAndDoorStaysOpenOnReentry)
{
    ASSERT_NO_FATAL_FAILURE(deploy(true, 0, WedgeStorage::Carried));
    ASSERT_NO_FATAL_FAILURE(move({840.0F, 260.0F}));
    session.update(aimAtBreachDoor(), 0.0F);
    ASSERT_TRUE(session.raidFreightBreach());
    EXPECT_TRUE(session.raidFreightBreach()->hasTool);

    holdBreach(11, true);
    EXPECT_FLOAT_EQ(breachProgress(), 11.0F / 12.0F);
    EXPECT_NE(session.profile().assets.find(wedges[0]), nullptr);
    session.update(aimAtBreachDoor(), 0.0F);
    EXPECT_FLOAT_EQ(breachProgress(), 0.0F);
    holdBreach(12, false);
    EXPECT_FLOAT_EQ(breachProgress(), 0.0F);

    holdBreach(11, true);
    const auto started = std::chrono::steady_clock::now();
    holdBreach(1, false);
    const std::chrono::duration<double, std::milli> finishingFrame =
        std::chrono::steady_clock::now() - started;
    RecordProperty("breach_finishing_frame_ms", std::to_string(finishingFrame.count()));
    EXPECT_FALSE(session.world().nearbyFreightBreachDoor());
    EXPECT_EQ(session.profile().assets.find(wedges[0]), nullptr);
    EXPECT_TRUE(assetIsCarried(session.profile(), wedges[1]));
    EXPECT_FALSE(unlocked());
    EXPECT_TRUE(assetIsCarried(session.profile(), key));
    EXPECT_TRUE(validateProfileState(session.profile(), publishedContentRegistry()).valid);
    holdBreach(16, false);
    EXPECT_TRUE(assetIsCarried(session.profile(), wedges[1]));

    const auto &rooms = session.profile().pendingRaid->interiors;
    const auto room = std::find_if(rooms.begin(), rooms.end(),
        [](const auto &entry) { return entry.id == kFreightRoom; });
    ASSERT_NE(room, rooms.end());
    const ContentRect exit = room->interiorExit;
    const ContentRect entrance = room->exteriorEntrance;
    ASSERT_NO_FATAL_FAILURE(move({exit.position.x + exit.size.x * 0.5F,
                                 exit.position.y + exit.size.y * 0.5F}));
    GameplayInput interact;
    interact.interactJustPressed = true;
    session.update(interact, 0.0F);
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    ASSERT_NO_FATAL_FAILURE(move({entrance.position.x + entrance.size.x * 0.5F,
                                 entrance.position.y + entrance.size.y * 0.5F}));
    session.update(interact, 0.0F);
    ASSERT_EQ(session.world().activeRaidSpaceId(), kFreightRoom);
    ASSERT_NO_FATAL_FAILURE(move({640.0F, 260.0F}));
    session.update(aimAtBreachDoor(), 0.0F);
    EXPECT_FALSE(session.world().nearbyFreightBreachDoor());
    EXPECT_TRUE(assetIsCarried(session.profile(), wedges[1]));
}

TEST_F(RaidFreightRuntimeTest, BreachDamageAndDroppedToolsRequireAFreshHold)
{
    ASSERT_NO_FATAL_FAILURE(deploy(false, 0, WedgeStorage::Carried));
    ASSERT_NO_FATAL_FAILURE(move({640.0F, 260.0F}));
    session.update(aimAtBreachDoor(), 0.0F);
    holdBreach(6, true);
    ASSERT_FLOAT_EQ(breachProgress(), 0.5F);
    const int health = session.world().player().health();
    EXPECT_FALSE(session.world().damagePlayer(1));
    ASSERT_EQ(session.world().player().health(), health - 1);
    holdBreach(12, false);
    EXPECT_FLOAT_EQ(breachProgress(), 0.0F);
    EXPECT_TRUE(assetIsCarried(session.profile(), wedges[0]));

    session.update(aimAtBreachDoor(), 0.0F);
    holdBreach(6, true);
    ASSERT_FLOAT_EQ(breachProgress(), 0.5F);
    const auto dropped = session.dropRaidBackpack(
        bag, {session.profile().revision, "breach-drop-bound-tool"});
    ASSERT_TRUE(dropped.succeeded) << dropped.message;
    holdBreach(12, false);
    EXPECT_TRUE(session.world().nearbyFreightBreachDoor());
    EXPECT_FALSE(session.raidFreightBreach()->hasTool);
    for (const auto id : wedges)
    {
        EXPECT_NE(session.profile().assets.find(id), nullptr);
        EXPECT_FALSE(assetIsCarried(session.profile(), id));
    }
}

TEST_F(RaidFreightRuntimeTest, BreachConsumptionAndRemainingToolFollowOrdinarySettlement)
{
    ASSERT_NO_FATAL_FAILURE(deploy(false, 0, WedgeStorage::Carried));
    ASSERT_NO_FATAL_FAILURE(move({640.0F, 260.0F}));
    session.update(aimAtBreachDoor(), 0.0F);
    holdBreach(12, true);
    ASSERT_EQ(session.profile().assets.find(wedges[0]), nullptr);
    ASSERT_TRUE(assetIsCarried(session.profile(), wedges[1]));

    const auto &content = publishedContentRegistry();
    const std::string settlement = session.profile().pendingRaid->settlementId;
    for (const auto outcome : {RaidResultOutcome::Extracted,
                               RaidResultOutcome::PlayerDead,
                               RaidResultOutcome::ActiveQuit})
    {
        SCOPED_TRACE(static_cast<int>(outcome));
        ProfileState resolved = session.profile();
        const auto receipt = settlePendingRaid(resolved, content, settlement, outcome);
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        EXPECT_FALSE(resolved.pendingRaid);
        EXPECT_EQ(resolved.assets.find(wedges[0]), nullptr);
        const bool extracted = outcome == RaidResultOutcome::Extracted;
        EXPECT_EQ(assetIsCarried(resolved, wedges[1]), extracted);
        ASSERT_TRUE(resolved.lastRaidResult);
        EXPECT_EQ(resolved.lastRaidResult->outcome, outcome);
        const auto &returned = resolved.lastRaidResult->returnedItemDefinitionIds;
        EXPECT_EQ(std::count(returned.begin(), returned.end(), kFreightBreachWedgesId),
            extracted ? 1 : 0);
        EXPECT_TRUE(validateProfileState(resolved, content).valid);
    }
}

TEST_F(RaidFreightRuntimeTest, ObservationAndTransferUseOriginalSceneAuthorityWithDenseAssets)
{
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::duration<double, std::milli>;
    constexpr std::size_t warmup{10};
    constexpr std::size_t samples{150};
    constexpr std::size_t p95Index{142};
    ASSERT_NO_FATAL_FAILURE(deploy(true, 200));
    ASSERT_GE(session.profile().assets.records().size(), 200U);

    const auto observationBuilds = session.world().indoorObservationBuildCount();
    std::vector<double> inspectionTimes;
    for (std::size_t index = 0; index < warmup + samples; ++index)
    {
        const auto start = Clock::now();
        const auto projection = session.raidFreightUnlock();
        const auto elapsed = Milliseconds(Clock::now() - start).count();
        ASSERT_TRUE(projection);
        ASSERT_EQ(projection->containerAssetId, crate);
        ASSERT_TRUE(projection->hasKey);
        if (index >= warmup) inspectionTimes.push_back(elapsed);
    }
    EXPECT_EQ(session.world().indoorObservationBuildCount(), observationBuilds);

    hold(16, true);
    ASSERT_EQ(session.takeOpenedRaidFreightCrate(), crate);
    const auto compartment = ProfileContainerId::compartment(crate, 0);
    const auto contents = assetsInContainer(session.profile(), compartment);
    ASSERT_EQ(contents.size(), 1U);
    const AssetInstanceId material = contents.front()->instanceId;
    const GridPosition crateCell = std::get<StoredAssetLocation>(contents.front()->location).origin;
    const auto carried = ProfileContainerId::compartment(bag, 0);
    const auto &definition = publishedContentRegistry().item(contents.front()->definitionId);
    const auto orientation = contents.front()->orientation;
    const auto bagCell = findFirstProfileFit(session.profile(), publishedContentRegistry(), carried,
        definition, orientation, material);
    ASSERT_TRUE(bagCell);

    const auto benchmarkTransfer = [&](AssetInstanceId root, GridPosition rootCell, const char *series)
    {
        const auto container = ProfileContainerId::compartment(root, 0);
        const auto transferObservationBuilds = session.world().indoorObservationBuildCount();
        std::vector<double> transferTimes;
        for (std::size_t index = 0; index < warmup + samples; ++index)
        {
            const bool taking = index % 2 == 0;
            const InventoryCommand move = InventoryMoveCommand{material, 0,
                {taking ? carried : container, taking ? *bagCell : rootCell}};
            const auto transaction = std::string(series) + "-" + std::to_string(index);
            const auto start = Clock::now();
            const auto plan = session.queryRaidSceneContainerInventory(root, move);
            const auto receipt = session.executeRaidSceneContainerInventory(root, move, transaction);
            const auto elapsed = Milliseconds(Clock::now() - start).count();
            ASSERT_TRUE(plan.canCommit) << plan.message;
            ASSERT_TRUE(receipt.succeeded) << receipt.message;
            ASSERT_EQ(assetIsCarried(session.profile(), material), taking);
            if (index >= warmup) transferTimes.push_back(elapsed);
        }
        ASSERT_EQ(transferTimes.size(), samples);
        std::sort(transferTimes.begin(), transferTimes.end());
        RecordProperty(series, std::to_string(transferTimes[p95Index]));
        EXPECT_EQ(session.world().indoorObservationBuildCount(), transferObservationBuilds);
    };

    ASSERT_EQ(inspectionTimes.size(), samples);
    std::sort(inspectionTimes.begin(), inspectionTimes.end());
    RecordProperty("freight_inspection_p95_ms", std::to_string(inspectionTimes[p95Index]));
    ASSERT_NO_FATAL_FAILURE(benchmarkTransfer(crate, crateCell, "freight_transfer_p95_ms"));

    const InventoryCommand take = InventoryMoveCommand{material, 0, {carried, *bagCell}};
    ASSERT_TRUE(session.executeRaidSceneContainerInventory(crate, take, "take-for-ordinary-benchmark").succeeded);
    const auto &records = session.profile().assets.records();
    const auto ordinary = std::find_if(records.begin(), records.end(), [&](const auto &entry) {
        const auto *scene = std::get_if<RaidSceneContainerAssetLocation>(&entry.second.location);
        return entry.first != crate && scene && scene->spaceId == kFreightRoom;
    });
    ASSERT_NE(ordinary, records.end());
    const AssetInstanceId ordinaryId = ordinary->first;
    const auto position = std::get<RaidSceneContainerAssetLocation>(ordinary->second.location).position;
    ASSERT_NO_FATAL_FAILURE(move({position.x - 50.0F, position.y}));
    auto aim = aimAtCrate();
    aim.aimWorldPosition = position;
    session.update(aim, 0.0F);
    ASSERT_TRUE(session.canOpenRaidSceneContainer(ordinaryId));
    const auto ordinaryCompartment = ProfileContainerId::compartment(ordinaryId, 0);
    const auto ordinaryCell = findFirstProfileFit(session.profile(), publishedContentRegistry(), ordinaryCompartment,
        definition, orientation, material);
    ASSERT_TRUE(ordinaryCell);
    const InventoryCommand deposit = InventoryMoveCommand{material, 0, {ordinaryCompartment, *ordinaryCell}};
    ASSERT_TRUE(session.executeRaidSceneContainerInventory(ordinaryId, deposit, "setup-ordinary-benchmark").succeeded);
    ASSERT_NO_FATAL_FAILURE(benchmarkTransfer(ordinaryId, *ordinaryCell, "ordinary_transfer_p95_ms"));
}
} // namespace
