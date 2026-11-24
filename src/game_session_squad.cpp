#include "game_session.h"
#include "raid_squad_domain.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace
{
const RaidSquadMember *findSpecialist(const PendingRaidSnapshot &raid, RaidSquadRole role)
{
    const auto it = std::ranges::find(raid.squadMembers, role, &RaidSquadMember::role);
    return it == raid.squadMembers.end() ? nullptr : &*it;
}

bool onMember(const AssetRecord &asset, AssetInstanceId container)
{
    const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
    return stored && stored->container == ProfileContainerId::compartment(container, 0);
}

bool stopsBleeding(MedicalItemEffect effect, BleedingSeverity severity)
{
    return severity != BleedingSeverity::None && (effect == MedicalItemEffect::StopAnyBleeding ||
        (effect == MedicalItemEffect::StopLightBleeding && severity == BleedingSeverity::Light));
}
}

std::vector<AssetInstanceId> GameSession::squadCargo(RaidSquadRole role) const
{
    std::vector<AssetInstanceId> items;
    if (!alphaRaidActive_ || !world_ || !profile_.pendingRaid || role != RaidSquadRole::Fire ||
        !profile_.pendingRaid->fireSupport || !world_->canInteractWithSquad(role) ||
        world_->squadHealth(role).value_or(0) <= 0) return items;
    const auto container = profile_.pendingRaid->fireSupport->containerAssetId;
    for (const auto &loan : profile_.pendingRaid->fireSupportCargo)
        if (const auto *asset = profile_.assets.find(loan.assetId); asset && onMember(*asset, container))
            items.push_back(loan.assetId);
    return items;
}

InventoryReceipt GameSession::takeSquadCargo(RaidSquadRole role, AssetInstanceId id, std::string transactionId)
{
    if (!transactionId.empty() && profile_.committedTransactions.contains(transactionId))
        return {true, true, DomainErrorCode::None, {}, profile_.revision};
    const auto available = squadCargo(role);
    if (profileUiCommitPending() || std::ranges::find(available, id) == available.end())
        return {false, false, DomainErrorCode::IllegalDestination, "squad cargo unavailable", profile_.revision};
    return takeRaidSquadCargo(profile_, publishedContentRegistry(), role, id,
        {profile_.revision, std::move(transactionId)});
}

bool GameSession::orderMedicTreatment()
{
    return beginMedicTreatment(true);
}

MedicOrderStatus GameSession::medicOrderStatus() const
{
    MedicTreatment prepared{};
    return planMedicTreatment(true, prepared);
}

MedicOrderStatus GameSession::planMedicTreatment(bool ordered, MedicTreatment &prepared) const
{
    if (!alphaRaidActive_ || state_ != GameSessionState::InRaid || !world_ || !profile_.pendingRaid ||
        !world_->raidSession().isActive() || profileUiCommitPending()) return MedicOrderStatus::Unavailable;
    if (medicTreatment_) return MedicOrderStatus::Busy;
    const auto *medic = findSpecialist(*profile_.pendingRaid, RaidSquadRole::Support);
    if (!medic || world_->squadHealth(RaidSquadRole::Support).value_or(0) <= 0)
        return MedicOrderStatus::Unavailable;
    if (medic->cargo.empty()) return MedicOrderStatus::NoSupplies;

    std::optional<RaidSquadRole> patient;
    const bool playerReachable = world_->medicCanReach(std::nullopt);
    const auto bleeding = playerReachable ? profile_.medicalStatus.bleeding : BleedingSeverity::None;
    const int threshold = ordered ? kFireSupportMaxHealth : 60;
    bool wounded = profile_.currentHealth > 0 && (profile_.currentHealth < threshold ||
        profile_.medicalStatus.bleeding != BleedingSeverity::None);
    int lowest = playerReachable && profile_.currentHealth < threshold ? profile_.currentHealth : threshold;
    for (const auto role : {RaidSquadRole::Fire, RaidSquadRole::Support, RaidSquadRole::Sniper})
    {
        const int health = world_->squadHealth(role).value_or(0);
        wounded |= health > 0 && health < threshold;
        if (health > 0 && health < lowest && world_->medicCanReach(role))
        {
            patient = role;
            lowest = health;
        }
    }
    if (lowest >= threshold && bleeding == BleedingSeverity::None)
        return wounded ? MedicOrderStatus::NoReachablePatient : MedicOrderStatus::NoPatient;
    if (!ordered && world_->squadMedicalThreat()) return MedicOrderStatus::Unavailable;
    std::optional<AssetInstanceId> chosen;
    std::tuple<int, int, int, AssetInstanceId> best{};
    const auto &content = publishedContentRegistry();
    float duration{};
    bool treatingBleeding{}, stocked{};
    for (const auto &loan : medic->cargo)
    {
        const auto *asset = profile_.assets.find(loan.assetId);
        if (!asset || !asset->remainingCharges || !onMember(*asset, medic->containerAssetId)) continue;
        stocked = true;
        const auto &definition = content.item(asset->definitionId).medicalUse;
        if (!definition) continue;
        const bool hemostatic = stopsBleeding(definition->effect, bleeding);
        if (!hemostatic && (definition->effect != MedicalItemEffect::RestoreHealth || lowest >= threshold)) continue;
        const int potency = hemostatic ? static_cast<int>(definition->effect == MedicalItemEffect::StopAnyBleeding)
                                     : static_cast<int>(definition->effectMagnitude);
        const auto rank = std::tuple{hemostatic ? 0 : 1, -potency, static_cast<int>(asset->remainingCharges), loan.assetId};
        if (chosen && rank >= best) continue;
        chosen = loan.assetId;
        best = rank;
        treatingBleeding = hemostatic;
        duration = static_cast<float>(definition->actionDurationMs) * .001F;
    }
    if (!chosen) return stocked ? MedicOrderStatus::NoSuitableSupply : MedicOrderStatus::NoSupplies;
    if (treatingBleeding) patient.reset();
    const int health = patient ? world_->squadHealth(*patient).value_or(0) : profile_.currentHealth;
    prepared = MedicTreatment{world_->activeRaidSpaceId(), patient, *chosen, duration, ordered, health};
    return MedicOrderStatus::Ready;
}

