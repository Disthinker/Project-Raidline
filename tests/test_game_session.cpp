#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "game_session.h"

namespace
{
    class SequenceLootRandomSource final
        : public LootRandomSource
    {
    public:
        explicit SequenceLootRandomSource(
            std::vector<std::uint32_t> values)
            : values_{std::move(values)}
        {
        }

        std::uint32_t next(
            std::uint32_t upperExclusive) override
        {
            if (position_ >= values_.size())
            {
                throw std::runtime_error{
                    "SequenceLootRandomSource exhausted"};
            }

            const std::uint32_t value =
                values_[position_++];

            if (value >= upperExclusive)
            {
                throw std::out_of_range{
                    "SequenceLootRandomSource value is out of range"};
            }

            return value;
        }

    private:
        std::vector<std::uint32_t> values_;
        std::size_t position_{};
    };

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

    void pickUpDefaultMedkit(
        GameSession &session)
    {
        session.update(moveLeftInput(), 0.5F);
        session.update(moveDownInput(), 0.25F);

        GameplayInput interact{};
        interact.interactJustPressed = true;
        session.update(interact, 0.0F);

        ASSERT_EQ(
            session.world().inventory().placedItems().size(),
            1U);
        EXPECT_EQ(
            session.world().inventory().placedItems().front()
                .item.definitionId(),
            ItemId::Medkit);
    }

    void extractFromDefaultMedkitPosition(
        GameSession &session)
    {
        session.update(moveLeftInput(), 1.5F);
        session.update(moveDownInput(), 0.45F);
        session.update(GameplayInput{}, 3.0F);

        ASSERT_EQ(
            session.world().raidSession().state(),
            RaidSessionState::Extracted);
    }

    void moveFromCabinetToExtraction(
        GameSession &session)
    {
        session.update(moveLeftInput(), 3.0F);
        session.update(moveDownInput(), 0.65F);
        session.update(GameplayInput{}, 3.0F);

        ASSERT_EQ(
            session.world().raidSession().state(),
            RaidSessionState::Extracted);
    }
}

TEST(GameSessionTest, DefaultSessionStartsRaidOneWithEmptyStash)
{
    GameSession session;

    EXPECT_EQ(session.state(), GameSessionState::InRaid);
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(
        session.world().raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_EQ(session.stash().stackCount(), 0U);
    EXPECT_EQ(session.nextItemInstanceId(), 7U);
    EXPECT_FALSE(session.canStartNextRaid());
}

TEST(GameSessionTest, BaseClockUsesScaledSimulationTimeAndDailyDemand)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("base-clock-daily-demand"));
    const ProfileRevision revision = session.profile().revision;

    session.advanceBaseWorldClock(0.5F);
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute);
    session.advanceBaseWorldClock(0.5F);
    EXPECT_EQ(
        session.profile().worldClock.elapsedWorldMinutes,
        kInitialWorldMinute + 1U);

    session.advanceBaseWorldClock(959.0F);
    const WorldClockProjection clock = session.worldClockProjection();
    EXPECT_EQ(clock.day, 2U);
    EXPECT_EQ(clock.hour, 0U);
    EXPECT_EQ(session.profile().revision, revision);
    EXPECT_EQ(
        session.profile().baseResources.pool,
        (BaseResourceBundle{32, 34, 35, 36}));
    EXPECT_EQ(
        session.profile().baseResources.resolvedDemandCycleCount,
        1U);
    EXPECT_EQ(session.profile().baseMorale.resolvedDayCount, 1U);
    EXPECT_EQ(session.profile().baseMorale.tier, BaseMoraleTier::Stable);
    EXPECT_EQ(session.profile().baseMorale.trend, BaseMoraleTrend::Steady);
}

TEST(GameSessionTest, RaidTravelPreviewProjectsSelectedMapWithoutMutation)
{
    GameSession session;
    ASSERT_TRUE(session.startNewProfile("session-travel-preview"));
    const std::uint64_t fingerprint =
        profileStateFingerprint(session.profile());

    const auto preview = session.raidTravelPreview(
        MapDefinitionId{"map.raid.riverside"});

    ASSERT_TRUE(preview.has_value());
    EXPECT_EQ(preview->outboundMinutes, 90U);
    EXPECT_EQ(preview->arrival.hour, 9U);
    EXPECT_EQ(preview->arrival.minute, 30U);
    EXPECT_EQ(profileStateFingerprint(session.profile()), fingerprint);
    EXPECT_FALSE(session.raidTravelPreview(
        MapDefinitionId{"map.raid.missing"}).has_value());
}

TEST(GameSessionTest, ActiveRaidRejectsRestartWithoutMutation)
{
    GameSession session;
    const GameplayWorld *const originalWorld =
        &session.world();
    const ItemInstanceId originalNextId =
        session.nextItemInstanceId();

    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(&session.world(), originalWorld);
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(session.nextItemInstanceId(), originalNextId);
    EXPECT_EQ(session.state(), GameSessionState::InRaid);
}

