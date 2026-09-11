#pragma once

#include "combat_runtime_checkpoint.h"
#include "fortification_checkpoint.h"
#include "rect.h"
#include "vec2.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

inline constexpr std::uint32_t kBaseDefenseRulesVersion = 1U;
// Legacy events keep v1; new schema-48 warning preparations opt into v2.
inline constexpr std::uint32_t kFortifiedBaseDefenseRulesVersion = 2U;
inline constexpr std::uint32_t kBaseDefenseMaximumActiveEnemies = 16U;
inline constexpr std::uint32_t kBaseDefenseBreachLimit = 6U;

enum class BaseDefenseMode
{
    Realtime
};
enum class BaseDefenseEndReason
{
    Completed,
    PlayerDown,
    Breached,
    Abandoned
};

struct BaseDefenseWaveSnapshot
{
    float releaseSeconds{};
    Vec2 entry{};
    Vec2 target{};
    std::vector<Vec2> route;
    std::vector<std::uint64_t> enemyIds;
    int enemyMaxHealth{100};
};

struct BaseDefenseContactSnapshot
{
    std::uint64_t enemyId{};
    float seconds{};
};

// Frozen layout and checkpoint of one Base-only activity. Registry assets and
// player health remain in the same authoritative Profile, never in this DTO.
struct BaseDefenseSnapshot
{
    std::string eventId;
    std::uint64_t siegeSequence{};
    BaseDefenseMode mode{BaseDefenseMode::Realtime};
    std::uint32_t rulesVersion{kBaseDefenseRulesVersion};
    std::string siteDefinitionId;
    std::string plotId;
    std::string layoutIdentity;
    std::uint64_t seed{};
    Vec2 worldSize{};
    Rect safeCore{};
    std::vector<Rect> movementBlockers;
    std::vector<FortificationSnapshot> fortifications;
    std::uint32_t fortificationGeometryRevision{};
    std::vector<FortificationAttackBinding> fortificationAttacks;
    std::vector<Rect> corridors;
    std::vector<Rect> coreDefenseZones;
    std::vector<BaseDefenseWaveSnapshot> wavePlans;
    std::uint32_t frozenPopulation{};
    std::uint32_t frozenMoraleTier{};
    std::uint32_t frozenSiteThreat{};
    std::uint32_t breachLimit{kBaseDefenseBreachLimit};
    std::uint32_t maximumActiveEnemies{kBaseDefenseMaximumActiveEnemies};
    std::uint64_t layoutHash{};

    float elapsedSeconds{};
    std::uint32_t currentWave{};
    std::uint32_t spawnedEnemyCount{};
    float nextSpawnDelay{};
    std::uint32_t navigationScheduleCursor{};
    std::uint32_t attackScheduleCursor{};
    // Persist stable combat identities, never the coordinator's vector slots.
    std::vector<std::uint64_t> reservedAttackers;
    std::vector<std::uint64_t> killedIds;
    std::vector<std::uint64_t> breachedIds;
    std::vector<BaseDefenseContactSnapshot> contacts;
    std::vector<EnemyRuntimeCheckpoint> enemies;
    Vec2 playerPosition{};
    float damageProtectionSeconds{};
    WorldShootingCheckpoint shooting;
    std::uint32_t activeWeaponSlot{};
    std::uint64_t commandSequence{};
    std::uint64_t weaponFaultSequence{};
    std::uint64_t medicalRandomSequence{};
    std::uint64_t woundRandomSequence{};
    double pendingWorldSeconds{};
    float baseCombatElapsedSeconds{};
    float medicalTickAccumulatorSeconds{};
};

// Presence marks a new warning even when no legal realtime layout exists.
// A missing layout keeps automatic defense available, without retry/reroll.
struct BaseDefenseWarningSnapshot
{
    std::string eventId;
    std::optional<BaseDefenseSnapshot> layout;
};

[[nodiscard]] std::uint64_t baseDefenseLayoutHash(const BaseDefenseSnapshot &snapshot) noexcept;
[[nodiscard]] std::uint64_t baseDefenseCheckpointHash(const BaseDefenseSnapshot &snapshot) noexcept;
[[nodiscard]] bool validateBaseDefenseSnapshot(const BaseDefenseSnapshot &snapshot,
                                               std::string &message);
