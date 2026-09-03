#include "save_repository.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "base_resource_domain.h"
#include "base_facility_layout_domain.h"
#include "base_morale_domain.h"
#include "base_siege_domain.h"
#include "regional_operations_domain.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
using Json = nlohmann::json;

std::string checksum(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4U;
    }
    return result;
}

std::string orientationName(ItemOrientation orientation)
{
    switch (orientation)
    {
    case ItemOrientation::Degrees0:
        return "0";
    case ItemOrientation::Degrees90:
        return "90";
    case ItemOrientation::Degrees180:
        return "180";
    case ItemOrientation::Degrees270:
        return "270";
    }
    return "invalid";
}

std::optional<ItemOrientation> parseOrientation(std::string_view value)
{
    if (value == "0") return ItemOrientation::Degrees0;
    if (value == "90") return ItemOrientation::Degrees90;
    if (value == "180") return ItemOrientation::Degrees180;
    if (value == "270") return ItemOrientation::Degrees270;
    return std::nullopt;
}

std::string malfunctionName(WeaponMalfunctionType malfunction)
{
    switch (malfunction)
    {
    case WeaponMalfunctionType::None:
        return "none";
    case WeaponMalfunctionType::Stovepipe:
        return "stovepipe";
    }
    return "invalid";
}

std::optional<WeaponMalfunctionType> parseMalfunction(
    std::string_view value)
{
    if (value == "none") return WeaponMalfunctionType::None;
    if (value == "stovepipe") return WeaponMalfunctionType::Stovepipe;
    return std::nullopt;
}

std::string slotName(EquipmentSlotKind slot)
{
    switch (slot)
    {
    case EquipmentSlotKind::PrimaryWeapon:
        return "primary_weapon";
    case EquipmentSlotKind::SecondaryWeapon:
        return "secondary_weapon";
    case EquipmentSlotKind::Sidearm:
        return "sidearm";
    case EquipmentSlotKind::ChestRig:
        return "chest_rig";
    case EquipmentSlotKind::Backpack:
        return "backpack";
    case EquipmentSlotKind::Helmet:
        return "helmet";
    case EquipmentSlotKind::BodyArmor:
        return "body_armor";
    }
    return "invalid";
}

std::optional<EquipmentSlotKind> parseSlot(std::string_view value)
{
    if (value == "primary_weapon") return EquipmentSlotKind::PrimaryWeapon;
    if (value == "secondary_weapon") return EquipmentSlotKind::SecondaryWeapon;
    if (value == "sidearm") return EquipmentSlotKind::Sidearm;
    if (value == "chest_rig") return EquipmentSlotKind::ChestRig;
    if (value == "backpack") return EquipmentSlotKind::Backpack;
    if (value == "helmet") return EquipmentSlotKind::Helmet;
    if (value == "body_armor") return EquipmentSlotKind::BodyArmor;
    return std::nullopt;
}

std::string tutorialName(TutorialProgress progress)
{
    switch (progress)
    {
    case TutorialProgress::FindStorage:
        return "find_storage";
    case TutorialProgress::PrepareLoadout:
        return "prepare_loadout";
    case TutorialProgress::FindRaidGate:
        return "find_raid_gate";
    case TutorialProgress::Complete:
        return "complete";
    }
    return "invalid";
}

std::optional<TutorialProgress> parseTutorial(std::string_view value)
{
    if (value == "find_storage") return TutorialProgress::FindStorage;
    if (value == "prepare_loadout") return TutorialProgress::PrepareLoadout;
    if (value == "find_raid_gate") return TutorialProgress::FindRaidGate;
    if (value == "complete") return TutorialProgress::Complete;
    return std::nullopt;
}

std::string raidOutcomeName(RaidResultOutcome outcome)
{
    switch (outcome)
    {
    case RaidResultOutcome::Extracted:
        return "extracted";
    case RaidResultOutcome::PlayerDead:
        return "player_dead";
    case RaidResultOutcome::ActiveQuit:
        return "active_quit";
    case RaidResultOutcome::AbnormalQuit:
        return "abnormal_quit";
    }
    return "invalid";
}

std::optional<RaidResultOutcome> parseRaidOutcome(std::string_view value)
{
    if (value == "extracted") return RaidResultOutcome::Extracted;
    if (value == "player_dead") return RaidResultOutcome::PlayerDead;
    if (value == "active_quit") return RaidResultOutcome::ActiveQuit;
    if (value == "abnormal_quit") return RaidResultOutcome::AbnormalQuit;
    return std::nullopt;
}

std::string baseSupplyCategoryName(BaseSupplyCategory category)
{
    switch (category)
    {
    case BaseSupplyCategory::Food:
        return "food";
    case BaseSupplyCategory::Medical:
        return "medical";
    case BaseSupplyCategory::Recreation:
        return "recreation";
    case BaseSupplyCategory::Security:
        return "security";
    }
    return "invalid";
}

std::optional<BaseSupplyCategory> parseBaseSupplyCategory(
    std::string_view value)
{
    if (value == "food") return BaseSupplyCategory::Food;
    if (value == "medical") return BaseSupplyCategory::Medical;
    if (value == "recreation") return BaseSupplyCategory::Recreation;
    if (value == "security") return BaseSupplyCategory::Security;
    return std::nullopt;
}

std::string baseResidentProfessionValue(BaseResidentProfession profession)
{
    switch (profession)
    {
    case BaseResidentProfession::General:
        return "general";
    case BaseResidentProfession::Medical:
        return "medical";
    case BaseResidentProfession::Engineering:
        return "engineering";
    case BaseResidentProfession::Combat:
        return "combat";
    }
    return "invalid";
}

std::optional<BaseResidentProfession> parseBaseResidentProfession(
    std::string_view value)
{
    if (value == "general") return BaseResidentProfession::General;
    if (value == "medical") return BaseResidentProfession::Medical;
    if (value == "engineering") return BaseResidentProfession::Engineering;
    if (value == "combat") return BaseResidentProfession::Combat;
    return std::nullopt;
}

Json baseProfessionCountsValue(const BaseProfessionCounts &counts)
{
    return {
        {"general", counts[static_cast<std::size_t>(
            BaseResidentProfession::General)]},
        {"medical", counts[static_cast<std::size_t>(
            BaseResidentProfession::Medical)]},
        {"engineering", counts[static_cast<std::size_t>(
            BaseResidentProfession::Engineering)]},
        {"combat", counts[static_cast<std::size_t>(
            BaseResidentProfession::Combat)]}};
}

BaseProfessionCounts parseBaseProfessionCounts(const Json &value)
{
    return {
        value.at("general").get<std::uint32_t>(),
        value.at("medical").get<std::uint32_t>(),
        value.at("engineering").get<std::uint32_t>(),
        value.at("combat").get<std::uint32_t>()};
}

Json regionalOperationsValue(
    const RegionalOperationsState &state,
    bool includeShortcutThreat,
    bool includeBaseSites,
    bool includeTechnologyCore,
    bool includeUniqueFeatureState)
{
    Json outposts = Json::array();
    for (const auto &[outpostId, outpost] : state.outposts)
    {
        if (!includeShortcutThreat &&
            outpost.shortcutOperationsSinceRestoration != 0U)
        {
            throw std::invalid_argument{
                "legacy schema cannot represent regional outpost threat"};
        }
        Json entry{
            {"definition_id", outpostId.value()},
            {"unlocked", outpost.unlocked},
            {"established", outpost.established},
            {"disrupted", outpost.disrupted},
            {"assigned_staff",
             baseProfessionCountsValue(outpost.assignedStaff)}};
        if (includeShortcutThreat)
        {
            entry["shortcut_operations_since_restoration"] =
                outpost.shortcutOperationsSinceRestoration;
        }
        outposts.push_back(std::move(entry));
    }
    Json value{
        {"active_base_node_id", state.activeBaseNodeId.value()},
        {"outposts", std::move(outposts)}};
    if (includeBaseSites)
    {
        Json baseSites = Json::array();
        for (const auto &[siteId, site] : state.baseSites)
        {
            Json entry{
                {"definition_id", siteId.value()},
                {"unlocked", site.unlocked}};
            if (includeUniqueFeatureState)
            {
                entry["unique_feature_repaired"] =
                    site.uniqueFeatureRepaired;
            }
            baseSites.push_back(std::move(entry));
        }
        value["base_sites"] = std::move(baseSites);
    }
    if (includeTechnologyCore)
    {
        value["technology_core"] = {
            {"instance_id", state.technologyCore.instanceId},
            {"base_site_definition_id",
             state.technologyCore.baseSiteDefinitionId.value()}};
    }
    return value;
}

const RegionalBaseSiteDefinition &baseSiteAtNode(
    const ContentRegistry &content,
    const RegionNodeDefinitionId &nodeId)
{
    const auto &sites = content.regionalOperations().baseSites;
    const auto found = std::find_if(
        sites.begin(), sites.end(),
        [&](const RegionalBaseSiteDefinition &site)
        {
            return site.nodeId == nodeId;
        });
    if (found == sites.end())
    {
        throw std::runtime_error{"active Base node has no Base site"};
    }
    return *found;
}

void initializeLegacyFacilityOwnership(
    BaseConstructionState &state,
    const ContentRegistry &content,
    std::uint64_t currentWorldMinute)
{
    state.kitchenWaterLevel = 0U;
    state.facilities.clear();
    state.facilityReserveStartedWorldMinutes.clear();
    for (const BaseFacilityDefinition &facility : content.baseFacilities())
    {
        if (facility.initiallyOwned)
        {
            state.facilities.emplace(
                facility.id,
                facility.initiallyInstalled
                    ? BaseConstructionState::FacilityPlacement::Installed
                    : BaseConstructionState::FacilityPlacement::Reserve);
            if (!facility.initiallyInstalled)
            {
                state.facilityReserveStartedWorldMinutes.emplace(
                    facility.id, currentWorldMinute);
            }
        }
    }
}

std::string_view facilityPlacementName(
    BaseConstructionState::FacilityPlacement placement)
{
    switch (placement)
    {
    case BaseConstructionState::FacilityPlacement::Installed:
        return "installed";
    case BaseConstructionState::FacilityPlacement::Reserve:
        return "reserve";
    }
    throw std::invalid_argument{"unknown Base facility placement"};
}

BaseConstructionState::FacilityPlacement parseFacilityPlacement(
    std::string_view value)
{
    if (value == "installed")
    {
        return BaseConstructionState::FacilityPlacement::Installed;
    }
    if (value == "reserve")
    {
        return BaseConstructionState::FacilityPlacement::Reserve;
    }
    throw std::runtime_error{"Base facility placement is invalid"};
}

void appendFacilityState(Json &value, const BaseConstructionState &state)
{
    value["kitchen_water_level"] = state.kitchenWaterLevel;
    Json facilities = Json::array();
    for (const auto &[definitionId, placement] : state.facilities)
    {
        Json entry{
            {"definition_id", definitionId.value()},
            {"placement", facilityPlacementName(placement)}};
        if (placement == BaseConstructionState::FacilityPlacement::Reserve)
        {
            entry["reserve_started_world_minute"] =
                state.facilityReserveStartedWorldMinutes.at(definitionId);
        }
        facilities.push_back(std::move(entry));
    }
    value["facilities"] = std::move(facilities);
}

void parseFacilityState(
    const Json &value,
    BaseConstructionState &state,
    const ContentRegistry &content)
{
    state.kitchenWaterLevel = value.at("kitchen_water_level")
        .get<std::uint32_t>();
    state.facilities.clear();
    state.facilityReserveStartedWorldMinutes.clear();
    std::set<BaseFacilityDefinitionId> parsed;
    for (const Json &entry : value.at("facilities"))
    {
        const BaseFacilityDefinitionId definitionId{
            entry.at("definition_id").get<std::string>()};
        static_cast<void>(content.baseFacility(definitionId));
        if (!parsed.insert(definitionId).second)
        {
            throw std::runtime_error{"Base facility state is duplicated"};
        }
        const BaseConstructionState::FacilityPlacement placement =
            parseFacilityPlacement(
                entry.at("placement").get<std::string>());
        state.facilities.emplace(definitionId, placement);
        if (placement == BaseConstructionState::FacilityPlacement::Reserve)
        {
            state.facilityReserveStartedWorldMinutes.emplace(
                definitionId,
                entry.at("reserve_started_world_minute")
                    .get<std::uint64_t>());
        }
        else if (entry.contains("reserve_started_world_minute"))
        {
            throw std::runtime_error{
                "installed Base facility has reserve timing"};
        }
    }
}

RegionalOperationsState defaultRegionalOperations(
    const ContentRegistry &content)
{
    RegionalOperationsState state;
    state.activeBaseNodeId =
        content.regionalOperations().initialBaseNodeId;
    const RegionalBaseSiteDefinition &activeSite =
        baseSiteAtNode(content, state.activeBaseNodeId);
    state.technologyCore = {
        "technology_core.primary",
        activeSite.id};
    for (const RegionalBaseSiteDefinition &definition :
         content.regionalOperations().baseSites)
    {
        state.baseSites.emplace(
            definition.id,
            RegionalBaseSiteState{
                definition.initiallyUnlocked,
                definition.uniqueFeatureInitiallyRepaired});
    }
    for (const RegionalOutpostDefinition &definition :
         content.regionalOperations().outposts)
    {
        state.outposts.emplace(
            definition.id,
            RegionalOutpostState{definition.initiallyUnlocked});
    }
    return state;
}

RegionalOperationsState parseRegionalOperations(
    const Json &value,
    const ContentRegistry &content,
    std::uint32_t schemaVersion)
{
    RegionalOperationsState state;
    state.activeBaseNodeId = RegionNodeDefinitionId{
        value.at("active_base_node_id").get<std::string>()};
    for (const RegionalBaseSiteDefinition &definition :
         content.regionalOperations().baseSites)
    {
        state.baseSites.emplace(
            definition.id,
            RegionalBaseSiteState{
                definition.initiallyUnlocked,
                definition.uniqueFeatureInitiallyRepaired});
    }
    if (schemaVersion >= 29)
    {
        std::set<RegionalBaseSiteDefinitionId> parsed;
        for (const Json &entry : value.at("base_sites"))
        {
            const RegionalBaseSiteDefinitionId definitionId{
                entry.at("definition_id").get<std::string>()};
            static_cast<void>(content.regionalBaseSite(definitionId));
            if (!parsed.insert(definitionId).second)
            {
                throw std::runtime_error{
                    "regional Base site state is duplicated"};
            }
            state.baseSites.at(definitionId).unlocked =
                entry.at("unlocked").get<bool>();
            if (schemaVersion >= 31)
            {
                state.baseSites.at(definitionId).uniqueFeatureRepaired =
                    entry.at("unique_feature_repaired").get<bool>();
            }
        }
        if (parsed.size() != content.regionalOperations().baseSites.size())
        {
            throw std::runtime_error{
                "regional Base site state is incomplete"};
        }
    }
    for (const Json &entry : value.at("outposts"))
    {
        const RegionalOutpostDefinitionId definitionId{
            entry.at("definition_id").get<std::string>()};
        RegionalOutpostState outpost{
            entry.at("unlocked").get<bool>(),
            entry.at("established").get<bool>(),
            entry.at("disrupted").get<bool>(),
            parseBaseProfessionCounts(entry.at("assigned_staff")),
            schemaVersion >= 28
                ? entry.at("shortcut_operations_since_restoration")
                      .get<std::uint32_t>()
                : 0U};
        if (!state.outposts.emplace(definitionId, outpost).second)
        {
            throw std::runtime_error{
                "regional outpost state is duplicated"};
        }
    }
    if (schemaVersion < 29)
    {
        for (const RegionalOutpostDefinition &definition :
             content.regionalOperations().outposts)
        {
            state.outposts.try_emplace(
                definition.id,
                RegionalOutpostState{definition.initiallyUnlocked});
        }
    }
    const RegionalBaseSiteDefinition &activeSite =
        baseSiteAtNode(content, state.activeBaseNodeId);
    if (schemaVersion >= 30)
    {
        const Json &core = value.at("technology_core");
        const auto coreSite = core.at("base_site_definition_id").get<std::string>();
        state.technologyCore = {
            core.at("instance_id").get<std::string>(),
            schemaVersion >= 45 && coreSite.empty()
                ? RegionalBaseSiteDefinitionId{} : RegionalBaseSiteDefinitionId{coreSite}};
    }
    else
    {
        state.technologyCore = {
            "technology_core.primary",
            activeSite.id};
    }
    static_cast<void>(content.regionNode(state.activeBaseNodeId));
    for (const auto &[definitionId, outpost] : state.outposts)
    {
        static_cast<void>(outpost);
        static_cast<void>(content.regionalOutpost(definitionId));
    }
    return state;
}

std::string_view baseSiegeOutcomeValue(BaseSiegeOutcome outcome)
{
    switch (outcome)
    {
    case BaseSiegeOutcome::None: return "none";
    case BaseSiegeOutcome::Defended: return "defended";
    case BaseSiegeOutcome::SoftFailure: return "soft_failure";
    }
    throw std::invalid_argument{"unknown Base siege outcome"};
}

BaseSiegeOutcome parseBaseSiegeOutcome(std::string_view value)
{
    if (value == "none") return BaseSiegeOutcome::None;
    if (value == "defended") return BaseSiegeOutcome::Defended;
    if (value == "soft_failure") return BaseSiegeOutcome::SoftFailure;
    throw std::runtime_error{"Base siege outcome is invalid"};
}

Json baseSiegeValue(const BaseSiegeState &state)
{
    return {
        {"raid_threat_units", state.raidThreatUnits},
        {"population_threat_units", state.populationThreatUnits},
        {"site_threat_units", state.siteThreatUnits},
        {"resolved_day_count", state.resolvedDayCount},
        {"safe_until_world_minute", state.safeUntilWorldMinute},
        {"warning_active", state.warningActive},
        {"warning_remaining_seconds", state.warningRemainingSeconds},
        {"siege_sequence", state.siegeSequence},
        {"auto_defense_preset_saved", state.autoDefensePresetSaved},
        {"last_outcome", baseSiegeOutcomeValue(state.lastOutcome)},
        {"last_security_spent", state.lastSecuritySpent},
        {"last_population_lost", state.lastPopulationLost}};
}

BaseSiegeState defaultBaseSiege(const WorldClockState &clock)
{
    BaseSiegeState state;
    static_cast<void>(clock);
    state.resolvedDayCount = 0U;
    state.safeUntilWorldMinute =
        kInitialWorldMinute + 3U * kWorldMinutesPerDay;
    return state;
}

BaseSiegeState parseBaseSiege(const Json &value)
{
    return {
        value.at("raid_threat_units").get<std::uint32_t>(),
        value.at("population_threat_units").get<std::uint32_t>(),
        value.at("site_threat_units").get<std::uint32_t>(),
        value.at("resolved_day_count").get<std::uint64_t>(),
        value.at("safe_until_world_minute").get<std::uint64_t>(),
        value.at("warning_active").get<bool>(),
        value.at("warning_remaining_seconds").get<std::uint32_t>(),
        value.at("siege_sequence").get<std::uint64_t>(),
        value.at("auto_defense_preset_saved").get<bool>(),
        parseBaseSiegeOutcome(
            value.at("last_outcome").get<std::string>()),
        value.at("last_security_spent").get<std::uint32_t>(),
        value.at("last_population_lost").get<std::uint32_t>()};
}

Json optionalProfessionValue(
    const std::optional<BaseResidentProfession> &profession)
{
    return profession.has_value()
        ? Json(baseResidentProfessionValue(*profession))
        : Json(nullptr);
}

std::optional<BaseResidentProfession> parseOptionalProfession(
    const Json &value)
{
    if (value.is_null())
    {
        return std::nullopt;
    }
    const auto profession = parseBaseResidentProfession(
        value.get<std::string>());
    if (!profession.has_value())
    {
        throw std::runtime_error{"Base resident profession is invalid"};
    }
    return profession;
}

