#include "weapon_component_domain.h"
#include "recovery_task_domain.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

ProfileValidationResult validateWeaponComponents(const ProfileState &profile, const ContentRegistry &content)
{
    std::set<std::pair<AssetInstanceId, WeaponComponentSlot>> slots;
    try
    {
        for (const auto &[id, asset] : profile.assets.records())
        {
            const auto *location = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
            if (!location) continue;
            const auto &definition = content.item(asset.definitionId);
            const auto *weapon = profile.assets.find(location->weaponAssetId);
            if (!definition.weaponComponent || !weapon || id == weapon->instanceId || asset.quantity != 1 ||
                !content.item(weapon->definitionId).weaponUse ||
                definition.weaponComponent->weaponDefinitionId != weapon->definitionId ||
                !slots.emplace(weapon->instanceId, definition.weaponComponent->slot).second)
                return {false, "invalid installed weapon component"};
            if (profile.recoveryTask && recoveryTaskForAsset(profile, id) &&
                profile.recoveryTask->recoveredAssetIds.contains(id) !=
                    profile.recoveryTask->recoveredAssetIds.contains(weapon->instanceId))
                return {false, "component recovery outcome differs from weapon"};
            if (definition.weaponComponent->requiresUnderbarrel)
            {
                bool supported = false;
                for (const auto &[otherId, other] : profile.assets.records())
                {
                    const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&other.location);
                    if (!parent || parent->weaponAssetId != weapon->instanceId) continue;
                    const auto &capability = content.item(other.definitionId).weaponComponent;
                    supported |= capability && capability->providesUnderbarrel;
                }
                if (!supported) return {false, "UNDERBARREL RAIL REQUIRED"};
            }
        }
    }
    catch (...) { return {false, "unknown weapon component definition"}; }
    return {true, {}};
}

WeaponUseDefinition effectiveWeaponUse(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId weaponId)
{
    const auto *weapon = profile.assets.find(weaponId);
    if (!weapon) throw std::invalid_argument("missing weapon");
    auto result = content.item(weapon->definitionId).weaponUse.value();
    int recoil{}, stability{}, handling{}, ergonomics{}, accuracy{};
    float range{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
        if (!parent || parent->weaponAssetId != weaponId) continue;
        const auto &component = content.item(asset.definitionId).weaponComponent.value();
        recoil += component.recoilControl;
        stability += component.stability;
        handling += component.handlingSpeed;
        ergonomics += component.ergonomics;
        accuracy += component.accuracy;
        range += component.effectiveRange;
    }
    const auto adjusted = [](std::uint32_t base, int delta) { return static_cast<std::uint32_t>(std::clamp(static_cast<int>(base) + delta, 0, 100)); };
    result.recoilControl = adjusted(result.recoilControl, recoil);
    result.stability = adjusted(result.stability, stability);
    result.handlingSpeed = adjusted(result.handlingSpeed, handling);
    result.ergonomics = adjusted(result.ergonomics, ergonomics);
    result.accuracy = adjusted(result.accuracy, accuracy);
    result.effectiveRange = std::clamp(result.effectiveRange + range, 1.0F, result.maximumRange);
    return result;
}

