// Implementation of the App class
#include "app.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <SDL3_image/SDL_image.h>
#include <fmt/core.h>

#include "content_registry.h"
#include "developer_runtime_panel.h"
#include "frame_timing.h"
#include "alpha_content_ids.h"
#include "base_morale_domain.h"
#include "inventory_transfer.h"
#include "raid_camera.h"
#include "raid_tactical_map_presentation.h"

namespace
{
    constexpr int kWindowWidth{1280};
    constexpr int kWindowHeight{720};
    // Roughly one current reticle diameter. Relative aiming may briefly move
    // the reticle center this far beyond the viewport, but never farther.
    constexpr float kReticleViewportOutsideMargin{48.0F};

    constexpr int kPlayerSpriteWidth{64};
    constexpr int kPlayerSpriteHeight{80};

    constexpr float kPlayerMoveSourceFrameWidth{256.0f};
    constexpr float kPlayerMoveSourceFrameHeight{320.0f};

    constexpr float kPlayerMoveLeftRowY{0.0f};
    constexpr float kPlayerMoveRightRowY{320.0f};

    constexpr std::size_t kPlayerMoveFrameCount{6};

    constexpr float kEnemySpriteWidth{64.0f};
    constexpr float kEnemySpriteHeight{80.0f};

    constexpr float kEnemyMoveSourceFrameWidth{256.0f};
    constexpr float kEnemyMoveSourceFrameHeight{320.0f};

    constexpr float kEnemyMoveLeftRowY{0.0f};
    constexpr float kEnemyMoveRightRowY{320.0f};

    constexpr std::size_t kEnemyMoveFrameCount{6};
    constexpr float kInventoryCellSize{64.0f};
    constexpr float kInventoryPanelPadding{16.0f};
    constexpr float kInventoryHeaderHeight{32.0f};
    constexpr float kInventoryPanelsGap{24.0f};
    constexpr float kInventoryPanelY{72.0f};
    constexpr float kInventoryDropWidth{96.0f};
    constexpr float kStashCellSize{20.0F};
    constexpr float kStashPanelX{410.0F};
    constexpr float kStashPanelY{90.0F};
    constexpr float kStashPanelWidth{460.0F};
    constexpr float kStashPanelHeight{380.0F};
    constexpr float kStashGridX{440.0F};
    constexpr float kStashGridY{200.0F};
    constexpr float kFlowPanelX{340.0F};
    constexpr float kFlowPanelY{140.0F};
    constexpr float kFlowPanelWidth{600.0F};
    constexpr float kFlowPanelHeight{440.0F};
    constexpr float kFlowButtonX{500.0F};
    constexpr float kFlowButtonY{500.0F};
    constexpr float kFlowButtonWidth{280.0F};
    constexpr float kFlowButtonHeight{60.0F};
    constexpr float kBaseStashX{668.0F};
    constexpr float kBaseStashY{132.0F};
    constexpr float kBaseStashCellSize{28.0F};
    constexpr float kBaseStashWithRecoveryY{118.0F};
    constexpr float kBaseStashWithRecoveryCellSize{20.0F};
    constexpr float kBaseIntakeX{668.0F};
    constexpr float kBaseIntakeY{398.0F};
    constexpr float kBaseIntakeCellSize{20.0F};
    constexpr float kBasePocketCellSize{28.0F};
    constexpr std::array<EquipmentSlotKind, 3> kWeaponEquipmentSlots{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::SecondaryWeapon,
        EquipmentSlotKind::Sidearm};
    constexpr std::array<EquipmentSlotKind, 7> kProfileEquipmentSlots{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::SecondaryWeapon,
        EquipmentSlotKind::Sidearm,
        EquipmentSlotKind::Helmet,
        EquipmentSlotKind::BodyArmor,
        EquipmentSlotKind::ChestRig,
        EquipmentSlotKind::Backpack};

    const char *equipmentSlotLabel(EquipmentSlotKind slot) noexcept
    {
        switch (slot)
        {
        case EquipmentSlotKind::PrimaryWeapon:
            return "1 LONG GUN";
        case EquipmentSlotKind::SecondaryWeapon:
            return "2 LONG GUN";
        case EquipmentSlotKind::Sidearm:
            return "3 SIDEARM";
        case EquipmentSlotKind::Helmet:
            return "HELMET";
        case EquipmentSlotKind::BodyArmor:
            return "BODY ARMOR";
        case EquipmentSlotKind::ChestRig:
            return "CHEST RIG";
        case EquipmentSlotKind::Backpack:
            return "BACKPACK";
        }
        return "UNKNOWN";
    }

    const char *baseOperationalTierName(BaseOperationalTier tier) noexcept
    {
        switch (tier)
        {
        case BaseOperationalTier::Critical:
            return "CRITICAL";
        case BaseOperationalTier::Strained:
            return "STRAINED";
        case BaseOperationalTier::Stable:
            return "STABLE";
        case BaseOperationalTier::Supported:
            return "SUPPORTED";
        }
        return "UNKNOWN";
    }

    const char *baseResourceKindName(BaseResourceKind kind) noexcept
    {
        switch (kind)
        {
        case BaseResourceKind::Food:
            return "FOOD";
        case BaseResourceKind::Hygiene:
            return "HYGIENE";
        case BaseResourceKind::Morale:
            return "OPERATIONS SUPPORT";
        case BaseResourceKind::Security:
            return "SECURITY";
        }
        return "UNKNOWN";
    }

    std::string formatWorldMinutesRemaining(
        std::uint64_t totalMinutes)
    {
        const std::uint64_t days = totalMinutes / kWorldMinutesPerDay;
        const std::uint64_t remainder = totalMinutes % kWorldMinutesPerDay;
        const std::uint64_t hours = remainder / 60U;
        const std::uint64_t minutes = remainder % 60U;
        return fmt::format("{}D {:02}H {:02}M", days, hours, minutes);
    }

    // Legacy click-page geometry remains only for the supply/deployment
    // adapters while Profile inventory uses drag-and-drop.
    SDL_FRect baseActionButton(std::size_t index) noexcept
    {
        return SDL_FRect{
            570.0F + static_cast<float>(index) * 130.0F,
            548.0F,
            120.0F,
            42.0F};
    }

    SDL_FRect baseReliefButton() noexcept
    {
        return SDL_FRect{100.0F, 554.0F, 250.0F, 48.0F};
    }

    SDL_FRect baseRecycleButton() noexcept
    {
        return SDL_FRect{650.0F, 554.0F, 220.0F, 48.0F};
    }

    SDL_FRect baseGunsmithButton() noexcept
    {
        return SDL_FRect{890.0F, 554.0F, 280.0F, 48.0F};
    }

    SDL_FRect baseMedicalServiceButton() noexcept
    {
        return SDL_FRect{700.0F, 250.0F, 300.0F, 48.0F};
    }

    SDL_FRect baseResidentTreatmentButton() noexcept
    {
        return SDL_FRect{700.0F, 474.0F, 300.0F, 48.0F};
    }

    SDL_FRect baseRestButton(std::size_t index) noexcept
    {
        return SDL_FRect{
            170.0F + static_cast<float>(index) * 310.0F,
            550.0F,
            220.0F,
            50.0F};
    }

    SDL_FRect baseConstructionButton() noexcept
    {
        return SDL_FRect{710.0F, 400.0F, 340.0F, 48.0F};
    }

    SDL_FRect baseManufacturingButton() noexcept
    {
        return SDL_FRect{700.0F, 520.0F, 340.0F, 56.0F};
    }

    SDL_FRect baseFacilityStaffingButton() noexcept
    {
        return SDL_FRect{170.0F, 530.0F, 240.0F, 42.0F};
    }

    SDL_FRect baseFacilityUpgradeButton() noexcept
    {
        return SDL_FRect{430.0F, 530.0F, 240.0F, 42.0F};
    }

    SDL_FRect baseWorkforceAutoFillButton() noexcept
    {
        return SDL_FRect{170.0F, 400.0F, 300.0F, 48.0F};
    }

    SDL_FRect equipmentSlotRect(EquipmentSlotKind slot) noexcept
    {
        switch (slot)
        {
        case EquipmentSlotKind::PrimaryWeapon:
            return SDL_FRect{214.0F, 96.0F, 366.0F, 64.0F};
        case EquipmentSlotKind::SecondaryWeapon:
            return SDL_FRect{214.0F, 166.0F, 366.0F, 64.0F};
        case EquipmentSlotKind::Sidearm:
            return SDL_FRect{214.0F, 236.0F, 172.0F, 58.0F};
        case EquipmentSlotKind::Helmet:
            return SDL_FRect{408.0F, 236.0F, 172.0F, 58.0F};
        case EquipmentSlotKind::BodyArmor:
            return SDL_FRect{214.0F, 300.0F, 172.0F, 58.0F};
        case EquipmentSlotKind::ChestRig:
            return SDL_FRect{408.0F, 300.0F, 172.0F, 58.0F};
        case EquipmentSlotKind::Backpack:
            return SDL_FRect{214.0F, 364.0F, 366.0F, 54.0F};
        }
        return SDL_FRect{};
    }

    SDL_FRect installedMagazineRect(EquipmentSlotKind slot) noexcept
    {
        const SDL_FRect weaponSlot = equipmentSlotRect(slot);
        return SDL_FRect{
            weaponSlot.x + weaponSlot.w - 58.0F,
            weaponSlot.y + 20.0F,
            50.0F,
            weaponSlot.h - 26.0F};
    }

    std::optional<std::pair<EquipmentSlotKind, AssetInstanceId>>
    firstEquippedWeapon(const ProfileState &profile) noexcept
    {
        for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
        {
            if (const auto weapon = equippedAsset(profile, slot))
            {
                return std::pair{slot, *weapon};
            }
        }
        return std::nullopt;
    }

    std::optional<EquipmentSlotKind> equippedWeaponSlot(
        const ProfileState &profile,
        AssetInstanceId weaponAssetId) noexcept
    {
        for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
        {
            if (equippedAsset(profile, slot) == weaponAssetId)
            {
                return slot;
            }
        }
        return std::nullopt;
    }

    struct WeaponReadiness
    {
        bool hasWeapon{};
        bool hasChamberedRound{};
        std::size_t compatibleMagazineRounds{};
    };

    WeaponReadiness weaponReadiness(const ProfileState &profile)
    {
        WeaponReadiness result;
        std::vector<ItemDefinitionId> compatibleMagazines;
        for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
        {
            const auto weapon = equippedAsset(profile, slot);
            if (!weapon.has_value())
            {
                continue;
            }
            const AssetRecord *asset = profile.assets.find(*weapon);
            if (asset == nullptr)
            {
                continue;
            }
            result.hasWeapon = true;
            result.hasChamberedRound = result.hasChamberedRound ||
                asset->chamberedRound.has_value();
            const ItemDefinition &definition =
                publishedContentRegistry().item(asset->definitionId);
            if (definition.compatibleMagazineDefinitionId.has_value())
            {
                compatibleMagazines.push_back(
                    *definition.compatibleMagazineDefinitionId);
            }
        }
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (assetIsCarried(profile, id) &&
                std::find(
                    compatibleMagazines.begin(),
                    compatibleMagazines.end(),
                    asset.definitionId) != compatibleMagazines.end())
            {
                result.compatibleMagazineRounds += asset.magazineRounds.size();
            }
        }
        return result;
    }

    bool contains(const SDL_FRect &rect, MousePosition point) noexcept
    {
        return point.x >= rect.x && point.y >= rect.y &&
               point.x < rect.x + rect.w && point.y < rect.y + rect.h;
    }

    struct ProfileGridView
    {
        ProfileContainerId container;
        float x{};
        float y{};
        float cellSize{};
        const char *label{};
    };

    std::vector<ProfileGridView> profileGridViews(
        const ProfileState &profile,
        bool includeStash)
    {
        std::vector<ProfileGridView> result;
        if (includeStash)
        {
            const bool hasLegacyReturns = !assetsInContainer(
                profile, ProfileContainerId::baseIntake()).empty();
            result.push_back(ProfileGridView{
                ProfileContainerId::stash(),
                kBaseStashX,
                hasLegacyReturns ? kBaseStashWithRecoveryY : kBaseStashY,
                hasLegacyReturns
                    ? kBaseStashWithRecoveryCellSize
                    : kBaseStashCellSize,
                "STASH"});
            if (hasLegacyReturns)
            {
                result.push_back(ProfileGridView{
                    ProfileContainerId::baseIntake(),
                    kBaseIntakeX,
                    kBaseIntakeY,
                    kBaseIntakeCellSize,
                    "UNASSIGNED RETURNS (MOVE TO RECOVER)"});
            }
        }

        if (const auto chest = equippedAsset(
                profile,
                EquipmentSlotKind::ChestRig))
        {
            result.push_back({
                ProfileContainerId::compartment(*chest, 0),
                214.0F, 442.0F, kBasePocketCellSize, "MAG 1"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 1),
                258.0F, 442.0F, kBasePocketCellSize, "MAG 2"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 2),
                310.0F, 442.0F, kBasePocketCellSize, "UTIL 1"});
            result.push_back({
                ProfileContainerId::compartment(*chest, 3),
                354.0F, 442.0F, kBasePocketCellSize, "UTIL 2"});
        }
        if (const auto backpack = equippedAsset(
                profile,
                EquipmentSlotKind::Backpack))
        {
            result.push_back({
                ProfileContainerId::compartment(*backpack, 0),
                214.0F, 520.0F, kBasePocketCellSize, "BACKPACK"});
        }
        return result;
    }

    const char *baseSupplyCategoryName(BaseSupplyCategory category) noexcept
    {
        switch (category)
        {
        case BaseSupplyCategory::Food:
            return "FOOD";
        case BaseSupplyCategory::Medical:
            return "MEDICAL";
        case BaseSupplyCategory::Recreation:
            return "RECREATION";
        case BaseSupplyCategory::Security:
            return "SECURITY";
        }
        return "UNKNOWN";
    }

    const char *raidOutdoorPropLabel(RaidOutdoorPropKind kind) noexcept
    {
        switch (kind)
        {
        case RaidOutdoorPropKind::Factory:
            return "PROP: FACTORY";
        case RaidOutdoorPropKind::Warehouse:
            return "PROP: WAREHOUSE";
        case RaidOutdoorPropKind::Container:
            return "PROP: CONTAINER";
        case RaidOutdoorPropKind::EngineeringEquipment:
            return "PROP: ENGINEERING EQUIPMENT";
        case RaidOutdoorPropKind::Car:
            return "PROP: CAR";
        case RaidOutdoorPropKind::Truck:
            return "PROP: TRUCK";
        case RaidOutdoorPropKind::RoadBarrier:
            return "PROP: ROAD BARRIER";
        case RaidOutdoorPropKind::Debris:
            return "PROP: DEBRIS";
        }
        return "PROP: UNKNOWN";
    }

    struct BaseSupplyDefinitionProjection
    {
        const ItemDefinition *definition{};
        std::uint32_t ownedQuantity{};
        std::optional<AssetInstanceId> firstAssetId;
        bool assignedToSelectedCategory{};
        bool assignedElsewhere{};
    };

    std::vector<BaseSupplyDefinitionProjection> baseSupplyDefinitions(
        const ProfileState &profile,
        BaseSupplyCategory selectedCategory)
    {
        std::vector<BaseSupplyDefinitionProjection> result;
        for (const ItemDefinition &definition :
             publishedContentRegistry().items())
        {
            if (baseSupplyContribution(definition, selectedCategory) == 0U)
            {
                continue;
            }
            BaseSupplyDefinitionProjection projection;
            projection.definition = &definition;
            const auto assignment = profile.baseSupplyPolicy.assignments.find(
                definition.definitionId);
            projection.assignedToSelectedCategory =
                assignment != profile.baseSupplyPolicy.assignments.end() &&
                assignment->second == selectedCategory;
            projection.assignedElsewhere =
                assignment != profile.baseSupplyPolicy.assignments.end() &&
                assignment->second != selectedCategory;
            for (const auto &[id, asset] : profile.assets.records())
            {
                if (asset.definitionId != definition.definitionId ||
                    !assetIsBaseAccessible(profile, id))
                {
                    continue;
                }
                projection.ownedQuantity += asset.quantity;
                if (!projection.firstAssetId.has_value())
                {
                    projection.firstAssetId = id;
                }
            }
            if (projection.ownedQuantity > 0U ||
                projection.assignedToSelectedCategory)
            {
                result.push_back(std::move(projection));
            }
        }
        return result;
    }

    struct ProfileAssetHit
    {
        const AssetRecord *asset{};
        SDL_FRect bounds{};
        float cellSize{};
        GridPosition itemOrigin{};
        GridPosition clickedCell{};
    };

    std::optional<ProfileAssetHit> profileAssetHitAt(
        const ProfileState &profile,
        MousePosition position,
        bool includeStash)
    {
        for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
        {
            const auto weapon = equippedAsset(profile, slot);
            if (!weapon.has_value())
            {
                continue;
            }
            if (const auto magazine = installedMagazine(profile, *weapon);
                magazine.has_value() &&
                contains(installedMagazineRect(slot), position))
            {
                return ProfileAssetHit{
                    profile.assets.find(*magazine), installedMagazineRect(slot),
                    kBasePocketCellSize, GridPosition{}, GridPosition{}};
            }
        }

        for (EquipmentSlotKind slot : kProfileEquipmentSlots)
        {
            const SDL_FRect bounds = equipmentSlotRect(slot);
            if (contains(bounds, position))
            {
                if (const auto id = equippedAsset(profile, slot))
                {
                    return ProfileAssetHit{
                        profile.assets.find(*id), bounds,
                        kBasePocketCellSize, GridPosition{}, GridPosition{}};
                }
                return std::nullopt;
            }
        }

        for (const ProfileGridView &view : profileGridViews(profile, includeStash))
        {
            InventoryGridSize size{};
            try
            {
                size = profileContainerSize(
                    profile, publishedContentRegistry(), view.container);
            }
            catch (...)
            {
                continue;
            }
            const SDL_FRect bounds{
                view.x, view.y,
                static_cast<float>(size.width) * view.cellSize,
                static_cast<float>(size.height) * view.cellSize};
            if (!contains(bounds, position))
            {
                continue;
            }
            const GridPosition cell{
                static_cast<int>((position.x - view.x) / view.cellSize),
                static_cast<int>((position.y - view.y) / view.cellSize)};
            const auto id = profileAssetAtCell(
                profile, publishedContentRegistry(), view.container, cell);
            if (!id.has_value())
            {
                return std::nullopt;
            }
            const AssetRecord *asset = profile.assets.find(*id);
            const auto &stored = std::get<StoredAssetLocation>(asset->location);
            const InventoryFootprint footprint = inventoryFootprint(
                publishedContentRegistry().item(asset->definitionId),
                asset->orientation);
            return ProfileAssetHit{
                asset,
                SDL_FRect{
                    view.x + static_cast<float>(stored.origin.x) * view.cellSize,
                    view.y + static_cast<float>(stored.origin.y) * view.cellSize,
                    static_cast<float>(footprint.width) * view.cellSize,
                    static_cast<float>(footprint.height) * view.cellSize},
                view.cellSize,
                stored.origin,
                cell};
        }
        return std::nullopt;
    }

    std::optional<SDL_FRect> profileAssetBounds(
        const ProfileState &profile,
        const AssetRecord &asset,
        bool includeStash)
    {
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location))
        {
            for (const ProfileGridView &view : profileGridViews(profile, includeStash))
            {
                if (view.container != stored->container)
                {
                    continue;
                }
                const InventoryFootprint footprint = inventoryFootprint(
                    publishedContentRegistry().item(asset.definitionId),
                    asset.orientation);
                return SDL_FRect{
                    view.x + static_cast<float>(stored->origin.x) * view.cellSize,
                    view.y + static_cast<float>(stored->origin.y) * view.cellSize,
                    static_cast<float>(footprint.width) * view.cellSize,
                    static_cast<float>(footprint.height) * view.cellSize};
            }
        }
        if (const auto *equipped = std::get_if<EquippedAssetLocation>(&asset.location))
        {
            return equipmentSlotRect(equipped->slot);
        }
        if (std::holds_alternative<InstalledMagazineLocation>(asset.location))
        {
            const auto &installed = std::get<InstalledMagazineLocation>(
                asset.location);
            if (const auto slot = equippedWeaponSlot(
                    profile, installed.weaponAssetId))
            {
                return installedMagazineRect(*slot);
            }
        }
        return std::nullopt;
    }

    std::optional<ProfileDropTarget> profileDropTargetAt(
        const ProfileState &profile,
        MousePosition position,
        bool includeStash,
        GridPosition grabOffset,
        std::optional<AssetInstanceId> draggedAssetId)
    {
        const AssetRecord *dragged = draggedAssetId.has_value()
            ? profile.assets.find(*draggedAssetId)
            : nullptr;
        const ItemDefinition *draggedDefinition = dragged != nullptr
            ? &publishedContentRegistry().item(dragged->definitionId)
            : nullptr;
        const ItemCategory draggedCategory = draggedDefinition != nullptr
            ? draggedDefinition->category
            : ItemCategory::Loot;
        for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
        {
            if (!contains(equipmentSlotRect(slot), position))
            {
                continue;
            }
            if (const auto weapon = equippedAsset(profile, slot))
            {
                if (draggedCategory == ItemCategory::Magazine)
                {
                    return WeaponInstallTarget{*weapon};
                }
                if (draggedDefinition != nullptr &&
                    draggedDefinition->weaponMaintenance.has_value())
                {
                    return WeaponMaintenanceTarget{*weapon};
                }
            }
        }
        if (const auto hit = profileAssetHitAt(profile, position, includeStash);
            dragged != nullptr && hit.has_value())
        {
            const ItemCategory category = publishedContentRegistry()
                .item(hit->asset->definitionId).category;
            if (draggedCategory == ItemCategory::Ammunition &&
                category == ItemCategory::Magazine)
            {
                return MagazineLoadTarget{hit->asset->instanceId};
            }
            if (draggedCategory == ItemCategory::Magazine &&
                category == ItemCategory::Weapon)
            {
                return WeaponInstallTarget{hit->asset->instanceId};
            }
            if (draggedDefinition != nullptr &&
                draggedDefinition->weaponMaintenance.has_value() &&
                category == ItemCategory::Weapon)
            {
                return WeaponMaintenanceTarget{hit->asset->instanceId};
            }
            if (draggedDefinition != nullptr &&
                draggedDefinition->armorMaintenance.has_value() &&
                category == ItemCategory::ProtectiveGear)
            {
                return ArmorMaintenanceTarget{hit->asset->instanceId};
            }
        }

        for (EquipmentSlotKind slot : kProfileEquipmentSlots)
        {
            if (contains(equipmentSlotRect(slot), position))
            {
                return EquipmentSlotTarget{slot};
            }
        }

        for (const ProfileGridView &view : profileGridViews(profile, includeStash))
        {
            InventoryGridSize size{};
            try
            {
                size = profileContainerSize(
                    profile, publishedContentRegistry(), view.container);
            }
            catch (...)
            {
                continue;
            }
            InventoryGridLayout layout(view.x, view.y, view.cellSize, size);
            if (const auto cell = layout.screenToGrid(position))
            {
                return StoredCellTarget{StoredAssetLocation{
                    view.container,
                    GridPosition{cell->x - grabOffset.x, cell->y - grabOffset.y}}};
            }
        }
        return std::nullopt;
    }

    enum class ProfileDropFeedbackKind
    {
        Invalid,
        Ordinary,
        Special
    };

    struct ProfileDropFeedback
    {
        ProfileDropFeedbackKind kind{ProfileDropFeedbackKind::Invalid};
        const char *label{"BLOCKED"};
    };

    SDL_FRect profileContextActionRect(MousePosition anchor) noexcept
    {
        return SDL_FRect{
            std::clamp(anchor.x, 16.0F, 1000.0F),
            std::clamp(anchor.y, 16.0F, 654.0F),
            264.0F,
            46.0F};
    }

    std::optional<const char *> profileContextActionLabel(
        const ProfileState &profile,
        const AssetRecord &asset,
        bool inRaid)
    {
        const auto action = queryProfileContextAction(
            profile,
            publishedContentRegistry(),
            asset.instanceId,
            inRaid);
        if (!action.has_value())
        {
            return std::nullopt;
        }
        switch (*action)
        {
        case ProfileContextActionKind::UnloadMagazine:
            return inRaid
                ? "UNLOAD MAGAZINE (3 SEC)"
                : "UNLOAD ALL TO STASH";
        case ProfileContextActionKind::UseMedkit:
        {
            const MedicalUsePlan plan = queryMedicalUse(
                profile,
                publishedContentRegistry(),
                asset.instanceId,
                inRaid ? MedicalAccess::CarriedOnly : MedicalAccess::AnyOwned);
            switch (plan.effect)
            {
            case MedicalUseEffect::RestoreHealth:
                return inRaid ? "USE MEDKIT (5 SEC)" : "USE MEDKIT";
            case MedicalUseEffect::StopLightBleeding:
                return inRaid ? "APPLY BANDAGE (2 SEC)" : "APPLY BANDAGE";
            case MedicalUseEffect::StopAnyBleeding:
                return inRaid ? "APPLY TOURNIQUET (4 SEC)" : "APPLY TOURNIQUET";
            case MedicalUseEffect::SuppressPain:
                return inRaid ? "TAKE PAINKILLER (2 SEC)" : "TAKE PAINKILLER";
            }
            return std::nullopt;
        }
        case ProfileContextActionKind::ChamberWeapon:
            return "CHAMBER ROUND";
        }
        return std::nullopt;
    }

    const std::array<ItemDefinitionId, 15> &fixedSupplyIds()
    {
        static const std::array<ItemDefinitionId, 15> ids{
            alpha_content::rifle,
            alpha_content::pistol,
            alpha_content::magazine,
            alpha_content::pistolMagazine,
            alpha_content::ammunition,
            alpha_content::helmet,
            alpha_content::bodyArmor,
            alpha_content::chestRig,
            alpha_content::backpack,
            alpha_content::medkit,
            alpha_content::bandage,
            alpha_content::tourniquet,
            alpha_content::painkiller,
            alpha_content::weaponMaintenanceKit,
            alpha_content::armorMaintenanceKit};
        return ids;
    }

    double orientationAngle(
        ItemOrientation orientation) noexcept
    {
        switch (orientation)
        {
        case ItemOrientation::Degrees0:
            return 0.0;
        case ItemOrientation::Degrees90:
            return 90.0;
        case ItemOrientation::Degrees180:
            return 180.0;
        case ItemOrientation::Degrees270:
            return 270.0;
        }

        return 0.0;
    }

    void renderOrientedTexture(
        SDL_Renderer *renderer,
        SDL_Texture *texture,
        const SDL_FRect &orientedBounds,
        float baseWidth,
        float baseHeight,
        ItemOrientation orientation)
    {
        if (orientation == ItemOrientation::Degrees0)
        {
            static_cast<void>(
                SDL_RenderTexture(
                    renderer,
                    texture,
                    nullptr,
                    &orientedBounds));
            return;
        }

        const float centerX =
            orientedBounds.x + orientedBounds.w / 2.0F;
        const float centerY =
            orientedBounds.y + orientedBounds.h / 2.0F;
        const SDL_FRect unrotatedDestination{
            centerX - baseWidth / 2.0F,
            centerY - baseHeight / 2.0F,
            baseWidth,
            baseHeight};

        static_cast<void>(
            SDL_RenderTextureRotated(
                renderer,
                texture,
                nullptr,
                &unrotatedDestination,
                orientationAngle(orientation),
                nullptr,
                SDL_FLIP_NONE));
    }

    void renderItemQuantityBadge(
        SDL_Renderer *renderer,
        const SDL_FRect &itemBounds,
        std::uint32_t quantity,
        bool showSingle = false)
    {
        if (quantity == 0 ||
            (quantity == 1 && !showSingle))
        {
            return;
        }

        const std::string text =
            std::to_string(quantity);
        const float textWidth =
            static_cast<float>(text.size()) * 8.0F;
        const float badgeWidth =
            std::max(18.0F, textWidth + 6.0F);
        const SDL_FRect badge{
            itemBounds.x + itemBounds.w - badgeWidth - 3.0F,
            itemBounds.y + itemBounds.h - 15.0F,
            badgeWidth,
            13.0F};

        SDL_SetRenderDrawBlendMode(
            renderer,
            SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 10, 12, 220);
        SDL_RenderFillRect(renderer, &badge);
        SDL_SetRenderDrawColor(renderer, 235, 238, 240, 255);
        SDL_RenderRect(renderer, &badge);
        SDL_RenderDebugText(
            renderer,
            badge.x + 3.0F,
            badge.y + 3.0F,
            text.c_str());
    }

    bool loadTexture(
        SDL_Renderer *renderer,
        const std::string &path,
        bool useNearestScaling,
        Texture &destination)
    {
        Texture loaded{
            IMG_LoadTexture(
                renderer,
                path.c_str())};

        if (!loaded.valid())
        {
            fmt::print(
                "IMG_LoadTexture failed for '{}': {}\n",
                path,
                SDL_GetError());

            return false;
        }

        if (
            useNearestScaling &&
            !SDL_SetTextureScaleMode(
                loaded.get(),
                SDL_SCALEMODE_NEAREST))
        {
            fmt::print(
                "SDL_SetTextureScaleMode failed "
                "for '{}': {}\n",
                path,
                SDL_GetError());

            return false;
        }

        destination =
            std::move(loaded);

        return true;
    }

    std::optional<InventoryPointerEvent>
    toInventoryPointerEvent(
        const SDL_Event &event) noexcept
    {
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            return InventoryPointerEvent{
                InventoryPointerEventType::Motion,
                MousePosition{
                    event.motion.x,
                    event.motion.y}};
        }

        if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.type != SDL_EVENT_MOUSE_BUTTON_UP)
        {
            return std::nullopt;
        }

        if (event.button.button != SDL_BUTTON_LEFT)
        {
            return std::nullopt;
        }

        return InventoryPointerEvent{
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                ? InventoryPointerEventType::LeftButtonDown
                : InventoryPointerEventType::LeftButtonUp,
            MousePosition{
                event.button.x,
                event.button.y}};
    }

    std::optional<InventoryUiEvent>
    toInventoryUiEvent(
        const SDL_Event &event,
        bool controlPressed,
        bool shiftPressed) noexcept
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_LEFT &&
            (controlPressed || shiftPressed))
        {
            return InventoryUiEvent{
                InventoryPartialTransferEvent{
                    MousePosition{
                        event.button.x,
                        event.button.y},
                    controlPressed,
                    shiftPressed}};
        }

        const std::optional<InventoryPointerEvent> pointerEvent =
            toInventoryPointerEvent(event);

        if (pointerEvent.has_value())
        {
            return InventoryUiEvent{*pointerEvent};
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_R &&
            !event.key.repeat)
        {
            return InventoryUiEvent{
                InventoryRotateEvent{}};
        }

        if (event.type == SDL_EVENT_KEY_DOWN &&
            event.key.scancode == SDL_SCANCODE_F &&
            !event.key.repeat)
        {
            float pointerX{};
            float pointerY{};
            static_cast<void>(
                SDL_GetMouseState(
                    &pointerX,
                    &pointerY));

            return InventoryUiEvent{
                InventoryQuickTransferEvent{
                    MousePosition{
                        pointerX,
                        pointerY}}};
        }

        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_RIGHT &&
            controlPressed)
        {
            return InventoryUiEvent{
                InventoryQuickTransferEvent{
                    MousePosition{
                        event.button.x,
                        event.button.y}}};
        }

        return std::nullopt;
    }
}

App::App()
    : gameSession_{gameFlow_.gameSession()}
{
}

bool App::loadTextures()
{
    const char *basePath =
        SDL_GetBasePath();

    if (basePath == nullptr)
    {
        fmt::print(
            "SDL_GetBasePath failed: {}\n",
            SDL_GetError());

        return false;
    }

    fmt::print(
        "basePath: {}\n",
        basePath);

    const std::string assetRoot =
        std::string{basePath} +
        "assets/";

    const std::string backgroundPath =
        assetRoot +
        defaultV0MapDefinition()
            .backgroundTexturePath;

    const std::string playerPath =
        assetRoot +
        "characters/"
        "protagonist_left_minimal_256x320.png";

    const std::string playerMoveHorizontalPath =
        assetRoot +
        "characters/player/default/"
        "player_default_move_horizontal_6f_1536x640.png";

    const std::string enemyMoveHorizontalPath =
        assetRoot +
        "characters/enemy/default/"
        "enemy_default_move_horizontal_6f_1536x640.png";

    // 所有资源先加载到局部 RAII 对象。
    // 任意一步失败时，不会留下半完成的 App 状态。
    Texture backgroundTexture;
    Texture playerTexture;
    Texture playerMoveHorizontalTexture;
    Texture enemyMoveHorizontalTexture;

    std::array<Texture, itemCount()>
        worldItemTextures{};

    std::array<Texture, itemCount()>
        inventoryItemTextures{};

    if (!loadTexture(
            renderer_,
            backgroundPath,
            false,
            backgroundTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            playerPath,
            true,
            playerTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            playerMoveHorizontalPath,
            true,
            playerMoveHorizontalTexture))
    {
        return false;
    }

    if (!loadTexture(
            renderer_,
            enemyMoveHorizontalPath,
            true,
            enemyMoveHorizontalTexture))
    {
        return false;
    }

    const ItemDefinitionCatalog &definitions =
        itemDefinitions();

    for (
        std::size_t index = 0;
        index < definitions.size();
        ++index)
    {
        const ItemDefinition &definition =
            definitions[index];

        if (!definition.visualAssetsPublished)
        {
            continue;
        }

        const std::string worldPath =
            assetRoot +
            std::string{
                definition.worldTexturePath};

        const std::string inventoryPath =
            assetRoot +
            std::string{
                definition.inventoryTexturePath};

        if (!loadTexture(
                renderer_,
                worldPath,
                true,
                worldItemTextures[index]))
        {
            return false;
        }

        if (!loadTexture(
                renderer_,
                inventoryPath,
                true,
                inventoryItemTextures[index]))
        {
            return false;
        }
    }

    // 所有资源全部成功后，统一提交到 App 成员。
    backgroundTexture_ =
        std::move(backgroundTexture);

    playerTexture_ =
        std::move(playerTexture);

    playerMoveHorizontalTexture_ =
        std::move(
            playerMoveHorizontalTexture);

    enemyMoveHorizontalTexture_ =
        std::move(
            enemyMoveHorizontalTexture);

    worldItemTextures_ =
        std::move(worldItemTextures);

    inventoryItemTextures_ =
        std::move(inventoryItemTextures);

    return true;
}

// Init SDL video subsystem and create window
bool App::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fmt::print("SDL_Init failed: {}\n", SDL_GetError());
        return false;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        fmt::print("SDL audio unavailable: {}\n", SDL_GetError());
    }
    else
    {
        const char *basePath = SDL_GetBasePath();
        if (basePath == nullptr ||
            !gameAudio_.initialize(std::filesystem::path{basePath} / "assets"))
        {
            fmt::print(
                "Game audio output unavailable: {}\n",
                gameAudio_.lastError());
        }
    }

    char *preferencePath = SDL_GetPrefPath(
        "Disthinker",
        "Project Raidline");
    if (preferencePath == nullptr)
    {
        fmt::print("SDL_GetPrefPath failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    gameFlow_.configurePersistence(
        std::filesystem::path{preferencePath});
    settingsPath_ = std::filesystem::path{preferencePath} / "settings.json";
    uiTextRenderer_.setLanguage(loadUiLanguage(settingsPath_));
    SDL_free(preferencePath);

    window_ = SDL_CreateWindow("Project Raidline", kWindowWidth, kWindowHeight, 0);
    if (!window_)
    {
        fmt::print("SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_)
    {
        fmt::print("SDL_CreateRenderer failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }

    float displayRefreshHz{};
    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(window_);
    if (displayId != 0U)
    {
        if (const SDL_DisplayMode *displayMode =
                SDL_GetCurrentDisplayMode(displayId))
        {
            displayRefreshHz = displayMode->refresh_rate;
        }
    }
    int actualVSync{};
    const bool vsyncEnabled = SDL_SetRenderVSync(renderer_, 1) &&
        SDL_GetRenderVSync(renderer_, &actualVSync) &&
        actualVSync != 0;
    framePacingConfiguration_ = configureFramePacing(
        vsyncEnabled, displayRefreshHz);
    softwareFramePacer_.setTargetInterval(
        framePacingConfiguration_.targetIntervalNanoseconds);

    if (!uiTextRenderer_.initialize(renderer_))
    {
        fmt::print("UI font initialization failed: {}\n", SDL_GetError());
        shutdown();
        return false;
    }

    if (!loadTextures())
    {
        fmt::print("loadTextures failed: {}\n", SDL_GetError());
        shutdown();
        return false;
    }

    return true;
}

// 把输入状态翻译成 gameplay 输入
GameplayInput App::makeGameplayInput() const
{
    GameplayInput input{};

    input.moveUp =
        input_.isActionPressed(
            GameAction::MoveUp);

    input.moveDown =
        input_.isActionPressed(
            GameAction::MoveDown);

    input.moveLeft =
        input_.isActionPressed(
            GameAction::MoveLeft);

    input.moveRight =
        input_.isActionPressed(
            GameAction::MoveRight);

    input.sprint = input_.isShiftPressed();

    input.fireJustPressed =
        input_.wasActionJustPressed(
            GameAction::Fire) ||
        input_.wasPrimaryPointerJustPressed();

    input.firePressed =
        input_.isActionPressed(
            GameAction::Fire) ||
        input_.isPrimaryPointerPressed();

    input.aimDownSights = input_.isSecondaryPointerPressed();
    input.developerInfiniteAmmo = developerInfiniteAmmoEnabled_;

    const Vec2 aimCamera = raidWorldCameraOffset();
    if (pointerWorldPosition_.has_value())
    {
        input.aimWorldPosition = raidScreenToWorld(
            *pointerWorldPosition_, aimCamera);
    }
    input.aimWorldBounds = raidReticleWorldBounds(
        aimCamera,
        gameSession_.world().raidSpaceWorldSize(),
        {static_cast<float>(kWindowWidth),
         static_cast<float>(kWindowHeight)},
        kReticleViewportOutsideMargin);

    if (relativeMouseModeActive_ &&
        !inventoryOverlayState_.isOpen() &&
        !medicalWheelOpen_ &&
        !developerWeaponPanelOpen_ &&
        !tacticalMapOpen_)
    {
        input.aimMotionDelta = pendingRelativeAimMotion_;
    }
    else if (gameFlow_.isRaidScreen())
    {
        // Modal UI freezes aim. It must not feed restored absolute cursor
        // coordinates back into the relative aiming simulation.
        input.aimMotionDelta = Vec2{};
    }

    input.interactJustPressed =
        input_.wasActionJustPressed(
            GameAction::Interact);

    input.interactPressed = input_.isActionPressed(GameAction::Interact);

    input.reloadJustPressed =
        input_.wasActionJustPressed(GameAction::Reload);
    input.healJustPressed =
        input_.wasActionJustPressed(GameAction::Heal);
    if (input_.wasActionJustPressed(GameAction::SelectWeapon1))
    {
        input.weaponSlotJustPressed = EquipmentSlotKind::PrimaryWeapon;
    }
    else if (input_.wasActionJustPressed(GameAction::SelectWeapon2))
    {
        input.weaponSlotJustPressed = EquipmentSlotKind::SecondaryWeapon;
    }
    else if (input_.wasActionJustPressed(GameAction::SelectWeapon3))
    {
        input.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
    }
    return input;
}

bool App::handleScreenConfirm()
{
    bool transitioned{false};

    switch (gameFlow_.state())
    {
    case GameFlowState::MainMenu:
        handleMainMenuCommand(
            gameSession_.hasSavedProfile()
                ? MainMenuCommand::Continue
                : MainMenuCommand::NewGame);
        transitioned = gameFlow_.state() == GameFlowState::Base;
        break;
    case GameFlowState::Base:
        transitioned =
            gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
            !lostRaidRecordsOpen_ &&
            !regionalOperationsOpen_ &&
            tryDeployFromBase();
        break;
    case GameFlowState::Raid:
        break;
    case GameFlowState::RaidResult:
        transitioned = gameFlow_.returnToBase();
        break;
    }

    if (transitioned)
    {
        closeInventory();
        pendingInventoryUiEvents_.clear();
    }

    return transitioned;
}

bool App::tryDeployFromBase(
    std::optional<RegionalOutpostDefinitionId> outpostRestorationId,
    std::optional<RegionalBaseSiteDefinitionId> baseSiteClearanceId,
    std::optional<RegionalBaseSiteDefinitionId> basePerimeterSweepId)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState &profile = gameSession_.profile();
    const WeaponReadiness readiness = weaponReadiness(profile);
    const std::size_t usableRounds = readiness.compatibleMagazineRounds +
        (readiness.hasChamberedRound ? 1U : 0U);
    const bool unsafe = !readiness.hasWeapon || usableRounds == 0;
    const LostRaidAgingPreview aging = gameSession_.lostRaidAgingPreview();
    const bool expiring = aging.recordsExpiringOnNextSettlement > 0U;
    if ((unsafe || expiring) && !deploymentWarningArmed_)
    {
        deploymentWarningArmed_ = true;
        if (unsafe && expiring)
        {
            uiMessage_ =
                "UNSAFE LOADOUT AND LOST RECORD EXPIRY - CONFIRM AGAIN";
        }
        else if (expiring)
        {
            uiMessage_ =
                "LOST RECORD EXPIRES AFTER THIS RAID - CONFIRM AGAIN";
        }
        else
        {
            uiMessage_ = "UNSAFE LOADOUT - CONFIRM DEPLOY AGAIN";
        }
        return false;
    }
    const bool regionalMission = outpostRestorationId.has_value() ||
        baseSiteClearanceId.has_value() ||
        basePerimeterSweepId.has_value();
    const MapDefinition &targetMap = outpostRestorationId.has_value()
        ? content.map(content.regionalOutpost(
              *outpostRestorationId).restorationMapDefinitionId)
        : baseSiteClearanceId.has_value()
            ? content.map(*content.regionalBaseSite(
                  *baseSiteClearanceId).clearanceMapDefinitionId)
            : basePerimeterSweepId.has_value()
                ? content.map(content.regionalBaseSite(
                      *basePerimeterSweepId)
                          .perimeterSweepMapDefinitionId)
                : selectedRaidMap();
    if (targetMap.proceduralOutdoor.enabled &&
        targetMap.proceduralOutdoor.layoutVersion >= 3U)
    {
        renderRaidLoadingScreen(
            targetMap,
            {0.02F, RaidDeploymentProgressStage::Validating});
    }
    if (!gameFlow_.deploy(
            targetMap.id,
            regionalMission
                ? RaidIntelligenceLoadout{}
                : selectedRaidIntelligence_,
            regionalMission
                ? std::optional<std::string>{}
                : selectedRaidSelfRecoveryRecordId_,
            outpostRestorationId,
            baseSiteClearanceId,
            basePerimeterSweepId,
            targetMap.proceduralOutdoor.enabled
                ? RaidDeploymentProgressCallback{
                      [this, &targetMap](
                          const RaidDeploymentProgress &progress)
                      {
                          renderRaidLoadingScreen(targetMap, progress);
                      }}
                : RaidDeploymentProgressCallback{}))
    {
        uiMessage_ = gameSession_.persistenceMessage().empty()
            ? "DEPLOYMENT IS NOT AVAILABLE"
            : gameSession_.persistenceMessage();
        return false;
    }
    deploymentWarningArmed_ = false;
    lostRaidRecordsOpen_ = false;
    regionalOperationsOpen_ = false;
    selectedLostRaidRecordId_.reset();
    selectedRaidSelfRecoveryRecordId_.reset();
    selectedRaidIntelligence_ = {};
    developerMapFogEnabled_ = true;
    developerInfiniteAmmoEnabled_ = false;
    uiMessage_.clear();
    return true;
}

void App::renderRaidLoadingScreen(
    const MapDefinition &map,
    const RaidDeploymentProgress &progress)
{
    static_cast<void>(SDL_SetRenderViewport(renderer_, nullptr));
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 10, 13, 11, 255);
    SDL_RenderClear(renderer_);

    const SDL_FRect panel{170.0F, 188.0F, 940.0F, 344.0F};
    SDL_SetRenderDrawColor(renderer_, 27, 32, 27, 255);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 147, 132, 76, 255);
    SDL_RenderRect(renderer_, &panel);

    uiTextRenderer_.render(
        renderer_, panel.x + 34.0F, panel.y + 34.0F,
        "PREPARING RAID");
    uiTextRenderer_.render(
        renderer_, panel.x + 34.0F, panel.y + 82.0F,
        map.displayName.c_str());
    const char *stage = "VALIDATING RAID REQUEST";
    switch (progress.stage)
    {
    case RaidDeploymentProgressStage::Validating:
        stage = "VALIDATING RAID REQUEST";
        break;
    case RaidDeploymentProgressStage::PreparingSnapshot:
        stage = "PREPARING FROZEN RAID SNAPSHOT";
        break;
    case RaidDeploymentProgressStage::GeneratingLayout:
        stage = "GENERATING DISTRICTS, ROADS AND LANDMARKS";
        break;
    case RaidDeploymentProgressStage::FreezingLoot:
        stage = "FREEZING RESOURCE POINTS, LOOT AND ENEMIES";
        break;
    case RaidDeploymentProgressStage::ValidatingSnapshot:
        stage = "VALIDATING ROUTES AND EXTRACTION POINTS";
        break;
    case RaidDeploymentProgressStage::BuildingWorld:
        stage = "BUILDING RAID WORLD";
        break;
    case RaidDeploymentProgressStage::SavingProfile:
        stage = "SAVING DEPLOYMENT STATE";
        break;
    case RaidDeploymentProgressStage::Complete:
        stage = "RAID READY";
        break;
    }
    uiTextRenderer_.render(
        renderer_, panel.x + 34.0F, panel.y + 146.0F, stage);

    const float completion = std::clamp(progress.completion, 0.0F, 1.0F);
    const SDL_FRect track{
        panel.x + 34.0F, panel.y + 218.0F, panel.w - 68.0F, 28.0F};
    SDL_SetRenderDrawColor(renderer_, 8, 12, 11, 255);
    SDL_RenderFillRect(renderer_, &track);
    SDL_SetRenderDrawColor(renderer_, 76, 82, 67, 255);
    SDL_RenderRect(renderer_, &track);
    const SDL_FRect fill{
        track.x + 3.0F,
        track.y + 3.0F,
        std::max(0.0F, (track.w - 6.0F) * completion),
        track.h - 6.0F};
    SDL_SetRenderDrawColor(renderer_, 177, 154, 68, 255);
    SDL_RenderFillRect(renderer_, &fill);
    if (fill.w > 18.0F)
    {
        const float pulseOffset = std::fmod(
            static_cast<float>(SDL_GetTicks()) * 0.18F,
            std::max(1.0F, fill.w));
        const SDL_FRect pulse{
            fill.x + std::min(pulseOffset, fill.w - 12.0F),
            fill.y,
            std::min(12.0F, fill.w),
            fill.h};
        SDL_SetRenderDrawColor(renderer_, 238, 220, 132, 210);
        SDL_RenderFillRect(renderer_, &pulse);
    }
    const std::string percentage = fmt::format(
        "{}%", static_cast<int>(std::round(completion * 100.0F)));
    uiTextRenderer_.render(
        renderer_, track.x + track.w - 52.0F, track.y + 5.0F,
        percentage.c_str());
    uiTextRenderer_.render(
        renderer_, panel.x + 34.0F, panel.y + 278.0F,
        "PLEASE WAIT - PROGRESS FOLLOWS ACTUAL DEPLOYMENT STAGES");
    SDL_RenderPresent(renderer_);
    SDL_PumpEvents();
}

const MapDefinition &App::selectedRaidMap() const
{
    const auto &maps = publishedContentRegistry().maps();
    if (maps.empty())
    {
        throw std::logic_error("published content has no Raid maps");
    }
    return maps[selectedRaidMapIndex_ % maps.size()];
}

void App::cycleSelectedRaidMap(int direction) noexcept
{
    const std::size_t count = publishedContentRegistry().maps().size();
    if (count < 2U || direction == 0)
    {
        return;
    }
    selectedRaidMapIndex_ = direction < 0
        ? (selectedRaidMapIndex_ + count - 1U) % count
        : (selectedRaidMapIndex_ + 1U) % count;
    deploymentWarningArmed_ = false;
    selectedRaidIntelligence_ = {};
    selectedRaidSelfRecoveryRecordId_.reset();
    uiMessage_.clear();
}

void App::handleRaidIntelligenceSelection(
    RaidIntelligenceCategory category)
{
    const MapDefinition &map = selectedRaidMap();
    const std::uint32_t owned = gameSession_.profile().raidIntelligence.count(
        map.id, category);
    if (owned == 0U)
    {
        const RaidIntelligencePurchaseReceipt receipt =
            gameSession_.purchaseRaidIntelligence(
                RaidIntelligencePurchaseCommand{map.id, category},
                nextProfileTransactionId("raid-intelligence"));
        if (!receipt.succeeded)
        {
            uiMessage_ = receipt.message.empty()
                ? "INTELLIGENCE PURCHASE BLOCKED"
                : receipt.message;
            return;
        }
        selectedRaidIntelligence_.set(category, true);
        uiMessage_ = "INTELLIGENCE PURCHASED AND SELECTED";
        return;
    }
    const bool selected = selectedRaidIntelligence_.has(category);
    selectedRaidIntelligence_.set(category, !selected);
    uiMessage_ = selected
        ? "INTELLIGENCE REMOVED FROM BRIEFING"
        : "INTELLIGENCE SELECTED FOR DEPLOYMENT";
}

void App::handleRaidInteriorIntelligencePurchase(
    const RaidSpaceDefinitionId &interiorId)
{
    if (gameSession_.profile().raidInteriorIntelligence.knows(interiorId))
    {
        uiMessage_ = "INTERIOR LAYOUT PERMANENTLY KNOWN";
        return;
    }
    const RaidInteriorIntelligencePurchaseReceipt receipt =
        gameSession_.purchaseRaidInteriorIntelligence(
            RaidInteriorIntelligencePurchaseCommand{interiorId},
            nextProfileTransactionId("raid-interior-intelligence"));
    uiMessage_ = receipt.succeeded
        ? "INTERIOR LAYOUT INTELLIGENCE PURCHASED"
        : receipt.message.empty()
            ? "INTERIOR INTELLIGENCE PURCHASE BLOCKED"
            : receipt.message;
}

SDL_FRect App::mainMenuButton(std::size_t index) const noexcept
{
    return SDL_FRect{500.0F, 340.0F + static_cast<float>(index) * 58.0F, 280.0F, 46.0F};
}

std::optional<MainMenuCommand> App::mainMenuCommandAt(
    float x,
    float y) const noexcept
{
    const MousePosition point{x, y};
    if (settingsOpen_)
    {
        if (contains(mainMenuButton(0), point))
        {
            return MainMenuCommand::ToggleLanguage;
        }
        return contains(mainMenuButton(2), point)
            ? std::optional<MainMenuCommand>{MainMenuCommand::Settings}
            : std::nullopt;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (contains(mainMenuButton(index), point))
        {
            return static_cast<MainMenuCommand>(index);
        }
    }
    return std::nullopt;
}

SDL_FRect App::pauseMenuButton(std::size_t index) const noexcept
{
    return SDL_FRect{
        500.0F,
        280.0F + static_cast<float>(index) * 58.0F,
        280.0F,
        46.0F};
}

std::optional<PauseMenuCommand> App::pauseMenuCommandAt(
    float x,
    float y) const noexcept
{
    const MousePosition point{x, y};
    if (pauseMenu_.settingsOpen())
    {
        if (contains(pauseMenuButton(0), point))
        {
            return PauseMenuCommand::ToggleLanguage;
        }
        return contains(pauseMenuButton(1), point)
            ? std::optional<PauseMenuCommand>{PauseMenuCommand::Settings}
            : std::nullopt;
    }
    for (std::size_t index = 0; index < 4; ++index)
    {
        if (contains(pauseMenuButton(index), point))
        {
            return static_cast<PauseMenuCommand>(index);
        }
    }
    return std::nullopt;
}

void App::handlePauseMenuCommand(PauseMenuCommand command)
{
    if (!pauseMenu_.isOpen())
    {
        return;
    }
    if (pauseMenu_.settingsOpen())
    {
        if (command == PauseMenuCommand::ToggleLanguage)
        {
            toggleLanguage();
        }
        else if (command == PauseMenuCommand::Settings)
        {
            static_cast<void>(pauseMenu_.handleEscape());
        }
        return;
    }

    switch (command)
    {
    case PauseMenuCommand::Continue:
        pauseMenu_.close();
        uiMessage_.clear();
        break;
    case PauseMenuCommand::Settings:
        pauseMenu_.showSettings();
        break;
    case PauseMenuCommand::MainMenu:
        if (gameFlow_.state() == GameFlowState::Base &&
            !gameSession_.checkpointWorldClock())
        {
            uiMessage_ = "WORLD CLOCK SAVE FAILED: " +
                gameSession_.persistenceMessage();
            break;
        }
        closeInventory();
        medicalWheelOpen_ = false;
        medicalWheelOptions_.clear();
        developerWeaponPanelOpen_ = false;
        profileContextMenu_.reset();
        if (gameFlow_.returnToMainMenu())
        {
            pauseMenu_.close();
            uiMessage_ = gameSession_.hasSavedProfile()
                ? "RETURNED TO MAIN MENU"
                : "RETURNED TO MAIN MENU - NO PERSISTENT PROFILE";
        }
        break;
    case PauseMenuCommand::ExitDesktop:
        if (gameFlow_.state() == GameFlowState::Base &&
            !gameSession_.checkpointWorldClock())
        {
            uiMessage_ = "WORLD CLOCK SAVE FAILED: " +
                gameSession_.persistenceMessage();
            break;
        }
        running_ = false;
        break;
    case PauseMenuCommand::ToggleLanguage:
        break;
    }
}

void App::handleMainMenuCommand(MainMenuCommand command)
{
    if (settingsOpen_)
    {
        if (command == MainMenuCommand::ToggleLanguage)
        {
            toggleLanguage();
        }
        else if (command == MainMenuCommand::Settings)
        {
            settingsOpen_ = false;
        }
        return;
    }

    if (command == MainMenuCommand::Continue)
    {
        if (!gameSession_.hasSavedProfile())
        {
            uiMessage_ = "NO VALID PRIMARY SAVE";
            return;
        }
        if (!gameFlow_.continueGame())
        {
            uiMessage_ = gameSession_.persistenceMessage();
            return;
        }
        uiMessage_ = gameSession_.recoveredAbandonedRaid()
            ? "UNFINISHED RAID RESTORED TO BASE"
            : gameSession_.lastSaveLoadStatus() ==
                    SaveLoadStatus::RecoveredBackup
                ? "RECOVERED SAFE BACKUP"
                : "PROFILE LOADED";
        return;
    }

    if (command == MainMenuCommand::NewGame)
    {
        if (gameSession_.hasSavedProfile() && !newGameOverwriteArmed_)
        {
            newGameOverwriteArmed_ = true;
            uiMessage_ = "PRESS NEW GAME AGAIN TO OVERWRITE";
            return;
        }
        const auto ticks = std::chrono::system_clock::now()
            .time_since_epoch().count();
        if (!gameFlow_.startNewGame(
                "profile-" + std::to_string(ticks)))
        {
            uiMessage_ = gameSession_.persistenceMessage();
            return;
        }
        newGameOverwriteArmed_ = false;
        uiMessage_ = "NEW PROFILE CREATED";
        return;
    }

    if (command == MainMenuCommand::Settings)
    {
        settingsOpen_ = true;
        uiMessage_.clear();
        return;
    }
    running_ = false;
}

void App::toggleLanguage()
{
    const UiLanguage language = toggledUiLanguage(uiTextRenderer_.language());
    uiTextRenderer_.setLanguage(language);
    if (!saveUiLanguage(settingsPath_, language))
    {
        uiMessage_ = "LANGUAGE CHANGED - SETTINGS SAVE FAILED";
        return;
    }
    uiMessage_ = "LANGUAGE UPDATED";
}

std::string App::nextProfileTransactionId(const char *prefix)
{
    ++profileTransactionSequence_;
    return fmt::format(
        "ui.{}.rev{}.{}",
        prefix,
        gameSession_.profile().revision,
        profileTransactionSequence_);
}

SDL_FRect App::screenPrimaryButton() const noexcept
{
    return SDL_FRect{
        kFlowButtonX,
        kFlowButtonY,
        kFlowButtonWidth,
        kFlowButtonHeight};
}

bool App::screenPrimaryButtonContains(
    float x,
    float y) const noexcept
{
    const SDL_FRect button =
        screenPrimaryButton();

    return x >= button.x &&
           y >= button.y &&
           x < button.x + button.w &&
           y < button.y + button.h;
}

SDL_FRect App::raidMapPreviousButton() const noexcept
{
    return SDL_FRect{422.0F, 226.0F, 54.0F, 42.0F};
}

SDL_FRect App::raidMapNextButton() const noexcept
{
    return SDL_FRect{804.0F, 226.0F, 54.0F, 42.0F};
}

SDL_FRect App::raidLostRecordsButton() const noexcept
{
    return SDL_FRect{920.0F, 184.0F, 200.0F, 34.0F};
}

SDL_FRect App::raidRegionalOperationsButton() const noexcept
{
    return SDL_FRect{700.0F, 184.0F, 200.0F, 34.0F};
}

SDL_FRect App::regionalBaseSiteClearanceButton() const noexcept
{
    return SDL_FRect{300.0F, 360.0F, 520.0F, 40.0F};
}

SDL_FRect App::regionalFacilityReserveButton() const noexcept
{
    return SDL_FRect{300.0F, 405.0F, 420.0F, 32.0F};
}

SDL_FRect App::regionalBaseSiteFeatureButton() const noexcept
{
    return SDL_FRect{740.0F, 405.0F, 375.0F, 32.0F};
}

SDL_FRect App::regionalBasePerimeterSweepButton() const noexcept
{
    return SDL_FRect{300.0F, 610.0F, 815.0F, 34.0F};
}

SDL_FRect App::baseAutoDefenseButton() const noexcept
{
    return SDL_FRect{470.0F, 474.0F, 340.0F, 52.0F};
}

SDL_FRect App::regionalOutpostActionButton(std::size_t index) const noexcept
{
    return SDL_FRect{
        755.0F,
        446.0F + static_cast<float>(index) * 62.0F,
        360.0F,
        36.0F};
}

SDL_FRect App::lostRaidRecordRow(std::size_t index) const noexcept
{
    return SDL_FRect{
        350.0F, 238.0F + static_cast<float>(index) * 56.0F,
        580.0F, 52.0F};
}

SDL_FRect App::recoveryTaskPrimaryButton() const noexcept
{
    return SDL_FRect{360.0F, 474.0F, 270.0F, 40.0F};
}

SDL_FRect App::recoveryTaskSecondaryButton() const noexcept
{
    return SDL_FRect{650.0F, 474.0F, 270.0F, 40.0F};
}

SDL_FRect App::recoveryTaskCancelButton() const noexcept
{
    return SDL_FRect{940.0F, 474.0F, 170.0F, 40.0F};
}

SDL_FRect App::raidIntelligenceButton(std::size_t index) const noexcept
{
    return SDL_FRect{
        382.0F,
        326.0F + static_cast<float>(index) * 42.0F,
        260.0F,
        34.0F};
}

void App::closeInventory() noexcept
{
    // Tab / browsing Esc closes the inventory and clears all pointer state,
    // hover state, and transient pointer selection.
    inventoryInteraction_.reset();
    profileInventoryInteraction_.reset();
    profileContextMenu_.reset();
    profileAssetSelection_.reset();

    inventoryOverlayState_.close();
    input_.suppressPrimaryPointerUntilRelease();
}

void App::handleInventoryCancel()
{
    if (profileContextMenu_.has_value())
    {
        profileContextMenu_.reset();
        return;
    }
    if (profileInventoryInteraction_.pointerGestureActive())
    {
        profileInventoryInteraction_.cancelPointerGesture();
        uiMessage_.clear();
        return;
    }
    if (inventoryInteraction_.pointerGestureActive())
    {
        inventoryInteraction_.cancelPointerGesture();
        return;
    }

    closeInventory();
}

void App::updateBase(float deltaTime)
{
    gameSession_.advanceBaseWorldClock(deltaTime);
    if (gameSession_.baseThreatProjection().warningActive)
    {
        closeInventory();
        gameFlow_.closeBaseFacility();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        selectedLostRaidRecordId_.reset();
        profileContextMenu_.reset();
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            if (!contains(baseAutoDefenseButton(), click.position))
            {
                continue;
            }
            const BaseAutoDefenseReceipt receipt =
                gameSession_.executeBaseAutoDefense(
                    nextProfileTransactionId("base-auto-defense"));
            uiMessage_ = receipt.succeeded
                ? receipt.outcome == BaseSiegeOutcome::Defended
                    ? "BASE DEFENDED | SAFETY PERIOD STARTED"
                    : "BASE DEFENSE FAILED SOFTLY | RECOVERY PERIOD STARTED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            break;
        }
        pendingBaseClicks_.clear();
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        return;
    }

    const InventoryFrameInputDecision inventoryDecision =
        decideInventoryFrameInput(
            inventoryOverlayState_.isOpen(),
            input_.wasActionJustPressed(GameAction::ToggleInventory),
            input_.wasActionJustPressed(GameAction::InventoryCancel));

    if (inventoryDecision.controlAction == InventoryFrameControlAction::OpenInventory)
    {
        gameFlow_.closeBaseFacility();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        selectedLostRaidRecordId_.reset();
        inventoryOverlayState_.openContainerInventory();
        uiMessage_.clear();
    }
    else if (inventoryDecision.controlAction == InventoryFrameControlAction::CloseInventory)
    {
        closeInventory();
    }
    else if (inventoryDecision.controlAction == InventoryFrameControlAction::CancelInteraction)
    {
        handleInventoryCancel();
    }

    if (inventoryOverlayState_.isOpen())
    {
        if (inventoryDecision.processUiEvents)
        {
            for (const InventoryUiEvent &event : pendingInventoryUiEvents_)
            {
                handleProfileInventoryUiEvent(event, false);
            }
            for (MousePosition position : pendingProfileRightClicks_)
            {
                handleProfileRightClick(position, false);
            }
        }
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        return;
    }

    if (gameFlow_.activeBaseFacility().has_value())
    {
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                lostRaidRecordsOpen_)
            {
                lostRaidRecordsOpen_ = false;
                selectedLostRaidRecordId_.reset();
                pendingBaseClicks_.clear();
                uiMessage_.clear();
                return;
            }
            gameFlow_.closeBaseFacility();
            profileAssetSelection_.reset();
            pendingBaseClicks_.clear();
            deploymentWarningArmed_ = false;
            lostRaidRecordsOpen_ = false;
            selectedLostRaidRecordId_.reset();
            uiMessage_.clear();
            return;
        }

        if (pendingBaseRotate_ && profileAssetSelection_.has_value())
        {
            profileAssetSelection_->orientation = rotatedClockwise(
                profileAssetSelection_->orientation);
        }
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            handleBasePointerClick(click);
        }
        pendingBaseClicks_.clear();
        return;
    }

    BaseInput input;
    input.moveUp = input_.isActionPressed(GameAction::MoveUp);
    input.moveDown = input_.isActionPressed(GameAction::MoveDown);
    input.moveLeft = input_.isActionPressed(GameAction::MoveLeft);
    input.moveRight = input_.isActionPressed(GameAction::MoveRight);
    input.sprint = input_.isShiftPressed();
    input.interactJustPressed =
        input_.wasActionJustPressed(GameAction::Interact);
    gameFlow_.updateBase(input, deltaTime);
    if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Storage)
    {
        gameFlow_.closeBaseFacility();
        inventoryOverlayState_.openContainerInventory();
        uiMessage_.clear();
    }
}

void App::handleBasePointerClick(const BasePointerClick &click)
{
    const auto facility = gameFlow_.activeBaseFacility();
    if (!facility.has_value())
    {
        return;
    }

    if (*facility == BaseFacilityKind::RaidGate)
    {
        if (regionalOperationsOpen_)
        {
            if (contains(raidRegionalOperationsButton(), click.position))
            {
                regionalOperationsOpen_ = false;
                deploymentWarningArmed_ = false;
                uiMessage_.clear();
                return;
            }
            const auto &outposts =
                publishedContentRegistry().regionalOperations().outposts;
            const auto &baseSites =
                publishedContentRegistry().regionalOperations().baseSites;
            if (contains(
                    regionalBasePerimeterSweepButton(), click.position))
            {
                const BasePerimeterSweepPlan plan = queryBasePerimeterSweep(
                    gameSession_.profile(), publishedContentRegistry());
                if (!plan.canDeploy)
                {
                    uiMessage_ = plan.message;
                    gameAudio_.play(SoundEventId::UiDeny);
                    return;
                }
                const bool deployed = tryDeployFromBase(
                    std::nullopt,
                    std::nullopt,
                    plan.baseSiteDefinitionId);
                gameAudio_.play(deployed
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
            const auto candidate = std::find_if(
                baseSites.begin(), baseSites.end(),
                [&](const RegionalBaseSiteDefinition &site)
                {
                    return site.nodeId != gameSession_.profile()
                        .regionalOperations.activeBaseNodeId;
                });
            if (candidate != baseSites.end() &&
                contains(regionalBaseSiteClearanceButton(), click.position))
            {
                const RegionalBaseSiteState &state = gameSession_.profile()
                    .regionalOperations.baseSites.at(candidate->id);
                if (!state.unlocked)
                {
                    const bool deployed = tryDeployFromBase(
                        std::nullopt, candidate->id);
                    gameAudio_.play(deployed
                        ? SoundEventId::UiConfirm
                        : SoundEventId::UiDeny);
                    return;
                }
                const BaseFacilityDefinitionId kitchenWater{
                    "base_facility.kitchen_water"};
                if (!baseFacilityOwned(gameSession_.profile(), kitchenWater))
                {
                    const auto &projects = publishedContentRegistry()
                        .baseConstructionProjects();
                    const auto project = std::find_if(
                        projects.begin(), projects.end(),
                        [](const BaseConstructionProjectDefinition &entry)
                        {
                            return entry.target ==
                                BaseFacilityUpgradeTarget::KitchenWater;
                        });
                    if (project == projects.end())
                    {
                        uiMessage_ = "KITCHEN & WATER PROJECT IS NOT PUBLISHED";
                        gameAudio_.play(SoundEventId::UiDeny);
                        return;
                    }
                    const auto &active = gameSession_.profile()
                        .baseConstruction.activeProject;
                    const bool cancelling = active.has_value() &&
                        active->definitionId == project->id;
                    const BaseConstructionReceipt receipt = cancelling
                        ? gameSession_.executeCancelBaseConstruction(
                            project->id,
                            nextProfileTransactionId(
                                "cancel-kitchen-water"))
                        : gameSession_.executeStartBaseConstruction(
                            project->id,
                            nextProfileTransactionId(
                                "start-kitchen-water"));
                    uiMessage_ = receipt.succeeded
                        ? cancelling
                            ? "KITCHEN & WATER CONSTRUCTION CANCELLED"
                            : "KITCHEN & WATER CONSTRUCTION STARTED"
                        : receipt.message;
                    gameAudio_.play(receipt.succeeded
                        ? SoundEventId::UiConfirm
                        : SoundEventId::UiDeny);
                    return;
                }
                const BaseMigrationReceipt receipt =
                    gameSession_.executeBaseMigration(
                        candidate->id,
                        nextProfileTransactionId("migrate-main-base"));
                uiMessage_ = receipt.succeeded
                    ? "MAIN BASE MIGRATION COMPLETE | OLD BASE IS AN OUTPOST"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
            const BaseFacilityDefinitionId workshop{
                "base_facility.workshop"};
            if (contains(regionalFacilityReserveButton(), click.position) &&
                baseFacilityOwned(gameSession_.profile(), workshop) &&
                !baseFacilityInstalled(gameSession_.profile(), workshop))
            {
                const InstallBaseFacilityReceipt receipt =
                    gameSession_.executeInstallBaseFacility(
                        workshop,
                        nextProfileTransactionId(
                            "install-reserve-facility"));
                uiMessage_ = receipt.succeeded
                    ? "WORKSHOP INSTALLED FROM FACILITY RESERVE"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
            const auto activeSite = std::find_if(
                baseSites.begin(), baseSites.end(),
                [&](const RegionalBaseSiteDefinition &site)
                {
                    return site.nodeId == gameSession_.profile()
                        .regionalOperations.activeBaseNodeId;
                });
            const RegionalBaseSiteDefinition *featureSite =
                activeSite != baseSites.end() ? &*activeSite : nullptr;
            if (featureSite != nullptr &&
                gameSession_.profile().regionalOperations.baseSites.at(
                    featureSite->id).uniqueFeatureRepaired)
            {
                const auto stagedSite = std::find_if(
                    baseSites.begin(), baseSites.end(),
                    [&](const RegionalBaseSiteDefinition &site)
                    {
                        const RegionalBaseSiteState &state =
                            gameSession_.profile().regionalOperations
                                .baseSites.at(site.id);
                        return site.id != featureSite->id &&
                            state.unlocked &&
                            !state.uniqueFeatureRepaired;
                    });
                if (stagedSite != baseSites.end())
                {
                    featureSite = &*stagedSite;
                }
            }
            if (featureSite != nullptr &&
                contains(regionalBaseSiteFeatureButton(), click.position))
            {
                const BaseSiteFeatureRepairPlan plan =
                    queryBaseSiteFeatureRepair(
                        gameSession_.profile(),
                        publishedContentRegistry(),
                        BaseSiteFeatureRepairCommand{featureSite->id});
                if (plan.canCommit)
                {
                    const BaseSiteFeatureRepairReceipt receipt =
                        gameSession_.executeBaseSiteFeatureRepair(
                            featureSite->id,
                            nextProfileTransactionId(
                                "repair-base-site-feature"));
                    uiMessage_ = receipt.succeeded
                        ? "BASE SITE FEATURE REPAIRED | OPERATIONAL WHILE THIS IS THE MAIN BASE"
                        : receipt.message;
                    gameAudio_.play(receipt.succeeded
                        ? SoundEventId::UiConfirm
                        : SoundEventId::UiDeny);
                }
                else
                {
                    uiMessage_ = plan.message;
                    gameAudio_.play(SoundEventId::UiDeny);
                }
                return;
            }
            for (std::size_t index{}; index < outposts.size(); ++index)
            {
                if (!contains(
                        regionalOutpostActionButton(index), click.position))
                {
                    continue;
                }
                const RegionalOutpostDefinition &definition = outposts[index];
                const RegionalOutpostState &state = gameSession_.profile()
                    .regionalOperations.outposts.at(definition.id);
                if (state.disrupted)
                {
                    const bool deployed = tryDeployFromBase(
                        definition.id);
                    gameAudio_.play(deployed
                        ? SoundEventId::UiConfirm
                        : SoundEventId::UiDeny);
                    return;
                }
                if (!state.established)
                {
                    const RegionalOutpostReceipt receipt =
                        gameSession_.executeEstablishRegionalOutpost(
                            definition.id,
                            nextProfileTransactionId("establish-outpost"));
                    uiMessage_ = receipt.succeeded
                        ? "REGIONAL OUTPOST ESTABLISHED"
                        : receipt.message;
                    gameAudio_.play(receipt.succeeded
                        ? SoundEventId::UiConfirm
                        : SoundEventId::UiDeny);
                    return;
                }
                const bool assign =
                    assignedRegionalOutpostStaff(state) == 0U;
                const RegionalOutpostStaffingReceipt receipt =
                    gameSession_.executeRegionalOutpostStaffing(
                        definition.id,
                        assign,
                        nextProfileTransactionId(
                            assign ? "assign-outpost-garrison"
                                   : "clear-outpost-garrison"));
                uiMessage_ = receipt.succeeded
                    ? assign
                        ? "OUTPOST GARRISON ASSIGNED | SHORTCUTS ONLINE"
                        : "OUTPOST GARRISON RETURNED | SHORTCUTS OFFLINE"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
            return;
        }
        if (lostRaidRecordsOpen_)
        {
            if (contains(raidLostRecordsButton(), click.position))
            {
                lostRaidRecordsOpen_ = false;
                selectedLostRaidRecordId_.reset();
                uiMessage_.clear();
                return;
            }
            if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                regionalOperationsOpen_)
            {
                regionalOperationsOpen_ = false;
                pendingBaseClicks_.clear();
                uiMessage_.clear();
                return;
            }
            const std::vector<LostRaidRecordProjection> records =
                gameSession_.lostRaidRecordProjections();
            for (std::size_t index{};
                 index < records.size() && index < 4U;
                 ++index)
            {
                if (contains(lostRaidRecordRow(index), click.position))
                {
                    selectedLostRaidRecordId_ = records[index].recordId;
                    uiMessage_.clear();
                    return;
                }
            }
            const std::optional<RecoveryTaskProjection> task =
                gameSession_.recoveryTaskProjection();
            if (contains(recoveryTaskPrimaryButton(), click.position))
            {
                RecoveryTaskReceipt receipt;
                if (task.has_value() && task->readyForCollection)
                {
                    receipt = gameSession_.collectRecoveryTask(
                        nextProfileTransactionId("recovery-collect"));
                    uiMessage_ = receipt.succeeded
                        ? fmt::format(
                              "RECOVERY COLLECTED | {} RETURNED | {} LOST",
                              receipt.recoveredAssetCount,
                              receipt.lostAssetCount)
                        : receipt.message;
                }
                else if (!task.has_value() &&
                         selectedLostRaidRecordId_.has_value())
                {
                    receipt = gameSession_.startRecoveryTask(
                        *selectedLostRaidRecordId_,
                        nextProfileTransactionId("recovery-start"));
                    uiMessage_ = receipt.succeeded
                        ? "RECOVERY TASK STARTED"
                        : receipt.message;
                    if (receipt.succeeded)
                    {
                        if (selectedRaidSelfRecoveryRecordId_ ==
                            selectedLostRaidRecordId_)
                        {
                            selectedRaidSelfRecoveryRecordId_.reset();
                        }
                        selectedLostRaidRecordId_.reset();
                    }
                }
                else
                {
                    uiMessage_ = task.has_value()
                        ? "RECOVERY TASK IS STILL IN PROGRESS"
                        : "SELECT A LOST RAID RECORD";
                }
                return;
            }
            if (selectedLostRaidRecordId_.has_value() &&
                contains(recoveryTaskSecondaryButton(), click.position))
            {
                if (selectedRaidSelfRecoveryRecordId_ ==
                    selectedLostRaidRecordId_)
                {
                    selectedRaidSelfRecoveryRecordId_.reset();
                    uiMessage_ = "RAID SELF-RECOVERY TARGET CLEARED";
                    return;
                }
                const auto selected = std::find_if(
                    records.begin(), records.end(),
                    [&](const LostRaidRecordProjection &record)
                    {
                        return record.recordId ==
                            *selectedLostRaidRecordId_;
                    });
                if (selected == records.end())
                {
                    uiMessage_ = "SELECT A LOST RAID RECORD";
                    return;
                }
                const auto &maps = publishedContentRegistry().maps();
                const auto map = std::find_if(
                    maps.begin(), maps.end(),
                    [&](const MapDefinition &definition)
                    { return definition.id == selected->mapDefinitionId; });
                if (map == maps.end())
                {
                    uiMessage_ = "LOST RAID MAP IS UNAVAILABLE";
                    return;
                }
                selectedRaidMapIndex_ = static_cast<std::size_t>(
                    std::distance(maps.begin(), map));
                selectedRaidSelfRecoveryRecordId_ = selected->recordId;
                selectedRaidIntelligence_ = {};
                deploymentWarningArmed_ = false;
            lostRaidRecordsOpen_ = false;
            regionalOperationsOpen_ = false;
                uiMessage_ =
                    "RAID SELF-RECOVERY TARGET SET | RESERVE CARRY SPACE";
                return;
            }
            if (task.has_value() &&
                contains(recoveryTaskCancelButton(), click.position))
            {
                const std::string recordId = task->recordId;
                const RecoveryTaskReceipt receipt =
                    gameSession_.cancelRecoveryTask(
                        nextProfileTransactionId("recovery-cancel"));
                uiMessage_ = receipt.succeeded
                    ? "RECOVERY CANCELLED | NO REFUND"
                    : receipt.message;
                if (receipt.succeeded)
                {
                    selectedLostRaidRecordId_ = recordId;
                }
                return;
            }
            return;
        }
        if (contains(raidLostRecordsButton(), click.position))
        {
            lostRaidRecordsOpen_ = true;
            selectedLostRaidRecordId_.reset();
            deploymentWarningArmed_ = false;
            uiMessage_.clear();
            return;
        }
        if (contains(raidRegionalOperationsButton(), click.position))
        {
            regionalOperationsOpen_ = true;
            lostRaidRecordsOpen_ = false;
            deploymentWarningArmed_ = false;
            uiMessage_.clear();
            return;
        }
        if (contains(raidMapPreviousButton(), click.position))
        {
            cycleSelectedRaidMap(-1);
            return;
        }
        if (contains(raidMapNextButton(), click.position))
        {
            cycleSelectedRaidMap(1);
            return;
        }
        for (std::size_t index = 0;
             index < kRaidIntelligenceCategoryCount;
             ++index)
        {
            if (contains(raidIntelligenceButton(index), click.position))
            {
                handleRaidIntelligenceSelection(
                    static_cast<RaidIntelligenceCategory>(index));
                return;
            }
        }
        for (std::size_t index{}; index < selectedRaidMap().interiors.size();
             ++index)
        {
            if (contains(
                    raidIntelligenceButton(
                        kRaidIntelligenceCategoryCount + index),
                    click.position))
            {
                handleRaidInteriorIntelligencePurchase(
                    selectedRaidMap().interiors[index].id);
                return;
            }
        }
        if (screenPrimaryButtonContains(click.position.x, click.position.y))
        {
            if (!tryDeployFromBase())
            {
                if (uiMessage_.empty())
                {
                    uiMessage_ = "DEPLOYMENT IS NOT AVAILABLE";
                }
            }
            else
            {
                profileAssetSelection_.reset();
            }
        }
        return;
    }

    if (*facility == BaseFacilityKind::Allocation)
    {
        constexpr std::array<BaseSupplyCategory, 4> categories{
            BaseSupplyCategory::Food,
            BaseSupplyCategory::Medical,
            BaseSupplyCategory::Recreation,
            BaseSupplyCategory::Security};
        for (std::size_t index{}; index < categories.size(); ++index)
        {
            const SDL_FRect tab{
                650.0F + static_cast<float>(index) * 126.0F,
                218.0F,
                120.0F,
                34.0F};
            if (contains(tab, click.position))
            {
                selectedBaseSupplyCategory_ = categories[index];
                profileAssetSelection_.reset();
                uiMessage_.clear();
                return;
            }
        }

        const auto allocation = baseSupplyDefinitions(
            gameSession_.profile(), selectedBaseSupplyCategory_);
        for (std::size_t index{};
             index < allocation.size() && index < 7U;
             ++index)
        {
            const SDL_FRect row{
                650.0F,
                262.0F + static_cast<float>(index) * 40.0F,
                500.0F,
                34.0F};
            if (contains(row, click.position))
            {
                const BaseSupplyDefinitionProjection &projection =
                    allocation[index];
                if (projection.firstAssetId.has_value())
                {
                    const AssetRecord *asset = gameSession_.profile().assets.find(
                        *projection.firstAssetId);
                    profileAssetSelection_ = asset == nullptr
                        ? std::nullopt
                        : std::optional<ProfileAssetSelection>{
                              ProfileAssetSelection{
                                  asset->instanceId, 0, asset->orientation}};
                }
                else
                {
                    profileAssetSelection_.reset();
                }
                const SDL_FRect check{
                    row.x + 8.0F, row.y + 7.0F, 20.0F, 20.0F};
                if (!contains(check, click.position))
                {
                    uiMessage_ = projection.firstAssetId.has_value()
                        ? "OWNED SUPPLY ITEM SELECTED"
                        : "SUPPLY RULE HAS NO CURRENT ITEM";
                    return;
                }
                const std::optional<BaseSupplyCategory> next =
                    projection.assignedToSelectedCategory
                        ? std::nullopt
                        : std::optional<BaseSupplyCategory>{
                              selectedBaseSupplyCategory_};
                const BaseSupplyAssignmentReceipt receipt =
                    gameSession_.executeBaseSupplyAssignment(
                        projection.definition->definitionId,
                        next,
                        nextProfileTransactionId("base-supply-policy"));
                uiMessage_ = receipt.succeeded
                    ? (next.has_value()
                        ? "AUTOMATIC BASE SUPPLY ENABLED"
                        : "AUTOMATIC BASE SUPPLY DISABLED")
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
        }

        const SDL_FRect materialButton{650.0F, 604.0F, 230.0F, 42.0F};
        const SDL_FRect priorityButton{920.0F, 604.0F, 230.0F, 42.0F};
        if (!contains(materialButton, click.position) &&
            !contains(priorityButton, click.position))
        {
            return;
        }
        if (!profileAssetSelection_.has_value())
        {
            uiMessage_ = "SELECT AN OWNED SUPPLY ITEM FIRST";
            gameAudio_.play(SoundEventId::UiDeny);
            return;
        }
        const AssetInstanceId selectedId =
            profileAssetSelection_->instanceId;
        const AssetRecord *selected =
            gameSession_.profile().assets.find(selectedId);
        if (selected == nullptr ||
            !assetIsBaseAccessible(
                gameSession_.profile(), selected->instanceId))
        {
            profileAssetSelection_.reset();
            uiMessage_ = "SELECT AN OWNED SUPPLY ITEM FIRST";
            return;
        }
        if (contains(materialButton, click.position))
        {
            const ConstructionMaterialReceipt receipt =
                gameSession_.executeConstructionMaterialContribution(
                    selectedId,
                    nextProfileTransactionId(
                        "process-construction-material"));
            uiMessage_ = receipt.succeeded
                ? fmt::format(
                    "ITEM PROCESSED | CONSTRUCTION MATERIAL +{}",
                    receipt.materialUnits)
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }
        if (contains(priorityButton, click.position))
        {
            const BasePriorityReceipt receipt =
                gameSession_.executeBasePrioritySubmission(
                    selectedId,
                    nextProfileTransactionId("fulfill-base-priority"));
            uiMessage_ = receipt.succeeded
                ? "BASE WISH FULFILLED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }
        return;
    }

    if (*facility == BaseFacilityKind::Workshop)
    {
        const BaseFacilityDefinitionId workshop{
            "base_facility.workshop"};
        if (!baseFacilityInstalled(gameSession_.profile(), workshop))
        {
            uiMessage_ =
                "WORKSHOP IS IN FACILITY RESERVE | INSTALL IT ON REGIONAL PAGE";
            gameAudio_.play(SoundEventId::UiDeny);
            return;
        }
        if (contains(baseFacilityStaffingButton(), click.position))
        {
            const bool assigned = gameSession_.profile().baseWorkforce
                .workshopWorker.has_value();
            const BaseWorkforceReceipt receipt = assigned
                ? gameSession_.executeClearBaseWorker(
                    BaseFacilityStaffingKind::Workshop,
                    nextProfileTransactionId("clear-workshop-worker"))
                : gameSession_.executeAssignBestBaseWorker(
                    BaseFacilityStaffingKind::Workshop,
                    nextProfileTransactionId("assign-workshop-worker"));
            uiMessage_ = receipt.succeeded
                ? assigned ? "WORKSHOP WORKER CLEARED"
                           : "WORKSHOP WORKER ASSIGNED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        if (contains(baseFacilityUpgradeButton(), click.position))
        {
            const auto &projects =
                publishedContentRegistry().baseConstructionProjects();
            const auto found = std::find_if(
                projects.begin(), projects.end(),
                [](const BaseConstructionProjectDefinition &project)
                {
                    return project.target ==
                        BaseFacilityUpgradeTarget::Workshop;
                });
            if (found == projects.end())
            {
                uiMessage_ = "NO WORKSHOP UPGRADE PUBLISHED";
                gameAudio_.play(SoundEventId::UiDeny);
                return;
            }
            const auto &active = gameSession_.profile()
                .baseConstruction.activeProject;
            const bool matchingActive = active.has_value() &&
                active->definitionId == found->id;
            const BaseConstructionReceipt receipt = matchingActive
                ? gameSession_.executeCancelBaseConstruction(
                    found->id,
                    nextProfileTransactionId("cancel-workshop-upgrade"))
                : gameSession_.executeStartBaseConstruction(
                    found->id,
                    nextProfileTransactionId("start-workshop-upgrade"));
            uiMessage_ = receipt.succeeded
                ? matchingActive ? "WORKSHOP UPGRADE CANCELLED"
                                 : "WORKSHOP UPGRADE STARTED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        if (!contains(baseManufacturingButton(), click.position))
        {
            return;
        }
        const ProfileState &profile = gameSession_.profile();
        const auto &recipes =
            publishedContentRegistry().baseManufacturingRecipes();
        if (recipes.empty())
        {
            uiMessage_ = "NO PUBLISHED MANUFACTURING RECIPE";
            gameAudio_.play(SoundEventId::UiDeny);
            return;
        }
        BaseManufacturingReceipt receipt;
        if (!profile.baseManufacturing.activeOrder.has_value())
        {
            receipt = gameSession_.executeStartBaseManufacturing(
                recipes.front().id,
                nextProfileTransactionId("start-base-manufacturing"));
            uiMessage_ = receipt.succeeded
                ? "MANUFACTURING STARTED"
                : receipt.message;
        }
        else if (profile.baseManufacturing.activeOrder->outputReady)
        {
            receipt = gameSession_.executeCollectBaseManufacturing(
                nextProfileTransactionId("collect-base-manufacturing"));
            uiMessage_ = receipt.succeeded
                ? "MANUFACTURED ITEM COLLECTED"
                : receipt.message;
        }
        else
        {
            receipt = gameSession_.executeCancelBaseManufacturing(
                nextProfileTransactionId("cancel-base-manufacturing"));
            uiMessage_ = receipt.succeeded
                ? "MANUFACTURING CANCELLED | INPUTS RETURNED"
                : receipt.message;
        }
        gameAudio_.play(receipt.succeeded
            ? SoundEventId::UiConfirm
            : SoundEventId::UiDeny);
        return;
    }

    if (*facility == BaseFacilityKind::Medical)
    {
        if (contains(baseFacilityStaffingButton(), click.position))
        {
            const bool assigned = gameSession_.profile().baseWorkforce
                .medicalWorker.has_value();
            const BaseWorkforceReceipt receipt = assigned
                ? gameSession_.executeClearBaseWorker(
                    BaseFacilityStaffingKind::Medical,
                    nextProfileTransactionId("clear-medical-worker"))
                : gameSession_.executeAssignBestBaseWorker(
                    BaseFacilityStaffingKind::Medical,
                    nextProfileTransactionId("assign-medical-worker"));
            uiMessage_ = receipt.succeeded
                ? assigned ? "MEDICAL WORKER CLEARED"
                           : "MEDICAL WORKER ASSIGNED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        if (contains(baseFacilityUpgradeButton(), click.position))
        {
            const auto &projects =
                publishedContentRegistry().baseConstructionProjects();
            const auto found = std::find_if(
                projects.begin(), projects.end(),
                [](const BaseConstructionProjectDefinition &project)
                {
                    return project.target == BaseFacilityUpgradeTarget::Medical;
                });
            if (found == projects.end())
            {
                uiMessage_ = "NO MEDICAL UPGRADE PUBLISHED";
                gameAudio_.play(SoundEventId::UiDeny);
                return;
            }
            const auto &active = gameSession_.profile()
                .baseConstruction.activeProject;
            const bool matchingActive = active.has_value() &&
                active->definitionId == found->id;
            const BaseConstructionReceipt receipt = matchingActive
                ? gameSession_.executeCancelBaseConstruction(
                    found->id,
                    nextProfileTransactionId("cancel-medical-upgrade"))
                : gameSession_.executeStartBaseConstruction(
                    found->id,
                    nextProfileTransactionId("start-medical-upgrade"));
            uiMessage_ = receipt.succeeded
                ? matchingActive ? "MEDICAL UPGRADE CANCELLED"
                                 : "MEDICAL UPGRADE STARTED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        if (contains(baseMedicalServiceButton(), click.position))
        {
            const BaseMedicalServiceReceipt receipt =
                gameSession_.executeBasePaidMedicalService(
                    nextProfileTransactionId("base-paid-medical"));
            uiMessage_ = receipt.succeeded
                ? fmt::format(
                    "PLAYER FULLY TREATED | PAID {}",
                    receipt.currencyPaid)
                : receipt.message;
            if (!receipt.succeeded)
            {
                gameAudio_.play(SoundEventId::UiDeny);
            }
            return;
        }
        if (contains(baseResidentTreatmentButton(), click.position))
        {
            const ResidentTreatmentReceipt receipt =
                gameSession_.executeStartResidentTreatment(
                    nextProfileTransactionId("resident-treatment"));
            uiMessage_ = receipt.succeeded
                ? fmt::format(
                    "RESIDENT TREATMENT STARTED | READY IN {} MIN",
                    receipt.completionWorldMinute -
                        gameSession_.profile().worldClock.elapsedWorldMinutes)
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
        }
        return;
    }

    if (*facility == BaseFacilityKind::Dormitory)
    {
        if (contains(baseWorkforceAutoFillButton(), click.position))
        {
            const BaseWorkforceReceipt receipt =
                gameSession_.executeAutoFillBaseWorkers(
                    nextProfileTransactionId("autofill-base-workers"));
            uiMessage_ = receipt.succeeded
                ? "BASE FACILITY WORKERS AUTO-FILLED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        const auto &projects =
            publishedContentRegistry().baseConstructionProjects();
        const auto found = std::find_if(
            projects.begin(), projects.end(),
            [](const BaseConstructionProjectDefinition &candidate)
            {
                return candidate.target ==
                    BaseFacilityUpgradeTarget::Dormitory;
            });
        if (found == projects.end())
        {
            return;
        }
        const BaseConstructionProjectDefinition &project = *found;
        if (contains(baseConstructionButton(), click.position))
        {
            const auto &activeProject = gameSession_.profile()
                .baseConstruction.activeProject;
            const bool active = activeProject.has_value() &&
                activeProject->definitionId == project.id;
            if (activeProject.has_value() && !active)
            {
                uiMessage_ = "ANOTHER BASE CONSTRUCTION PROJECT IS ACTIVE";
                gameAudio_.play(SoundEventId::UiDeny);
                return;
            }
            if (!active)
            {
                const BaseConstructionPlan plan = queryStartBaseConstruction(
                    gameSession_.profile(),
                    publishedContentRegistry(),
                    StartBaseConstructionCommand{project.id});
                if (!plan.canCommit)
                {
                    uiMessage_ = plan.message;
                    gameAudio_.play(SoundEventId::UiDeny);
                    return;
                }
            }
            const BaseConstructionReceipt receipt = active
                ? gameSession_.executeCancelBaseConstruction(
                    project.id,
                    nextProfileTransactionId("cancel-base-construction"))
                : gameSession_.executeStartBaseConstruction(
                    project.id,
                    nextProfileTransactionId("start-base-construction"));
            uiMessage_ = receipt.succeeded
                ? active
                    ? "CONSTRUCTION CANCELLED | MATERIAL REFUNDED"
                    : "DORMITORY EXPANSION STARTED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        constexpr std::array<std::uint32_t, 3> restHours{1U, 6U, 12U};
        for (std::size_t index{}; index < restHours.size(); ++index)
        {
            if (!contains(baseRestButton(index), click.position))
            {
                continue;
            }
            const BaseRestReceipt receipt = gameSession_.executeBaseRest(
                restHours[index],
                nextProfileTransactionId("base-rest"));
            uiMessage_ = receipt.succeeded
                ? fmt::format(
                    "RESTED {}H | DAILY CYCLES {}",
                    restHours[index],
                    receipt.dailyCyclesResolved)
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return;
        }
        return;
    }

    if (*facility == BaseFacilityKind::Supply)
    {
        const auto &supply = fixedSupplyIds();
        for (std::size_t index = 0; index < supply.size(); ++index)
        {
            const SDL_FRect row{
                76.0F + static_cast<float>(index / 5U) * 184.0F,
                164.0F + static_cast<float>(index % 5U) * 46.0F,
                176.0F,
                40.0F};
            if (!contains(row, click.position))
            {
                continue;
            }
            const std::uint32_t quantity =
                supply[index] == alpha_content::ammunition ? 30U : 1U;
            const EconomyReceipt receipt =
                gameSession_.executeProfileEconomy(
                    PurchaseCommand{supply[index], quantity},
                    nextProfileTransactionId("purchase"));
            uiMessage_ = receipt.succeeded
                ? fmt::format("PURCHASED | CURRENCY {}", gameSession_.profile().currency)
                : receipt.message;
            gameAudio_.play(
                receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
            return;
        }

        std::size_t rowIndex{};
        for (const AssetRecord *asset : assetsInContainer(
                 gameSession_.profile(),
                 ProfileContainerId::stash()))
        {
            if (rowIndex >= 12)
            {
                break;
            }
            const SDL_FRect row{
                650.0F,
                164.0F + static_cast<float>(rowIndex) * 30.0F,
                500.0F,
                26.0F};
            if (contains(row, click.position))
            {
                profileAssetSelection_ = ProfileAssetSelection{
                    asset->instanceId,
                    0,
                    asset->orientation};
                uiMessage_ = "STASH ITEM SELECTED";
                return;
            }
            ++rowIndex;
        }

        const SDL_FRect gunsmithButton = baseGunsmithButton();
        if (contains(gunsmithButton, click.position))
        {
            if (gameSession_.profile().gunsmithMaintenanceJob.has_value())
            {
                const GunsmithCollectionReceipt receipt =
                    gameSession_.collectBaseGunsmithMaintenance(
                        nextProfileTransactionId("collect-gunsmith"));
                uiMessage_ = receipt.succeeded
                    ? "SERVICED WEAPON COLLECTED"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                if (receipt.succeeded)
                {
                    profileAssetSelection_.reset();
                }
                return;
            }
            if (!profileAssetSelection_.has_value())
            {
                uiMessage_ = "SELECT A STASH WEAPON";
                gameAudio_.play(SoundEventId::UiDeny);
                return;
            }
            const GunsmithMaintenanceReceipt receipt =
                gameSession_.executeBaseGunsmithMaintenance(
                    profileAssetSelection_->instanceId,
                    nextProfileTransactionId("start-gunsmith"));
            uiMessage_ = receipt.succeeded
                ? fmt::format(
                    "WEAPON FULLY SERVICED | PAID {}",
                    receipt.currencyPaid)
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }

        const SDL_FRect recycleButton = baseRecycleButton();
        if (contains(recycleButton, click.position) &&
            profileAssetSelection_.has_value())
        {
            const EconomyReceipt receipt = gameSession_.executeProfileEconomy(
                RecycleCommand{profileAssetSelection_->instanceId},
                nextProfileTransactionId("recycle"));
            uiMessage_ = receipt.succeeded
                ? fmt::format("RECYCLED | CURRENCY {}", gameSession_.profile().currency)
                : receipt.message;
            gameAudio_.play(
                receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }

        const SDL_FRect reliefButton = baseReliefButton();
        if (contains(reliefButton, click.position))
        {
            const std::string batchId = nextProfileTransactionId("relief-batch");
            const EconomyReceipt receipt = gameSession_.executeProfileEconomy(
                ClaimReliefCommand{batchId},
                nextProfileTransactionId("claim-relief"));
            uiMessage_ = receipt.succeeded
                ? "RELIEF BATCH ADDED TO STASH"
                : receipt.message;
            gameAudio_.play(
                receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
        }
        return;
    }

    const ProfileState &profile = gameSession_.profile();
    for (EquipmentSlotKind slot : kProfileEquipmentSlots)
    {
        if (!contains(equipmentSlotRect(slot), click.position))
        {
            continue;
        }
        if (!profileAssetSelection_.has_value())
        {
            if (const auto equipped = equippedAsset(profile, slot))
            {
                const AssetRecord *asset = profile.assets.find(*equipped);
                profileAssetSelection_ = ProfileAssetSelection{
                    *equipped, 0, asset->orientation};
            }
            return;
        }
        const InventoryReceipt receipt =
            gameSession_.executeProfileInventory(
                InventoryEquipCommand{
                    profileAssetSelection_->instanceId,
                    slot},
                nextProfileTransactionId("equip"));
        uiMessage_ = receipt.succeeded ? "EQUIPMENT UPDATED" : receipt.message;
        gameAudio_.play(
            receipt.succeeded
                ? SoundEventId::InventoryEquip
                : SoundEventId::UiDeny);
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    for (std::size_t actionIndex = 0; actionIndex < 5; ++actionIndex)
    {
        if (!contains(baseActionButton(actionIndex), click.position))
        {
            continue;
        }
        if (actionIndex == 3)
        {
            const auto weapon = firstEquippedWeapon(profile);
            if (!weapon.has_value())
            {
                uiMessage_ = "NO WEAPON EQUIPPED";
                return;
            }
            const AssetRecord *record = profile.assets.find(weapon->second);
            if (record->chamberedRound.has_value())
            {
                uiMessage_ = "WEAPON ALREADY CHAMBERED";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    ChamberWeaponCommand{weapon->second},
                    nextProfileTransactionId("base-chamber"));
            uiMessage_ = receipt.result == WeaponAmmoResult::Chambered
                ? "WEAPON CHAMBERED"
                : receipt.message.empty() ? "NO ROUND AVAILABLE" : receipt.message;
            return;
        }
        if (!profileAssetSelection_.has_value())
        {
            uiMessage_ = "SELECT AN ASSET FIRST";
            return;
        }
        const AssetRecord *selected = profile.assets.find(
            profileAssetSelection_->instanceId);
        if (selected == nullptr)
        {
            profileAssetSelection_.reset();
            return;
        }
        const AssetInstanceId selectedId = selected->instanceId;
        if (actionIndex == 0)
        {
            const ItemDefinition &definition =
                publishedContentRegistry().item(selected->definitionId);
            if (definition.category != ItemCategory::Magazine ||
                !definition.compatibleAmmunitionDefinitionId.has_value())
            {
                uiMessage_ = "SELECT A MAGAZINE TO FILL";
                return;
            }
            const AssetRecord *ammunition{};
            for (const AssetRecord *candidate : assetsInContainer(
                     profile,
                     ProfileContainerId::stash()))
            {
                if (candidate->definitionId ==
                    *definition.compatibleAmmunitionDefinitionId)
                {
                    ammunition = candidate;
                    break;
                }
            }
            if (ammunition == nullptr)
            {
                uiMessage_ = "NO COMPATIBLE STASH AMMUNITION";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    LoadMagazineCommand{
                        selected->instanceId,
                        ammunition->instanceId,
                        0},
                    nextProfileTransactionId("base-load"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE FILLED"
                : receipt.message;
            return;
        }
        if (actionIndex == 1)
        {
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    UnloadMagazineCommand{
                        selected->instanceId,
                        ProfileContainerId::stash()},
                    nextProfileTransactionId("base-unload"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE UNLOADED TO STASH"
                : receipt.message;
            return;
        }
        if (actionIndex == 2)
        {
            std::optional<AssetInstanceId> weapon;
            for (const EquipmentSlotKind slot : kWeaponEquipmentSlots)
            {
                const auto candidate = equippedAsset(profile, slot);
                if (candidate.has_value() && queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        InstallMagazineCommand{*candidate, selected->instanceId})
                        .canCommit)
                {
                    weapon = candidate;
                    break;
                }
            }
            if (!weapon.has_value())
            {
                uiMessage_ = "NO COMPATIBLE EQUIPPED WEAPON";
                return;
            }
            const WeaponAmmoReceipt receipt =
                gameSession_.executeProfileWeaponAmmo(
                    InstallMagazineCommand{
                        *weapon,
                        selected->instanceId},
                    nextProfileTransactionId("base-install"));
            uiMessage_ = receipt.succeeded
                ? "MAGAZINE INSTALLED"
                : receipt.message;
            if (receipt.succeeded)
            {
                profileAssetSelection_.reset();
            }
            return;
        }

        const HealReceipt receipt = gameSession_.executeBaseHeal(
            selectedId,
            nextProfileTransactionId("base-heal"));
        uiMessage_ = receipt.succeeded
            ? fmt::format("HEALED {} HP", receipt.healedAmount)
            : receipt.message;
        if (gameSession_.profile().assets.find(selectedId) == nullptr)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    for (const ProfileGridView &view : profileGridViews(profile, true))
    {
        InventoryGridSize size{};
        try
        {
            size = profileContainerSize(
                profile,
                publishedContentRegistry(),
                view.container);
        }
        catch (...)
        {
            continue;
        }
        const SDL_FRect bounds{
            view.x,
            view.y,
            static_cast<float>(size.width) * view.cellSize,
            static_cast<float>(size.height) * view.cellSize};
        if (!contains(bounds, click.position))
        {
            continue;
        }
        const GridPosition cell{
            static_cast<int>((click.position.x - view.x) / view.cellSize),
            static_cast<int>((click.position.y - view.y) / view.cellSize)};
        if (!profileAssetSelection_.has_value())
        {
            const auto occupant = profileAssetAtCell(
                profile,
                publishedContentRegistry(),
                view.container,
                cell);
            if (occupant.has_value())
            {
                const AssetRecord *asset = profile.assets.find(*occupant);
                std::uint32_t quantity{};
                if (click.controlPressed != click.shiftPressed)
                {
                    quantity = click.controlPressed
                        ? 1U
                        : (asset->quantity + 1U) / 2U;
                }
                profileAssetSelection_ = ProfileAssetSelection{
                    *occupant,
                    quantity,
                    asset->orientation};
                uiMessage_ = quantity == 0
                    ? "ASSET SELECTED"
                    : fmt::format("LOCKED QUANTITY {}", quantity);
            }
            return;
        }

        const InventoryReceipt receipt =
            gameSession_.executeProfileInventory(
                InventoryMoveCommand{
                    profileAssetSelection_->instanceId,
                    profileAssetSelection_->quantity,
                    StoredAssetLocation{view.container, cell},
                    profileAssetSelection_->orientation},
                nextProfileTransactionId("move"));
        uiMessage_ = receipt.succeeded ? "INVENTORY UPDATED" : receipt.message;
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    profileAssetSelection_.reset();
    uiMessage_.clear();
}

void App::handleRaidProfileClick(const BasePointerClick &click)
{
    const ProfileState &profile = gameSession_.profile();
    if (contains(baseActionButton(4), click.position))
    {
        if (!profileAssetSelection_.has_value() ||
            !gameSession_.startAlphaHeal(
                profileAssetSelection_->instanceId))
        {
            uiMessage_ = "SELECT A CARRIED MEDKIT WHILE INJURED";
            return;
        }
        profileAssetSelection_.reset();
        uiMessage_ = "MEDKIT ACTION STARTED";
        closeInventory();
        return;
    }

    for (const ProfileGridView &view : profileGridViews(profile, false))
    {
        if (view.container.kind == ProfileContainerKind::Stash)
        {
            continue;
        }
        InventoryGridSize size{};
        try
        {
            size = profileContainerSize(
                profile,
                publishedContentRegistry(),
                view.container);
        }
        catch (...)
        {
            continue;
        }
        const SDL_FRect bounds{
            view.x,
            view.y,
            static_cast<float>(size.width) * view.cellSize,
            static_cast<float>(size.height) * view.cellSize};
        if (!contains(bounds, click.position))
        {
            continue;
        }
        const GridPosition cell{
            static_cast<int>((click.position.x - view.x) / view.cellSize),
            static_cast<int>((click.position.y - view.y) / view.cellSize)};
        if (!profileAssetSelection_.has_value())
        {
            const auto occupant = profileAssetAtCell(
                profile,
                publishedContentRegistry(),
                view.container,
                cell);
            if (occupant.has_value())
            {
                const AssetRecord *asset = profile.assets.find(*occupant);
                std::uint32_t quantity{};
                if (click.controlPressed != click.shiftPressed)
                {
                    quantity = click.controlPressed
                        ? 1U
                        : (asset->quantity + 1U) / 2U;
                }
                profileAssetSelection_ = ProfileAssetSelection{
                    *occupant,
                    quantity,
                    asset->orientation};
                uiMessage_ = "CARRIED ASSET SELECTED";
            }
            return;
        }

        const InventoryReceipt receipt = gameSession_.executeProfileInventory(
            InventoryMoveCommand{
                profileAssetSelection_->instanceId,
                profileAssetSelection_->quantity,
                StoredAssetLocation{view.container, cell},
                profileAssetSelection_->orientation},
            nextProfileTransactionId("raid-move"));
        uiMessage_ = receipt.succeeded
            ? "CARRIED INVENTORY UPDATED"
            : receipt.message;
        if (receipt.succeeded)
        {
            profileAssetSelection_.reset();
        }
        return;
    }

    profileAssetSelection_.reset();
}

void App::handleProfileInventoryUiEvent(
    const InventoryUiEvent &event,
    bool inRaid)
{
    const bool includeStash = !inRaid;
    const ProfileState &profile = gameSession_.profile();

    if (std::holds_alternative<InventoryRotateEvent>(event))
    {
        if (profileInventoryInteraction_.rotatePointerItemClockwise())
        {
            uiMessage_ = "ROTATED";
        }
        return;
    }
    if (std::holds_alternative<InventoryQuickTransferEvent>(event))
    {
        if (inRaid)
        {
            return;
        }
        const auto &quick = std::get<InventoryQuickTransferEvent>(event);
        if (!quick.pointerPosition.has_value())
        {
            return;
        }
        const auto hit = profileAssetHitAt(
            profile, *quick.pointerPosition, true);
        if (!hit.has_value() || hit->asset == nullptr)
        {
            return;
        }
        const AssetRecord &asset = *hit->asset;
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset.definitionId);
        if (const auto equipmentTarget = queryProfileQuickEquipTarget(
                profile,
                publishedContentRegistry(),
                asset.instanceId))
        {
            executeProfileDrop(
                ProfileDropRequest{
                    ProfileDragSource{
                        asset.instanceId,
                        profile.revision,
                        asset.location,
                        0,
                        asset.orientation},
                    *equipmentTarget},
                false);
            return;
        }
        std::vector<ProfileContainerId> destinations;
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
            stored != nullptr && stored->container.kind == ProfileContainerKind::Stash)
        {
            for (const ProfileGridView &view : profileGridViews(profile, false))
            {
                destinations.push_back(view.container);
            }
        }
        else
        {
            destinations.push_back(ProfileContainerId::stash());
        }
        for (ProfileContainerId container : destinations)
        {
            const auto fit = findFirstProfileFit(
                profile,
                publishedContentRegistry(),
                container,
                definition,
                asset.orientation,
                asset.instanceId);
            if (!fit.has_value())
            {
                continue;
            }
            ProfileDropRequest request{
                ProfileDragSource{
                    asset.instanceId,
                    profile.revision,
                    asset.location,
                    0,
                    asset.orientation},
                StoredCellTarget{StoredAssetLocation{container, *fit}}};
            bool allowed{};
            if (std::holds_alternative<InstalledMagazineLocation>(asset.location))
            {
                const auto installed = std::get<InstalledMagazineLocation>(asset.location);
                allowed = queryWeaponAmmo(
                    profile,
                    publishedContentRegistry(),
                    UninstallMagazineCommand{
                        installed.weaponAssetId,
                        std::get<StoredCellTarget>(request.target).location,
                        asset.orientation}).canCommit;
            }
            else
            {
                allowed = queryInventory(
                    profile,
                    publishedContentRegistry(),
                    InventoryMoveCommand{
                        asset.instanceId,
                        0,
                        std::get<StoredCellTarget>(request.target).location,
                        asset.orientation}).canCommit;
            }
            if (allowed)
            {
                executeProfileDrop(request, false);
                return;
            }
        }
        uiMessage_ = "NO QUICK-TRANSFER SPACE";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }

    auto beginPress = [&](MousePosition position, bool control, bool shift)
    {
        if (control && shift)
        {
            return;
        }
        profileContextMenu_.reset();
        const auto hit = profileAssetHitAt(profile, position, includeStash);
        if (!hit.has_value() || hit->asset == nullptr)
        {
            return;
        }
        const ItemDefinition &definition =
            publishedContentRegistry().item(hit->asset->definitionId);
        const InventoryFootprint footprint = inventoryFootprint(
            definition, hit->asset->orientation);
        std::uint32_t quantity{};
        if (control != shift)
        {
            quantity = control
                ? 1U
                : (hit->asset->quantity + 1U) / 2U;
        }
        const MousePosition grabOffset{
            std::clamp(
                (position.x - hit->bounds.x) / hit->cellSize,
                0.0F,
                static_cast<float>(footprint.width) - 0.001F),
            std::clamp(
                (position.y - hit->bounds.y) / hit->cellSize,
                0.0F,
                static_cast<float>(footprint.height) - 0.001F)};
        static_cast<void>(profileInventoryInteraction_.beginPointerPress(
            ProfileDragSource{
                hit->asset->instanceId,
                profile.revision,
                hit->asset->location,
                quantity,
                hit->asset->orientation},
            hit->itemOrigin,
            hit->clickedCell,
            position,
            InventoryPointerItemGeometry{
                hit->asset->orientation,
                footprint,
                definition.canRotate,
                grabOffset}));
        if (quantity > 0)
        {
            uiMessage_ = fmt::format("LOCKED QUANTITY {}", quantity);
        }
    };

    if (const auto *partial = std::get_if<InventoryPartialTransferEvent>(&event))
    {
        beginPress(
            partial->pointerPosition,
            partial->controlPressed,
            partial->shiftPressed);
        return;
    }

    const auto &pointer = std::get<InventoryPointerEvent>(event);
    const GridPosition grabOffset =
        profileInventoryInteraction_.activeDragVisual().has_value()
            ? GridPosition{
                  static_cast<int>(std::floor(
                      profileInventoryInteraction_.activeDragVisual()
                          ->grabOffsetInCells.x)),
                  static_cast<int>(std::floor(
                      profileInventoryInteraction_.activeDragVisual()
                          ->grabOffsetInCells.y))}
            : GridPosition{};
    const auto target = profileDropTargetAt(
        profile,
        pointer.position,
        includeStash,
        grabOffset,
        profileInventoryInteraction_.source().has_value()
            ? std::optional<AssetInstanceId>{
                  profileInventoryInteraction_.source()->instanceId}
            : std::nullopt);

    switch (pointer.type)
    {
    case InventoryPointerEventType::Motion:
        profileInventoryInteraction_.updatePointerPosition(
            pointer.position, target);
        break;
    case InventoryPointerEventType::LeftButtonDown:
        if (profileContextMenu_.has_value())
        {
            if (contains(
                    profileContextActionRect(profileContextMenu_->position),
                    pointer.position))
            {
                executeProfileContextAction(inRaid);
            }
            else
            {
                profileContextMenu_.reset();
            }
            return;
        }
        beginPress(pointer.position, false, false);
        break;
    case InventoryPointerEventType::LeftButtonUp:
        if (const auto request = profileInventoryInteraction_.releasePointer(
                pointer.position, target))
        {
            executeProfileDrop(*request, inRaid);
        }
        break;
    }
}

void App::handleProfileRightClick(MousePosition position, bool inRaid)
{
    if (profileInventoryInteraction_.pointerGestureActive())
    {
        return;
    }
    const auto hit = profileAssetHitAt(
        gameSession_.profile(), position, !inRaid);
    if (!hit.has_value() || hit->asset == nullptr)
    {
        profileContextMenu_.reset();
        return;
    }
    if (!profileContextActionLabel(
            gameSession_.profile(), *hit->asset, inRaid).has_value())
    {
        profileContextMenu_.reset();
        return;
    }
    profileContextMenu_ = ProfileContextMenu{
        hit->asset->instanceId,
        position};
}

void App::executeProfileDrop(const ProfileDropRequest &request, bool inRaid)
{
    const ProfileState &profile = gameSession_.profile();
    if (!profileDragSourceMatches(profile, request.source))
    {
        uiMessage_ = "INVENTORY CHANGED - TRY AGAIN";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }

    std::visit(
        [&](const auto &target)
        {
            using Target = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<Target, StoredCellTarget>)
            {
                if (inRaid &&
                    target.location.container.kind == ProfileContainerKind::Stash)
                {
                    uiMessage_ = "BLOCKED";
                    gameAudio_.play(SoundEventId::UiDeny);
                    return;
                }
                if (std::holds_alternative<InstalledMagazineLocation>(
                        request.source.location))
                {
                    const auto installed = std::get<InstalledMagazineLocation>(
                        request.source.location);
                    const WeaponAmmoReceipt receipt =
                        gameSession_.executeProfileWeaponAmmo(
                            UninstallMagazineCommand{
                                installed.weaponAssetId,
                                target.location,
                                request.source.orientation},
                            nextProfileTransactionId(
                                inRaid ? "raid-uninstall" : "base-uninstall"));
                    uiMessage_ = receipt.succeeded
                        ? "MAGAZINE UNINSTALLED"
                        : receipt.message;
                    return;
                }
                const InventoryReceipt receipt =
                    gameSession_.executeProfileInventory(
                        InventoryMoveCommand{
                            request.source.instanceId,
                            request.source.quantity,
                            target.location,
                            request.source.orientation},
                        nextProfileTransactionId(inRaid ? "raid-move" : "base-move"));
                uiMessage_ = receipt.succeeded
                    ? "INVENTORY UPDATED"
                    : receipt.message;
                gameAudio_.play(
                    receipt.succeeded
                        ? SoundEventId::InventoryMoveOrPlace
                        : SoundEventId::UiDeny);
            }
            else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
            {
                const InventoryReceipt receipt =
                    gameSession_.executeProfileInventory(
                        InventoryEquipCommand{
                            request.source.instanceId,
                            target.slot},
                        nextProfileTransactionId(inRaid ? "raid-equip" : "base-equip"));
                uiMessage_ = receipt.succeeded
                    ? "EQUIPMENT UPDATED"
                    : receipt.message;
                gameAudio_.play(
                    receipt.succeeded
                        ? SoundEventId::InventoryEquip
                        : SoundEventId::UiDeny);
            }
            else if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
            {
                if (inRaid)
                {
                    if (gameSession_.startAlphaLoadMagazine(
                            request.source.instanceId,
                            target.magazineAssetId,
                            request.source.quantity))
                    {
                        uiMessage_ = "MAGAZINE PACKING STARTED";
                        closeInventory();
                    }
                    else
                    {
                        uiMessage_ = "AMMUNITION CANNOT BE LOADED";
                        gameAudio_.play(SoundEventId::UiDeny);
                    }
                    return;
                }
                const WeaponAmmoReceipt receipt =
                    gameSession_.executeProfileWeaponAmmo(
                        LoadMagazineCommand{
                            target.magazineAssetId,
                            request.source.instanceId,
                            request.source.quantity},
                        nextProfileTransactionId("base-load"));
                uiMessage_ = receipt.succeeded
                    ? "MAGAZINE LOADED"
                    : receipt.message;
            }
            else if constexpr (std::is_same_v<Target, WeaponInstallTarget>)
            {
                if (inRaid)
                {
                    if (gameSession_.startAlphaReload(
                            target.weaponAssetId,
                            request.source.instanceId))
                    {
                        uiMessage_ = "RELOAD STARTED (2 SEC)";
                        closeInventory();
                    }
                    else
                    {
                        uiMessage_ = "MAGAZINE CANNOT BE INSTALLED";
                        gameAudio_.play(SoundEventId::UiDeny);
                    }
                    return;
                }
                const WeaponAmmoReceipt receipt =
                    gameSession_.executeProfileWeaponAmmo(
                        InstallMagazineAndChamberCommand{
                            target.weaponAssetId,
                            request.source.instanceId},
                        nextProfileTransactionId("base-install"));
                uiMessage_ = receipt.succeeded
                    ? "MAGAZINE INSTALLED"
                    : receipt.message;
            }
            else if constexpr (
                std::is_same_v<Target, WeaponMaintenanceTarget>)
            {
                if (inRaid)
                {
                    if (gameSession_.startAlphaWeaponMaintenance(
                            request.source.instanceId,
                            target.weaponAssetId))
                    {
                        uiMessage_ = "WEAPON MAINTENANCE STARTED (8 SEC)";
                        closeInventory();
                    }
                    else
                    {
                        uiMessage_ = "WEAPON CANNOT BE MAINTAINED";
                    }
                    return;
                }
                const WeaponMaintenanceReceipt receipt =
                    gameSession_.executeBaseWeaponMaintenance(
                        request.source.instanceId,
                        target.weaponAssetId,
                        nextProfileTransactionId("base-weapon-maintenance"));
                uiMessage_ = receipt.succeeded
                    ? fmt::format(
                          "WEAPON RESTORED {:.2f}",
                          static_cast<float>(
                              receipt.restoredDurabilityCenti) / 100.0F)
                    : receipt.message;
            }
            else
            {
                if (inRaid)
                {
                    if (gameSession_.startAlphaArmorMaintenance(
                            request.source.instanceId,
                            target.armorAssetId))
                    {
                        uiMessage_ = "ARMOR REPAIR STARTED (6 SEC)";
                        closeInventory();
                    }
                    else
                    {
                        uiMessage_ = "ARMOR CANNOT BE REPAIRED";
                    }
                    return;
                }
                const ArmorMaintenanceReceipt receipt =
                    gameSession_.executeBaseArmorMaintenance(
                        request.source.instanceId,
                        target.armorAssetId,
                        nextProfileTransactionId("base-armor-maintenance"));
                uiMessage_ = receipt.succeeded
                    ? fmt::format(
                          "ARMOR RESTORED {} | MAX {}",
                          receipt.restoredDurability,
                          receipt.currentMaximumAfter)
                    : receipt.message;
            }
        },
        request.target);
}

void App::executeProfileContextAction(bool inRaid)
{
    if (!profileContextMenu_.has_value())
    {
        return;
    }
    const AssetInstanceId id = profileContextMenu_->instanceId;
    profileContextMenu_.reset();
    const AssetRecord *asset = gameSession_.profile().assets.find(id);
    if (asset == nullptr)
    {
        uiMessage_ = "ASSET NO LONGER EXISTS";
        return;
    }
    const ItemCategory category = publishedContentRegistry()
        .item(asset->definitionId).category;
    if (inRaid)
    {
        if (category == ItemCategory::Magazine)
        {
            if (gameSession_.startAlphaUnloadMagazine(id))
            {
                uiMessage_ = "MAGAZINE UNLOAD STARTED (3 SEC)";
                closeInventory();
            }
            else
            {
                uiMessage_ = "MAGAZINE EMPTY OR NO CARRIED SPACE";
            }
        }
        else if (category == ItemCategory::Medical &&
            gameSession_.startAlphaMedical(id))
        {
            const MedicalUsePlan plan = queryMedicalUse(
                gameSession_.profile(),
                publishedContentRegistry(),
                id,
                MedicalAccess::CarriedOnly);
            uiMessage_ = fmt::format(
                "MEDICAL ACTION STARTED ({:.1f} SEC)",
                static_cast<float>(plan.durationMs) / 1000.0F);
            closeInventory();
        }
        else
        {
            uiMessage_ = "MEDICAL ITEM CANNOT BE USED";
        }
        return;
    }
    if (category == ItemCategory::Magazine)
    {
        const WeaponAmmoReceipt receipt = gameSession_.executeProfileWeaponAmmo(
            UnloadMagazineCommand{id, ProfileContainerId::stash()},
            nextProfileTransactionId("base-unload"));
        uiMessage_ = receipt.succeeded ? "MAGAZINE UNLOADED" : receipt.message;
        return;
    }
    if (category == ItemCategory::Medical)
    {
        const MedicalUseReceipt receipt = gameSession_.executeBaseMedical(
            id, nextProfileTransactionId("base-medical"));
        uiMessage_ = receipt.succeeded
            ? receipt.healedAmount > 0
                ? fmt::format("HEALED {} HP", receipt.healedAmount)
                : "MEDICAL EFFECT APPLIED"
            : receipt.message;
        return;
    }
    if (category == ItemCategory::Weapon)
    {
        const WeaponAmmoReceipt receipt = gameSession_.executeProfileWeaponAmmo(
            ChamberWeaponCommand{id},
            nextProfileTransactionId("base-chamber"));
        uiMessage_ = receipt.succeeded ? "WEAPON CHAMBERED" : receipt.message;
    }
}

InventoryGridLayout
App::inventoryGridLayout(
    InventoryContainerId container) const
{
    const GridInventory &inventory =
        inventoryFor(container);

    const GridInventory &playerInventory =
        gameSession_.world().inventory();
    const GridInventory &externalInventory =
        gameSession_.world().containerInventory();

    const float playerPanelWidth =
        static_cast<float>(playerInventory.width()) *
            kInventoryCellSize +
        kInventoryPanelPadding * 2.0F;

    const float externalPanelWidth =
        static_cast<float>(externalInventory.width()) *
            kInventoryCellSize +
        kInventoryPanelPadding * 2.0F;

    const float totalWidth =
        inventoryOverlayState_.showsExternalContainer()
            ? playerPanelWidth +
                  kInventoryPanelsGap +
                  externalPanelWidth
            : playerPanelWidth;

    const float usableWidth =
        static_cast<float>(kWindowWidth) -
        kInventoryDropWidth;

    const float firstPanelX =
        (usableWidth - totalWidth) /
        2.0F;

    const float panelX =
        container == InventoryContainerId::Player
            ? firstPanelX
            : firstPanelX +
                  playerPanelWidth +
                  kInventoryPanelsGap;

    return InventoryGridLayout{
        panelX + kInventoryPanelPadding,
        kInventoryPanelY +
            kInventoryPanelPadding +
            kInventoryHeaderHeight,
        kInventoryCellSize,
        InventoryGridSize{
            inventory.width(),
            inventory.height()}};
}

std::optional<InventoryGridLocation>
App::inventoryLocationAt(
    MousePosition position) const
{
    const std::optional<GridPosition> playerCell =
        inventoryGridLayout(InventoryContainerId::Player)
            .screenToGrid(position);

    if (playerCell.has_value())
    {
        return InventoryGridLocation{
            InventoryContainerId::Player,
            *playerCell};
    }

    if (!inventoryOverlayState_.showsExternalContainer())
    {
        return std::nullopt;
    }

    const std::optional<GridPosition> externalCell =
        inventoryGridLayout(InventoryContainerId::External)
            .screenToGrid(position);

    if (externalCell.has_value())
    {
        return InventoryGridLocation{
            InventoryContainerId::External,
            *externalCell};
    }

    return std::nullopt;
}

GridInventory &App::inventoryFor(
    InventoryContainerId container) noexcept
{
    return container == InventoryContainerId::Player
        ? gameSession_.world().inventory()
        : gameSession_.world().containerInventory();
}

const GridInventory &App::inventoryFor(
    InventoryContainerId container) const noexcept
{
    return container == InventoryContainerId::Player
        ? gameSession_.world().inventory()
        : gameSession_.world().containerInventory();
}

SDL_FRect App::inventoryDropZone() const noexcept
{
    const InventoryScreenRect zone =
        makeRightEdgeInventoryDropZone(
            static_cast<float>(kWindowWidth),
            static_cast<float>(kWindowHeight),
            kInventoryDropWidth);

    return SDL_FRect{
        zone.x,
        zone.y,
        zone.width,
        zone.height};
}

bool App::inventoryDropZoneContains(
    MousePosition position) const noexcept
{
    return makeRightEdgeInventoryDropZone(
               static_cast<float>(kWindowWidth),
               static_cast<float>(kWindowHeight),
               kInventoryDropWidth)
        .contains(position);
}

void App::handleInventoryPointerEvent(
    const InventoryPointerEvent &event)
{
    const MousePosition position = event.position;
    const std::optional<InventoryGridLocation> location =
        inventoryLocationAt(position);
    const bool overDropZone =
        inventoryDropZoneContains(position);

    if (event.type == InventoryPointerEventType::Motion)
    {
        inventoryInteraction_.updatePointerPosition(
            position,
            location,
            overDropZone);
        return;
    }

    if (event.type == InventoryPointerEventType::LeftButtonDown)
    {
        inventoryInteraction_.updatePointerPosition(
            position,
            location,
            overDropZone);

        if (!location.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const GridInventory &inventory =
            inventoryFor(location->container);
        const std::optional<ItemInstanceId> instanceId =
            inventory.occupantAt(location->cell);

        if (!instanceId.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const std::optional<GridPosition> itemOrigin =
            inventory.originOf(*instanceId);

        if (!itemOrigin.has_value())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const auto &placedItems = inventory.placedItems();
        const auto placedIt = std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() == *instanceId;
            });

        if (placedIt == placedItems.end())
        {
            inventoryInteraction_.clearSelection();
            return;
        }

        const ItemDefinition &definition =
            itemDefinition(
                placedIt->item.definitionId());
        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placedIt->item.orientation());
        const InventoryGridLayout layout =
            inventoryGridLayout(location->container);
        const float cellSize = layout.cellSize();
        const MousePosition grabOffsetInCells{
            (position.x -
             (layout.gridX() +
              static_cast<float>(itemOrigin->x) * cellSize)) /
                cellSize,
            (position.y -
             (layout.gridY() +
              static_cast<float>(itemOrigin->y) * cellSize)) /
                cellSize};

        static_cast<void>(
            inventoryInteraction_.beginPointerPress(
                InventoryItemSelection{
                    location->container,
                    *instanceId},
                *itemOrigin,
                location->cell,
                position,
                InventoryPointerItemGeometry{
                    placedIt->item.orientation(),
                    footprint,
                    definition.canRotate,
                    grabOffsetInCells}));
        return;
    }

    const std::optional<InventoryPointerRequest> request =
        inventoryInteraction_.releasePointer(
            position,
            location,
            overDropZone);

    if (!request.has_value())
    {
        return;
    }

    if (const auto *placement =
            std::get_if<InventoryPlacementRequest>(&*request))
    {
        GridInventory &source =
            inventoryFor(placement->source.container);
        GridInventory &destination =
            inventoryFor(placement->destination.container);

        bool succeeded{};

        if (placement->selectedQuantity.has_value())
        {
            succeeded = gameSession_.world().placeInventoryItemQuantity(
                placement->source.container ==
                    InventoryContainerId::Player,
                placement->destination.container ==
                    InventoryContainerId::Player,
                placement->source.instanceId,
                *placement->selectedQuantity,
                placement->destination.cell,
                placement->orientation);
        }
        else
        {
            succeeded = tryPlaceWholeItemAt(
                source,
                destination,
                placement->source.instanceId,
                placement->destination.cell,
                placement->orientation);
        }

        if (succeeded &&
            placement->source.container !=
                placement->destination.container)
        {
            inventoryInteraction_.clearSelection();
        }

        return;
    }

    const InventoryDropRequest &drop =
        std::get<InventoryDropRequest>(*request);

    if (drop.source.container ==
            InventoryContainerId::Player &&
        (drop.selectedQuantity.has_value()
             ? gameSession_.world().dropInventoryItemQuantity(
                   drop.source.instanceId,
                   *drop.selectedQuantity,
                   drop.orientation)
             : gameSession_.world().dropInventoryItem(
                   drop.source.instanceId,
                   drop.orientation)))
    {
        inventoryInteraction_.clearSelection();
    }
}

void App::handleInventoryRotateEvent() noexcept
{
    static_cast<void>(
        inventoryInteraction_.rotatePointerItemClockwise());
}

void App::handleInventoryQuickTransferEvent(
    const InventoryQuickTransferEvent &event)
{
    if (event.pointerPosition.has_value())
    {
        const MousePosition position =
            *event.pointerPosition;

        inventoryInteraction_.updatePointerPosition(
            position,
            inventoryLocationAt(position),
            inventoryDropZoneContains(position));
    }

    const std::optional<InventoryQuickTransferRequest> request =
        decideInventoryQuickTransfer(
            inventoryOverlayState_.mode(),
            inventoryInteraction_.pointerPhase(),
            inventoryInteraction_.hoveredLocation());

    if (!request.has_value())
    {
        return;
    }

    GridInventory &source =
        inventoryFor(request->source.container);

    const InventoryContainerId destinationContainer =
        request->source.container == InventoryContainerId::Player
            ? InventoryContainerId::External
            : InventoryContainerId::Player;

    GridInventory &destination =
        inventoryFor(destinationContainer);

    if (tryTransferItemAtCellFirstFit(
            source,
            destination,
            request->source.cell))
    {
        inventoryInteraction_.clearSelection();
    }
}

void App::handleInventoryPartialTransferEvent(
    const InventoryPartialTransferEvent &event)
{
    const std::optional<InventoryPartialTransferMode> mode =
        decideInventoryPartialTransferMode(
            event.controlPressed,
            event.shiftPressed);

    if (!mode.has_value())
    {
        return;
    }

    if (inventoryInteraction_.pointerPhase() !=
        InventoryPointerPhase::Idle)
    {
        return;
    }

    const std::optional<InventoryGridLocation> location =
        inventoryLocationAt(event.pointerPosition);
    inventoryInteraction_.updatePointerPosition(
        event.pointerPosition,
        location,
        inventoryDropZoneContains(event.pointerPosition));

    if (!location.has_value())
    {
        return;
    }

    GridInventory &source =
        inventoryFor(location->container);
    const std::optional<ItemInstanceId> instanceId =
        source.occupantAt(location->cell);

    if (!instanceId.has_value())
    {
        return;
    }

    const std::optional<std::uint32_t> availableQuantity =
        source.quantityOf(*instanceId);

    if (!availableQuantity.has_value())
    {
        return;
    }

    const std::uint32_t requestedQuantity =
        inventoryPartialTransferQuantity(
            *mode,
            *availableQuantity);

    const auto &placedItems = source.placedItems();
    const auto placedIt = std::find_if(
        placedItems.begin(),
        placedItems.end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() == *instanceId;
        });

    if (placedIt == placedItems.end())
    {
        return;
    }

    const ItemDefinition &definition =
        itemDefinition(placedIt->item.definitionId());
    if (definition.maxStackSize <= 1)
    {
        return;
    }

    const std::optional<GridPosition> itemOrigin =
        source.originOf(*instanceId);
    if (!itemOrigin.has_value())
    {
        return;
    }

    const InventoryGridLayout layout =
        inventoryGridLayout(location->container);
    const float cellSize = layout.cellSize();
    const InventoryFootprint footprint =
        inventoryFootprint(
            definition,
            placedIt->item.orientation());
    const MousePosition grabOffsetInCells{
        (event.pointerPosition.x -
         (layout.gridX() +
          static_cast<float>(itemOrigin->x) * cellSize)) /
            cellSize,
        (event.pointerPosition.y -
         (layout.gridY() +
          static_cast<float>(itemOrigin->y) * cellSize)) /
            cellSize};

    static_cast<void>(
        inventoryInteraction_.beginQuantityPointerDrag(
            InventoryItemSelection{
                location->container,
                *instanceId},
            *itemOrigin,
            location->cell,
            event.pointerPosition,
            InventoryPointerItemGeometry{
                placedIt->item.orientation(),
                footprint,
                definition.canRotate,
                grabOffsetInCells},
            requestedQuantity));
}

// Process SDL events, set running_ to false if quit event is received
void App::processEvents()
{
    pendingInventoryUiEvents_.clear();
    pendingProfileRightClicks_.clear();
    pendingBaseClicks_.clear();
    pendingBaseRotate_ = false;
    pendingMainMenuCommand_.reset();
    pendingPauseMenuCommand_.reset();
    pendingScreenConfirm_ = false;
    developerWeaponPanelBlocksGameplayThisFrame_ =
        developerWeaponPanelOpen_;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        input_.handleEvent(event);

        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        {
            windowHasInputFocus_ = true;
        }
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
            event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                windowHasInputFocus_ = false;
                static_cast<void>(
                    SDL_SetWindowRelativeMouseMode(window_, false));
                relativeMouseModeActive_ = false;
                pendingRelativeAimMotion_ = Vec2{};
                static_cast<void>(SDL_ShowCursor());
                systemCursorHidden_ = false;
            }
            inventoryInteraction_.cancelPointerGesture();
            profileInventoryInteraction_.cancelPointerGesture();
            profileContextMenu_.reset();
            medicalWheelOpen_ = false;
            medicalWheelOptions_.clear();
            developerWeaponPanelOpen_ = false;
            developerWeaponPanelBlocksGameplayThisFrame_ = true;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
                (gameFlow_.state() == GameFlowState::Base ||
                 gameFlow_.state() == GameFlowState::Raid))
            {
                pauseMenu_.open();
            }
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            Vec2 motion{event.motion.xrel, event.motion.yrel};
            if (relativeMouseModeActive_)
            {
                pendingRelativeAimMotion_.x += motion.x;
                pendingRelativeAimMotion_.y += motion.y;
            }
            else
            {
                const std::optional<Vec2> previousPointer =
                    pointerWorldPosition_;
                pointerWorldPosition_ = Vec2{
                    event.motion.x,
                    event.motion.y};
                if (previousPointer.has_value())
                {
                    motion = Vec2{
                        pointerWorldPosition_->x - previousPointer->x,
                        pointerWorldPosition_->y - previousPointer->y};
                }
            }
            if (gameFlow_.state() == GameFlowState::Raid &&
                !inventoryOverlayState_.isOpen() &&
                !pauseMenu_.isOpen() &&
                !developerWeaponPanelOpen_ &&
                gameSession_.observeAlphaWeaponClearMotion(motion))
            {
                uiMessage_ = "WEAPON MALFUNCTION CLEARED";
            }
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            if (!relativeMouseModeActive_)
            {
                pointerWorldPosition_ = Vec2{
                    event.button.x,
                    event.button.y};
            }
        }

        if (event.type == SDL_EVENT_QUIT)
        {
            if (gameFlow_.state() != GameFlowState::Base ||
                gameSession_.checkpointWorldClock())
            {
                running_ = false;
            }
            else
            {
                uiMessage_ = "WORLD CLOCK SAVE FAILED: " +
                    gameSession_.persistenceMessage();
            }
        }

        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_F9 &&
            (gameFlow_.state() == GameFlowState::Base ||
             gameFlow_.state() == GameFlowState::Raid))
        {
            developerPerformanceOverlayOpen_ =
                !developerPerformanceOverlayOpen_;
            performanceOverlayLines_ = {};
            uiMessage_ = developerPerformanceOverlayOpen_
                ? "DEVELOPER PERFORMANCE OVERLAY OPEN"
                : "DEVELOPER PERFORMANCE OVERLAY CLOSED";
            continue;
        }

        if (gameFlow_.state() == GameFlowState::Raid &&
            gameSession_.alphaRaidActive() && !pauseMenu_.isOpen())
        {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.scancode == SDL_SCANCODE_F10)
            {
                developerWeaponPanelOpen_ = !developerWeaponPanelOpen_;
                developerWeaponPanelBlocksGameplayThisFrame_ = true;
                input_.suppressPrimaryPointerUntilRelease();
                if (developerWeaponPanelOpen_)
                {
                    closeInventory();
                    medicalWheelOpen_ = false;
                    medicalWheelOptions_.clear();
                    tacticalMapOpen_ = false;
                    profileContextMenu_.reset();
                    uiMessage_ = "DEVELOPER RUNTIME PANEL OPEN";
                }
                else
                {
                    uiMessage_ = "DEVELOPER RUNTIME PANEL CLOSED";
                }
                continue;
            }

            if (developerWeaponPanelOpen_)
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT)
                {
                    input_.suppressPrimaryPointerUntilRelease();
                    handleDeveloperPanelClick(
                        MousePosition{event.button.x, event.button.y});
                    developerWeaponPanelBlocksGameplayThisFrame_ = true;
                    continue;
                }
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
                {
                    constexpr std::size_t parameterCount{
                        static_cast<std::size_t>(
                            DeveloperWeaponParameter::Count)};
                    switch (event.key.scancode)
                    {
                    case SDL_SCANCODE_UP:
                        developerWeaponParameterIndex_ =
                            (developerWeaponParameterIndex_ +
                             parameterCount - 1U) %
                            parameterCount;
                        break;
                    case SDL_SCANCODE_DOWN:
                        developerWeaponParameterIndex_ =
                            (developerWeaponParameterIndex_ + 1U) %
                            parameterCount;
                        break;
                    case SDL_SCANCODE_LEFT:
                    case SDL_SCANCODE_RIGHT:
                    {
                        const int direction =
                            event.key.scancode == SDL_SCANCODE_RIGHT ? 1 : -1;
                        if (gameSession_.adjustDeveloperWeaponTuning(
                                static_cast<DeveloperWeaponParameter>(
                                    developerWeaponParameterIndex_),
                                direction,
                                input_.isShiftPressed()))
                        {
                            uiMessage_ = "RUNTIME WEAPON TUNING UPDATED";
                        }
                        break;
                    }
                    case SDL_SCANCODE_R:
                        uiMessage_ = gameSession_.resetDeveloperWeaponTuning()
                            ? "RUNTIME WEAPON TUNING RESET"
                            : "WEAPON ALREADY USES CONTENT DEFAULTS";
                        break;
                    case SDL_SCANCODE_ESCAPE:
                        developerWeaponPanelOpen_ = false;
                        uiMessage_ = "DEVELOPER RUNTIME PANEL CLOSED";
                        break;
                    default:
                        break;
                    }
                }
                developerWeaponPanelBlocksGameplayThisFrame_ = true;
                continue;
            }
        }

        if ((gameFlow_.state() == GameFlowState::Base ||
             gameFlow_.state() == GameFlowState::Raid) &&
            pauseMenu_.isOpen())
        {
            if (pauseMenu_.settingsOpen() &&
                event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.scancode == SDL_SCANCODE_L)
            {
                pendingPauseMenuCommand_ = PauseMenuCommand::ToggleLanguage;
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                pendingPauseMenuCommand_ = pauseMenuCommandAt(
                    event.button.x, event.button.y);
            }
            continue;
        }

        if (gameFlow_.state() == GameFlowState::MainMenu)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                pendingMainMenuCommand_ = mainMenuCommandAt(
                    event.button.x,
                    event.button.y);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                if (settingsOpen_ && event.key.scancode == SDL_SCANCODE_L)
                {
                    pendingMainMenuCommand_ = MainMenuCommand::ToggleLanguage;
                    continue;
                }
                switch (event.key.scancode)
                {
                case SDL_SCANCODE_C:
                    pendingMainMenuCommand_ = MainMenuCommand::Continue;
                    break;
                case SDL_SCANCODE_N:
                    pendingMainMenuCommand_ = MainMenuCommand::NewGame;
                    break;
                case SDL_SCANCODE_S:
                    pendingMainMenuCommand_ = MainMenuCommand::Settings;
                    break;
                case SDL_SCANCODE_Q:
                    pendingMainMenuCommand_ = MainMenuCommand::Exit;
                    break;
                default:
                    break;
                }
            }
        }

        else if (gameFlow_.state() == GameFlowState::Base)
        {
            if (inventoryOverlayState_.isOpen())
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_RIGHT &&
                    !input_.isControlPressed())
                {
                    pendingProfileRightClicks_.push_back(
                        MousePosition{event.button.x, event.button.y});
                }
                else if (const auto uiEvent = toInventoryUiEvent(
                             event,
                             input_.isControlPressed(),
                             input_.isShiftPressed()))
                {
                    pendingInventoryUiEvents_.push_back(*uiEvent);
                }
            }
            else if (gameFlow_.activeBaseFacility().has_value() &&
                event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                pendingBaseClicks_.push_back(BasePointerClick{
                    MousePosition{event.button.x, event.button.y},
                    input_.isControlPressed(),
                    input_.isShiftPressed()});
            }
            else if (gameFlow_.activeBaseFacility().has_value() &&
                     event.type == SDL_EVENT_KEY_DOWN &&
                     event.key.scancode == SDL_SCANCODE_R &&
                     !event.key.repeat)
            {
                pendingBaseRotate_ = true;
            }
        }

        else if (gameFlow_.state() == GameFlowState::RaidResult)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
                if (screenPrimaryButtonContains(event.button.x, event.button.y))
                {
                    pendingScreenConfirm_ = true;
                }
            }
        }

        else if (inventoryOverlayState_.isOpen())
        {
            if (gameSession_.world().isAlphaRaidWorld())
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_RIGHT &&
                    !input_.isControlPressed())
                {
                    pendingProfileRightClicks_.push_back(
                        MousePosition{event.button.x, event.button.y});
                }
                else if (const auto uiEvent = toInventoryUiEvent(
                             event,
                             input_.isControlPressed(),
                             input_.isShiftPressed()))
                {
                    pendingInventoryUiEvents_.push_back(*uiEvent);
                }
                continue;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                event.button.button == SDL_BUTTON_LEFT)
            {
                input_.suppressPrimaryPointerUntilRelease();
            }

            const std::optional<InventoryUiEvent> uiEvent =
                toInventoryUiEvent(
                    event,
                    input_.isControlPressed(),
                    input_.isShiftPressed());

            if (uiEvent.has_value())
            {
                pendingInventoryUiEvents_.push_back(
                    *uiEvent);
            }
        }
    }
}

void App::handleDeveloperPanelClick(MousePosition position)
{
    constexpr std::size_t parameterCount{
        static_cast<std::size_t>(DeveloperWeaponParameter::Count)};
    const std::optional<DeveloperPanelAction> action =
        developerPanelActionAt(
            DeveloperPanelPoint{position.x, position.y},
            parameterCount);
    if (!action.has_value())
    {
        return;
    }

    switch (action->kind)
    {
    case DeveloperPanelActionKind::ToggleMapFog:
        developerMapFogEnabled_ = !developerMapFogEnabled_;
        uiMessage_ = developerMapFogEnabled_
            ? "MAP FOG OF WAR ENABLED"
            : "MAP FOG OF WAR DISABLED - FULL STATIC MAP";
        break;
    case DeveloperPanelActionKind::ToggleInfiniteAmmo:
        developerInfiniteAmmoEnabled_ = !developerInfiniteAmmoEnabled_;
        uiMessage_ = developerInfiniteAmmoEnabled_
            ? "DEVELOPER INFINITE AMMO ENABLED"
            : "DEVELOPER INFINITE AMMO DISABLED";
        break;
    case DeveloperPanelActionKind::ResetWeaponTuning:
        uiMessage_ = gameSession_.resetDeveloperWeaponTuning()
            ? "RUNTIME WEAPON TUNING RESET"
            : "WEAPON ALREADY USES CONTENT DEFAULTS";
        break;
    case DeveloperPanelActionKind::SelectWeaponParameter:
        developerWeaponParameterIndex_ = action->parameterIndex;
        break;
    case DeveloperPanelActionKind::DecreaseWeaponParameter:
    case DeveloperPanelActionKind::IncreaseWeaponParameter:
        developerWeaponParameterIndex_ = action->parameterIndex;
        if (gameSession_.adjustDeveloperWeaponTuning(
                static_cast<DeveloperWeaponParameter>(
                    action->parameterIndex),
                action->kind ==
                        DeveloperPanelActionKind::IncreaseWeaponParameter
                    ? 1
                    : -1,
                false))
        {
            uiMessage_ = "RUNTIME WEAPON TUNING UPDATED";
        }
        break;
    }
}

void App::update(float deltaTime)
{
    syncAmbience();
    consumePresentationAudioEvents();
    if (gameFlow_.state() != GameFlowState::Raid)
    {
        developerWeaponPanelOpen_ = false;
        developerWeaponPanelBlocksGameplayThisFrame_ = false;
        tacticalMapOpen_ = false;
    }
    const bool escapePressed = input_.wasActionJustPressed(
        GameAction::InventoryCancel);
    const bool pauseEligible =
        gameFlow_.state() == GameFlowState::Base ||
        gameFlow_.state() == GameFlowState::Raid;
    if (pauseMenu_.isOpen())
    {
        if (escapePressed)
        {
            static_cast<void>(pauseMenu_.handleEscape());
        }
        else if (pendingPauseMenuCommand_.has_value())
        {
            handlePauseMenuCommand(*pendingPauseMenuCommand_);
        }
        pendingPauseMenuCommand_.reset();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    const bool existingModalHandlesEscape =
        inventoryOverlayState_.isOpen() ||
        medicalWheelOpen_ ||
        tacticalMapOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ ||
        (gameFlow_.state() == GameFlowState::Base &&
         gameFlow_.activeBaseFacility().has_value());
    if (pauseEligible && escapePressed && !existingModalHandlesEscape)
    {
        pauseMenu_.open();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    const bool screenConfirm =
        pendingScreenConfirm_ ||
        input_.wasActionJustPressed(
            GameAction::ScreenConfirm);
    pendingScreenConfirm_ = false;

    if (gameFlow_.state() == GameFlowState::MainMenu)
    {
        pendingInventoryUiEvents_.clear();
        if (settingsOpen_ && escapePressed)
        {
            settingsOpen_ = false;
            return;
        }
        if (pendingMainMenuCommand_.has_value())
        {
            handleMainMenuCommand(*pendingMainMenuCommand_);
        }
        else if (screenConfirm)
        {
            static_cast<void>(
                handleScreenConfirm());
        }
        return;
    }

    if (gameFlow_.state() == GameFlowState::Base)
    {
        if (screenConfirm &&
            gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate)
        {
            static_cast<void>(handleScreenConfirm());
            return;
        }
        updateBase(deltaTime);
        return;
    }

    if (gameFlow_.state() == GameFlowState::RaidResult)
    {
        pendingInventoryUiEvents_.clear();
        if (screenConfirm)
        {
            static_cast<void>(handleScreenConfirm());
        }
        return;
    }

    const bool tacticalToggle = input_.wasActionJustPressed(
        GameAction::ToggleTacticalMap);
    if (tacticalMapOpen_ && (tacticalToggle || escapePressed))
    {
        tacticalMapOpen_ = false;
        uiMessage_ = "TACTICAL MAP CLOSED";
    }
    else if (tacticalToggle &&
             gameSession_.world().raidSession().isActive() &&
             !inventoryOverlayState_.isOpen() &&
             !medicalWheelOpen_ &&
             !developerWeaponPanelOpen_)
    {
        tacticalMapOpen_ = true;
        uiMessage_ = "TACTICAL MAP OPEN - WORLD CONTINUES";
        input_.suppressPrimaryPointerUntilRelease();
    }

    const bool raidAcceptsInventoryInput =
        gameSession_.world().raidSession().isActive();

    const InventoryFrameInputDecision inputDecision =
        decideInventoryFrameInput(
            inventoryOverlayState_.isOpen(),
            !developerWeaponPanelBlocksGameplayThisFrame_ &&
            !tacticalMapOpen_ &&
            raidAcceptsInventoryInput &&
                input_.wasActionJustPressed(
                    GameAction::ToggleInventory),
            !developerWeaponPanelBlocksGameplayThisFrame_ &&
            input_.wasActionJustPressed(
                GameAction::InventoryCancel));

    switch (inputDecision.controlAction)
    {
    case InventoryFrameControlAction::OpenInventory:
        inventoryOverlayState_.openPlayerInventory();
        break;

    case InventoryFrameControlAction::CloseInventory:
        closeInventory();
        break;

    case InventoryFrameControlAction::CancelInteraction:
        handleInventoryCancel();
        break;

    case InventoryFrameControlAction::None:
        break;
    }

    if (inputDecision.controlAction !=
        InventoryFrameControlAction::None)
    {
        input_.suppressPrimaryPointerUntilRelease();
    }

    const bool alphaRaidInventory =
        inventoryOverlayState_.isOpen() &&
        gameSession_.world().isAlphaRaidWorld();

    if (inputDecision.processUiEvents && !alphaRaidInventory)
    {
        for (const InventoryUiEvent &event :
             pendingInventoryUiEvents_)
        {
            if (const auto *pointerEvent =
                    std::get_if<InventoryPointerEvent>(&event))
            {
                handleInventoryPointerEvent(*pointerEvent);
            }
            else if (const auto *quickTransferEvent =
                         std::get_if<InventoryQuickTransferEvent>(&event))
            {
                handleInventoryQuickTransferEvent(
                    *quickTransferEvent);
            }
            else if (std::holds_alternative<InventoryRotateEvent>(event))
            {
                handleInventoryRotateEvent();
            }
            else
            {
                handleInventoryPartialTransferEvent(
                    std::get<InventoryPartialTransferEvent>(event));
            }
        }
    }

    if (alphaRaidInventory)
    {
        if (inputDecision.processUiEvents)
        {
            for (const InventoryUiEvent &event : pendingInventoryUiEvents_)
            {
                handleProfileInventoryUiEvent(event, true);
            }
            for (MousePosition position : pendingProfileRightClicks_)
            {
                handleProfileRightClick(position, true);
            }
        }
    }

    pendingInventoryUiEvents_.clear();
    pendingProfileRightClicks_.clear();

    GameplayInput gameplayInput{};

    // 背包打开时世界仍继续 update；保留移动/奔跑，屏蔽战斗、
    // 快捷动作、退出和世界交互输入。
    if (gameSession_.world().raidSession().isActive())
    {
        gameplayInput =
            makeGameplayInput();
    }

    gameplayInput.inventoryOpen = inventoryOverlayState_.isOpen();
    if (developerWeaponPanelBlocksGameplayThisFrame_)
    {
        gameplayInput = GameplayInput{};
        gameplayInput.aimWorldPosition =
            gameSession_.world().weaponAimWorldPosition();
        gameplayInput.aimMotionDelta = Vec2{};
    }
    if (inventoryOverlayState_.isOpen())
    {
        gameplayInput.fireJustPressed = false;
        gameplayInput.firePressed = false;
        gameplayInput.reloadJustPressed = false;
        gameplayInput.healJustPressed = false;
        gameplayInput.weaponSlotJustPressed.reset();
        gameplayInput.quitRaidJustPressed = false;
        gameplayInput.interactJustPressed = false;
        gameplayInput.interactPressed = false;
    }
    if (tacticalMapOpen_)
    {
        gameplayInput.movementSpeedMultiplier = 0.45F;
        gameplayInput.sprint = false;
        gameplayInput.fireJustPressed = false;
        gameplayInput.firePressed = false;
        gameplayInput.aimDownSights = false;
        gameplayInput.reloadJustPressed = false;
        gameplayInput.healJustPressed = false;
        gameplayInput.weaponSlotJustPressed.reset();
        gameplayInput.quitRaidJustPressed = false;
        gameplayInput.interactJustPressed = false;
        gameplayInput.interactPressed = false;
        gameplayInput.aimWorldPosition =
            gameSession_.world().weaponAimWorldPosition();
        gameplayInput.aimMotionDelta = Vec2{};
    }
    if (gameSession_.world().isAlphaRaidWorld() &&
        gameSession_.world().raidSession().isActive() &&
        !inventoryOverlayState_.isOpen() &&
        !tacticalMapOpen_ &&
        !developerWeaponPanelOpen_)
    {
        if (input_.wasActionJustPressed(GameAction::Heal) &&
            !gameSession_.raidActionState().active().has_value())
        {
            openMedicalWheel();
            gameplayInput.healJustPressed = false;
            gameplayInput.weaponSlotJustPressed.reset();
        }
        if (medicalWheelOpen_)
        {
            updateMedicalWheelSelection();
            gameplayInput.fireJustPressed = false;
            gameplayInput.firePressed = false;
            gameplayInput.reloadJustPressed = false;
            gameplayInput.interactJustPressed = false;
            gameplayInput.interactPressed = false;
            gameplayInput.healJustPressed = false;
            if (input_.wasActionJustPressed(GameAction::InventoryCancel))
            {
                medicalWheelOpen_ = false;
                medicalWheelOptions_.clear();
                gameplayInput.quitRaidJustPressed = false;
                uiMessage_ = "MEDICAL SELECTION CANCELLED";
            }
            else if (input_.wasActionJustReleased(GameAction::Heal))
            {
                commitMedicalWheelSelection();
            }
        }
    }
    else
    {
        medicalWheelOpen_ = false;
        medicalWheelOptions_.clear();
    }
    if (inputDecision.controlAction != InventoryFrameControlAction::None)
    {
        gameplayInput.quitRaidJustPressed = false;
    }
    const InventoryContainerInteractionDecision
        containerDecision =
            decideInventoryContainerInteraction(
                inventoryOverlayState_.isOpen(),
                inputDecision.controlAction !=
                    InventoryFrameControlAction::None,
                !gameSession_.world().isAlphaRaidWorld() &&
                    gameSession_.world().canInteractWithContainer(),
                gameplayInput.interactJustPressed);

    if (containerDecision.openContainer)
    {
        if (gameSession_.world().searchStorageCabinet())
        {
            inventoryInteraction_.reset();
            inventoryOverlayState_.openContainerInventory();
        }
    }

    if (containerDecision.suppressGameplayInput &&
        !gameSession_.world().isAlphaRaidWorld())
    {
        gameplayInput = GameplayInput{};
    }

    gameFlow_.update(
        gameplayInput,
        deltaTime);
    consumePresentationAudioEvents();

    if (gameSession_.world().shotFiredLastUpdate())
    {
        const auto tuning = gameSession_.developerWeaponTuning();
        gameAudio_.play(
            tuning.has_value() && tuning->weaponUse.automaticFire
                ? SoundEventId::WeaponRifleFire
                : SoundEventId::WeaponPistolFire);
        gameAudio_.play(SoundEventId::WeaponFireTailOutdoor);
    }
    for (const HitResult &hit : gameSession_.world().hitResultsLastUpdate())
    {
        switch (hit.targetKind)
        {
        case HitTargetKind::Enemy:
            gameAudio_.play(SoundEventId::ImpactEnemy);
            gameAudio_.play(
                hit.targetKilled
                    ? SoundEventId::InfectedDeath
                    : SoundEventId::InfectedHit);
            break;
        case HitTargetKind::Obstacle:
            gameAudio_.play(SoundEventId::ImpactObstacle);
            break;
        case HitTargetKind::Ground:
            gameAudio_.play(SoundEventId::ImpactGround);
            break;
        }
    }
    for (std::size_t index{};
         index < gameSession_.world().enemiesAlertedLastUpdate();
         ++index)
    {
        gameAudio_.play(SoundEventId::InfectedAlert);
    }

    specialHitFeedbackRemaining_ = std::max(
        0.0F,
        specialHitFeedbackRemaining_ - std::max(0.0F, deltaTime));
    playerDamageFeedbackRemaining_ = std::max(
        0.0F,
        playerDamageFeedbackRemaining_ - std::max(0.0F, deltaTime));
    for (const HitResult &hit : gameSession_.world().hitResultsLastUpdate())
    {
        if (hit.semantic == HitSemantic::Normal)
        {
            continue;
        }
        specialHitSemantic_ = hit.semantic;
        specialHitFeedbackRemaining_ = 0.18F;
    }
    if (gameSession_.lastIncomingDamage().has_value())
    {
        lastIncomingDamageReducedByArmor_ =
            gameSession_.lastIncomingDamage()->armorReducedDamage;
        playerDamageFeedbackRemaining_ = 0.28F;
        gameAudio_.play(
            gameSession_.lastIncomingDamage()->damageApplied >= 25
                ? SoundEventId::PlayerHurtHeavy
                : SoundEventId::PlayerHurtLight);
    }

    if ((!gameFlow_.isRaidScreen() ||
         gameSession_.world().raidSession().isTerminal()) &&
        inventoryOverlayState_.isOpen())
    {
        closeInventory();
    }
    if (!gameFlow_.isRaidScreen() ||
        gameSession_.world().raidSession().isTerminal())
    {
        developerWeaponPanelOpen_ = false;
    }
}

void App::syncAmbience()
{
    switch (gameFlow_.state())
    {
    case GameFlowState::Base:
        gameAudio_.setAmbience(SoundEventId::AmbienceBaseSafeLow);
        break;
    case GameFlowState::Raid:
        gameAudio_.setAmbience(SoundEventId::AmbienceRaidUrbanLow);
        break;
    case GameFlowState::MainMenu:
    case GameFlowState::RaidResult:
        gameAudio_.setAmbience(std::nullopt);
        break;
    }
}

void App::consumePresentationAudioEvents()
{
    for (const GameSessionPresentationEvent event :
         gameSession_.takePresentationEvents())
    {
        switch (event)
        {
        case GameSessionPresentationEvent::WeaponDryFire:
            gameAudio_.play(SoundEventId::WeaponDryFire);
            break;
        case GameSessionPresentationEvent::WeaponChambered:
            gameAudio_.play(SoundEventId::WeaponChamber);
            break;
        case GameSessionPresentationEvent::ReloadStarted:
            gameAudio_.play(SoundEventId::WeaponMagazineOut);
            break;
        case GameSessionPresentationEvent::ReloadCompleted:
            gameAudio_.play(SoundEventId::WeaponMagazineIn);
            gameAudio_.play(SoundEventId::WeaponChamber);
            break;
        case GameSessionPresentationEvent::MagazineLoaded:
            gameAudio_.play(SoundEventId::InventoryMoveOrPlace);
            break;
        case GameSessionPresentationEvent::MagazineUnloaded:
            gameAudio_.play(SoundEventId::WeaponMagazineOut);
            break;
        case GameSessionPresentationEvent::MedicalStarted:
            gameAudio_.play(SoundEventId::MedicalStart);
            break;
        case GameSessionPresentationEvent::MedicalCompleted:
            gameAudio_.play(SoundEventId::MedicalComplete);
            break;
        case GameSessionPresentationEvent::MedicalInterrupted:
            gameAudio_.play(SoundEventId::MedicalInterrupt);
            break;
        case GameSessionPresentationEvent::WeaponEquipped:
            gameAudio_.play(SoundEventId::InventoryEquip);
            break;
        case GameSessionPresentationEvent::MalfunctionCleared:
            gameAudio_.play(SoundEventId::WeaponMalfunctionClear);
            break;
        case GameSessionPresentationEvent::LootPickedUp:
            gameAudio_.play(SoundEventId::InventoryPickup);
            break;
        case GameSessionPresentationEvent::RescueSecured:
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        }
    }
}

void App::renderDebugText()
{
    SDL_SetRenderDrawColor(
        renderer_,
        220,
        220,
        220,
        255);

    const char *actionText{
        "Action: None"};

    if (
        input_.isActionPressed(
            GameAction::MoveUp))
    {
        actionText =
            "Action: MoveUp";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveDown))
    {
        actionText =
            "Action: MoveDown";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveLeft))
    {
        actionText =
            "Action: MoveLeft";
    }
    else if (
        input_.isActionPressed(
            GameAction::MoveRight))
    {
        actionText =
            "Action: MoveRight";
    }
    else if (
        input_.isActionPressed(
            GameAction::Fire))
    {
        actionText =
            "Action: Fire";
    }
    else if (
        input_.isActionPressed(
            GameAction::Interact))
    {
        actionText =
            "Action: Interact";
    }
    else if (
        input_.isActionPressed(
            GameAction::ToggleInventory))
    {
        actionText =
            "Action: ToggleInventory";
    }
    else if (
        input_.isActionPressed(
            GameAction::Sprint))
    {
        actionText =
            "Action: Sprint";
    }

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        20.0f,
        actionText);

    const std::string scoreText =
        fmt::format(
            "Score: {}",
            gameSession_.world().score());

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        36.0f,
        scoreText.c_str());

    const Player &player =
        gameSession_.world().player();

    const std::string playerHealthText =
        fmt::format(
            "Player HP: {}/{}",
            player.health(),
            player.maxHealth());

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        52.0f,
        playerHealthText.c_str());

    std::size_t aliveEnemyCount{};
    std::size_t alertedEnemyCount{};
    std::size_t searchingEnemyCount{};
    for (const Enemy &enemy : gameSession_.world().enemies())
    {
        if (!enemy.isDead())
        {
            ++aliveEnemyCount;
            if (enemy.awarenessState() == EnemyAwarenessState::Alerted)
            {
                ++alertedEnemyCount;
            }
            else if (enemy.awarenessState() == EnemyAwarenessState::Searching)
            {
                ++searchingEnemyCount;
            }
        }
    }

    const std::string enemyHealthText =
        fmt::format(
            "Enemies: {}/{} | Alerted {} | Searching {}",
            aliveEnemyCount,
            gameSession_.world().enemies().size(),
            alertedEnemyCount,
            searchingEnemyCount);

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        68.0f,
        enemyHealthText.c_str());

    std::size_t groundCount = gameSession_.world().groundItems().size();
    if (gameSession_.world().isAlphaRaidWorld())
    {
        groundCount = 0;
        for (const auto &[id, asset] : gameSession_.profile().assets.records())
        {
            static_cast<void>(id);
            if (std::holds_alternative<RaidGroundAssetLocation>(asset.location))
            {
                const auto &loot = gameSession_.profile().pendingRaid->loot;
                const auto snapshot =
                    std::find_if(loot.begin(),
                                 loot.end(),
                                 [&](const RaidLootSnapshot &entry)
                                 { return entry.assetId == asset.instanceId; });
                if (snapshot != loot.end() &&
                    gameSession_.raidLootAccessible(*snapshot))
                {
                    ++groundCount;
                }
            }
        }
    }
    const std::string groundItemText =
        fmt::format("Ground Items: {}", groundCount);

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        84.0f,
        groundItemText.c_str());

    const std::size_t carriedCount = gameSession_.world().isAlphaRaidWorld()
        ? carriedAssetIds(gameSession_.profile()).size()
        : gameSession_.world().inventory().placedItems().size();
    const std::string inventoryItemText =
        fmt::format("Inventory Items: {}", carriedCount);

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        100.0f,
        inventoryItemText.c_str());

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        116.0f,
        "Interact: F");
    const char *inventoryStateText =
        inventoryOverlayState_.showsExternalContainer()
            ? "Inventory: Cabinet [Open]"
            : inventoryOverlayState_.isOpen()
                  ? "Inventory: Player [Open]"
                  : "Inventory: Tab [Closed]";

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        132.0f,
        inventoryStateText);

    const RaidSession &raidSession =
        gameSession_.world().raidSession();

    const std::string raidStateText =
        fmt::format(
            "Raid: {}",
            raidSessionStateName(
                raidSession.state()));

    uiTextRenderer_.render(
        renderer_,
        20.0F,
        148.0F,
        raidStateText.c_str());

    std::string raidTimeText;
    if (gameSession_.world().emergencyExtractionPoint().has_value())
    {
        if (raidSession.phase() == RaidPhase::Regular)
        {
            raidTimeText = fmt::format(
                "REGULAR: {:.0f}s | NORMAL EXTRACTION OPEN{}",
                raidSession.raidTimeRemaining(),
                raidSession.raidTimeRemaining() <= 30.0F
                    ? " | HIGH RISK SOON"
                    : "");
        }
        else
        {
            raidTimeText =
                "HIGH RISK: CONTINUOUS | SIGNAL EXTRACTION OPEN";
        }
    }
    else
    {
        raidTimeText = gameSession_.world().isAlphaRaidWorld()
            ? "Raid Time: NO HARD LIMIT"
            : fmt::format(
                  "Raid Time: {:.1f}s",
                  raidSession.raidTimeRemaining());
    }

    uiTextRenderer_.render(
        renderer_,
        20.0F,
        164.0F,
        raidTimeText.c_str());

    if (raidSession.phase() == RaidPhase::HighRisk &&
        gameSession_.world().highRiskActiveEnemyCap() > 0U)
    {
        const std::string pressureText = fmt::format(
            "Pressure wave {} | Active {}/{}",
            gameSession_.world().highRiskPressureWaveCount(),
            gameSession_.world().aliveEnemyCount(),
            gameSession_.world().highRiskActiveEnemyCap());
        uiTextRenderer_.render(
            renderer_,
            20.0F,
            180.0F,
            pressureText.c_str());
    }

    const WorldClockProjection clock = gameSession_.worldClockProjection();
    const std::string worldTimeText = fmt::format(
        "DAY {} {:02}:{:02} {}",
        clock.day,
        clock.hour,
        clock.minute,
        worldTimeOfDayName(clock.timeOfDay));
    uiTextRenderer_.render(
        renderer_,
        20.0F,
        196.0F,
        worldTimeText.c_str());

    if (gameSession_.profile().pendingRaid.has_value() &&
        gameSession_.profile().pendingRaid->outpostRestoration.has_value())
    {
        const std::string objective =
            gameSession_.outpostRestorationObjectiveSecured()
            ? "OUTPOST CLEARING COMPLETE | EXTRACT TO RESTORE SHORTCUTS"
            : fmt::format(
                  "OUTPOST CLEARING | INITIAL HOSTILES REMAINING {} | NORMAL EXITS AVAILABLE",
                  gameSession_.world().aliveInitialEnemyCount());
        uiTextRenderer_.render(
            renderer_, 20.0F, 212.0F, objective.c_str());
    }
    else if (gameSession_.profile().pendingRaid.has_value() &&
             gameSession_.profile().pendingRaid->baseSiteClearance.has_value())
    {
        const std::string objective =
            gameSession_.baseSiteClearanceObjectiveSecured()
            ? "BASE SITE SECURED | EXTRACT TO UNLOCK THE LOCATION"
            : fmt::format(
                  "BASE SITE CLEARING | INITIAL HOSTILES REMAINING {} | NORMAL EXITS AVAILABLE",
                  gameSession_.world().aliveInitialEnemyCount());
        uiTextRenderer_.render(
            renderer_, 20.0F, 212.0F, objective.c_str());
    }
    else if (gameSession_.profile().pendingRaid.has_value() &&
             gameSession_.profile().pendingRaid->basePerimeterSweep.has_value())
    {
        const std::string objective =
            gameSession_.basePerimeterSweepObjectiveSecured()
            ? "PERIMETER SECURED | EXTRACT TO REDUCE BASE THREAT"
            : fmt::format(
                  "PERIMETER SWEEP | INITIAL HOSTILES REMAINING {} | NORMAL EXITS AVAILABLE",
                  gameSession_.world().aliveInitialEnemyCount());
        uiTextRenderer_.render(
            renderer_, 20.0F, 212.0F, objective.c_str());
    }

    const std::string stashText = gameSession_.world().isAlphaRaidWorld()
        ? fmt::format(
              "Profile assets: {} | Currency {}",
              gameSession_.profile().assets.records().size(),
              gameSession_.profile().currency)
        : fmt::format(
              "Stash: {} stacks / {} units",
              gameSession_.stash().stackCount(),
              gameSession_.stash().unitCount());

    uiTextRenderer_.render(
        renderer_,
        980.0F,
        20.0F,
        stashText.c_str());

    const std::string settlementStateText =
        fmt::format(
            "Settlement: {}",
            raidSettlementStateName(
                gameSession_.settlement().state()));

    uiTextRenderer_.render(
        renderer_,
        980.0F,
        36.0F,
        settlementStateText.c_str());

    const std::string gameSessionText =
        fmt::format(
            "Flow: {} | Session: {} | Raid {}",
            gameFlowStateName(
                gameFlow_.state()),
            gameSessionStateName(
                gameSession_.state()),
            gameSession_.raidNumber());

    uiTextRenderer_.render(
        renderer_,
        980.0F,
        52.0F,
        gameSessionText.c_str());

    std::string enemyAiText{"Enemy AI: none"};
    if (!gameSession_.world().enemies().empty())
    {
        const Enemy &enemy =
            gameSession_.world().enemies().front();
        if (enemy.isDead())
        {
            enemyAiText = "Enemy AI: defeated";
        }
        else if (enemy.attackType().has_value())
        {
            enemyAiText = fmt::format(
                "Lead AI: {} {}",
                enemyAttackTypeName(*enemy.attackType()),
                enemyAttackPhaseName(enemy.attackPhase()));
        }
        else if (enemy.isMoving())
        {
            enemyAiText = "Lead AI: moving";
        }
        else
        {
            enemyAiText = "Lead AI: holding";
        }

        if (!enemy.isDead())
        {
            enemyAiText += fmt::format(
                " | {} {} | Move {} {:.0f}",
                enemyAwarenessStateName(
                    enemy.awarenessState()),
                enemyTacticalRoleName(
                    enemy.tacticalRole()),
                enemyMovementStateName(
                    enemy.movementState()),
                enemy.movementSpeed());
            if (enemy.isImpactSlowed())
            {
                enemyAiText += fmt::format(
                    " | stagger {:.2f}s",
                    enemy.impactSlowRemaining());
            }
        }
    }

    uiTextRenderer_.render(
        renderer_,
        980.0F,
        68.0F,
        enemyAiText.c_str());

    std::string playerControlText =
        player.isControlled()
            ? fmt::format(
                  "Player controlled: {:.2f}s",
                  player.controlRemaining())
            : "Player controlled: no";
    if (player.isImpactSlowed())
    {
        playerControlText += fmt::format(
            " | stagger {:.2f}s",
            player.impactSlowRemaining());
    }
    uiTextRenderer_.render(
        renderer_,
        980.0F,
        84.0F,
        playerControlText.c_str());

    if (gameSession_.world().isAlphaRaidWorld())
    {
        std::string ammunitionText{"Weapon: NONE"};
        if (const auto weapon = gameSession_.activeAlphaWeapon())
        {
            const AssetRecord *record =
                gameSession_.profile().assets.find(*weapon);
            if (record != nullptr)
            {
                const auto magazine = installedMagazine(
                    gameSession_.profile(),
                    *weapon);
                const ItemDefinition &definition =
                    publishedContentRegistry().item(record->definitionId);
                ammunitionText = fmt::format(
                    "{} | Chamber {} | Magazine {} | Condition {:.2f}/{:.2f}{}",
                    definition.displayName,
                    record->chamberedRound.has_value() ? 1 : 0,
                    magazine.has_value()
                        ? magazineRoundCount(gameSession_.profile(), *magazine)
                        : 0U,
                    static_cast<float>(record->currentDurability) / 100.0F,
                    static_cast<float>(record->currentMaximumDurability) / 100.0F,
                    record->weaponMalfunction != WeaponMalfunctionType::None
                        ? " | MALFUNCTION - SWEEP MOUSE"
                        : "");
            }
        }
        uiTextRenderer_.render(
            renderer_, 980.0F, 100.0F, ammunitionText.c_str());

        if (gameSession_.raidActionState().active().has_value())
        {
            const char *name = std::visit(
                [](const auto &action)
                {
                    using Action = std::decay_t<decltype(action)>;
                    if constexpr (std::is_same_v<Action, ReloadRaidAction>)
                        return "RELOADING";
                    if constexpr (
                        std::is_same_v<Action, LoadMagazineRaidAction>)
                        return "PACKING MAGAZINE";
                    if constexpr (std::is_same_v<Action, HealRaidAction>)
                        return "HEALING";
                    if constexpr (std::is_same_v<Action, MedicalRaidAction>)
                    {
                        switch (action.effect)
                        {
                        case MedicalUseEffect::RestoreHealth:
                            return "HEALING";
                        case MedicalUseEffect::StopLightBleeding:
                            return "BANDAGING";
                        case MedicalUseEffect::StopAnyBleeding:
                            return "APPLYING TOURNIQUET";
                        case MedicalUseEffect::SuppressPain:
                            return "TAKING PAINKILLER";
                        }
                    }
                    if constexpr (
                        std::is_same_v<Action, UnloadMagazineRaidAction>)
                        return "UNLOADING MAGAZINE";
                    if constexpr (
                        std::is_same_v<Action, WeaponMaintenanceRaidAction>)
                        return "MAINTAINING WEAPON";
                    if constexpr (
                        std::is_same_v<Action, ArmorMaintenanceRaidAction>)
                        return "REPAIRING ARMOR";
                    if constexpr (
                        std::is_same_v<Action, WeaponSwitchRaidAction>)
                        return "SWITCHING WEAPON";
                    return "EXTRACTING";
                },
                *gameSession_.raidActionState().active());
            const std::string action = fmt::format(
                "{} {:.0f}%",
                name,
                gameSession_.raidActionState().progress() * 100.0F);
            uiTextRenderer_.render(
                renderer_, 980.0F, 116.0F, action.c_str());
        }
        else
        {
            uiTextRenderer_.render(
                renderer_, 980.0F, 116.0F,
                "LMB FIRE | RMB AIM | SHIFT SPRINT | R RELOAD");
        }
        const ProfileState &profile = gameSession_.profile();
        const char *bleed = profile.medicalStatus.bleeding ==
                BleedingSeverity::Heavy
            ? "HEAVY BLEED"
            : profile.medicalStatus.bleeding == BleedingSeverity::Light
                ? "LIGHT BLEED"
                : "NO BLEED";
        const std::string medicalStatus = fmt::format(
            "HP {} | {}{}",
            profile.currentHealth,
            bleed,
            hasPain(profile.medicalStatus)
                ? painIsSuppressed(profile.medicalStatus)
                    ? " | PAIN SUPPRESSED"
                    : " | PAIN -10%"
                : "");
        uiTextRenderer_.render(
            renderer_, 980.0F, 132.0F, medicalStatus.c_str());
        uiTextRenderer_.render(
            renderer_, 980.0F, 148.0F,
            "F9 PERFORMANCE | F10 RUNTIME DEVELOPER PANEL");

        for (std::size_t index = 0; index < kWeaponEquipmentSlots.size(); ++index)
        {
            const EquipmentSlotKind slot = kWeaponEquipmentSlots[index];
            const bool active = gameSession_.activeAlphaWeaponSlot() == slot;
            const SDL_FRect selector{
                20.0F + static_cast<float>(index) * 142.0F,
                674.0F,
                134.0F,
                30.0F};
            SDL_SetRenderDrawColor(
                renderer_, active ? 42 : 22, active ? 88 : 40,
                active ? 70 : 46, 220);
            SDL_RenderFillRect(renderer_, &selector);
            SDL_SetRenderDrawColor(
                renderer_, active ? 104 : 78, active ? 220 : 108,
                active ? 166 : 116, 255);
            SDL_RenderRect(renderer_, &selector);
            std::string name{"EMPTY"};
            if (const auto id = equippedAsset(profile, slot))
            {
                if (const AssetRecord *asset = profile.assets.find(*id))
                {
                    name = publishedContentRegistry()
                        .item(asset->definitionId).displayName;
                }
            }
            const std::string slotText = fmt::format(
                "{} {}{}",
                index + 1U,
                active ? ">" : " ",
                name.substr(0, 12));
            uiTextRenderer_.render(
                renderer_, selector.x + 6.0F, selector.y + 10.0F,
                slotText.c_str());
        }
        if (!uiMessage_.empty())
        {
            uiTextRenderer_.render(
                renderer_, 760.0F, 684.0F, uiMessage_.c_str());
        }
    }

    if (raidSession.state() ==
            RaidSessionState::Extracting ||
        raidSession.state() ==
            RaidSessionState::Extracted)
    {
        const std::string extractionText =
            fmt::format(
                "Extraction: {:.0f}%",
                raidSession.extractionProgress() *
                    100.0F);

        uiTextRenderer_.render(
            renderer_,
            20.0F,
            180.0F,
            extractionText.c_str());
    }

    if (raidSession.isTerminal() &&
        !gameSession_.world().isAlphaRaidWorld())
    {
        const std::string outcomeText =
            fmt::format(
                "RAID {} RESULT: {}",
                gameSession_.raidNumber(),
                raidSessionStateName(
                    raidSession.state()));

        const RaidSettlementSummary settlementSummary =
            gameSession_.settlement().summary();
        std::string settlementText;

        switch (gameSession_.settlement().state())
        {
        case RaidSettlementState::Blocked:
            settlementText =
                "STASH BLOCKED - INVENTORY PRESERVED";
            break;
        case RaidSettlementState::Extracted:
            settlementText = fmt::format(
                "STORED {} STACKS / {} UNITS",
                settlementSummary.stackCount,
                settlementSummary.unitCount);
            break;
        case RaidSettlementState::PlayerDead:
        case RaidSettlementState::RaidEnded:
            settlementText = fmt::format(
                "LOST {} STACKS / {} UNITS",
                settlementSummary.stackCount,
                settlementSummary.unitCount);
            break;
        case RaidSettlementState::Pending:
            settlementText = "SETTLEMENT PENDING";
            break;
        }

        uiTextRenderer_.render(
            renderer_,
            kStashPanelX + 130.0F,
            kStashPanelY + 42.0F,
            outcomeText.c_str());
        uiTextRenderer_.render(
            renderer_,
            kStashPanelX + 76.0F,
            kStashPanelY + 62.0F,
            settlementText.c_str());

        const std::string nextRaidText =
            gameFlow_.state() ==
                    GameFlowState::RaidResult
                ? "ENTER / CLICK: RETURN TO BASE"
                : "Resolve Stash capacity before returning";

        uiTextRenderer_.render(
            renderer_,
            kStashPanelX + 92.0F,
            kStashPanelY + 82.0F,
            nextRaidText.c_str());
    }

    if (!inventoryOverlayState_.isOpen())
    {
        return;
    }

    const char *phaseText = "Idle";

    if (inventoryInteraction_.pointerPhase() ==
        InventoryPointerPhase::Pressed)
    {
        phaseText = "Pressed";
    }
    else if (inventoryInteraction_.pointerPhase() ==
             InventoryPointerPhase::Dragging)
    {
        phaseText = "Dragging";
    }

    const std::string interactionText =
        fmt::format("Pointer: {}", phaseText);

    uiTextRenderer_.render(
        renderer_,
        20.0f,
        180.0f,
        interactionText.c_str());

    if (inventoryOverlayState_.showsExternalContainer())
    {
        uiTextRenderer_.render(
            renderer_,
            20.0f,
            212.0f,
            "Quick Transfer: F / Ctrl+Right Click");
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    if (selected.has_value())
    {
        const std::string selectedText =
            fmt::format(
                "Selected {} Item ID: {}",
                selected->container == InventoryContainerId::Player
                    ? "Player"
                    : "Container",
                selected->instanceId);

        uiTextRenderer_.render(
            renderer_,
            20.0f,
            196.0f,
            selectedText.c_str());
    }
}
void App::renderInventoryPlacementPreview(
    const GridInventory &inventory,
    InventoryContainerId container,
    const InventoryGridLayout &layout)
{
    const std::optional<InventoryGridLocation> activePreview =
        inventoryInteraction_.activePreviewLocation();

    const std::optional<InventoryDragVisual> dragVisual =
        inventoryInteraction_.activeDragVisual();

    if (!activePreview.has_value() &&
        !dragVisual.has_value())
    {
        return;
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    if (!selected.has_value())
    {
        return;
    }

    const GridInventory &sourceInventory =
        inventoryFor(selected->container);

    const auto &placedItems =
        sourceInventory.placedItems();

    const auto placedIt =
        std::find_if(
            placedItems.begin(),
            placedItems.end(),
            [selected](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       selected->instanceId;
            });

    // selectedItem 对应的物品理论上必须存在。
    // 如果核心模型和 UI 状态意外不同步，则不绘制预览。
    if (placedIt == placedItems.end())
    {
        return;
    }

    const ItemDefinition &definition =
        itemDefinition(
            placedIt->item.definitionId());

    const ItemOrientation previewOrientation =
        dragVisual.has_value()
            ? dragVisual->orientation
            : placedIt->item.orientation();
    const InventoryFootprint previewFootprint =
        inventoryFootprint(
            definition,
            previewOrientation);
    const int itemWidth = previewFootprint.width;
    const int itemHeight = previewFootprint.height;

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();
    const float cellSize = layout.cellSize();

    std::optional<SDL_FRect> ghostDestination;

    if (dragVisual.has_value() &&
        container == selected->container)
    {
        // 用当前鼠标位置减去旋转后的连续抓取锚点，
        // 保证按 R 后虚像不会从指针下跳走。
        ghostDestination = SDL_FRect{
            dragVisual->pointerPosition.x -
                dragVisual->grabOffsetInCells.x * cellSize,
            dragVisual->pointerPosition.y -
                dragVisual->grabOffsetInCells.y * cellSize,
            static_cast<float>(itemWidth) *
                cellSize,
            static_cast<float>(itemHeight) *
                cellSize};
    }

    if (ghostDestination.has_value())
    {
        const std::size_t textureIndex =
            static_cast<std::size_t>(definition.id);

        Texture &texture =
            inventoryItemTextures_[textureIndex];

        if (texture.valid())
        {
            SDL_SetTextureAlphaMod(texture.get(), 145);

            renderOrientedTexture(
                renderer_,
                texture.get(),
                *ghostDestination,
                static_cast<float>(
                    definition.inventoryWidthCells) * cellSize,
                static_cast<float>(
                    definition.inventoryHeightCells) * cellSize,
                previewOrientation);

            renderItemQuantityBadge(
                renderer_,
                *ghostDestination,
                dragVisual->selectedQuantity.value_or(
                    placedIt->item.quantity()),
                dragVisual->selectedQuantity.has_value());

            // 纹理对象会被后续帧继续复用，
            // 因此必须恢复默认不透明度。
            SDL_SetTextureAlphaMod(texture.get(), 255);
        }
    }

    // 鼠标在网格外时仍绘制平滑虚像，但不绘制候选 footprint。
    if (!activePreview.has_value() ||
        activePreview->container != container)
    {
        return;
    }

    const GridPosition previewOrigin = activePreview->cell;

    const bool legal = dragVisual->selectedQuantity.has_value()
        ? canPlaceItemQuantityAt(
              sourceInventory,
              inventory,
              selected->instanceId,
              *dragVisual->selectedQuantity,
              previewOrigin,
              previewOrientation)
        : canPlaceWholeItemAt(
              sourceInventory,
              inventory,
              selected->instanceId,
              previewOrigin,
              previewOrientation);

    // 合法位置使用淡绿色；
    // 非法位置使用淡红色。
    if (legal)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            70,
            190,
            105,
            105);
    }
    else
    {
        SDL_SetRenderDrawColor(
            renderer_,
            210,
            70,
            70,
            120);
    }

    // 对 footprint 中仍位于网格内的每个格子绘制背景。
    //
    // 当多格物品靠近右侧或底部产生越界时，
    // 网格内部分仍显示红色，不向面板外绘制。
    for (
        int offsetY = 0;
        offsetY < itemHeight;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < itemWidth;
            ++offsetX)
        {
            const GridPosition cell{
                previewOrigin.x + offsetX,
                previewOrigin.y + offsetY};

            if (
                cell.x < 0 ||
                cell.y < 0 ||
                cell.x >= inventory.width() ||
                cell.y >= inventory.height())
            {
                continue;
            }

            const SDL_FRect cellRect{
                gridX +
                    static_cast<float>(cell.x) *
                        cellSize +
                    1.0f,
                gridY +
                    static_cast<float>(cell.y) *
                        cellSize +
                    1.0f,
                cellSize - 2.0f,
                cellSize - 2.0f};

            SDL_RenderFillRect(
                renderer_,
                &cellRect);
        }
    }

    // 给候选 footprint 的网格内部分增加明确轮廓。
    if (legal)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            125,
            245,
            155,
            255);
    }
    else
    {
        SDL_SetRenderDrawColor(
            renderer_,
            255,
            125,
            125,
            255);
    }

    for (
        int offsetY = 0;
        offsetY < itemHeight;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < itemWidth;
            ++offsetX)
        {
            const GridPosition cell{
                previewOrigin.x + offsetX,
                previewOrigin.y + offsetY};

            if (
                cell.x < 0 ||
                cell.y < 0 ||
                cell.x >= inventory.width() ||
                cell.y >= inventory.height())
            {
                continue;
            }

            const SDL_FRect outlineRect{
                gridX +
                    static_cast<float>(cell.x) *
                        cellSize +
                    2.0f,
                gridY +
                    static_cast<float>(cell.y) *
                        cellSize +
                    2.0f,
                cellSize - 4.0f,
                cellSize - 4.0f};

            SDL_RenderRect(
                renderer_,
                &outlineRect);
        }
    }
}

void App::renderInventoryPointerFeedback(
    InventoryContainerId container,
    const InventoryGridLayout &layout)
{
    if (inventoryInteraction_.pointerPhase() !=
        InventoryPointerPhase::Pressed)
    {
        return;
    }

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();

    const std::optional<InventoryGridLocation> hovered =
        inventoryInteraction_.hoveredLocation();

    if (hovered.has_value() &&
        hovered->container == container)
    {
        SDL_SetRenderDrawColor(
            renderer_,
            80,
            205,
            225,
            255);

        const SDL_FRect hoverRect{
            gridX +
                static_cast<float>(hovered->cell.x) *
                    kInventoryCellSize +
                2.0F,
            gridY +
                static_cast<float>(hovered->cell.y) *
                    kInventoryCellSize +
                2.0F,
            kInventoryCellSize - 4.0F,
            kInventoryCellSize - 4.0F};

        SDL_RenderRect(renderer_, &hoverRect);
    }
}

void App::renderInventoryOverlay()
{
    if (!inventoryOverlayState_.isOpen())
    {
        return;
    }

    if (gameSession_.world().isAlphaRaidWorld())
    {
        renderProfileInventory(false, true);
        return;
    }

    const GridInventory &inventory =
        gameSession_.world().inventory();

    const InventoryGridLayout layout =
        inventoryGridLayout(
            InventoryContainerId::Player);

    const float gridWidth =
        static_cast<float>(inventory.width()) *
        layout.cellSize();

    const float gridHeight =
        static_cast<float>(inventory.height()) *
        layout.cellSize();

    const float panelWidth =
        gridWidth +
        kInventoryPanelPadding * 2.0f;

    const float panelHeight =
        gridHeight +
        kInventoryPanelPadding * 2.0f +
        kInventoryHeaderHeight;

    const float gridX = layout.gridX();
    const float gridY = layout.gridY();

    const float panelX =
        gridX - kInventoryPanelPadding;

    const float panelY =
        gridY -
        kInventoryPanelPadding -
        kInventoryHeaderHeight;

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    // 半透明面板背景。
    SDL_SetRenderDrawColor(
        renderer_,
        12,
        16,
        20,
        225);

    const SDL_FRect panelRect{
        panelX,
        panelY,
        panelWidth,
        panelHeight};

    SDL_RenderFillRect(
        renderer_,
        &panelRect);

    // 网格底色。
    SDL_SetRenderDrawColor(
        renderer_,
        28,
        34,
        40,
        245);

    const SDL_FRect gridRect{
        gridX,
        gridY,
        gridWidth,
        gridHeight};

    SDL_RenderFillRect(
        renderer_,
        &gridRect);

    // 原物品始终正常绘制在已提交位置。
    for (
        const PlacedItem &placed :
        inventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(
                placed.item.definitionId());

        const std::size_t textureIndex =
            static_cast<std::size_t>(
                definition.id);

        const Texture &texture =
            inventoryItemTextures_[textureIndex];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());

        const SDL_FRect destination{
            gridX +
                static_cast<float>(
                    placed.origin.x) *
                    kInventoryCellSize,
            gridY +
                static_cast<float>(
                    placed.origin.y) *
                    kInventoryCellSize,
            static_cast<float>(
                footprint.width) *
                kInventoryCellSize,
            static_cast<float>(
                footprint.height) *
                kInventoryCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(
                definition.inventoryWidthCells) *
                kInventoryCellSize,
            static_cast<float>(
                definition.inventoryHeightCells) *
                kInventoryCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    renderInventoryPointerFeedback(
        InventoryContainerId::Player,
        layout);

    // 网格线最后绘制，使物品和预览 footprint
    // 仍保持清晰的格子边界。
    SDL_SetRenderDrawColor(
        renderer_,
        105,
        116,
        126,
        220);

    for (
        int column = 0;
        column <= inventory.width();
        ++column)
    {
        const float x =
            gridX +
            static_cast<float>(column) *
                kInventoryCellSize;

        SDL_RenderLine(
            renderer_,
            x,
            gridY,
            x,
            gridY + gridHeight);
    }

    for (
        int row = 0;
        row <= inventory.height();
        ++row)
    {
        const float y =
            gridY +
            static_cast<float>(row) *
                kInventoryCellSize;

        SDL_RenderLine(
            renderer_,
            gridX,
            y,
            gridX + gridWidth,
            y);
    }

    // 面板外框。
    SDL_SetRenderDrawColor(
        renderer_,
        180,
        190,
        200,
        255);

    SDL_RenderRect(
        renderer_,
        &panelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        220,
        225,
        230,
        255);

    uiTextRenderer_.render(
        renderer_,
        panelX +
            kInventoryPanelPadding,
        panelY +
            kInventoryPanelPadding,
        "PLAYER");

    const std::string itemCountText =
        fmt::format(
            "{} item(s)",
            inventory.placedItems().size());

    uiTextRenderer_.render(
        renderer_,
        panelX +
            panelWidth -
            100.0f,
        panelY +
            kInventoryPanelPadding,
        itemCountText.c_str());

    const char *controlText =
        "Drag | Ctrl+LMB: 1 | Shift+LMB: half | R: rotate";

    uiTextRenderer_.render(
        renderer_,
        panelX +
            kInventoryPanelPadding,
        panelY +
            kInventoryPanelPadding +
            16.0f,
        controlText);

    const GridInventory &externalInventory =
        gameSession_.world().containerInventory();

    const InventoryGridLayout externalLayout =
        inventoryGridLayout(
            InventoryContainerId::External);

    const float externalGridX =
        externalLayout.gridX();
    const float externalGridY =
        externalLayout.gridY();
    const float externalGridWidth =
        static_cast<float>(externalInventory.width()) *
        externalLayout.cellSize();
    const float externalGridHeight =
        static_cast<float>(externalInventory.height()) *
        externalLayout.cellSize();
    const float externalPanelWidth =
        externalGridWidth +
        kInventoryPanelPadding * 2.0F;
    const float externalPanelHeight =
        externalGridHeight +
        kInventoryPanelPadding * 2.0F +
        kInventoryHeaderHeight;
    const float externalPanelX =
        externalGridX - kInventoryPanelPadding;
    const float externalPanelY =
        externalGridY -
        kInventoryPanelPadding -
        kInventoryHeaderHeight;

    if (inventoryOverlayState_.showsExternalContainer())
    {
    const SDL_FRect externalPanelRect{
        externalPanelX,
        externalPanelY,
        externalPanelWidth,
        externalPanelHeight};
    const SDL_FRect externalGridRect{
        externalGridX,
        externalGridY,
        externalGridWidth,
        externalGridHeight};

    SDL_SetRenderDrawColor(
        renderer_,
        12,
        16,
        20,
        225);
    SDL_RenderFillRect(renderer_, &externalPanelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        28,
        34,
        40,
        245);
    SDL_RenderFillRect(renderer_, &externalGridRect);

    for (const PlacedItem &placed :
         externalInventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(placed.item.definitionId());
        const Texture &texture =
            inventoryItemTextures_[
                static_cast<std::size_t>(definition.id)];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());

        const SDL_FRect destination{
            externalGridX +
                static_cast<float>(placed.origin.x) *
                    kInventoryCellSize,
            externalGridY +
                static_cast<float>(placed.origin.y) *
                    kInventoryCellSize,
            static_cast<float>(footprint.width) *
                kInventoryCellSize,
            static_cast<float>(footprint.height) *
                kInventoryCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(definition.inventoryWidthCells) *
                kInventoryCellSize,
            static_cast<float>(definition.inventoryHeightCells) *
                kInventoryCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    renderInventoryPointerFeedback(
        InventoryContainerId::External,
        externalLayout);

    SDL_SetRenderDrawColor(
        renderer_,
        105,
        116,
        126,
        220);

    for (int column = 0;
         column <= externalInventory.width();
         ++column)
    {
        const float x =
            externalGridX +
            static_cast<float>(column) *
                kInventoryCellSize;
        SDL_RenderLine(
            renderer_,
            x,
            externalGridY,
            x,
            externalGridY + externalGridHeight);
    }

    for (int row = 0;
         row <= externalInventory.height();
         ++row)
    {
        const float y =
            externalGridY +
            static_cast<float>(row) *
                kInventoryCellSize;
        SDL_RenderLine(
            renderer_,
            externalGridX,
            y,
            externalGridX + externalGridWidth,
            y);
    }

    SDL_SetRenderDrawColor(
        renderer_,
        180,
        190,
        200,
        255);
    SDL_RenderRect(renderer_, &externalPanelRect);

    SDL_SetRenderDrawColor(
        renderer_,
        220,
        225,
        230,
        255);
    uiTextRenderer_.render(
        renderer_,
        externalPanelX + kInventoryPanelPadding,
        externalPanelY + kInventoryPanelPadding,
        "CONTAINER");

    const std::string externalCountText =
        fmt::format(
            "{} item(s)",
            externalInventory.placedItems().size());
    uiTextRenderer_.render(
        renderer_,
        externalPanelX + externalPanelWidth - 100.0F,
        externalPanelY + kInventoryPanelPadding,
        externalCountText.c_str());
    }

    const std::optional<InventoryItemSelection> selected =
        inventoryInteraction_.selectedItem();

    const SDL_FRect dropZone = inventoryDropZone();
    const bool draggingPlayerItem =
        inventoryInteraction_.pointerPhase() ==
            InventoryPointerPhase::Dragging &&
        selected.has_value() &&
        selected->container == InventoryContainerId::Player;
    const bool draggingExternalItem =
        inventoryInteraction_.pointerPhase() ==
            InventoryPointerPhase::Dragging &&
        selected.has_value() &&
        selected->container == InventoryContainerId::External;
    const bool dropZoneHovered =
        inventoryInteraction_.pointerOverDropZone();

    if (dropZoneHovered && draggingPlayerItem)
    {
        SDL_SetRenderDrawColor(renderer_, 35, 105, 58, 230);
    }
    else if (dropZoneHovered && draggingExternalItem)
    {
        SDL_SetRenderDrawColor(renderer_, 115, 42, 42, 230);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer_, 30, 36, 42, 88);
    }

    SDL_RenderFillRect(renderer_, &dropZone);

    SDL_SetRenderDrawColor(
        renderer_,
        dropZoneHovered && draggingPlayerItem ? 125 : 160,
        dropZoneHovered && draggingPlayerItem ? 245 : 170,
        dropZoneHovered && draggingPlayerItem ? 155 : 180,
        255);
    SDL_RenderRect(renderer_, &dropZone);

    SDL_SetRenderDrawColor(renderer_, 230, 230, 230, 255);
    uiTextRenderer_.render(
        renderer_,
        dropZone.x + 28.0F,
        dropZone.y + 326.0F,
        "DROP");
    uiTextRenderer_.render(
        renderer_,
        dropZone.x + 20.0F,
        dropZone.y + 344.0F,
        "PLAYER");
    uiTextRenderer_.render(
        renderer_,
        dropZone.x + 32.0F,
        dropZone.y + 362.0F,
        "ONLY");

    // Draw the destination footprint first and the smooth source ghost last.
    // Keeping both above the panels and drop zone prevents cross-container
    // drags from being obscured by later UI layers.
    if (!inventoryOverlayState_.showsExternalContainer())
    {
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
    }
    else if (selected.has_value() &&
             selected->container == InventoryContainerId::External)
    {
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
        renderInventoryPlacementPreview(
            externalInventory,
            InventoryContainerId::External,
            externalLayout);
    }
    else
    {
        renderInventoryPlacementPreview(
            externalInventory,
            InventoryContainerId::External,
            externalLayout);
        renderInventoryPlacementPreview(
            inventory,
            InventoryContainerId::Player,
            layout);
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderStashOverlay()
{
    if (gameSession_.world().isAlphaRaidWorld() &&
        gameFlow_.state() == GameFlowState::RaidResult)
    {
        const SDL_FRect panel{
            kStashPanelX,
            kStashPanelY,
            kStashPanelWidth,
            kStashPanelHeight};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 20, 28, 30, 244);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 122, 184, 166, 255);
        SDL_RenderRect(renderer_, &panel);
        uiTextRenderer_.render(
            renderer_, panel.x + 154.0F, panel.y + 24.0F,
            "RAID RESULT");
        const auto &result = gameSession_.profile().lastRaidResult;
        if (result.has_value())
        {
            const char *outcome = result->outcome == RaidResultOutcome::Extracted
                ? "EXTRACTED"
                : result->outcome == RaidResultOutcome::PlayerDead
                    ? "PLAYER DEAD"
                    : result->outcome == RaidResultOutcome::ActiveQuit
                        ? "RAID ABANDONED"
                        : "ABNORMAL EXIT - FAILED";
            uiTextRenderer_.render(
                renderer_, panel.x + 24.0F, panel.y + 64.0F, outcome);
            const std::string currency = fmt::format(
                "CURRENCY CHANGE: {:+}",
                result->currencyDelta);
            uiTextRenderer_.render(
                renderer_, panel.x + 24.0F, panel.y + 90.0F,
                currency.c_str());
            const std::string travel = fmt::format(
                "RETURN / REGROUP TRAVEL: {} MIN",
                result->travelMinutesApplied);
            uiTextRenderer_.render(
                renderer_, panel.x + 24.0F, panel.y + 112.0F,
                travel.c_str());
            const std::string rescued = fmt::format(
                "RESCUED RESIDENTS: +{} (SECURED)",
                result->rescuedOrdinaryResidents);
            uiTextRenderer_.render(
                renderer_, panel.x + 24.0F, panel.y + 136.0F,
                rescued.c_str());
            if (result->baseThreatReducedUnits > 0U)
            {
                const std::string threat = fmt::format(
                    "BASE THREAT REDUCED: -{}",
                    result->baseThreatReducedUnits);
                uiTextRenderer_.render(
                    renderer_, panel.x + 250.0F, panel.y + 136.0F,
                    threat.c_str());
            }
            if (result->outcome == RaidResultOutcome::Extracted)
            {
                const std::string count = fmt::format(
                    "RETURNED ASSETS: {}",
                    result->returnedItemDefinitionIds.size());
                uiTextRenderer_.render(
                    renderer_, panel.x + 24.0F, panel.y + 162.0F,
                    count.c_str());
                float y = panel.y + 188.0F;
                for (std::size_t index = 0;
                     index < result->returnedItemDefinitionIds.size() &&
                     index < 9U;
                     ++index)
                {
                    const std::string &name = publishedContentRegistry().item(
                        result->returnedItemDefinitionIds[index]).displayName;
                    uiTextRenderer_.render(
                        renderer_, panel.x + 42.0F, y, name.c_str());
                    y += 20.0F;
                }
            }
            else
            {
                uiTextRenderer_.render(
                    renderer_, panel.x + 24.0F, panel.y + 166.0F,
                    "ALL CARRIED ASSETS WERE LOST");
                uiTextRenderer_.render(
                    renderer_, panel.x + 24.0F, panel.y + 190.0F,
                    result->lostRaidRecordId.has_value()
                        ? "A LOST RAID RECORD IS AVAILABLE AT THE RAID GATE"
                        : "NO GEAR WAS CARRIED; NO LOST RECORD WAS CREATED");
            }
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }

    const GridInventory &stashInventory =
        gameSession_.stash().inventory();
    const float gridWidth =
        static_cast<float>(stashInventory.width()) *
        kStashCellSize;
    const float gridHeight =
        static_cast<float>(stashInventory.height()) *
        kStashCellSize;
    const SDL_FRect panel{
        kStashPanelX,
        kStashPanelY,
        kStashPanelWidth,
        kStashPanelHeight};
    const SDL_FRect grid{
        kStashGridX,
        kStashGridY,
        gridWidth,
        gridHeight};

    const bool extracted =
        gameSession_.settlement().state() ==
        RaidSettlementState::Extracted;
    const bool baseScreen =
        gameFlow_.state() == GameFlowState::Base;

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        baseScreen ? 18 : extracted ? 14 : 36,
        baseScreen ? 31 : extracted ? 45 : 20,
        baseScreen ? 38 : extracted ? 31 : 22,
        238);
    SDL_RenderFillRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 24, 30, 34, 248);
    SDL_RenderFillRect(renderer_, &grid);

    for (const PlacedItem &placed :
         stashInventory.placedItems())
    {
        const ItemDefinition &definition =
            itemDefinition(
                placed.item.definitionId());
        const Texture &texture =
            inventoryItemTextures_[
                static_cast<std::size_t>(
                    definition.id)];

        if (!texture.valid())
        {
            continue;
        }

        const InventoryFootprint footprint =
            inventoryFootprint(
                definition,
                placed.item.orientation());
        const SDL_FRect destination{
            kStashGridX +
                static_cast<float>(placed.origin.x) *
                    kStashCellSize,
            kStashGridY +
                static_cast<float>(placed.origin.y) *
                    kStashCellSize,
            static_cast<float>(footprint.width) *
                kStashCellSize,
            static_cast<float>(footprint.height) *
                kStashCellSize};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            static_cast<float>(
                definition.inventoryWidthCells) *
                kStashCellSize,
            static_cast<float>(
                definition.inventoryHeightCells) *
                kStashCellSize,
            placed.item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            placed.item.quantity());
    }

    SDL_SetRenderDrawColor(renderer_, 91, 105, 112, 205);

    for (int column = 0;
         column <= stashInventory.width();
         ++column)
    {
        const float x =
            kStashGridX +
            static_cast<float>(column) *
                kStashCellSize;
        SDL_RenderLine(
            renderer_,
            x,
            kStashGridY,
            x,
            kStashGridY + gridHeight);
    }

    for (int row = 0;
         row <= stashInventory.height();
         ++row)
    {
        const float y =
            kStashGridY +
            static_cast<float>(row) *
                kStashCellSize;
        SDL_RenderLine(
            renderer_,
            kStashGridX,
            y,
            kStashGridX + gridWidth,
            y);
    }

    SDL_SetRenderDrawColor(
        renderer_,
        baseScreen ? 105 : extracted ? 125 : 215,
        baseScreen ? 170 : extracted ? 235 : 110,
        baseScreen ? 205 : extracted ? 155 : 110,
        255);
    SDL_RenderRect(renderer_, &panel);
    SDL_RenderRect(renderer_, &grid);

    SDL_SetRenderDrawColor(renderer_, 225, 230, 232, 255);
    uiTextRenderer_.render(
        renderer_,
        kStashPanelX + 18.0F,
        kStashPanelY + 16.0F,
        baseScreen
            ? "BASE STASH - READ ONLY"
            : "STASH - READ ONLY");

    const std::string countText =
        fmt::format(
            "{} STACKS / {} UNITS",
            gameSession_.stash().stackCount(),
            gameSession_.stash().unitCount());
    uiTextRenderer_.render(
        renderer_,
        kStashPanelX + 282.0F,
        kStashPanelY + 16.0F,
        countText.c_str());

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderBackground(bool drawOutdoorDetails)
{
    if (gameSession_.world().isAlphaRaidWorld() &&
        !gameSession_.world().inOutdoorRaidSpace())
    {
        SDL_SetRenderDrawColor(renderer_, 17, 20, 18, 255);
        SDL_RenderClear(renderer_);
        const Vec2 worldSize = gameSession_.world().raidSpaceWorldSize();
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 54, 59, 51, 75);
        for (float x = 0.0F; x <= worldSize.x; x += 64.0F)
        {
            SDL_RenderLine(renderer_, x, 0.0F, x, worldSize.y);
        }
        for (float y = 0.0F; y <= worldSize.y; y += 64.0F)
        {
            SDL_RenderLine(renderer_, 0.0F, y, worldSize.x, y);
        }
        const SDL_FRect room{1.0F, 1.0F,
                             worldSize.x - 2.0F, worldSize.y - 2.0F};
        SDL_SetRenderDrawColor(renderer_, 130, 119, 84, 255);
        SDL_RenderRect(renderer_, &room);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    const MapDefinition *map = &defaultV0MapDefinition();
    if (gameSession_.profile().pendingRaid.has_value())
    {
        map = &publishedContentRegistry().map(
            gameSession_.profile().pendingRaid->mapDefinitionId);
    }
    if (map->proceduralOutdoor.enabled &&
        gameSession_.world().inOutdoorRaidSpace() &&
        gameSession_.profile().pendingRaid.has_value() &&
        gameSession_.profile().pendingRaid->spatialLayout.layoutVersion >= 2U)
    {
        const SDL_FRect ground{
            map->walkableBounds.position.x,
            map->walkableBounds.position.y,
            map->walkableBounds.size.x,
            map->walkableBounds.size.y};
        SDL_SetRenderDrawColor(renderer_, 45, 49, 36, 255);
        SDL_RenderFillRect(renderer_, &ground);

        const float cellWidth = map->walkableBounds.size.x /
            static_cast<float>(map->proceduralOutdoor.columns);
        const float cellHeight = map->walkableBounds.size.y /
            static_cast<float>(map->proceduralOutdoor.rows);
        if (gameSession_.profile().pendingRaid->spatialLayout.layoutVersion >=
            3U)
        {
            if (!drawOutdoorDetails)
            {
                return;
            }
            const Vec2 camera = raidWorldCameraOffset();
            const RaidOutdoorPresentationProjection &projection =
                gameSession_.world().outdoorPresentation(ContentRect{
                    camera,
                    {static_cast<float>(kWindowWidth),
                     static_cast<float>(kWindowHeight)}});
            for (const RaidTerrainSpan &span : projection.terrainSpans)
            {
                switch (span.kind)
                {
                case RaidTerrainKind::Grass:
                    SDL_SetRenderDrawColor(renderer_, 54, 66, 42, 255);
                    break;
                case RaidTerrainKind::Concrete:
                    SDL_SetRenderDrawColor(renderer_, 76, 75, 68, 255);
                    break;
                case RaidTerrainKind::Dirt:
                    SDL_SetRenderDrawColor(renderer_, 84, 69, 49, 255);
                    break;
                case RaidTerrainKind::Asphalt:
                    SDL_SetRenderDrawColor(renderer_, 50, 52, 50, 255);
                    break;
                case RaidTerrainKind::Puddle:
                    SDL_SetRenderDrawColor(renderer_, 48, 70, 76, 255);
                    break;
                }
                const SDL_FRect terrain{
                    map->walkableBounds.position.x +
                        span.firstColumn * cellWidth,
                    map->walkableBounds.position.y + span.row * cellHeight,
                    span.length * cellWidth + 0.5F,
                    cellHeight + 0.5F};
                SDL_RenderFillRect(renderer_, &terrain);
            }
            for (const RaidOutdoorRoadCell &road : projection.roadCells)
            {
                const SDL_FRect roadBounds{
                    map->walkableBounds.position.x +
                        road.column * cellWidth,
                    map->walkableBounds.position.y + road.row * cellHeight,
                    cellWidth + 0.5F,
                    cellHeight + 0.5F};
                if (road.kind == RaidOutdoorRoadKind::Primary)
                    SDL_SetRenderDrawColor(renderer_, 91, 91, 83, 255);
                else if (road.kind == RaidOutdoorRoadKind::Secondary)
                    SDL_SetRenderDrawColor(renderer_, 76, 78, 72, 255);
                else
                    SDL_SetRenderDrawColor(renderer_, 66, 69, 62, 255);
                SDL_RenderFillRect(renderer_, &roadBounds);
            }
            SDL_SetRenderDrawColor(renderer_, 139, 127, 86, 220);
            SDL_RenderRect(renderer_, &ground);
            return;
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 82, 86, 62, 70);
        for (std::uint32_t column{};
             column <= map->proceduralOutdoor.columns;
             ++column)
        {
            const float x = map->walkableBounds.position.x +
                static_cast<float>(column) * cellWidth;
            SDL_RenderLine(renderer_, x, ground.y, x, ground.y + ground.h);
        }
        for (std::uint32_t row{}; row <= map->proceduralOutdoor.rows; ++row)
        {
            const float y = map->walkableBounds.position.y +
                static_cast<float>(row) * cellHeight;
            SDL_RenderLine(renderer_, ground.x, y, ground.x + ground.w, y);
        }
        for (const RaidOutdoorRoadCell &road :
             gameSession_.profile().pendingRaid->spatialLayout.roadCells)
        {
            const SDL_FRect roadBounds{
                map->walkableBounds.position.x + road.column * cellWidth + 2.0F,
                map->walkableBounds.position.y + road.row * cellHeight + 2.0F,
                cellWidth - 4.0F,
                cellHeight - 4.0F};
            if (road.kind == RaidOutdoorRoadKind::Primary)
            {
                SDL_SetRenderDrawColor(renderer_, 104, 103, 90, 255);
            }
            else if (road.kind == RaidOutdoorRoadKind::Secondary)
            {
                SDL_SetRenderDrawColor(renderer_, 88, 89, 77, 255);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer_, 73, 75, 65, 255);
            }
            SDL_RenderFillRect(renderer_, &roadBounds);
            SDL_SetRenderDrawColor(renderer_, 133, 126, 96, 150);
            SDL_RenderRect(renderer_, &roadBounds);
        }
        SDL_SetRenderDrawColor(renderer_, 139, 127, 86, 220);
        SDL_RenderRect(renderer_, &ground);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    SDL_SetTextureColorMod(
        backgroundTexture_.get(),
        map->backgroundTint.red,
        map->backgroundTint.green,
        map->backgroundTint.blue);
    SDL_RenderTexture(renderer_, backgroundTexture_.get(), nullptr, nullptr);
}

void App::renderExtractionPoint()
{
    if (!gameSession_.world().inOutdoorRaidSpace())
    {
        return;
    }
    const RaidSession &raidSession =
        gameSession_.world().raidSession();
    const bool extracted =
        raidSession.state() ==
        RaidSessionState::Extracted;

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    const auto renderZone =
        [&](const ExtractionPoint &point,
            RaidExtractionRoute route,
            bool open,
            const char *label,
            bool signal,
            const char *closedLabel)
    {
        const Rect &bounds = point.bounds();
        const SDL_FRect zone{
            bounds.position.x,
            bounds.position.y,
            bounds.size.x,
            bounds.size.y};
        const bool active =
            raidSession.extractionRoute() == route &&
            (raidSession.state() == RaidSessionState::Extracting || extracted);

        if (!open && !active)
        {
            SDL_SetRenderDrawColor(renderer_, 72, 30, 30, 54);
            SDL_RenderFillRect(renderer_, &zone);
            SDL_SetRenderDrawColor(renderer_, 138, 68, 68, 180);
            SDL_RenderRect(renderer_, &zone);
            uiTextRenderer_.render(
                renderer_,
                zone.x + 18.0F,
                zone.y + 16.0F,
                closedLabel);
            return;
        }

        SDL_SetRenderDrawColor(
            renderer_,
            signal ? 145 : 25,
            signal ? 94 : (active ? 155 : 92),
            signal ? 28 : 62,
            active ? 128 : 82);
        SDL_RenderFillRect(renderer_, &zone);
        SDL_SetRenderDrawColor(
            renderer_,
            signal ? 255 : (active ? 132 : 88),
            signal ? 196 : (active ? 245 : 188),
            signal ? 88 : (active ? 158 : 112),
            255);
        SDL_RenderRect(renderer_, &zone);
        uiTextRenderer_.render(
            renderer_,
            zone.x + 18.0F,
            zone.y + 16.0F,
            label);

        if (!active)
        {
            return;
        }

        const float progress = raidSession.extractionProgress();
        const SDL_FRect progressTrack{
            zone.x + 12.0F,
            zone.y + zone.h - 24.0F,
            zone.w - 24.0F,
            10.0F};
        SDL_SetRenderDrawColor(
            renderer_,
            10,
            30,
            18,
            190);
        SDL_RenderFillRect(
            renderer_,
            &progressTrack);

        const SDL_FRect progressFill{
            progressTrack.x,
            progressTrack.y,
            progressTrack.w * progress,
            progressTrack.h};

        SDL_SetRenderDrawColor(
            renderer_,
            signal ? 255 : 126,
            signal ? 202 : 235,
            signal ? 98 : 154,
            240);
        SDL_RenderFillRect(renderer_, &progressFill);

        const float timeRemaining =
            std::max(
                0.0F,
                raidSession.extractionDuration() -
                    raidSession.extractionTimeElapsed());

        const std::string extractionPrompt =
            extracted
                ? "EXTRACTED"
                : fmt::format(
                      "HOLD POSITION {:.1f}s",
                      timeRemaining);

        uiTextRenderer_.render(
            renderer_,
            zone.x + 22.0F,
            zone.y + 48.0F,
            extractionPrompt.c_str());
    };

    renderZone(
        gameSession_.world().extractionPoint(),
        RaidExtractionRoute::Normal,
        raidSession.normalExtractionOpen() ||
            raidSession.normalExtractionGraceActive(),
        raidSession.normalExtractionGraceActive()
            ? "NORMAL - GRACE"
            : "NORMAL EXTRACTION",
        false,
        "EXTRACTION CLOSED");

    if (const std::optional<ExtractionPoint> &emergency =
            gameSession_.world().emergencyExtractionPoint();
        emergency.has_value())
    {
        renderZone(
            *emergency,
            RaidExtractionRoute::EmergencySignal,
            raidSession.emergencyExtractionOpen(),
            "SIGNAL EXTRACTION",
            true,
            "SIGNAL LOCKED");
    }

    if (const std::optional<ExtractionPoint> &conditional =
            gameSession_.world().conditionalExtractionPoint();
        conditional.has_value())
    {
        const std::uint64_t weight =
            gameSession_.currentRaidCarriedWeightGrams();
        const std::uint64_t limit =
            gameSession_.conditionalExtractionWeightLimitGrams();
        const bool phaseOpen = raidSession.conditionalExtractionOpen();
        const bool eligible = gameSession_.conditionalExtractionEligible();
        const std::string label = fmt::format(
            "LIGHT EXIT {:.1f}/{:.1f} KG",
            static_cast<double>(weight) / 1000.0,
            static_cast<double>(limit) / 1000.0);
        const std::string closedLabel = phaseOpen
            ? fmt::format(
                  "TOO HEAVY {:.1f}/{:.1f} KG",
                  static_cast<double>(weight) / 1000.0,
                  static_cast<double>(limit) / 1000.0)
            : "LIGHT EXTRACTION LOCKED";
        renderZone(
            *conditional,
            RaidExtractionRoute::EmergencyConditional,
            phaseOpen && eligible,
            label.c_str(),
            false,
            closedLabel.c_str());
    }

    const bool highRisk = raidSession.phase() == RaidPhase::HighRisk;
    if (const auto &resourceArea =
            gameSession_.world().highRiskAdvancedResourceArea();
        resourceArea.has_value())
    {
        const SDL_FRect zone{resourceArea->position.x,
                             resourceArea->position.y,
                             resourceArea->size.x,
                             resourceArea->size.y};
        SDL_SetRenderDrawColor(renderer_,
                               highRisk ? 132 : 48,
                               highRisk ? 98 : 38,
                               highRisk ? 24 : 34,
                               highRisk ? 62 : 72);
        SDL_RenderFillRect(renderer_, &zone);
        SDL_SetRenderDrawColor(renderer_,
                               highRisk ? 244 : 126,
                               highRisk ? 190 : 72,
                               highRisk ? 68 : 72,
                               220);
        SDL_RenderRect(renderer_, &zone);
        uiTextRenderer_.render(renderer_,
                            zone.x + 12.0F,
                            zone.y + 12.0F,
                            highRisk ? "ADVANCED RESOURCE OPEN"
                                     : "ADVANCED RESOURCE LOCKED");
    }

    if (const auto &controlPoint = gameSession_.world().highRiskControlPoint();
        controlPoint.has_value())
    {
        const SDL_FRect control{controlPoint->position.x,
                                controlPoint->position.y,
                                controlPoint->size.x,
                                controlPoint->size.y};
        const bool inRange =
            gameSession_.world().highRiskControlInteractionInRange();
        SDL_SetRenderDrawColor(renderer_,
                               highRisk ? 80 : (inRange ? 168 : 106),
                               highRisk ? 54 : (inRange ? 112 : 72),
                               highRisk ? 42 : 28,
                               150);
        SDL_RenderFillRect(renderer_, &control);
        SDL_SetRenderDrawColor(renderer_,
                               highRisk ? 150 : 255,
                               highRisk ? 104 : 176,
                               highRisk ? 80 : 68,
                               255);
        SDL_RenderRect(renderer_, &control);
        uiTextRenderer_.render(
            renderer_,
            control.x + 6.0F,
            control.y + 8.0F,
            highRisk ? "HIGH RISK ACTIVE"
                     : (inRange ? "HOLD F: TRIGGER" : "HIGH RISK CONTROL"));

        const float progress = gameSession_.world().highRiskControlProgress();
        if (!highRisk && progress > 0.0F)
        {
            const SDL_FRect track{control.x + 6.0F,
                                  control.y + control.h - 16.0F,
                                  control.w - 12.0F,
                                  8.0F};
            SDL_SetRenderDrawColor(renderer_, 32, 20, 16, 220);
            SDL_RenderFillRect(renderer_, &track);
            const SDL_FRect fill{track.x, track.y, track.w * progress, track.h};
            SDL_SetRenderDrawColor(renderer_, 255, 176, 68, 245);
            SDL_RenderFillRect(renderer_, &fill);
            const std::string remaining = fmt::format(
                "{:.1f}s", gameSession_.world().highRiskControlTimeRemaining());
            uiTextRenderer_.render(renderer_,
                                control.x + 8.0F,
                                control.y + 30.0F,
                                remaining.c_str());
        }
    }

    if (const auto &rescuePoint =
            gameSession_.world().ordinarySurvivorRescuePoint();
        rescuePoint.has_value())
    {
        const SDL_FRect rescue{
            rescuePoint->position.x,
            rescuePoint->position.y,
            rescuePoint->size.x,
            rescuePoint->size.y};
        const bool inRange =
            gameSession_.world().ordinarySurvivorRescueInteractionInRange();
        const auto plan = gameSession_.ordinarySurvivorRescuePlan();
        const bool secured = plan.has_value() && plan->alreadyCommitted;
        SDL_SetRenderDrawColor(
            renderer_,
            secured ? 48 : (inRange ? 46 : 34),
            secured ? 112 : (inRange ? 154 : 92),
            secured ? 88 : (inRange ? 132 : 78),
            150);
        SDL_RenderFillRect(renderer_, &rescue);
        SDL_SetRenderDrawColor(
            renderer_,
            secured ? 116 : 116,
            secured ? 224 : 246,
            secured ? 174 : 206,
            255);
        SDL_RenderRect(renderer_, &rescue);
        uiTextRenderer_.render(
            renderer_,
            rescue.x + 7.0F,
            rescue.y + 7.0F,
            secured
                ? "SAFE TRANSFER COMPLETE"
                : (inRange
                       ? "HOLD F: SECURE TRANSFER"
                       : "ORDINARY SURVIVOR"));

        if (plan.has_value())
        {
            const std::string population = fmt::format(
                "RESIDENTS {} -> {} | BEDS {}",
                plan->residentsBefore,
                plan->residentsAfter,
                plan->bedCapacity);
            uiTextRenderer_.render(
                renderer_,
                rescue.x + 7.0F,
                rescue.y + 29.0F,
                population.c_str());
            const std::string rations = fmt::format(
                "RATIONS {}/DAY",
                plan->dailyRationsAfter);
            uiTextRenderer_.render(
                renderer_,
                rescue.x + 7.0F,
                rescue.y + 48.0F,
                rations.c_str());
            if (plan->bedShortfallAfter > 0U)
            {
                const std::string warning = fmt::format(
                    "WARNING: BED SHORTFALL +{}",
                    plan->bedShortfallAfter);
                uiTextRenderer_.render(
                    renderer_,
                    rescue.x + 7.0F,
                    rescue.y + 67.0F,
                    warning.c_str());
            }
        }

        const float progress =
            gameSession_.world().ordinarySurvivorRescueProgress();
        if (!secured && progress > 0.0F)
        {
            const SDL_FRect track{
                rescue.x + 7.0F,
                rescue.y + rescue.h - 15.0F,
                rescue.w - 14.0F,
                8.0F};
            SDL_SetRenderDrawColor(renderer_, 14, 34, 28, 220);
            SDL_RenderFillRect(renderer_, &track);
            const SDL_FRect fill{
                track.x, track.y, track.w * progress, track.h};
            SDL_SetRenderDrawColor(renderer_, 116, 246, 206, 245);
            SDL_RenderFillRect(renderer_, &fill);
        }
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderStorageCabinet()
{
    const StorageCabinet &cabinet =
        gameSession_.world().storageCabinet();
    const Rect bounds = cabinet.bounds();

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    const SDL_FRect shadow{
        bounds.position.x + 7.0F,
        bounds.position.y + 9.0F,
        bounds.size.x,
        bounds.size.y};
    SDL_SetRenderDrawColor(renderer_, 18, 13, 10, 120);
    SDL_RenderFillRect(renderer_, &shadow);

    const SDL_FRect body{
        bounds.position.x,
        bounds.position.y,
        bounds.size.x,
        bounds.size.y};
    SDL_SetRenderDrawColor(renderer_, 92, 62, 40, 255);
    SDL_RenderFillRect(renderer_, &body);

    const SDL_FRect inset{
        body.x + 8.0F,
        body.y + 10.0F,
        body.w - 16.0F,
        body.h - 20.0F};
    SDL_SetRenderDrawColor(renderer_, 54, 39, 29, 255);
    SDL_RenderFillRect(renderer_, &inset);

    SDL_SetRenderDrawColor(renderer_, 132, 91, 54, 255);
    SDL_RenderLine(
        renderer_,
        body.x + body.w / 2.0F,
        inset.y,
        body.x + body.w / 2.0F,
        inset.y + inset.h);
    SDL_RenderLine(
        renderer_,
        inset.x,
        inset.y + inset.h / 2.0F,
        inset.x + inset.w,
        inset.y + inset.h / 2.0F);

    const SDL_FRect handle{
        body.x + body.w / 2.0F - 2.0F,
        body.y + body.h / 2.0F - 8.0F,
        4.0F,
        16.0F};
    SDL_SetRenderDrawColor(renderer_, 205, 174, 92, 255);
    SDL_RenderFillRect(renderer_, &handle);

    SDL_SetRenderDrawColor(
        renderer_,
        gameSession_.world().canInteractWithContainer() ? 225 : 45,
        gameSession_.world().canInteractWithContainer() ? 205 : 32,
        gameSession_.world().canInteractWithContainer() ? 115 : 25,
        255);
    SDL_RenderRect(renderer_, &body);

    if (gameSession_.world().canInteractWithContainer() &&
        !inventoryOverlayState_.isOpen())
    {
        uiTextRenderer_.render(
            renderer_,
            body.x - 12.0F,
            body.y - 20.0F,
            cabinet.isSearched()
                ? "F: OPEN CABINET"
                : "F: SEARCH CABINET");
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderShotPresentations()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (
        const ShotPresentationSnapshot &shot :
        gameSession_.world().shotPresentationSnapshots())
    {
        if (shot.tracerStyle == TracerStyle::None)
        {
            continue;
        }
        const Vec2 travelled{
            shot.end.x - shot.start.x,
            shot.end.y - shot.start.y};
        const float trailLength = std::hypot(travelled.x, travelled.y);
        if (trailLength <= 0.0001F)
        {
            continue;
        }

        const Uint8 opacity = static_cast<Uint8>(std::clamp(
            std::lround(shot.tracerOpacity * 255.0F),
            0L,
            255L));
        const Vec2 normal{
            -travelled.y / trailLength,
            travelled.x / trailLength};
        const auto drawLine = [&](Vec2 start, Vec2 end,
                                  Uint8 red, Uint8 green, Uint8 blue,
                                  float alphaScale)
        {
            SDL_SetRenderDrawColor(
                renderer_,
                red,
                green,
                blue,
                static_cast<Uint8>(std::clamp(
                    std::lround(
                        static_cast<float>(opacity) * alphaScale),
                    0L,
                    255L)));
            SDL_RenderLine(renderer_, start.x, start.y, end.x, end.y);
        };
        const auto drawOffsetLine = [&drawLine, &normal, &shot](
            float offset,
            Uint8 red,
            Uint8 green,
            Uint8 blue,
            float alphaScale)
        {
            const Vec2 shiftedStart{
                shot.start.x + normal.x * offset,
                shot.start.y + normal.y * offset};
            const Vec2 shiftedEnd{
                shot.end.x + normal.x * offset,
                shot.end.y + normal.y * offset};
            drawLine(
                shiftedStart,
                shiftedEnd,
                red,
                green,
                blue,
                alphaScale);
        };

        // One continuous, uniformly thick five-pixel streak: bright yellow
        // edges around a white-hot center. Flicker is already encoded in the
        // read-only opacity projection; this remains collision-free rendering.
        drawOffsetLine(-2.0F, 255, 205, 48, 0.68F);
        drawOffsetLine(2.0F, 255, 205, 48, 0.68F);
        drawOffsetLine(-1.0F, 255, 238, 150, 0.88F);
        drawOffsetLine(1.0F, 255, 238, 150, 0.88F);
        drawOffsetLine(0.0F, 255, 255, 244, 1.00F);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

Vec2 App::raidWorldCameraOffset() const noexcept
{
    if (!gameFlow_.isRaidScreen() ||
        !gameSession_.world().raidSession().isActive() ||
        !gameSession_.world().inOutdoorRaidSpace())
    {
        return {};
    }
    const Player &player = gameSession_.world().player();
    const Vec2 focus{
        player.position().x + player.size() * 0.5F,
        player.position().y + player.size() * 0.5F};
    return ::raidCameraOffset(
        focus,
        gameSession_.world().raidSpaceWorldSize(),
        {static_cast<float>(kWindowWidth),
         static_cast<float>(kWindowHeight)});
}

Vec2 App::raidWorldScreenShakePixels() const noexcept
{
    const Vec2 normalized =
        gameSession_.world().normalizedShotScreenShakeOffset();
    // Deliberately below the threshold that would displace the crosshair or
    // interfere with ordinary visual reading. Only the world viewport moves.
    return Vec2{normalized.x * 1.8F, normalized.y * 1.3F};
}

void App::renderShotFeedbackPresentations()
{
    constexpr int kLightSegments{20};
    constexpr float kTau{6.28318530718F};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const ShotFeedbackPresentationSnapshot &shot :
         gameSession_.world().shotFeedbackPresentationSnapshots())
    {
        const Vec2 normal{-shot.direction.y, shot.direction.x};

        if (shot.muzzleFlashIntensity > 0.0F)
        {
            // A low-alpha radial gradient briefly warms the area around the
            // player. Transparent outer vertices keep its boundary soft and
            // avoid a harsh full-screen flash.
            std::array<SDL_Vertex, kLightSegments + 1> vertices{};
            std::array<int, kLightSegments * 3> indices{};
            const Vec2 lightCenter{
                shot.origin.x - shot.direction.x * 12.0F,
                shot.origin.y - shot.direction.y * 12.0F};
            vertices[0] = SDL_Vertex{
                SDL_FPoint{lightCenter.x, lightCenter.y},
                SDL_FColor{
                    1.0F,
                    0.76F,
                    0.32F,
                    0.10F * shot.muzzleFlashIntensity},
                SDL_FPoint{}};
            constexpr float kSoftLightRadius{92.0F};
            for (int index{}; index < kLightSegments; ++index)
            {
                const float angle = kTau * static_cast<float>(index) /
                    static_cast<float>(kLightSegments);
                vertices[static_cast<std::size_t>(index) + 1U] = SDL_Vertex{
                    SDL_FPoint{
                        lightCenter.x + std::cos(angle) * kSoftLightRadius,
                        lightCenter.y + std::sin(angle) * kSoftLightRadius},
                    SDL_FColor{1.0F, 0.72F, 0.24F, 0.0F},
                    SDL_FPoint{}};
                indices[static_cast<std::size_t>(index) * 3U] = 0;
                indices[static_cast<std::size_t>(index) * 3U + 1U] =
                    index + 1;
                indices[static_cast<std::size_t>(index) * 3U + 2U] =
                    (index + 1) % kLightSegments + 1;
            }
            static_cast<void>(SDL_RenderGeometry(
                renderer_,
                nullptr,
                vertices.data(),
                static_cast<int>(vertices.size()),
                indices.data(),
                static_cast<int>(indices.size())));

            const float length =
                10.0F + 10.0F * shot.muzzleFlashIntensity;
            const Vec2 tip{
                shot.origin.x + shot.direction.x * length,
                shot.origin.y + shot.direction.y * length};
            const Uint8 flashAlpha = static_cast<Uint8>(std::clamp(
                std::lround(255.0F * shot.muzzleFlashIntensity),
                0L,
                255L));
            const auto drawFlashLine = [&](float offset, float lengthScale,
                                           Uint8 red, Uint8 green, Uint8 blue,
                                           float alphaScale)
            {
                SDL_SetRenderDrawColor(
                    renderer_,
                    red,
                    green,
                    blue,
                    static_cast<Uint8>(std::clamp(
                        std::lround(
                            static_cast<float>(flashAlpha) * alphaScale),
                        0L,
                        255L)));
                SDL_RenderLine(
                    renderer_,
                    shot.origin.x + normal.x * offset,
                    shot.origin.y + normal.y * offset,
                    shot.origin.x +
                        (tip.x - shot.origin.x) * lengthScale -
                        normal.x * offset * 0.35F,
                    shot.origin.y +
                        (tip.y - shot.origin.y) * lengthScale -
                        normal.y * offset * 0.35F);
            };
            drawFlashLine(-2.0F, 0.72F, 255, 177, 42, 0.72F);
            drawFlashLine(2.0F, 0.72F, 255, 177, 42, 0.72F);
            drawFlashLine(-1.0F, 0.90F, 255, 230, 126, 0.90F);
            drawFlashLine(1.0F, 0.90F, 255, 230, 126, 0.90F);
            drawFlashLine(0.0F, 1.0F, 255, 255, 238, 1.0F);
        }

        if (shot.smokeOpacity > 0.0F)
        {
            const float lateralSign =
                (shot.shotId % 2U == 0U) ? 1.0F : -1.0F;
            for (int puff{}; puff < 3; ++puff)
            {
                const float puffOffset = static_cast<float>(puff);
                const float forward =
                    5.0F + shot.smokeProgress * 20.0F + puffOffset * 3.5F;
                const float lateral = lateralSign *
                    (2.0F + shot.smokeProgress * 5.0F + puffOffset * 1.5F);
                const float size =
                    3.0F + shot.smokeProgress * 8.0F + puffOffset * 1.2F;
                const SDL_FRect cloud{
                    shot.origin.x + shot.direction.x * forward +
                        normal.x * lateral - size * 0.5F,
                    shot.origin.y + shot.direction.y * forward +
                        normal.y * lateral - size * 0.5F,
                    size,
                    size};
                const float opacityScale = 0.74F - puffOffset * 0.16F;
                const Uint8 alpha = static_cast<Uint8>(std::clamp(
                    std::lround(
                        shot.smokeOpacity * opacityScale * 255.0F),
                    0L,
                    255L));
                SDL_SetRenderDrawColor(
                    renderer_, 174, 178, 170, alpha);
                SDL_RenderFillRect(renderer_, &cloud);
            }
        }
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBallisticBlockers()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (gameSession_.world().isAlphaRaidWorld() &&
        gameSession_.world().inOutdoorRaidSpace() &&
        gameSession_.profile().pendingRaid.has_value() &&
        gameSession_.profile().pendingRaid->spatialLayout.layoutVersion >= 3U)
    {
        const Vec2 camera = raidWorldCameraOffset();
        const ContentRect visibleWorldBounds{
            camera,
            {static_cast<float>(kWindowWidth),
             static_cast<float>(kWindowHeight)}};
        const RaidOutdoorPresentationProjection &projection =
            gameSession_.world().outdoorPresentation(visibleWorldBounds);
        const auto visible = [&visibleWorldBounds](ContentRect bounds)
        {
            return bounds.position.x <
                    visibleWorldBounds.position.x + visibleWorldBounds.size.x &&
                bounds.position.x + bounds.size.x >
                    visibleWorldBounds.position.x &&
                bounds.position.y <
                    visibleWorldBounds.position.y + visibleWorldBounds.size.y &&
                bounds.position.y + bounds.size.y >
                    visibleWorldBounds.position.y;
        };
        for (const RaidResourcePointSnapshot &resourcePoint :
             projection.resourcePoints)
        {
            if (!visible(resourcePoint.bounds))
                continue;
            const SDL_FRect bounds{
                resourcePoint.bounds.position.x,
                resourcePoint.bounds.position.y,
                resourcePoint.bounds.size.x,
                resourcePoint.bounds.size.y};
            if (resourcePoint.kind == RaidResourcePointKind::HighValue)
                SDL_SetRenderDrawColor(renderer_, 218, 178, 58, 52);
            else if (resourcePoint.kind ==
                     RaidResourcePointKind::LandmarkSpecific)
                SDL_SetRenderDrawColor(renderer_, 86, 176, 190, 48);
            else
                SDL_SetRenderDrawColor(renderer_, 92, 158, 102, 42);
            SDL_RenderFillRect(renderer_, &bounds);
            if (resourcePoint.kind == RaidResourcePointKind::HighValue)
                SDL_SetRenderDrawColor(renderer_, 236, 198, 80, 220);
            else if (resourcePoint.kind ==
                     RaidResourcePointKind::LandmarkSpecific)
                SDL_SetRenderDrawColor(renderer_, 118, 214, 224, 210);
            else
                SDL_SetRenderDrawColor(renderer_, 126, 196, 132, 190);
            SDL_RenderRect(renderer_, &bounds);
            const std::string label = resourcePoint.displayName +
                " | RISK TIER " + std::to_string(resourcePoint.riskTier);
            uiTextRenderer_.render(
                renderer_, bounds.x + 4.0F, bounds.y + 4.0F,
                label.c_str());
        }
        for (const RaidOutdoorPropSnapshot &prop : projection.props)
        {
            if (!visible(prop.bounds))
                continue;
            const SDL_FRect bounds{
                prop.bounds.position.x,
                prop.bounds.position.y,
                prop.bounds.size.x,
                prop.bounds.size.y};
            switch (prop.kind)
            {
            case RaidOutdoorPropKind::Factory:
                SDL_SetRenderDrawColor(renderer_, 62, 66, 62, 242);
                break;
            case RaidOutdoorPropKind::Warehouse:
                SDL_SetRenderDrawColor(renderer_, 69, 67, 57, 242);
                break;
            case RaidOutdoorPropKind::Container:
                SDL_SetRenderDrawColor(renderer_, 91, 72, 48, 235);
                break;
            case RaidOutdoorPropKind::EngineeringEquipment:
                SDL_SetRenderDrawColor(renderer_, 112, 89, 43, 235);
                break;
            case RaidOutdoorPropKind::Car:
                SDL_SetRenderDrawColor(renderer_, 69, 78, 73, 235);
                break;
            case RaidOutdoorPropKind::Truck:
                SDL_SetRenderDrawColor(renderer_, 79, 76, 61, 235);
                break;
            case RaidOutdoorPropKind::RoadBarrier:
                SDL_SetRenderDrawColor(renderer_, 119, 103, 70, 235);
                break;
            case RaidOutdoorPropKind::Debris:
                SDL_SetRenderDrawColor(renderer_, 97, 91, 75, 160);
                break;
            }
            SDL_RenderFillRect(renderer_, &bounds);
            if (prop.collidable)
            {
                SDL_SetRenderDrawColor(renderer_, 151, 134, 92, 230);
                SDL_RenderRect(renderer_, &bounds);
            }
            SDL_SetRenderDrawColor(renderer_, 226, 218, 176, 245);
            uiTextRenderer_.render(
                renderer_, bounds.x + 3.0F,
                prop.collidable ? bounds.y + 3.0F : bounds.y - 14.0F,
                raidOutdoorPropLabel(prop.kind));
        }
        SDL_SetRenderDrawColor(renderer_, 226, 218, 176, 245);
        for (const RaidOutdoorLabelProjection &label : projection.labels)
        {
            if (label.position.x < visibleWorldBounds.position.x ||
                label.position.y < visibleWorldBounds.position.y ||
                label.position.x > visibleWorldBounds.position.x +
                    visibleWorldBounds.size.x ||
                label.position.y > visibleWorldBounds.position.y +
                    visibleWorldBounds.size.y)
                continue;
            uiTextRenderer_.render(
                renderer_, label.position.x, label.position.y,
                label.text.c_str());
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    for (const BallisticBlocker &blocker :
         gameSession_.world().ballisticBlockers())
    {
        const SDL_FRect bounds{
            blocker.bounds.position.x,
            blocker.bounds.position.y,
            blocker.bounds.size.x,
            blocker.bounds.size.y};
        SDL_SetRenderDrawColor(renderer_, 44, 48, 43, 235);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 126, 112, 82, 255);
        SDL_RenderRect(renderer_, &bounds);

        const SDL_FRect inset{
            bounds.x + 4.0F,
            bounds.y + 4.0F,
            std::max(0.0F, bounds.w - 8.0F),
            std::max(0.0F, bounds.h - 8.0F)};
        SDL_SetRenderDrawColor(renderer_, 71, 76, 67, 220);
        SDL_RenderRect(renderer_, &inset);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::syncRaidPointerCapture() noexcept
{
    const bool shouldCapture = shouldCaptureRaidPointer(
        RaidPointerCaptureContext{
            gameFlow_.isRaidScreen(),
            gameSession_.world().raidSession().isActive(),
            inventoryOverlayState_.isOpen(),
            medicalWheelOpen_,
            developerWeaponPanelOpen_,
            pauseMenu_.isOpen(),
            tacticalMapOpen_,
            windowHasInputFocus_});

    if (shouldCapture != relativeMouseModeActive_)
    {
        if (SDL_SetWindowRelativeMouseMode(window_, shouldCapture))
        {
            relativeMouseModeActive_ = shouldCapture;
            pendingRelativeAimMotion_ = Vec2{};
        }
    }

    if (relativeMouseModeActive_ && !systemCursorHidden_)
    {
        if (SDL_HideCursor())
        {
            systemCursorHidden_ = true;
        }
        return;
    }

    if (!relativeMouseModeActive_ && systemCursorHidden_ && SDL_ShowCursor())
    {
        systemCursorHidden_ = false;
    }
}

void App::renderAimCrosshair()
{
    if (inventoryOverlayState_.isOpen() ||
        medicalWheelOpen_ ||
        tacticalMapOpen_ ||
        developerWeaponPanelOpen_ ||
        pauseMenu_.isOpen() ||
        !gameSession_.world().raidSession().isActive())
    {
        return;
    }

    const WeaponAccuracyProjection accuracy =
        gameSession_.world().weaponAccuracyProjection();
    const float feedbackRadius = std::round(std::max(
        10.0F,
        accuracy.reticleRadius));
    constexpr float kArmLength{15.0F};
    const Vec2 screenCenter = raidWorldToScreen(
        accuracy.center, raidWorldCameraOffset());
    const Vec2 center{
        std::round(screenCenter.x),
        std::round(screenCenter.y)};

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (accuracy.beyondEffectiveRange)
    {
        SDL_SetRenderDrawColor(renderer_, 232, 62, 52, 235);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer_, 235, 240, 225, 220);
    }
    const auto drawHorizontalArm = [&](float startX, float endX)
    {
        for (const float offset : {-1.0F, 0.0F, 1.0F})
        {
            SDL_RenderLine(
                renderer_, startX, center.y + offset,
                endX, center.y + offset);
        }
    };
    const auto drawVerticalArm = [&](float startY, float endY)
    {
        for (const float offset : {-1.0F, 0.0F, 1.0F})
        {
            SDL_RenderLine(
                renderer_, center.x + offset, startY,
                center.x + offset, endY);
        }
    };
    drawHorizontalArm(
        center.x - feedbackRadius - kArmLength,
        center.x - feedbackRadius);
    drawHorizontalArm(
        center.x + feedbackRadius,
        center.x + feedbackRadius + kArmLength);
    drawVerticalArm(
        center.y - feedbackRadius - kArmLength,
        center.y - feedbackRadius);
    drawVerticalArm(
        center.y + feedbackRadius,
        center.y + feedbackRadius + kArmLength);
    if (specialHitFeedbackRemaining_ > 0.0F)
    {
        const Uint8 red = specialHitSemantic_ == HitSemantic::WeakPoint
            ? 246
            : 250;
        const Uint8 green = specialHitSemantic_ == HitSemantic::WeakPoint
            ? 92
            : 194;
        const Uint8 blue = specialHitSemantic_ == HitSemantic::WeakPoint
            ? 92
            : 72;
        SDL_SetRenderDrawColor(renderer_, red, green, blue, 245);
        constexpr float kHitMarkHalfExtent{5.0F};
        SDL_RenderLine(
            renderer_,
            center.x - kHitMarkHalfExtent,
            center.y - kHitMarkHalfExtent,
            center.x + kHitMarkHalfExtent,
            center.y + kHitMarkHalfExtent);
        SDL_RenderLine(
            renderer_,
            center.x + kHitMarkHalfExtent,
            center.y - kHitMarkHalfExtent,
            center.x - kHitMarkHalfExtent,
            center.y + kHitMarkHalfExtent);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::openMedicalWheel()
{
    medicalWheelOptions_.clear();
    medicalWheelSelectedIndex_ = 0;
    const ProfileState &profile = gameSession_.profile();
    const auto chest = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    if (!chest.has_value())
    {
        uiMessage_ = "NO CHEST RIG MEDICAL ITEM";
        return;
    }

    std::array<const AssetRecord *, 4> best{};
    const AssetRecord *chestAsset = profile.assets.find(*chest);
    if (chestAsset == nullptr)
    {
        return;
    }
    const ItemDefinition &chestDefinition = publishedContentRegistry().item(
        chestAsset->definitionId);
    for (std::size_t compartment = 0;
         compartment < chestDefinition.containerCompartments.size();
         ++compartment)
    {
        if (chestDefinition.containerCompartments[compartment].pocketKind !=
            ContainerPocketKind::General)
        {
            continue;
        }
        for (const AssetRecord *asset : assetsInContainer(
                 profile,
                 ProfileContainerId::compartment(
                     *chest,
                     static_cast<std::uint32_t>(compartment))))
        {
            const ItemDefinition &definition = publishedContentRegistry().item(
                asset->definitionId);
            if (!definition.medicalUse.has_value() ||
                asset->remainingCharges == 0)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(
                definition.medicalUse->effect);
            if (index >= best.size())
            {
                continue;
            }
            if (best[index] == nullptr ||
                asset->remainingCharges < best[index]->remainingCharges ||
                (asset->remainingCharges == best[index]->remainingCharges &&
                 asset->instanceId < best[index]->instanceId))
            {
                best[index] = asset;
            }
        }
    }
    for (const AssetRecord *asset : best)
    {
        if (asset != nullptr)
        {
            medicalWheelOptions_.push_back(asset->instanceId);
        }
    }
    medicalWheelOpen_ = !medicalWheelOptions_.empty();
    if (!medicalWheelOpen_)
    {
        uiMessage_ = "NO CHEST RIG MEDICAL ITEM";
    }
}

void App::updateMedicalWheelSelection()
{
    if (!medicalWheelOpen_ || medicalWheelOptions_.empty() ||
        !pointerWorldPosition_.has_value())
    {
        return;
    }
    constexpr Vec2 center{640.0F, 360.0F};
    const float dx = pointerWorldPosition_->x - center.x;
    const float dy = pointerWorldPosition_->y - center.y;
    if (dx * dx + dy * dy < 30.0F * 30.0F)
    {
        return;
    }
    constexpr float pi = 3.14159265358979323846F;
    float angle = std::atan2(dy, dx) + pi;
    const float sector = 2.0F * pi /
        static_cast<float>(medicalWheelOptions_.size());
    medicalWheelSelectedIndex_ = static_cast<std::size_t>(
        std::floor((angle + sector * 0.5F) / sector)) %
        medicalWheelOptions_.size();
}

void App::commitMedicalWheelSelection()
{
    if (!medicalWheelOpen_ || medicalWheelOptions_.empty())
    {
        medicalWheelOpen_ = false;
        return;
    }
    const AssetInstanceId selected =
        medicalWheelOptions_[medicalWheelSelectedIndex_];
    medicalWheelOpen_ = false;
    medicalWheelOptions_.clear();
    if (gameSession_.startAlphaMedical(selected))
    {
        uiMessage_ = "MEDICAL ACTION STARTED";
    }
    else
    {
        uiMessage_ = "SELECTED MEDICAL ITEM IS NOT APPLICABLE";
    }
}

void App::renderCombatFeedback()
{
    if (playerDamageFeedbackRemaining_ <= 0.0F ||
        !gameSession_.world().raidSession().isActive())
    {
        return;
    }

    const float normalized = std::clamp(
        playerDamageFeedbackRemaining_ / 0.28F,
        0.0F,
        1.0F);
    const Uint8 alpha = static_cast<Uint8>(80.0F + normalized * 120.0F);
    if (lastIncomingDamageReducedByArmor_)
    {
        SDL_SetRenderDrawColor(renderer_, 70, 160, 202, alpha);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer_, 196, 42, 42, alpha);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    constexpr float kEdgeThickness{14.0F};
    const SDL_FRect top{0.0F, 0.0F, 1280.0F, kEdgeThickness};
    const SDL_FRect bottom{0.0F, 720.0F - kEdgeThickness, 1280.0F, kEdgeThickness};
    const SDL_FRect left{0.0F, 0.0F, kEdgeThickness, 720.0F};
    const SDL_FRect right{1280.0F - kEdgeThickness, 0.0F, kEdgeThickness, 720.0F};
    SDL_RenderFillRect(renderer_, &top);
    SDL_RenderFillRect(renderer_, &bottom);
    SDL_RenderFillRect(renderer_, &left);
    SDL_RenderFillRect(renderer_, &right);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    if (lastIncomingDamageReducedByArmor_)
    {
        uiTextRenderer_.render(renderer_, 594.0F, 42.0F, "ARMOR HIT");
    }
}

void App::renderGroundItems()
{
    for (
        const GroundItem &groundItem :
        gameSession_.world().groundItems())
    {
        const ItemInstance &item =
            groundItem.item();

        if (!item.valid())
        {
            continue;
        }

        const ItemDefinition &definition =
            itemDefinition(
                item.definitionId());

        const std::size_t textureIndex =
            static_cast<std::size_t>(
                definition.id);

        const Texture &texture =
            worldItemTextures_[textureIndex];

        if (!texture.valid())
        {
            continue;
        }

        const Vec2 center =
            groundItem.position();

        const Vec2 renderSize =
            orientedSize(
                definition.worldRenderSize,
                item.orientation());

        SDL_FRect destination{
            center.x -
                renderSize.x / 2.0f,
            center.y -
                renderSize.y / 2.0f,
            renderSize.x,
            renderSize.y};

        renderOrientedTexture(
            renderer_,
            texture.get(),
            destination,
            definition.worldRenderSize.x,
            definition.worldRenderSize.y,
            item.orientation());

        renderItemQuantityBadge(
            renderer_,
            destination,
            item.quantity());
    }
}

void App::renderPlayerAvatar(
    Vec2 position,
    Vec2 bodySize,
    Vec2 facingDirection,
    bool moving,
    std::size_t animationFrame)
{
    const float spriteW = kPlayerSpriteWidth;
    const float spriteH = kPlayerSpriteHeight;
    // The camera is committed through an integer SDL viewport. Align the
    // player to the same pixel grid so fractional simulation positions cannot
    // make the centered avatar oscillate by one pixel against the world.
    const float spriteX = std::round(
        position.x + (bodySize.x - spriteW) / 2.0F);
    const float spriteY = std::round(
        position.y + (bodySize.y - spriteH) / 2.0F);

    SDL_FRect playerRect{
        spriteX,
        spriteY,
        spriteW,
        spriteH};
    if (!moving && facingDirection.x <= 0.0F)
    {
        SDL_RenderTexture(
            renderer_,
            playerTexture_.get(),
            nullptr,
            &playerRect);
    }
    else
    {
        std::size_t frameIndex = moving ? animationFrame : 0U;
        if (frameIndex >= kPlayerMoveFrameCount)
        {
            frameIndex = 0;
        }
        const float sourceX =
            static_cast<float>(frameIndex) *
            kPlayerMoveSourceFrameWidth;
        const float sourceY =
            facingDirection.x < 0.0f
                ? kPlayerMoveLeftRowY
                : kPlayerMoveRightRowY;
        SDL_FRect sourceRect{
            sourceX,
            sourceY,
            kPlayerMoveSourceFrameWidth,
            kPlayerMoveSourceFrameHeight};
        SDL_RenderTexture(
            renderer_,
            playerMoveHorizontalTexture_.get(),
            &sourceRect,
            &playerRect);
    }
}

void App::renderPlayerPreview(const SDL_FRect &bounds)
{
    const float height = std::min(bounds.h, 250.0F);
    const float width = height *
        (kPlayerMoveSourceFrameWidth / kPlayerMoveSourceFrameHeight);
    const SDL_FRect preview{
        bounds.x + (bounds.w - width) / 2.0F,
        bounds.y + (bounds.h - height) / 2.0F,
        width,
        height};
    SDL_RenderTexture(renderer_, playerTexture_.get(), nullptr, &preview);
}

void App::renderPlayer()
{
    const Player &player = gameSession_.world().player();
    const Vec2 logicPos = player.position();
    const float logicSize = player.size();
    renderPlayerAvatar(
        logicPos,
        Vec2{logicSize, logicSize},
        player.facingDirection(),
        player.isMoving(),
        player.currentAnimationFrameIndex());

    if (player.isImpactSlowed())
    {
        SDL_SetRenderDrawColor(
            renderer_,
            255U,
            196U,
            72U,
            255U);
        const SDL_FRect playerRect{
            logicPos.x + (logicSize - kPlayerSpriteWidth) / 2.0F,
            logicPos.y + (logicSize - kPlayerSpriteHeight) / 2.0F,
            static_cast<float>(kPlayerSpriteWidth),
            static_cast<float>(kPlayerSpriteHeight)};
        SDL_RenderRect(renderer_, &playerRect);
    }
}

void App::renderEnemies()
{
    const std::vector<Enemy> &enemies =
        gameSession_.world().enemies();
    for (std::size_t enemyIndex{0U};
         enemyIndex < enemies.size();
         ++enemyIndex)
    {
        const Enemy &enemy = enemies[enemyIndex];
        const Rect bounds = enemy.bounds();
        const float spriteX =
            bounds.position.x +
            (bounds.size.x - kEnemySpriteWidth) / 2.0f;

        const float spriteY =
            bounds.position.y +
            (bounds.size.y - kEnemySpriteHeight) / 2.0f;

        SDL_FRect enemyRect{
            spriteX,
            spriteY,
            kEnemySpriteWidth,
            kEnemySpriteHeight};
        std::size_t frameIndex =
            enemy.currentAnimationFrameIndex();
        if (frameIndex >= kEnemyMoveFrameCount)
        {
            frameIndex = 0;
        }
        const float sourceY =
            enemy.facingDirection() ==
                    EnemyFacingDirection::Left
                ? kEnemyMoveLeftRowY
                : kEnemyMoveRightRowY;
        const float sourceX =
            static_cast<float>(frameIndex) *
            kEnemyMoveSourceFrameWidth;
        SDL_FRect sourceRect{
            sourceX,
            sourceY,
            kEnemyMoveSourceFrameWidth,
            kEnemyMoveSourceFrameHeight};
        if (enemy.attackPhase() == EnemyAttackPhase::OffBalance)
        {
            SDL_RenderTextureRotated(
                renderer_,
                enemyMoveHorizontalTexture_.get(),
                &sourceRect,
                &enemyRect,
                90.0,
                nullptr,
                SDL_FLIP_NONE);
        }
        else
        {
            SDL_RenderTexture(
                renderer_,
                enemyMoveHorizontalTexture_.get(),
                &sourceRect,
                &enemyRect);
        }

        if (enemy.isImpactSlowed())
        {
            SDL_SetRenderDrawColor(
                renderer_,
                255U,
                196U,
                72U,
                255U);
            SDL_RenderRect(renderer_, &enemyRect);
        }
        else if (!enemy.isDead())
        {
            switch (enemy.awarenessState())
            {
            case EnemyAwarenessState::Unaware:
                SDL_SetRenderDrawColor(
                    renderer_, 120U, 132U, 146U, 220U);
                break;
            case EnemyAwarenessState::Alerted:
                if (enemy.tacticalRole() == EnemyTacticalRole::Engage)
                {
                    SDL_SetRenderDrawColor(
                        renderer_, 255U, 76U, 60U, 255U);
                }
                else
                {
                    SDL_SetRenderDrawColor(
                        renderer_, 72U, 190U, 255U, 235U);
                }
                break;
            case EnemyAwarenessState::Searching:
                SDL_SetRenderDrawColor(
                    renderer_, 255U, 152U, 48U, 240U);
                break;
            }
            SDL_RenderRect(renderer_, &enemyRect);
        }

        // Per-enemy text causes texture-cache and draw-call pressure exactly
        // when enemy density is highest. Keep a bounded sample available as
        // diagnostics only while the explicit performance overlay is open.
        if (!enemy.isDead() && developerPerformanceOverlayOpen_ &&
            enemyIndex < 16U)
        {
            const std::string enemyStateText = fmt::format(
                "E{} {} {}",
                enemyIndex + 1U,
                enemyAwarenessStateName(
                    enemy.awarenessState()),
                enemyTacticalRoleName(
                    enemy.tacticalRole()));
            uiTextRenderer_.render(
                renderer_,
                enemyRect.x,
                enemyRect.y - 10.0F,
                enemyStateText.c_str());
        }
    }
}

void App::renderEnemyAttackTelegraphs()
{
    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);

    for (const Enemy &enemy :
         gameSession_.world().enemies())
    {
        const std::optional<EnemyAttackType> attackType =
            enemy.attackType();
        if (!attackType.has_value())
        {
            continue;
        }

        const EnemyAttackPhase phase =
            enemy.attackPhase();
        const std::optional<Rect> attackRegion =
            phase == EnemyAttackPhase::Active
                ? enemy.attackHitbox()
                : enemy.attackTelegraphBounds();
        if (!attackRegion.has_value())
        {
            continue;
        }

        Uint8 red{255U};
        Uint8 green{196U};
        Uint8 blue{64U};
        switch (*attackType)
        {
        case EnemyAttackType::Grab:
            red = 30U;
            green = 210U;
            blue = 255U;
            break;
        case EnemyAttackType::Scratch:
            break;
        case EnemyAttackType::Bite:
            red = 255U;
            green = 62U;
            blue = 132U;
            break;
        }

        if (phase == EnemyAttackPhase::OffBalance)
        {
            red = 255U;
            green = 92U;
            blue = 48U;
        }

        Uint8 fillAlpha{50U};
        Uint8 borderAlpha{235U};
        if (phase == EnemyAttackPhase::Active)
        {
            fillAlpha = 112U;
            borderAlpha = 255U;
        }
        else if (phase == EnemyAttackPhase::Recovery)
        {
            fillAlpha = 18U;
            borderAlpha = 90U;
        }
        else if (phase == EnemyAttackPhase::OffBalance)
        {
            fillAlpha = 36U;
            borderAlpha = 220U;
        }

        const SDL_FRect region{
            attackRegion->position.x,
            attackRegion->position.y,
            attackRegion->size.x,
            attackRegion->size.y};
        SDL_SetRenderDrawColor(
            renderer_,
            red,
            green,
            blue,
            fillAlpha);
        SDL_RenderFillRect(renderer_, &region);
        SDL_SetRenderDrawColor(
            renderer_,
            red,
            green,
            blue,
            borderAlpha);
        SDL_RenderRect(renderer_, &region);

        const Rect enemyBounds = enemy.bounds();
        const Vec2 direction = enemy.attackDirection();
        const float centerX =
            enemyBounds.position.x + enemyBounds.size.x / 2.0F;
        const float centerY =
            enemyBounds.position.y + enemyBounds.size.y / 2.0F;
        if (phase != EnemyAttackPhase::OffBalance)
        {
            SDL_RenderLine(
                renderer_,
                centerX,
                centerY,
                centerX + direction.x * 72.0F,
                centerY + direction.y * 72.0F);
        }

        const std::string label = fmt::format(
            "{} {}",
            enemyAttackTypeName(*attackType),
            enemyAttackPhaseName(phase));
        uiTextRenderer_.render(
            renderer_,
            enemyBounds.position.x,
            enemyBounds.position.y - 14.0F,
            label.c_str());
    }

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderParticles()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    for (const Particle &particle : gameSession_.world().particles())
    {
        const float life = particle.normalizedLifetime();
        const Vec2 center = particle.position();
        const Vec2 velocity = particle.velocity();
        const float speed = std::sqrt(
            velocity.x * velocity.x +
            velocity.y * velocity.y);
        const Vec2 direction =
            std::isfinite(speed) && speed > 0.0F
                                   ? Vec2{
                                         velocity.x / speed,
                                         velocity.y / speed}
                                   : Vec2{};

        const float sparkLength = std::clamp(
            particle.size() * (1.5F + life * 1.5F),
            3.0F,
            12.0F);
        const Uint8 emberAlpha = static_cast<Uint8>(
            std::clamp(life, 0.0F, 1.0F) * 170.0F);
        const Uint8 hotAlpha = static_cast<Uint8>(
            std::clamp(life, 0.0F, 1.0F) * 255.0F);

        SDL_SetRenderDrawColor(renderer_, 255, 72, 8, emberAlpha);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * sparkLength,
            center.y - direction.y * sparkLength,
            center.x,
            center.y);

        SDL_SetRenderDrawColor(renderer_, 255, 188, 40, hotAlpha);
        SDL_RenderLine(
            renderer_,
            center.x - direction.x * sparkLength * 0.5F,
            center.y - direction.y * sparkLength * 0.5F,
            center.x,
            center.y);

        const float glowSize = std::max(
            2.0F,
            particle.size() * life * 1.6F);
        const SDL_FRect glow{
            center.x - glowSize / 2.0F,
            center.y - glowSize / 2.0F,
            glowSize,
            glowSize};
        SDL_SetRenderDrawColor(renderer_, 255, 112, 12, emberAlpha / 2U);
        SDL_RenderFillRect(renderer_, &glow);

        const float coreSize = std::max(
            1.0F,
            particle.size() * life * 0.55F);
        const SDL_FRect core{
            center.x - coreSize / 2.0F,
            center.y - coreSize / 2.0F,
            coreSize,
            coreSize};
        SDL_SetRenderDrawColor(renderer_, 255, 244, 176, hotAlpha);
        SDL_RenderFillRect(renderer_, &core);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderScreenPrimaryButton(
    const char *label)
{
    const SDL_FRect button =
        screenPrimaryButton();

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        42,
        102,
        82,
        245);
    SDL_RenderFillRect(renderer_, &button);
    SDL_SetRenderDrawColor(
        renderer_,
        132,
        225,
        176,
        255);
    SDL_RenderRect(renderer_, &button);

    const float textWidth =
        static_cast<float>(
            std::char_traits<char>::length(label)) *
        8.0F;
    uiTextRenderer_.render(
        renderer_,
        button.x + (button.w - textWidth) / 2.0F,
        button.y + 26.0F,
        label);
    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_NONE);
}

void App::renderMainMenu()
{
    const SDL_FRect panel{
        kFlowPanelX,
        kFlowPanelY,
        kFlowPanelWidth,
        kFlowPanelHeight};

    SDL_SetRenderDrawBlendMode(
        renderer_,
        SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(
        renderer_,
        14,
        24,
        29,
        245);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(
        renderer_,
        92,
        154,
        132,
        255);
    SDL_RenderRect(renderer_, &panel);

    SDL_SetRenderDrawColor(
        renderer_,
        230,
        236,
        232,
        255);
    uiTextRenderer_.render(
        renderer_,
        564.0F,
        244.0F,
        "PROJECT RAIDLINE");
    uiTextRenderer_.render(renderer_, 548.0F, 278.0F, "CORE EXTRACTION ALPHA");

    if (settingsOpen_)
    {
        uiTextRenderer_.render(renderer_, 520.0F, 314.0F, "KEYBOARD & MOUSE");
        uiTextRenderer_.render(renderer_, 490.0F, 340.0F, "WASD MOVE  SHIFT SPRINT  E INTERACT");
        uiTextRenderer_.render(renderer_, 466.0F, 364.0F, "RAID: LMB FIRE  RMB AIM  R RELOAD  5 MED");
        uiTextRenderer_.render(renderer_, 430.0F, 388.0F, "SHIFT SPRINT  TAB INVENTORY  M TACTICAL MAP  ESC PAUSE/CLOSE");
        const SDL_FRect language = mainMenuButton(0);
        SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
        SDL_RenderFillRect(renderer_, &language);
        SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
        SDL_RenderRect(renderer_, &language);
        const std::string languageLabel = fmt::format(
            "LANGUAGE: {}",
            uiLanguageDisplayName(uiTextRenderer_.language()));
        uiTextRenderer_.render(
            renderer_, language.x + 32.0F, language.y + 15.0F,
            languageLabel.c_str());
        const SDL_FRect back = mainMenuButton(2);
        SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
        SDL_RenderFillRect(renderer_, &back);
        SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
        SDL_RenderRect(renderer_, &back);
        uiTextRenderer_.render(renderer_, back.x + 122.0F, back.y + 17.0F, "BACK");
    }
    else
    {
        const std::array<const char *, 4> labels{
            "CONTINUE GAME",
            newGameOverwriteArmed_ ? "CONFIRM NEW GAME" : "NEW GAME",
            "SETTINGS",
            "EXIT"};
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            const SDL_FRect button = mainMenuButton(index);
            const bool disabled = index == 0 && !gameSession_.hasSavedProfile();
            SDL_SetRenderDrawColor(
                renderer_,
                disabled ? 34 : 42,
                disabled ? 45 : 102,
                disabled ? 48 : 82,
                245);
            SDL_RenderFillRect(renderer_, &button);
            SDL_SetRenderDrawColor(
                renderer_,
                disabled ? 80 : 132,
                disabled ? 90 : 225,
                disabled ? 94 : 176,
                255);
            SDL_RenderRect(renderer_, &button);
            uiTextRenderer_.render(
                renderer_,
                button.x + 16.0F,
                button.y + 17.0F,
                labels[index]);
        }
    }

    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(renderer_, 484.0F, 596.0F, uiMessage_.c_str());
    }
}

void App::renderPauseMenu()
{
    if (!pauseMenu_.isOpen())
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect shade{0.0F, 0.0F, 1280.0F, 720.0F};
    SDL_SetRenderDrawColor(renderer_, 3, 7, 9, 185);
    SDL_RenderFillRect(renderer_, &shade);

    const SDL_FRect panel{kFlowPanelX, 112.0F, kFlowPanelWidth, 496.0F};
    SDL_SetRenderDrawColor(renderer_, 14, 24, 29, 250);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 92, 154, 132, 255);
    SDL_RenderRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 230, 236, 232, 255);
    uiTextRenderer_.render(renderer_, 600.0F, 160.0F, "PAUSED");

    if (pauseMenu_.settingsOpen())
    {
        uiTextRenderer_.render(renderer_, 568.0F, 208.0F, "SETTINGS");
        uiTextRenderer_.render(
            renderer_, 452.0F, 238.0F,
            "WASD MOVE  SHIFT SPRINT  E INTERACT");
        uiTextRenderer_.render(
            renderer_, 438.0F, 262.0F,
            "RAID: LMB FIRE  RMB AIM  R RELOAD  5 MED");
        uiTextRenderer_.render(
            renderer_, 448.0F, 286.0F,
            "TAB INVENTORY  M TACTICAL MAP  F10 DEVELOPER PANEL");
        const SDL_FRect language = pauseMenuButton(0);
        SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
        SDL_RenderFillRect(renderer_, &language);
        SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
        SDL_RenderRect(renderer_, &language);
        const std::string languageLabel = fmt::format(
            "LANGUAGE: {}",
            uiLanguageDisplayName(uiTextRenderer_.language()));
        uiTextRenderer_.render(
            renderer_, language.x + 32.0F, language.y + 15.0F,
            languageLabel.c_str());
        const SDL_FRect back = pauseMenuButton(1);
        SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
        SDL_RenderFillRect(renderer_, &back);
        SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
        SDL_RenderRect(renderer_, &back);
        uiTextRenderer_.render(renderer_, back.x + 122.0F, back.y + 17.0F, "BACK");
    }
    else
    {
        const std::array<const char *, 4> labels{
            "CONTINUE GAME",
            "SETTINGS",
            "EXIT TO MAIN MENU",
            "EXIT TO DESKTOP"};
        for (std::size_t index{}; index < labels.size(); ++index)
        {
            const SDL_FRect button = pauseMenuButton(index);
            SDL_SetRenderDrawColor(renderer_, 42, 102, 82, 245);
            SDL_RenderFillRect(renderer_, &button);
            SDL_SetRenderDrawColor(renderer_, 132, 225, 176, 255);
            SDL_RenderRect(renderer_, &button);
            uiTextRenderer_.render(
                renderer_, button.x + 16.0F, button.y + 17.0F,
                labels[index]);
        }
        uiTextRenderer_.render(
            renderer_, 476.0F, 542.0F,
            "ESC CONTINUES | RAID EXIT RESTORES GEAR; RESCUES PERSIST");
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseWorld()
{
    SDL_SetRenderDrawColor(renderer_, 20, 32, 34, 255);
    const SDL_FRect courtyard{32.0F, 24.0F, 1216.0F, 664.0F};
    SDL_RenderFillRect(renderer_, &courtyard);
    SDL_SetRenderDrawColor(renderer_, 76, 98, 96, 255);
    SDL_RenderRect(renderer_, &courtyard);

    for (const BaseFacility &facility : gameFlow_.baseWorld().facilities())
    {
        const SDL_FRect bounds{
            facility.bounds.position.x,
            facility.bounds.position.y,
            facility.bounds.size.x,
            facility.bounds.size.y};
        switch (facility.kind)
        {
        case BaseFacilityKind::Storage:
            SDL_SetRenderDrawColor(renderer_, 48, 78, 96, 255);
            break;
        case BaseFacilityKind::Supply:
            SDL_SetRenderDrawColor(renderer_, 78, 76, 48, 255);
            break;
        case BaseFacilityKind::Allocation:
            SDL_SetRenderDrawColor(renderer_, 42, 82, 68, 255);
            break;
        case BaseFacilityKind::Medical:
            SDL_SetRenderDrawColor(renderer_, 54, 72, 74, 255);
            break;
        case BaseFacilityKind::Dormitory:
            SDL_SetRenderDrawColor(renderer_, 66, 64, 78, 255);
            break;
        case BaseFacilityKind::Workshop:
            SDL_SetRenderDrawColor(renderer_, 80, 66, 44, 255);
            break;
        case BaseFacilityKind::RaidGate:
            SDL_SetRenderDrawColor(renderer_, 76, 48, 48, 255);
            break;
        }
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 174, 188, 180, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_,
            bounds.x + 18.0F,
            bounds.y + bounds.h / 2.0F,
            baseFacilityName(facility.kind));
    }

    const Vec2 playerPosition = gameFlow_.baseWorld().playerPosition();
    const Vec2 playerSize = gameFlow_.baseWorld().playerSize();
    renderPlayerAvatar(
        playerPosition,
        playerSize,
        gameFlow_.baseWorld().playerFacingDirection(),
        gameFlow_.baseWorld().playerIsMoving(),
        gameFlow_.baseWorld().playerAnimationFrame());

    if (const auto facility = gameFlow_.baseWorld().interactableFacility())
    {
        const std::string prompt = fmt::format(
            "E - {}",
            baseFacilityName(*facility));
        uiTextRenderer_.render(renderer_, 520.0F, 650.0F, prompt.c_str());
    }
}

void App::renderProfileAsset(
    const AssetRecord &asset,
    const SDL_FRect &bounds,
    float cellSize,
    Uint8 alpha,
    bool showWeaponCondition)
{
    const ItemDefinition &definition =
        publishedContentRegistry().item(asset.definitionId);
    const auto legacy = legacyItemId(asset.definitionId);
    const Texture *texture{};
    if (legacy.has_value())
    {
        texture = &inventoryItemTextures_[static_cast<std::size_t>(*legacy)];
    }
    else if (definition.category == ItemCategory::Medical &&
             definition.visualAssetsPublished)
    {
        // The first medical slice deliberately reuses the already approved
        // Medkit placeholder until formal item art is authorized.
        texture = &inventoryItemTextures_[
            static_cast<std::size_t>(ItemId::Medkit)];
    }

    if (texture != nullptr && texture->valid())
    {
        SDL_SetTextureAlphaMod(texture->get(), alpha);
        renderOrientedTexture(
            renderer_,
            texture->get(),
            bounds,
            static_cast<float>(definition.inventoryWidthCells) * cellSize,
            static_cast<float>(definition.inventoryHeightCells) * cellSize,
            asset.orientation);
        SDL_SetTextureAlphaMod(texture->get(), 255);
    }
    else
    {
        switch (definition.category)
        {
        case ItemCategory::Weapon:
            SDL_SetRenderDrawColor(renderer_, 94, 112, 122, alpha);
            break;
        case ItemCategory::Magazine:
        case ItemCategory::Ammunition:
            SDL_SetRenderDrawColor(renderer_, 116, 98, 54, alpha);
            break;
        case ItemCategory::Container:
            SDL_SetRenderDrawColor(renderer_, 78, 104, 82, alpha);
            break;
        case ItemCategory::Medical:
            SDL_SetRenderDrawColor(renderer_, 126, 68, 68, alpha);
            break;
        case ItemCategory::ProtectiveGear:
            SDL_SetRenderDrawColor(renderer_, 70, 92, 112, alpha);
            break;
        case ItemCategory::Maintenance:
            SDL_SetRenderDrawColor(renderer_, 72, 112, 106, alpha);
            break;
        default:
            SDL_SetRenderDrawColor(renderer_, 92, 80, 112, alpha);
            break;
        }
        SDL_RenderFillRect(renderer_, &bounds);
        const std::string shortName = definition.displayName.substr(
            0,
            std::min<std::size_t>(definition.displayName.size(), 12));
        SDL_SetRenderDrawColor(renderer_, 232, 236, 232, 255);
        uiTextRenderer_.render(
            renderer_,
            bounds.x + 3.0F,
            bounds.y + 4.0F,
            shortName.c_str());
    }

    SDL_SetRenderDrawColor(renderer_, 214, 220, 214, alpha);
    SDL_RenderRect(renderer_, &bounds);
    renderItemQuantityBadge(renderer_, bounds, asset.quantity);
    if (showWeaponCondition && definition.weaponCondition.has_value())
    {
        const std::string condition = fmt::format(
            "D {:.2f}/{:.2f}",
            static_cast<float>(asset.currentDurability) / 100.0F,
            static_cast<float>(asset.currentMaximumDurability) / 100.0F);
        uiTextRenderer_.render(
            renderer_, bounds.x + 3.0F, bounds.y + bounds.h - 13.0F,
            condition.c_str());
    }
    else if (showWeaponCondition && definition.armorProtection.has_value())
    {
        const std::string condition = fmt::format(
            "D {}/{}",
            asset.currentDurability,
            asset.currentMaximumDurability);
        uiTextRenderer_.render(
            renderer_, bounds.x + 3.0F, bounds.y + bounds.h - 13.0F,
            condition.c_str());
    }
    else if (definition.weaponMaintenance.has_value() ||
             definition.armorMaintenance.has_value())
    {
        const std::string capacity = fmt::format(
            "KIT {:.2f}",
            static_cast<float>(asset.remainingCharges) / 100.0F);
        uiTextRenderer_.render(
            renderer_, bounds.x + 3.0F, bounds.y + bounds.h - 13.0F,
            capacity.c_str());
    }
}

void App::renderProfileGrid(
    ProfileContainerId container,
    float x,
    float y,
    float cellSize,
    const char *label)
{
    const ProfileState &profile = gameSession_.profile();
    InventoryGridSize size{};
    try
    {
        size = profileContainerSize(
            profile,
            publishedContentRegistry(),
            container);
    }
    catch (...)
    {
        return;
    }
    const SDL_FRect grid{
        x,
        y,
        static_cast<float>(size.width) * cellSize,
        static_cast<float>(size.height) * cellSize};
    const bool pendingReturns =
        container == ProfileContainerId::baseIntake();
    SDL_SetRenderDrawColor(
        renderer_,
        pendingReturns ? 34 : 20,
        pendingReturns ? 31 : 27,
        pendingReturns ? 20 : 30,
        255);
    SDL_RenderFillRect(renderer_, &grid);
    SDL_SetRenderDrawColor(
        renderer_,
        pendingReturns ? 156 : 86,
        pendingReturns ? 132 : 102,
        pendingReturns ? 62 : 108,
        255);
    for (int column = 0; column <= size.width; ++column)
    {
        SDL_RenderLine(
            renderer_,
            x + static_cast<float>(column) * cellSize,
            y,
            x + static_cast<float>(column) * cellSize,
            y + grid.h);
    }
    for (int row = 0; row <= size.height; ++row)
    {
        SDL_RenderLine(
            renderer_,
            x,
            y + static_cast<float>(row) * cellSize,
            x + grid.w,
            y + static_cast<float>(row) * cellSize);
    }
    uiTextRenderer_.render(renderer_, x, y - 20.0F, label);

    for (const AssetRecord *asset : assetsInContainer(profile, container))
    {
        const auto &stored = std::get<StoredAssetLocation>(asset->location);
        const InventoryFootprint footprint = inventoryFootprint(
            publishedContentRegistry().item(asset->definitionId),
            asset->orientation);
        renderProfileAsset(
            *asset,
            SDL_FRect{
                x + static_cast<float>(stored.origin.x) * cellSize,
                y + static_cast<float>(stored.origin.y) * cellSize,
                static_cast<float>(footprint.width) * cellSize,
                static_cast<float>(footprint.height) * cellSize},
            cellSize);
    }
}

void App::renderProfileDragFeedback(bool includeStash, bool inRaid)
{
    const auto source = profileInventoryInteraction_.source();
    const auto visual = profileInventoryInteraction_.activeDragVisual();
    if (!source.has_value() || !visual.has_value())
    {
        return;
    }
    const ProfileState &profile = gameSession_.profile();
    const AssetRecord *asset = profile.assets.find(source->instanceId);
    if (asset == nullptr)
    {
        return;
    }

    ProfileDropFeedback feedback;
    const auto target = profileInventoryInteraction_.hoveredTarget();
    if (target.has_value())
    {
        const ProfileDropRequest request{*source, *target};
        std::visit(
            [&](const auto &typedTarget)
            {
                using Target = std::decay_t<decltype(typedTarget)>;
                if constexpr (std::is_same_v<Target, StoredCellTarget>)
                {
                    bool allowed{};
                    if (std::holds_alternative<InstalledMagazineLocation>(
                            source->location))
                    {
                        const auto installed = std::get<InstalledMagazineLocation>(
                            source->location);
                        allowed = queryWeaponAmmo(
                            profile,
                            publishedContentRegistry(),
                            UninstallMagazineCommand{
                                installed.weaponAssetId,
                                typedTarget.location,
                                source->orientation}).canCommit;
                    }
                    else
                    {
                        allowed = queryInventory(
                            profile,
                            publishedContentRegistry(),
                            InventoryMoveCommand{
                                source->instanceId,
                                source->quantity,
                                typedTarget.location,
                                source->orientation}).canCommit;
                    }
                    if (inRaid &&
                        typedTarget.location.container.kind == ProfileContainerKind::Stash)
                    {
                        allowed = false;
                    }
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Ordinary
                        : ProfileDropFeedbackKind::Invalid;
                    if (allowed)
                    {
                        const auto occupant = profileAssetAtCell(
                            profile,
                            publishedContentRegistry(),
                            typedTarget.location.container,
                            typedTarget.location.origin);
                        if (!occupant.has_value() || *occupant == source->instanceId)
                        {
                            feedback.label = "MOVE";
                        }
                        else
                        {
                            const AssetRecord *other = profile.assets.find(*occupant);
                            feedback.label = other != nullptr &&
                                    other->definitionId == asset->definitionId
                                ? "MERGE"
                                : "SWAP";
                        }
                    }
                }
                else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
                {
                    const bool allowed = queryInventory(
                        profile,
                        publishedContentRegistry(),
                        InventoryEquipCommand{
                            source->instanceId,
                            typedTarget.slot}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Ordinary
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed && equippedAsset(profile, typedTarget.slot).has_value()
                        ? "SWAP"
                        : allowed ? "MOVE" : "BLOCKED";
                }
                else if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
                {
                    const bool allowed = queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        LoadMagazineCommand{
                            typedTarget.magazineAssetId,
                            source->instanceId,
                            source->quantity}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed ? "LOAD" : "BLOCKED";
                }
                else if constexpr (std::is_same_v<Target, WeaponInstallTarget>)
                {
                    const bool allowed = queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        InstallMagazineAndChamberCommand{
                            typedTarget.weaponAssetId,
                            source->instanceId}).canCommit;
                    feedback.kind = allowed
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = allowed ? "INSTALL" : "BLOCKED";
                }
                else if constexpr (
                    std::is_same_v<Target, WeaponMaintenanceTarget>)
                {
                    const WeaponMaintenancePlan plan =
                        queryWeaponMaintenance(
                            profile,
                            publishedContentRegistry(),
                            WeaponMaintenanceCommand{
                                source->instanceId,
                                typedTarget.weaponAssetId,
                                inRaid
                                    ? MaintenanceAccess::CarriedOnly
                                    : MaintenanceAccess::AnyOwned,
                                inRaid
                                    ? MaintenanceLocation::Raid
                                    : MaintenanceLocation::Base});
                    feedback.kind = plan.canCommit
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = plan.canCommit ? "REPAIR" : "BLOCKED";
                }
                else
                {
                    const ArmorMaintenancePlan plan = queryArmorMaintenance(
                        profile,
                        publishedContentRegistry(),
                        ArmorMaintenanceCommand{
                            source->instanceId,
                            typedTarget.armorAssetId,
                            inRaid
                                ? MaintenanceAccess::CarriedOnly
                                : MaintenanceAccess::AnyOwned,
                            inRaid
                                ? MaintenanceLocation::Raid
                                : MaintenanceLocation::Base});
                    feedback.kind = plan.canCommit
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = plan.canCommit ? "REPAIR" : "BLOCKED";
                }
            },
            *target);
    }

    SDL_Color color{212, 72, 72, 255};
    if (feedback.kind == ProfileDropFeedbackKind::Ordinary)
    {
        color = SDL_Color{74, 206, 122, 255};
    }
    else if (feedback.kind == ProfileDropFeedbackKind::Special)
    {
        color = SDL_Color{74, 158, 232, 255};
    }

    if (target.has_value())
    {
        std::optional<SDL_FRect> targetBounds;
        std::visit(
            [&](const auto &typedTarget)
            {
                using Target = std::decay_t<decltype(typedTarget)>;
                if constexpr (std::is_same_v<Target, StoredCellTarget>)
                {
                    for (const ProfileGridView &view : profileGridViews(profile, includeStash))
                    {
                        if (view.container == typedTarget.location.container)
                        {
                            targetBounds = SDL_FRect{
                                view.x + typedTarget.location.origin.x * view.cellSize,
                                view.y + typedTarget.location.origin.y * view.cellSize,
                                visual->footprint.width * view.cellSize,
                                visual->footprint.height * view.cellSize};
                            break;
                        }
                    }
                }
                else if constexpr (std::is_same_v<Target, EquipmentSlotTarget>)
                {
                    targetBounds = equipmentSlotRect(typedTarget.slot);
                }
                else
                {
                    AssetInstanceId id{};
                    if constexpr (std::is_same_v<Target, MagazineLoadTarget>)
                    {
                        id = typedTarget.magazineAssetId;
                    }
                    else if constexpr (std::is_same_v<Target, WeaponInstallTarget>)
                    {
                        id = typedTarget.weaponAssetId;
                    }
                    else if constexpr (
                        std::is_same_v<Target, WeaponMaintenanceTarget>)
                    {
                        id = typedTarget.weaponAssetId;
                    }
                    else
                    {
                        id = typedTarget.armorAssetId;
                    }
                    if (const AssetRecord *targetAsset = profile.assets.find(id))
                    {
                        targetBounds = profileAssetBounds(
                            profile, *targetAsset, includeStash);
                    }
                }
            },
            *target);
        if (targetBounds.has_value())
        {
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 110);
            SDL_RenderFillRect(renderer_, &*targetBounds);
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
            SDL_RenderRect(renderer_, &*targetBounds);
        }
    }

    AssetRecord ghost = *asset;
    ghost.orientation = visual->orientation;
    if (visual->selectedQuantity.has_value())
    {
        ghost.quantity = *visual->selectedQuantity;
    }
    const SDL_FRect ghostBounds{
        visual->pointerPosition.x -
            visual->grabOffsetInCells.x * kBasePocketCellSize,
        visual->pointerPosition.y -
            visual->grabOffsetInCells.y * kBasePocketCellSize,
        static_cast<float>(visual->footprint.width) * kBasePocketCellSize,
        static_cast<float>(visual->footprint.height) * kBasePocketCellSize};
    renderProfileAsset(ghost, ghostBounds, kBasePocketCellSize, 150);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderRect(renderer_, &ghostBounds);
    uiTextRenderer_.render(
        renderer_,
        visual->pointerPosition.x + 16.0F,
        visual->pointerPosition.y + 16.0F,
        feedback.label);
}

void App::renderProfileContextMenu(bool inRaid)
{
    if (!profileContextMenu_.has_value())
    {
        return;
    }
    const AssetRecord *asset = gameSession_.profile().assets.find(
        profileContextMenu_->instanceId);
    if (asset == nullptr)
    {
        return;
    }
    const auto label = profileContextActionLabel(
        gameSession_.profile(), *asset, inRaid);
    if (!label.has_value())
    {
        return;
    }
    const SDL_FRect menu = profileContextActionRect(profileContextMenu_->position);
    SDL_SetRenderDrawColor(renderer_, 24, 34, 38, 250);
    SDL_RenderFillRect(renderer_, &menu);
    SDL_SetRenderDrawColor(renderer_, 118, 188, 190, 255);
    SDL_RenderRect(renderer_, &menu);
    uiTextRenderer_.render(renderer_, menu.x + 12.0F, menu.y + 18.0F, *label);
}

void App::renderMedicalWheel()
{
    if (!medicalWheelOpen_ || medicalWheelOptions_.empty())
    {
        return;
    }
    constexpr float pi = 3.14159265358979323846F;
    constexpr Vec2 center{640.0F, 360.0F};
    constexpr float radius{118.0F};
    const SDL_FRect shade{0.0F, 0.0F, 1280.0F, 720.0F};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 4, 8, 10, 150);
    SDL_RenderFillRect(renderer_, &shade);
    uiTextRenderer_.render(renderer_, 560.0F, 246.0F,
                        "HOLD 5 / POINT / RELEASE");

    const ProfileState &profile = gameSession_.profile();
    const float sector = 2.0F * pi /
        static_cast<float>(medicalWheelOptions_.size());
    for (std::size_t index = 0; index < medicalWheelOptions_.size(); ++index)
    {
        const AssetRecord *asset = profile.assets.find(
            medicalWheelOptions_[index]);
        if (asset == nullptr)
        {
            continue;
        }
        const ItemDefinition &definition = publishedContentRegistry().item(
            asset->definitionId);
        const MedicalUsePlan plan = queryMedicalUse(
            profile,
            publishedContentRegistry(),
            asset->instanceId,
            MedicalAccess::CarriedOnly);
        const float angle = static_cast<float>(index) * sector - pi;
        const SDL_FRect option{
            center.x + std::cos(angle) * radius - 78.0F,
            center.y + std::sin(angle) * radius - 28.0F,
            156.0F,
            56.0F};
        const bool selected = index == medicalWheelSelectedIndex_;
        SDL_SetRenderDrawColor(
            renderer_,
            plan.canCommit ? selected ? 42 : 24 : 78,
            plan.canCommit ? selected ? 118 : 72 : 34,
            plan.canCommit ? selected ? 126 : 78 : 34,
            245);
        SDL_RenderFillRect(renderer_, &option);
        SDL_SetRenderDrawColor(
            renderer_,
            plan.canCommit ? selected ? 145 : 88 : 210,
            plan.canCommit ? selected ? 235 : 168 : 82,
            plan.canCommit ? selected ? 225 : 180 : 82,
            255);
        SDL_RenderRect(renderer_, &option);
        uiTextRenderer_.render(
            renderer_, option.x + 8.0F, option.y + 11.0F,
            definition.displayName.c_str());
        const std::string charges = fmt::format(
            "{} | {} charge(s)",
            plan.canCommit ? "READY" : "NOT NEEDED",
            asset->remainingCharges);
        uiTextRenderer_.render(
            renderer_, option.x + 8.0F, option.y + 31.0F,
            charges.c_str());
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderDeveloperWeaponPanel()
{
    if (!developerWeaponPanelOpen_)
    {
        return;
    }

    const std::optional<DeveloperWeaponTuningSnapshot> tuning =
        gameSession_.developerWeaponTuning();
    const auto sdlRect = [](DeveloperPanelRect rect) noexcept
    {
        return SDL_FRect{rect.x, rect.y, rect.width, rect.height};
    };
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect shade{0.0F, 0.0F, 1280.0F, 720.0F};
    SDL_SetRenderDrawColor(renderer_, 4, 7, 8, 175);
    SDL_RenderFillRect(renderer_, &shade);
    const SDL_FRect panel = sdlRect(developerPanelBounds());
    SDL_SetRenderDrawColor(renderer_, 14, 24, 27, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 92, 178, 173, 255);
    SDL_RenderRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 220, 235, 226, 255);
    uiTextRenderer_.render(
        renderer_, panel.x + 22.0F, panel.y + 18.0F,
        "DEVELOPER RUNTIME PANEL - RUNTIME ONLY");
    const auto renderButton = [&](DeveloperPanelRect definition,
                                  bool active,
                                  const char *label)
    {
        const SDL_FRect bounds = sdlRect(definition);
        SDL_SetRenderDrawColor(
            renderer_, active ? 34 : 50, active ? 108 : 58,
            active ? 94 : 62, 245);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(
            renderer_, active ? 126 : 112, active ? 224 : 128,
            active ? 193 : 132, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_, bounds.x + 10.0F, bounds.y + 9.0F, label);
    };
    renderButton(
        developerFogButton(),
        developerMapFogEnabled_,
        developerMapFogEnabled_ ? "MAP FOG: ON" : "MAP FOG: OFF");
    renderButton(
        developerInfiniteAmmoButton(),
        developerInfiniteAmmoEnabled_,
        developerInfiniteAmmoEnabled_
            ? "INFINITE AMMO: ON"
            : "INFINITE AMMO: OFF");
    renderButton(
        developerResetWeaponButton(), false, "RESET WEAPON PARAMETERS");
    if (!tuning.has_value())
    {
        uiTextRenderer_.render(
            renderer_, panel.x + 22.0F, panel.y + 102.0F,
            "NO ACTIVE WEAPON");
        uiTextRenderer_.render(
            renderer_, panel.x + 22.0F, panel.y + panel.h - 28.0F,
            "CLICK OPTIONS | F10 / ESC CLOSE");
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }

    const std::string weaponLine = fmt::format(
        "{} | ASSET {} | {}",
        tuning->weaponDefinitionId.value(),
        tuning->weaponAssetId,
        tuning->overridden ? "OVERRIDDEN" : "CONTENT DEFAULT");
    uiTextRenderer_.render(
        renderer_, panel.x + 22.0F, panel.y + 94.0F,
        weaponLine.c_str());

    constexpr std::array<const char *,
        static_cast<std::size_t>(DeveloperWeaponParameter::Count)> labels{
        "Recoil control", "Stability", "Handling speed", "Ergonomics",
        "Accuracy", "Shot interval", "Base damage", "Effective range",
        "Maximum range", "Logical ballistic speed",
        "High-scope maximum speed", "High-scope control accel",
        "Spread per shot", "Recoil lateral ratio", "Recoil bend duration",
        "Moving spread fraction", "Sprinting spread fraction",
        "Reticle motion spread rate",
        "Near-distance spread scale", "Effective-range distance bloom",
        "Right-click aim accuracy", "Right-click aim stability",
        "Weak tracer length", "Weak tracer opacity",
        "Weak tracer lifetime"};

    const WeaponUseDefinition &weapon = tuning->weaponUse;
    const WeaponHandlingParameters &handling = tuning->handling;
    for (std::size_t index{}; index < labels.size(); ++index)
    {
        const DeveloperWeaponParameter parameter =
            static_cast<DeveloperWeaponParameter>(index);
        std::string value;
        switch (parameter)
        {
        case DeveloperWeaponParameter::RecoilControl:
            value = fmt::format(
                "{}  (recoil speed {:.1f})",
                weapon.recoilControl,
                handling.recoilInitialSpeed);
            break;
        case DeveloperWeaponParameter::Stability:
            value = fmt::format(
                "{}  (max spread {:.2f} deg)",
                weapon.stability,
                handling.maximumSpreadDegrees);
            break;
        case DeveloperWeaponParameter::HandlingSpeed:
            value = fmt::format(
                "{}  (recovery {:.2f} deg/s)",
                weapon.handlingSpeed,
                handling.spreadRecoveryDegreesPerSecond);
            break;
        case DeveloperWeaponParameter::Ergonomics:
            value = fmt::format(
                "{}  (control accel {:.0f})",
                weapon.ergonomics,
                handling.reticleControlAcceleration);
            break;
        case DeveloperWeaponParameter::Accuracy:
            value = fmt::format(
                "{}  (min spread {:.2f} deg)",
                weapon.accuracy,
                handling.minimumSpreadDegrees);
            break;
        case DeveloperWeaponParameter::ShotInterval:
            value = fmt::format("{:.3f} s", weapon.shotIntervalSeconds);
            break;
        case DeveloperWeaponParameter::BaseDamage:
            value = fmt::format("{}", weapon.baseDamage);
            break;
        case DeveloperWeaponParameter::EffectiveRange:
            value = fmt::format("{:.0f} px", weapon.effectiveRange);
            break;
        case DeveloperWeaponParameter::MaximumRange:
            value = fmt::format("{:.0f} px", weapon.maximumRange);
            break;
        case DeveloperWeaponParameter::LogicalBallisticSpeed:
            value = fmt::format(
                "{:.0f} px/s", weapon.logicalBallisticSpeed);
            break;
        case DeveloperWeaponParameter::MaximumReticleSpeed:
            value = fmt::format("{:.0f} px/s", handling.maximumReticleSpeed);
            break;
        case DeveloperWeaponParameter::ReticleControlAcceleration:
            value = fmt::format(
                "{:.0f} px/s2", handling.reticleControlAcceleration);
            break;
        case DeveloperWeaponParameter::SpreadPerShot:
            value = fmt::format("{:.2f} deg", handling.spreadPerShotDegrees);
            break;
        case DeveloperWeaponParameter::RecoilLateralRatio:
            value = fmt::format("{:.2f}", handling.recoilLateralRatio);
            break;
        case DeveloperWeaponParameter::RecoilBendDuration:
            value = fmt::format(
                "{:.3f} s", handling.recoilBendDurationSeconds);
            break;
        case DeveloperWeaponParameter::MovingSpreadFraction:
            value = fmt::format("{:.2f}", handling.movingSpreadFraction);
            break;
        case DeveloperWeaponParameter::SprintingSpreadFraction:
            value = fmt::format("{:.2f}", handling.sprintingSpreadFraction);
            break;
        case DeveloperWeaponParameter::ReticleMotionSpreadRate:
            value = fmt::format(
                "{:.2f} deg/s",
                handling.reticleMotionSpreadDegreesPerSecond);
            break;
        case DeveloperWeaponParameter::NearDistanceSpreadScale:
            value = fmt::format(
                "{:.2f}", handling.nearDistanceSpreadScale);
            break;
        case DeveloperWeaponParameter::DistanceBloomAtEffectiveRange:
            value = fmt::format(
                "{:.2f}", handling.distanceBloomAtEffectiveRange);
            break;
        case DeveloperWeaponParameter::AdsAccuracyMultiplier:
            value = fmt::format(
                "{:.2f}", handling.aimDownSightsAccuracyMultiplier);
            break;
        case DeveloperWeaponParameter::AdsStabilityMultiplier:
            value = fmt::format(
                "{:.2f}", handling.aimDownSightsStabilityMultiplier);
            break;
        case DeveloperWeaponParameter::WeakTracerLength:
            value = fmt::format("{:.0f} px", handling.weakTracerLength);
            break;
        case DeveloperWeaponParameter::WeakTracerOpacity:
            value = fmt::format("{:.2f}", handling.weakTracerOpacity);
            break;
        case DeveloperWeaponParameter::WeakTracerLifetime:
            value = fmt::format(
                "{:.3f} s", handling.weakTracerLifetimeSeconds);
            break;
        case DeveloperWeaponParameter::Count:
            break;
        }

        const DeveloperPanelRect rowDefinition =
            developerParameterRow(index);
        const float rowY = rowDefinition.y + 4.0F;
        if (index == developerWeaponParameterIndex_)
        {
            const SDL_FRect selected = sdlRect(rowDefinition);
            SDL_SetRenderDrawColor(renderer_, 42, 94, 91, 235);
            SDL_RenderFillRect(renderer_, &selected);
            SDL_SetRenderDrawColor(renderer_, 136, 226, 207, 255);
            SDL_RenderRect(renderer_, &selected);
        }
        SDL_SetRenderDrawColor(renderer_, 220, 235, 226, 255);
        const std::string row = fmt::format(
            "{} {:<30} {}",
            index == developerWeaponParameterIndex_ ? ">" : " ",
            labels[index],
            value);
        uiTextRenderer_.render(
            renderer_, rowDefinition.x + 8.0F, rowY, row.c_str());

        const SDL_FRect decrease = sdlRect(
            developerParameterDecreaseButton(index));
        const SDL_FRect increase = sdlRect(
            developerParameterIncreaseButton(index));
        SDL_SetRenderDrawColor(renderer_, 46, 65, 67, 245);
        SDL_RenderFillRect(renderer_, &decrease);
        SDL_RenderFillRect(renderer_, &increase);
        SDL_SetRenderDrawColor(renderer_, 112, 174, 169, 255);
        SDL_RenderRect(renderer_, &decrease);
        SDL_RenderRect(renderer_, &increase);
        uiTextRenderer_.render(
            renderer_, decrease.x + 18.0F, decrease.y + 4.0F, "-");
        uiTextRenderer_.render(
            renderer_, increase.x + 17.0F, increase.y + 4.0F, "+");
    }

    const std::string derived = fmt::format(
        "RECOIL DECEL {:.0f} | RMB AIM {:.2f}s | MOVE {:.2f}x",
        handling.recoilDeceleration,
        handling.aimDownSightsDurationSeconds,
        handling.aimDownSightsMovementMultiplier);
    uiTextRenderer_.render(
        renderer_, panel.x + 22.0F, panel.y + panel.h - 61.0F,
        derived.c_str());
    uiTextRenderer_.render(
        renderer_, panel.x + 22.0F, panel.y + panel.h - 28.0F,
        "CLICK ROW OR - / + | F10 / ESC CLOSE");
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderProfileInventory(bool includeStash, bool inRaid)
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel = includeStash
        ? SDL_FRect{20.0F, 50.0F, 1240.0F, 650.0F}
        : SDL_FRect{20.0F, 50.0F, 590.0F, 650.0F};
    SDL_SetRenderDrawColor(renderer_, 12, 20, 24, 244);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 102, 156, 168, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(renderer_, 42.0F, 70.0F, "CHARACTER & LOADOUT");
    if (includeStash)
    {
        uiTextRenderer_.render(renderer_, 668.0F, 70.0F, "WAREHOUSE");
        SDL_RenderLine(renderer_, 630.0F, 68.0F, 630.0F, 678.0F);
    }

    const SDL_FRect preview{42.0F, 96.0F, 150.0F, 250.0F};
    SDL_SetRenderDrawColor(renderer_, 24, 34, 38, 255);
    SDL_RenderFillRect(renderer_, &preview);
    SDL_SetRenderDrawColor(renderer_, 78, 108, 112, 255);
    SDL_RenderRect(renderer_, &preview);
    renderPlayerPreview(preview);

    const ProfileState &profile = gameSession_.profile();
    for (EquipmentSlotKind slot : kProfileEquipmentSlots)
    {
        const SDL_FRect bounds = equipmentSlotRect(slot);
        SDL_SetRenderDrawColor(renderer_, 28, 42, 46, 255);
        SDL_RenderFillRect(renderer_, &bounds);
        const bool activeWeapon = inRaid && isWeaponEquipmentSlot(slot) &&
            gameSession_.activeAlphaWeaponSlot() == slot;
        SDL_SetRenderDrawColor(
            renderer_,
            activeWeapon ? 92 : 96,
            activeWeapon ? 198 : 126,
            activeWeapon ? 158 : 132,
            255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_,
            bounds.x + 7.0F,
            bounds.y + 7.0F,
            equipmentSlotLabel(slot));
        if (const auto id = equippedAsset(profile, slot))
        {
            if (const AssetRecord *asset = profile.assets.find(*id))
            {
                const SDL_FRect itemBounds{
                    bounds.x + 4.0F, bounds.y + 22.0F,
                    bounds.w - 8.0F, bounds.h - 26.0F};
                renderProfileAsset(
                    *asset,
                    itemBounds,
                    kBasePocketCellSize,
                    255,
                    false);
                if (asset->currentMaximumDurability > 0)
                {
                    const ItemDefinition &definition =
                        publishedContentRegistry().item(asset->definitionId);
                    const std::string durability =
                        definition.weaponCondition.has_value()
                            ? fmt::format(
                                  "DUR {:.2f}/{:.2f}",
                                  static_cast<float>(
                                      asset->currentDurability) / 100.0F,
                                  static_cast<float>(
                                      asset->currentMaximumDurability) /
                                      100.0F)
                            : fmt::format(
                                  "DUR {}/{}",
                                  asset->currentDurability,
                                  asset->currentMaximumDurability);
                    uiTextRenderer_.render(
                        renderer_,
                        bounds.x + 7.0F,
                        bounds.y + bounds.h - 13.0F,
                        durability.c_str());
                }
                if (isWeaponEquipmentSlot(slot))
                {
                    const auto magazine = installedMagazine(profile, *id);
                    const std::string ammo = fmt::format(
                        "C{} M{}",
                        asset->chamberedRound.has_value() ? 1 : 0,
                        magazine.has_value()
                            ? magazineRoundCount(profile, *magazine)
                            : 0U);
                    uiTextRenderer_.render(
                        renderer_,
                        bounds.x + bounds.w - 108.0F,
                        bounds.y + 7.0F,
                        ammo.c_str());
                    if (magazine.has_value())
                    {
                        if (const AssetRecord *magazineAsset =
                                profile.assets.find(*magazine))
                        {
                            renderProfileAsset(
                                *magazineAsset,
                                installedMagazineRect(slot),
                                kBasePocketCellSize);
                        }
                    }
                }
            }
        }
    }

    for (const ProfileGridView &view : profileGridViews(profile, includeStash))
    {
        renderProfileGrid(
            view.container, view.x, view.y, view.cellSize, view.label);
    }

    const std::string health = fmt::format("HP {}/100", profile.currentHealth);
    uiTextRenderer_.render(renderer_, 42.0F, 366.0F, health.c_str());
    const char *bleeding = profile.medicalStatus.bleeding == BleedingSeverity::Heavy
        ? "HEAVY BLEEDING"
        : profile.medicalStatus.bleeding == BleedingSeverity::Light
            ? "LIGHT BLEEDING"
            : "NO BLEEDING";
    std::string medical = bleeding;
    if (hasPain(profile.medicalStatus))
    {
        medical += painIsSuppressed(profile.medicalStatus)
            ? " | PAIN SUPPRESSED"
            : " | PAIN";
    }
    uiTextRenderer_.render(renderer_, 42.0F, 382.0F, medical.c_str());
    uiTextRenderer_.render(
        renderer_, 42.0F, 642.0F,
        "DRAG: MOVE | CTRL: 1 | SHIFT: HALF | R: ROTATE");
    uiTextRenderer_.render(
        renderer_, 42.0F, 664.0F,
        inRaid ? "RMB MEDICAL ITEM | TAB/ESC CLOSE"
               : "RMB: ITEM ACTION | TAB/ESC CLOSE");
    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(renderer_, includeStash ? 668.0F : 350.0F, 664.0F,
                            uiMessage_.c_str());
    }
    renderProfileDragFeedback(includeStash, inRaid);
    renderProfileContextMenu(inRaid);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseStorage()
{
    renderProfileInventory(true, false);
}

void App::renderAlphaRaidLoot()
{
    const ProfileState &profile = gameSession_.profile();
    if (!profile.pendingRaid.has_value())
    {
        return;
    }
    const std::optional<RaidSelfRecoveryProjection> recovery =
        gameSession_.raidSelfRecoveryProjection();
    if (recovery.has_value() && !recovery->opened &&
        gameSession_.world().inOutdoorRaidSpace())
    {
        const SDL_FRect glow{
            recovery->cachePosition.x - 38.0F,
            recovery->cachePosition.y - 30.0F,
            76.0F,
            60.0F};
        const SDL_FRect cache{
            recovery->cachePosition.x - 30.0F,
            recovery->cachePosition.y - 22.0F,
            60.0F,
            44.0F};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(
            renderer_, 196, 132, 72,
            recovery->interactionInRange ? 76 : 38);
        SDL_RenderFillRect(renderer_, &glow);
        SDL_SetRenderDrawColor(renderer_, 72, 58, 44, 245);
        SDL_RenderFillRect(renderer_, &cache);
        SDL_SetRenderDrawColor(
            renderer_, 232,
            recovery->interactionInRange ? 206 : 166,
            116, 255);
        SDL_RenderRect(renderer_, &cache);
        SDL_RenderLine(
            renderer_, cache.x + 8.0F, cache.y + 12.0F,
            cache.x + cache.w - 8.0F, cache.y + 12.0F);
        uiTextRenderer_.render(
            renderer_, cache.x - 18.0F, cache.y - 22.0F,
            recovery->interactionInRange
                ? "LOST CACHE | HOLD F"
                : "LOST CACHE");
        if (recovery->interactionProgress > 0.0F)
        {
            const SDL_FRect progressBack{
                cache.x, cache.y + cache.h + 5.0F, cache.w, 6.0F};
            const SDL_FRect progress{
                progressBack.x, progressBack.y,
                progressBack.w * recovery->interactionProgress,
                progressBack.h};
            SDL_SetRenderDrawColor(renderer_, 40, 34, 28, 220);
            SDL_RenderFillRect(renderer_, &progressBack);
            SDL_SetRenderDrawColor(renderer_, 236, 190, 92, 255);
            SDL_RenderFillRect(renderer_, &progress);
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
    for (const RaidLootSnapshot &loot : profile.pendingRaid->loot)
    {
        if (!gameSession_.raidLootAccessible(loot))
        {
            continue;
        }
        const AssetRecord *asset = profile.assets.find(loot.assetId);
        if (asset == nullptr ||
            !std::holds_alternative<RaidGroundAssetLocation>(asset->location))
        {
            continue;
        }
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        const Vec2 size = definition.worldRenderSize;
        const SDL_FRect destination{
            loot.position.x - size.x / 2.0F,
            loot.position.y - size.y / 2.0F,
            size.x,
            size.y};
        bool rendered{};
        if (const auto legacy = legacyItemId(asset->definitionId))
        {
            const Texture &texture = worldItemTextures_[
                static_cast<std::size_t>(*legacy)];
            if (texture.valid())
            {
                SDL_RenderTexture(
                    renderer_, texture.get(), nullptr, &destination);
                rendered = true;
            }
        }
        if (!rendered)
        {
            SDL_SetRenderDrawColor(renderer_, 176, 142, 76, 255);
            SDL_RenderFillRect(renderer_, &destination);
            SDL_SetRenderDrawColor(renderer_, 238, 214, 132, 255);
            SDL_RenderRect(renderer_, &destination);
        }
        renderItemQuantityBadge(renderer_, destination, asset->quantity);
    }
}

void App::renderBaseSupply()
{
    const SDL_FRect panel{40.0F, 70.0F, 1200.0F, 600.0F};
    SDL_SetRenderDrawColor(renderer_, 24, 25, 18, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 170, 154, 94, 255);
    SDL_RenderRect(renderer_, &panel);
    const std::string currency = fmt::format(
        "SUPPLY & RECOVERY | CURRENCY {}",
        gameSession_.profile().currency);
    SDL_SetRenderDrawColor(renderer_, 236, 220, 150, 255);
    uiTextRenderer_.render(renderer_, 96.0F, 100.0F, currency.c_str());
    const BaseOperationalProjection operations = projectBaseOperations(
        gameSession_.profile().baseResources,
        publishedContentRegistry().baseOperations(),
        populationAdjustedDailyDemand(
            gameSession_.profile().basePopulation));
    const std::string operationsStatus = fmt::format(
        "BASE OPERATIONS {} | LIMITING {}",
        baseOperationalTierName(operations.tier),
        baseResourceKindName(operations.limitingResource));
    SDL_SetRenderDrawColor(renderer_, 224, 214, 174, 255);
    uiTextRenderer_.render(
        renderer_, 96.0F, 122.0F, operationsStatus.c_str());

    const auto &supply = fixedSupplyIds();
    for (std::size_t index = 0; index < supply.size(); ++index)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(supply[index]);
        const SDL_FRect row{
            76.0F + static_cast<float>(index / 5U) * 184.0F,
            164.0F + static_cast<float>(index % 5U) * 46.0F,
            176.0F,
            40.0F};
        SDL_SetRenderDrawColor(renderer_, 62, 62, 38, 255);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, 174, 158, 92, 255);
        SDL_RenderRect(renderer_, &row);
        const std::uint32_t quantity =
            supply[index] == alpha_content::ammunition ? 30U : 1U;
        const std::string supplyName = definition.displayName.substr(
            0, std::min<std::size_t>(definition.displayName.size(), 10U));
        const std::string label = fmt::format(
            "BUY {} x{} | {}",
            supplyName,
            quantity,
            definition.marketBuyPrice * quantity);
        SDL_SetRenderDrawColor(renderer_, 232, 224, 178, 255);
        uiTextRenderer_.render(renderer_, row.x + 6.0F, row.y + 16.0F, label.c_str());
    }

    SDL_SetRenderDrawColor(renderer_, 236, 220, 150, 255);
    uiTextRenderer_.render(renderer_, 650.0F, 138.0F, "STASH - SELECT AN ITEM");
    std::size_t rowIndex{};
    for (const AssetRecord *asset : assetsInContainer(
             gameSession_.profile(),
             ProfileContainerId::stash()))
    {
        if (rowIndex >= 12) break;
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        const SDL_FRect row{
            650.0F,
            164.0F + static_cast<float>(rowIndex) * 30.0F,
            500.0F,
            26.0F};
        SDL_SetRenderDrawColor(renderer_, 42, 44, 34, 255);
        SDL_RenderFillRect(renderer_, &row);
        const bool selected = profileAssetSelection_.has_value() &&
            profileAssetSelection_->instanceId == asset->instanceId;
        if (selected)
        {
            SDL_SetRenderDrawColor(renderer_, 245, 214, 90, 255);
            SDL_RenderRect(renderer_, &row);
        }
        const std::string label = fmt::format(
            "{} x{} | RECYCLE {}{}",
            definition.displayName,
            asset->quantity,
            definition.marketRecyclePrice * asset->quantity,
            asset->reliefBatchId.has_value() ? " | RELIEF-LOCKED" : "");
        SDL_SetRenderDrawColor(
            renderer_,
            selected ? 255 : 218,
            selected ? 231 : 214,
            selected ? 116 : 190,
            255);
        uiTextRenderer_.render(renderer_, row.x + 6.0F, row.y + 9.0F, label.c_str());
        ++rowIndex;
    }

    const ProfileState &profile = gameSession_.profile();
    const SDL_FRect reliefButton = baseReliefButton();
    const bool eligible = isReliefEligible(
        gameSession_.profile(),
        publishedContentRegistry());
    SDL_SetRenderDrawColor(renderer_, eligible ? 68 : 42, eligible ? 104 : 48, 60, 255);
    SDL_RenderFillRect(renderer_, &reliefButton);
    SDL_SetRenderDrawColor(renderer_, 228, 232, 214, 255);
    uiTextRenderer_.render(
        renderer_, reliefButton.x + 24.0F, reliefButton.y + 18.0F,
        eligible ? "CLAIM RELIEF BATCH" : "RELIEF NOT REQUIRED");

    const SDL_FRect recycleButton = baseRecycleButton();
    SDL_SetRenderDrawColor(renderer_, 92, 66, 44, 255);
    SDL_RenderFillRect(renderer_, &recycleButton);
    SDL_SetRenderDrawColor(renderer_, 238, 224, 202, 255);
    uiTextRenderer_.render(renderer_, recycleButton.x + 26.0F, recycleButton.y + 18.0F, "RECYCLE SELECTED");

    const SDL_FRect gunsmithButton = baseGunsmithButton();
    std::string gunsmithStatus;
    const char *gunsmithLabel = "START FULL MAINTENANCE";
    bool gunsmithAvailable{};
    if (profile.gunsmithMaintenanceJob.has_value())
    {
        const GunsmithMaintenanceJob &job =
            *profile.gunsmithMaintenanceJob;
        const AssetRecord *weapon = profile.assets.find(job.weaponAssetId);
        const std::string weaponName = weapon == nullptr
            ? "UNKNOWN WEAPON"
            : publishedContentRegistry().item(
                weapon->definitionId).displayName;
        const GunsmithCollectionPlan plan = queryGunsmithCollection(
            profile, publishedContentRegistry());
        gunsmithAvailable = plan.canCommit;
        if (plan.canCommit)
        {
            gunsmithStatus = fmt::format(
                "GUNSMITH READY | {}", weaponName);
            gunsmithLabel = "COLLECT SERVICED WEAPON";
        }
        else
        {
            gunsmithStatus = "GUNSMITH READY | STASH SPACE REQUIRED";
            gunsmithLabel = "COLLECT - STASH BLOCKED";
        }
    }
    else if (profileAssetSelection_.has_value())
    {
        const GunsmithMaintenancePlan plan = queryGunsmithMaintenance(
            profile,
            publishedContentRegistry(),
            StartGunsmithMaintenanceCommand{
                profileAssetSelection_->instanceId});
        gunsmithAvailable = plan.canCommit;
        gunsmithStatus = plan.canCommit
            ? fmt::format(
                "GUNSMITH QUOTE {} | INSTANT | FULL FACTORY CONDITION",
                plan.quotedCurrency)
            : plan.message;
    }
    else
    {
        gunsmithStatus = "GUNSMITH | SELECT A DAMAGED STASH WEAPON";
    }
    SDL_SetRenderDrawColor(
        renderer_,
        gunsmithAvailable ? 58 : 42,
        gunsmithAvailable ? 92 : 48,
        gunsmithAvailable ? 112 : 52,
        255);
    SDL_RenderFillRect(renderer_, &gunsmithButton);
    SDL_SetRenderDrawColor(renderer_, 228, 232, 220, 255);
    uiTextRenderer_.render(
        renderer_,
        gunsmithButton.x + 18.0F,
        gunsmithButton.y + 18.0F,
        gunsmithLabel);
    SDL_SetRenderDrawColor(
        renderer_,
        gunsmithAvailable ? 184 : 214,
        gunsmithAvailable ? 222 : 204,
        gunsmithAvailable ? 236 : 174,
        255);
    uiTextRenderer_.render(
        renderer_, 650.0F, 532.0F, gunsmithStatus.c_str());
    SDL_SetRenderDrawColor(renderer_, 206, 202, 178, 255);
    uiTextRenderer_.render(renderer_, 96.0F, 630.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_SetRenderDrawColor(renderer_, 236, 220, 150, 255);
        uiTextRenderer_.render(renderer_, 470.0F, 630.0F, uiMessage_.c_str());
    }
}

void App::renderRaidSpacePortal()
{
    for (const RaidSpacePortalProjection &portal :
         gameSession_.world().visibleRaidSpacePortals())
    {
        const SDL_FRect bounds{
            portal.bounds.position.x,
            portal.bounds.position.y,
            portal.bounds.size.x,
            portal.bounds.size.y};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(
            renderer_, 50, portal.interactionInRange ? 116 : 82, 105,
            portal.interactionInRange ? 118 : 70);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(
            renderer_, 112, portal.interactionInRange ? 230 : 162, 192, 255);
        SDL_RenderRect(renderer_, &bounds);
        const SDL_FRect inset{
            bounds.x + 5.0F,
            bounds.y + 5.0F,
            std::max(0.0F, bounds.w - 10.0F),
            std::max(0.0F, bounds.h - 10.0F)};
        SDL_RenderRect(renderer_, &inset);
        const std::string label = portal.returnsOutside
            ? "OUTDOOR | F EXIT"
            : fmt::format("{} | F ENTER", portal.displayName);
        uiTextRenderer_.render(
            renderer_, bounds.x + 8.0F, bounds.y + 8.0F, label.c_str());
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderDeveloperPerformanceOverlay()
{
    if (!developerPerformanceOverlayOpen_)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{344.0F, 12.0F, 592.0F, 192.0F};
    SDL_SetRenderDrawColor(renderer_, 8, 15, 17, 225);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 88, 181, 165, 255);
    SDL_RenderRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 224, 238, 230, 255);
    uiTextRenderer_.render(
        renderer_, panel.x + 12.0F, panel.y + 10.0F,
        "RAID PERFORMANCE | F9 CLOSE");

    if (performanceOverlayLines_.front().empty())
    {
        uiTextRenderer_.render(
            renderer_, panel.x + 12.0F, panel.y + 30.0F,
            "NO FRAME SAMPLES");
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }

    for (std::size_t index{}; index < performanceOverlayLines_.size(); ++index)
    {
        uiTextRenderer_.render(
            renderer_, panel.x + 12.0F,
            panel.y + 30.0F + static_cast<float>(index) * 18.0F,
            performanceOverlayLines_[index].c_str());
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::refreshDeveloperPerformanceOverlay()
{
    const FramePerformanceSummary performance = framePerformance_.summary();
    if (performance.sampleCount == 0U)
    {
        performanceOverlayLines_ = {};
        return;
    }
    const RaidSimulationWorkload workload =
        gameFlow_.state() == GameFlowState::Raid
        ? gameSession_.world().simulationWorkloadLastUpdate()
        : RaidSimulationWorkload{};
    const float averageFps = performance.average.totalMilliseconds > 0.0F
        ? 1000.0F / performance.average.totalMilliseconds
        : 0.0F;
    const auto timings = [](const FramePhaseDurations &sample)
    {
        return fmt::format(
            "{:.2f}/{:.2f}/{:.2f}/{:.2f}/{:.2f}",
            sample.eventMilliseconds,
            sample.updateMilliseconds,
            sample.renderMilliseconds,
            sample.pacingMilliseconds,
            sample.totalMilliseconds);
    };
    const std::string header = fmt::format(
        "FRAME EVENT/UPDATE/PRESENT/PACING/TOTAL MS | {:.0f} FPS | {} SAMPLES",
        averageFps,
        performance.sampleCount);
    const std::string pacing = fmt::format(
        "PACING {} | TARGET {:.1f} HZ",
        framePacingConfiguration_.mode == FramePacingMode::VSync
            ? "VSYNC + DEADLINE"
            : "SOFTWARE DEADLINE",
        framePacingConfiguration_.targetRefreshHz);
    const std::string average =
        "AVG  " + timings(performance.average);
    const std::string percentile =
        "P95  " + timings(performance.percentile95);
    const std::string maximum =
        "MAX  " + timings(performance.maximum);
    const std::string simulation = fmt::format(
        "SIM ENEMIES {} | BLOCKERS {} | SUBSTEPS {}",
        workload.activeEnemies,
        workload.activeBlockers,
        workload.enemySubsteps);
    const std::string navigation = fmt::format(
        "NAV QUERY {} | DEFERRED {} | NEIGHBOR {}",
        workload.navigationQueries,
        workload.navigationRefreshesDeferred,
        workload.neighborCandidatesExamined);
    const std::string collision = fmt::format(
        "LOS TESTS {} | MOVE TESTS {}",
        workload.lineOfSightBlockersExamined,
        workload.movementBlockersExamined);
    performanceOverlayLines_ = {
        header, average, percentile, maximum,
        pacing, simulation, navigation, collision};
}

void App::renderBaseAllocation()
{
    const SDL_FRect panel{40.0F, 70.0F, 1200.0F, 600.0F};
    SDL_SetRenderDrawColor(renderer_, 18, 30, 27, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 92, 176, 142, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(
        renderer_, 80.0F, 100.0F,
        "ALLOCATION & NEEDS | TEXT/GEOMETRY PLACEHOLDER");
    const BaseResourceState &state =
        gameSession_.profile().baseResources;
    const BaseResourceBundle demand = populationAdjustedDailyDemand(
        gameSession_.profile().basePopulation);
    const std::string demandSummary = fmt::format(
        "DAILY 00:00 NEEDS: {} RATIONS / {} HYGIENE / {} OPERATIONS SUPPORT / {} SECURITY",
        demand.food,
        demand.hygiene,
        demand.morale,
        demand.security);
    uiTextRenderer_.render(
        renderer_, 80.0F, 124.0F, demandSummary.c_str());
    const BaseOperationsDefinition &operationsDefinition =
        publishedContentRegistry().baseOperations();
    const BaseOperationalProjection operations = projectBaseOperations(
        state,
        operationsDefinition,
        demand);
    const WorldClockProjection clock = gameSession_.worldClockProjection();
    const std::string nextDemand = fmt::format(
        "NEXT DAILY NEED IN {}H {:02}M | RESOLVED DAILY CYCLES {}",
        clock.minutesUntilNextDay / kWorldMinutesPerHour,
        clock.minutesUntilNextDay % kWorldMinutesPerHour,
        state.resolvedDemandCycleCount);
    uiTextRenderer_.render(
        renderer_, 80.0F, 146.0F, nextDemand.c_str());
    const std::array<std::pair<const char *, std::uint32_t>, 4> values{{
        {"RATION STOCK", state.pool.food},
        {"HYGIENE", state.pool.hygiene},
        {"OPERATIONS SUPPORT", state.pool.morale},
        {"SECURITY", state.pool.security}}};
    const std::array<std::uint32_t, 4> shortages{
        state.lastShortfall.food,
        state.lastShortfall.hygiene,
        state.lastShortfall.morale,
        state.lastShortfall.security};
    const std::array<std::uint32_t, 4> dailyDemand{
        demand.food,
        demand.hygiene,
        demand.morale,
        demand.security};
    for (std::size_t index{}; index < values.size(); ++index)
    {
        const BaseOperationalTier resourceTier = projectBaseResourceTier(
            values[index].second,
            dailyDemand[index],
            operationsDefinition);
        const float y = 174.0F + static_cast<float>(index) * 76.0F;
        const SDL_FRect track{80.0F, y + 23.0F, 450.0F, 24.0F};
        const SDL_FRect fill{
            track.x,
            track.y,
            track.w * static_cast<float>(values[index].second) / 100.0F,
            track.h};
        SDL_SetRenderDrawColor(renderer_, 38, 48, 44, 255);
        SDL_RenderFillRect(renderer_, &track);
        const SDL_Color fillColor = resourceTier == BaseOperationalTier::Critical
            ? SDL_Color{148, 72, 62, 255}
            : resourceTier == BaseOperationalTier::Strained
                ? SDL_Color{164, 132, 58, 255}
                : resourceTier == BaseOperationalTier::Supported
                    ? SDL_Color{74, 178, 138, 255}
                    : SDL_Color{68, 152, 116, 255};
        SDL_SetRenderDrawColor(
            renderer_,
            fillColor.r,
            fillColor.g,
            fillColor.b,
            fillColor.a);
        SDL_RenderFillRect(renderer_, &fill);
        SDL_SetRenderDrawColor(renderer_, 128, 190, 164, 255);
        SDL_RenderRect(renderer_, &track);
        const char *tier = baseOperationalTierName(resourceTier);
        const std::string label = fmt::format(
            "{} {}/100 | {}{}",
            values[index].first,
            values[index].second,
            tier,
            shortages[index] > 0U
                ? fmt::format(" | LAST SHORTFALL {}", shortages[index])
                : "");
        uiTextRenderer_.render(renderer_, 80.0F, y, label.c_str());
    }
    const std::string operationsStatus = fmt::format(
        "BASE OPERATIONS {} | LIMITING {}",
        baseOperationalTierName(operations.tier),
        baseResourceKindName(operations.limitingResource));
    uiTextRenderer_.render(
        renderer_, 80.0F, 474.0F, operationsStatus.c_str());
    const std::string cycles = fmt::format(
        "DAY {} {:02}:{:02} {} | SHORTAGE NEVER BLOCKS PLAY",
        clock.day,
        clock.hour,
        clock.minute,
        worldTimeOfDayName(clock.timeOfDay));
    uiTextRenderer_.render(renderer_, 80.0F, 500.0F, cycles.c_str());

    const BaseMoraleState &morale = gameSession_.profile().baseMorale;
    const BaseMoraleDailyLedger &ledger = morale.lastLedger;
    const BaseMoraleDefinition &moraleRules =
        publishedContentRegistry().baseMorale();
    const BaseCommunityEventDefinition &communityEvent =
        publishedContentRegistry().baseCommunityEvent(
            gameSession_.profile().baseCommunityEvent.definitionId);
    const std::uint64_t nextEventDay =
        (gameSession_.profile().baseCommunityEvent.cycleIndex + 1U) *
        moraleRules.eventCycleDays;
    const std::uint64_t daysUntilEvent = nextEventDay > clock.completedDays
        ? nextEventDay - clock.completedDays
        : 0U;
    const std::string moraleSummary = fmt::format(
        "RESIDENT MORALE {} | TREND {} | LOW DAYS {}",
        baseMoraleTierName(morale.tier),
        baseMoraleTrendName(morale.trend),
        morale.consecutiveLowDays);
    uiTextRenderer_.render(
        renderer_, 80.0F, 526.0F, moraleSummary.c_str());
    const bool resourceShortage = ledger.resourceShortfall.food > 0U ||
        ledger.resourceShortfall.hygiene > 0U ||
        ledger.resourceShortfall.morale > 0U ||
        ledger.resourceShortfall.security > 0U;
    const std::string moraleReport = fmt::format(
        "LAST DAILY REPORT | SCORE {} | SHORTAGE {} | BED {} | WISH +{}/-{} | EVENT +{}/-{}",
        ledger.netScore,
        resourceShortage ? 1 : 0,
        ledger.bedShortfall,
        ledger.fulfilledWishCount,
        ledger.missedWishCount,
        ledger.positiveEventCount,
        ledger.negativeEventCount);
    uiTextRenderer_.render(
        renderer_, 80.0F, 550.0F, moraleReport.c_str());
    const std::string eventSummary = fmt::format(
        "ACTIVE EVENT {} | EFFECT {:+} | NEXT ROTATION {}D",
        communityEvent.displayName,
        communityEvent.moraleEffect,
        daysUntilEvent);
    uiTextRenderer_.render(
        renderer_, 80.0F, 574.0F, eventSummary.c_str());
    const std::string productionSummary = fmt::format(
        "PRODUCTION DURATION {}% | APPLIES WHEN ORDER STARTS",
        baseManufacturingDurationPercent(morale.tier, moraleRules));
    uiTextRenderer_.render(
        renderer_, 80.0F, 598.0F, productionSummary.c_str());

    const BasePriorityState &priorityState =
        gameSession_.profile().basePriority;
    const BasePriorityDefinition &priority =
        publishedContentRegistry().basePriority(
            priorityState.definitionId);
    const ItemDefinition &priorityItem =
        publishedContentRegistry().item(
            priority.requiredItemDefinitionId);
    const std::uint64_t cycleMinutes =
        publishedContentRegistry().basePriorityCycleMinutes();
    const std::uint64_t elapsedSinceProfileStart =
        gameSession_.profile().worldClock.elapsedWorldMinutes <=
                kInitialWorldMinute
            ? 0U
            : gameSession_.profile().worldClock.elapsedWorldMinutes -
                  kInitialWorldMinute;
    const std::uint64_t remainingMinutes = cycleMinutes -
        elapsedSinceProfileStart % cycleMinutes;
    uiTextRenderer_.render(
        renderer_, 650.0F, 116.0F,
        "CURRENT BASE WISH - ONE LOW-PRESENCE PRIORITY");
    const std::string priorityName = fmt::format(
        "{} | {}",
        priority.displayName,
        priorityState.fulfilled ? "FULFILLED" : "ACTIVE");
    uiTextRenderer_.render(
        renderer_, 650.0F, 142.0F, priorityName.c_str());
    const std::string priorityRequirement = fmt::format(
        "NEEDS {} x{} FROM BASE-ACCESSIBLE INVENTORY",
        priorityItem.displayName,
        priority.requiredQuantity);
    uiTextRenderer_.render(
        renderer_, 650.0F, 166.0F, priorityRequirement.c_str());
    const BaseResourceBundle &wishReward = priority.resourceReward;
    const std::string priorityTiming = fmt::format(
        "IMPROVES F{} H{} M{} S{} | {}H {:02}M LEFT | MISSED {}",
        wishReward.food,
        wishReward.hygiene,
        wishReward.morale,
        wishReward.security,
        remainingMinutes / kWorldMinutesPerHour,
        remainingMinutes % kWorldMinutesPerHour,
        priorityState.missedCycleCount);
    uiTextRenderer_.render(
        renderer_, 650.0F, 190.0F, priorityTiming.c_str());
    constexpr std::array<BaseSupplyCategory, 4> categories{
        BaseSupplyCategory::Food,
        BaseSupplyCategory::Medical,
        BaseSupplyCategory::Recreation,
        BaseSupplyCategory::Security};
    for (std::size_t index{}; index < categories.size(); ++index)
    {
        const SDL_FRect tab{
            650.0F + static_cast<float>(index) * 126.0F,
            218.0F,
            120.0F,
            34.0F};
        const bool selected = categories[index] ==
            selectedBaseSupplyCategory_;
        SDL_SetRenderDrawColor(
            renderer_, selected ? 58 : 34, selected ? 104 : 54,
            selected ? 88 : 50, 255);
        SDL_RenderFillRect(renderer_, &tab);
        SDL_SetRenderDrawColor(renderer_, 120, 190, 162, 255);
        SDL_RenderRect(renderer_, &tab);
        uiTextRenderer_.render(
            renderer_, tab.x + 12.0F, tab.y + 13.0F,
            baseSupplyCategoryName(categories[index]));
    }
    const auto allocation = baseSupplyDefinitions(
        gameSession_.profile(), selectedBaseSupplyCategory_);
    if (allocation.empty())
    {
        uiTextRenderer_.render(
            renderer_, 650.0F, 272.0F,
            "NO OWNED ITEMS CAN SUPPLY THIS CATEGORY");
    }
    for (std::size_t index{};
         index < allocation.size() && index < 7U;
         ++index)
    {
        const BaseSupplyDefinitionProjection &projection = allocation[index];
        const ItemDefinition &definition = *projection.definition;
        const SDL_FRect row{
            650.0F,
            262.0F + static_cast<float>(index) * 40.0F,
            500.0F,
            34.0F};
        SDL_SetRenderDrawColor(
            renderer_,
            projection.assignedToSelectedCategory ? 42 : 34,
            projection.assignedToSelectedCategory ? 84 : 62,
            projection.assignedToSelectedCategory ? 68 : 54,
            255);
        SDL_RenderFillRect(renderer_, &row);
        const bool selected = projection.firstAssetId.has_value() &&
            profileAssetSelection_.has_value() &&
            *projection.firstAssetId == profileAssetSelection_->instanceId;
        SDL_SetRenderDrawColor(
            renderer_, selected ? 232 : 98, selected ? 206 : 168,
            selected ? 126 : 120, 255);
        SDL_RenderRect(renderer_, &row);
        const SDL_FRect check{row.x + 8.0F, row.y + 7.0F, 20.0F, 20.0F};
        SDL_SetRenderDrawColor(
            renderer_,
            projection.assignedToSelectedCategory ? 76 : 28,
            projection.assignedToSelectedCategory ? 166 : 52,
            projection.assignedToSelectedCategory ? 122 : 48,
            255);
        SDL_RenderFillRect(renderer_, &check);
        SDL_SetRenderDrawColor(renderer_, 170, 216, 194, 255);
        SDL_RenderRect(renderer_, &check);
        if (projection.assignedToSelectedCategory)
        {
            uiTextRenderer_.render(
                renderer_, check.x + 5.0F, check.y + 10.0F, "X");
        }
        std::string assignment;
        const auto existing =
            gameSession_.profile().baseSupplyPolicy.assignments.find(
                definition.definitionId);
        if (projection.assignedElsewhere &&
            existing != gameSession_.profile()
                            .baseSupplyPolicy.assignments.end())
        {
            assignment = fmt::format(
                " | ASSIGNED {}",
                baseSupplyCategoryName(existing->second));
        }
        const std::string label = fmt::format(
            "{} x{} | +{} PER ITEM{}",
            definition.displayName,
            projection.ownedQuantity,
            baseSupplyContribution(
                definition, selectedBaseSupplyCategory_),
            assignment);
        uiTextRenderer_.render(
            renderer_, row.x + 38.0F, row.y + 13.0F, label.c_str());
    }

    const SDL_FRect materialButton{650.0F, 604.0F, 230.0F, 42.0F};
    const SDL_FRect priorityButton{920.0F, 604.0F, 230.0F, 42.0F};
    bool canProcessMaterial{};
    const char *materialLabel = "SELECT SALVAGE";
    if (profileAssetSelection_.has_value())
    {
        const ConstructionMaterialPlan plan =
            queryConstructionMaterialContribution(
                gameSession_.profile(),
                publishedContentRegistry(),
                ContributeConstructionMaterialCommand{
                    profileAssetSelection_->instanceId});
        canProcessMaterial = plan.canCommit;
        materialLabel = plan.canCommit
            ? "PROCESS FOR BUILDING"
            : "NOT BUILDING MATERIAL";
    }
    SDL_SetRenderDrawColor(
        renderer_, canProcessMaterial ? 92 : 62,
        canProcessMaterial ? 108 : 68,
        canProcessMaterial ? 132 : 66, 255);
    SDL_RenderFillRect(renderer_, &materialButton);
    bool canFulfillPriority{};
    const char *priorityLabel = priorityState.fulfilled
        ? "BASE WISH ALREADY FULFILLED"
        : "SELECT MATCHING ACCESSIBLE ITEM";
    if (profileAssetSelection_.has_value() && !priorityState.fulfilled)
    {
        const BasePriorityPlan plan = queryBasePrioritySubmission(
            gameSession_.profile(),
            publishedContentRegistry(),
            SubmitBasePriorityCommand{
                profileAssetSelection_->instanceId});
        canFulfillPriority = plan.canCommit;
        priorityLabel = plan.canCommit
            ? "FULFILL CURRENT BASE WISH"
            : "SELECTED ITEM DOES NOT MATCH WISH";
    }
    SDL_SetRenderDrawColor(
        renderer_, canFulfillPriority ? 82 : 62,
        canFulfillPriority ? 126 : 68,
        canFulfillPriority ? 112 : 66, 255);
    SDL_RenderFillRect(renderer_, &priorityButton);
    SDL_SetRenderDrawColor(renderer_, 154, 202, 184, 255);
    SDL_RenderRect(renderer_, &materialButton);
    SDL_RenderRect(renderer_, &priorityButton);
    uiTextRenderer_.render(
        renderer_, 650.0F, 562.0F,
        "CHECK A DEFINITION TO AUTHORIZE AUTOMATIC USE AT DAILY NEEDS");
    uiTextRenderer_.render(
        renderer_, 650.0F, 582.0F,
        "ITEMS KEEP THEIR REAL LOCATION UNTIL THE BASE CONSUMES THEM");
    uiTextRenderer_.render(
        renderer_, materialButton.x + 24.0F,
        materialButton.y + 18.0F,
        materialLabel);
    uiTextRenderer_.render(
        renderer_, priorityButton.x + 24.0F,
        priorityButton.y + 18.0F,
        priorityLabel);
    uiTextRenderer_.render(renderer_, 80.0F, 646.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(renderer_, 470.0F, 646.0F, uiMessage_.c_str());
    }
}

void App::renderBaseMedicalService()
{
    const SDL_FRect panel{180.0F, 76.0F, 920.0F, 570.0F};
    SDL_SetRenderDrawColor(renderer_, 20, 29, 30, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 118, 186, 178, 255);
    SDL_RenderRect(renderer_, &panel);

    const ProfileState &profile = gameSession_.profile();
    const BaseMedicalServicePlan playerPlan = queryBaseMedicalService(
        profile,
        publishedContentRegistry());
    const ResidentTreatmentPlan residentPlan = queryStartResidentTreatment(
        profile,
        publishedContentRegistry());
    const BaseResidentMedicalProjection residents =
        projectBaseResidentMedical(profile);
    const char *bleeding =
        profile.medicalStatus.bleeding == BleedingSeverity::Heavy
        ? "HEAVY BLEEDING"
        : profile.medicalStatus.bleeding == BleedingSeverity::Light
            ? "LIGHT BLEEDING"
            : "NO BLEEDING";
    const char *pain = hasPain(profile.medicalStatus)
        ? (painIsSuppressed(profile.medicalStatus)
            ? "PAIN SUPPRESSED"
            : "PAIN ACTIVE")
        : "NO INJURY PAIN";

    SDL_SetRenderDrawColor(renderer_, 224, 240, 234, 255);
    uiTextRenderer_.render(
        renderer_, 220.0F, 108.0F,
        "MEDICAL SERVICE | TEXT/GEOMETRY PLACEHOLDER");
    const std::string currency = fmt::format(
        "CURRENCY {}",
        profile.currency);
    uiTextRenderer_.render(
        renderer_, 900.0F, 108.0F, currency.c_str());

    const std::string playerStatus = fmt::format(
        "PLAYER SERVICE | HEALTH {}/100 | {} | {}",
        profile.currentHealth,
        bleeding,
        pain);
    uiTextRenderer_.render(renderer_, 230.0F, 164.0F, playerStatus.c_str());
    const std::string painkiller = fmt::format(
        "PAINKILLER REMAINING {} SEC",
        profile.medicalStatus.painkillerRemainingMs / 1000U);
    uiTextRenderer_.render(
        renderer_, 230.0F, 194.0F, painkiller.c_str());

    std::string status;
    if (playerPlan.quotedCurrency > 0)
    {
        status = fmt::format(
            "TREATMENT QUOTE {} | HP {} + INJURY {} | {}",
            playerPlan.quotedCurrency,
            playerPlan.healthCost,
            playerPlan.injuryCost,
            playerPlan.canCommit ? "READY" : "INSUFFICIENT CURRENCY");
    }
    else
    {
        status = playerPlan.message;
    }
    SDL_SetRenderDrawColor(
        renderer_,
        playerPlan.canCommit ? 176 : 218,
        playerPlan.canCommit ? 232 : 190,
        playerPlan.canCommit ? 216 : 150,
        255);
    uiTextRenderer_.render(renderer_, 230.0F, 224.0F, status.c_str());

    const SDL_FRect button = baseMedicalServiceButton();
    SDL_SetRenderDrawColor(
        renderer_,
        playerPlan.canCommit ? 48 : 42,
        playerPlan.canCommit ? 104 : 50,
        playerPlan.canCommit ? 94 : 52,
        255);
    SDL_RenderFillRect(renderer_, &button);
    SDL_SetRenderDrawColor(renderer_, 206, 236, 228, 255);
    SDL_RenderRect(renderer_, &button);
    uiTextRenderer_.render(
        renderer_,
        button.x + 54.0F,
        button.y + 17.0F,
        playerPlan.canCommit ? "PAY & TREAT" : "TREATMENT UNAVAILABLE");

    SDL_SetRenderDrawColor(renderer_, 94, 130, 124, 255);
    const SDL_FRect divider{220.0F, 326.0F, 840.0F, 1.0F};
    SDL_RenderFillRect(renderer_, &divider);
    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    const std::string facility = fmt::format(
        "MEDICAL LEVEL {} | ASSIGNED {} | GENERAL FALLBACK IS 50% SLOWER",
        profile.baseConstruction.medicalLevel,
        workforce.medicalWorker.has_value()
            ? baseResidentProfessionName(*workforce.medicalWorker)
            : "EMPTY");
    uiTextRenderer_.render(renderer_, 230.0F, 334.0F, facility.c_str());
    const std::string residentStatus = fmt::format(
        "RESIDENT CARE | RESIDENTS {} | INJURED {} | HEALTHY {}",
        residents.ordinaryResidents,
        residents.injuredResidents,
        residents.healthyResidents);
    SDL_SetRenderDrawColor(renderer_, 224, 240, 234, 255);
    uiTextRenderer_.render(
        renderer_, 230.0F, 366.0F, residentStatus.c_str());

    std::string residentDetail;
    if (residents.treatmentActive)
    {
        residentDetail = fmt::format(
            "TREATMENT ACTIVE | READY IN {} H {} MIN",
            residents.remainingMinutes / kWorldMinutesPerHour,
            residents.remainingMinutes % kWorldMinutesPerHour);
    }
    else if (residentPlan.canCommit)
    {
        std::string supplies;
        for (std::size_t index{}; index < residentPlan.supplies.size(); ++index)
        {
            const ResidentMedicalSupplySelection &selection =
                residentPlan.supplies[index];
            if (index != 0U)
            {
                supplies += " + ";
            }
            supplies += fmt::format(
                "{} x{}",
                publishedContentRegistry().item(selection.definitionId)
                    .displayName,
                selection.quantity);
        }
        residentDetail = fmt::format(
            "AUTHORIZED SUPPLIES {} | CONTRIBUTION {}/{} | {} MIN",
            supplies,
            residentPlan.plannedContribution,
            residentPlan.requiredContribution,
            residentPlan.durationMinutes);
    }
    else
    {
        residentDetail = residentPlan.message;
    }
    SDL_SetRenderDrawColor(
        renderer_,
        residentPlan.canCommit ? 176 : 218,
        residentPlan.canCommit ? 232 : 190,
        residentPlan.canCommit ? 216 : 150,
        255);
    uiTextRenderer_.render(
        renderer_, 230.0F, 408.0F, residentDetail.c_str());
    uiTextRenderer_.render(
        renderer_, 230.0F, 436.0F,
        "USES ONLY ITEMS ASSIGNED TO MEDICAL SUPPLY | CONSUMED ON START");

    const SDL_FRect residentButton = baseResidentTreatmentButton();
    SDL_SetRenderDrawColor(
        renderer_,
        residentPlan.canCommit ? 48 : 42,
        residentPlan.canCommit ? 104 : 50,
        residentPlan.canCommit ? 94 : 52,
        255);
    SDL_RenderFillRect(renderer_, &residentButton);
    SDL_SetRenderDrawColor(renderer_, 206, 236, 228, 255);
    SDL_RenderRect(renderer_, &residentButton);
    uiTextRenderer_.render(
        renderer_,
        residentButton.x + 42.0F,
        residentButton.y + 17.0F,
        residentPlan.canCommit
            ? "START RESIDENT TREATMENT"
            : residents.treatmentActive
                ? "RESIDENT TREATMENT ACTIVE"
                : "RESIDENT TREATMENT UNAVAILABLE");

    const SDL_FRect staffingButton = baseFacilityStaffingButton();
    SDL_SetRenderDrawColor(renderer_, 42, 76, 70, 255);
    SDL_RenderFillRect(renderer_, &staffingButton);
    SDL_SetRenderDrawColor(renderer_, 206, 236, 228, 255);
    SDL_RenderRect(renderer_, &staffingButton);
    uiTextRenderer_.render(
        renderer_, staffingButton.x + 24.0F, staffingButton.y + 14.0F,
        workforce.medicalWorker.has_value()
            ? "CLEAR MEDICAL WORKER"
            : "ASSIGN BEST WORKER");

    const auto &projects =
        publishedContentRegistry().baseConstructionProjects();
    const auto upgrade = std::find_if(
        projects.begin(), projects.end(),
        [](const BaseConstructionProjectDefinition &candidate)
        {
            return candidate.target == BaseFacilityUpgradeTarget::Medical;
        });
    const SDL_FRect upgradeButton = baseFacilityUpgradeButton();
    bool upgradeAvailable{};
    std::string upgradeLabel = "NO MEDICAL UPGRADE";
    if (upgrade != projects.end())
    {
        const auto &active = profile.baseConstruction.activeProject;
        const bool matchingActive = active.has_value() &&
            active->definitionId == upgrade->id;
        const BaseConstructionPlan plan = matchingActive
            ? queryCancelBaseConstruction(
                profile,
                publishedContentRegistry(),
                CancelBaseConstructionCommand{upgrade->id})
            : queryStartBaseConstruction(
                profile,
                publishedContentRegistry(),
                StartBaseConstructionCommand{upgrade->id});
        upgradeAvailable = plan.canCommit;
        upgradeLabel = matchingActive
            ? "CANCEL MEDICAL UPGRADE"
            : profile.baseConstruction.medicalLevel >= upgrade->targetLevel
                ? "MEDICAL LEVEL 2 COMPLETE"
                : "UPGRADE MEDICAL TO LEVEL 2";
    }
    SDL_SetRenderDrawColor(
        renderer_, upgradeAvailable ? 54 : 42,
        upgradeAvailable ? 90 : 52,
        upgradeAvailable ? 82 : 54, 255);
    SDL_RenderFillRect(renderer_, &upgradeButton);
    SDL_SetRenderDrawColor(renderer_, 206, 236, 228, 255);
    SDL_RenderRect(renderer_, &upgradeButton);
    uiTextRenderer_.render(
        renderer_, upgradeButton.x + 20.0F, upgradeButton.y + 14.0F,
        upgradeLabel.c_str());

    SDL_SetRenderDrawColor(renderer_, 190, 208, 202, 255);
    uiTextRenderer_.render(
        renderer_, 230.0F, 590.0F,
        "CURRENCY ONLY | PERSONAL MEDICAL ITEMS ARE NOT CONSUMED | ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_SetRenderDrawColor(renderer_, 228, 220, 156, 255);
        uiTextRenderer_.render(
            renderer_, 230.0F, 608.0F, uiMessage_.c_str());
    }
}

void App::renderBaseDormitory()
{
    const SDL_FRect panel{120.0F, 90.0F, 1040.0F, 560.0F};
    SDL_SetRenderDrawColor(renderer_, 22, 28, 36, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 132, 144, 174, 255);
    SDL_RenderRect(renderer_, &panel);

    const ProfileState &profile = gameSession_.profile();
    const BasePopulationProjection population = projectBasePopulation(
        profile.basePopulation);
    const BaseResourceBundle demand = populationAdjustedDailyDemand(
        profile.basePopulation);
    const WorldClockProjection clock = gameSession_.worldClockProjection();
    SDL_SetRenderDrawColor(renderer_, 224, 232, 242, 255);
    uiTextRenderer_.render(
        renderer_, 170.0F, 128.0F,
        "DORMITORY & REST | AGGREGATED RESIDENTS");

    const std::string residents = fmt::format(
        "ORDINARY RESIDENTS {} | INJURED {} | HEALTHY {} | BEDS {} | BED SHORTFALL {}",
        population.ordinaryResidents,
        population.injuredResidents,
        population.healthyResidents,
        population.bedCapacity,
        population.bedShortfall);
    uiTextRenderer_.render(renderer_, 170.0F, 194.0F, residents.c_str());
    const std::string rations = fmt::format(
        "DAILY RATIONS {} | RESERVE {} | NEXT NEED IN {}H {:02}M",
        demand.food,
        profile.baseResources.pool.food,
        clock.minutesUntilNextDay / kWorldMinutesPerHour,
        clock.minutesUntilNextDay % kWorldMinutesPerHour);
    uiTextRenderer_.render(renderer_, 170.0F, 236.0F, rations.c_str());
    const std::string time = fmt::format(
        "DAY {} {:02}:{:02} {}",
        clock.day,
        clock.hour,
        clock.minute,
        worldTimeOfDayName(clock.timeOfDay));
    uiTextRenderer_.render(renderer_, 170.0F, 278.0F, time.c_str());

    SDL_SetRenderDrawColor(
        renderer_,
        population.bedShortfall > 0U ? 224 : 166,
        population.bedShortfall > 0U ? 164 : 214,
        population.bedShortfall > 0U ? 124 : 194,
        255);
    uiTextRenderer_.render(
        renderer_, 170.0F, 316.0F,
        population.bedShortfall > 0U
            ? "OVERCROWDED | SHORTAGE IS NON-BLOCKING"
            : "BED CAPACITY AVAILABLE");
    const BaseConstructionProjection construction = projectBaseConstruction(
        profile,
        publishedContentRegistry());
    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    const auto &projects =
        publishedContentRegistry().baseConstructionProjects();
    const auto dormitoryProject = std::find_if(
        projects.begin(), projects.end(),
        [](const BaseConstructionProjectDefinition &candidate)
        {
            return candidate.target == BaseFacilityUpgradeTarget::Dormitory;
        });
    if (dormitoryProject == projects.end())
    {
        return;
    }
    const BaseConstructionProjectDefinition &project = *dormitoryProject;
    SDL_SetRenderDrawColor(renderer_, 184, 194, 210, 255);
    const std::string professions = fmt::format(
        "PROFESSIONS | GENERAL {} | MEDICAL {} | ENGINEERING {} | COMBAT {}",
        workforce.residentsByProfession[baseProfessionIndex(
            BaseResidentProfession::General)],
        workforce.residentsByProfession[baseProfessionIndex(
            BaseResidentProfession::Medical)],
        workforce.residentsByProfession[baseProfessionIndex(
            BaseResidentProfession::Engineering)],
        workforce.residentsByProfession[baseProfessionIndex(
            BaseResidentProfession::Combat)]);
    uiTextRenderer_.render(renderer_, 170.0F, 346.0F, professions.c_str());
    const std::string staffing = fmt::format(
        "STAFFING | WORKSHOP {} | MEDICAL {} | UNASSIGNED {}",
        workforce.workshopWorker.has_value()
            ? baseResidentProfessionName(*workforce.workshopWorker)
            : "EMPTY",
        workforce.medicalWorker.has_value()
            ? baseResidentProfessionName(*workforce.medicalWorker)
            : "EMPTY",
        workforce.availableResidents);
    uiTextRenderer_.render(renderer_, 170.0F, 370.0F, staffing.c_str());
    const std::string constructionSummary = fmt::format(
        "DORMITORY LEVEL {} | BUILDING MATERIAL {}/{} | WORKERS {}/{} AVAILABLE",
        construction.dormitoryLevel,
        construction.materialUnits,
        construction.maximumMaterialUnits,
        construction.availableWorkers,
        construction.totalWorkers);
    uiTextRenderer_.render(
        renderer_, 170.0F, 466.0F, constructionSummary.c_str());

    std::string projectStatus;
    bool constructionAvailable{};
    const char *constructionLabel{};
    if (construction.activeProjectId.has_value() &&
        construction.activeProjectId == project.id)
    {
        projectStatus = fmt::format(
            "EXPANSION IN PROGRESS | {}H {:02}M LEFT | {} WORKERS COMMITTED",
            construction.remainingMinutes / kWorldMinutesPerHour,
            construction.remainingMinutes % kWorldMinutesPerHour,
            construction.committedWorkers);
        constructionAvailable = true;
        constructionLabel = "CANCEL & REFUND MATERIAL";
    }
    else if (construction.activeProjectId.has_value())
    {
        projectStatus = "ANOTHER BASE CONSTRUCTION PROJECT IS ACTIVE";
        constructionLabel = "DORMITORY EXPANSION BLOCKED";
    }
    else if (construction.dormitoryLevel >= project.targetLevel)
    {
        projectStatus = "DORMITORY EXPANSION COMPLETE";
        constructionLabel = "NO FURTHER PROJECT";
    }
    else
    {
        const BaseConstructionPlan plan = queryStartBaseConstruction(
            profile,
            publishedContentRegistry(),
            StartBaseConstructionCommand{project.id});
        constructionAvailable = plan.canCommit;
        projectStatus = fmt::format(
            "NEXT: LEVEL {} / {} BEDS | COST {} MATERIAL / {} WORKERS / {}H",
            project.targetLevel,
            project.bedCapacityAfter,
            project.materialCost,
            project.workerCount,
            project.durationMinutes / kWorldMinutesPerHour);
        constructionLabel = plan.canCommit
            ? "START DORMITORY EXPANSION"
            : "EXPANSION REQUIREMENTS NOT MET";
    }
    uiTextRenderer_.render(
        renderer_, 170.0F, 490.0F, projectStatus.c_str());
    const SDL_FRect autoFillButton = baseWorkforceAutoFillButton();
    SDL_SetRenderDrawColor(renderer_, 58, 86, 106, 255);
    SDL_RenderFillRect(renderer_, &autoFillButton);
    SDL_SetRenderDrawColor(renderer_, 170, 188, 216, 255);
    SDL_RenderRect(renderer_, &autoFillButton);
    uiTextRenderer_.render(
        renderer_, autoFillButton.x + 34.0F,
        autoFillButton.y + 17.0F,
        "AUTO-FILL EMPTY FACILITY JOBS");
    const SDL_FRect constructionButton = baseConstructionButton();
    SDL_SetRenderDrawColor(
        renderer_, constructionAvailable ? 66 : 52,
        constructionAvailable ? 102 : 58,
        constructionAvailable ? 124 : 64, 255);
    SDL_RenderFillRect(renderer_, &constructionButton);
    SDL_SetRenderDrawColor(renderer_, 170, 188, 216, 255);
    SDL_RenderRect(renderer_, &constructionButton);
    uiTextRenderer_.render(
        renderer_, constructionButton.x + 28.0F,
        constructionButton.y + 22.0F,
        constructionLabel);

    uiTextRenderer_.render(
        renderer_, 170.0F, 518.0F,
        "REST ADVANCES WORLD TIME AND DAILY NEEDS | MAXIMUM 12H");

    constexpr std::array<const char *, 3> labels{
        "REST 1 HOUR", "REST 6 HOURS", "REST 12 HOURS"};
    for (std::size_t index{}; index < labels.size(); ++index)
    {
        const SDL_FRect button = baseRestButton(index);
        SDL_SetRenderDrawColor(renderer_, 52, 66, 84, 255);
        SDL_RenderFillRect(renderer_, &button);
        SDL_SetRenderDrawColor(renderer_, 160, 178, 210, 255);
        SDL_RenderRect(renderer_, &button);
        uiTextRenderer_.render(
            renderer_, button.x + 44.0F, button.y + 20.0F, labels[index]);
    }
    SDL_SetRenderDrawColor(renderer_, 194, 204, 218, 255);
    uiTextRenderer_.render(renderer_, 170.0F, 610.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_SetRenderDrawColor(renderer_, 230, 218, 154, 255);
        uiTextRenderer_.render(
            renderer_, 420.0F, 610.0F, uiMessage_.c_str());
    }
}

void App::renderBaseWorkshop()
{
    const SDL_FRect panel{120.0F, 90.0F, 1040.0F, 560.0F};
    SDL_SetRenderDrawColor(renderer_, 30, 27, 22, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 176, 142, 92, 255);
    SDL_RenderRect(renderer_, &panel);

    const ProfileState &profile = gameSession_.profile();
    const BaseFacilityDefinitionId workshopFacility{
        "base_facility.workshop"};
    if (!baseFacilityInstalled(profile, workshopFacility))
    {
        SDL_SetRenderDrawColor(renderer_, 232, 222, 196, 255);
        uiTextRenderer_.render(
            renderer_, 170.0F, 128.0F,
            "WORKSHOP & PRODUCTION | FACILITY RESERVE");
        uiTextRenderer_.render(
            renderer_, 170.0F, 200.0F,
            "WORKSHOP IS PACKED | PRODUCTION AND UPGRADES ARE PAUSED");
        uiTextRenderer_.render(
            renderer_, 170.0F, 228.0F,
            "INSTALL IT FREE ON THE REGIONAL PAGE TO RESUME EXACT PROGRESS");
        uiTextRenderer_.render(renderer_, 170.0F, 610.0F, "ESC CLOSE");
        return;
    }
    const BaseManufacturingRecipeDefinition &recipe =
        publishedContentRegistry().baseManufacturingRecipes().front();
    const BaseManufacturingProjection projection =
        projectBaseManufacturing(profile);
    SDL_SetRenderDrawColor(renderer_, 232, 222, 196, 255);
    uiTextRenderer_.render(
        renderer_, 170.0F, 128.0F,
        "WORKSHOP & PRODUCTION | TEXT/GEOMETRY PLACEHOLDER");
    uiTextRenderer_.render(
        renderer_, 170.0F, 178.0F, recipe.displayName.c_str());

    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    const std::string worker = fmt::format(
        "WORKSHOP LEVEL {} | ASSIGNED {} | GENERAL FALLBACK IS 50% SLOWER",
        profile.baseConstruction.workshopLevel,
        workforce.workshopWorker.has_value()
            ? baseResidentProfessionName(*workforce.workshopWorker)
            : "EMPTY");
    uiTextRenderer_.render(renderer_, 170.0F, 204.0F, worker.c_str());

    float y = 234.0F;
    uiTextRenderer_.render(renderer_, 170.0F, y, "REQUIRED INPUTS");
    for (const BaseManufacturingInputDefinition &input : recipe.inputs)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(input.itemDefinitionId);
        const std::string row = fmt::format(
            "{} x{}", definition.displayName, input.quantity);
        y += 30.0F;
        uiTextRenderer_.render(renderer_, 190.0F, y, row.c_str());
    }
    const ItemDefinition &outputDefinition =
        publishedContentRegistry().item(recipe.outputItemDefinitionId);
    const std::uint32_t moraleDuration = applyBaseMoraleDurationPercent(
        recipe.durationMinutes,
        profile.baseMorale.tier,
        publishedContentRegistry().baseMorale());
    const std::uint32_t staffedDuration = workforce.workshopWorker.has_value()
        ? applyBaseFacilityTaskDuration(
            moraleDuration,
            BaseFacilityStaffingKind::Workshop,
            *workforce.workshopWorker,
            profile.baseConstruction.workshopLevel,
            publishedContentRegistry().baseWorkforce())
        : 0U;
    const std::uint32_t adjustedDuration = staffedDuration == 0U
        ? 0U
        : applyActiveBaseSiteManufacturingDuration(
            staffedDuration, profile, publishedContentRegistry());
    const std::uint32_t sitePercent =
        activeBaseSiteManufacturingDurationPercent(
            profile, publishedContentRegistry());
    const std::string output = fmt::format(
        "OUTPUT {} x{} | {} WORKER | BASE {}H | SITE {}% | CURRENT {}H {:02}M",
        outputDefinition.displayName,
        recipe.outputQuantity,
        recipe.workerCount,
        recipe.durationMinutes / kWorldMinutesPerHour,
        sitePercent,
        adjustedDuration / kWorldMinutesPerHour,
        adjustedDuration % kWorldMinutesPerHour);
    uiTextRenderer_.render(renderer_, 170.0F, 338.0F, output.c_str());

    std::string status;
    const char *buttonLabel{};
    bool available{};
    if (!projection.orderPresent)
    {
        const BaseManufacturingStartPlan plan =
            queryStartBaseManufacturing(
                profile,
                publishedContentRegistry(),
                StartBaseManufacturingCommand{recipe.id});
        status = plan.canCommit
            ? "WORKSHOP READY | INPUTS REMAIN IN OWNED INVENTORY UNTIL START"
            : plan.message;
        buttonLabel = plan.canCommit
            ? "START PRODUCTION"
            : "PRODUCTION REQUIREMENTS NOT MET";
        available = plan.canCommit;
    }
    else if (projection.outputReady)
    {
        const BaseManufacturingReturnPlan plan =
            queryCollectBaseManufacturing(
                profile, publishedContentRegistry());
        status = "OUTPUT READY | WORKER RELEASED";
        buttonLabel = plan.canCommit
            ? "COLLECT TO STASH"
            : "OUTPUT READY | STASH SPACE REQUIRED";
        available = plan.canCommit;
    }
    else
    {
        status = fmt::format(
            "PRODUCTION ACTIVE | {}H {:02}M LEFT | {} WORKER COMMITTED",
            projection.remainingMinutes / kWorldMinutesPerHour,
            projection.remainingMinutes % kWorldMinutesPerHour,
            projection.committedWorkers);
        const BaseManufacturingReturnPlan plan =
            queryCancelBaseManufacturing(
                profile, publishedContentRegistry());
        buttonLabel = plan.canCommit
            ? "CANCEL & RETURN INPUTS"
            : "CANCEL BLOCKED | STASH SPACE REQUIRED";
        available = plan.canCommit;
    }
    uiTextRenderer_.render(renderer_, 170.0F, 400.0F, status.c_str());

    const SDL_FRect button = baseManufacturingButton();
    SDL_SetRenderDrawColor(
        renderer_, available ? 92 : 54,
        available ? 104 : 58,
        available ? 72 : 56, 255);
    SDL_RenderFillRect(renderer_, &button);
    SDL_SetRenderDrawColor(renderer_, 206, 184, 132, 255);
    SDL_RenderRect(renderer_, &button);
    uiTextRenderer_.render(
        renderer_, button.x + 28.0F, button.y + 20.0F, buttonLabel);

    const SDL_FRect staffingButton = baseFacilityStaffingButton();
    SDL_SetRenderDrawColor(renderer_, 58, 76, 64, 255);
    SDL_RenderFillRect(renderer_, &staffingButton);
    SDL_SetRenderDrawColor(renderer_, 206, 184, 132, 255);
    SDL_RenderRect(renderer_, &staffingButton);
    uiTextRenderer_.render(
        renderer_, staffingButton.x + 24.0F, staffingButton.y + 14.0F,
        workforce.workshopWorker.has_value()
            ? "CLEAR WORKSHOP WORKER"
            : "ASSIGN BEST WORKER");

    const auto &projects =
        publishedContentRegistry().baseConstructionProjects();
    const auto upgrade = std::find_if(
        projects.begin(), projects.end(),
        [](const BaseConstructionProjectDefinition &candidate)
        {
            return candidate.target == BaseFacilityUpgradeTarget::Workshop;
        });
    const SDL_FRect upgradeButton = baseFacilityUpgradeButton();
    bool upgradeAvailable{};
    std::string upgradeLabel = "NO WORKSHOP UPGRADE";
    if (upgrade != projects.end())
    {
        const auto &active = profile.baseConstruction.activeProject;
        const bool matchingActive = active.has_value() &&
            active->definitionId == upgrade->id;
        const BaseConstructionPlan plan = matchingActive
            ? queryCancelBaseConstruction(
                profile,
                publishedContentRegistry(),
                CancelBaseConstructionCommand{upgrade->id})
            : queryStartBaseConstruction(
                profile,
                publishedContentRegistry(),
                StartBaseConstructionCommand{upgrade->id});
        upgradeAvailable = plan.canCommit;
        upgradeLabel = matchingActive
            ? "CANCEL WORKSHOP UPGRADE"
            : profile.baseConstruction.workshopLevel >= upgrade->targetLevel
                ? "WORKSHOP LEVEL 2 COMPLETE"
                : "UPGRADE WORKSHOP TO LEVEL 2";
    }
    SDL_SetRenderDrawColor(
        renderer_, upgradeAvailable ? 72 : 48,
        upgradeAvailable ? 86 : 52, 64, 255);
    SDL_RenderFillRect(renderer_, &upgradeButton);
    SDL_SetRenderDrawColor(renderer_, 206, 184, 132, 255);
    SDL_RenderRect(renderer_, &upgradeButton);
    uiTextRenderer_.render(
        renderer_, upgradeButton.x + 20.0F, upgradeButton.y + 14.0F,
        upgradeLabel.c_str());
    uiTextRenderer_.render(renderer_, 170.0F, 610.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        SDL_SetRenderDrawColor(renderer_, 230, 218, 154, 255);
        uiTextRenderer_.render(
            renderer_, 420.0F, 610.0F, uiMessage_.c_str());
    }
}

void App::renderBaseDeployment()
{
    if (regionalOperationsOpen_)
    {
        renderRegionalOperations();
        return;
    }
    if (lostRaidRecordsOpen_)
    {
        renderLostRaidRecords();
        return;
    }
    const SDL_FRect panel{kFlowPanelX, kFlowPanelY, kFlowPanelWidth, kFlowPanelHeight};
    SDL_SetRenderDrawColor(renderer_, 28, 18, 18, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 180, 102, 92, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(renderer_, 560.0F, 190.0F, "RAID DEPLOYMENT");

    const SDL_FRect regionalButton = raidRegionalOperationsButton();
    SDL_SetRenderDrawColor(renderer_, 48, 66, 58, 255);
    SDL_RenderFillRect(renderer_, &regionalButton);
    SDL_SetRenderDrawColor(renderer_, 126, 166, 142, 255);
    SDL_RenderRect(renderer_, &regionalButton);
    uiTextRenderer_.render(
        renderer_, regionalButton.x + 16.0F,
        regionalButton.y + 10.0F, "REGIONAL ROUTES");

    const MapDefinition &map = selectedRaidMap();
    const SDL_FRect previous = raidMapPreviousButton();
    const SDL_FRect next = raidMapNextButton();
    SDL_SetRenderDrawColor(renderer_, 68, 54, 52, 255);
    SDL_RenderFillRect(renderer_, &previous);
    SDL_RenderFillRect(renderer_, &next);
    SDL_SetRenderDrawColor(renderer_, 180, 102, 92, 255);
    SDL_RenderRect(renderer_, &previous);
    SDL_RenderRect(renderer_, &next);
    uiTextRenderer_.render(renderer_, previous.x + 22.0F, previous.y + 16.0F, "<");
    uiTextRenderer_.render(renderer_, next.x + 22.0F, next.y + 16.0F, ">");
    const std::string mapLabel = fmt::format(
        "MAP {}/{} | {}",
        selectedRaidMapIndex_ % publishedContentRegistry().maps().size() + 1U,
        publishedContentRegistry().maps().size(),
        map.displayName);
    uiTextRenderer_.render(renderer_, 492.0F, 232.0F, mapLabel.c_str());
    uiTextRenderer_.render(renderer_, 492.0F, 250.0F, map.routeProfile.c_str());
    const std::string danger = fmt::format(
        "DIFFICULTY {} | {}",
        map.operationBriefing.difficulty,
        map.operationBriefing.warning);
    uiTextRenderer_.render(renderer_, 410.0F, 270.0F, danger.c_str());

    const ProfileState &profile = gameSession_.profile();
    if (const auto travel = gameSession_.raidTravelPreview(map.id))
    {
        const std::string timing = fmt::format(
            "DEPART DAY {} {:02}:{:02} -> ARRIVE DAY {} {:02}:{:02} {} | {} MIN",
            travel->departure.day,
            travel->departure.hour,
            travel->departure.minute,
            travel->arrival.day,
            travel->arrival.hour,
            travel->arrival.minute,
            worldTimeOfDayName(travel->arrival.timeOfDay),
            travel->outboundMinutes);
        std::string routeLabel;
        for (const RegionRouteDefinitionId &routeId : travel->routeIds)
        {
            if (!routeLabel.empty())
            {
                routeLabel += " > ";
            }
            routeLabel += publishedContentRegistry()
                .regionRoute(routeId).displayName;
        }
        const std::string returnTiming = fmt::format(
            "ROUTE {}{} | RETURN {} MIN | FAILURE {} MIN",
            routeLabel,
            travel->usesOnlineOutpost ? " [OUTPOST]" : "",
            travel->returnMinutes,
            travel->failureRegroupMinutes);
        uiTextRenderer_.render(renderer_, 410.0F, 290.0F, timing.c_str());
        uiTextRenderer_.render(
            renderer_, 478.0F, 308.0F, returnTiming.c_str());
    }

    constexpr std::array<const char *, kRaidIntelligenceCategoryCount>
        intelligenceLabels{"TRANSPORT MAP", "RESOURCE MANIFEST", "ENEMY DOSSIER"};
    for (std::size_t index = 0;
         index < kRaidIntelligenceCategoryCount;
         ++index)
    {
        const auto category = static_cast<RaidIntelligenceCategory>(index);
        const std::uint32_t owned = profile.raidIntelligence.count(
            map.id, category);
        const bool selected = selectedRaidIntelligence_.has(category);
        const SDL_FRect button = raidIntelligenceButton(index);
        if (selected)
        {
            SDL_SetRenderDrawColor(renderer_, 48, 98, 126, 255);
        }
        else if (owned > 0U)
        {
            SDL_SetRenderDrawColor(renderer_, 48, 82, 62, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer_, 58, 50, 48, 255);
        }
        SDL_RenderFillRect(renderer_, &button);
        SDL_SetRenderDrawColor(
            renderer_, selected ? 96 : 180, selected ? 180 : 102,
            selected ? 220 : 92, 255);
        SDL_RenderRect(renderer_, &button);
        const std::string label = fmt::format(
            "{} | OWN {} | {} {}",
            intelligenceLabels[index],
            owned,
            selected ? "SELECTED" : owned > 0U ? "SELECT" : "BUY",
            selected || owned > 0U
                ? 0U
                : map.operationBriefing.price(category));
        uiTextRenderer_.render(
            renderer_, button.x + 10.0F, button.y + 11.0F, label.c_str());
    }
    for (std::size_t index{}; index < map.interiors.size(); ++index)
    {
        const RaidInteriorDefinition &interior = map.interiors[index];
        const bool known =
            profile.raidInteriorIntelligence.knows(interior.id);
        const SDL_FRect button = raidIntelligenceButton(
            kRaidIntelligenceCategoryCount + index);
        SDL_SetRenderDrawColor(
            renderer_, known ? 42 : 58, known ? 88 : 50,
            known ? 76 : 48, 255);
        SDL_RenderFillRect(renderer_, &button);
        SDL_SetRenderDrawColor(
            renderer_, known ? 116 : 180, known ? 210 : 102,
            known ? 174 : 92, 255);
        SDL_RenderRect(renderer_, &button);
        const std::string label = known
            ? fmt::format(
                  "INTERIOR PLAN | {} | PERMANENTLY KNOWN",
                  interior.displayName)
            : fmt::format(
                  "INTERIOR PLAN | {} | BUY {}",
                  interior.displayName,
                  interior.intelligencePrice);
        uiTextRenderer_.render(
            renderer_, button.x + 10.0F, button.y + 11.0F,
            label.c_str());
    }

    float y = 318.0F;
    for (EquipmentSlotKind slot : kProfileEquipmentSlots)
    {
        const auto id = equippedAsset(profile, slot);
        const std::string name = id.has_value()
            ? publishedContentRegistry().item(
                  profile.assets.find(*id)->definitionId).displayName
            : "EMPTY";
        const std::string row = fmt::format(
            "{}: {}",
            equipmentSlotLabel(slot),
            name);
        uiTextRenderer_.render(renderer_, 666.0F, y, row.c_str());
        y += 18.0F;
    }

    const WeaponReadiness readiness = weaponReadiness(profile);
    const bool capable = readiness.hasWeapon &&
        (readiness.hasChamberedRound ||
         readiness.compatibleMagazineRounds > 0);
    const std::string fireStatus = !readiness.hasWeapon
        ? "NO WEAPON EQUIPPED"
        : readiness.hasChamberedRound
            ? fmt::format(
                  "CAN FIRE NOW | {} COMPATIBLE MAGAZINE ROUNDS",
                  readiness.compatibleMagazineRounds)
            : readiness.compatibleMagazineRounds > 0
                ? fmt::format(
                      "NEEDS CHAMBER/RELOAD | {} COMPATIBLE ROUNDS",
                      readiness.compatibleMagazineRounds)
                : "NO USABLE AMMUNITION";
    uiTextRenderer_.render(
        renderer_,
        666.0F,
        458.0F,
        fireStatus.c_str());
    if (!capable)
    {
        uiTextRenderer_.render(
            renderer_, 666.0F, 478.0F,
            "WARNING: UNSAFE DEPLOY REQUIRES SECOND CONFIRMATION");
    }
    if (selectedRaidSelfRecoveryRecordId_.has_value())
    {
        const auto records = gameSession_.lostRaidRecordProjections();
        const auto target = std::find_if(
            records.begin(), records.end(),
            [&](const LostRaidRecordProjection &record)
            {
                return record.recordId ==
                    *selectedRaidSelfRecoveryRecordId_;
            });
        const std::string targetText = target == records.end()
            ? "RAID RECOVERY TARGET IS NO LONGER AVAILABLE"
            : fmt::format(
                  "RAID RECOVERY | {} | {} ASSETS | RESERVE SPACE",
                  target->mapDisplayName,
                  target->assets.size());
        uiTextRenderer_.render(
            renderer_, 666.0F, 500.0F, targetText.c_str());
    }
    const LostRaidAgingPreview aging = gameSession_.lostRaidAgingPreview();
    const SDL_FRect recordsButton = raidLostRecordsButton();
    SDL_SetRenderDrawColor(
        renderer_,
        aging.recordsExpiringOnNextSettlement > 0U ? 104 : 54,
        aging.recordsExpiringOnNextSettlement > 0U ? 62 : 70,
        58, 255);
    SDL_RenderFillRect(renderer_, &recordsButton);
    SDL_SetRenderDrawColor(renderer_, 190, 132, 102, 255);
    SDL_RenderRect(renderer_, &recordsButton);
    const std::string recordsLabel = fmt::format(
        "LOST RECORDS {} | OPEN",
        aging.activeRecordCount);
    uiTextRenderer_.render(
        renderer_, recordsButton.x + 18.0F, recordsButton.y + 13.0F,
        recordsLabel.c_str());
    if (aging.recordsExpiringOnNextSettlement > 0U)
    {
        const std::string expiry = fmt::format(
            "WARNING: {} RECORD(S) EXPIRE AFTER NEXT RAID SETTLEMENT",
            aging.recordsExpiringOnNextSettlement);
        uiTextRenderer_.render(renderer_, 700.0F, 560.0F, expiry.c_str());
    }
    renderScreenPrimaryButton(
        deploymentWarningArmed_ ? "CONFIRM UNSAFE DEPLOY" : "DEPLOY ALPHA RAID");
    uiTextRenderer_.render(renderer_, 420.0F, 590.0F, "CLICK INTELLIGENCE TO BUY/SELECT | ENTER DEPLOY | ESC CLOSE");
}

void App::renderRegionalOperations()
{
    const SDL_FRect panel{
        100.0F, 120.0F, 1080.0F, 580.0F};
    SDL_SetRenderDrawColor(renderer_, 18, 25, 22, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 110, 156, 128, 255);
    SDL_RenderRect(renderer_, &panel);

    uiTextRenderer_.render(
        renderer_, 430.0F, 150.0F,
        "REGIONAL BASE SITES, ROUTES & OUTPOSTS");
    const SDL_FRect back = raidRegionalOperationsButton();
    SDL_SetRenderDrawColor(renderer_, 48, 66, 58, 255);
    SDL_RenderFillRect(renderer_, &back);
    SDL_SetRenderDrawColor(renderer_, 126, 166, 142, 255);
    SDL_RenderRect(renderer_, &back);
    uiTextRenderer_.render(
        renderer_, back.x + 24.0F, back.y + 10.0F,
        "BACK TO BRIEFING");

    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState &profile = gameSession_.profile();
    const RegionNodeDefinition &base = content.regionNode(
        profile.regionalOperations.activeBaseNodeId);
    const std::string baseLabel = fmt::format(
        "ACTIVE MAIN BASE | {} | TECHNOLOGY CORE {}",
        base.displayName,
        profile.regionalOperations.technologyCore.instanceId);
    uiTextRenderer_.render(
        renderer_, 300.0F, 232.0F, baseLabel.c_str());
    uiTextRenderer_.render(
        renderer_, 300.0F, 258.0F,
        "CLEAR SITE > ESTABLISH STAGING OUTPOST > BUILD CORE FACILITIES > MIGRATE");

    const auto &baseSites = content.regionalOperations().baseSites;
    const auto activeSite = std::find_if(
        baseSites.begin(), baseSites.end(),
        [&](const RegionalBaseSiteDefinition &site)
        { return site.nodeId == profile.regionalOperations.activeBaseNodeId; });
    const std::optional<RegionalOutpostDefinitionId> activeBaseOutpost =
        activeSite != baseSites.end()
            ? activeSite->outpostDefinitionId
            : std::nullopt;
    const auto candidate = std::find_if(
        baseSites.begin(), baseSites.end(),
        [&](const RegionalBaseSiteDefinition &site)
        { return site.nodeId != profile.regionalOperations.activeBaseNodeId; });
    if (candidate != baseSites.end())
    {
        const RegionalBaseSiteState &siteState =
            profile.regionalOperations.baseSites.at(candidate->id);
        const char *tier = candidate->tier == RegionalBaseSiteTier::Basic
            ? "BASIC"
            : candidate->tier == RegionalBaseSiteTier::Mature
                ? "MATURE" : "STRATEGIC";
        const std::string siteStatus = fmt::format(
            "CANDIDATE SITE | {} | {} | {}",
            candidate->displayName, tier,
            siteState.unlocked ? "UNLOCKED" : "LOCKED");
        uiTextRenderer_.render(
            renderer_, 300.0F, 288.0F, siteStatus.c_str());
        const std::string advantage = fmt::format(
            "ADVANTAGE | {}", candidate->advantage);
        uiTextRenderer_.render(
            renderer_, 300.0F, 310.0F, advantage.c_str());
        const BaseFacilityDefinitionId warehouse{
            "base_facility.warehouse"};
        const BaseFacilityDefinitionId dormitory{
            "base_facility.dormitory"};
        const BaseFacilityDefinitionId kitchenWater{
            "base_facility.kitchen_water"};
        const BaseFacilityDefinitionId medical{
            "base_facility.medical"};
        const auto facilityStatus = [&](const BaseFacilityDefinitionId &id)
        {
            return baseFacilityOwned(profile, id) ? "READY" : "MISSING";
        };
        const std::string infrastructure = fmt::format(
            "MIGRATION CORE | WAREHOUSE {} | DORMITORY FACILITY {} | KITCHEN/WATER {} | MEDICAL FACILITY {}",
            facilityStatus(warehouse),
            facilityStatus(dormitory),
            facilityStatus(kitchenWater),
            facilityStatus(medical));
        uiTextRenderer_.render(
            renderer_, 300.0F, 334.0F, infrastructure.c_str());

        const RegionalBaseSiteClearancePlan clearance =
            queryRegionalBaseSiteClearance(profile, content, candidate->id);
        bool actionAvailable{};
        std::string actionLabel;
        if (!siteState.unlocked)
        {
            actionAvailable = clearance.canDeploy;
            actionLabel = deploymentWarningArmed_
                ? "CONFIRM BASE SITE CLEARING RAID"
                : "DEPLOY BASE SITE CLEARING RAID";
        }
        else if (!baseFacilityOwned(profile, kitchenWater))
        {
            const auto &projects = content.baseConstructionProjects();
            const auto project = std::find_if(
                projects.begin(), projects.end(),
                [](const BaseConstructionProjectDefinition &entry)
                {
                    return entry.target ==
                        BaseFacilityUpgradeTarget::KitchenWater;
                });
            if (project == projects.end())
            {
                actionLabel = "KITCHEN & WATER PROJECT NOT PUBLISHED";
            }
            else
            {
                const auto &active = profile.baseConstruction.activeProject;
                const bool cancelling = active.has_value() &&
                    active->definitionId == project->id;
                const BaseConstructionPlan plan = cancelling
                    ? queryCancelBaseConstruction(
                        profile, content,
                        CancelBaseConstructionCommand{project->id})
                    : queryStartBaseConstruction(
                        profile, content,
                        StartBaseConstructionCommand{project->id});
                actionAvailable = plan.canCommit;
                actionLabel = cancelling
                    ? fmt::format(
                        "CANCEL KITCHEN & WATER | {} MIN REMAINING",
                        profile.baseConstruction.activeProject
                            ->completionWorldMinute -
                            profile.worldClock.elapsedWorldMinutes)
                    : plan.canCommit
                        ? fmt::format(
                            "BUILD KITCHEN & WATER | MATERIAL {} | WORKERS {} | {}H",
                            plan.materialCost,
                            plan.workerCount,
                            plan.durationMinutes / kWorldMinutesPerHour)
                        : "KITCHEN & WATER REQUIREMENTS NOT MET";
            }
        }
        else
        {
            const BaseMigrationPlan migration = queryBaseMigration(
                profile, content, BaseMigrationCommand{candidate->id});
            actionAvailable = migration.canCommit;
            actionLabel = migration.canCommit
                ? fmt::format(
                    "MIGRATE MAIN BASE | {}H | OLD BASE BECOMES OUTPOST",
                    migration.migrationMinutes / kWorldMinutesPerHour)
                : "MIGRATION BLOCKED | ESTABLISH TARGET OUTPOST";
        }
        const SDL_FRect action = regionalBaseSiteClearanceButton();
        SDL_SetRenderDrawColor(
            renderer_, actionAvailable ? 56 : 42,
            actionAvailable ? 92 : 52,
            actionAvailable ? 70 : 52, 255);
        SDL_RenderFillRect(renderer_, &action);
        SDL_SetRenderDrawColor(renderer_, 164, 208, 184, 255);
        SDL_RenderRect(renderer_, &action);
        uiTextRenderer_.render(
            renderer_, action.x + 34.0F, action.y + 12.0F,
            actionLabel.c_str());
    }

    const BaseFacilityDefinitionId workshop{"base_facility.workshop"};
    const bool workshopInReserve = baseFacilityOwned(profile, workshop) &&
        !baseFacilityInstalled(profile, workshop);
    const SDL_FRect reserveButton = regionalFacilityReserveButton();
    SDL_SetRenderDrawColor(
        renderer_, workshopInReserve ? 54 : 38,
        workshopInReserve ? 86 : 48,
        workshopInReserve ? 72 : 50, 255);
    SDL_RenderFillRect(renderer_, &reserveButton);
    SDL_SetRenderDrawColor(renderer_, 142, 184, 158, 255);
    SDL_RenderRect(renderer_, &reserveButton);
    uiTextRenderer_.render(
        renderer_, reserveButton.x + 16.0F, reserveButton.y + 8.0F,
        workshopInReserve
            ? "FACILITY RESERVE | WORKSHOP | INSTALL FREE"
            : "FACILITY RESERVE | EMPTY");

    const RegionalBaseSiteDefinition *featureSite =
        activeSite != baseSites.end() ? &*activeSite : nullptr;
    if (featureSite != nullptr &&
        profile.regionalOperations.baseSites.at(featureSite->id)
            .uniqueFeatureRepaired &&
        candidate != baseSites.end())
    {
        const RegionalBaseSiteState &candidateState =
            profile.regionalOperations.baseSites.at(candidate->id);
        if (candidateState.unlocked &&
            !candidateState.uniqueFeatureRepaired)
        {
            featureSite = &*candidate;
        }
    }
    if (featureSite != nullptr)
    {
        const RegionalBaseSiteState &state =
            profile.regionalOperations.baseSites.at(featureSite->id);
        const BaseSiteFeatureRepairPlan repair =
            queryBaseSiteFeatureRepair(
                profile, content,
                BaseSiteFeatureRepairCommand{featureSite->id});
        const SDL_FRect featureButton = regionalBaseSiteFeatureButton();
        SDL_SetRenderDrawColor(
            renderer_,
            state.uniqueFeatureRepaired ? 54 : repair.canCommit ? 66 : 42,
            state.uniqueFeatureRepaired ? 90 : repair.canCommit ? 104 : 52,
            state.uniqueFeatureRepaired ? 72 : repair.canCommit ? 78 : 52,
            255);
        SDL_RenderFillRect(renderer_, &featureButton);
        SDL_SetRenderDrawColor(renderer_, 164, 208, 184, 255);
        SDL_RenderRect(renderer_, &featureButton);
        const std::string featureLabel = state.uniqueFeatureRepaired
            ? featureSite->uniqueFeatureManufacturingDurationPercent < 100U
                ? fmt::format(
                    "FEATURE ONLINE | TIME {}%",
                    featureSite->uniqueFeatureManufacturingDurationPercent)
                : "SITE FEATURE ONLINE | BASELINE EFFECT"
            : repair.canCommit
                ? fmt::format(
                    "REPAIR FEATURE | {} MAT | {}H | TIME {}%",
                    repair.materialUnits,
                    repair.durationMinutes / kWorldMinutesPerHour,
                    repair.manufacturingDurationPercent)
                : "SITE FEATURE REPAIR BLOCKED";
        uiTextRenderer_.render(
            renderer_, featureButton.x + 12.0F,
            featureButton.y + 8.0F, featureLabel.c_str());
    }

    const auto &definitions = content.regionalOperations().outposts;
    for (std::size_t index{}; index < definitions.size(); ++index)
    {
        const RegionalOutpostDefinition &definition = definitions[index];
        const RegionalOutpostState &state =
            profile.regionalOperations.outposts.at(definition.id);
        const float rowY = 444.0F + static_cast<float>(index) * 62.0F;
        const bool representsActiveBase =
            activeBaseOutpost.has_value() &&
            *activeBaseOutpost == definition.id;
        const std::string status = fmt::format(
            "{} | {} | STAFF {}/{} | {}",
            definition.displayName,
            representsActiveBase ? "MAIN BASE" :
                state.established ? "ESTABLISHED" :
                state.unlocked ? "UNLOCKED" : "LOCKED",
            assignedRegionalOutpostStaff(state),
            definition.requiredStaff,
            representsActiveBase
                ? "CORE ACTIVE"
                : state.disrupted
                ? "DISRUPTED"
                : regionalOutpostOnline(profile, content, definition.id)
                    ? "ONLINE" : "OFFLINE");
        uiTextRenderer_.render(renderer_, 300.0F, rowY, status.c_str());
        const std::string threat = representsActiveBase
            ? "ACTIVE MAIN BASE LOCATION | NOT A LIGHT OUTPOST"
            : fmt::format(
                "THREAT {}/{} | {} SAFE OPERATION(S) REMAIN",
                state.shortcutOperationsSinceRestoration,
                definition.safeShortcutOperations,
                definition.safeShortcutOperations >
                        state.shortcutOperationsSinceRestoration
                    ? definition.safeShortcutOperations -
                          state.shortcutOperationsSinceRestoration
                    : 0U);
        uiTextRenderer_.render(
            renderer_, 300.0F, rowY + 22.0F, threat.c_str());

        const bool hasGarrison =
            assignedRegionalOutpostStaff(state) != 0U;
        const RegionalOutpostPlan establishPlan =
            queryEstablishRegionalOutpost(
                profile, content,
                EstablishRegionalOutpostCommand{definition.id});
        const RegionalOutpostStaffingPlan staffingPlan =
            queryRegionalOutpostStaffing(
                profile, content,
                RegionalOutpostStaffingCommand{
                    definition.id, !hasGarrison});
        const bool canCommit = !representsActiveBase && (state.disrupted
            ? state.established &&
                assignedRegionalOutpostStaff(state) ==
                    definition.requiredStaff
            : state.established
                ? staffingPlan.canCommit
                : establishPlan.canCommit);
        const SDL_FRect action = regionalOutpostActionButton(index);
        SDL_SetRenderDrawColor(
            renderer_, canCommit ? 48 : 42,
            canCommit ? 98 : 52,
            canCommit ? 74 : 52, 255);
        SDL_RenderFillRect(renderer_, &action);
        SDL_SetRenderDrawColor(renderer_, 164, 208, 184, 255);
        SDL_RenderRect(renderer_, &action);
        uiTextRenderer_.render(
            renderer_, action.x + 18.0F, action.y + 13.0F,
            representsActiveBase
                ? "MAIN BASE | TECHNOLOGY CORE ACTIVE"
                : state.disrupted
                ? deploymentWarningArmed_
                    ? "CONFIRM OUTPOST CLEARING RAID"
                    : "DEPLOY OUTPOST CLEARING RAID"
                : state.established
                    ? hasGarrison
                        ? "RETURN OUTPOST GARRISON"
                        : staffingPlan.canCommit
                            ? "ASSIGN FULL GARRISON | ACTIVATE SHORTCUTS"
                            : "GARRISON ASSIGNMENT BLOCKED"
                    : establishPlan.canCommit
                        ? "ESTABLISH LIGHT OUTPOST"
                        : state.unlocked
                            ? "OUTPOST ESTABLISHMENT BLOCKED"
                            : "CLEAR BASE SITE TO UNLOCK");
    }

    const BasePerimeterSweepPlan sweep = queryBasePerimeterSweep(
        profile, content);
    const SDL_FRect sweepButton = regionalBasePerimeterSweepButton();
    SDL_SetRenderDrawColor(
        renderer_,
        sweep.canDeploy ? 64 : 42,
        sweep.canDeploy ? 96 : 52,
        sweep.canDeploy ? 62 : 52,
        255);
    SDL_RenderFillRect(renderer_, &sweepButton);
    SDL_SetRenderDrawColor(renderer_, 204, 184, 104, 255);
    SDL_RenderRect(renderer_, &sweepButton);
    const std::string sweepLabel = sweep.canDeploy
        ? fmt::format(
              "{}PERIMETER SWEEP | THREAT {} | SUCCESS -{} | RAID +{} | NORMAL EXITS OPEN",
              deploymentWarningArmed_ ? "CONFIRM " : "",
              sweep.currentThreatUnits,
              sweep.threatReductionUnits,
              kBaseSiegeRaidThreatUnits)
        : fmt::format(
              "PERIMETER SWEEP LOCKED | THREAT {}/{}",
              sweep.currentThreatUnits,
              kBasePerimeterSweepMinimumThreat);
    uiTextRenderer_.render(
        renderer_, sweepButton.x + 14.0F, sweepButton.y + 10.0F,
        sweepLabel.c_str());

    const RegionalRoutePlan riverside = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.riverside"});
    const RegionalRoutePlan industrial = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.industrial"});
    const RegionalRoutePlan frontier = queryRegionalRoute(
        profile, content, MapDefinitionId{"map.raid.frontier_exchange"});
    const std::string routeSummary = fmt::format(
        "CURRENT ONE-WAY ROUTES | RIVERSIDE {} | ASHWORKS {} | FRONTIER {} MIN",
        riverside.travelMinutes,
        industrial.travelMinutes,
        frontier.travelMinutes);
    uiTextRenderer_.render(
        renderer_, 300.0F, 650.0F, routeSummary.c_str());
    uiTextRenderer_.render(
        renderer_, 300.0F, 672.0F,
        "OUTPOSTS HAVE NO STORAGE OR SERVICES | ESC BACK");
    if (!uiMessage_.empty())
    {
        SDL_SetRenderDrawColor(renderer_, 224, 218, 152, 255);
        uiTextRenderer_.render(
            renderer_, 300.0F, 694.0F, uiMessage_.c_str());
    }
}

void App::renderLostRaidRecords()
{
    const SDL_FRect panel{kFlowPanelX, kFlowPanelY,
                          kFlowPanelWidth, kFlowPanelHeight};
    SDL_SetRenderDrawColor(renderer_, 24, 20, 18, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 182, 132, 92, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(renderer_, 500.0F, 166.0F,
                           "LOST RAID RECORDS & RECOVERY");

    const std::optional<RecoveryTaskProjection> task =
        gameSession_.recoveryTaskProjection();
    if (task.has_value())
    {
        const std::string taskStatus = task->readyForCollection
            ? fmt::format(
                  "NPC RECOVERY READY | {} | RESULT {}/{}",
                  task->mapDisplayName,
                  task->recoveredAssetCount,
                  task->totalAssetCount)
            : fmt::format(
                  "NPC RECOVERY ACTIVE | {} | REMAINING {}H {:02}M",
                  task->mapDisplayName,
                  task->remainingWorldMinutes / kWorldMinutesPerHour,
                  task->remainingWorldMinutes % kWorldMinutesPerHour);
        uiTextRenderer_.render(
            renderer_, 360.0F, 210.0F, taskStatus.c_str());
    }
    else
    {
        uiTextRenderer_.render(
            renderer_, 360.0F, 210.0F,
            "SELECT ONE COMPLETE RECORD TO COMMISSION NPC RECOVERY");
    }

    const std::vector<LostRaidRecordProjection> records =
        gameSession_.lostRaidRecordProjections();
    if (records.empty())
    {
        uiTextRenderer_.render(
            renderer_, 454.0F, 330.0F,
            "NO RECOVERABLE LOST LOADOUTS");
        uiTextRenderer_.render(
            renderer_, 414.0F, 352.0F,
            "DEATH OR ACTIVE QUIT CREATES A RECORD WHEN GEAR IS CARRIED");
    }
    else
    {
        for (std::size_t index{};
             index < records.size() && index < 4U;
             ++index)
        {
            const LostRaidRecordProjection &record = records[index];
            const SDL_FRect row = lostRaidRecordRow(index);
            const bool finalOpportunity =
                record.retainedSettlementsRemaining == 0U;
            const bool selected = selectedLostRaidRecordId_.has_value() &&
                *selectedLostRaidRecordId_ == record.recordId;
            SDL_SetRenderDrawColor(
                renderer_, selected ? 54 : (finalOpportunity ? 78 : 42),
                selected ? 78 : (finalOpportunity ? 42 : 50),
                selected ? 66 : (finalOpportunity ? 36 : 44), 255);
            SDL_RenderFillRect(renderer_, &row);
            SDL_SetRenderDrawColor(
                renderer_, selected ? 116 : (finalOpportunity ? 218 : 142),
                selected ? 218 : (finalOpportunity ? 104 : 152),
                selected ? 166 : (finalOpportunity ? 82 : 112), 255);
            SDL_RenderRect(renderer_, &row);

            const std::string heading = fmt::format(
                "{} | DIFFICULTY {} | {}{}",
                record.mapDisplayName,
                record.difficulty,
                record.outcome == RaidResultOutcome::PlayerDead
                    ? "PLAYER DEAD"
                    : "RAID ABANDONED",
                selectedRaidSelfRecoveryRecordId_ ==
                        std::optional<std::string>{record.recordId}
                    ? " | RAID TARGET"
                    : "");
            uiTextRenderer_.render(
                renderer_, row.x + 12.0F, row.y + 8.0F,
                heading.c_str());
            const std::string retention = finalOpportunity
                ? "FINAL OPPORTUNITY | NEXT RAID SETTLEMENT DESTROYS RECORD"
                : fmt::format(
                      "FOLLOW-UP RAIDS {}/{} | {} REMAIN BEFORE FINAL OPPORTUNITY",
                      record.subsequentRaidSettlementCount,
                      kLostRaidRecordRetainedSettlementCount,
                      record.retainedSettlementsRemaining);
            uiTextRenderer_.render(
                renderer_, row.x + 12.0F, row.y + 27.0F,
                retention.c_str());
            std::string assetSummary = fmt::format(
                "ASSETS {} | ", record.assets.size());
            std::size_t listed{};
            for (const LostRaidAssetProjection &asset : record.assets)
            {
                if (listed >= 4U)
                {
                    assetSummary += "...";
                    break;
                }
                if (listed > 0U)
                {
                    assetSummary += ", ";
                }
                assetSummary += publishedContentRegistry().item(
                    asset.definitionId).displayName;
                if (asset.quantity > 1U)
                {
                    assetSummary += fmt::format(" x{}", asset.quantity);
                }
                ++listed;
            }
            uiTextRenderer_.render(
                renderer_, row.x + 12.0F, row.y + 44.0F,
                assetSummary.c_str());
        }
        if (records.size() > 4U)
        {
            const std::string more = fmt::format(
                "+{} OLDER RECORD(S) | NEWEST FOUR SHOWN",
                records.size() - 4U);
            uiTextRenderer_.render(renderer_, 650.0F, 458.0F, more.c_str());
        }
    }

    const SDL_FRect primary = recoveryTaskPrimaryButton();
    const bool primaryAvailable =
        (task.has_value() && task->readyForCollection) ||
        (!task.has_value() && selectedLostRaidRecordId_.has_value());
    SDL_SetRenderDrawColor(
        renderer_, primaryAvailable ? 72 : 50,
        primaryAvailable ? 104 : 54,
        primaryAvailable ? 82 : 52, 255);
    SDL_RenderFillRect(renderer_, &primary);
    SDL_SetRenderDrawColor(renderer_, 150, 188, 158, 255);
    SDL_RenderRect(renderer_, &primary);
    std::string primaryLabel = "SELECT A RECORD";
    if (task.has_value())
    {
        primaryLabel = task->readyForCollection
            ? "COLLECT RECOVERED ASSETS"
            : "RECOVERY IN PROGRESS";
    }
    else if (selectedLostRaidRecordId_.has_value())
    {
        const RecoveryTaskQuote quote = gameSession_.recoveryTaskQuote(
            *selectedLostRaidRecordId_);
        primaryLabel = quote.canCommit
            ? fmt::format(
                  "START | COST {} | {}H {:02}M | HIGH-TENDENCY {} LOW-TENDENCY {}",
                  quote.serviceFee,
                  quote.durationMinutes / kWorldMinutesPerHour,
                  quote.durationMinutes % kWorldMinutesPerHour,
                  quote.highTendencyAssets,
                  quote.lowTendencyAssets)
            : quote.message;
    }
    uiTextRenderer_.render(
        renderer_, primary.x + 8.0F, primary.y + 13.0F,
        primaryLabel.c_str());

    if (selectedLostRaidRecordId_.has_value())
    {
        const SDL_FRect secondary = recoveryTaskSecondaryButton();
        const bool alreadyTarget = selectedRaidSelfRecoveryRecordId_ ==
            selectedLostRaidRecordId_;
        SDL_SetRenderDrawColor(
            renderer_, alreadyTarget ? 88 : 52,
            alreadyTarget ? 54 : 82,
            alreadyTarget ? 48 : 98, 255);
        SDL_RenderFillRect(renderer_, &secondary);
        SDL_SetRenderDrawColor(
            renderer_, alreadyTarget ? 190 : 112,
            alreadyTarget ? 112 : 174,
            alreadyTarget ? 92 : 204, 255);
        SDL_RenderRect(renderer_, &secondary);
        uiTextRenderer_.render(
            renderer_, secondary.x + 22.0F, secondary.y + 13.0F,
            alreadyTarget
                ? "CLEAR RAID RECOVERY TARGET"
                : "RECOVER IN NEXT RAID");
    }

    if (task.has_value())
    {
        const SDL_FRect cancel = recoveryTaskCancelButton();
        SDL_SetRenderDrawColor(renderer_, 88, 54, 48, 255);
        SDL_RenderFillRect(renderer_, &cancel);
        SDL_SetRenderDrawColor(renderer_, 190, 112, 92, 255);
        SDL_RenderRect(renderer_, &cancel);
        uiTextRenderer_.render(
            renderer_, cancel.x + 12.0F, cancel.y + 13.0F,
            "CANCEL NPC | NO REFUND");
    }

    const SDL_FRect back = raidLostRecordsButton();
    SDL_SetRenderDrawColor(renderer_, 64, 58, 48, 255);
    SDL_RenderFillRect(renderer_, &back);
    SDL_SetRenderDrawColor(renderer_, 190, 132, 102, 255);
    SDL_RenderRect(renderer_, &back);
    uiTextRenderer_.render(
        renderer_, back.x + 36.0F, back.y + 13.0F,
        "BACK TO RAID BRIEFING");
    uiTextRenderer_.render(renderer_, 390.0F, 532.0F,
                           "NPC TASK OR RAID RECOVERY | ONE OWNER PER RECORD");
    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(
            renderer_, 390.0F, 554.0F, uiMessage_.c_str());
    }
}

void App::renderBase()
{
    renderBaseWorld();

    SDL_SetRenderDrawColor(
        renderer_,
        220,
        232,
        228,
        255);
    uiTextRenderer_.render(
        renderer_,
        574.0F,
        54.0F,
        "RAIDLINE BASE");

    const std::string currency = fmt::format(
        "CURRENCY {} | REV {}",
        gameSession_.profile().currency,
        gameSession_.profile().revision);
    uiTextRenderer_.render(renderer_, 1010.0F, 54.0F, currency.c_str());
    const BaseResourceBundle &base =
        gameSession_.profile().baseResources.pool;
    const std::string resources = fmt::format(
        "BASE R{} H{} O{} S{}",
        base.food,
        base.hygiene,
        base.morale,
        base.security);
    uiTextRenderer_.render(renderer_, 1010.0F, 72.0F, resources.c_str());
    const WorldClockProjection clock = gameSession_.worldClockProjection();
    const std::string clockText = fmt::format(
        "DAY {} {:02}:{:02} {}",
        clock.day,
        clock.hour,
        clock.minute,
        worldTimeOfDayName(clock.timeOfDay));
    uiTextRenderer_.render(renderer_, 1010.0F, 90.0F, clockText.c_str());
    const BaseThreatProjection threat = gameSession_.baseThreatProjection();
    const std::string threatText = fmt::format(
        "BASE THREAT {} {}/{}",
        baseThreatTierName(threat.tier),
        threat.totalThreatUnits,
        kBaseSiegeThreatThreshold);
    uiTextRenderer_.render(renderer_, 1010.0F, 108.0F, threatText.c_str());
    renderBaseSiegeQueuedNotice();

    const char *goal = "OBJECTIVE COMPLETE";
    switch (gameSession_.profile().tutorial)
    {
    case TutorialProgress::FindStorage:
        goal = "OBJECTIVE: FIND STORAGE";
        break;
    case TutorialProgress::PrepareLoadout:
        goal = "OBJECTIVE: EQUIP PRIMARY WEAPON";
        break;
    case TutorialProgress::FindRaidGate:
        goal = "OBJECTIVE: FIND RAID GATE";
        break;
    case TutorialProgress::Complete:
        break;
    }
    uiTextRenderer_.render(renderer_, 48.0F, 54.0F, goal);

    if (const auto facility = gameFlow_.activeBaseFacility())
    {
        switch (*facility)
        {
        case BaseFacilityKind::Storage:
            renderBaseStorage();
            break;
        case BaseFacilityKind::Supply:
            renderBaseSupply();
            break;
        case BaseFacilityKind::Allocation:
            renderBaseAllocation();
            break;
        case BaseFacilityKind::Medical:
            renderBaseMedicalService();
            break;
        case BaseFacilityKind::Dormitory:
            renderBaseDormitory();
            break;
        case BaseFacilityKind::Workshop:
            renderBaseWorkshop();
            break;
        case BaseFacilityKind::RaidGate:
            renderBaseDeployment();
            break;
        }
    }
    if (inventoryOverlayState_.isOpen())
    {
        renderProfileInventory(true, false);
    }
    renderBaseSiegeWarning();
}

void App::renderBaseSiegeQueuedNotice()
{
    const BaseThreatProjection threat = gameSession_.baseThreatProjection();
    if (!threat.siegeQueued)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{330.0F, 82.0F, 620.0F, 46.0F};
    SDL_SetRenderDrawColor(renderer_, 46, 35, 20, 238);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 224, 168, 76, 255);
    SDL_RenderRect(renderer_, &panel);
    const std::string queued = fmt::format(
        "BASE SIEGE QUEUED | SAFETY {}",
        formatWorldMinutesRemaining(threat.safeMinutesRemaining));
    uiTextRenderer_.render(
        renderer_, 350.0F, 90.0F, queued.c_str());
    uiTextRenderer_.render(
        renderer_, 350.0F, 108.0F,
        "RAIDS AVAILABLE | 3-MIN WARNING AFTER SAFETY");
}

void App::renderBaseSiegeWarning()
{
    const BaseThreatProjection threat = gameSession_.baseThreatProjection();
    if (!threat.warningActive)
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 12, 10, 8, 214);
    const SDL_FRect shade{
        0.0F, 0.0F,
        static_cast<float>(kWindowWidth),
        static_cast<float>(kWindowHeight)};
    SDL_RenderFillRect(renderer_, &shade);
    const SDL_FRect panel{330.0F, 190.0F, 620.0F, 390.0F};
    SDL_SetRenderDrawColor(renderer_, 38, 32, 28, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 205, 92, 62, 255);
    SDL_RenderRect(renderer_, &panel);

    uiTextRenderer_.render(
        renderer_, 500.0F, 220.0F, "BASE SIEGE WARNING");
    const std::string timer = threat.warningRemainingSeconds > 0U
        ? fmt::format(
              "PREPARATION {:02}:{:02}",
              threat.warningRemainingSeconds / 60U,
              threat.warningRemainingSeconds % 60U)
        : "PREPARATION COMPLETE | MANUAL CONFIRMATION REQUIRED";
    uiTextRenderer_.render(renderer_, 430.0F, 254.0F, timer.c_str());
    const std::string sources = fmt::format(
        "THREAT SOURCES | RAID {} | POPULATION {} | SITE {}",
        threat.raidThreatUnits,
        threat.populationThreatUnits,
        threat.siteThreatUnits);
    uiTextRenderer_.render(renderer_, 390.0F, 294.0F, sources.c_str());

    const BaseAutoDefensePlan defense = gameSession_.baseAutoDefensePlan();
    const std::string requirement = fmt::format(
        "AUTO DEFENSE | SECURITY {}/{} | {}",
        defense.availableSecurity,
        defense.requiredSecurity,
        defense.projectedSuccess ? "PROJECTED SUCCESS" : "SOFT FAILURE RISK");
    uiTextRenderer_.render(renderer_, 390.0F, 332.0F, requirement.c_str());
    uiTextRenderer_.render(
        renderer_, 390.0F, 362.0F,
        "SUCCESS: MATERIAL +8, MORALE SUPPORT, 7 SAFE DAYS");
    uiTextRenderer_.render(
        renderer_, 390.0F, 390.0F,
        "FAILURE: LIMITED PUBLIC LOSS, 12 SAFE DAYS, NO PERSONAL GEAR LOSS");
    uiTextRenderer_.render(
        renderer_, 390.0F, 420.0F,
        !threat.autoDefensePresetSaved
            ? "FIRST SIEGE WILL NOT RESOLVE UNTIL YOU CONFIRM"
            : "SAVED PRESET WILL RESOLVE WHEN THE TIMER ENDS");

    const SDL_FRect button = baseAutoDefenseButton();
    SDL_SetRenderDrawColor(
        renderer_, defense.projectedSuccess ? 48 : 92,
        defense.projectedSuccess ? 88 : 52,
        defense.projectedSuccess ? 58 : 44, 255);
    SDL_RenderFillRect(renderer_, &button);
    SDL_SetRenderDrawColor(renderer_, 220, 190, 120, 255);
    SDL_RenderRect(renderer_, &button);
    uiTextRenderer_.render(
        renderer_, button.x + 58.0F, button.y + 18.0F,
        "START AUTO DEFENSE NOW");
    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(
            renderer_, 390.0F, 546.0F, uiMessage_.c_str());
    }
}

void App::renderRaidTacticalMap()
{
    if (!tacticalMapOpen_ ||
        !gameSession_.world().raidSession().isActive())
    {
        return;
    }

    if (!gameSession_.world().inOutdoorRaidSpace())
    {
        const std::optional<RaidInteriorMapProjection> interior =
            gameSession_.world().activeInteriorMapProjection();
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const SDL_FRect shade{0.0F, 0.0F,
                              static_cast<float>(kWindowWidth),
                              static_cast<float>(kWindowHeight)};
        SDL_SetRenderDrawColor(renderer_, 3, 7, 9, 210);
        SDL_RenderFillRect(renderer_, &shade);
        if (interior.has_value())
        {
            const SDL_FRect panel{104.0F, 54.0F, 1072.0F, 612.0F};
            const SDL_FRect chart{144.0F, 114.0F, 992.0F, 504.0F};
            SDL_SetRenderDrawColor(renderer_, 12, 22, 25, 248);
            SDL_RenderFillRect(renderer_, &panel);
            SDL_SetRenderDrawColor(renderer_, 86, 142, 126, 255);
            SDL_RenderRect(renderer_, &panel);
            SDL_SetRenderDrawColor(renderer_, 31, 52, 52, 245);
            SDL_RenderFillRect(renderer_, &chart);
            const auto screenRect = [chart, &interior](ContentRect rect)
            {
                return SDL_FRect{
                    chart.x + rect.position.x / interior->worldSize.x * chart.w,
                    chart.y + rect.position.y / interior->worldSize.y * chart.h,
                    rect.size.x / interior->worldSize.x * chart.w,
                    rect.size.y / interior->worldSize.y * chart.h};
            };
            for (const BallisticBlocker &blocker : interior->blockers)
            {
                const SDL_FRect bounds = screenRect(ContentRect{
                    blocker.bounds.position, blocker.bounds.size});
                SDL_SetRenderDrawColor(renderer_, 76, 88, 88, 235);
                SDL_RenderFillRect(renderer_, &bounds);
                SDL_SetRenderDrawColor(renderer_, 124, 146, 140, 245);
                SDL_RenderRect(renderer_, &bounds);
            }
            const SDL_FRect exit = screenRect(interior->exit);
            SDL_SetRenderDrawColor(renderer_, 82, 216, 132, 245);
            SDL_RenderRect(renderer_, &exit);
            uiTextRenderer_.render(
                renderer_, exit.x + 5.0F, exit.y + 4.0F, "EXIT");

            const Player &player = gameSession_.world().player();
            const Vec2 center{
                player.position().x + player.size() * 0.5F,
                player.position().y + player.size() * 0.5F};
            const Vec2 marker{
                chart.x + center.x / interior->worldSize.x * chart.w,
                chart.y + center.y / interior->worldSize.y * chart.h};
            SDL_SetRenderDrawColor(renderer_, 226, 238, 224, 255);
            SDL_RenderLine(
                renderer_, marker.x - 7.0F, marker.y,
                marker.x + 7.0F, marker.y);
            SDL_RenderLine(
                renderer_, marker.x, marker.y - 7.0F,
                marker.x, marker.y + 7.0F);
            const std::string title = fmt::format(
                "INTERIOR MAP | {} | PERMANENT INTELLIGENCE",
                interior->displayName);
            uiTextRenderer_.render(
                renderer_, 144.0F, 76.0F, title.c_str());
            uiTextRenderer_.render(
                renderer_, 740.0F, 76.0F,
                "M/ESC CLOSE | WORLD CONTINUES | MOVE 45%");
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            return;
        }
        const SDL_FRect panel{252.0F, 188.0F, 776.0F, 344.0F};
        SDL_SetRenderDrawColor(renderer_, 12, 22, 25, 248);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 86, 142, 126, 255);
        SDL_RenderRect(renderer_, &panel);
        uiTextRenderer_.render(
            renderer_, 322.0F, 246.0F,
            "INTERIOR LAYOUT INTELLIGENCE REQUIRED");
        uiTextRenderer_.render(
            renderer_, 322.0F, 298.0F,
            "BUY THE BUILDING PLAN AT THE RAID GATE");
        uiTextRenderer_.render(
            renderer_, 322.0F, 350.0F,
            "M/ESC CLOSE | WORLD CONTINUES | MOVE 45%");
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }

    const RaidTacticalMapState &map = gameSession_.world().tacticalMap();
    if (!map.configured())
    {
        return;
    }
    const RaidTacticalMapPresentationMode presentationMode =
        developerMapFogEnabled_
            ? RaidTacticalMapPresentationMode::FogOfWar
            : RaidTacticalMapPresentationMode::FullStaticMap;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect shade{0.0F, 0.0F,
                          static_cast<float>(kWindowWidth),
                          static_cast<float>(kWindowHeight)};
    SDL_SetRenderDrawColor(renderer_, 3, 7, 9, 210);
    SDL_RenderFillRect(renderer_, &shade);

    const SDL_FRect panel{104.0F, 54.0F, 1072.0F, 612.0F};
    const SDL_FRect chart{144.0F, 114.0F, 992.0F, 504.0F};
    SDL_SetRenderDrawColor(renderer_, 12, 22, 25, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 86, 142, 126, 255);
    SDL_RenderRect(renderer_, &panel);

    const float cellWidth = chart.w / static_cast<float>(map.columns());
    const float cellHeight = chart.h / static_cast<float>(map.rows());
    const auto setDistrictColor = [&](RaidDistrictKind district)
    {
        switch (district)
        {
        case RaidDistrictKind::Industrial:
            SDL_SetRenderDrawColor(renderer_, 58, 64, 61, 245);
            break;
        case RaidDistrictKind::Logistics:
            SDL_SetRenderDrawColor(renderer_, 67, 62, 50, 245);
            break;
        case RaidDistrictKind::Highway:
            SDL_SetRenderDrawColor(renderer_, 48, 51, 50, 245);
            break;
        case RaidDistrictKind::OpenGround:
            SDL_SetRenderDrawColor(renderer_, 72, 57, 39, 245);
            break;
        case RaidDistrictKind::Greenbelt:
            SDL_SetRenderDrawColor(renderer_, 43, 61, 42, 245);
            break;
        case RaidDistrictKind::RoadsideService:
            SDL_SetRenderDrawColor(renderer_, 69, 66, 47, 245);
            break;
        }
    };
    const auto setTerrainColor = [&](RaidTerrainKind terrain)
    {
        switch (terrain)
        {
        case RaidTerrainKind::Grass:
            SDL_SetRenderDrawColor(renderer_, 78, 112, 70, 220);
            break;
        case RaidTerrainKind::Concrete:
            SDL_SetRenderDrawColor(renderer_, 132, 138, 128, 220);
            break;
        case RaidTerrainKind::Dirt:
            SDL_SetRenderDrawColor(renderer_, 126, 91, 55, 220);
            break;
        case RaidTerrainKind::Asphalt:
            SDL_SetRenderDrawColor(renderer_, 86, 92, 91, 220);
            break;
        case RaidTerrainKind::Puddle:
            SDL_SetRenderDrawColor(renderer_, 58, 112, 132, 230);
            break;
        }
    };
    for (int row = 0; row < map.rows(); ++row)
    {
        for (int column = 0; column < map.columns(); ++column)
        {
            const SDL_FRect cell{
                chart.x + static_cast<float>(column) * cellWidth,
                chart.y + static_cast<float>(row) * cellHeight,
                cellWidth + 0.5F,
                cellHeight + 0.5F};
            const bool visible = tacticalMapCellVisible(
                map, column, row, presentationMode);
            if (visible)
            {
                setDistrictColor(map.outdoorDistrictKind(
                    column, row).value_or(RaidDistrictKind::OpenGround));
            }
            else
            {
                SDL_SetRenderDrawColor(renderer_, 4, 8, 10, 252);
            }
            SDL_RenderFillRect(renderer_, &cell);
            const std::optional<RaidTerrainKind> terrain =
                visible ? map.outdoorTerrainKind(column, row) : std::nullopt;
            if (terrain.has_value())
            {
                const SDL_FRect terrainMark{
                    cell.x + cell.w * 0.32F,
                    cell.y + cell.h * 0.32F,
                    std::max(2.0F, cell.w * 0.36F),
                    std::max(2.0F, cell.h * 0.36F)};
                setTerrainColor(*terrain);
                SDL_RenderFillRect(renderer_, &terrainMark);
            }
        }
    }

    if (!map.outdoorRoadCells().empty())
    {
        for (const RaidTacticalRoadCell &road : map.outdoorRoadCells())
        {
            const int column = road.column;
            const int row = road.row;
            if (column < 0 || row < 0 || column >= map.columns() ||
                row >= map.rows() || !tacticalMapCellVisible(
                    map, column, row, presentationMode))
            {
                continue;
            }
            const SDL_FRect roadCell{
                chart.x + static_cast<float>(column) * cellWidth + 1.0F,
                chart.y + static_cast<float>(row) * cellHeight + 1.0F,
                std::max(1.0F, cellWidth - 2.0F),
                std::max(1.0F, cellHeight - 2.0F)};
            if (road.kind == RaidOutdoorRoadKind::Primary)
            {
                SDL_SetRenderDrawColor(renderer_, 146, 142, 116, 235);
            }
            else if (road.kind == RaidOutdoorRoadKind::Secondary)
            {
                SDL_SetRenderDrawColor(renderer_, 112, 116, 98, 225);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer_, 82, 98, 86, 215);
            }
            SDL_RenderFillRect(renderer_, &roadCell);
        }
    }

    const Vec2 worldSize = map.worldSize();
    const auto screenPoint = [chart, worldSize](Vec2 point)
    {
        return Vec2{
            chart.x + point.x / worldSize.x * chart.w,
            chart.y + point.y / worldSize.y * chart.h};
    };
    const auto screenRect = [chart, worldSize](ContentRect rect)
    {
        return SDL_FRect{
            chart.x + rect.position.x / worldSize.x * chart.w,
            chart.y + rect.position.y / worldSize.y * chart.h,
            rect.size.x / worldSize.x * chart.w,
            rect.size.y / worldSize.y * chart.h};
    };
    for (const RaidTacticalWorldLabel &label : map.outdoorLabels())
    {
        if (!tacticalMapPointVisible(
                map, label.position, presentationMode))
            continue;
        const Vec2 point = screenPoint(label.position);
        uiTextRenderer_.render(
            renderer_, point.x, point.y, label.text.c_str());
    }

    SDL_SetRenderDrawColor(renderer_, 76, 88, 88, 220);
    for (const BallisticBlocker &blocker :
         gameSession_.world().ballisticBlockers())
    {
        const Vec2 center{
            blocker.bounds.position.x + blocker.bounds.size.x * 0.5F,
            blocker.bounds.position.y + blocker.bounds.size.y * 0.5F};
        if (tacticalMapPointVisible(map, center, presentationMode))
        {
            const SDL_FRect rect = screenRect(ContentRect{
                blocker.bounds.position, blocker.bounds.size});
            SDL_RenderFillRect(renderer_, &rect);
        }
    }

    const auto renderExtraction = [&](
        RaidMapExtractionKind kind,
        const std::optional<ContentRect> &rect,
        const char *label,
        Uint8 red,
        Uint8 green,
        Uint8 blue)
    {
        if (!rect.has_value() || !tacticalMapExtractionVisible(
                map, kind, presentationMode))
        {
            return;
        }
        const SDL_FRect marker = screenRect(*rect);
        SDL_SetRenderDrawColor(renderer_, red, green, blue, 245);
        SDL_RenderRect(renderer_, &marker);
        const SDL_FRect inset{
            marker.x + 3.0F, marker.y + 3.0F,
            std::max(0.0F, marker.w - 6.0F),
            std::max(0.0F, marker.h - 6.0F)};
        SDL_RenderRect(renderer_, &inset);
        uiTextRenderer_.render(
            renderer_, marker.x + 5.0F, marker.y + 4.0F, label);
    };
    renderExtraction(
        RaidMapExtractionKind::Normal,
        std::optional<ContentRect>{map.normalExtraction()},
        "N",
        82, 216, 132);
    renderExtraction(
        RaidMapExtractionKind::EmergencySignal,
        map.emergencyExtraction(),
        "S",
        230, 168, 76);
    renderExtraction(
        RaidMapExtractionKind::EmergencyConditional,
        map.conditionalExtraction(),
        "L",
        104, 180, 230);

    if (tacticalMapAdvancedResourceVisible(map, presentationMode))
    {
        ContentRect coarse = *map.advancedResourceArea();
        coarse.position.x = std::max(0.0F, coarse.position.x - 90.0F);
        coarse.position.y = std::max(0.0F, coarse.position.y - 70.0F);
        coarse.size.x = std::min(
            worldSize.x - coarse.position.x, coarse.size.x + 180.0F);
        coarse.size.y = std::min(
            worldSize.y - coarse.position.y, coarse.size.y + 140.0F);
        const SDL_FRect resource = screenRect(coarse);
        SDL_SetRenderDrawColor(renderer_, 218, 188, 82, 110);
        SDL_RenderFillRect(renderer_, &resource);
        SDL_SetRenderDrawColor(renderer_, 232, 206, 106, 235);
        SDL_RenderRect(renderer_, &resource);
    }

    for (const RaidTacticalResourcePoint &resourcePoint :
         map.outdoorResourcePoints())
    {
        if (!tacticalMapResourcePointVisible(
                map, resourcePoint, presentationMode))
            continue;
        SDL_FRect marker = screenRect(resourcePoint.bounds);
        marker.w = std::max(marker.w, 5.0F);
        marker.h = std::max(marker.h, 5.0F);
        if (resourcePoint.kind == RaidResourcePointKind::HighValue)
            SDL_SetRenderDrawColor(renderer_, 236, 198, 80, 210);
        else if (resourcePoint.kind ==
                 RaidResourcePointKind::LandmarkSpecific)
            SDL_SetRenderDrawColor(renderer_, 118, 214, 224, 205);
        else
            SDL_SetRenderDrawColor(renderer_, 126, 196, 132, 185);
        SDL_RenderFillRect(renderer_, &marker);
        SDL_SetRenderDrawColor(renderer_, 234, 228, 188, 235);
        SDL_RenderRect(renderer_, &marker);
    }

    if (tacticalMapEnemyDeploymentVisible(map))
    {
        for (Vec2 enemy : map.initialEnemyCenters())
        {
            const Vec2 center = screenPoint(enemy);
            const SDL_FRect coarse{
                center.x - 26.0F, center.y - 20.0F, 52.0F, 40.0F};
            SDL_SetRenderDrawColor(renderer_, 190, 72, 70, 86);
            SDL_RenderFillRect(renderer_, &coarse);
            SDL_SetRenderDrawColor(renderer_, 224, 104, 96, 220);
            SDL_RenderRect(renderer_, &coarse);
        }
    }

    for (const RaidSpecialLocationMapState &location :
         map.specialLocations())
    {
        if (!tacticalMapSpecialLocationVisible(
                location, presentationMode))
        {
            continue;
        }
        const SDL_FRect marker = screenRect(location.entrance);
        SDL_SetRenderDrawColor(renderer_, 86, 194, 178, 104);
        SDL_RenderFillRect(renderer_, &marker);
        SDL_SetRenderDrawColor(renderer_, 126, 234, 210, 245);
        SDL_RenderRect(renderer_, &marker);
        const SDL_FRect inset{
            marker.x + 3.0F,
            marker.y + 3.0F,
            std::max(0.0F, marker.w - 6.0F),
            std::max(0.0F, marker.h - 6.0F)};
        SDL_RenderRect(renderer_, &inset);
        uiTextRenderer_.render(
            renderer_, marker.x + 5.0F, marker.y + 4.0F,
            "SPECIAL SITE");
    }

    if (presentationMode ==
            RaidTacticalMapPresentationMode::FullStaticMap &&
        gameSession_.world().highRiskControlPoint().has_value())
    {
        const SDL_FRect marker = screenRect(
            *gameSession_.world().highRiskControlPoint());
        SDL_SetRenderDrawColor(renderer_, 224, 110, 72, 100);
        SDL_RenderFillRect(renderer_, &marker);
        SDL_SetRenderDrawColor(renderer_, 242, 148, 96, 245);
        SDL_RenderRect(renderer_, &marker);
        uiTextRenderer_.render(
            renderer_, marker.x + 5.0F, marker.y + 4.0F,
            "HIGH-RISK CONTROL");
    }

    const Player &player = gameSession_.world().player();
    const Vec2 playerMarker = screenPoint(Vec2{
        player.position().x + player.size() * 0.5F,
        player.position().y + player.size() * 0.5F});
    SDL_SetRenderDrawColor(renderer_, 226, 238, 224, 255);
    SDL_RenderLine(renderer_, playerMarker.x - 7.0F, playerMarker.y,
                   playerMarker.x + 7.0F, playerMarker.y);
    SDL_RenderLine(renderer_, playerMarker.x, playerMarker.y - 7.0F,
                   playerMarker.x, playerMarker.y + 7.0F);

    const char *mapMode = developerMapFogEnabled_
        ? "FOG OF WAR"
        : "FULL STATIC MAP";
    const std::string tacticalTitle = fmt::format(
        "TACTICAL MAP | {} | M/ESC CLOSE",
        mapMode);
    uiTextRenderer_.render(
        renderer_, 144.0F, 76.0F, tacticalTitle.c_str());
    const std::string permissions = fmt::format(
        "TRANSPORT {} | RESOURCE {} | ENEMY {} | NO LIVE TRACKING",
        map.hasIntelligence(RaidIntelligenceCategory::Transport)
            ? "ACTIVE" : "UNKNOWN",
        map.hasIntelligence(RaidIntelligenceCategory::Resource)
            ? "ACTIVE" : "UNKNOWN",
        map.hasIntelligence(RaidIntelligenceCategory::Enemy)
            ? "ACTIVE" : "UNKNOWN");
    uiTextRenderer_.render(renderer_, 532.0F, 76.0F, permissions.c_str());
    if (map.hasIntelligence(RaidIntelligenceCategory::Transport))
    {
        const RaidSession &raidSession =
            gameSession_.world().raidSession();
        const std::string exitStatus = fmt::format(
            "EXITS | NORMAL {} | SIGNAL {} | LIGHT {}",
            (raidSession.normalExtractionOpen() ||
             raidSession.normalExtractionGraceActive())
                ? "OPEN" : "CLOSED",
            raidSession.emergencyExtractionOpen()
                ? "OPEN" : "LOCKED",
            raidSession.conditionalExtractionOpen() &&
                    gameSession_.conditionalExtractionEligible()
                ? "OPEN" : "LOCKED");
        uiTextRenderer_.render(
            renderer_, 430.0F, 638.0F, exitStatus.c_str());
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderRaidScreen()
{
    const Vec2 cameraOffset = raidWorldCameraOffset();
    const Vec2 shakePixels = raidWorldScreenShakePixels();
    const Vec2 activeWorldSize = gameSession_.world().raidSpaceWorldSize();
    const SDL_Rect worldViewport{
        static_cast<int>(std::lround(shakePixels.x - cameraOffset.x)),
        static_cast<int>(std::lround(shakePixels.y - cameraOffset.y)),
        static_cast<int>(std::ceil(std::max(
            activeWorldSize.x, static_cast<float>(kWindowWidth)))),
        static_cast<int>(std::ceil(std::max(
            activeWorldSize.y, static_cast<float>(kWindowHeight))))};
    const bool worldIsTranslated =
        worldViewport.x != 0 || worldViewport.y != 0;
    if (worldIsTranslated)
    {
        // Keep only the inexpensive ground layer underneath camera and
        // screen-shake translation so viewport edges never expose black
        // strips. Outdoor details are queried and drawn once in world space.
        renderBackground(false);
        static_cast<void>(
            SDL_SetRenderViewport(renderer_, &worldViewport));
    }
    renderBackground();
    renderExtractionPoint();
    renderRaidSpacePortal();
    renderBallisticBlockers();
    if (!gameSession_.world().isAlphaRaidWorld())
    {
        renderStorageCabinet();
    }

    // 地面物品位于角色与敌人下层。
    if (gameSession_.world().isAlphaRaidWorld())
    {
        renderAlphaRaidLoot();
    }
    else
    {
        renderGroundItems();
    }

    renderEnemyAttackTelegraphs();
    renderEnemies();
    renderPlayer();
    renderShotPresentations();
    renderShotFeedbackPresentations();
    renderParticles();

    // Crosshair and every UI/modal layer stay in stable screen coordinates;
    // screen shake therefore cannot alter aiming or inventory interaction.
    if (worldIsTranslated)
    {
        static_cast<void>(SDL_SetRenderViewport(renderer_, nullptr));
    }
    renderAimCrosshair();
    renderCombatFeedback();
    renderRaidTacticalMap();

    // 背包覆盖层显示在游戏世界上方。
    renderInventoryOverlay();
    renderMedicalWheel();

    if (gameSession_.world().raidSession().isTerminal() ||
        gameFlow_.state() == GameFlowState::RaidResult)
    {
        renderStashOverlay();
    }

    // 常驻调试信息位于世界 UI 上方；模态调参面板最后绘制。
    renderDebugText();
    renderDeveloperWeaponPanel();
}

void App::render()
{
    syncRaidPointerCapture();

    SDL_SetRenderDrawColor(
        renderer_,
        0,
        0,
        0,
        255);

    SDL_RenderClear(
        renderer_);

    switch (gameFlow_.state())
    {
    case GameFlowState::MainMenu:
        renderMainMenu();
        break;
    case GameFlowState::Base:
        renderBase();
        break;
    case GameFlowState::Raid:
        renderRaidScreen();
        break;
    case GameFlowState::RaidResult:
        renderRaidScreen();
        renderScreenPrimaryButton(
            "RETURN TO BASE");
        break;
    }

    renderPauseMenu();
    renderDeveloperPerformanceOverlay();

    SDL_RenderPresent(
        renderer_);
}

void App::shutdown()
{
    if (window_ != nullptr)
    {
        static_cast<void>(
            SDL_SetWindowRelativeMouseMode(window_, false));
    }
    relativeMouseModeActive_ = false;
    static_cast<void>(SDL_ShowCursor());
    gameAudio_.shutdown();
    systemCursorHidden_ = false;

    // 所有 SDL_Texture 必须在 Renderer 之前释放。
    for (
        Texture &texture :
        inventoryItemTextures_)
    {
        texture.reset();
    }

    for (
        Texture &texture :
        worldItemTextures_)
    {
        texture.reset();
    }

    enemyMoveHorizontalTexture_.reset();
    playerMoveHorizontalTexture_.reset();
    playerTexture_.reset();
    backgroundTexture_.reset();
    uiTextRenderer_.shutdown();

    SDL_DestroyRenderer(
        renderer_);

    renderer_ = nullptr;

    SDL_DestroyWindow(
        window_);

    window_ = nullptr;

    SDL_Quit();
}

int App::run()
{
    if (!initialize())
    {
        return 1;
    }

    running_ = true;
    lastCounter_ = SDL_GetPerformanceCounter();
    softwareFramePacer_.reset(SDL_GetTicksNS());

    while (running_)
    {
        const Uint64 currentCounter = SDL_GetPerformanceCounter();
        const Uint64 frequency = SDL_GetPerformanceFrequency();

        const float deltaTime = boundedFrameDeltaSeconds(
            static_cast<float>(currentCounter - lastCounter_) /
            static_cast<float>(frequency));
        lastCounter_ = currentCounter;

        const Uint64 frameStart = currentCounter;
        processEvents();
        const Uint64 eventsComplete = SDL_GetPerformanceCounter();
        update(deltaTime);
        const Uint64 updateComplete = SDL_GetPerformanceCounter();
        render();
        const Uint64 renderComplete = SDL_GetPerformanceCounter();
        if (framePacingConfiguration_.absoluteDeadlineEnabled)
        {
            const Uint64 waitNanoseconds =
                softwareFramePacer_.waitDuration(SDL_GetTicksNS());
            if (waitNanoseconds > 0U)
            {
                SDL_DelayPrecise(waitNanoseconds);
            }
        }
        const Uint64 pacingComplete = SDL_GetPerformanceCounter();
        const auto milliseconds = [frequency](Uint64 start, Uint64 end)
        {
            return frequency == 0U
                ? 0.0F
                : static_cast<float>(end - start) * 1000.0F /
                    static_cast<float>(frequency);
        };
        framePerformance_.record(FramePhaseDurations{
            milliseconds(frameStart, eventsComplete),
            milliseconds(eventsComplete, updateComplete),
            milliseconds(updateComplete, renderComplete),
            milliseconds(renderComplete, pacingComplete),
            milliseconds(frameStart, pacingComplete)});
        ++framePerformanceSequence_;
        if (developerPerformanceOverlayOpen_ &&
            (performanceOverlayLines_.front().empty() ||
             framePerformanceSequence_ % 30U == 0U))
        {
            refreshDeveloperPerformanceOverlay();
        }
        pendingRelativeAimMotion_ = Vec2{};
        input_.endFrame();
    }
    shutdown();
    return 0;
}
