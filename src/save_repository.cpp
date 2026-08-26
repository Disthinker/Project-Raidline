#include "save_repository.h"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "base_resource_domain.h"
#include "base_morale_domain.h"

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
        else
        {
            if (schemaVersion < 10)
            {
                throw std::invalid_argument{
                    "legacy schema cannot represent Base service assets"};
            }
            const auto &service =
                std::get<BaseServiceAssetLocation>(asset.location);
            location = {
                {"type", "base_service"},
                {"job_id", service.jobId}};
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
        payload["base_priority"] = {
            {"definition_id", profile.basePriority.definitionId.value()},
            {"cycle_index", profile.basePriority.cycleIndex},
            {"fulfilled", profile.basePriority.fulfilled},
            {"missed_cycle_count",
             profile.basePriority.missedCycleCount}};
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
        Json enemies = Json::array();
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
            enemies.push_back(std::move(enemyValue));
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
                    ["starting_base_priority"] = {
                        {"definition_id",
                         raid.travel.startingBasePriority
                             .definitionId.value()},
                        {"cycle_index",
                         raid.travel.startingBasePriority.cycleIndex},
                        {"fulfilled",
                         raid.travel.startingBasePriority.fulfilled},
                        {"missed_cycle_count",
                         raid.travel.startingBasePriority
                             .missedCycleCount}};
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
    }
    else
    {
        payload["last_raid_result"] = nullptr;
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
        schemaVersion != 22 && schemaVersion != 23)
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
                  "raid-special-location-placement-content-31"));
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
             schemaVersion != 23) ||
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
            profile.basePriority = BasePriorityState{
                BasePriorityDefinitionId{
                    priority.at("definition_id").get<std::string>()},
                priority.at("cycle_index").get<std::uint64_t>(),
                priority.at("fulfilled").get<bool>(),
                priority.at("missed_cycle_count").get<std::uint64_t>()};
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
            else if (schemaVersion >= 10 && locationType == "base_service")
            {
                asset.location = BaseServiceAssetLocation{
                    location.at("job_id").get<BaseServiceJobId>()};
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
                        : outdoorRaidSpaceId()});
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
                        : outdoorRaidSpaceId()});
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
                        ? BasePriorityState{
                              BasePriorityDefinitionId{
                                  travel.at("starting_base_priority")
                                      .at("definition_id")
                                      .get<std::string>()},
                              travel.at("starting_base_priority")
                                  .at("cycle_index")
                                  .get<std::uint64_t>(),
                              travel.at("starting_base_priority")
                                  .at("fulfilled")
                                  .get<bool>(),
                              travel.at("starting_base_priority")
                                  .at("missed_cycle_count")
                                  .get<std::uint64_t>()}
                        : BasePriorityState{}};
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
            profile.lastRaidResult = std::move(result);
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
