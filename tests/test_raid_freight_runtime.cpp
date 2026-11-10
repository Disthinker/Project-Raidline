#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "alpha_content_ids.h"
#include "game_session.h"

namespace
{
const MapDefinitionId kFrontier{"map.raid.frontier_exchange"};
const RaidSpaceDefinitionId kFreightRoom{"raid_space.frontier_exchange.freight_service_bay"};
constexpr float kStepSeconds{0.125F};

class RaidFreightRuntimeTest : public testing::Test
{
protected:
    void deploy(bool carryKey, int additionalWarehouseStacks = 0)
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

    GameSession session;
    AssetInstanceId bag{};
    AssetInstanceId crate{};
    AssetInstanceId key{};
    Vec2 cratePosition{};
};

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