bool GameSession::beginMedicTreatment(bool ordered)
{
    MedicTreatment prepared{};
    if (planMedicTreatment(ordered, prepared) != MedicOrderStatus::Ready) return false;
    medicTreatment_ = std::move(prepared);
    world_->setMedicWorking(true);
    return true;
}

void GameSession::advanceMedicTreatment(float seconds)
{
    if (seconds <= 0 || !std::isfinite(seconds)) return;
    if (!medicTreatment_)
    {
        medicSearchDelay_ -= seconds;
        if (medicSearchDelay_ <= 0)
        {
            medicSearchDelay_ = .25F;
            static_cast<void>(beginMedicTreatment(false));
        }
        return;
    }
    auto &action = *medicTreatment_;
    const auto *asset = profile_.assets.find(action.medicine);
    const auto &content = publishedContentRegistry();
    const auto *medic = profile_.pendingRaid ? findSpecialist(*profile_.pendingRaid, RaidSquadRole::Support) : nullptr;
    const auto currentHealth = action.patient ? world_->squadHealth(*action.patient).value_or(0) : profile_.currentHealth;
    const auto stop = [&] { medicTreatment_.reset(); world_->setMedicWorking(false); };
    if (!asset || !medic || !asset->remainingCharges || !onMember(*asset, medic->containerAssetId) ||
        action.space != world_->activeRaidSpaceId() || !world_->medicCanReach(action.patient) ||
        (action.patient ? currentHealth < action.patientHealth : (lastIncomingDamage_ && lastIncomingDamage_->damageApplied > 0)) ||
        (!action.ordered && world_->squadMedicalThreat()))
    {
        stop();
        return;
    }
    action.patientHealth = currentHealth;
    const auto use = content.item(asset->definitionId).medicalUse;
    const bool healing = use && use->effect == MedicalItemEffect::RestoreHealth;
    if (!use || (healing ? currentHealth >= kFireSupportMaxHealth :
        (action.patient || !stopsBleeding(use->effect, profile_.medicalStatus.bleeding))))
    {
        stop();
        return;
    }
    action.remaining -= seconds;
    if (action.remaining > 0) return;
    const auto receipt = consumeRaidSquadMedical(profile_, content, action.medicine,
        {profile_.revision, nextRaidTransaction("squad-medicine")});
    if (receipt.succeeded && !receipt.alreadyCommitted)
    {
        if (healing && action.patient)
        {
            world_->restoreSquadHealth(*action.patient, static_cast<int>(use->effectMagnitude));
            setRaidSquadHealthInSimulation(profile_, *action.patient, world_->squadHealth(*action.patient).value());
        }
        else if (healing)
        {
            const int restored = std::min(100 - profile_.currentHealth, static_cast<int>(use->effectMagnitude));
            profile_.currentHealth += restored;
            static_cast<void>(world_->restorePlayerHealth(restored));
        }
        else
        {
            auto &status = profile_.medicalStatus;
            status.bleeding = BleedingSeverity::None;
            status.lightBleedingRemainingMs = 0;
            status.bleedingDamageRemainingMs = 0;
            status.painScreamRemainingMs = 0;
        }
    }
    stop();
}
