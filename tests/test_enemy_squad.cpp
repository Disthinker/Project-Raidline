#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
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

namespace
{
    // Independent all-pairs oracle: no cell containers, cell-range lookup or
    // merge. Scan original input order to freeze floating-point accumulation.
    void expectNaiveNeighbors(const std::vector<EnemySquadMemberSnapshot> &members,
                              const EnemySquadConfig &config,
                              const std::vector<EnemyTacticalDirective> &actual,
                              const EnemySquadDecisionMetrics &metrics)
    {
        ASSERT_EQ(actual.size(), members.size());
        std::size_t candidates{};
        const auto valid = [](const auto &m) {
            return m.alive && std::isfinite(m.position.x) && std::isfinite(m.position.y);
        };
        for (std::size_t i{}; i < members.size(); ++i)
        {
            Vec2 expected{};
            if (valid(members[i]))
            {
                for (std::size_t j{}; j < members.size(); ++j)
                {
                    if (j == i || !valid(members[j]))
                        continue;
                    if (std::abs(std::floor(members[i].position.x / config.separationRadius) -
                                 std::floor(members[j].position.x / config.separationRadius)) > 1 ||
                        std::abs(std::floor(members[i].position.y / config.separationRadius) -
                                 std::floor(members[j].position.y / config.separationRadius)) > 1)
                        continue;
                    ++candidates;
                    Vec2 away{members[i].position.x - members[j].position.x,
                              members[i].position.y - members[j].position.y};
                    const float squared = away.x * away.x + away.y * away.y;
                    if (!std::isfinite(squared) || squared >= config.separationRadius * config.separationRadius)
                        continue;
                    float distance{};
                    if (squared <= 0.00001F * 0.00001F)
                        away = {i < j ? -1.0F : 1.0F, 0};
                    else
                    {
                        distance = std::sqrt(squared);
                        away.x /= distance;
                        away.y /= distance;
                    }
                    const float weight = (config.separationRadius - distance) / config.separationRadius;
                    expected.x += away.x * weight;
                    expected.y += away.y * weight;
                }
                const float squared = expected.x * expected.x + expected.y * expected.y;
                if (squared > 0.00001F * 0.00001F)
                {
                    const float length = std::sqrt(squared);
                    const float clamped = std::min(length, config.maximumSeparationWeight);
                    expected = {expected.x / length * clamped, expected.y / length * clamped};
                }
                else
                    expected = {};
            }
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[i].separationDirection.x),
                      std::bit_cast<std::uint32_t>(expected.x)) << "member=" << i << " x";
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[i].separationDirection.y),
                      std::bit_cast<std::uint32_t>(expected.y)) << "member=" << i << " y";
        }
        EXPECT_EQ(metrics.neighborCandidatesExamined, candidates);
    }
}

TEST(EnemySquadCoordinatorTest, DenseAndSparseCellsMatchNaiveInputOrderAcrossSeeds)
{
    for (unsigned seed : {1U, 17U, 42U, 9917U, 20261017U})
        for (float radius : {33.0F, 82.0F, 128.0F})
            for (std::size_t count : {32U, 100U})
                for (int spacing : {35, 250})
                {
                    SCOPED_TRACE(::testing::Message() << "seed=" << seed << " radius=" << radius
                                                     << " count=" << count << " spacing=" << spacing);
                    std::mt19937 random(seed);
                    std::uniform_int_distribution<int> jitter(-18, 18);
                    EnemySquadConfig config;
                    config.separationRadius = radius;
                    EnemySquadCoordinator coordinator(config);
                    std::vector<EnemySquadMemberSnapshot> members;
                    for (std::size_t i{}; i < count; ++i)
                    {
                        auto m = alertedMember({static_cast<float>(i % 10) * spacing - 300 + jitter(random),
                                                static_cast<float>(i / 10) * spacing - 300 + jitter(random)});
                        m.alive = i % 13 != 0;
                        m.hasAttackOpportunity = false;
                        m.awareness = i % 3 == 0 ? EnemyAwarenessState::Unaware : EnemyAwarenessState::Alerted;
                        m.attackPhase = i % 17 == 0 ? EnemyAttackPhase::Windup : EnemyAttackPhase::Idle;
                        members.push_back(m);
                    }
                    std::shuffle(members.begin(), members.end(), random);
                    EnemySquadDecisionMetrics metrics;
                    const auto result = coordinator.decide(members, {}, &metrics);
                    expectNaiveNeighbors(members, config, result, metrics);
                    for (std::size_t i{}; i < members.size(); ++i)
                    {
                        EXPECT_FLOAT_EQ(result[i].supportSide, i % 2 == 0 ? 1.0F : -1.0F);
                        EXPECT_FALSE(result[i].canStartAttack);
                        EXPECT_EQ(result[i].role, members[i].alive && members[i].attackPhase == EnemyAttackPhase::Windup
                            ? EnemyTacticalRole::Engage : members[i].alive && members[i].awareness == EnemyAwarenessState::Alerted
                            ? EnemyTacticalRole::Pressure : EnemyTacticalRole::Support);
                    }
                }
}

