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
        EnemySquadDecisionMetrics *metrics = nullptr) const;

    [[nodiscard]]
    const EnemySquadConfig &config() const noexcept;

private:
    EnemySquadConfig config_;
};
