#include "ui_base_deployment_presentation.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>
#include <fmt/format.h>

namespace {
using C = UiDeploymentControl;
using T = UiDeploymentTab;

// Length-prefixed strings prevent two different stable identities from
// producing the same operation proof merely by containing a separator.
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
        else
            add(std::to_string(number));
    }
};

void addClock(Key &key, const WorldClockProjection &clock) {
    key.number(clock.day);
    key.number(clock.hour);
    key.number(clock.minute);
    key.number(clock.timeOfDay);
}

std::string contextKey(const ProfileState &profile, const UiDeploymentProjection &p) {
    Key key;
    key.add(p.profileId);
    key.number(p.revision);
    key.number(p.worldMinute);
    key.number(p.language);
    key.add(p.selectedMapId.value());
    key.number(p.mapValid);
    key.number(p.credentialUnlocked.has_value());
    if (p.credentialUnlocked) key.number(*p.credentialUnlocked);
    key.number(p.currency);
    key.number(profile.currentHealth);
    key.number(profile.pendingRaid.has_value());
    key.number(p.travel.reachable);
    key.number(p.travel.outboundMinutes);
    key.number(p.travel.returnMinutes);
    key.number(p.travel.failureRegroupMinutes);
    key.number(p.travel.usesOnlineOutpost);
    addClock(key, p.travel.departure);
    addClock(key, p.travel.arrival);
    addClock(key, p.travel.extractedReturn);
    addClock(key, p.travel.failureReturn);
    key.number(p.travel.routeIds.size());
    for (const auto &route : p.travel.routeIds) key.add(route.value());
    for (const auto &row : p.intelligence) {
        key.number(row.category);
        key.number(row.owned);
        key.number(row.selected);
        key.number(row.purchase.canCommit);
        key.number(row.purchase.error);
        key.number(row.price.has_value());
        if (row.price) key.number(*row.price);
        key.add(row.purchase.message);
    }
    key.number(p.interiors.size());
    for (const auto &row : p.interiors) {
        key.add(row.id.value());
        key.number(row.known);
        key.number(row.purchase.canCommit);
        key.number(row.purchase.error);
        key.number(row.price.has_value());
        if (row.price) key.number(*row.price);
        key.add(row.purchase.message);
    }
    const auto &supply = p.weaponSupply;
    key.number(supply.anyWeaponCanFire);
    key.number(supply.hasCarriedMedical);
    key.number(supply.hasBodyArmor);
    key.number(supply.hasChestRig);
    key.number(supply.hasBackpack);
    key.number(supply.weapons.size());
    for (const auto &weapon : supply.weapons) {
        key.number(weapon.slot);
        key.number(weapon.weaponAssetId);
        key.add(weapon.weaponDefinitionId.value());
        key.number(weapon.definitionKnown);
        key.number(weapon.fireResult);
        key.number(weapon.canFireNow);
        key.number(weapon.chamberedRounds);
        key.number(weapon.installedMagazineId.value_or(0));
        key.number(weapon.installedMagazineRounds);
        key.number(weapon.carriedLooseRounds);
        key.number(weapon.quickReloadMagazineId.value_or(0));
        key.number(weapon.quickReloadMagazineRounds);
        key.number(weapon.quickReloadCanInstall);
        key.number(weapon.quickReloadLoadedMagazineCount);
        key.number(weapon.quickReloadLoadedRounds);
        key.number(weapon.otherLoadedSpareMagazineCount);
        key.number(weapon.otherLoadedSpareMagazineRounds);
    }
    key.number(p.selectedLoadout.has_value());
    if (p.selectedLoadout) {
        const auto &role = *p.selectedLoadout;
        key.add(role.archetypeId.value());
        key.number(role.matchingWeaponAssetId.value_or(0));
        key.number(role.compatibleRoundCount);
        key.number(role.minimumCompatibleRounds);
        key.number(role.bodyArmorReady);
        key.number(role.chestRigReady);
        key.number(role.backpackReady);
        key.number(role.issues.size());
        for (const auto issue : role.issues) key.number(issue);
    }
    key.number(p.recoveryRecordId.has_value());
    if (p.recoveryRecordId) key.add(*p.recoveryRecordId);
    key.number(p.recoveryValid);
    key.number(p.recoveryMapMatches);
    key.number(p.recovery.has_value());
    if (p.recovery) {
        const auto &recovery = *p.recovery;
        key.add(recovery.recordId);
        key.add(recovery.raidId);
        key.add(recovery.mapDefinitionId.value());
        key.number(recovery.createdWorldMinute);
        key.number(recovery.subsequentRaidSettlementCount);
        key.number(recovery.retainedSettlementsRemaining);
        key.number(recovery.assets.size());
        for (const auto &asset : recovery.assets) {
            key.number(asset.assetId);
            key.add(asset.definitionId.value());
            key.number(asset.quantity);
            key.number(asset.ownershipRoot);
        }
    }
    key.number(p.aging.activeRecordCount);
    key.number(p.aging.recordsExpiringOnNextSettlement);
    key.number(p.aging.assetInstancesExpiringOnNextSettlement);
    key.number(p.cannotFire);
    key.number(p.expiryRisk);
    key.number(p.wish.relevance);
    key.number(p.wish.informed);
    key.add(p.wishFocusName);
    key.add(p.wish.reason);
    return std::move(key.value);
}

