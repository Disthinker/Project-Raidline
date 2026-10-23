#include "ui_regional_operations_presentation.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

#include "base_workforce_domain.h"

namespace {
using C = UiRegionalOperationsControl;
using T = UiRegionalOperationsTab;

// IDs and names may contain delimiters. Length-prefix all pieces so different
// targets cannot alias an existing press or a deployment confirmation.
struct Key {
    std::string value;
    void add(std::string_view text) {
        value += std::to_string(text.size());
        value += ':';
        value.append(text);
        value += ';';
    }
    template<class V> void number(V number) {
        if constexpr (std::is_enum_v<V>)
            add(std::to_string(static_cast<std::underlying_type_t<V>>(number)));
        else add(std::to_string(number));
    }
};

template<class Id> void optionalId(Key &key, const std::optional<Id> &id) {
    key.number(id.has_value());
    if (id) key.add(id->value());
}

template<class Plan> void planKey(Key &key, const Plan &plan) {
    key.number(plan.error);
    key.add(plan.message);
    key.number(plan.revision);
}

void viewKey(Key &key, const UiRegionalOperationsState &s, bool confirmation) {
    key.number(s.tab);
    optionalId(key, s.selectedOutpostId);
    optionalId(key, s.selectedSiteId);
    key.number(s.listPage);
    key.number(s.detailsScroll);
    key.number(s.feedbackScroll);
    if (confirmation) {
        key.number(s.confirmationKey.has_value());
        if (s.confirmationKey) key.add(*s.confirmationKey);
    }
}

std::string contextKey(const ProfileState &profile,
                       const UiRegionalOperationsProjection &p) {
    Key key;
    key.add(p.profileId);
    key.number(p.revision);
    key.number(p.worldMinute);
    key.number(p.language);
    // Compute only when refreshing the projection. This covers same-revision
    // asset/workforce changes between Down and Up without per-frame copying.
    key.number(profileStateFingerprint(profile));
    key.add(p.activeBaseNodeId.value());
    optionalId(key, p.selectedOutpostId);
    optionalId(key, p.selectedSiteId);
    key.number(p.listPage);
    key.number(p.routes.size());
    for (const auto &route : p.routes) {
        key.add(route.mapDefinitionId.value());
        key.add(route.mapDisplayName);
        key.number(route.plan.reachable);
        key.number(route.plan.travelMinutes);
        key.number(route.plan.usesOnlineOutpost);
        planKey(key, route.plan);
        key.number(route.plan.routeIds.size());
        for (const auto &id : route.plan.routeIds) key.add(id.value());
    }
    key.number(p.outposts.size());
    for (const auto &outpost : p.outposts) {
        key.add(outpost.id.value());
        key.add(outpost.nodeId.value());
        key.add(outpost.restorationMapDefinitionId.value());
        key.add(outpost.name);
        key.number(outpost.stateValid);
        key.number(outpost.activeBase);
        key.number(outpost.unlocked);
        key.number(outpost.established);
        key.number(outpost.disrupted);
        key.number(outpost.online);
        key.number(outpost.assignedStaff);
        key.number(outpost.requiredStaff);
        key.number(outpost.safeOperations);
        key.number(outpost.usedOperations);
        key.number(outpost.establish.canCommit);
        planKey(key, outpost.establish);
        key.number(outpost.assign.canCommit);
        planKey(key, outpost.assign);
        key.number(outpost.recall.canCommit);
        planKey(key, outpost.recall);
        key.number(outpost.canRestore);
        key.add(outpost.restoreMessage);
    }
    key.number(p.sites.size());
    for (const auto &site : p.sites) {
        key.add(site.id.value());
        key.add(site.nodeId.value());
        optionalId(key, site.outpostId);
        optionalId(key, site.clearanceMapDefinitionId);
        key.add(site.name);
        key.add(site.advantage);
        key.add(site.disadvantage);
        key.add(site.uniqueFeature);
        key.number(site.tier);
        key.number(site.stateValid);
        key.number(site.unlocked);
        key.number(site.activeBase);
        key.number(site.featureRepaired);
        key.number(site.featureActive);
        key.number(site.migrationMinutes);
        key.number(site.repairMaterialUnits);
        key.number(site.repairDurationMinutes);
        key.number(site.manufacturingDurationPercent);
        key.number(site.clearance.canDeploy);
        planKey(key, site.clearance);
        key.number(site.migration.canCommit);
        planKey(key, site.migration);
        key.number(site.migration.missingRequiredFacilities.size());
        for (const auto &id : site.migration.missingRequiredFacilities) key.add(id.value());
        key.number(site.migration.facilitiesEnteringReserve.size());
        for (const auto &id : site.migration.facilitiesEnteringReserve) key.add(id.value());
        key.number(site.repair.canCommit);
        planKey(key, site.repair);
    }
    key.number(p.sweep.canDeploy);
    planKey(key, p.sweep);
    key.add(p.sweep.baseSiteDefinitionId.value());
    key.add(p.sweep.mapDefinitionId.value());
    key.number(p.sweep.currentThreatUnits);
    key.number(p.sweep.threatReductionUnits);
    key.number(p.sweep.projectedThreatAfterSettlement);
    return std::move(key.value);
}

UiRegionalOperationsHit actionHit(C control, const UiRegionalOperationsProjection &p) {
    UiRegionalOperationsHit hit{control};
    switch (control) {
    case C::Establish: case C::Assign: case C::Recall: case C::Restore:
        if (p.selectedOutpostId) hit.outpostId = *p.selectedOutpostId;
        break;
    case C::ClearSite: case C::Migrate: case C::RepairFeature: case C::OpenConstruction:
        if (p.selectedSiteId) hit.siteId = *p.selectedSiteId;
        break;
    case C::DeploySweep: hit.siteId = p.sweep.baseSiteDefinitionId; break;
    default: break;
    }
    return hit;
}

std::string restoreUnavailable(const UiRegionalOperationsOutpost &outpost,
                              bool pendingRaid) {
    if (!outpost.stateValid) return "regional outpost state is missing";
    if (pendingRaid) return "regional operations are unavailable during a Raid";
    if (outpost.activeBase) return "ACTIVE MAIN BASE LOCATION | NOT A LIGHT OUTPOST";
    if (!outpost.unlocked) return "regional outpost location is locked";
    if (!outpost.established) return "ESTABLISH THE OUTPOST BEFORE RESTORATION";
    if (!outpost.disrupted) return "OUTPOST SHORTCUTS ARE NOT DISRUPTED";
    if (outpost.assignedStaff != outpost.requiredStaff)
        return "RESTORATION REQUIRES A FULL GARRISON";
    return {};
}
}

