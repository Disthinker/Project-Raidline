#include "ui_base_medical_presentation.h"

#include <algorithm>
#include <fmt/format.h>

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
        if (it != result.supplies.end()) it->quantity += s.quantity;
        else result.supplies.push_back({s.definitionId, localizeUiText(language, c.item(s.definitionId).displayName), s.quantity});
    }
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
            for (const auto &s : p.resident.supplies) key += fmt::format(":{}:{}:{}:{}", s.assetId, s.definitionId.value(), s.quantity, s.contribution);
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
SDL_FRect uiBaseMedicalControlRect(UiBaseMedicalControl control) noexcept {
    switch (control) {
    case UiBaseMedicalControl::Personal: return {64, 132, 200, 34};
    case UiBaseMedicalControl::Residents: return {276, 132, 200, 34};
    case UiBaseMedicalControl::Details: return {464, 306, 236, 32};
    case UiBaseMedicalControl::Facilities: return {64, 552, 360, 40};
    case UiBaseMedicalControl::Staffing: return {80, 382, 328, 30};
    case UiBaseMedicalControl::Upgrade: return {80, 422, 328, 30};
    case UiBaseMedicalControl::Main: return {916, 552, 300, 40};
    case UiBaseMedicalControl::Close: return {1084, 80, 132, 34};
    }
    return {};
}
SDL_FRect uiBaseMedicalDetailsRect() noexcept { return {464, 350, 728, 148}; }
SDL_FRect uiBaseMedicalFeedbackRect() noexcept { return {76, 616, 1128, 44}; }
std::optional<UiBaseMedicalControl> hitUiBaseMedical(Vec2 position, const UiBaseMedicalState &state) noexcept {
    for (auto c : {UiBaseMedicalControl::Personal, UiBaseMedicalControl::Residents, UiBaseMedicalControl::Details,
                  UiBaseMedicalControl::Main, UiBaseMedicalControl::Close})
        if (uiContains(uiBaseMedicalControlRect(c), position)) return c;
    if (state.tab == UiBaseMedicalTab::Residents) {
        if (uiContains(uiBaseMedicalControlRect(UiBaseMedicalControl::Facilities), position)) return UiBaseMedicalControl::Facilities;
        if (state.facilities) for (auto c : {UiBaseMedicalControl::Staffing, UiBaseMedicalControl::Upgrade})
            if (uiContains(uiBaseMedicalControlRect(c), position)) return c;
    }
    return std::nullopt;
}