std::size_t lastPage(std::size_t count, std::size_t size) {
    return uiDeploymentPageCount(count, size) - 1U;
}
}

UiDeploymentProjection projectUiDeployment(
    const ProfileState &profile, const ContentRegistry &content, UiLanguage language,
    const MapDefinitionId &mapId, const RaidIntelligenceLoadout &intelligence,
    const std::optional<std::string> &recoveryRecordId, std::size_t loadoutIndex,
    const UiDeploymentState &state) {
    UiDeploymentProjection result;
    result.profileId = profile.profileId;
    result.revision = profile.revision;
    result.worldMinute = profile.worldClock.elapsedWorldMinutes;
    result.language = language;
    result.currency = profile.currency;
    result.selectedMapId = mapId;
    result.recoveryRecordId = recoveryRecordId;
    const MapDefinition *selectedMap{};
    result.maps.reserve(content.maps().size());
    for (const auto &map : content.maps()) {
        // Only the existing public definition metadata crosses into this UI.
        result.maps.push_back({map.id, map.displayName,
            map.operationBriefing.difficulty, map.operationBriefing.warning,
            map.routeProfile});
        if (map.id == mapId) {
            selectedMap = &map;
            result.selectedMapIndex = result.maps.size() - 1U;
        }
    }
    result.mapPage = std::min(state.mapPage,
        lastPage(result.maps.size(), uiDeploymentMapPageSize()));
    if (selectedMap) {
        result.mapValid = true;
        result.mapName = selectedMap->displayName;
        result.difficulty = selectedMap->operationBriefing.difficulty;
        result.warning = selectedMap->operationBriefing.warning;
        result.routeProfile = selectedMap->routeProfile;
        if (selectedMap->credentialExtraction && !recoveryRecordId)
            result.credentialUnlocked = profile.raidExtractionPermissions.contains(
                selectedMap->credentialExtraction->permissionId);
        result.travel = queryRaidTravel(profile, content, *selectedMap);
        for (const auto &routeId : result.travel.routeIds) {
            if (!result.routeName.empty()) result.routeName += " > ";
            result.routeName += content.regionRoute(routeId).displayName;
        }
        result.interiors.reserve(selectedMap->interiors.size());
        for (const auto &interior : selectedMap->interiors) {
            UiDeploymentInterior row;
            row.id = interior.id;
            row.name = interior.displayName;
            row.known = profile.raidInteriorIntelligence.knows(interior.id);
            row.purchase = queryRaidInteriorIntelligencePurchase(
                profile, content, {interior.id});
            // A valid zero-price success is represented explicitly; a failed
            // query's zero remains unavailable, never the word "free".
            if (row.purchase.canCommit || row.purchase.price > 0U)
                row.price = row.purchase.price;
            result.interiors.push_back(std::move(row));
        }
        result.wish = projectBaseWishExpeditionRelevance(profile, content, mapId);
        if (profile.basePriority.focus &&
            isBaseWishActive(profile.basePriority, *profile.basePriority.focus))
            result.wishFocusName = content.basePriority(
                profile.basePriority.focus->definitionId).displayName;
    }
    constexpr std::array<const char *, kRaidIntelligenceCategoryCount> names{
        "TRANSPORT MAP", "RESOURCE MANIFEST", "ENEMY DOSSIER"};
    for (std::size_t i{}; i < kRaidIntelligenceCategoryCount; ++i) {
        auto &row = result.intelligence[i];
        row.category = static_cast<RaidIntelligenceCategory>(i);
        row.name = names[i];
        row.owned = profile.raidIntelligence.count(mapId, row.category);
        row.selected = intelligence.has(row.category);
        row.purchase = queryRaidIntelligencePurchase(profile, content, {mapId, row.category});
        if (row.purchase.canCommit || row.purchase.price > 0U)
            row.price = row.purchase.price;
    }
    result.interiorPage = std::min(state.interiorPage,
        lastPage(result.interiors.size(), uiDeploymentInteriorPageSize()));
    if (state.selectedIntelligence &&
        *state.selectedIntelligence != RaidIntelligenceCategory::Count)
        result.selectedIntelligence = state.selectedIntelligence;
    if (state.selectedInterior && std::any_of(result.interiors.begin(),
        result.interiors.end(), [&](const auto &row) { return row.id == *state.selectedInterior; }))
        result.selectedInterior = state.selectedInterior;
    result.loadouts = projectAllLoadoutReadiness(profile, content);
    result.selectedLoadoutIndex = result.loadouts.empty() ? 0U : loadoutIndex % result.loadouts.size();
    if (!result.loadouts.empty())
        result.selectedLoadout = result.loadouts[result.selectedLoadoutIndex];
    result.weaponSupply = projectWeaponSupply(profile, content, false);
    result.aging = queryLostRaidAging(profile);
    if (recoveryRecordId) {
        // The established query walks lost ownership, never a frozen Raid
        // layout. Only the explicitly chosen target is retained by this page.
        const auto records = queryLostRaidRecords(profile, content);
        const auto found = std::find_if(records.begin(), records.end(),
            [&](const auto &record) { return record.recordId == *recoveryRecordId; });
        if (found != records.end()) {
            result.recovery = *found;
            result.recoveryMapMatches = found->mapDefinitionId == mapId;
            result.recoveryValid = !recoveryRecordId->empty() &&
                result.recoveryMapMatches && std::any_of(found->assets.begin(),
                    found->assets.end(), [](const auto &asset) { return asset.ownershipRoot; });
        }
    }
    result.cannotFire = !result.weaponSupply.anyWeaponCanFire;
    result.expiryRisk = result.aging.recordsExpiringOnNextSettlement > 0U;
    result.requiresConfirmation = result.cannotFire || result.expiryRisk;
    if (result.cannotFire)
        result.riskReasons.push_back("CANNOT FIRE NOW | CHECK WEAPON IN TAB INVENTORY");
    if (result.expiryRisk)
        result.riskReasons.push_back(fmt::format(
            "WARNING: {} RECORD(S) EXPIRE AFTER NEXT RAID SETTLEMENT",
            result.aging.recordsExpiringOnNextSettlement));
    result.contextKey = contextKey(profile, result);
    return result;
}

