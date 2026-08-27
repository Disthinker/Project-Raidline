#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include "enemy_squad.h"

namespace
{
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
    const EnemySquadCoordinator coordinator;

    EXPECT_TRUE(
        coordinator.decide({}, Vec2{}).empty());
}

TEST(EnemySquadCoordinatorTest, NearestAlertedIdleMemberGetsAttackPermission)
{
    const EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{200.0F, 0.0F}),
        alertedMember(Vec2{80.0F, 0.0F}),
        alertedMember(Vec2{120.0F, 0.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    ASSERT_EQ(directives.size(), 3U);
    EXPECT_FALSE(directives[0].canStartAttack);
    EXPECT_EQ(directives[0].role, EnemyTacticalRole::Pressure);
    EXPECT_TRUE(directives[1].canStartAttack);
    EXPECT_EQ(directives[1].role, EnemyTacticalRole::Engage);
    EXPECT_FALSE(directives[2].canStartAttack);
    EXPECT_EQ(directives[2].role, EnemyTacticalRole::Pressure);
}

TEST(EnemySquadCoordinatorTest, EqualDistanceTieUsesStableLowestIndex)
{
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
    const std::vector<EnemySquadMemberSnapshot> members{
        alertedMember(Vec2{40.0F, 40.0F}),
        alertedMember(Vec2{40.0F, 40.0F})};

    const auto directives =
        coordinator.decide(members, Vec2{});

    EXPECT_LT(directives[0].separationDirection.x, 0.0F);
    EXPECT_GT(directives[1].separationDirection.x, 0.0F);
}

TEST(EnemySquadCoordinatorTest, NeighborGridFindsCloseMembersAcrossCellEdge)
{
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
    const EnemySquadCoordinator coordinator;
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
}
