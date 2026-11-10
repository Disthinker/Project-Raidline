#include "tactical_device_domain.h"
#include "recovery_task_domain.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr std::uint32_t kNightVisionLowOutputIntervalMs = 25000;
constexpr std::uint32_t kNightVisionFlickerDurationMs = 150;
const ItemDefinitionId kUniversalBatteryId{"item.battery.universal"};

bool poweredDevice(const ItemDefinition &definition) noexcept
{
    return definition.weaponLight || definition.nightVision;
}

double nightVisionChargePerMs(const NightVisionOutputDefinition &vision, std::uint32_t capacity)
{
    return static_cast<double>(capacity) / (1000.0 * vision.fullOutputSeconds);
}

std::uint32_t nightVisionChargeTicks(std::uint32_t ms, double chargePerMs)
{
    return static_cast<std::uint32_t>(std::ceil(ms * chargePerMs));
}

InventoryReceipt failure(const ProfileState &p, DomainErrorCode code, const char *message)
{ return {false, false, code, message, p.revision}; }

std::optional<StoredAssetLocation> returnLocation(const ProfileState &p, const ContentRegistry &c,
                                                const AssetRecord &asset, bool carriedOnly,
                                                std::optional<StoredAssetLocation> preferred = {})
{
    const auto &d = c.item(asset.definitionId);
    if (preferred && preferred->container.kind != ProfileContainerKind::LegacyOverflow &&
        (!carriedOnly || (preferred->container.kind == ProfileContainerKind::AssetCompartment &&
                         assetIsCarried(p, preferred->container.ownerAssetId))) &&
        !assetIsLegacyOverflowAsset(p, preferred->container.ownerAssetId) &&
        profilePlacementFits(p, c, preferred->container, preferred->origin, d, asset.orientation, asset.instanceId)) return preferred;
    if (!carriedOnly)
    {
        const auto fit = findFirstProfileFit(p, c, ProfileContainerId::stash(), d, asset.orientation, asset.instanceId);
        if (fit) return StoredAssetLocation{ProfileContainerId::stash(), *fit};
        return {};
    }
    // Backpack before chest rig, then other legal carried compartments.
    for (const auto slot : {EquipmentSlotKind::Backpack, EquipmentSlotKind::ChestRig})
    {
        const auto owner = equippedAsset(p, slot);
        const auto *containerOwner = owner ? p.assets.find(*owner) : nullptr;
        if (!containerOwner) continue;
        const auto &compartments = c.item(containerOwner->definitionId).containerCompartments;
        for (std::uint32_t index = 0; index < compartments.size(); ++index)
        {
            const auto container = ProfileContainerId::compartment(*owner, index);
            const auto fit = findFirstProfileFit(p, c, container, d, asset.orientation, asset.instanceId);
            if (fit) return StoredAssetLocation{container, *fit};
        }
    }
    return {};
}

