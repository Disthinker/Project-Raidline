#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation.h"
#include "base_facility_layout_domain.h"
#include "gameplay_input.h"
#include "home_perimeter_domain.h"
#include "home_region_layout.h"
#include "raid_space_spatial_index.h"
#include "rect.h"
#include "vec2.h"
#include "world_shooting_runtime.h"

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

    void configureSite(std::string_view siteDefinitionId);
    void configureSite(
        std::string_view siteDefinitionId,
        std::vector<BaseFacilitySpatialOverride> overrides);

    [[nodiscard]] std::optional<BaseFacilityKind> update(
        const BaseInput &input,
        float deltaTime);

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

    void configureWeaponFire(const WeaponUseDefinition &definition);
    void configureWeaponFire(
        const WeaponUseDefinition &definition,
        const WeaponHandlingParameters &handling,
        bool preserveWeaponFireTransientState);
    void configureWeaponAmmunition(int penetration) noexcept;
    [[nodiscard]] std::vector<ShotPresentationSnapshot>
    shotPresentationSnapshots() const;
    [[nodiscard]] std::vector<ShotFeedbackPresentationSnapshot>
    shotFeedbackPresentationSnapshots() const;
    [[nodiscard]] const std::vector<Particle> &particles() const noexcept;
    [[nodiscard]] const std::vector<HitResult> &
    hitResultsLastUpdate() const noexcept;
    [[nodiscard]] bool shotFiredLastUpdate() const noexcept;
    [[nodiscard]] WeaponAccuracyProjection
    weaponAccuracyProjection() const noexcept;
    [[nodiscard]] Vec2 weaponAimWorldPosition() const noexcept;
    [[nodiscard]] Vec2 weaponAimDirection() const noexcept;
    [[nodiscard]] Vec2 normalizedShotScreenShakeOffset() const noexcept;
    void discardUncommittedShot() noexcept;

    void resetAtRaidGate() noexcept;
    void resetAtMedicalPoint() noexcept;

private:
    void rebuildSite(std::string_view siteDefinitionId);
    void rebuildCollisionIndex();

    std::string siteDefinitionId_;
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
    std::vector<Enemy> perimeterEnemies_;
    std::vector<Vec2> perimeterEnemySpawns_;
    std::optional<std::uint64_t> perimeterCycleIndex_;
    int perimeterDamageLastUpdate_{};
    float perimeterDamageProtectionRemainingSeconds_{};
    mutable HomeRegionPresentationProjection presentationCache_;
    mutable bool presentationCacheValid_{};
    mutable std::uint32_t cachedFirstChunkColumn_{};
    mutable std::uint32_t cachedLastChunkColumn_{};
    mutable std::uint32_t cachedFirstChunkRow_{};
    mutable std::uint32_t cachedLastChunkRow_{};
};

[[nodiscard]] const char *baseFacilityName(BaseFacilityKind kind) noexcept;
