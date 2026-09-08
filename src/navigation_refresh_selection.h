#pragma once

#include "enemy.h"
#include <algorithm>
#include <optional>
#include <span>

struct NavigationRefreshSelection
{
    std::optional<CombatTargetId> target;
    std::size_t nextCursor{};
};

// Select at most one live actor. Cursor is a scheduling offset, never identity.
// Eligibility and scan budget are activity policy; no actor or path is mutated.
// The predicate's index is valid only during this call. The result is a stable ID.
template <class Eligible>
[[nodiscard]] NavigationRefreshSelection selectNavigationRefresh(
    std::span<const Enemy> actors, std::size_t cursor,
    std::size_t scanBudget, Eligible eligible)
{
    if (actors.empty()) return {std::nullopt, cursor};
    const auto start = cursor % actors.size();
    auto candidate = start;
    for (std::size_t scanned = 0; scanned < std::min(scanBudget, actors.size()); ++scanned)
    {
        const auto &actor = actors[candidate];
        if (!actor.isDead() && eligible(candidate))
            return {actor.combatTargetId(), (candidate + 1) % actors.size()};
        candidate = (candidate + 1) % actors.size();
    }
    return {std::nullopt, start};
}