namespace
{
WeaponComponentPlan prepare(ProfileState &candidate, const ContentRegistry &content, const WeaponComponentCommand &command)
{
    WeaponComponentPlan plan;
    plan.revision = candidate.revision;
    const auto reject = [&plan](DomainErrorCode error, const char *message) { plan.error = error; plan.message = message; return plan; };
    if (const auto validation = validateProfileState(candidate, content); !validation.valid)
    { plan.error = DomainErrorCode::InvalidProfile; plan.message = validation.message; return plan; }
    if (candidate.pendingRaid || candidate.activeBaseDefense)
        return reject(DomainErrorCode::IllegalDestination, "MODIFICATION REQUIRES BASE");
    const auto *weapon = candidate.assets.find(command.weaponAssetId);
    if (!weapon || !assetIsBaseAccessible(candidate, command.weaponAssetId) || !content.item(weapon->definitionId).weaponUse)
        return reject(DomainErrorCode::MissingAsset, "SELECT AN ACCESSIBLE WEAPON");
    // Supported platform is derived from published capabilities, not a display label.
    bool platform = false;
    for (const auto &definition : content.items())
        platform |= definition.weaponComponent && definition.weaponComponent->weaponDefinitionId == weapon->definitionId;
    if (!platform) return reject(DomainErrorCode::IncompatibleEquipment, "UNSUPPORTED WEAPON PLATFORM");
    if (command.slot != WeaponComponentSlot::Barrel && command.slot != WeaponComponentSlot::Handguard && command.slot != WeaponComponentSlot::Grip)
        return reject(DomainErrorCode::IncompatibleEquipment, "INVALID COMPONENT SLOT");
    if (command.componentAssetId)
    {
        const auto *component = candidate.assets.find(command.componentAssetId);
        if (!component || !assetIsBaseAccessible(candidate, component->instanceId) ||
            !std::holds_alternative<StoredAssetLocation>(component->location))
            return reject(DomainErrorCode::MissingAsset, "SELECT A LOOSE COMPONENT");
        const auto &definition = content.item(component->definitionId).weaponComponent;
        if (!definition || definition->weaponDefinitionId != weapon->definitionId || definition->slot != command.slot)
            return reject(DomainErrorCode::IncompatibleEquipment, "INCOMPATIBLE COMPONENT");
    }
    plan.before = effectiveWeaponUse(candidate, content, command.weaponAssetId);
    std::vector<AssetInstanceId> removed;
    for (const auto &[id, asset] : candidate.assets.records())
    {
        const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
        if (parent && parent->weaponAssetId == command.weaponAssetId && content.item(asset.definitionId).weaponComponent->slot == command.slot)
            removed.push_back(id);
    }
    if (!command.componentAssetId && removed.empty())
        return reject(DomainErrorCode::MissingAsset, "NO COMPONENT IN SLOT");
    // Remove all outgoing parts from the candidate before allocating any return space.
    std::vector<AssetRecord> outgoing;
    for (auto id : removed) { outgoing.push_back(*candidate.assets.find(id)); static_cast<void>(candidate.assets.erase(id)); }
    if (command.componentAssetId)
        candidate.assets.findMutable(command.componentAssetId)->location = InstalledWeaponComponentLocation{command.weaponAssetId};
    bool rail = false;
    for (const auto &[id, asset] : candidate.assets.records())
    {
        const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
        if (parent && parent->weaponAssetId == command.weaponAssetId)
            rail |= content.item(asset.definitionId).weaponComponent->providesUnderbarrel;
    }
    if (!rail)
    {
        removed.clear();
        for (const auto &[id, asset] : candidate.assets.records())
        {
            const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
            if (parent && parent->weaponAssetId == command.weaponAssetId && content.item(asset.definitionId).weaponComponent->requiresUnderbarrel)
            {
                if (id == command.componentAssetId)
                    return reject(DomainErrorCode::IncompatibleEquipment, "UNDERBARREL RAIL REQUIRED");
                removed.push_back(id);
            }
        }
        for (auto id : removed) { outgoing.push_back(*candidate.assets.find(id)); static_cast<void>(candidate.assets.erase(id)); }
    }
    for (auto &asset : outgoing)
    {
        const auto &definition = content.item(asset.definitionId);
        auto position = findFirstProfileFit(candidate, content, ProfileContainerId::stash(), definition, asset.orientation);
        if (!position && definition.canRotate)
        {
            asset.orientation = asset.orientation == ItemOrientation::Degrees0 ? ItemOrientation::Degrees90 : ItemOrientation::Degrees0;
            position = findFirstProfileFit(candidate, content, ProfileContainerId::stash(), definition, asset.orientation);
        }
        if (!position) return reject(DomainErrorCode::Capacity, "STASH SPACE REQUIRED FOR ALL REMOVED COMPONENTS");
        const StoredAssetLocation destination{ProfileContainerId::stash(), *position};
        asset.location = destination;
        plan.returned.push_back({asset.instanceId, destination});
        if (!candidate.assets.insertLoaded(std::move(asset)))
            return reject(DomainErrorCode::InvalidProfile, "COMPONENT ID CONFLICT");
    }
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid) { plan.error = DomainErrorCode::InvalidProfile; plan.message = validation.message; return plan; }
    plan.after = effectiveWeaponUse(candidate, content, command.weaponAssetId);
    plan.canCommit = true;
    return plan;
}
}

WeaponComponentPlan queryWeaponComponentChange(const ProfileState &profile, const ContentRegistry &content, const WeaponComponentCommand &command)
{
    auto candidate = profile;
    return prepare(candidate, content, command);
}

InventoryReceipt executeWeaponComponentChange(ProfileState &profile, const ContentRegistry &content, const WeaponComponentCommand &command, const CommandContext &context)
{
    if (context.transactionId.empty()) return {false, false, DomainErrorCode::InvalidTransaction, "transaction ID must not be empty", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId)) return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision) return {false, false, DomainErrorCode::StaleRevision, "profile revision is stale", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max()) return {false, false, DomainErrorCode::RevisionOverflow, "profile revision cannot advance", profile.revision};
    auto candidate = profile;
    auto plan = prepare(candidate, content, command);
    if (!plan.canCommit) return {false, false, plan.error, plan.message, profile.revision};
    ++candidate.revision;
    candidate.committedTransactions.insert(context.transactionId);
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

std::uint64_t installedWeaponComponentRecycleValue(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId weapon)
{
    std::uint64_t value{};
    for (const auto &[id, asset] : profile.assets.records())
        if (const auto *parent = std::get_if<InstalledWeaponComponentLocation>(&asset.location);
            parent && parent->weaponAssetId == weapon)
            value += content.item(asset.definitionId).marketRecyclePrice;
    return value;
}
