#include "ui_base_dormitory_presentation.h"

#include <algorithm>
#include <fmt/format.h>

UiDormitoryProjection projectUiDormitory(const ProfileState &profile, const ContentRegistry &content,
    std::optional<std::uint32_t> hours)
{
    UiDormitoryProjection p;
    p.profileId = profile.profileId;
    p.revision = profile.revision;
    p.worldMinute = profile.worldClock.elapsedWorldMinutes;
    p.clock = projectWorldClock(profile.worldClock);
    p.population = projectBasePopulation(profile.basePopulation);
    p.dailyDemand = populationAdjustedDailyDemand(profile.basePopulation);
    p.foodStock = profile.baseResources.pool.food;
    p.workforce = projectBaseWorkforce(profile);
    p.autoFill = queryAutoFillBaseWorkers(profile);
    p.construction = projectBaseConstruction(profile, content);
    p.activeProject = profile.baseConstruction.activeProject;
    for (const auto &project : content.baseConstructionProjects()) {
        if (project.target != BaseFacilityUpgradeTarget::Dormitory) continue;
        p.project = project;
        p.ownExpansion = p.activeProject && p.activeProject->definitionId == project.id;
        p.expansionComplete = p.construction.dormitoryLevel >= project.targetLevel;
        if (p.ownExpansion) p.cancel = queryCancelBaseConstruction(profile, content, {project.id});
        else p.start = queryStartBaseConstruction(profile, content, {project.id});
        break;
    }
    if (hours == 1 || hours == 6 || hours == 12) {
        p.restHours = hours;
        p.rest = queryBaseRest(profile, {*hours});
    }
    return p;
}

SDL_FRect uiDormitoryControlRect(UiDormitoryControl c) noexcept
{
    using C = UiDormitoryControl;
    switch (c) {
    case C::Residents: return {64, 130, 260, 38};
    case C::Rest: return {336, 130, 180, 38};
    case C::Close: return {1116, 80, 100, 36};
    case C::AutoFill: return {64, 550, 260, 42};
    case C::Rations: return {342, 550, 270, 42};
    case C::CancelExpansion: return {632, 550, 208, 42};
    case C::StartExpansion: case C::StartRest: return {856, 550, 360, 42};
    case C::Hour1: return {84, 248, 320, 56};
    case C::Hour6: return {84, 328, 320, 56};
    case C::Hour12: return {84, 408, 320, 56};
    }
    return {};
}
SDL_FRect uiDormitoryDetailsRect() noexcept { return {652, 318, 544, 206}; }
SDL_FRect uiDormitoryFeedbackRect() noexcept { return {64, 610, 1152, 50}; }

std::optional<UiDormitoryControl> hitUiDormitory(Vec2 point, const UiDormitoryState &s,
    const UiDormitoryProjection &p)
{
    using C = UiDormitoryControl;
    for (C c : {C::Residents, C::Rest, C::Close})
        if (uiContains(uiDormitoryControlRect(c), point)) return c;
    if (s.tab == UiDormitoryTab::Rest) {
        for (C c : {C::Hour1, C::Hour6, C::Hour12, C::StartRest})
            if (uiContains(uiDormitoryControlRect(c), point)) return c;
    } else {
        for (C c : {C::AutoFill, C::Rations, C::StartExpansion})
            if (uiContains(uiDormitoryControlRect(c), point)) return c;
        if (p.ownExpansion && uiContains(uiDormitoryControlRect(C::CancelExpansion), point))
            return C::CancelExpansion;
    }
    return std::nullopt;
}

bool uiDormitoryEnabled(UiDormitoryControl c, const UiDormitoryProjection &p)
{
    using C = UiDormitoryControl;
    switch (c) {
    case C::AutoFill: return p.autoFill.canCommit;
    case C::StartExpansion: return p.project && !p.activeProject && !p.expansionComplete && p.start.canCommit;
    case C::CancelExpansion: return p.ownExpansion && p.cancel.canCommit;
    case C::StartRest: return p.restHours && p.rest.canCommit;
    default: return true;
    }
}

std::string uiDormitoryUnavailable(UiDormitoryControl c, const UiDormitoryProjection &p)
{
    using C = UiDormitoryControl;
    switch (c) {
    case C::AutoFill: return p.autoFill.message;
    case C::CancelExpansion: return p.cancel.message;
    case C::StartExpansion:
        if (!p.project) return "NO DORMITORY PROJECT";
        if (p.ownExpansion) return "EXPANSION IN PROGRESS";
        if (p.expansionComplete) return "DORMITORY EXPANSION COMPLETE";
        if (p.activeProject) return "ANOTHER BASE CONSTRUCTION PROJECT IS ACTIVE";
        return p.start.message;
    case C::StartRest: return p.restHours ? p.rest.message : "CHOOSE REST DURATION FIRST";
    default: return {};
    }
}

std::string uiDormitoryActionKey(UiDormitoryControl c, const UiDormitoryProjection &p)
{
    using C = UiDormitoryControl;
    auto key = fmt::format("{}:{}", p.profileId, static_cast<int>(c));
    if (c != C::StartRest && c != C::StartExpansion && c != C::CancelExpansion && c != C::AutoFill)
        return key;
    key += fmt::format(":{}:{}:{}", p.revision, p.worldMinute, uiDormitoryEnabled(c, p));
    if (c == C::StartRest) {
        key += fmt::format(":{}:{}:{}:{}:{}:{}:{}", p.restHours.value_or(0), p.rest.worldMinutes,
            p.rest.arrival.day, p.rest.arrival.hour, p.rest.arrival.minute,
            static_cast<int>(p.rest.arrival.timeOfDay), p.rest.dailyCyclesCrossed);
    } else if (c == C::AutoFill) {
        const auto profession = [](auto value) { return value ? static_cast<int>(*value) : -1; };
        key += fmt::format(":{}:{}:{}:{}:{}", profession(p.workforce.workshopWorker),
            profession(p.workforce.medicalWorker), profession(p.autoFill.resultingState.workshopWorker),
            profession(p.autoFill.resultingState.medicalWorker), p.workforce.availableResidents);
    } else {
        const auto &plan = c == C::StartExpansion ? p.start : p.cancel;
        key += fmt::format(":{}:{}:{}:{}:{}:{}:{}", p.project ? p.project->id.value() : "",
            plan.materialCost, plan.workerCount, plan.durationMinutes, plan.targetLevel,
            plan.bedCapacityAfter, p.construction.materialUnits);
        if (p.activeProject) key += fmt::format(":{}:{}:{}:{}:{}", p.activeProject->definitionId.value(),
            p.activeProject->startedWorldMinute, p.activeProject->completionWorldMinute,
            p.activeProject->lockedMaterialUnits, p.activeProject->committedWorkers);
    }
    return key;
}