BaseResidentProfession parseRequiredProfession(const Json &value)
{
    const auto profession = parseOptionalProfession(value);
    if (!profession.has_value())
    {
        throw std::runtime_error{"Base resident profession is required"};
    }
    return *profession;
}

std::string baseMoraleTierValue(BaseMoraleTier tier)
{
    switch (tier)
    {
    case BaseMoraleTier::Low:
        return "low";
    case BaseMoraleTier::Stable:
        return "stable";
    case BaseMoraleTier::High:
        return "high";
    }
    return "invalid";
}

std::optional<BaseMoraleTier> parseBaseMoraleTier(std::string_view value)
{
    if (value == "low") return BaseMoraleTier::Low;
    if (value == "stable") return BaseMoraleTier::Stable;
    if (value == "high") return BaseMoraleTier::High;
    return std::nullopt;
}

std::string baseMoraleTrendValue(BaseMoraleTrend trend)
{
    switch (trend)
    {
    case BaseMoraleTrend::Falling:
        return "falling";
    case BaseMoraleTrend::Steady:
        return "steady";
    case BaseMoraleTrend::Rising:
        return "rising";
    }
    return "invalid";
}

std::optional<BaseMoraleTrend> parseBaseMoraleTrend(std::string_view value)
{
    if (value == "falling") return BaseMoraleTrend::Falling;
    if (value == "steady") return BaseMoraleTrend::Steady;
    if (value == "rising") return BaseMoraleTrend::Rising;
    return std::nullopt;
}

Json baseMoraleValue(const BaseMoraleState &state)
{
    return {
        {"tier", baseMoraleTierValue(state.tier)},
        {"trend", baseMoraleTrendValue(state.trend)},
        {"resolved_day_count", state.resolvedDayCount},
        {"consecutive_low_days", state.consecutiveLowDays},
        {"supported_recovery_days", state.supportedRecoveryDays},
        {"pending_fulfilled_wish_count",
         state.pendingFulfilledWishCount},
        {"pending_missed_wish_count", state.pendingMissedWishCount},
        {"pending_positive_event_count", state.pendingPositiveEventCount},
        {"pending_negative_event_count", state.pendingNegativeEventCount},
        {"last_ledger", {
            {"day_index", state.lastLedger.dayIndex},
            {"shortfall_food", state.lastLedger.resourceShortfall.food},
            {"shortfall_hygiene",
             state.lastLedger.resourceShortfall.hygiene},
            {"shortfall_operations",
             state.lastLedger.resourceShortfall.morale},
            {"shortfall_security",
             state.lastLedger.resourceShortfall.security},
            {"bed_shortfall", state.lastLedger.bedShortfall},
            {"fulfilled_wish_count",
             state.lastLedger.fulfilledWishCount},
            {"missed_wish_count", state.lastLedger.missedWishCount},
            {"positive_event_count",
             state.lastLedger.positiveEventCount},
            {"negative_event_count",
             state.lastLedger.negativeEventCount},
            {"net_score", state.lastLedger.netScore}}}};
}

BaseMoraleState parseBaseMorale(const Json &value)
{
    const auto tier = parseBaseMoraleTier(
        value.at("tier").get<std::string>());
    const auto trend = parseBaseMoraleTrend(
        value.at("trend").get<std::string>());
    if (!tier.has_value() || !trend.has_value())
    {
        throw std::runtime_error{"Base morale enum is invalid"};
    }
    const Json &ledger = value.at("last_ledger");
    return BaseMoraleState{
        *tier,
        *trend,
        value.at("resolved_day_count").get<std::uint64_t>(),
        value.at("consecutive_low_days").get<std::uint64_t>(),
        value.at("supported_recovery_days").get<std::uint32_t>(),
        value.at("pending_fulfilled_wish_count").get<std::uint64_t>(),
        value.at("pending_missed_wish_count").get<std::uint64_t>(),
        value.at("pending_positive_event_count").get<std::uint64_t>(),
        value.at("pending_negative_event_count").get<std::uint64_t>(),
        BaseMoraleDailyLedger{
            ledger.at("day_index").get<std::uint64_t>(),
            BaseResourceBundle{
                ledger.at("shortfall_food").get<std::uint32_t>(),
                ledger.at("shortfall_hygiene").get<std::uint32_t>(),
                ledger.at("shortfall_operations").get<std::uint32_t>(),
                ledger.at("shortfall_security").get<std::uint32_t>()},
            ledger.at("bed_shortfall").get<std::uint32_t>(),
            ledger.at("fulfilled_wish_count").get<std::uint64_t>(),
            ledger.at("missed_wish_count").get<std::uint64_t>(),
            ledger.at("positive_event_count").get<std::uint64_t>(),
            ledger.at("negative_event_count").get<std::uint64_t>(),
            ledger.at("net_score").get<std::int32_t>()}};
}

Json baseCommunityEventValue(const BaseCommunityEventState &state)
{
    return {
        {"definition_id", state.definitionId.value()},
        {"cycle_index", state.cycleIndex}};
}

BaseCommunityEventState parseBaseCommunityEvent(const Json &value)
{
    return {
        BaseCommunityEventDefinitionId{
            value.at("definition_id").get<std::string>()},
        value.at("cycle_index").get<std::uint64_t>()};
}

Json wishInstanceValue(const BaseWishInstanceId &id)
{
    return {{"cycle_index", id.cycleIndex}, {"definition_id", id.definitionId.value()}};
}

BaseWishInstanceId parseWishInstance(const Json &value)
{
    return {value.at("cycle_index").get<std::uint64_t>(),
        BasePriorityDefinitionId{value.at("definition_id").get<std::string>()}};
}

Json wishSnapshotValue(const BaseWishExpeditionSnapshot &focus)
{
    return {{"wish", wishInstanceValue(focus.wish)},
        {"category", static_cast<unsigned>(focus.category)},
        {"required", focus.requiredContribution}, {"version", focus.assessmentVersion}};
}

BaseWishExpeditionSnapshot parseWishSnapshot(const Json &value)
{
    const auto category = value.at("category").get<unsigned>();
    if (category > static_cast<unsigned>(BaseSupplyCategory::Security))
        throw std::invalid_argument{"invalid wish contribution category"};
    return {parseWishInstance(value.at("wish")), static_cast<BaseSupplyCategory>(category),
        value.at("required").get<std::uint32_t>(), value.at("version").get<std::string>()};
}

Json basePriorityValue(const BasePriorityState &state, std::uint32_t schemaVersion)
{
    Json wishes = Json::array();
    for (const BasePriorityWishState &wish : state.wishes)
    {
        wishes.push_back({
            {"definition_id", wish.definitionId.value()},
            {"fulfilled", wish.fulfilled}});
    }
    Json result = {
        {"cycle_index", state.cycleIndex},
        {"frozen_population", state.frozenPopulation},
        {"wishes", std::move(wishes)},
        {"missed_cycle_count", state.missedCycleCount},
        {"migrated_legacy_cycle", state.migratedLegacyCycle}};
    if (schemaVersion >= 44)
        result["focus"] = state.focus ? wishInstanceValue(*state.focus) : Json(nullptr);
    return result;
}

BasePriorityState parseBasePriority(const Json &value, std::uint32_t schemaVersion)
{
    BasePriorityState state;
    state.cycleIndex = value.at("cycle_index").get<std::uint64_t>();
    state.frozenPopulation =
        value.at("frozen_population").get<std::uint32_t>();
    state.missedCycleCount =
        value.at("missed_cycle_count").get<std::uint64_t>();
    state.migratedLegacyCycle =
        value.at("migrated_legacy_cycle").get<bool>();
    if (schemaVersion >= 44 && !value.at("focus").is_null())
        state.focus = parseWishInstance(value.at("focus"));
    for (const Json &wish : value.at("wishes"))
    {
        state.wishes.push_back({
            BasePriorityDefinitionId{
                wish.at("definition_id").get<std::string>()},
            wish.at("fulfilled").get<bool>()});
    }
    return state;
}

Json legacyBasePriorityValue(const BasePriorityState &state)
{
    const BasePriorityWishState wish = state.wishes.empty()
        ? BasePriorityWishState{}
        : state.wishes.front();
    return {
        {"definition_id", wish.definitionId.value()},
        {"cycle_index", state.cycleIndex},
        {"fulfilled", wish.fulfilled},
        {"missed_cycle_count", state.missedCycleCount}};
}

BasePriorityState parseLegacyBasePriority(
    const Json &value,
    std::uint32_t frozenPopulation)
{
    return BasePriorityState{
        value.at("cycle_index").get<std::uint64_t>(),
        frozenPopulation,
        {{BasePriorityDefinitionId{
              value.at("definition_id").get<std::string>()},
          value.at("fulfilled").get<bool>()}},
        value.at("missed_cycle_count").get<std::uint64_t>(),
        true};
}

void reconcileLegacyBasePriority(
    BasePriorityState &state,
    const ContentRegistry &content)
{
    if (!state.migratedLegacyCycle || state.wishes.size() != 1U)
    {
        return;
    }
    const std::vector<BasePriorityDefinitionId> selected =
        selectBasePriorityDefinitions(
            state.cycleIndex,
            state.frozenPopulation,
            content);
    if (selected.size() == 1U &&
        selected.front() == state.wishes.front().definitionId)
    {
        // The legacy snapshot already represents the complete one-wish tier.
        // Keep its cycle and completion bit without treating it as a rerolled
        // compatibility exception.
        state.migratedLegacyCycle = false;
    }
}

Json vectorValue(Vec2 value)
{
    return {{"x", value.x}, {"y", value.y}};
}

Vec2 parseVector(const Json &value)
{
    return {value.at("x").get<float>(), value.at("y").get<float>()};
}

Json roundValue(const MagazineRoundRecord &round)
{
    Json value{{"definition_id", round.definitionId.value()}};
    if (round.reliefBatchId.has_value())
    {
        value["relief_batch_id"] = *round.reliefBatchId;
    }
    return value;
}

MagazineRoundRecord parseRound(const Json &value)
{
    MagazineRoundRecord round{
        ItemDefinitionId{value.at("definition_id").get<std::string>()},
        std::nullopt};
    if (value.contains("relief_batch_id"))
    {
        round.reliefBatchId = value.at("relief_batch_id").get<std::string>();
        if (round.reliefBatchId->empty())
        {
            throw std::runtime_error{"round relief batch ID is empty"};
        }
    }
    return round;
}

