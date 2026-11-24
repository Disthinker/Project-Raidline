#pragma once

#include "health_system.h"
#include "hit_resolution.h"
#include "logical_ballistics.h"
#include "player_damage_observation.h"
#include "raid_space_query.h"

enum class FireSupportMovementOrder { Follow, Hold, Regroup, Spread };
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

enum class SniperTargetKind { Infected, Armored, Runner, Scavenger };
enum class SniperShotOutcome { None, Fired, Hit, Killed };
struct SniperReport
{
    SniperTargetKind kind{};
    unsigned bearing{};
    std::uint64_t token{};
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

    std::optional<SniperReport> sniperReport;
    SniperShotOutcome sniperOutcome{};
    bool treating{};

    [[nodiscard]] bool alive() const noexcept { return health > 0; }
};

struct FireSupportShotEvent
{
    Vec2 origin;
    Vec2 direction;
    float noiseRadius{};
};

// Shared spatial and contact state for an escortee and an armed teammate.
class RaidCompanionRuntime
{
public:
    static constexpr Vec2 kSize{32.0F, 32.0F};
    static constexpr float kInteractionReach = 72.0F;
    explicit RaidCompanionRuntime(Vec2 spawnPosition, float followSpeed = 285.0F, float catchUpSpeed = 375.0F)
        : position_{spawnPosition}, followSpeed_{followSpeed}, catchUpSpeed_{catchUpSpeed} {}

    [[nodiscard]] Rect bounds() const noexcept { return {position_, kSize}; }
    [[nodiscard]] Vec2 center() const noexcept;
    [[nodiscard]] bool alive() const noexcept { return !health_.isDead(); }
    [[nodiscard]] int health() const noexcept { return health_.current(); }
    [[nodiscard]] bool treatmentBlocked() const noexcept { return controlRemaining_ > 0.0F || treatmentInterrupted_; }
    void restoreHealth(int amount) { if (alive() && amount > 0) static_cast<void>(health_.restore(amount)); }
    void setDeploymentHealth(int health)
    {
        if (health >= 0 && health < health_.current())
            static_cast<void>(health_.takeDamage(health_.current() - health));
    }
    void setMovementOrder(FireSupportMovementOrder order) noexcept;

    // World owns the shared static geometry and resolves the requested sweep.
    [[nodiscard]] Vec2 prepareMovement(
        Vec2 goal, const RaidSpaceNavigationField *field,
        const RaidSpaceBlockerIndex &blockers, float seconds, float stopDistance = 57.0F);
    void setPosition(Vec2 position) noexcept { position_ = position; }

    [[nodiscard]] bool hasAttackLock(CombatTargetId id) const noexcept;
    void lockAttack(CombatTargetId id);
    void forgetAttack(CombatTargetId id) noexcept;
    [[nodiscard]] float &contactProtection() noexcept { return contactProtection_; }
    void applyContact(const PlayerDamageObservation &, float controlDuration);

protected:
    FireSupportMovementOrder movementOrder_{FireSupportMovementOrder::Follow};
    float controlRemaining_{};
    bool receivingTreatment_{};
    bool treatmentInterrupted_{};
    bool performingTreatment_{};
    void clearNavigation() noexcept;
    void clearAttackLocks() noexcept { attackLocks_.clear(); }

private:
    Vec2 position_;
    Health health_{kFireSupportMaxHealth};
    float followSpeed_, catchUpSpeed_;
    std::optional<Vec2> waypoint_;
    float navigationRefresh_{};
    std::vector<CombatTargetId> attackLocks_;
    float contactProtection_{};
};

class RaidFireSupportRuntime : public RaidCompanionRuntime
{
public:
    RaidFireSupportRuntime(FireSupportConfig config, Vec2 spawnPosition);
    [[nodiscard]] FireSupportView view() const noexcept;
    void setOrders(FireSupportMovementOrder, FireSupportFireOrder) noexcept;
    void clearSpatialTransient() noexcept;
    void leaveSpace() noexcept;
    void beginFrame(bool receivingTreatment = false, bool performingTreatment = false) noexcept
    {
        shotEvent_.reset();
        receivingTreatment_ = receivingTreatment || performingTreatment;
        performingTreatment_ = performingTreatment;
        treatmentInterrupted_ = false;
    }
    [[nodiscard]] const std::optional<FireSupportShotEvent> &shotEvent() const noexcept
    {
        return shotEvent_;
    }
    void observeSniperTargets(const EnemyLifecycle &, const RaidSpaceBlockerIndex &, const RaidSmokeSight &,
                            RaidSightParameters ambient, float seconds, std::span<const RaidIlluminationCoverage *const> lights);
    [[nodiscard]] bool authorizeSniperShot(std::uint64_t token) noexcept;
    void cancelSniperAuthorization() noexcept { authorizedTarget_.reset(); }
    [[nodiscard]] HitResolutionResult updateWeapon(
        float seconds, Rect protectedPlayer, EnemyLifecycle &targets,
        const RaidSpaceBlockerIndex &blockerIndex,
        const std::vector<BallisticBlocker> &blockers, const RaidSmokeSight &smoke = {}, bool sniper = false);

private:
    FireSupportConfig config_;
    FireSupportFireOrder fireOrder_{FireSupportFireOrder::HoldFire};
    float shotCooldown_{};
    ShotId nextShotId_{1};
    std::optional<LogicalBallisticFlight> flight_;
    std::optional<FireSupportShotEvent> shotEvent_;

    std::optional<CombatTargetId> reportedTarget_, authorizedTarget_;
    std::optional<SniperReport> report_;
    SniperShotOutcome sniperOutcome_{};
    float sniperRange_{};
    Vec2 observerPosition_{};
    float watchHeading_{};
    std::uint64_t reportGeneration_{};
    void tryFire(Rect protectedPlayer, const EnemyLifecycle &targets,
                 const RaidSpaceBlockerIndex &blockerIndex, const RaidSmokeSight &smoke, bool sniper);
};
