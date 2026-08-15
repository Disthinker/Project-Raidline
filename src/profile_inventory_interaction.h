#pragma once

#include <optional>
#include <variant>

#include "inventory_interaction.h"
#include "profile_state.h"

struct StoredCellTarget
{
    StoredAssetLocation location;

    friend bool operator==(const StoredCellTarget &, const StoredCellTarget &) = default;
};

struct EquipmentSlotTarget
{
    EquipmentSlotKind slot{EquipmentSlotKind::PrimaryWeapon};

    friend bool operator==(const EquipmentSlotTarget &, const EquipmentSlotTarget &) = default;
};

struct MagazineLoadTarget
{
    AssetInstanceId magazineAssetId{};

    friend bool operator==(const MagazineLoadTarget &, const MagazineLoadTarget &) = default;
};

struct WeaponInstallTarget
{
    AssetInstanceId weaponAssetId{};

    friend bool operator==(const WeaponInstallTarget &, const WeaponInstallTarget &) = default;
};

using ProfileDropTarget = std::variant<
    StoredCellTarget,
    EquipmentSlotTarget,
    MagazineLoadTarget,
    WeaponInstallTarget>;

struct ProfileDragSource
{
    AssetInstanceId instanceId{};
    ProfileRevision expectedRevision{};
    AssetLocation location;
    std::uint32_t quantity{}; // zero means the complete current stack
    ItemOrientation orientation{ItemOrientation::Degrees0};

    friend bool operator==(const ProfileDragSource &, const ProfileDragSource &) = default;
};

struct ProfileDropRequest
{
    ProfileDragSource source;
    ProfileDropTarget target;

    friend bool operator==(const ProfileDropRequest &, const ProfileDropRequest &) = default;
};

[[nodiscard]] bool profileDragSourceMatches(
    const ProfileState &profile,
    const ProfileDragSource &source) noexcept;

// Owns only pointer intent. ProfileState remains authoritative and is queried
// again before a drop is committed.
class ProfileInventoryInteractionState
{
public:
    [[nodiscard]] InventoryPointerPhase pointerPhase() const noexcept;
    [[nodiscard]] bool pointerGestureActive() const noexcept;
    [[nodiscard]] std::optional<ProfileDragSource> source() const noexcept;
    [[nodiscard]] std::optional<ProfileDropTarget> hoveredTarget() const noexcept;
    [[nodiscard]] std::optional<InventoryDragVisual> activeDragVisual() const noexcept;

    [[nodiscard]] bool beginPointerPress(
        ProfileDragSource source,
        GridPosition itemOrigin,
        GridPosition clickedCell,
        MousePosition position,
        InventoryPointerItemGeometry geometry) noexcept;

    void updatePointerPosition(
        MousePosition position,
        std::optional<ProfileDropTarget> target) noexcept;

    [[nodiscard]] bool rotatePointerItemClockwise() noexcept;

    [[nodiscard]] std::optional<ProfileDropRequest> releasePointer(
        MousePosition position,
        std::optional<ProfileDropTarget> target) noexcept;

    void cancelPointerGesture() noexcept;
    void reset() noexcept;

private:
    std::optional<ProfileDragSource> source_;
    std::optional<ProfileDropTarget> hoveredTarget_;
    InventoryDragGesture gesture_;
};