Json profilePayload(const ProfileState &profile, std::uint32_t schemaVersion)
{
    Json assets = Json::array();
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        Json location;
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset.location))
        {
            location = {
                {"type", "stored"},
                {"origin", {{"x", stored->origin.x}, {"y", stored->origin.y}}}};
            if (stored->container.kind == ProfileContainerKind::Stash)
            {
                location["container"] = {{"type", "stash"}};
            }
            else if (stored->container.kind ==
                     ProfileContainerKind::BaseIntake)
            {
                if (schemaVersion < 7)
                {
                    throw std::invalid_argument{
                        "legacy schema cannot represent Base intake"};
                }
                location["container"] = {{"type", "base_intake"}};
            }
            else
            {
                location["container"] = {
                    {"type", "asset_compartment"},
                    {"owner_asset_id", stored->container.ownerAssetId},
                    {"compartment", stored->container.compartmentIndex}};
            }
        }
        else if (const auto *equipped =
                     std::get_if<EquippedAssetLocation>(&asset.location))
        {
            location = {
                {"type", "equipped"},
                {"slot", slotName(equipped->slot)}};
        }
        else if (const auto *installed =
                     std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            if (schemaVersion == 1)
            {
                throw std::invalid_argument{
                    "schema v1 cannot represent installed magazines"};
            }
            location = {
                {"type", "installed_magazine"},
                {"weapon_asset_id", installed->weaponAssetId}};
        }
        else if (const auto *ground =
                     std::get_if<RaidGroundAssetLocation>(&asset.location))
        {
            if (schemaVersion == 1)
            {
                throw std::invalid_argument{
                    "schema v1 cannot represent Raid ground assets"};
            }
            location = {
                {"type", "raid_ground"},
                {"raid_id", ground->raidId},
                {"loot_slot_index", ground->lootSlotIndex}};
        }
        else if (const auto *ground =
                     std::get_if<BaseGroundAssetLocation>(&asset.location))
        {
            if (schemaVersion < 39)
            {
                throw std::invalid_argument{
                    "legacy schema cannot represent Base ground assets"};
            }
            location = {
                {"type", "base_ground"},
                {"base_site_definition_id",
                 ground->baseSiteDefinitionId.value()},
                {"position", {
                    {"x", ground->position.x},
                    {"y", ground->position.y}}}};
        }
        else if (const auto *service =
                     std::get_if<BaseServiceAssetLocation>(&asset.location))
        {
            if (schemaVersion < 10)
            {
                throw std::invalid_argument{
                    "legacy schema cannot represent Base service assets"};
            }
            location = {
                {"type", "base_service"},
                {"job_id", service->jobId}};
        }
        else if (const auto *lost =
                     std::get_if<LostRaidAssetLocation>(&asset.location))
        {
            if (schemaVersion < 24)
            {
                throw std::invalid_argument{
                    "legacy schema cannot represent lost Raid assets"};
            }
            location = {
                {"type", "lost_raid"},
                {"record_id", lost->recordId},
                {"source_slot", slotName(lost->sourceSlot)}};
        }
        else
        {
            if (schemaVersion < 25)
            {
                throw std::invalid_argument{
                    "legacy schema cannot represent recovery task assets"};
            }
            const auto &task =
                std::get<RecoveryTaskAssetLocation>(asset.location);
            location = {
                {"type", "recovery_task"},
                {"task_id", task.taskId},
                {"source_slot", slotName(task.sourceSlot)}};
        }

        Json value{
            {"instance_id", asset.instanceId},
            {"definition_id", asset.definitionId.value()},
            {"quantity", asset.quantity},
            {"orientation", orientationName(asset.orientation)},
            {"remaining_charges", asset.remainingCharges},
            {"location", std::move(location)}};
        if (asset.reliefBatchId.has_value())
        {
            value["relief_batch_id"] = *asset.reliefBatchId;
        }
        if (schemaVersion >= 2)
        {
            value["magazine_rounds"] = Json::array();
            for (const MagazineRoundRecord &round : asset.magazineRounds)
            {
                value["magazine_rounds"].push_back(roundValue(round));
            }
            value["chambered_round"] = asset.chamberedRound.has_value()
                ? roundValue(*asset.chamberedRound)
                : Json(nullptr);
        }
        if (schemaVersion >= 3)
        {
            value["current_maximum_durability"] =
                asset.currentMaximumDurability;
            value["current_durability"] = asset.currentDurability;
        }
        if (schemaVersion >= 5)
        {
            value["weapon_malfunction"] =
                malfunctionName(asset.weaponMalfunction);
        }
        assets.push_back(std::move(value));
    }

    Json transactions = Json::array();
    for (const std::string &transaction : profile.committedTransactions)
    {
        transactions.push_back(transaction);
    }

    Json payload{
        {"profile_id", profile.profileId},
        {"revision", profile.revision},
        {"currency", profile.currency},
        {"tutorial", tutorialName(profile.tutorial)},
        {"next_asset_id", profile.assets.nextAssetId()},
        {"committed_transactions", std::move(transactions)},
        {"assets", std::move(assets)}};
    if (schemaVersion == 1)
    {
        return payload;
    }

    payload["current_health"] = profile.currentHealth;
    if (schemaVersion >= 4)
    {
        payload["medical_status"] = {
            {"bleeding", static_cast<std::uint32_t>(
                profile.medicalStatus.bleeding)},
            {"light_bleeding_remaining_ms",
                profile.medicalStatus.lightBleedingRemainingMs},
            {"bleeding_damage_remaining_ms",
                profile.medicalStatus.bleedingDamageRemainingMs},
            {"painkiller_remaining_ms",
                profile.medicalStatus.painkillerRemainingMs},
            {"pain_scream_remaining_ms",
                profile.medicalStatus.painScreamRemainingMs}};
    }
    if (schemaVersion >= 7)
    {
        payload["base_resources"] = {
            {"food", profile.baseResources.pool.food},
            {"hygiene", profile.baseResources.pool.hygiene},
            {"morale", profile.baseResources.pool.morale},
            {"security", profile.baseResources.pool.security},
            {"shortfall_food", profile.baseResources.lastShortfall.food},
            {"shortfall_hygiene", profile.baseResources.lastShortfall.hygiene},
            {"shortfall_morale", profile.baseResources.lastShortfall.morale},
            {"shortfall_security", profile.baseResources.lastShortfall.security}};
        if (schemaVersion >= 8)
        {
            payload["base_resources"]["resolved_demand_cycle_count"] =
                profile.baseResources.resolvedDemandCycleCount;
        }
        else
        {
            payload["base_resources"]["resolved_raid_count"] =
                profile.baseResources.resolvedDemandCycleCount;
        }
    }
    if (schemaVersion >= 8)
    {
        payload["world_clock"] = {
            {"elapsed_world_minutes",
             profile.worldClock.elapsedWorldMinutes}};
    }
    if (schemaVersion >= 11)
    {
        payload["base_priority"] = schemaVersion >= 43
            ? basePriorityValue(profile.basePriority, schemaVersion)
            : legacyBasePriorityValue(profile.basePriority);
    }
    if (schemaVersion >= 12)
    {
        payload["base_population"] = {
            {"ordinary_residents",
             profile.basePopulation.ordinaryResidents},
            {"bed_capacity", profile.basePopulation.bedCapacity}};
        if (schemaVersion >= 16)
        {
            payload["base_population"]["injured_residents"] =
                profile.basePopulation.injuredResidents;
        }
        if (schemaVersion >= 19)
        {
            payload["base_population"]["profession_residents"] =
                baseProfessionCountsValue(
                    profile.basePopulation.professionResidents);
            payload["base_population"]["injured_by_profession"] =
                baseProfessionCountsValue(
                    profile.basePopulation.injuredByProfession);
        }
    }
    if (schemaVersion >= 14)
    {
        payload["base_construction"] = {
            {"material_units", profile.baseConstruction.materialUnits},
            {"dormitory_level", profile.baseConstruction.dormitoryLevel}};
        if (schemaVersion >= 19)
        {
            payload["base_construction"]["workshop_level"] =
                profile.baseConstruction.workshopLevel;
            payload["base_construction"]["medical_level"] =
                profile.baseConstruction.medicalLevel;
        }
        if (schemaVersion >= 30)
        {
            appendFacilityState(
                payload["base_construction"],
                profile.baseConstruction);
        }
        if (profile.baseConstruction.activeProject.has_value())
        {
            const ActiveBaseConstructionProject &project =
                *profile.baseConstruction.activeProject;
            payload["base_construction"]["active_project"] = {
                {"definition_id", project.definitionId.value()},
                {"locked_material_units", project.lockedMaterialUnits},
                {"committed_workers", project.committedWorkers},
                {"started_world_minute", project.startedWorldMinute},
                {"completion_world_minute", project.completionWorldMinute}};
        }
        else
        {
            payload["base_construction"]["active_project"] = nullptr;
        }
    }
    if (schemaVersion >= 40)
    {
        Json sites = Json::array();
        for (const auto &[siteDefinitionId, placements] :
             profile.baseFacilityLayout.placements)
        {
            Json facilities = Json::array();
            for (const auto &[facilityDefinitionId, normalizedCenter] :
                 placements)
            {
                facilities.push_back({
                    {"definition_id", facilityDefinitionId.value()},
                    {"normalized_x", normalizedCenter.x},
                    {"normalized_y", normalizedCenter.y}});
            }
            sites.push_back({
                {"site_definition_id", siteDefinitionId.value()},
                {"facilities", std::move(facilities)}});
        }
        payload["base_facility_layout"] = {
            {"sites", std::move(sites)}};
    }
    if (schemaVersion >= 15)
    {
        Json assignments = Json::array();
        for (const auto &[definitionId, category] :
             profile.baseSupplyPolicy.assignments)
        {
            assignments.push_back({
                {"item_definition_id", definitionId.value()},
                {"category", baseSupplyCategoryName(category)}});
        }
        payload["base_supply_policy"] = {
            {"assignments", std::move(assignments)}};
    }
    if (schemaVersion >= 16)
    {
        if (profile.residentMedical.activeTreatment.has_value())
        {
            const ActiveResidentTreatment &treatment =
                *profile.residentMedical.activeTreatment;
            payload["resident_medical"] = {
                {"active_treatment", {
                    {"job_id", treatment.jobId},
                    {"started_world_minute", treatment.startedWorldMinute},
                    {"completion_world_minute",
                     treatment.completionWorldMinute},
                     {"consumed_contribution",
                      treatment.consumedContribution}}}};
            if (schemaVersion >= 19)
            {
                Json &value = payload["resident_medical"]["active_treatment"];
                value["patient_profession"] =
                    baseResidentProfessionValue(treatment.patientProfession);
                value["worker_profession"] =
                    baseResidentProfessionValue(treatment.workerProfession);
            }
        }
        else
        {
            payload["resident_medical"] = {
                {"active_treatment", nullptr}};
        }
    }
    if (schemaVersion >= 17)
    {
        if (profile.baseManufacturing.activeOrder.has_value())
        {
            const BaseManufacturingOrder &order =
                *profile.baseManufacturing.activeOrder;
            payload["base_manufacturing"] = {
                {"active_order", {
                    {"job_id", order.jobId},
                    {"recipe_definition_id",
                     order.recipeDefinitionId.value()},
                     {"committed_workers", order.committedWorkers},
                    {"started_world_minute", order.startedWorldMinute},
                    {"completion_world_minute", order.completionWorldMinute},
                    {"input_asset_ids", order.inputAssetIds},
                    {"output_asset_id", order.outputAssetId},
                     {"output_ready", order.outputReady}}}};
            if (schemaVersion >= 19)
            {
                payload["base_manufacturing"]["active_order"]
                    ["worker_profession"] =
                        baseResidentProfessionValue(order.workerProfession);
            }
        }
        else
        {
            payload["base_manufacturing"] = {{"active_order", nullptr}};
        }
    }
    if (schemaVersion >= 18)
    {
        payload["base_morale"] = baseMoraleValue(profile.baseMorale);
        payload["base_community_event"] = baseCommunityEventValue(
            profile.baseCommunityEvent);
    }
    if (schemaVersion >= 19)
    {
        payload["base_workforce"] = {
            {"workshop_worker", optionalProfessionValue(
                profile.baseWorkforce.workshopWorker)},
            {"medical_worker", optionalProfessionValue(
                profile.baseWorkforce.medicalWorker)}};
    }
    if (schemaVersion >= 20)
    {
        payload["raid_intelligence_archive"] = Json::array();
        for (const auto &[mapId, counts] : profile.raidIntelligence.counts)
        {
            payload["raid_intelligence_archive"].push_back({
                {"map_definition_id", mapId.value()},
                {"transport", counts[raidIntelligenceCategoryIndex(
                    RaidIntelligenceCategory::Transport)]},
                {"resource", counts[raidIntelligenceCategoryIndex(
                    RaidIntelligenceCategory::Resource)]},
                {"enemy", counts[raidIntelligenceCategoryIndex(
                    RaidIntelligenceCategory::Enemy)]}});
        }
    }
    if (schemaVersion >= 23)
    {
        payload["raid_interior_intelligence"] = Json::array();
        for (const RaidSpaceDefinitionId &interiorId :
             profile.raidInteriorIntelligence.knownLayouts)
        {
            payload["raid_interior_intelligence"].push_back(
                interiorId.value());
        }
    }
    if (schemaVersion >= 24)
    {
        payload["lost_raid_records"] = Json::array();
        for (const auto &[recordId, record] : profile.lostRaidRecords)
        {
            static_cast<void>(recordId);
            payload["lost_raid_records"].push_back({
                {"record_id", record.recordId},
                {"raid_id", record.raidId},
                {"settlement_id", record.settlementId},
                {"map_definition_id", record.mapDefinitionId.value()},
                {"difficulty", record.difficulty},
                {"outcome", raidOutcomeName(record.outcome)},
                {"created_world_minute", record.createdWorldMinute},
                {"subsequent_raid_settlement_count",
                 record.subsequentRaidSettlementCount}});
        }
    }
    if (schemaVersion >= 27)
    {
        payload["regional_operations"] =
            regionalOperationsValue(
                profile.regionalOperations,
                schemaVersion >= 28,
                schemaVersion >= 29,
                schemaVersion >= 30,
                schemaVersion >= 31);
    }
    else
    {
        bool hasUnrepresentableRegionalState =
            profile.regionalOperations.activeBaseNodeId !=
                RegionNodeDefinitionId{
                    "region_node.base.greyline_yard"};
        for (const auto &[outpostId, outpost] :
             profile.regionalOperations.outposts)
        {
            static_cast<void>(outpostId);
            hasUnrepresentableRegionalState =
                hasUnrepresentableRegionalState || outpost.established ||
                outpost.disrupted ||
                std::any_of(
                    outpost.assignedStaff.begin(),
                    outpost.assignedStaff.end(),
                    [](std::uint32_t count) { return count != 0U; });
        }
        if (hasUnrepresentableRegionalState)
        {
            throw std::invalid_argument{
                "legacy schema cannot represent regional operations"};
        }
    }
    if (schemaVersion >= 32)
    {
        payload["base_siege"] = baseSiegeValue(profile.baseSiege);
    }
    if (schemaVersion >= 45)
    {
        Json plots = Json::array();
        for (const auto &[region, plot] : profile.homeFounding.plots)
            plots.push_back({{"region", region.value()}, {"plot", plot}});
        payload["home_founding"] = {{"established", profile.homeFounding.established},
            {"hints_dismissed", profile.homeFounding.hintsDismissed}, {"plots", plots},
            {"layout_version", profile.homeFounding.layoutVersion}};
    }
    else if (profile.homeFounding != HomeFoundingState{})
        throw std::invalid_argument{"legacy schema cannot represent Home founding"};
    if (schemaVersion >= 42)
    {
        Json sites = Json::array();
        for (const auto &[siteId, snapshot] : profile.homePerimeter.sites)
        {
            Json enemies = Json::array();
            for (const HomePerimeterEnemySnapshot &enemy : snapshot.enemies)
            {
                enemies.push_back({
                    {"local_id", enemy.localId},
                    {"spawn_x", enemy.spawnPosition.x},
                    {"spawn_y", enemy.spawnPosition.y},
                    {"position_x", enemy.position.x},
                    {"position_y", enemy.position.y},
                    {"size_x", enemy.size.x},
                    {"size_y", enemy.size.y},
                    {"maximum_health", enemy.maximumHealth},
                    {"health", enemy.health}});
            }
            sites.push_back({
                {"site_definition_id", siteId.value()},
                {"cycle_index", snapshot.cycleIndex},
                {"seed", snapshot.seed},
                {"enemies", std::move(enemies)},
                {"loot_asset_ids", snapshot.lootAssetIds}});
        }
        Json committedResults = Json::array();
        for (const std::string &resultId :
             profile.homePerimeter.committedResults)
            committedResults.push_back(resultId);
        Json active = nullptr;
        if (profile.homePerimeter.activeOuting.has_value())
        {
            active = {
                {"outing_id", profile.homePerimeter.activeOuting->outingId},
                {"site_definition_id",
                 profile.homePerimeter.activeOuting->baseSiteDefinitionId.value()},
                {"cycle_index",
                 profile.homePerimeter.activeOuting->cycleIndex}};
        }
        payload["home_perimeter"] = {
            {"sites", std::move(sites)},
            {"active_outing", std::move(active)},
            {"committed_results", std::move(committedResults)}};
    }
    else if (!profile.homePerimeter.sites.empty() ||
             profile.homePerimeter.activeOuting.has_value() ||
             !profile.homePerimeter.committedResults.empty())
    {
        throw std::invalid_argument{
            "legacy schema cannot represent Home perimeter state"};
    }
    if (schemaVersion >= 25)
    {
        payload["next_recovery_task_id"] = profile.nextRecoveryTaskId;
        if (profile.recoveryTask.has_value())
        {
            const RecoveryTask &task = *profile.recoveryTask;
            payload["recovery_task"] = {
                {"task_id", task.taskId},
                {"record_id", task.sourceRecord.recordId},
                {"raid_id", task.sourceRecord.raidId},
                {"settlement_id", task.sourceRecord.settlementId},
                {"map_definition_id",
                 task.sourceRecord.mapDefinitionId.value()},
                {"difficulty", task.sourceRecord.difficulty},
                {"outcome", raidOutcomeName(task.sourceRecord.outcome)},
                {"created_world_minute",
                 task.sourceRecord.createdWorldMinute},
                {"subsequent_raid_settlement_count",
                 task.sourceRecord.subsequentRaidSettlementCount},
                {"paid_currency", task.paidCurrency},
                {"started_world_minute", task.startedWorldMinute},
                {"completion_world_minute", task.completionWorldMinute},
                {"ready_for_collection", task.readyForCollection},
                {"recovered_asset_ids", task.recoveredAssetIds}};
        }
        else
        {
            payload["recovery_task"] = nullptr;
        }
    }
    if (schemaVersion >= 10)
    {
        payload["next_base_service_job_id"] =
            profile.nextBaseServiceJobId;
        if (profile.gunsmithMaintenanceJob.has_value())
        {
            const GunsmithMaintenanceJob &job =
                *profile.gunsmithMaintenanceJob;
            payload["gunsmith_maintenance_job"] = {
                {"job_id", job.jobId},
                {"weapon_asset_id", job.weaponAssetId},
                {"return_origin", {
                    {"x", job.returnOrigin.x},
                    {"y", job.returnOrigin.y}}},
                {"started_world_minute", job.startedWorldMinute},
                {"completion_world_minute", job.completionWorldMinute},
                {"paid_currency", job.paidCurrency},
                {"target_factory_durability_centi",
                 job.targetFactoryDurabilityCenti}};
        }
        else
        {
            payload["gunsmith_maintenance_job"] = nullptr;
        }
    }
    payload["committed_settlements"] = Json::array();
    for (const std::string &settlement : profile.committedSettlements)
    {
        payload["committed_settlements"].push_back(settlement);
    }
    if (schemaVersion >= 13)
    {
        payload["committed_rescues"] = Json::array();
        for (const RescueDefinitionId &rescue : profile.committedRescues)
        {
            payload["committed_rescues"].push_back(rescue.value());
        }
    }

    if (profile.pendingRaid.has_value())
    {
        const PendingRaidSnapshot &raid = *profile.pendingRaid;
        if (schemaVersion < 26 && raid.selfRecovery.has_value())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent Raid self-recovery"};
        }
        if (schemaVersion < 28 && raid.outpostRestoration.has_value())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent outpost restoration"};
        }
        if (schemaVersion < 29 && raid.baseSiteClearance.has_value())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent Base site clearance"};
        }
        if (schemaVersion < 33 && raid.basePerimeterSweep.has_value())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent Base perimeter sweep"};
        }
        if (schemaVersion < 38 && raid.highRiskCrisis.has_value())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent a frozen high-risk crisis"};
        }
        Json enemies = Json::array();
        if (schemaVersion < 37 && !raid.encounterGroups.empty())
        {
            throw std::invalid_argument{
                "legacy schema cannot represent Raid encounter groups"};
        }
        for (const RaidEnemySnapshot &enemy : raid.enemies)
        {
            Json enemyValue{
                {"position", vectorValue(enemy.position)},
                {"size", vectorValue(enemy.size)},
                {"maximum_health", enemy.maximumHealth}};
            if (schemaVersion >= 22)
            {
                enemyValue["space_id"] = enemy.spaceId.value();
            }
            if (schemaVersion >= 37)
            {
                enemyValue["encounter_group_instance_id"] =
                    enemy.encounterGroupInstanceId;
            }
            enemies.push_back(std::move(enemyValue));
        }
        Json encounterGroups = Json::array();
        if (schemaVersion >= 37)
        {
            for (const RaidEncounterGroupSnapshot &group :
                 raid.encounterGroups)
            {
                Json patrolPoints = Json::array();
                for (Vec2 point : group.patrolPoints)
                    patrolPoints.push_back(vectorValue(point));
                encounterGroups.push_back({
                    {"instance_id", group.instanceId},
                    {"definition_id", group.definitionId},
                    {"kind", static_cast<std::uint32_t>(group.kind)},
                    {"space_id", group.spaceId.value()},
                    {"home_position", vectorValue(group.homePosition)},
                    {"patrol_points", std::move(patrolPoints)},
                    {"member_enemy_indices", group.memberEnemyIndices},
                    {"activation_distance", group.activationDistance}});
            }
        }
        Json loot = Json::array();
        for (const RaidLootSnapshot &entry : raid.loot)
        {
            Json lootEntry{
                {"asset_id", entry.assetId},
                {"slot_index", entry.slotIndex},
                {"position", vectorValue(entry.position)},
                {"requires_high_risk", entry.requiresHighRisk}};
            if (schemaVersion >= 7)
            {
                lootEntry["definition_id"] = entry.definitionId.value();
                lootEntry["quantity"] = entry.quantity;
                lootEntry["collected"] = entry.collected;
            }
            if (schemaVersion >= 22)
            {
                lootEntry["space_id"] = entry.spaceId.value();
            }
            if (schemaVersion >= 36)
            {
                lootEntry["resource_point_instance_id"] =
                    entry.resourcePointInstanceId;
                lootEntry["resource_point_slot_index"] =
                    entry.resourcePointSlotIndex;
            }
            loot.push_back(std::move(lootEntry));
        }
        payload["pending_raid"] = {
            {"raid_id", raid.raidId},
            {"settlement_id", raid.settlementId},
            {"rules_version", raid.rulesVersion},
            {"map_definition_id", raid.mapDefinitionId.value()},
            {"seed", raid.seed},
            {"spawn_extraction_pair_id", raid.spawnExtractionPairId},
            {"enemy_deployment_id", raid.enemyDeploymentId.value()},
            {"player_spawn", vectorValue(raid.playerSpawn)},
            {"extraction_point", {
                {"position", vectorValue(raid.extractionPoint.position)},
                {"size", vectorValue(raid.extractionPoint.size)}}},
            {"enemies", std::move(enemies)},
            {"encounter_groups", std::move(encounterGroups)},
            {"loot", std::move(loot)},
            {"carried_root_asset_ids", raid.carriedRootAssetIds},
            {"starting_health", raid.startingHealth}};
        if (schemaVersion >= 4)
        {
            payload["pending_raid"]["starting_medical_status"] = {
                {"bleeding", static_cast<std::uint32_t>(
                    raid.startingMedicalStatus.bleeding)},
                {"light_bleeding_remaining_ms",
                    raid.startingMedicalStatus.lightBleedingRemainingMs},
                {"bleeding_damage_remaining_ms",
                    raid.startingMedicalStatus.bleedingDamageRemainingMs},
                {"painkiller_remaining_ms",
                    raid.startingMedicalStatus.painkillerRemainingMs},
                {"pain_scream_remaining_ms",
                    raid.startingMedicalStatus.painScreamRemainingMs}};
        }
        if (schemaVersion >= 20)
        {
            payload["pending_raid"]["intelligence"] = {
                {"transport", raid.intelligence.has(
                    RaidIntelligenceCategory::Transport)},
                {"resource", raid.intelligence.has(
                    RaidIntelligenceCategory::Resource)},
                {"enemy", raid.intelligence.has(
                    RaidIntelligenceCategory::Enemy)}};
        }
        if (schemaVersion >= 21)
        {
            Json blockers = Json::array();
            for (const ContentRect &blocker :
                 raid.spatialLayout.ballisticBlockers)
            {
                blockers.push_back({
                    {"position", vectorValue(blocker.position)},
                    {"size", vectorValue(blocker.size)}});
            }
            payload["pending_raid"]["spatial_layout"] = {
                {"ballistic_blockers", std::move(blockers)},
                {"generation_attempt",
                 raid.spatialLayout.generationAttempt},
                {"layout_hash", raid.spatialLayout.layoutHash},
                {"used_fallback", raid.spatialLayout.usedFallback}};
            if (schemaVersion >= 34)
            {
                Json roads = Json::array();
                for (const RaidOutdoorRoadCell &cell :
                     raid.spatialLayout.roadCells)
                {
                    roads.push_back({
                        {"column", cell.column},
                        {"row", cell.row},
                        {"kind", static_cast<std::uint32_t>(cell.kind)}});
                }
                payload["pending_raid"]["spatial_layout"]
                    ["layout_version"] = raid.spatialLayout.layoutVersion;
                payload["pending_raid"]["spatial_layout"]
                    ["road_cells"] = std::move(roads);
                payload["pending_raid"]["spatial_layout"]
                    ["fallback_reason"] = static_cast<std::uint32_t>(
                        raid.spatialLayout.fallbackReason);
            }
            if (schemaVersion >= 35)
            {
                Json districts = Json::array();
                for (const RaidDistrictSnapshot &district :
                     raid.spatialLayout.districts)
                {
                    Json cells = Json::array();
                    for (const RaidGridSpan &cell : district.cells)
                    {
                        cells.push_back({
                            {"row", cell.row},
                            {"first_column", cell.firstColumn},
                            {"length", cell.length}});
                    }
                    districts.push_back({
                        {"instance_id", district.instanceId},
                        {"definition_id", district.definitionId},
                        {"display_name", district.displayName},
                        {"kind", static_cast<std::uint32_t>(district.kind)},
                        {"cells", std::move(cells)},
                        {"label_position", vectorValue(
                            district.labelPosition)}});
                }
                Json terrain = Json::array();
                for (const RaidTerrainSpan &span :
                     raid.spatialLayout.terrainSpans)
                {
                    terrain.push_back({
                        {"row", span.row},
                        {"first_column", span.firstColumn},
                        {"length", span.length},
                        {"kind", static_cast<std::uint32_t>(span.kind)}});
                }
                Json props = Json::array();
                for (const RaidOutdoorPropSnapshot &prop :
                     raid.spatialLayout.props)
                {
                    props.push_back({
                        {"instance_id", prop.instanceId},
                        {"kind", static_cast<std::uint32_t>(prop.kind)},
                        {"state", static_cast<std::uint32_t>(prop.state)},
                        {"bounds", {
                            {"position", vectorValue(prop.bounds.position)},
                            {"size", vectorValue(prop.bounds.size)}}},
                        {"quarter_turns", prop.quarterTurns},
                        {"collidable", prop.collidable}});
                }
                Json anchors = Json::array();
                for (const RaidAnchorPlacementSnapshot &anchor :
                     raid.spatialLayout.anchorPlacements)
                {
                    anchors.push_back({
                        {"id", anchor.id},
                        {"kind", static_cast<std::uint32_t>(anchor.kind)},
                        {"bounds", {
                            {"position", vectorValue(anchor.bounds.position)},
                            {"size", vectorValue(anchor.bounds.size)}}},
                        {"district_instance_id",
                         anchor.districtInstanceId}});
                }
                Json landmarks = Json::array();
                for (const RaidLandmarkPlacementSnapshot &landmark :
                     raid.spatialLayout.landmarks)
                {
                    Json structures = Json::array();
                    for (ContentRect structure : landmark.structures)
                    {
                        structures.push_back({
                            {"position", vectorValue(structure.position)},
                            {"size", vectorValue(structure.size)}});
                    }
                    Json sockets = Json::array();
                    for (Vec2 socket : landmark.roadSockets)
                        sockets.push_back(vectorValue(socket));
                    landmarks.push_back({
                        {"definition_id", landmark.definitionId},
                        {"display_name", landmark.displayName},
                        {"bounds", {
                            {"position", vectorValue(landmark.bounds.position)},
                            {"size", vectorValue(landmark.bounds.size)}}},
                        {"district_instance_id",
                         landmark.districtInstanceId},
                        {"structures", std::move(structures)},
                        {"road_sockets", std::move(sockets)}});
                }
                Json &layout = payload["pending_raid"]["spatial_layout"];
                layout["districts"] = std::move(districts);
                layout["terrain_spans"] = std::move(terrain);
                layout["props"] = std::move(props);
                layout["anchor_placements"] = std::move(anchors);
                layout["landmarks"] = std::move(landmarks);
                if (schemaVersion >= 36)
                {
                    Json resourcePoints = Json::array();
                    for (const RaidResourcePointSnapshot &resourcePoint :
                         raid.spatialLayout.resourcePoints)
                    {
                        resourcePoints.push_back({
                            {"instance_id", resourcePoint.instanceId},
                            {"definition_id", resourcePoint.definitionId},
                            {"display_name", resourcePoint.displayName},
                            {"kind", static_cast<std::uint32_t>(
                                resourcePoint.kind)},
                            {"loot_table_id",
                             resourcePoint.lootTableId.value()},
                            {"risk_tier", resourcePoint.riskTier},
                            {"capacity", resourcePoint.capacity},
                            {"bounds", {
                                {"position", vectorValue(
                                    resourcePoint.bounds.position)},
                                {"size", vectorValue(
                                    resourcePoint.bounds.size)}}},
                            {"district_instance_id",
                             resourcePoint.districtInstanceId},
                            {"landmark_definition_id",
                             resourcePoint.landmarkDefinitionId}});
                    }
                    layout["resource_points"] = std::move(resourcePoints);
                }
            }
        }
        if (schemaVersion >= 38)
        {
            if (raid.highRiskCrisis.has_value())
            {
                const RaidHighRiskCrisisSnapshot &crisis =
                    *raid.highRiskCrisis;
                Json pressureSpawns = Json::array();
                for (const RaidHighRiskPressureSpawnSnapshot &spawn :
                     crisis.pressureSpawns)
                {
                    pressureSpawns.push_back({
                        {"anchor_id", spawn.anchorId},
                        {"position", vectorValue(spawn.position)},
                        {"size", vectorValue(spawn.size)},
                        {"maximum_health", spawn.maximumHealth}});
                }
                payload["pending_raid"]["high_risk_crisis"] = {
                    {"definition_id", crisis.definitionId},
                    {"display_name", crisis.displayName},
                    {"warning", crisis.warning},
                    {"district_instance_id", crisis.districtInstanceId},
                    {"resource_point_instance_id",
                     crisis.resourcePointInstanceId},
                    {"focus_area", {
                        {"position", vectorValue(crisis.focusArea.position)},
                        {"size", vectorValue(crisis.focusArea.size)}}},
                    {"initial_wave_delay_seconds",
                     crisis.initialWaveDelaySeconds},
                    {"wave_interval_seconds", crisis.waveIntervalSeconds},
                    {"wave_size", crisis.waveSize},
                    {"active_enemy_cap", crisis.activeEnemyCap},
                    {"advanced_loot_table_id",
                     crisis.advancedLootTableId.value()},
                    {"pressure_spawns", std::move(pressureSpawns)}};
            }
            else
            {
                payload["pending_raid"]["high_risk_crisis"] = nullptr;
            }
        }
        if (schemaVersion >= 22)
        {
            Json interiors = Json::array();
            for (const RaidInteriorSnapshot &interior : raid.interiors)
            {
                Json blockers = Json::array();
                for (const ContentRect &blocker : interior.ballisticBlockers)
                {
                    blockers.push_back({
                        {"position", vectorValue(blocker.position)},
                        {"size", vectorValue(blocker.size)}});
                }
                Json interiorValue{
                    {"id", interior.id.value()},
                    {"display_name", interior.displayName},
                    {"world_size", vectorValue(interior.worldSize)},
                    {"exterior_entrance", {
                        {"position", vectorValue(
                            interior.exteriorEntrance.position)},
                        {"size", vectorValue(interior.exteriorEntrance.size)}}},
                    {"exterior_return", vectorValue(interior.exteriorReturn)},
                    {"interior_spawn", vectorValue(interior.interiorSpawn)},
                    {"interior_exit", {
                        {"position", vectorValue(interior.interiorExit.position)},
                        {"size", vectorValue(interior.interiorExit.size)}}},
                    {"ballistic_blockers", std::move(blockers)}};
                if (schemaVersion >= 23)
                {
                    interiorValue["layout_known"] = interior.layoutKnown;
                }
                interiors.push_back(std::move(interiorValue));
            }
            payload["pending_raid"]["interiors"] = std::move(interiors);
        }
        if (schemaVersion >= 9)
        {
            payload["pending_raid"]["travel"] = {
                {"outbound_minutes", raid.travel.outboundMinutes},
                {"return_minutes", raid.travel.returnMinutes},
                {"failure_regroup_minutes",
                 raid.travel.failureRegroupMinutes},
                {"starting_world_clock", {
                    {"elapsed_world_minutes",
                     raid.travel.startingWorldClock.elapsedWorldMinutes}}},
                {"starting_base_resources", {
                    {"food", raid.travel.startingBaseResources.pool.food},
                    {"hygiene",
                     raid.travel.startingBaseResources.pool.hygiene},
                    {"morale",
                     raid.travel.startingBaseResources.pool.morale},
                    {"security",
                     raid.travel.startingBaseResources.pool.security},
                    {"shortfall_food",
                     raid.travel.startingBaseResources.lastShortfall.food},
                    {"shortfall_hygiene",
                     raid.travel.startingBaseResources.lastShortfall.hygiene},
                    {"shortfall_morale",
                     raid.travel.startingBaseResources.lastShortfall.morale},
                    {"shortfall_security",
                     raid.travel.startingBaseResources.lastShortfall.security},
                    {"resolved_demand_cycle_count",
                     raid.travel.startingBaseResources
                         .resolvedDemandCycleCount}}}};
            if (schemaVersion >= 11)
            {
                payload["pending_raid"]["travel"]
                    ["starting_base_priority"] = schemaVersion >= 43
                        ? basePriorityValue(
                              raid.travel.startingBasePriority, schemaVersion)
                        : legacyBasePriorityValue(
                              raid.travel.startingBasePriority);
            }
            if (schemaVersion >= 18)
            {
                payload["pending_raid"]["travel"]["starting_base_morale"] =
                    baseMoraleValue(raid.travel.startingBaseMorale);
                payload["pending_raid"]["travel"]
                    ["starting_base_community_event"] =
                        baseCommunityEventValue(
                            raid.travel.startingBaseCommunityEvent);
            }
            if (schemaVersion >= 20)
            {
                Json startingArchive = Json::array();
                for (const auto &[mapId, counts] :
                     raid.travel.startingRaidIntelligence.counts)
                {
                    startingArchive.push_back({
                        {"map_definition_id", mapId.value()},
                        {"transport", counts[raidIntelligenceCategoryIndex(
                            RaidIntelligenceCategory::Transport)]},
                        {"resource", counts[raidIntelligenceCategoryIndex(
                            RaidIntelligenceCategory::Resource)]},
                        {"enemy", counts[raidIntelligenceCategoryIndex(
                            RaidIntelligenceCategory::Enemy)]}});
                }
                payload["pending_raid"]["travel"]
                    ["starting_raid_intelligence_archive"] =
                        std::move(startingArchive);
            }
            if (schemaVersion >= 14)
            {
                Json startingConstruction{
                    {"material_units",
                     raid.travel.startingBaseConstruction.materialUnits},
                    {"dormitory_level",
                     raid.travel.startingBaseConstruction.dormitoryLevel}};
                if (schemaVersion >= 19)
                {
                    startingConstruction["workshop_level"] =
                        raid.travel.startingBaseConstruction.workshopLevel;
                    startingConstruction["medical_level"] =
                        raid.travel.startingBaseConstruction.medicalLevel;
                }
                if (schemaVersion >= 30)
                {
                    appendFacilityState(
                        startingConstruction,
                        raid.travel.startingBaseConstruction);
                }
                if (raid.travel.startingBaseConstruction.activeProject
                        .has_value())
                {
                    const ActiveBaseConstructionProject &project =
                        *raid.travel.startingBaseConstruction.activeProject;
                    startingConstruction["active_project"] = {
                        {"definition_id", project.definitionId.value()},
                        {"locked_material_units",
                         project.lockedMaterialUnits},
                        {"committed_workers", project.committedWorkers},
                        {"started_world_minute",
                         project.startedWorldMinute},
                        {"completion_world_minute",
                         project.completionWorldMinute}};
                }
                else
                {
                    startingConstruction["active_project"] = nullptr;
                }
                payload["pending_raid"]["travel"]
                    ["starting_base_construction"] =
                        std::move(startingConstruction);
                payload["pending_raid"]["travel"]
                    ["starting_bed_capacity"] =
                        raid.travel.startingBedCapacity;
                if (schemaVersion >= 19)
                {
                    payload["pending_raid"]["travel"]
                        ["starting_base_workforce"] = {
                            {"workshop_worker", optionalProfessionValue(
                                raid.travel.startingBaseWorkforce
                                    .workshopWorker)},
                            {"medical_worker", optionalProfessionValue(
                                raid.travel.startingBaseWorkforce
                                    .medicalWorker)}};
                }
                if (schemaVersion >= 16)
                {
                    payload["pending_raid"]["travel"]
                        ["starting_injured_residents"] =
                            raid.travel.startingInjuredResidents;
                    if (schemaVersion >= 19)
                    {
                        payload["pending_raid"]["travel"]
                            ["starting_injured_by_profession"] =
                                baseProfessionCountsValue(
                                    raid.travel.startingInjuredByProfession);
                    }
                    if (raid.travel.startingResidentMedical.activeTreatment
                            .has_value())
                    {
                        const ActiveResidentTreatment &treatment =
                            *raid.travel.startingResidentMedical
                                 .activeTreatment;
                        payload["pending_raid"]["travel"]
                            ["starting_resident_treatment"] = {
                                {"job_id", treatment.jobId},
                                {"started_world_minute",
                                 treatment.startedWorldMinute},
                                {"completion_world_minute",
                                 treatment.completionWorldMinute},
                                 {"consumed_contribution",
                                  treatment.consumedContribution}};
                        if (schemaVersion >= 19)
                        {
                            Json &value = payload["pending_raid"]["travel"]
                                ["starting_resident_treatment"];
                            value["patient_profession"] =
                                baseResidentProfessionValue(
                                    treatment.patientProfession);
                            value["worker_profession"] =
                                baseResidentProfessionValue(
                                    treatment.workerProfession);
                        }
                    }
                    else
                    {
                        payload["pending_raid"]["travel"]
                            ["starting_resident_treatment"] = nullptr;
                    }
                }
            }
        }
        if (schemaVersion >= 13)
        {
            if (raid.rescue.has_value())
            {
                payload["pending_raid"]["rescue"] = {
                    {"definition_id", raid.rescue->definitionId.value()},
                    {"subject_kind", "ordinary_residents"},
                    {"transfer_point", {
                        {"position", vectorValue(
                            raid.rescue->transferPoint.position)},
                        {"size", vectorValue(
                            raid.rescue->transferPoint.size)}}},
                    {"interaction_duration_seconds",
                     raid.rescue->interactionDurationSeconds},
                    {"ordinary_resident_count",
                     raid.rescue->ordinaryResidentCount},
                    {"secured", raid.rescue->secured}};
                if (schemaVersion >= 16)
                {
                    payload["pending_raid"]["rescue"]
                        ["injured_resident_count"] =
                            raid.rescue->injuredResidentCount;
                }
                if (schemaVersion >= 19)
                {
                    payload["pending_raid"]["rescue"]["profession"] =
                        baseResidentProfessionValue(
                            raid.rescue->profession);
                }
            }
            else
            {
                payload["pending_raid"]["rescue"] = nullptr;
            }
            if (schemaVersion >= 27 ||
                raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
                raid.rulesVersion == "procedural-playable-outdoor-layout-21" ||
                raid.rulesVersion == "procedural-frontier-district-layout-22" ||
                raid.rulesVersion ==
                    "procedural-frontier-resource-ecology-23" ||
                raid.rulesVersion ==
                    "procedural-frontier-resource-ecology-24")
            {
                Json routes = Json::array();
                for (const RegionRouteDefinitionId &routeId :
                     raid.travel.routeIds)
                {
                    routes.push_back(routeId.value());
                }
                payload["pending_raid"]["travel"]["route_ids"] =
                    std::move(routes);
                payload["pending_raid"]["travel"]
                    ["starting_regional_operations"] =
                        regionalOperationsValue(
                            raid.travel.startingRegionalOperations,
                            schemaVersion >= 28,
                            schemaVersion >= 29,
                            schemaVersion >= 30,
                            schemaVersion >= 31);
                if (schemaVersion >= 32)
                {
                    payload["pending_raid"]["travel"]
                        ["starting_base_siege"] =
                            baseSiegeValue(raid.travel.startingBaseSiege);
                }
            }
        }
        if (schemaVersion >= 26)
        {
            if (raid.selfRecovery.has_value())
            {
                const RaidSelfRecoverySnapshot &recovery =
                    *raid.selfRecovery;
                Json roots = Json::array();
                for (const RaidSelfRecoveryRootSnapshot &root :
                     recovery.roots)
                {
                    roots.push_back({
                        {"asset_id", root.assetId},
                        {"source_slot", slotName(root.sourceSlot)},
                        {"loot_slot_index", root.lootSlotIndex},
                        {"position", vectorValue(root.position)}});
                }
                payload["pending_raid"]["self_recovery"] = {
                    {"source_record", {
                        {"record_id", recovery.sourceRecord.recordId},
                        {"raid_id", recovery.sourceRecord.raidId},
                        {"settlement_id",
                         recovery.sourceRecord.settlementId},
                        {"map_definition_id",
                         recovery.sourceRecord.mapDefinitionId.value()},
                        {"difficulty", recovery.sourceRecord.difficulty},
                        {"outcome", raidOutcomeName(
                            recovery.sourceRecord.outcome)},
                        {"created_world_minute",
                         recovery.sourceRecord.createdWorldMinute},
                        {"subsequent_raid_settlement_count",
                         recovery.sourceRecord
                             .subsequentRaidSettlementCount}}},
                    {"cache_position", vectorValue(recovery.cachePosition)},
                    {"interaction_duration_seconds",
                     recovery.interactionDurationSeconds},
                    {"opened", recovery.opened},
                    {"roots", std::move(roots)}};
            }
            else
            {
                payload["pending_raid"]["self_recovery"] = nullptr;
            }
        }
        if (schemaVersion >= 28)
        {
            if (raid.outpostRestoration.has_value())
            {
                payload["pending_raid"]["outpost_restoration"] = {
                    {"outpost_definition_id",
                     raid.outpostRestoration->outpostDefinitionId.value()},
                    {"objective_secured",
                     raid.outpostRestoration->objectiveSecured}};
            }
            else
            {
                payload["pending_raid"]["outpost_restoration"] = nullptr;
            }
        }
        if (schemaVersion >= 29)
        {
            if (raid.baseSiteClearance.has_value())
            {
                payload["pending_raid"]["base_site_clearance"] = {
                    {"base_site_definition_id",
                     raid.baseSiteClearance->baseSiteDefinitionId.value()},
                    {"objective_secured",
                     raid.baseSiteClearance->objectiveSecured}};
            }
            else
            {
                payload["pending_raid"]["base_site_clearance"] = nullptr;
            }
        }
        if (schemaVersion >= 33)
        {
            if (raid.basePerimeterSweep.has_value())
            {
                payload["pending_raid"]["base_perimeter_sweep"] = {
                    {"base_site_definition_id",
                     raid.basePerimeterSweep->baseSiteDefinitionId.value()},
                    {"threat_reduction_units",
                     raid.basePerimeterSweep->threatReductionUnits},
                    {"objective_secured",
                     raid.basePerimeterSweep->objectiveSecured}};
            }
            else
            {
                payload["pending_raid"]["base_perimeter_sweep"] = nullptr;
            }
        }
    }
    else
    {
        payload["pending_raid"] = nullptr;
    }

    if (profile.lastRaidResult.has_value())
    {
        Json returned = Json::array();
        for (const ItemDefinitionId &id :
             profile.lastRaidResult->returnedItemDefinitionIds)
        {
            returned.push_back(id.value());
        }
        payload["last_raid_result"] = {
            {"settlement_id", profile.lastRaidResult->settlementId},
            {"outcome", raidOutcomeName(profile.lastRaidResult->outcome)},
            {"returned_item_definition_ids", std::move(returned)},
            {"currency_delta", profile.lastRaidResult->currencyDelta}};
        if (schemaVersion >= 9)
        {
            payload["last_raid_result"]["travel_minutes_applied"] =
                profile.lastRaidResult->travelMinutesApplied;
        }
        if (schemaVersion >= 13)
        {
            payload["last_raid_result"]["rescued_ordinary_residents"] =
                profile.lastRaidResult->rescuedOrdinaryResidents;
        }
        if (schemaVersion >= 16)
        {
            payload["last_raid_result"]["rescued_injured_residents"] =
                profile.lastRaidResult->rescuedInjuredResidents;
        }
        if (schemaVersion >= 24)
        {
            payload["last_raid_result"]["lost_raid_record_id"] =
                profile.lastRaidResult->lostRaidRecordId.has_value()
                    ? Json(*profile.lastRaidResult->lostRaidRecordId)
                    : Json(nullptr);
        }
        if (schemaVersion >= 33)
        {
            payload["last_raid_result"]["base_threat_reduced_units"] =
                profile.lastRaidResult->baseThreatReducedUnits;
        }
    }
    else
    {
        payload["last_raid_result"] = nullptr;
    }
    if (schemaVersion >= 44)
    {
        if (profile.pendingRaid)
            payload["pending_raid"]["wish_focus"] = profile.pendingRaid->wishFocus
                ? wishSnapshotValue(*profile.pendingRaid->wishFocus) : Json(nullptr);
        if (profile.lastRaidResult)
        {
            Json summary = nullptr;
            if (profile.lastRaidResult->wishReturn)
            {
                const auto &value = *profile.lastRaidResult->wishReturn;
                summary = {{"focus", wishSnapshotValue(value.focus)}, {"items", value.itemCount},
                    {"contribution", value.contribution}, {"expired", value.expired}};
            }
            payload["last_raid_result"]["wish_return"] = std::move(summary);
        }
    }
    return payload;
}

