#include "enemy_squad.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr float kDistanceEpsilon{0.00001F};

    bool isFinitePositive(float value) noexcept
    {
        return std::isfinite(value) && value > 0.0F;
    }

    bool isFinite(Vec2 value) noexcept
    {
        return std::isfinite(value.x) &&
               std::isfinite(value.y);
    }

    float lengthSquared(Vec2 value) noexcept
    {
        return value.x * value.x +
               value.y * value.y;
    }

    bool occupiesAttackToken(
        EnemyAttackPhase phase) noexcept
    {
        return phase == EnemyAttackPhase::Windup ||
               phase == EnemyAttackPhase::Active ||
               phase == EnemyAttackPhase::Recovery;
    }

    struct EnemyCell
    {
        int x{};
        int y{};

        friend bool operator==(const EnemyCell &, const EnemyCell &) = default;
    };

    struct EnemyCellHash
    {
        std::size_t operator()(EnemyCell cell) const noexcept
        {
            const std::size_t x = static_cast<std::size_t>(
                static_cast<unsigned int>(cell.x));
            const std::size_t y = static_cast<std::size_t>(
                static_cast<unsigned int>(cell.y));
            return x * 0x9E3779B185EBCA87ULL ^
                   (y + 0x9E3779B97F4A7C15ULL + (x << 6U) + (x >> 2U));
        }
    };

    EnemyCell enemyCell(Vec2 position, float cellSize) noexcept
    {
        return EnemyCell{
            static_cast<int>(std::floor(position.x / cellSize)),
            static_cast<int>(std::floor(position.y / cellSize))};
    }
}

EnemySquadCoordinator::EnemySquadCoordinator()
    : EnemySquadCoordinator{EnemySquadConfig{}}
{
}

EnemySquadCoordinator::EnemySquadCoordinator(
    EnemySquadConfig config)
    : config_{config}
{
    if (!isFinitePositive(config_.separationRadius) ||
        !isFinitePositive(config_.maximumSeparationWeight))
    {
        throw std::invalid_argument{
            "EnemySquadConfig requires finite positive values"};
    }
}