UiRegionalOperationsProjection projectUiRegionalOperations(
    const ProfileState &profile, const ContentRegistry &content, UiLanguage language,
    const UiRegionalOperationsState &state) {
    UiRegionalOperationsProjection p;
    p.profileId = profile.profileId;
    p.revision = profile.revision;
    p.worldMinute = profile.worldClock.elapsedWorldMinutes;
    p.language = language;
    p.pendingRaid = profile.pendingRaid.has_value();
    p.siegeWarning = profile.baseSiege.warningActive;
    p.defenseActive = profile.activeBaseDefense.has_value();
    p.activeBaseNodeId = profile.regionalOperations.activeBaseNodeId;
    p.technologyCoreId = profile.regionalOperations.technologyCore.instanceId;
    p.materialUnits = profile.baseConstruction.materialUnits;
    p.availableResidents = projectBaseWorkforce(profile).availableResidents;
    p.availableWorkers = availableBaseWorkers(profile);
    p.threat = projectBaseThreat(profile);
    p.sweep = queryBasePerimeterSweep(profile, content);
    const auto &regional = content.regionalOperations();
    p.maximumEstablishedOutposts = regional.maximumEstablishedOutposts;
    const auto activeNode = std::find_if(regional.nodes.begin(), regional.nodes.end(),
        [&](const auto &node) { return node.id == p.activeBaseNodeId; });
    if (activeNode != regional.nodes.end()) p.activeBaseName = activeNode->displayName;
    const auto activeSite = std::find_if(regional.baseSites.begin(), regional.baseSites.end(),
        [&](const auto &site) { return site.nodeId == p.activeBaseNodeId; });
    if (activeSite != regional.baseSites.end()) p.activeBaseSiteId = activeSite->id;

    const BaseFacilityDefinitionId kitchen{"base_facility.kitchen_water"};
    const BaseFacilityDefinitionId workshop{"base_facility.workshop"};
    p.kitchenWaterOwned = baseFacilityOwned(profile, kitchen);
    const bool workshopInstalled = baseFacilityInstalled(profile, workshop);
    p.workshopInReserve = baseFacilityOwned(profile, workshop) && !workshopInstalled;

    // Only published regional destinations are shown. No terrain, enemy,
    // loot or permission-changing data is inferred from route availability.
    for (const auto &node : regional.nodes) {
        if (!node.mapDefinitionId) continue;
        const auto found = std::find_if(content.maps().begin(), content.maps().end(),
            [&](const auto &map) { return map.id == *node.mapDefinitionId; });
        if (found == content.maps().end()) continue;
        p.routes.push_back({found->id, found->displayName,
            queryRegionalRoute(profile, content, found->id)});
    }
    for (const auto &definition : regional.outposts) {
        UiRegionalOperationsOutpost outpost;
        outpost.id = definition.id;
        outpost.nodeId = definition.nodeId;
        outpost.restorationMapDefinitionId = definition.restorationMapDefinitionId;
        outpost.name = definition.displayName;
        outpost.requiredStaff = definition.requiredStaff;
        outpost.safeOperations = definition.safeShortcutOperations;
        outpost.activeBase = activeSite != regional.baseSites.end() &&
            activeSite->outpostDefinitionId == std::optional{definition.id};
        const auto found = profile.regionalOperations.outposts.find(definition.id);
        if (found != profile.regionalOperations.outposts.end()) {
            outpost.stateValid = true;
            outpost.unlocked = found->second.unlocked;
            outpost.established = found->second.established;
            outpost.disrupted = found->second.disrupted;
            outpost.assignedStaff = assignedRegionalOutpostStaff(found->second);
            outpost.usedOperations = found->second.shortcutOperationsSinceRestoration;
            outpost.remainingSafeOperations = outpost.safeOperations > outpost.usedOperations
                ? outpost.safeOperations - outpost.usedOperations : 0U;
            if (outpost.established) ++p.establishedOutposts;
        }
        outpost.online = regionalOutpostOnline(profile, content, definition.id);
        outpost.establish = queryEstablishRegionalOutpost(profile, content, {definition.id});
        outpost.assign = queryRegionalOutpostStaffing(profile, content, {definition.id, true});
        outpost.recall = queryRegionalOutpostStaffing(profile, content, {definition.id, false});
        outpost.restoreMessage = restoreUnavailable(outpost, p.pendingRaid);
        outpost.canRestore = outpost.restoreMessage.empty();
        p.outposts.push_back(std::move(outpost));
    }
    for (const auto &definition : regional.baseSites) {
        UiRegionalOperationsSite site;
        site.id = definition.id;
        site.nodeId = definition.nodeId;
        site.outpostId = definition.outpostDefinitionId;
        site.clearanceMapDefinitionId = definition.clearanceMapDefinitionId;
        site.name = definition.displayName;
        site.tier = definition.tier;
        site.advantage = definition.advantage;
        site.disadvantage = definition.disadvantage;
        site.uniqueFeature = definition.uniqueFeature;
        site.activeBase = definition.nodeId == p.activeBaseNodeId;
        site.migrationMinutes = definition.migrationMinutes;
        site.repairMaterialUnits = definition.uniqueFeatureRepairMaterialUnits;
        site.repairDurationMinutes = definition.uniqueFeatureRepairMinutes;
        site.manufacturingDurationPercent = definition.uniqueFeatureManufacturingDurationPercent;
        site.coreFacilitySlots = definition.coreFacilitySlots;
        const auto found = profile.regionalOperations.baseSites.find(definition.id);
        if (found != profile.regionalOperations.baseSites.end()) {
            site.stateValid = true;
            site.unlocked = found->second.unlocked;
            site.featureRepaired = found->second.uniqueFeatureRepaired;
        }
        site.featureActive = site.activeBase && site.featureRepaired && workshopInstalled;
        site.clearance = queryRegionalBaseSiteClearance(profile, content, definition.id);
        site.migration = queryBaseMigration(profile, content, {definition.id});
        site.repair = queryBaseSiteFeatureRepair(profile, content, {definition.id});
        p.sites.push_back(std::move(site));
    }

    auto outpost = p.outposts.end();
    if (state.selectedOutpostId) {
        outpost = std::find_if(p.outposts.begin(), p.outposts.end(),
            [&](const auto &entry) { return entry.id == *state.selectedOutpostId; });
        if (outpost == p.outposts.end() && state.tab == T::Routes) p.selectionExpired = true;
    } else {
        outpost = std::find_if(p.outposts.begin(), p.outposts.end(),
            [](const auto &entry) { return !entry.activeBase; });
        if (outpost == p.outposts.end()) outpost = p.outposts.begin();
    }
    if (outpost != p.outposts.end()) {
        p.selectedOutpostId = outpost->id;
        p.selectedOutpost = *outpost;
    }
    auto site = p.sites.end();
    if (state.selectedSiteId) {
        site = std::find_if(p.sites.begin(), p.sites.end(),
            [&](const auto &entry) { return entry.id == *state.selectedSiteId; });
        if (site == p.sites.end() && state.tab == T::Base) p.selectionExpired = true;
    } else {
        site = std::find_if(p.sites.begin(), p.sites.end(),
            [](const auto &entry) { return !entry.activeBase; });
        if (site == p.sites.end()) site = p.sites.begin();
    }
    if (site != p.sites.end()) {
        p.selectedSiteId = site->id;
        p.selectedSite = *site;
    }
    p.listPage = std::min(state.listPage,
        uiRegionalOperationsPageCount(uiRegionalOperationsListCount(state, p)) - 1U);
    p.contextKey = contextKey(profile, p);
    return p;
}

