// Implementation of the App class
#include "app.h"
#include "base_siege_warning_layout.h"
#include "base_wish_expedition.h"
#include "first_raid_guidance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <SDL3_image/SDL_image.h>
#include <fmt/core.h>

#include "content_registry.h"
#include "developer_runtime_panel.h"
#include "developer_range_panel.h"
#include "developer_test_range.h"
#include "frame_timing.h"
#include "alpha_content_ids.h"
#include "base_build_catalog_presentation.h"
#include "base_facility_management.h"
#include "base_morale_domain.h"
#include "inventory_transfer.h"
#include "loadout_progression.h"
#include "profile_container_presentation.h"
#include "raid_camera.h"
#include "raid_space_signage.h"
#include "raid_tactical_map_presentation.h"

namespace
{
    const char *lightStateLabel(const WeaponLightProjection &light);
    constexpr int kWindowWidth{1280};
    constexpr int kWindowHeight{720};
    constexpr float kBaseBuildCameraPanSpeed{720.0F};
    constexpr float kBaseOperationNoticeLifetimeSeconds{9.0F};
    constexpr std::size_t kBaseBuildCardsPerPage{4U};

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
    constexpr float kBaseStashCellSize{24.0F};
    constexpr float kBaseStashWithRecoveryY{118.0F};
    constexpr float kBaseStashWithRecoveryCellSize{16.0F};
    constexpr float kBaseIntakeX{668.0F};
    constexpr float kBaseIntakeY{398.0F};
    constexpr float kBaseIntakeCellSize{20.0F};
    constexpr float kBasePocketCellSize{28.0F};
    constexpr std::array<EquipmentSlotKind, 3> kWeaponEquipmentSlots{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::SecondaryWeapon,
        EquipmentSlotKind::Sidearm};
    constexpr std::array<EquipmentSlotKind, 8> kProfileEquipmentSlots{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::SecondaryWeapon,
        EquipmentSlotKind::Sidearm,
        EquipmentSlotKind::Helmet,
        EquipmentSlotKind::BodyArmor,
        EquipmentSlotKind::ChestRig,
        EquipmentSlotKind::Backpack,EquipmentSlotKind::QuickMelee};

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
        case EquipmentSlotKind::QuickMelee:
            return "V QUICK MELEE";
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







    SDL_FRect baseBuildBarBounds() noexcept
    {
        return SDL_FRect{70.0F, 566.0F, 1140.0F, 134.0F};
    }

    SDL_FRect baseBuildPurchaseTab() noexcept
    {
        return SDL_FRect{88.0F, 608.0F, 126.0F, 34.0F};
    }

    SDL_FRect baseBuildOwnedTab() noexcept
    {
        return SDL_FRect{224.0F, 608.0F, 126.0F, 34.0F};
    }

    SDL_FRect baseBuildCard(std::size_t index) noexcept
    {
        return SDL_FRect{
            370.0F + static_cast<float>(index) * 168.0F,
            588.0F,
            158.0F,
            82.0F};
    }

    SDL_FRect baseBuildZoomResetButton() noexcept
    {
        return SDL_FRect{1050.0F, 608.0F, 136.0F, 34.0F};
    }

    SDL_FRect baseBuildPreviousPageButton() noexcept
    {
        return SDL_FRect{928.0F, 568.0F, 40.0F, 18.0F};
    }

    SDL_FRect baseBuildNextPageButton() noexcept
    {
        return SDL_FRect{978.0F, 568.0F, 40.0F, 18.0F};
    }

    SDL_FRect baseFacilityInspectorBounds() noexcept
    {
        return SDL_FRect{70.0F, 324.0F, 520.0F, 226.0F};
    }

    SDL_FRect baseOperationsOverviewBounds() noexcept
    {
        return SDL_FRect{812.0F, 64.0F, 398.0F, 244.0F};
    }

    SDL_FRect baseOperationsOverviewRow(std::size_t index) noexcept
    {
        const SDL_FRect panel = baseOperationsOverviewBounds();
        return SDL_FRect{
            panel.x + 12.0F,
            panel.y + 68.0F + static_cast<float>(index) * 34.0F,
            panel.w - 24.0F,
            28.0F};
    }

    SDL_FRect baseOperationsOverviewFilterButton(
        std::size_t index) noexcept
    {
        const SDL_FRect panel = baseOperationsOverviewBounds();
        return SDL_FRect{
            panel.x + 12.0F + static_cast<float>(index) * 92.0F,
            panel.y + 34.0F,
            88.0F,
            24.0F};
    }

    SDL_FRect baseOperationsOverviewPreviousPageButton() noexcept
    {
        const SDL_FRect panel = baseOperationsOverviewBounds();
        return SDL_FRect{panel.x + 12.0F, panel.y + 212.0F, 34.0F, 22.0F};
    }

    SDL_FRect baseOperationsOverviewNextPageButton() noexcept
    {
        const SDL_FRect panel = baseOperationsOverviewBounds();
        return SDL_FRect{
            panel.x + panel.w - 46.0F,
            panel.y + 212.0F,
            34.0F,
            22.0F};
    }

    SDL_FRect basePlacedFacilityInspectorButton(
        std::size_t index,
        std::size_t count) noexcept
    {
        const SDL_FRect panel = baseFacilityInspectorBounds();
        constexpr float gap{8.0F};
        const float width =
            (panel.w - 32.0F - gap * static_cast<float>(count - 1U)) /
            static_cast<float>(count);
        return SDL_FRect{
            panel.x + 16.0F + static_cast<float>(index) * (width + gap),
            panel.y + panel.h - 48.0F,
            width,
            34.0F};
    }

    SDL_FRect baseFacilityQuickActionButton(std::size_t index) noexcept
    {
        const SDL_FRect panel = baseFacilityInspectorBounds();
        constexpr float gap{8.0F};
        constexpr float width{240.0F};
        constexpr float height{34.0F};
        return SDL_FRect{
            panel.x + 16.0F + static_cast<float>(index % 2U) *
                (width + gap),
            panel.y + 138.0F + static_cast<float>(index / 2U) *
                (height + gap),
            width,
            height};
    }

    const char *baseFacilityStatusName(
        BaseFacilityOperationalStatus status) noexcept
    {
        switch (status)
        {
        case BaseFacilityOperationalStatus::Operational:
            return "OPERATIONAL";
        case BaseFacilityOperationalStatus::Reserve:
            return "IN RESERVE";
        case BaseFacilityOperationalStatus::Unavailable:
            return "UNAVAILABLE";
        }
        return "UNKNOWN";
    }

    const char *baseFacilityTaskName(BaseFacilityTaskKind task) noexcept
    {
        switch (task)
        {
        case BaseFacilityTaskKind::Idle:
            return "IDLE";
        case BaseFacilityTaskKind::Construction:
            return "FACILITY UPGRADE";
        case BaseFacilityTaskKind::Manufacturing:
            return "MANUFACTURING";
        case BaseFacilityTaskKind::OutputReady:
            return "OUTPUT READY";
        case BaseFacilityTaskKind::ResidentTreatment:
            return "RESIDENT TREATMENT";
        }
        return "UNKNOWN";
    }

    const char *baseFacilityWorldServiceStatusName(
        BaseFacilityWorldServiceStatus status) noexcept
    {
        switch (status)
        {
        case BaseFacilityWorldServiceStatus::Ready:
            return "READY";
        case BaseFacilityWorldServiceStatus::Working:
            return "WORKING";
        case BaseFacilityWorldServiceStatus::OutputReady:
            return "OUTPUT READY";
        case BaseFacilityWorldServiceStatus::NeedsStaff:
            return "NEEDS STAFF";
        case BaseFacilityWorldServiceStatus::Blocked:
            return "BLOCKED";
        }
        return "UNKNOWN";
    }

    SDL_Color baseFacilityWorldServiceColor(
        BaseFacilityWorldServiceStatus status) noexcept
    {
        switch (status)
        {
        case BaseFacilityWorldServiceStatus::Ready:
            return SDL_Color{126, 224, 174, 255};
        case BaseFacilityWorldServiceStatus::Working:
            return SDL_Color{232, 180, 84, 255};
        case BaseFacilityWorldServiceStatus::OutputReady:
            return SDL_Color{108, 210, 240, 255};
        case BaseFacilityWorldServiceStatus::NeedsStaff:
            return SDL_Color{238, 130, 72, 255};
        case BaseFacilityWorldServiceStatus::Blocked:
            return SDL_Color{226, 84, 78, 255};
        }
        return SDL_Color{174, 188, 180, 255};
    }

    const char *baseFacilityWorkSocketName(
        BaseFacilityWorkSocketKind kind) noexcept
    {
        switch (kind)
        {
        case BaseFacilityWorkSocketKind::StorageHandling:
            return "STORAGE HANDLING";
        case BaseFacilityWorkSocketKind::MedicalBed:
            return "MEDICAL BED";
        case BaseFacilityWorkSocketKind::DormitoryBunk:
            return "DORMITORY BUNK";
        case BaseFacilityWorkSocketKind::KitchenProcessing:
            return "KITCHEN STATION";
        case BaseFacilityWorkSocketKind::WorkshopBench:
            return "WORKBENCH";
        }
        return "WORK POINT";
    }

    const char *baseFacilityWorkerWorldStatusName(
        BaseFacilityWorkerWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseFacilityWorkerWorldStatus::Missing:
            return "MISSING";
        case BaseFacilityWorkerWorldStatus::Idle:
            return "IDLE";
        case BaseFacilityWorkerWorldStatus::Working:
            return "WORKING";
        case BaseFacilityWorkerWorldStatus::Paused:
            return "PAUSED";
        }
        return "MISSING";
    }

    const char *baseFacilityWorkerProfessionName(
        BaseResidentProfession profession) noexcept
    {
        switch (profession)
        {
        case BaseResidentProfession::General:
            return "GENERAL";
        case BaseResidentProfession::Medical:
            return "MEDICAL";
        case BaseResidentProfession::Engineering:
            return "ENGINEERING";
        case BaseResidentProfession::Combat:
            return "COMBAT";
        }
        return "UNKNOWN";
    }

    SDL_Color baseFacilityWorkerWorldColor(
        BaseFacilityWorkerWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseFacilityWorkerWorldStatus::Missing:
            return SDL_Color{230, 102, 78, 255};
        case BaseFacilityWorkerWorldStatus::Idle:
            return SDL_Color{154, 190, 170, 255};
        case BaseFacilityWorkerWorldStatus::Working:
            return SDL_Color{240, 190, 84, 255};
        case BaseFacilityWorkerWorldStatus::Paused:
            return SDL_Color{214, 132, 76, 255};
        }
        return SDL_Color{174, 188, 180, 255};
    }

    std::string baseFacilityWorkerWorldLabel(
        const BaseFacilityWorkerWorldProjection &projection)
    {
        if (!projection.profession.has_value())
            return "WORKER | MISSING";
        std::string label = fmt::format(
            "WORKER | {} | {}",
            baseFacilityWorkerProfessionName(*projection.profession),
            baseFacilityWorkerWorldStatusName(projection.status));
        if (projection.status == BaseFacilityWorkerWorldStatus::Working &&
            projection.remainingMinutes > 0U)
        {
            label += fmt::format(" | {} MIN", projection.remainingMinutes);
        }
        return label;
    }

    SDL_Color baseResidentWorldColor(
        BaseResidentWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseResidentWorldStatus::Empty:
            return SDL_Color{126, 138, 134, 255};
        case BaseResidentWorldStatus::Stable:
            return SDL_Color{122, 202, 166, 255};
        case BaseResidentWorldStatus::Injured:
            return SDL_Color{232, 176, 82, 255};
        case BaseResidentWorldStatus::Overcrowded:
            return SDL_Color{226, 98, 76, 255};
        }
        return SDL_Color{174, 188, 180, 255};
    }

    const char *baseResidentWorldStatusName(
        BaseResidentWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseResidentWorldStatus::Empty:
            return "EMPTY";
        case BaseResidentWorldStatus::Stable:
            return "STABLE";
        case BaseResidentWorldStatus::Injured:
            return "INJURED";
        case BaseResidentWorldStatus::Overcrowded:
            return "OVERCROWDED";
        }
        return "EMPTY";
    }

    std::string baseResidentWorldPrimaryLabel(
        const BaseResidentWorldProjection &projection)
    {
        return fmt::format(
            "RESIDENTS {} | HEALTHY {} | INJURED {} | {}",
            projection.residents,
            projection.healthyResidents,
            projection.injuredResidents,
            baseResidentWorldStatusName(projection.status));
    }

    std::string baseResidentWorldWorkforceLabel(
        const BaseResidentWorldProjection &projection)
    {
        return fmt::format(
            "BEDS {}/{} | AVAILABLE {} | ASSIGNED {} | BUILDING {}",
            projection.residents,
            projection.bedCapacity,
            projection.availableResidents,
            projection.assignedResidents,
            projection.constructionResidents);
    }

    SDL_Color baseResourceFlowWorldColor(
        BaseResourceFlowWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseResourceFlowWorldStatus::Empty:
            return SDL_Color{126, 138, 134, 255};
        case BaseResourceFlowWorldStatus::Available:
            return SDL_Color{126, 174, 218, 255};
        case BaseResourceFlowWorldStatus::Prepared:
            return SDL_Color{122, 202, 166, 255};
        case BaseResourceFlowWorldStatus::Shortage:
            return SDL_Color{226, 112, 76, 255};
        }
        return SDL_Color{174, 188, 180, 255};
    }

    const char *baseResourceFlowWorldStatusName(
        BaseResourceFlowWorldStatus status) noexcept
    {
        switch (status)
        {
        case BaseResourceFlowWorldStatus::Empty:
            return "EMPTY";
        case BaseResourceFlowWorldStatus::Available:
            return "AVAILABLE";
        case BaseResourceFlowWorldStatus::Prepared:
            return "PREPARED";
        case BaseResourceFlowWorldStatus::Shortage:
            return "SHORTAGE";
        }
        return "EMPTY";
    }

    std::string baseResourceFlowWorldPrimaryLabel(
        const BaseResourceFlowWorldProjection &projection)
    {
        if (projection.facility == BaseFacilityKind::Storage)
        {
            return fmt::format(
                "STORAGE | STACKS {} | UNITS {} | {}",
                projection.readiness.baseAccessibleStacks,
                projection.readiness.baseAccessibleUnits,
                baseResourceFlowWorldStatusName(projection.status));
        }
        const BaseResourceBundle &demand = projection.readiness.dailyDemand;
        return fmt::format(
            "DAILY NEED | FOOD {} HYGIENE {} MORALE {} SECURITY {} | {}",
            demand.food, demand.hygiene, demand.morale, demand.security,
            baseResourceFlowWorldStatusName(projection.status));
    }

    std::string baseResourceFlowWorldSecondaryLabel(
        const BaseResourceFlowWorldProjection &projection)
    {
        if (projection.facility == BaseFacilityKind::Storage)
        {
            return fmt::format(
                "SUPPLY RULES {} | OWNED READY {}",
                projection.readiness.assignedDefinitionCount,
                projection.readiness.ownedAssignedDefinitionCount);
        }
        const BaseResourceBundle &pool = projection.readiness.pool;
        const BaseResourceBundle &gap =
            projection.readiness.projectedShortfall;
        return fmt::format(
            "POOL | FOOD {} HYGIENE {} MORALE {} SECURITY {} | "
            "GAP | FOOD {} HYGIENE {} MORALE {} SECURITY {}",
            pool.food, pool.hygiene, pool.morale, pool.security,
            gap.food, gap.hygiene, gap.morale, gap.security);
    }

    std::string baseFacilityWorldServiceLabel(
        const BaseFacilityWorldServiceProjection &projection)
    {
        std::string label = baseFacilityWorldServiceStatusName(
            projection.status);
        if (projection.status == BaseFacilityWorldServiceStatus::Working &&
            projection.remainingMinutes > 0U)
        {
            label += fmt::format(" | {} MIN", projection.remainingMinutes);
        }
        return label;
    }

    const char *baseFacilityQuickActionName(
        BaseFacilityQuickActionKind action) noexcept
    {
        switch (action)
        {
        case BaseFacilityQuickActionKind::OpenFunction:
            return "OPEN DETAILS";
        case BaseFacilityQuickActionKind::AssignBestWorker:
            return "ASSIGN WORKER";
        case BaseFacilityQuickActionKind::ClearWorker:
            return "CLEAR WORKER";
        case BaseFacilityQuickActionKind::StartUpgrade:
            return "START UPGRADE";
        case BaseFacilityQuickActionKind::CancelUpgrade:
            return "CANCEL UPGRADE";
        case BaseFacilityQuickActionKind::CollectManufacturing:
            return "COLLECT OUTPUT";
        case BaseFacilityQuickActionKind::CancelManufacturing:
            return "CANCEL PRODUCTION";
        case BaseFacilityQuickActionKind::StartResidentTreatment:
            return "START TREATMENT";
        case BaseFacilityQuickActionKind::AutoFillWorkers:
            return "AUTO-FILL STAFF";
        }
        return "UNKNOWN ACTION";
    }

    SDL_FRect baseFacilityContextMenuBounds(
        MousePosition origin,
        std::size_t rowCount) noexcept
    {
        constexpr float width{220.0F};
        const float height = 16.0F + static_cast<float>(rowCount) * 37.0F;
        return SDL_FRect{
            std::clamp(origin.x, 8.0F,
                       static_cast<float>(kWindowWidth) - width - 8.0F),
            std::clamp(origin.y, 8.0F,
                       static_cast<float>(kWindowHeight) - height - 8.0F),
            width,
            height};
    }

    SDL_FRect baseFacilityContextMenuRow(
        MousePosition origin,
        std::size_t index,
        std::size_t rowCount) noexcept
    {
        const SDL_FRect menu = baseFacilityContextMenuBounds(origin, rowCount);
        return SDL_FRect{
            menu.x + 8.0F,
            menu.y + 8.0F + static_cast<float>(index) * 37.0F,
            menu.w - 16.0F,
            32.0F};
    }

    SDL_FRect baseFacilityStaffingButton() noexcept
    {
        return SDL_FRect{170.0F, 530.0F, 240.0F, 42.0F};
    }

    SDL_FRect baseFacilityUpgradeButton() noexcept
    {
        return SDL_FRect{430.0F, 530.0F, 240.0F, 42.0F};
    }


    SDL_FRect equipmentSlotRect(EquipmentSlotKind slot) noexcept
    {
        const auto r=profileEquipmentBounds(slot);
        return {r.x,r.y,r.width,r.height};
    }

    SDL_FRect installedMagazineRect(EquipmentSlotKind slot) noexcept
    {
        const auto r=equipmentSlotRect(slot);
        return {r.x+r.w-28,r.y+r.h-34,24,30};
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
        std::string label;
        std::optional<SDL_FRect> clip;
        std::optional<ProfileInventoryViewport> viewport;
    };

    std::vector<ProfileGridView> profileGridViews(
        const ProfileState &profile, bool includeStash,
        std::optional<AssetInstanceId> externalContainerId = std::nullopt,
        const ProfileInventoryPresentation *presentation = nullptr)
    {
        const ProfileInventoryPresentation fallback;
        const auto &state=presentation?*presentation:fallback;
        const auto &content=publishedContentRegistry();
        std::vector<ProfileGridView> result;
        const auto append=[&](ProfileContainerId container,float x,float y,
                              InventoryGridSize window,std::string label,
                              std::optional<SDL_FRect> panelClip=std::nullopt) {
            ProfileInventoryViewport v{profileContainerSize(profile,content,container),window,
                state.originFor(container),x,y,kProfileItemCell,
                container.kind==ProfileContainerKind::Stash};
            // Personal grids are continuous and scroll as one module.
            if(panelClip) v.origin={}; else v.clamp();
            const auto c=v.clip();SDL_FRect clip{c.x,c.y,c.width,c.height};
            if(panelClip) {
                SDL_FRect intersection{};
                if(!SDL_GetRectIntersectionFloat(&clip,&*panelClip,&intersection))return;
                clip=intersection;
            }
            result.push_back({container,x-v.origin.x*kProfileItemCell,
                y-v.origin.y*kProfileItemCell,kProfileItemCell,std::move(label),clip,v});
        };
        if(externalContainerId) {
            if(const auto *owner=profile.assets.find(*externalContainerId)) {
                const auto &d=content.item(owner->definitionId);
                if(!d.containerCompartments.empty()) {
                    const auto index=std::min(state.externalCompartment,
                        static_cast<std::uint32_t>(d.containerCompartments.size()-1));
                    append(ProfileContainerId::compartment(*externalContainerId,index),808,128,{8,8},"");
                }
            }
        } else if(includeStash) {
            const bool overflow=state.legacyOverflow && !assetsInContainer(profile,ProfileContainerId::legacyOverflow()).empty();
            const bool returns=!overflow && state.returns && !assetsInContainer(profile,ProfileContainerId::baseIntake()).empty();
            append(overflow?ProfileContainerId::legacyOverflow():returns?ProfileContainerId::baseIntake():ProfileContainerId::stash(),808,128,{8,8},"");
        }
        const SDL_FRect clip{kProfileCarriedClip.x,kProfileCarriedClip.y,kProfileCarriedClip.width,kProfileCarriedClip.height};
        for(const auto &grid:layoutProfileCarriedGrids(profile,content,state.chestScroll))
            append(grid.container,grid.bounds.x,grid.bounds.y,{9,32},grid.label,clip);
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

    bool movableCoreFacility(BaseFacilityKind facility) noexcept
    {
        return facility == BaseFacilityKind::Storage ||
            facility == BaseFacilityKind::Medical ||
            facility == BaseFacilityKind::Dormitory ||
            facility == BaseFacilityKind::KitchenWater ||
            facility == BaseFacilityKind::Workshop;
    }

    std::optional<BaseFacilityDefinitionId> spatialFacilityDefinitionId(
        BaseFacilityKind facility)
    {
        switch (facility)
        {
        case BaseFacilityKind::Storage:
            return BaseFacilityDefinitionId{"base_facility.warehouse"};
        case BaseFacilityKind::Medical:
            return BaseFacilityDefinitionId{"base_facility.medical"};
        case BaseFacilityKind::Dormitory:
            return BaseFacilityDefinitionId{"base_facility.dormitory"};
        case BaseFacilityKind::KitchenWater:
            return BaseFacilityDefinitionId{"base_facility.kitchen_water"};
        case BaseFacilityKind::Workshop:
            return BaseFacilityDefinitionId{"base_facility.workshop"};
        case BaseFacilityKind::Supply:
        case BaseFacilityKind::Allocation:
        case BaseFacilityKind::RaidGate:
            return std::nullopt;
        }
        return std::nullopt;
    }

    const char *baseOperationOverviewKindName(
        BaseOperationOverviewKind kind) noexcept
    {
        switch (kind)
        {
        case BaseOperationOverviewKind::OutputReady:
            return "OUTPUT READY";
        case BaseOperationOverviewKind::Construction:
            return "UPGRADE";
        case BaseOperationOverviewKind::ResidentTreatment:
            return "RESIDENT TREATMENT";
        case BaseOperationOverviewKind::Manufacturing:
            return "PRODUCTION";
        case BaseOperationOverviewKind::StaffingGap:
            return "NO WORKER";
        case BaseOperationOverviewKind::ResourceShortage:
            return "RESOURCE SHORTAGE";
        case BaseOperationOverviewKind::ResidentPressure:
            return "RESIDENT PRESSURE";
        case BaseOperationOverviewKind::BaseWish:
            return "BASE WISH";
        }
        return "UNKNOWN";
    }

    const char *baseOperationsOverviewFilterName(
        BaseOperationsOverviewFilter filter) noexcept
    {
        switch (filter)
        {
        case BaseOperationsOverviewFilter::All:
            return "VIEW ALL";
        case BaseOperationsOverviewFilter::Attention:
            return "NEEDS ACTION";
        case BaseOperationsOverviewFilter::InProgress:
            return "IN PROGRESS";
        case BaseOperationsOverviewFilter::Ready:
            return "OUTPUT READY";
        }
        return "VIEW ALL";
    }

    const char *baseOperationsFacilityName(BaseFacilityKind kind) noexcept
    {
        switch (kind)
        {
        case BaseFacilityKind::Dormitory:
            return "DORMITORY";
        case BaseFacilityKind::KitchenWater:
            return "KITCHEN / WATER";
        case BaseFacilityKind::Medical:
            return "MEDICAL";
        case BaseFacilityKind::Workshop:
            return "WORKSHOP";
        case BaseFacilityKind::Storage:
            return "STORAGE";
        case BaseFacilityKind::Supply:
            return "SUPPLY";
        case BaseFacilityKind::Allocation:
            return "ALLOCATION";
        case BaseFacilityKind::RaidGate:
            return "RAID GATE";
        }
        return "UNKNOWN";
    }

    const char *baseConstructionTargetName(
        BaseFacilityUpgradeTarget target) noexcept
    {
        switch (target)
        {
        case BaseFacilityUpgradeTarget::Dormitory:
            return "DORMITORY";
        case BaseFacilityUpgradeTarget::KitchenWater:
            return "KITCHEN / WATER";
        case BaseFacilityUpgradeTarget::Workshop:
            return "WORKSHOP";
        case BaseFacilityUpgradeTarget::Medical:
            return "MEDICAL";
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
        std::vector<AssetInstanceId> assetIds;
        bool assignedToSelectedCategory{};
        bool assignedElsewhere{};
    };

    std::vector<BaseSupplyDefinitionProjection> baseSupplyDefinitions(
        const ProfileState &profile,
        BaseSupplyCategory selectedCategory,
        bool focusOnly = false)
    {
        std::vector<BaseSupplyDefinitionProjection> result;
        if (focusOnly && profile.basePriority.focus)
            selectedCategory = publishedContentRegistry().basePriority(
                profile.basePriority.focus->definitionId).category;
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
                projection.assetIds.push_back(id);
                if (!projection.firstAssetId.has_value())
                {
                    projection.firstAssetId = id;
                }
            }
            if (projection.ownedQuantity > 0U ||
                (!focusOnly && projection.assignedToSelectedCategory))
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
        const ProfileState &profile, MousePosition position, bool includeStash,
        std::optional<AssetInstanceId> externalContainerId = std::nullopt,
        const ProfileInventoryPresentation *presentation = nullptr)
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
                    return ProfileAssetHit{profile.assets.find(*magazine),
                                           installedMagazineRect(slot),
                                           kBasePocketCellSize, GridPosition{},
                                           GridPosition{}};
                }
            }
            for (EquipmentSlotKind slot : kProfileEquipmentSlots)
            {
                const SDL_FRect bounds = equipmentSlotRect(slot);
                if (contains(bounds, position))
                {
                    if (const auto id = equippedAsset(profile, slot))
                    {
                        return ProfileAssetHit{profile.assets.find(*id), bounds,
                                               kBasePocketCellSize,
                                               GridPosition{}, GridPosition{}};
                    }
                    return std::nullopt;
                }
            }

        for (const ProfileGridView &view : profileGridViews(
                 profile, includeStash, externalContainerId, presentation))
        {
            if (view.clip && !contains(*view.clip, position))
                continue;
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
        const ProfileState &profile, const AssetRecord &asset,
        bool includeStash,
        std::optional<AssetInstanceId> externalContainerId = std::nullopt,
        const ProfileInventoryPresentation *presentation = nullptr)
    {
        if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location))
        {
            for (const ProfileGridView &view : profileGridViews(
                     profile, includeStash, externalContainerId, presentation))
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

    SDL_FRect profileBaseGroundDropRect() noexcept;
    SDL_FRect profileGroundContainerPickupRect() noexcept;

    std::optional<ProfileDropTarget> profileDropTargetAt(
        const ProfileState &profile, MousePosition position, bool includeStash,
        bool allowBaseGround, GridPosition grabOffset,
        std::optional<AssetInstanceId> draggedAssetId,
        std::optional<AssetInstanceId> externalContainerId = std::nullopt,
        const ProfileInventoryPresentation *presentation = nullptr)
    {
        // Legacy migration storage is a source only, including special item
        // actions such as loading a magazine displayed in that area.
        for (const auto &view : profileGridViews(
                 profile, includeStash, externalContainerId, presentation))
            if (view.container.kind == ProfileContainerKind::LegacyOverflow &&
                view.clip && contains(*view.clip, position))
                return std::nullopt;
        const AssetRecord *dragged = draggedAssetId.has_value()
            ? profile.assets.find(*draggedAssetId)
            : nullptr;
        const ItemDefinition *draggedDefinition = dragged != nullptr
            ? &publishedContentRegistry().item(dragged->definitionId)
            : nullptr;
        const ItemCategory draggedCategory = draggedDefinition != nullptr
            ? draggedDefinition->category
            : ItemCategory::Loot;
        if (allowBaseGround && contains(profileBaseGroundDropRect(), position))
        {
            return BaseGroundTarget{};
        }
        if (!externalContainerId.has_value())
        {
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
            if (const auto hit =
                    profileAssetHitAt(profile, position, includeStash,
                                      externalContainerId, presentation);
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
        }
            for (EquipmentSlotKind slot : kProfileEquipmentSlots)
            {
                if (contains(equipmentSlotRect(slot), position))
                {
                    return EquipmentSlotTarget{slot};
                }
            }

        for (const ProfileGridView &view : profileGridViews(
                 profile, includeStash, externalContainerId, presentation))
        {
            if (view.clip && !contains(*view.clip, position))
                continue;
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

    SDL_FRect profileBaseGroundDropRect() noexcept
    { return {32,642,232,26}; }

    SDL_FRect profileGroundContainerPickupRect() noexcept
    { return {1148,64,108,26}; }

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
        bool shiftPressed,
        UiViewport viewport) noexcept
    {
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            (event.key.scancode == SDL_SCANCODE_PAGEUP ||
             event.key.scancode == SDL_SCANCODE_PAGEDOWN))
        {
            float x{}, y{};
            SDL_GetMouseState(&x, &y);
            const auto p = viewport.toLogical({x, y});
            return InventoryNavigationEvent{
                {p.x, p.y},
                event.key.scancode == SDL_SCANCODE_PAGEUP ? -1 : 1,
                0,
                0};
        }
        if (event.type == SDL_EVENT_MOUSE_WHEEL)
        {
            const auto p =
                viewport.toLogical({event.wheel.mouse_x, event.wheel.mouse_y});
            const float direction =
                event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
            const int rows =
                static_cast<int>(std::round(-event.wheel.y * direction));
            return InventoryNavigationEvent{
                {p.x, p.y},
                0,
                shiftPressed
                    ? rows
                    : static_cast<int>(std::round(-event.wheel.x * direction)),
                shiftPressed ? 0 : rows};
        }
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
            const auto logical=viewport.toLogical({pointerX,pointerY});
            pointerX=logical.x; pointerY=logical.y;

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

    playerTexture=loadInventoryTexture(renderer_,std::filesystem::u8path(playerPath));
    if (!playerTexture.valid())
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

        inventoryItemTextures[index]=loadInventoryTexture(renderer_,std::filesystem::u8path(inventoryPath));
        if (!inventoryItemTextures[index].valid())
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
    const auto clientSettings=loadUiClientSettings(settingsPath_);
    uiTextRenderer_.setLanguage(clientSettings.language);
    const auto area=availableUiWindowArea();
    uiWindowSize_=safeUiWindowSize(clientSettings.window,area.width,area.height);
    SDL_free(preferencePath);

    window_ = SDL_CreateWindow("Project Raidline", uiWindowSize_.width, uiWindowSize_.height, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_ && (uiWindowSize_.width != 1280 || uiWindowSize_.height != 720))
    {
        uiWindowSize_=safeUiWindowSize({1280,720},area.width,area.height);
        window_=SDL_CreateWindow("Project Raidline",uiWindowSize_.width,uiWindowSize_.height,SDL_WINDOW_HIGH_PIXEL_DENSITY);
    }
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

    if (!SDL_SetRenderLogicalPresentation(renderer_,kWindowWidth,kWindowHeight,SDL_LOGICAL_PRESENTATION_LETTERBOX))
    {
        shutdown();
        return false;
    }
    if (!ensureIndoorObservationTargets())
    {
        fmt::print("Indoor observation target creation failed: {}\n", SDL_GetError());
        shutdown();
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
    const auto &aimPointer = worldAimPointerAnchor_.has_value()
        ? worldAimPointerAnchor_ : pointerWorldPosition_;
    if (aimPointer.has_value())
    {
        input.aimWorldPosition = raidScreenToWorld(
            *aimPointer, aimCamera);
    }
    input.aimViewport = Rect{
        aimCamera,
        {static_cast<float>(kWindowWidth),
         static_cast<float>(kWindowHeight)}};
    input.aimWorldBounds = raidReticleWorldBounds(
        aimCamera, input.aimViewport->size);

    if (relativeMouseModeActive_ && shouldCaptureWorldPointer())
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
    input.lightJustPressed = input_.wasActionJustPressed(GameAction::WeaponLight);
    input.lightJustReleased = input_.wasActionJustReleased(GameAction::WeaponLight);
    input.lightPressed = input_.isActionPressed(GameAction::WeaponLight);
    input.meleeJustPressed = input_.wasActionJustPressed(GameAction::QuickMelee);
    input.meleePressed = input_.isActionPressed(GameAction::QuickMelee);
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

GameplayInput App::makeBaseGameplayInput() const
{
    GameplayInput input{};
    input.meleeJustPressed=input_.wasActionJustPressed(GameAction::QuickMelee);
    input.meleePressed=input_.isActionPressed(GameAction::QuickMelee);
    input.moveUp = input_.isActionPressed(GameAction::MoveUp);
    input.moveDown = input_.isActionPressed(GameAction::MoveDown);
    input.moveLeft = input_.isActionPressed(GameAction::MoveLeft);
    input.moveRight = input_.isActionPressed(GameAction::MoveRight);
    input.sprint = input_.isShiftPressed();
    input.fireJustPressed =
        input_.wasActionJustPressed(GameAction::Fire) ||
        input_.wasPrimaryPointerJustPressed();
    input.firePressed =
        input_.isActionPressed(GameAction::Fire) ||
        input_.isPrimaryPointerPressed();
    input.aimDownSights = input_.isSecondaryPointerPressed();
    input.developerInfiniteAmmo = developerInfiniteAmmoEnabled_;

    const Vec2 aimCamera = baseWorldCameraOffset();
    const auto &aimPointer = worldAimPointerAnchor_.has_value()
        ? worldAimPointerAnchor_ : pointerWorldPosition_;
    if (aimPointer.has_value())
    {
        input.aimWorldPosition = raidScreenToWorld(
            *aimPointer, aimCamera);
    }
    input.aimWorldBounds = raidReticleWorldBounds(
        aimCamera,
        {static_cast<float>(kWindowWidth),
         static_cast<float>(kWindowHeight)});
    if (!baseBuildCamera_.active())
    {
        input.aimViewport = Rect{
            aimCamera,
            {static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}};
    }
    input.aimMotionDelta = relativeMouseModeActive_ && shouldCaptureWorldPointer()
        ? pendingRelativeAimMotion_ : Vec2{};
    input.interactJustPressed =
        input_.wasActionJustPressed(GameAction::Interact);
    input.interactPressed = input_.isActionPressed(GameAction::Interact);
    if (input_.wasActionJustPressed(GameAction::SelectWeapon1))
        input.weaponSlotJustPressed = EquipmentSlotKind::PrimaryWeapon;
    else if (input_.wasActionJustPressed(GameAction::SelectWeapon2))
        input.weaponSlotJustPressed = EquipmentSlotKind::SecondaryWeapon;
    else if (input_.wasActionJustPressed(GameAction::SelectWeapon3))
        input.weaponSlotJustPressed = EquipmentSlotKind::Sidearm;
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
            handleBaseDeploymentConfirm();
        break;
    case GameFlowState::Raid:
        break;
    case GameFlowState::RaidResult:
        transitioned = gameFlow_.returnToBase();
        uiMessage_ = transitioned
            ? "TAB: CHECK YOUR ITEMS | PREPARE YOUR NEXT RAID"
            : "hint acknowledgement save failed";
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
    const bool unsafe = !weaponSupply(false).anyWeaponCanFire;
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
    resetRegionalOperationsUi();
    resetLostRecoveryUi();
    selectedRaidSelfRecoveryRecordId_.reset();
    selectedRaidIntelligence_ = {};
    developerMapFogEnabled_ = true;
    developerInfiniteAmmoEnabled_ = false;
    developerCrisisRevealEnabled_ = false;
    uiMessage_.clear();
    return true;
}

void App::openRegionalConstruction(bool reserve)
{
    if (reserve)
    {
        baseConstructionPage_ = BaseConstructionPage::Owned;
        baseConstructionCatalogPage_ = 0U;
    }
    else
    {
        const auto &projects = publishedContentRegistry().baseConstructionProjects();
        const auto project = std::find_if(projects.begin(), projects.end(),
            [](const BaseConstructionProjectDefinition &entry)
            { return entry.target == BaseFacilityUpgradeTarget::KitchenWater; });
        baseConstructionPage_ = BaseConstructionPage::Purchase;
        baseConstructionCatalogPage_ = project == projects.end() ? 0U :
            static_cast<std::size_t>(std::distance(projects.begin(), project)) / kBaseBuildCardsPerPage;
    }
    regionalOperationsOpen_ = false;
    gameFlow_.closeBaseFacility();
    baseConstructionPanelOpen_ = true;
    activateBaseBuildCamera();
    uiMessage_ = reserve ? "SELECT RESERVE FACILITY IN BASE BUILD OWNED PAGE" :
        "OPENED BASE BUILD | KITCHEN & WATER REQUIRED";
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
        if (contains(uiSettingsLanguageButton(), point))
        {
            return MainMenuCommand::ToggleLanguage;
        }
        return contains(uiSettingsBackButton(), point)
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
        if (contains(uiSettingsLanguageButton(), point))
        {
            return PauseMenuCommand::ToggleLanguage;
        }
        return contains(uiSettingsBackButton(), point)
            ? std::optional<PauseMenuCommand>{PauseMenuCommand::Settings}
            : std::nullopt;
    }
    if (gameSession_.baseDefenseActive() && contains(pauseMenuButton(4), point))
        return PauseMenuCommand::AbandonBaseDefense;
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
        baseDefenseAbandonArmed_ = false;
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
        baseConstructionPanelOpen_ = false;
        basePlacementState_.reset();
        baseFixedFacilityPlacementState_.reset();
        deactivateBaseBuildCamera();
        selectedBasePlacedAssetId_.reset();
        selectedBaseFixedFacility_.reset();
        clearProfileItemContext();
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
    case PauseMenuCommand::AbandonBaseDefense:
        if (!baseDefenseAbandonArmed_) baseDefenseAbandonArmed_ = true;
        else if (gameSession_.abandonBaseRealtimeDefense())
        {
            baseDefenseAbandonArmed_ = false;
            pauseMenu_.close();
        }
        uiMessage_ = gameSession_.persistenceMessage();
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
        weaponSupplyCacheProfileId_.clear();
        firstRaidHintsHiddenForRun_ = false;
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
                "profile-" + std::to_string(ticks), true))
        {
            uiMessage_ = gameSession_.persistenceMessage();
            return;
        }
        newGameOverwriteArmed_ = false;
        weaponSupplyCacheProfileId_.clear();
        firstRaidHintsHiddenForRun_ = false;
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
    clearProfileItemContext();
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

SDL_FRect App::baseAutoDefenseButton() const noexcept
{
    return base_siege_warning_layout::automatic;
}

SDL_FRect App::raidIntelligenceButton(std::size_t index) const noexcept
{
    return SDL_FRect{
        382.0F,
        326.0F + static_cast<float>(index) * 42.0F,
        260.0F,
        34.0F};
}

SDL_FRect App::loadoutArchetypeButton(std::size_t index) const noexcept
{
    return SDL_FRect{
        666.0F,
        326.0F + static_cast<float>(index) * 40.0F,
        454.0F,
        34.0F};
}

void App::closeInventory() noexcept
{
    // Tab / browsing Esc closes the inventory and clears all pointer state,
    // hover state, and transient pointer selection.
    inventoryWeaponComponentsOpen_ = false;
    resetWeaponComponentsUi();
    profileDetailsAsset_.reset(); deviceBatteryDrag_.reset();
    inventoryInteraction_.reset();
    profileInventoryInteraction_.reset();
    profilePresentation_ = {};
    clearProfileItemContext();
    profileAssetSelection_.reset();
    openedBaseGroundContainerId_.reset();
    openedRaidSceneContainerId_.reset();

    inventoryOverlayState_.close();
    input_.suppressPrimaryPointerUntilRelease();
}

void App::handleInventoryCancel()
{
    if (inventoryWeaponComponentsOpen_ || profileDetailsAsset_)
    {
        resetWeaponComponentsUi();
        inventoryWeaponComponentsOpen_ = false;
        profileDetailsAsset_.reset(); deviceBatteryDrag_.reset();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (profileContextMenu_.has_value())
    {
        clearProfileItemContext();
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
    updateBaseOperationNotices(deltaTime);
    if (developerWeaponPanelBlocksGameplayThisFrame_)
    {
        pendingBaseClicks_.clear();
        pendingBaseRightClicks_.clear();
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }

    if (handleBaseDefenseControls()) return;
    if (updateBaseSiegeWarning(deltaTime)) return;

    if (baseConstructionPanelOpen_)
    {
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            if (cancelBaseFortificationUi())
            {
                pendingBaseClicks_.clear(); pendingBaseRightClicks_.clear();
                uiMessage_.clear(); input_.suppressPrimaryPointerUntilRelease(); return;
            }
            if (baseFacilityContextMenu_.has_value())
            {
                baseFacilityContextMenu_.reset();
                pendingBaseClicks_.clear();
                pendingBaseRightClicks_.clear();
                uiMessage_.clear();
                input_.suppressPrimaryPointerUntilRelease();
                return;
            }
            baseConstructionPanelOpen_ = false;
            deactivateBaseBuildCamera();
            selectedBasePlacedAssetId_.reset();
            selectedBaseFixedFacility_.reset();
            pendingBaseClicks_.clear();
            pendingBaseRightClicks_.clear();
            uiMessage_ = "BASE BUILD PANEL CLOSED";
            input_.suppressPrimaryPointerUntilRelease();
            return;
        }
        if (updateBaseBuildCameraKeyboard(deltaTime))
        {
            baseFacilityContextMenu_.reset();
            fortificationBuildUi_.menu.reset();
        }
        updateBaseFortificationUi();
        if (!baseConstructionPanelOpen_)
        {
            pendingBaseClicks_.clear(); pendingBaseRightClicks_.clear(); return;
        }
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            if (baseFacilityContextMenu_.has_value())
                handleBaseFacilityContextMenuClick(click.position);
            else
                handleBaseConstructionPanelClick(click.position);
        }
        for (MousePosition position : pendingBaseRightClicks_)
            handleBaseConstructionRightClick(position);
        pendingBaseClicks_.clear();
        pendingBaseRightClicks_.clear();
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }

    if (baseFixedFacilityPlacementState_.has_value())
    {
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            const bool reopenPanel =
                baseFixedFacilityPlacementState_->returnToBuildPanel;
            baseFixedFacilityPlacementState_.reset();
            baseConstructionPanelOpen_ = reopenPanel;
            if (!reopenPanel)
                deactivateBaseBuildCamera();
            pendingBaseClicks_.clear();
            uiMessage_ = "BASE FACILITY PLACEMENT CANCELLED";
            input_.suppressPrimaryPointerUntilRelease();
            return;
        }
        static_cast<void>(updateBaseBuildCameraKeyboard(deltaTime));
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            const Vec2 worldPosition = baseScreenToWorld(
                {click.position.x, click.position.y});
            const BaseFacilityKind facility =
                baseFixedFacilityPlacementState_->facility;
            const bool installing =
                baseFixedFacilityPlacementState_->mode ==
                BaseFixedFacilityPlacementState::Mode::Install;
            const BaseFacilityLayoutReceipt receipt = installing
                ? gameFlow_.installBaseFacilityAt(
                      facility,
                      worldPosition,
                      nextProfileTransactionId(
                          "base-core-facility-install"))
                : gameFlow_.repositionBaseFacilityAt(
                      facility,
                      worldPosition,
                      nextProfileTransactionId(
                          "base-core-facility-move"));
            uiMessage_ = receipt.succeeded
                ? installing
                    ? "BASE FACILITY INSTALLED"
                    : "BASE FACILITY REPOSITIONED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::InventoryMoveOrPlace
                : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                const bool reopenPanel =
                    baseFixedFacilityPlacementState_->returnToBuildPanel;
                baseFixedFacilityPlacementState_.reset();
                baseConstructionPanelOpen_ = reopenPanel;
                if (!reopenPanel)
                    deactivateBaseBuildCamera();
            }
            break;
        }
        pendingBaseClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }

    if (basePlacementState_.has_value())
    {
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            const bool reopenPanel =
                basePlacementState_->returnToBuildPanel;
            basePlacementState_.reset();
            baseFixedFacilityPlacementState_.reset();
            baseConstructionPanelOpen_ = reopenPanel;
            if (!reopenPanel)
                deactivateBaseBuildCamera();
            pendingBaseClicks_.clear();
            pendingBaseRotate_ = false;
            uiMessage_ = "BASE FACILITY PLACEMENT CANCELLED";
            input_.suppressPrimaryPointerUntilRelease();
            return;
        }
        const AssetRecord *asset = gameSession_.profile().assets.find(
            basePlacementState_->assetId);
        if (asset == nullptr)
        {
            basePlacementState_.reset();
            deactivateBaseBuildCamera();
            pendingBaseClicks_.clear();
            pendingBaseRotate_ = false;
            uiMessage_ = "ASSET NO LONGER EXISTS";
            return;
        }
        static_cast<void>(updateBaseBuildCameraKeyboard(deltaTime));
        const ItemDefinition &definition = publishedContentRegistry().item(
            asset->definitionId);
        if (pendingBaseRotate_ && definition.canRotate)
        {
            basePlacementState_->orientation = rotatedClockwise(
                basePlacementState_->orientation);
        }
        pendingBaseRotate_ = false;
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            const Vec2 worldPosition = baseScreenToWorld(
                {click.position.x, click.position.y});
            const bool reposition = basePlacementState_->mode ==
                BasePlacementState::Mode::Reposition;
            const BaseGroundReceipt receipt = reposition
                ? gameFlow_.repositionBaseGroundAssetAt(
                      basePlacementState_->assetId,
                      basePlacementState_->orientation,
                      worldPosition,
                      nextProfileTransactionId("base-facility-reposition"))
                : gameFlow_.dropBaseGroundAssetAt(
                      basePlacementState_->assetId,
                      basePlacementState_->quantity,
                      basePlacementState_->orientation,
                      worldPosition,
                      nextProfileTransactionId("base-facility-place"));
            uiMessage_ = receipt.succeeded
                ? reposition
                    ? "BASE FACILITY REPOSITIONED"
                    : "BASE FACILITY PLACED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::InventoryMoveOrPlace
                : SoundEventId::UiDeny);
            if (receipt.succeeded)
            {
                const bool reopenPanel =
                    basePlacementState_->returnToBuildPanel;
                basePlacementState_.reset();
                baseConstructionPanelOpen_ = reopenPanel;
                if (!reopenPanel)
                    deactivateBaseBuildCamera();
            }
            break;
        }
        pendingBaseClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }

    const bool tacticalToggle = input_.wasActionJustPressed(
        GameAction::ToggleTacticalMap);
    const bool escapePressed = input_.wasActionJustPressed(
        GameAction::InventoryCancel);
    if (tacticalMapOpen_ && (tacticalToggle || escapePressed))
    {
        tacticalMapOpen_ = false;
        uiMessage_ = "HOME REGION MAP CLOSED";
    }
    else if (tacticalToggle &&
             !inventoryOverlayState_.isOpen() &&
             !gameFlow_.activeBaseFacility().has_value() &&
             !developerWeaponPanelOpen_)
    {
        tacticalMapOpen_ = true;
        uiMessage_ = "HOME REGION MAP OPEN - WORLD CONTINUES";
        input_.suppressPrimaryPointerUntilRelease();
    }

    const InventoryFrameInputDecision inventoryDecision =
        decideInventoryFrameInput(
            inventoryOverlayState_.isOpen(),
            !tacticalMapOpen_ &&
                input_.wasActionJustPressed(GameAction::ToggleInventory),
            escapePressed);

    if (inventoryDecision.controlAction == InventoryFrameControlAction::OpenInventory)
    {
        openedBaseGroundContainerId_.reset();
        gameFlow_.closeBaseFacility();
        resetBaseSupplyUi();
        resetBaseMedicalUi();
        resetBaseDormitoryUi();
        resetBaseAllocationUi();
        resetBaseDeploymentUi();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        resetRegionalOperationsUi();
        resetLostRecoveryUi();
        if (gameFlow_.baseWorld().canAccessStash())
        {
            inventoryOverlayState_.openContainerInventory();
            uiMessage_.clear();
        }
        else
        {
            inventoryOverlayState_.openPlayerInventory();
            uiMessage_ = "WAREHOUSE DISCONNECTED - RETURN TO BASE PARCEL";
        }
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
        if (gameSession_.baseDefenseActive())
        {
            BaseInput inventoryInput = makeBaseGameplayInput();
            inventoryInput.inventoryOpen = true;
            inventoryInput.fireJustPressed = inventoryInput.firePressed = false;
            inventoryInput.interactJustPressed = inventoryInput.interactPressed = false;
            gameFlow_.updateBase(inventoryInput, deltaTime);
            consumePresentationAudioEvents();
        }
        return;
    }

    if (gameFlow_.activeBaseFacility().has_value())
    {
        if (gameSession_.baseDefenseActive())
        {
            BaseInput modalInput;
            modalInput.inventoryOpen = true;
            gameFlow_.updateBase(modalInput, deltaTime);
            consumePresentationAudioEvents();
        }
        if (input_.wasActionJustPressed(GameAction::InventoryCancel))
        {
            if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                regionalOperationsOpen_)
            {
                regionalOperationsOpen_ = false;
                resetRegionalOperationsUi();
                resetBaseDeploymentUi();
                deploymentWarningArmed_ = false;
                input_.suppressPrimaryPointerUntilRelease();
                pendingBaseClicks_.clear();
                uiMessage_.clear();
                return;
            }
            if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                lostRaidRecordsOpen_)
            {
                lostRaidRecordsOpen_ = false;
                resetLostRecoveryUi();
                resetBaseDeploymentUi();
                input_.suppressPrimaryPointerUntilRelease();
                pendingBaseClicks_.clear();
                uiMessage_.clear();
                return;
            }
            gameFlow_.closeBaseFacility();
            resetBaseSupplyUi();
            resetBaseMedicalUi();
            resetBaseWorkshopUi();
            resetBaseDormitoryUi();
        resetBaseAllocationUi();
        resetBaseDeploymentUi();
            profileAssetSelection_.reset();
            pendingBaseClicks_.clear();
            deploymentWarningArmed_ = false;
            lostRaidRecordsOpen_ = false;
            regionalOperationsOpen_ = false;
            resetRegionalOperationsUi();
            resetLostRecoveryUi();
            uiMessage_.clear();
            return;
        }

        if (pendingBaseRotate_ && profileAssetSelection_.has_value())
        {
            profileAssetSelection_->orientation = rotatedClockwise(
                profileAssetSelection_->orientation);
        }
        updateRegionalOperationsUi();
        auto lostRecoveryEvents = std::move(pendingLostRecoveryEvents_);
        pendingLostRecoveryEvents_.clear();
        for (const auto &event : lostRecoveryEvents) handleLostRecoveryEvent(event);
        auto deploymentEvents = std::move(pendingDeploymentEvents_);
        pendingDeploymentEvents_.clear();
        for (const auto &event : deploymentEvents) handleBaseDeploymentEvent(event);
        auto allocationEvents = std::move(pendingAllocationEvents_);
        pendingAllocationEvents_.clear();
        for (const auto &event : allocationEvents) handleBaseAllocationEvent(event);
        // Move the queue: closing the panel may clear its member queue.
        auto supplyEvents = std::move(pendingSupplyEvents_);
        pendingSupplyEvents_.clear();
        for (const auto &event : supplyEvents) handleBaseSupplyEvent(event);
        auto workshopEvents = std::move(pendingWorkshopEvents_);
        pendingWorkshopEvents_.clear();
        for (const auto &event : workshopEvents) handleBaseWorkshopEvent(event);
        auto dormitoryEvents = std::move(pendingDormitoryEvents_);
        pendingDormitoryEvents_.clear();
        for (const auto &event : dormitoryEvents) handleBaseDormitoryEvent(event);
        auto medicalEvents = std::move(pendingMedicalEvents_);
        pendingMedicalEvents_.clear(); pendingWorkshopEvents_.clear();
        for (const auto &event : medicalEvents) handleBaseMedicalEvent(event);
        for (const BasePointerClick &click : pendingBaseClicks_)
        {
            handleBasePointerClick(click);
        }
        pendingBaseClicks_.clear();
        return;
    }

    BaseInput input = makeBaseGameplayInput();
    input.freezeObservationDirection=tacticalMapOpen_ || !windowHasInputFocus_;
    if (tacticalMapOpen_)
    {
        input.movementSpeedMultiplier = 0.45F;
        input.sprint = false;
        input.fireJustPressed = false;
        input.firePressed = false;
        input.aimDownSights = false;
        input.weaponSlotJustPressed.reset();
        input.interactJustPressed = false;
        input.interactPressed = false;
    }
    if (input.interactJustPressed)
    {
        if (const auto nearest = gameFlow_.nearestBaseGroundAsset())
        {
            const AssetRecord *asset = gameSession_.profile().assets.find(
                nearest->assetId);
            const bool isContainer = asset != nullptr &&
                !publishedContentRegistry().item(asset->definitionId)
                    .containerCompartments.empty();
            if (isContainer)
            {
                const BaseGroundPlan plan =
                    gameFlow_.queryBaseGroundContainerAccess(
                        nearest->assetId);
                if (plan.canCommit)
                {
                    openedBaseGroundContainerId_ = nearest->assetId;
                    inventoryOverlayState_.openContainerInventory();
                    uiMessage_ = "BASE GROUND CONTAINER OPENED";
                    input_.suppressPrimaryPointerUntilRelease();
                }
                else
                {
                    uiMessage_ = plan.message;
                    gameAudio_.play(SoundEventId::UiDeny);
                }
            }
            else
            {
                const BaseGroundReceipt receipt =
                    gameFlow_.pickupBaseGroundAsset(
                        nearest->assetId,
                        nextProfileTransactionId("base-ground-pickup"));
                uiMessage_ = receipt.succeeded
                    ? "BASE GROUND ITEM PICKED UP"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::InventoryPickup
                    : SoundEventId::UiDeny);
            }
            input.interactJustPressed = false;
            input.interactPressed = false;
        }
    }
    gameFlow_.updateBase(input, deltaTime);
    consumePresentationAudioEvents();
    const BaseWorld &baseWorld = gameFlow_.baseWorld();
    if (baseWorld.shotFiredLastUpdate())
    {
        const auto tuning = gameSession_.developerWeaponTuning();
        gameAudio_.play(
            tuning.has_value() && tuning->weaponUse.automaticFire
                ? SoundEventId::WeaponRifleFire
                : SoundEventId::WeaponPistolFire);
        gameAudio_.play(SoundEventId::WeaponFireTailOutdoor);
    }
    for (const HitResult &hit : baseWorld.hitResultsLastUpdate())
    {
        if (hit.targetKind == HitTargetKind::Enemy)
        {
            gameAudio_.play(SoundEventId::ImpactEnemy);
            gameAudio_.play(hit.targetKilled ? SoundEventId::InfectedDeath : SoundEventId::InfectedHit);
        }
        else gameAudio_.play(hit.targetKind == HitTargetKind::Obstacle
            ? SoundEventId::ImpactObstacle : SoundEventId::ImpactGround);
    }
    if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Storage)
    {
        gameFlow_.closeBaseFacility();
        openedBaseGroundContainerId_.reset();
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
        // Regional commands are owned by their native down/up adapter.
        if (regionalOperationsOpen_) return;
        // Lost-record commands are owned by their native down/up adapter.
        if (lostRaidRecordsOpen_) return;
        // Ordinary deployment input is owned by the native down/up queue.
        return;
    }

    // Allocation owns its native down/up queue; no down-only transaction route.
    if (*facility == BaseFacilityKind::Allocation) return;

    // Workshop owns the native down/up queue.
    if (*facility == BaseFacilityKind::Workshop) return;

    // Medical owns its down/up queue; the legacy down-only adapter must
    // never execute the same transaction through a second route.
    if (*facility == BaseFacilityKind::Medical) return;

    // Dormitory owns its native down/up queue; no legacy command adapter.
    if (*facility == BaseFacilityKind::Dormitory) return;

    if (*facility == BaseFacilityKind::Supply)
    {
        // Supply owns down/up gestures; legacy down-only callbacks cannot trade.
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
                !definition.magazineUse.has_value())
            {
                uiMessage_ = "SELECT A MAGAZINE TO FILL";
                return;
            }
            const AssetRecord *ammunition{};
            for (const AssetRecord *candidate : assetsInContainer(
                     profile,
                     ProfileContainerId::stash()))
            {
                if (publishedContentRegistry().ammunitionFitsMagazine(
                        candidate->definitionId, selected->definitionId))
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
    if (const auto *pointer = std::get_if<InventoryPointerEvent>(&event)) {
        profilePresentationPointer_ = pointer->position;
        if (profileMenuReleaseGuard_) {
            if (pointer->type == InventoryPointerEventType::LeftButtonUp)
                profileMenuReleaseGuard_ = false;
            return;
        }
    } else if (profileMenuReleaseGuard_ &&
               !std::holds_alternative<InventoryNavigationEvent>(event)) return;
    if (profileContextMenu_) {
        if (const auto *pointer = std::get_if<InventoryPointerEvent>(&event)) {
            handleProfileContextMenuPointer(*pointer, inRaid);
            return;
        }
        if (const auto *partial = std::get_if<InventoryPartialTransferEvent>(&event)) {
            handleProfileContextMenuPointer(
                {InventoryPointerEventType::LeftButtonDown, partial->pointerPosition}, inRaid);
            return;
        }
        if (std::holds_alternative<InventoryNavigationEvent>(event))
            clearProfileItemContext();
        else return; // F / rotate cannot operate through a menu.
    }
    if (inventoryWeaponComponentsOpen_)
    {
        if (const auto *pointer = std::get_if<InventoryPointerEvent>(&event)) {
            const auto kind = pointer->type == InventoryPointerEventType::LeftButtonDown ? UiComponentEventKind::Down :
                pointer->type == InventoryPointerEventType::LeftButtonUp ? UiComponentEventKind::Up : UiComponentEventKind::Move;
            handleWeaponComponentsEvent(kind, {pointer->position.x, pointer->position.y});
            if (kind != UiComponentEventKind::Move) input_.suppressPrimaryPointerUntilRelease();
        } else if (const auto *nav = std::get_if<InventoryNavigationEvent>(&event))
            handleWeaponComponentsEvent(UiComponentEventKind::Wheel, {nav->position.x, nav->position.y}, nav->pageDelta);
        return;
    }
    if (profileDetailsAsset_)
    {
        if (profileDetailsAsset_)
            if (const auto *pointer = std::get_if<InventoryPointerEvent>(&event)) handleWeaponLightDetailsPointer(*pointer,inRaid);
        if (const auto *pointer = std::get_if<InventoryPointerEvent>(&event);
            pointer && pointer->type == InventoryPointerEventType::LeftButtonDown)
        {
            if (contains(SDL_FRect{860, 92, 340, 32}, pointer->position))
            {
                inventoryWeaponComponentsOpen_ = false;
                profileDetailsAsset_.reset(); deviceBatteryDrag_.reset();
                uiMessage_.clear();
            }
            input_.suppressPrimaryPointerUntilRelease();
        }
        return;
    }

    const std::optional<AssetInstanceId> externalContainerId = inRaid
        ? openedRaidSceneContainerId_
        : openedBaseGroundContainerId_;
    const bool includeStash = !inRaid &&
        inventoryOverlayState_.showsExternalContainer() &&
        !externalContainerId.has_value();
    const ProfileState &profile = gameSession_.profile();
    if (handleProfilePresentationEvent(event, includeStash,
                                       externalContainerId))
        return;

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
        if (inRaid && !externalContainerId)
        {
            return;
        }
        const auto &quick = std::get<InventoryQuickTransferEvent>(event);
        if (!quick.pointerPosition.has_value())
        {
            return;
        }
        const auto hit =
            profileAssetHitAt(profile, *quick.pointerPosition, includeStash,
                              externalContainerId, &profilePresentation_);
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
                asset.instanceId); equipmentTarget && !(inRaid && externalContainerId))
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
                inRaid);
            return;
        }
        if (!includeStash && !externalContainerId.has_value())
        {
            uiMessage_ = "NO QUICK-TRANSFER DESTINATION";
            gameAudio_.play(SoundEventId::UiDeny);
            return;
        }
        std::vector<ProfileContainerId> destinations;
        if (externalContainerId.has_value())
        {
            const auto *stored = std::get_if<StoredAssetLocation>(
                &asset.location);
            const bool sourceIsExternal = stored != nullptr &&
                stored->container.kind ==
                    ProfileContainerKind::AssetCompartment &&
                stored->container.ownerAssetId == *externalContainerId;
            for (const ProfileGridView &view : profileGridViews(
                     profile,
                     false,
                     sourceIsExternal
                         ? std::nullopt
                         : externalContainerId))
            {
                const bool viewIsExternal =
                    view.container.kind ==
                        ProfileContainerKind::AssetCompartment &&
                    view.container.ownerAssetId == *externalContainerId;
                if (viewIsExternal == !sourceIsExternal)
                    destinations.push_back(view.container);
            }
        }
        else if (const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
                 stored != nullptr &&
                 stored->container.kind == ProfileContainerKind::Stash)
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
            if (inRaid && externalContainerId && definition.maxStackSize > 1U)
                for (const auto *stack : assetsInContainer(profile, container))
                {
                    if (stack->instanceId == asset.instanceId || stack->definitionId != asset.definitionId) continue;
                    const auto location = std::get<StoredAssetLocation>(stack->location);
                    const InventoryCommand merge = InventoryMoveCommand{asset.instanceId, 0, location, asset.orientation};
                    if (gameSession_.queryRaidSceneContainerInventory(*externalContainerId, merge).canCommit)
                    {
                        const auto id = asset.instanceId;
                        const auto revision = profile.revision;
                        executeProfileDrop({{id, revision, asset.location, 0,
                                             asset.orientation},
                                            StoredCellTarget{location}},
                                           true);
                        if (gameSession_.profile().revision != revision)
                            describeProfileInventoryDestination(id, location);
                        return;
                    }
                }
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
                if (!externalContainerId.has_value())
                {
                    const auto installed =
                        std::get<InstalledMagazineLocation>(asset.location);
                    allowed = queryWeaponAmmo(
                        profile,
                        publishedContentRegistry(),
                        UninstallMagazineCommand{
                            installed.weaponAssetId,
                            std::get<StoredCellTarget>(request.target).location,
                            asset.orientation}).canCommit;
                }
            }
            else
            {
                const InventoryCommand command = InventoryMoveCommand{
                    asset.instanceId,
                    0,
                    std::get<StoredCellTarget>(request.target).location,
                    asset.orientation};
                allowed = inRaid && externalContainerId
                    ? gameSession_.queryRaidSceneContainerInventory(*externalContainerId, command).canCommit
                    : externalContainerId.has_value()
                    ? gameFlow_.queryBaseGroundContainerInventory(
                          *externalContainerId,
                          command).canCommit
                    : queryInventory(
                          profile,
                          publishedContentRegistry(),
                          command).canCommit;
            }
            if (allowed)
            {
                const auto revisionBefore = gameSession_.profile().revision;
                const auto targetLocation =
                    std::get<StoredCellTarget>(request.target).location;
                const auto movedId = request.source.instanceId;
                executeProfileDrop(request, inRaid);
                if (gameSession_.profile().revision != revisionBefore)
                    describeProfileInventoryDestination(movedId,
                                                        targetLocation);
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
        clearProfileItemContext();
        const auto hit =
            profileAssetHitAt(profile, position, includeStash,
                              externalContainerId, &profilePresentation_);
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
    if (!inRaid && externalContainerId.has_value() &&
        pointer.type == InventoryPointerEventType::LeftButtonDown &&
        contains(profileGroundContainerPickupRect(), pointer.position))
    {
        const BaseGroundReceipt receipt = gameFlow_.pickupBaseGroundAsset(
            *externalContainerId,
            nextProfileTransactionId("base-ground-container-pickup"));
        uiMessage_ = receipt.succeeded
            ? "BASE GROUND CONTAINER PICKED UP"
            : receipt.message;
        gameAudio_.play(receipt.succeeded
            ? SoundEventId::InventoryPickup
            : SoundEventId::UiDeny);
        if (receipt.succeeded)
            closeInventory();
        return;
    }
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
        profile, pointer.position, includeStash,
        !inRaid && !externalContainerId.has_value(), grabOffset,
        profileInventoryInteraction_.source().has_value()
            ? std::optional<AssetInstanceId>{profileInventoryInteraction_
                                                 .source()
                                                 ->instanceId}
            : std::nullopt,
        externalContainerId, &profilePresentation_);

    switch (pointer.type)
    {
    case InventoryPointerEventType::Motion:
        profileInventoryInteraction_.updatePointerPosition(
            pointer.position, target);
        break;
    case InventoryPointerEventType::LeftButtonDown:
        beginPress(pointer.position, false, false);
        break;
    case InventoryPointerEventType::LeftButtonUp:
        profileInventoryInteraction_.updatePointerPosition(pointer.position,
                                                           target);
        if (profileInventoryInteraction_.pointerPhase() ==
            InventoryPointerPhase::Pressed)
        {
            const auto source = profileInventoryInteraction_.source();
            if (source)
                locateProfileInventoryAsset(source->instanceId, includeStash,
                                            externalContainerId);
        }
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
    if (inventoryWeaponComponentsOpen_ || profileDetailsAsset_ || profileMenuReleaseGuard_) return;
    if (profileInventoryInteraction_.pointerGestureActive())
    {
        return;
    }
    const std::optional<AssetInstanceId> externalContainerId = inRaid
        ? openedRaidSceneContainerId_
        : openedBaseGroundContainerId_;
    const bool includeStash = !inRaid &&
        inventoryOverlayState_.showsExternalContainer() &&
        !externalContainerId.has_value();
    const auto hit =
        profileAssetHitAt(gameSession_.profile(), position, includeStash,
                          externalContainerId, &profilePresentation_);
    if (!hit.has_value() || hit->asset == nullptr)
    {
        clearProfileItemContext();
        return;
    }
    if (externalContainerId.has_value())
    {
        const auto *stored = std::get_if<StoredAssetLocation>(
            &hit->asset->location);
        if (stored != nullptr &&
            stored->container.kind ==
                ProfileContainerKind::AssetCompartment &&
            stored->container.ownerAssetId == *externalContainerId)
        {
            clearProfileItemContext();
            uiMessage_ = "MOVE ITEM TO CARRIED INVENTORY FIRST";
            return;
        }
    }
    const auto actions = projectUiItemActions(gameSession_.profile(),
        publishedContentRegistry(), hit->asset->instanceId, inRaid);
    if (actions.empty())
    {
        clearProfileItemContext();
        return;
    }
    clearProfileItemContext();
    profilePresentationPointer_ = position;
    ProfileContextMenu menu;
    menu.instanceId = hit->asset->instanceId;
    menu.position = position;
    menu.source = {hit->asset->instanceId, gameSession_.profile().revision,
                   hit->asset->location, 0, hit->asset->orientation};
    menu.profileId = gameSession_.profile().profileId;
    menu.inRaid = inRaid;
    menu.language = uiTextRenderer_.language();
    menu.actions = actions;
    menu.layout = layoutUiItemContext({position.x, position.y},
        publishedContentRegistry().item(hit->asset->definitionId).displayName,
        actions, menu.language,
        [&](auto value, int size) { return uiTextRenderer_.measure(value, size); });
    profileContextMenu_ = std::move(menu);
}

void App::executeProfileDrop(const ProfileDropRequest &request, bool inRaid)
{
    const ProfileState &profile = gameSession_.profile();
    if (inRaid && raidSceneContainerRoot(profile, request.source.instanceId) &&
        !std::holds_alternative<StoredCellTarget>(request.target) && !std::holds_alternative<EquipmentSlotTarget>(request.target))
    {
        uiMessage_ = "MOVE ITEM TO CARRIED INVENTORY FIRST";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
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
                    if (!inRaid && openedBaseGroundContainerId_.has_value())
                    {
                        uiMessage_ =
                            "UNINSTALL MAGAZINE TO CARRIED INVENTORY FIRST";
                        gameAudio_.play(SoundEventId::UiDeny);
                        return;
                    }
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
                const InventoryCommand command = InventoryMoveCommand{
                    request.source.instanceId,
                    request.source.quantity,
                    target.location,
                    request.source.orientation};
                const std::string transactionId = nextProfileTransactionId(
                    inRaid ? "raid-move" : "base-move");
                const InventoryReceipt receipt =
                    inRaid && openedRaidSceneContainerId_
                        ? gameSession_.executeRaidSceneContainerInventory(*openedRaidSceneContainerId_, command, transactionId)
                        : !inRaid && openedBaseGroundContainerId_.has_value()
                        ? gameFlow_.executeBaseGroundContainerInventory(
                              *openedBaseGroundContainerId_,
                              command,
                              transactionId)
                        : gameSession_.executeProfileInventory(
                              command,
                              transactionId);
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
                const InventoryCommand command = InventoryEquipCommand{
                    request.source.instanceId,
                    target.slot};
                const std::string transactionId = nextProfileTransactionId(
                    inRaid ? "raid-equip" : "base-equip");
                const InventoryReceipt receipt =
                    inRaid && openedRaidSceneContainerId_
                        ? gameSession_.executeRaidSceneContainerInventory(*openedRaidSceneContainerId_, command, transactionId)
                        : !inRaid && openedBaseGroundContainerId_.has_value()
                        ? gameFlow_.executeBaseGroundContainerInventory(
                              *openedBaseGroundContainerId_,
                              command,
                              transactionId)
                        : gameSession_.executeProfileInventory(
                              command,
                              transactionId);
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
            else if constexpr (
                std::is_same_v<Target, ArmorMaintenanceTarget>)
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
            else
            {
                if (inRaid)
                {
                    uiMessage_ = "BASE GROUND IS NOT AVAILABLE IN RAID";
                    gameAudio_.play(SoundEventId::UiDeny);
                    return;
                }
                const AssetRecord *sourceAsset = profile.assets.find(
                    request.source.instanceId);
                if (sourceAsset == nullptr)
                {
                    uiMessage_ = "ASSET NO LONGER EXISTS";
                    return;
                }
                const ItemDefinition &definition =
                    publishedContentRegistry().item(
                        sourceAsset->definitionId);
                if (definition.basePlacement.has_value())
                {
                    startBasePlacement(
                        request.source.instanceId,
                        BasePlacementState::Mode::PlaceOwned,
                        false);
                    return;
                }
                const BaseGroundReceipt receipt =
                    gameFlow_.dropBaseGroundAsset(
                        request.source.instanceId,
                        request.source.quantity,
                        request.source.orientation,
                        nextProfileTransactionId("base-ground-drop"));
                uiMessage_ = receipt.succeeded
                    ? "ITEM PLACED ON BASE GROUND"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::InventoryMoveOrPlace
                    : SoundEventId::UiDeny);
            }
        },
        request.target);
}

void App::executeProfileContextAction(AssetInstanceId id, bool inRaid)
{
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
    syncBaseSiegeWarningUi();
    pendingSiegeWarningAction_.reset();
    pendingLostRecoveryEvents_.clear();
    pendingRegionalEvents_.clear();
    pendingDeploymentEvents_.clear();
    siegeWarningBlocksGameplayThisFrame_ = baseSiegeWarningVisible();
    pendingInventoryUiEvents_.clear();
    pendingProfileRightClicks_.clear();
    pendingBaseClicks_.clear();
    pendingSupplyEvents_.clear();
    pendingAllocationEvents_.clear();
    pendingMedicalEvents_.clear(); pendingWorkshopEvents_.clear(); pendingDormitoryEvents_.clear();
    pendingBaseRightClicks_.clear();
    pendingBaseRotate_ = false;
    pendingMainMenuCommand_.reset();
    pendingPauseMenuCommand_.reset();
    pendingScreenConfirm_ = false;
    homeFoundingInputBlockedThisFrame_ = homeFoundingPrompt_.has_value();
    developerWeaponPanelBlocksGameplayThisFrame_ =
        developerWeaponPanelOpen_;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        const auto viewport=windowUiViewport();
        if(event.type==SDL_EVENT_MOUSE_MOTION) {
            const auto point=viewport.toLogical({event.motion.x,event.motion.y});
            event.motion.x=point.x; event.motion.y=point.y;
            event.motion.xrel/=viewport.scale; event.motion.yrel/=viewport.scale;
            uiPointerInside_=true;
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP) {
            const auto point=viewport.toLogical({event.button.x,event.button.y});
            event.button.x=point.x; event.button.y=point.y;
            if(event.button.button==SDL_BUTTON_LEFT) uiPointerPressed_=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
        }
        if(event.type==SDL_EVENT_WINDOW_MOUSE_LEAVE || event.type==SDL_EVENT_WINDOW_FOCUS_LOST) {
            uiPointerInside_=false; uiPointerPressed_=false; uiHoveredButton_=-1;
        }
        if(event.type==SDL_EVENT_WINDOW_MOUSE_ENTER) uiPointerInside_=true;
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE ||
            event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
            event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            regionalUi_.press.reset();
            if (regionalUi_.confirmationKey) deploymentWarningArmed_ = false;
            regionalUi_.confirmationKey.reset();
            pendingRegionalEvents_.clear();
            lostRecoveryUi_.press.reset();
            lostRecoveryUi_.confirmationKey.reset();
            pendingLostRecoveryEvents_.clear();
            deploymentUi_.press.reset();
            pendingDeploymentEvents_.clear();
            if (deploymentUi_.confirmationKey) deploymentWarningArmed_ = false;
            deploymentUi_.confirmationKey.reset();
        }
        input_.handleEvent(event);
        if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            allocationUi_.press.reset(); pendingAllocationEvents_.clear();
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            medicalUi_.press.reset(); pendingMedicalEvents_.clear(); workshopUi_.press.reset(); pendingWorkshopEvents_.clear(); dormitoryUi_.press.reset(); pendingDormitoryEvents_.clear(); allocationUi_.press.reset(); pendingAllocationEvents_.clear();
        }
        if((medicalWheelOpen_ || throwableWheelOpen_) && event.type==SDL_EVENT_MOUSE_WHEEL) {
            const auto count=medicalWheelOpen_?medicalWheelOptions_.size():throwableWheelOptions_.size();
            const int pages=static_cast<int>(std::max<std::size_t>(1,(count+7)/8));
            const float direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.0F:1.0F;
            const int delta=event.wheel.y*direction>0?-1:event.wheel.y*direction<0?1:0;
            quickWheelPage_=static_cast<std::size_t>(std::clamp(static_cast<int>(quickWheelPage_)+delta,0,pages-1));
            const auto selected=quickWheelPage_*8+quickWheelSector(wheelPointerOffset_).value_or(0);
            if(medicalWheelOpen_)medicalWheelSelectedIndex_=selected;else throwableWheelSelected_=selected;
            continue;
        }

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
                homeFoundingPrompt_.reset();
                static_cast<void>(
                    SDL_SetWindowRelativeMouseMode(window_, false));
                relativeMouseModeActive_ = false;wheelPointerCaptureActive_=false;wheelPointerOffset_={};quickWheelPage_=0;
                pendingRelativeAimMotion_ = Vec2{};
                static_cast<void>(SDL_ShowCursor());
                systemCursorHidden_ = false;
            }
            supplyUi_.press.reset();
            medicalUi_.press.reset(); pendingMedicalEvents_.clear(); workshopUi_.press.reset(); pendingWorkshopEvents_.clear(); dormitoryUi_.press.reset(); pendingDormitoryEvents_.clear(); allocationUi_.press.reset(); pendingAllocationEvents_.clear();
            componentPress_.reset();
            if (inventoryWeaponComponentsOpen_) pendingInventoryUiEvents_.clear();
            pendingSupplyEvents_.clear();
            inventoryInteraction_.cancelPointerGesture();
            profileInventoryInteraction_.cancelPointerGesture();
            clearProfileItemContext();
            medicalWheelOpen_ = false;
            medicalWheelOptions_.clear();
            developerWeaponPanelOpen_ = false;
            developerWeaponPanelBlocksGameplayThisFrame_ = true;
            baseConstructionPanelOpen_ = false;
            baseFacilityContextMenu_.reset();
            basePlacementState_.reset();
            baseFixedFacilityPlacementState_.reset();
            deactivateBaseBuildCamera();
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
            if(medicalWheelOpen_ || throwableWheelOpen_)
            {
                wheelPointerOffset_=moveQuickWheelPointer(wheelPointerOffset_,motion);
            }
            else if (relativeMouseModeActive_)
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
            if (gameFlow_.state() == GameFlowState::Base &&
                (baseConstructionPanelOpen_ ||
                 basePlacementState_.has_value() ||
                 baseFixedFacilityPlacementState_.has_value()) &&
                baseBuildCamera_.updatePointer(
                    {event.motion.x, event.motion.y},
                    baseConstructionZoom(),
                    gameFlow_.baseWorld().worldSize(),
                    baseBuildViewportWorldSize()))
            {
                baseFacilityContextMenu_.reset();
                fortificationBuildUi_.menu.reset();
            }
            if ((gameFlow_.state() == GameFlowState::Raid ||
                 gameFlow_.state() == GameFlowState::Base) &&
                !inventoryOverlayState_.isOpen() &&
                !pauseMenu_.isOpen() &&
                !developerWeaponPanelOpen_ &&
                !medicalWheelOpen_ && !throwableWheelOpen_ &&
                !baseConstructionPanelOpen_ &&
                !basePlacementState_.has_value() &&
                !baseFixedFacilityPlacementState_.has_value() &&
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

        if((gameFlow_.state()==GameFlowState::MainMenu && settingsOpen_) ||
           (pauseMenu_.isOpen() && pauseMenu_.settingsOpen())) {
            if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button==SDL_BUTTON_LEFT) {
                bool sizeClick=false;
                const auto available=availableUiWindowArea();
                for(std::size_t i=0;i<kUiWindowSizes.size();++i) if(uiWindowSizeAllowed(kUiWindowSizes[i],available.width,available.height) && uiContains(uiSettingsSizeButton(i),{event.button.x,event.button.y})) {
                    pendingUiWindowSize_=i; sizeClick=true; break;
                }
                if(sizeClick) { input_.suppressPrimaryPointerUntilRelease(); continue; }
            }
        }
        if (homeFoundingPrompt_)
        {
            homeFoundingInputBlockedThisFrame_ = true;
            const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
            const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT;
            const bool confirm = (key && event.key.scancode == SDL_SCANCODE_RETURN) ||
                (click && contains(SDL_FRect{370,405,240,42}, MousePosition{event.button.x,event.button.y}));
            const bool cancel = (key && event.key.scancode == SDL_SCANCODE_ESCAPE) ||
                (click && contains(SDL_FRect{660,405,240,42}, MousePosition{event.button.x,event.button.y}));
            if (confirm)
            {
                uiMessage_ = gameFlow_.establishHome(*homeFoundingPrompt_)
                    ? "MAIN BASE ESTABLISHED - YOUR ITEMS ARE UNCHANGED"
                    : gameSession_.persistenceMessage();
                homeFoundingPrompt_.reset();
            }
            else if (cancel) homeFoundingPrompt_.reset();
            input_.suppressPrimaryPointerUntilRelease();
            continue;
        }
        if (gameFlow_.state() != GameFlowState::MainMenu &&
            !pauseMenu_.isOpen() && !developerWeaponPanelOpen_ &&
            event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_H &&
            !gameSession_.profile().homeFounding.hintsDismissed)
        {
            // A Raid-local display choice is never a save of the active Raid.
            if (gameFlow_.state() == GameFlowState::Raid)
                firstRaidHintsHiddenForRun_ = true;
            else if (!gameSession_.dismissHomeHints())
                uiMessage_ = gameSession_.persistenceMessage();
            continue;
        }
        if (gameFlow_.state() == GameFlowState::Base &&
            !pauseMenu_.isOpen() && !developerWeaponPanelOpen_ &&
            !tacticalMapOpen_ && !inventoryOverlayState_.isOpen() &&
            !gameFlow_.activeBaseFacility() &&
            event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
        {
            if (event.key.scancode == SDL_SCANCODE_E && gameFlow_.baseWorld().surveying())
            {
                const auto &world = gameFlow_.baseWorld();
                const Vec2 center{world.playerPosition().x + world.playerSize().x*0.5F,
                    world.playerPosition().y + world.playerSize().y*0.5F};
                for (const auto &plot : homePlotDefinitions())
                {
                    const float dx = center.x - plot.corePosition.x, dy = center.y - plot.corePosition.y;
                    if (dx*dx + dy*dy <= 140.0F*140.0F)
                    {
                        homeFoundingPrompt_ = std::string{plot.id};
                        homeFoundingInputBlockedThisFrame_ = true;
                        break;
                    }
                }
                if (homeFoundingPrompt_) continue;
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

        if (gameSession_.developerRangeActive() && !pauseMenu_.isOpen())
        {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.scancode == SDL_SCANCODE_F8)
            {
                if (developerRangePanelOpen_ || gameSession_.developerRangeAtConsole())
                {
                    developerRangePanelOpen_ = !developerRangePanelOpen_;
                    closeInventory();
                    developerWeaponPanelOpen_ = false;
                    tacticalMapOpen_ = false;
                }
                else uiMessage_ = "RETURN TO RANGE CONSOLE TO OPEN F8";
                developerWeaponPanelBlocksGameplayThisFrame_ = true;
                input_.suppressPrimaryPointerUntilRelease();
                continue;
            }
            if (developerRangePanelOpen_)
            {
                developerWeaponPanelBlocksGameplayThisFrame_ = true;
                input_.suppressPrimaryPointerUntilRelease();
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)
                    developerRangePanelOpen_ = false;
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
                    handleDeveloperRangeClick({event.button.x, event.button.y});
                continue;
            }
        }
        const bool developerPanelAvailable =
            gameFlow_.state() == GameFlowState::Base ||
            (gameFlow_.state() == GameFlowState::Raid &&
             gameSession_.alphaRaidActive());
        if (developerPanelAvailable && !pauseMenu_.isOpen())
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
                    baseConstructionPanelOpen_ = false;
                    basePlacementState_.reset();
                    baseFixedFacilityPlacementState_.reset();
                    deactivateBaseBuildCamera();
                    medicalWheelOpen_ = false;
                    medicalWheelOptions_.clear();
                    tacticalMapOpen_ = false;
                    clearProfileItemContext();
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

        if (gameFlow_.state() == GameFlowState::Base &&
            (gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) &&
            !pauseMenu_.isOpen() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_LEFT)
        {
            input_.suppressPrimaryPointerUntilRelease();
            pendingBaseClicks_.push_back(BasePointerClick{
                MousePosition{event.button.x,event.button.y}, false, false});
            continue;
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

        if (developerWeaponPanelBlocksGameplayThisFrame_)
            continue;
        syncBaseSiegeWarningUi();
        if (routeBaseSiegeWarningEvent(event))
            continue;

        if (gameFlow_.state() == GameFlowState::Base &&
            !pauseMenu_.isOpen() &&
            event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
            event.key.scancode == SDL_SCANCODE_B)
        {
            if (gameSession_.baseDefenseActive())
            {
                uiMessage_ = "BASE LAYOUT LOCKED DURING DEFENSE";
                continue;
            }
            if (!gameSession_.profile().homeFounding.established)
            {
                uiMessage_ = "Establish your main base before building";
                continue;
            }
            if (basePlacementState_.has_value())
                basePlacementState_.reset();
            if (baseFixedFacilityPlacementState_.has_value())
                baseFixedFacilityPlacementState_.reset();
            if (baseConstructionPanelOpen_)
            {
                baseConstructionPanelOpen_ = false;
                deactivateBaseBuildCamera();
                selectedBasePlacedAssetId_.reset();
                selectedBaseFixedFacility_.reset();
                baseFacilityContextMenu_.reset();
                uiMessage_ = "BASE BUILD PANEL CLOSED";
            }
            else if (!gameFlow_.baseWorld().canAccessStash())
            {
                uiMessage_ =
                    "BASE BUILD PANEL REQUIRES BASE PARCEL ACCESS";
                gameAudio_.play(SoundEventId::UiDeny);
            }
            else
            {
                closeInventory();
                gameFlow_.closeBaseFacility();
                tacticalMapOpen_ = false;
                medicalWheelOpen_ = false;
                baseConstructionPanelOpen_ = true;
                fortificationBuildUi_ = {};
                fortificationBuildUi_.category = gameSession_.profile().baseSiege.warningActive;
                baseConstructionCatalogPage_ = 0U;
                selectedBasePlacedAssetId_.reset();
                selectedBaseFixedFacility_.reset();
                baseFacilityContextMenu_.reset();
                activateBaseBuildCamera();
                uiMessage_ = "BASE BUILD PANEL OPEN";
            }
            input_.suppressPrimaryPointerUntilRelease();
            continue;
        }

        if (gameFlow_.state() == GameFlowState::Base &&
            !pauseMenu_.isOpen() &&
            event.type == SDL_EVENT_MOUSE_WHEEL &&
            (baseConstructionPanelOpen_ || basePlacementState_.has_value() ||
             baseFixedFacilityPlacementState_.has_value()))
        {
            if (event.wheel.y != 0.0F)
            {
                adjustBaseConstructionZoom(event.wheel.y > 0.0F ? 1 : -1);
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
            if (baseConstructionPanelOpen_)
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT)
                {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingBaseClicks_.push_back(BasePointerClick{
                        MousePosition{event.button.x, event.button.y},
                        input_.isControlPressed(),
                        input_.isShiftPressed()});
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                         event.button.button == SDL_BUTTON_RIGHT)
                {
                    const MousePosition position{
                        event.button.x, event.button.y};
                    if (fortificationBuildUi_.category && contains(baseBuildBarBounds(), position))
                        pendingBaseRightClicks_.push_back(position);
                    else if (!contains(baseBuildBarBounds(), position) &&
                        (fortificationBuildUi_.category ||
                        (!contains(baseFacilityInspectorBounds(), position) &&
                        !contains(baseOperationsOverviewBounds(), position))))
                        baseBuildCamera_.beginPointer(
                            {position.x, position.y});
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                         event.button.button == SDL_BUTTON_RIGHT)
                {
                    const BaseBuildPointerRelease release =
                        baseBuildCamera_.endPointer();
                    if (release == BaseBuildPointerRelease::Click)
                    {
                        pendingBaseRightClicks_.push_back(
                            MousePosition{
                                event.button.x, event.button.y});
                    }
                }
            }
            else if (basePlacementState_.has_value() ||
                     baseFixedFacilityPlacementState_.has_value())
            {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                    event.button.button == SDL_BUTTON_LEFT)
                {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingBaseClicks_.push_back(BasePointerClick{
                        MousePosition{event.button.x, event.button.y},
                        input_.isControlPressed(),
                        input_.isShiftPressed()});
                }
                else if (basePlacementState_.has_value() &&
                         event.type == SDL_EVENT_KEY_DOWN &&
                         event.key.scancode == SDL_SCANCODE_R &&
                         !event.key.repeat)
                {
                    pendingBaseRotate_ = true;
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                         event.button.button == SDL_BUTTON_RIGHT)
                {
                    baseBuildCamera_.beginPointer(
                        {event.button.x, event.button.y});
                }
                else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                         event.button.button == SDL_BUTTON_RIGHT)
                {
                    static_cast<void>(baseBuildCamera_.endPointer());
                }
            }
            else if (inventoryOverlayState_.isOpen())
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
                             input_.isShiftPressed(), windowUiViewport()))
                {
                    pendingInventoryUiEvents_.push_back(*uiEvent);
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate && regionalOperationsOpen_)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingRegionalEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiRegionalOperationsEventKind::Down : UiRegionalOperationsEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingRegionalEvents_.push_back({UiRegionalOperationsEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingRegionalEvents_.push_back({UiRegionalOperationsEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                     lostRaidRecordsOpen_ && !regionalOperationsOpen_)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingLostRecoveryEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiLostRecoveryEventKind::Down : UiLostRecoveryEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingLostRecoveryEvents_.push_back({UiLostRecoveryEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingLostRecoveryEvents_.push_back({UiLostRecoveryEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate &&
                     !regionalOperationsOpen_ && !lostRaidRecordsOpen_)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingDeploymentEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiDeploymentEventKind::Down : UiDeploymentEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingDeploymentEvents_.push_back({UiDeploymentEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingDeploymentEvents_.push_back({UiDeploymentEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Allocation)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingAllocationEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiAllocationEventKind::Down : UiAllocationEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingAllocationEvents_.push_back({UiAllocationEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingAllocationEvents_.push_back({UiAllocationEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Dormitory)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingDormitoryEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiDormitoryEventKind::Down : UiDormitoryEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingDormitoryEvents_.push_back({UiDormitoryEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingDormitoryEvents_.push_back({UiDormitoryEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Workshop)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingWorkshopEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiWorkshopEventKind::Down : UiWorkshopEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingWorkshopEvents_.push_back({UiWorkshopEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingWorkshopEvents_.push_back({UiWorkshopEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Medical)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingMedicalEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiBaseMedicalEventKind::Down : UiBaseMedicalEventKind::Up, {event.button.x, event.button.y}});
                } else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingMedicalEvents_.push_back({UiBaseMedicalEventKind::Move, {event.motion.x, event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0F : 1.0F;
                    const int delta = event.wheel.y * direction > 0 ? -1 : event.wheel.y * direction < 0 ? 1 : 0;
                    pendingMedicalEvents_.push_back({UiBaseMedicalEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x, event.wheel.mouse_y}), delta});
                }
            }
            else if (gameFlow_.activeBaseFacility() == BaseFacilityKind::Supply)
            {
                if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT)
                {
                    input_.suppressPrimaryPointerUntilRelease();
                    pendingSupplyEvents_.push_back({event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? UiBaseSupplyEventKind::Down : UiBaseSupplyEventKind::Up, {event.button.x,event.button.y}});
                }
                else if (event.type == SDL_EVENT_MOUSE_MOTION)
                    pendingSupplyEvents_.push_back({UiBaseSupplyEventKind::Move, {event.motion.x,event.motion.y}});
                else if (event.type == SDL_EVENT_MOUSE_WHEEL)
                {
                    const float direction=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-1.0F:1.0F;
                    const int delta=event.wheel.y*direction>0?-1:event.wheel.y*direction<0?1:0;
                    pendingSupplyEvents_.push_back({UiBaseSupplyEventKind::Wheel, windowUiViewport().toLogical({event.wheel.mouse_x,event.wheel.mouse_y}),delta});
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
                             input_.isShiftPressed(), windowUiViewport()))
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
                    input_.isShiftPressed(), windowUiViewport());

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
    case DeveloperPanelActionKind::EnterTestRange:
        if (gameSession_.developerRangeActive())
        {
            static_cast<void>(gameFlow_.leaveDeveloperRange());
            developerRangePanelOpen_ = false;
            developerWeaponPanelOpen_ = false;
        }
        else if (gameFlow_.enterDeveloperRange())
        {
            closeInventory();
            tacticalMapOpen_ = false;
            medicalWheelOpen_ = false;
            developerWeaponPanelOpen_ = false;
            developerRangePanelOpen_ = true;
            developerRangeSelection_ = {};
            savedRangeDeveloperSettings_ = std::array{developerInfiniteAmmoEnabled_, developerMapFogEnabled_, developerCrisisRevealEnabled_};
            developerInfiniteAmmoEnabled_ = false;
            uiMessage_ = "RANGE: SELECT EQUIPMENT, THEN GRANT KIT";
        }
        else uiMessage_ = "RANGE REQUIRES AN IDLE BASE";
        break;
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
    case DeveloperPanelActionKind::ToggleCrisisReveal:
        if (!gameSession_.raidHighRiskCrisisProjection().has_value())
        {
            uiMessage_ = "NO FROZEN CRISIS ON THIS MAP";
            break;
        }
        developerCrisisRevealEnabled_ = !developerCrisisRevealEnabled_;
        uiMessage_ = developerCrisisRevealEnabled_
            ? "DEVELOPER CRISIS HUD REVEAL ENABLED"
            : "DEVELOPER CRISIS HUD REVEAL DISABLED";
        break;
    case DeveloperPanelActionKind::TriggerHighRisk:
        uiMessage_ = gameSession_.triggerDeveloperHighRisk()
            ? "DEVELOPER HIGH RISK ACTIVATED"
            : "HIGH RISK ALREADY ACTIVE OR UNAVAILABLE";
        break;
    case DeveloperPanelActionKind::GrantPublishedCatalog:
    {
        const WarehouseCatalogGrantReceipt receipt =
            gameSession_.grantDeveloperWarehouseCatalog();
        uiMessage_ = !receipt.succeeded
            ? receipt.message
            : receipt.alreadyGranted
                ? "DEVELOPER CATALOG ALREADY GRANTED"
                : fmt::format(
                      "DEVELOPER CATALOG GRANTED | {} DEFINITION(S) ADDED",
                      receipt.addedDefinitionCount);
        gameAudio_.play(receipt.succeeded
            ? SoundEventId::UiConfirm
            : SoundEventId::UiDeny);
        break;
    }
    case DeveloperPanelActionKind::TriggerBaseSiegeWarning:
        uiMessage_ = gameFlow_.state() == GameFlowState::Base &&
                gameSession_.triggerDeveloperBaseSiegeWarning()
            ? "BASE SIEGE WARNING READY | CLOSE F10 TO CHOOSE"
            : "BASE SIEGE DEBUG REQUIRES AN IDLE ESTABLISHED BASE";
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
    uiClockSeconds_+=std::max(0.0F,deltaTime);
    deploymentConfirmConsumedThisFrame_ = false;
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate || !regionalOperationsOpen_) {
        if (gameFlow_.state() != GameFlowState::Base ||
            gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate) regionalOperationsOpen_ = false;
        if (regionalProjectionRevision_ || regionalUi_.press || regionalUi_.confirmationKey || !pendingRegionalEvents_.empty())
            resetRegionalOperationsUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
        inventoryOverlayState_.isOpen() || !windowHasInputFocus_ || baseSiegeWarningVisible() ||
        siegeWarningBlocksGameplayThisFrame_ || gameSession_.baseDefenseSaveBlocked() ||
        gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) {
        regionalUi_.press.reset();
        if (regionalUi_.confirmationKey) deploymentWarningArmed_ = false;
        regionalUi_.confirmationKey.reset();
        pendingRegionalEvents_.clear();
    }
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        !lostRaidRecordsOpen_ || regionalOperationsOpen_) {
        if (lostRecoveryProjectionRevision_ || lostRecoveryUi_.press ||
            lostRecoveryUi_.confirmationKey || !pendingLostRecoveryEvents_.empty())
            resetLostRecoveryUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
        inventoryOverlayState_.isOpen() || !windowHasInputFocus_ || baseSiegeWarningVisible() ||
        siegeWarningBlocksGameplayThisFrame_ || gameSession_.baseDefenseSaveBlocked() ||
        gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) {
        lostRecoveryUi_.press.reset();
        lostRecoveryUi_.confirmationKey.reset();
        pendingLostRecoveryEvents_.clear();
    }
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        regionalOperationsOpen_ || lostRaidRecordsOpen_) {
        if (deploymentProjectionRevision_ || deploymentUi_.press ||
            deploymentUi_.confirmationKey || !pendingDeploymentEvents_.empty())
            resetBaseDeploymentUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
        inventoryOverlayState_.isOpen() || !windowHasInputFocus_ || baseSiegeWarningVisible() ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) {
        deploymentUi_.press.reset(); pendingDeploymentEvents_.clear();
        if (deploymentUi_.confirmationKey) deploymentWarningArmed_ = false;
        deploymentUi_.confirmationKey.reset();
    }
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Allocation) {
        if (allocationProjectionRevision_ || allocationUi_.press || !pendingAllocationEvents_.empty()) resetBaseAllocationUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
        inventoryOverlayState_.isOpen() || !windowHasInputFocus_ || baseSiegeWarningVisible() ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() || baseDefenseResultVisible_) {
        allocationUi_.press.reset(); pendingAllocationEvents_.clear();
    }
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Dormitory) {
        if (dormitoryProjectionRevision_ || dormitoryUi_.press || !pendingDormitoryEvents_.empty()) resetBaseDormitoryUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || inventoryOverlayState_.isOpen() || !windowHasInputFocus_) {
        dormitoryUi_.press.reset(); pendingDormitoryEvents_.clear();
    }
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Workshop) {
        if (workshopProjectionRevision_ || workshopUi_.press || !pendingWorkshopEvents_.empty()) resetBaseWorkshopUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || inventoryOverlayState_.isOpen() || !windowHasInputFocus_) {
        workshopUi_.press.reset(); pendingWorkshopEvents_.clear();
    }

    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Medical) {
        if (medicalProjectionRevision_ || medicalUi_.press || !pendingMedicalEvents_.empty()) resetBaseMedicalUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || inventoryOverlayState_.isOpen() || !windowHasInputFocus_) {
        medicalUi_.press.reset(); pendingMedicalEvents_.clear(); workshopUi_.press.reset(); pendingWorkshopEvents_.clear(); dormitoryUi_.press.reset(); pendingDormitoryEvents_.clear(); allocationUi_.press.reset(); pendingAllocationEvents_.clear();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || !windowHasInputFocus_ ||
        (!inventoryWeaponComponentsOpen_ && !weaponComponentsOpen_)) componentPress_.reset();
    if (gameFlow_.state() != GameFlowState::Base || gameFlow_.activeBaseFacility() != BaseFacilityKind::Supply)
    {
        if (supplyRowsRevision_ || supplyUi_.selection || supplyUi_.press || weaponComponentsOpen_) resetBaseSupplyUi();
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ || inventoryOverlayState_.isOpen() || !windowHasInputFocus_)
    {
        supplyUi_.press.reset(); pendingSupplyEvents_.clear();
    }
    if (profilePresentationProfileId_ != gameSession_.profile().profileId)
    {
        profilePresentation_ = {};
        profileHoverTab_ = -1;
        profileHoverSeconds_ = 0;
        profilePresentationProfileId_ = gameSession_.profile().profileId;
    }
    updateProfilePresentationHover(deltaTime);
    updateProfileItemContext(deltaTime);
    if(pendingUiWindowSize_) {const auto index=*pendingUiWindowSize_;pendingUiWindowSize_.reset();applyUiWindowSize(index);}

    // Revoke the external view before processing queued clicks or stale drags.
    if (openedRaidSceneContainerId_ && !gameSession_.raidSceneContainerAvailable(*openedRaidSceneContainerId_))
    {
        closeInventory();
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        uiMessage_ = gameSession_.world().inOutdoorRaidSpace() ? "SCENE CONTAINER OUT OF RANGE" : "STORAGE ACCESS LOST";
    }
    if (!gameSession_.developerRangeActive())
    {
        developerRangePanelOpen_ = false;
        if (savedRangeDeveloperSettings_)
        {
            developerInfiniteAmmoEnabled_ = (*savedRangeDeveloperSettings_)[0];
            developerMapFogEnabled_ = (*savedRangeDeveloperSettings_)[1];
            developerCrisisRevealEnabled_ = (*savedRangeDeveloperSettings_)[2];
            savedRangeDeveloperSettings_.reset();
        }
    }
    if (developerRangePanelOpen_) return;
    if (homeFoundingInputBlockedThisFrame_) return;
    syncAmbience();
    consumePresentationAudioEvents();
    if (gameFlow_.state() != GameFlowState::Base &&
        gameFlow_.state() != GameFlowState::Raid)
    {
        developerWeaponPanelOpen_ = false;
        developerWeaponPanelBlocksGameplayThisFrame_ = false;
    }
    if (gameFlow_.state() != GameFlowState::Base &&
        gameFlow_.state() != GameFlowState::Raid)
    {
        tacticalMapOpen_ = false;
    }
    if (gameFlow_.state() != GameFlowState::Base)
    {
        basePlacementState_.reset();
        baseFixedFacilityPlacementState_.reset();
        deactivateBaseBuildCamera();
    }
    if (gameFlow_.state() != GameFlowState::Raid)
    {
        developerCrisisRevealEnabled_ = false;
    }
    const bool escapePressed = input_.wasActionJustPressed(
        GameAction::InventoryCancel);
    const bool pauseEligible =
        gameFlow_.state() == GameFlowState::Base ||
        gameFlow_.state() == GameFlowState::Raid;
    if (pauseMenu_.isOpen())
    {
        gameSession_.cancelWeaponLightInput();
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
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ ||
        baseDefenseResultVisible_ ||
        baseConstructionPanelOpen_ ||
        inventoryOverlayState_.isOpen() ||
        basePlacementState_.has_value() ||
        baseFixedFacilityPlacementState_.has_value() ||
        medicalWheelOpen_ || throwableWheelOpen_ || selectedThrowable_.has_value() ||
        tacticalMapOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ ||
        (gameFlow_.state() == GameFlowState::Base &&
         gameFlow_.activeBaseFacility().has_value());
    if (pauseEligible && escapePressed && !existingModalHandlesEscape)
    {
        pauseMenu_.open();
        gameSession_.cancelWeaponLightInput();
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
        if (screenConfirm && !baseSiegeWarningVisible() &&
            !siegeWarningBlocksGameplayThisFrame_ && !regionalOperationsOpen_ &&
            gameFlow_.activeBaseFacility() == BaseFacilityKind::RaidGate)
        {
            // Enter and a queued pointer release share one explicit intention.
            pendingDeploymentEvents_.clear();
            deploymentUi_.press.reset();
            pendingLostRecoveryEvents_.clear();
            lostRecoveryUi_.press.reset();
            lostRecoveryUi_.confirmationKey.reset();
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
            else if (std::holds_alternative<InventoryPartialTransferEvent>(
                         event))
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
        if (!throwableWheelOpen_ && !selectedThrowable_ && !gameSession_.world().noiseDecoy().flight() &&
            input_.wasActionJustPressed(GameAction::Heal) &&
            !gameSession_.raidActionState().active().has_value())
        {
            openMedicalWheel();
            gameplayInput.healJustPressed = false;
            gameplayInput.weaponSlotJustPressed.reset();
        }
        if (medicalWheelOpen_)
        {
            updateMedicalWheelSelection();
            gameplayInput.aimMotionDelta={};
            input_.suppressPrimaryPointerUntilRelease();
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
    updateThrowableInput(gameplayInput);
    if (gameplayInput.interactJustPressed && !inventoryOverlayState_.isOpen() && !tacticalMapOpen_ &&
        !medicalWheelOpen_ && !developerWeaponPanelOpen_ && !developerWeaponPanelBlocksGameplayThisFrame_ &&
        inputDecision.controlAction == InventoryFrameControlAction::None)
    {
        if (const auto container = gameSession_.nearbyRaidSceneContainer())
        {
            profileInventoryInteraction_.reset();
            clearProfileItemContext();
            openedRaidSceneContainerId_ = container;
            inventoryOverlayState_.openContainerInventory();
            input_.suppressPrimaryPointerUntilRelease();
            gameplayInput.inventoryOpen = true;
            gameplayInput.interactJustPressed = gameplayInput.interactPressed = false;
            gameplayInput.fireJustPressed = gameplayInput.firePressed = false;
            gameplayInput.reloadJustPressed = gameplayInput.healJustPressed = false;
            gameplayInput.weaponSlotJustPressed.reset();
            const auto *root = gameSession_.profile().assets.find(*container);
            uiMessage_ = root && std::holds_alternative<RaidCorpseContainerAssetLocation>(root->location)
                ? "CORPSE OPENED | WORLD CONTINUES" : "SCENE CONTAINER OPENED | WORLD CONTINUES";
        }
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

    gameplayInput.freezeObservationDirection = inventoryOverlayState_.isOpen() ||
        tacticalMapOpen_ || medicalWheelOpen_ || throwableWheelOpen_ || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_;
    gameFlow_.update(
        gameplayInput,
        deltaTime);
    if (openedRaidSceneContainerId_ && !gameSession_.raidSceneContainerAvailable(*openedRaidSceneContainerId_))
    {
        closeInventory();
        uiMessage_ = gameSession_.world().inOutdoorRaidSpace() ? "SCENE CONTAINER OUT OF RANGE" : "STORAGE ACCESS LOST";
    }
    if (gameFlow_.state() == GameFlowState::Base)
    {
        closeInventory();
        tacticalMapOpen_ = false;
        medicalWheelOpen_ = false;
        developerWeaponPanelOpen_ = false;
        uiMessage_ = "RETURNED FROM TEST RANGE | ORIGINAL PROFILE RESTORED";
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    consumePresentationAudioEvents();
    for (unsigned pulse=0; pulse<gameSession_.world().noiseDecoy().pulsesLastUpdate(); ++pulse)
        gameAudio_.play(SoundEventId::NoiseDecoyPulse);

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

    playerDamageFeedbackRemaining_ = std::max(
        0.0F,
        playerDamageFeedbackRemaining_ - std::max(0.0F, deltaTime));
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
        case GameSessionPresentationEvent::PlayerHurtLight:
        case GameSessionPresentationEvent::PlayerHurtHeavy:
            gameAudio_.play(event == GameSessionPresentationEvent::PlayerHurtHeavy
                ? SoundEventId::PlayerHurtHeavy : SoundEventId::PlayerHurtLight);
            break;
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
        case GameSessionPresentationEvent::BaseDormitoryUpgradeCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::FacilityUpgrade,
                BaseFacilityKind::Dormitory);
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        case GameSessionPresentationEvent::BaseKitchenWaterUpgradeCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::FacilityUpgrade,
                BaseFacilityKind::KitchenWater);
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        case GameSessionPresentationEvent::BaseWorkshopUpgradeCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::FacilityUpgrade,
                BaseFacilityKind::Workshop);
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        case GameSessionPresentationEvent::BaseMedicalUpgradeCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::FacilityUpgrade,
                BaseFacilityKind::Medical);
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        case GameSessionPresentationEvent::BaseManufacturingCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::Manufacturing,
                BaseFacilityKind::Workshop);
            gameAudio_.play(SoundEventId::UiConfirm);
            break;
        case GameSessionPresentationEvent::BaseResidentTreatmentCompleted:
            pushBaseOperationNotice(
                BaseOperationNoticeKind::ResidentTreatment,
                BaseFacilityKind::Medical);
            gameAudio_.play(SoundEventId::MedicalComplete);
            break;
        }
    }
}

void App::updateBaseOperationNotices(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
        return;
    baseOperationNoticePulseSeconds_ += deltaTime;
    for (BaseOperationNotice &notice : baseOperationNotices_)
        notice.remainingSeconds = std::max(
            0.0F, notice.remainingSeconds - deltaTime);
    std::erase_if(
        baseOperationNotices_,
        [](const BaseOperationNotice &notice)
        {
            return notice.remainingSeconds <= 0.0F;
        });
}

void App::pushBaseOperationNotice(
    BaseOperationNoticeKind kind,
    BaseFacilityKind facility)
{
    const auto matching = std::find_if(
        baseOperationNotices_.begin(), baseOperationNotices_.end(),
        [&](const BaseOperationNotice &notice)
        {
            return notice.kind == kind && notice.facility == facility;
        });
    if (matching != baseOperationNotices_.end())
    {
        matching->remainingSeconds = kBaseOperationNoticeLifetimeSeconds;
        return;
    }
    if (baseOperationNotices_.size() >= 4U)
        baseOperationNotices_.erase(baseOperationNotices_.begin());
    baseOperationNotices_.push_back(BaseOperationNotice{
        kind, facility, kBaseOperationNoticeLifetimeSeconds});
}

bool App::hasBaseOperationNotice(BaseFacilityKind facility) const noexcept
{
    return std::any_of(
        baseOperationNotices_.begin(), baseOperationNotices_.end(),
        [&](const BaseOperationNotice &notice)
        {
            return notice.facility == facility;
        });
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

    const bool indoorVisibility=gameSession_.world().isAlphaRaidWorld() &&
        (gameSession_.world().lightConeObservationEnabled() || !gameSession_.world().inOutdoorRaidSpace());
    const Vec2 camera=raidWorldCameraOffset();
    const auto onScreen=[&](Rect bounds)
    {
        return !indoorVisibility || (bounds.position.x<camera.x+kWindowWidth && bounds.position.y<camera.y+kWindowHeight &&
            bounds.position.x+bounds.size.x>camera.x && bounds.position.y+bounds.size.y>camera.y);
    };
    std::size_t aliveEnemyCount{};
    std::size_t alertedEnemyCount{};
    std::size_t searchingEnemyCount{};
    for (const Enemy &enemy : gameSession_.world().enemies())
    {
        if (!enemy.isDead() && (!indoorVisibility || (gameSession_.world().raidEnemyObservable(enemy.combatTargetId()) && onScreen(enemy.bounds()))))
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
            fmt::runtime(indoorVisibility ? "Visible Enemies: {0} | Alerted {2} | Searching {3}" : "Enemies: {}/{} | Alerted {} | Searching {}"),
            aliveEnemyCount,
            indoorVisibility ? aliveEnemyCount : gameSession_.world().enemies().size(),
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
                    gameSession_.raidLootAccessible(*snapshot) &&
                    onScreen({{snapshot->position.x-publishedContentRegistry().item(asset.definitionId).worldRenderSize.x*0.5F,
                               snapshot->position.y-publishedContentRegistry().item(asset.definitionId).worldRenderSize.y*0.5F},
                               publishedContentRegistry().item(asset.definitionId).worldRenderSize}))
                {
                    ++groundCount;
                }
            }
        }
    }
    const std::string groundItemText =
        fmt::format(fmt::runtime(indoorVisibility ? "Visible Ground Items: {}" : "Ground Items: {}"), groundCount);

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

    float leftHudLineY = 180.0F;
    const bool signalCallAvailable = gameSession_.canCallSignalExtraction(
        inventoryOverlayState_.isOpen());
    const char *signalHint = nullptr;
    if (raidSession.extractionRoute() == RaidExtractionRoute::EmergencySignal)
        signalHint = "SIGNAL SENT - NEARBY ENEMIES ALERTED";
    else if (gameSession_.world().signalInterruptedVisible())
        signalHint = signalCallAvailable
            ? "SIGNAL INTERRUPTED - RETURN AND PRESS F TO RETRY"
            : "SIGNAL INTERRUPTED";
    else if (signalCallAvailable)
        signalHint = "F - CALL EXTRACTION (ATTRACTS NEARBY ENEMIES)";
    if (signalHint != nullptr)
    {
        uiTextRenderer_.render(renderer_, 20.0F, leftHudLineY, signalHint);
        leftHudLineY += 16.0F;
    }
    if (raidSession.phase() == RaidPhase::HighRisk &&
        gameSession_.world().highRiskActiveEnemyCap() > 0U)
    {
        const std::string pressureText = fmt::format(
            fmt::runtime(indoorVisibility
                ? "Pressure wave {} | Next {:.0f}s | Visible Enemies {} | Converging on crisis target"
                : "Pressure wave {} | Next {:.0f}s | Active {}/{} | Converging on crisis target"),
            gameSession_.world().highRiskPressureWaveCount(),
            gameSession_.world().highRiskNextWaveSeconds(),
            indoorVisibility ? aliveEnemyCount : gameSession_.world().aliveEnemyCount(),
            gameSession_.world().highRiskActiveEnemyCap());
        uiTextRenderer_.render(
            renderer_,
            20.0F,
            leftHudLineY,
            pressureText.c_str());
        leftHudLineY += 16.0F;
    }

    if (const auto crisis = gameSession_.raidHighRiskCrisisProjection();
        crisis.has_value())
    {
        const std::string crisisText =
            (crisis->detailsKnown || developerCrisisRevealEnabled_)
            ? fmt::format(
                  "CRISIS: {} | {}",
                  crisis->displayName,
                  crisis->warning)
            : "CRISIS: UNKNOWN | ENEMY DOSSIER REVEALS BEFORE ACTIVATION";
        uiTextRenderer_.render(
            renderer_, 20.0F, leftHudLineY, crisisText.c_str());
        leftHudLineY += 16.0F;

        if (crisis->active && raidSession.highRiskTimeElapsed() < 5.0F)
        {
            const SDL_FRect warningPanel{360.0F, 238.0F, 560.0F, 78.0F};
            SDL_SetRenderDrawColor(renderer_, 76, 18, 12, 218);
            SDL_RenderFillRect(renderer_, &warningPanel);
            SDL_SetRenderDrawColor(renderer_, 248, 150, 58, 248);
            SDL_RenderRect(renderer_, &warningPanel);
            const SDL_FRect warningInset{
                warningPanel.x + 4.0F,
                warningPanel.y + 4.0F,
                warningPanel.w - 8.0F,
                warningPanel.h - 8.0F};
            SDL_RenderRect(renderer_, &warningInset);
            uiTextRenderer_.render(
                renderer_, warningPanel.x + 176.0F,
                warningPanel.y + 14.0F,
                "HIGH-RISK CRISIS ACTIVE");
            const std::string warning = fmt::format(
                "{} | PRESSURE CONVERGING ON MARKED TARGET",
                crisis->displayName);
            uiTextRenderer_.render(
                renderer_, warningPanel.x + 78.0F,
                warningPanel.y + 42.0F,
                warning.c_str());
        }
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
        leftHudLineY,
        worldTimeText.c_str());
    leftHudLineY += 16.0F;

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
            renderer_, 20.0F, leftHudLineY, objective.c_str());
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
            renderer_, 20.0F, leftHudLineY, objective.c_str());
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
            renderer_, 20.0F, leftHudLineY, objective.c_str());
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
    const auto &diagnosticActors=gameSession_.world().enemies();
    const auto diagnosticEnemy=std::find_if(diagnosticActors.begin(),diagnosticActors.end(),[&](const Enemy &enemy)
    { return !indoorVisibility || (!enemy.isDead() && onScreen(enemy.bounds()) && gameSession_.world().raidEnemyObservable(enemy.combatTargetId())); });
    if (diagnosticEnemy!=diagnosticActors.end())
    {
        const Enemy &enemy = *diagnosticEnemy;
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

    if(indoorVisibility && enemyAiText.starts_with("Lead AI")) enemyAiText.replace(0,7,"Visible target AI");
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
        renderProfileInventory(false, true, openedRaidSceneContainerId_);
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
                     index < (result->wishReturn ? 5U : 9U);
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
        if (result && result->wishReturn)
        {
            const auto &summary = *result->wishReturn;
            const std::string title = "WISH RETURN | " + publishedContentRegistry().basePriority(
                summary.focus.wish.definitionId).displayName;
            const std::string numbers = fmt::format("WISH ITEMS {} | CONTRIBUTION {}/{}",
                summary.itemCount, summary.contribution, summary.focus.requiredContribution);
            uiTextRenderer_.render(renderer_, panel.x + 24.0F, panel.y + 294.0F, title.c_str());
            uiTextRenderer_.render(renderer_, panel.x + 24.0F, panel.y + 316.0F, numbers.c_str());
            uiTextRenderer_.render(renderer_, panel.x + 24.0F, panel.y + 340.0F,
                summary.expired ? (result->outcome == RaidResultOutcome::Extracted ?
                    "WISH ENDED | ITEMS STAY IN PLACE" : "WISH ENDED | NO RETURNED CONTRIBUTION") :
                summary.contribution >= summary.focus.requiredContribution ?
                    "WISH STOCK SUFFICIENT | SUBMIT MANUALLY IN BASE" : "WISH STOCK INSUFFICIENT | NO EXTRA PENALTY");
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
    if (gameSession_.developerRangeActive())
    {
        SDL_SetRenderDrawColor(renderer_, 24, 31, 33, 255);
        SDL_RenderClear(renderer_);
        SDL_SetRenderDrawColor(renderer_, 48, 62, 65, 255);
        for (float x = 0; x <= kDeveloperRangeSize.x; x += 100)
            SDL_RenderLine(renderer_, x, 0, x, kDeveloperRangeSize.y);
        for (float y = 0; y <= kDeveloperRangeSize.y; y += 100)
            SDL_RenderLine(renderer_, 0, y, kDeveloperRangeSize.x, y);
        for (std::size_t i = 0; i < kDeveloperRangePads.size(); ++i)
        {
            const auto p = kDeveloperRangePads[i];
            const SDL_FRect rect{p.x - 10, p.y - 10, 240, 160};
            SDL_SetRenderDrawColor(renderer_, 193, 151, 79, 255);
            SDL_RenderRect(renderer_, &rect);
            uiTextRenderer_.render(renderer_, p.x, p.y - 35, fmt::format("TARGET PAD {}", i + 1).c_str());
        }
        const SDL_FRect console{kDeveloperRangeConsole.x - 40, kDeveloperRangeConsole.y - 35, 200, 110};
        SDL_SetRenderDrawColor(renderer_, 80, 181, 158, 255);
        SDL_RenderRect(renderer_, &console);
        uiTextRenderer_.render(renderer_, console.x, console.y - 30, "RANGE CONSOLE | F8");
        return;
    }
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
        if (gameSession_.profile().pendingRaid)
        {
            const auto &map = publishedContentRegistry().map(
                gameSession_.profile().pendingRaid->mapDefinitionId);
            const ContentRect view{raidWorldCameraOffset(),
                {static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}};
            for (const auto &sign : raidRoomSigns(map,
                     gameSession_.world().activeRaidSpaceId(), worldSize))
            {
                if (!raidSignVisible(sign.bounds, view)) continue;
                const SDL_FRect plate{sign.bounds.position.x, sign.bounds.position.y,
                    sign.bounds.size.x, sign.bounds.size.y};
                SDL_SetRenderDrawColor(renderer_, 24, 44, 39, 220);
                SDL_RenderFillRect(renderer_, &plate);
                SDL_SetRenderDrawColor(renderer_, 176, 217, 193, 255);
                uiTextRenderer_.render(renderer_, plate.x + 5, plate.y + 5,
                    sign.displayName.c_str());
            }
        }
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
            gameSession_.canCallSignalExtraction(inventoryOverlayState_.isOpen())
                ? "SIGNAL EXTRACTION | F TO CALL" : "SIGNAL EXTRACTION",
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

    const std::vector<ShotPresentationSnapshot> shots =
        gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().shotPresentationSnapshots()
        : gameSession_.world().shotPresentationSnapshots();
    for (
        const ShotPresentationSnapshot &shot :
        shots)
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
        !gameSession_.world().raidSession().isActive())
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

Vec2 App::baseWorldCameraOffset() const noexcept
{
    if (gameFlow_.state() != GameFlowState::Base)
        return {};
    const BaseWorld &world = gameFlow_.baseWorld();
    const Vec2 position = world.playerPosition();
    const Vec2 size = world.playerSize();
    const Vec2 viewport = baseBuildViewportWorldSize();
    if (baseBuildCamera_.active())
        return baseBuildCamera_.offset(world.worldSize(), viewport);
    return ::raidCameraOffset(
        {position.x + size.x * 0.5F, position.y + size.y * 0.5F},
        world.worldSize(),
        viewport);
}

float App::baseConstructionZoom() const noexcept
{
    if (!baseConstructionPanelOpen_ && !basePlacementState_.has_value() &&
        !baseFixedFacilityPlacementState_.has_value())
        return 1.0F;
    constexpr std::array<float, 5U> zoomLevels{
        0.60F, 0.75F, 1.00F, 1.25F, 1.50F};
    return zoomLevels[std::min(
        baseConstructionZoomIndex_, zoomLevels.size() - 1U)];
}

Vec2 App::baseScreenToWorld(Vec2 screenPosition) const noexcept
{
    const float zoom = baseConstructionZoom();
    const Vec2 camera = baseWorldCameraOffset();
    return baseBuildScreenToWorld(
        screenPosition,
        camera,
        zoom,
        raidWorldScreenShakePixels());
}

Vec2 App::baseBuildViewportWorldSize() const noexcept
{
    const float zoom = baseConstructionZoom();
    return {
        static_cast<float>(kWindowWidth) / zoom,
        static_cast<float>(kWindowHeight) / zoom};
}

void App::activateBaseBuildCamera() noexcept
{
    if (gameFlow_.state() != GameFlowState::Base ||
        baseBuildCamera_.active())
    {
        return;
    }
    const BaseWorld &world = gameFlow_.baseWorld();
    const Vec2 position = world.playerPosition();
    const Vec2 size = world.playerSize();
    baseBuildCamera_.activate(
        {position.x + size.x * 0.5F,
         position.y + size.y * 0.5F},
        world.worldSize(),
        baseBuildViewportWorldSize());
}

void App::focusBaseFixedFacility(BaseFacilityKind facility) noexcept
{
    if (gameFlow_.state() != GameFlowState::Base)
        return;
    const BaseWorld &world = gameFlow_.baseWorld();
    const auto found = std::find_if(
        world.facilities().begin(), world.facilities().end(),
        [facility](const BaseFacility &candidate)
        {
            return candidate.kind == facility;
        });
    if (found == world.facilities().end())
        return;
    const BaseFacilityAccessGeometry access =
        baseFacilityAccessGeometry(*found);
    baseBuildCamera_.activate(
        access.entrancePoint,
        world.worldSize(),
        baseBuildViewportWorldSize());
}

void App::deactivateBaseBuildCamera() noexcept
{
    fortificationBuildUi_ = {};
    baseBuildCamera_.deactivate();
}

bool App::updateBaseBuildCameraKeyboard(float deltaTime) noexcept
{
    Vec2 direction{};
    direction.x += input_.isActionPressed(GameAction::MoveRight) ? 1.0F : 0.0F;
    direction.x -= input_.isActionPressed(GameAction::MoveLeft) ? 1.0F : 0.0F;
    direction.y += input_.isActionPressed(GameAction::MoveDown) ? 1.0F : 0.0F;
    direction.y -= input_.isActionPressed(GameAction::MoveUp) ? 1.0F : 0.0F;
    return baseBuildCamera_.panKeyboard(
        direction,
        deltaTime,
        kBaseBuildCameraPanSpeed,
        baseConstructionZoom(),
        gameFlow_.baseWorld().worldSize(),
        baseBuildViewportWorldSize());
}

Vec2 App::raidWorldScreenShakePixels() const noexcept
{
    const Vec2 normalized = gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().normalizedShotScreenShakeOffset()
        : gameSession_.world().normalizedShotScreenShakeOffset();
    // Deliberately below the threshold that would displace the crosshair or
    // interfere with ordinary visual reading. Only the world viewport moves.
    return Vec2{normalized.x * 1.8F, normalized.y * 1.3F};
}

void App::renderShotFeedbackPresentations()
{
    constexpr int kLightSegments{20};
    constexpr float kTau{6.28318530718F};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    const std::vector<ShotFeedbackPresentationSnapshot> shots =
        gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().shotFeedbackPresentationSnapshots()
        : gameSession_.world().shotFeedbackPresentationSnapshots();
    for (const ShotFeedbackPresentationSnapshot &shot : shots)
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
            const auto structureSign = raidLandmarkStructureSign(prop,
                gameSession_.profile().pendingRaid->spatialLayout.landmarks);
            uiTextRenderer_.render(
                renderer_, bounds.x + 3.0F,
                prop.collidable ? bounds.y + 3.0F : bounds.y - 14.0F,
                structureSign.empty() ? raidOutdoorPropLabel(prop.kind) : structureSign.data());
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

bool App::shouldCaptureWorldPointer() const noexcept
{
    const bool baseWorldActive = gameFlow_.state() == GameFlowState::Base &&
        !gameFlow_.activeBaseFacility().has_value() &&
        !gameSession_.baseDefenseSaveBlocked() && !gameSession_.baseDailySaveBlocked() && !baseDefenseResultVisible_ &&
        !baseSiegeWarningVisible() && !siegeWarningBlocksGameplayThisFrame_;
    const bool raidWorldActive = gameFlow_.isRaidScreen() &&
        gameSession_.world().raidSession().isActive();
    return shouldCaptureRaidPointer(
        RaidPointerCaptureContext{
            raidWorldActive || baseWorldActive,
            raidWorldActive || baseWorldActive,
            inventoryOverlayState_.isOpen() ||
                basePlacementState_.has_value() ||
                baseFixedFacilityPlacementState_.has_value() ||
                baseConstructionPanelOpen_ || homeFoundingPrompt_.has_value(),
            medicalWheelOpen_ || throwableWheelOpen_,
            developerWeaponPanelOpen_ || developerRangePanelOpen_,
            pauseMenu_.isOpen(),
            tacticalMapOpen_,
            windowHasInputFocus_});
}

void App::syncRaidPointerCapture() noexcept
{
    const bool wheel=(medicalWheelOpen_ || throwableWheelOpen_) && windowHasInputFocus_ &&
        !inventoryOverlayState_.isOpen() && !pauseMenu_.isOpen() && !tacticalMapOpen_;
    if(wheelPointerCaptureActive_!=wheel) {
        wheelPointerCaptureActive_=wheel;pendingRelativeAimMotion_={};
        if(!wheel) {wheelPointerOffset_={};quickWheelPage_=0;}
        input_.suppressPrimaryPointerUntilRelease();
    }
    const bool shouldCapture = shouldCaptureWorldPointer() || wheel;
    if (shouldCapture != relativeMouseModeActive_)
    {
        const Vec2 reticle = gameFlow_.isRaidScreen()
            ? raidWorldToScreen(gameSession_.world().weaponAimWorldPosition(), raidWorldCameraOffset())
            : raidWorldToScreen(gameFlow_.baseWorld().weaponAimWorldPosition(), baseWorldCameraOffset());
        if (!shouldCapture && windowHasInputFocus_ && window_ != nullptr)
        {
            pointerWorldPosition_ = Vec2{
                std::clamp(reticle.x, 0.0F, static_cast<float>(kWindowWidth - 1)),
                std::clamp(reticle.y, 0.0F, static_cast<float>(kWindowHeight - 1))};
            // SDL uses this position when relative mode ends. Warp before
            // releasing capture so its motion cannot enter world aiming.
            const auto windowPoint=windowUiViewport().toWindow(*pointerWorldPosition_);
            SDL_WarpMouseInWindow(window_, windowPoint.x, windowPoint.y);
        }
        if (SDL_SetWindowRelativeMouseMode(window_, shouldCapture))
        {
            if (shouldCapture && !worldAimPointerAnchor_)
                worldAimPointerAnchor_ = pointerWorldPosition_.value_or(reticle);
            relativeMouseModeActive_ = shouldCapture;
            pendingRelativeAimMotion_ = Vec2{};
            if (shouldCapture) input_.suppressPrimaryPointerUntilRelease();
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
    const bool inBaseWorld = gameFlow_.state() == GameFlowState::Base &&
        !gameFlow_.activeBaseFacility().has_value() &&
        !gameSession_.baseDefenseSaveBlocked() && !gameSession_.baseDailySaveBlocked() && !baseDefenseResultVisible_ &&
        !baseSiegeWarningVisible() && !siegeWarningBlocksGameplayThisFrame_;
    const bool inRaidWorld = gameFlow_.isRaidScreen() &&
        gameSession_.world().raidSession().isActive();
    if (inventoryOverlayState_.isOpen() ||
        basePlacementState_.has_value() ||
        baseFixedFacilityPlacementState_.has_value() ||
        baseConstructionPanelOpen_ ||
        medicalWheelOpen_ || throwableWheelOpen_ ||
        tacticalMapOpen_ ||
        developerWeaponPanelOpen_ ||
        developerRangePanelOpen_ ||
        pauseMenu_.isOpen() ||
        (!inBaseWorld && !inRaidWorld))
    {
        return;
    }

    const WeaponAccuracyProjection accuracy = inBaseWorld
        ? gameFlow_.baseWorld().weaponAccuracyProjection()
        : gameSession_.world().weaponAccuracyProjection();
    const float feedbackRadius = std::round(std::max(
        10.0F,
        accuracy.reticleRadius));
    constexpr float kArmLength{15.0F};
    const Vec2 camera = inBaseWorld
        ? baseWorldCameraOffset() : raidWorldCameraOffset();
    const Vec2 screenCenter = raidWorldToScreen(accuracy.center, camera);
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
    const HitFeedbackPresentationSnapshot hitFeedback = inBaseWorld
        ? gameFlow_.baseWorld().hitFeedbackPresentation()
        : gameSession_.world().observableHitFeedback();
    const auto feedbackSourceOnScreen = [&](const std::optional<HitFeedbackSource> &source)
    {
        if (inBaseWorld || !gameSession_.world().lightConeObservationEnabled()) return true;
        if (!source) return false;
        const Vec2 camera=raidWorldCameraOffset();
        return source->position.x>=camera.x && source->position.y>=camera.y &&
            source->position.x<camera.x+kWindowWidth && source->position.y<camera.y+kWindowHeight;
    };
    if (hitFeedback.armorFeedbackSeconds > 0.0F && feedbackSourceOnScreen(hitFeedback.armorSource))
        uiTextRenderer_.render(renderer_, center.x + 24, center.y + 26,
            hitFeedback.armorBroken ? "ENEMY ARMOR BROKEN" : "ENEMY ARMOR ABSORBED HIT");
    if (hitFeedback.remainingSeconds > 0.0F && feedbackSourceOnScreen(hitFeedback.semanticSource))
    {
        const Uint8 red = hitFeedback.semantic == HitSemantic::WeakPoint
            ? 246
            : 250;
        const Uint8 green = hitFeedback.semantic == HitSemantic::WeakPoint
            ? 92
            : 194;
        const Uint8 blue = hitFeedback.semantic == HitSemantic::WeakPoint
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
    medicalWheelOptions_=quickWheelCandidates(gameSession_.profile(),publishedContentRegistry(),UiQuickWheelKind::Medical);
    medicalWheelSelectedIndex_=0;quickWheelPage_=0;wheelPointerOffset_={};
    pendingRelativeAimMotion_={};medicalWheelOpen_=true;uiMessage_.clear();
}

void App::updateMedicalWheelSelection()
{
    if(!medicalWheelOpen_)return;
    if(const auto sector=quickWheelSector(wheelPointerOffset_))medicalWheelSelectedIndex_=quickWheelPage_*8+*sector;
}

void App::commitMedicalWheelSelection()
{
    if (!medicalWheelOpen_ || medicalWheelSelectedIndex_ >= medicalWheelOptions_.size())
    {
        medicalWheelOpen_ = false;
        medicalWheelOptions_.clear();
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
    if(!playerTexture_.valid())return;
    const auto source=playerTexture_.contentBounds();
    const auto fit=fitInventoryTexture(source.w,source.h,{bounds.x,bounds.y,bounds.w,bounds.h},ItemOrientation::Degrees0,0);
    const SDL_FRect destination{fit.x,fit.y,fit.width,fit.height};
    SDL_RenderTexture(renderer_,playerTexture_.get(),&source,&destination);
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
        if (gameSession_.world().isAlphaRaidWorld() && !gameSession_.world().raidEnemyObservable(enemy.combatTargetId())) continue;
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

        if (enemy.torsoArmor())
        {
            const bool intact = enemy.torsoArmor()->durability > 0;
            const SDL_FRect vest{bounds.position.x + bounds.size.x * 0.20F,
                bounds.position.y + bounds.size.y * 0.29F, bounds.size.x * 0.60F, bounds.size.y * 0.43F};
            SDL_SetRenderDrawColor(renderer_, 48, 58, 62, 255);
            SDL_RenderFillRect(renderer_, &vest);
            SDL_SetRenderDrawColor(renderer_, 220, 215, 175, 255);
            SDL_RenderRect(renderer_, &vest);
            if (intact) {
                SDL_RenderLine(renderer_, vest.x, vest.y + vest.h/2, vest.x + vest.w, vest.y + vest.h/2);
                SDL_RenderLine(renderer_, vest.x + vest.w/2, vest.y, vest.x + vest.w/2, vest.y + vest.h);
            } else {
                SDL_RenderLine(renderer_, vest.x, vest.y, vest.x + vest.w*0.4F, vest.y + vest.h*0.5F);
                SDL_RenderLine(renderer_, vest.x + vest.w*0.6F, vest.y + vest.h*0.5F, vest.x + vest.w, vest.y + vest.h);
            }
            uiTextRenderer_.render(renderer_, enemyRect.x, enemyRect.y - 16,
                intact ? "TORSO ARMOR" : "BROKEN VEST");
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
        if (gameSession_.world().isAlphaRaidWorld() && !gameSession_.world().raidEnemyObservable(enemy.combatTargetId())) continue;
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

    const std::vector<Particle> &particles =
        gameFlow_.state() == GameFlowState::Base
        ? gameFlow_.baseWorld().particles()
        : gameSession_.world().particles();
    for (const Particle &particle : particles)
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

void App::renderMainMenu() { renderFoundationMenu(false); }
void App::renderPauseMenu() { if(pauseMenu_.isOpen()) renderFoundationMenu(true); }

void App::renderBaseWorld()
{
    const BaseWorld &world = gameFlow_.baseWorld();
    const Vec2 camera = baseWorldCameraOffset();
    const Vec2 shakePixels = raidWorldScreenShakePixels();
    const Vec2 worldSize = world.worldSize();
    const float zoom = baseConstructionZoom();
    static_cast<void>(SDL_SetRenderScale(renderer_, zoom, zoom));
    const Vec2 viewportOrigin = baseBuildViewportOrigin(
        camera, zoom, shakePixels);
    const SDL_Rect worldViewport{
        static_cast<int>(std::lround(viewportOrigin.x)),
        static_cast<int>(std::lround(viewportOrigin.y)),
        static_cast<int>(std::ceil(worldSize.x)),
        static_cast<int>(std::ceil(worldSize.y))};
    static_cast<void>(SDL_SetRenderViewport(renderer_, &worldViewport));

    SDL_SetRenderDrawColor(renderer_, 45, 49, 36, 255);
    const SDL_FRect worldGround{0.0F, 0.0F, worldSize.x, worldSize.y};
    SDL_RenderFillRect(renderer_, &worldGround);

    const HomeRegionLayout &layout = world.layout();
    const HomeRegionPresentationProjection &projection =
        world.outdoorPresentation(ContentRect{
            camera,
            {static_cast<float>(kWindowWidth) / zoom,
             static_cast<float>(kWindowHeight) / zoom}});
    const float cellWidth = worldSize.x / static_cast<float>(layout.columns);
    const float cellHeight = worldSize.y / static_cast<float>(layout.rows);
    for (const RaidTerrainSpan &span : projection.terrainSpans)
    {
        switch (span.kind)
        {
        case RaidTerrainKind::Grass:
            SDL_SetRenderDrawColor(renderer_, 54, 66, 42, 255); break;
        case RaidTerrainKind::Concrete:
            SDL_SetRenderDrawColor(renderer_, 76, 75, 68, 255); break;
        case RaidTerrainKind::Dirt:
            SDL_SetRenderDrawColor(renderer_, 84, 69, 49, 255); break;
        case RaidTerrainKind::Asphalt:
            SDL_SetRenderDrawColor(renderer_, 50, 52, 50, 255); break;
        case RaidTerrainKind::Puddle:
            SDL_SetRenderDrawColor(renderer_, 48, 70, 76, 255); break;
        }
        const SDL_FRect bounds{
            span.firstColumn * cellWidth, span.row * cellHeight,
            span.length * cellWidth + 0.5F, cellHeight + 0.5F};
        SDL_RenderFillRect(renderer_, &bounds);
    }
    for (const RaidOutdoorRoadCell &road : projection.roadCells)
    {
        if (road.kind == RaidOutdoorRoadKind::Primary)
            SDL_SetRenderDrawColor(renderer_, 91, 91, 83, 255);
        else if (road.kind == RaidOutdoorRoadKind::Secondary)
            SDL_SetRenderDrawColor(renderer_, 76, 78, 72, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 66, 69, 62, 255);
        const SDL_FRect bounds{road.column * cellWidth,
                               road.row * cellHeight,
                               cellWidth + 0.5F, cellHeight + 0.5F};
        SDL_RenderFillRect(renderer_, &bounds);
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const ContentRect &baseParcel = world.baseParcel();
    const SDL_FRect transition{
        baseParcel.position.x - kHomePerimeterTransitionWidth,
        baseParcel.position.y - kHomePerimeterTransitionWidth,
        baseParcel.size.x + kHomePerimeterTransitionWidth * 2.0F,
        baseParcel.size.y + kHomePerimeterTransitionWidth * 2.0F};
    SDL_SetRenderDrawColor(renderer_, 188, 174, 92, 82);
    SDL_RenderRect(renderer_, &transition);
    const SDL_FRect parcel{baseParcel.position.x, baseParcel.position.y,
                           baseParcel.size.x, baseParcel.size.y};
    SDL_SetRenderDrawColor(renderer_, 30, 56, 50, 120);
    SDL_RenderFillRect(renderer_, &parcel);
    SDL_SetRenderDrawColor(renderer_, 126, 196, 166, 240);
    SDL_RenderRect(renderer_, &parcel);

    if (world.surveying())
    {
        SDL_SetRenderDrawColor(renderer_, 220, 230, 200, 255);
        uiTextRenderer_.render(renderer_, kFoundingCamp.position.x,
            kFoundingCamp.position.y - 26, "TEMPORARY SUPPLY POINT - SHARED STASH");
    }
    if (world.hasFoundingPlots())
        for (const auto &plot : homePlotDefinitions())
        {
            const SDL_FRect bounds{plot.bounds.position.x,plot.bounds.position.y,plot.bounds.size.x,plot.bounds.size.y};
            SDL_SetRenderDrawColor(renderer_, 165,180,120,180);
            SDL_RenderRect(renderer_, &bounds);
            const SDL_FRect core{plot.corePosition.x-20,plot.corePosition.y-20,40,40};
            SDL_RenderFillRect(renderer_, &core);
            uiTextRenderer_.render(renderer_,plot.bounds.position.x+24,plot.bounds.position.y+24,plot.name.data());
            if (world.surveying())
                uiTextRenderer_.render(renderer_,plot.corePosition.x-120,plot.corePosition.y+34,"E INSPECT BASE SITE");
        }

    for (const RaidOutdoorPropSnapshot &prop : projection.props)
    {
        const SDL_FRect bounds{prop.bounds.position.x, prop.bounds.position.y,
                               prop.bounds.size.x, prop.bounds.size.y};
        switch (prop.kind)
        {
        case RaidOutdoorPropKind::Factory:
            SDL_SetRenderDrawColor(renderer_, 62, 66, 62, 242); break;
        case RaidOutdoorPropKind::Warehouse:
            SDL_SetRenderDrawColor(renderer_, 69, 67, 57, 242); break;
        case RaidOutdoorPropKind::Container:
            SDL_SetRenderDrawColor(renderer_, 91, 72, 48, 235); break;
        case RaidOutdoorPropKind::EngineeringEquipment:
            SDL_SetRenderDrawColor(renderer_, 112, 89, 43, 235); break;
        case RaidOutdoorPropKind::Car:
            SDL_SetRenderDrawColor(renderer_, 69, 78, 73, 235); break;
        case RaidOutdoorPropKind::Truck:
            SDL_SetRenderDrawColor(renderer_, 79, 76, 61, 235); break;
        case RaidOutdoorPropKind::RoadBarrier:
            SDL_SetRenderDrawColor(renderer_, 119, 103, 70, 235); break;
        case RaidOutdoorPropKind::Debris:
            SDL_SetRenderDrawColor(renderer_, 97, 91, 75, 160); break;
        }
        SDL_RenderFillRect(renderer_, &bounds);
        if (prop.collidable)
        {
            SDL_SetRenderDrawColor(renderer_, 151, 134, 92, 230);
            SDL_RenderRect(renderer_, &bounds);
        }
        uiTextRenderer_.render(renderer_, bounds.x + 3.0F,
                               bounds.y + 3.0F,
                               raidOutdoorPropLabel(prop.kind));
    }
    for (const HomeRegionDistrictSnapshot &district : projection.districts)
    {
        uiTextRenderer_.render(renderer_, district.labelPosition.x,
                               district.labelPosition.y,
                               homeRegionDistrictName(district.kind));
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    for (const BaseFacility &facility : world.facilities())
    {
        if (!facility.active)
            continue;
        const BaseFacilityAccessGeometry access =
            baseFacilityAccessGeometry(facility);
        const BaseFacilityWorldServiceProjection service =
            projectBaseFacilityWorldService(
                gameSession_.profile(),
                publishedContentRegistry(),
                facility.kind);
        const std::optional<BaseFacilityWorkSocketProjection> workSocket =
            baseFacilityWorkSocket(facility);
        const std::optional<BaseFacilityWorkerWorldProjection> worker =
            projectBaseFacilityWorkerWorldStatus(
                gameSession_.profile(), publishedContentRegistry(),
                facility.kind);
        const std::optional<BaseResidentWorldProjection> residents =
            projectBaseResidentWorldStatus(
                gameSession_.profile(), facility.kind);
        const std::optional<BaseResourceFlowWorldProjection> resourceFlow =
            projectBaseResourceFlowWorldStatus(
                gameSession_.profile(), publishedContentRegistry(),
                facility.kind);
        const BaseWorldInformationDetail informationDetail =
            projectBaseWorldInformationDetail(
                zoom,
                selectedBaseFixedFacility_ == facility.kind);
        const SDL_FRect bounds{
            facility.bounds.position.x,
            facility.bounds.position.y,
            facility.bounds.size.x,
            facility.bounds.size.y};
        if (baseConstructionPanelOpen_)
        {
            const SDL_FRect workZone{
                access.workZone.position.x,
                access.workZone.position.y,
                access.workZone.size.x,
                access.workZone.size.y};
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 84, 188, 142, 42);
            SDL_RenderFillRect(renderer_, &workZone);
            SDL_SetRenderDrawColor(renderer_, 112, 224, 174, 170);
            SDL_RenderRect(renderer_, &workZone);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        }
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
        case BaseFacilityKind::KitchenWater:
            SDL_SetRenderDrawColor(renderer_, 50, 82, 88, 255);
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
        if (baseConstructionPanelOpen_ &&
            selectedBaseFixedFacility_ == facility.kind)
        {
            SDL_SetRenderDrawColor(renderer_, 126, 244, 178, 255);
            const SDL_FRect halo{
                bounds.x - 5.0F, bounds.y - 5.0F,
                bounds.w + 10.0F, bounds.h + 10.0F};
            SDL_RenderRect(renderer_, &halo);
            const SDL_FRect outer{
                halo.x - 3.0F, halo.y - 3.0F,
                halo.w + 6.0F, halo.h + 6.0F};
            SDL_RenderRect(renderer_, &outer);
        }
        if (baseConstructionPanelOpen_ &&
            hasBaseOperationNotice(facility.kind))
        {
            const float pulse = 0.5F + 0.5F * std::sin(
                baseOperationNoticePulseSeconds_ * 5.0F);
            SDL_SetRenderDrawColor(
                renderer_, 246, 190, 72,
                static_cast<Uint8>(150.0F + 105.0F * pulse));
            const SDL_FRect noticeHalo{
                bounds.x - 10.0F, bounds.y - 10.0F,
                bounds.w + 20.0F, bounds.h + 20.0F};
            SDL_RenderRect(renderer_, &noticeHalo);
        }
        uiTextRenderer_.render(
            renderer_,
            bounds.x + 18.0F,
            bounds.y + bounds.h / 2.0F,
            baseFacilityName(facility.kind));
        const SDL_FRect entrance{
            access.entrancePoint.x - 20.0F,
            access.entrancePoint.y - 5.0F,
            40.0F,
            10.0F};
        const SDL_Color serviceColor =
            baseFacilityWorldServiceColor(service.status);
        SDL_SetRenderDrawColor(
            renderer_, serviceColor.r, serviceColor.g,
            serviceColor.b, serviceColor.a);
        SDL_RenderFillRect(renderer_, &entrance);
        if (informationDetail != BaseWorldInformationDetail::Marker)
        {
            const std::string serviceLabel = fmt::format(
                "ENTRY | {}", baseFacilityWorldServiceLabel(service));
            uiTextRenderer_.render(
                renderer_, entrance.x - 2.0F, entrance.y + 12.0F,
                serviceLabel.c_str());
        }
        if (workSocket.has_value())
        {
            const bool active = service.activeWorkSocket.has_value() &&
                *service.activeWorkSocket == workSocket->kind;
            const SDL_Color socketColor = active
                ? serviceColor
                : SDL_Color{128, 146, 138, 255};
            const SDL_FRect socketBounds{
                workSocket->bounds.position.x,
                workSocket->bounds.position.y,
                workSocket->bounds.size.x,
                workSocket->bounds.size.y};
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(
                renderer_, socketColor.r, socketColor.g,
                socketColor.b, active ? 92 : 38);
            SDL_RenderFillRect(renderer_, &socketBounds);
            SDL_SetRenderDrawColor(
                renderer_, socketColor.r, socketColor.g,
                socketColor.b, active ? 255 : 150);
            SDL_RenderRect(renderer_, &socketBounds);
            if (active)
            {
                const SDL_FRect halo{
                    socketBounds.x - 3.0F,
                    socketBounds.y - 3.0F,
                    socketBounds.w + 6.0F,
                    socketBounds.h + 6.0F};
                SDL_RenderRect(renderer_, &halo);
            }
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            if (informationDetail != BaseWorldInformationDetail::Marker)
            {
                uiTextRenderer_.render(
                    renderer_, socketBounds.x,
                    socketBounds.y + socketBounds.h + 3.0F,
                    baseFacilityWorkSocketName(workSocket->kind));
            }

            if (worker.has_value() &&
                worker->workSocket == workSocket->kind)
            {
                const SDL_Color workerColor =
                    baseFacilityWorkerWorldColor(worker->status);
                const float centerX = workSocket->interactionPoint.x;
                const float centerY = workSocket->interactionPoint.y;
                const SDL_FRect head{
                    centerX - 5.0F, centerY - 13.0F, 10.0F, 10.0F};
                const SDL_FRect body{
                    centerX - 7.0F, centerY - 1.0F, 14.0F, 16.0F};
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(
                    renderer_, workerColor.r, workerColor.g,
                    workerColor.b,
                    worker->status ==
                            BaseFacilityWorkerWorldStatus::Missing
                        ? 110
                        : 230);
                if (worker->status ==
                    BaseFacilityWorkerWorldStatus::Missing)
                {
                    SDL_RenderRect(renderer_, &head);
                    SDL_RenderRect(renderer_, &body);
                }
                else
                {
                    SDL_RenderFillRect(renderer_, &head);
                    SDL_RenderFillRect(renderer_, &body);
                }
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                if (informationDetail != BaseWorldInformationDetail::Marker)
                {
                    const std::string workerLabel =
                        baseFacilityWorkerWorldLabel(*worker);
                    uiTextRenderer_.render(
                        renderer_, socketBounds.x,
                        socketBounds.y - 18.0F,
                        workerLabel.c_str());
                }
            }
            if (residents.has_value() &&
                residents->workSocket == workSocket->kind)
            {
                const SDL_Color residentColor = baseResidentWorldColor(
                    residents->status);
                const float centerX = workSocket->interactionPoint.x;
                const float centerY = workSocket->interactionPoint.y;
                const std::uint32_t visibleFigures = std::min(
                    residents->residents, 3U);
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                for (std::uint32_t index{}; index < 3U; ++index)
                {
                    const float offset =
                        (static_cast<float>(index) - 1.0F) * 16.0F;
                    const SDL_FRect head{
                        centerX + offset - 4.0F,
                        centerY - 12.0F,
                        8.0F,
                        8.0F};
                    const SDL_FRect body{
                        centerX + offset - 6.0F,
                        centerY - 2.0F,
                        12.0F,
                        14.0F};
                    SDL_SetRenderDrawColor(
                        renderer_, residentColor.r, residentColor.g,
                        residentColor.b,
                        index < visibleFigures ? 225 : 72);
                    if (index < visibleFigures)
                    {
                        SDL_RenderFillRect(renderer_, &head);
                        SDL_RenderFillRect(renderer_, &body);
                    }
                    else
                    {
                        SDL_RenderRect(renderer_, &head);
                        SDL_RenderRect(renderer_, &body);
                    }
                }
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                if (informationDetail != BaseWorldInformationDetail::Marker)
                {
                    const std::string populationLabel =
                        baseResidentWorldPrimaryLabel(*residents);
                    uiTextRenderer_.render(
                        renderer_, socketBounds.x,
                        socketBounds.y - 18.0F,
                        populationLabel.c_str());
                    if (informationDetail ==
                        BaseWorldInformationDetail::Detail)
                    {
                        const std::string workforceLabel =
                            baseResidentWorldWorkforceLabel(*residents);
                        uiTextRenderer_.render(
                            renderer_, socketBounds.x,
                            socketBounds.y - 36.0F,
                            workforceLabel.c_str());
                    }
                }
            }
            if (resourceFlow.has_value() &&
                resourceFlow->workSocket == workSocket->kind)
            {
                const SDL_Color flowColor = baseResourceFlowWorldColor(
                    resourceFlow->status);
                const float centerX = workSocket->interactionPoint.x;
                const float centerY = workSocket->interactionPoint.y;
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
                for (int index{}; index < 3; ++index)
                {
                    const SDL_FRect package{
                        centerX - 23.0F + index * 16.0F,
                        centerY - 7.0F - (index % 2) * 6.0F,
                        14.0F,
                        14.0F};
                    SDL_SetRenderDrawColor(
                        renderer_, flowColor.r, flowColor.g,
                        flowColor.b,
                        resourceFlow->status ==
                                BaseResourceFlowWorldStatus::Empty
                            ? 72
                            : 218);
                    if (resourceFlow->status ==
                        BaseResourceFlowWorldStatus::Empty)
                    {
                        SDL_RenderRect(renderer_, &package);
                    }
                    else
                    {
                        SDL_RenderFillRect(renderer_, &package);
                    }
                }
                SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
                if (informationDetail != BaseWorldInformationDetail::Marker)
                {
                    const std::string primary =
                        baseResourceFlowWorldPrimaryLabel(*resourceFlow);
                    uiTextRenderer_.render(
                        renderer_, socketBounds.x,
                        socketBounds.y - 18.0F,
                        primary.c_str());
                    if (informationDetail ==
                        BaseWorldInformationDetail::Detail)
                    {
                        const std::string secondary =
                            baseResourceFlowWorldSecondaryLabel(*resourceFlow);
                        uiTextRenderer_.render(
                            renderer_, socketBounds.x,
                            socketBounds.y - 36.0F,
                            secondary.c_str());
                    }
                }
            }
        }
    }

    for (const BaseGroundAssetProjection &ground :
         gameFlow_.baseGroundAssets())
    {
        if (ground.position.x < camera.x - 96.0F ||
            ground.position.y < camera.y - 96.0F ||
            ground.position.x > camera.x + kWindowWidth / zoom + 96.0F ||
            ground.position.y > camera.y + kWindowHeight / zoom + 96.0F)
        {
            continue;
        }
        const AssetRecord *asset = gameSession_.profile().assets.find(
            ground.assetId);
        if (asset == nullptr)
            continue;
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        const InventoryFootprint footprint = inventoryFootprint(
            definition, asset->orientation);
        constexpr float cellSize{22.0F};
        const Vec2 drawSize = definition.basePlacement.has_value()
            ? orientedSize(definition.worldRenderSize, asset->orientation)
            : Vec2{
                  static_cast<float>(footprint.width) * cellSize,
                  static_cast<float>(footprint.height) * cellSize};
        const SDL_FRect bounds{
            ground.position.x - drawSize.x * 0.5F,
            ground.position.y - drawSize.y * 0.5F,
            drawSize.x,
            drawSize.y};
        SDL_SetRenderDrawColor(renderer_, 86, 166, 190, 80);
        const SDL_FRect halo{
            bounds.x - 5.0F, bounds.y - 5.0F,
            bounds.w + 10.0F, bounds.h + 10.0F};
        SDL_RenderFillRect(renderer_, &halo);
        if (baseConstructionPanelOpen_ &&
            selectedBasePlacedAssetId_ == ground.assetId)
        {
            SDL_SetRenderDrawColor(renderer_, 126, 244, 178, 255);
            SDL_RenderRect(renderer_, &halo);
            const SDL_FRect outer{
                halo.x - 3.0F, halo.y - 3.0F,
                halo.w + 6.0F, halo.h + 6.0F};
            SDL_RenderRect(renderer_, &outer);
        }
        renderProfileAsset(*asset, bounds, cellSize, 255, false, false);
    }

    renderBasePlacementPreview();
    renderBaseDefenseWorld();
    renderBaseFortificationPreview();

    for (const Enemy &enemy : world.baseDefenseActive() ? world.baseDefenseEnemies() : world.perimeterEnemies())
    {
        const Rect enemyBounds = enemy.bounds();
        if (enemyBounds.position.x < camera.x - 96.0F ||
            enemyBounds.position.y < camera.y - 96.0F ||
            enemyBounds.position.x > camera.x + kWindowWidth / zoom + 96.0F ||
            enemyBounds.position.y > camera.y + kWindowHeight / zoom + 96.0F)
            continue;
        SDL_FRect destination{
            enemyBounds.position.x +
                (enemyBounds.size.x - kEnemySpriteWidth) * 0.5F,
            enemyBounds.position.y +
                (enemyBounds.size.y - kEnemySpriteHeight) * 0.5F,
            kEnemySpriteWidth,
            kEnemySpriteHeight};
        std::size_t frame = enemy.currentAnimationFrameIndex();
        if (frame >= kEnemyMoveFrameCount)
            frame = 0U;
        const SDL_FRect source{
            static_cast<float>(frame) * kEnemyMoveSourceFrameWidth,
            enemy.facingDirection() == EnemyFacingDirection::Left
                ? kEnemyMoveLeftRowY : kEnemyMoveRightRowY,
            kEnemyMoveSourceFrameWidth,
            kEnemyMoveSourceFrameHeight};
        SDL_RenderTexture(
            renderer_, enemyMoveHorizontalTexture_.get(), &source,
            &destination);
        if (!enemy.isDead())
        {
            SDL_SetRenderDrawColor(
                renderer_,
                enemy.awarenessState() == EnemyAwarenessState::Alerted
                    ? 255U : 134U,
                enemy.awarenessState() == EnemyAwarenessState::Alerted
                    ? 76U : 146U,
                72U,
                235U);
            SDL_RenderRect(renderer_, &destination);
        }
    }

    const Vec2 playerPosition = world.playerPosition();
    const Vec2 playerSize = world.playerSize();
    renderPlayerAvatar(
        playerPosition,
        playerSize,
        world.playerFacingDirection(),
        world.playerIsMoving(),
        world.playerAnimationFrame());
    renderMeleeSwing(world.melee());
    renderShotPresentations();
    renderShotFeedbackPresentations();
    renderParticles();

    static_cast<void>(SDL_SetRenderViewport(renderer_, nullptr));
    static_cast<void>(SDL_SetRenderScale(renderer_, 1.0F, 1.0F));

    renderMeleeStatus();
    const std::size_t livingPerimeterEnemies =
        static_cast<std::size_t>(std::count_if(
            world.perimeterEnemies().begin(),
            world.perimeterEnemies().end(),
            [](const Enemy &enemy) { return !enemy.isDead(); }));
    const char *zoneName = "OUTER AREA";
    if (world.playerSafetyZone() == HomeRegionSafetyZone::SafeCore)
        zoneName = "SAFE CORE";
    else if (world.playerSafetyZone() ==
             HomeRegionSafetyZone::TransitionBuffer)
        zoneName = "TRANSITION BUFFER";
    const bool localOutingActive =
        gameSession_.profile().homePerimeter.activeOuting.has_value();
    const std::string perimeterStatus = fmt::format(
        "HOME REGION | {} | OUTER HOSTILES {}{}",
        zoneName,
        livingPerimeterEnemies,
        localOutingActive ? " | LOCAL OUTING ACTIVE" : "");
    uiTextRenderer_.render(
        renderer_, 16.0F, 612.0F, perimeterStatus.c_str());

    if (const auto ground = gameFlow_.nearestBaseGroundAsset())
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(ground->definitionId);
        const std::string prompt = definition.containerCompartments.empty()
            ? fmt::format(
                  "E - PICK UP {} x{}",
                  definition.displayName,
                  ground->quantity)
            : fmt::format(
                  "E - OPEN CONTAINER {}",
                  definition.displayName);
        uiTextRenderer_.render(renderer_, 450.0F, 650.0F, prompt.c_str());
    }
    else if (const auto facility = world.interactableFacility())
    {
        const BaseFacilityWorldServiceProjection service =
            projectBaseFacilityWorldService(
                gameSession_.profile(),
                publishedContentRegistry(),
                *facility);
        const std::string prompt = fmt::format(
            "E - {} | {}",
            baseFacilityName(*facility),
            baseFacilityWorldServiceLabel(service));
        uiTextRenderer_.render(renderer_, 520.0F, 650.0F, prompt.c_str());
    }
}

void App::startBasePlacement(
    AssetInstanceId assetId,
    BasePlacementState::Mode mode,
    bool returnToBuildPanel)
{
    const AssetRecord *asset = gameSession_.profile().assets.find(assetId);
    if (asset == nullptr)
    {
        uiMessage_ = "ASSET NO LONGER EXISTS";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    const ItemDefinition &definition =
        publishedContentRegistry().item(asset->definitionId);
    if (!definition.basePlacement.has_value())
    {
        uiMessage_ = "ASSET IS NOT A PLACEABLE BASE FACILITY";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    closeInventory();
    gameFlow_.closeBaseFacility();
    tacticalMapOpen_ = false;
    baseConstructionPanelOpen_ = false;
    baseFacilityContextMenu_.reset();
    selectedBasePlacedAssetId_.reset();
    selectedBaseFixedFacility_.reset();
    baseFixedFacilityPlacementState_.reset();
    basePlacementState_ = BasePlacementState{
        assetId,
        0U,
        asset->orientation,
        mode,
        returnToBuildPanel};
    activateBaseBuildCamera();
    uiMessage_ = mode == BasePlacementState::Mode::Reposition
        ? "MOVE BASE FACILITY | LMB PLACE | R ROTATE | ESC CANCEL"
        : "PLACE BASE FACILITY | LMB PLACE | R ROTATE | ESC CANCEL";
    input_.suppressPrimaryPointerUntilRelease();
}

std::size_t App::baseBuildCatalogEntryCount() const
{
    const ProfileState &profile = gameSession_.profile();
    if (baseConstructionPage_ == BaseConstructionPage::Purchase)
    {
        std::size_t count = publishedContentRegistry()
            .baseConstructionProjects().size();
        for (const ItemDefinition &definition :
             publishedContentRegistry().items())
        {
            if (definition.basePlacement.has_value() &&
                definition.marketBuyPrice > 0U)
            {
                ++count;
            }
        }
        return count;
    }

    std::size_t count{};
    constexpr std::array<BaseFacilityKind, 5U> coreFacilities{
        BaseFacilityKind::Storage,
        BaseFacilityKind::Medical,
        BaseFacilityKind::Dormitory,
        BaseFacilityKind::KitchenWater,
        BaseFacilityKind::Workshop};
    for (BaseFacilityKind facility : coreFacilities)
    {
        const auto definitionId = spatialFacilityDefinitionId(facility);
        if (definitionId.has_value() &&
            baseFacilityOwned(profile, *definitionId) &&
            !baseFacilityInstalled(profile, *definitionId))
        {
            ++count;
        }
    }
    const auto stashAssets = assetsInContainer(
        profile, ProfileContainerId::stash());
    for (const ItemDefinition &definition :
         publishedContentRegistry().items())
    {
        if (!definition.basePlacement.has_value())
            continue;
        if (std::any_of(
                stashAssets.begin(), stashAssets.end(),
                [&](const AssetRecord *asset)
                { return asset->definitionId == definition.definitionId; }))
        {
            ++count;
        }
    }
    return count;
}

void App::handleBaseConstructionPanelClick(MousePosition position)
{
    if (handleBaseFortificationClick(position)) return;
    if (handleBaseOperationsOverviewClick(position))
        return;
    if (handleBaseFacilityInspectorClick(position))
        return;
    if (contains(baseBuildPurchaseTab(), position))
    {
        baseConstructionPage_ = BaseConstructionPage::Purchase;
        baseConstructionCatalogPage_ = 0U;
        baseFacilityContextMenu_.reset();
        uiMessage_ = "BASE BUILD PURCHASE PAGE";
        return;
    }
    if (contains(baseBuildOwnedTab(), position))
    {
        baseConstructionPage_ = BaseConstructionPage::Owned;
        baseConstructionCatalogPage_ = 0U;
        baseFacilityContextMenu_.reset();
        uiMessage_ = "BASE BUILD OWNED PAGE";
        return;
    }
    if (contains(baseBuildZoomResetButton(), position))
    {
        baseConstructionZoomIndex_ = 2U;
        uiMessage_ = "BUILD VIEW ZOOM RESET";
        return;
    }
    const std::size_t entryCount = baseBuildCatalogEntryCount();
    const BaseBuildCatalogPage catalogPage = projectBaseBuildCatalogPage(
        entryCount,
        baseConstructionCatalogPage_,
        kBaseBuildCardsPerPage);
    baseConstructionCatalogPage_ = catalogPage.pageIndex;
    if (contains(baseBuildPreviousPageButton(), position))
    {
        if (baseConstructionCatalogPage_ > 0U)
            --baseConstructionCatalogPage_;
        uiMessage_ = "BASE BUILD PREVIOUS PAGE";
        return;
    }
    if (contains(baseBuildNextPageButton(), position))
    {
        if (baseConstructionCatalogPage_ + 1U < catalogPage.pageCount)
            ++baseConstructionCatalogPage_;
        uiMessage_ = "BASE BUILD NEXT PAGE";
        return;
    }

    if (contains(baseBuildBarBounds(), position))
    {
        const std::size_t firstEntry = catalogPage.firstEntry;
        std::size_t entryIndex{};
        const auto clickedCard = [&]()
        {
            const std::size_t current = entryIndex++;
            return current >= firstEntry &&
                current < firstEntry + kBaseBuildCardsPerPage &&
                contains(baseBuildCard(current - firstEntry), position);
        };
        if (baseConstructionPage_ == BaseConstructionPage::Purchase)
        {
            const auto projects = projectBaseConstructionCatalog(
                gameSession_.profile(), publishedContentRegistry());
            for (const BaseConstructionCatalogEntry &project : projects)
            {
                if (!clickedCard())
                    continue;
                if (!project.canCommit)
                {
                    uiMessage_ = project.action ==
                        BaseConstructionCatalogAction::Complete
                        ? "CONSTRUCTION PROJECT COMPLETE"
                        : project.message;
                    gameAudio_.play(SoundEventId::UiDeny);
                    return;
                }
                const bool cancelling = project.action ==
                    BaseConstructionCatalogAction::Cancel;
                const BaseConstructionReceipt receipt = cancelling
                    ? gameSession_.executeCancelBaseConstruction(
                        project.definitionId,
                        nextProfileTransactionId(
                            "base-build-cancel-project"))
                    : gameSession_.executeStartBaseConstruction(
                        project.definitionId,
                        nextProfileTransactionId(
                            "base-build-start-project"));
                uiMessage_ = receipt.succeeded
                    ? cancelling
                        ? "CONSTRUCTION PROJECT CANCELLED | MATERIAL REFUNDED"
                        : "CONSTRUCTION PROJECT STARTED"
                    : receipt.message;
                gameAudio_.play(receipt.succeeded
                    ? SoundEventId::UiConfirm
                    : SoundEventId::UiDeny);
                return;
            }
        }
        if (baseConstructionPage_ == BaseConstructionPage::Owned)
        {
            constexpr std::array<BaseFacilityKind, 5U> coreFacilities{
                BaseFacilityKind::Storage,
                BaseFacilityKind::Medical,
                BaseFacilityKind::Dormitory,
                BaseFacilityKind::KitchenWater,
                BaseFacilityKind::Workshop};
            for (BaseFacilityKind facility : coreFacilities)
            {
                const auto definitionId =
                    spatialFacilityDefinitionId(facility);
                if (!definitionId.has_value() ||
                    !baseFacilityOwned(
                        gameSession_.profile(), *definitionId) ||
                    baseFacilityInstalled(
                        gameSession_.profile(), *definitionId))
                {
                    continue;
                }
                if (clickedCard())
                {
                    startBaseFixedFacilityPlacement(
                        facility,
                        BaseFixedFacilityPlacementState::Mode::Install);
                    return;
                }
            }
        }
        const auto stashAssets = assetsInContainer(
            gameSession_.profile(), ProfileContainerId::stash());
        for (const ItemDefinition &definition :
             publishedContentRegistry().items())
        {
            if (!definition.basePlacement.has_value())
                continue;
            AssetInstanceId ownedAssetId{};
            std::size_t ownedCount{};
            for (const AssetRecord *asset : stashAssets)
            {
                if (asset->definitionId == definition.definitionId)
                {
                    if (ownedCount == 0U)
                        ownedAssetId = asset->instanceId;
                    ++ownedCount;
                }
            }
            const bool visible = baseConstructionPage_ ==
                BaseConstructionPage::Purchase
                ? definition.marketBuyPrice > 0U
                : ownedCount > 0U;
            if (!visible)
                continue;
            if (clickedCard())
            {
                if (baseConstructionPage_ ==
                    BaseConstructionPage::Purchase)
                {
                    const auto receipt =
                        gameSession_.purchaseBasePlaceable(
                            definition.definitionId,
                            nextProfileTransactionId(
                                "base-build-purchase"));
                    if (!receipt.succeeded)
                    {
                        uiMessage_ = receipt.message;
                        gameAudio_.play(SoundEventId::UiDeny);
                        return;
                    }
                    gameAudio_.play(SoundEventId::UiConfirm);
                    startBasePlacement(
                        receipt.assetId,
                        BasePlacementState::Mode::PlaceOwned,
                        true);
                    return;
                }
                startBasePlacement(
                    ownedAssetId,
                    BasePlacementState::Mode::PlaceOwned,
                    true);
                return;
            }
        }
        return;
    }

    baseFacilityContextMenu_.reset();
    const std::optional<BaseFacilityKind> residentFacility =
        baseResidentFacilityAt(position);
    const std::optional<BaseFacilityKind> workerFacility =
        residentFacility.has_value()
        ? std::optional<BaseFacilityKind>{}
        : baseWorkerFacilityAt(position);
    const std::optional<BaseFacilityKind> resourceFacility =
        residentFacility.has_value() || workerFacility.has_value()
        ? std::optional<BaseFacilityKind>{}
        : baseResourceFlowFacilityAt(position);
    const std::optional<BaseFacilityKind> worldActionFacility =
        residentFacility.has_value()
        ? residentFacility
        : workerFacility.has_value() ? workerFacility : resourceFacility;
    selectedBasePlacedAssetId_ = worldActionFacility.has_value()
        ? std::optional<AssetInstanceId>{}
        : basePlacedFacilityAt(position);
    selectedBaseFixedFacility_ = worldActionFacility.has_value()
        ? worldActionFacility
        : selectedBasePlacedAssetId_.has_value()
            ? std::optional<BaseFacilityKind>{}
            : baseFixedFacilityAt(position);
    uiMessage_ = selectedBasePlacedAssetId_.has_value() ||
            selectedBaseFixedFacility_.has_value()
        ? residentFacility.has_value()
            ? "RESIDENT AREA SELECTED | LMB PANEL / RMB QUICK ACTIONS"
            : workerFacility.has_value()
                ? "WORKFORCE STATION SELECTED | LMB PANEL / RMB QUICK ACTIONS"
                : resourceFacility.has_value()
                    ? "RESOURCE FLOW AREA SELECTED | LMB PANEL / RMB OPEN"
                : "BASE FACILITY SELECTED | RMB FOR ACTIONS"
        : "BASE FACILITY SELECTION CLEARED";
}

bool App::handleBaseOperationsOverviewClick(MousePosition position)
{
    if (!contains(baseOperationsOverviewBounds(), position))
        return false;

    constexpr std::array<BaseOperationsOverviewFilter, 4U> filters{
        BaseOperationsOverviewFilter::All,
        BaseOperationsOverviewFilter::Attention,
        BaseOperationsOverviewFilter::InProgress,
        BaseOperationsOverviewFilter::Ready};
    for (std::size_t index{}; index < filters.size(); ++index)
    {
        if (!contains(baseOperationsOverviewFilterButton(index), position))
            continue;
        baseOperationsOverviewFilter_ = filters[index];
        baseOperationsOverviewPage_ = 0U;
        baseFacilityContextMenu_.reset();
        uiMessage_ = "BASE OPERATIONS FILTER CHANGED";
        return true;
    }

    const BaseOperationsOverviewProjection projection =
        projectBaseOperationsOverview(
            gameSession_.profile(), publishedContentRegistry());
    BaseOperationsOverviewPage page = projectBaseOperationsOverviewPage(
        projection,
        baseOperationsOverviewFilter_,
        baseOperationsOverviewPage_);
    baseOperationsOverviewPage_ = page.pageIndex;
    if (contains(baseOperationsOverviewPreviousPageButton(), position))
    {
        if (baseOperationsOverviewPage_ > 0U)
            --baseOperationsOverviewPage_;
        uiMessage_ = "BASE OPERATIONS PREVIOUS PAGE";
        return true;
    }
    if (contains(baseOperationsOverviewNextPageButton(), position))
    {
        if (baseOperationsOverviewPage_ + 1U < page.pageCount)
            ++baseOperationsOverviewPage_;
        uiMessage_ = "BASE OPERATIONS NEXT PAGE";
        return true;
    }
    for (std::size_t index{}; index < page.visibleEntryCount; ++index)
    {
        if (!contains(baseOperationsOverviewRow(index), position))
            continue;
        const BaseFacilityKind facility =
            projection.entries[page.entryIndices[index]].facility;
        selectedBasePlacedAssetId_.reset();
        selectedBaseFixedFacility_ = facility;
        baseFacilityContextMenu_.reset();
        focusBaseFixedFacility(facility);
        uiMessage_ = "BASE OPERATION LOCATED";
        gameAudio_.play(SoundEventId::UiConfirm);
        return true;
    }
    return true;
}

bool App::handleBaseFacilityInspectorClick(MousePosition position)
{
    if (!contains(baseFacilityInspectorBounds(), position) ||
        (!selectedBasePlacedAssetId_.has_value() &&
         !selectedBaseFixedFacility_.has_value()))
    {
        return false;
    }

    if (selectedBaseFixedFacility_.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(),
                publishedContentRegistry(),
                *selectedBaseFixedFacility_);
        for (std::size_t index{};
             index < projection.quickActions.size(); ++index)
        {
            if (!contains(baseFacilityQuickActionButton(index), position))
                continue;
            const BaseFacilityQuickActionProjection &action =
                projection.quickActions[index];
            if (!action.canCommit)
            {
                uiMessage_ = action.message;
                gameAudio_.play(SoundEventId::UiDeny);
                return true;
            }

            bool succeeded{};
            std::string message;
            switch (action.kind)
            {
            case BaseFacilityQuickActionKind::OpenFunction:
                openBaseFixedFacility(projection.kind);
                return true;
            case BaseFacilityQuickActionKind::AssignBestWorker:
            case BaseFacilityQuickActionKind::ClearWorker:
            {
                const BaseFacilityStaffingKind facility =
                    projection.kind == BaseFacilityKind::Workshop
                    ? BaseFacilityStaffingKind::Workshop
                    : BaseFacilityStaffingKind::Medical;
                const BaseWorkforceReceipt receipt =
                    action.kind ==
                        BaseFacilityQuickActionKind::AssignBestWorker
                    ? gameSession_.executeAssignBestBaseWorker(
                        facility,
                        nextProfileTransactionId(
                            "base-inspector-assign-worker"))
                    : gameSession_.executeClearBaseWorker(
                        facility,
                        nextProfileTransactionId(
                            "base-inspector-clear-worker"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? action.kind ==
                            BaseFacilityQuickActionKind::AssignBestWorker
                        ? "BASE FACILITY WORKER ASSIGNED"
                        : "BASE FACILITY WORKER CLEARED"
                    : receipt.message;
                break;
            }
            case BaseFacilityQuickActionKind::StartUpgrade:
            case BaseFacilityQuickActionKind::CancelUpgrade:
            {
                if (!action.constructionProjectId.has_value())
                {
                    message = "BASE FACILITY UPGRADE IS NOT AVAILABLE";
                    break;
                }
                const BaseConstructionReceipt receipt =
                    action.kind == BaseFacilityQuickActionKind::StartUpgrade
                    ? gameSession_.executeStartBaseConstruction(
                        *action.constructionProjectId,
                        nextProfileTransactionId(
                            "base-inspector-start-upgrade"))
                    : gameSession_.executeCancelBaseConstruction(
                        *action.constructionProjectId,
                        nextProfileTransactionId(
                            "base-inspector-cancel-upgrade"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? action.kind == BaseFacilityQuickActionKind::StartUpgrade
                        ? "BASE FACILITY UPGRADE STARTED"
                        : "BASE FACILITY UPGRADE CANCELLED"
                    : receipt.message;
                break;
            }
            case BaseFacilityQuickActionKind::CollectManufacturing:
            {
                const BaseManufacturingReceipt receipt =
                    gameSession_.executeCollectBaseManufacturing(
                        nextProfileTransactionId(
                            "base-inspector-collect-output"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? "MANUFACTURED ITEM COLLECTED"
                    : receipt.message;
                break;
            }
            case BaseFacilityQuickActionKind::CancelManufacturing:
            {
                const BaseManufacturingReceipt receipt =
                    gameSession_.executeCancelBaseManufacturing(
                        nextProfileTransactionId(
                            "base-inspector-cancel-production"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? "MANUFACTURING CANCELLED | INPUTS RETURNED"
                    : receipt.message;
                break;
            }
            case BaseFacilityQuickActionKind::StartResidentTreatment:
            {
                const ResidentTreatmentReceipt receipt =
                    gameSession_.executeStartResidentTreatment(
                        nextProfileTransactionId(
                            "base-inspector-resident-treatment"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? "RESIDENT TREATMENT STARTED"
                    : receipt.message;
                break;
            }
            case BaseFacilityQuickActionKind::AutoFillWorkers:
            {
                const BaseWorkforceReceipt receipt =
                    gameSession_.executeAutoFillBaseWorkers(
                        nextProfileTransactionId(
                            "base-inspector-auto-fill-workers"));
                succeeded = receipt.succeeded;
                message = receipt.succeeded
                    ? "BASE FACILITY WORKERS AUTO-FILLED"
                    : receipt.message;
                break;
            }
            }
            uiMessage_ = std::move(message);
            gameAudio_.play(succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            return true;
        }
        if (movableCoreFacility(projection.kind) &&
            contains(
                baseFacilityQuickActionButton(
                    projection.quickActions.size()),
                position))
        {
            startBaseFixedFacilityPlacement(
                projection.kind,
                projection.status == BaseFacilityOperationalStatus::Reserve
                    ? BaseFixedFacilityPlacementState::Mode::Install
                    : BaseFixedFacilityPlacementState::Mode::Move);
            return true;
        }
        return true;
    }

    const AssetInstanceId assetId = *selectedBasePlacedAssetId_;
    if (contains(basePlacedFacilityInspectorButton(0U, 3U), position))
    {
        openBasePlacedFacility(assetId);
        return true;
    }
    if (contains(basePlacedFacilityInspectorButton(1U, 3U), position))
    {
        startBasePlacement(
            assetId,
            BasePlacementState::Mode::Reposition,
            true);
        return true;
    }
    if (contains(basePlacedFacilityInspectorButton(2U, 3U), position))
    {
        const BaseGroundReceipt receipt =
            gameFlow_.pickupBaseGroundAssetForManagement(
                assetId,
                nextProfileTransactionId("base-build-inspector-pickup"));
        uiMessage_ = receipt.succeeded
            ? "BASE FACILITY RETURNED TO STASH"
            : receipt.message;
        gameAudio_.play(receipt.succeeded
            ? SoundEventId::InventoryPickup
            : SoundEventId::UiDeny);
        if (receipt.succeeded)
            selectedBasePlacedAssetId_.reset();
        return true;
    }
    return true;
}

void App::handleBaseConstructionRightClick(MousePosition position)
{
    if (handleBaseFortificationRightClick(position)) return;
    if (contains(baseBuildBarBounds(), position) ||
        contains(baseFacilityInspectorBounds(), position) ||
        contains(baseOperationsOverviewBounds(), position))
    {
        baseFacilityContextMenu_.reset();
        return;
    }
    const auto residentFacility = baseResidentFacilityAt(position);
    const auto workerFacility = residentFacility.has_value()
        ? std::optional<BaseFacilityKind>{}
        : baseWorkerFacilityAt(position);
    const auto resourceFacility =
        residentFacility.has_value() || workerFacility.has_value()
        ? std::optional<BaseFacilityKind>{}
        : baseResourceFlowFacilityAt(position);
    const auto worldActionFacility = residentFacility.has_value()
        ? residentFacility
        : workerFacility.has_value() ? workerFacility : resourceFacility;
    const auto assetId = worldActionFacility.has_value()
        ? std::optional<AssetInstanceId>{}
        : basePlacedFacilityAt(position);
    const auto fixedFacility = worldActionFacility.has_value()
        ? worldActionFacility
        : assetId.has_value()
            ? std::optional<BaseFacilityKind>{}
            : baseFixedFacilityAt(position);
    if (!assetId.has_value() && !fixedFacility.has_value())
    {
        selectedBasePlacedAssetId_.reset();
        selectedBaseFixedFacility_.reset();
        baseFacilityContextMenu_.reset();
        return;
    }
    selectedBasePlacedAssetId_ = assetId;
    selectedBaseFixedFacility_ = fixedFacility;
    baseFacilityContextMenu_ = BaseFacilityContextMenu{
        assetId,
        fixedFacility,
        position,
        residentFacility.has_value()
            ? BaseFacilityContextMenuMode::ResidentArea
            : workerFacility.has_value()
                ? BaseFacilityContextMenuMode::WorkforceStation
                : resourceFacility.has_value()
                    ? BaseFacilityContextMenuMode::ResourceFlowArea
                : BaseFacilityContextMenuMode::Facility};
    uiMessage_ = residentFacility.has_value()
        ? "RESIDENT AREA ACTIONS"
        : workerFacility.has_value()
            ? "WORKFORCE ACTIONS"
            : resourceFacility.has_value()
                ? "RESOURCE FLOW ACTIONS"
            : "BASE FACILITY ACTIONS";
}

void App::handleBaseFacilityContextMenuClick(MousePosition position)
{
    if (!baseFacilityContextMenu_.has_value())
        return;
    const BaseFacilityContextMenu menu = *baseFacilityContextMenu_;
    if (menu.mode == BaseFacilityContextMenuMode::ResourceFlowArea &&
        menu.fixedFacility.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                *menu.fixedFacility);
        const auto action = std::find_if(
            projection.quickActions.begin(), projection.quickActions.end(),
            [](const BaseFacilityQuickActionProjection &candidate)
            {
                return candidate.kind ==
                    BaseFacilityQuickActionKind::OpenFunction;
            });
        if (action != projection.quickActions.end() &&
            contains(
                baseFacilityContextMenuRow(menu.position, 0U, 1U),
                position))
        {
            if (!action->canCommit)
            {
                uiMessage_ = action->message;
                gameAudio_.play(SoundEventId::UiDeny);
                baseFacilityContextMenu_.reset();
                return;
            }
            openBaseFixedFacility(*menu.fixedFacility);
            return;
        }
        baseFacilityContextMenu_.reset();
        return;
    }
    if (menu.mode == BaseFacilityContextMenuMode::WorkforceStation &&
        menu.fixedFacility.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                *menu.fixedFacility);
        std::vector<const BaseFacilityQuickActionProjection *> actions;
        for (const BaseFacilityQuickActionProjection &action :
             projection.quickActions)
        {
            if (action.kind ==
                    BaseFacilityQuickActionKind::AssignBestWorker ||
                action.kind == BaseFacilityQuickActionKind::ClearWorker ||
                action.kind == BaseFacilityQuickActionKind::AutoFillWorkers)
            {
                actions.push_back(&action);
            }
        }
        for (std::size_t index{}; index < actions.size(); ++index)
        {
            if (!contains(
                    baseFacilityContextMenuRow(
                        menu.position, index, actions.size()),
                    position))
            {
                continue;
            }
            const BaseFacilityQuickActionProjection &action =
                *actions[index];
            if (!action.canCommit)
            {
                uiMessage_ = action.message;
                gameAudio_.play(SoundEventId::UiDeny);
                baseFacilityContextMenu_.reset();
                return;
            }

            BaseWorkforceReceipt receipt;
            if (action.kind == BaseFacilityQuickActionKind::AutoFillWorkers)
            {
                receipt = gameSession_.executeAutoFillBaseWorkers(
                    nextProfileTransactionId(
                        "base-workforce-station-auto-fill"));
            }
            else
            {
                const BaseFacilityStaffingKind facility =
                    *menu.fixedFacility == BaseFacilityKind::Workshop
                    ? BaseFacilityStaffingKind::Workshop
                    : BaseFacilityStaffingKind::Medical;
                receipt = action.kind ==
                        BaseFacilityQuickActionKind::AssignBestWorker
                    ? gameSession_.executeAssignBestBaseWorker(
                          facility,
                          nextProfileTransactionId(
                              "base-workforce-station-assign"))
                    : gameSession_.executeClearBaseWorker(
                          facility,
                          nextProfileTransactionId(
                              "base-workforce-station-clear"));
            }
            uiMessage_ = receipt.succeeded
                ? action.kind == BaseFacilityQuickActionKind::AutoFillWorkers
                    ? "BASE FACILITY WORKERS AUTO-FILLED"
                    : action.kind ==
                            BaseFacilityQuickActionKind::AssignBestWorker
                        ? "BASE FACILITY WORKER ASSIGNED"
                        : "BASE FACILITY WORKER CLEARED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            baseFacilityContextMenu_.reset();
            return;
        }
        baseFacilityContextMenu_.reset();
        return;
    }
    if (menu.mode == BaseFacilityContextMenuMode::ResidentArea &&
        menu.fixedFacility == BaseFacilityKind::Dormitory)
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                BaseFacilityKind::Dormitory);
        std::vector<const BaseFacilityQuickActionProjection *> actions;
        for (const BaseFacilityQuickActionProjection &action :
             projection.quickActions)
        {
            if (action.kind == BaseFacilityQuickActionKind::OpenFunction ||
                action.kind == BaseFacilityQuickActionKind::AutoFillWorkers)
            {
                actions.push_back(&action);
            }
        }
        for (std::size_t index{}; index < actions.size(); ++index)
        {
            if (!contains(
                    baseFacilityContextMenuRow(
                        menu.position, index, actions.size()),
                    position))
            {
                continue;
            }
            const BaseFacilityQuickActionProjection &action =
                *actions[index];
            if (!action.canCommit)
            {
                uiMessage_ = action.message;
                gameAudio_.play(SoundEventId::UiDeny);
                baseFacilityContextMenu_.reset();
                return;
            }
            if (action.kind == BaseFacilityQuickActionKind::OpenFunction)
            {
                openBaseFixedFacility(BaseFacilityKind::Dormitory);
                return;
            }
            const BaseWorkforceReceipt receipt =
                gameSession_.executeAutoFillBaseWorkers(
                    nextProfileTransactionId(
                        "base-resident-area-auto-fill"));
            uiMessage_ = receipt.succeeded
                ? "BASE FACILITY WORKERS AUTO-FILLED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded
                ? SoundEventId::UiConfirm
                : SoundEventId::UiDeny);
            baseFacilityContextMenu_.reset();
            return;
        }
        baseFacilityContextMenu_.reset();
        return;
    }
    const std::size_t rowCount = menu.assetId.has_value()
        ? 3U
        : menu.fixedFacility.has_value() &&
                movableCoreFacility(*menu.fixedFacility)
            ? 2U
            : 1U;
    if (contains(
            baseFacilityContextMenuRow(menu.position, 0U, rowCount),
            position))
    {
        if (menu.assetId.has_value())
            openBasePlacedFacility(*menu.assetId);
        else if (menu.fixedFacility.has_value())
            openBaseFixedFacility(*menu.fixedFacility);
        return;
    }
    if (!menu.assetId.has_value())
    {
        if (menu.fixedFacility.has_value() && rowCount > 1U &&
            contains(
                baseFacilityContextMenuRow(menu.position, 1U, rowCount),
                position))
        {
            startBaseFixedFacilityPlacement(*menu.fixedFacility);
        }
        baseFacilityContextMenu_.reset();
        return;
    }
    if (contains(
            baseFacilityContextMenuRow(menu.position, 1U, rowCount),
            position))
    {
        startBasePlacement(
            *menu.assetId,
            BasePlacementState::Mode::Reposition,
            true);
        baseFacilityContextMenu_.reset();
        return;
    }
    if (contains(
            baseFacilityContextMenuRow(menu.position, 2U, rowCount),
            position))
    {
        const BaseGroundReceipt receipt =
            gameFlow_.pickupBaseGroundAssetForManagement(
                *menu.assetId,
                nextProfileTransactionId("base-build-pickup"));
        uiMessage_ = receipt.succeeded
            ? "BASE FACILITY RETURNED TO STASH"
            : receipt.message;
        gameAudio_.play(receipt.succeeded
            ? SoundEventId::InventoryPickup
            : SoundEventId::UiDeny);
        if (receipt.succeeded)
        {
            selectedBasePlacedAssetId_.reset();
            selectedBaseFixedFacility_.reset();
        }
        baseFacilityContextMenu_.reset();
        return;
    }
    baseFacilityContextMenu_.reset();
}

void App::adjustBaseConstructionZoom(int direction)
{
    if (direction > 0 && baseConstructionZoomIndex_ < 4U)
        ++baseConstructionZoomIndex_;
    else if (direction < 0 && baseConstructionZoomIndex_ > 0U)
        --baseConstructionZoomIndex_;
    if (baseBuildCamera_.active())
    {
        baseBuildCamera_.constrain(
            gameFlow_.baseWorld().worldSize(),
            baseBuildViewportWorldSize());
    }
    uiMessage_ = "BUILD VIEW ZOOM UPDATED";
}

std::optional<AssetInstanceId>
App::basePlacedFacilityAt(MousePosition position) const
{
    const Vec2 worldPosition = baseScreenToWorld({position.x, position.y});
    const std::vector<BaseGroundAssetProjection> placed =
        gameFlow_.baseGroundAssets();
    for (auto it = placed.rbegin(); it != placed.rend(); ++it)
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(it->definitionId);
        if (!definition.basePlacement.has_value())
            continue;
        const Vec2 size = orientedSize(
            definition.basePlacement->footprint, it->orientation);
        const ContentRect bounds{
            {it->position.x - size.x * 0.5F,
             it->position.y - size.y * 0.5F},
            size};
        if (worldPosition.x >= bounds.position.x &&
            worldPosition.y >= bounds.position.y &&
            worldPosition.x <= bounds.position.x + bounds.size.x &&
            worldPosition.y <= bounds.position.y + bounds.size.y)
        {
            return it->assetId;
        }
    }
    return std::nullopt;
}

std::optional<BaseFacilityKind>
App::baseFixedFacilityAt(MousePosition position) const
{
    const Vec2 worldPosition = baseScreenToWorld({position.x, position.y});
    const auto &facilities = gameFlow_.baseWorld().facilities();
    for (auto it = facilities.rbegin(); it != facilities.rend(); ++it)
    {
        if (!it->active)
            continue;
        if (worldPosition.x >= it->bounds.position.x &&
            worldPosition.y >= it->bounds.position.y &&
            worldPosition.x <= it->bounds.position.x + it->bounds.size.x &&
            worldPosition.y <= it->bounds.position.y + it->bounds.size.y)
        {
            return it->kind;
        }
    }
    return std::nullopt;
}

std::optional<BaseFacilityKind>
App::baseWorkerFacilityAt(MousePosition position) const
{
    const Vec2 worldPosition = baseScreenToWorld({position.x, position.y});
    const auto &facilities = gameFlow_.baseWorld().facilities();
    for (auto it = facilities.rbegin(); it != facilities.rend(); ++it)
    {
        if (!it->active)
            continue;
        const auto worker = projectBaseFacilityWorkerWorldStatus(
            gameSession_.profile(), publishedContentRegistry(), it->kind);
        const auto socket = baseFacilityWorkSocket(*it);
        if (!worker.has_value() || !socket.has_value() ||
            worker->workSocket != socket->kind)
        {
            continue;
        }
        const ContentRect &bounds = socket->bounds;
        if (worldPosition.x >= bounds.position.x &&
            worldPosition.y >= bounds.position.y &&
            worldPosition.x <= bounds.position.x + bounds.size.x &&
            worldPosition.y <= bounds.position.y + bounds.size.y)
        {
            return it->kind;
        }
    }
    return std::nullopt;
}

std::optional<BaseFacilityKind>
App::baseResidentFacilityAt(MousePosition position) const
{
    const Vec2 worldPosition = baseScreenToWorld({position.x, position.y});
    const auto &facilities = gameFlow_.baseWorld().facilities();
    for (auto it = facilities.rbegin(); it != facilities.rend(); ++it)
    {
        if (!it->active)
            continue;
        const auto residents = projectBaseResidentWorldStatus(
            gameSession_.profile(), it->kind);
        const auto socket = baseFacilityWorkSocket(*it);
        if (!residents.has_value() || !socket.has_value() ||
            residents->workSocket != socket->kind)
        {
            continue;
        }
        const ContentRect &bounds = socket->bounds;
        if (worldPosition.x >= bounds.position.x &&
            worldPosition.y >= bounds.position.y &&
            worldPosition.x <= bounds.position.x + bounds.size.x &&
            worldPosition.y <= bounds.position.y + bounds.size.y)
        {
            return it->kind;
        }
    }
    return std::nullopt;
}

std::optional<BaseFacilityKind>
App::baseResourceFlowFacilityAt(MousePosition position) const
{
    const Vec2 worldPosition = baseScreenToWorld({position.x, position.y});
    const auto &facilities = gameFlow_.baseWorld().facilities();
    for (auto it = facilities.rbegin(); it != facilities.rend(); ++it)
    {
        if (!it->active)
            continue;
        const auto resourceFlow = projectBaseResourceFlowWorldStatus(
            gameSession_.profile(), publishedContentRegistry(), it->kind);
        const auto socket = baseFacilityWorkSocket(*it);
        if (!resourceFlow.has_value() || !socket.has_value() ||
            resourceFlow->workSocket != socket->kind)
        {
            continue;
        }
        const ContentRect &bounds = socket->bounds;
        if (worldPosition.x >= bounds.position.x &&
            worldPosition.y >= bounds.position.y &&
            worldPosition.x <= bounds.position.x + bounds.size.x &&
            worldPosition.y <= bounds.position.y + bounds.size.y)
        {
            return it->kind;
        }
    }
    return std::nullopt;
}

void App::openBasePlacedFacility(AssetInstanceId assetId)
{
    const AssetRecord *asset = gameSession_.profile().assets.find(assetId);
    if (asset == nullptr)
    {
        uiMessage_ = "ASSET NO LONGER EXISTS";
        baseFacilityContextMenu_.reset();
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    const ItemDefinition &definition =
        publishedContentRegistry().item(asset->definitionId);
    if (definition.containerCompartments.empty())
    {
        uiMessage_ = "FACILITY HAS NO FUNCTION PAGE";
        baseFacilityContextMenu_.reset();
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    const BaseGroundPlan plan =
        gameFlow_.queryBaseGroundContainerAccess(assetId);
    if (!plan.canCommit)
    {
        uiMessage_ = plan.message;
        baseFacilityContextMenu_.reset();
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    openedBaseGroundContainerId_ = assetId;
    inventoryOverlayState_.openContainerInventory();
    baseConstructionPanelOpen_ = false;
    deactivateBaseBuildCamera();
    baseFacilityContextMenu_.reset();
    selectedBasePlacedAssetId_.reset();
    selectedBaseFixedFacility_.reset();
    uiMessage_ = "BASE GROUND CONTAINER OPENED";
    gameAudio_.play(SoundEventId::UiConfirm);
    input_.suppressPrimaryPointerUntilRelease();
}

void App::startBaseFixedFacilityPlacement(
    BaseFacilityKind facility,
    BaseFixedFacilityPlacementState::Mode mode)
{
    if (!movableCoreFacility(facility))
    {
        uiMessage_ = "BASE FACILITY CANNOT BE MOVED";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    const BaseFacilityManagementProjection projection =
        projectBaseFacilityManagement(
            gameSession_.profile(),
            publishedContentRegistry(),
            facility);
    const bool validStatus = mode ==
        BaseFixedFacilityPlacementState::Mode::Install
        ? projection.status == BaseFacilityOperationalStatus::Reserve
        : projection.status == BaseFacilityOperationalStatus::Operational;
    if (!validStatus)
    {
        uiMessage_ = "BASE FACILITY IS NOT AVAILABLE";
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    closeInventory();
    gameFlow_.closeBaseFacility();
    tacticalMapOpen_ = false;
    baseConstructionPanelOpen_ = false;
    baseFacilityContextMenu_.reset();
    selectedBasePlacedAssetId_.reset();
    selectedBaseFixedFacility_ = facility;
    basePlacementState_.reset();
    baseFixedFacilityPlacementState_ =
        BaseFixedFacilityPlacementState{facility, mode, true};
    if (!baseBuildCamera_.active())
        activateBaseBuildCamera();
    uiMessage_ = mode == BaseFixedFacilityPlacementState::Mode::Install
        ? "INSTALL BASE FACILITY | LMB PLACE | ESC CANCEL"
        : "MOVE BASE FACILITY | LMB PLACE | ESC CANCEL";
    input_.suppressPrimaryPointerUntilRelease();
}

void App::openBaseFixedFacility(BaseFacilityKind facility)
{
    if (!gameFlow_.openBaseFacilityForManagement(facility))
    {
        uiMessage_ = "BASE FACILITY IS NOT AVAILABLE";
        baseFacilityContextMenu_.reset();
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }

    baseConstructionPanelOpen_ = false;
    deactivateBaseBuildCamera();
    baseFacilityContextMenu_.reset();
    selectedBasePlacedAssetId_.reset();
    selectedBaseFixedFacility_.reset();
    if (facility == BaseFacilityKind::Storage)
    {
        gameFlow_.closeBaseFacility();
        openedBaseGroundContainerId_.reset();
        inventoryOverlayState_.openContainerInventory();
    }
    uiMessage_ = "BASE FACILITY FUNCTION OPENED";
    gameAudio_.play(SoundEventId::UiConfirm);
    input_.suppressPrimaryPointerUntilRelease();
}

void App::renderBasePlacementPreview()
{
    if (!pointerWorldPosition_.has_value())
    {
        return;
    }
    if (baseFixedFacilityPlacementState_.has_value())
    {
        const BaseFacilityKind facilityKind =
            baseFixedFacilityPlacementState_->facility;
        const auto facility = std::find_if(
            gameFlow_.baseWorld().facilities().begin(),
            gameFlow_.baseWorld().facilities().end(),
            [facilityKind](const BaseFacility &candidate)
            { return candidate.kind == facilityKind; });
        if (facility == gameFlow_.baseWorld().facilities().end())
            return;
        const Vec2 worldPosition = baseScreenToWorld(*pointerWorldPosition_);
        const bool installing =
            baseFixedFacilityPlacementState_->mode ==
            BaseFixedFacilityPlacementState::Mode::Install;
        const BaseFacilityLayoutPlan plan = installing
            ? gameFlow_.queryInstallBaseFacilityAt(
                  facilityKind, worldPosition)
            : gameFlow_.queryBaseFacilityRepositionAt(
                  facilityKind, worldPosition);
        const SDL_FRect bounds{
            worldPosition.x - facility->bounds.size.x * 0.5F,
            worldPosition.y - facility->bounds.size.y * 0.5F,
            facility->bounds.size.x,
            facility->bounds.size.y};
        const BaseFacilityAccessGeometry access =
            projectBaseFacilityAccessGeometry(
                worldPosition, facility->bounds.size);
        const SDL_FRect workZone{
            access.workZone.position.x,
            access.workZone.position.y,
            access.workZone.size.x,
            access.workZone.size.y};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(
            renderer_, plan.canCommit ? 72 : 220,
            plan.canCommit ? 206 : 72,
            plan.canCommit ? 132 : 66, 45);
        SDL_RenderFillRect(renderer_, &workZone);
        SDL_SetRenderDrawColor(
            renderer_, plan.canCommit ? 112 : 255,
            plan.canCommit ? 244 : 104,
            plan.canCommit ? 170 : 94, 190);
        SDL_RenderRect(renderer_, &workZone);
        SDL_SetRenderDrawColor(
            renderer_, plan.canCommit ? 72 : 220,
            plan.canCommit ? 206 : 72,
            plan.canCommit ? 132 : 66, 105);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(
            renderer_, plan.canCommit ? 112 : 255,
            plan.canCommit ? 244 : 104,
            plan.canCommit ? 170 : 94, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_, bounds.x + 8.0F, bounds.y + 8.0F,
            plan.canCommit
                ? installing ? "INSTALL FACILITY" : "MOVE FACILITY"
                : "BLOCKED");
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    if (!basePlacementState_.has_value())
        return;
    const AssetRecord *asset = gameSession_.profile().assets.find(
        basePlacementState_->assetId);
    if (asset == nullptr)
        return;
    const ItemDefinition &definition = publishedContentRegistry().item(
        asset->definitionId);
    if (!definition.basePlacement.has_value())
        return;
    const Vec2 worldPosition = baseScreenToWorld(*pointerWorldPosition_);
    const Vec2 size = orientedSize(
        definition.basePlacement->footprint,
        basePlacementState_->orientation);
    const BaseGroundPlan plan = basePlacementState_->mode ==
        BasePlacementState::Mode::Reposition
        ? gameFlow_.queryBaseGroundRepositionAt(
              basePlacementState_->assetId,
              basePlacementState_->orientation,
              worldPosition)
        : gameFlow_.queryBaseGroundDropAt(
              basePlacementState_->assetId,
              basePlacementState_->quantity,
              basePlacementState_->orientation,
              worldPosition);
    const SDL_FRect bounds{
        worldPosition.x - size.x * 0.5F,
        worldPosition.y - size.y * 0.5F,
        size.x,
        size.y};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (plan.canCommit)
        SDL_SetRenderDrawColor(renderer_, 72, 206, 132, 105);
    else
        SDL_SetRenderDrawColor(renderer_, 220, 72, 66, 105);
    SDL_RenderFillRect(renderer_, &bounds);
    if (plan.canCommit)
        SDL_SetRenderDrawColor(renderer_, 112, 244, 170, 255);
    else
        SDL_SetRenderDrawColor(renderer_, 255, 104, 94, 255);
    SDL_RenderRect(renderer_, &bounds);
    uiTextRenderer_.render(
        renderer_,
        bounds.x + 8.0F,
        bounds.y + 8.0F,
        plan.canCommit ? "PLACE FACILITY" : "BLOCKED");
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseConstructionPanel()
{
    if (!baseConstructionPanelOpen_ ||
        gameFlow_.state() != GameFlowState::Base)
        return;

    if (fortificationBuildUi_.category) { renderBaseFortificationPanel(); return; }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel = baseBuildBarBounds();
    SDL_SetRenderDrawColor(renderer_, 18, 30, 31, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 112, 190, 164, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(renderer_, 88.0F, 578.0F, "BASE BUILD");
    uiTextRenderer_.render(
        renderer_, 184.0F, 578.0F,
        "B/ESC CLOSE | WASD/RMB DRAG PAN | WHEEL ZOOM | LMB SELECT | RMB CLICK ACTIONS");

    const auto drawButton = [this](const SDL_FRect &bounds,
                                   const std::string &label,
                                   bool selected = false)
    {
        SDL_SetRenderDrawColor(
            renderer_, selected ? 48 : 34, selected ? 102 : 70,
            selected ? 84 : 66, 245);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 116, 204, 170, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_, bounds.x + 10.0F, bounds.y + 9.0F, label.c_str());
    };

    drawButton(
        baseBuildPurchaseTab(),
        "PURCHASE",
        baseConstructionPage_ == BaseConstructionPage::Purchase);
    drawButton(
        baseBuildOwnedTab(),
        "OWNED",
        baseConstructionPage_ == BaseConstructionPage::Owned);
    drawButton(
        baseBuildZoomResetButton(),
        fmt::format("VIEW {}%", static_cast<int>(
            std::lround(baseConstructionZoom() * 100.0F))));

    const BaseConstructionProjection construction = projectBaseConstruction(
        gameSession_.profile(), publishedContentRegistry());
    const std::string resources = fmt::format(
        "MATERIAL {}/{} | WORKERS {}/{} AVAILABLE",
        construction.materialUnits,
        construction.maximumMaterialUnits,
        construction.availableWorkers,
        construction.totalWorkers);
    uiTextRenderer_.render(renderer_, 350.0F, 574.0F, resources.c_str());

    const std::size_t entryCount = baseBuildCatalogEntryCount();
    const BaseBuildCatalogPage catalogPage = projectBaseBuildCatalogPage(
        entryCount,
        baseConstructionCatalogPage_,
        kBaseBuildCardsPerPage);
    baseConstructionCatalogPage_ = catalogPage.pageIndex;
    const std::string pageLabel = fmt::format(
        "PAGE {}/{}", baseConstructionCatalogPage_ + 1U,
        catalogPage.pageCount);
    uiTextRenderer_.render(renderer_, 830.0F, 574.0F, pageLabel.c_str());
    drawButton(baseBuildPreviousPageButton(), "<");
    drawButton(baseBuildNextPageButton(), ">");

    const auto stashAssets = assetsInContainer(
        gameSession_.profile(), ProfileContainerId::stash());
    const std::size_t firstEntry = catalogPage.firstEntry;
    std::size_t entryIndex{};
    const auto visibleCard = [&]() -> std::optional<std::size_t>
    {
        const std::size_t current = entryIndex++;
        if (current < firstEntry ||
            current >= firstEntry + kBaseBuildCardsPerPage)
        {
            return std::nullopt;
        }
        return current - firstEntry;
    };
    if (baseConstructionPage_ == BaseConstructionPage::Purchase)
    {
        const auto projects = projectBaseConstructionCatalog(
            gameSession_.profile(), publishedContentRegistry());
        for (const BaseConstructionCatalogEntry &project : projects)
        {
            const auto cardIndex = visibleCard();
            if (!cardIndex.has_value())
                continue;
            const SDL_FRect card = baseBuildCard(*cardIndex);
            const bool active = project.action ==
                BaseConstructionCatalogAction::Cancel;
            const bool complete = project.action ==
                BaseConstructionCatalogAction::Complete;
            SDL_SetRenderDrawColor(
                renderer_, active ? 78 : complete ? 48 : 42,
                active ? 82 : complete ? 74 : 68,
                active ? 46 : complete ? 58 : 70, 245);
            SDL_RenderFillRect(renderer_, &card);
            SDL_SetRenderDrawColor(
                renderer_, project.canCommit ? 212 : 126,
                project.canCommit ? 190 : 142,
                project.canCommit ? 102 : 136, 255);
            SDL_RenderRect(renderer_, &card);
            uiTextRenderer_.render(
                renderer_, card.x + 8.0F, card.y + 8.0F,
                baseConstructionTargetName(project.target));
            const std::string levelAndMaterial = fmt::format(
                "LV {}>{} | MAT {}",
                project.currentLevel,
                project.targetLevel,
                project.materialCost);
            uiTextRenderer_.render(
                renderer_, card.x + 8.0F, card.y + 24.0F,
                levelAndMaterial.c_str());
            const std::string workersAndTime = fmt::format(
                "WORKERS {} | HOURS {}",
                project.workerCount,
                project.durationMinutes / kWorldMinutesPerHour);
            uiTextRenderer_.render(
                renderer_, card.x + 8.0F, card.y + 40.0F,
                workersAndTime.c_str());
            std::string status;
            switch (project.action)
            {
            case BaseConstructionCatalogAction::Start:
                status = "START PROJECT";
                break;
            case BaseConstructionCatalogAction::Cancel:
                status = fmt::format(
                    "CANCEL | {} MIN", project.remainingMinutes);
                break;
            case BaseConstructionCatalogAction::Complete:
                status = "PROJECT COMPLETE";
                break;
            case BaseConstructionCatalogAction::Blocked:
                status = "PROJECT BLOCKED";
                break;
            }
            uiTextRenderer_.render(
                renderer_, card.x + 8.0F, card.y + 58.0F,
                status.c_str());
        }
    }
    if (baseConstructionPage_ == BaseConstructionPage::Owned)
    {
        constexpr std::array<BaseFacilityKind, 5U> coreFacilities{
            BaseFacilityKind::Storage,
            BaseFacilityKind::Medical,
            BaseFacilityKind::Dormitory,
            BaseFacilityKind::KitchenWater,
            BaseFacilityKind::Workshop};
        for (BaseFacilityKind facility : coreFacilities)
        {
            const auto definitionId = spatialFacilityDefinitionId(facility);
            if (!definitionId.has_value() ||
                !baseFacilityOwned(gameSession_.profile(), *definitionId) ||
                baseFacilityInstalled(gameSession_.profile(), *definitionId))
            {
                continue;
            }
            const auto cardIndex = visibleCard();
            if (!cardIndex.has_value())
                continue;
            const SDL_FRect card = baseBuildCard(*cardIndex);
            SDL_SetRenderDrawColor(renderer_, 53, 72, 65, 245);
            SDL_RenderFillRect(renderer_, &card);
            SDL_SetRenderDrawColor(renderer_, 172, 204, 132, 255);
            SDL_RenderRect(renderer_, &card);
            const SDL_FRect icon{
                card.x + 8.0F, card.y + 8.0F, 46.0F, 34.0F};
            SDL_SetRenderDrawColor(renderer_, 92, 104, 70, 255);
            SDL_RenderFillRect(renderer_, &icon);
            SDL_SetRenderDrawColor(renderer_, 206, 222, 154, 255);
            SDL_RenderRect(renderer_, &icon);
            uiTextRenderer_.render(
                renderer_, card.x + 8.0F, card.y + 50.0F,
                baseFacilityName(facility));
            uiTextRenderer_.render(
                renderer_, card.x + 62.0F, card.y + 16.0F,
                "RESERVE | PLACE");
        }
    }
    for (const ItemDefinition &definition :
         publishedContentRegistry().items())
    {
        if (!definition.basePlacement.has_value())
            continue;
        std::size_t ownedCount{};
        for (const AssetRecord *asset : stashAssets)
        {
            if (asset->definitionId == definition.definitionId)
                ++ownedCount;
        }
        const bool visible = baseConstructionPage_ ==
            BaseConstructionPage::Purchase
            ? definition.marketBuyPrice > 0U
            : ownedCount > 0U;
        if (!visible)
            continue;
        const auto cardIndex = visibleCard();
        if (!cardIndex.has_value())
            continue;
        const SDL_FRect card = baseBuildCard(*cardIndex);
        SDL_SetRenderDrawColor(renderer_, 31, 59, 56, 245);
        SDL_RenderFillRect(renderer_, &card);
        SDL_SetRenderDrawColor(renderer_, 116, 204, 170, 255);
        SDL_RenderRect(renderer_, &card);
        const SDL_FRect icon{
            card.x + 8.0F, card.y + 8.0F, 46.0F, 34.0F};
        SDL_SetRenderDrawColor(renderer_, 82, 124, 116, 255);
        SDL_RenderFillRect(renderer_, &icon);
        SDL_SetRenderDrawColor(renderer_, 158, 220, 190, 255);
        SDL_RenderRect(renderer_, &icon);
        const SDL_FRect iconInset{
            icon.x + 7.0F, icon.y + 7.0F,
            icon.w - 14.0F, icon.h - 14.0F};
        SDL_RenderRect(renderer_, &iconInset);
        uiTextRenderer_.render(
            renderer_, card.x + 8.0F, card.y + 50.0F,
            definition.displayName.c_str());
        const std::string action = baseConstructionPage_ ==
            BaseConstructionPage::Purchase
            ? fmt::format("BUY {}", definition.marketBuyPrice)
            : fmt::format("OWNED x{}", ownedCount);
        uiTextRenderer_.render(
            renderer_, card.x + 62.0F, card.y + 16.0F,
            action.c_str());
    }
    if (entryCount == 0U)
    {
        uiTextRenderer_.render(
            renderer_, 390.0F, 624.0F,
            baseConstructionPage_ == BaseConstructionPage::Purchase
                ? "NO PLACEABLE FACILITIES AVAILABLE"
                : "NO UNPLACED FACILITIES");
    }
    if (!uiMessage_.empty())
        uiTextRenderer_.render(renderer_, 88.0F, 680.0F, uiMessage_.c_str());
    renderBaseFortificationCategoryButton();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    renderBaseOperationsOverview();
    renderBaseFacilityInspector();
    renderBaseFacilityContextMenu();
}

void App::renderBaseOperationsOverview()
{
    const BaseOperationsOverviewProjection projection =
        projectBaseOperationsOverview(
            gameSession_.profile(), publishedContentRegistry());
    const BaseOperationsOverviewPage page =
        projectBaseOperationsOverviewPage(
            projection,
            baseOperationsOverviewFilter_,
            baseOperationsOverviewPage_);
    baseOperationsOverviewPage_ = page.pageIndex;
    const SDL_FRect panel = baseOperationsOverviewBounds();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 242);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 104, 184, 154, 255);
    SDL_RenderRect(renderer_, &panel);
    uiTextRenderer_.render(
        renderer_, panel.x + 12.0F, panel.y + 10.0F,
        "BASE OPERATIONS | CLICK TO LOCATE");

    constexpr std::array<BaseOperationsOverviewFilter, 4U> filters{
        BaseOperationsOverviewFilter::All,
        BaseOperationsOverviewFilter::Attention,
        BaseOperationsOverviewFilter::InProgress,
        BaseOperationsOverviewFilter::Ready};
    for (std::size_t index{}; index < filters.size(); ++index)
    {
        const BaseOperationsOverviewPage filtered =
            projectBaseOperationsOverviewPage(
                projection, filters[index], 0U);
        const bool selected = filters[index] == baseOperationsOverviewFilter_;
        const SDL_FRect button = baseOperationsOverviewFilterButton(index);
        SDL_SetRenderDrawColor(
            renderer_, selected ? 48 : 28, selected ? 100 : 58,
            selected ? 82 : 54, 246);
        SDL_RenderFillRect(renderer_, &button);
        SDL_SetRenderDrawColor(
            renderer_, selected ? 126 : 78,
            selected ? 214 : 122,
            selected ? 174 : 112, 255);
        SDL_RenderRect(renderer_, &button);
        const std::string label = fmt::format(
            "{} {}",
            baseOperationsOverviewFilterName(filters[index]),
            filtered.matchingEntryCount);
        uiTextRenderer_.render(
            renderer_, button.x + 6.0F, button.y + 5.0F, label.c_str());
    }

    if (page.visibleEntryCount == 0U)
    {
        uiTextRenderer_.render(
            renderer_, panel.x + 12.0F, panel.y + 76.0F,
            projection.entries.empty()
                ? "NO ACTIVE OPERATIONS"
                : "NO MATCHING OPERATIONS");
    }

    for (std::size_t index{}; index < page.visibleEntryCount; ++index)
    {
        const BaseOperationOverviewEntry &entry =
            projection.entries[page.entryIndices[index]];
        const SDL_FRect row = baseOperationsOverviewRow(index);
        const bool selected = selectedBaseFixedFacility_ == entry.facility;
        const bool attention = baseOperationMatchesFilter(
            entry.kind, BaseOperationsOverviewFilter::Attention);
        SDL_SetRenderDrawColor(
            renderer_, selected ? 48 : 30, selected ? 100 : 62,
            selected ? 82 : 58, 246);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(
            renderer_,
            entry.kind == BaseOperationOverviewKind::OutputReady
                ? 232
                : attention ? 226 : 102,
            entry.kind == BaseOperationOverviewKind::OutputReady
                ? 196
                : attention ? 112 : 184,
            entry.kind == BaseOperationOverviewKind::OutputReady
                ? 88
                : attention ? 76 : 154,
            255);
        SDL_RenderRect(renderer_, &row);
        std::string label = fmt::format(
            "{} | {}",
            baseOperationsFacilityName(entry.facility),
            baseOperationOverviewKindName(entry.kind));
        if (entry.remainingMinutes > 0U)
            label += fmt::format(" | {} MIN", entry.remainingMinutes);
        if (entry.paused)
            label += " | PAUSED";
        uiTextRenderer_.render(
            renderer_, row.x + 9.0F, row.y + 7.0F, label.c_str());
    }

    const SDL_FRect previous = baseOperationsOverviewPreviousPageButton();
    const SDL_FRect next = baseOperationsOverviewNextPageButton();
    SDL_SetRenderDrawColor(renderer_, 30, 62, 58, 246);
    SDL_RenderFillRect(renderer_, &previous);
    SDL_RenderFillRect(renderer_, &next);
    SDL_SetRenderDrawColor(renderer_, 102, 184, 154, 255);
    SDL_RenderRect(renderer_, &previous);
    SDL_RenderRect(renderer_, &next);
    uiTextRenderer_.render(
        renderer_, previous.x + 12.0F, previous.y + 4.0F, "<");
    uiTextRenderer_.render(
        renderer_, next.x + 12.0F, next.y + 4.0F, ">");
    const std::string pageLabel = fmt::format(
        "PAGE {}/{}",
        page.pageIndex + 1U,
        page.pageCount);
    uiTextRenderer_.render(
        renderer_, panel.x + 164.0F, panel.y + 216.0F,
        pageLabel.c_str());
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseFacilityInspector()
{
    if (!selectedBasePlacedAssetId_.has_value() &&
        !selectedBaseFixedFacility_.has_value())
    {
        return;
    }

    const SDL_FRect panel = baseFacilityInspectorBounds();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 246);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 126, 214, 174, 255);
    SDL_RenderRect(renderer_, &panel);

    const auto drawButton = [this](const SDL_FRect &bounds,
                                   const char *label,
                                   bool enabled = true)
    {
        SDL_SetRenderDrawColor(
            renderer_, 34, enabled ? 70 : 42,
            enabled ? 66 : 42, 248);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(
            renderer_, enabled ? 104 : 76, enabled ? 184 : 82,
            enabled ? 154 : 82, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(
            renderer_, bounds.x + 9.0F, bounds.y + 9.0F, label);
    };

    if (selectedBaseFixedFacility_.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(),
                publishedContentRegistry(),
                *selectedBaseFixedFacility_);
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 14.0F,
            baseFacilityName(projection.kind));
        const std::string state = fmt::format(
            "STATUS: {} | LEVEL: {}",
            baseFacilityStatusName(projection.status),
            projection.level.has_value()
                ? std::to_string(*projection.level)
                : std::string{"FIXED SERVICE"});
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 42.0F,
            state.c_str());

        if (projection.kind == BaseFacilityKind::Dormitory)
        {
            const auto residents = projectBaseResidentWorldStatus(
                gameSession_.profile(), projection.kind);
            if (residents.has_value())
            {
                const std::string population =
                    baseResidentWorldPrimaryLabel(*residents);
                const std::string workforce =
                    baseResidentWorldWorkforceLabel(*residents);
                uiTextRenderer_.render(
                    renderer_, panel.x + 16.0F, panel.y + 70.0F,
                    population.c_str());
                uiTextRenderer_.render(
                    renderer_, panel.x + 16.0F, panel.y + 98.0F,
                    workforce.c_str());
            }
        }
        else if (const auto resourceFlow =
                     projectBaseResourceFlowWorldStatus(
                         gameSession_.profile(), publishedContentRegistry(),
                         projection.kind);
                 resourceFlow.has_value())
        {
            const std::string primary =
                baseResourceFlowWorldPrimaryLabel(*resourceFlow);
            const std::string secondary =
                baseResourceFlowWorldSecondaryLabel(*resourceFlow);
            uiTextRenderer_.render(
                renderer_, panel.x + 16.0F, panel.y + 70.0F,
                primary.c_str());
            uiTextRenderer_.render(
                renderer_, panel.x + 16.0F, panel.y + 98.0F,
                secondary.c_str());
        }
        else
        {
            const std::string staffing = projection.staffingApplicable
                ? fmt::format(
                      "STAFF: {}",
                      projection.assignedWorker.has_value()
                          ? baseResidentProfessionName(
                                *projection.assignedWorker)
                          : "UNASSIGNED")
                : "STAFF: NOT REQUIRED";
            uiTextRenderer_.render(
                renderer_, panel.x + 16.0F, panel.y + 70.0F,
                staffing.c_str());

            std::string task = fmt::format(
                "CURRENT TASK: {}",
                baseFacilityTaskName(projection.task));
            if (projection.remainingMinutes > 0U)
                task += fmt::format(
                    " | REMAINING {} MIN",
                    projection.remainingMinutes);
            uiTextRenderer_.render(
                renderer_, panel.x + 16.0F, panel.y + 98.0F,
                task.c_str());
        }
        for (std::size_t index{};
             index < projection.quickActions.size(); ++index)
        {
            const BaseFacilityQuickActionProjection &action =
                projection.quickActions[index];
            drawButton(
                baseFacilityQuickActionButton(index),
                baseFacilityQuickActionName(action.kind),
                action.canCommit);
        }
        if (movableCoreFacility(projection.kind))
        {
            drawButton(
                baseFacilityQuickActionButton(
                    projection.quickActions.size()),
                projection.status == BaseFacilityOperationalStatus::Reserve
                    ? "INSTALL FACILITY"
                    : "MOVE FACILITY",
                projection.status !=
                    BaseFacilityOperationalStatus::Unavailable);
        }
    }
    else
    {
        const AssetRecord *asset = gameSession_.profile().assets.find(
            *selectedBasePlacedAssetId_);
        if (asset == nullptr)
        {
            selectedBasePlacedAssetId_.reset();
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            return;
        }
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        std::size_t storedStacks{};
        for (const auto &[candidateId, candidate] :
             gameSession_.profile().assets.records())
        {
            static_cast<void>(candidateId);
            const auto *stored = std::get_if<StoredAssetLocation>(
                &candidate.location);
            if (stored != nullptr &&
                stored->container.kind ==
                    ProfileContainerKind::AssetCompartment &&
                stored->container.ownerAssetId == asset->instanceId)
            {
                ++storedStacks;
            }
        }
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 14.0F,
            definition.displayName.c_str());
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 42.0F,
            "STATUS: PLACED | LEVEL: FIXED SERVICE");
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 70.0F,
            "STAFF: NOT REQUIRED");
        const std::string contents = fmt::format(
            "CURRENT TASK: IDLE | CONTENT STACKS: {}",
            storedStacks);
        uiTextRenderer_.render(
            renderer_, panel.x + 16.0F, panel.y + 98.0F,
            contents.c_str());
        drawButton(
            basePlacedFacilityInspectorButton(0U, 3U), "OPEN FUNCTION");
        drawButton(
            basePlacedFacilityInspectorButton(1U, 3U), "MOVE FACILITY");
        drawButton(
            basePlacedFacilityInspectorButton(2U, 3U), "RECOVER EMPTY");
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderBaseFacilityContextMenu()
{
    if (!baseConstructionPanelOpen_ ||
        !baseFacilityContextMenu_.has_value())
    {
        return;
    }
    const BaseFacilityContextMenu &menu = *baseFacilityContextMenu_;
    if (menu.mode == BaseFacilityContextMenuMode::ResourceFlowArea &&
        menu.fixedFacility.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                *menu.fixedFacility);
        const auto action = std::find_if(
            projection.quickActions.begin(), projection.quickActions.end(),
            [](const BaseFacilityQuickActionProjection &candidate)
            {
                return candidate.kind ==
                    BaseFacilityQuickActionKind::OpenFunction;
            });
        if (action == projection.quickActions.end())
            return;
        const SDL_FRect bounds = baseFacilityContextMenuBounds(
            menu.position, 1U);
        const SDL_FRect row = baseFacilityContextMenuRow(
            menu.position, 0U, 1U);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 250);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 126, 174, 218, 255);
        SDL_RenderRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(
            renderer_, 34, action->canCommit ? 70 : 42,
            action->canCommit ? 66 : 42, 248);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(
            renderer_, action->canCommit ? 104 : 76,
            action->canCommit ? 184 : 82,
            action->canCommit ? 154 : 82, 255);
        SDL_RenderRect(renderer_, &row);
        uiTextRenderer_.render(
            renderer_, row.x + 12.0F, row.y + 9.0F,
            baseFacilityQuickActionName(action->kind));
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    if (menu.mode == BaseFacilityContextMenuMode::WorkforceStation &&
        menu.fixedFacility.has_value())
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                *menu.fixedFacility);
        std::vector<const BaseFacilityQuickActionProjection *> actions;
        for (const BaseFacilityQuickActionProjection &action :
             projection.quickActions)
        {
            if (action.kind ==
                    BaseFacilityQuickActionKind::AssignBestWorker ||
                action.kind == BaseFacilityQuickActionKind::ClearWorker ||
                action.kind == BaseFacilityQuickActionKind::AutoFillWorkers)
            {
                actions.push_back(&action);
            }
        }
        if (actions.empty())
            return;
        const SDL_FRect bounds = baseFacilityContextMenuBounds(
            menu.position, actions.size());
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 250);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 126, 214, 174, 255);
        SDL_RenderRect(renderer_, &bounds);
        for (std::size_t index{}; index < actions.size(); ++index)
        {
            const BaseFacilityQuickActionProjection &action =
                *actions[index];
            const SDL_FRect row = baseFacilityContextMenuRow(
                menu.position, index, actions.size());
            SDL_SetRenderDrawColor(
                renderer_, 34, action.canCommit ? 70 : 42,
                action.canCommit ? 66 : 42, 248);
            SDL_RenderFillRect(renderer_, &row);
            SDL_SetRenderDrawColor(
                renderer_, action.canCommit ? 104 : 76,
                action.canCommit ? 184 : 82,
                action.canCommit ? 154 : 82, 255);
            SDL_RenderRect(renderer_, &row);
            uiTextRenderer_.render(
                renderer_, row.x + 12.0F, row.y + 9.0F,
                baseFacilityQuickActionName(action.kind));
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    if (menu.mode == BaseFacilityContextMenuMode::ResidentArea &&
        menu.fixedFacility == BaseFacilityKind::Dormitory)
    {
        const BaseFacilityManagementProjection projection =
            projectBaseFacilityManagement(
                gameSession_.profile(), publishedContentRegistry(),
                BaseFacilityKind::Dormitory);
        std::vector<const BaseFacilityQuickActionProjection *> actions;
        for (const BaseFacilityQuickActionProjection &action :
             projection.quickActions)
        {
            if (action.kind == BaseFacilityQuickActionKind::OpenFunction ||
                action.kind == BaseFacilityQuickActionKind::AutoFillWorkers)
            {
                actions.push_back(&action);
            }
        }
        if (actions.empty())
            return;
        const SDL_FRect bounds = baseFacilityContextMenuBounds(
            menu.position, actions.size());
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 250);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 132, 170, 214, 255);
        SDL_RenderRect(renderer_, &bounds);
        for (std::size_t index{}; index < actions.size(); ++index)
        {
            const BaseFacilityQuickActionProjection &action =
                *actions[index];
            const SDL_FRect row = baseFacilityContextMenuRow(
                menu.position, index, actions.size());
            SDL_SetRenderDrawColor(
                renderer_, 38, action.canCommit ? 66 : 42,
                action.canCommit ? 86 : 42, 248);
            SDL_RenderFillRect(renderer_, &row);
            SDL_SetRenderDrawColor(
                renderer_, action.canCommit ? 126 : 76,
                action.canCommit ? 174 : 82,
                action.canCommit ? 218 : 82, 255);
            SDL_RenderRect(renderer_, &row);
            uiTextRenderer_.render(
                renderer_, row.x + 12.0F, row.y + 9.0F,
                baseFacilityQuickActionName(action.kind));
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    const std::size_t rowCount = menu.assetId.has_value()
        ? 3U
        : menu.fixedFacility.has_value() &&
                movableCoreFacility(*menu.fixedFacility)
            ? 2U
            : 1U;
    const SDL_FRect bounds = baseFacilityContextMenuBounds(
        menu.position, rowCount);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 12, 23, 24, 250);
    SDL_RenderFillRect(renderer_, &bounds);
    SDL_SetRenderDrawColor(renderer_, 126, 214, 174, 255);
    SDL_RenderRect(renderer_, &bounds);
    constexpr std::array<const char *, 3U> placedLabels{
        "OPEN FUNCTION", "MOVE FACILITY", "RECOVER EMPTY"};
    for (std::size_t index{}; index < rowCount; ++index)
    {
        const SDL_FRect row = baseFacilityContextMenuRow(
            menu.position, index, rowCount);
        SDL_SetRenderDrawColor(renderer_, 34, 70, 66, 248);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, 104, 184, 154, 255);
        SDL_RenderRect(renderer_, &row);
        uiTextRenderer_.render(
            renderer_, row.x + 12.0F, row.y + 9.0F,
            placedLabels[index]);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderHomeRegionMap()
{
    if (!tacticalMapOpen_ || gameFlow_.state() != GameFlowState::Base)
    {
        return;
    }

    const BaseWorld &world = gameFlow_.baseWorld();
    const HomeRegionLayout &layout = world.layout();
    const Vec2 worldSize = world.worldSize();
    const SDL_FRect shade{0.0F, 0.0F,
                          static_cast<float>(kWindowWidth),
                          static_cast<float>(kWindowHeight)};
    const SDL_FRect panel{104.0F, 54.0F, 1072.0F, 612.0F};
    const SDL_FRect chart{192.0F, 114.0F, 896.0F, 504.0F};
    const float cellWidth = chart.w / static_cast<float>(layout.columns);
    const float cellHeight = chart.h / static_cast<float>(layout.rows);
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

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 3, 7, 9, 220);
    SDL_RenderFillRect(renderer_, &shade);
    SDL_SetRenderDrawColor(renderer_, 12, 22, 25, 248);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 86, 142, 126, 255);
    SDL_RenderRect(renderer_, &panel);

    SDL_SetRenderDrawColor(renderer_, 42, 50, 38, 255);
    SDL_RenderFillRect(renderer_, &chart);
    for (const RaidTerrainSpan &span : layout.terrainSpans)
    {
        switch (span.kind)
        {
        case RaidTerrainKind::Grass:
            SDL_SetRenderDrawColor(renderer_, 68, 91, 56, 235); break;
        case RaidTerrainKind::Concrete:
            SDL_SetRenderDrawColor(renderer_, 104, 106, 98, 235); break;
        case RaidTerrainKind::Dirt:
            SDL_SetRenderDrawColor(renderer_, 104, 79, 50, 235); break;
        case RaidTerrainKind::Asphalt:
            SDL_SetRenderDrawColor(renderer_, 62, 66, 64, 235); break;
        case RaidTerrainKind::Puddle:
            SDL_SetRenderDrawColor(renderer_, 48, 86, 98, 235); break;
        }
        const SDL_FRect terrain{
            chart.x + static_cast<float>(span.firstColumn) * cellWidth,
            chart.y + static_cast<float>(span.row) * cellHeight,
            static_cast<float>(span.length) * cellWidth + 0.25F,
            cellHeight + 0.25F};
        SDL_RenderFillRect(renderer_, &terrain);
    }

    for (const RaidOutdoorRoadCell &road : layout.roadCells)
    {
        if (road.kind == RaidOutdoorRoadKind::Primary)
            SDL_SetRenderDrawColor(renderer_, 146, 142, 116, 235);
        else if (road.kind == RaidOutdoorRoadKind::Secondary)
            SDL_SetRenderDrawColor(renderer_, 112, 116, 98, 225);
        else
            SDL_SetRenderDrawColor(renderer_, 82, 98, 86, 215);
        const SDL_FRect roadCell{
            chart.x + static_cast<float>(road.column) * cellWidth,
            chart.y + static_cast<float>(road.row) * cellHeight,
            cellWidth + 0.25F,
            cellHeight + 0.25F};
        SDL_RenderFillRect(renderer_, &roadCell);
    }

    for (const RaidOutdoorPropSnapshot &prop : layout.props)
    {
        SDL_FRect bounds = screenRect(prop.bounds);
        bounds.w = std::max(bounds.w, prop.collidable ? 2.0F : 1.0F);
        bounds.h = std::max(bounds.h, prop.collidable ? 2.0F : 1.0F);
        SDL_SetRenderDrawColor(
            renderer_, prop.collidable ? 82 : 104,
            prop.collidable ? 76 : 92,
            prop.collidable ? 62 : 72,
            prop.collidable ? 235 : 155);
        SDL_RenderFillRect(renderer_, &bounds);
    }

    const SDL_FRect baseBounds = screenRect(world.baseParcel());
    SDL_SetRenderDrawColor(renderer_, 30, 92, 72, 185);
    SDL_RenderFillRect(renderer_, &baseBounds);
    SDL_SetRenderDrawColor(renderer_, 126, 224, 174, 255);
    SDL_RenderRect(renderer_, &baseBounds);
    if (world.hasFoundingPlots())
        for (const auto &plot : homePlotDefinitions())
        {
            const auto bounds = screenRect(plot.bounds);
            SDL_SetRenderDrawColor(renderer_,210,201,129,240);
            SDL_RenderRect(renderer_, &bounds);
            uiTextRenderer_.render(renderer_,bounds.x,bounds.y+4,plot.name.data());
        }
    for (const BaseFacility &facility : world.facilities())
    {
        if (!facility.active)
            continue;
        SDL_FRect bounds = screenRect(ContentRect{
            facility.bounds.position, facility.bounds.size});
        bounds.w = std::max(bounds.w, 4.0F);
        bounds.h = std::max(bounds.h, 4.0F);
        SDL_SetRenderDrawColor(renderer_, 174, 198, 184, 235);
        SDL_RenderFillRect(renderer_, &bounds);
        const BaseFacilityAccessGeometry access =
            baseFacilityAccessGeometry(facility);
        const BaseFacilityWorldServiceProjection service =
            projectBaseFacilityWorldService(
                gameSession_.profile(),
                publishedContentRegistry(),
                facility.kind);
        const Vec2 entrance = screenPoint(access.entrancePoint);
        const SDL_FRect entranceMarker{
            entrance.x - 2.5F, entrance.y - 2.5F, 5.0F, 5.0F};
        const SDL_Color serviceColor =
            baseFacilityWorldServiceColor(service.status);
        SDL_SetRenderDrawColor(
            renderer_, serviceColor.r, serviceColor.g,
            serviceColor.b, serviceColor.a);
        SDL_RenderFillRect(renderer_, &entranceMarker);
    }

    for (const HomeRegionDistrictSnapshot &district : layout.districts)
    {
        const Vec2 label = screenPoint(district.labelPosition);
        uiTextRenderer_.render(
            renderer_, label.x, label.y,
            homeRegionDistrictName(district.kind));
    }

    for (const BaseGroundAssetProjection &ground :
         gameFlow_.baseGroundAssets())
    {
        const Vec2 marker = screenPoint(ground.position);
        const SDL_FRect bounds{marker.x - 2.5F, marker.y - 2.5F, 5.0F, 5.0F};
        SDL_SetRenderDrawColor(renderer_, 92, 206, 230, 245);
        SDL_RenderFillRect(renderer_, &bounds);
    }

    const Vec2 player = screenPoint(Vec2{
        world.playerPosition().x + world.playerSize().x * 0.5F,
        world.playerPosition().y + world.playerSize().y * 0.5F});
    SDL_SetRenderDrawColor(renderer_, 238, 242, 224, 255);
    SDL_RenderLine(renderer_, player.x - 7.0F, player.y,
                   player.x + 7.0F, player.y);
    SDL_RenderLine(renderer_, player.x, player.y - 7.0F,
                   player.x, player.y + 7.0F);

    uiTextRenderer_.render(
        renderer_, 144.0F, 76.0F,
        "HOME REGION MAP | M/ESC CLOSE | WORLD CONTINUES");
    uiTextRenderer_.render(
        renderer_, 144.0F, 638.0F,
        world.canAccessStash()
            ? "WAREHOUSE LINK: CONNECTED INSIDE BASE PARCEL"
            : "WAREHOUSE LINK: DISCONNECTED OUTSIDE BASE PARCEL");
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

void App::renderProfileAsset(const AssetRecord &asset, const SDL_FRect &bounds,
                             float cellSize, Uint8 alpha,
                             bool showWeaponCondition, bool fitImage, bool showQuantity)
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
        if (fitImage)
        {
            const auto source=texture->contentBounds();
            const auto fit = fitInventoryTexture(
                source.w, source.h, {bounds.x, bounds.y, bounds.w, bounds.h},
                asset.orientation, 1.0F);
            const SDL_FRect destination{fit.x, fit.y, fit.width, fit.height};
            SDL_RenderTextureRotated(
                renderer_, texture->get(), &source, &destination,
                orientationAngle(asset.orientation), nullptr, SDL_FLIP_NONE);
        }
        else
            renderOrientedTexture(renderer_, texture->get(), bounds,
                                  definition.inventoryWidthCells * cellSize,
                                  definition.inventoryHeightCells * cellSize,
                                  asset.orientation);
        SDL_SetTextureAlphaMod(texture->get(), 255);
    }
    else
    {
        switch (definition.category)
        {
        case ItemCategory::Weapon:
        case ItemCategory::MeleeWeapon:
            SDL_SetRenderDrawColor(renderer_, 94, 112, 122, alpha);
            break;
        case ItemCategory::Battery:
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
        SDL_SetRenderDrawColor(renderer_,37,47,48,static_cast<Uint8>(alpha*0.6F));
        SDL_RenderFillRect(renderer_, &bounds);
        const std::string shortName = localizeUiLabel(
            uiTextRenderer_.language(),
            compactUiItemName(definition,uiTextRenderer_.language()), 80);
        SDL_SetRenderDrawColor(renderer_, 232, 236, 232, 255);
        uiTextRenderer_.renderBox(renderer_,
                                  {bounds.x + 3, bounds.y + 3,
                                   std::max(0.0F, bounds.w - 6),
                                   std::max(0.0F, bounds.h - 24)},
                                  shortName, {12, UiTextAlign::Left, 1});
    }

    SDL_SetRenderDrawColor(renderer_, 214, 220, 214, alpha);
    SDL_RenderRect(renderer_, &bounds);
    if(showQuantity)renderItemQuantityBadge(renderer_, bounds, asset.quantity);
    if(texture && texture->valid() && bounds.w>30 && bounds.h>34) {
        SDL_SetRenderDrawColor(renderer_,224,234,229,alpha);
        uiTextRenderer_.renderBox(renderer_,{bounds.x+3,bounds.y+3,bounds.w-6,18},
            compactUiItemName(definition,uiTextRenderer_.language()),{12,UiTextAlign::Left,1});
    }
    if(showWeaponCondition && asset.currentMaximumDurability>0) {
        const float ratio=std::clamp(static_cast<float>(asset.currentDurability)/asset.currentMaximumDurability,0.0F,1.0F);
        SDL_FRect bar{bounds.x+3,bounds.y+bounds.h-4,(bounds.w-6)*ratio,2};
        SDL_SetRenderDrawColor(renderer_,ratio<.25F?196:112,ratio<.25F?103:170,128,alpha);
        SDL_RenderFillRect(renderer_,&bar);
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
        container == ProfileContainerId::baseIntake() ||
        container == ProfileContainerId::legacyOverflow();
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
    SDL_SetRenderDrawColor(renderer_, 230, 230, 210, 255);
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
        if (container.kind != ProfileContainerKind::Stash &&
            SDL_RenderClipEnabled(renderer_))
        {
            SDL_Rect c{};
            SDL_GetRenderClipRect(renderer_, &c);
            const SDL_FRect full{
                x + stored.origin.x * cellSize, y + stored.origin.y * cellSize,
                footprint.width * cellSize, footprint.height * cellSize};
            const SDL_FRect clip{
                static_cast<float>(c.x), static_cast<float>(c.y),
                static_cast<float>(c.w), static_cast<float>(c.h)};
            SDL_FRect visible{};
            if (SDL_GetRectIntersectionFloat(&full, &clip, &visible) &&
                (visible.w < full.w || visible.h < full.h))
            {
                SDL_SetRenderDrawColor(renderer_, 26, 58, 68, 240);
                const SDL_FRect marker{visible.x + 2, visible.y + 2,
                                       std::min(60.0F, visible.w - 4), 20};
                SDL_RenderFillRect(renderer_, &marker);
                SDL_SetRenderDrawColor(renderer_, 235, 223, 170, 255);
                uiTextRenderer_.renderBox(renderer_, marker, "CONT.",
                                          {14, UiTextAlign::Center, 1});
            }
        }
    }
}

void App::renderProfileDragFeedback(
    bool includeStash,
    bool inRaid,
    std::optional<AssetInstanceId> externalContainerId)
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
                        if (!externalContainerId.has_value())
                        {
                            const auto installed =
                                std::get<InstalledMagazineLocation>(
                                    source->location);
                            allowed = queryWeaponAmmo(
                                profile,
                                publishedContentRegistry(),
                                UninstallMagazineCommand{
                                    installed.weaponAssetId,
                                    typedTarget.location,
                                    source->orientation}).canCommit;
                        }
                    }
                    else
                    {
                        const InventoryCommand command = InventoryMoveCommand{
                            source->instanceId,
                            source->quantity,
                            typedTarget.location,
                            source->orientation};
                        allowed = inRaid && externalContainerId
                            ? gameSession_.queryRaidSceneContainerInventory(*externalContainerId, command).canCommit
                            : externalContainerId.has_value()
                            ? gameFlow_.queryBaseGroundContainerInventory(
                                  *externalContainerId,
                                  command).canCommit
                            : queryInventory(
                                  profile,
                                  publishedContentRegistry(),
                                  command).canCommit;
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
                    const InventoryCommand command = InventoryEquipCommand{
                        source->instanceId,
                        typedTarget.slot};
                    const bool allowed = inRaid && externalContainerId
                        ? gameSession_.queryRaidSceneContainerInventory(*externalContainerId, command).canCommit
                        : externalContainerId.has_value()
                        ? gameFlow_.queryBaseGroundContainerInventory(
                              *externalContainerId,
                              command).canCommit
                        : queryInventory(
                              profile,
                              publishedContentRegistry(),
                              command).canCommit;
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
                else if constexpr (
                    std::is_same_v<Target, ArmorMaintenanceTarget>)
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
                else
                {
                    const BaseGroundPlan plan = inRaid
                        ? BaseGroundPlan{
                              false,
                              DomainErrorCode::IllegalDestination,
                              "Base ground is unavailable in Raid",
                              profile.revision,
                              0}
                        : gameFlow_.queryBaseGroundDrop(
                              source->instanceId,
                              source->quantity,
                              visual->orientation);
                    feedback.kind = plan.canCommit
                        ? ProfileDropFeedbackKind::Special
                        : ProfileDropFeedbackKind::Invalid;
                    feedback.label = plan.canCommit ? "DROP" : "BLOCKED";
                }
            },
            *target);
    }

    if (inRaid && raidSceneContainerRoot(profile, source->instanceId) && target &&
        !std::holds_alternative<StoredCellTarget>(*target) && !std::holds_alternative<EquipmentSlotTarget>(*target))
    {
        feedback.kind = ProfileDropFeedbackKind::Invalid;
        feedback.label = "MOVE ITEM TO CARRIED INVENTORY FIRST";
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
                    for (const ProfileGridView &view : profileGridViews(
                             profile, includeStash, externalContainerId,
                             &profilePresentation_))
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
                else if constexpr (
                    std::is_same_v<Target, BaseGroundTarget>)
                {
                    targetBounds = profileBaseGroundDropRect();
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
                            profile, *targetAsset, includeStash,
                            externalContainerId, &profilePresentation_);
                    }
                }
            },
            *target);
        if (targetBounds && std::holds_alternative<StoredCellTarget>(*target))
            for (const auto &view :
                 profileGridViews(profile, includeStash, externalContainerId,
                                  &profilePresentation_))
                if (view.container == std::get<StoredCellTarget>(*target)
                                          .location.container &&
                    view.clip)
                {
                    SDL_FRect visible{};
                    if (SDL_GetRectIntersectionFloat(&*targetBounds,
                                                     &*view.clip, &visible))
                        targetBounds = visible;
                    else
                        targetBounds.reset();
                    break;
                }
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
        visual->pointerPosition.x - visual->grabOffsetInCells.x * kProfileItemCell,
        visual->pointerPosition.y - visual->grabOffsetInCells.y * kProfileItemCell,
        static_cast<float>(visual->footprint.width) * kProfileItemCell,
        static_cast<float>(visual->footprint.height) * kProfileItemCell};
    renderProfileAsset(ghost, ghostBounds, kProfileItemCell, 150);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderRect(renderer_, &ghostBounds);
    uiTextRenderer_.render(
        renderer_,
        visual->pointerPosition.x + 16.0F,
        visual->pointerPosition.y + 16.0F,
        feedback.label);
}

void App::renderMedicalWheel()
{
    if (!medicalWheelOpen_)
    {
        if (!throwableWheelOpen_) {quickWheelEmphasis_ = {};quickWheelVisualIndex_=static_cast<std::size_t>(-1);}
        return;
    }
    renderQuickWheel(UiQuickWheelKind::Medical, medicalWheelOptions_, medicalWheelSelectedIndex_);
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
        "DEVELOPER RUNTIME PANEL");
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
    const std::optional<RaidHighRiskCrisisProjection> crisis =
        gameSession_.raidHighRiskCrisisProjection();
    renderButton(
        developerCrisisRevealButton(),
        developerCrisisRevealEnabled_,
        developerCrisisRevealEnabled_
            ? "CRISIS HUD: REVEALED"
            : "CRISIS HUD: NORMAL");
    renderButton(
        developerTriggerHighRiskButton(),
        crisis.has_value() && crisis->active,
        crisis.has_value() && crisis->active
            ? "HIGH RISK: ACTIVE"
            : "ACTIVATE HIGH RISK NOW");
    renderButton(
        developerResetWeaponButton(), false, "RESET WEAPON PARAMETERS");
    renderButton(
        developerPublishedCatalogButton(),
        gameSession_.developerWarehouseCatalogGranted(),
        gameSession_.developerWarehouseCatalogGranted()
            ? "PUBLISHED CATALOG: GRANTED"
            : "GRANT PUBLISHED CATALOG");
    if (gameFlow_.state() == GameFlowState::Base)
        renderButton(developerBaseSiegeButton(), gameSession_.baseDefenseActive(),
            "CREATE BASE SIEGE WARNING");
    renderButton({670, 108, 280, 34}, gameSession_.developerRangeActive(),
        gameSession_.developerRangeActive() ? "LEAVE TEST RANGE" : "ENTER COMBAT TEST RANGE");
    const std::string crisisIdentityLine = crisis.has_value()
        ? fmt::format(
              "CRISIS DEBUG: {} | DISTRICT {} | RESOURCE POINT {}",
              crisis->displayName,
              crisis->districtInstanceId,
              crisis->resourcePointInstanceId)
        : "NO FROZEN CRISIS ON THIS MAP";
    const std::string crisisPressureLine = crisis.has_value()
        ? fmt::format(
              "PRESSURE DEBUG: NEXT {:.1f}s | INTERVAL {:.0f}s | WAVE {} | CAP {} | SOURCES {}",
              crisis->nextWaveSeconds,
              crisis->waveIntervalSeconds,
              crisis->waveSize,
              crisis->activeEnemyCap,
              crisis->pressureSpawnCount)
        : "PRESSURE DEBUG: UNAVAILABLE";
    // The second button row must not be overprinted by crisis diagnostics.
    if (gameFlow_.state() != GameFlowState::Base)
    {
        uiTextRenderer_.render(renderer_, 665.0F, 111.0F, crisisIdentityLine.c_str());
        uiTextRenderer_.render(renderer_, 665.0F, 128.0F, crisisPressureLine.c_str());
    }
    if (!tuning.has_value())
    {
        uiTextRenderer_.render(
            renderer_, panel.x + 22.0F, panel.y + 138.0F,
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
        renderer_, panel.x + 22.0F, panel.y + 138.0F,
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

void App::renderProfileInventory(
    bool includeStash,
    bool inRaid,
    std::optional<AssetInstanceId> externalContainerId)
{
    if(inventoryWeaponComponentsOpen_ && (!inRaid || gameSession_.developerRangeAtConsole())) {renderWeaponComponents();return;}
    if(profileDetailsAsset_) {renderProfileWeaponDetails();return;}
    const auto &profile=gameSession_.profile();
    const bool right=includeStash || externalContainerId.has_value();
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{20,54,right?1240.0F:780.0F,646};
    SDL_SetRenderDrawColor(renderer_,16,23,26,245);SDL_RenderFillRect(renderer_,&panel);
    SDL_SetRenderDrawColor(renderer_,62,84,87,255);SDL_RenderRect(renderer_,&panel);
    const auto text=[&](SDL_FRect r,const std::string &t,int size=14){
        SDL_SetRenderDrawColor(renderer_,216,228,224,255);
        uiTextRenderer_.renderBox(renderer_,r,t,{size,UiTextAlign::Left,1});
    };
    text({32,68,232,28},"CHARACTER & LOADOUT",16);
    const SDL_FRect preview{92,120,108,226};renderPlayerPreview(preview);
    for(auto slot:kProfileEquipmentSlots) {
        const auto bounds=equipmentSlotRect(slot);
        SDL_SetRenderDrawColor(renderer_,25,36,39,220);SDL_RenderFillRect(renderer_,&bounds);
        const bool active=inRaid && isWeaponEquipmentSlot(slot) && gameSession_.activeAlphaWeaponSlot()==slot;
        SDL_SetRenderDrawColor(renderer_,active?99:58,active?184:78,active?151:81,255);SDL_RenderRect(renderer_,&bounds);
        if(const auto id=equippedAsset(profile,slot)) {
            if(const auto *asset=profile.assets.find(*id)) {
                renderProfileAsset(*asset,bounds,kProfileItemCell,255,false,true);
                if(isWeaponEquipmentSlot(slot)) if(const auto magazine=installedMagazine(profile,*id))
                    if(const auto *m=profile.assets.find(*magazine))
                        renderProfileAsset(*m,installedMagazineRect(slot),kProfileItemCell,235,false,true);
            }
        } else {
            SDL_SetRenderDrawColor(renderer_,130,151,150,210);
            uiTextRenderer_.renderBox(renderer_,{bounds.x+3,bounds.y+3,bounds.w-6,bounds.h-6},
                equipmentSlotLabel(slot),{12,UiTextAlign::Center,2});
        }
    }
    text({32,594,232,22},fmt::format("HP {}   {:.1f} KG",profile.currentHealth,
        carriedWeightGrams(profile,publishedContentRegistry())/1000.0));
    if(profile.medicalStatus.bleeding!=BleedingSeverity::None)
        text({32,618,232,20},profile.medicalStatus.bleeding==BleedingSeverity::Heavy?"HEAVY BLEEDING":"LIGHT BLEEDING",12);
    if(right) {
        const char *title="WAREHOUSE";
        if(profilePresentation_.legacyOverflow && !assetsInContainer(profile,ProfileContainerId::legacyOverflow()).empty())title="LEGACY ITEMS";
        else if(profilePresentation_.returns && !assetsInContainer(profile,ProfileContainerId::baseIntake()).empty())title="UNASSIGNED RETURNS";
        if(externalContainerId) {
            const auto *owner=profile.assets.find(*externalContainerId);
            title=owner && std::holds_alternative<RaidCorpseContainerAssetLocation>(owner->location)?"CORPSE":inRaid?"SCENE STORAGE":"BASE GROUND CONTAINER";
        }
        text({808,64,190,24},title,16);
        if(externalContainerId && !inRaid)
            renderFoundationButton(3142,profileGroundContainerPickupRect(),"PICK UP CONTAINER",true);
    }
    renderProfileInventoryToolbar(includeStash,inRaid,externalContainerId);
    SDL_Rect originalClip{};const bool clipped=SDL_RenderClipEnabled(renderer_);SDL_GetRenderClipRect(renderer_,&originalClip);
    for(const auto &view:profileGridViews(profile,includeStash,externalContainerId,&profilePresentation_)) {
        if(view.clip) {
            SDL_Rect c{static_cast<int>(view.clip->x),static_cast<int>(view.clip->y),static_cast<int>(view.clip->w),static_cast<int>(view.clip->h)};
            SDL_SetRenderClipRect(renderer_,&c);
        }
        renderProfileGrid(view.container,view.x,view.y,view.cellSize,"");
        SDL_SetRenderClipRect(renderer_,clipped?&originalClip:nullptr);
    }
    const SDL_Rect personalClip{282,128,504,504};SDL_SetRenderClipRect(renderer_,&personalClip);
    for(const auto &g:layoutProfileCarriedGrids(profile,publishedContentRegistry(),profilePresentation_.chestScroll))
        text({g.bounds.x,g.bounds.y-22,500,20},g.label,14);
    SDL_SetRenderClipRect(renderer_,clipped?&originalClip:nullptr);
    if(!equippedAsset(profile,EquipmentSlotKind::ChestRig) && !equippedAsset(profile,EquipmentSlotKind::Backpack))
        text({282,146,504,26},"NO CARRIED CONTAINER",14);
    const int maxScroll=maximumProfileCarriedScroll(profile,publishedContentRegistry());
    profilePresentation_.chestScroll=std::clamp(profilePresentation_.chestScroll,0,maxScroll);
    if(maxScroll>0) {
        SDL_FRect rail{790,128,4,504};SDL_SetRenderDrawColor(renderer_,42,59,62,220);SDL_RenderFillRect(renderer_,&rail);
        const float height=std::max(40.0F,504.0F*504.0F/(504+maxScroll));
        SDL_FRect thumb{790,128+(504-height)*profilePresentation_.chestScroll/maxScroll,4,height};
        SDL_SetRenderDrawColor(renderer_,109,160,147,230);SDL_RenderFillRect(renderer_,&thumb);
    }
    if(!inRaid && !externalContainerId)renderFoundationButton(3140,profileBaseGroundDropRect(),"DROP TO BASE GROUND",true);
    text({282,646,504,22},"DRAG / R ROTATE / RMB DETAILS",12);
    if(!uiMessage_.empty() && !profilePresentation_.locateAsset)text({282,674,right?974.0F:504.0F,20},uiMessage_,12);
    renderProfileDragFeedback(includeStash,inRaid,externalContainerId);
    updateProfileItemContext(0);renderProfileItemCard(includeStash,inRaid);renderProfileContextMenu(inRaid);
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
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
        gameSession_.world().inOutdoorRaidSpace() &&
        gameSession_.world().raidBoundsObservable({{recovery->cachePosition.x-30,recovery->cachePosition.y-22},{60,44}}))
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
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *scene = std::get_if<RaidSceneContainerAssetLocation>(&asset.location);
        if (!scene || scene->spaceId != gameSession_.world().activeRaidSpaceId()) continue;
        if (!gameSession_.world().raidBoundsObservable({{scene->position.x-24,scene->position.y-18},{48,36}})) continue;
        const SDL_FRect box{scene->position.x - 24, scene->position.y - 18, 48, 36};
        SDL_SetRenderDrawColor(renderer_, 86, 76, 52, 255);
        SDL_RenderFillRect(renderer_, &box);
        SDL_SetRenderDrawColor(renderer_, 224, 195, 123, 255);
        SDL_RenderRect(renderer_, &box);
        SDL_RenderLine(renderer_, box.x, box.y + 12, box.x + box.w, box.y + 12);
        SDL_SetRenderDrawColor(renderer_, 244, 230, 180, 255);
        uiTextRenderer_.render(renderer_, box.x, box.y - 20,
            !inventoryOverlayState_.isOpen() && gameSession_.canOpenRaidSceneContainer(id) ? "F: OPEN STORAGE" : "SCENE STORAGE");
    }
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location);
        if (!corpse || !corpse->deathPosition || corpse->spaceId != gameSession_.world().activeRaidSpaceId()) continue;
        if (!gameSession_.world().raidBoundsObservable({{corpse->deathPosition->x-26,corpse->deathPosition->y-10},{52,20}})) continue;
        const auto p = *corpse->deathPosition;
        const SDL_FRect body{p.x - 26, p.y - 10, 52, 20};
        SDL_SetRenderDrawColor(renderer_, 86, 63, 58, 255);
        SDL_RenderFillRect(renderer_, &body);
        SDL_SetRenderDrawColor(renderer_, 191, 164, 150, 255);
        SDL_RenderRect(renderer_, &body);
        SDL_RenderLine(renderer_, p.x - 14, p.y, p.x + 14, p.y);
        const bool empty = assetsInContainer(profile, ProfileContainerId::compartment(id, 0)).empty();
        SDL_SetRenderDrawColor(renderer_, 244, 230, 180, 255);
        uiTextRenderer_.render(renderer_, body.x, body.y - 20,
            !inventoryOverlayState_.isOpen() && gameSession_.canOpenRaidSceneContainer(id) ? "F: SEARCH CORPSE" :
            empty ? "EMPTY CORPSE" : "SEARCHABLE CORPSE");
    }
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *drop=std::get_if<RaidDroppedBatteryLocation>(&asset.location);
        if (!drop || drop->spaceId!=gameSession_.world().activeRaidSpaceId() ||
            !gameSession_.world().raidBoundsObservable({{drop->position.x-12,drop->position.y-12},{24,24}})) continue;
        const SDL_FRect cell{drop->position.x-10,drop->position.y-7,20,14};
        SDL_SetRenderDrawColor(renderer_,160,154,86,255); SDL_RenderFillRect(renderer_,&cell);
        SDL_SetRenderDrawColor(renderer_,230,224,168,255); SDL_RenderRect(renderer_,&cell);
        uiTextRenderer_.render(renderer_,cell.x-12,cell.y-24,"USED BATTERY | F");
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

void App::renderBaseKitchenWater()
{
    if (!gameSession_.profile().basePriority.focus) baseWishFocusFilter_ = false;
    const SDL_FRect panel{40.0F, 70.0F, 1200.0F, 636.0F};
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

    const std::array<std::pair<SDL_FRect, const char *>, 3> focusControls{{
        {{80.0F, 626.0F, 148.0F, 30.0F}, "FOCUS SELECTED WISH"},
        {{236.0F, 626.0F, 148.0F, 30.0F}, "CLEAR WISH FOCUS"},
        {{392.0F, 626.0F, 148.0F, 30.0F}, baseWishFocusFilter_ ? "FILTER: FOCUSED STOCK" : "FILTER: ALL STOCK"}}};
    for (const auto &[bounds, label] : focusControls)
    {
        SDL_SetRenderDrawColor(renderer_, 38, 70, 63, 255);
        SDL_RenderFillRect(renderer_, &bounds);
        SDL_SetRenderDrawColor(renderer_, 174, 208, 170, 255);
        SDL_RenderRect(renderer_, &bounds);
        uiTextRenderer_.render(renderer_, bounds.x + 7.0F, bounds.y + 10.0F, label);
    }

    const BasePriorityState &priorityState =
        gameSession_.profile().basePriority;
    const std::vector<BasePriorityProjection> wishes =
        projectBasePriorities(
            gameSession_.profile(), publishedContentRegistry());
    if (selectedBasePriorityIndex_ >= wishes.size())
    {
        selectedBasePriorityIndex_ = 0U;
        selectedBasePriorityAssetIds_.clear();
    }
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
        "BASE WISHES - SELECT ONE, THEN SELECT CONTRIBUTION ITEMS");
    const std::string priorityTiming = fmt::format(
        "POPULATION {} FROZEN | {}H {:02}M LEFT | MISSED TOTAL {}",
        priorityState.frozenPopulation,
        remainingMinutes / kWorldMinutesPerHour,
        remainingMinutes % kWorldMinutesPerHour,
        priorityState.missedCycleCount);
    uiTextRenderer_.render(
        renderer_, 650.0F, 136.0F, priorityTiming.c_str());
    for (std::size_t index{}; index < wishes.size(); ++index)
    {
        const BasePriorityProjection &wish = wishes[index];
        const SDL_FRect row{
            650.0F,
            156.0F + static_cast<float>(index) * 44.0F,
            500.0F,
            38.0F};
        const bool selected = index == selectedBasePriorityIndex_;
        SDL_SetRenderDrawColor(
            renderer_,
            selected ? 52 : 30,
            selected ? 92 : 50,
            selected ? 76 : 46,
            255);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(
            renderer_, selected ? 224 : 104, selected ? 198 : 152,
            selected ? 116 : 112, 255);
        SDL_RenderRect(renderer_, &row);
        const std::string title = wish.displayName +
            (priorityState.focus && priorityState.focus->definitionId == wish.definitionId ? " | FOCUSED" : "");
        const std::string label = fmt::format(
            "{} | NEED {} | {}",
            baseSupplyCategoryName(wish.category),
            wish.requiredContribution,
            wish.fulfilled ? "FULFILLED" : "ACTIVE");
        uiTextRenderer_.render(renderer_, row.x + 10.0F, row.y + 4.0F, title.c_str());
        uiTextRenderer_.render(
            renderer_, row.x + 10.0F, row.y + 22.0F, label.c_str());
    }
    if (!wishes.empty() && selectedBasePriorityIndex_ < wishes.size())
    {
        const std::string hint = fmt::format(
            "SOURCE HINT: {}",
            wishes[selectedBasePriorityIndex_].sourceHint);
        uiTextRenderer_.render(
            renderer_, 650.0F, 286.0F, hint.c_str());
    }
    constexpr std::array<BaseSupplyCategory, 4> categories{
        BaseSupplyCategory::Food,
        BaseSupplyCategory::Medical,
        BaseSupplyCategory::Recreation,
        BaseSupplyCategory::Security};
    for (std::size_t index{}; index < categories.size(); ++index)
    {
        const SDL_FRect tab{
            650.0F + static_cast<float>(index) * 126.0F,
            300.0F,
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
        gameSession_.profile(), selectedBaseSupplyCategory_, baseWishFocusFilter_);
    constexpr std::size_t entriesPerPage{5U};
    const std::size_t pageCount = allocation.empty()
        ? 1U
        : 1U + (allocation.size() - 1U) / entriesPerPage;
    baseSupplyPage_ = std::min(baseSupplyPage_, pageCount - 1U);
    if (allocation.empty())
    {
        uiTextRenderer_.render(
            renderer_, 650.0F, 344.0F,
            "NO OWNED ITEMS CAN SUPPLY THIS CATEGORY");
    }
    const std::size_t first = baseSupplyPage_ * entriesPerPage;
    for (std::size_t visibleIndex{};
         visibleIndex < entriesPerPage &&
         first + visibleIndex < allocation.size();
         ++visibleIndex)
    {
        const std::size_t index = first + visibleIndex;
        const BaseSupplyDefinitionProjection &projection = allocation[index];
        const ItemDefinition &definition = *projection.definition;
        const SDL_FRect row{
            650.0F,
            344.0F + static_cast<float>(visibleIndex) * 40.0F,
            500.0F,
            34.0F};
        SDL_SetRenderDrawColor(
            renderer_,
            projection.assignedToSelectedCategory ? 42 : 34,
            projection.assignedToSelectedCategory ? 84 : 62,
            projection.assignedToSelectedCategory ? 68 : 54,
            255);
        SDL_RenderFillRect(renderer_, &row);
        const bool selected = !projection.assetIds.empty() &&
            std::all_of(
                projection.assetIds.begin(), projection.assetIds.end(),
                [this](AssetInstanceId id) {
                    return selectedBasePriorityAssetIds_.contains(id);
                });
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
            "{} x{} | +{} EACH | {}{}",
            definition.displayName,
            projection.ownedQuantity,
            baseSupplyContribution(
                definition, selectedBaseSupplyCategory_),
            selected ? "SELECTED FOR WISH" : "CLICK TO SELECT",
            assignment);
        uiTextRenderer_.render(
            renderer_, row.x + 38.0F, row.y + 13.0F, label.c_str());
    }
    const SDL_FRect previousPage{650.0F, 540.0F, 64.0F, 18.0F};
    const SDL_FRect nextPage{1086.0F, 540.0F, 64.0F, 18.0F};
    SDL_SetRenderDrawColor(renderer_, 34, 62, 54, 255);
    SDL_RenderFillRect(renderer_, &previousPage);
    SDL_RenderFillRect(renderer_, &nextPage);
    SDL_SetRenderDrawColor(renderer_, 120, 190, 162, 255);
    SDL_RenderRect(renderer_, &previousPage);
    SDL_RenderRect(renderer_, &nextPage);
    uiTextRenderer_.render(
        renderer_, previousPage.x + 26.0F, previousPage.y + 9.0F, "<");
    uiTextRenderer_.render(
        renderer_, nextPage.x + 26.0F, nextPage.y + 9.0F, ">");
    const std::string pageLabel = fmt::format(
        "PAGE {}/{}", baseSupplyPage_ + 1U, pageCount);
    uiTextRenderer_.render(
        renderer_, 870.0F, 549.0F, pageLabel.c_str());

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
    std::string priorityLabel = "SELECT AN ACTIVE BASE WISH";
    if (!wishes.empty() && selectedBasePriorityIndex_ < wishes.size())
    {
        const BasePriorityProjection &wish =
            wishes[selectedBasePriorityIndex_];
        const BasePriorityPlan plan = queryBasePrioritySubmission(
            gameSession_.profile(),
            publishedContentRegistry(),
            SubmitBasePriorityCommand{
                wish.definitionId,
                std::vector<AssetInstanceId>{
                    selectedBasePriorityAssetIds_.begin(),
                    selectedBasePriorityAssetIds_.end()}});
        canFulfillPriority = plan.canCommit;
        priorityLabel = wish.fulfilled
            ? "BASE WISH ALREADY FULFILLED"
            : fmt::format(
                "CONTRIBUTION {}/{} | EXCESS {} | {}",
                plan.totalContribution,
                wish.requiredContribution,
                plan.canCommit ? plan.excessContribution : 0U,
                plan.canCommit ? "FULFILL WISH" : "SELECT MORE ITEMS");
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
        priorityLabel.c_str());
    uiTextRenderer_.render(renderer_, 80.0F, 684.0F, "ESC CLOSE");
    if (!uiMessage_.empty())
    {
        uiTextRenderer_.render(renderer_, 250.0F, 684.0F, uiMessage_.c_str());
    }
}

void App::renderBase()
{
    renderBaseWorld();
    if (!baseConstructionPanelOpen_ && !basePlacementState_.has_value() &&
        !baseFixedFacilityPlacementState_.has_value())
        renderAimCrosshair();

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

    std::string goal{"OBJECTIVE COMPLETE"};
    switch (gameSession_.profile().tutorial)
    {
    case TutorialProgress::FindStorage:
        goal = "OBJECTIVE: FIND STORAGE";
        break;
    case TutorialProgress::PrepareLoadout:
    {
        const LoadoutReadinessProjection readiness = projectLoadoutReadiness(
            gameSession_.profile(),
            publishedContentRegistry(),
            LoadoutArchetypeDefinitionId{
                "loadout_archetype.light_scavenger"});
        std::string gaps;
        const auto appendGap = [&](const char *label)
        {
            if (!gaps.empty())
            {
                gaps += '/';
            }
            gaps += label;
        };
        for (const LoadoutReadinessIssue issue : readiness.issues)
        {
            switch (issue)
            {
            case LoadoutReadinessIssue::Weapon:
                appendGap("[WEAPON]");
                break;
            case LoadoutReadinessIssue::CompatibleAmmunition:
                appendGap("[AMMO]");
                break;
            case LoadoutReadinessIssue::BodyArmor:
                appendGap("[ARMOR]");
                break;
            case LoadoutReadinessIssue::ChestRig:
                appendGap("[RIG]");
                break;
            case LoadoutReadinessIssue::Backpack:
                appendGap("[PACK]");
                break;
            }
        }
        goal = fmt::format(
            "OBJECTIVE: PREPARE LIGHT LOADOUT | NEED {} | AMMO {}/{}",
            gaps,
            readiness.compatibleRoundCount,
            readiness.minimumCompatibleRounds);
        break;
    }
    case TutorialProgress::FindRaidGate:
        goal = "OBJECTIVE: FIND RAID GATE";
        break;
    case TutorialProgress::Complete:
        break;
    }
    const auto &founding = gameSession_.profile().homeFounding;
    if (!founding.established)
        goal = founding.hintsDismissed ? "" : "SURVEY THREE SITES | M MAP | E INSPECT | H HIDE HINTS";
    else if (!founding.plots.empty())
        goal.clear(); // The contextual first-Raid card owns new-profile guidance.
    uiTextRenderer_.render(renderer_, 48.0F, 54.0F, goal.c_str());
    if (!baseConstructionPanelOpen_ && !basePlacementState_.has_value() &&
        !baseFixedFacilityPlacementState_.has_value() &&
        !inventoryOverlayState_.isOpen() &&
        !gameFlow_.activeBaseFacility().has_value() && !tacticalMapOpen_)
    {
        uiTextRenderer_.render(
            renderer_, 48.0F, 676.0F,
            founding.established ? "B BASE BUILD | TAB INVENTORY | M MAP | ESC MENU"
                : "TAB INVENTORY | M MAP | E INSPECT | ESC MENU");
        if (!founding.established && !uiMessage_.empty())
            uiTextRenderer_.render(renderer_, 48.0F, 650.0F, uiMessage_.c_str());
    }

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
        case BaseFacilityKind::KitchenWater:
            renderBaseKitchenWater();
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
        renderProfileInventory(
            inventoryOverlayState_.showsExternalContainer() &&
                !openedBaseGroundContainerId_.has_value(),
            false,
            openedBaseGroundContainerId_);
    }
    renderHomeRegionMap();
    renderBaseConstructionPanel();
    renderBaseOperationNotices();
    renderBaseSiegeWarning();
    renderBaseDefenseHud();
    if (homeFoundingPrompt_)
    {
        const auto *plot = homePlotDefinition(*homeFoundingPrompt_);
        if (plot)
        {
            const SDL_FRect panel{310,220,660,255};
            SDL_SetRenderDrawColor(renderer_,24,30,26,255);
            SDL_RenderFillRect(renderer_,&panel);
            SDL_SetRenderDrawColor(renderer_,210,220,184,255);
            SDL_RenderRect(renderer_,&panel);
            uiTextRenderer_.render(renderer_,340,246,"ESTABLISH YOUR ONLY MAIN BASE HERE?");
            uiTextRenderer_.render(renderer_,340,281,plot->name.data());
            uiTextRenderer_.render(renderer_,340,311,plot->description.data());
            uiTextRenderer_.render(renderer_,340,350,"One core / one starter budget / existing items kept");
            const SDL_FRect confirm{370,405,240,42}, cancel{660,405,240,42};
            SDL_SetRenderDrawColor(renderer_,60,90,66,255); SDL_RenderFillRect(renderer_,&confirm);
            SDL_SetRenderDrawColor(renderer_,70,72,66,255); SDL_RenderFillRect(renderer_,&cancel);
            SDL_SetRenderDrawColor(renderer_,232,236,217,255);
            uiTextRenderer_.render(renderer_,390,418,"ENTER - ESTABLISH");
            uiTextRenderer_.render(renderer_,680,418,"ESC - KEEP LOOKING");
        }
    }
}

void App::renderBaseOperationNotices()
{
    if (baseOperationNotices_.empty())
        return;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    constexpr float x{820.0F};
    constexpr float y{142.0F};
    constexpr float width{420.0F};
    constexpr float rowHeight{42.0F};
    for (std::size_t index{}; index < baseOperationNotices_.size(); ++index)
    {
        const BaseOperationNotice &notice = baseOperationNotices_[index];
        const SDL_FRect row{
            x, y + static_cast<float>(index) * (rowHeight + 6.0F),
            width, rowHeight};
        SDL_SetRenderDrawColor(renderer_, 34, 31, 22, 232);
        SDL_RenderFillRect(renderer_, &row);
        SDL_SetRenderDrawColor(renderer_, 236, 184, 68, 255);
        SDL_RenderRect(renderer_, &row);

        const char *label = "FACILITY UPGRADE COMPLETE";
        switch (notice.kind)
        {
        case BaseOperationNoticeKind::FacilityUpgrade:
            break;
        case BaseOperationNoticeKind::Manufacturing:
            label = "MANUFACTURING COMPLETE | OUTPUT READY";
            break;
        case BaseOperationNoticeKind::ResidentTreatment:
            label = "RESIDENT TREATMENT COMPLETE";
            break;
        }
        const std::string text = fmt::format(
            "{} | {}", label, baseFacilityName(notice.facility));
        uiTextRenderer_.render(renderer_, row.x + 14.0F, row.y + 13.0F,
                               text.c_str());
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
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
    if (!threat.warningActive || gameSession_.baseDefenseActive())
    {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (!baseSiegeWarningVisible())
    {
        const auto banner = base_siege_warning_layout::banner;
        SDL_SetRenderDrawColor(renderer_, 65, 39, 28, 240);
        SDL_RenderFillRect(renderer_, &banner);
        SDL_SetRenderDrawColor(renderer_, 205, 122, 62, 255);
        SDL_RenderRect(renderer_, &banner);
        const auto label = fmt::format("SIEGE TIMER {:02}:{:02} | F6 / CLICK: DEFENSE OPTIONS",
            threat.warningRemainingSeconds / 60U, threat.warningRemainingSeconds % 60U);
        uiTextRenderer_.render(renderer_, banner.x + 12, banner.y + 12, label.c_str());
        return;
    }
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
    uiTextRenderer_.render(renderer_, 390.0F, 322.0F, requirement.c_str());
    const auto fortificationCost = base_siege_warning_layout::fortificationCostText(defense);
    uiTextRenderer_.render(renderer_, 390.0F, 348.0F, fortificationCost.c_str());
    uiTextRenderer_.render(
        renderer_, 390.0F, 375.0F,
        "SUCCESS: MATERIAL +8, MORALE SUPPORT, 7 SAFE DAYS");
    uiTextRenderer_.render(
        renderer_, 390.0F, 399.0F,
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
        renderer_, button.x + 16.0F, button.y + 18.0F,
        "START AUTO DEFENSE NOW");
    const SDL_FRect manual = base_siege_warning_layout::manual;
    SDL_SetRenderDrawColor(renderer_, 48, 78, 94, 255);
    SDL_RenderFillRect(renderer_, &manual);
    SDL_SetRenderDrawColor(renderer_, 220, 190, 120, 255);
    SDL_RenderRect(renderer_, &manual);
    uiTextRenderer_.render(renderer_, 674.0F, 492.0F, "DEFEND PERSONALLY");
    uiTextRenderer_.render(renderer_, 390.0F, 443.0F,
        "MANUAL: REAL AMMO / MEDICINE | NO SECURITY FEE");
    const auto close = base_siege_warning_layout::close;
    SDL_SetRenderDrawColor(renderer_, 92, 62, 48, 255);
    SDL_RenderFillRect(renderer_, &close);
    uiTextRenderer_.render(renderer_, close.x + 12, close.y + 8, "X");
    uiTextRenderer_.render(renderer_, 360, 564,
        "ESC / X: KEEP PREPARING | WARNING TIMER CONTINUES");
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
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer_, &chart);
            const auto screenRect = [chart, &interior](ContentRect rect)
            {
                return SDL_FRect{
                    chart.x + rect.position.x / interior->worldSize.x * chart.w,
                    chart.y + rect.position.y / interior->worldSize.y * chart.h,
                    rect.size.x / interior->worldSize.x * chart.w,
                    rect.size.y / interior->worldSize.y * chart.h};
            };
            const SDL_FRect visible = screenRect(interior->visibleBounds);
            const SDL_Rect mapClip{static_cast<int>(std::ceil(visible.x)),
                static_cast<int>(std::ceil(visible.y)),
                static_cast<int>(visible.w), static_cast<int>(visible.h)};
            SDL_SetRenderClipRect(renderer_, &mapClip);
            SDL_SetRenderDrawColor(renderer_, 31, 52, 52, 255);
            SDL_RenderFillRect(renderer_, &chart);
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

            SDL_SetRenderClipRect(renderer_, nullptr);

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
                "INTERIOR MAP | {} | {}",
                interior->displayName,
                interior->layoutKnown ? "PERMANENT INTELLIGENCE" : "LOCAL VIEW ONLY");
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

    const std::optional<RaidHighRiskCrisisProjection> crisis =
        gameSession_.raidHighRiskCrisisProjection();
    if (crisis.has_value() &&
        (crisis->active || developerCrisisRevealEnabled_))
    {
        const Vec2 targetCenterWorld{
            crisis->focusArea.position.x + crisis->focusArea.size.x * 0.5F,
            crisis->focusArea.position.y + crisis->focusArea.size.y * 0.5F};
        const Vec2 targetCenter = screenPoint(targetCenterWorld);
        SDL_SetRenderDrawColor(renderer_, 246, 92, 52, 205);
        for (std::size_t index{}; index < crisis->pressureSpawnCount; ++index)
        {
            const Vec2 source = screenPoint(crisis->pressureSpawnCenters[index]);
            SDL_RenderLine(
                renderer_, source.x, source.y,
                targetCenter.x, targetCenter.y);
            SDL_RenderLine(
                renderer_, source.x + 1.0F, source.y,
                targetCenter.x + 1.0F, targetCenter.y);
            const SDL_FRect sourceMarker{
                source.x - 4.0F, source.y - 4.0F, 8.0F, 8.0F};
            SDL_RenderRect(renderer_, &sourceMarker);
        }

        SDL_FRect crisisTarget = screenRect(crisis->focusArea);
        crisisTarget.w = std::max(crisisTarget.w, 18.0F);
        crisisTarget.h = std::max(crisisTarget.h, 18.0F);
        SDL_SetRenderDrawColor(renderer_, 126, 24, 16, 150);
        SDL_RenderFillRect(renderer_, &crisisTarget);
        SDL_SetRenderDrawColor(renderer_, 255, 164, 68, 250);
        SDL_RenderRect(renderer_, &crisisTarget);
        const SDL_FRect targetInset{
            crisisTarget.x + 3.0F,
            crisisTarget.y + 3.0F,
            std::max(0.0F, crisisTarget.w - 6.0F),
            std::max(0.0F, crisisTarget.h - 6.0F)};
        SDL_RenderRect(renderer_, &targetInset);
        uiTextRenderer_.render(
            renderer_, crisisTarget.x + 5.0F,
            crisisTarget.y + 4.0F,
            "CRISIS TARGET");
    }

    const auto wishHints = gameSession_.profile().pendingRaid ? projectBaseWishResourceHints(
        *gameSession_.profile().pendingRaid, publishedContentRegistry()) : std::vector<std::string>{};
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
        if (std::find(wishHints.begin(), wishHints.end(), resourcePoint.instanceId) != wishHints.end())
        {
            // Reuse the already-permitted resource marker, never a route or an actual item position.
            SDL_SetRenderDrawColor(renderer_, 234, 222, 140, 255);
            uiTextRenderer_.render(renderer_, marker.x + marker.w + 3.0F, marker.y, "WISH SOURCE?");
        }
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

    for (const RaidTacticalObjective &objective : map.objectives())
    {
        if (objective.kind == RaidTacticalObjectiveKind::Rescue &&
            (!gameSession_.profile().pendingRaid.has_value() ||
             !gameSession_.profile().pendingRaid->rescue.has_value() ||
             gameSession_.profile().pendingRaid->rescue->secured))
        {
            continue;
        }
        if (objective.kind == RaidTacticalObjectiveKind::SelfRecovery)
        {
            const auto recovery =
                gameSession_.raidSelfRecoveryProjection();
            if (!recovery.has_value() || recovery->opened)
                continue;
        }
        if (!tacticalMapObjectiveVisible(map, objective, presentationMode))
            continue;
        SDL_FRect marker = screenRect(objective.bounds);
        marker.w = std::max(marker.w, 8.0F);
        marker.h = std::max(marker.h, 8.0F);
        const char *label{};
        switch (objective.kind)
        {
        case RaidTacticalObjectiveKind::HighRiskControl:
            SDL_SetRenderDrawColor(renderer_, 224, 110, 72, 100);
            label = "HIGH-RISK CONTROL";
            break;
        case RaidTacticalObjectiveKind::Rescue:
            SDL_SetRenderDrawColor(renderer_, 86, 194, 178, 110);
            label = "RESCUE TARGET";
            break;
        case RaidTacticalObjectiveKind::SelfRecovery:
            SDL_SetRenderDrawColor(renderer_, 226, 188, 76, 115);
            label = "LOST CACHE";
            break;
        }
        SDL_RenderFillRect(renderer_, &marker);
        switch (objective.kind)
        {
        case RaidTacticalObjectiveKind::HighRiskControl:
            SDL_SetRenderDrawColor(renderer_, 242, 148, 96, 245);
            break;
        case RaidTacticalObjectiveKind::Rescue:
            SDL_SetRenderDrawColor(renderer_, 126, 234, 210, 245);
            break;
        case RaidTacticalObjectiveKind::SelfRecovery:
            SDL_SetRenderDrawColor(renderer_, 242, 214, 112, 245);
            break;
        }
        SDL_RenderRect(renderer_, &marker);
        uiTextRenderer_.render(
            renderer_, marker.x + 5.0F, marker.y + 4.0F, label);
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
    if (map.hasIntelligence(RaidIntelligenceCategory::Enemy) && gameSession_.profile().pendingRaid &&
        std::any_of(gameSession_.profile().pendingRaid->enemies.begin(), gameSession_.profile().pendingRaid->enemies.end(),
            [](const RaidEnemySnapshot &enemy) { return enemy.torsoArmor.has_value(); }))
        uiTextRenderer_.render(renderer_, 144, 640, "ENEMY INTEL: SOME RESOURCE GUARDS WEAR TORSO ARMOR");

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
    const RaidOperationProjection operation =
        gameSession_.raidOperationProjection();
    if (operation.basePerimeterSweepActive)
    {
        uiTextRenderer_.render(
            renderer_, 144.0F, 638.0F,
            operation.objectiveSecured
                ? "OPERATION | PERIMETER SWEEP | SECURED"
                : "OPERATION | PERIMETER SWEEP | ACTIVE");
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

bool App::ensureIndoorObservationTargets()
{
    if (!renderer_)
        return false;
    if (!indoorInformationLayer_.valid())
        indoorInformationLayer_ = Texture{SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
            kWindowWidth, kWindowHeight)};
    if (!indoorShadowMask_.valid())
        indoorShadowMask_ = Texture{SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
            kWindowWidth, kWindowHeight)};
    return indoorInformationLayer_.valid() && indoorShadowMask_.valid() &&
           SDL_SetTextureBlendMode(indoorInformationLayer_.get(),
                                   SDL_BLENDMODE_NONE) &&
           SDL_SetTextureBlendMode(indoorShadowMask_.get(),
                                   SDL_BLENDMODE_BLEND) &&
           SDL_SetTextureScaleMode(indoorInformationLayer_.get(),
                                   SDL_SCALEMODE_NEAREST);
}

bool App::renderIndoorInformationLayer(const SDL_Rect &worldViewport)
{
    if (!ensureIndoorObservationTargets())
        return false;
    SDL_Texture *originalTarget = SDL_GetRenderTarget(renderer_);
    SDL_Rect originalViewport{}, originalClip{};
    SDL_GetRenderViewport(renderer_, &originalViewport);
    const bool viewportSet = SDL_RenderViewportSet(renderer_);
    const bool clipSet = SDL_RenderClipEnabled(renderer_);
    SDL_GetRenderClipRect(renderer_, &originalClip);
    SDL_BlendMode originalBlend{};
    SDL_GetRenderDrawBlendMode(renderer_, &originalBlend);
    Uint8 r{}, g{}, b{}, a{};
    SDL_GetRenderDrawColor(renderer_, &r, &g, &b, &a);
    struct Restore
    {
        SDL_Renderer *renderer;
        SDL_Texture *target;
        SDL_Rect viewport, clip;
        bool viewportSet, clipSet;
        SDL_BlendMode blend;
        Uint8 r, g, b, a;
        ~Restore()
        {
            SDL_SetRenderTarget(renderer, target);
            SDL_SetRenderViewport(renderer, viewportSet ? &viewport : nullptr);
            SDL_SetRenderClipRect(renderer, clipSet ? &clip : nullptr);
            SDL_SetRenderDrawBlendMode(renderer, blend);
            SDL_SetRenderDrawColor(renderer, r, g, b, a);
        }
    } restore{renderer_,
              originalTarget,
              originalViewport,
              originalClip,
              viewportSet,
              clipSet,
              originalBlend,
              r,
              g,
              b,
              a};

    const auto &view = gameSession_.world().raidObservation();
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    const Rect screenWorld{
        {-static_cast<float>(worldViewport.x),
         -static_cast<float>(worldViewport.y)},
        {static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)}};
    if (view.valid)
        for (std::size_t i = 0; i < view.boundary.size(); ++i)
        {
            const auto polygon = clipRaidVisibilityTriangle(
                view.observer, view.boundary[i],
                view.boundary[(i + 1) % view.boundary.size()], screenWorld);
            if (polygon.size() < 3)
                continue;
            const int first = static_cast<int>(vertices.size());
            for (auto p : polygon)
            {
                const SDL_FPoint screen{
                    p.x + static_cast<float>(worldViewport.x),
                    p.y + static_cast<float>(worldViewport.y)};
                vertices.push_back(
                    {screen,
                     {1, 1, 1, 1},
                     {std::clamp(screen.x / static_cast<float>(kWindowWidth),
                                 0.0F, 1.0F),
                      std::clamp(screen.y / static_cast<float>(kWindowHeight),
                                 0.0F, 1.0F)}});
            }
            for (int j = 1; j + 1 < static_cast<int>(polygon.size()); ++j)
            {
                indices.push_back(first);
                indices.push_back(first + j);
                indices.push_back(first + j + 1);
            }
        }
    if (!SDL_SetRenderTarget(renderer_, indoorShadowMask_.get()))
        return false;
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    auto maskVertices = vertices;
    for (auto &vertex : maskVertices)
        vertex.color = {0, 0, 0, 0};
    if (!indices.empty() &&
        !SDL_RenderGeometry(renderer_, nullptr, maskVertices.data(),
                            static_cast<int>(maskVertices.size()),
                            indices.data(), static_cast<int>(indices.size())))
        return false;

    if (!SDL_SetRenderTarget(renderer_, indoorInformationLayer_.get()))
        return false;
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    SDL_SetRenderViewport(renderer_, &worldViewport);
    // An opaque copy of the original background preserves straight-alpha
    // sprites/effects exactly; clipping the finished pixels avoids blending
    // their alpha a second time and works on the software renderer too.
    renderBackground();
    renderExtractionPoint();
    renderRaidSpacePortal();
    renderBallisticBlockers();
    renderAlphaRaidLoot();
    renderEnemyAttackTelegraphs();
    renderEnemies();
    renderShotPresentations();
    renderShotFeedbackPresentations();
    renderParticles();
    renderNoiseDecoyWorld();

    if (!SDL_SetRenderTarget(renderer_, originalTarget))
        return false;
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_Rect screenClip = originalClip;
    screenClip.x += originalViewport.x;
    screenClip.y += originalViewport.y;
    SDL_SetRenderClipRect(renderer_, clipSet ? &screenClip : nullptr);
    SDL_SetTextureAlphaMod(indoorShadowMask_.get(), 64);
    if (!SDL_RenderTexture(renderer_, indoorShadowMask_.get(), nullptr,
                           nullptr))
        return false;
    // This light is drawn onto background pixels before copying any entities.
    // Its separate occluded fan must never grant observation or interaction.
    if (gameSession_.world().weaponLightBackgroundRadius()>0)
    {
        std::vector<SDL_Vertex> nearVertices;
        for (const auto &near : gameSession_.world().weaponLightBackgroundObservations())
            for (std::size_t i=0; near.valid && i<near.boundary.size(); ++i)
            {
                const auto polygon=clipRaidVisibilityTriangle(near.observer,near.boundary[i],near.boundary[(i+1)%near.boundary.size()],screenWorld);
                for (std::size_t j=1; j+1<polygon.size(); ++j) for (const auto p : {polygon[0],polygon[j],polygon[j+1]})
                {
                    const float fade=std::clamp(1.0F-std::hypot(p.x-near.observer.x,p.y-near.observer.y)/gameSession_.world().weaponLightBackgroundRadius(),0.0F,1.0F);
                    nearVertices.push_back({{p.x+worldViewport.x,p.y+worldViewport.y},{0.09F,0.08F,0.04F,fade},{0,0}});
                }
            }
        SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_ADD);
        if (!nearVertices.empty()) SDL_RenderGeometry(renderer_,nullptr,nearVertices.data(),static_cast<int>(nearVertices.size()),nullptr,0);
        SDL_SetRenderDrawBlendMode(renderer_,originalBlend);
    }
    if (!indices.empty() &&
        !SDL_RenderGeometry(renderer_, indoorInformationLayer_.get(),
                            vertices.data(), static_cast<int>(vertices.size()),
                            indices.data(), static_cast<int>(indices.size())))
        return false;
    // Static portal labels are information about the layout, not hidden actors.
    SDL_SetRenderViewport(renderer_, &worldViewport);
    SDL_SetRenderClipRect(renderer_, clipSet ? &originalClip : nullptr);
    renderRaidSpacePortal();
    return true;
}

void App::renderRaidScreen()
{
    bool wheelBackdrop=medicalWheelOpen_ || throwableWheelOpen_;
    if(wheelBackdrop && !quickWheelBackdrop_.begin(renderer_)) {
        medicalWheelOpen_=false;medicalWheelOptions_.clear();cancelThrowableUi();
        uiMessage_="QUICK WHEEL BACKDROP UNAVAILABLE";wheelBackdrop=false;
        SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Quick wheel backdrop: %s",SDL_GetError());
    }
    const Vec2 cameraOffset = raidWorldCameraOffset();
    const Vec2 shakePixels = raidWorldScreenShakePixels();
    const Vec2 activeWorldSize = gameSession_.world().raidSpaceWorldSize();
    const SDL_Rect worldViewport{
        static_cast<int>(std::lround(shakePixels.x - cameraOffset.x)),
        static_cast<int>(std::lround(shakePixels.y - cameraOffset.y)),
        static_cast<int>(std::ceil(
            std::max(activeWorldSize.x, static_cast<float>(kWindowWidth)))),
        static_cast<int>(std::ceil(
            std::max(activeWorldSize.y, static_cast<float>(kWindowHeight))))};
    const bool worldIsTranslated = worldViewport.x != 0 || worldViewport.y != 0;
    if (worldIsTranslated)
    {
        // Keep only the inexpensive ground layer underneath camera and
        // screen-shake translation so viewport edges never expose black
        // strips. Outdoor details are queried and drawn once in world space.
        renderBackground(false);
        static_cast<void>(SDL_SetRenderViewport(renderer_, &worldViewport));
    }
    renderBackground();
    renderExtractionPoint();
    renderRaidSpacePortal();
    renderBallisticBlockers();
    if (!gameSession_.world().isAlphaRaidWorld())
    {
        renderStorageCabinet();
    }

    const bool indoorVisibility = gameSession_.world().isAlphaRaidWorld() &&
        (gameSession_.world().lightConeObservationEnabled() || !gameSession_.world().inOutdoorRaidSpace());
    if (indoorVisibility)
    {
        if (!renderIndoorInformationLayer(worldViewport))
        {
            uiMessage_ = "RAID OBSERVATION UNAVAILABLE";
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "Indoor observation failed: %s", SDL_GetError());
        }
    }
    else
    {
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
    renderNoiseDecoyWorld();
    }

    if (indoorVisibility) renderPlayer(); // Own avatar is never hidden by its observation cone.
    renderMeleeSwing(gameSession_.world().melee());

    // Crosshair and every UI/modal layer stay in stable screen coordinates;
    // screen shake therefore cannot alter aiming or inventory interaction.
    if (worldIsTranslated)
    {
        static_cast<void>(SDL_SetRenderViewport(renderer_, nullptr));
    }
    if(wheelBackdrop) {
        if(!quickWheelBackdrop_.compose()) {
            medicalWheelOpen_=false;medicalWheelOptions_.clear();cancelThrowableUi();
            uiMessage_="QUICK WHEEL BACKDROP UNAVAILABLE";
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,"Quick wheel filter: %s",SDL_GetError());
        } else {
            renderMedicalWheel();
            if(throwableWheelOpen_)renderQuickWheel(UiQuickWheelKind::Throwable,throwableWheelOptions_,throwableWheelSelected_);
        }
        return;
    }
    renderAimCrosshair();
    renderCombatFeedback();
    renderMeleeStatus();
    const auto light=gameSession_.activeWeaponLight();
    if (light.binding.deviceAssetId && !inventoryOverlayState_.isOpen())
    {
        SDL_SetRenderDrawColor(renderer_,234,225,169,255);
        uiTextRenderer_.render(renderer_,16,620,
            fmt::format("{} | BATTERY CHARGE {}% | U",lightStateLabel(light),
                light.capacityMs ? static_cast<std::uint64_t>(light.remainingMs)*100/light.capacityMs : 0).c_str());
    }
    if (!inventoryOverlayState_.isOpen() && (gameSession_.persistenceMessage()=="ONLY WEAPON LIGHT | TAP U" ||
        gameSession_.persistenceMessage()=="NO ACTIVE WEAPON LIGHT"))
        uiTextRenderer_.render(renderer_,16,644,gameSession_.persistenceMessage().c_str());
    renderRaidTacticalMap();

    // 背包覆盖层显示在游戏世界上方。
    renderInventoryOverlay();
    renderMedicalWheel();
    renderThrowableUi();

    if (gameSession_.world().raidSession().isTerminal() ||
        gameFlow_.state() == GameFlowState::RaidResult)
    {
        renderStashOverlay();
    }

    // 常驻调试信息位于世界 UI 上方；模态面板由 render() 统一绘制。
    renderDebugText();
}

void App::render()
{
    int outputWidth=1280,outputHeight=720;
    SDL_GetRenderOutputSize(renderer_,&outputWidth,&outputHeight);
    uiTextRenderer_.setRasterScale(std::min(outputWidth/1280.0F,outputHeight/720.0F));
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
        if (uiMessage_ == "hint acknowledgement save failed")
        {
            SDL_SetRenderDrawColor(renderer_, 244, 204, 156, 255);
            uiTextRenderer_.render(renderer_, 410.0F, 478.0F, uiMessage_.c_str());
        }
        break;
    }

    if(!medicalWheelOpen_ && !throwableWheelOpen_) {
    renderFirstRaidHints();
    renderPauseMenu();
    renderDeveloperPerformanceOverlay();
    renderDeveloperWeaponPanel();
    renderDeveloperRangePanel();
    }

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
    quickWheelBackdrop_.reset();
    indoorInformationLayer_.reset();
    indoorShadowMask_.reset();
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



namespace
{
std::vector<AssetInstanceId> detailBatteries(const ProfileState &profile, bool inRaid)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
        if (std::holds_alternative<StoredAssetLocation>(asset.location) && publishedContentRegistry().item(asset.definitionId).battery &&
            (inRaid ? assetIsCarried(profile,id) : assetIsBaseAccessible(profile,id))) result.push_back(id);
    return result;
}
const char *lightStateLabel(const WeaponLightProjection &light)
{
    if (!light.binding.deviceAssetId) return "NO WEAPON LIGHT INSTALLED";
    if (!light.remainingMs) return "LIGHT EMPTY";
    if (!light.switchOn) return "WEAPON LIGHT OFF";
    if (light.interrupted || light.flickering) return "LIGHT INTERRUPTED | SHAKE TO RESTORE";
    return light.output ? "WEAPON LIGHT ON" : "LIGHT OUTPUT PAUSED";
}
}

void App::handleWeaponLightDetailsPointer(const InventoryPointerEvent &event, bool inRaid)
{
    if (!profileDetailsAsset_) return;
    if(const auto *a=gameSession_.profile().assets.find(*profileDetailsAsset_);a && (publishedContentRegistry().item(a->definitionId).meleeUse || publishedContentRegistry().item(a->definitionId).noiseDecoyUse))return;
    deviceBatteryDragPosition_=event.position;
    const auto light=findWeaponLight(gameSession_.profile(),publishedContentRegistry(),*profileDetailsAsset_);
    const auto batteries=detailBatteries(gameSession_.profile(),inRaid);
    if (event.type==InventoryPointerEventType::LeftButtonDown)
    {
        deviceBatteryDrag_.reset();
        if (contains(SDL_FRect{70,602,100,32},event.position)) { if (deviceBatteryPage_) --deviceBatteryPage_; return; }
        if (contains(SDL_FRect{190,602,100,32},event.position)) { if ((deviceBatteryPage_+1)*10<batteries.size()) ++deviceBatteryPage_; return; }
        if (light.deviceAssetId && contains(SDL_FRect{640,420,150,30},event.position))
        {
            const auto r=gameSession_.changeDeviceBattery({light.deviceAssetId,0},nextProfileTransactionId("remove-battery"));
            uiMessage_=r.succeeded ? "BATTERY CHANGE COMPLETE" : r.message;
            input_.suppressPrimaryPointerUntilRelease(); return;
        }
        if (!light.deviceAssetId) return;
        for (std::size_t slot=0; slot<10; ++slot)
        {
            const auto index=deviceBatteryPage_*10+slot;
            if (index<batteries.size() && contains(SDL_FRect{70+110.0F*slot,490,104,70},event.position))
            {
                const auto *asset=gameSession_.profile().assets.find(batteries[index]);
                deviceBatteryDrag_=ProfileDragSource{asset->instanceId,gameSession_.profile().revision,asset->location,1,asset->orientation};
                input_.suppressPrimaryPointerUntilRelease(); return;
            }
        }
    }
    if (event.type==InventoryPointerEventType::LeftButtonUp && deviceBatteryDrag_)
    {
        const auto source=*deviceBatteryDrag_; deviceBatteryDrag_.reset();
        if (light.deviceAssetId && contains(SDL_FRect{1090,350,96,76},event.position))
        {
            if (!profileDragSourceMatches(gameSession_.profile(),source)) { uiMessage_="BATTERY SOURCE CHANGED"; return; }
            const auto r=gameSession_.changeDeviceBattery({light.deviceAssetId,source.instanceId},nextProfileTransactionId("replace-battery"));
            uiMessage_=r.succeeded ? "BATTERY CHANGE COMPLETE" : r.message;
        }
        input_.suppressPrimaryPointerUntilRelease();
    }
}

void App::renderProfileWeaponDetails()
{
    const auto &profile = gameSession_.profile();
    const auto *asset = profile.assets.find(*profileDetailsAsset_);
    if (!asset) { profileDetailsAsset_.reset(); deviceBatteryDrag_.reset(); return; }
    const auto &content = publishedContentRegistry();
    const auto &definition = content.item(asset->definitionId);
    if(definition.meleeUse){renderProfileMeleeDetails(*asset,definition);return;}
    if(definition.noiseDecoyUse){renderProfileDecoyDetails(*asset,definition);return;}
    const auto weapon = effectiveWeaponUse(profile, content, asset->instanceId);
    const SDL_FRect panel{40, 70, 1200, 600};
    SDL_SetRenderDrawColor(renderer_, 24, 29, 30, 250);
    SDL_RenderFillRect(renderer_, &panel);
    const auto text = [&](float x, float y, const std::string &label) {
        SDL_SetRenderDrawColor(renderer_,232,225,201,255);
        uiTextRenderer_.render(renderer_, x, y, label.c_str());
    };
    text(70, 100, "WEAPON DETAILS");
    const SDL_FRect back{860, 92, 340, 32};
    SDL_SetRenderDrawColor(renderer_, 43, 60, 65, 255);
    SDL_RenderFillRect(renderer_, &back);
    text(866, 100, "BACK TO INVENTORY");
    text(70, 155, definition.displayName);
    text(70, 200, fmt::format("RECOIL CONTROL {}", weapon.recoilControl));
    text(70, 235, fmt::format("STABILITY {}", weapon.stability));
    text(70, 270, fmt::format("HANDLING {}", weapon.handlingSpeed));
    text(70, 305, fmt::format("ERGONOMICS {}", weapon.ergonomics));
    text(70, 340, fmt::format("ACCURACY {}", weapon.accuracy));
    text(70, 375, fmt::format("EFFECTIVE RANGE {:.0f}", weapon.effectiveRange));
    text(640, 155, "INSTALLED COMPONENTS");
    float y = 200;
    for (const auto &[id, part] : profile.assets.records())
        if (const auto *location = std::get_if<InstalledWeaponComponentLocation>(&part.location);
            location && location->weaponAssetId == asset->instanceId)
        { text(640, y, content.item(part.definitionId).displayName); y += 35; }
    if (y == 200) text(640, y, "DEFAULT CONFIGURATION");
    const bool inRaid=gameSession_.alphaRaidActive();
    const auto binding=findWeaponLight(profile,content,asset->instanceId);
    const auto active=gameSession_.activeWeaponLight();
    const auto light=queryWeaponLight(profile,content,binding,inRaid && active.binding.weaponAssetId==asset->instanceId && active.output);
    text(640,350,"WEAPON LIGHT");
    text(640,380,lightStateLabel(light));
    text(640,398,fmt::format("BATTERY CHARGE {}%",light.capacityMs ? static_cast<std::uint64_t>(light.remainingMs)*100/light.capacityMs : 0));
    const SDL_FRect remove{640,420,150,30};
    SDL_SetRenderDrawColor(renderer_,43,60,65,255); SDL_RenderFillRect(renderer_,&remove); text(646,425,"REMOVE BATTERY");
    const SDL_FRect batterySlot{1090,350,96,76};
    SDL_SetRenderDrawColor(renderer_,binding.deviceAssetId ? 80 : 43,76,45,255); SDL_RenderFillRect(renderer_,&batterySlot);
    SDL_SetRenderDrawColor(renderer_,230,220,166,255); SDL_RenderRect(renderer_,&batterySlot); text(1096,372,"BATTERY SLOT");
    text(70,455,"DRAG ONE BATTERY INTO LIGHT");
    const auto batteries=detailBatteries(profile,inRaid);
    if (deviceBatteryPage_*10>=batteries.size()) deviceBatteryPage_=0;
    for (std::size_t slot=0; slot<10 && deviceBatteryPage_*10+slot<batteries.size(); ++slot)
    {
        const auto &cell=*profile.assets.find(batteries[deviceBatteryPage_*10+slot]);
        const auto capacity=content.item(cell.definitionId).maximumCharges;
        const SDL_FRect bounds{70+110.0F*slot,490,104,70};
        SDL_SetRenderDrawColor(renderer_,73,73,48,255); SDL_RenderFillRect(renderer_,&bounds);
        SDL_SetRenderDrawColor(renderer_,225,219,170,255); SDL_RenderRect(renderer_,&bounds);
        text(bounds.x+5,bounds.y+8,fmt::format("BATTERY X{}",cell.quantity));
        text(bounds.x+5,bounds.y+38,fmt::format("{}%",static_cast<std::uint64_t>(cell.remainingCharges)*100/capacity));
    }
    text(70,608,"PREVIOUS PAGE"); text(190,608,"NEXT PAGE");
    text(330,608,fmt::format("PAGE {} / {}",deviceBatteryPage_+1,std::max<std::size_t>(1,(batteries.size()+9)/10)));
    if (deviceBatteryDrag_)
    {
        const SDL_FRect ghost{deviceBatteryDragPosition_.x-16,deviceBatteryDragPosition_.y-12,32,24};
        SDL_SetRenderDrawColor(renderer_,230,220,140,210); SDL_RenderFillRect(renderer_,&ghost);
    }
    text(70,646,uiMessage_);
}

namespace
{
SDL_FRect personalInventoryTabRect(int tab)
{
    return {42.0F + tab * 192.0F, 94, 184, 32};
}
SDL_FRect inventoryPreviousPageRect() { return {808, 94, 64, 26}; }
SDL_FRect inventoryNextPageRect() { return {1192, 94, 64, 26}; }
SDL_FRect inventoryReturnsRect() { return {1004, 64, 120, 24}; }
SDL_FRect inventoryLegacyOverflowRect() { return {1132, 64, 124, 24}; }
SDL_FRect inventoryLocateRect() { return {808, 602, 448, 26}; }
SDL_FRect inventoryReturnViewRect() { return {1080, 94, 104, 26}; }
} // namespace

void App::locateProfileInventoryAsset(AssetInstanceId id, bool includeStash,
                                      std::optional<AssetInstanceId> external)
{
    const auto &profile = gameSession_.profile();
    const auto *asset = profile.assets.find(id);
    if (!asset)
        return;
    const auto *stored = std::get_if<StoredAssetLocation>(&asset->location);
    if (!stored)
        return;
    for(const auto &grid:layoutProfileCarriedGrids(profile,publishedContentRegistry(),0))
        if(grid.container==stored->container) {
            const auto f=inventoryFootprint(publishedContentRegistry().item(asset->definitionId),asset->orientation);
            const float top=grid.bounds.y+stored->origin.y*kProfileItemCell;
            const float bottom=top+f.height*kProfileItemCell;
            if(top-profilePresentation_.chestScroll<128)profilePresentation_.chestScroll=static_cast<int>(top-128);
            else if(bottom-profilePresentation_.chestScroll>632)profilePresentation_.chestScroll=static_cast<int>(bottom-632);
            profilePresentation_.chestScroll=std::clamp(profilePresentation_.chestScroll,0,maximumProfileCarriedScroll(profile,publishedContentRegistry()));
            clearProfileItemContext();return;
        }
    for (const auto &view : profileGridViews(profile, includeStash, external,
                                             &profilePresentation_))
    {
        if (view.container != stored->container || !view.viewport)
            continue;
        auto viewport = *view.viewport;
        const auto footprint = inventoryFootprint(
            publishedContentRegistry().item(asset->definitionId),
            asset->orientation);
        const auto bounds = viewport.bounds(stored->origin, footprint);
        const auto clip = viewport.clip();
        if (bounds.x >= clip.x && bounds.y >= clip.y &&
            bounds.x + bounds.width <= clip.x + clip.width &&
            bounds.y + bounds.height <= clip.y + clip.height)
            return;
        if (viewport.isolatedPages)
            profilePresentation_.returnContainer.reset();
        else
        {
            profilePresentation_.returnContainer = view.container;
            profilePresentation_.returnOrigin = viewport.origin;
        }
        viewport.reveal(stored->origin, footprint);
        profilePresentation_.remember(view.container, viewport.origin);
        clearProfileItemContext();
        return;
    }
}

void App::describeProfileInventoryDestination(AssetInstanceId id,
                                              StoredAssetLocation location)
{
    const auto &profile = gameSession_.profile();
    const auto *asset = profile.assets.find(id);
    if (asset)
    {
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset->location))
            location = *stored;
    }
    else if (const auto target =
                 profileAssetAtCell(profile, publishedContentRegistry(),
                                    location.container, location.origin))
        id = *target;
    else
        return;
    profilePresentation_.locateAsset = id;
    const auto size = profileContainerSize(profile, publishedContentRegistry(),
                                           location.container);
    const bool personal =
        location.container.kind == ProfileContainerKind::AssetCompartment &&
        (equippedAsset(profile, EquipmentSlotKind::ChestRig) ==
             location.container.ownerAssetId ||
         equippedAsset(profile, EquipmentSlotKind::Backpack) ==
             location.container.ownerAssetId);
    const auto width = personal ? 9 : 8;
    const int page =
        location.origin.y / 8 * ((size.width + width - 1) / width) +
        location.origin.x / width + 1;
    const char *container =
        location.container.kind == ProfileContainerKind::Stash ? "STASH"
        : location.container.kind == ProfileContainerKind::LegacyOverflow
            ? "LEGACY ITEMS"
        : location.container.kind == ProfileContainerKind::BaseIntake
            ? "UNASSIGNED RETURNS"
        : equippedAsset(profile, EquipmentSlotKind::Backpack) ==
                location.container.ownerAssetId
            ? "BACKPACK"
        : equippedAsset(profile, EquipmentSlotKind::ChestRig) ==
                location.container.ownerAssetId
            ? "CHEST RIG"
            : "COMPARTMENT";
    uiMessage_ = fmt::format("{} | PAGE {} | LOCATE", container, page);
}

bool App::handleProfilePresentationEvent(
    const InventoryUiEvent &event, bool includeStash,
    std::optional<AssetInstanceId> external)
{
    const auto &profile = gameSession_.profile();
    const auto *pointer = std::get_if<InventoryPointerEvent>(&event);
    const auto *navigation = std::get_if<InventoryNavigationEvent>(&event);
    if (pointer)
        profilePresentationPointer_ = pointer->position;
    if (navigation)
        profilePresentationPointer_ = navigation->position;
    const bool press =
        pointer && pointer->type == InventoryPointerEventType::LeftButtonDown;
    const auto point = profilePresentationPointer_;
    const auto refresh = [&]
    {
        clearProfileItemContext();
        const auto visual = profileInventoryInteraction_.activeDragVisual();
        const GridPosition offset =
            visual ? GridPosition{static_cast<int>(visual->grabOffsetInCells.x),
                                  static_cast<int>(visual->grabOffsetInCells.y)}
                   : GridPosition{};
        const auto source = profileInventoryInteraction_.source();
        const bool raid = gameFlow_.state() == GameFlowState::Raid;
        profileInventoryInteraction_.updatePointerPosition(
            point,
            profileDropTargetAt(
                profile, point, includeStash, !raid && !external, offset,
                source ? std::optional<AssetInstanceId>{source->instanceId}
                       : std::nullopt,
                external, &profilePresentation_));
    };
    if (press && !profileInventoryInteraction_.pointerGestureActive())
    {
        if (includeStash && contains(inventoryReturnsRect(), point) &&
            !assetsInContainer(profile, ProfileContainerId::baseIntake())
                 .empty())
        {
            profilePresentation_.legacyOverflow = false;
            profilePresentation_.returns = !profilePresentation_.returns;
            refresh();
            return true;
        }
        if (includeStash && contains(inventoryLegacyOverflowRect(), point) &&
            !assetsInContainer(profile, ProfileContainerId::legacyOverflow())
                 .empty())
        {
            profilePresentation_.returns = false;
            profilePresentation_.legacyOverflow =
                !profilePresentation_.legacyOverflow;
            refresh();
            return true;
        }
        if (external && contains(SDL_FRect{1004, 64, 136, 24}, point))
            if (const auto *owner = profile.assets.find(*external))
            {
                const auto count = publishedContentRegistry()
                                       .item(owner->definitionId)
                                       .containerCompartments.size();
                if (count > 1)
                {
                    profilePresentation_.externalCompartment =
                        (profilePresentation_.externalCompartment + 1) % count;
                    refresh();
                    return true;
                }
            }
        if (profilePresentation_.returnContainer &&
            contains(inventoryReturnViewRect(), point))
        {
            profilePresentation_.remember(*profilePresentation_.returnContainer,
                                          profilePresentation_.returnOrigin);
            profilePresentation_.returnContainer.reset();
            refresh();
            return true;
        }
        if (profilePresentation_.locateAsset &&
            contains(inventoryLocateRect(), point))
        {
            const auto id = *profilePresentation_.locateAsset;
            if (const auto *asset = profile.assets.find(id))
                if (const auto *stored =
                        std::get_if<StoredAssetLocation>(&asset->location))
                {
                    const auto container = stored->container;
                    if (container.kind == ProfileContainerKind::Stash ||
                        container.kind == ProfileContainerKind::BaseIntake ||
                        container.kind == ProfileContainerKind::LegacyOverflow)
                    {
                        if (!includeStash)
                            return true;
                        profilePresentation_.returns =
                            container.kind == ProfileContainerKind::BaseIntake;
                        profilePresentation_.legacyOverflow =
                            container.kind == ProfileContainerKind::LegacyOverflow;
                    }
                    else if (external == container.ownerAssetId)
                        profilePresentation_.externalCompartment = container.compartmentIndex;
                    else if(equippedAsset(profile,EquipmentSlotKind::Backpack)!=container.ownerAssetId &&
                            equippedAsset(profile,EquipmentSlotKind::ChestRig)!=container.ownerAssetId)
                        return true;
                    locateProfileInventoryAsset(id, includeStash, external);
                    refresh();
                    return true;
                }
            profilePresentation_.locateAsset.reset();
            return true;
        }
    }
    const auto views = profileGridViews(profile, includeStash, external,
                                        &profilePresentation_);
    for (const auto &view : views)
        if (view.clip && contains(*view.clip, point))
        {
            profilePresentation_.lastTarget = view.container;
            break;
        }
    int pageDelta = navigation ? navigation->pageDelta : 0;
    const bool restorePage = press && (includeStash || external) &&
                             contains(SDL_FRect{880, 94, 188, 26}, point);
    if (press && (includeStash || external))
    {
        if (contains(inventoryPreviousPageRect(), point))
            pageDelta = -1;
        if (contains(inventoryNextPageRect(), point))
            pageDelta = 1;
        // Explicit page buttons select the external/right side.
        if (pageDelta || restorePage)
            for (const auto &v : views)
                if (v.clip && v.clip->x >= 808)
                    profilePresentation_.lastTarget = v.container;
    }
    if (!navigation && !pageDelta && !restorePage)
        return false;
    if (navigation && !navigation->pageDelta && kProfileCarriedClip.contains(point))
    {
        profilePresentation_.chestScroll = std::clamp(
            profilePresentation_.chestScroll + navigation->rows * static_cast<int>(kProfileItemCell),
            0,maximumProfileCarriedScroll(profile,publishedContentRegistry()));
        refresh();
        return true;
    }
    const ProfileGridView *selected = nullptr;
    for (const auto &view : views)
        if (profilePresentation_.lastTarget == view.container)
            selected = &view;
    if (!selected)
        for (const auto &view : views)
            if (view.clip && view.clip->x >= 808)
            {
                selected = &view;
                break;
            }
    if (selected && selected->viewport)
    {
        auto viewport = *selected->viewport;
        if (pageDelta || restorePage)
            viewport.selectPage(viewport.page() + pageDelta);
        else
            viewport.pan(navigation->columns, navigation->rows);
        profilePresentation_.remember(selected->container, viewport.origin);
        profilePresentation_.returnContainer.reset();
        refresh();
    }
    return true;
}

void App::updateProfilePresentationHover(float deltaTime)
{
    if (!inventoryOverlayState_.isOpen() || inventoryWeaponComponentsOpen_ ||
        profileDetailsAsset_ ||
        !profileInventoryInteraction_.activeDragVisual())
    {
        profileHoverTab_ = -1;
        profileHoverSeconds_ = 0;
        return;
    }
    int tab = -1;
    const bool raid = gameFlow_.state() == GameFlowState::Raid;
    const auto external =
        raid ? openedRaidSceneContainerId_ : openedBaseGroundContainerId_;
    if (!raid && !external && inventoryOverlayState_.showsExternalContainer() &&
        !assetsInContainer(gameSession_.profile(),
                           ProfileContainerId::baseIntake())
             .empty() &&
        contains(inventoryReturnsRect(), profilePresentationPointer_))
        tab = 3;
    if (!raid && !external && inventoryOverlayState_.showsExternalContainer() &&
        !assetsInContainer(gameSession_.profile(),
                           ProfileContainerId::legacyOverflow())
             .empty() &&
        contains(inventoryLegacyOverflowRect(), profilePresentationPointer_))
        tab = 5;
    if (external &&
        contains(SDL_FRect{1004, 64, 136, 24}, profilePresentationPointer_))
        if (const auto *owner = gameSession_.profile().assets.find(*external);
            owner && publishedContentRegistry()
                             .item(owner->definitionId)
                             .containerCompartments.size() > 1)
            tab = 4;
    if (tab < 0 ||
        (tab < 3 && tab == static_cast<int>(profilePresentation_.tab)))
    {
        profileHoverTab_ = -1;
        profileHoverSeconds_ = 0;
        return;
    }
    if (profileHoverTab_ != tab)
    {
        profileHoverTab_ = tab;
        profileHoverSeconds_ = 0;
    }
    if (profileHoverSeconds_ < 0)
        return;
    profileHoverSeconds_ += std::max(0.0F, deltaTime);
    if (profileHoverSeconds_ >= 0.35F)
    {
        if (tab < 3)
            profilePresentation_.tab = static_cast<ProfileInventoryTab>(tab);
        else if (tab == 3)
        {
            profilePresentation_.legacyOverflow = false;
            profilePresentation_.returns = !profilePresentation_.returns;
        }
        else if (tab == 5)
        {
            profilePresentation_.returns = false;
            profilePresentation_.legacyOverflow =
                !profilePresentation_.legacyOverflow;
        }
        else if (external)
        {
            const auto *owner = gameSession_.profile().assets.find(*external);
            const auto count = publishedContentRegistry()
                                   .item(owner->definitionId)
                                   .containerCompartments.size();
            profilePresentation_.externalCompartment =
                (profilePresentation_.externalCompartment + 1) % count;
        }
        profileHoverSeconds_ = -1; // One switch per hover; leaving re-arms it.
        clearProfileItemContext();
        handleProfileInventoryUiEvent(
            InventoryPointerEvent{InventoryPointerEventType::Motion,
                                  profilePresentationPointer_},
            raid);
    }
}

void App::renderProfileInventoryToolbar(bool includeStash, bool inRaid,
                                        std::optional<AssetInstanceId> external)
{
    const auto &profile = gameSession_.profile();
    if (includeStash && !inRaid &&
        !assetsInContainer(profile, ProfileContainerId::baseIntake()).empty())
        renderFoundationButton(
            3110, inventoryReturnsRect(),
            profilePresentation_.returns ? "STASH" : "UNASSIGNED RETURNS", true,
            false, profilePresentation_.returns);
    if (includeStash && !inRaid &&
        !assetsInContainer(profile, ProfileContainerId::legacyOverflow()).empty())
        renderFoundationButton(
            3112, inventoryLegacyOverflowRect(),
            profilePresentation_.legacyOverflow ? "RETURN TO STASH"
                                               : "LEGACY ITEMS",
            true, false, profilePresentation_.legacyOverflow,
            "LEGACY ITEMS (WITHDRAW ONLY)");
    if (external)
        if (const auto *owner = profile.assets.find(*external))
        {
            const auto count = publishedContentRegistry()
                                   .item(owner->definitionId)
                                   .containerCompartments.size();
            renderFoundationButton(
                3111, {1004, 64, 136, 24},
                fmt::format("COMPARTMENT {}/{}",
                            profilePresentation_.externalCompartment + 1,
                            count),
                count > 1);
        }
    for (const auto &view : profileGridViews(profile, includeStash, external,
                                             &profilePresentation_))
    {
        if (!view.viewport || !view.clip || view.clip->x < 808)
            continue;
        const auto &v = *view.viewport;
        renderFoundationButton(3120, inventoryPreviousPageRect(), "‹",
                               v.page() > 0);
        renderFoundationButton(3121, inventoryNextPageRect(), "›",
                               v.page() + 1 < v.pageCount());
        const bool locating = !v.isolatedPages &&
                              (v.origin.x % v.window.width != 0 ||
                               v.origin.y % v.window.height != 0);
        const auto text =
            fmt::format("{} / {}{}", v.page() + 1, v.pageCount(),
                        locating ? " | LOCATING" : "");
        if (!v.isolatedPages)
            renderFoundationButton(3123, {880, 94, 188, 26}, "", true, false,
                                   locating, "RESTORE REGULAR PAGE");
        SDL_SetRenderDrawColor(renderer_, 235, 242, 240, 255);
        uiTextRenderer_.renderBox(renderer_, {886, 97, 176, 22}, text,
                                  {14, UiTextAlign::Center, 1});
        if (!v.isolatedPages &&
            profilePresentation_.returnContainer == view.container)
            renderFoundationButton(3122, inventoryReturnViewRect(),
                                   "RETURN TO PAGE");
    }
    if (profilePresentation_.locateAsset)
        renderFoundationButton(3130, inventoryLocateRect(),
                               uiMessage_.empty() ? "LOCATE ITEM" : uiMessage_);
    if (includeStash || external)
    {
        const bool warehouse = includeStash &&
            !(profilePresentation_.returns &&
              !assetsInContainer(profile, ProfileContainerId::baseIntake()).empty()) &&
            !(profilePresentation_.legacyOverflow &&
              !assetsInContainer(profile, ProfileContainerId::legacyOverflow()).empty());
        uiTextRenderer_.renderBox(
            renderer_, {808, 642, 448, 19},
            warehouse ? "PGUP / PGDN" : "WHEEL / PGUP / PGDN",
            {13, UiTextAlign::Left, 1});
    }
}

namespace {
bool sameItemContextView(const ProfileInventoryPresentation &a,
                         const ProfileInventoryPresentation &b) {
    return a.tab == b.tab && a.returns == b.returns &&
        a.legacyOverflow == b.legacyOverflow &&
        a.externalCompartment == b.externalCompartment &&
        a.chestScroll == b.chestScroll && a.positions.size() == b.positions.size() &&
        std::equal(a.positions.begin(), a.positions.end(), b.positions.begin(),
            [](const auto &x, const auto &y) {
                return x.container == y.container && x.origin == y.origin;
            });
}
struct ItemContextRenderState {
    SDL_Renderer *renderer;
    SDL_BlendMode blend{};
    Uint8 r{}, g{}, b{}, a{};
    float x{1}, y{1};
    bool clipped{};
    SDL_Rect clip{};
    explicit ItemContextRenderState(SDL_Renderer *value) : renderer(value) {
        SDL_GetRenderDrawBlendMode(renderer, &blend);
        SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
        SDL_GetRenderScale(renderer, &x, &y);
        clipped = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderClipRect(renderer, &clip);
    }
    ~ItemContextRenderState() {
        SDL_SetRenderScale(renderer, x, y);
        SDL_SetRenderClipRect(renderer, clipped ? &clip : nullptr);
        SDL_SetRenderDrawBlendMode(renderer, blend);
        SDL_SetRenderDrawColor(renderer, r, g, b, a);
    }
};
}

void App::clearProfileItemHover() noexcept {
    profileItemHover_ = {};
    profileItemHoverSource_.reset();
    profileItemLastPointer_.reset();
    profileItemView_.reset();
    profileItemProfile_.clear();
    profileItemCardValid_ = false;
}
void App::clearProfileItemContext() noexcept {
    if (profileContextMenu_ && profileContextMenu_->press.held) {
        profileMenuReleaseGuard_ = true;
        input_.suppressPrimaryPointerUntilRelease();
    }
    profileContextMenu_.reset();
    clearProfileItemHover();
}
bool App::profileContextMenuValid(bool inRaid) const {
    if (!profileContextMenu_) return false;
    const auto &menu = *profileContextMenu_;
    const auto *asset = gameSession_.profile().assets.find(menu.instanceId);
    return menu.inRaid == inRaid &&
        asset && asset->orientation == menu.source.orientation &&
        menu.profileId == gameSession_.profile().profileId &&
        menu.language == uiTextRenderer_.language() &&
        profileDragSourceMatches(gameSession_.profile(), menu.source) &&
        windowHasInputFocus_ && uiPointerInside_;
}
void App::handleProfileContextMenuPointer(const InventoryPointerEvent &event,
                                         bool inRaid) {
    if (!profileContextMenuValid(inRaid)) {
        if (event.type == InventoryPointerEventType::LeftButtonDown)
            profileMenuReleaseGuard_ = true;
        if (event.type == InventoryPointerEventType::LeftButtonUp && profileContextMenu_)
            profileContextMenu_->press = {};
        clearProfileItemContext();
        if (event.type == InventoryPointerEventType::LeftButtonUp)
            profileMenuReleaseGuard_ = false;
        return;
    }
    const auto row = profileContextMenu_->layout.rowAt({event.position.x, event.position.y});
    const auto action = row ? std::optional{profileContextMenu_->actions[*row].id} : std::nullopt;
    if (event.type == InventoryPointerEventType::LeftButtonDown) {
        profileContextMenu_->press.down(action);
        input_.suppressPrimaryPointerUntilRelease();
        if (!action && !contains(profileContextMenu_->layout.panel, event.position))
            clearProfileItemContext();
    } else if (event.type == InventoryPointerEventType::LeftButtonUp) {
        handleProfileContextMenuClick(event.position, inRaid);
    }
}
void App::handleProfileContextMenuClick(MousePosition position, bool inRaid) {
    if (!profileContextMenuValid(inRaid)) {
        if (profileContextMenu_) profileContextMenu_->press = {};
        clearProfileItemContext(); profileMenuReleaseGuard_ = false; return;
    }
    const auto row = profileContextMenu_->layout.rowAt({position.x, position.y});
    const auto released = row ? std::optional{profileContextMenu_->actions[*row].id} : std::nullopt;
    const auto chosen = profileContextMenu_->press.up(released);
    if (!chosen) return;
    const AssetInstanceId id = profileContextMenu_->instanceId;
    const auto sourcePoint = profileContextMenu_->position;
    const auto external = inRaid ? openedRaidSceneContainerId_ : openedBaseGroundContainerId_;
    const bool stash = !inRaid && inventoryOverlayState_.showsExternalContainer() && !external;
    const auto hit = profileAssetHitAt(gameSession_.profile(), sourcePoint, stash,
                                     external, &profilePresentation_);
    const auto actions = projectUiItemActions(gameSession_.profile(),
        publishedContentRegistry(), id, inRaid);
    const bool stillAvailable = hit && hit->asset && hit->asset->instanceId == id &&
        std::any_of(actions.begin(), actions.end(),
            [&](const auto &entry) { return entry.id == *chosen; });
    clearProfileItemContext();
    input_.suppressPrimaryPointerUntilRelease();
    if (!stillAvailable) { uiMessage_ = "ITEM ACTION NO LONGER AVAILABLE"; return; }
    profileInventoryInteraction_.reset();
    uiMessage_.clear();
    switch (*chosen) {
    case UiItemAction::WeaponDetails:
    case UiItemAction::MeleeDetails:
    case UiItemAction::DecoyDetails:
        profileDetailsAsset_ = id;
        return;
    case UiItemAction::ModifyWeapon: {
        const auto &profile = gameSession_.profile();
        if ((inRaid && !gameSession_.developerRangeAtConsole()) ||
            profile.pendingRaid || profile.activeBaseDefense) {
            uiMessage_ = "MODIFICATION REQUIRES BASE"; return;
        }
        if (!openWeaponComponentsUi(id)) { uiMessage_ = "UNSUPPORTED WEAPON PLATFORM"; return; }
        inventoryWeaponComponentsOpen_ = true;
        return;
    }
    case UiItemAction::UnloadMagazine:
    case UiItemAction::UseMedical:
    case UiItemAction::ChamberRound:
        executeProfileContextAction(id, inRaid);
        return;
    }
}

void App::updateProfileItemContext(float seconds) {
    const bool raid = gameFlow_.state() == GameFlowState::Raid;
    const bool active = inventoryOverlayState_.isOpen() ||
        (gameFlow_.state() == GameFlowState::Base &&
         gameFlow_.activeBaseFacility() == BaseFacilityKind::Storage);
    if (!active || !windowHasInputFocus_ || !uiPointerInside_ || pauseMenu_.isOpen() ||
        inventoryWeaponComponentsOpen_ || profileDetailsAsset_ ||
        developerRangePanelOpen_ || developerWeaponPanelOpen_ || tacticalMapOpen_) {
        clearProfileItemContext();
        if ((!active && !uiPointerPressed_) || !windowHasInputFocus_ || !uiPointerInside_)
            profileMenuReleaseGuard_ = false;
        return;
    }
    if (profileContextMenu_) {
        if (!profileContextMenuValid(raid)) clearProfileItemContext();
        clearProfileItemHover();
        return;
    }
    if (profileInventoryInteraction_.pointerGestureActive() || profileMenuReleaseGuard_ ||
        !pointerWorldPosition_) { clearProfileItemHover(); return; }
    const auto &profile = gameSession_.profile();
    const auto external = raid ? openedRaidSceneContainerId_ : openedBaseGroundContainerId_;
    const bool stash = !raid && inventoryOverlayState_.showsExternalContainer() && !external;
    const bool contextChanged = !profileItemView_ ||
        !sameItemContextView(*profileItemView_, profilePresentation_) ||
        profileItemProfile_ != profile.profileId ||
        profileItemLanguage_ != uiTextRenderer_.language() ||
        profileItemExternal_ != external || profileItemIncludesStash_ != stash ||
        profileItemInRaid_ != raid;
    if (contextChanged) {
        clearProfileItemHover();
        profileItemView_ = profilePresentation_;
        profileItemProfile_ = profile.profileId;
        profileItemLanguage_ = uiTextRenderer_.language();
        profileItemExternal_ = external;
        profileItemIncludesStash_ = stash; profileItemInRaid_ = raid;
    }
    const auto point = *pointerWorldPosition_;
    const bool changed = !profileItemLastPointer_ ||
        profileItemLastPointer_->x != point.x || profileItemLastPointer_->y != point.y ||
        profileItemHitRevision_ != profile.revision;
    if (changed) {
        // Hit-test only on actual pointer/content/view changes. A stationary
        // hover does not rescan the warehouse or rebuild its information.
        const auto hit = profileAssetHitAt(profile, {point.x, point.y}, stash,
                                          external, &profilePresentation_);
        const auto id = hit && hit->asset ? hit->asset->instanceId : 0;
        if (id != profileItemHover_.instanceId ||
            (profileItemHoverSource_ && !profileDragSourceMatches(profile, *profileItemHoverSource_))) {
            profileItemHover_ = {};
            profileItemCardValid_ = false;
        }
        profileItemHover_.update(id, hit ? hit->bounds : SDL_FRect{}, 0);
        profileItemHoverSource_ = id ? std::optional{ProfileDragSource{
            id, profile.revision, hit->asset->location, 0, hit->asset->orientation}} : std::nullopt;
        profileItemLastPointer_ = point;
        profileItemHitRevision_ = profile.revision;
    }
    profileItemHover_.update(profileItemHover_.instanceId, profileItemHover_.anchor, seconds);
}

void App::renderProfileItemCard(bool includeStash, bool inRaid) {
    if (!renderer_ || !profileItemHover_.visible() || profileContextMenu_ ||
        !profileItemHoverSource_ ||
        !profileDragSourceMatches(gameSession_.profile(), *profileItemHoverSource_)) return;
    const auto &profile = gameSession_.profile();
    const auto *asset = profile.assets.find(profileItemHover_.instanceId);
    if (!asset) return;
    if (!profileItemCardValid_ || profileItemCardRevision_ != profile.revision ||
        profileItemCardWeaponSlot_ != gameSession_.activeAlphaWeaponSlot()) {
        const auto &definition = publishedContentRegistry().item(asset->definitionId);
        const auto summary = projectUiItemSummary(*asset, definition);
        const auto supply = definition.weaponUse
            ? weaponSupplyTooltipLines(asset->instanceId, includeStash, inRaid)
            : std::vector<std::string>{};
        profileItemCard_ = layoutUiItemCard(profileItemHover_.anchor,
            definition.displayName, summary, supply, uiTextRenderer_.language(),
            [&](auto text, int size) { return uiTextRenderer_.measure(text, size); });
        profileItemCardRevision_ = profile.revision;
        profileItemCardWeaponSlot_ = gameSession_.activeAlphaWeaponSlot();
        profileItemCardValid_ = true;
    }
    const ItemContextRenderState saved(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    // Fade the surface only: text colour stays stable and its cached textures
    // do not get regenerated once per animation frame.
    SDL_SetRenderDrawColor(renderer_, 18, 24, 33,
        static_cast<Uint8>(210 + 40 * profileItemHover_.fade()));
    SDL_RenderFillRect(renderer_, &profileItemCard_.panel);
    SDL_SetRenderDrawColor(renderer_, 113, 191, 179, 255);
    SDL_RenderRect(renderer_, &profileItemCard_.panel);
    renderProfileAsset(*asset, profileItemCard_.image, 64, 255, false);
    for (std::size_t i = 0; i < profileItemCard_.text.size(); ++i) {
        const auto &block = profileItemCard_.text[i];
        SDL_SetRenderDrawColor(renderer_, i == 0 ? 232 : 200,
            i == 0 ? 237 : 211, i == 0 ? 242 : 220, 255);
        uiTextRenderer_.renderBox(renderer_, block.bounds, block.text,
                                  {block.size, UiTextAlign::Left, 0});
    }
}

void App::renderProfileContextMenu(bool inRaid) {
    if (!renderer_ || !profileContextMenu_) return;
    if (!profileContextMenuValid(inRaid)) { clearProfileItemContext(); return; }
    const ItemContextRenderState saved(renderer_);
    auto &menu = *profileContextMenu_;
    const auto row = menu.layout.rowAt({profilePresentationPointer_.x, profilePresentationPointer_.y});
    if (row != menu.hoveredRow) { menu.hoveredRow = row; menu.hoverStarted = uiClockSeconds_; }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 18, 24, 33, 250);
    SDL_RenderFillRect(renderer_, &menu.layout.panel);
    SDL_SetRenderDrawColor(renderer_, 62, 77, 94, 255);
    SDL_RenderRect(renderer_, &menu.layout.panel);
    SDL_SetRenderDrawColor(renderer_, 232, 237, 242, 255);
    uiTextRenderer_.renderBox(renderer_, menu.layout.title.bounds, menu.layout.title.text,
                              {18, UiTextAlign::Left, 0});
    for (std::size_t i = 0; i < menu.layout.rows.size(); ++i) {
        const auto &block = menu.layout.rows[i];
        const bool hovered = row == i;
        const bool pressed = menu.press.held && menu.press.action == menu.actions[i].id;
        const float amount = hovered ? uiHoverAmount(uiClockSeconds_ - menu.hoverStarted) : 0;
        SDL_SetRenderDrawColor(renderer_, pressed ? 25 : static_cast<Uint8>(37 + 12 * amount),
            pressed ? 36 : static_cast<Uint8>(50 + 15 * amount),
            pressed ? 46 : static_cast<Uint8>(65 + 17 * amount), 255);
        SDL_RenderFillRect(renderer_, &block.bounds);
        if (hovered) {
            SDL_SetRenderDrawColor(renderer_, 113, 191, 179, 255);
            SDL_RenderRect(renderer_, &block.bounds);
        }
        SDL_SetRenderDrawColor(renderer_, 232, 237, 242, 255);
        const float zoom = 1 + .025F * amount;
        SDL_SetRenderScale(renderer_, saved.x * zoom, saved.y * zoom);
        uiTextRenderer_.renderBox(renderer_,
            {(block.bounds.x + 12) / zoom, (block.bounds.y + 8) / zoom,
             (block.bounds.w - 24) / zoom, (block.bounds.h - 16) / zoom},
            block.text, {18, UiTextAlign::Left, 0});
        SDL_SetRenderScale(renderer_, saved.x, saved.y);
    }
}