std::vector<EnemyTacticalDirective>
EnemySquadCoordinator::decide(
    const std::vector<EnemySquadMemberSnapshot> &members,
    Vec2 targetPosition,
    EnemySquadDecisionMetrics *metrics) const
{
    if (metrics != nullptr)
    {
        *metrics = EnemySquadDecisionMetrics{};
    }
    std::vector<EnemyTacticalDirective> directives(
        members.size());

    for (std::size_t index{0U};
         index < directives.size();
         ++index)
    {
        directives[index].supportSide =
            index % 2U == 0U ? 1.0F : -1.0F;
        if (members[index].alive &&
            members[index].awareness == EnemyAwarenessState::Alerted)
        {
            // Pursuit pressure is independent from the single permission to
            // start an attack. Alerted teammates must keep closing while one
            // member owns the active attack token.
            directives[index].role = EnemyTacticalRole::Pressure;
        }
    }

    std::optional<std::size_t> engageIndex;
    for (std::size_t index{0U};
         index < members.size();
         ++index)
    {
        if (members[index].alive &&
            occupiesAttackToken(members[index].attackPhase))
        {
            engageIndex = index;
            break;
        }
    }

    if (!engageIndex.has_value() &&
        isFinite(targetPosition))
    {
        float nearestDistanceSquared =
            std::numeric_limits<float>::max();
        for (std::size_t index{0U};
             index < members.size();
             ++index)
        {
            const EnemySquadMemberSnapshot &member =
                members[index];
            if (!member.alive ||
                member.awareness != EnemyAwarenessState::Alerted ||
                member.attackPhase != EnemyAttackPhase::Idle ||
                !member.hasAttackOpportunity ||
                !isFinite(member.position))
            {
                continue;
            }

            const Vec2 offset{
                targetPosition.x - member.position.x,
                targetPosition.y - member.position.y};
            const float distanceSquared = lengthSquared(offset);
            if (!std::isfinite(distanceSquared))
            {
                continue;
            }

            if (!engageIndex.has_value() ||
                distanceSquared < nearestDistanceSquared)
            {
                engageIndex = index;
                nearestDistanceSquared = distanceSquared;
            }
        }
    }

    if (engageIndex.has_value())
    {
        directives[*engageIndex].role =
            EnemyTacticalRole::Engage;
        directives[*engageIndex].canStartAttack =
            members[*engageIndex].attackPhase ==
                EnemyAttackPhase::Idle &&
            members[*engageIndex].awareness ==
                EnemyAwarenessState::Alerted;
    }

    const float radiusSquared =
        config_.separationRadius * config_.separationRadius;
    std::unordered_map<EnemyCell, std::vector<std::size_t>, EnemyCellHash>
        neighborCells;
    neighborCells.reserve(members.size());
    for (std::size_t index{}; index < members.size(); ++index)
    {
        if (!members[index].alive || !isFinite(members[index].position))
        {
            continue;
        }
        neighborCells[enemyCell(
            members[index].position,
            config_.separationRadius)]
            .push_back(index);
    }

    std::vector<std::size_t> neighborCandidates;
    neighborCandidates.reserve(members.size());
    for (std::size_t index{0U};
         index < members.size();
         ++index)
    {
        if (!members[index].alive ||
            !isFinite(members[index].position))
        {
            continue;
        }

        Vec2 separation{};
        neighborCandidates.clear();
        const EnemyCell center = enemyCell(
            members[index].position,
            config_.separationRadius);
        for (int offsetY{-1}; offsetY <= 1; ++offsetY)
        {
            for (int offsetX{-1}; offsetX <= 1; ++offsetX)
            {
                const auto found = neighborCells.find(
                    EnemyCell{center.x + offsetX, center.y + offsetY});
                if (found != neighborCells.end())
                {
                    neighborCandidates.insert(
                        neighborCandidates.end(),
                        found->second.begin(),
                        found->second.end());
                }
            }
        }
        std::sort(neighborCandidates.begin(), neighborCandidates.end());
        for (const std::size_t neighborIndex : neighborCandidates)
        {
            if (neighborIndex == index ||
                !members[neighborIndex].alive ||
                !isFinite(members[neighborIndex].position))
            {
                continue;
            }
            if (metrics != nullptr)
            {
                ++metrics->neighborCandidatesExamined;
            }

            Vec2 away{
                members[index].position.x -
                    members[neighborIndex].position.x,
                members[index].position.y -
                    members[neighborIndex].position.y};
            const float distanceSquared = lengthSquared(away);
            if (!std::isfinite(distanceSquared) ||
                distanceSquared >= radiusSquared)
            {
                continue;
            }

            float distance{};
            if (distanceSquared <=
                kDistanceEpsilon * kDistanceEpsilon)
            {
                away = Vec2{
                    index < neighborIndex ? -1.0F : 1.0F,
                    0.0F};
            }
            else
            {
                distance = std::sqrt(distanceSquared);
                away.x /= distance;
                away.y /= distance;
            }

            const float weight =
                (config_.separationRadius - distance) /
                config_.separationRadius;
            separation.x += away.x * weight;
            separation.y += away.y * weight;
        }

        const float separationLengthSquared =
            lengthSquared(separation);
        if (separationLengthSquared >
            kDistanceEpsilon * kDistanceEpsilon)
        {
            const float separationLength =
                std::sqrt(separationLengthSquared);
            const float clampedLength = std::min(
                separationLength,
                config_.maximumSeparationWeight);
            directives[index].separationDirection = Vec2{
                separation.x / separationLength * clampedLength,
                separation.y / separationLength * clampedLength};
        }
    }

    return directives;
}

const EnemySquadConfig &
EnemySquadCoordinator::config() const noexcept
{
    return config_;
}
