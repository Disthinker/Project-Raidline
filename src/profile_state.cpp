#include "profile_state.h"

#include "base_population_domain.h"
#include "base_morale_domain.h"
#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"

#include <algorithm>
#include <cmath>
#include <bit>
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <type_traits>

#include "alpha_content_ids.h"
#include "base_resource_domain.h"

namespace
{
constexpr InventoryGridSize kStashSize{20, 12};
constexpr InventoryGridSize kBaseIntakeSize{20, 12};

bool validMedicalStatus(const MedicalStatusState &status) noexcept
{
    if (status.painkillerRemainingMs > 180000)
    {
        return false;
    }
    switch (status.bleeding)
    {
    case BleedingSeverity::None:
        return status.lightBleedingRemainingMs == 0 &&
               status.bleedingDamageRemainingMs == 0 &&
               status.painScreamRemainingMs == 0;
    case BleedingSeverity::Light:
        return status.lightBleedingRemainingMs > 0 &&
               status.lightBleedingRemainingMs <= 40000 &&
               status.bleedingDamageRemainingMs > 0 &&
               status.bleedingDamageRemainingMs <= 1000 &&
               status.painScreamRemainingMs > 0 &&
               status.painScreamRemainingMs <= 25000;
    case BleedingSeverity::Heavy:
        return status.lightBleedingRemainingMs == 0 &&
               status.bleedingDamageRemainingMs > 0 &&
               status.bleedingDamageRemainingMs <= 500 &&
               status.painScreamRemainingMs > 0 &&
               status.painScreamRemainingMs <= 25000;
    }
    return false;
}

bool validBasePriorityState(
    const BasePriorityState &state,
    std::uint64_t elapsedWorldMinutes,
    const ContentRegistry &content) noexcept
{
    try
    {
        const auto &definitions = content.basePriorities();
        const std::uint64_t cycleMinutes =
            content.basePriorityCycleMinutes();
        if (!state.definitionId.valid() || definitions.empty() ||
            cycleMinutes == 0U)
        {
            return false;
        }
        const std::uint64_t expectedCycle =
            elapsedWorldMinutes <= kInitialWorldMinute
                ? 0U
                : (elapsedWorldMinutes - kInitialWorldMinute) /
                      cycleMinutes;
        return state.cycleIndex == expectedCycle &&
               state.definitionId == definitions[
                   expectedCycle % definitions.size()].id &&
               content.basePriority(state.definitionId).id ==
                   state.definitionId;
    }
    catch (...)
    {
        return false;
    }
}

bool overlaps(
    GridPosition leftOrigin,
    InventoryFootprint left,
    GridPosition rightOrigin,
    InventoryFootprint right) noexcept
{
    return leftOrigin.x < rightOrigin.x + right.width &&
           leftOrigin.x + left.width > rightOrigin.x &&
           leftOrigin.y < rightOrigin.y + right.height &&
           leftOrigin.y + left.height > rightOrigin.y;
}

bool pointInside(
    GridPosition point,
    GridPosition origin,
    InventoryFootprint footprint) noexcept
{
    return point.x >= origin.x && point.y >= origin.y &&
           point.x < origin.x + footprint.width &&
           point.y < origin.y + footprint.height;
}

void hashBytes(std::uint64_t &hash, std::string_view value) noexcept
{
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
}

template <typename Integer>
void hashInteger(std::uint64_t &hash, Integer value) noexcept
{
    using Unsigned = std::make_unsigned_t<Integer>;
    Unsigned unsignedValue = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index)
    {
        hash ^= static_cast<unsigned char>(unsignedValue & 0xffU);
        hash *= 1099511628211ULL;
        unsignedValue >>= 8U;
    }
}

void hashFloat(std::uint64_t &hash, float value) noexcept
{
    hashInteger(hash, std::bit_cast<std::uint32_t>(value));
}

bool containerAccepts(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &item)
{
    if (container.kind != ProfileContainerKind::AssetCompartment)
    {
        return true;
    }

    const AssetRecord *owner = profile.assets.find(container.ownerAssetId);
    if (owner == nullptr)
    {
        return false;
    }
    const ItemDefinition &ownerDefinition =
        content.item(owner->definitionId);
    if (container.compartmentIndex >=
        ownerDefinition.containerCompartments.size())
    {
        return false;
    }

    const ContainerCompartmentDefinition &compartment =
        ownerDefinition.containerCompartments[container.compartmentIndex];
    return compartment.pocketKind == ContainerPocketKind::General ||
           item.category == ItemCategory::Magazine;
}

bool placementFits(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition origin,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset)
{
    InventoryGridSize size{};
    try
    {
        size = profileContainerSize(profile, content, container);
    }
    catch (...)
    {
        return false;
    }

    if (!containerAccepts(profile, content, container, definition))
    {
        return false;
    }

    const InventoryFootprint footprint =
        inventoryFootprint(definition, orientation);
    if (footprint.width <= 0 || footprint.height <= 0 ||
        origin.x < 0 || origin.y < 0 ||
        origin.x > size.width - footprint.width ||
        origin.y > size.height - footprint.height)
    {
        return false;
    }

    for (const AssetRecord *other : assetsInContainer(profile, container))
    {
        if (ignoredAsset.has_value() &&
            other->instanceId == *ignoredAsset)
        {
            continue;
        }
        const auto *stored = std::get_if<StoredAssetLocation>(&other->location);
        const ItemDefinition &otherDefinition =
            content.item(other->definitionId);
        if (overlaps(
                origin,
                footprint,
                stored->origin,
                inventoryFootprint(otherDefinition, other->orientation)))
        {
            return false;
        }
    }
    return true;
}

void placeNewAsset(
    ProfileState &profile,
    const ContentRegistry &content,
    const ItemDefinitionId &definitionId,
    std::uint32_t quantity = 1)
{
    const ItemDefinition &definition = content.item(definitionId);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    if (!origin.has_value())
    {
        throw std::runtime_error{"new Alpha profile does not fit in Stash"};
    }
    static_cast<void>(profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        quantity));
}
}

ProfileContainerId ProfileContainerId::stash() noexcept
{
    return ProfileContainerId{};
}

ProfileContainerId ProfileContainerId::baseIntake() noexcept
{
    return ProfileContainerId{ProfileContainerKind::BaseIntake, 0, 0};
}

ProfileContainerId ProfileContainerId::compartment(
    AssetInstanceId ownerAssetId,
    std::uint32_t compartmentIndex) noexcept
{
    return ProfileContainerId{
        ProfileContainerKind::AssetCompartment,
        ownerAssetId,
        compartmentIndex};
}

AssetInstanceId AssetRegistry::nextAssetId() const noexcept
{
    return nextAssetId_;
}

void AssetRegistry::setNextAssetIdForLoad(AssetInstanceId nextAssetId)
{
    if (nextAssetId == 0)
    {
        throw std::invalid_argument{"next asset ID must be positive"};
    }
    nextAssetId_ = nextAssetId;
}

AssetInstanceId AssetRegistry::create(
    const ItemDefinition &definition,
    AssetLocation location,
    std::uint32_t quantity,
    std::optional<std::string> reliefBatchId)
{
    if (nextAssetId_ == 0 ||
        nextAssetId_ == std::numeric_limits<AssetInstanceId>::max() ||
        quantity == 0 || quantity > definition.maxStackSize)
    {
        throw std::invalid_argument{"invalid asset creation request"};
    }

    const AssetInstanceId id = nextAssetId_++;
    AssetRecord record{
        id,
        definition.definitionId,
        quantity,
        ItemOrientation::Degrees0,
        definition.maximumCharges,
        definition.armorProtection.has_value()
            ? definition.armorProtection->maximumDurability
            : definition.weaponCondition.has_value()
                ? definition.weaponCondition->maximumDurabilityCenti
                : 0U,
        definition.armorProtection.has_value()
            ? definition.armorProtection->maximumDurability
            : definition.weaponCondition.has_value()
                ? definition.weaponCondition->maximumDurabilityCenti
                : 0U,
        std::move(reliefBatchId),
        {},
        std::nullopt,
        WeaponMalfunctionType::None,
        std::move(location)};
    if (!records_.emplace(id, std::move(record)).second)
    {
        throw std::logic_error{"asset ID allocation collided"};
    }
    return id;
}

bool AssetRegistry::insertLoaded(AssetRecord record)
{
    if (record.instanceId == 0)
    {
        return false;
    }
    return records_.emplace(record.instanceId, std::move(record)).second;
}

bool AssetRegistry::erase(AssetInstanceId instanceId) noexcept
{
    return records_.erase(instanceId) == 1U;
}

const AssetRecord *AssetRegistry::find(AssetInstanceId instanceId) const noexcept
{
    const auto found = records_.find(instanceId);
    return found == records_.end() ? nullptr : &found->second;
}

AssetRecord *AssetRegistry::findMutable(AssetInstanceId instanceId) noexcept
{
    const auto found = records_.find(instanceId);
    return found == records_.end() ? nullptr : &found->second;
}

const std::map<AssetInstanceId, AssetRecord> &
AssetRegistry::records() const noexcept
{
    return records_;
}

ProfileState makeNewAlphaProfile(
    std::string profileId,
    const ContentRegistry &content)
{
    if (profileId.empty())
    {
        throw std::invalid_argument{"profile ID must not be empty"};
    }

    ProfileState profile;
    profile.profileId = std::move(profileId);
    profile.currency = 200;

    placeNewAsset(profile, content, alpha_content::rifle);
    placeNewAsset(profile, content, alpha_content::pistol);
    placeNewAsset(profile, content, alpha_content::chestRig);
    placeNewAsset(profile, content, alpha_content::backpack);
    for (int index = 0; index < 3; ++index)
    {
        placeNewAsset(profile, content, alpha_content::magazine);
    }
    for (int index = 0; index < 2; ++index)
    {
        placeNewAsset(profile, content, alpha_content::pistolMagazine);
    }
    placeNewAsset(profile, content, alpha_content::ammunition, 60);
    placeNewAsset(profile, content, alpha_content::ammunition, 30);
    for (int index = 0; index < 2; ++index)
    {
        placeNewAsset(profile, content, alpha_content::medkit);
    }
    placeNewAsset(profile, content, alpha_content::bandage);
    placeNewAsset(profile, content, alpha_content::tourniquet);
    placeNewAsset(profile, content, alpha_content::painkiller);
    placeNewAsset(profile, content, alpha_content::helmet);
    placeNewAsset(profile, content, alpha_content::bodyArmor);
    placeNewAsset(profile, content, alpha_content::weaponMaintenanceKit);
    placeNewAsset(profile, content, alpha_content::armorMaintenanceKit);

    static_cast<void>(synchronizeBasePriorityThrough(profile, content));
    static_cast<void>(synchronizeBaseCommunityEventThrough(profile, content));

    const ProfileValidationResult validation =
        validateProfileState(profile, content);
    if (!validation.valid)
    {
        throw std::logic_error{validation.message};
    }
    return profile;
}

InventoryGridSize profileContainerSize(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container)
{
    if (container.kind == ProfileContainerKind::Stash)
    {
        return kStashSize;
    }
    if (container.kind == ProfileContainerKind::BaseIntake)
    {
        return kBaseIntakeSize;
    }
    const AssetRecord *owner = profile.assets.find(container.ownerAssetId);
    if (owner == nullptr)
    {
        throw std::out_of_range{"container owner does not exist"};
    }
    const ItemDefinition &definition = content.item(owner->definitionId);
    if (container.compartmentIndex >= definition.containerCompartments.size())
    {
        throw std::out_of_range{"container compartment does not exist"};
    }
    const auto &compartment =
        definition.containerCompartments[container.compartmentIndex];
    return InventoryGridSize{compartment.width, compartment.height};
}