TEST(GameSessionTest, ExtractionPersistsStashAndStartsFreshSecondRaid)
{
    GameSession session;
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);

    ASSERT_EQ(session.state(), GameSessionState::BetweenRaids);
    ASSERT_EQ(session.stash().stackCount(), 1U);
    ASSERT_EQ(session.stash().unitCount(), 1U);
    const ItemInstanceId storedId =
        session.stash().inventory().placedItems().front()
            .item.instanceId();
    const ItemInstanceId secondRaidFirstId =
        session.nextItemInstanceId();

    ASSERT_TRUE(session.canStartNextRaid());
    ASSERT_TRUE(session.startNextRaid());

    EXPECT_EQ(session.state(), GameSessionState::InRaid);
    EXPECT_EQ(session.raidNumber(), 2U);
    EXPECT_FALSE(session.canStartNextRaid());
    EXPECT_EQ(
        session.world().raidSession().state(),
        RaidSessionState::InRaid);
    EXPECT_FLOAT_EQ(
        session.world().raidSession().raidTimeRemaining(),
        180.0F);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_FALSE(session.world().storageCabinet().isSearched());
    EXPECT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(
        session.stash().inventory().placedItems().front()
            .item.instanceId(),
        storedId);

    ASSERT_FALSE(session.world().groundItems().empty());
    EXPECT_EQ(
        session.world().groundItems().front()
            .item().instanceId(),
        secondRaidFirstId);
    EXPECT_TRUE(std::all_of(
        session.world().groundItems().begin(),
        session.world().groundItems().end(),
        [storedId](const GroundItem &groundItem)
        {
            return groundItem.item().instanceId() > storedId;
        }));
    EXPECT_EQ(
        session.nextItemInstanceId(),
        secondRaidFirstId + 6U);
}

TEST(GameSessionTest, SearchTransferExtractionAndRestartFormOneSuccessSlice)
{
    GameSession session{std::vector<EnemySpawn>{}};

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    session.update(moveRight, 1.0F);
    ASSERT_TRUE(
        session.world().canInteractWithContainer());

    SequenceLootRandomSource random{
        {0, 99, 0, 99, 20}};
    ASSERT_TRUE(
        session.world().searchStorageCabinet(random));
    ASSERT_EQ(
        session.world().containerInventory()
            .placedItems()
            .size(),
        2U);

    const PlacedItem &cabinetItem =
        session.world().containerInventory()
            .placedItems()
            .front();
    const ItemInstanceId transferredId =
        cabinetItem.item.instanceId();
    const std::uint32_t transferredQuantity =
        cabinetItem.item.quantity();

    ASSERT_TRUE(
        session.world().transferInventoryItemQuantity(
            false,
            transferredId,
            transferredQuantity));
    ASSERT_EQ(
        session.world().inventory()
            .placedItems()
            .size(),
        1U);

    moveFromCabinetToExtraction(session);

    ASSERT_EQ(
        session.state(),
        GameSessionState::BetweenRaids);
    ASSERT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(session.stash().unitCount(), 1U);
    EXPECT_EQ(
        session.stash().inventory()
            .placedItems()
            .front()
            .item.instanceId(),
        transferredId);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 2U);
    EXPECT_EQ(session.world().player().health(), 3);
    EXPECT_EQ(session.world().player().maxHealth(), 3);
    EXPECT_TRUE(
        session.world().inventory()
            .placedItems()
            .empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
}

TEST(GameSessionTest, SecondRaidLossDoesNotRemovePreviousStash)
{
    GameSession session;
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);
    ASSERT_TRUE(session.startNextRaid());

    pickUpDefaultMedkit(session);
    ASSERT_TRUE(session.world().damagePlayer(3));
    session.update(GameplayInput{}, 0.0F);

    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_EQ(
        session.settlement().state(),
        RaidSettlementState::PlayerDead);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(session.stash().unitCount(), 1U);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 3U);
    EXPECT_EQ(session.world().player().health(), 3);
    EXPECT_TRUE(
        session.world().inventory()
            .placedItems()
            .empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
}

TEST(GameSessionTest, BlockedSettlementCannotStartAnotherRaid)
{
    GameSession session{{1, 1}};
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);

    // 1x2 Medkit cannot fit into the 1x1 Stash, so extraction remains
    // retryable and the player inventory is preserved.
    EXPECT_EQ(
        session.state(),
        GameSessionState::SettlementBlocked);
    EXPECT_EQ(
        session.settlement().state(),
        RaidSettlementState::Blocked);
    EXPECT_FALSE(session.canStartNextRaid());
    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 1U);
    EXPECT_EQ(
        session.world().inventory().placedItems().size(),
        1U);
    EXPECT_EQ(session.stash().stackCount(), 0U);
}

TEST(GameSessionTest, RestartIsAcceptedOnlyOncePerCompletedRaid)
{
    GameSession session;
    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);

    ASSERT_TRUE(session.startNextRaid());
    EXPECT_FALSE(session.startNextRaid());
    EXPECT_EQ(session.raidNumber(), 2U);
}

TEST(GameSessionTest, NextRaidStartsWithFreshWeaponFireState)
{
    GameSession freshSession;
    const float freshSpread =
        freshSession.world().weaponSpreadDegrees();
    GameSession session;
    GameplayInput fire{};
    fire.fireJustPressed = true;
    fire.firePressed = true;
    session.update(fire, 0.0F);

    ASSERT_GT(session.world().weaponSpreadDegrees(), 0.0F);
    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);
    ASSERT_TRUE(session.startNextRaid());

    EXPECT_FLOAT_EQ(session.world().weaponSpreadDegrees(), freshSpread);
    EXPECT_FLOAT_EQ(session.world().weaponVisualRecoilPixels(), 0.0F);
}

TEST(GameSessionTest, StateNamesAreStableForDebugOutput)
{
    EXPECT_STREQ(
        gameSessionStateName(GameSessionState::InRaid),
        "InRaid");
    EXPECT_STREQ(
        gameSessionStateName(
            GameSessionState::SettlementBlocked),
        "SettlementBlocked");
    EXPECT_STREQ(
        gameSessionStateName(GameSessionState::BetweenRaids),
        "BetweenRaids");
}
