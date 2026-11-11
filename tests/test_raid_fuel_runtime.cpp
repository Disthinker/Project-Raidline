#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string_view>
#include <thread>

#include "alpha_content_ids.h"
#include "game_session.h"
#include "profile_ui_test_wait.h"
#include "test_temp_directory.h"

namespace
{
const MapDefinitionId kFrontier{"map.raid.frontier_exchange"};
const RaidSpaceDefinitionId kFreightRoom{"raid_space.frontier_exchange.freight_service_bay"};
const ItemDefinitionId kBag{"item.container.backpack_expedition"};
const ItemDefinitionId kArmor{"item.protective_gear.body_armor_heavy"};
const ItemDefinitionId kWeapon{"item.weapon.lmg_7_62x51_service"};
constexpr float kStep{0.25F};

class RaidFuelRuntimeTest : public testing::Test
{
protected:
    void begin(bool persistent)
    {
        auto profile = makeNewPublishedProfile("fuel-runtime", content);
        profile.tutorial = TutorialProgress::Complete;
        const auto equip = [&](const ItemDefinitionId &definition, EquipmentSlotKind slot,
                               std::string_view transaction)
        {
            const auto found = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
                [&](const auto &entry) { return entry.second.definitionId == definition; });
            EXPECT_NE(found, profile.assets.records().end());
            if (found == profile.assets.records().end()) return AssetInstanceId{};
            const auto id = found->first;
            EXPECT_TRUE(executeInventory(profile, content, InventoryEquipCommand{id, slot},
                {profile.revision, std::string{transaction}}).succeeded);
            return id;
        };
        bag = equip(kBag, EquipmentSlotKind::Backpack, "equip-bag");
        ASSERT_NE(bag, 0U);
        ASSERT_NE(equip(kArmor, EquipmentSlotKind::BodyArmor, "equip-armor"), 0U);
        ASSERT_NE(equip(kWeapon, EquipmentSlotKind::PrimaryWeapon, "equip-weapon"), 0U);
        if (persistent)
        {
            ASSERT_NO_THROW(directory.claim("raidline-fuel-runtime-", "two-sorties"));
            const auto saved = SaveRepository(directory.path()).save(profile, content.contentVersion());
            ASSERT_TRUE(saved.succeeded) << saved.message;
            session.configurePersistence(directory.path());
            ASSERT_TRUE(session.continueProfile()) << session.persistenceMessage();
        }
        else
        {
            ASSERT_TRUE(session.startNewProfile("fuel-runtime"));
            const_cast<ProfileState &>(session.profile()) = std::move(profile);
        }
    }

    void deploy()
    {
        ASSERT_TRUE(session.deployAlpha(42U, kFrontier)) << session.persistenceMessage();
        ASSERT_TRUE(session.raidFuelExtractionEnabled());
        ASSERT_TRUE(session.profile().pendingRaid->fuelExtractionLootAssetId);
        ASSERT_GT(session.currentRaidCarriedWeightGrams(), 22000U);
    }

    void move(Vec2 center)
    {
        auto &player = const_cast<Player &>(session.world().player());
        const auto half = player.size() * 0.5F;
        ASSERT_TRUE(player.setPosition({center.x - half, center.y - half}));
    }

    static Vec2 center(const auto &bounds)
    {
        return {bounds.position.x + bounds.size.x * 0.5F,
                bounds.position.y + bounds.size.y * 0.5F};
    }

