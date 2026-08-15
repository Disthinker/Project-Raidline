#include "profile_inventory_interaction.h"

#include <utility>

#include "weapon_ammo_domain.h"

std::optional<ProfileContextActionKind> queryProfileContextAction(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId instanceId,
    bool inRaid)
{
    const AssetRecord *asset = profile.assets.find(instanceId);
    if (asset == nullptr)
    {
        return std::nullopt;
    }
    const ItemDefinition &definition = content.item(asset->definitionId);
    if (inRaid)
    {
        if (definition.category == ItemCategory::Medical &&
            assetIsCarried(profile, instanceId) &&
            asset->remainingCharges > 0)
        {
            return ProfileContextActionKind::UseMedkit;
        }
        return std::nullopt;
    }
    if (definition.category == ItemCategory::Magazine)
    {
        // Keep the affordance discoverable even when the magazine is empty or
        // the Stash currently lacks room. Execution reports the domain reason.
        return ProfileContextActionKind::UnloadMagazine;
    }
    if (definition.category == ItemCategory::Medical &&
        asset->remainingCharges > 0 && profile.currentHealth < 100)
    {
        return ProfileContextActionKind::UseMedkit;
    }
    if (definition.category == ItemCategory::Weapon &&
        queryWeaponAmmo(
            profile,
            content,
            ChamberWeaponCommand{instanceId}).canCommit)
    {
        return ProfileContextActionKind::ChamberWeapon;
    }
    return std::nullopt;
}

bool profileDragSourceMatches(
    const ProfileState &profile,
    const ProfileDragSource &source) noexcept
{
    const AssetRecord *asset = profile.assets.find(source.instanceId);
    return profile.revision == source.expectedRevision &&
        asset != nullptr && asset->location == source.location;
}

InventoryPointerPhase ProfileInventoryInteractionState::pointerPhase() const noexcept
{
    return gesture_.phase();
}

bool ProfileInventoryInteractionState::pointerGestureActive() const noexcept
{
    return gesture_.active();
}

std::optional<ProfileDragSource> ProfileInventoryInteractionState::source() const noexcept
{
    return source_;
}

std::optional<ProfileDropTarget> ProfileInventoryInteractionState::hoveredTarget() const noexcept
{
    return hoveredTarget_;
}

std::optional<InventoryDragVisual>
ProfileInventoryInteractionState::activeDragVisual() const noexcept
{
    return gesture_.visual();
}

bool ProfileInventoryInteractionState::beginPointerPress(
    ProfileDragSource source,
    GridPosition itemOrigin,
    GridPosition clickedCell,
    MousePosition position,
    InventoryPointerItemGeometry geometry) noexcept
{
    if (!gesture_.begin(
            itemOrigin,
            clickedCell,
            position,
            geometry,
            source.quantity == 0
                ? std::nullopt
                : std::optional<std::uint32_t>{source.quantity}))
    {
        return false;
    }
    source.orientation = geometry.orientation;
    source_ = std::move(source);
    hoveredTarget_.reset();
    return true;
}

void ProfileInventoryInteractionState::updatePointerPosition(
    MousePosition position,
    std::optional<ProfileDropTarget> target) noexcept
{
    gesture_.update(position);
    hoveredTarget_ = std::move(target);
}

bool ProfileInventoryInteractionState::rotatePointerItemClockwise() noexcept
{
    if (!gesture_.rotateClockwise())
    {
        return false;
    }
    if (source_.has_value())
    {
        source_->orientation = rotatedClockwise(source_->orientation);
    }
    return true;
}

std::optional<ProfileDropRequest>
ProfileInventoryInteractionState::releasePointer(
    MousePosition position,
    std::optional<ProfileDropTarget> target) noexcept
{
    gesture_.update(position);
    hoveredTarget_ = std::move(target);
    if (gesture_.phase() != InventoryPointerPhase::Dragging ||
        !source_.has_value() || !hoveredTarget_.has_value())
    {
        reset();
        return std::nullopt;
    }

    ProfileDropRequest request{*source_, *hoveredTarget_};
    if (const auto visual = gesture_.visual())
    {
        request.source.orientation = visual->orientation;
    }
    reset();
    return request;
}

void ProfileInventoryInteractionState::cancelPointerGesture() noexcept
{
    reset();
}

void ProfileInventoryInteractionState::reset() noexcept
{
    source_.reset();
    hoveredTarget_.reset();
    gesture_.reset();
}