std::string uiDeploymentViewKey(const UiDeploymentState &state) {
    Key key;
    key.number(state.tab);
    key.number(state.mapPage);
    key.number(state.interiorPage);
    key.number(state.selectedIntelligence.has_value());
    if (state.selectedIntelligence) key.number(*state.selectedIntelligence);
    key.number(state.selectedInterior.has_value());
    if (state.selectedInterior) key.add(state.selectedInterior->value());
    key.number(state.briefingDetails);
    key.number(state.readinessDetails);
    return std::move(key.value);
}

std::string uiDeploymentActionKey(const UiDeploymentHit &hit,
    const UiDeploymentState &state, const UiDeploymentProjection &projection) {
    Key key;
    key.add(projection.contextKey);
    key.add(uiDeploymentViewKey(state));
    key.number(hit.control);
    key.number(hit.index);
    key.add(hit.mapId.value());
    key.number(hit.category);
    key.add(hit.interiorId.value());
    return std::move(key.value);
}

bool uiDeploymentEnabled(C control, const UiDeploymentState &state,
    const UiDeploymentProjection &projection, std::size_t index) {
    switch (control) {
    case C::Map: return index < projection.maps.size();
    case C::MapPrevious: return projection.mapPage > 0U;
    case C::MapNext:
        return projection.mapPage < lastPage(projection.maps.size(), uiDeploymentMapPageSize());
    case C::BriefingDetails: return state.tab == T::Briefing;
    case C::IntelligenceRow:
        return state.tab == T::Intelligence && index < projection.intelligence.size();
    case C::IntelligenceAction:
        return state.tab == T::Intelligence && index < projection.intelligence.size() &&
            (projection.intelligence[index].owned > 0U || projection.intelligence[index].purchase.canCommit);
    case C::Interior:
        return state.tab == T::Intelligence && index < projection.interiors.size();
    case C::InteriorAction:
        return state.tab == T::Intelligence && index < projection.interiors.size() &&
            !projection.interiors[index].known && projection.interiors[index].purchase.canCommit;
    case C::InteriorPrevious:
        return state.tab == T::Intelligence && projection.interiorPage > 0U;
    case C::InteriorNext:
        return state.tab == T::Intelligence &&
            projection.interiorPage < lastPage(projection.interiors.size(), uiDeploymentInteriorPageSize());
    case C::LoadoutPrevious: case C::LoadoutNext: return projection.loadouts.size() > 1U;
    case C::RecoveryClear: return projection.recoveryRecordId.has_value();
    case C::Deploy: return projection.mapValid;
    default: return true;
    }
}

