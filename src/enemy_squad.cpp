#include "enemy_squad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
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

    bool cellBefore(EnemyCell first, EnemyCell second) noexcept
    {
        return first.y < second.y ||
               (first.y == second.y && first.x < second.x);
    }

    struct CellMember
    {
        EnemyCell cell;
        std::size_t index;
    };

    struct CellMembers
    {
        const CellMember *first{};
        const CellMember *last{};
    };

    struct NeighborCell
    {
        EnemyCell cell;
        CellMembers members;
        std::array<CellMembers, 9> adjacent{};
        std::size_t adjacentCount{};
        std::size_t candidateBegin{};
        std::size_t candidateEnd{};
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
        !isFinitePositive(config_.maximumSeparationWeight) ||
        config_.maximumConcurrentAttackers == 0U)
    {
        throw std::invalid_argument{
            "EnemySquadConfig requires positive finite movement values "
            "and at least one concurrent attacker"};
    }
}

std::vector<EnemyTacticalDirective>
EnemySquadCoordinator::decide(
    const std::vector<EnemySquadMemberSnapshot> &members,
    Vec2 targetPosition,
    EnemySquadDecisionMetrics *metrics)
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
            // Pursuit pressure is independent from the bounded permission to
            // start an attack. Alerted teammates must keep closing while all
            // concurrent attack slots are occupied.
            directives[index].role = EnemyTacticalRole::Pressure;
        }
    }

    std::size_t occupiedAttackSlots{};
    for (std::size_t index{0U};
         index < members.size();
         ++index)
    {
        if (members[index].alive &&
            occupiesAttackToken(members[index].attackPhase))
        {
            directives[index].role = EnemyTacticalRole::Engage;
            ++occupiedAttackSlots;
        }
    }

    std::vector<bool> attackCandidates(
        members.size(),
        false);
    if (occupiedAttackSlots < config_.maximumConcurrentAttackers &&
        isFinite(targetPosition))
    {
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
            attackCandidates[index] = true;
        }
    }

    const std::size_t availableAttackSlots =
        occupiedAttackSlots >= config_.maximumConcurrentAttackers
            ? 0U
            : config_.maximumConcurrentAttackers - occupiedAttackSlots;
    std::vector<std::size_t> retainedReservations;
    retainedReservations.reserve(availableAttackSlots);
    std::vector<bool> granted(members.size(), false);
    for (const std::size_t memberIndex : reservedAttackers_)
    {
        if (retainedReservations.size() >= availableAttackSlots ||
            memberIndex >= members.size() ||
            !attackCandidates[memberIndex])
        {
            continue;
        }
        directives[memberIndex].role =
            EnemyTacticalRole::Engage;
        directives[memberIndex].canStartAttack = true;
        granted[memberIndex] = true;
        retainedReservations.push_back(memberIndex);
    }

    if (members.empty())
    {
        attackScheduleCursor_ = 0U;
    }
    else
    {
        attackScheduleCursor_ %= members.size();
        const std::size_t scheduleStart = attackScheduleCursor_;
        for (std::size_t scanned{};
             scanned < members.size() &&
             retainedReservations.size() < availableAttackSlots;
             ++scanned)
        {
            const std::size_t memberIndex =
                (scheduleStart + scanned) % members.size();
            if (!attackCandidates[memberIndex] || granted[memberIndex])
            {
                continue;
            }
            directives[memberIndex].role =
                EnemyTacticalRole::Engage;
            directives[memberIndex].canStartAttack = true;
            granted[memberIndex] = true;
            retainedReservations.push_back(memberIndex);
            attackScheduleCursor_ =
                (memberIndex + 1U) % members.size();
        }
    }
    reservedAttackers_ = std::move(retainedReservations);

    const float radiusSquared =
        config_.separationRadius * config_.separationRadius;
    // Input storage is immutable for this call. The inner neighbor merge uses
    // validated original indices; avoid repeatedly resolving the same checked
    // vector/array access for every directed pair in a dense crowd.
    const auto *const memberData = members.data();
    // All spatial data below belongs to this call. Each valid member enters
    // one cell, ordered by its original input index within that cell.
    std::vector<CellMember> cellMembers;
    cellMembers.reserve(members.size());
    for (std::size_t index{}; index < members.size(); ++index)
    {
        if (!members[index].alive || !isFinite(members[index].position))
        {
            continue;
        }
        cellMembers.push_back({enemyCell(
            members[index].position, config_.separationRadius), index});
    }
    if (cellMembers.empty())
        return directives;
    std::sort(cellMembers.begin(), cellMembers.end(),
        [](const CellMember &first, const CellMember &second)
        {
            return cellBefore(first.cell, second.cell) ||
                   (first.cell == second.cell && first.index < second.index);
        });
    const std::size_t noCell = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> memberCells(members.size(), noCell);
    std::vector<NeighborCell> neighborCells;
    neighborCells.reserve(cellMembers.size());
    for (std::size_t first{}; first < cellMembers.size();)
    {
        std::size_t last = first + 1U;
        while (last < cellMembers.size() &&
               cellMembers[last].cell == cellMembers[first].cell)
            ++last;
        const std::size_t cellIndex = neighborCells.size();
        neighborCells.push_back({cellMembers[first].cell,
            {cellMembers.data() + first, cellMembers.data() + last}});
        for (std::size_t entry = first; entry < last; ++entry)
            memberCells[cellMembers[entry].index] = cellIndex;
        first = last;
    }
    // Resolve each cell's nine neighbors once. Neither buffer grows after
    // this point, so these local ranges remain valid until decide returns.
    const auto *cellsEnd = neighborCells.data() + neighborCells.size();
    for (auto &cell : neighborCells)
    {
        for (int offsetY{-1}; offsetY <= 1; ++offsetY)
        {
            for (int offsetX{-1}; offsetX <= 1; ++offsetX)
            {
                const EnemyCell key{cell.cell.x + offsetX, cell.cell.y + offsetY};
                const auto *first = neighborCells.data();
                const auto *last = cellsEnd;
                while (first < last)
                {
                    const auto *middle = first + (last - first) / 2;
                    if (cellBefore(middle->cell, key))
                        first = middle + 1;
                    else
                        last = middle;
                }
                if (first != cellsEnd && first->cell == key)
                    cell.adjacent[cell.adjacentCount++] = first->members;
            }
        }
    }
    // All members in one cell visit exactly the same sorted candidate list.
    // Merge it once for that cell, retaining original input order. Each input
    // member appears in at most nine such lists; nothing survives this call.
    std::vector<std::size_t> neighborCandidates;
    neighborCandidates.reserve(cellMembers.size());
    for (auto &cell : neighborCells)
    {
        cell.candidateBegin = neighborCandidates.size();
        auto cursors = cell.adjacent;
        auto *const cursorData = cursors.data();
        std::size_t active = cell.adjacentCount;
        while (active != 0U)
        {
            std::size_t selected{};
            std::size_t neighborIndex = cursorData[0].first->index;
            for (std::size_t candidate{1U}; candidate < active; ++candidate)
                if (cursorData[candidate].first->index < neighborIndex)
                {
                    selected = candidate;
                    neighborIndex = cursorData[candidate].first->index;
                }
            if (++cursorData[selected].first == cursorData[selected].last)
                cursorData[selected] = cursorData[--active];
            neighborCandidates.push_back(neighborIndex);
        }
        cell.candidateEnd = neighborCandidates.size();
    }
    const auto *const candidateData = neighborCandidates.data();
    for (std::size_t index{0U};
         index < members.size();
         ++index)
    {
        if (memberCells[index] == noCell)
            continue;

        Vec2 separation{};
        const Vec2 memberPosition = memberData[index].position;
        const auto &cell = neighborCells[memberCells[index]];
        for (const auto *cursor = candidateData + cell.candidateBegin;
             cursor != candidateData + cell.candidateEnd; ++cursor)
        {
            const std::size_t neighborIndex = *cursor;
            if (neighborIndex == index)
                continue;
            if (metrics != nullptr)
            {
                ++metrics->neighborCandidatesExamined;
            }

            Vec2 away{
                memberPosition.x - memberData[neighborIndex].position.x,
                memberPosition.y - memberData[neighborIndex].position.y};
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
