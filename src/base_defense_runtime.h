#pragma once
#include "base_defense_state.h"
#include "enemy_combat_definition.h"
#include "enemy_squad.h"
#include "player_damage_observation.h"
#include "raid_space_query.h"
#include "world_shooting_runtime.h"
#include "world_melee_runtime.h"
#include "fortification_runtime.h"
#include <optional>
#include <span>
#include <functional>
#include "raid_noise_decoy_runtime.h"

struct BaseDefenseRuntimeMetrics
{
    std::size_t navigationQueries{};
    std::size_t blockerTests{};
    std::size_t activeEnemies{};
    std::size_t substeps{};
    double updateMilliseconds{};
    double navigationMilliseconds{};
    std::size_t fortificationDamageFacts{};
    std::size_t fortificationsDisabled{};
    std::size_t dynamicGeometryBatches{};
};

struct BaseEnemyDeathFact {
    CombatTargetId id{};
    Vec2 center{}, size{};
};
inline std::vector<std::pair<CombatTargetId, Vec2>> captureBaseEnemySizes(const EnemyLifecycle &actors) {
    std::vector<std::pair<CombatTargetId, Vec2>> sizes;
    sizes.reserve(actors.size());
    for (const auto &enemy : actors) sizes.emplace_back(enemy.combatTargetId(), enemy.size());
    return sizes;
}
inline void appendBaseEnemyDeaths(std::vector<BaseEnemyDeathFact> &deaths,
    std::span<const EnemyRemovalFact> removals,
    std::span<const std::pair<CombatTargetId, Vec2>> sizes) {
    for (const auto &fact : removals) if (fact.reason == EnemyRemovalReason::Death) {
        const auto found = std::find_if(sizes.begin(), sizes.end(), [&](const auto &entry) { return entry.first == fact.id; });
        if (found != sizes.end()) deaths.push_back({fact.id,
            {fact.position.x + found->second.x / 2, fact.position.y + found->second.y / 2}, found->second});
    }
}

// Owns siege-source actors only. It never owns or mutates Profile/Registry.
class BaseDefenseRuntime
{
  public:
    [[nodiscard]] static std::optional<BaseDefenseSnapshot>
    prepare(BaseDefenseSnapshot seedInputs, std::span<const BallisticBlocker> blockers,
            const EnemyCombatDefinition &enemyDefinition);
    [[nodiscard]] bool resume(const BaseDefenseSnapshot &,
                              std::span<const BallisticBlocker> blockers);
    void advance(const GameplayInput &, float dt, Vec2 playerPosition, Vec2 playerSize, bool moving,
                 WorldShootingRuntime &shooting, const std::vector<BallisticBlocker> &shotBlockers, WorldMeleeRuntime *melee=nullptr,
                 RaidNoiseDecoyRuntime *decoy=nullptr, const RaidSpaceDefinitionId &space=RaidSpaceDefinitionId{},
                 const std::function<bool()> &beforeShots={});
    [[nodiscard]] float controlLastUpdate() const noexcept { return controlLastUpdate_; }
    [[nodiscard]] const std::vector<BaseEnemyDeathFact> &deathsLastUpdate() const noexcept { return deathsLastUpdate_; }
    [[nodiscard]] bool commitPreparedMelee(WorldMeleeRuntime &);
    [[nodiscard]] const BaseDefenseSnapshot &state() const noexcept { return state_; }
    [[nodiscard]] const std::vector<Enemy> &enemies() const noexcept { return enemies_; }
    [[nodiscard]] BaseDefenseSnapshot checkpoint(const WorldShootingRuntime &) const;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] bool breached() const noexcept;
    [[nodiscard]] int damageLastUpdate() const noexcept { return damageObservation_ ? damageObservation_->baseDamage : 0; }
    [[nodiscard]] std::optional<PlayerDamageObservation> damageObservationLastUpdate() const noexcept { return damageObservation_; }
    [[nodiscard]] std::optional<EnemyAttackType> attackTypeLastUpdate() const noexcept
    {
        return damageObservation_ ? damageObservation_->attackType : std::nullopt;
    }
    [[nodiscard]] const BaseDefenseRuntimeMetrics &metrics() const noexcept { return metrics_; }
    [[nodiscard]] bool playerExposed(Vec2 center) const noexcept;
    [[nodiscard]] const FortificationRuntime &fortifications() const noexcept { return fortifications_; }
    [[nodiscard]] const std::vector<BallisticBlocker> &worldBlockers() const noexcept { return worldBlockers_; }

  private:
    friend class BaseWorld;
    friend struct EnemyLifecycleTestAccess;
    friend struct MeleeTestAccess;
    BaseDefenseSnapshot state_;
    struct EnemyAttachedState
    {
        std::optional<CheckpointPoint> navigationTarget;
        float navigationRefreshRemaining{};
        std::optional<float> contactSeconds;
        std::optional<FortificationInstanceId> structureAttack;
    };
    EnemyRoster<EnemyAttachedState> enemies_;
    std::vector<BallisticBlocker> enemyBlockers_;
    std::optional<RaidSpaceBlockerIndex> blockerIndex_;
    std::optional<RaidSpaceNavigationField> navigation_;
    std::vector<std::size_t> blockerScratch_;
    std::optional<PlayerDamageObservation> damageObservation_;
    BaseDefenseRuntimeMetrics metrics_;
    FortificationRuntime fortifications_;
    std::vector<BallisticBlocker> worldBlockers_;
    void refreshWorldBlockers();
    void spawn(float dt, Vec2 playerCenter);
    void step(float dt, Vec2 playerPosition, Vec2 playerSize, RaidNoiseDecoyRuntime *, const RaidSpaceDefinitionId &);
    float controlLastUpdate_{};
    std::vector<BaseEnemyDeathFact> deathsLastUpdate_;
    void synchronizeActorCheckpoints();
    [[nodiscard]] const BaseDefenseWaveSnapshot *waveFor(std::uint64_t id) const noexcept;
};