std::optional<std::string> readText(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
    {
        return std::nullopt;
    }
    return buffer.str();
}

bool writeText(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.flush();
    return stream.good();
}

bool atomicReplace(
    const std::filesystem::path &temporary,
    const std::filesystem::path &destination)
{
#ifdef _WIN32
    return MoveFileExW(
               temporary.c_str(),
               destination.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

bool isValidEnvelope(
    const std::filesystem::path &path,
    const ContentRegistry &content)
{
    const auto text = readText(path);
    return text.has_value() &&
           deserializeProfileEnvelope(*text, content).profile.has_value();
}
}

std::string serializeProfileEnvelope(
    const ProfileState &profile,
    std::string_view contentVersion,
    std::uint32_t schemaVersion)
{
    if (schemaVersion != 1 && schemaVersion != 2 &&
        schemaVersion != 3 && schemaVersion != 4 && schemaVersion != 5 &&
        schemaVersion != 6 && schemaVersion != 7 && schemaVersion != 8 &&
        schemaVersion != 9 && schemaVersion != 10 &&
        schemaVersion != 11 && schemaVersion != 12 &&
        schemaVersion != 13 && schemaVersion != 14 && schemaVersion != 15 &&
        schemaVersion != 16 && schemaVersion != 17 && schemaVersion != 18 &&
        schemaVersion != 19 && schemaVersion != 20 && schemaVersion != 21 &&
        schemaVersion != 22 && schemaVersion != 23 && schemaVersion != 24 &&
        schemaVersion != 25 && schemaVersion != 26 &&
        schemaVersion != 27 && schemaVersion != 28 &&
        schemaVersion != 29 && schemaVersion != 30 && schemaVersion != 31 &&
        schemaVersion != 32 && schemaVersion != 33 && schemaVersion != 34 &&
        schemaVersion != 35 && schemaVersion != 36 && schemaVersion != 37 &&
        schemaVersion != 38 && schemaVersion != 39 && schemaVersion != 40 &&
        schemaVersion != 41 && schemaVersion != 42 && schemaVersion != 43 && schemaVersion != 44 && schemaVersion != 45)
    {
        throw std::invalid_argument{"unsupported save schema version"};
    }
    Json payload = profilePayload(profile, schemaVersion);
    const std::string payloadText = payload.dump();
    Json envelope{
        {"schema_version", schemaVersion},
        {"profile_id", profile.profileId},
        {"revision", profile.revision},
        {"content_version", contentVersion},
        {"payload_checksum", checksum(payloadText)},
        {"payload", std::move(payload)}};
    return envelope.dump(2);
}

SaveLoadResult deserializeProfileEnvelope(
    std::string_view text,
    const ContentRegistry &content)
{
    try
    {
        const Json envelope = Json::parse(text.begin(), text.end());
        if (!envelope.is_object() || !envelope.at("schema_version").is_number_unsigned() ||
            !envelope.at("content_version").is_string())
        {
            return {SaveLoadStatus::Failed, std::nullopt, "unsupported save envelope"};
        }
        const std::uint32_t schemaVersion =
            envelope.at("schema_version").get<std::uint32_t>();
        const std::string contentVersion =
            envelope.at("content_version").get<std::string>();
        const bool legacyContent =
            (schemaVersion == 1 &&
             contentVersion == "core-alpha-content-1") ||
            (schemaVersion <= 2 &&
             contentVersion == "core-alpha-content-2") ||
            (schemaVersion == 3 &&
             contentVersion == "survival-loadout-content-1") ||
            (schemaVersion == 4 &&
             contentVersion == "survival-loadout-content-2") ||
            (schemaVersion == 5 &&
             contentVersion == "survival-loadout-content-3") ||
            (schemaVersion == 6 &&
             (contentVersion == "survival-loadout-content-4" ||
               contentVersion == "survival-loadout-content-5" ||
               contentVersion == "combat-aim-content-6" ||
               contentVersion == "combat-input-content-7" ||
               contentVersion == "combat-ballistics-content-8" ||
               contentVersion == "raid-fixed-maps-content-9" ||
               contentVersion == "raid-pressure-content-10" ||
               contentVersion == "raid-control-resource-content-11" ||
               contentVersion == "raid-conditional-extraction-content-12")) ||
            ((schemaVersion == 7 || schemaVersion == 8) &&
             contentVersion == "base-resource-pressure-content-13") ||
            (schemaVersion == 9 &&
             contentVersion == "raid-travel-time-content-14") ||
            (schemaVersion == 10 &&
             contentVersion == "base-gunsmith-service-content-15") ||
            (schemaVersion == 11 &&
             (contentVersion == "base-periodic-wishes-content-16" ||
              contentVersion == "base-operational-readiness-content-17" ||
              contentVersion == "base-instant-gunsmith-content-18" ||
              contentVersion == "base-paid-medical-content-19")) ||
            (schemaVersion == 12 &&
             contentVersion == "base-residents-beds-sleep-content-20") ||
            (schemaVersion == 13 &&
             contentVersion ==
                 "raid-ordinary-survivor-rescue-content-21") ||
            (schemaVersion == 14 &&
             contentVersion == "base-dormitory-expansion-content-22") ||
            (schemaVersion == 15 &&
             contentVersion == "base-supply-policy-content-23") ||
            (schemaVersion == 16 &&
             contentVersion == "base-resident-medical-content-24") ||
            (schemaVersion == 17 &&
             contentVersion == "base-basic-manufacturing-content-25") ||
            (schemaVersion == 18 &&
             (contentVersion == "base-morale-events-content-26" ||
              contentVersion == "base-workforce-facilities-content-27")) ||
            (schemaVersion == 19 &&
             contentVersion == "base-workforce-facilities-content-27") ||
            (schemaVersion == 20 &&
             contentVersion == "regional-map-intelligence-content-28") ||
            (schemaVersion == 21 &&
             contentVersion == "procedural-outdoor-layout-content-29") ||
            (schemaVersion == 22 &&
             (contentVersion == "raid-interior-spaces-content-30" ||
              contentVersion ==
                  "raid-special-location-placement-content-31")) ||
            (schemaVersion == 23 &&
             (contentVersion == "raid-building-intelligence-content-32" ||
              contentVersion ==
                  "raid-second-representative-location-content-33")) ||
            (schemaVersion == 24 &&
             contentVersion == "regional-loss-record-content-34") ||
            (schemaVersion == 25 &&
             contentVersion == "regional-recovery-task-content-35") ||
            (schemaVersion == 26 &&
             contentVersion == "regional-recovery-task-content-35") ||
            (schemaVersion == 27 &&
             contentVersion == "regional-route-outpost-content-36") ||
            (schemaVersion == 28 &&
             contentVersion == "regional-outpost-disruption-content-37") ||
            (schemaVersion == 29 &&
             contentVersion == "regional-base-site-clearance-content-38") ||
            (schemaVersion == 30 &&
             contentVersion == "regional-main-base-migration-content-39") ||
            (schemaVersion == 31 &&
             contentVersion == "regional-base-site-feature-content-40") ||
            (schemaVersion == 32 &&
             contentVersion == "regional-base-threat-content-41") ||
            (schemaVersion == 33 &&
             contentVersion == "regional-base-perimeter-sweep-content-42") ||
            (schemaVersion == 34 &&
             contentVersion ==
                 "procedural-playable-outdoor-layout-content-43") ||
            (schemaVersion == 35 &&
             contentVersion ==
                 "procedural-frontier-district-layout-content-44") ||
            (schemaVersion == 36 &&
             contentVersion ==
                 "procedural-frontier-resource-ecology-content-45") ||
            (schemaVersion == 36 &&
             contentVersion ==
                 "procedural-frontier-resource-ecology-hardening-content-46") ||
            (schemaVersion == 37 &&
             contentVersion ==
                 "procedural-frontier-encounter-ecology-content-47") ||
            (schemaVersion == 37 &&
             contentVersion ==
                 "procedural-frontier-encounter-ecology-hardening-content-48") ||
            (schemaVersion == 37 &&
             contentVersion ==
                 "procedural-frontier-consumer-integration-content-49") ||
            (schemaVersion == 37 &&
             contentVersion ==
                 "procedural-frontier-loot-identity-content-50") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "procedural-frontier-high-risk-crisis-content-51") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "content-beta-weapon-caliber-content-52") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "content-beta-warehouse-catalog-content-53") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "content-beta-loadout-gear-content-54") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "content-beta-loot-economy-content-55") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "content-beta-loadout-readiness-content-56") ||
            (schemaVersion >= 38 &&
             contentVersion ==
                 "home-region-onboarding-content-57") ||
            (schemaVersion >= 39 &&
             contentVersion ==
                 "home-region-placeable-storage-content-58");
        if ((schemaVersion != 1 && schemaVersion != 2 &&
             schemaVersion != 3 && schemaVersion != 4 &&
             schemaVersion != 5 && schemaVersion != 6 &&
             schemaVersion != 7 && schemaVersion != 8 &&
             schemaVersion != 9 && schemaVersion != 10 &&
             schemaVersion != 11 && schemaVersion != 12 &&
             schemaVersion != 13 && schemaVersion != 14 &&
             schemaVersion != 15 && schemaVersion != 16 &&
             schemaVersion != 17 && schemaVersion != 18 &&
             schemaVersion != 19 && schemaVersion != 20 &&
             schemaVersion != 21 && schemaVersion != 22 &&
             schemaVersion != 23 && schemaVersion != 24 &&
             schemaVersion != 25 && schemaVersion != 26 &&
             schemaVersion != 27 && schemaVersion != 28 &&
             schemaVersion != 29 && schemaVersion != 30 &&
              schemaVersion != 31 && schemaVersion != 32 &&
              schemaVersion != 33 && schemaVersion != 34 &&
              schemaVersion != 35 && schemaVersion != 36 &&
              schemaVersion != 37 && schemaVersion != 38 &&
              schemaVersion != 39 && schemaVersion != 40 &&
              schemaVersion != 41 && schemaVersion != 42 &&
              schemaVersion != 43 && schemaVersion != 44 && schemaVersion != 45) ||
            (contentVersion != content.contentVersion() && !legacyContent))
        {
            return {SaveLoadStatus::Failed, std::nullopt, "unsupported save envelope"};
        }
        const Json &payload = envelope.at("payload");
        if (!payload.is_object() ||
            checksum(payload.dump()) !=
                envelope.at("payload_checksum").get<std::string>())
        {
            return {SaveLoadStatus::Failed, std::nullopt, "save checksum mismatch"};
        }

        ProfileState profile;
        if (schemaVersion >= 45)
        {
            const auto &founding = payload.at("home_founding");
            profile.homeFounding.established = founding.at("established").get<bool>();
            profile.homeFounding.hintsDismissed = founding.at("hints_dismissed").get<bool>();
            profile.homeFounding.layoutVersion = founding.at("layout_version").get<std::uint32_t>();
            for (const auto &plot : founding.at("plots"))
                if (!profile.homeFounding.plots.emplace(
                    RegionalBaseSiteDefinitionId{plot.at("region").get<std::string>()},
                    plot.at("plot").get<std::string>()).second)
                    throw std::invalid_argument{"duplicate Home founding region"};
        }
        profile.profileId = payload.at("profile_id").get<std::string>();
        profile.revision = payload.at("revision").get<ProfileRevision>();
        profile.currency = payload.at("currency").get<std::uint32_t>();
        const auto tutorial = parseTutorial(
            payload.at("tutorial").get<std::string>());
        if (!tutorial.has_value() ||
            envelope.at("profile_id").get<std::string>() != profile.profileId ||
            envelope.at("revision").get<ProfileRevision>() != profile.revision)
        {
            return {SaveLoadStatus::Failed, std::nullopt, "save header does not match payload"};
        }
        profile.tutorial = *tutorial;
        profile.currentHealth = schemaVersion >= 2
            ? payload.at("current_health").get<int>()
            : 100;
        if (schemaVersion >= 4)
        {
            const Json &medical = payload.at("medical_status");
            const std::uint32_t bleeding =
                medical.at("bleeding").get<std::uint32_t>();
            if (bleeding > static_cast<std::uint32_t>(
                    BleedingSeverity::Heavy))
            {
                return {SaveLoadStatus::Failed, std::nullopt,
                        "bleeding severity is invalid"};
            }
            profile.medicalStatus = MedicalStatusState{
                static_cast<BleedingSeverity>(bleeding),
                medical.at("light_bleeding_remaining_ms")
                    .get<std::uint32_t>(),
                medical.at("bleeding_damage_remaining_ms")
                    .get<std::uint32_t>(),
                medical.at("painkiller_remaining_ms")
                    .get<std::uint32_t>(),
                medical.at("pain_scream_remaining_ms")
                    .get<std::uint32_t>()};
        }
        if (schemaVersion >= 8)
        {
            profile.worldClock.elapsedWorldMinutes =
                payload.at("world_clock")
                    .at("elapsed_world_minutes")
                    .get<std::uint64_t>();
        }
        if (schemaVersion >= 10)
        {
            profile.nextBaseServiceJobId = payload.at(
                "next_base_service_job_id").get<BaseServiceJobId>();
        }
        if (schemaVersion >= 7)
        {
            const Json &resources = payload.at("base_resources");
            profile.baseResources = BaseResourceState{
                BaseResourceBundle{
                    resources.at("food").get<std::uint32_t>(),
                    resources.at("hygiene").get<std::uint32_t>(),
                    resources.at("morale").get<std::uint32_t>(),
                    resources.at("security").get<std::uint32_t>()},
                BaseResourceBundle{
                    resources.at("shortfall_food").get<std::uint32_t>(),
                    resources.at("shortfall_hygiene").get<std::uint32_t>(),
                    resources.at("shortfall_morale").get<std::uint32_t>(),
                    resources.at("shortfall_security").get<std::uint32_t>()},
                schemaVersion >= 8
                    ? resources.at("resolved_demand_cycle_count")
                          .get<std::uint64_t>()
                    : 0U};
        }
        if (schemaVersion >= 11)
        {
            const Json &priority = payload.at("base_priority");
            profile.basePriority = schemaVersion >= 43
                ? parseBasePriority(priority, schemaVersion)
                : parseLegacyBasePriority(
                      priority,
                      profile.basePopulation.ordinaryResidents);
        }
        else
        {
            static_cast<void>(synchronizeBasePriorityThrough(
                profile,
                content));
        }
        if (schemaVersion >= 12)
        {
            const Json &population = payload.at("base_population");
            profile.basePopulation.ordinaryResidents = population.at(
                "ordinary_residents").get<std::uint32_t>();
            profile.basePopulation.bedCapacity = population.at(
                "bed_capacity").get<std::uint32_t>();
            profile.basePopulation.injuredResidents = schemaVersion >= 16
                ? population.at("injured_residents").get<std::uint32_t>()
                : 0U;
            if (schemaVersion >= 19)
            {
                profile.basePopulation.professionResidents =
                    parseBaseProfessionCounts(
                        population.at("profession_residents"));
                profile.basePopulation.injuredByProfession =
                    parseBaseProfessionCounts(
                        population.at("injured_by_profession"));
            }
            else
            {
                const std::uint32_t specialists =
                    std::min(profile.basePopulation.ordinaryResidents, 2U);
                profile.basePopulation.professionResidents = {
                    profile.basePopulation.ordinaryResidents - specialists,
                    specialists >= 1U ? 1U : 0U,
                    specialists >= 2U ? 1U : 0U,
                    0U};
                profile.basePopulation.injuredByProfession = {
                    profile.basePopulation.injuredResidents, 0U, 0U, 0U};
            }
        }
        if (schemaVersion >= 11 && schemaVersion < 43)
        {
            profile.basePriority.frozenPopulation =
                profile.basePopulation.ordinaryResidents;
            reconcileLegacyBasePriority(
                profile.basePriority,
                content);
        }
        if (schemaVersion >= 14)
        {
            const Json &construction = payload.at("base_construction");
            profile.baseConstruction.materialUnits = construction.at(
                "material_units").get<std::uint32_t>();
            profile.baseConstruction.dormitoryLevel = construction.at(
                "dormitory_level").get<std::uint32_t>();
            if (schemaVersion >= 19)
            {
                profile.baseConstruction.workshopLevel = construction.at(
                    "workshop_level").get<std::uint32_t>();
                profile.baseConstruction.medicalLevel = construction.at(
                    "medical_level").get<std::uint32_t>();
            }
            if (schemaVersion >= 30)
            {
                parseFacilityState(
                    construction,
                    profile.baseConstruction,
                    content);
            }
            else
            {
                initializeLegacyFacilityOwnership(
                    profile.baseConstruction,
                    content,
                    profile.worldClock.elapsedWorldMinutes);
            }
            if (!construction.at("active_project").is_null())
            {
                const Json &project = construction.at("active_project");
                profile.baseConstruction.activeProject =
                    ActiveBaseConstructionProject{
                        BaseConstructionProjectDefinitionId{
                            project.at("definition_id")
                                .get<std::string>()},
                        project.at("locked_material_units")
                            .get<std::uint32_t>(),
                        project.at("committed_workers")
                            .get<std::uint32_t>(),
                        project.at("started_world_minute")
                            .get<std::uint64_t>(),
                        project.at("completion_world_minute")
                            .get<std::uint64_t>()};
            }
        }
        else
        {
            initializeLegacyFacilityOwnership(
                profile.baseConstruction,
                content,
                profile.worldClock.elapsedWorldMinutes);
        }
        profile.baseFacilityLayout.placements.clear();
        if (schemaVersion >= 40)
        {
            const Json &layout = payload.at("base_facility_layout");
            for (const Json &site : layout.at("sites"))
            {
                const RegionalBaseSiteDefinitionId siteDefinitionId{
                    site.at("site_definition_id").get<std::string>()};
                auto [siteIt, inserted] =
                    profile.baseFacilityLayout.placements.emplace(
                        siteDefinitionId,
                        std::map<BaseFacilityDefinitionId, Vec2>{});
                if (!inserted)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Base facility site layout is duplicated"};
                }
                for (const Json &facility : site.at("facilities"))
                {
                    const BaseFacilityDefinitionId definitionId{
                        facility.at("definition_id").get<std::string>()};
                    if (!siteIt->second.emplace(
                            definitionId,
                            Vec2{
                                facility.at("normalized_x").get<float>(),
                                facility.at("normalized_y").get<float>()})
                            .second)
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "Base facility spatial layout is duplicated"};
                    }
                }
            }
            if (schemaVersion == 40)
            {
                initializeBaseFacilityLayouts(profile, content);
            }
        }
        else
        {
            initializeBaseFacilityLayouts(profile, content);
        }
        if (schemaVersion >= 15)
        {
            const Json &policy = payload.at("base_supply_policy");
            for (const Json &assignment : policy.at("assignments"))
            {
                const ItemDefinitionId definitionId{
                    assignment.at("item_definition_id")
                        .get<std::string>()};
                const auto category = parseBaseSupplyCategory(
                    assignment.at("category").get<std::string>());
                if (!category.has_value() ||
                    !profile.baseSupplyPolicy.assignments.emplace(
                        definitionId, *category).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Base supply policy is invalid"};
                }
            }
        }
        if (schemaVersion >= 16)
        {
            const Json &residentMedical = payload.at("resident_medical");
            if (!residentMedical.at("active_treatment").is_null())
            {
                const Json &treatment =
                    residentMedical.at("active_treatment");
                ActiveResidentTreatment active;
                active.jobId = treatment.at("job_id").get<BaseServiceJobId>();
                active.startedWorldMinute = treatment.at(
                    "started_world_minute").get<std::uint64_t>();
                active.completionWorldMinute = treatment.at(
                    "completion_world_minute").get<std::uint64_t>();
                active.consumedContribution = treatment.at(
                    "consumed_contribution").get<std::uint32_t>();
                if (schemaVersion >= 19)
                {
                    active.patientProfession = parseRequiredProfession(
                        treatment.at("patient_profession"));
                    active.workerProfession = parseRequiredProfession(
                        treatment.at("worker_profession"));
                }
                else
                {
                    active.patientProfession = BaseResidentProfession::General;
                    active.workerProfession = BaseResidentProfession::Medical;
                }
                profile.residentMedical.activeTreatment = active;
            }
        }
        if (schemaVersion >= 17)
        {
            const Json &manufacturing = payload.at("base_manufacturing");
            if (!manufacturing.at("active_order").is_null())
            {
                const Json &order = manufacturing.at("active_order");
                BaseManufacturingOrder active;
                active.jobId = order.at("job_id").get<BaseServiceJobId>();
                active.recipeDefinitionId =
                    BaseManufacturingRecipeDefinitionId{
                        order.at("recipe_definition_id").get<std::string>()};
                active.committedWorkers = order.at(
                    "committed_workers").get<std::uint32_t>();
                active.workerProfession = schemaVersion >= 19
                    ? parseRequiredProfession(order.at("worker_profession"))
                    : BaseResidentProfession::Engineering;
                active.startedWorldMinute = order.at(
                    "started_world_minute").get<std::uint64_t>();
                active.completionWorldMinute = order.at(
                    "completion_world_minute").get<std::uint64_t>();
                active.inputAssetIds = order.at(
                    "input_asset_ids").get<std::vector<AssetInstanceId>>();
                active.outputAssetId = order.at(
                    "output_asset_id").get<AssetInstanceId>();
                active.outputReady = order.at("output_ready").get<bool>();
                profile.baseManufacturing.activeOrder = std::move(active);
            }
        }
        if (schemaVersion >= 18)
        {
            profile.baseMorale = parseBaseMorale(
                payload.at("base_morale"));
            profile.baseCommunityEvent = parseBaseCommunityEvent(
                payload.at("base_community_event"));
        }
        else
        {
            profile.baseMorale = BaseMoraleState{};
            profile.baseMorale.resolvedDayCount =
                projectWorldClock(profile.worldClock).completedDays;
            static_cast<void>(synchronizeBaseCommunityEventThrough(
                profile,
                content));
        }
        if (schemaVersion >= 19)
        {
            const Json &workforce = payload.at("base_workforce");
            profile.baseWorkforce.workshopWorker = parseOptionalProfession(
                workforce.at("workshop_worker"));
            profile.baseWorkforce.medicalWorker = parseOptionalProfession(
                workforce.at("medical_worker"));
        }
        else
        {
            profile.baseWorkforce.workshopWorker =
                profile.baseManufacturing.activeOrder.has_value()
                ? std::optional<BaseResidentProfession>{
                      BaseResidentProfession::Engineering}
                : std::optional<BaseResidentProfession>{
                      BaseResidentProfession::Engineering};
            profile.baseWorkforce.medicalWorker =
                profile.residentMedical.activeTreatment.has_value()
                ? std::optional<BaseResidentProfession>{
                      BaseResidentProfession::Medical}
                    : std::optional<BaseResidentProfession>{
                          BaseResidentProfession::Medical};
        }
        profile.regionalOperations = schemaVersion >= 27
            ? parseRegionalOperations(
                  payload.at("regional_operations"), content, schemaVersion)
            : defaultRegionalOperations(content);
        profile.baseSiege = schemaVersion >= 32
            ? parseBaseSiege(payload.at("base_siege"))
            : defaultBaseSiege(profile.worldClock);
        if (schemaVersion < 33)
        {
            normalizeBaseThreatCapacity(profile.baseSiege);
        }
        if (schemaVersion >= 42)
        {
            const Json &perimeter = payload.at("home_perimeter");
            for (const Json &site : perimeter.at("sites"))
            {
                HomePerimeterSiteSnapshot snapshot;
                snapshot.baseSiteDefinitionId =
                    RegionalBaseSiteDefinitionId{
                        site.at("site_definition_id").get<std::string>()};
                snapshot.cycleIndex =
                    site.at("cycle_index").get<std::uint64_t>();
                snapshot.seed = site.at("seed").get<std::uint64_t>();
                snapshot.lootAssetIds =
                    site.at("loot_asset_ids")
                        .get<std::vector<AssetInstanceId>>();
                for (const Json &enemy : site.at("enemies"))
                {
                    snapshot.enemies.push_back(
                        HomePerimeterEnemySnapshot{
                            enemy.at("local_id").get<std::uint32_t>(),
                            {enemy.at("spawn_x").get<float>(),
                             enemy.at("spawn_y").get<float>()},
                            {enemy.at("position_x").get<float>(),
                             enemy.at("position_y").get<float>()},
                            {enemy.at("size_x").get<float>(),
                             enemy.at("size_y").get<float>()},
                            enemy.at("maximum_health").get<int>(),
                            enemy.at("health").get<int>()});
                }
                if (!profile.homePerimeter.sites.emplace(
                        snapshot.baseSiteDefinitionId,
                        std::move(snapshot)).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Home perimeter site is duplicated"};
                }
            }
            if (!perimeter.at("active_outing").is_null())
            {
                const Json &active = perimeter.at("active_outing");
                profile.homePerimeter.activeOuting =
                    HomePerimeterOutingState{
                        active.at("outing_id").get<std::string>(),
                        RegionalBaseSiteDefinitionId{
                            active.at("site_definition_id")
                                .get<std::string>()},
                        active.at("cycle_index").get<std::uint64_t>()};
            }
            for (const Json &result : perimeter.at("committed_results"))
            {
                const std::string resultId = result.get<std::string>();
                if (resultId.empty() ||
                    !profile.homePerimeter.committedResults
                         .insert(resultId).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Home perimeter result history is invalid"};
                }
            }
        }
        if (schemaVersion >= 20)
        {
            for (const Json &entry :
                 payload.at("raid_intelligence_archive"))
            {
                const MapDefinitionId mapId{
                    entry.at("map_definition_id").get<std::string>()};
                std::array<std::uint32_t, kRaidIntelligenceCategoryCount>
                    counts{
                        entry.at("transport").get<std::uint32_t>(),
                        entry.at("resource").get<std::uint32_t>(),
                        entry.at("enemy").get<std::uint32_t>()};
                if (!profile.raidIntelligence.counts.emplace(
                        mapId, counts).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Raid intelligence archive is duplicated"};
                }
            }
        }
        if (schemaVersion >= 23)
        {
            for (const Json &entry :
                 payload.at("raid_interior_intelligence"))
            {
                const RaidSpaceDefinitionId interiorId{
                    entry.get<std::string>()};
                if (!profile.raidInteriorIntelligence.knownLayouts
                         .insert(interiorId).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "Raid interior intelligence is duplicated"};
                }
            }
        }
        profile.assets.setNextAssetIdForLoad(
            payload.at("next_asset_id").get<AssetInstanceId>());

        for (const Json &transaction : payload.at("committed_transactions"))
        {
            const std::string value = transaction.get<std::string>();
            if (value.empty() || !profile.committedTransactions.insert(value).second)
            {
                return {SaveLoadStatus::Failed, std::nullopt, "transaction history is invalid"};
            }
        }

        if (schemaVersion >= 2)
        {
            for (const Json &settlement : payload.at("committed_settlements"))
            {
                const std::string value = settlement.get<std::string>();
                if (value.empty() || !profile.committedSettlements.insert(value).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "settlement history is invalid"};
                }
            }
        }
        if (schemaVersion >= 13)
        {
            for (const Json &rescue : payload.at("committed_rescues"))
            {
                RescueDefinitionId id{rescue.get<std::string>()};
                if (!profile.committedRescues.insert(std::move(id)).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "rescue history is invalid"};
                }
            }
        }
        if (schemaVersion >= 24)
        {
            for (const Json &value : payload.at("lost_raid_records"))
            {
                const auto outcome = parseRaidOutcome(
                    value.at("outcome").get<std::string>());
                if (!outcome.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "lost Raid record outcome is invalid"};
                }
                LostRaidRecord record{
                    value.at("record_id").get<std::string>(),
                    value.at("raid_id").get<std::string>(),
                    value.at("settlement_id").get<std::string>(),
                    MapDefinitionId{value.at("map_definition_id")
                        .get<std::string>()},
                    value.at("difficulty").get<std::string>(),
                    *outcome,
                    value.at("created_world_minute").get<std::uint64_t>(),
                    value.at("subsequent_raid_settlement_count")
                        .get<std::uint32_t>()};
                if (!profile.lostRaidRecords.emplace(
                        record.recordId, record).second)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "lost Raid record is duplicated"};
                }
            }
        }
        if (schemaVersion >= 25)
        {
            profile.nextRecoveryTaskId =
                payload.at("next_recovery_task_id")
                    .get<RecoveryTaskId>();
            if (!payload.at("recovery_task").is_null())
            {
                const Json &value = payload.at("recovery_task");
                const auto outcome = parseRaidOutcome(
                    value.at("outcome").get<std::string>());
                if (!outcome.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "recovery task outcome is invalid"};
                }
                RecoveryTask task;
                task.taskId = value.at("task_id").get<RecoveryTaskId>();
                task.sourceRecord = LostRaidRecord{
                    value.at("record_id").get<std::string>(),
                    value.at("raid_id").get<std::string>(),
                    value.at("settlement_id").get<std::string>(),
                    MapDefinitionId{value.at("map_definition_id")
                        .get<std::string>()},
                    value.at("difficulty").get<std::string>(),
                    *outcome,
                    value.at("created_world_minute").get<std::uint64_t>(),
                    value.at("subsequent_raid_settlement_count")
                        .get<std::uint32_t>()};
                task.paidCurrency =
                    value.at("paid_currency").get<std::uint32_t>();
                task.startedWorldMinute =
                    value.at("started_world_minute").get<std::uint64_t>();
                task.completionWorldMinute =
                    value.at("completion_world_minute").get<std::uint64_t>();
                task.readyForCollection =
                    value.at("ready_for_collection").get<bool>();
                for (const Json &assetId :
                     value.at("recovered_asset_ids"))
                {
                    if (!task.recoveredAssetIds.insert(
                            assetId.get<AssetInstanceId>()).second)
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "recovery result contains duplicate assets"};
                    }
                }
                profile.recoveryTask = std::move(task);
            }
        }
        if (schemaVersion < 19)
        {
            const auto moveResidentsToProfession =
                [&profile](BaseResidentProfession profession,
                           std::uint32_t ordinaryCount,
                           std::uint32_t injuredCount)
            {
                if (profession == BaseResidentProfession::General)
                {
                    return;
                }
                const std::size_t general = static_cast<std::size_t>(
                    BaseResidentProfession::General);
                const std::size_t target = static_cast<std::size_t>(profession);
                const std::uint32_t movedResidents = std::min(
                    ordinaryCount,
                    profile.basePopulation.professionResidents[general]);
                profile.basePopulation.professionResidents[general] -=
                    movedResidents;
                profile.basePopulation.professionResidents[target] +=
                    movedResidents;
                const std::uint32_t movedInjured = std::min({
                    injuredCount,
                    profile.basePopulation.injuredByProfession[general],
                    movedResidents});
                profile.basePopulation.injuredByProfession[general] -=
                    movedInjured;
                profile.basePopulation.injuredByProfession[target] +=
                    movedInjured;
            };
            for (const MapDefinition &map : content.maps())
            {
                if (map.rescue.has_value() &&
                    profile.committedRescues.contains(map.rescue->id))
                {
                    moveResidentsToProfession(
                        map.rescue->profession,
                        map.rescue->ordinaryResidentCount,
                        map.rescue->injuredResidentCount);
                }
            }
            const auto ensureHealthyProfession =
                [&profile](BaseResidentProfession profession)
            {
                const std::size_t general = static_cast<std::size_t>(
                    BaseResidentProfession::General);
                const std::size_t target = static_cast<std::size_t>(profession);
                if (profile.basePopulation.professionResidents[target] >
                        profile.basePopulation.injuredByProfession[target] ||
                    profile.basePopulation.professionResidents[general] <=
                        profile.basePopulation.injuredByProfession[general])
                {
                    return;
                }
                --profile.basePopulation.professionResidents[general];
                ++profile.basePopulation.professionResidents[target];
            };
            if (profile.baseManufacturing.activeOrder.has_value())
            {
                ensureHealthyProfession(BaseResidentProfession::Engineering);
            }
            if (profile.residentMedical.activeTreatment.has_value())
            {
                ensureHealthyProfession(BaseResidentProfession::Medical);
                for (BaseResidentProfession profession : {
                         BaseResidentProfession::Medical,
                         BaseResidentProfession::Engineering,
                         BaseResidentProfession::Combat,
                         BaseResidentProfession::General})
                {
                    if (profile.basePopulation.injuredByProfession[
                            static_cast<std::size_t>(profession)] > 0U)
                    {
                        profile.residentMedical.activeTreatment
                            ->patientProfession = profession;
                        break;
                    }
                }
            }
        }

        for (const Json &value : payload.at("assets"))
        {
            AssetRecord asset;
            asset.instanceId = value.at("instance_id").get<AssetInstanceId>();
            asset.definitionId = ItemDefinitionId{
                value.at("definition_id").get<std::string>()};
            asset.quantity = value.at("quantity").get<std::uint32_t>();
            const auto orientation = parseOrientation(
                value.at("orientation").get<std::string>());
            if (!orientation.has_value())
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset orientation is invalid"};
            }
            asset.orientation = *orientation;
            asset.remainingCharges =
                value.at("remaining_charges").get<std::uint32_t>();
            if (schemaVersion >= 3)
            {
                asset.currentMaximumDurability = value.at(
                    "current_maximum_durability").get<std::uint32_t>();
                asset.currentDurability = value.at(
                    "current_durability").get<std::uint32_t>();
            }
            else
            {
                const ItemDefinition &definition =
                    content.item(asset.definitionId);
                if (definition.armorProtection.has_value())
                {
                    asset.currentMaximumDurability =
                        definition.armorProtection->maximumDurability;
                    asset.currentDurability = asset.currentMaximumDurability;
                }
            }
            const ItemDefinition &assetDefinition =
                content.item(asset.definitionId);
            if (schemaVersion < 5 &&
                assetDefinition.weaponCondition.has_value())
            {
                asset.currentMaximumDurability =
                    assetDefinition.weaponCondition->maximumDurabilityCenti;
                asset.currentDurability = asset.currentMaximumDurability;
                asset.weaponMalfunction = WeaponMalfunctionType::None;
            }
            else if (schemaVersion >= 5)
            {
                const auto malfunction = parseMalfunction(
                    value.at("weapon_malfunction").get<std::string>());
                if (!malfunction.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "weapon malfunction is invalid"};
                }
                asset.weaponMalfunction = *malfunction;
            }
            if (schemaVersion < 6 &&
                assetDefinition.weaponCondition.has_value() &&
                asset.currentMaximumDurability == 0)
            {
                // Schema v5 could contain legacy weapon instances that only
                // became durable production weapons in content v4 (the
                // pistol). Give those instances the new definition baseline
                // without overwriting condition already persisted by v5.
                asset.currentMaximumDurability =
                    assetDefinition.weaponCondition->maximumDurabilityCenti;
                asset.currentDurability = asset.currentMaximumDurability;
                asset.weaponMalfunction = WeaponMalfunctionType::None;
            }
            if (value.contains("relief_batch_id"))
            {
                asset.reliefBatchId =
                    value.at("relief_batch_id").get<std::string>();
                if (asset.reliefBatchId->empty())
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "relief batch ID is empty"};
                }
            }
            if (schemaVersion >= 2)
            {
                for (const Json &round : value.at("magazine_rounds"))
                {
                    asset.magazineRounds.push_back(parseRound(round));
                }
                if (!value.at("chambered_round").is_null())
                {
                    asset.chamberedRound =
                        parseRound(value.at("chambered_round"));
                }
            }

            const Json &location = value.at("location");
            const std::string locationType =
                location.at("type").get<std::string>();
            if (locationType == "equipped")
            {
                const auto slot = parseSlot(
                    location.at("slot").get<std::string>());
                if (!slot.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "equipment slot is invalid"};
                }
                asset.location = EquippedAssetLocation{*slot};
            }
            else if (locationType == "stored")
            {
                const Json &container = location.at("container");
                ProfileContainerId containerId;
                const std::string containerType =
                    container.at("type").get<std::string>();
                if (containerType == "stash")
                {
                    containerId = ProfileContainerId::stash();
                }
                else if (schemaVersion >= 7 &&
                         containerType == "base_intake")
                {
                    containerId = ProfileContainerId::baseIntake();
                }
                else if (containerType == "asset_compartment")
                {
                    containerId = ProfileContainerId::compartment(
                        container.at("owner_asset_id").get<AssetInstanceId>(),
                        container.at("compartment").get<std::uint32_t>());
                }
                else
                {
                    return {SaveLoadStatus::Failed, std::nullopt, "container type is invalid"};
                }
                const Json &origin = location.at("origin");
                asset.location = StoredAssetLocation{
                    containerId,
                    GridPosition{
                        origin.at("x").get<int>(),
                        origin.at("y").get<int>()}};
            }
            else if (schemaVersion >= 2 &&
                     locationType == "installed_magazine")
            {
                asset.location = InstalledMagazineLocation{
                    location.at("weapon_asset_id").get<AssetInstanceId>()};
            }
            else if (schemaVersion >= 2 && locationType == "raid_ground")
            {
                asset.location = RaidGroundAssetLocation{
                    location.at("raid_id").get<std::string>(),
                    location.at("loot_slot_index").get<std::uint32_t>()};
            }
            else if (schemaVersion >= 39 && locationType == "base_ground")
            {
                const Json &position = location.at("position");
                asset.location = BaseGroundAssetLocation{
                    RegionalBaseSiteDefinitionId{
                        location.at("base_site_definition_id")
                            .get<std::string>()},
                    Vec2{
                        position.at("x").get<float>(),
                        position.at("y").get<float>()}};
            }
            else if (schemaVersion >= 10 && locationType == "base_service")
            {
                asset.location = BaseServiceAssetLocation{
                    location.at("job_id").get<BaseServiceJobId>()};
            }
            else if (schemaVersion >= 24 && locationType == "lost_raid")
            {
                const auto slot = parseSlot(
                    location.at("source_slot").get<std::string>());
                if (!slot.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "lost Raid source slot is invalid"};
                }
                asset.location = LostRaidAssetLocation{
                    location.at("record_id").get<std::string>(),
                    *slot};
            }
            else if (schemaVersion >= 25 &&
                     locationType == "recovery_task")
            {
                const auto slot = parseSlot(
                    location.at("source_slot").get<std::string>());
                if (!slot.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "recovery task source slot is invalid"};
                }
                asset.location = RecoveryTaskAssetLocation{
                    location.at("task_id").get<RecoveryTaskId>(),
                    *slot};
            }
            else
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset location type is invalid"};
            }

            if (!profile.assets.insertLoaded(std::move(asset)))
            {
                return {SaveLoadStatus::Failed, std::nullopt, "asset ID is duplicated"};
            }
        }

        if (schemaVersion >= 10 &&
            !payload.at("gunsmith_maintenance_job").is_null())
        {
            const Json &job = payload.at("gunsmith_maintenance_job");
            const Json &origin = job.at("return_origin");
            profile.gunsmithMaintenanceJob = GunsmithMaintenanceJob{
                job.at("job_id").get<BaseServiceJobId>(),
                job.at("weapon_asset_id").get<AssetInstanceId>(),
                GridPosition{
                    origin.at("x").get<int>(),
                    origin.at("y").get<int>()},
                job.at("started_world_minute").get<std::uint64_t>(),
                job.at("completion_world_minute").get<std::uint64_t>(),
                job.at("paid_currency").get<std::uint32_t>(),
                job.at("target_factory_durability_centi")
                    .get<std::uint32_t>()};
        }

        if (schemaVersion >= 2 && !payload.at("pending_raid").is_null())
        {
            const Json &value = payload.at("pending_raid");
            PendingRaidSnapshot raid;
            raid.raidId = value.at("raid_id").get<std::string>();
            raid.settlementId = value.at("settlement_id").get<std::string>();
            raid.rulesVersion = value.at("rules_version").get<std::string>();
            raid.mapDefinitionId = MapDefinitionId{
                value.at("map_definition_id").get<std::string>()};
            raid.seed = value.at("seed").get<std::uint64_t>();
            raid.spawnExtractionPairId =
                value.at("spawn_extraction_pair_id").get<std::string>();
            raid.enemyDeploymentId = EnemyDeploymentDefinitionId{
                value.at("enemy_deployment_id").get<std::string>()};
            raid.playerSpawn = parseVector(value.at("player_spawn"));
            raid.extractionPoint = ContentRect{
                parseVector(value.at("extraction_point").at("position")),
                parseVector(value.at("extraction_point").at("size"))};
            for (const Json &enemy : value.at("enemies"))
            {
                raid.enemies.push_back(RaidEnemySnapshot{
                    parseVector(enemy.at("position")),
                    parseVector(enemy.at("size")),
                    enemy.at("maximum_health").get<int>(),
                    schemaVersion >= 22
                        ? RaidSpaceDefinitionId{
                              enemy.at("space_id").get<std::string>()}
                        : outdoorRaidSpaceId(),
                    schemaVersion >= 37
                        ? enemy.at("encounter_group_instance_id")
                              .get<std::string>()
                        : std::string{}});
            }
            if (schemaVersion >= 37)
            {
                for (const Json &entry : value.at("encounter_groups"))
                {
                    RaidEncounterGroupSnapshot group;
                    group.instanceId =
                        entry.at("instance_id").get<std::string>();
                    group.definitionId =
                        entry.at("definition_id").get<std::string>();
                    const std::uint32_t kind =
                        entry.at("kind").get<std::uint32_t>();
                    if (kind > static_cast<std::uint32_t>(
                                   RaidEncounterKind::Ambush))
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "Raid encounter kind is invalid"};
                    }
                    group.kind = static_cast<RaidEncounterKind>(kind);
                    group.spaceId = RaidSpaceDefinitionId{
                        entry.at("space_id").get<std::string>()};
                    group.homePosition =
                        parseVector(entry.at("home_position"));
                    for (const Json &point : entry.at("patrol_points"))
                        group.patrolPoints.push_back(parseVector(point));
                    group.memberEnemyIndices =
                        entry.at("member_enemy_indices")
                            .get<std::vector<std::uint32_t>>();
                    group.activationDistance =
                        entry.at("activation_distance").get<float>();
                    raid.encounterGroups.push_back(std::move(group));
                }
            }
            for (const Json &entry : value.at("loot"))
            {
                const AssetInstanceId assetId =
                    entry.at("asset_id").get<AssetInstanceId>();
                const AssetRecord *legacyAsset = profile.assets.find(assetId);
                raid.loot.push_back(RaidLootSnapshot{
                    assetId,
                    schemaVersion >= 7
                        ? ItemDefinitionId{entry.at("definition_id")
                              .get<std::string>()}
                        : (legacyAsset != nullptr
                              ? legacyAsset->definitionId
                              : ItemDefinitionId{}),
                    schemaVersion >= 7
                        ? entry.at("quantity").get<std::uint32_t>()
                        : (legacyAsset != nullptr ? legacyAsset->quantity : 0U),
                    entry.at("slot_index").get<std::uint32_t>(),
                    parseVector(entry.at("position")),
                    entry.value("requires_high_risk", false),
                    schemaVersion >= 7
                        ? entry.at("collected").get<bool>()
                        : (legacyAsset != nullptr &&
                           assetIsCarried(profile, assetId)),
                    schemaVersion >= 22
                        ? RaidSpaceDefinitionId{
                              entry.at("space_id").get<std::string>()}
                        : outdoorRaidSpaceId(),
                    schemaVersion >= 36
                        ? entry.at("resource_point_instance_id")
                              .get<std::string>()
                        : std::string{},
                    schemaVersion >= 36
                        ? entry.at("resource_point_slot_index")
                              .get<std::uint32_t>()
                        : 0U});
            }
            raid.carriedRootAssetIds =
                value.at("carried_root_asset_ids")
                    .get<std::vector<AssetInstanceId>>();
            raid.startingHealth = value.at("starting_health").get<int>();
            if (schemaVersion >= 4)
            {
                const Json &medical = value.at("starting_medical_status");
                const std::uint32_t bleeding =
                    medical.at("bleeding").get<std::uint32_t>();
                if (bleeding > static_cast<std::uint32_t>(
                        BleedingSeverity::Heavy))
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "starting bleeding severity is invalid"};
                }
                raid.startingMedicalStatus = MedicalStatusState{
                    static_cast<BleedingSeverity>(bleeding),
                    medical.at("light_bleeding_remaining_ms")
                        .get<std::uint32_t>(),
                    medical.at("bleeding_damage_remaining_ms")
                        .get<std::uint32_t>(),
                    medical.at("painkiller_remaining_ms")
                        .get<std::uint32_t>(),
                    medical.at("pain_scream_remaining_ms")
                        .get<std::uint32_t>()};
            }
            if (schemaVersion >= 20)
            {
                const Json &intelligence = value.at("intelligence");
                raid.intelligence.set(
                    RaidIntelligenceCategory::Transport,
                    intelligence.at("transport").get<bool>());
                raid.intelligence.set(
                    RaidIntelligenceCategory::Resource,
                    intelligence.at("resource").get<bool>());
                raid.intelligence.set(
                    RaidIntelligenceCategory::Enemy,
                    intelligence.at("enemy").get<bool>());
            }
            if (schemaVersion >= 38 &&
                !value.at("high_risk_crisis").is_null())
            {
                const Json &crisisValue = value.at("high_risk_crisis");
                RaidHighRiskCrisisSnapshot crisis;
                crisis.definitionId =
                    crisisValue.at("definition_id").get<std::string>();
                crisis.displayName =
                    crisisValue.at("display_name").get<std::string>();
                crisis.warning =
                    crisisValue.at("warning").get<std::string>();
                crisis.districtInstanceId =
                    crisisValue.at("district_instance_id")
                        .get<std::uint16_t>();
                crisis.resourcePointInstanceId =
                    crisisValue.at("resource_point_instance_id")
                        .get<std::string>();
                crisis.focusArea = ContentRect{
                    parseVector(crisisValue.at("focus_area").at("position")),
                    parseVector(crisisValue.at("focus_area").at("size"))};
                crisis.initialWaveDelaySeconds =
                    crisisValue.at("initial_wave_delay_seconds").get<float>();
                crisis.waveIntervalSeconds =
                    crisisValue.at("wave_interval_seconds").get<float>();
                crisis.waveSize =
                    crisisValue.at("wave_size").get<std::uint32_t>();
                crisis.activeEnemyCap =
                    crisisValue.at("active_enemy_cap").get<std::uint32_t>();
                crisis.advancedLootTableId = LootTableDefinitionId{
                    crisisValue.at("advanced_loot_table_id")
                        .get<std::string>()};
                for (const Json &spawn :
                     crisisValue.at("pressure_spawns"))
                {
                    crisis.pressureSpawns.push_back({
                        spawn.at("anchor_id").get<std::string>(),
                        parseVector(spawn.at("position")),
                        parseVector(spawn.at("size")),
                        spawn.at("maximum_health").get<int>()});
                }
                raid.highRiskCrisis = std::move(crisis);
            }
            if (schemaVersion >= 21)
            {
                const Json &layout = value.at("spatial_layout");
                for (const Json &blocker :
                     layout.at("ballistic_blockers"))
                {
                    raid.spatialLayout.ballisticBlockers.push_back(
                        ContentRect{
                            parseVector(blocker.at("position")),
                            parseVector(blocker.at("size"))});
                }
                raid.spatialLayout.generationAttempt =
                    layout.at("generation_attempt").get<std::uint32_t>();
                raid.spatialLayout.layoutHash =
                    layout.at("layout_hash").get<std::uint64_t>();
                raid.spatialLayout.usedFallback =
                    layout.at("used_fallback").get<bool>();
                if (schemaVersion >= 34)
                {
                    raid.spatialLayout.layoutVersion =
                        layout.at("layout_version").get<std::uint32_t>();
                    for (const Json &road : layout.at("road_cells"))
                    {
                        const std::uint32_t kind =
                            road.at("kind").get<std::uint32_t>();
                        if (kind > static_cast<std::uint32_t>(
                                RaidOutdoorRoadKind::Primary))
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "Raid road kind is invalid"};
                        }
                        raid.spatialLayout.roadCells.push_back({
                            road.at("column").get<std::uint16_t>(),
                            road.at("row").get<std::uint16_t>(),
                            static_cast<RaidOutdoorRoadKind>(kind)});
                    }
                    const std::uint32_t fallbackReason =
                        layout.at("fallback_reason").get<std::uint32_t>();
                    if (fallbackReason > static_cast<std::uint32_t>(
                            RaidMapFallbackReason::AttemptsExhausted))
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "Raid layout fallback reason is invalid"};
                    }
                    raid.spatialLayout.fallbackReason =
                        static_cast<RaidMapFallbackReason>(fallbackReason);
                }
                if (schemaVersion >= 35)
                {
                    for (const Json &district : layout.at("districts"))
                    {
                        const std::uint32_t kind =
                            district.at("kind").get<std::uint32_t>();
                        if (kind > static_cast<std::uint32_t>(
                                RaidDistrictKind::RoadsideService))
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "Raid district kind is invalid"};
                        }
                        RaidDistrictSnapshot snapshot;
                        snapshot.instanceId = district.at("instance_id")
                            .get<std::uint16_t>();
                        snapshot.definitionId = district.at("definition_id")
                            .get<std::string>();
                        snapshot.displayName = district.at("display_name")
                            .get<std::string>();
                        snapshot.kind = static_cast<RaidDistrictKind>(kind);
                        snapshot.labelPosition = parseVector(
                            district.at("label_position"));
                        for (const Json &cell : district.at("cells"))
                        {
                            snapshot.cells.push_back({
                                cell.at("row").get<std::uint16_t>(),
                                cell.at("first_column")
                                    .get<std::uint16_t>(),
                                cell.at("length").get<std::uint16_t>()});
                        }
                        raid.spatialLayout.districts.push_back(
                            std::move(snapshot));
                    }
                    for (const Json &span : layout.at("terrain_spans"))
                    {
                        const std::uint32_t kind =
                            span.at("kind").get<std::uint32_t>();
                        if (kind > static_cast<std::uint32_t>(
                                RaidTerrainKind::Puddle))
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "Raid terrain kind is invalid"};
                        }
                        raid.spatialLayout.terrainSpans.push_back({
                            span.at("row").get<std::uint16_t>(),
                            span.at("first_column").get<std::uint16_t>(),
                            span.at("length").get<std::uint16_t>(),
                            static_cast<RaidTerrainKind>(kind)});
                    }
                    for (const Json &prop : layout.at("props"))
                    {
                        const std::uint32_t kind =
                            prop.at("kind").get<std::uint32_t>();
                        const std::uint32_t state =
                            prop.at("state").get<std::uint32_t>();
                        if (kind > static_cast<std::uint32_t>(
                                RaidOutdoorPropKind::Debris) ||
                            state > static_cast<std::uint32_t>(
                                RaidOutdoorPropState::Abandoned))
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "Raid outdoor prop is invalid"};
                        }
                        const Json &bounds = prop.at("bounds");
                        raid.spatialLayout.props.push_back({
                            prop.at("instance_id").get<std::uint32_t>(),
                            static_cast<RaidOutdoorPropKind>(kind),
                            static_cast<RaidOutdoorPropState>(state),
                            {parseVector(bounds.at("position")),
                             parseVector(bounds.at("size"))},
                            prop.at("quarter_turns").get<std::uint8_t>(),
                            prop.at("collidable").get<bool>()});
                    }
                    for (const Json &anchor :
                         layout.at("anchor_placements"))
                    {
                        const std::uint32_t kind =
                            anchor.at("kind").get<std::uint32_t>();
                        if (kind > static_cast<std::uint32_t>(
                                schemaVersion >= 36
                                    ? RaidMapAnchorKind::ResourcePoint
                                    : RaidMapAnchorKind::InteriorEntrance))
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "Raid anchor kind is invalid"};
                        }
                        const Json &bounds = anchor.at("bounds");
                        raid.spatialLayout.anchorPlacements.push_back({
                            anchor.at("id").get<std::string>(),
                            static_cast<RaidMapAnchorKind>(kind),
                            {parseVector(bounds.at("position")),
                             parseVector(bounds.at("size"))},
                            anchor.at("district_instance_id")
                                .get<std::uint16_t>()});
                    }
                    for (const Json &landmark : layout.at("landmarks"))
                    {
                        RaidLandmarkPlacementSnapshot snapshot;
                        snapshot.definitionId = landmark.at("definition_id")
                            .get<std::string>();
                        snapshot.displayName = landmark.at("display_name")
                            .get<std::string>();
                        snapshot.bounds = {
                            parseVector(landmark.at("bounds").at("position")),
                            parseVector(landmark.at("bounds").at("size"))};
                        snapshot.districtInstanceId =
                            landmark.at("district_instance_id")
                                .get<std::uint16_t>();
                        for (const Json &structure :
                             landmark.at("structures"))
                        {
                            snapshot.structures.push_back({
                                parseVector(structure.at("position")),
                                parseVector(structure.at("size"))});
                        }
                        for (const Json &socket :
                             landmark.at("road_sockets"))
                            snapshot.roadSockets.push_back(parseVector(socket));
                        raid.spatialLayout.landmarks.push_back(
                            std::move(snapshot));
                    }
                    if (schemaVersion >= 36)
                    {
                        for (const Json &resourcePoint :
                             layout.at("resource_points"))
                        {
                            const std::uint32_t kind = resourcePoint.at("kind")
                                .get<std::uint32_t>();
                            if (kind > static_cast<std::uint32_t>(
                                    RaidResourcePointKind::LandmarkSpecific))
                            {
                                return {SaveLoadStatus::Failed, std::nullopt,
                                        "Raid resource point kind is invalid"};
                            }
                            const Json &bounds = resourcePoint.at("bounds");
                            raid.spatialLayout.resourcePoints.push_back({
                                resourcePoint.at("instance_id")
                                    .get<std::string>(),
                                resourcePoint.at("definition_id")
                                    .get<std::string>(),
                                resourcePoint.at("display_name")
                                    .get<std::string>(),
                                static_cast<RaidResourcePointKind>(kind),
                                LootTableDefinitionId{resourcePoint.at(
                                    "loot_table_id").get<std::string>()},
                                resourcePoint.at("risk_tier")
                                    .get<std::uint32_t>(),
                                resourcePoint.at("capacity")
                                    .get<std::uint32_t>(),
                                {parseVector(bounds.at("position")),
                                 parseVector(bounds.at("size"))},
                                resourcePoint.at("district_instance_id")
                                    .get<std::uint16_t>(),
                                resourcePoint.at("landmark_definition_id")
                                    .get<std::string>()});
                        }
                    }
                }
            }
            else
            {
                const MapDefinition &legacyMap = content.map(
                    raid.mapDefinitionId);
                raid.spatialLayout = generateRaidMapLayout(
                    legacyMap,
                    raid.seed,
                    RaidMapGenerationAnchors{
                        raid.playerSpawn,
                        raid.extractionPoint,
                        {},
                        {}});
            }
            if (schemaVersion >= 22)
            {
                for (const Json &interior : value.at("interiors"))
                {
                    RaidInteriorSnapshot snapshot;
                    snapshot.id = RaidSpaceDefinitionId{
                        interior.at("id").get<std::string>()};
                    snapshot.displayName =
                        interior.at("display_name").get<std::string>();
                    snapshot.layoutKnown = schemaVersion >= 23
                        ? interior.at("layout_known").get<bool>()
                        : false;
                    snapshot.worldSize =
                        parseVector(interior.at("world_size"));
                    snapshot.exteriorEntrance = ContentRect{
                        parseVector(interior.at("exterior_entrance")
                                        .at("position")),
                        parseVector(interior.at("exterior_entrance")
                                        .at("size"))};
                    snapshot.exteriorReturn =
                        parseVector(interior.at("exterior_return"));
                    snapshot.interiorSpawn =
                        parseVector(interior.at("interior_spawn"));
                    snapshot.interiorExit = ContentRect{
                        parseVector(interior.at("interior_exit")
                                        .at("position")),
                        parseVector(interior.at("interior_exit")
                                        .at("size"))};
                    for (const Json &blocker :
                         interior.at("ballistic_blockers"))
                    {
                        snapshot.ballisticBlockers.push_back(ContentRect{
                            parseVector(blocker.at("position")),
                            parseVector(blocker.at("size"))});
                    }
                    raid.interiors.push_back(std::move(snapshot));
                }
            }
            if (schemaVersion >= 9)
            {
                const Json &travel = value.at("travel");
                const Json &startingResources =
                    travel.at("starting_base_resources");
                raid.travel = RaidTravelSnapshot{
                    travel.at("outbound_minutes").get<std::uint32_t>(),
                    travel.at("return_minutes").get<std::uint32_t>(),
                    travel.at("failure_regroup_minutes")
                        .get<std::uint32_t>(),
                    WorldClockState{
                        travel.at("starting_world_clock")
                            .at("elapsed_world_minutes")
                            .get<std::uint64_t>()},
                    BaseResourceState{
                        BaseResourceBundle{
                            startingResources.at("food")
                                .get<std::uint32_t>(),
                            startingResources.at("hygiene")
                                .get<std::uint32_t>(),
                            startingResources.at("morale")
                                .get<std::uint32_t>(),
                            startingResources.at("security")
                                .get<std::uint32_t>()},
                        BaseResourceBundle{
                            startingResources.at("shortfall_food")
                                .get<std::uint32_t>(),
                            startingResources.at("shortfall_hygiene")
                                .get<std::uint32_t>(),
                            startingResources.at("shortfall_morale")
                                .get<std::uint32_t>(),
                            startingResources.at("shortfall_security")
                                .get<std::uint32_t>()},
                        startingResources.at(
                            "resolved_demand_cycle_count")
                            .get<std::uint64_t>()},
                    schemaVersion >= 11
                        ? (schemaVersion >= 43
                            ? parseBasePriority(
                                  travel.at("starting_base_priority"), schemaVersion)
                            : parseLegacyBasePriority(
                                  travel.at("starting_base_priority"),
                                  profile.basePopulation.ordinaryResidents))
                        : BasePriorityState{}};
                if (schemaVersion >= 11 && schemaVersion < 43)
                {
                    reconcileLegacyBasePriority(
                        raid.travel.startingBasePriority,
                        content);
                }
                if (schemaVersion < 11)
                {
                    ProfileState startingState = profile;
                    startingState.worldClock =
                        raid.travel.startingWorldClock;
                    startingState.basePriority = BasePriorityState{};
                    static_cast<void>(synchronizeBasePriorityThrough(
                        startingState,
                        content));
                    raid.travel.startingBasePriority =
                        startingState.basePriority;
                }
                if (schemaVersion >= 18)
                {
                    raid.travel.startingBaseMorale = parseBaseMorale(
                        travel.at("starting_base_morale"));
                    raid.travel.startingBaseCommunityEvent =
                        parseBaseCommunityEvent(
                            travel.at("starting_base_community_event"));
                }
                else
                {
                    raid.travel.startingBaseMorale = BaseMoraleState{};
                    raid.travel.startingBaseMorale.resolvedDayCount =
                        projectWorldClock(
                            raid.travel.startingWorldClock).completedDays;
                    ProfileState startingState;
                    startingState.profileId = profile.profileId;
                    startingState.worldClock =
                        raid.travel.startingWorldClock;
                    static_cast<void>(synchronizeBaseCommunityEventThrough(
                        startingState,
                        content));
                    raid.travel.startingBaseCommunityEvent =
                        startingState.baseCommunityEvent;
                }
                if (schemaVersion >= 20)
                {
                    for (const Json &entry : travel.at(
                             "starting_raid_intelligence_archive"))
                    {
                        const MapDefinitionId mapId{
                            entry.at("map_definition_id")
                                .get<std::string>()};
                        std::array<std::uint32_t,
                                   kRaidIntelligenceCategoryCount> counts{
                            entry.at("transport").get<std::uint32_t>(),
                            entry.at("resource").get<std::uint32_t>(),
                            entry.at("enemy").get<std::uint32_t>()};
                        if (!raid.travel.startingRaidIntelligence.counts
                                 .emplace(mapId, counts).second)
                        {
                            return {SaveLoadStatus::Failed, std::nullopt,
                                    "starting Raid intelligence is duplicated"};
                        }
                    }
                }
                else
                {
                    raid.travel.startingRaidIntelligence =
                        profile.raidIntelligence;
                }
                if (travel.contains("route_ids") &&
                    travel.contains("starting_regional_operations"))
                {
                    for (const Json &route : travel.at("route_ids"))
                    {
                        raid.travel.routeIds.emplace_back(
                            route.get<std::string>());
                    }
                    raid.travel.startingRegionalOperations =
                        parseRegionalOperations(
                            travel.at("starting_regional_operations"),
                            content,
                            schemaVersion);
                }
                else
                {
                    raid.travel.startingRegionalOperations =
                        profile.regionalOperations;
                }
                raid.travel.startingBaseSiege = schemaVersion >= 32
                    ? parseBaseSiege(travel.at("starting_base_siege"))
                    : defaultBaseSiege(raid.travel.startingWorldClock);
                if (schemaVersion < 33)
                {
                    normalizeBaseThreatCapacity(
                        raid.travel.startingBaseSiege);
                }
                if (schemaVersion >= 14)
                {
                    const Json &construction = travel.at(
                        "starting_base_construction");
                    raid.travel.startingBaseConstruction.materialUnits =
                        construction.at("material_units")
                            .get<std::uint32_t>();
                    raid.travel.startingBaseConstruction.dormitoryLevel =
                        construction.at("dormitory_level")
                            .get<std::uint32_t>();
                    if (schemaVersion >= 19)
                    {
                        raid.travel.startingBaseConstruction.workshopLevel =
                            construction.at("workshop_level")
                                .get<std::uint32_t>();
                        raid.travel.startingBaseConstruction.medicalLevel =
                            construction.at("medical_level")
                                .get<std::uint32_t>();
                        const Json &workforce = travel.at(
                            "starting_base_workforce");
                        raid.travel.startingBaseWorkforce.workshopWorker =
                            parseOptionalProfession(
                                workforce.at("workshop_worker"));
                        raid.travel.startingBaseWorkforce.medicalWorker =
                            parseOptionalProfession(
                                workforce.at("medical_worker"));
                    }
                    else
                    {
                        raid.travel.startingBaseWorkforce =
                            profile.baseWorkforce;
                    }
                    if (schemaVersion >= 30)
                    {
                        parseFacilityState(
                            construction,
                            raid.travel.startingBaseConstruction,
                            content);
                    }
                    else
                    {
                        initializeLegacyFacilityOwnership(
                            raid.travel.startingBaseConstruction,
                            content,
                            raid.travel.startingWorldClock
                                .elapsedWorldMinutes);
                    }
                    if (!construction.at("active_project").is_null())
                    {
                        const Json &project = construction.at(
                            "active_project");
                        raid.travel.startingBaseConstruction.activeProject =
                            ActiveBaseConstructionProject{
                                BaseConstructionProjectDefinitionId{
                                    project.at("definition_id")
                                        .get<std::string>()},
                                project.at("locked_material_units")
                                    .get<std::uint32_t>(),
                                project.at("committed_workers")
                                    .get<std::uint32_t>(),
                                project.at("started_world_minute")
                                    .get<std::uint64_t>(),
                                project.at("completion_world_minute")
                                    .get<std::uint64_t>()};
                    }
                    raid.travel.startingBedCapacity = travel.at(
                        "starting_bed_capacity").get<std::uint32_t>();
                    if (schemaVersion >= 16)
                    {
                        raid.travel.startingInjuredResidents = travel.at(
                            "starting_injured_residents")
                                .get<std::uint32_t>();
                        raid.travel.startingInjuredByProfession =
                            schemaVersion >= 19
                            ? parseBaseProfessionCounts(travel.at(
                                  "starting_injured_by_profession"))
                            : profile.basePopulation.injuredByProfession;
                        if (!travel.at("starting_resident_treatment").is_null())
                        {
                            const Json &treatment = travel.at(
                                "starting_resident_treatment");
                            ActiveResidentTreatment active;
                            active.jobId = treatment.at("job_id")
                                .get<BaseServiceJobId>();
                            active.startedWorldMinute = treatment.at(
                                "started_world_minute").get<std::uint64_t>();
                            active.completionWorldMinute = treatment.at(
                                "completion_world_minute").get<std::uint64_t>();
                            active.consumedContribution = treatment.at(
                                "consumed_contribution").get<std::uint32_t>();
                            if (schemaVersion >= 19)
                            {
                                active.patientProfession =
                                    parseRequiredProfession(treatment.at(
                                        "patient_profession"));
                                active.workerProfession =
                                    parseRequiredProfession(treatment.at(
                                        "worker_profession"));
                            }
                            else if (profile.residentMedical.activeTreatment
                                         .has_value())
                            {
                                active.patientProfession = profile
                                    .residentMedical.activeTreatment
                                    ->patientProfession;
                                active.workerProfession = profile
                                    .residentMedical.activeTreatment
                                    ->workerProfession;
                            }
                            raid.travel.startingResidentMedical
                                .activeTreatment = active;
                        }
                    }
                }
                else
                {
                    raid.travel.startingBaseConstruction =
                        profile.baseConstruction;
                    raid.travel.startingBaseWorkforce = profile.baseWorkforce;
                    raid.travel.startingBedCapacity =
                        profile.basePopulation.bedCapacity;
                    raid.travel.startingInjuredResidents = 0U;
                    raid.travel.startingInjuredByProfession = {};
                    raid.travel.startingResidentMedical = {};
                }
            }
            else
            {
                raid.travel.startingWorldClock = profile.worldClock;
                raid.travel.startingBaseResources = profile.baseResources;
                raid.travel.startingBasePriority = profile.basePriority;
                raid.travel.startingBaseConstruction =
                    profile.baseConstruction;
                raid.travel.startingBaseWorkforce = profile.baseWorkforce;
                raid.travel.startingBedCapacity =
                    profile.basePopulation.bedCapacity;
                raid.travel.startingInjuredResidents = 0U;
                raid.travel.startingInjuredByProfession = {};
                raid.travel.startingResidentMedical = {};
                raid.travel.startingRaidIntelligence =
                    profile.raidIntelligence;
                raid.travel.startingRegionalOperations =
                    profile.regionalOperations;
            }
            if (schemaVersion < 27 &&
                (raid.rulesVersion == "regional-route-network-17" ||
                 raid.rulesVersion ==
                     "regional-outpost-restoration-18" ||
                 raid.rulesVersion ==
                     "regional-base-site-clearance-19"))
            {
                ProfileState startingRouteProfile = profile;
                startingRouteProfile.regionalOperations =
                    raid.travel.startingRegionalOperations;
                const RegionalRoutePlan route = queryRegionalRoute(
                    startingRouteProfile,
                    content,
                    raid.mapDefinitionId);
                if (!route.reachable ||
                    route.travelMinutes != raid.travel.outboundMinutes ||
                    route.travelMinutes != raid.travel.returnMinutes ||
                    route.travelMinutes >
                        std::numeric_limits<std::uint32_t>::max() / 2U ||
                    route.travelMinutes * 2U !=
                        raid.travel.failureRegroupMinutes)
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "legacy regional route snapshot is invalid"};
                }
                raid.travel.routeIds = route.routeIds;
            }
            if (!raid.travel.startingBaseCommunityEvent.definitionId.valid())
            {
                raid.travel.startingBaseMorale = BaseMoraleState{};
                raid.travel.startingBaseMorale.resolvedDayCount =
                    projectWorldClock(
                        raid.travel.startingWorldClock).completedDays;
                ProfileState startingState;
                startingState.profileId = profile.profileId;
                startingState.worldClock = raid.travel.startingWorldClock;
                static_cast<void>(synchronizeBaseCommunityEventThrough(
                    startingState,
                    content));
                raid.travel.startingBaseCommunityEvent =
                    startingState.baseCommunityEvent;
            }
            if (schemaVersion >= 13 && !value.at("rescue").is_null())
            {
                const Json &rescue = value.at("rescue");
                if (rescue.at("subject_kind").get<std::string>() !=
                    "ordinary_residents")
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "pending rescue subject kind is invalid"};
                }
                const RescueDefinitionId rescueId{
                    rescue.at("definition_id").get<std::string>()};
                BaseResidentProfession rescueProfession =
                    BaseResidentProfession::General;
                if (schemaVersion >= 19)
                {
                    const auto parsed = parseBaseResidentProfession(
                        rescue.at("profession").get<std::string>());
                    if (!parsed.has_value())
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "pending rescue profession is invalid"};
                    }
                    rescueProfession = *parsed;
                }
                else
                {
                    for (const MapDefinition &map : content.maps())
                    {
                        if (map.rescue.has_value() &&
                            map.rescue->id == rescueId)
                        {
                            rescueProfession = map.rescue->profession;
                            break;
                        }
                    }
                }
                raid.rescue = RaidRescueSnapshot{
                    rescueId,
                    RaidRescueSubjectKind::OrdinaryResidents,
                    ContentRect{
                        parseVector(rescue.at("transfer_point").at("position")),
                        parseVector(rescue.at("transfer_point").at("size"))},
                    rescue.at("interaction_duration_seconds").get<float>(),
                    rescue.at("ordinary_resident_count").get<std::uint32_t>(),
                    schemaVersion >= 16
                         ? rescue.at("injured_resident_count")
                               .get<std::uint32_t>()
                         : 0U,
                    rescueProfession,
                    rescue.at("secured").get<bool>()};
            }
            if (schemaVersion >= 26 &&
                !value.at("self_recovery").is_null())
            {
                const Json &self = value.at("self_recovery");
                const Json &source = self.at("source_record");
                const auto outcome = parseRaidOutcome(
                    source.at("outcome").get<std::string>());
                if (!outcome.has_value())
                {
                    return {SaveLoadStatus::Failed, std::nullopt,
                            "self-recovery outcome is invalid"};
                }
                RaidSelfRecoverySnapshot recovery;
                recovery.sourceRecord = LostRaidRecord{
                    source.at("record_id").get<std::string>(),
                    source.at("raid_id").get<std::string>(),
                    source.at("settlement_id").get<std::string>(),
                    MapDefinitionId{source.at("map_definition_id")
                        .get<std::string>()},
                    source.at("difficulty").get<std::string>(),
                    *outcome,
                    source.at("created_world_minute").get<std::uint64_t>(),
                    source.at("subsequent_raid_settlement_count")
                        .get<std::uint32_t>()};
                recovery.cachePosition = parseVector(
                    self.at("cache_position"));
                recovery.interactionDurationSeconds = self.at(
                    "interaction_duration_seconds").get<float>();
                recovery.opened = self.at("opened").get<bool>();
                for (const Json &root : self.at("roots"))
                {
                    const auto slot = parseSlot(
                        root.at("source_slot").get<std::string>());
                    if (!slot.has_value())
                    {
                        return {SaveLoadStatus::Failed, std::nullopt,
                                "self-recovery source slot is invalid"};
                    }
                    recovery.roots.push_back(
                        RaidSelfRecoveryRootSnapshot{
                            root.at("asset_id").get<AssetInstanceId>(),
                            *slot,
                            root.at("loot_slot_index")
                                .get<std::uint32_t>(),
                            parseVector(root.at("position"))});
                }
                raid.selfRecovery = std::move(recovery);
            }
            if (schemaVersion >= 28 &&
                !value.at("outpost_restoration").is_null())
            {
                const Json &restoration =
                    value.at("outpost_restoration");
                raid.outpostRestoration =
                    RegionalOutpostRestorationSnapshot{
                        RegionalOutpostDefinitionId{
                            restoration.at("outpost_definition_id")
                                .get<std::string>()},
                        restoration.at("objective_secured")
                            .get<bool>()};
            }
            if (schemaVersion >= 29 &&
                !value.at("base_site_clearance").is_null())
            {
                const Json &clearance = value.at("base_site_clearance");
                raid.baseSiteClearance =
                    RegionalBaseSiteClearanceSnapshot{
                        RegionalBaseSiteDefinitionId{
                            clearance.at("base_site_definition_id")
                                .get<std::string>()},
                        clearance.at("objective_secured").get<bool>()};
            }
            if (schemaVersion >= 33 &&
                !value.at("base_perimeter_sweep").is_null())
            {
                const Json &sweep = value.at("base_perimeter_sweep");
                raid.basePerimeterSweep = BasePerimeterSweepSnapshot{
                    RegionalBaseSiteDefinitionId{
                        sweep.at("base_site_definition_id")
                            .get<std::string>()},
                    sweep.at("threat_reduction_units")
                        .get<std::uint32_t>(),
                    sweep.at("objective_secured").get<bool>()};
            }
            profile.pendingRaid = std::move(raid);
        }

        if (schemaVersion >= 2 && !payload.at("last_raid_result").is_null())
        {
            const Json &value = payload.at("last_raid_result");
            const auto outcome = parseRaidOutcome(
                value.at("outcome").get<std::string>());
            if (!outcome.has_value())
            {
                return {SaveLoadStatus::Failed, std::nullopt,
                        "Raid result outcome is invalid"};
            }
            LastRaidResult result;
            result.settlementId =
                value.at("settlement_id").get<std::string>();
            result.outcome = *outcome;
            for (const Json &id :
                 value.at("returned_item_definition_ids"))
            {
                result.returnedItemDefinitionIds.emplace_back(
                    id.get<std::string>());
            }
            result.currencyDelta = value.at("currency_delta").get<std::int64_t>();
            result.travelMinutesApplied = schemaVersion >= 9
                ? value.at("travel_minutes_applied").get<std::uint32_t>()
                : 0U;
            result.rescuedOrdinaryResidents = schemaVersion >= 13
                ? value.at("rescued_ordinary_residents")
                      .get<std::uint32_t>()
                : 0U;
            result.rescuedInjuredResidents = schemaVersion >= 16
                ? value.at("rescued_injured_residents")
                      .get<std::uint32_t>()
                : 0U;
            if (schemaVersion >= 24 &&
                !value.at("lost_raid_record_id").is_null())
            {
                result.lostRaidRecordId = value.at("lost_raid_record_id")
                    .get<std::string>();
            }
            result.baseThreatReducedUnits = schemaVersion >= 33
                ? value.at("base_threat_reduced_units")
                      .get<std::uint32_t>()
                : 0U;
            profile.lastRaidResult = std::move(result);
        }

        if (schemaVersion >= 44)
        {
            if (profile.pendingRaid && !payload.at("pending_raid").at("wish_focus").is_null())
                profile.pendingRaid->wishFocus = parseWishSnapshot(payload.at("pending_raid").at("wish_focus"));
            if (profile.lastRaidResult && !payload.at("last_raid_result").at("wish_return").is_null())
            {
                const auto &value = payload.at("last_raid_result").at("wish_return");
                profile.lastRaidResult->wishReturn = BaseWishReturnSummary{
                    parseWishSnapshot(value.at("focus")), value.at("items").get<std::uint64_t>(),
                    value.at("contribution").get<std::uint64_t>(), value.at("expired").get<bool>()};
            }
        }
        const ProfileValidationResult validation =
            validateProfileState(profile, content);
        if (!validation.valid)
        {
            return {SaveLoadStatus::Failed, std::nullopt, validation.message};
        }
        return {SaveLoadStatus::LoadedPrimary, std::move(profile), {}};
    }
    catch (const std::exception &error)
    {
        return {SaveLoadStatus::Failed, std::nullopt, error.what()};
    }
}

