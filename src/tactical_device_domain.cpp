#include "tactical_device_domain.h"
#include "recovery_task_domain.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
InventoryReceipt failure(const ProfileState &p, DomainErrorCode code, const char *message)
{ return {false, false, code, message, p.revision}; }

std::optional<StoredAssetLocation> returnLocation(const ProfileState &p, const ContentRegistry &c,
                                                const AssetRecord &battery, bool raid,
                                                std::optional<StoredAssetLocation> preferred = {})
{
    const auto &d = c.item(battery.definitionId);
    if (preferred && profilePlacementFits(p, c, preferred->container, preferred->origin, d, battery.orientation, battery.instanceId)) return preferred;
    if (!raid)
    {
        const auto fit = findFirstProfileFit(p, c, ProfileContainerId::stash(), d, battery.orientation, battery.instanceId);
        if (fit) return StoredAssetLocation{ProfileContainerId::stash(), *fit};
        return {};
    }
    // Backpack before chest rig, then other legal carried compartments.
    for (const auto slot : {EquipmentSlotKind::Backpack, EquipmentSlotKind::ChestRig})
    {
        const auto owner = equippedAsset(p, slot);
        const auto *asset = owner ? p.assets.find(*owner) : nullptr;
        if (!asset) continue;
        const auto &compartments = c.item(asset->definitionId).containerCompartments;
        for (std::uint32_t index = 0; index < compartments.size(); ++index)
        {
            const auto container = ProfileContainerId::compartment(*owner, index);
            const auto fit = findFirstProfileFit(p, c, container, d, battery.orientation, battery.instanceId);
            if (fit) return StoredAssetLocation{container, *fit};
        }
    }
    return {};
}

InventoryReceipt prepareBattery(ProfileState &p, const ContentRegistry &c, DeviceBatteryCommand cmd, const DeviceBatteryAccess &access)
{
    if (!validateProfileState(p, c).valid) return failure(p, DomainErrorCode::InvalidProfile, "INVALID DEVICE PROFILE");
    const auto *device = p.assets.find(cmd.deviceAssetId);
    if (!device || !c.item(device->definitionId).weaponLight || !device->deviceState ||
        (access.raid ? (!p.pendingRaid || !assetIsCarried(p, cmd.deviceAssetId)) :
                      (p.pendingRaid || p.activeBaseDefense || !assetIsBaseAccessible(p, cmd.deviceAssetId))))
        return failure(p, DomainErrorCode::IllegalDestination, "SELECT AN ACCESSIBLE LIGHT");
    AssetInstanceId outgoing{};
    for (const auto &[id, a] : p.assets.records())
        if (const auto *loc = std::get_if<InstalledDeviceBatteryLocation>(&a.location); loc && loc->deviceAssetId == cmd.deviceAssetId) outgoing = id;
    std::optional<StoredAssetLocation> preferred;
    if (cmd.batteryAssetId)
    {
        const auto *incoming = p.assets.find(cmd.batteryAssetId);
        const auto *loc = incoming ? std::get_if<StoredAssetLocation>(&incoming->location) : nullptr;
        if (!incoming || !loc || !c.item(incoming->definitionId).battery || incoming->remainingCharges == 0 ||
            (access.raid ? !assetIsCarried(p, cmd.batteryAssetId) : !assetIsBaseAccessible(p, cmd.batteryAssetId)))
            return failure(p, DomainErrorCode::IllegalDestination, "SELECT A STORED BATTERY");
        preferred = *loc;
        if (incoming->quantity > 1)
        {
            const auto definition = incoming->definitionId;
            --p.assets.findMutable(cmd.batteryAssetId)->quantity;
            static_cast<void>(p.assets.create(c.item(definition), InstalledDeviceBatteryLocation{cmd.deviceAssetId}, 1));
        }
        else p.assets.findMutable(cmd.batteryAssetId)->location = InstalledDeviceBatteryLocation{cmd.deviceAssetId};
    }
    else if (!outgoing) return failure(p, DomainErrorCode::MissingAsset, "LIGHT HAS NO BATTERY");
    if (outgoing)
    {
        auto *old = p.assets.findMutable(outgoing);
        const auto destination = returnLocation(p, c, *old, access.raid, preferred);
        if (destination) old->location = *destination;
        else if (access.raid && access.overflow && p.pendingRaid && access.overflow->raidId == p.pendingRaid->raidId)
            old->location = *access.overflow;
        else return failure(p, DomainErrorCode::Capacity, "SPACE REQUIRED FOR REMOVED BATTERY");
    }
    auto &state = *p.assets.findMutable(cmd.deviceAssetId)->deviceState;
    // Replacing a cell is an explicit reseat, independent of switch intent.
    state.interrupted = false; state.flickerRemainingMs = 0; state.lowOutputElapsedMs = 0; state.outputRemainderMs = 0;
    if (!validateProfileState(p, c).valid) return failure(p, DomainErrorCode::InvalidProfile, "INVALID DEVICE RESULT");
    return {true, false, DomainErrorCode::None, {}, p.revision};
}