TEST(EnemySquadCoordinatorTest, CellBoundariesInvalidPositionsAndOverlapKeepExactResults)
{
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    std::vector<EnemySquadMemberSnapshot> members;
    for (Vec2 p : std::vector<Vec2>{{-82.0001F, 0}, {-82, 0}, {-81.9999F, 0}, {-.0001F, 0}, {0, 0},
                                   {81.9999F, 82}, {82, 82}, {82.0001F, 82}, {0, nan}, {inf, 0}, {-inf, 0}})
        members.push_back(alertedMember(p));
    for (int n{}; n < 37; ++n)
        members.push_back(alertedMember({-82, -82}));
    members[2].alive = false;
    EnemySquadCoordinator coordinator;
    EnemySquadDecisionMetrics metrics;
    const auto result = coordinator.decide(members, {}, &metrics);
    expectNaiveNeighbors(members, coordinator.config(), result, metrics);
    const std::vector<EnemySquadMemberSnapshot> invalid{alertedMember({nan, 0}), alertedMember({inf, 0})};
    const auto after = coordinator.decide(invalid, {}, &metrics);
    expectNaiveNeighbors(invalid, coordinator.config(), after, metrics);
    EXPECT_EQ(metrics.neighborCandidatesExamined, 0U);
}

TEST(EnemySquadCoordinatorTest, MembershipAndPositionChangesKeepReservationsAndNeighborResults)
{
    EnemySquadConfig config = singleAttackSlotConfig();
    EnemySquadCoordinator dense(config), sparse(config);
    std::vector<EnemySquadMemberSnapshot> members;
    for (int n{}; n < 32; ++n)
        members.push_back(alertedMember({static_cast<float>(n % 8) * 20 - 90,
                                       static_cast<float>(n / 8) * 20 - 90}));
    for (int frame{}; frame < 60; ++frame)
    {
        SCOPED_TRACE(frame);
        if (frame == 10)
            members[0].alive = false;
        if (frame == 20)
        {
            members.erase(members.begin() + 12);
            dense.invalidateActorMembership();
            sparse.invalidateActorMembership();
        }
        if (frame == 30)
        {
            members.push_back(alertedMember({-82, 82}));
            dense.invalidateActorMembership();
            sparse.invalidateActorMembership();
        }
        if (frame == 40)
        {
            members.clear();
            dense.invalidateActorMembership();
            sparse.invalidateActorMembership();
        }
        if (frame == 41)
            members = {alertedMember({-82, 82}), alertedMember({82, -82})};
        for (auto &m : members)
            m.position.x += frame % 2 == 0 ? 14.0F : -8.0F;
        auto far = members;
        for (std::size_t n{}; n < far.size(); ++n)
            far[n].position = {static_cast<float>(n) * 250, static_cast<float>(frame) * 250};
        EnemySquadDecisionMetrics denseMetrics, sparseMetrics;
        const auto result = dense.decide(members, {}, &denseMetrics);
        const auto control = sparse.decide(far, {}, &sparseMetrics);
        expectNaiveNeighbors(members, config, result, denseMetrics);
        expectNaiveNeighbors(far, config, control, sparseMetrics);
        ASSERT_EQ(result.size(), control.size());
        for (std::size_t n{}; n < result.size(); ++n)
        {
            EXPECT_EQ(result[n].role, control[n].role);
            EXPECT_EQ(result[n].canStartAttack, control[n].canStartAttack);
            EXPECT_FLOAT_EQ(result[n].supportSide, control[n].supportSide);
        }
    }
}
