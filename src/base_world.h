#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation.h"
#include "base_defense_runtime.h"
#include "base_facility_layout_domain.h"
#include "gameplay_input.h"
#include "home_perimeter_domain.h"
#include "home_region_layout.h"
#include "raid_space_spatial_index.h"
#include "rect.h"
#include "vec2.h"
#include "world_shooting_runtime.h"
#include "world_melee_runtime.h"
#include "raid_grenade_runtime.h"
#include "raid_noise_decoy_runtime.h"
#include "raid_visibility.h"
#include "grenade_explosion_plan.h"

enum class BaseFacilityKind
{
    Storage,
    Supply,
    Allocation,
    Medical,
    Dormitory,
    KitchenWater,
    Workshop,
    RaidGate
};

struct BaseFacility
{
    BaseFacilityKind kind{BaseFacilityKind::Storage};
    Rect bounds;
    bool active{true};
};

[[nodiscard]] BaseFacilityAccessGeometry baseFacilityAccessGeometry(
    const BaseFacility &facility) noexcept;

[[nodiscard]] std::optional<BaseFacilityWorkSocketProjection>
baseFacilityWorkSocket(const BaseFacility &facility) noexcept;

struct BaseFacilitySpatialOverride
{
    BaseFacilityKind kind{BaseFacilityKind::Storage};
    Vec2 worldCenter{};
    bool active{true};

    friend bool operator==(
        const BaseFacilitySpatialOverride &left,
        const BaseFacilitySpatialOverride &right)
    {
        return left.kind == right.kind &&
            left.worldCenter.x == right.worldCenter.x &&
            left.worldCenter.y == right.worldCenter.y &&
            left.active == right.active;
    }
};

using BaseInput = GameplayInput;

struct HomeRegionPresentationProjection
{
    std::vector<RaidTerrainSpan> terrainSpans;
    std::vector<RaidOutdoorRoadCell> roadCells;
    std::vector<RaidOutdoorPropSnapshot> props;
    std::vector<HomeRegionDistrictSnapshot> districts;
    std::size_t queriedChunkCount{};
    std::uint64_t cacheRevision{};
};

class BaseWorld
{
public:
    BaseWorld();
    [[nodiscard]] std::optional<BaseDefenseSnapshot> checkoutFrozenDefense(
        BaseDefenseSnapshot snapshot) const;

    void configureSite(std::string_view siteDefinitionId);
    void configureSite(
        std::string_view siteDefinitionId,
        std::vector<BaseFacilitySpatialOverride> overrides,
        std::string plotId = {});
    [[nodiscard]] bool surveying() const noexcept { return plotId_ == "survey"; }
    [[nodiscard]] bool hasFoundingPlots() const noexcept { return !plotId_.empty(); }
    [[nodiscard]] const std::string &plotId() const noexcept { return plotId_; }

    [[nodiscard]] std::optional<BaseFacilityKind> update(
        const BaseInput &input,
        float deltaTime,
        const std::function<bool()> &commitGrenade = {});