template<class Operation>
InventoryReceipt transaction(ProfileState &p, const ContentRegistry &c, const CommandContext &context, Operation op)
{
    if (context.transactionId.empty()) return failure(p, DomainErrorCode::InvalidTransaction, "transaction ID must not be empty");
    if (p.committedTransactions.contains(context.transactionId)) return {true, true, DomainErrorCode::None, {}, p.revision};
    if (p.revision != context.expectedRevision) return failure(p, DomainErrorCode::StaleRevision, "profile revision is stale");
    if (p.revision == std::numeric_limits<ProfileRevision>::max()) return failure(p, DomainErrorCode::RevisionOverflow, "profile revision cannot advance");
    auto candidate = p;
    auto receipt = op(candidate);
    if (!receipt.succeeded) return receipt;
    if (!validateProfileState(candidate, c).valid) return failure(p, DomainErrorCode::InvalidProfile, "INVALID DEVICE RESULT");
    ++candidate.revision;
    candidate.committedTransactions.insert(context.transactionId);
    p = std::move(candidate); receipt.revision = p.revision;
    return receipt;
}
}

ProfileValidationResult validateTacticalDevices(const ProfileState &p, const ContentRegistry &c)
{
    std::set<AssetInstanceId> batteryOwners;
    try
    {
        for (const auto &[id, asset] : p.assets.records())
        {
            const auto &d = c.item(asset.definitionId);
            if (d.battery && (asset.remainingCharges == 0 || asset.remainingCharges > d.maximumCharges ||
                asset.quantity == 0 || asset.quantity > d.maxStackSize || (asset.quantity != 1 && asset.remainingCharges != d.maximumCharges)))
                return {false, "invalid battery charge or stack"};
            if (bool(asset.deviceState) != bool(d.weaponLight)) return {false, "device state does not match capability"};
            if (asset.deviceState)
            {
                const auto &s = *asset.deviceState;
                if (asset.quantity != 1 || !std::isfinite(s.outputRemainderMs) || s.outputRemainderMs < 0 || s.outputRemainderMs >= 1 ||
                    s.lowOutputElapsedMs > d.weaponLight->lowOutputIntervalMs || s.flickerRemainingMs > d.weaponLight->flickerDurationMs ||
                    (s.interrupted && s.flickerRemainingMs != 0)) return {false, "invalid device power state"};
            }
            if (const auto *b = std::get_if<InstalledDeviceBatteryLocation>(&asset.location))
            {
                const auto *device = p.assets.find(b->deviceAssetId);
                if (!d.battery || asset.quantity != 1 || !device || id == b->deviceAssetId ||
                    !c.item(device->definitionId).weaponLight || !device->deviceState || !batteryOwners.insert(b->deviceAssetId).second)
                    return {false, "invalid installed battery ownership"};
                if (p.recoveryTask && recoveryTaskForAsset(p, id) &&
                    p.recoveryTask->recoveredAssetIds.contains(id) != p.recoveryTask->recoveredAssetIds.contains(device->instanceId))
                    return {false, "battery recovery differs from device"};
            }
            if (const auto *drop = std::get_if<RaidDroppedBatteryLocation>(&asset.location))
            {
                if (!d.battery || asset.quantity != 1 || !p.pendingRaid || drop->raidId != p.pendingRaid->raidId ||
                    !std::isfinite(drop->position.x) || !std::isfinite(drop->position.y)) return {false, "invalid dropped battery"};
                Vec2 size = c.map(p.pendingRaid->mapDefinitionId).worldSize;
                if (drop->spaceId != outdoorRaidSpaceId())
                {
                    const auto &map = c.map(p.pendingRaid->mapDefinitionId);
                    const auto found = std::find_if(map.interiors.begin(), map.interiors.end(), [&](const auto &v) { return v.id == drop->spaceId; });
                    if (found == map.interiors.end()) return {false, "unknown battery drop space"};
                    size = found->worldSize;
                }
                if (drop->position.x < 0 || drop->position.y < 0 || drop->position.x > size.x || drop->position.y > size.y)
                    return {false, "battery drop outside space"};
            }
        }
    }
    catch (...) { return {false, "unknown device definition"}; }
    return {true, {}};
}