std::vector<const AssetRecord *> assetsInContainer(
    const ProfileState &profile,
    ProfileContainerId container)
{
    std::vector<const AssetRecord *> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(id);
        const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
        if (stored != nullptr && stored->container == container)
        {
            result.push_back(&asset);
        }
    }
    return result;
}

std::optional<AssetInstanceId> equippedAsset(
    const ProfileState &profile,
    EquipmentSlotKind slot) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *equipped = std::get_if<EquippedAssetLocation>(&asset.location);
        if (equipped != nullptr && equipped->slot == slot)
        {
            return id;
        }
    }
    return std::nullopt;
}

std::optional<AssetInstanceId> installedMagazine(
    const ProfileState &profile,
    AssetInstanceId weaponAssetId) noexcept
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        const auto *installed =
            std::get_if<InstalledMagazineLocation>(&asset.location);
        if (installed != nullptr &&
            installed->weaponAssetId == weaponAssetId)
        {
            return id;
        }
    }
    return std::nullopt;
}

bool assetIsCarried(
    const ProfileState &profile,
    AssetInstanceId instanceId) noexcept
{
    std::set<AssetInstanceId> visited;
    AssetInstanceId current = instanceId;
    while (current != 0 && visited.insert(current).second)
    {
        const AssetRecord *asset = profile.assets.find(current);
        if (asset == nullptr)
        {
            return false;
        }
        if (std::holds_alternative<EquippedAssetLocation>(asset->location))
        {
            return true;
        }
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset->location))
        {
            if (stored->container.kind !=
                ProfileContainerKind::AssetCompartment)
            {
                return false;
            }
            current = stored->container.ownerAssetId;
            continue;
        }
        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset->location))
        {
            current = installed->weaponAssetId;
            continue;
        }
        return false;
    }
    return false;
}

std::uint32_t baseFacilityLevel(
    const BaseConstructionState &state,
    BaseFacilityUpgradeTarget target) noexcept
{
    switch (target)
    {
    case BaseFacilityUpgradeTarget::Dormitory:
        return state.dormitoryLevel;
    case BaseFacilityUpgradeTarget::Workshop:
        return state.workshopLevel;
    case BaseFacilityUpgradeTarget::Medical:
        return state.medicalLevel;
    }
    return 0U;
}

bool publishedBaseFacilityLevel(
    const BaseConstructionState &state,
    BaseFacilityUpgradeTarget target,
    const ContentRegistry &content) noexcept
{
    const std::uint32_t level = baseFacilityLevel(state, target);
    if (level == 1U)
    {
        return true;
    }
    return std::any_of(
        content.baseConstructionProjects().begin(),
        content.baseConstructionProjects().end(),
        [target, level](const BaseConstructionProjectDefinition &definition)
        {
            return definition.target == target &&
                definition.targetLevel == level;
        });
}

namespace
{
bool validBaseMoraleSnapshot(
    const BaseMoraleState &morale,
    const BaseCommunityEventState &event,
    std::string_view profileId,
    const WorldClockState &worldClock,
    const ContentRegistry &content) noexcept
{
    try
    {
        const WorldClockProjection clock = projectWorldClock(worldClock);
        const bool validTier =
            morale.tier == BaseMoraleTier::Low ||
            morale.tier == BaseMoraleTier::Stable ||
            morale.tier == BaseMoraleTier::High;
        const bool validTrend =
            morale.trend == BaseMoraleTrend::Falling ||
            morale.trend == BaseMoraleTrend::Steady ||
            morale.trend == BaseMoraleTrend::Rising;
        const BaseResourceBundle &shortfall =
            morale.lastLedger.resourceShortfall;
        const std::uint64_t expectedCycle =
            clock.completedDays / content.baseMorale().eventCycleDays;
        return validTier && validTrend &&
            morale.resolvedDayCount <= clock.completedDays &&
            morale.supportedRecoveryDays <
                content.baseMorale().recoveryDaysFromLow &&
            (morale.tier == BaseMoraleTier::Low ||
             morale.consecutiveLowDays == 0U) &&
            morale.lastLedger.dayIndex <= morale.resolvedDayCount &&
            morale.lastLedger.bedShortfall <= kMaximumOrdinaryResidents &&
            morale.lastLedger.netScore >= -9 &&
            morale.lastLedger.netScore <= 9 &&
            shortfall.food <= kMaximumBaseResource &&
            shortfall.hygiene <= kMaximumBaseResource &&
            shortfall.morale <= kMaximumBaseResource &&
            shortfall.security <= kMaximumBaseResource &&
            event.definitionId.valid() &&
            event.cycleIndex == expectedCycle &&
            event.definitionId == selectBaseCommunityEvent(
                profileId,
                expectedCycle,
                content);
    }
    catch (...)
    {
        return false;
    }
}
}

bool assetIsBaseAccessible(
    const ProfileState &profile,
    AssetInstanceId instanceId) noexcept
{
    std::set<AssetInstanceId> visited;
    AssetInstanceId current = instanceId;
    while (current != 0 && visited.insert(current).second)
    {
        const AssetRecord *asset = profile.assets.find(current);
        if (asset == nullptr)
        {
            return false;
        }
        if (std::holds_alternative<EquippedAssetLocation>(asset->location))
        {
            return true;
        }
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset->location))
        {
            if (stored->container.kind == ProfileContainerKind::Stash ||
                stored->container.kind == ProfileContainerKind::BaseIntake)
            {
                return true;
            }
            if (stored->container.kind !=
                ProfileContainerKind::AssetCompartment)
            {
                return false;
            }
            current = stored->container.ownerAssetId;
            continue;
        }
        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset->location))
        {
            current = installed->weaponAssetId;
            continue;
        }
        return false;
    }
    return false;
}

std::vector<AssetInstanceId> carriedAssetIds(const ProfileState &profile)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        static_cast<void>(asset);
        if (assetIsCarried(profile, id))
        {
            result.push_back(id);
        }
    }
    return result;
}

