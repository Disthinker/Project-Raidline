#pragma once

#include <cstddef>
#include <vector>

#include "enemy_ai.h"
#include "enemy_attack.h"
#include "vec2.h"

struct EnemySquadConfig
{
    float separationRadius{82.0F};
    float maximumSeparationWeight{1.25F};
    std::size_t maximumConcurrentAttackers{10U};
};

struct EnemySquadMemberSnapshot
{
    Vec2 position{};
    bool alive{};
    EnemyAwarenessState awareness{EnemyAwarenessState::Unaware};
    EnemyAttackPhase attackPhase{EnemyAttackPhase::Idle};
    bool hasAttackOpportunity{};
};

struct EnemySquadDecisionMetrics
{
    std::size_t neighborCandidatesExamined{};
};

class EnemySquadCoordinator
{
public:
    EnemySquadCoordinator();
    explicit EnemySquadCoordinator(EnemySquadConfig config);

    [[nodiscard]]
    std::vector<EnemyTacticalDirective> decide(
        const std::vector<EnemySquadMemberSnapshot> &members,
        Vec2 targetPosition,
        EnemySquadDecisionMetrics *metrics = nullptr);

    [[nodiscard]]
    const EnemySquadConfig &config() const noexcept;

private:
    EnemySquadConfig config_;
    std::size_t attackScheduleCursor_{};
    std::vector<std::size_t> reservedAttackers_;
};