    [[nodiscard]] const RaidSpaceDefinitionId &activeSpaceId() const noexcept { return spaceId_; }
    [[nodiscard]] bool playerControlled() const noexcept { return playerControlRemaining_ > 0; }
    [[nodiscard]] float playerControlRemaining() const noexcept { return playerControlRemaining_; }
    [[nodiscard]] float playerImpactSlowRemaining() const noexcept { return playerImpactSlowRemaining_; }
    void applyPlayerControl(float seconds) noexcept;
    void applyPlayerImpactSlow(float seconds) noexcept;
    [[nodiscard]] bool restorePlayerBody(Vec2 position, Vec2 facing, float control, float slow,
                                         bool preserveWeaponState = false) noexcept;
    [[nodiscard]] const RaidVisibilityProjection &observation() const;
    [[nodiscard]] RaidObservationKey observationKey() const;
    [[nodiscard]] bool boundsObservable(Rect bounds) const;
    [[nodiscard]] bool enemyObservable(CombatTargetId id) const;
    void setDaylight(bool day) noexcept;
    void setWeaponLight(std::uint64_t device, std::uint64_t battery,
        float angle, float distance, float backgroundRadius) noexcept;
    [[nodiscard]] float weaponLightBackgroundRadius() const noexcept { return lightBackgroundRadius_; }
    [[nodiscard]] const std::array<RaidVisibilityProjection, 2> &weaponLightBackgroundObservations() const;
    [[nodiscard]] const RaidNoiseDecoyRuntime &noiseDecoy() const noexcept { return noiseDecoy_; }
    [[nodiscard]] const RaidGrenadeRuntime &grenade() const noexcept { return grenade_; }
    [[nodiscard]] std::optional<NoiseDecoyLanding> previewNoiseDecoy(Vec2, const NoiseDecoyUseDefinition &) const;
    [[nodiscard]] std::optional<GrenadeLanding> previewGrenade(Vec2, const GrenadeUseDefinition &) const;
    void commitNoiseDecoy(RaidNoiseDecoyRuntime prepared) noexcept { noiseDecoy_ = std::move(prepared); }
    void commitGrenade(RaidGrenadeRuntime prepared) noexcept { grenade_ = std::move(prepared); }
    [[nodiscard]] std::optional<GrenadeExplosionPlan> prepareGrenadeExplosion(int playerHealth) const;
    [[nodiscard]] bool validatePreparedGrenadeExplosion(const GrenadeExplosionPlan &) const;
    [[nodiscard]] bool commitPreparedGrenadeExplosion(const GrenadeExplosionPlan &);
    [[nodiscard]] const std::optional<GrenadeExplosion> &lastGrenadeExplosion() const noexcept { return lastGrenadeExplosion_; }
    [[nodiscard]] bool grenadeExplodedLastUpdate() const noexcept { return grenadeExplodedLastUpdate_; }

    [[nodiscard]] Vec2 playerPosition() const noexcept;
    [[nodiscard]] Vec2 playerSize() const noexcept;
    [[nodiscard]] Vec2 playerFacingDirection() const noexcept;
    [[nodiscard]] bool playerIsMoving() const noexcept;
    [[nodiscard]] std::size_t playerAnimationFrame() const noexcept;
    [[nodiscard]] const std::array<BaseFacility, 8> &facilities() const noexcept;
    [[nodiscard]] Vec2 worldSize() const noexcept;
    [[nodiscard]] const std::string &siteDefinitionId() const noexcept;
    [[nodiscard]] const ContentRect &baseParcel() const noexcept;
    [[nodiscard]] bool canAccessStash() const noexcept;
    [[nodiscard]] const HomeRegionLayout &layout() const noexcept;
    [[nodiscard]] std::vector<ContentRect>
    basePlacementBlockers() const;
    [[nodiscard]] std::vector<ContentRect>
    basePlacementBlockersExcluding(BaseFacilityKind facility) const;
    void configureGroundBlockers(std::vector<ContentRect> blockers);
    // Bounded Profile projection, never a route proof or enemy reconstruction.
    [[nodiscard]] bool configureFortifications(const BaseFortificationState &, const ContentRegistry &);
    [[nodiscard]] std::span<const FortificationSnapshot> fortifications() const noexcept;
    [[nodiscard]] std::uint64_t placementGeometryRevision() const noexcept { return placementGeometryRevision_; }
    [[nodiscard]] const HomeRegionPresentationProjection &
    outdoorPresentation(ContentRect visibleWorldBounds) const;
    [[nodiscard]] std::optional<BaseFacilityKind>
    interactableFacility() const noexcept;
    void configureHomePerimeter(
        const HomePerimeterSiteSnapshot *snapshot);
    [[nodiscard]] HomeRegionSafetyZone playerSafetyZone() const noexcept;
    [[nodiscard]] const std::vector<Enemy> &perimeterEnemies() const noexcept;
    [[nodiscard]] std::vector<HomePerimeterEnemySnapshot>
    perimeterEnemySnapshots() const;
    [[nodiscard]] int perimeterDamageLastUpdate() const noexcept;
    [[nodiscard]] std::optional<PlayerDamageObservation> perimeterDamageObservation() const noexcept { return perimeterDamageObservation_; }
    [[nodiscard]] std::optional<PlayerDamageObservation> baseDefenseDamageObservation() const noexcept
    { return baseDefense_ ? baseDefense_->damageObservationLastUpdate() : std::nullopt; }
    [[nodiscard]] const std::vector<EnemyRemovalFact> &perimeterRemovalsLastUpdate() const noexcept
    { return perimeterRemovals_; }
    [[nodiscard]] const std::vector<BaseEnemyDeathFact> &baseEnemyDeathsLastUpdate() const noexcept { return baseEnemyDeaths_; }
    [[nodiscard]] std::optional<BaseDefenseSnapshot> prepareBaseDefenseSnapshot(
        BaseDefenseSnapshot seedInputs, const EnemyCombatDefinition &enemyDefinition) const;
    [[nodiscard]] bool resumeBaseDefense(const BaseDefenseSnapshot &snapshot);
    [[nodiscard]] std::optional<BaseDefenseSnapshot> baseDefenseCheckpoint() const;
    [[nodiscard]] const BaseDefenseSnapshot *baseDefenseState() const noexcept;
    [[nodiscard]] bool baseDefenseActive() const noexcept;
    [[nodiscard]] const std::vector<Enemy> &baseDefenseEnemies() const noexcept;
    [[nodiscard]] int baseDefenseDamageLastUpdate() const noexcept;
    [[nodiscard]] std::optional<EnemyAttackType> baseDefenseAttackTypeLastUpdate() const noexcept;
    [[nodiscard]] const BaseDefenseRuntimeMetrics &baseDefenseMetrics() const noexcept;
    void clearBaseDefense() noexcept;
    // Accepted Profile replacement is not a same-cycle synchronization.
    void resetCombatForProfileLoad() noexcept;
    // Discard space-local flights/facts, retaining the current enemy roster.
    void clearSpatialCombatState() noexcept;

