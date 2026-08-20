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

struct WeaponMaintenanceTarget
{
    AssetInstanceId weaponAssetId{};

    friend bool operator==(const WeaponMaintenanceTarget &,
                           const WeaponMaintenanceTarget &) = default;
};

using ProfileDropTarget = std::variant<
    StoredCellTarget,
    EquipmentSlotTarget,
    MagazineLoadTarget,
    WeaponInstallTarget,
    WeaponMaintenanceTarget>;

// Returns an equipment target only when the asset declares a compatible slot,
// that slot is empty, and the authoritative inventory query accepts the move.
[[nodiscard]] std::optional<EquipmentSlotTarget>
queryProfileQuickEquipTarget(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId instanceId);

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

enum class ProfileContextActionKind
{
    UnloadMagazine,
    UseMedkit,
    ChamberWeapon
};

[[nodiscard]] std::optional<ProfileContextActionKind>
queryProfileContextAction(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId instanceId,
    bool inRaid);

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
