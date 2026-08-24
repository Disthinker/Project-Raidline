#include "profile_inventory_interaction.h"

#include <utility>

#include "inventory_domain.h"
#include "medical_domain.h"
#include "weapon_ammo_domain.h"

std::optional<EquipmentSlotTarget> queryProfileQuickEquipTarget(
    const ProfileState &profile,
    const ContentRegistry &content,
    AssetInstanceId instanceId)
{
    const AssetRecord *asset = profile.assets.find(instanceId);
    if (asset == nullptr ||
        !std::holds_alternative<StoredAssetLocation>(asset->location))
    {
        return std::nullopt;
    }
    const ItemDefinition &definition = content.item(asset->definitionId);
    for (EquipmentSlotKind slot : itemEquipmentSlots(definition))
    {
        const InventoryEquipCommand command{instanceId, slot};
        if (!equippedAsset(profile, slot).has_value() &&
            queryInventory(profile, content, command).canCommit)
        {
            return EquipmentSlotTarget{slot};
        }
    }
    return std::nullopt;
}

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
        if (definition.category == ItemCategory::Magazine &&
            assetIsCarried(profile, instanceId))
        {
            // Keep the Raid affordance visible for empty magazines and full
            // packs as well; action start reports why it cannot proceed.
            return ProfileContextActionKind::UnloadMagazine;
        }
        if (definition.category == ItemCategory::Medical &&
            queryMedicalUse(
                profile,
                content,
                instanceId,
                MedicalAccess::CarriedOnly).canCommit)
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
        queryMedicalUse(
            profile,
            content,
            instanceId,
            MedicalAccess::AnyOwned).canCommit)
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
    // Raid combat and medical ticks may advance the Profile revision while a
    // pointer gesture is in progress. The drop path always queries and
    // executes against the current Profile, so an unrelated revision change
    // must not make every carried item immovable. A moved/removed source is
    // still stale and is rejected before any command is sent.
    return asset != nullptr && asset->location == source.location;
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