std::string uiRegionalOperationsViewKey(const UiRegionalOperationsState &state) {
    Key key;
    viewKey(key, state, true);
    return std::move(key.value);
}

std::string uiRegionalOperationsActionKey(const UiRegionalOperationsHit &hit,
    const UiRegionalOperationsState &state, const UiRegionalOperationsProjection &p) {
    Key key;
    key.add(p.contextKey);
    // A confirmation can store this identity without its own stored value
    // recursively changing the action that the second click must confirm.
    viewKey(key, state, false);
    key.number(hit.control);
    key.number(hit.index);
    key.add(hit.outpostId.value());
    key.add(hit.siteId.value());
    return std::move(key.value);
}

bool uiRegionalOperationsEnabled(C control, const UiRegionalOperationsState &s,
    const UiRegionalOperationsProjection &p, std::size_t index) {
    const bool routes = s.tab == T::Routes;
    const bool base = s.tab == T::Base;
    switch (control) {
    case C::Outpost: return routes && index < p.outposts.size();
    case C::Site: return base && index < p.sites.size();
    case C::Previous: return s.tab != T::Sweep && p.listPage > 0U;
    case C::Next: return s.tab != T::Sweep && p.listPage + 1U <
        uiRegionalOperationsPageCount(uiRegionalOperationsListCount(s, p));
    case C::Establish: return routes && p.selectedOutpost && p.selectedOutpost->establish.canCommit;
    case C::Assign: return routes && p.selectedOutpost && p.selectedOutpost->assign.canCommit;
    case C::Recall: return routes && p.selectedOutpost && p.selectedOutpost->recall.canCommit;
    case C::Restore: return routes && p.selectedOutpost && p.selectedOutpost->canRestore;
    case C::ClearSite: return base && p.selectedSite && p.selectedSite->clearance.canDeploy;
    case C::Migrate: return base && p.selectedSite && p.selectedSite->migration.canCommit;
    case C::RepairFeature: return base && p.selectedSite && p.selectedSite->repair.canCommit;
    case C::OpenConstruction: return base && p.selectedSite && p.selectedSite->unlocked &&
        !p.selectedSite->activeBase && !p.kitchenWaterOwned;
    case C::OpenReserve: return base && p.workshopInReserve;
    case C::DeploySweep: return s.tab == T::Sweep && p.sweep.canDeploy;
    case C::Routes: case C::Base: case C::Sweep: case C::Close: return true;
    }
    return false;
}

