#pragma once

#include "health_system.h"
#include "hit_resolution.h"
#include "logical_ballistics.h"
#include "player_damage_observation.h"
#include "raid_space_query.h"

enum class FireSupportMovementOrder { Follow, Hold };
enum class FireSupportFireOrder { HoldFire, FreeFire };

inline constexpr int kFireSupportMaxHealth = 100;
inline constexpr float kFireSupportRange = 320.0F;
inline constexpr float kFireSupportShotInterval = 1.2F;

struct FireSupportConfig
{
    int damage{};
    float projectileSpeed{};
    int penetration{};
    float noiseRadius{};
};

struct FireSupportView
{
    Rect bounds;
    int health{};
    FireSupportMovementOrder movementOrder{FireSupportMovementOrder::Follow};
    FireSupportFireOrder fireOrder{FireSupportFireOrder::HoldFire};
    bool outdoor{}; // Player is outdoors (legacy display field).
    bool sameSpace{};
    bool visible{};
    bool inInteractionRange{};

    [[nodiscard]] bool alive() const noexcept { return health > 0; }
};

struct FireSupportShotEvent
{
    Vec2 origin;
    Vec2 direction;
    float noiseRadius{};
};

class RaidFireSupportRuntime
{
public:
    static constexpr Vec2 kSize{32.0F, 32.0F};
    static constexpr float kInteractionReach = 72.0F;

    RaidFireSupportRuntime(FireSupportConfig config, Vec2 spawnPosition);

    [[nodiscard]] Rect bounds() const noexcept { return {position_, kSize}; }
    [[nodiscard]] Vec2 center() const noexcept;
    [[nodiscard]] bool alive() const noexcept { return !health_.isDead(); }
    [[nodiscard]] FireSupportView view() const noexcept;
    void setOrders(FireSupportMovementOrder, FireSupportFireOrder) noexcept;
    void clearSpatialTransient() noexcept;
    void leaveSpace() noexcept;

    // World owns the shared static geometry and resolves the requested sweep.
    [[nodiscard]] Vec2 prepareMovement(
        Vec2 goal, const RaidSpaceNavigationField *field,
        const RaidSpaceBlockerIndex &blockers, float seconds);
    void setPosition(Vec2 position) noexcept { position_ = position; }

    [[nodiscard]] bool hasAttackLock(CombatTargetId id) const noexcept;
    void lockAttack(CombatTargetId id);
    void forgetAttack(CombatTargetId id) noexcept;
    [[nodiscard]] float &contactProtection() noexcept { return contactProtection_; }
    void applyContact(const PlayerDamageObservation &, float controlDuration);

    void beginFrame() noexcept { shotEvent_.reset(); }
    [[nodiscard]] const std::optional<FireSupportShotEvent> &shotEvent() const noexcept
    {
        return shotEvent_;
    }
    [[nodiscard]] HitResolutionResult updateWeapon(
        float seconds, Rect protectedPlayer, EnemyLifecycle &targets,
        const RaidSpaceBlockerIndex &blockerIndex,
        const std::vector<BallisticBlocker> &blockers);

private:
    FireSupportConfig config_;
    Vec2 position_;
    Health health_{kFireSupportMaxHealth};
    FireSupportMovementOrder movementOrder_{FireSupportMovementOrder::Follow};
    FireSupportFireOrder fireOrder_{FireSupportFireOrder::HoldFire};
    std::optional<Vec2> waypoint_;
    float navigationRefresh_{};
    std::vector<CombatTargetId> attackLocks_;
    float contactProtection_{};
    float controlRemaining_{};
    float shotCooldown_{};
    ShotId nextShotId_{1};
    std::optional<LogicalBallisticFlight> flight_;
    std::optional<FireSupportShotEvent> shotEvent_;

    void clearNavigation() noexcept;
    void tryFire(Rect protectedPlayer, const EnemyLifecycle &targets,
                 const RaidSpaceBlockerIndex &blockerIndex);
};
