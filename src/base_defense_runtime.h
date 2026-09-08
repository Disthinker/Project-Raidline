#pragma once
#include "base_defense_state.h"
#include "enemy_combat_definition.h"
#include "enemy_squad.h"
#include "raid_space_query.h"
#include "world_shooting_runtime.h"
#include <optional>
#include <span>

struct BaseDefenseRuntimeMetrics
{
    std::size_t navigationQueries{};
    std::size_t blockerTests{};
    std::size_t activeEnemies{};
    std::size_t substeps{};
    double updateMilliseconds{};
    double navigationMilliseconds{};
};

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
                 WorldShootingRuntime &shooting, const std::vector<BallisticBlocker> &shotBlockers);
    [[nodiscard]] const BaseDefenseSnapshot &state() const noexcept { return state_; }
    [[nodiscard]] const std::vector<Enemy> &enemies() const noexcept { return enemies_; }
    [[nodiscard]] BaseDefenseSnapshot checkpoint(const WorldShootingRuntime &) const;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] bool breached() const noexcept;
    [[nodiscard]] int damageLastUpdate() const noexcept { return damageLastUpdate_; }
    [[nodiscard]] std::optional<EnemyAttackType> attackTypeLastUpdate() const noexcept
    {
        return attackTypeLastUpdate_;
    }
    [[nodiscard]] const BaseDefenseRuntimeMetrics &metrics() const noexcept { return metrics_; }
    [[nodiscard]] bool playerExposed(Vec2 center) const noexcept;

  private:
    friend struct EnemyLifecycleTestAccess;
    BaseDefenseSnapshot state_;
    struct EnemyAttachedState
    {
        std::optional<CheckpointPoint> navigationTarget;
        float navigationRefreshRemaining{};
        std::optional<float> contactSeconds;
    };
    EnemyRoster<EnemyAttachedState> enemies_;
    std::vector<BallisticBlocker> enemyBlockers_;
    std::optional<RaidSpaceBlockerIndex> blockerIndex_;
    std::optional<RaidSpaceNavigationField> navigation_;
    std::vector<std::size_t> blockerScratch_;
    int damageLastUpdate_{};
    std::optional<EnemyAttackType> attackTypeLastUpdate_;
    BaseDefenseRuntimeMetrics metrics_;
    void spawn(float dt, Vec2 playerCenter);
    void step(float dt, Vec2 playerPosition, Vec2 playerSize, bool shotFired);
    void synchronizeActorCheckpoints();
    [[nodiscard]] const BaseDefenseWaveSnapshot *waveFor(std::uint64_t id) const noexcept;
};