WeaponLightBinding findWeaponLight(const ProfileState &p, const ContentRegistry &c, AssetInstanceId weapon)
{
    WeaponLightBinding binding{weapon};
    for (const auto &[id, a] : p.assets.records())
        if (const auto *part = std::get_if<InstalledWeaponComponentLocation>(&a.location); part && part->weaponAssetId == weapon && c.item(a.definitionId).weaponLight)
            binding.deviceAssetId = id;
    for (const auto &[id, a] : p.assets.records())
        if (const auto *battery = std::get_if<InstalledDeviceBatteryLocation>(&a.location); battery && battery->deviceAssetId == binding.deviceAssetId)
            binding.batteryAssetId = id;
    return binding;
}

WeaponLightProjection queryWeaponLight(const ProfileState &p, const ContentRegistry &c, WeaponLightBinding binding, bool allowOutput)
{
    WeaponLightProjection result; result.binding = binding;
    const auto *device = p.assets.find(binding.deviceAssetId);
    const auto *part = device ? std::get_if<InstalledWeaponComponentLocation>(&device->location) : nullptr;
    if (!device || !device->deviceState || !part || part->weaponAssetId != binding.weaponAssetId || !c.item(device->definitionId).weaponLight) return result;
    const auto &state = *device->deviceState;
    result.light = *c.item(device->definitionId).weaponLight;
    result.switchOn = state.switchOn; result.interrupted = state.interrupted; result.flickering = state.flickerRemainingMs != 0;
    const auto *battery = p.assets.find(binding.batteryAssetId);
    const auto *loc = battery ? std::get_if<InstalledDeviceBatteryLocation>(&battery->location) : nullptr;
    if (!battery || !loc || loc->deviceAssetId != binding.deviceAssetId || !c.item(battery->definitionId).battery) return result;
    result.remainingMs = battery->remainingCharges; result.capacityMs = c.item(battery->definitionId).maximumCharges;
    result.output = allowOutput && state.switchOn && !state.interrupted && !result.flickering && result.remainingMs > 0;
    return result;
}

InventoryPlan queryDeviceBatteryChange(const ProfileState &p, const ContentRegistry &c, DeviceBatteryCommand cmd, const DeviceBatteryAccess &access)
{
    auto candidate = p; const auto r = prepareBattery(candidate, c, cmd, access);
    return {r.succeeded, r.error, r.message, p.revision};
}
InventoryReceipt executeDeviceBatteryChange(ProfileState &p, const ContentRegistry &c, DeviceBatteryCommand cmd, const DeviceBatteryAccess &access, const CommandContext &context)
{ return transaction(p, c, context, [&](auto &candidate) { return prepareBattery(candidate, c, cmd, access); }); }
InventoryReceipt executeDeviceSwitch(ProfileState &p, const ContentRegistry &c, AssetInstanceId id, const CommandContext &context)
{
    return transaction(p, c, context, [&](auto &candidate) {
        auto *asset = candidate.assets.findMutable(id);
        if (!asset || !asset->deviceState || !c.item(asset->definitionId).weaponLight ||
            (candidate.pendingRaid ? !assetIsCarried(candidate, id) : !assetIsBaseAccessible(candidate, id)))
            return failure(candidate, DomainErrorCode::IllegalDestination, "SELECT AN ACCESSIBLE LIGHT");
        asset->deviceState->switchOn = !asset->deviceState->switchOn;
        return InventoryReceipt{true, false, DomainErrorCode::None, {}, candidate.revision};
    });
}

