#include "raid_squad_save.h"

#include <limits>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace {
using Json = nlohmann::json;

void require(bool condition)
{
    if (!condition) throw std::invalid_argument{"invalid specialist squad save data"};
}

const char *roleName(RaidSquadRole role)
{
    switch (role) {
    case RaidSquadRole::Fire: return "fire";
    case RaidSquadRole::Support: return "support";
    case RaidSquadRole::Sniper: return "sniper";
    }
    throw std::invalid_argument{"unknown squad role"};
}

RaidSquadRole parseRole(const Json &value)
{
    for (auto role : {RaidSquadRole::Fire, RaidSquadRole::Support, RaidSquadRole::Sniper})
        if (value == roleName(role)) return role;
    throw std::invalid_argument{"unknown squad role"};
}

template<class T> T unsignedValue(const Json &value)
{
    require(value.is_number_unsigned() && value.get<std::uint64_t>() <=
            static_cast<std::uint64_t>(std::numeric_limits<T>::max()));
    return value.get<T>();
}

Json loanValue(const RaidFireSupportLoan &loan)
{
    return {{"asset",loan.assetId},{"x",loan.returnOrigin.x},{"y",loan.returnOrigin.y},
            {"orientation",static_cast<unsigned>(loan.originalOrientation)}};
}

RaidFireSupportLoan readLoan(const Json &value)
{
    const auto rotation = unsignedValue<unsigned>(value.at("orientation"));
    require(rotation <= static_cast<unsigned>(ItemOrientation::Degrees270));
    return {unsignedValue<AssetInstanceId>(value.at("asset")),
            {unsignedValue<int>(value.at("x")), unsignedValue<int>(value.at("y"))},
            static_cast<ItemOrientation>(rotation)};
}

Json loanArray(const std::vector<RaidFireSupportLoan> &loans)
{
    auto result = Json::array();
    for (const auto &loan : loans) result.push_back(loanValue(loan));
    return result;
}

void readLoans(const Json &array, std::vector<RaidFireSupportLoan> &loans)
{
    require(array.is_array());
    for (const auto &entry : array) loans.push_back(readLoan(entry));
}

bool namedPatient(const BaseResidentMedicalState &medical) noexcept
{
    return medical.activeTreatment && medical.activeTreatment->squadPatient;
}

Json patientValue(const BaseResidentMedicalState &medical)
{
    return namedPatient(medical) ? Json(roleName(*medical.activeTreatment->squadPatient)) : Json(nullptr);
}

void readPatient(const Json &value, BaseResidentMedicalState &medical)
{
    if (value.is_null()) return;
    require(medical.activeTreatment.has_value());
    medical.activeTreatment->squadPatient = parseRole(value);
}

bool specialistLoss(const LostRaidRecord &record) noexcept
{
    return record.kind == LostRaidRecordKind::SpecialistSupport || record.kind == LostRaidRecordKind::SpecialistSniper;
}

const char *statusName(RaidSquadResidentStatus status)
{
    switch (status) {
    case RaidSquadResidentStatus::Ready: return "ready";
    case RaidSquadResidentStatus::Wounded: return "wounded";
    case RaidSquadResidentStatus::Missing: return "missing";
    }
    throw std::invalid_argument{"unknown resident status"};
}
}

bool hasRaidSquadSaveData(const ProfileState &profile) noexcept
{
    if (!profile.squadRoster.empty() || namedPatient(profile.residentMedical) ||
        (profile.lastRaidResult && !profile.lastRaidResult->squadResults.empty())) return true;
    if (const auto &raid = profile.pendingRaid) {
        if (!raid->squadMembers.empty() || !raid->squadOrder.empty() || raid->specialistRescue ||
            raid->fireSupportHealth != 100 || !raid->fireSupportCargo.empty() ||
            namedPatient(raid->travel.startingResidentMedical) ||
            (raid->selfRecovery && specialistLoss(raid->selfRecovery->sourceRecord))) return true;
    }
    if (profile.recoveryTask && specialistLoss(profile.recoveryTask->sourceRecord)) return true;
    for (const auto &[id, record] : profile.lostRaidRecords)
        if (specialistLoss(record)) return true;
    return false;
}

