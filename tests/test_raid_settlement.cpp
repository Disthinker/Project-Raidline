#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <utility>

#include "raid_settlement.h"

namespace
{

    void place(
        GridInventory &inventory,
        ItemInstanceId instanceId,
        ItemId definitionId,
        GridPosition origin,
        std::uint32_t quantity = 1)
    {
        ItemInstance item{
            instanceId,
            definitionId,
            quantity};
        ASSERT_TRUE(inventory.tryPlace(
            std::move(item),
            origin));
    }

}

TEST(RaidSettlementTest, DefaultStashIsTwentyByTwelve)
{
    RaidSettlement settlement;

    EXPECT_EQ(settlement.stash().inventory().width(), 20);
    EXPECT_EQ(settlement.stash().inventory().height(), 12);
    EXPECT_EQ(settlement.state(), RaidSettlementState::Pending);
    EXPECT_FALSE(settlement.isComplete());
}

TEST(RaidSettlementTest, NonTerminalRaidDoesNotMutateInventory)
{
    RaidSettlement settlement;
    GridInventory player{{3, 1}};
    place(player, 1, ItemId::Pistol, {0, 0});

    EXPECT_EQ(
        settlement.settle(RaidSessionState::InRaid, player),
        RaidSettlementAttempt::NotTerminal);
    EXPECT_EQ(player.placedItems().size(), 1U);
    EXPECT_EQ(settlement.stash().stackCount(), 0U);
    EXPECT_EQ(settlement.summary(), (RaidSettlementSummary{}));
}

TEST(RaidSettlementTest, ExtractionMovesExactStacksAndRecordsSummary)
{
    RaidSettlement settlement;
    GridInventory player{{6, 4}};
    place(player, 2, ItemId::Pistol, {0, 0});
    place(player, 3, ItemId::Ammo9mm, {2, 0}, 41);

    ItemInstance rifle{4, ItemId::Rifle};
    ASSERT_TRUE(
        rifle.trySetOrientation(ItemOrientation::Degrees90));
    ASSERT_TRUE(player.tryPlace(std::move(rifle), {3, 0}));

    EXPECT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Completed);

    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(settlement.state(), RaidSettlementState::Extracted);
    EXPECT_TRUE(settlement.isComplete());
    EXPECT_EQ(settlement.summary(),
              (RaidSettlementSummary{3, 43}));
    EXPECT_EQ(settlement.stash().stackCount(), 3U);
    EXPECT_EQ(settlement.stash().unitCount(), 43U);
    EXPECT_EQ(settlement.stash().inventory().quantityOf(3), 41U);

    const auto &stored =
        settlement.stash().inventory().placedItems();
    ASSERT_EQ(stored.size(), 3U);
    EXPECT_EQ(stored[0].item.instanceId(), 2U);
    EXPECT_EQ(stored[1].item.instanceId(), 3U);
    EXPECT_EQ(stored[2].item.instanceId(), 4U);
    EXPECT_EQ(stored[2].item.orientation(),
              ItemOrientation::Degrees90);
}

TEST(RaidSettlementTest, DeathClearsCarriedGoodsAndRecordsLoss)
{
    RaidSettlement settlement;
    GridInventory player{{2, 1}};
    place(player, 5, ItemId::Ammo9mm, {0, 0}, 24);
    place(player, 6, ItemId::Cola, {1, 0});

    EXPECT_EQ(
        settlement.settle(RaidSessionState::PlayerDead, player),
        RaidSettlementAttempt::Completed);
    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(settlement.state(), RaidSettlementState::PlayerDead);
    EXPECT_EQ(settlement.summary(),
              (RaidSettlementSummary{2, 25}));
    EXPECT_EQ(settlement.stash().stackCount(), 0U);
}

TEST(RaidSettlementTest, TimeoutClearsCarriedGoodsAndRecordsLoss)
{
    RaidSettlement settlement;
    GridInventory player{{1, 1}};
    place(player, 7, ItemId::Cola, {0, 0});

    EXPECT_EQ(
        settlement.settle(RaidSessionState::RaidEnded, player),
        RaidSettlementAttempt::Completed);
    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(settlement.state(), RaidSettlementState::RaidEnded);
    EXPECT_EQ(settlement.summary(),
              (RaidSettlementSummary{1, 1}));
}