    void configureWeaponFire(const WeaponUseDefinition &definition);
    void configureWeaponFire(
        const WeaponUseDefinition &definition,
        const WeaponHandlingParameters &handling,
        bool preserveWeaponFireTransientState);
    void configureWeaponAmmunition(int penetration) noexcept;
    void configureMelee(std::optional<MeleeUseDefinition> use, bool usable, std::uint64_t assetId=0) noexcept {melee_.configure(use,usable,assetId);}
    [[nodiscard]] const WorldMeleeRuntime &melee() const noexcept {return melee_;}
    [[nodiscard]] bool commitPreparedMelee();
    void cancelPreparedMelee() noexcept {melee_.cancelPrepared();}
    [[nodiscard]] std::vector<ShotPresentationSnapshot>
    shotPresentationSnapshots() const;
    [[nodiscard]] std::vector<ShotFeedbackPresentationSnapshot>
    shotFeedbackPresentationSnapshots() const;
    [[nodiscard]] const std::vector<Particle> &particles() const noexcept;
    [[nodiscard]] const std::vector<HitResult> &
    hitResultsLastUpdate() const noexcept;
    [[nodiscard]] HitFeedbackPresentationSnapshot hitFeedbackPresentation() const noexcept
    { return shooting_.hitFeedbackPresentation(); }
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
    [[nodiscard]] WeaponAcousticsDefinition shotAcousticsLastUpdate() const noexcept;
    [[nodiscard]] WeaponAccuracyProjection
    weaponAccuracyProjection() const noexcept;
    [[nodiscard]] Vec2 weaponAimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 weaponAimDirection() const noexcept;
    [[nodiscard]] WeaponOpticMode configuredWeaponOptic() const noexcept { return shooting_.configuredWeaponOptic(); }
    [[nodiscard]] WeaponOpticMode activeWeaponOptic() const noexcept
    {
        const auto optic = shooting_.activeWeaponOptic();
        return optic != WeaponOpticMode::None && playerControlled() ? WeaponOpticMode::None : optic;
    }
    [[nodiscard]] const std::optional<WeaponOpticViewProjection> &weaponOpticView() const noexcept
    { return shooting_.weaponOpticView(); }
    [[nodiscard]] bool willOpenHighOptic(const GameplayInput &input, float deltaTime) const noexcept;
    void cancelWeaponOptic() noexcept { shooting_.cancelWeaponOptic(); }
    void clearWeaponOptic() noexcept { shooting_.clearWeaponOptic(); }
    [[nodiscard]] Vec2 normalizedShotScreenShakeOffset() const noexcept;
    void discardUncommittedShot() noexcept;