std::string uiRegionalOperationsUnavailable(const UiRegionalOperationsHit &hit,
    const UiRegionalOperationsState &s, const UiRegionalOperationsProjection &p) {
    if (uiRegionalOperationsEnabled(hit.control, s, p, hit.index)) return {};
    switch (hit.control) {
    case C::Establish: case C::Assign: case C::Recall: case C::Restore:
        if (!p.selectedOutpost) return "SELECT AN OUTPOST";
        if (hit.control == C::Establish) return p.selectedOutpost->establish.message;
        if (hit.control == C::Assign) return p.selectedOutpost->assign.message;
        if (hit.control == C::Recall) return p.selectedOutpost->recall.message;
        return p.selectedOutpost->restoreMessage;
    case C::ClearSite: case C::Migrate: case C::RepairFeature: case C::OpenConstruction:
        if (!p.selectedSite) return "SELECT A BASE SITE";
        if (hit.control == C::ClearSite) return p.selectedSite->clearance.message;
        if (hit.control == C::Migrate) return p.selectedSite->migration.message;
        if (hit.control == C::RepairFeature) return p.selectedSite->repair.message;
        if (p.kitchenWaterOwned) return "KITCHEN AND WATER IS ALREADY OWNED";
        return "CLEAR THE BASE SITE FIRST";
    case C::OpenReserve: return "NO WORKSHOP IN RESERVE";
    case C::DeploySweep: return p.sweep.message;
    default: return {};
    }
}