TEST(RaidSettlementTest, CompletedSettlementIsSticky)
{
    RaidSettlement settlement;
    GridInventory player{{2, 1}};
    place(player, 8, ItemId::Cola, {0, 0});
    ASSERT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Completed);

    place(player, 9, ItemId::Cola, {1, 0});

    EXPECT_EQ(
        settlement.settle(RaidSessionState::PlayerDead, player),
        RaidSettlementAttempt::AlreadyCompleted);
    EXPECT_EQ(settlement.state(), RaidSettlementState::Extracted);
    EXPECT_EQ(settlement.summary(),
              (RaidSettlementSummary{1, 1}));
    EXPECT_EQ(player.placedItems().size(), 1U);
    EXPECT_EQ(settlement.stash().stackCount(), 1U);
}

TEST(RaidSettlementTest, BlockedExtractionPreservesPlayerInventory)
{
    RaidSettlement settlement{{1, 1}};
    GridInventory player{{2, 1}};
    place(player, 10, ItemId::Pistol, {0, 0});

    EXPECT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Blocked);
    EXPECT_EQ(settlement.state(), RaidSettlementState::Blocked);
    EXPECT_FALSE(settlement.isComplete());
    EXPECT_EQ(player.placedItems().size(), 1U);
    EXPECT_EQ(player.occupantAt({0, 0}),
              (std::optional<ItemInstanceId>{10}));
    EXPECT_EQ(settlement.stash().stackCount(), 0U);
}

TEST(RaidSettlementTest, BlockedExtractionCanRetryAfterCapacityChanges)
{
    RaidSettlement settlement{{1, 1}};
    GridInventory player{{1, 1}};
    place(settlement.stash().inventory(), 11, ItemId::Cola, {0, 0});
    place(player, 12, ItemId::Ammo9mm, {0, 0}, 9);

    ASSERT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Blocked);
    ASSERT_TRUE(settlement.stash().inventory().remove(11).has_value());

    EXPECT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Completed);
    EXPECT_TRUE(player.placedItems().empty());
    EXPECT_EQ(settlement.stash().inventory().quantityOf(12), 9U);
    EXPECT_EQ(settlement.summary(),
              (RaidSettlementSummary{1, 9}));
}

TEST(RaidSettlementTest, DuplicateStableIdBlocksWithoutMutation)
{
    RaidSettlement settlement{{2, 1}};
    GridInventory player{{1, 1}};
    place(settlement.stash().inventory(), 13, ItemId::Cola, {0, 0});
    place(player, 13, ItemId::Ammo9mm, {0, 0}, 7);

    EXPECT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Blocked);
    EXPECT_EQ(player.placedItems().size(), 1U);
    EXPECT_EQ(player.quantityOf(13), 7U);
    EXPECT_EQ(settlement.stash().stackCount(), 1U);
    EXPECT_EQ(settlement.stash().inventory().quantityOf(13), 1U);
}

TEST(RaidSettlementTest, EmptyExtractionCompletesSuccessfully)
{
    RaidSettlement settlement;
    GridInventory player{{1, 1}};

    EXPECT_EQ(
        settlement.settle(RaidSessionState::Extracted, player),
        RaidSettlementAttempt::Completed);
    EXPECT_EQ(settlement.summary(), (RaidSettlementSummary{}));
    EXPECT_EQ(settlement.state(), RaidSettlementState::Extracted);
}

TEST(RaidSettlementTest, InvalidStashDimensionsAreRejected)
{
    EXPECT_THROW(
        RaidSettlement((InventoryGridSize{0, 1})),
        std::invalid_argument);
}

TEST(RaidSettlementTest, StateNamesAreStableForDebugOutput)
{
    EXPECT_STREQ(
        raidSettlementStateName(RaidSettlementState::Pending),
        "Pending");
    EXPECT_STREQ(
        raidSettlementStateName(RaidSettlementState::Blocked),
        "Blocked");
    EXPECT_STREQ(
        raidSettlementStateName(RaidSettlementState::Extracted),
        "Extracted");
    EXPECT_STREQ(
        raidSettlementStateName(RaidSettlementState::PlayerDead),
        "PlayerDead");
    EXPECT_STREQ(
        raidSettlementStateName(RaidSettlementState::RaidEnded),
        "RaidEnded");
}
