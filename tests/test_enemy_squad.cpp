#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include "enemy_squad.h"

namespace
{
    EnemySquadConfig singleAttackSlotConfig()
    {
        EnemySquadConfig config;
        config.maximumConcurrentAttackers = 1U;
        return config;
    }

    EnemySquadMemberSnapshot alertedMember(
        Vec2 position,
        EnemyAttackPhase phase = EnemyAttackPhase::Idle,
        bool hasAttackOpportunity = true)
    {
        return EnemySquadMemberSnapshot{
            position,
            true,
            EnemyAwarenessState::Alerted,
            phase,
            hasAttackOpportunity};
    }
}

TEST(EnemySquadCoordinatorTest, EmptySquadProducesNoDirectives)
{
    EnemySquadCoordinator coordinator;

    EXPECT_TRUE(
        coordinator.decide({}, Vec2{}).empty());
}

TEST(EnemySquadCoordinatorTest, SingleSlotUsesStableRoundRobinOrder)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{200.0F, 0.0F}),
        alertedMember(Vec2{80.0F, 0.0F}),
        alertedMember(Vec2{120.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    ASSERT_EQ(directives.size(), 3U);
    EXPECT_TRUE(directives[0].canStartAttack);
    EXPECT_EQ(directives[0].role, EnemyTacticalRole::Engage);
    EXPECT_FALSE(directives[1].canStartAttack);
    EXPECT_EQ(directives[1].role, EnemyTacticalRole::Pressure);
    EXPECT_FALSE(directives[2].canStartAttack);
    EXPECT_EQ(directives[2].role, EnemyTacticalRole::Pressure);
}

TEST(EnemySquadCoordinatorTest, EqualDistanceTieUsesStableLowestIndex)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{-100.0F, 0.0F}),
        alertedMember(Vec2{100.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_TRUE(directives[0].canStartAttack);
    EXPECT_FALSE(directives[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, CoolingNearestMemberYieldsPermissionToReadyTeammate)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(
            Vec2{40.0F, 0.0F},
            EnemyAttackPhase::Idle,
            false),
        alertedMember(Vec2{70.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_EQ(directives[0].role, EnemyTacticalRole::Pressure);
    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_EQ(directives[1].role, EnemyTacticalRole::Engage);
    EXPECT_TRUE(directives[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, ExistingAttackKeepsTokenWithoutRestartPermission)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{200.0F, 0.0F}),
        alertedMember(
            Vec2{120.0F, 0.0F},
            EnemyAttackPhase::Active)};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_EQ(directives[0].role, EnemyTacticalRole::Pressure);
    EXPECT_EQ(directives[1].role, EnemyTacticalRole::Engage);
    EXPECT_FALSE(directives[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, OffBalanceMemberYieldsAttackToken)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(
            Vec2{40.0F, 0.0F},
            EnemyAttackPhase::OffBalance),
        alertedMember(Vec2{100.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_TRUE(directives[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, UnawareAndDeadMembersCannotEngage)
{
    EnemySquadCoordinator coordinator;
    std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{20.0F, 0.0F}),
        alertedMember(Vec2{100.0F, 0.0F})};
    members[0].alive = false;
    members[1].awareness = EnemyAwarenessState::Unaware;

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_FALSE(directives[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, NearbyMembersReceiveOpposingSeparation)
{
    EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{0.0F, 0.0F}),
        alertedMember(Vec2{20.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{200.0F, 0.0F});

    EXPECT_LT(directives[0].separationDirection.x, 0.0F);
    EXPECT_GT(directives[1].separationDirection.x, 0.0F);
    EXPECT_FLOAT_EQ(directives[0].separationDirection.y, 0.0F);
}

TEST(EnemySquadCoordinatorTest, ExactOverlapUsesDeterministicSeparation)
{
    EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{40.0F, 40.0F}),
        alertedMember(Vec2{40.0F, 40.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_LT(directives[0].separationDirection.x, 0.0F);
    EXPECT_GT(directives[1].separationDirection.x, 0.0F);
}

TEST(EnemySquadCoordinatorTest, DefaultConcurrentLimitGrantsOnlyTenAttackers)
{
    EnemySquadCoordinator coordinator;
    std::vector<EnemySquadMemberSnapshot> members;
    for (std::size_t index{}; index < 12U; ++index)
    {
        members.push_back(alertedMember(
            Vec2{10.0F + static_cast<float>(index) * 10.0F, 0.0F}));
    }

    const auto directives = coordinator.decide(members, Vec2{});

    std::size_t granted{};
    for (std::size_t index{}; index < directives.size(); ++index)
    {
        granted += directives[index].canStartAttack ? 1U : 0U;
        EXPECT_EQ(
            directives[index].role,
            index < 10U
                ? EnemyTacticalRole::Engage
                : EnemyTacticalRole::Pressure);
    }
    EXPECT_EQ(granted, 10U);
}

TEST(EnemySquadCoordinatorTest, ReleasedSlotsRotateToWaitingAttackers)
{
    EnemySquadCoordinator coordinator;
    std::vector<EnemySquadMemberSnapshot> members;
    for (std::size_t index{}; index < 12U; ++index)
    {
        members.push_back(alertedMember(
            Vec2{10.0F + static_cast<float>(index) * 10.0F, 0.0F}));
    }

    static_cast<void>(coordinator.decide(members, Vec2{}));
    for (std::size_t index{}; index < 10U; ++index)
    {
        members[index].attackPhase = EnemyAttackPhase::Active;
    }
    static_cast<void>(coordinator.decide(members, Vec2{}));
    for (EnemySquadMemberSnapshot &member : members)
    {
        member.attackPhase = EnemyAttackPhase::Idle;
    }

    const auto directives = coordinator.decide(members, Vec2{});

    EXPECT_TRUE(directives[10].canStartAttack);
    EXPECT_TRUE(directives[11].canStartAttack);
    EXPECT_FALSE(directives[8].canStartAttack);
    EXPECT_FALSE(directives[9].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, PreparingAttackRetainsItsGrantedSlot)
{
    EnemySquadCoordinator coordinator{singleAttackSlotConfig()};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{100.0F, 0.0F}),
        alertedMember(Vec2{120.0F, 0.0F})};

    const auto first = coordinator.decide(members, Vec2{});
    const auto second = coordinator.decide(members, Vec2{});

    ASSERT_TRUE(first[0].canStartAttack);
    EXPECT_TRUE(second[0].canStartAttack);
    EXPECT_FALSE(second[1].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, ActiveAttacksConsumeConcurrentSlots)
{
    EnemySquadConfig config;
    config.maximumConcurrentAttackers = 3U;
    EnemySquadCoordinator coordinator{config};
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{20.0F, 0.0F}, EnemyAttackPhase::Active),
        alertedMember(Vec2{30.0F, 0.0F}, EnemyAttackPhase::Recovery),
        alertedMember(Vec2{40.0F, 0.0F}),
        alertedMember(Vec2{50.0F, 0.0F})};

    const auto directives = coordinator.decide(members, Vec2{});

    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_FALSE(directives[1].canStartAttack);
    EXPECT_TRUE(directives[2].canStartAttack);
    EXPECT_FALSE(directives[3].canStartAttack);
    EXPECT_EQ(directives[3].role, EnemyTacticalRole::Pressure);
}

TEST(EnemySquadCoordinatorTest, NeighborGridFindsCloseMembersAcrossCellEdge)
{
    EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{81.0F, 20.0F}),
        alertedMember(Vec2{83.0F, 20.0F})};
    EnemySquadDecisionMetrics metrics;

    const auto directives = coordinator.decide(members, Vec2{}, &metrics);

    ASSERT_EQ(directives.size(), 2U);
    EXPECT_LT(directives[0].separationDirection.x, 0.0F);
    EXPECT_GT(directives[1].separationDirection.x, 0.0F);
    EXPECT_EQ(metrics.neighborCandidatesExamined, 2U);
}

TEST(EnemySquadCoordinatorTest, SparseHundredMemberSquadAvoidsAllPairsScan)
{
    EnemySquadCoordinator coordinator;
    std::vector<EnemySquadMemberSnapshot> members;
    members.reserve(100U);
    for (std::size_t index{}; index < 100U; ++index)
    {
        members.push_back(alertedMember(Vec2{
            static_cast<float>(index % 10U) * 200.0F,
            static_cast<float>(index / 10U) * 200.0F}));
    }
    EnemySquadDecisionMetrics metrics;

    const auto directives = coordinator.decide(members, Vec2{}, &metrics);

    EXPECT_EQ(directives.size(), members.size());
    EXPECT_EQ(metrics.neighborCandidatesExamined, 0U);
}

TEST(EnemySquadCoordinatorTest, InvalidTargetCannotGrantAttackPermission)
{
    EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{})};

    const auto directives = coordinator.decide(
        members,
        Vec2{
            std::numeric_limits<float>::quiet_NaN(),
            0.0F});

    ASSERT_EQ(directives.size(), 1U);
    EXPECT_FALSE(directives[0].canStartAttack);
}

TEST(EnemySquadCoordinatorTest, InvalidConfigIsRejected)
{
    EnemySquadConfig config;
    config.separationRadius = 0.0F;

    EXPECT_THROW(
        static_cast<void>(EnemySquadCoordinator{config}),
        std::invalid_argument);

    config = EnemySquadConfig{};
    config.maximumConcurrentAttackers = 0U;
    EXPECT_THROW(
        static_cast<void>(EnemySquadCoordinator{config}),
        std::invalid_argument);
}
