#include "ui_base_medical_presentation.h"

#include <algorithm>
#include <fmt/format.h>
#include <tuple>

UiBaseMedicalProjection projectUiBaseMedical(const ProfileState &p, const ContentRegistry &c, UiLanguage language) {
    UiBaseMedicalProjection result;
    result.profileId = p.profileId; result.revision = p.revision;
    result.worldMinute = p.worldClock.elapsedWorldMinutes;
    result.currency = p.currency; result.level = p.baseConstruction.medicalLevel;
    result.health = p.currentHealth; result.medical = p.medicalStatus;
    result.player = queryBaseMedicalService(p, c);
    result.resident = queryStartResidentTreatment(p, c);
    result.population = projectBaseResidentMedical(p);
    result.workforce = projectBaseWorkforce(p);
    result.reservePolicy = p.residentMedicalReservePolicy;
    result.staffing = result.workforce.medicalWorker
        ? queryClearBaseWorker(p, {BaseFacilityStaffingKind::Medical})
        : queryAssignBestBaseWorker(p, {BaseFacilityStaffingKind::Medical});
    result.activeTreatment = p.residentMedical.activeTreatment;
    for (const auto &project : c.baseConstructionProjects()) {
        if (project.target != BaseFacilityUpgradeTarget::Medical) continue;
        result.upgradeId = project.id;
        result.cancelUpgrade = p.baseConstruction.activeProject && p.baseConstruction.activeProject->definitionId == project.id;
        result.upgradeComplete = p.baseConstruction.medicalLevel >= project.targetLevel;
        result.upgrade = result.cancelUpgrade
            ? queryCancelBaseConstruction(p, c, {project.id})
            : queryStartBaseConstruction(p, c, {project.id});
        if (result.cancelUpgrade) result.activeUpgrade = p.baseConstruction.activeProject;
        break;
    }
    for (const auto &s : result.resident.supplies) {
        const auto it = std::find_if(result.supplies.begin(), result.supplies.end(), [&](const auto &row) { return row.definitionId == s.definitionId; });
        if (it != result.supplies.end())
        {
            it->quantity += s.quantity;
            it->charges += s.charges;
        }
        else result.supplies.push_back({s.definitionId,
            localizeUiText(language, c.item(s.definitionId).displayName), s.quantity, s.charges});
    }
    for (const auto &[id, asset] : p.assets.records())
    {
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (!stored || !isMedicalReserveItem(asset.definitionId)) continue;
        const bool inWarehouse = stored->container == ProfileContainerId::stash();
        const bool inReserve = stored->container == ProfileContainerId::medicalReserve();
        if (!inWarehouse && !inReserve) continue;
        const auto &item = c.item(asset.definitionId);
        (inWarehouse ? result.warehouseMedicine : result.reserveMedicine).push_back({
            id, *stored, inventoryFootprint(item, asset.orientation),
            localizeUiText(language, item.displayName), asset.remainingCharges});
    }
    const auto warehouseOrder = [](const UiBaseMedicalAsset &item)
    {
        const auto [x, y] = item.location.origin;
        return std::tuple{y / 8, x / 8, y, x, item.id};
    };
    std::ranges::sort(result.warehouseMedicine, {}, warehouseOrder);
    return result;
}

