#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation.h"
#include "home_region_layout.h"
#include "raid_space_spatial_index.h"
#include "rect.h"
#include "vec2.h"

enum class BaseFacilityKind
{
    Storage,
    Supply,
    Allocation,
    Medical,
    Dormitory,
    Workshop,
    RaidGate
};

struct BaseFacility
{
    BaseFacilityKind kind{BaseFacilityKind::Storage};
    Rect bounds;
    float interactionRange{56.0F};
};

struct BaseInput
{
    bool moveUp{};
    bool moveDown{};
    bool moveLeft{};
    bool moveRight{};
    bool sprint{};
    bool interactJustPressed{};
};

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

    [[nodiscard]] std::optional<BaseFacilityKind> update(
        const BaseInput &input,
        float deltaTime) noexcept;

    [[nodiscard]] Vec2 playerPosition() const noexcept;
    [[nodiscard]] Vec2 playerSize() const noexcept;
    [[nodiscard]] Vec2 playerFacingDirection() const noexcept;
    [[nodiscard]] bool playerIsMoving() const noexcept;
    [[nodiscard]] std::size_t playerAnimationFrame() const noexcept;
    [[nodiscard]] const std::array<BaseFacility, 7> &facilities() const noexcept;
    [[nodiscard]] Vec2 worldSize() const noexcept;
    [[nodiscard]] const ContentRect &baseParcel() const noexcept;
    [[nodiscard]] const HomeRegionLayout &layout() const noexcept;
    [[nodiscard]] const HomeRegionPresentationProjection &
    outdoorPresentation(ContentRect visibleWorldBounds) const;
    [[nodiscard]] std::optional<BaseFacilityKind>
    interactableFacility() const noexcept;

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
    std::array<BaseFacility, 7> facilities_;
    std::vector<BallisticBlocker> movementBlockers_;
    std::optional<RaidSpaceBlockerIndex> movementBlockerIndex_;
    std::vector<std::size_t> movementCandidates_;
    mutable HomeRegionPresentationProjection presentationCache_;
    mutable bool presentationCacheValid_{};
    mutable std::uint32_t cachedFirstChunkColumn_{};
    mutable std::uint32_t cachedLastChunkColumn_{};
    mutable std::uint32_t cachedFirstChunkRow_{};
    mutable std::uint32_t cachedLastChunkRow_{};
};

[[nodiscard]] const char *baseFacilityName(BaseFacilityKind kind) noexcept;
