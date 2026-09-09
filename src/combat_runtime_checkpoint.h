#pragma once

#include "vec2.h"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Value-only schema used by the Base defense checkpoint. Enum codes and fixed
// field arrays deliberately do not import combat services into ProfileState.
using CheckpointPoint = std::array<float, 2>;
inline CheckpointPoint checkpointPoint(Vec2 p) noexcept { return {p.x, p.y}; }
inline Vec2 runtimePoint(CheckpointPoint p) noexcept { return {p[0], p[1]}; }

struct EnemyRuntimeCheckpoint
{
    std::uint64_t id{};
    CheckpointPoint position{}, size{}, velocity{};
    int health{}, maximumHealth{};
    std::uint32_t facing{}, movement{}, role{}, awareness{}, attackPhase{};
    std::optional<std::uint32_t> attackType;
    CheckpointPoint attackDirection{}, aiMoveDirection{};
    std::optional<CheckpointPoint> lastKnownTarget;
    float attackRemaining{}, impactSlowRemaining{}, grabCooldown{}, scratchCooldown{};
    float specialChargeHold{}, searchRemaining{};
    bool specialChargeArmed{}, hitConsumed{}, activeOpportunityPending{};
    std::optional<CheckpointPoint> navigationTarget;
    float navigationRefreshRemaining{};
    friend bool operator==(const EnemyRuntimeCheckpoint &,
                           const EnemyRuntimeCheckpoint &) = default;
};

struct LogicalFlightCheckpoint
{
    std::uint64_t id{};
    CheckpointPoint origin{}, position{}, direction{}, impact{};
    float speed{}, extent{}, travelled{}, maximumDistance{};
    int damage{}, penetration{};
    std::uint64_t aimedTarget{};
    std::uint32_t aimedRegion{}, tracerStyle{};
    bool weakPoint{};
    float tracerLength{}, tracerOpacity{}, tracerLifetime{};
    friend bool operator==(const LogicalFlightCheckpoint &,
                           const LogicalFlightCheckpoint &) = default;
};

struct WorldShootingCheckpoint
{
    std::uint64_t nextShotId{1};
    std::vector<LogicalFlightCheckpoint> flights;
    // WeaponFireConfig declaration order, followed by the exact dynamic state.
    std::array<float, 16> fireConfig{};
    std::array<float, 13> fireState{};
    std::uint64_t spreadSeed{}, spreadRandomState{}, spreadRandomIncrement{1};
    std::uint32_t burstShotCount{};
    // WeaponAimConfig float declaration order and full reticle/recoil state.
    std::array<float, 9> aimConfig{};
    std::array<CheckpointPoint, 9> aimVectors{};
    float aimDownSightsProgress{}, recoilBendRemaining{};
    std::uint32_t aimControlMode{};
    bool aimInitialized{};
    std::uint64_t recoilSeed{}, recoilRandomState{}, recoilRandomIncrement{1};
    int weaponDamage{1}, weaponPenetration{};
    float maximumRange{2048}, logicalSpeed{6000};
    std::uint32_t tracerStyle{1};
    float tracerLength{30}, tracerOpacity{0.42F}, tracerLifetime{0.055F};
    // The pre-start empty DTO is legal; live checkpoints always set initialized.
    bool initialized{};
    friend bool operator==(const WorldShootingCheckpoint &,
                           const WorldShootingCheckpoint &) = default;
};

[[nodiscard]] bool validateEnemyRuntimeCheckpoint(const EnemyRuntimeCheckpoint &) noexcept;
[[nodiscard]] bool validateWorldShootingCheckpoint(const WorldShootingCheckpoint &) noexcept;