    void resetAtRaidGate() noexcept;
    void resetAtMedicalPoint() noexcept;
    [[nodiscard]] Vec2 medicalRespawnPosition() const noexcept;

private:
    friend class GameSession;
    friend struct EnemyLifecycleTestAccess;
    friend struct MeleeTestAccess;
    void rebuildSite(std::string_view siteDefinitionId);
    void rebuildCollisionIndex();
    void refreshDailyShotBlockers();
    std::uint64_t placementGeometryRevision_{};

    std::string siteDefinitionId_;
    RaidSpaceDefinitionId spaceId_;
    float playerControlRemaining_{}, playerImpactSlowRemaining_{};
    RaidNoiseDecoyRuntime noiseDecoy_;
    RaidGrenadeRuntime grenade_;
    std::optional<GrenadeExplosion> lastGrenadeExplosion_;
    bool grenadeExplodedLastUpdate_{};
    float grenadeFlashSeconds_{};
    bool daylight_{true};
    std::uint64_t lightDevice_{}, lightBattery_{};
    float lightAngle_{}, lightDistance_{}, lightBackgroundRadius_{};
    Vec2 observationDirection_{-1, 0};
    mutable RaidVisibilityProjection observationCache_;
    mutable std::array<RaidVisibilityProjection, 2> lightBackgroundCache_;
    mutable std::vector<std::pair<CombatTargetId, bool>> enemyObservationCache_;
    [[nodiscard]] EnemyLifecycle &activeActors() noexcept;
    [[nodiscard]] const EnemyLifecycle &activeActors() const noexcept;
    void emitNoise(Vec2 source, float radius);
    void advanceThrowables(float seconds);
    [[nodiscard]] const RaidSpaceBlockerIndex &playerEffectsGeometry() const;
    mutable std::optional<RaidSpaceBlockerIndex> playerEffectsIndex_;
    mutable std::uint64_t playerEffectsGeometryKey_{};
    mutable RaidVisibilityBoundsQuery observationBounds_;
    std::string plotId_;
    HomeRegionLayout layout_;
    Vec2 playerPosition_{};
    Vec2 playerSize_{40.0F, 52.0F};
    Vec2 playerFacingDirection_{-1.0F, 0.0F};
    float playerHorizontalFacing_{-1.0F};
    bool playerIsMoving_{};
    Animator playerMovementAnimator_;
    Rect walkableBounds_{};
    std::array<BaseFacility, 8> facilities_;
    std::vector<BaseFacilitySpatialOverride> facilityOverrides_;
    std::vector<BallisticBlocker> movementBlockers_;
    std::vector<ContentRect> groundBlockers_;
    std::optional<RaidSpaceBlockerIndex> movementBlockerIndex_;
    std::vector<std::size_t> movementCandidates_;
    WorldShootingRuntime shooting_;
    WorldMeleeRuntime melee_;
    FortificationRuntime dailyFortifications_;
    std::optional<BaseFortificationState> configuredFortifications_;
    std::vector<BallisticBlocker> dailyShotBlockers_;
    EnemyRoster<Vec2> perimeterEnemies_;
    std::vector<EnemyRemovalFact> perimeterRemovals_;
    std::vector<BaseEnemyDeathFact> baseEnemyDeaths_;
    std::optional<std::uint64_t> perimeterCycleIndex_;
    std::optional<PlayerDamageObservation> perimeterDamageObservation_;
    float perimeterDamageProtectionRemainingSeconds_{};
    std::optional<BaseDefenseRuntime> baseDefense_;
    mutable HomeRegionPresentationProjection presentationCache_;
    mutable bool presentationCacheValid_{};
    mutable std::uint32_t cachedFirstChunkColumn_{};
    mutable std::uint32_t cachedLastChunkColumn_{};
    mutable std::uint32_t cachedFirstChunkRow_{};
    mutable std::uint32_t cachedLastChunkRow_{};
};

[[nodiscard]] const char *baseFacilityName(BaseFacilityKind kind) noexcept;