InventoryReceipt prepareHelmetDevice(ProfileState &profile, const ContentRegistry &content, HelmetDeviceCommand command)
{
    const auto *helmet = profile.assets.find(command.helmetAssetId);
    if (profile.pendingRaid || profile.activeBaseDefense || !helmet ||
        !assetIsBaseAccessible(profile, command.helmetAssetId) ||
        content.item(helmet->definitionId).equipmentSlot != EquipmentSlotKind::Helmet)
        return failure(profile, DomainErrorCode::IllegalDestination, "SELECT AN ACCESSIBLE HELMET");

    const auto binding = findNightVision(profile, content, command.helmetAssetId);
    std::optional<StoredAssetLocation> preferred;
    if (command.deviceAssetId)
    {
        auto *device = profile.assets.findMutable(command.deviceAssetId);
        const auto *stored = device ? std::get_if<StoredAssetLocation>(&device->location) : nullptr;
        const auto *vision = device && content.item(device->definitionId).nightVision
            ? &*content.item(device->definitionId).nightVision : nullptr;
        if (!stored || !vision || vision->helmetDefinitionId != helmet->definitionId ||
            !assetIsBaseAccessible(profile, command.deviceAssetId))
            return failure(profile, DomainErrorCode::IllegalDestination, "SELECT A COMPATIBLE STORED NIGHT VISION DEVICE");
        preferred = *stored;
        device->location = InstalledHelmetDeviceLocation{command.helmetAssetId};
    }
    else if (!binding.deviceAssetId)
        return failure(profile, DomainErrorCode::MissingAsset, "HELMET HAS NO NIGHT VISION DEVICE");

    if (binding.deviceAssetId)
    {
        auto *previous = profile.assets.findMutable(binding.deviceAssetId);
        const auto destination = returnLocation(profile, content, *previous, false, preferred);
        if (!destination)
            return failure(profile, DomainErrorCode::Capacity, "SPACE REQUIRED FOR REMOVED NIGHT VISION DEVICE");
        previous->location = *destination;
    }
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

InventoryReceipt prepareBattery(ProfileState &p, const ContentRegistry &c, DeviceBatteryCommand cmd, const DeviceBatteryAccess &access)
{
    if (!validateProfileState(p, c).valid) return failure(p, DomainErrorCode::InvalidProfile, "INVALID DEVICE PROFILE");
    bool realRaid=access.raid;
    bool carriedOnly=access.raid;
    if(access.activity)
    {
        if(!weaponActionScopeMatches(p,*access.activity))
            return failure(p,DomainErrorCode::IllegalDestination,"BATTERY ACTIVITY CHANGED");
        realRaid=access.activity->activity==WeaponActionActivity::Raid;
        carriedOnly=access.activity->activity!=WeaponActionActivity::BaseDaily || !access.stashAccessible;
    }
    else if(access.raid ? !p.pendingRaid : (p.pendingRaid || p.activeBaseDefense))
        return failure(p,DomainErrorCode::IllegalDestination,"SELECT AN ACCESSIBLE LIGHT");
    const auto *device = p.assets.find(cmd.deviceAssetId);
    const auto *deviceDefinition = device ? &c.item(device->definitionId) : nullptr;
    const bool nightVision = deviceDefinition && deviceDefinition->nightVision;
    if (!deviceDefinition || !poweredDevice(*deviceDefinition) || !device->deviceState ||
        (carriedOnly ? !assetIsCarried(p, cmd.deviceAssetId) : !assetIsBaseAccessible(p, cmd.deviceAssetId)))
        return failure(p, DomainErrorCode::IllegalDestination,
            nightVision ? "SELECT AN ACCESSIBLE NIGHT VISION DEVICE" : "SELECT AN ACCESSIBLE LIGHT");
    AssetInstanceId outgoing{};
    for (const auto &[id, a] : p.assets.records())
        if (const auto *loc = std::get_if<InstalledDeviceBatteryLocation>(&a.location); loc && loc->deviceAssetId == cmd.deviceAssetId) outgoing = id;
    std::optional<StoredAssetLocation> preferred;
    if (cmd.batteryAssetId)
    {
        const auto *incoming = p.assets.find(cmd.batteryAssetId);
        const auto *loc = incoming ? std::get_if<StoredAssetLocation>(&incoming->location) : nullptr;
        if (!incoming || !loc || !c.item(incoming->definitionId).battery || incoming->remainingCharges == 0 ||
            (nightVision && incoming->definitionId != kUniversalBatteryId) ||
            (carriedOnly ? !assetIsCarried(p, cmd.batteryAssetId) : !assetIsBaseAccessible(p, cmd.batteryAssetId)))
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
    else if (!outgoing) return failure(p, DomainErrorCode::MissingAsset,
        nightVision ? "NIGHT VISION HAS NO BATTERY" : "LIGHT HAS NO BATTERY");
    if (outgoing)
    {
        auto *old = p.assets.findMutable(outgoing);
        const auto destination = returnLocation(p, c, *old, carriedOnly, preferred);
        if (destination) old->location = *destination;
        else if (realRaid && access.overflow && p.pendingRaid && access.overflow->raidId == p.pendingRaid->raidId)
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
    std::set<AssetInstanceId> helmetOwners;
    try
    {
        for (const auto &[id, asset] : p.assets.records())
        {
            const auto &d = c.item(asset.definitionId);
            if (d.battery && (asset.remainingCharges == 0 || asset.remainingCharges > d.maximumCharges ||
                asset.quantity == 0 || asset.quantity > d.maxStackSize || (asset.quantity != 1 && asset.remainingCharges != d.maximumCharges)))
                return {false, "invalid battery charge or stack"};
            if (bool(asset.deviceState) != poweredDevice(d)) return {false, "device state does not match capability"};
            if (asset.deviceState)
            {
                const auto &s = *asset.deviceState;
                const double chargePerMs = d.nightVision
                    ? nightVisionChargePerMs(d.nightVision->output, c.item(kUniversalBatteryId).maximumCharges) : 1.0;
                const auto lowInterval = d.nightVision ? nightVisionChargeTicks(kNightVisionLowOutputIntervalMs, chargePerMs)
                    : d.weaponLight->lowOutputIntervalMs;
                const auto flickerDuration = d.nightVision ? nightVisionChargeTicks(kNightVisionFlickerDurationMs, chargePerMs)
                    : d.weaponLight->flickerDurationMs;
                if (asset.quantity != 1 || !std::isfinite(s.outputRemainderMs) || s.outputRemainderMs < 0 || s.outputRemainderMs >= 1 ||
                    s.lowOutputElapsedMs > lowInterval || s.flickerRemainingMs > flickerDuration ||
                    (s.interrupted && s.flickerRemainingMs != 0)) return {false, "invalid device power state"};
            }
            if (const auto *mount = std::get_if<InstalledHelmetDeviceLocation>(&asset.location))
            {
                const auto *helmet = p.assets.find(mount->helmetAssetId);
                if (!d.nightVision || !helmet || id == mount->helmetAssetId ||
                    helmet->definitionId != d.nightVision->helmetDefinitionId ||
                    !helmetOwners.insert(mount->helmetAssetId).second)
                    return {false, "invalid helmet device ownership"};
                if (p.recoveryTask && recoveryTaskForAsset(p, id) &&
                    p.recoveryTask->recoveredAssetIds.contains(id) != p.recoveryTask->recoveredAssetIds.contains(helmet->instanceId))
                    return {false, "night vision recovery differs from helmet"};
            }
            if (const auto *b = std::get_if<InstalledDeviceBatteryLocation>(&asset.location))
            {
                const auto *device = p.assets.find(b->deviceAssetId);
                if (!d.battery || asset.quantity != 1 || !device || id == b->deviceAssetId ||
                    !poweredDevice(c.item(device->definitionId)) || !device->deviceState ||
                    (c.item(device->definitionId).nightVision && asset.definitionId != kUniversalBatteryId) ||
                    !batteryOwners.insert(b->deviceAssetId).second)
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

NightVisionBinding findNightVision(const ProfileState &profile, const ContentRegistry &content, AssetInstanceId helmet)
{
    NightVisionBinding binding{helmet};
    if (!helmet) return binding;
    for (const auto &[id, asset] : profile.assets.records())
        if (const auto *mount = std::get_if<InstalledHelmetDeviceLocation>(&asset.location);
            mount && mount->helmetAssetId == helmet && content.item(asset.definitionId).nightVision)
        {
            binding.deviceAssetId = id;
            break;
        }
    if (!binding.deviceAssetId) return binding;
    for (const auto &[id, asset] : profile.assets.records())
        if (const auto *battery = std::get_if<InstalledDeviceBatteryLocation>(&asset.location);
            battery && battery->deviceAssetId == binding.deviceAssetId)
        {
            binding.batteryAssetId = id;
            break;
        }
    return binding;
}

namespace
{
bool nightVisionHelmetEquipped(const AssetRecord &helmet)
{
    const auto *equipped = std::get_if<EquippedAssetLocation>(&helmet.location);
    return equipped && equipped->slot == EquipmentSlotKind::Helmet;
}
}

NightVisionProjection queryNightVision(const ProfileState &profile, const ContentRegistry &content,
    NightVisionBinding binding, bool allowOutput)
{
    NightVisionProjection result;
    result.binding = binding;
    if (!binding.deviceAssetId) return result;
    const auto *device = profile.assets.find(binding.deviceAssetId);
    const auto *helmet = profile.assets.find(binding.helmetAssetId);
    const auto *mount = device ? std::get_if<InstalledHelmetDeviceLocation>(&device->location) : nullptr;
    if (!device || !device->deviceState || !helmet || !mount || mount->helmetAssetId != binding.helmetAssetId)
        return result;
    const auto &definition = content.item(device->definitionId).nightVision;
    if (!definition || helmet->definitionId != definition->helmetDefinitionId) return result;
    const auto &state = *device->deviceState;
    result.vision = definition->output;
    result.switchOn = state.switchOn;
    result.interrupted = state.interrupted;
    result.flickering = state.flickerRemainingMs != 0;
    const auto *battery = profile.assets.find(binding.batteryAssetId);
    const auto *location = battery ? std::get_if<InstalledDeviceBatteryLocation>(&battery->location) : nullptr;
    if (!battery || battery->definitionId != kUniversalBatteryId || !location || location->deviceAssetId != binding.deviceAssetId)
        return result;
    result.remainingCharges = battery->remainingCharges;
    result.capacityCharges = content.item(battery->definitionId).maximumCharges;
    result.output = allowOutput && nightVisionHelmetEquipped(*helmet) &&
        result.switchOn && !result.interrupted && !result.flickering && result.remainingCharges > 0;
    return result;
}

InventoryPlan queryHelmetDeviceChange(const ProfileState &profile, const ContentRegistry &content, HelmetDeviceCommand command)
{
    if (!validateProfileState(profile, content).valid)
        return {false, DomainErrorCode::InvalidProfile, "INVALID DEVICE PROFILE", profile.revision};
    auto candidate = profile;
    const auto receipt = prepareHelmetDevice(candidate, content, command);
    return {receipt.succeeded, receipt.error, receipt.message, profile.revision};
}

InventoryReceipt executeHelmetDeviceChange(ProfileState &profile, const ContentRegistry &content,
    HelmetDeviceCommand command, const CommandContext &context)
{
    return transaction(profile, content, context, [&](auto &candidate) {
        if (!validateProfileState(candidate, content).valid)
            return failure(candidate, DomainErrorCode::InvalidProfile, "INVALID DEVICE PROFILE");
        return prepareHelmetDevice(candidate, content, command);
    });
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
        const auto *definition = asset ? &c.item(asset->definitionId) : nullptr;
        if (!definition || !asset->deviceState || !poweredDevice(*definition) ||
            (candidate.pendingRaid ? !assetIsCarried(candidate, id) : !assetIsBaseAccessible(candidate, id)))
            return failure(candidate, DomainErrorCode::IllegalDestination,
                definition && definition->nightVision ? "SELECT AN ACCESSIBLE NIGHT VISION DEVICE" : "SELECT AN ACCESSIBLE LIGHT");
        asset->deviceState->switchOn = !asset->deviceState->switchOn;
        return InventoryReceipt{true, false, DomainErrorCode::None, {}, candidate.revision};
    });
}

namespace
{
void advanceDevicePower(ProfileState &profile, AssetRecord &battery, TacticalDeviceState &state,
    double chargeTicks, std::uint32_t lowThreshold, std::uint32_t lowOutputInterval, std::uint32_t flickerDuration)
{
    // The persisted counters are power ticks. A weapon light uses one per
    // millisecond, while a night vision device drains the same cell faster.
    const double available = chargeTicks + state.outputRemainderMs;
    auto elapsed = static_cast<std::uint32_t>(available);
    state.outputRemainderMs = available - elapsed;
    if (state.flickerRemainingMs)
    {
        if (elapsed >= state.flickerRemainingMs) { state.flickerRemainingMs = 0; state.interrupted = true; }
        else state.flickerRemainingMs -= elapsed;
        return;
    }
    // Account for crossing the threshold during a long simulation frame.
    const auto normal = battery.remainingCharges > lowThreshold ? battery.remainingCharges - lowThreshold : 0U;
    const auto normalConsumed = std::min(elapsed, normal);
    battery.remainingCharges -= normalConsumed; elapsed -= normalConsumed;
    if (elapsed && battery.remainingCharges)
    {
        const auto untilFault = lowOutputInterval - state.lowOutputElapsedMs;
        const auto consumed = std::min({elapsed, battery.remainingCharges, untilFault});
        battery.remainingCharges -= consumed; state.lowOutputElapsedMs += consumed;
        if (battery.remainingCharges && state.lowOutputElapsedMs == lowOutputInterval)
        {
            state.flickerRemainingMs = flickerDuration;
            if (!state.flickerRemainingMs || elapsed - consumed >= state.flickerRemainingMs)
            { state.flickerRemainingMs = 0; state.interrupted = true; }
            else state.flickerRemainingMs -= elapsed - consumed;
        }
    }
    if (battery.remainingCharges == 0)
    {
        static_cast<void>(profile.assets.erase(battery.instanceId));
        state.interrupted = false; state.flickerRemainingMs = 0;
        state.lowOutputElapsedMs = 0; state.outputRemainderMs = 0;
    }
}
}

void advanceWeaponLight(ProfileState &p, const ContentRegistry &c, WeaponLightBinding binding, bool allowOutput, float seconds)
{
    if (!allowOutput || !std::isfinite(seconds) || seconds <= 0) return;
    const auto projection = queryWeaponLight(p, c, binding, true);
    auto *device = p.assets.findMutable(binding.deviceAssetId);
    auto *battery = p.assets.findMutable(binding.batteryAssetId);
    if (!device || !device->deviceState || !battery || !projection.switchOn || projection.interrupted || !projection.capacityMs) return;
    const auto lowThreshold = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(projection.capacityMs) * projection.light.lowBatteryPercent / 100);
    advanceDevicePower(p, *battery, *device->deviceState,
        std::min(3600000.0, static_cast<double>(seconds) * 1000.0), lowThreshold,
        projection.light.lowOutputIntervalMs, projection.light.flickerDurationMs);
}

void advanceNightVision(ProfileState &profile, const ContentRegistry &content, NightVisionBinding binding, bool allowOutput, float seconds)
{
    if (!allowOutput || !std::isfinite(seconds) || seconds <= 0 || !binding.deviceAssetId) return;
    const auto projection = queryNightVision(profile, content, binding, true);
    if (!projection.output && (!projection.flickering || !projection.switchOn)) return;
    const auto *helmet = profile.assets.find(binding.helmetAssetId);
    if (!helmet || !nightVisionHelmetEquipped(*helmet) || !projection.capacityCharges) return;
    auto *device = profile.assets.findMutable(binding.deviceAssetId);
    auto *battery = profile.assets.findMutable(binding.batteryAssetId);
    const auto chargePerMs = nightVisionChargePerMs(projection.vision, projection.capacityCharges);
    const auto lowThreshold = projection.capacityCharges * NightVisionOutputDefinition::lowBatteryPercent / 100U;
    advanceDevicePower(profile, *battery, *device->deviceState,
        std::min(3600000.0, static_cast<double>(seconds) * 1000.0) * chargePerMs, lowThreshold,
        nightVisionChargeTicks(kNightVisionLowOutputIntervalMs, chargePerMs),
        nightVisionChargeTicks(kNightVisionFlickerDurationMs, chargePerMs));
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