std::size_t uiRegionalOperationsPageSize() noexcept { return 5U; }
std::size_t uiRegionalOperationsPageCount(std::size_t count) noexcept {
    return count ? 1U + (count - 1U) / uiRegionalOperationsPageSize() : 1U;
}
std::size_t uiRegionalOperationsListCount(const UiRegionalOperationsState &s,
    const UiRegionalOperationsProjection &p) noexcept {
    return s.tab == T::Routes ? p.outposts.size() : s.tab == T::Base ? p.sites.size() : 0U;
}
SDL_FRect uiRegionalOperationsPanelRect() noexcept { return {40, 50, 1200, 620}; }
SDL_FRect uiRegionalOperationsControlRect(C control) noexcept {
    switch (control) {
    case C::Routes: return {62, 114, 200, 36};
    case C::Base: return {270, 114, 200, 36};
    case C::Sweep: return {478, 114, 200, 36};
    case C::Close: return {1100, 64, 118, 36};
    case C::Previous: return {62, 542, 104, 32};
    case C::Next: return {318, 542, 104, 32};
    case C::Establish: return {438, 500, 184, 34};
    case C::Assign: return {636, 500, 184, 34};
    case C::Recall: return {834, 500, 184, 34};
    case C::Restore: return {1032, 500, 184, 34};
    case C::ClearSite: return {438, 500, 246, 34};
    case C::Migrate: return {704, 500, 246, 34};
    case C::RepairFeature: return {970, 500, 246, 34};
    case C::OpenConstruction: return {438, 542, 378, 32};
    case C::OpenReserve: return {836, 542, 380, 32};
    case C::DeploySweep: return {438, 536, 778, 38};
    default: return {};
    }
}
SDL_FRect uiRegionalOperationsRowRect(std::size_t row) noexcept {
    return {62, 176 + static_cast<float>(row) * 66.0F, 360, 58};
}
SDL_FRect uiRegionalOperationsListRect() noexcept { return {62, 176, 360, 354}; }
SDL_FRect uiRegionalOperationsDetailsRect() noexcept { return {438, 176, 780, 314}; }
SDL_FRect uiRegionalOperationsFeedbackRect() noexcept { return {62, 588, 1156, 58}; }

std::optional<UiRegionalOperationsHit> hitUiRegionalOperations(Vec2 point,
    const UiRegionalOperationsState &s, const UiRegionalOperationsProjection &p) {
    for (auto control : {C::Routes, C::Base, C::Sweep, C::Close})
        if (uiContains(uiRegionalOperationsControlRect(control), point))
            return UiRegionalOperationsHit{control};
    if (s.tab != T::Sweep) {
        for (auto control : {C::Previous, C::Next})
            if (uiContains(uiRegionalOperationsControlRect(control), point))
                return UiRegionalOperationsHit{control};
        const auto count = uiRegionalOperationsListCount(s, p);
        const auto first = p.listPage * uiRegionalOperationsPageSize();
        for (std::size_t row{}; row < uiRegionalOperationsPageSize() && first + row < count; ++row) {
            if (!uiContains(uiRegionalOperationsRowRect(row), point)) continue;
            UiRegionalOperationsHit hit{s.tab == T::Routes ? C::Outpost : C::Site, first + row};
            if (s.tab == T::Routes) hit.outpostId = p.outposts[hit.index].id;
            else hit.siteId = p.sites[hit.index].id;
            return hit;
        }
    }
    if (s.tab == T::Routes) {
        for (auto control : {C::Establish, C::Assign, C::Recall, C::Restore})
            if (uiContains(uiRegionalOperationsControlRect(control), point)) return actionHit(control, p);
    } else if (s.tab == T::Base) {
        for (auto control : {C::ClearSite, C::Migrate, C::RepairFeature,
                             C::OpenConstruction, C::OpenReserve})
            if (uiContains(uiRegionalOperationsControlRect(control), point)) return actionHit(control, p);
    } else if (uiContains(uiRegionalOperationsControlRect(C::DeploySweep), point)) {
        return actionHit(C::DeploySweep, p);
    }
    return {};
}