    void tick(const GameplayInput &input, float seconds)
    {
        for (const auto &enemy : session.world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        session.update(input, seconds);
    }

    void progress(unsigned steps)
    {
        for (unsigned i = 0; i < steps; ++i) tick({}, kStep);
    }

    void activateHighRisk()
    {
        const auto control = session.world().highRiskControlPoint();
        ASSERT_TRUE(control);
        ASSERT_NO_FATAL_FAILURE(move(center(*control)));
        GameplayInput input;
        input.interactJustPressed = input.interactPressed = true;
        for (int i = 0; i < 16; ++i)
        {
            tick(input, kStep);
            input.interactJustPressed = false;
        }
        ASSERT_EQ(session.world().raidSession().phase(), RaidPhase::HighRisk);
    }

    AssetInstanceId createFuel(const ProfileContainerId &container)
    {
        auto &profile = const_cast<ProfileState &>(session.profile());
        const auto &item = content.item(kRaidFuelCanId);
        const auto fit = findFirstProfileFit(profile, content, container, item, ItemOrientation::Degrees0);
        EXPECT_TRUE(fit);
        return fit ? profile.assets.create(item, StoredAssetLocation{container, *fit}) : 0;
    }

    bool moveFuel(AssetInstanceId fuel, const ProfileContainerId &container, std::string transaction)
    {
        const auto fit = findFirstProfileFit(session.profile(), content, container,
            content.item(kRaidFuelCanId), ItemOrientation::Degrees0);
        EXPECT_TRUE(fit);
        if (!fit) return false;
        const auto moved = session.executeProfileInventory(
            InventoryMoveCommand{fuel, 0U, {container, *fit}}, std::move(transaction));
        EXPECT_TRUE(moved.succeeded) << moved.message;
        return moved.succeeded && waitForProfileUiDurability(session);
    }

    raidline::test::TemporaryDirectory directory;
    GameSession session;
    const ContentRegistry &content = publishedContentRegistry();
    AssetInstanceId bag{};
};

TEST_F(RaidFuelRuntimeTest, WarehouseFuelAndInterruptedBindingCannotCompleteTheCountdown)
{
    ASSERT_NO_FATAL_FAILURE(begin(false));
    const auto backpack = ProfileContainerId::compartment(bag, 0);
    const auto warehouse = ProfileContainerId::stash();
    const auto inStash = createFuel(warehouse);
    ASSERT_NE(inStash, 0U);
    ASSERT_NO_FATAL_FAILURE(deploy());
    ASSERT_NO_FATAL_FAILURE(activateHighRisk());
    const auto exit = session.world().conditionalExtractionPoint();
    ASSERT_TRUE(exit);
    ASSERT_NO_FATAL_FAILURE(move(center(exit->bounds())));
    progress(1);
    EXPECT_EQ(session.world().raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FALSE(session.conditionalExtractionEligible());

    const auto first = createFuel(backpack);
    const auto second = createFuel(backpack);
    ASSERT_GT(first, 0U);
    ASSERT_GT(second, first);
    progress(4);
    EXPECT_EQ(session.world().raidSession().extractionRoute(), RaidExtractionRoute::Fuel);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionTimeElapsed(), 1.0F);

    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto fit = findFirstProfileFit(profile, content, warehouse, content.item(kRaidFuelCanId),
        ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    ASSERT_TRUE(executeInventory(profile, content,
        InventoryMoveCommand{first, 0U, {warehouse, *fit}}, {profile.revision, "fuel-moved-away"}).succeeded);
    progress(1);
    EXPECT_EQ(session.world().raidSession().state(), RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionProgress(), 0.0F);

    progress(4);
    ASSERT_TRUE(const_cast<Player &>(session.world().player()).applyControl(0.01F));
    progress(1);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionProgress(), 0.0F);
    progress(4);
    GameplayInput map;
    map.freezeObservationDirection = true;
    tick(map, 0.0F);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionProgress(), 0.0F);
    progress(4);
    session.cancelRaidFuelExtraction();
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionProgress(), 0.0F);

    progress(24);
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_TRUE(session.profile().lastRaidResult->fuelExtractionPaid);
    EXPECT_TRUE(session.profile().assets.find(inStash));
    EXPECT_TRUE(session.profile().assets.find(first));
    EXPECT_FALSE(session.profile().assets.find(second));
}