void advanceWeaponLight(ProfileState &p, const ContentRegistry &c, WeaponLightBinding binding, bool allowOutput, float seconds)
{
    if (!allowOutput || !std::isfinite(seconds) || seconds <= 0) return;
    const auto projection = queryWeaponLight(p, c, binding, true);
    auto *device = p.assets.findMutable(binding.deviceAssetId);
    auto *battery = p.assets.findMutable(binding.batteryAssetId);
    if (!device || !device->deviceState || !battery || !projection.switchOn || projection.interrupted || !projection.capacityMs) return;
    auto &s = *device->deviceState;
    const double available = std::min(3600000.0, static_cast<double>(seconds) * 1000.0) + s.outputRemainderMs;
    auto elapsed = static_cast<std::uint32_t>(available);
    s.outputRemainderMs = available - elapsed;
    if (s.flickerRemainingMs)
    {
        if (elapsed >= s.flickerRemainingMs) { s.flickerRemainingMs = 0; s.interrupted = true; }
        else s.flickerRemainingMs -= elapsed;
        return;
    }
    const auto lowThreshold = static_cast<std::uint64_t>(projection.capacityMs) * projection.light.lowBatteryPercent / 100;
    // Account for crossing the threshold during a long simulation frame.
    const auto normal = battery->remainingCharges > lowThreshold ? battery->remainingCharges - static_cast<std::uint32_t>(lowThreshold) : 0U;
    const auto normalConsumed = std::min(elapsed, normal);
    battery->remainingCharges -= normalConsumed; elapsed -= normalConsumed;
    if (elapsed && battery->remainingCharges)
    {
        const auto untilFault = projection.light.lowOutputIntervalMs - s.lowOutputElapsedMs;
        const auto consumed = std::min({elapsed, battery->remainingCharges, untilFault});
        battery->remainingCharges -= consumed; s.lowOutputElapsedMs += consumed;
        if (battery->remainingCharges && s.lowOutputElapsedMs == projection.light.lowOutputIntervalMs)
        {
            s.flickerRemainingMs = projection.light.flickerDurationMs;
            if (!s.flickerRemainingMs || elapsed - consumed >= s.flickerRemainingMs) { s.flickerRemainingMs = 0; s.interrupted = true; }
            else s.flickerRemainingMs -= elapsed - consumed;
        }
    }
    if (battery->remainingCharges == 0)
    {
        static_cast<void>(p.assets.erase(binding.batteryAssetId));
        s.interrupted = false; s.flickerRemainingMs = 0; s.lowOutputElapsedMs = 0; s.outputRemainderMs = 0;
    }
}
bool restoreWeaponLight(ProfileState &p, const ContentRegistry &c, WeaponLightBinding binding)
{
    const auto projection = queryWeaponLight(p, c, binding, true);
    if (!projection.switchOn || !projection.interrupted || !projection.remainingMs) return false;
    auto &s = *p.assets.findMutable(binding.deviceAssetId)->deviceState;
    s.interrupted = false; s.lowOutputElapsedMs = 0; s.flickerRemainingMs = 0;
    return true;
}

InventoryReceipt pickupDroppedBattery(ProfileState &p, const ContentRegistry &c, AssetInstanceId id, const CommandContext &context,
                                      const RaidVisibilityProjection *observation, const RaidObservationKey *key)
{
    return transaction(p, c, context, [&](auto &candidate) {
        auto *asset = candidate.assets.findMutable(id);
        const auto *drop = asset ? std::get_if<RaidDroppedBatteryLocation>(&asset->location) : nullptr;
        if (!candidate.pendingRaid || !drop || drop->raidId != candidate.pendingRaid->raidId ||
            !key || !observation || !observation->matches(*key) || drop->spaceId != key->spaceId ||
            !observation->boundsVisible({{drop->position.x-12,drop->position.y-12},{24,24}})) return failure(candidate, DomainErrorCode::IllegalDestination, "BATTERY IS NOT OBSERVABLE");
        const auto dx = drop->position.x - key->observer.x, dy = drop->position.y - key->observer.y;
        if (dx * dx + dy * dy > 72 * 72) return failure(candidate, DomainErrorCode::IllegalDestination, "BATTERY IS TOO FAR AWAY");
        const auto location = returnLocation(candidate, c, *asset, true);
        if (!location) return failure(candidate, DomainErrorCode::Capacity, "SPACE REQUIRED FOR BATTERY");
        asset->location = *location;
        return InventoryReceipt{true, false, DomainErrorCode::None, {}, candidate.revision};
    });
}