std::string uiDeploymentUnavailable(const UiDeploymentHit &hit,
    const UiDeploymentState &state, const UiDeploymentProjection &projection) {
    if (uiDeploymentEnabled(hit.control, state, projection, hit.index)) return {};
    if (hit.control == C::IntelligenceAction && hit.index < projection.intelligence.size())
        return projection.intelligence[hit.index].purchase.message;
    if (hit.control == C::InteriorAction && hit.index < projection.interiors.size())
        return projection.interiors[hit.index].known ? "PERMANENTLY KNOWN" :
            projection.interiors[hit.index].purchase.message;
    if (hit.control == C::Deploy) return "Raid intelligence map does not exist";
    return {};
}

std::size_t uiDeploymentMapPageSize() noexcept { return 6U; }
std::size_t uiDeploymentInteriorPageSize() noexcept { return 2U; }
std::size_t uiDeploymentPageCount(std::size_t count, std::size_t pageSize) noexcept {
    return !count || !pageSize ? 1U : 1U + (count - 1U) / pageSize;
}
SDL_FRect uiDeploymentPanelRect() noexcept { return {40, 50, 1200, 620}; }
SDL_FRect uiDeploymentControlRect(C control) noexcept {
    switch (control) {
    case C::MapPrevious: return {62, 528, 92, 28};
    case C::MapNext: return {168, 528, 92, 28};
    case C::Briefing: return {310, 108, 180, 36};
    case C::Intelligence: return {498, 108, 180, 36};
    case C::BriefingDetails: return {690, 108, 136, 36};
    case C::InteriorPrevious: return {664, 356, 76, 28};
    case C::InteriorNext: return {750, 356, 76, 28};
    case C::LoadoutPrevious: return {872, 144, 36, 30};
    case C::LoadoutNext: return {916, 144, 36, 30};
    case C::ReadinessDetails: return {1086, 144, 128, 30};
    case C::RecoveryClear: return {1086, 556, 128, 26};
    case C::Regional: return {62, 612, 112, 40};
    case C::LostRecords: return {184, 612, 112, 40};
    case C::Deploy: return {878, 612, 220, 40};
    case C::Close: return {1110, 612, 108, 40};
    default: return {};
    }
}
SDL_FRect uiDeploymentMapRect(std::size_t row) noexcept {
    return {62, 164 + 58.0F * static_cast<float>(row), 212, 52};
}
SDL_FRect uiDeploymentIntelligenceRect(std::size_t row, bool action) noexcept {
    return {action ? 596.0F : 310.0F,
        166 + 58.0F * static_cast<float>(row), action ? 230.0F : 278.0F, 50};
}
SDL_FRect uiDeploymentInteriorRect(std::size_t row, bool action) noexcept {
    return {action ? 596.0F : 310.0F,
        394 + 46.0F * static_cast<float>(row), action ? 230.0F : 278.0F, 40};
}
SDL_FRect uiDeploymentBriefingRect() noexcept { return {310, 166, 516, 388}; }
SDL_FRect uiDeploymentIntelligenceDetailsRect() noexcept { return {310, 492, 516, 62}; }
SDL_FRect uiDeploymentReadinessRect() noexcept { return {872, 188, 342, 362}; }
SDL_FRect uiDeploymentFeedbackRect() noexcept { return {310, 572, 760, 32}; }