SaveRepository::SaveRepository(std::filesystem::path directory)
    : directory_{std::move(directory)},
      primaryPath_{directory_ / "profile.json"},
      backupPath_{directory_ / "profile.backup.json"},
      temporaryPath_{directory_ / "profile.tmp.json"},
      backupTemporaryPath_{directory_ / "profile.backup.tmp.json"}
{
}

bool SaveRepository::primaryExists() const
{
    std::error_code error;
    if (std::filesystem::is_regular_file(primaryPath_, error))
    {
        return true;
    }
    error.clear();
    return std::filesystem::is_regular_file(backupPath_, error);
}

SaveWriteResult SaveRepository::save(
    const ProfileState &profile,
    std::string_view contentVersion) const
{
    const ProfileValidationResult validation =
        validateProfileState(profile, publishedContentRegistry());
    if (!validation.valid)
    {
        return {false, validation.message};
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error)
    {
        return {false, "save directory could not be created"};
    }

    const std::string text = serializeProfileEnvelope(profile, contentVersion);
    if (!writeText(temporaryPath_, text))
    {
        return {false, "temporary save could not be written"};
    }
    const auto readBack = readText(temporaryPath_);
    if (!readBack.has_value() ||
        !deserializeProfileEnvelope(*readBack, publishedContentRegistry())
             .profile.has_value())
    {
        std::filesystem::remove(temporaryPath_, error);
        return {false, "temporary save failed read-back validation"};
    }

    const bool validPrimary =
        isValidEnvelope(primaryPath_, publishedContentRegistry());
    const bool validBackup =
        isValidEnvelope(backupPath_, publishedContentRegistry());
    const bool mirrorPendingRaid = profile.pendingRaid.has_value();
    bool installCandidateBackup = false;
    if (mirrorPendingRaid)
    {
        error.clear();
        std::filesystem::copy_file(
            temporaryPath_,
            backupTemporaryPath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !isValidEnvelope(
                         backupTemporaryPath_,
                         publishedContentRegistry()))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "pending Raid recovery backup could not be prepared"};
        }
        installCandidateBackup = true;
    }
    else if (validPrimary)
    {
        std::filesystem::copy_file(
            primaryPath_,
            backupTemporaryPath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !isValidEnvelope(
                         backupTemporaryPath_,
                         publishedContentRegistry()))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "safe backup could not be updated"};
        }
        if (!atomicReplace(backupTemporaryPath_, backupPath_))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "safe backup could not be atomically replaced"};
        }
    }
    else if (!validBackup)
    {
        error.clear();
        std::filesystem::copy_file(
            temporaryPath_,
            backupTemporaryPath_,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !isValidEnvelope(
                         backupTemporaryPath_,
                         publishedContentRegistry()))
        {
            std::filesystem::remove(temporaryPath_, error);
            std::filesystem::remove(backupTemporaryPath_, error);
            return {false, "initial safe backup could not be created"};
        }
        installCandidateBackup = true;
    }

    if (!atomicReplace(temporaryPath_, primaryPath_))
    {
        std::filesystem::remove(temporaryPath_, error);
        std::filesystem::remove(backupTemporaryPath_, error);
        return {false, "primary save could not be atomically replaced"};
    }
    if (installCandidateBackup &&
        !atomicReplace(backupTemporaryPath_, backupPath_))
    {
        std::filesystem::remove(backupTemporaryPath_, error);
        return {true, "primary save committed; recovery backup could not be installed"};
    }
    return {true, {}};
}

SaveLoadResult SaveRepository::load(const ContentRegistry &content) const
{
    const auto primaryText = readText(primaryPath_);
    if (primaryText.has_value())
    {
        SaveLoadResult result = deserializeProfileEnvelope(*primaryText, content);
        if (result.profile.has_value())
        {
            result.status = SaveLoadStatus::LoadedPrimary;
            return result;
        }
    }

    const auto backupText = readText(backupPath_);
    if (backupText.has_value())
    {
        SaveLoadResult result = deserializeProfileEnvelope(*backupText, content);
        if (result.profile.has_value())
        {
            result.status = SaveLoadStatus::RecoveredBackup;
            result.message = "primary save was invalid; recovered safe backup";
            return result;
        }
    }

    if (!primaryText.has_value() && !backupText.has_value())
    {
        return {SaveLoadStatus::NotFound, std::nullopt, {}};
    }
    return {SaveLoadStatus::Failed, std::nullopt, "no valid primary or backup save"};
}

const std::filesystem::path &SaveRepository::primaryPath() const noexcept
{
    return primaryPath_;
}

const std::filesystem::path &SaveRepository::backupPath() const noexcept
{
    return backupPath_;
}
