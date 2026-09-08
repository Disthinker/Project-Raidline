#include "navigation_refresh_selection.h"
#include "enemy_lifecycle.h"
#include <gtest/gtest.h>
#include <array>
#include <limits>

namespace {
std::vector<Enemy> actors() {
    return {Enemy{{}, {20,20}, {}, 3, 11}, Enemy{{}, {20,20}, {}, 3, 22},
            Enemy{{}, {20,20}, {}, 3, 33}};
}
const auto all = [](std::size_t) { return true; };
}

TEST(NavigationRefreshSelection, EmptyAndZeroBudgetNeverSelect) {
    EXPECT_FALSE(selectNavigationRefresh({}, 9, 1, all).target);
    EXPECT_EQ(selectNavigationRefresh({}, 9, 1, all).nextCursor, 9U);
    const auto set = actors();
    EXPECT_FALSE(selectNavigationRefresh(set, 9, 0, all).target);
    EXPECT_EQ(selectNavigationRefresh(set, 9, 0, all).nextCursor, 0U);
}
TEST(NavigationRefreshSelection, WrapAndLargeCursorAreBounded) {
    const auto set = actors();
    const auto result = selectNavigationRefresh(set, 2, 3, all);
    EXPECT_EQ(result.target, 33U); EXPECT_EQ(result.nextCursor, 0U);
    const auto cursor = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(selectNavigationRefresh(set, cursor, 3, all).target,
              set[cursor % set.size()].combatTargetId());
}
TEST(NavigationRefreshSelection, RaidSkipsIneligibleButDefenseConsumesOneTurn) {
    const auto set = actors();
    const auto eligible = [](std::size_t i) { return i == 2; };
    EXPECT_EQ(selectNavigationRefresh(set, 0, set.size(), eligible).target, 33U);
    // Defense selects a turn first, and applies cooldown outside selection.
    EXPECT_EQ(selectNavigationRefresh(set, 0, 1, all).target, 11U);
    EXPECT_FALSE(selectNavigationRefresh(set, 0, 1, eligible).target);
}
TEST(NavigationRefreshSelection, DeadActorCannotReceiveTurn) {
    auto set = actors();
    static_cast<void>(set[0].takeDamage(3));
    EXPECT_FALSE(selectNavigationRefresh(set, 0, 1, all).target);
    EXPECT_EQ(selectNavigationRefresh(set, 0, 3, all).target, 22U);
}
TEST(NavigationRefreshSelection, AllIneligibleKeepsNormalizedCursor) {
    const auto set = actors();
    unsigned calls = 0;
    const auto result = selectNavigationRefresh(set, 4, 100, [&](std::size_t) {
        ++calls; return false;
    });
    EXPECT_EQ(calls, 3U); EXPECT_FALSE(result.target);
    EXPECT_EQ(result.nextCursor, 1U);
}
TEST(NavigationRefreshSelection, FairRoundRobinVisitsEachEligibleActor) {
    const auto set = actors();
    std::size_t cursor = 0;
    for (const auto expected : {11U, 33U, 11U, 33U, 11U, 33U}) {
        const auto result = selectNavigationRefresh(set, cursor, set.size(),
            [](std::size_t i) { return i != 1; });
        EXPECT_EQ(result.target, expected); cursor = result.nextCursor;
    }
}
TEST(NavigationRefreshSelection, IdentitySurvivesRemovalAndReordering) {
    EnemyRoster<> roster;
    for (auto actor : actors()) roster.spawn(std::move(actor));
    const auto result = selectNavigationRefresh(roster.view(), 1, 3, all);
    ASSERT_EQ(result.target, 22U);
    const std::array<CombatTargetId,1> first{11};
    static_cast<void>(roster.removeForObjective(first));
    ASSERT_NE(roster.find(*result.target), nullptr);
    EXPECT_EQ(roster.front().combatTargetId(), *result.target);
    const std::array<CombatTargetId,1> selected{22};
    static_cast<void>(roster.removeForObjective(selected));
    EXPECT_EQ(roster.find(*result.target), nullptr);
    EXPECT_EQ(selectNavigationRefresh(roster.view(), result.nextCursor, 1, all).target, 33U);
}
TEST(NavigationRefreshSelection, MatchesLegacyRaidAndDefenseTraces) {
    const auto set = actors();
    for (std::size_t start = 0; start < 12; ++start) {
        for (unsigned mask = 0; mask < 8; ++mask) {
            auto cursor = start % set.size();
            std::optional<CombatTargetId> expected;
            for (std::size_t offset = 0; offset < set.size(); ++offset) {
                const auto index = (cursor + offset) % set.size();
                if ((mask & (1U << index)) == 0) continue;
                expected = set[index].combatTargetId();
                cursor = (index + 1) % set.size(); break;
            }
            const auto result = selectNavigationRefresh(set, start, set.size(),
                [&](std::size_t i) { return (mask & (1U << i)) != 0; });
            EXPECT_EQ(result.target, expected); EXPECT_EQ(result.nextCursor, cursor);
            const auto defense = selectNavigationRefresh(set, start, 1, all);
            EXPECT_EQ(defense.target, set[start % set.size()].combatTargetId());
            EXPECT_EQ(defense.nextCursor, (start % set.size() + 1) % set.size());
        }
    }
}