std::optional<UiDeploymentHit> hitUiDeployment(Vec2 point,
    const UiDeploymentState &state, const UiDeploymentProjection &projection) {
    for (auto control : {C::Close, C::Deploy, C::Regional, C::LostRecords,
        C::Briefing, C::Intelligence, C::MapPrevious, C::MapNext,
        C::LoadoutPrevious, C::LoadoutNext, C::ReadinessDetails})
        if (uiContains(uiDeploymentControlRect(control), point))
            return UiDeploymentHit{control};
    if (projection.recoveryRecordId && uiContains(uiDeploymentControlRect(C::RecoveryClear), point))
        return UiDeploymentHit{C::RecoveryClear};
    const auto mapFirst = projection.mapPage * uiDeploymentMapPageSize();
    for (std::size_t row{}; row < uiDeploymentMapPageSize() &&
         mapFirst + row < projection.maps.size(); ++row)
        if (uiContains(uiDeploymentMapRect(row), point)) {
            UiDeploymentHit hit{C::Map, mapFirst + row};
            hit.mapId = projection.maps[hit.index].id;
            return hit;
        }
    if (state.tab == T::Briefing) {
        if (uiContains(uiDeploymentControlRect(C::BriefingDetails), point))
            return UiDeploymentHit{C::BriefingDetails};
        return {};
    }
    for (std::size_t row{}; row < projection.intelligence.size(); ++row)
        for (const bool action : {false, true})
            if (uiContains(uiDeploymentIntelligenceRect(row, action), point)) {
                UiDeploymentHit hit{action ? C::IntelligenceAction : C::IntelligenceRow, row};
                hit.mapId = projection.selectedMapId;
                hit.category = projection.intelligence[row].category;
                return hit;
            }
    for (auto control : {C::InteriorPrevious, C::InteriorNext})
        if (uiContains(uiDeploymentControlRect(control), point))
            return UiDeploymentHit{control};
    const auto interiorFirst = projection.interiorPage * uiDeploymentInteriorPageSize();
    for (std::size_t row{}; row < uiDeploymentInteriorPageSize() &&
         interiorFirst + row < projection.interiors.size(); ++row)
        for (const bool action : {false, true})
            if (uiContains(uiDeploymentInteriorRect(row, action), point)) {
                UiDeploymentHit hit{action ? C::InteriorAction : C::Interior, interiorFirst + row};
                hit.mapId = projection.selectedMapId;
                hit.interiorId = projection.interiors[hit.index].id;
                return hit;
            }
    return {};
}
