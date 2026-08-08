#include <gtest/gtest.h>

#include <algorithm>

#include "game_session.h"

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

TEST(GameSessionTest, SecondRaidLossDoesNotRemovePreviousStash)
{
    GameSession session;
    pickUpDefaultMedkit(session);
    extractFromDefaultMedkitPosition(session);
    ASSERT_TRUE(session.startNextRaid());

    pickUpDefaultMedkit(session);
    ASSERT_TRUE(session.world().markPlayerDead());
    session.update(GameplayInput{}, 0.0F);

    EXPECT_EQ(session.state(), GameSessionState::BetweenRaids);
    EXPECT_EQ(
        session.settlement().state(),
        RaidSettlementState::PlayerDead);
    EXPECT_TRUE(
        session.world().inventory().placedItems().empty());
    EXPECT_EQ(session.stash().stackCount(), 1U);
    EXPECT_EQ(session.stash().unitCount(), 1U);
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