nlohmann::json writeRaidSquadSave(const ProfileState &profile)
{
    Json out{{"roster", Json::array()}, {"patient", patientValue(profile.residentMedical)},
             {"raid", nullptr}, {"results", Json::array()}};
    for (const auto &[role, resident] : profile.squadRoster)
        out["roster"].push_back({{"role",roleName(role)}, {"status",statusName(resident.status)},
            {"available_minute",resident.replacementReadyMinute}, {"generation",resident.generation}});
    if (const auto &raid = profile.pendingRaid) {
        Json active{{"members",Json::array()}, {"order",Json::array()},
            {"escort",raid->specialistRescue ? Json(roleName(*raid->specialistRescue)) : Json(nullptr)},
            {"fire_health",raid->fireSupportHealth}, {"fire_cargo",loanArray(raid->fireSupportCargo)},
            {"starting_patient",patientValue(raid->travel.startingResidentMedical)}};
        for (auto role : raid->squadOrder) active["order"].push_back(roleName(role));
        for (const auto &member : raid->squadMembers)
            active["members"].push_back({{"role",roleName(member.role)}, {"container",member.containerAssetId},
                {"weapon",loanValue(member.weapon)}, {"cargo",loanArray(member.cargo)}, {"health",member.health}});
        out["raid"] = std::move(active);
    }
    if (profile.lastRaidResult) {
        for (const auto &[role, result] : profile.lastRaidResult->squadResults)
            out["results"].push_back({{"role",roleName(role)}, {"died",result.died},
                {"returned",result.returnedLoanCount}, {"loss",result.lostRecordId ? Json(*result.lostRecordId) : Json(nullptr)}});
    }
    return out;
}

void readRaidSquadSave(const nlohmann::json &value, ProfileState &profile)
{
    const auto &roster = value.at("roster");
    require(roster.is_array() && roster.size() <= 3);
    for (const auto &entry : roster) {
        RaidSquadResident resident;
        const auto &status = entry.at("status");
        if (status == "ready") resident.status = RaidSquadResidentStatus::Ready;
        else if (status == "wounded") resident.status = RaidSquadResidentStatus::Wounded;
        else if (status == "missing") resident.status = RaidSquadResidentStatus::Missing;
        else throw std::invalid_argument{"unknown squad resident status"};
        resident.replacementReadyMinute = unsignedValue<std::uint64_t>(entry.at("available_minute"));
        resident.generation = unsignedValue<std::uint32_t>(entry.at("generation"));
        require(profile.squadRoster.emplace(parseRole(entry.at("role")), resident).second);
    }
    readPatient(value.at("patient"), profile.residentMedical);
    const auto &active = value.at("raid");
    require(active.is_null() == !profile.pendingRaid.has_value());
    if (auto &raid = profile.pendingRaid) {
        const auto &order = active.at("order");
        const auto &members = active.at("members");
        require(order.is_array() && order.size() <= 3 && members.is_array() && members.size() <= 2);
        for (const auto &entry : order) raid->squadOrder.push_back(parseRole(entry));
        if (!active.at("escort").is_null()) raid->specialistRescue = parseRole(active.at("escort"));
        raid->fireSupportHealth = unsignedValue<int>(active.at("fire_health"));
        readLoans(active.at("fire_cargo"), raid->fireSupportCargo);
        readPatient(active.at("starting_patient"), raid->travel.startingResidentMedical);
        for (const auto &entry : members) {
            RaidSquadMember member;
            member.role = parseRole(entry.at("role"));
            member.containerAssetId = unsignedValue<AssetInstanceId>(entry.at("container"));
            member.weapon = readLoan(entry.at("weapon"));
            readLoans(entry.at("cargo"), member.cargo);
            member.health = unsignedValue<int>(entry.at("health"));
            raid->squadMembers.push_back(std::move(member));
        }
    }
    const auto &results = value.at("results");
    require(results.is_array() && results.size() <= 3 && (results.empty() || profile.lastRaidResult.has_value()));
    for (const auto &entry : results) {
        RaidFireSupportResult result;
        result.died = entry.at("died").get<bool>();
        result.returnedLoanCount = unsignedValue<std::uint32_t>(entry.at("returned"));
        if (!entry.at("loss").is_null()) result.lostRecordId = entry.at("loss").get<std::string>();
        require(profile.lastRaidResult->squadResults.emplace(parseRole(entry.at("role")), std::move(result)).second);
    }
}