std::string uiBaseMedicalActionKey(UiBaseMedicalControl control, UiBaseMedicalTab tab, const UiBaseMedicalProjection &p) {
    std::string key = fmt::format("{}:{}", p.profileId, static_cast<int>(control));
    if (control == UiBaseMedicalControl::Main) {
        // The caller binds the selected tab too. Do not bind elapsed time or
        // unrelated revisions: ordinary world-clock ticks do not change a quote.
        if (tab == UiBaseMedicalTab::Personal) {
            key += fmt::format(":{}:{}:{}:{}:{}:{}:{}", p.currency, p.health,
                static_cast<int>(p.medical.bleeding), p.player.canCommit, p.player.quotedCurrency,
                p.player.healthCost, p.player.injuryCost);
        } else {
            key += fmt::format(":{}:{}:{}:{}:{}:{}:{}", p.resident.canCommit, p.resident.durationMinutes,
                p.activeTreatment ? p.activeTreatment->jobId : 0, p.resident.requiredContribution,
                p.resident.plannedContribution, static_cast<int>(p.resident.patientProfession), static_cast<int>(p.resident.workerProfession));
            for (const auto &s : p.resident.supplies)
                key += fmt::format(":{}:{}:{}:{}:{}", s.assetId, s.definitionId.value(), s.quantity, s.contribution, s.charges);
        }
    } else if (control == UiBaseMedicalControl::Staffing) {
        const auto &s = p.staffing.resultingState;
        key += fmt::format(":{}:{}:{}:{}", p.staffing.canCommit, p.workforce.medicalWorker ? static_cast<int>(*p.workforce.medicalWorker) : -1,
            s.medicalWorker ? static_cast<int>(*s.medicalWorker) : -1,
            s.workshopWorker ? static_cast<int>(*s.workshopWorker) : -1);
    } else if (control == UiBaseMedicalControl::Upgrade) {
        key += fmt::format(":{}:{}:{}:{}:{}:{}:{}:{}", p.upgradeId ? p.upgradeId->value() : "", p.cancelUpgrade,
            p.upgrade.canCommit, p.upgradeComplete, p.upgrade.materialCost, p.upgrade.workerCount,
            p.upgrade.durationMinutes, p.upgrade.targetLevel);
        if (p.activeUpgrade) key += fmt::format(":{}:{}:{}:{}", p.activeUpgrade->startedWorldMinute,
            p.activeUpgrade->completionWorldMinute, p.activeUpgrade->lockedMaterialUnits, p.activeUpgrade->committedWorkers);
    }
    return key;
}
bool uiBaseMedicalEnabled(UiBaseMedicalControl control, UiBaseMedicalTab tab, const UiBaseMedicalProjection &p) {
    switch (control) {
    case UiBaseMedicalControl::Main: return tab == UiBaseMedicalTab::Personal ? p.player.canCommit : p.resident.canCommit;
    case UiBaseMedicalControl::Staffing: return p.staffing.canCommit;
    case UiBaseMedicalControl::Upgrade: return p.upgradeId && p.upgrade.canCommit;
    default: return true;
    }
}
std::string uiBaseMedicalDuration(std::uint64_t minutes, UiLanguage language) {
    return fmt::format("{}{} {}{}", minutes / 60, (language == UiLanguage::English ? "h" : "小时"), minutes % 60, (language == UiLanguage::English ? "m" : "分"));
}
SDL_FRect uiBaseMedicalControlRect(UiBaseMedicalControl control, UiBaseMedicalTab tab) noexcept {
    switch (control) {
    case UiBaseMedicalControl::Personal: return {64, 132, 200, 34};
    case UiBaseMedicalControl::Residents: return {276, 132, 200, 34};
    case UiBaseMedicalControl::Details:
        return tab == UiBaseMedicalTab::Personal ? SDL_FRect{464, 306, 236, 32} : SDL_FRect{80, 394, 328, 32};
    case UiBaseMedicalControl::Facilities: return {64, 552, 360, 40};
    case UiBaseMedicalControl::Staffing: return {80, 382, 328, 30};
    case UiBaseMedicalControl::Upgrade: return {80, 422, 328, 30};
    case UiBaseMedicalControl::Main: return {916, 552, 300, 40};
    case UiBaseMedicalControl::Close: return {1084, 80, 132, 34};
    case UiBaseMedicalControl::PreviousWarehouse: return {720, 432, 52, 30};
    case UiBaseMedicalControl::NextWarehouse: return {1140, 432, 52, 30};
    case UiBaseMedicalControl::ReserveTransfer: return {916, 498, 276, 32};
    case UiBaseMedicalControl::Priority: return {444, 552, 452, 40};
    case UiBaseMedicalControl::AutoAdmission: return {880, 208, 312, 36};
    case UiBaseMedicalControl::WarehouseRow:
    case UiBaseMedicalControl::ReserveCell:
    case UiBaseMedicalControl::MoveEarlier:
    case UiBaseMedicalControl::MoveLater:
        break;
    }
    return {};
}
SDL_FRect uiBaseMedicalDetailsRect(UiBaseMedicalTab tab) noexcept {
    return tab == UiBaseMedicalTab::Personal ? SDL_FRect{464, 350, 728, 148} : SDL_FRect{80, 434, 328, 94};
}
SDL_FRect uiBaseMedicalFeedbackRect() noexcept { return {76, 616, 1128, 44}; }
SDL_FRect uiBaseMedicalReserveRect() noexcept { return {464, 234, 224, 224}; }
SDL_FRect uiBaseMedicalWarehouseRowRect(std::size_t index) noexcept {
    return {720, 234 + 32.0F * static_cast<float>(index), 472, 30};
}
SDL_FRect uiBaseMedicalPriorityButtonRect(UiBaseMedicalControl control, std::size_t index) noexcept {
    return {control == UiBaseMedicalControl::MoveEarlier ? 952.0F : 1076.0F,
        262.0F + 48.0F * static_cast<float>(index), 116, 34};
}
GridPosition uiBaseMedicalReserveCell(std::size_t index) noexcept {
    const auto width = static_cast<std::size_t>(kMedicalReserveGridSize.width);
    return {static_cast<int>(index % width), static_cast<int>(index / width)};
}
std::size_t uiBaseMedicalWarehousePageCount(const UiBaseMedicalProjection &p) noexcept {
    return std::max<std::size_t>(1, (p.warehouseMedicine.size() + kUiBaseMedicalWarehouseRows - 1) / kUiBaseMedicalWarehouseRows);
}
const UiBaseMedicalAsset *uiBaseMedicalAsset(const UiBaseMedicalProjection &p,
    std::optional<AssetInstanceId> id) noexcept
{
    if (!id) return nullptr;
    const auto byId = [id](const auto &asset) { return asset.id == *id; };
    const auto reserve = std::ranges::find_if(p.reserveMedicine, byId);
    if (reserve != p.reserveMedicine.end()) return &*reserve;
    const auto warehouse = std::ranges::find_if(p.warehouseMedicine, byId);
    return warehouse != p.warehouseMedicine.end() ? &*warehouse : nullptr;
}
const UiBaseMedicalAsset *uiBaseMedicalReserveAssetAt(
    const UiBaseMedicalProjection &p, GridPosition cell) noexcept
{
    const auto item = std::ranges::find_if(p.reserveMedicine, [cell](const auto &asset) {
        const auto [x, y] = asset.location.origin;
        return cell.x >= x && cell.y >= y && cell.x < x + asset.footprint.width &&
            cell.y < y + asset.footprint.height;
    });
    return item != p.reserveMedicine.end() ? &*item : nullptr;
}
UiBaseMedicalTransferProjection projectUiBaseMedicalTransfer(
    const ProfileState &p, const ContentRegistry &c, const UiBaseMedicalState &state,
    const UiBaseMedicalProjection &projection)
{
    const auto *row = uiBaseMedicalAsset(projection, state.selectedMedicine);
    if (!row) return {};
    const auto *asset = p.assets.find(row->id);
    if (!asset) return {};
    const bool withdrawing = row->location.container == ProfileContainerId::medicalReserve();
    const auto target = withdrawing ? ProfileContainerId::stash() : ProfileContainerId::medicalReserve();
    auto position = !withdrawing ? state.reserveDestination : std::nullopt;
    if (!position)
        position = findFirstProfileFit(p, c, target, c.item(asset->definitionId), asset->orientation);
    if (!position)
        return {std::nullopt, withdrawing ? "WAREHOUSE IS FULL - MAKE ROOM TO WITHDRAW" :
            "MEDICINE CABINET IS FULL - MAKE ROOM TO DEPOSIT", false};
    const MoveMedicalReserveAssetCommand command{row->id, row->location, {target, *position}};
    const auto plan = queryMoveMedicalReserveAsset(p, c, command);
    return {command, plan.message, plan.canCommit};
}
std::optional<UiBaseMedicalHit> hitUiBaseMedical(Vec2 position, const UiBaseMedicalState &state) noexcept {
    using Control = UiBaseMedicalControl;
    for (auto c : {Control::Personal, Control::Residents, Control::Main, Control::Close})
        if (uiContains(uiBaseMedicalControlRect(c, state.tab), position)) return UiBaseMedicalHit{c};
    if (state.tab == UiBaseMedicalTab::Personal || !state.facilities)
        if (uiContains(uiBaseMedicalControlRect(Control::Details, state.tab), position))
            return UiBaseMedicalHit{Control::Details};
    if (state.tab != UiBaseMedicalTab::Residents) return std::nullopt;
    for (auto c : {Control::Facilities, Control::Priority})
        if (uiContains(uiBaseMedicalControlRect(c), position)) return UiBaseMedicalHit{c};
    if (state.facilities)
        for (auto c : {Control::Staffing, Control::Upgrade})
            if (uiContains(uiBaseMedicalControlRect(c), position)) return UiBaseMedicalHit{c};
    if (state.priorityOpen) {
        if (uiContains(uiBaseMedicalControlRect(Control::AutoAdmission), position))
            return UiBaseMedicalHit{Control::AutoAdmission};
        for (std::size_t i = 0; i < kBaseResidentProfessionCount; ++i)
            for (auto c : {Control::MoveEarlier, Control::MoveLater})
                if (uiContains(uiBaseMedicalPriorityButtonRect(c, i), position)) return UiBaseMedicalHit{c, i};
    } else {
        for (auto c : {Control::PreviousWarehouse, Control::NextWarehouse, Control::ReserveTransfer})
            if (uiContains(uiBaseMedicalControlRect(c), position)) return UiBaseMedicalHit{c};
        for (std::size_t i = 0; i < kUiBaseMedicalWarehouseRows; ++i)
            if (uiContains(uiBaseMedicalWarehouseRowRect(i), position)) return UiBaseMedicalHit{Control::WarehouseRow, i};
        const auto grid = uiBaseMedicalReserveRect();
        if (uiContains(grid, position))
        {
            const auto column = static_cast<std::size_t>((position.x - grid.x) / kUiBaseMedicalReserveCellSize);
            const auto row = static_cast<std::size_t>((position.y - grid.y) / kUiBaseMedicalReserveCellSize);
            return UiBaseMedicalHit{Control::ReserveCell, row * kMedicalReserveGridSize.width + column};
        }
    }
    return std::nullopt;
}