TEST_F(RaidFuelRuntimeTest, TwoSortiesPersistOnlySuccessfulFuelPaymentThroughRealSaveRetry)
{
    ASSERT_NO_FATAL_FAILURE(begin(true));
    ASSERT_NO_FATAL_FAILURE(deploy());
    const auto fuel = *session.profile().pendingRaid->fuelExtractionLootAssetId;
    const auto &raid = *session.profile().pendingRaid;
    const auto foundRoom = std::find_if(raid.interiors.begin(), raid.interiors.end(),
        [](const auto &room) { return room.id == kFreightRoom; });
    ASSERT_NE(foundRoom, raid.interiors.end());
    const auto room = *foundRoom;
    const auto foundLoot = std::find_if(raid.loot.begin(), raid.loot.end(),
        [fuel](const auto &loot) { return loot.assetId == fuel; });
    ASSERT_NE(foundLoot, raid.loot.end());
    const auto fuelPoint = foundLoot->position;

    GameplayInput interact;
    interact.interactJustPressed = true;
    ASSERT_NO_FATAL_FAILURE(move(center(room.exteriorEntrance)));
    tick(interact, 0.0F);
    ASSERT_EQ(session.world().activeRaidSpaceId(), kFreightRoom);
    ASSERT_NO_FATAL_FAILURE(move(fuelPoint));
    interact.aimWorldPosition = {fuelPoint.x + 60.0F, fuelPoint.y};
    interact.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
    tick(interact, 0.0F);
    ASSERT_TRUE(isCarriedRaidFuel(session.profile(), fuel)) << session.persistenceMessage();

    ASSERT_NO_FATAL_FAILURE(move(center(room.interiorExit)));
    tick(interact, 0.0F);
    ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    ASSERT_NO_FATAL_FAILURE(move(center(session.world().extractionPoint().bounds())));
    progress(12);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_FALSE(session.profile().lastRaidResult->fuelExtractionPaid);
    ASSERT_TRUE(moveFuel(fuel, ProfileContainerId::stash(), "stash-fuel"));
    ASSERT_TRUE(moveFuel(fuel, ProfileContainerId::compartment(bag, 0), "pack-fuel"));

    ASSERT_NO_FATAL_FAILURE(deploy());
    ASSERT_NO_FATAL_FAILURE(activateHighRisk());
    const auto exit = session.world().conditionalExtractionPoint();
    ASSERT_TRUE(exit);
    ASSERT_NO_FATAL_FAILURE(move(center(exit->bounds())));
    progress(4);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionTimeElapsed(), 1.0F);
    ASSERT_NO_FATAL_FAILURE(move({exit->bounds().position.x - 48.0F, center(exit->bounds()).y}));
    progress(1);
    EXPECT_FLOAT_EQ(session.world().raidSession().extractionProgress(), 0.0F);
    EXPECT_TRUE(isCarriedRaidFuel(session.profile(), fuel));

    ASSERT_NO_FATAL_FAILURE(move(center(exit->bounds())));
    progress(23);
    ASSERT_FLOAT_EQ(session.world().raidSession().extractionTimeElapsed(), 5.75F);
    const auto blocker = directory.path() / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(blocker));
    progress(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (session.state() != GameSessionState::SettlementBlocked &&
           std::chrono::steady_clock::now() < deadline)
    {
        session.updateInventoryPersistence(0);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    ASSERT_EQ(session.state(), GameSessionState::SettlementBlocked);
    ASSERT_TRUE(session.profile().pendingRaid);
    EXPECT_TRUE(isCarriedRaidFuel(session.profile(), fuel));
    EXPECT_FALSE(session.profile().lastRaidResult);

    ASSERT_TRUE(std::filesystem::remove(blocker));
    const auto retry = session.retryProfileUiCommit();
    ASSERT_TRUE(retry.accepted) << retry.message;
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.profile().lastRaidResult);
    EXPECT_TRUE(session.profile().lastRaidResult->fuelExtractionPaid);
    EXPECT_FALSE(session.profile().assets.find(fuel));
    EXPECT_TRUE(session.profile().assets.find(bag));
    const auto &returned = session.profile().lastRaidResult->returnedItemDefinitionIds;
    EXPECT_EQ(std::count(returned.begin(), returned.end(), kRaidFuelCanId), 0);

    GameSession reopened;
    reopened.configurePersistence(directory.path());
    ASSERT_TRUE(reopened.continueProfile()) << reopened.persistenceMessage();
    ASSERT_TRUE(reopened.profile().lastRaidResult);
    EXPECT_TRUE(reopened.profile().lastRaidResult->fuelExtractionPaid);
    EXPECT_FALSE(reopened.profile().assets.find(fuel));
    EXPECT_TRUE(reopened.profile().assets.find(bag));
}
}