std::uint64_t carriedWeightGrams(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept
{
    constexpr std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    std::uint64_t total{};
    const auto add = [&total](std::uint64_t value)
    {
        if (value > maximum - total)
        {
            total = maximum;
            return;
        }
        total += value;
    };

    try
    {
        for (AssetInstanceId assetId : carriedAssetIds(profile))
        {
            const AssetRecord *asset = profile.assets.find(assetId);
            if (asset == nullptr)
            {
                return maximum;
            }
            const ItemDefinition &definition =
                content.item(asset->definitionId);
            add(static_cast<std::uint64_t>(definition.unitWeightGrams) *
                asset->quantity);

            for (const MagazineRoundRecord &round : asset->magazineRounds)
            {
                add(content.item(round.definitionId).unitWeightGrams);
            }
            if (asset->chamberedRound.has_value())
            {
                add(content.item(
                        asset->chamberedRound->definitionId)
                        .unitWeightGrams);
            }
        }
    }
    catch (...)
    {
        return maximum;
    }
    return total;
}

std::optional<AssetInstanceId> profileAssetAtCell(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition cell) noexcept
{
    try
    {
        for (const AssetRecord *asset : assetsInContainer(profile, container))
        {
            const auto &location = std::get<StoredAssetLocation>(asset->location);
            if (pointInside(
                    cell,
                    location.origin,
                    inventoryFootprint(
                        content.item(asset->definitionId),
                        asset->orientation)))
            {
                return asset->instanceId;
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<GridPosition> findFirstProfileFit(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset) noexcept
{
    try
    {
        const InventoryGridSize size =
            profileContainerSize(profile, content, container);
        for (int y = 0; y < size.height; ++y)
        {
            for (int x = 0; x < size.width; ++x)
            {
                const GridPosition origin{x, y};
                if (placementFits(
                        profile,
                        content,
                        container,
                        origin,
                        definition,
                        orientation,
                        ignoredAsset))
                {
                    return origin;
                }
            }
        }
    }
    catch (...)
    {
    }
    return std::nullopt;
}

bool profilePlacementFits(
    const ProfileState &profile,
    const ContentRegistry &content,
    ProfileContainerId container,
    GridPosition origin,
    const ItemDefinition &definition,
    ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset) noexcept
{
    try
    {
        return placementFits(
            profile,
            content,
            container,
            origin,
            definition,
            orientation,
            ignoredAsset);
    }
    catch (...)
    {
        return false;
    }
}

ProfileValidationResult validateProfileState(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    if (profile.profileId.empty() || profile.revision == 0 ||
        profile.nextBaseServiceJobId == 0 ||
        profile.assets.nextAssetId() == 0 ||
        profile.currentHealth < 0 || profile.currentHealth > 100 ||
        (profile.currentHealth == 0 && !profile.pendingRaid.has_value()))
    {
        return {false, "profile header is invalid"};
    }
    if (!validMedicalStatus(profile.medicalStatus))
    {
        return {false, "profile medical status is invalid"};
    }
    const auto validIntelligenceArchive = [&content](
        const RaidIntelligenceArchiveState &archive) noexcept
    {
        for (const auto &[mapId, counts] : archive.counts)
        {
            try
            {
                static_cast<void>(content.map(mapId));
            }
            catch (...)
            {
                return false;
            }
            bool ownsAnyIntelligence = false;
            for (std::uint32_t count : counts)
            {
                if (count > 1000000U)
                {
                    return false;
                }
                ownsAnyIntelligence = ownsAnyIntelligence || count > 0U;
            }
            if (!ownsAnyIntelligence)
            {
                return false;
            }
        }
        return true;
    };
    if (!validIntelligenceArchive(profile.raidIntelligence))
    {
        return {false, "Raid intelligence archive is invalid"};
    }
    const BaseResourceBundle &resources = profile.baseResources.pool;
    const BaseResourceBundle &shortfall =
        profile.baseResources.lastShortfall;
    if (resources.food > kMaximumBaseResource ||
        resources.hygiene > kMaximumBaseResource ||
        resources.morale > kMaximumBaseResource ||
        resources.security > kMaximumBaseResource ||
        shortfall.food > kMaximumBaseResource ||
        shortfall.hygiene > kMaximumBaseResource ||
        shortfall.morale > kMaximumBaseResource ||
        shortfall.security > kMaximumBaseResource)
    {
        return {false, "Base resource state is invalid"};
    }
    for (const auto &[definitionId, category] :
         profile.baseSupplyPolicy.assignments)
    {
        const ItemDefinition *definition{};
        try
        {
            definition = &content.item(definitionId);
        }
        catch (...)
        {
            return {false, "Base supply policy item is unknown"};
        }
        if (!definition->baseContribution.has_value())
        {
            return {false, "Base supply policy item has no contribution"};
        }
        const BaseResourceBundle &value = *definition->baseContribution;
        const bool valid =
            (category == BaseSupplyCategory::Food && value.food > 0U) ||
            (category == BaseSupplyCategory::Medical &&
             value.hygiene > 0U) ||
            (category == BaseSupplyCategory::Recreation &&
             value.morale > 0U) ||
            (category == BaseSupplyCategory::Security &&
             value.security > 0U);
        if (!valid)
        {
            return {false, "Base supply policy category is invalid"};
        }
    }
    const WorldClockProjection clock = projectWorldClock(profile.worldClock);
    if (profile.baseResources.resolvedDemandCycleCount > clock.completedDays)
    {
        return {false, "Base demand cycle is ahead of the world clock"};
    }
    if (!validBaseMoraleSnapshot(
            profile.baseMorale,
            profile.baseCommunityEvent,
            profile.profileId,
            profile.worldClock,
            content))
    {
        return {false, "Base morale state is invalid"};
    }
    std::uint64_t professionResidents{};
    std::uint64_t injuredByProfession{};
    for (std::size_t index = 0; index < kBaseResidentProfessionCount; ++index)
    {
        professionResidents +=
            profile.basePopulation.professionResidents[index];
        injuredByProfession +=
            profile.basePopulation.injuredByProfession[index];
        if (profile.basePopulation.injuredByProfession[index] >
            profile.basePopulation.professionResidents[index])
        {
            return {false, "Base profession injury state is invalid"};
        }
    }
    if (profile.basePopulation.ordinaryResidents >
            kMaximumOrdinaryResidents ||
        profile.basePopulation.bedCapacity > kMaximumBedCapacity ||
        profile.basePopulation.injuredResidents >
            profile.basePopulation.ordinaryResidents ||
        professionResidents != profile.basePopulation.ordinaryResidents ||
        injuredByProfession != profile.basePopulation.injuredResidents)
    {
        return {false, "Base population state is invalid"};
    }
    const BaseWorkforceProjection workforce = projectBaseWorkforce(profile);
    BaseProfessionCounts assignedByProfession{};
    for (const auto &worker : {
             profile.baseWorkforce.workshopWorker,
             profile.baseWorkforce.medicalWorker})
    {
        if (worker.has_value() &&
            baseProfessionIndex(*worker) < assignedByProfession.size())
        {
            ++assignedByProfession[baseProfessionIndex(*worker)];
        }
    }
    bool professionAssignmentsValid = true;
    for (std::size_t index = 0; index < assignedByProfession.size(); ++index)
    {
        const std::uint32_t healthy =
            profile.basePopulation.professionResidents[index] -
            profile.basePopulation.injuredByProfession[index];
        professionAssignmentsValid = professionAssignmentsValid &&
            assignedByProfession[index] <= healthy;
    }
    if (workforce.assignedResidents > workforce.healthyResidents ||
        !professionAssignmentsValid ||
        (profile.baseWorkforce.workshopWorker.has_value() &&
         !baseFacilityAcceptsProfession(
             BaseFacilityStaffingKind::Workshop,
             *profile.baseWorkforce.workshopWorker)) ||
        (profile.baseWorkforce.medicalWorker.has_value() &&
         !baseFacilityAcceptsProfession(
             BaseFacilityStaffingKind::Medical,
             *profile.baseWorkforce.medicalWorker)))
    {
        return {false, "Base workforce assignment state is invalid"};
    }
    if (profile.baseConstruction.materialUnits >
        content.maximumBaseConstructionMaterials())
    {
        return {false, "Base construction material state is invalid"};
    }
    if (!publishedBaseFacilityLevel(
            profile.baseConstruction,
            BaseFacilityUpgradeTarget::Dormitory,
            content) ||
        !publishedBaseFacilityLevel(
            profile.baseConstruction,
            BaseFacilityUpgradeTarget::Workshop,
            content) ||
        !publishedBaseFacilityLevel(
            profile.baseConstruction,
            BaseFacilityUpgradeTarget::Medical,
            content))
    {
        return {false, "Base facility level is invalid"};
    }
    if (profile.baseConstruction.activeProject.has_value())
    {
        const ActiveBaseConstructionProject &active =
            *profile.baseConstruction.activeProject;
        const BaseConstructionProjectDefinition *definition{};
        try
        {
            definition = &content.baseConstructionProject(
                active.definitionId);
        }
        catch (...)
        {
            return {false, "Base construction project is unknown"};
        }
        const std::uint32_t maximum =
            content.maximumBaseConstructionMaterials();
        if (baseFacilityLevel(profile.baseConstruction, definition->target) !=
                definition->requiredLevel ||
            active.lockedMaterialUnits != definition->materialCost ||
            active.committedWorkers != definition->workerCount ||
            active.committedWorkers >
                profile.basePopulation.ordinaryResidents -
                    profile.basePopulation.injuredResidents ||
            active.startedWorldMinute >= active.completionWorldMinute ||
            active.completionWorldMinute - active.startedWorldMinute !=
                definition->durationMinutes ||
            active.completionWorldMinute <=
                profile.worldClock.elapsedWorldMinutes ||
            active.lockedMaterialUnits > maximum ||
            profile.baseConstruction.materialUnits >
                maximum - active.lockedMaterialUnits)
        {
            return {false, "Base construction project state is invalid"};
        }
    }
    const std::uint32_t constructionWorkers =
        profile.baseConstruction.activeProject.has_value()
        ? profile.baseConstruction.activeProject->committedWorkers
        : 0U;
    if (constructionWorkers >
            workforce.healthyResidents - workforce.assignedResidents)
    {
        return {false, "Base worker commitments are invalid"};
    }
    if (profile.baseManufacturing.activeOrder.has_value() &&
        !profile.baseManufacturing.activeOrder->outputReady &&
        (!profile.baseWorkforce.workshopWorker.has_value() ||
         profile.baseManufacturing.activeOrder->workerProfession !=
             *profile.baseWorkforce.workshopWorker))
    {
        return {false, "Base manufacturing worker state is invalid"};
    }
    if (profile.residentMedical.activeTreatment.has_value())
    {
        const ActiveResidentTreatment &treatment =
            *profile.residentMedical.activeTreatment;
        const ResidentMedicalDefinition &definition = content.residentMedical();
        if (treatment.jobId == 0U ||
            treatment.jobId >= profile.nextBaseServiceJobId ||
            treatment.startedWorldMinute >= treatment.completionWorldMinute ||
            treatment.startedWorldMinute >
                profile.worldClock.elapsedWorldMinutes ||
            treatment.completionWorldMinute <=
                profile.worldClock.elapsedWorldMinutes ||
            treatment.completionWorldMinute - treatment.startedWorldMinute !=
                applyBaseFacilityTaskDuration(
                    definition.durationMinutes,
                    BaseFacilityStaffingKind::Medical,
                    treatment.workerProfession,
                    profile.baseConstruction.medicalLevel,
                    content.baseWorkforce()) ||
            treatment.consumedContribution <
                definition.requiredContribution ||
            profile.basePopulation.injuredResidents == 0U ||
            baseProfessionIndex(treatment.patientProfession) >=
                kBaseResidentProfessionCount ||
            profile.basePopulation.injuredByProfession[
                baseProfessionIndex(treatment.patientProfession)] == 0U ||
            !profile.baseWorkforce.medicalWorker.has_value() ||
            treatment.workerProfession !=
                *profile.baseWorkforce.medicalWorker ||
            !baseFacilityAcceptsProfession(
                BaseFacilityStaffingKind::Medical,
                treatment.workerProfession))
        {
            return {false, "resident treatment state is invalid"};
        }
    }
    std::set<BaseServiceJobId> activeBaseServiceJobIds;
    const auto claimBaseServiceJob = [&activeBaseServiceJobIds](
        BaseServiceJobId jobId)
    {
        return jobId == 0U ||
            !activeBaseServiceJobIds.insert(jobId).second;
    };
    if ((profile.residentMedical.activeTreatment.has_value() &&
         claimBaseServiceJob(
             profile.residentMedical.activeTreatment->jobId)) ||
        (profile.gunsmithMaintenanceJob.has_value() &&
         claimBaseServiceJob(profile.gunsmithMaintenanceJob->jobId)) ||
        (profile.baseManufacturing.activeOrder.has_value() &&
         claimBaseServiceJob(
             profile.baseManufacturing.activeOrder->jobId)))
    {
        return {false, "Base service job identity is duplicated"};
    }
    for (const RescueDefinitionId &rescue : profile.committedRescues)
    {
        const bool published = std::any_of(
            content.maps().begin(),
            content.maps().end(),
            [&rescue](const MapDefinition &map)
            {
                return map.rescue.has_value() && map.rescue->id == rescue;
            });
        if (!rescue.valid() || !published)
        {
            return {false, "committed rescue ID is invalid or unknown"};
        }
    }
    if (!validBasePriorityState(
            profile.basePriority,
            profile.worldClock.elapsedWorldMinutes,
            content))
    {
        return {false, "Base priority state is invalid"};
    }

    AssetInstanceId maximumId{};
    std::set<EquipmentSlotKind> occupiedSlots;
    std::set<AssetInstanceId> installedWeaponIds;
    std::set<AssetInstanceId> groundAssetIds;
    std::set<AssetInstanceId> serviceAssetIds;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (id == 0 || id != asset.instanceId)
        {
            return {false, "asset ID is invalid"};
        }
        maximumId = std::max(maximumId, id);

        const ItemDefinition *definition{};
        try
        {
            definition = &content.item(asset.definitionId);
        }
        catch (...)
        {
            return {false, "asset definition is unknown"};
        }
        if (asset.quantity == 0 || asset.quantity > definition->maxStackSize ||
            !canUseItemOrientation(*definition, asset.orientation) ||
            asset.remainingCharges > definition->maximumCharges ||
            (definition->maximumCharges == 0 && asset.remainingCharges != 0))
        {
            return {false, "asset value is outside definition limits"};
        }
        if (definition->armorProtection.has_value())
        {
            if (asset.currentMaximumDurability == 0 ||
                asset.currentMaximumDurability >
                    definition->armorProtection->maximumDurability ||
                asset.currentDurability > asset.currentMaximumDurability)
            {
                return {false, "armor durability is outside definition limits"};
            }
        }
        else if (definition->weaponCondition.has_value())
        {
            if (asset.currentMaximumDurability == 0 ||
                asset.currentMaximumDurability >
                    definition->weaponCondition->maximumDurabilityCenti ||
                asset.currentDurability > asset.currentMaximumDurability)
            {
                return {false, "weapon durability is outside definition limits"};
            }
        }
        else if (asset.currentMaximumDurability != 0 ||
                 asset.currentDurability != 0 ||
                 asset.weaponMalfunction != WeaponMalfunctionType::None)
        {
            return {false, "non-durable asset contains weapon state"};
        }
        if (asset.weaponMalfunction != WeaponMalfunctionType::None)
        {
            if (!definition->weaponCondition.has_value() ||
                asset.weaponMalfunction != WeaponMalfunctionType::Stovepipe)
            {
                return {false, "weapon malfunction is outside definition limits"};
            }
        }

        if (definition->category == ItemCategory::Magazine)
        {
            if (definition->magazineCapacity == 0 ||
                asset.magazineRounds.size() > definition->magazineCapacity ||
                !definition->compatibleAmmunitionDefinitionId.has_value())
            {
                return {false, "magazine state is invalid"};
            }
            for (const MagazineRoundRecord &round : asset.magazineRounds)
            {
                if (round.definitionId !=
                        *definition->compatibleAmmunitionDefinitionId ||
                    (round.reliefBatchId.has_value() &&
                     round.reliefBatchId->empty()))
                {
                    return {false, "magazine contains incompatible ammunition"};
                }
            }
        }
        else if (!asset.magazineRounds.empty())
        {
            return {false, "non-magazine asset contains rounds"};
        }

        if (asset.chamberedRound.has_value())
        {
            if (definition->category != ItemCategory::Weapon ||
                !definition->compatibleAmmunitionDefinitionId.has_value() ||
                asset.chamberedRound->definitionId !=
                    *definition->compatibleAmmunitionDefinitionId ||
                (asset.chamberedRound->reliefBatchId.has_value() &&
                 asset.chamberedRound->reliefBatchId->empty()))
            {
                return {false, "weapon chamber state is invalid"};
            }
        }

        if (const auto *equipped =
                std::get_if<EquippedAssetLocation>(&asset.location))
        {
            if (!itemCanEquipInSlot(*definition, equipped->slot) ||
                !occupiedSlots.insert(equipped->slot).second)
            {
                return {false, "equipment slot ownership is invalid"};
            }
            continue;
        }

        if (const auto *installed =
                std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            const AssetRecord *weapon =
                profile.assets.find(installed->weaponAssetId);
            if (definition->category != ItemCategory::Magazine ||
                weapon == nullptr ||
                !installedWeaponIds.insert(installed->weaponAssetId).second)
            {
                return {false, "installed magazine ownership is invalid"};
            }
            const ItemDefinition *weaponDefinition{};
            try
            {
                weaponDefinition = &content.item(weapon->definitionId);
            }
            catch (...)
            {
                return {false, "installed magazine weapon is unknown"};
            }
            if (!weaponDefinition->compatibleMagazineDefinitionId.has_value() ||
                *weaponDefinition->compatibleMagazineDefinitionId !=
                    definition->definitionId)
            {
                return {false, "installed magazine is incompatible"};
            }
            continue;
        }

        if (const auto *ground =
                std::get_if<RaidGroundAssetLocation>(&asset.location))
        {
            if (!profile.pendingRaid.has_value() || ground->raidId.empty() ||
                ground->raidId != profile.pendingRaid->raidId)
            {
                return {false, "Raid ground ownership is invalid"};
            }
            groundAssetIds.insert(id);
            continue;
        }

        if (const auto *service =
                std::get_if<BaseServiceAssetLocation>(&asset.location))
        {
            if (service->jobId == 0)
            {
                return {false, "Base service ownership is invalid"};
            }
            serviceAssetIds.insert(id);
            continue;
        }

        const auto &stored = std::get<StoredAssetLocation>(asset.location);
        if (!placementFits(
                profile,
                content,
                stored.container,
                stored.origin,
                *definition,
                asset.orientation,
                id))
        {
            return {false, "asset placement is invalid"};
        }

        if (stored.container.kind == ProfileContainerKind::AssetCompartment)
        {
            if (stored.container.ownerAssetId == id)
            {
                return {false, "container owns itself"};
            }
            if (!definition->containerCompartments.empty())
            {
                for (const auto &[childId, child] : profile.assets.records())
                {
                    static_cast<void>(childId);
                    const auto *childStored =
                        std::get_if<StoredAssetLocation>(&child.location);
                    if (childStored != nullptr &&
                        childStored->container.kind ==
                            ProfileContainerKind::AssetCompartment &&
                        childStored->container.ownerAssetId == id)
                    {
                        return {false, "non-empty container is nested"};
                    }
                }
            }
        }
    }

    if (profile.assets.nextAssetId() <= maximumId)
    {
        return {false, "asset high-water mark moved backward"};
    }

    std::set<AssetInstanceId> claimedServiceAssetIds;
    if (profile.gunsmithMaintenanceJob.has_value())
    {
        const GunsmithMaintenanceJob &job =
            *profile.gunsmithMaintenanceJob;
        const AssetRecord *weapon = profile.assets.find(job.weaponAssetId);
        const auto *service = weapon != nullptr
            ? std::get_if<BaseServiceAssetLocation>(&weapon->location)
            : nullptr;
        const ItemDefinition *definition{};
        try
        {
            if (weapon != nullptr)
            {
                definition = &content.item(weapon->definitionId);
            }
        }
        catch (...)
        {
        }
        if (job.jobId == 0 || job.weaponAssetId == 0 ||
            job.jobId >= profile.nextBaseServiceJobId ||
            job.completionWorldMinute <= job.startedWorldMinute ||
            job.startedWorldMinute > profile.worldClock.elapsedWorldMinutes ||
            job.paidCurrency == 0 ||
            job.targetFactoryDurabilityCenti == 0 ||
            weapon == nullptr || service == nullptr ||
            service->jobId != job.jobId || definition == nullptr ||
            !definition->weaponCondition.has_value() ||
            job.targetFactoryDurabilityCenti !=
                definition->weaponCondition->maximumDurabilityCenti ||
            job.returnOrigin.x < 0 || job.returnOrigin.y < 0 ||
            !serviceAssetIds.contains(job.weaponAssetId))
        {
            return {false, "gunsmith maintenance job is invalid"};
        }
        claimedServiceAssetIds.insert(job.weaponAssetId);
    }

    if (profile.baseManufacturing.activeOrder.has_value())
    {
        const BaseManufacturingOrder &order =
            *profile.baseManufacturing.activeOrder;
        const BaseManufacturingRecipeDefinition *recipe{};
        try
        {
            recipe = &content.baseManufacturingRecipe(
                order.recipeDefinitionId);
        }
        catch (...)
        {
            return {false, "Base manufacturing recipe is unknown"};
        }
        const AssetRecord *output = profile.assets.find(order.outputAssetId);
        const auto *outputService = output != nullptr
            ? std::get_if<BaseServiceAssetLocation>(&output->location)
            : nullptr;
        const std::uint64_t frozenDuration =
            order.completionWorldMinute - order.startedWorldMinute;
        bool publishedDuration = false;
        for (BaseMoraleTier tier : {
                 BaseMoraleTier::Low,
                 BaseMoraleTier::Stable,
                 BaseMoraleTier::High})
        {
            const std::uint32_t moraleDuration =
                applyBaseMoraleDurationPercent(
                    recipe->durationMinutes,
                    tier,
                    content.baseMorale());
            for (std::uint32_t facilityLevel : {1U, 2U})
            {
                publishedDuration = publishedDuration ||
                    frozenDuration == applyBaseFacilityTaskDuration(
                        moraleDuration,
                        BaseFacilityStaffingKind::Workshop,
                        order.workerProfession,
                        facilityLevel,
                        content.baseWorkforce());
            }
        }
        const bool timingValid =
            order.startedWorldMinute < order.completionWorldMinute &&
            order.startedWorldMinute <=
                profile.worldClock.elapsedWorldMinutes &&
            publishedDuration;
        const bool phaseValid = order.outputReady
            ? order.committedWorkers == 0U &&
                order.inputAssetIds.empty() &&
                order.completionWorldMinute <=
                    profile.worldClock.elapsedWorldMinutes
            : order.committedWorkers == recipe->workerCount &&
                order.inputAssetIds.size() == recipe->inputs.size() &&
                (order.completionWorldMinute >
                     profile.worldClock.elapsedWorldMinutes ||
                 profile.pendingRaid.has_value());
        if (order.jobId == 0U ||
            order.jobId >= profile.nextBaseServiceJobId ||
            !baseFacilityAcceptsProfession(
                BaseFacilityStaffingKind::Workshop,
                order.workerProfession) ||
            !timingValid || !phaseValid || output == nullptr ||
            outputService == nullptr ||
            outputService->jobId != order.jobId ||
            output->definitionId != recipe->outputItemDefinitionId ||
            output->quantity != recipe->outputQuantity ||
            !claimedServiceAssetIds.insert(order.outputAssetId).second)
        {
            return {false, "Base manufacturing order is invalid"};
        }
        std::set<ItemDefinitionId> expectedInputDefinitions;
        for (const BaseManufacturingInputDefinition &input : recipe->inputs)
        {
            expectedInputDefinitions.insert(input.itemDefinitionId);
        }
        for (AssetInstanceId inputId : order.inputAssetIds)
        {
            const AssetRecord *input = profile.assets.find(inputId);
            const auto *inputService = input != nullptr
                ? std::get_if<BaseServiceAssetLocation>(&input->location)
                : nullptr;
            if (input == nullptr || inputService == nullptr ||
                inputService->jobId != order.jobId ||
                input->quantity != 1U ||
                !expectedInputDefinitions.erase(input->definitionId) ||
                !claimedServiceAssetIds.insert(inputId).second)
            {
                return {false, "Base manufacturing input is invalid"};
            }
        }
        if (!order.outputReady && !expectedInputDefinitions.empty())
        {
            return {false, "Base manufacturing inputs are incomplete"};
        }
    }

    if (claimedServiceAssetIds != serviceAssetIds)
    {
        return {false, "Base service asset exists without its typed job"};
    }


    if (profile.pendingRaid.has_value())
    {
        const PendingRaidSnapshot &raid = *profile.pendingRaid;
        const MapDefinition *raidMap{};
        try
        {
            raidMap = &content.map(raid.mapDefinitionId);
        }
        catch (...)
        {
            return {false, "pending Raid map is invalid"};
        }
        const bool advancedLootRules =
            raid.rulesVersion == "raid-control-resource-2" ||
            raid.rulesVersion == "raid-conditional-extraction-3" ||
            raid.rulesVersion == "raid-travel-time-4" ||
            raid.rulesVersion == "base-periodic-priority-5" ||
            raid.rulesVersion == "raid-ordinary-rescue-6" ||
            raid.rulesVersion == "raid-resident-medical-7" ||
            raid.rulesVersion == "base-morale-events-8" ||
            raid.rulesVersion == "base-workforce-facilities-9" ||
            raid.rulesVersion == "regional-map-intelligence-10" ||
            raid.rulesVersion == "procedural-outdoor-layout-11" ||
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13";
        const bool travelRules =
            raid.rulesVersion == "raid-travel-time-4" ||
            raid.rulesVersion == "base-periodic-priority-5" ||
            raid.rulesVersion == "raid-ordinary-rescue-6" ||
            raid.rulesVersion == "raid-resident-medical-7" ||
            raid.rulesVersion == "base-morale-events-8" ||
            raid.rulesVersion == "base-workforce-facilities-9" ||
            raid.rulesVersion == "regional-map-intelligence-10" ||
            raid.rulesVersion == "procedural-outdoor-layout-11" ||
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13";
        const bool spatialLayoutRules =
            raid.rulesVersion == "procedural-outdoor-layout-11" ||
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13";
        const bool interiorRules =
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13";
        const bool specialLocationRules =
            raid.rulesVersion == "raid-special-location-placement-13";
        const std::size_t advancedLootCount = static_cast<std::size_t>(
            std::count_if(raid.loot.begin(),
                          raid.loot.end(),
                          [](const RaidLootSnapshot &loot)
                          { return loot.requiresHighRisk; }));
        const std::size_t outdoorEnemyCount = static_cast<std::size_t>(
            std::count_if(
                raid.enemies.begin(), raid.enemies.end(),
                [](const RaidEnemySnapshot &enemy)
                { return enemy.spaceId == outdoorRaidSpaceId(); }));
        const std::size_t interiorEnemyCount =
            raid.enemies.size() - outdoorEnemyCount;
        const std::size_t outdoorRegularLootCount = static_cast<std::size_t>(
            std::count_if(
                raid.loot.begin(), raid.loot.end(),
                [](const RaidLootSnapshot &loot)
                {
                    return !loot.requiresHighRisk &&
                        loot.spaceId == outdoorRaidSpaceId();
                }));
        const std::size_t interiorLootCount = static_cast<std::size_t>(
            std::count_if(
                raid.loot.begin(), raid.loot.end(),
                [](const RaidLootSnapshot &loot)
                { return loot.spaceId != outdoorRaidSpaceId(); }));
        std::size_t expectedInteriorEnemyCount{};
        std::size_t expectedInteriorLootCount{};
        for (const RaidInteriorDefinition &interior : raidMap->interiors)
        {
            expectedInteriorEnemyCount += interior.enemies.size();
            expectedInteriorLootCount += interior.lootSlots.size();
        }
        if (raid.raidId.empty() || raid.settlementId.empty() ||
            raid.rulesVersion.empty() || raid.mapDefinitionId.value().empty() ||
            raid.spawnExtractionPairId.empty() ||
            raid.enemyDeploymentId.value().empty() || raid.seed == 0 ||
            raid.startingHealth <= 0 || raid.startingHealth > 100 ||
            !validMedicalStatus(raid.startingMedicalStatus) ||
            outdoorEnemyCount < 4 || outdoorEnemyCount > 6 ||
            outdoorRegularLootCount < 6 || outdoorRegularLootCount > 9 ||
            (interiorRules &&
             (raid.interiors.size() != raidMap->interiors.size() ||
              interiorEnemyCount != expectedInteriorEnemyCount ||
              interiorLootCount != expectedInteriorLootCount)) ||
            (!interiorRules &&
             (!raid.interiors.empty() || interiorEnemyCount != 0U ||
              interiorLootCount != 0U)) ||
            (advancedLootRules &&
             advancedLootCount != raidMap->highRisk.advancedLootSlots.size()) ||
            (!advancedLootRules && advancedLootCount != 0U))
        {
            return {false, "pending Raid header is invalid"};
        }
        if (spatialLayoutRules)
        {
            const auto insideWalkable = [raidMap](ContentRect rect) noexcept
            {
                return std::isfinite(rect.position.x) &&
                    std::isfinite(rect.position.y) &&
                    std::isfinite(rect.size.x) &&
                    std::isfinite(rect.size.y) && rect.size.x > 0.0F &&
                    rect.size.y > 0.0F &&
                    rect.position.x >=
                        raidMap->walkableBounds.position.x &&
                    rect.position.y >=
                        raidMap->walkableBounds.position.y &&
                    rect.position.x + rect.size.x <=
                        raidMap->walkableBounds.position.x +
                            raidMap->walkableBounds.size.x &&
                    rect.position.y + rect.size.y <=
                        raidMap->walkableBounds.position.y +
                            raidMap->walkableBounds.size.y;
            };
            const ProceduralOutdoorDefinition &procedural =
                raidMap->proceduralOutdoor;
            const bool generatedCountValid =
                !procedural.enabled || raid.spatialLayout.usedFallback ||
                (raid.spatialLayout.ballisticBlockers.size() >=
                     procedural.minimumBlockers &&
                 raid.spatialLayout.ballisticBlockers.size() <=
                     procedural.maximumBlockers);
            bool fixedLayoutValid = procedural.enabled ||
                raid.spatialLayout.ballisticBlockers.size() ==
                    raidMap->ballisticBlockers.size();
            if (!procedural.enabled && fixedLayoutValid)
            {
                for (std::size_t index{};
                     index < raidMap->ballisticBlockers.size();
                     ++index)
                {
                    fixedLayoutValid = fixedLayoutValid &&
                        raid.spatialLayout.ballisticBlockers[index] ==
                            raidMap->ballisticBlockers[index].bounds;
                }
            }
            RaidMapGenerationAnchors anchors;
            anchors.playerSpawn = raid.playerSpawn;
            anchors.extractionPoint = raid.extractionPoint;
            anchors.occupiedRegions = {
                raidMap->highRisk.emergencyExtractionPoint,
                raidMap->highRisk.conditionalExtractionPoint,
                raidMap->highRisk.activationControlPoint,
                raidMap->highRisk.advancedResourceArea};
            for (const RaidEnemySnapshot &enemy : raid.enemies)
            {
                if (enemy.spaceId != outdoorRaidSpaceId())
                {
                    continue;
                }
                anchors.occupiedRegions.push_back(
                    ContentRect{enemy.position, enemy.size});
                anchors.reachablePoints.push_back(
                    Vec2{enemy.position.x + enemy.size.x * 0.5F,
                         enemy.position.y + enemy.size.y * 0.5F});
            }
            for (const RaidLootSnapshot &loot : raid.loot)
            {
                if (loot.spaceId != outdoorRaidSpaceId())
                {
                    continue;
                }
                anchors.reachablePoints.push_back(loot.position);
            }
            for (const EnemySpawnDefinition &spawn :
                 raidMap->highRisk.pressureSpawns)
            {
                anchors.occupiedRegions.push_back(
                    ContentRect{spawn.position, spawn.size});
            }
            const auto addRegion = [&anchors](ContentRect region)
            {
                anchors.reachablePoints.push_back(
                    Vec2{region.position.x + region.size.x * 0.5F,
                         region.position.y + region.size.y * 0.5F});
            };
            addRegion(raidMap->highRisk.emergencyExtractionPoint);
            addRegion(raidMap->highRisk.conditionalExtractionPoint);
            addRegion(raidMap->highRisk.activationControlPoint);
            addRegion(raidMap->highRisk.advancedResourceArea);
            if (raid.rescue.has_value())
            {
                anchors.occupiedRegions.push_back(
                    raid.rescue->transferPoint);
                addRegion(raid.rescue->transferPoint);
            }
            for (std::size_t interiorIndex{};
                 interiorIndex < raid.interiors.size(); ++interiorIndex)
            {
                const RaidInteriorSnapshot &interior =
                    raid.interiors[interiorIndex];
                if (specialLocationRules)
                {
                    const RaidInteriorDefinition &definition =
                        raidMap->interiors[interiorIndex];
                    const auto placement = std::find_if(
                        definition.exteriorPlacements.begin(),
                        definition.exteriorPlacements.end(),
                        [&](const RaidExteriorPlacementDefinition &candidate)
                        {
                            return candidate.entrance ==
                                    interior.exteriorEntrance &&
                                candidate.returnPoint.x ==
                                    interior.exteriorReturn.x &&
                                candidate.returnPoint.y ==
                                    interior.exteriorReturn.y;
                        });
                    if (placement == definition.exteriorPlacements.end() ||
                        !raidExteriorPlacementIsLegal(*placement, anchors))
                    {
                        return {false,
                                "pending Raid special location is invalid"};
                    }
                }
                anchors.occupiedRegions.push_back(
                    interior.exteriorEntrance);
                addRegion(interior.exteriorEntrance);
            }
            if (raid.spatialLayout.layoutHash == 0U ||
                raid.spatialLayout.layoutHash != raidMapLayoutHash(
                    raid.spatialLayout.ballisticBlockers) ||
                !generatedCountValid || !fixedLayoutValid ||
                (procedural.enabled &&
                 (raid.spatialLayout.generationAttempt == 0U ||
                  raid.spatialLayout.generationAttempt >
                      procedural.maximumAttempts)) ||
                (!procedural.enabled &&
                 (raid.spatialLayout.generationAttempt != 0U ||
                  raid.spatialLayout.usedFallback)) ||
                std::any_of(
                    raid.spatialLayout.ballisticBlockers.begin(),
                    raid.spatialLayout.ballisticBlockers.end(),
                    [&insideWalkable](ContentRect blocker)
                    { return !insideWalkable(blocker); }) ||
                (procedural.enabled &&
                 !raidMapLayoutConnectsAnchors(
                     *raidMap, raid.spatialLayout, anchors)))
            {
                return {false, "pending Raid spatial layout is invalid"};
            }
        }
        if (interiorRules)
        {
            const auto validRectInSpace = [](ContentRect rect,
                                             Vec2 worldSize) noexcept
            {
                return std::isfinite(rect.position.x) &&
                    std::isfinite(rect.position.y) &&
                    std::isfinite(rect.size.x) &&
                    std::isfinite(rect.size.y) && rect.size.x > 0.0F &&
                    rect.size.y > 0.0F && rect.position.x >= 0.0F &&
                    rect.position.y >= 0.0F &&
                    rect.position.x + rect.size.x <= worldSize.x &&
                    rect.position.y + rect.size.y <= worldSize.y;
            };
            for (std::size_t index{}; index < raid.interiors.size(); ++index)
            {
                const RaidInteriorSnapshot &snapshot = raid.interiors[index];
                const RaidInteriorDefinition &definition =
                    raidMap->interiors[index];
                const bool blockersMatch =
                    snapshot.ballisticBlockers.size() ==
                        definition.ballisticBlockers.size() &&
                    std::equal(
                        snapshot.ballisticBlockers.begin(),
                        snapshot.ballisticBlockers.end(),
                        definition.ballisticBlockers.begin(),
                        [](ContentRect bounds,
                           const BallisticBlockerDefinition &blocker)
                        { return bounds == blocker.bounds; });
                const bool portalMatches = specialLocationRules
                    ? std::any_of(
                          definition.exteriorPlacements.begin(),
                          definition.exteriorPlacements.end(),
                          [&](const RaidExteriorPlacementDefinition &placement)
                          {
                              return placement.entrance ==
                                      snapshot.exteriorEntrance &&
                                  placement.returnPoint.x ==
                                      snapshot.exteriorReturn.x &&
                                  placement.returnPoint.y ==
                                      snapshot.exteriorReturn.y;
                          })
                    : snapshot.exteriorEntrance ==
                              definition.exteriorEntrance &&
                          snapshot.exteriorReturn.x ==
                              definition.exteriorReturn.x &&
                          snapshot.exteriorReturn.y ==
                              definition.exteriorReturn.y;
                if (snapshot.id != definition.id ||
                    snapshot.displayName != definition.displayName ||
                    snapshot.worldSize.x != definition.worldSize.x ||
                    snapshot.worldSize.y != definition.worldSize.y ||
                    !portalMatches ||
                    snapshot.interiorSpawn.x != definition.interiorSpawn.x ||
                    snapshot.interiorSpawn.y != definition.interiorSpawn.y ||
                    snapshot.interiorExit != definition.interiorExit ||
                    !std::isfinite(snapshot.worldSize.x) ||
                    !std::isfinite(snapshot.worldSize.y) ||
                    snapshot.worldSize.x <= 0.0F ||
                    snapshot.worldSize.y <= 0.0F ||
                    !validRectInSpace(
                        snapshot.interiorExit, snapshot.worldSize) ||
                    !blockersMatch ||
                    std::any_of(
                        snapshot.ballisticBlockers.begin(),
                        snapshot.ballisticBlockers.end(),
                        [&](ContentRect blocker)
                        { return !validRectInSpace(blocker, snapshot.worldSize); }))
                {
                    return {false, "pending Raid interior snapshot is invalid"};
                }
            }
        }
        if (travelRules)
        {
            const BaseResourceBundle &startingPool =
                raid.travel.startingBaseResources.pool;
            const BaseResourceBundle &startingShortfall =
                raid.travel.startingBaseResources.lastShortfall;
            const std::uint64_t startingCompletedDays =
                projectWorldClock(
                    raid.travel.startingWorldClock).completedDays;
            const bool outboundWouldOverflow =
                raid.travel.startingWorldClock.elapsedWorldMinutes >
                    std::numeric_limits<std::uint64_t>::max() -
                        raid.travel.outboundMinutes;
            bool startingConstructionValid =
                raid.travel.startingBaseConstruction.materialUnits <=
                    content.maximumBaseConstructionMaterials() &&
                raid.travel.startingBedCapacity <= kMaximumBedCapacity &&
                raid.travel.startingInjuredResidents <=
                    profile.basePopulation.ordinaryResidents;
            startingConstructionValid = startingConstructionValid &&
                publishedBaseFacilityLevel(
                    raid.travel.startingBaseConstruction,
                    BaseFacilityUpgradeTarget::Dormitory,
                    content) &&
                publishedBaseFacilityLevel(
                    raid.travel.startingBaseConstruction,
                    BaseFacilityUpgradeTarget::Workshop,
                    content) &&
                publishedBaseFacilityLevel(
                    raid.travel.startingBaseConstruction,
                    BaseFacilityUpgradeTarget::Medical,
                    content);
            if (raid.travel.startingBaseConstruction.activeProject
                    .has_value())
            {
                const ActiveBaseConstructionProject &active =
                    *raid.travel.startingBaseConstruction.activeProject;
                try
                {
                    const BaseConstructionProjectDefinition &definition =
                        content.baseConstructionProject(active.definitionId);
                    const std::uint32_t maximum =
                        content.maximumBaseConstructionMaterials();
                    startingConstructionValid = startingConstructionValid &&
                        baseFacilityLevel(
                            raid.travel.startingBaseConstruction,
                            definition.target) == definition.requiredLevel &&
                        active.lockedMaterialUnits == definition.materialCost &&
                        active.committedWorkers == definition.workerCount &&
                        active.committedWorkers <=
                            profile.basePopulation.ordinaryResidents -
                                raid.travel.startingInjuredResidents &&
                        active.startedWorldMinute <
                            active.completionWorldMinute &&
                        active.completionWorldMinute -
                                active.startedWorldMinute ==
                            definition.durationMinutes &&
                        active.completionWorldMinute >
                            raid.travel.startingWorldClock
                                .elapsedWorldMinutes &&
                        active.lockedMaterialUnits <= maximum &&
                        raid.travel.startingBaseConstruction.materialUnits <=
                            maximum - active.lockedMaterialUnits;
                }
                catch (...)
                {
                    startingConstructionValid = false;
                }
            }
            ProfileState startingWorkforceProfile = profile;
            startingWorkforceProfile.baseConstruction =
                raid.travel.startingBaseConstruction;
            startingWorkforceProfile.baseWorkforce =
                raid.travel.startingBaseWorkforce;
            startingWorkforceProfile.basePopulation.injuredResidents =
                raid.travel.startingInjuredResidents;
            startingWorkforceProfile.basePopulation.injuredByProfession =
                raid.travel.startingInjuredByProfession;
            std::uint64_t startingInjuredProfessionTotal{};
            bool startingInjuredProfessionsValid = true;
            for (std::size_t index = 0;
                 index < kBaseResidentProfessionCount;
                 ++index)
            {
                startingInjuredProfessionTotal +=
                    raid.travel.startingInjuredByProfession[index];
                startingInjuredProfessionsValid =
                    startingInjuredProfessionsValid &&
                    raid.travel.startingInjuredByProfession[index] <=
                        profile.basePopulation.professionResidents[index];
            }
            const BaseWorkforceProjection startingWorkforce =
                projectBaseWorkforce(startingWorkforceProfile);
            const bool startingWorkforceValid =
                startingInjuredProfessionsValid &&
                startingInjuredProfessionTotal ==
                    raid.travel.startingInjuredResidents &&
                startingWorkforce.assignedResidents <=
                    startingWorkforce.healthyResidents &&
                (!raid.travel.startingBaseWorkforce.workshopWorker.has_value() ||
                 baseFacilityAcceptsProfession(
                     BaseFacilityStaffingKind::Workshop,
                     *raid.travel.startingBaseWorkforce.workshopWorker)) &&
                (!raid.travel.startingBaseWorkforce.medicalWorker.has_value() ||
                 baseFacilityAcceptsProfession(
                     BaseFacilityStaffingKind::Medical,
                     *raid.travel.startingBaseWorkforce.medicalWorker));
            bool startingResidentMedicalValid = true;
            if (raid.travel.startingResidentMedical.activeTreatment.has_value())
            {
                const ActiveResidentTreatment &treatment =
                    *raid.travel.startingResidentMedical.activeTreatment;
                const ResidentMedicalDefinition &definition =
                    content.residentMedical();
                startingResidentMedicalValid =
                    treatment.jobId > 0U &&
                    treatment.jobId < profile.nextBaseServiceJobId &&
                    treatment.startedWorldMinute <
                        treatment.completionWorldMinute &&
                    treatment.startedWorldMinute <=
                        raid.travel.startingWorldClock.elapsedWorldMinutes &&
                    treatment.completionWorldMinute >
                        raid.travel.startingWorldClock.elapsedWorldMinutes &&
                    treatment.completionWorldMinute -
                            treatment.startedWorldMinute ==
                        applyBaseFacilityTaskDuration(
                            definition.durationMinutes,
                            BaseFacilityStaffingKind::Medical,
                            treatment.workerProfession,
                            raid.travel.startingBaseConstruction.medicalLevel,
                            content.baseWorkforce()) &&
                    treatment.consumedContribution >=
                        definition.requiredContribution &&
                    raid.travel.startingInjuredResidents > 0U;
            }
            if (raid.travel.outboundMinutes == 0U ||
                raid.travel.returnMinutes == 0U ||
                raid.travel.failureRegroupMinutes <
                    raid.travel.returnMinutes ||
                outboundWouldOverflow ||
                profile.worldClock.elapsedWorldMinutes <
                    raid.travel.startingWorldClock.elapsedWorldMinutes +
                        raid.travel.outboundMinutes ||
                startingPool.food > kMaximumBaseResource ||
                startingPool.hygiene > kMaximumBaseResource ||
                startingPool.morale > kMaximumBaseResource ||
                startingPool.security > kMaximumBaseResource ||
                startingShortfall.food > kMaximumBaseResource ||
                startingShortfall.hygiene > kMaximumBaseResource ||
                startingShortfall.morale > kMaximumBaseResource ||
                startingShortfall.security > kMaximumBaseResource ||
                raid.travel.startingBaseResources
                        .resolvedDemandCycleCount > startingCompletedDays ||
                !validBasePriorityState(
                    raid.travel.startingBasePriority,
                    raid.travel.startingWorldClock.elapsedWorldMinutes,
                    content) ||
                !validBaseMoraleSnapshot(
                    raid.travel.startingBaseMorale,
                    raid.travel.startingBaseCommunityEvent,
                    profile.profileId,
                    raid.travel.startingWorldClock,
                    content) ||
                 !startingConstructionValid || !startingWorkforceValid ||
                !startingResidentMedicalValid ||
                !validIntelligenceArchive(
                    raid.travel.startingRaidIntelligence))
            {
                return {false, "pending Raid travel snapshot is invalid"};
            }
        }
        std::set<AssetInstanceId> snapshotLoot;
        std::set<std::uint32_t> snapshotSlots;
        for (const RaidLootSnapshot &loot : raid.loot)
        {
            const AssetRecord *asset = profile.assets.find(loot.assetId);
            const std::size_t regularSlotCount = raidMap->raidLootSlots.size();
            const std::size_t advancedSlotCount =
                raidMap->highRisk.advancedLootSlots.size();
            bool validSlot{};
            bool validPosition{};
            if (loot.spaceId == outdoorRaidSpaceId())
            {
                validSlot = loot.requiresHighRisk
                    ? loot.slotIndex >= regularSlotCount &&
                          loot.slotIndex < regularSlotCount + advancedSlotCount
                    : loot.slotIndex < regularSlotCount;
                if (validSlot)
                {
                    const Vec2 expected = loot.requiresHighRisk
                        ? raidMap->highRisk.advancedLootSlots[
                              loot.slotIndex - regularSlotCount].position
                        : raidMap->raidLootSlots[loot.slotIndex].position;
                    validPosition = loot.position.x == expected.x &&
                        loot.position.y == expected.y;
                }
            }
            else if (!loot.requiresHighRisk)
            {
                std::size_t firstInteriorSlot =
                    regularSlotCount + advancedSlotCount;
                for (const RaidInteriorDefinition &interior :
                     raidMap->interiors)
                {
                    const std::size_t end =
                        firstInteriorSlot + interior.lootSlots.size();
                    if (loot.spaceId == interior.id)
                    {
                        validSlot = loot.slotIndex >= firstInteriorSlot &&
                            loot.slotIndex < end;
                        if (validSlot)
                        {
                            const Vec2 expected = interior.lootSlots[
                                loot.slotIndex - firstInteriorSlot].position;
                            validPosition = loot.position.x == expected.x &&
                                loot.position.y == expected.y;
                        }
                        break;
                    }
                    firstInteriorSlot = end;
                }
            }
            if (loot.assetId == 0 || loot.definitionId.value().empty() ||
                loot.quantity == 0 ||
                !snapshotLoot.insert(loot.assetId).second ||
                !snapshotSlots.insert(loot.slotIndex).second || !validSlot ||
                !validPosition ||
                !std::isfinite(loot.position.x) ||
                !std::isfinite(loot.position.y) ||
                (!loot.collected && asset == nullptr) ||
                (asset != nullptr &&
                 !groundAssetIds.contains(loot.assetId) &&
                 !assetIsCarried(profile, loot.assetId)) ||
                (loot.collected && asset != nullptr &&
                 !groundAssetIds.contains(loot.assetId) &&
                 !assetIsCarried(profile, loot.assetId)) ||
                (asset != nullptr &&
                 asset->definitionId != loot.definitionId))
            {
                return {false, "pending Raid Loot ownership is invalid"};
            }
        }
        for (AssetInstanceId groundAssetId : groundAssetIds)
        {
            if (!snapshotLoot.contains(groundAssetId))
            {
                return {false, "Raid ground asset is absent from snapshot"};
            }
        }
        if (!std::isfinite(raid.playerSpawn.x) ||
            !std::isfinite(raid.playerSpawn.y) ||
            !std::isfinite(raid.extractionPoint.position.x) ||
            !std::isfinite(raid.extractionPoint.position.y) ||
            !std::isfinite(raid.extractionPoint.size.x) ||
            !std::isfinite(raid.extractionPoint.size.y) ||
            raid.extractionPoint.size.x <= 0.0F ||
            raid.extractionPoint.size.y <= 0.0F)
        {
            return {false, "pending Raid geometry is invalid"};
        }
        for (const RaidEnemySnapshot &enemy : raid.enemies)
        {
            Vec2 spaceSize = raidMap->worldSize;
            if (enemy.spaceId != outdoorRaidSpaceId())
            {
                const auto interior = std::find_if(
                    raid.interiors.begin(), raid.interiors.end(),
                    [&](const RaidInteriorSnapshot &candidate)
                    { return candidate.id == enemy.spaceId; });
                if (interior == raid.interiors.end())
                {
                    return {false, "pending Raid enemy space is invalid"};
                }
                spaceSize = interior->worldSize;
            }
            if (!std::isfinite(enemy.position.x) ||
                !std::isfinite(enemy.position.y) ||
                !std::isfinite(enemy.size.x) ||
                !std::isfinite(enemy.size.y) ||
                enemy.size.x <= 0.0F || enemy.size.y <= 0.0F ||
                enemy.maximumHealth <= 0 || enemy.position.x < 0.0F ||
                enemy.position.y < 0.0F ||
                enemy.position.x + enemy.size.x > spaceSize.x ||
                enemy.position.y + enemy.size.y > spaceSize.y)
            {
                return {false, "pending Raid enemy is invalid"};
            }
        }
        const auto enemyMatches = [](const RaidEnemySnapshot &snapshot,
                                     const EnemySpawnDefinition &definition)
        {
            return snapshot.position.x == definition.position.x &&
                snapshot.position.y == definition.position.y &&
                snapshot.size.x == definition.size.x &&
                snapshot.size.y == definition.size.y &&
                snapshot.maximumHealth == definition.maximumHealth;
        };
        const EnemyDeploymentDefinition &deployment =
            content.enemyDeployment(raid.enemyDeploymentId);
        std::vector<RaidEnemySnapshot> outdoorEnemies;
        std::copy_if(
            raid.enemies.begin(), raid.enemies.end(),
            std::back_inserter(outdoorEnemies),
            [](const RaidEnemySnapshot &enemy)
            { return enemy.spaceId == outdoorRaidSpaceId(); });
        if (outdoorEnemies.size() != deployment.enemies.size() ||
            !std::equal(
                outdoorEnemies.begin(), outdoorEnemies.end(),
                deployment.enemies.begin(), deployment.enemies.end(),
                enemyMatches))
        {
            return {false, "pending Raid outdoor enemies do not match deployment"};
        }
        for (const RaidInteriorDefinition &interior : raidMap->interiors)
        {
            std::vector<RaidEnemySnapshot> interiorEnemies;
            std::copy_if(
                raid.enemies.begin(), raid.enemies.end(),
                std::back_inserter(interiorEnemies),
                [&](const RaidEnemySnapshot &enemy)
                { return enemy.spaceId == interior.id; });
            if (interiorEnemies.size() != interior.enemies.size() ||
                !std::equal(
                    interiorEnemies.begin(), interiorEnemies.end(),
                    interior.enemies.begin(), interior.enemies.end(),
                    enemyMatches))
            {
                return {false,
                        "pending Raid interior enemies do not match content"};
            }
        }
        for (AssetInstanceId root : raid.carriedRootAssetIds)
        {
            const AssetRecord *asset = profile.assets.find(root);
            // The deployment snapshot records the roots that entered the
            // Raid, not permanent equipment-slot assignments. During a Raid
            // those roots may be moved between equipment and another carried
            // container, but must never leave the carried ownership tree.
            if (asset == nullptr || !assetIsCarried(profile, root))
            {
                return {false, "pending Raid carried root is invalid"};
            }
        }
        if (raid.rescue.has_value())
        {
            const RaidRescueSnapshot &rescue = *raid.rescue;
            const bool committed =
                profile.committedRescues.contains(rescue.definitionId);
            if (!raidMap->rescue.has_value() ||
                rescue.definitionId != raidMap->rescue->id ||
                rescue.subjectKind != RaidRescueSubjectKind::OrdinaryResidents ||
                rescue.profession != raidMap->rescue->profession ||
                rescue.ordinaryResidentCount == 0U ||
                rescue.ordinaryResidentCount > 16U ||
                rescue.injuredResidentCount >
                    rescue.ordinaryResidentCount ||
                !std::isfinite(rescue.interactionDurationSeconds) ||
                rescue.interactionDurationSeconds <= 0.0F ||
                rescue.interactionDurationSeconds > 30.0F ||
                !std::isfinite(rescue.transferPoint.position.x) ||
                !std::isfinite(rescue.transferPoint.position.y) ||
                !std::isfinite(rescue.transferPoint.size.x) ||
                !std::isfinite(rescue.transferPoint.size.y) ||
                rescue.transferPoint.size.x <= 0.0F ||
                rescue.transferPoint.size.y <= 0.0F ||
                rescue.secured != committed)
            {
                return {false, "pending Raid rescue is invalid"};
            }
        }
    }
    else if (!groundAssetIds.empty())
    {
        return {false, "Raid ground asset exists without pending Raid"};
    }

    if (profile.lastRaidResult.has_value() &&
        (profile.lastRaidResult->settlementId.empty() ||
         profile.lastRaidResult->rescuedOrdinaryResidents > 16U ||
         profile.lastRaidResult->rescuedInjuredResidents >
             profile.lastRaidResult->rescuedOrdinaryResidents))
    {
        return {false, "last Raid result is invalid"};
    }
    return {true, {}};
}

std::uint64_t profileStateFingerprint(const ProfileState &profile) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    hashBytes(hash, profile.profileId);
    hashInteger(hash, profile.revision);
    hashInteger(hash, profile.currency);
    for (const auto &[mapId, counts] : profile.raidIntelligence.counts)
    {
        hashBytes(hash, mapId.value());
        for (std::uint32_t count : counts)
        {
            hashInteger(hash, count);
        }
    }
    hashInteger(hash, static_cast<std::uint32_t>(profile.tutorial));
    hashInteger(hash, profile.currentHealth);
    hashInteger(hash, static_cast<std::uint32_t>(
        profile.medicalStatus.bleeding));
    hashInteger(hash, profile.medicalStatus.lightBleedingRemainingMs);
    hashInteger(hash, profile.medicalStatus.bleedingDamageRemainingMs);
    hashInteger(hash, profile.medicalStatus.painkillerRemainingMs);
    hashInteger(hash, profile.medicalStatus.painScreamRemainingMs);
    hashInteger(hash, profile.worldClock.elapsedWorldMinutes);
    hashInteger(hash, profile.baseResources.pool.food);
    hashInteger(hash, profile.baseResources.pool.hygiene);
    hashInteger(hash, profile.baseResources.pool.morale);
    hashInteger(hash, profile.baseResources.pool.security);
    hashInteger(hash, profile.baseResources.lastShortfall.food);
    hashInteger(hash, profile.baseResources.lastShortfall.hygiene);
    hashInteger(hash, profile.baseResources.lastShortfall.morale);
    hashInteger(hash, profile.baseResources.lastShortfall.security);
    hashInteger(hash, profile.baseResources.resolvedDemandCycleCount);
    hashInteger(hash, static_cast<std::uint32_t>(profile.baseMorale.tier));
    hashInteger(hash, static_cast<std::uint32_t>(profile.baseMorale.trend));
    hashInteger(hash, profile.baseMorale.resolvedDayCount);
    hashInteger(hash, profile.baseMorale.consecutiveLowDays);
    hashInteger(hash, profile.baseMorale.supportedRecoveryDays);
    hashInteger(hash, profile.baseMorale.pendingFulfilledWishCount);
    hashInteger(hash, profile.baseMorale.pendingMissedWishCount);
    hashInteger(hash, profile.baseMorale.pendingPositiveEventCount);
    hashInteger(hash, profile.baseMorale.pendingNegativeEventCount);
    hashInteger(hash, profile.baseMorale.lastLedger.dayIndex);
    hashInteger(hash, profile.baseMorale.lastLedger.resourceShortfall.food);
    hashInteger(hash, profile.baseMorale.lastLedger.resourceShortfall.hygiene);
    hashInteger(hash, profile.baseMorale.lastLedger.resourceShortfall.morale);
    hashInteger(hash, profile.baseMorale.lastLedger.resourceShortfall.security);
    hashInteger(hash, profile.baseMorale.lastLedger.bedShortfall);
    hashInteger(hash, profile.baseMorale.lastLedger.fulfilledWishCount);
    hashInteger(hash, profile.baseMorale.lastLedger.missedWishCount);
    hashInteger(hash, profile.baseMorale.lastLedger.positiveEventCount);
    hashInteger(hash, profile.baseMorale.lastLedger.negativeEventCount);
    hashInteger(hash, profile.baseMorale.lastLedger.netScore);
    hashBytes(hash, profile.baseCommunityEvent.definitionId.value());
    hashInteger(hash, profile.baseCommunityEvent.cycleIndex);
    for (const auto &[definitionId, category] :
         profile.baseSupplyPolicy.assignments)
    {
        hashBytes(hash, definitionId.value());
        hashInteger(hash, static_cast<std::uint32_t>(category));
    }
    hashInteger(hash, profile.basePopulation.ordinaryResidents);
    hashInteger(hash, profile.basePopulation.bedCapacity);
    hashInteger(hash, profile.basePopulation.injuredResidents);
    for (std::size_t index = 0; index < kBaseResidentProfessionCount; ++index)
    {
        hashInteger(hash, profile.basePopulation.professionResidents[index]);
        hashInteger(hash, profile.basePopulation.injuredByProfession[index]);
    }
    hashInteger(hash, profile.baseWorkforce.workshopWorker.has_value() ? 1U : 0U);
    if (profile.baseWorkforce.workshopWorker.has_value())
    {
        hashInteger(hash, static_cast<std::uint32_t>(
            *profile.baseWorkforce.workshopWorker));
    }
    hashInteger(hash, profile.baseWorkforce.medicalWorker.has_value() ? 1U : 0U);
    if (profile.baseWorkforce.medicalWorker.has_value())
    {
        hashInteger(hash, static_cast<std::uint32_t>(
            *profile.baseWorkforce.medicalWorker));
    }
    hashInteger(hash, profile.baseConstruction.materialUnits);
    hashInteger(hash, profile.baseConstruction.dormitoryLevel);
    hashInteger(hash, profile.baseConstruction.workshopLevel);
    hashInteger(hash, profile.baseConstruction.medicalLevel);
    if (profile.baseConstruction.activeProject.has_value())
    {
        const ActiveBaseConstructionProject &project =
            *profile.baseConstruction.activeProject;
        hashBytes(hash, project.definitionId.value());
        hashInteger(hash, project.lockedMaterialUnits);
        hashInteger(hash, project.committedWorkers);
        hashInteger(hash, project.startedWorldMinute);
        hashInteger(hash, project.completionWorldMinute);
    }
    if (profile.residentMedical.activeTreatment.has_value())
    {
        const ActiveResidentTreatment &treatment =
            *profile.residentMedical.activeTreatment;
        hashInteger(hash, treatment.jobId);
        hashInteger(hash, treatment.startedWorldMinute);
        hashInteger(hash, treatment.completionWorldMinute);
        hashInteger(hash, treatment.consumedContribution);
        hashInteger(hash, static_cast<std::uint32_t>(
            treatment.patientProfession));
        hashInteger(hash, static_cast<std::uint32_t>(
            treatment.workerProfession));
    }
    if (profile.baseManufacturing.activeOrder.has_value())
    {
        const BaseManufacturingOrder &order =
            *profile.baseManufacturing.activeOrder;
        hashInteger(hash, order.jobId);
        hashBytes(hash, order.recipeDefinitionId.value());
        hashInteger(hash, order.committedWorkers);
        hashInteger(hash, static_cast<std::uint32_t>(
            order.workerProfession));
        hashInteger(hash, order.startedWorldMinute);
        hashInteger(hash, order.completionWorldMinute);
        for (AssetInstanceId inputId : order.inputAssetIds)
        {
            hashInteger(hash, inputId);
        }
        hashInteger(hash, order.outputAssetId);
        hashInteger(hash, order.outputReady ? 1U : 0U);
    }
    hashBytes(hash, profile.basePriority.definitionId.value());
    hashInteger(hash, profile.basePriority.cycleIndex);
    hashInteger(hash, profile.basePriority.fulfilled ? 1U : 0U);
    hashInteger(hash, profile.basePriority.missedCycleCount);
    hashInteger(hash, profile.nextBaseServiceJobId);
    if (profile.gunsmithMaintenanceJob.has_value())
    {
        const GunsmithMaintenanceJob &job =
            *profile.gunsmithMaintenanceJob;
        hashInteger(hash, job.jobId);
        hashInteger(hash, job.weaponAssetId);
        hashInteger(hash, job.returnOrigin.x);
        hashInteger(hash, job.returnOrigin.y);
        hashInteger(hash, job.startedWorldMinute);
        hashInteger(hash, job.completionWorldMinute);
        hashInteger(hash, job.paidCurrency);
        hashInteger(hash, job.targetFactoryDurabilityCenti);
    }
    hashInteger(hash, profile.assets.nextAssetId());
    for (const auto &[id, asset] : profile.assets.records())
    {
        hashInteger(hash, id);
        hashBytes(hash, asset.definitionId.value());
        hashInteger(hash, asset.quantity);
        hashInteger(hash, static_cast<std::uint32_t>(asset.orientation));
        hashInteger(hash, asset.remainingCharges);
        hashInteger(hash, asset.currentMaximumDurability);
        hashInteger(hash, asset.currentDurability);
        hashInteger(hash, static_cast<std::uint32_t>(asset.weaponMalfunction));
        hashBytes(hash, asset.reliefBatchId.value_or(""));
        for (const MagazineRoundRecord &round : asset.magazineRounds)
        {
            hashBytes(hash, round.definitionId.value());
            hashBytes(hash, round.reliefBatchId.value_or(""));
        }
        hashBytes(hash, asset.chamberedRound.has_value()
            ? asset.chamberedRound->definitionId.value()
            : "");
        hashBytes(hash, asset.chamberedRound.has_value()
            ? asset.chamberedRound->reliefBatchId.value_or("")
            : "");
        if (const auto *stored =
                std::get_if<StoredAssetLocation>(&asset.location))
        {
            hashInteger(hash, 0U);
            hashInteger(hash, static_cast<std::uint32_t>(stored->container.kind));
            hashInteger(hash, stored->container.ownerAssetId);
            hashInteger(hash, stored->container.compartmentIndex);
            hashInteger(hash, stored->origin.x);
            hashInteger(hash, stored->origin.y);
        }
        else if (const auto *equipped =
                     std::get_if<EquippedAssetLocation>(&asset.location))
        {
            hashInteger(hash, 1U);
            hashInteger(hash, static_cast<std::uint32_t>(equipped->slot));
        }
        else if (const auto *installed =
                     std::get_if<InstalledMagazineLocation>(&asset.location))
        {
            hashInteger(hash, 2U);
            hashInteger(hash, installed->weaponAssetId);
        }
        else if (const auto *ground =
                     std::get_if<RaidGroundAssetLocation>(&asset.location))
        {
            hashInteger(hash, 3U);
            hashBytes(hash, ground->raidId);
            hashInteger(hash, ground->lootSlotIndex);
        }
        else
        {
            const auto &service =
                std::get<BaseServiceAssetLocation>(asset.location);
            hashInteger(hash, 4U);
            hashInteger(hash, service.jobId);
        }
    }
    for (const std::string &transaction : profile.committedTransactions)
    {
        hashBytes(hash, transaction);
    }
    for (const std::string &settlement : profile.committedSettlements)
    {
        hashBytes(hash, settlement);
    }
    for (const RescueDefinitionId &rescue : profile.committedRescues)
    {
        hashBytes(hash, rescue.value());
    }
    if (profile.pendingRaid.has_value())
    {
        const PendingRaidSnapshot &raid = *profile.pendingRaid;
        hashBytes(hash, raid.raidId);
        hashBytes(hash, raid.settlementId);
        hashBytes(hash, raid.rulesVersion);
        hashBytes(hash, raid.mapDefinitionId.value());
        hashInteger(hash, raid.seed);
        hashBytes(hash, raid.spawnExtractionPairId);
        hashBytes(hash, raid.enemyDeploymentId.value());
        hashFloat(hash, raid.playerSpawn.x);
        hashFloat(hash, raid.playerSpawn.y);
        hashFloat(hash, raid.extractionPoint.position.x);
        hashFloat(hash, raid.extractionPoint.position.y);
        hashFloat(hash, raid.extractionPoint.size.x);
        hashFloat(hash, raid.extractionPoint.size.y);
        hashInteger(hash, raid.spatialLayout.generationAttempt);
        hashInteger(hash, raid.spatialLayout.layoutHash);
        hashInteger(hash, raid.spatialLayout.usedFallback ? 1U : 0U);
        for (const ContentRect &blocker :
             raid.spatialLayout.ballisticBlockers)
        {
            hashFloat(hash, blocker.position.x);
            hashFloat(hash, blocker.position.y);
            hashFloat(hash, blocker.size.x);
            hashFloat(hash, blocker.size.y);
        }
        for (const RaidEnemySnapshot &enemy : raid.enemies)
        {
            hashFloat(hash, enemy.position.x);
            hashFloat(hash, enemy.position.y);
            hashFloat(hash, enemy.size.x);
            hashFloat(hash, enemy.size.y);
            hashInteger(hash, enemy.maximumHealth);
            hashBytes(hash, enemy.spaceId.value());
        }
        for (const RaidLootSnapshot &loot : raid.loot)
        {
            hashInteger(hash, loot.assetId);
            hashBytes(hash, loot.definitionId.value());
            hashInteger(hash, loot.quantity);
            hashInteger(hash, loot.slotIndex);
            hashFloat(hash, loot.position.x);
            hashFloat(hash, loot.position.y);
            hashInteger(hash, loot.requiresHighRisk ? 1U : 0U);
            hashInteger(hash, loot.collected ? 1U : 0U);
            hashBytes(hash, loot.spaceId.value());
        }
        for (const RaidInteriorSnapshot &interior : raid.interiors)
        {
            hashBytes(hash, interior.id.value());
            hashBytes(hash, interior.displayName);
            hashFloat(hash, interior.worldSize.x);
            hashFloat(hash, interior.worldSize.y);
            hashFloat(hash, interior.exteriorEntrance.position.x);
            hashFloat(hash, interior.exteriorEntrance.position.y);
            hashFloat(hash, interior.exteriorEntrance.size.x);
            hashFloat(hash, interior.exteriorEntrance.size.y);
            hashFloat(hash, interior.exteriorReturn.x);
            hashFloat(hash, interior.exteriorReturn.y);
            hashFloat(hash, interior.interiorSpawn.x);
            hashFloat(hash, interior.interiorSpawn.y);
            hashFloat(hash, interior.interiorExit.position.x);
            hashFloat(hash, interior.interiorExit.position.y);
            hashFloat(hash, interior.interiorExit.size.x);
            hashFloat(hash, interior.interiorExit.size.y);
            for (const ContentRect &blocker : interior.ballisticBlockers)
            {
                hashFloat(hash, blocker.position.x);
                hashFloat(hash, blocker.position.y);
                hashFloat(hash, blocker.size.x);
                hashFloat(hash, blocker.size.y);
            }
        }
        for (AssetInstanceId root : raid.carriedRootAssetIds)
        {
            hashInteger(hash, root);
        }
        if (raid.rescue.has_value())
        {
            hashBytes(hash, raid.rescue->definitionId.value());
            hashInteger(hash, static_cast<std::uint32_t>(
                raid.rescue->subjectKind));
            hashFloat(hash, raid.rescue->transferPoint.position.x);
            hashFloat(hash, raid.rescue->transferPoint.position.y);
            hashFloat(hash, raid.rescue->transferPoint.size.x);
            hashFloat(hash, raid.rescue->transferPoint.size.y);
            hashFloat(hash, raid.rescue->interactionDurationSeconds);
            hashInteger(hash, raid.rescue->ordinaryResidentCount);
            hashInteger(hash, raid.rescue->injuredResidentCount);
            hashInteger(hash, static_cast<std::uint32_t>(
                raid.rescue->profession));
            hashInteger(hash, raid.rescue->secured ? 1U : 0U);
        }
        hashInteger(hash, raid.startingHealth);
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.startingMedicalStatus.bleeding));
        hashInteger(hash, raid.startingMedicalStatus.lightBleedingRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.bleedingDamageRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.painkillerRemainingMs);
        hashInteger(hash, raid.startingMedicalStatus.painScreamRemainingMs);
        for (bool selected : raid.intelligence.selected)
        {
            hashInteger(hash, static_cast<std::uint32_t>(selected));
        }
        hashInteger(hash, raid.travel.outboundMinutes);
        hashInteger(hash, raid.travel.returnMinutes);
        hashInteger(hash, raid.travel.failureRegroupMinutes);
        hashInteger(hash,
                    raid.travel.startingWorldClock.elapsedWorldMinutes);
        for (const auto &[mapId, counts] :
             raid.travel.startingRaidIntelligence.counts)
        {
            hashBytes(hash, mapId.value());
            for (std::uint32_t count : counts)
            {
                hashInteger(hash, count);
            }
        }
        hashInteger(hash, raid.travel.startingBaseResources.pool.food);
        hashInteger(hash, raid.travel.startingBaseResources.pool.hygiene);
        hashInteger(hash, raid.travel.startingBaseResources.pool.morale);
        hashInteger(hash, raid.travel.startingBaseResources.pool.security);
        hashInteger(hash,
                    raid.travel.startingBaseResources.lastShortfall.food);
        hashInteger(hash,
                    raid.travel.startingBaseResources.lastShortfall.hygiene);
        hashInteger(hash,
                    raid.travel.startingBaseResources.lastShortfall.morale);
        hashInteger(hash,
                    raid.travel.startingBaseResources.lastShortfall.security);
        hashInteger(hash,
                    raid.travel.startingBaseResources
                        .resolvedDemandCycleCount);
        hashBytes(hash,
                  raid.travel.startingBasePriority.definitionId.value());
        hashInteger(hash, raid.travel.startingBasePriority.cycleIndex);
        hashInteger(hash,
                    raid.travel.startingBasePriority.fulfilled ? 1U : 0U);
        hashInteger(hash,
                    raid.travel.startingBasePriority.missedCycleCount);
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.travel.startingBaseMorale.tier));
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.travel.startingBaseMorale.trend));
        hashInteger(hash, raid.travel.startingBaseMorale.resolvedDayCount);
        hashInteger(hash, raid.travel.startingBaseMorale.consecutiveLowDays);
        hashInteger(hash, raid.travel.startingBaseMorale.supportedRecoveryDays);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.pendingFulfilledWishCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.pendingMissedWishCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.pendingPositiveEventCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.pendingNegativeEventCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger.dayIndex);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .resourceShortfall.food);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .resourceShortfall.hygiene);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .resourceShortfall.morale);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .resourceShortfall.security);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger.bedShortfall);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .fulfilledWishCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .missedWishCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .positiveEventCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger
                        .negativeEventCount);
        hashInteger(hash,
                    raid.travel.startingBaseMorale.lastLedger.netScore);
        hashBytes(hash,
                  raid.travel.startingBaseCommunityEvent.definitionId.value());
        hashInteger(hash, raid.travel.startingBaseCommunityEvent.cycleIndex);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.materialUnits);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.dormitoryLevel);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.workshopLevel);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.medicalLevel);
        hashInteger(hash,
                    raid.travel.startingBaseWorkforce.workshopWorker.has_value()
                        ? 1U
                        : 0U);
        if (raid.travel.startingBaseWorkforce.workshopWorker.has_value())
        {
            hashInteger(hash, static_cast<std::uint32_t>(
                *raid.travel.startingBaseWorkforce.workshopWorker));
        }
        hashInteger(hash,
                    raid.travel.startingBaseWorkforce.medicalWorker.has_value()
                        ? 1U
                        : 0U);
        if (raid.travel.startingBaseWorkforce.medicalWorker.has_value())
        {
            hashInteger(hash, static_cast<std::uint32_t>(
                *raid.travel.startingBaseWorkforce.medicalWorker));
        }
        if (raid.travel.startingBaseConstruction.activeProject.has_value())
        {
            const ActiveBaseConstructionProject &project =
                *raid.travel.startingBaseConstruction.activeProject;
            hashBytes(hash, project.definitionId.value());
            hashInteger(hash, project.lockedMaterialUnits);
            hashInteger(hash, project.committedWorkers);
            hashInteger(hash, project.startedWorldMinute);
            hashInteger(hash, project.completionWorldMinute);
        }
        hashInteger(hash, raid.travel.startingBedCapacity);
        hashInteger(hash, raid.travel.startingInjuredResidents);
        for (std::uint32_t count :
             raid.travel.startingInjuredByProfession)
        {
            hashInteger(hash, count);
        }
        if (raid.travel.startingResidentMedical.activeTreatment.has_value())
        {
            const ActiveResidentTreatment &treatment =
                *raid.travel.startingResidentMedical.activeTreatment;
            hashInteger(hash, treatment.jobId);
            hashInteger(hash, treatment.startedWorldMinute);
            hashInteger(hash, treatment.completionWorldMinute);
            hashInteger(hash, treatment.consumedContribution);
            hashInteger(hash, static_cast<std::uint32_t>(
                treatment.patientProfession));
            hashInteger(hash, static_cast<std::uint32_t>(
                treatment.workerProfession));
        }
    }
    if (profile.lastRaidResult.has_value())
    {
        hashBytes(hash, profile.lastRaidResult->settlementId);
        hashInteger(hash, static_cast<std::uint32_t>(
            profile.lastRaidResult->outcome));
        for (const ItemDefinitionId &id :
             profile.lastRaidResult->returnedItemDefinitionIds)
        {
            hashBytes(hash, id.value());
        }
        hashInteger(hash, profile.lastRaidResult->currencyDelta);
        hashInteger(hash, profile.lastRaidResult->travelMinutesApplied);
        hashInteger(hash, profile.lastRaidResult->rescuedOrdinaryResidents);
        hashInteger(hash, profile.lastRaidResult->rescuedInjuredResidents);
    }
    return hash;
}

WeaponReliabilityTier weaponReliabilityTier(
    const AssetRecord &weapon,
    const ItemDefinition &definition) noexcept
{
    if (!definition.weaponCondition.has_value() ||
        weapon.currentDurability == 0)
    {
        return WeaponReliabilityTier::Broken;
    }
    const std::uint32_t points = weapon.currentDurability / 100U;
    if (points >= 61U)
        return WeaponReliabilityTier::Reliable;
    if (points >= 31U)
        return WeaponReliabilityTier::Worn;
    if (points >= 11U)
        return WeaponReliabilityTier::HighRisk;
    return WeaponReliabilityTier::Critical;
}
