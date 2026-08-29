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
#include <numeric>
#include <stdexcept>
#include <type_traits>

#include "alpha_content_ids.h"
#include "base_construction_domain.h"
#include "base_resource_domain.h"
#include "recovery_task_domain.h"
#include "regional_operations_domain.h"

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
    profile.regionalOperations.activeBaseNodeId =
        content.regionalOperations().initialBaseNodeId;
    const auto initialSite = std::find_if(
        content.regionalOperations().baseSites.begin(),
        content.regionalOperations().baseSites.end(),
        [&](const RegionalBaseSiteDefinition &site)
        { return site.nodeId == profile.regionalOperations.activeBaseNodeId; });
    if (initialSite == content.regionalOperations().baseSites.end())
    {
        throw std::logic_error{"initial regional Base site is missing"};
    }
    profile.regionalOperations.technologyCore = TechnologyCoreState{
        "technology_core.primary", initialSite->id};
    for (const RegionalBaseSiteDefinition &site :
         content.regionalOperations().baseSites)
    {
        profile.regionalOperations.baseSites.emplace(
            site.id,
            RegionalBaseSiteState{
                site.initiallyUnlocked,
                site.uniqueFeatureInitiallyRepaired});
    }
    for (const RegionalOutpostDefinition &outpost :
         content.regionalOperations().outposts)
    {
        profile.regionalOperations.outposts.emplace(
            outpost.id,
            RegionalOutpostState{outpost.initiallyUnlocked});
    }
    for (const BaseFacilityDefinition &facility : content.baseFacilities())
    {
        if (facility.initiallyOwned)
        {
            profile.baseConstruction.facilities.emplace(
                facility.id,
                facility.initiallyInstalled
                    ? BaseConstructionState::FacilityPlacement::Installed
                    : BaseConstructionState::FacilityPlacement::Reserve);
            if (!facility.initiallyInstalled)
            {
                profile.baseConstruction.facilityReserveStartedWorldMinutes
                    .emplace(
                        facility.id,
                        profile.worldClock.elapsedWorldMinutes);
            }
        }
    }

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
    if (level == 0U)
    {
        return std::any_of(
            content.baseConstructionProjects().begin(),
            content.baseConstructionProjects().end(),
            [target](const BaseConstructionProjectDefinition &definition)
            {
                return definition.target == target &&
                    definition.requiredLevel == 0U;
            });
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
    for (const RaidSpaceDefinitionId &interiorId :
         profile.raidInteriorIntelligence.knownLayouts)
    {
        try
        {
            static_cast<void>(content.raidInterior(interiorId));
        }
        catch (...)
        {
            return {false, "Raid interior intelligence archive is invalid"};
        }
    }
    try
    {
        if (content.regionNode(
                profile.regionalOperations.activeBaseNodeId).kind !=
            RegionNodeKind::Base)
        {
            return {false, "regional active Base node is invalid"};
        }
    }
    catch (...)
    {
        return {false, "regional active Base node is unknown"};
    }
    if (profile.regionalOperations.baseSites.size() !=
        content.regionalOperations().baseSites.size())
    {
        return {false, "regional Base site state set is incomplete"};
    }
    bool activeBaseSiteUnlocked{};
    const RegionalBaseSiteDefinition *activeBaseSite{};
    for (const RegionalBaseSiteDefinition &definition :
         content.regionalOperations().baseSites)
    {
        const auto found = profile.regionalOperations.baseSites.find(
            definition.id);
        if (found == profile.regionalOperations.baseSites.end())
        {
            return {false, "regional Base site state is missing"};
        }
        if (found->second.uniqueFeatureRepaired &&
            !found->second.unlocked &&
            !definition.uniqueFeatureInitiallyRepaired)
        {
            return {false,
                    "locked regional Base site has a repaired feature"};
        }
        if (definition.nodeId ==
            profile.regionalOperations.activeBaseNodeId)
        {
            activeBaseSiteUnlocked = found->second.unlocked;
            activeBaseSite = &definition;
        }
        if (definition.outpostDefinitionId.has_value())
        {
            const auto outpost = profile.regionalOperations.outposts.find(
                *definition.outpostDefinitionId);
            if (outpost == profile.regionalOperations.outposts.end() ||
                outpost->second.unlocked != found->second.unlocked)
            {
                return {false,
                        "regional Base site and outpost unlock state differ"};
            }
        }
    }
    if (!activeBaseSiteUnlocked)
    {
        return {false, "regional active Base site is locked"};
    }
    if (activeBaseSite == nullptr ||
        profile.regionalOperations.technologyCore.instanceId !=
            "technology_core.primary" ||
        profile.regionalOperations.technologyCore.baseSiteDefinitionId !=
            activeBaseSite->id)
    {
        return {false, "technology core and active Base site disagree"};
    }
    std::uint32_t establishedOutposts{};
    BaseProfessionCounts regionalStaffByProfession{};
    if (profile.regionalOperations.outposts.size() !=
        content.regionalOperations().outposts.size())
    {
        return {false, "regional outpost state set is incomplete"};
    }
    for (const RegionalOutpostDefinition &definition :
         content.regionalOperations().outposts)
    {
        const auto found = profile.regionalOperations.outposts.find(
            definition.id);
        if (found == profile.regionalOperations.outposts.end())
        {
            return {false, "regional outpost state is missing"};
        }
        const RegionalOutpostState &state = found->second;
        std::uint32_t assigned{};
        for (std::size_t index{}; index < state.assignedStaff.size(); ++index)
        {
            assigned += state.assignedStaff[index];
            regionalStaffByProfession[index] += state.assignedStaff[index];
        }
        establishedOutposts += state.established ? 1U : 0U;
        if ((state.established && !state.unlocked) ||
            (state.disrupted && !state.established) ||
            assigned > definition.requiredStaff ||
            (!state.established && assigned != 0U) ||
            state.shortcutOperationsSinceRestoration >
                definition.safeShortcutOperations ||
            (!state.established &&
             state.shortcutOperationsSinceRestoration != 0U) ||
            (state.disrupted &&
             state.shortcutOperationsSinceRestoration !=
                 definition.safeShortcutOperations))
        {
            return {false, "regional outpost state is invalid"};
        }
        if (activeBaseSite->outpostDefinitionId ==
                std::optional<RegionalOutpostDefinitionId>{definition.id} &&
            (state.established || assigned != 0U))
        {
            return {false, "active Base cannot also operate as an outpost"};
        }
    }
    if (establishedOutposts >
        content.regionalOperations().maximumEstablishedOutposts)
    {
        return {false, "regional outpost capacity is exceeded"};
    }
    const BaseSiegeState &siege = profile.baseSiege;
    if (siege.raidThreatUnits > kBaseSiegeThreatThreshold ||
        siege.populationThreatUnits > kBaseSiegeThreatThreshold ||
        siege.siteThreatUnits > kBaseSiegeThreatThreshold ||
        static_cast<std::uint64_t>(siege.raidThreatUnits) +
                siege.populationThreatUnits + siege.siteThreatUnits >
            kBaseSiegeThreatThreshold ||
        siege.resolvedDayCount >
            projectWorldClock(profile.worldClock).completedDays ||
        siege.warningRemainingSeconds > kBaseSiegeWarningSeconds ||
        (!siege.warningActive && siege.warningRemainingSeconds != 0U) ||
        (siege.warningActive &&
         totalBaseThreat(siege) < kBaseSiegeThreatThreshold) ||
        (siege.lastOutcome == BaseSiegeOutcome::None &&
         (siege.lastSecuritySpent != 0U ||
          siege.lastPopulationLost != 0U)) ||
        siege.lastPopulationLost > 1U)
    {
        return {false, "Base siege state is invalid"};
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
    for (std::size_t index{}; index < assignedByProfession.size(); ++index)
    {
        assignedByProfession[index] += regionalStaffByProfession[index];
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
    for (const auto &[definitionId, placement] :
         profile.baseConstruction.facilities)
    {
        try
        {
            static_cast<void>(content.baseFacility(definitionId));
        }
        catch (...)
        {
            return {false, "Base facility ownership is invalid"};
        }
        if (placement != BaseConstructionState::FacilityPlacement::Installed &&
            placement != BaseConstructionState::FacilityPlacement::Reserve)
        {
            return {false, "Base facility placement is invalid"};
        }
        const auto reserveStarted = profile.baseConstruction
            .facilityReserveStartedWorldMinutes.find(definitionId);
        if ((placement ==
                 BaseConstructionState::FacilityPlacement::Reserve) !=
                (reserveStarted != profile.baseConstruction
                    .facilityReserveStartedWorldMinutes.end()) ||
            (reserveStarted != profile.baseConstruction
                    .facilityReserveStartedWorldMinutes.end() &&
             reserveStarted->second >
                 profile.worldClock.elapsedWorldMinutes))
        {
            return {false, "Base facility reserve timing is invalid"};
        }
    }
    for (const auto &[definitionId, reserveStarted] :
         profile.baseConstruction.facilityReserveStartedWorldMinutes)
    {
        static_cast<void>(reserveStarted);
        if (!profile.baseConstruction.facilities.contains(definitionId))
        {
            return {false, "Base facility reserve owner is missing"};
        }
    }
    const auto ownsFacility = [&](std::string_view value)
    {
        return profile.baseConstruction.facilities.contains(
            BaseFacilityDefinitionId{std::string{value}});
    };
    if (!ownsFacility("base_facility.warehouse") ||
        (profile.baseConstruction.dormitoryLevel > 0U) !=
            ownsFacility("base_facility.dormitory") ||
        (profile.baseConstruction.kitchenWaterLevel > 0U) !=
            ownsFacility("base_facility.kitchen_water") ||
        (profile.baseConstruction.workshopLevel > 0U) !=
            ownsFacility("base_facility.workshop") ||
        (profile.baseConstruction.medicalLevel > 0U) !=
            ownsFacility("base_facility.medical"))
    {
        return {false, "Base facility ownership and levels disagree"};
    }
    if (!publishedBaseFacilityLevel(
            profile.baseConstruction,
            BaseFacilityUpgradeTarget::Dormitory,
            content) ||
        !publishedBaseFacilityLevel(
            profile.baseConstruction,
            BaseFacilityUpgradeTarget::KitchenWater,
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
        std::uint64_t projectProgressWorldMinute =
            profile.worldClock.elapsedWorldMinutes;
        if (const std::optional<std::uint64_t> reserveStarted =
                baseFacilityReserveStartedWorldMinute(
                    profile,
                    baseFacilityDefinitionId(definition->target));
            reserveStarted.has_value())
        {
            projectProgressWorldMinute = *reserveStarted;
        }
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
            active.completionWorldMinute <= projectProgressWorldMinute ||
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
    const auto validLostRecordMetadata = [&profile, &content](
        const LostRaidRecord &record)
    {
        bool mapPublished{};
        try
        {
            static_cast<void>(content.map(record.mapDefinitionId));
            mapPublished = true;
        }
        catch (...)
        {
        }
        return !record.recordId.empty() &&
            !record.raidId.empty() &&
            record.settlementId == record.recordId &&
            !record.difficulty.empty() && mapPublished &&
            profile.committedSettlements.contains(record.settlementId) &&
            (record.outcome == RaidResultOutcome::PlayerDead ||
             record.outcome == RaidResultOutcome::ActiveQuit) &&
            record.subsequentRaidSettlementCount <=
                kLostRaidRecordRetainedSettlementCount;
    };
    for (const auto &[recordId, record] : profile.lostRaidRecords)
    {
        if (record.recordId != recordId ||
            !validLostRecordMetadata(record))
        {
            return {false, "lost Raid record is invalid"};
        }
    }
    if (profile.nextRecoveryTaskId == 0U)
    {
        return {false, "recovery task high-water mark is invalid"};
    }
    if (profile.recoveryTask.has_value())
    {
        const RecoveryTask &task = *profile.recoveryTask;
        if (task.taskId == 0U || task.taskId >= profile.nextRecoveryTaskId ||
            !validLostRecordMetadata(task.sourceRecord) ||
            profile.lostRaidRecords.contains(task.sourceRecord.recordId) ||
            task.paidCurrency == 0U ||
            task.completionWorldMinute <= task.startedWorldMinute ||
            task.startedWorldMinute > profile.worldClock.elapsedWorldMinutes ||
            task.readyForCollection !=
                (profile.worldClock.elapsedWorldMinutes >=
                 task.completionWorldMinute))
        {
            return {false, "recovery task is invalid"};
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
    std::set<std::pair<std::string, EquipmentSlotKind>> lostSourceSlots;
    std::set<std::string> lostRecordsWithRoots;
    std::set<EquipmentSlotKind> recoverySourceSlots;
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

        if (const auto *lost =
                std::get_if<LostRaidAssetLocation>(&asset.location))
        {
            if (!profile.lostRaidRecords.contains(lost->recordId) ||
                !itemCanEquipInSlot(*definition, lost->sourceSlot) ||
                !lostSourceSlots.emplace(
                    lost->recordId, lost->sourceSlot).second)
            {
                return {false, "lost Raid asset ownership is invalid"};
            }
            lostRecordsWithRoots.insert(lost->recordId);
            continue;
        }

        if (const auto *task =
                std::get_if<RecoveryTaskAssetLocation>(&asset.location))
        {
            if (!profile.recoveryTask.has_value() ||
                task->taskId != profile.recoveryTask->taskId ||
                !itemCanEquipInSlot(*definition, task->sourceSlot) ||
                !recoverySourceSlots.insert(task->sourceSlot).second)
            {
                return {false, "recovery task asset ownership is invalid"};
            }
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
    if (lostRecordsWithRoots.size() != profile.lostRaidRecords.size())
    {
        return {false, "lost Raid record has no owned asset root"};
    }
    if (profile.recoveryTask.has_value())
    {
        if (recoverySourceSlots.empty())
        {
            return {false, "recovery task has no owned asset root"};
        }
        std::set<AssetInstanceId> taskAssetIds;
        for (const auto &[assetId, asset] : profile.assets.records())
        {
            static_cast<void>(asset);
            const std::optional<RecoveryTaskId> owner =
                recoveryTaskForAsset(profile, assetId);
            if (owner.has_value() &&
                *owner == profile.recoveryTask->taskId)
            {
                taskAssetIds.insert(assetId);
            }
        }
        if (!std::includes(
                taskAssetIds.begin(), taskAssetIds.end(),
                profile.recoveryTask->recoveredAssetIds.begin(),
                profile.recoveryTask->recoveredAssetIds.end()))
        {
            return {false, "recovery result references an unowned asset"};
        }
    }
    else if (!recoverySourceSlots.empty())
    {
        return {false, "recovery task assets have no owner"};
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
        const std::optional<std::uint64_t> workshopReserveStarted =
            baseFacilityReserveStartedWorldMinute(
                profile,
                BaseFacilityDefinitionId{"base_facility.workshop"});
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
                const std::uint32_t staffedDuration =
                    applyBaseFacilityTaskDuration(
                        moraleDuration,
                        BaseFacilityStaffingKind::Workshop,
                        order.workerProfession,
                        facilityLevel,
                        content.baseWorkforce());
                publishedDuration = publishedDuration ||
                    frozenDuration == staffedDuration;
                for (const RegionalBaseSiteDefinition &site :
                     content.regionalOperations().baseSites)
                {
                    const std::uint64_t adjusted =
                        (static_cast<std::uint64_t>(staffedDuration) *
                             site.uniqueFeatureManufacturingDurationPercent +
                         99U) /
                        100U;
                    publishedDuration = publishedDuration ||
                        frozenDuration ==
                            std::max<std::uint64_t>(1U, adjusted);
                }
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
                     workshopReserveStarted.value_or(
                         profile.worldClock.elapsedWorldMinutes) ||
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
        const bool legacyPlayableOutdoorRules =
            raid.rulesVersion == "procedural-playable-outdoor-layout-21";
        const bool legacyDistrictLayoutRules =
            raid.rulesVersion == "procedural-frontier-district-layout-22";
        const bool resourceEcologyRules =
            raid.rulesVersion == "procedural-frontier-resource-ecology-23";
        const bool districtLayoutRules =
            legacyDistrictLayoutRules || resourceEcologyRules;
        const bool playableOutdoorRules =
            legacyPlayableOutdoorRules || districtLayoutRules;
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
            raid.rulesVersion == "raid-special-location-placement-13" ||
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
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
            raid.rulesVersion == "raid-special-location-placement-13" ||
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool spatialLayoutRules =
            raid.rulesVersion == "procedural-outdoor-layout-11" ||
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13" ||
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool interiorRules =
            raid.rulesVersion == "raid-interior-spaces-12" ||
            raid.rulesVersion == "raid-special-location-placement-13" ||
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool specialLocationRules =
            raid.rulesVersion == "raid-special-location-placement-13" ||
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool buildingIntelligenceRules =
            raid.rulesVersion == "raid-building-intelligence-14" ||
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool multipleInteriorRules =
            raid.rulesVersion == "raid-second-representative-location-15" ||
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool selfRecoveryRules =
            raid.rulesVersion == "raid-self-recovery-16" ||
            raid.rulesVersion == "regional-route-network-17" ||
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool outpostRestorationRules =
            raid.rulesVersion == "regional-outpost-restoration-18" ||
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool baseSiteClearanceRules =
            raid.rulesVersion == "regional-base-site-clearance-19" ||
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        const bool basePerimeterSweepRules =
            raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
            playableOutdoorRules;
        std::set<AssetInstanceId> selfRecoveryRootIds;
        if (raid.selfRecovery.has_value())
        {
            for (const RaidSelfRecoveryRootSnapshot &root :
                 raid.selfRecovery->roots)
            {
                selfRecoveryRootIds.insert(root.assetId);
            }
        }
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
                [&](const RaidLootSnapshot &loot)
                {
                    return !loot.requiresHighRisk &&
                        loot.spaceId == outdoorRaidSpaceId() &&
                        !selfRecoveryRootIds.contains(loot.assetId);
                }));
        const std::size_t interiorLootCount = static_cast<std::size_t>(
            std::count_if(
                raid.loot.begin(), raid.loot.end(),
                [](const RaidLootSnapshot &loot)
                { return loot.spaceId != outdoorRaidSpaceId(); }));
        const std::size_t expectedResourceLootCount = resourceEcologyRules
            ? std::accumulate(
                  raid.spatialLayout.resourcePoints.begin(),
                  raid.spatialLayout.resourcePoints.end(),
                  std::size_t{},
                  [](std::size_t total,
                     const RaidResourcePointSnapshot &resourcePoint)
                  { return total + resourcePoint.capacity; })
            : 0U;
        std::size_t expectedInteriorEnemyCount{};
        std::size_t expectedInteriorLootCount{};
        const std::size_t expectedInteriorCount = multipleInteriorRules
            ? raidMap->interiors.size()
            : std::min<std::size_t>(raidMap->interiors.size(), 1U);
        for (std::size_t index{}; index < expectedInteriorCount; ++index)
        {
            const RaidInteriorDefinition &interior = raidMap->interiors[index];
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
            (resourceEcologyRules
                 ? outdoorRegularLootCount != expectedResourceLootCount
                 : outdoorRegularLootCount < 6 ||
                       outdoorRegularLootCount > 9) ||
            (interiorRules &&
             (raid.interiors.size() != expectedInteriorCount ||
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
        if (!selfRecoveryRules && raid.selfRecovery.has_value())
        {
            return {false, "pending Raid self-recovery rules are invalid"};
        }
        if (!outpostRestorationRules &&
            raid.outpostRestoration.has_value())
        {
            return {false,
                    "pending Raid outpost restoration rules are invalid"};
        }
        if (!baseSiteClearanceRules && raid.baseSiteClearance.has_value())
        {
            return {false,
                    "pending Raid Base site clearance rules are invalid"};
        }
        if (!basePerimeterSweepRules &&
            raid.basePerimeterSweep.has_value())
        {
            return {false,
                    "pending Raid Base perimeter sweep rules are invalid"};
        }
        const std::size_t regionalMissionCount =
            static_cast<std::size_t>(raid.selfRecovery.has_value()) +
            static_cast<std::size_t>(raid.outpostRestoration.has_value()) +
            static_cast<std::size_t>(raid.baseSiteClearance.has_value()) +
            static_cast<std::size_t>(raid.basePerimeterSweep.has_value());
        if (regionalMissionCount > 1U)
        {
            return {false, "pending Raid mission modes cannot be combined"};
        }
        if (raid.basePerimeterSweep.has_value())
        {
            try
            {
                const BasePerimeterSweepSnapshot &sweep =
                    *raid.basePerimeterSweep;
                const RegionalBaseSiteDefinition &definition =
                    content.regionalBaseSite(sweep.baseSiteDefinitionId);
                if (definition.nodeId != raid.travel
                        .startingRegionalOperations.activeBaseNodeId ||
                    definition.perimeterSweepMapDefinitionId !=
                        raid.mapDefinitionId ||
                    definition.perimeterSweepThreatReductionUnits !=
                        sweep.threatReductionUnits ||
                    sweep.threatReductionUnits == 0U ||
                    totalBaseThreat(raid.travel.startingBaseSiege) <
                        kBasePerimeterSweepMinimumThreat)
                {
                    return {false,
                            "pending Raid Base perimeter sweep is invalid"};
                }
            }
            catch (...)
            {
                return {false,
                        "pending Raid Base perimeter sweep is invalid"};
            }
        }
        if (raid.baseSiteClearance.has_value())
        {
            if (raid.selfRecovery.has_value() ||
                raid.outpostRestoration.has_value())
            {
                return {false,
                        "pending Raid mission modes cannot be combined"};
            }
            try
            {
                const RegionalBaseSiteDefinition &definition =
                    content.regionalBaseSite(
                        raid.baseSiteClearance->baseSiteDefinitionId);
                const auto state = raid.travel.startingRegionalOperations
                    .baseSites.find(definition.id);
                const auto outpost = definition.outpostDefinitionId.has_value()
                    ? raid.travel.startingRegionalOperations.outposts.find(
                          *definition.outpostDefinitionId)
                    : raid.travel.startingRegionalOperations.outposts.end();
                if (!definition.clearanceMapDefinitionId.has_value() ||
                    *definition.clearanceMapDefinitionId !=
                        raid.mapDefinitionId ||
                    !definition.outpostDefinitionId.has_value() ||
                    state == raid.travel.startingRegionalOperations
                        .baseSites.end() ||
                    state->second.unlocked ||
                    outpost == raid.travel.startingRegionalOperations
                        .outposts.end() ||
                    outpost->second.unlocked)
                {
                    return {false,
                            "pending Raid Base site clearance is invalid"};
                }
            }
            catch (...)
            {
                return {false,
                        "pending Raid Base site clearance is invalid"};
            }
        }
        if (raid.outpostRestoration.has_value())
        {
            if (raid.selfRecovery.has_value())
            {
                return {false,
                        "pending Raid mission modes cannot be combined"};
            }
            try
            {
                const RegionalOutpostRestorationSnapshot &restoration =
                    *raid.outpostRestoration;
                const RegionalOutpostDefinition &definition =
                    content.regionalOutpost(
                        restoration.outpostDefinitionId);
                const auto state = raid.travel.startingRegionalOperations
                    .outposts.find(definition.id);
                if (definition.restorationMapDefinitionId !=
                        raid.mapDefinitionId ||
                    state == raid.travel.startingRegionalOperations
                        .outposts.end() ||
                    !state->second.unlocked ||
                    !state->second.established ||
                    !state->second.disrupted ||
                    assignedRegionalOutpostStaff(state->second) !=
                        definition.requiredStaff)
                {
                    return {false,
                            "pending Raid outpost restoration is invalid"};
                }
            }
            catch (...)
            {
                return {false,
                        "pending Raid outpost restoration is invalid"};
            }
        }
        if (raid.selfRecovery.has_value())
        {
            const RaidSelfRecoverySnapshot &recovery = *raid.selfRecovery;
            const auto record = profile.lostRaidRecords.find(
                recovery.sourceRecord.recordId);
            const bool sourceOwnershipValid = recovery.opened
                ? record == profile.lostRaidRecords.end()
                : record != profile.lostRaidRecords.end() &&
                    record->second == recovery.sourceRecord;
            const RaidAnchorPlacementSnapshot *recoveryAnchor =
                districtLayoutRules
                    ? findRaidAnchorPlacement(
                          raid.spatialLayout, kRaidAnchorSelfRecovery)
                    : nullptr;
            const bool cacheMatches = districtLayoutRules
                ? recoveryAnchor != nullptr &&
                    recovery.cachePosition.x ==
                        recoveryAnchor->bounds.position.x +
                            recoveryAnchor->bounds.size.x * 0.5F &&
                    recovery.cachePosition.y ==
                        recoveryAnchor->bounds.position.y +
                            recoveryAnchor->bounds.size.y * 0.5F
                : std::any_of(
                      raidMap->raidLootSlots.begin(),
                      raidMap->raidLootSlots.end(),
                      [&](const RaidLootSlotDefinition &slot)
                      {
                          return slot.position.x == recovery.cachePosition.x &&
                              slot.position.y == recovery.cachePosition.y;
                      });
            std::set<EquipmentSlotKind> sourceSlots;
            std::set<std::uint32_t> recoverySlots;
            bool rootsValid = !recovery.roots.empty();
            for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
            {
                const AssetRecord *asset = profile.assets.find(root.assetId);
                const auto *lost = asset != nullptr
                    ? std::get_if<LostRaidAssetLocation>(&asset->location)
                    : nullptr;
                const auto *ground = asset != nullptr
                    ? std::get_if<RaidGroundAssetLocation>(&asset->location)
                    : nullptr;
                const bool carried = asset != nullptr &&
                    assetIsCarried(profile, root.assetId);
                const auto loot = std::find_if(
                    raid.loot.begin(), raid.loot.end(),
                    [&](const RaidLootSnapshot &candidate)
                    { return candidate.assetId == root.assetId; });
                const bool phaseValid = recovery.opened
                    ? asset != nullptr && (carried ||
                        (ground != nullptr &&
                         ground->raidId == raid.raidId &&
                         ground->lootSlotIndex == root.lootSlotIndex)) &&
                        loot != raid.loot.end() &&
                        loot->slotIndex == root.lootSlotIndex &&
                        loot->position.x == root.position.x &&
                        loot->position.y == root.position.y &&
                        loot->collected == carried
                    : lost != nullptr &&
                        lost->recordId == recovery.sourceRecord.recordId &&
                        lost->sourceSlot == root.sourceSlot &&
                        loot == raid.loot.end();
                rootsValid = rootsValid && root.assetId != 0U &&
                    std::isfinite(root.position.x) &&
                    std::isfinite(root.position.y) && phaseValid &&
                    sourceSlots.insert(root.sourceSlot).second &&
                    recoverySlots.insert(root.lootSlotIndex).second;
            }
            if (recovery.sourceRecord.recordId.empty() ||
                recovery.sourceRecord.mapDefinitionId != raid.mapDefinitionId ||
                !std::isfinite(recovery.cachePosition.x) ||
                !std::isfinite(recovery.cachePosition.y) ||
                !std::isfinite(recovery.interactionDurationSeconds) ||
                recovery.interactionDurationSeconds <= 0.0F ||
                recovery.interactionDurationSeconds > 10.0F ||
                !cacheMatches || !sourceOwnershipValid || !rootsValid)
            {
                return {false, "pending Raid self-recovery snapshot is invalid"};
            }
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
            const bool legacyScatterLayoutRules =
                spatialLayoutRules && !playableOutdoorRules;
            const bool generatedCountValid = !procedural.enabled ||
                raid.spatialLayout.usedFallback ||
                (legacyScatterLayoutRules
                     ? raid.spatialLayout.ballisticBlockers.size() >= 18U &&
                           raid.spatialLayout.ballisticBlockers.size() <= 26U
                     : legacyPlayableOutdoorRules
                         ? raid.spatialLayout.ballisticBlockers.size() >= 70U &&
                               raid.spatialLayout.ballisticBlockers.size() <= 95U
                         : raid.spatialLayout.ballisticBlockers.size() >=
                                   procedural.minimumBlockers &&
                               raid.spatialLayout.ballisticBlockers.size() <=
                                   procedural.maximumBlockers);
            const bool layoutVersionValid = !playableOutdoorRules ||
                (!procedural.enabled
                     ? raid.spatialLayout.layoutVersion == 0U &&
                           raid.spatialLayout.roadCells.empty()
                     : legacyPlayableOutdoorRules
                         ? raid.spatialLayout.layoutVersion == 2U &&
                               !raid.spatialLayout.roadCells.empty()
                         : raid.spatialLayout.layoutVersion ==
                                   (legacyDistrictLayoutRules
                                        ? 3U
                                        : procedural.layoutVersion) &&
                               !raid.spatialLayout.roadCells.empty());
            const bool fallbackStateValid =
                raid.spatialLayout.usedFallback
                    ? raid.spatialLayout.fallbackReason ==
                          RaidMapFallbackReason::AttemptsExhausted
                    : raid.spatialLayout.fallbackReason ==
                          RaidMapFallbackReason::None;
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
                if (selfRecoveryRootIds.contains(loot.assetId))
                {
                    continue;
                }
                anchors.reachablePoints.push_back(loot.position);
            }
            if (raid.selfRecovery.has_value())
            {
                anchors.reachablePoints.push_back(
                    raid.selfRecovery->cachePosition);
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
                if (specialLocationRules && !districtLayoutRules)
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
                if (multipleInteriorRules)
                {
                    appendRaidExteriorPlacementAnchors(
                        anchors,
                        RaidExteriorPlacementDefinition{
                            {},
                            interior.exteriorEntrance,
                            interior.exteriorReturn});
                }
                else
                {
                    anchors.occupiedRegions.push_back(
                        interior.exteriorEntrance);
                    addRegion(interior.exteriorEntrance);
                }
            }
            const std::uint64_t expectedLayoutHash = playableOutdoorRules
                ? raidMapLayoutHash(raid.spatialLayout)
                : raidMapLayoutHash(raid.spatialLayout.ballisticBlockers);
            bool districtSnapshotValid = true;
            if (districtLayoutRules)
            {
                std::set<std::string> anchorIds;
                std::set<std::uint16_t> districtIds;
                for (const RaidDistrictSnapshot &district :
                     raid.spatialLayout.districts)
                    districtSnapshotValid = districtSnapshotValid &&
                        district.instanceId != 0U &&
                        districtIds.insert(district.instanceId).second &&
                        !district.definitionId.empty() &&
                        !district.displayName.empty() &&
                        !district.cells.empty();
                for (const RaidAnchorPlacementSnapshot &placement :
                     raid.spatialLayout.anchorPlacements)
                    districtSnapshotValid = districtSnapshotValid &&
                        !placement.id.empty() &&
                        anchorIds.insert(placement.id).second &&
                        districtIds.contains(placement.districtInstanceId) &&
                        insideWalkable(placement.bounds);
                const auto matchesRect = [&](std::string_view id,
                                             RaidMapAnchorKind kind,
                                             ContentRect bounds)
                {
                    const auto *placement = findRaidAnchorPlacement(
                        raid.spatialLayout, id);
                    return placement != nullptr && placement->kind == kind &&
                        placement->bounds == bounds;
                };
                const auto matchesPoint = [&](std::string_view id,
                                              RaidMapAnchorKind kind,
                                              Vec2 point,
                                              bool useCenter)
                {
                    const auto *placement = findRaidAnchorPlacement(
                        raid.spatialLayout, id);
                    if (placement == nullptr || placement->kind != kind)
                        return false;
                    const Vec2 expected = useCenter
                        ? Vec2{placement->bounds.position.x +
                                   placement->bounds.size.x * 0.5F,
                               placement->bounds.position.y +
                                   placement->bounds.size.y * 0.5F}
                        : placement->bounds.position;
                    return point.x == expected.x && point.y == expected.y;
                };
                districtSnapshotValid = districtSnapshotValid &&
                    raid.spatialLayout.layoutVersion ==
                        (legacyDistrictLayoutRules
                             ? 3U
                             : raidMap->proceduralOutdoor.layoutVersion) &&
                    raid.spatialLayout.districts.size() == 8U &&
                    raid.spatialLayout.landmarks.size() == 3U &&
                    !raid.spatialLayout.terrainSpans.empty() &&
                    !raid.spatialLayout.props.empty() &&
                    matchesPoint(kRaidAnchorPlayerSpawn,
                                 RaidMapAnchorKind::PlayerSpawn,
                                 raid.playerSpawn, false) &&
                    matchesRect(kRaidAnchorNormalExtraction,
                                RaidMapAnchorKind::NormalExtraction,
                                raid.extractionPoint) &&
                    findRaidAnchorPlacement(
                        raid.spatialLayout,
                        kRaidAnchorEmergencyExtraction) != nullptr &&
                    findRaidAnchorPlacement(
                        raid.spatialLayout,
                        kRaidAnchorConditionalExtraction) != nullptr &&
                    findRaidAnchorPlacement(
                        raid.spatialLayout,
                        kRaidAnchorHighRiskControl) != nullptr &&
                    findRaidAnchorPlacement(
                        raid.spatialLayout,
                        kRaidAnchorAdvancedResource) != nullptr;
                std::set<std::string> resourcePointIds;
                std::size_t resourceCapacity{};
                if (resourceEcologyRules)
                {
                    districtSnapshotValid = districtSnapshotValid &&
                        !raid.spatialLayout.resourcePoints.empty();
                    for (const RaidResourcePointSnapshot &resourcePoint :
                         raid.spatialLayout.resourcePoints)
                    {
                        const auto definition = std::find_if(
                            raidMap->proceduralOutdoor
                                .resourcePointArchetypes.begin(),
                            raidMap->proceduralOutdoor
                                .resourcePointArchetypes.end(),
                            [&](const RaidResourcePointArchetypeDefinition &value)
                            { return value.id == resourcePoint.definitionId; });
                        const auto *anchor = findRaidAnchorPlacement(
                            raid.spatialLayout, resourcePoint.instanceId);
                        const auto district = std::find_if(
                            raid.spatialLayout.districts.begin(),
                            raid.spatialLayout.districts.end(),
                            [&](const RaidDistrictSnapshot &value)
                            {
                                return value.instanceId ==
                                    resourcePoint.districtInstanceId;
                            });
                        const auto landmark = std::find_if(
                            raid.spatialLayout.landmarks.begin(),
                            raid.spatialLayout.landmarks.end(),
                            [&](const RaidLandmarkPlacementSnapshot &value)
                            {
                                return value.definitionId ==
                                    resourcePoint.landmarkDefinitionId;
                            });
                        const bool districtAllowed =
                            definition != raidMap->proceduralOutdoor
                                .resourcePointArchetypes.end() &&
                            district != raid.spatialLayout.districts.end() &&
                            (definition->allowedDistrictKinds.empty() ||
                             std::find(
                                 definition->allowedDistrictKinds.begin(),
                                 definition->allowedDistrictKinds.end(),
                                 district->kind) !=
                                 definition->allowedDistrictKinds.end());
                        const bool landmarkBindingValid =
                            definition != raidMap->proceduralOutdoor
                                .resourcePointArchetypes.end() &&
                            (definition->landmarkDefinitionId.empty()
                                 ? landmark ==
                                       raid.spatialLayout.landmarks.end()
                                 : landmark !=
                                           raid.spatialLayout.landmarks.end() &&
                                       landmark->districtInstanceId ==
                                           resourcePoint.districtInstanceId);
                        districtSnapshotValid = districtSnapshotValid &&
                            !resourcePoint.instanceId.empty() &&
                            resourcePointIds.insert(
                                resourcePoint.instanceId).second &&
                            definition != raidMap->proceduralOutdoor
                                .resourcePointArchetypes.end() &&
                            resourcePoint.displayName == definition->displayName &&
                            resourcePoint.kind == definition->kind &&
                            resourcePoint.lootTableId == definition->lootTableId &&
                            resourcePoint.riskTier == definition->riskTier &&
                            resourcePoint.capacity == definition->capacity &&
                            resourcePoint.capacity > 0U &&
                            anchor != nullptr &&
                            anchor->kind == RaidMapAnchorKind::ResourcePoint &&
                            resourcePoint.bounds == anchor->bounds &&
                            resourcePoint.districtInstanceId ==
                                anchor->districtInstanceId &&
                            districtAllowed &&
                            resourcePoint.landmarkDefinitionId ==
                                definition->landmarkDefinitionId &&
                            landmarkBindingValid &&
                            insideWalkable(resourcePoint.bounds);
                        resourceCapacity += resourcePoint.capacity;
                    }
                    for (const RaidResourcePointArchetypeDefinition &definition :
                         raidMap->proceduralOutdoor.resourcePointArchetypes)
                    {
                        const std::size_t count = std::count_if(
                            raid.spatialLayout.resourcePoints.begin(),
                            raid.spatialLayout.resourcePoints.end(),
                            [&](const RaidResourcePointSnapshot &value)
                            { return value.definitionId == definition.id; });
                        districtSnapshotValid = districtSnapshotValid &&
                            count >= definition.minimumInstances &&
                            count <= definition.maximumInstances;
                    }
                    districtSnapshotValid = districtSnapshotValid &&
                        resourceCapacity == outdoorRegularLootCount;
                }
                else
                {
                    districtSnapshotValid = districtSnapshotValid &&
                        raid.spatialLayout.resourcePoints.empty();
                }
                if (raid.rescue.has_value())
                    districtSnapshotValid = districtSnapshotValid &&
                        matchesRect(kRaidAnchorRescue,
                                    RaidMapAnchorKind::Rescue,
                                    raid.rescue->transferPoint);
                for (std::size_t index{}; index < raid.enemies.size(); ++index)
                {
                    const RaidEnemySnapshot &enemy = raid.enemies[index];
                    if (enemy.spaceId == outdoorRaidSpaceId())
                        districtSnapshotValid = districtSnapshotValid &&
                            matchesPoint(raidIndexedAnchorId("enemy", index),
                                         RaidMapAnchorKind::Enemy,
                                         enemy.position, false);
                }
                for (std::size_t index{}; index < raid.loot.size(); ++index)
                {
                    const RaidLootSnapshot &loot = raid.loot[index];
                    if (loot.spaceId == outdoorRaidSpaceId() &&
                        !selfRecoveryRootIds.contains(loot.assetId))
                    {
                        if (loot.requiresHighRisk)
                        {
                            const auto *resource = findRaidAnchorPlacement(
                                raid.spatialLayout,
                                kRaidAnchorAdvancedResource);
                            const std::size_t ordinal = loot.slotIndex -
                                (resourceEcologyRules
                                     ? expectedResourceLootCount
                                     : raidMap->raidLootSlots.size());
                            const std::size_t count = raidMap->highRisk
                                .advancedLootSlots.size();
                            districtSnapshotValid = districtSnapshotValid &&
                                resource != nullptr &&
                                loot.position.x ==
                                    resource->bounds.position.x +
                                        resource->bounds.size.x *
                                            static_cast<float>(ordinal + 1U) /
                                            static_cast<float>(count + 1U) &&
                                loot.position.y ==
                                    resource->bounds.position.y +
                                        resource->bounds.size.y * 0.5F;
                        }
                        else if (!resourceEcologyRules)
                            districtSnapshotValid = districtSnapshotValid &&
                                matchesPoint(
                                    raidIndexedAnchorId("loot", index),
                                    RaidMapAnchorKind::Loot,
                                    loot.position, true);
                        else
                        {
                            const auto point = std::find_if(
                                raid.spatialLayout.resourcePoints.begin(),
                                raid.spatialLayout.resourcePoints.end(),
                                [&](const RaidResourcePointSnapshot &value)
                                {
                                    return value.instanceId ==
                                        loot.resourcePointInstanceId;
                                });
                            const Vec2 expected = point ==
                                    raid.spatialLayout.resourcePoints.end()
                                ? Vec2{}
                                : raidResourcePointLootPosition(
                                      *point,
                                      loot.resourcePointSlotIndex);
                            districtSnapshotValid = districtSnapshotValid &&
                                point != raid.spatialLayout.resourcePoints.end() &&
                                loot.resourcePointSlotIndex < point->capacity &&
                                loot.position.x == expected.x &&
                                loot.position.y == expected.y;
                        }
                    }
                }
                for (std::size_t index{}; index < raid.interiors.size(); ++index)
                    districtSnapshotValid = districtSnapshotValid &&
                        matchesRect(raidIndexedAnchorId("interior", index),
                                    RaidMapAnchorKind::InteriorEntrance,
                                    raid.interiors[index].exteriorEntrance);
                const std::size_t expectedAnchorCount = 6U +
                    static_cast<std::size_t>(raid.rescue.has_value()) +
                    static_cast<std::size_t>(raid.selfRecovery.has_value()) +
                    outdoorEnemyCount +
                    (resourceEcologyRules
                         ? raid.spatialLayout.resourcePoints.size()
                         : outdoorRegularLootCount) +
                    raidMap->highRisk.pressureSpawns.size() +
                    raid.interiors.size();
                districtSnapshotValid = districtSnapshotValid &&
                    raid.spatialLayout.anchorPlacements.size() ==
                        expectedAnchorCount;
            }
            if (raid.spatialLayout.layoutHash == 0U ||
                raid.spatialLayout.layoutHash != expectedLayoutHash)
                return {false, "pending Raid spatial layout hash is invalid"};
            if (!layoutVersionValid)
                return {false, "pending Raid spatial layout version is invalid"};
            if (!fallbackStateValid)
                return {false, "pending Raid spatial fallback is invalid"};
            if (!generatedCountValid || !fixedLayoutValid)
                return {false, "pending Raid spatial blocker set is invalid"};
            if (!districtSnapshotValid)
                return {false, "pending Raid district snapshot is invalid"};
            if ((procedural.enabled &&
                 (raid.spatialLayout.generationAttempt == 0U ||
                  raid.spatialLayout.generationAttempt >
                      procedural.maximumAttempts)) ||
                (!procedural.enabled &&
                 (raid.spatialLayout.generationAttempt != 0U ||
                  raid.spatialLayout.usedFallback)))
                return {false, "pending Raid generation attempt is invalid"};
            if (std::any_of(
                    raid.spatialLayout.ballisticBlockers.begin(),
                    raid.spatialLayout.ballisticBlockers.end(),
                    [&insideWalkable](ContentRect blocker)
                    { return !insideWalkable(blocker); }))
                return {false, "pending Raid spatial blocker bounds are invalid"};
            if (procedural.enabled && !legacyPlayableOutdoorRules &&
                !raidMapLayoutConnectsAnchors(
                    *raidMap, raid.spatialLayout, anchors))
                return {false, "pending Raid spatial connectivity is invalid"};
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
                const RaidAnchorPlacementSnapshot *districtPortal =
                    districtLayoutRules
                        ? findRaidAnchorPlacement(
                              raid.spatialLayout,
                              raidIndexedAnchorId("interior", index))
                        : nullptr;
                const bool portalMatches = districtLayoutRules
                    ? districtPortal != nullptr &&
                          districtPortal->bounds == snapshot.exteriorEntrance &&
                          std::isfinite(snapshot.exteriorReturn.x) &&
                          std::isfinite(snapshot.exteriorReturn.y) &&
                          snapshot.exteriorReturn.x >= 0.0F &&
                          snapshot.exteriorReturn.y >= 0.0F &&
                          snapshot.exteriorReturn.x <= raidMap->worldSize.x &&
                          snapshot.exteriorReturn.y <= raidMap->worldSize.y
                    : specialLocationRules
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
                    (buildingIntelligenceRules &&
                     snapshot.layoutKnown !=
                         profile.raidInteriorIntelligence.knows(snapshot.id)) ||
                    (!buildingIntelligenceRules && snapshot.layoutKnown) ||
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
            for (const auto &[definitionId, placement] :
                 raid.travel.startingBaseConstruction.facilities)
            {
                try
                {
                    static_cast<void>(content.baseFacility(definitionId));
                }
                catch (...)
                {
                    startingConstructionValid = false;
                }
                startingConstructionValid = startingConstructionValid &&
                    (placement ==
                         BaseConstructionState::FacilityPlacement::Installed ||
                     placement ==
                         BaseConstructionState::FacilityPlacement::Reserve);
                const auto reserveStarted = raid.travel
                    .startingBaseConstruction
                    .facilityReserveStartedWorldMinutes.find(definitionId);
                startingConstructionValid = startingConstructionValid &&
                    ((placement ==
                          BaseConstructionState::FacilityPlacement::Reserve) ==
                     (reserveStarted != raid.travel.startingBaseConstruction
                         .facilityReserveStartedWorldMinutes.end())) &&
                    (reserveStarted == raid.travel.startingBaseConstruction
                         .facilityReserveStartedWorldMinutes.end() ||
                     reserveStarted->second <= raid.travel.startingWorldClock
                         .elapsedWorldMinutes);
            }
            for (const auto &[definitionId, reserveStarted] : raid.travel
                     .startingBaseConstruction
                     .facilityReserveStartedWorldMinutes)
            {
                static_cast<void>(reserveStarted);
                startingConstructionValid = startingConstructionValid &&
                    raid.travel.startingBaseConstruction.facilities.contains(
                        definitionId);
            }
            const auto startingOwnsFacility = [&](std::string_view value)
            {
                return raid.travel.startingBaseConstruction.facilities.contains(
                    BaseFacilityDefinitionId{std::string{value}});
            };
            startingConstructionValid = startingConstructionValid &&
                startingOwnsFacility("base_facility.warehouse") &&
                (raid.travel.startingBaseConstruction.dormitoryLevel > 0U) ==
                    startingOwnsFacility("base_facility.dormitory") &&
                (raid.travel.startingBaseConstruction.kitchenWaterLevel > 0U) ==
                    startingOwnsFacility("base_facility.kitchen_water") &&
                (raid.travel.startingBaseConstruction.workshopLevel > 0U) ==
                    startingOwnsFacility("base_facility.workshop") &&
                (raid.travel.startingBaseConstruction.medicalLevel > 0U) ==
                    startingOwnsFacility("base_facility.medical");
            startingConstructionValid = startingConstructionValid &&
                publishedBaseFacilityLevel(
                    raid.travel.startingBaseConstruction,
                    BaseFacilityUpgradeTarget::Dormitory,
                    content) &&
                publishedBaseFacilityLevel(
                    raid.travel.startingBaseConstruction,
                    BaseFacilityUpgradeTarget::KitchenWater,
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
                    std::uint64_t projectProgressWorldMinute =
                        raid.travel.startingWorldClock.elapsedWorldMinutes;
                    const BaseFacilityDefinitionId facilityId =
                        baseFacilityDefinitionId(definition.target);
                    const auto reserveStarted = raid.travel
                        .startingBaseConstruction
                        .facilityReserveStartedWorldMinutes.find(facilityId);
                    if (reserveStarted != raid.travel.startingBaseConstruction
                            .facilityReserveStartedWorldMinutes.end())
                    {
                        projectProgressWorldMinute = reserveStarted->second;
                    }
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
                            projectProgressWorldMinute &&
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
            bool regionalTravelValid = true;
            if (raid.rulesVersion == "regional-route-network-17" ||
                raid.rulesVersion == "regional-outpost-restoration-18" ||
                raid.rulesVersion == "regional-base-site-clearance-19" ||
                raid.rulesVersion == "regional-base-perimeter-sweep-20" ||
                playableOutdoorRules)
            {
                ProfileState startingRouteProfile = profile;
                startingRouteProfile.regionalOperations =
                    raid.travel.startingRegionalOperations;
                const RegionalRoutePlan route = queryRegionalRoute(
                    startingRouteProfile, content, raid.mapDefinitionId);
                regionalTravelValid =
                    route.reachable &&
                    route.routeIds == raid.travel.routeIds &&
                    route.travelMinutes == raid.travel.outboundMinutes &&
                    route.travelMinutes == raid.travel.returnMinutes &&
                    route.travelMinutes <=
                        std::numeric_limits<std::uint32_t>::max() / 2U &&
                    route.travelMinutes * 2U ==
                        raid.travel.failureRegroupMinutes &&
                    raid.travel.startingRegionalOperations ==
                        profile.regionalOperations;
            }
            else
            {
                regionalTravelValid = raid.travel.routeIds.empty();
            }
            const BaseSiegeState &startingSiege =
                raid.travel.startingBaseSiege;
            const bool startingSiegeValid =
                startingSiege.raidThreatUnits <=
                    kBaseSiegeThreatThreshold &&
                startingSiege.populationThreatUnits <=
                    kBaseSiegeThreatThreshold &&
                startingSiege.siteThreatUnits <=
                    kBaseSiegeThreatThreshold &&
                static_cast<std::uint64_t>(
                    startingSiege.raidThreatUnits) +
                        startingSiege.populationThreatUnits +
                        startingSiege.siteThreatUnits <=
                    kBaseSiegeThreatThreshold &&
                startingSiege.resolvedDayCount <= startingCompletedDays &&
                !startingSiege.warningActive &&
                startingSiege.warningRemainingSeconds == 0U;
            if (!startingSiegeValid)
            {
                return {false,
                        "pending Raid starting Base siege snapshot is invalid"};
            }
            if (!regionalTravelValid)
            {
                return {false,
                        "pending Raid regional travel snapshot is invalid"};
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
        for (std::size_t lootIndex{};
             lootIndex < raid.loot.size(); ++lootIndex)
        {
            const RaidLootSnapshot &loot = raid.loot[lootIndex];
            const AssetRecord *asset = profile.assets.find(loot.assetId);
            const std::size_t regularSlotCount = resourceEcologyRules
                ? expectedResourceLootCount
                : raidMap->raidLootSlots.size();
            const std::size_t advancedSlotCount =
                raidMap->highRisk.advancedLootSlots.size();
            bool validSlot{};
            bool validPosition{};
            const RaidSelfRecoveryRootSnapshot *recoveryRoot{};
            if (raid.selfRecovery.has_value())
            {
                const auto found = std::find_if(
                    raid.selfRecovery->roots.begin(),
                    raid.selfRecovery->roots.end(),
                    [&](const RaidSelfRecoveryRootSnapshot &root)
                    { return root.assetId == loot.assetId; });
                if (found != raid.selfRecovery->roots.end())
                {
                    recoveryRoot = &*found;
                }
            }
            if (recoveryRoot != nullptr)
            {
                validSlot = loot.slotIndex == recoveryRoot->lootSlotIndex;
                validPosition = loot.position.x == recoveryRoot->position.x &&
                    loot.position.y == recoveryRoot->position.y &&
                    !loot.requiresHighRisk &&
                    loot.spaceId == outdoorRaidSpaceId();
            }
            else if (loot.spaceId == outdoorRaidSpaceId())
            {
                validSlot = loot.requiresHighRisk
                    ? loot.slotIndex >= regularSlotCount &&
                          loot.slotIndex < regularSlotCount + advancedSlotCount
                    : loot.slotIndex < regularSlotCount;
                if (validSlot)
                {
                    const RaidAnchorPlacementSnapshot *anchor =
                        districtLayoutRules && !resourceEcologyRules
                            ? findRaidAnchorPlacement(
                                  raid.spatialLayout,
                                  raidIndexedAnchorId("loot", lootIndex))
                            : nullptr;
                    if (districtLayoutRules)
                    {
                        if (loot.requiresHighRisk)
                        {
                            const auto *resource = findRaidAnchorPlacement(
                                raid.spatialLayout,
                                kRaidAnchorAdvancedResource);
                            const std::size_t ordinal = loot.slotIndex -
                                regularSlotCount;
                            validPosition = resource != nullptr &&
                                loot.position.x ==
                                    resource->bounds.position.x +
                                        resource->bounds.size.x *
                                            static_cast<float>(ordinal + 1U) /
                                            static_cast<float>(
                                                advancedSlotCount + 1U) &&
                                loot.position.y ==
                                    resource->bounds.position.y +
                                        resource->bounds.size.y * 0.5F;
                        }
                        else if (!resourceEcologyRules)
                            validPosition = anchor != nullptr &&
                                anchor->kind == RaidMapAnchorKind::Loot &&
                                loot.position.x ==
                                    anchor->bounds.position.x +
                                        anchor->bounds.size.x * 0.5F &&
                                loot.position.y ==
                                    anchor->bounds.position.y +
                                        anchor->bounds.size.y * 0.5F;
                        else
                        {
                            const auto point = std::find_if(
                                raid.spatialLayout.resourcePoints.begin(),
                                raid.spatialLayout.resourcePoints.end(),
                                [&](const RaidResourcePointSnapshot &value)
                                {
                                    return value.instanceId ==
                                        loot.resourcePointInstanceId;
                                });
                            if (point != raid.spatialLayout.resourcePoints.end() &&
                                loot.resourcePointSlotIndex < point->capacity)
                            {
                                const Vec2 expected =
                                    raidResourcePointLootPosition(
                                        *point,
                                        loot.resourcePointSlotIndex);
                                std::size_t pointBase{};
                                for (const RaidResourcePointSnapshot &value :
                                     raid.spatialLayout.resourcePoints)
                                {
                                    if (value.instanceId == point->instanceId)
                                        break;
                                    pointBase += value.capacity;
                                }
                                validSlot = loot.slotIndex == pointBase +
                                    loot.resourcePointSlotIndex;
                                validPosition = loot.position.x == expected.x &&
                                    loot.position.y == expected.y;
                            }
                        }
                    }
                    else
                    {
                        const Vec2 expected = loot.requiresHighRisk
                            ? raidMap->highRisk.advancedLootSlots[
                                  loot.slotIndex - regularSlotCount].position
                            : raidMap->raidLootSlots[loot.slotIndex].position;
                        validPosition = loot.position.x == expected.x &&
                            loot.position.y == expected.y;
                    }
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
            if (resourceEcologyRules &&
                loot.spaceId == outdoorRaidSpaceId() &&
                !loot.requiresHighRisk && recoveryRoot == nullptr)
            {
                const auto point = std::find_if(
                    raid.spatialLayout.resourcePoints.begin(),
                    raid.spatialLayout.resourcePoints.end(),
                    [&](const RaidResourcePointSnapshot &value)
                    {
                        return value.instanceId ==
                            loot.resourcePointInstanceId;
                    });
                if (point == raid.spatialLayout.resourcePoints.end())
                    return {false, "pending Raid Loot resource point is invalid"};
                const LootTableDefinition &table = content.lootTable(
                    point->lootTableId);
                const auto entry = std::find_if(
                    table.entries.begin(), table.entries.end(),
                    [&](const LootContentEntry &value)
                    { return value.itemDefinitionId == loot.definitionId; });
                if (entry == table.entries.end() ||
                    loot.quantity < entry->minimumQuantity ||
                    loot.quantity > entry->maximumQuantity)
                    return {false, "pending Raid Loot table result is invalid"};
            }
            else if (!loot.resourcePointInstanceId.empty() ||
                     loot.resourcePointSlotIndex != 0U)
            {
                return {false, "legacy Raid Loot has resource point metadata"};
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
                [&](const RaidEnemySnapshot &snapshot,
                    const EnemySpawnDefinition &definition)
                {
                    return districtLayoutRules
                        ? snapshot.size.x == definition.size.x &&
                              snapshot.size.y == definition.size.y &&
                              snapshot.maximumHealth ==
                                  definition.maximumHealth
                        : enemyMatches(snapshot, definition);
                }))
        {
            return {false, "pending Raid outdoor enemies do not match deployment"};
        }
        for (std::size_t index{}; index < expectedInteriorCount; ++index)
        {
            const RaidInteriorDefinition &interior = raidMap->interiors[index];
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
         profile.lastRaidResult->baseThreatReducedUnits >
             kBaseSiegeThreatThreshold ||
         profile.lastRaidResult->rescuedOrdinaryResidents > 16U ||
         profile.lastRaidResult->rescuedInjuredResidents >
             profile.lastRaidResult->rescuedOrdinaryResidents ||
         (profile.lastRaidResult->lostRaidRecordId.has_value() &&
          (!profile.lostRaidRecords.contains(
               *profile.lastRaidResult->lostRaidRecordId) ||
           (profile.lastRaidResult->outcome != RaidResultOutcome::PlayerDead &&
            profile.lastRaidResult->outcome != RaidResultOutcome::ActiveQuit)))))
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
    for (const RaidSpaceDefinitionId &interiorId :
         profile.raidInteriorIntelligence.knownLayouts)
    {
        hashBytes(hash, interiorId.value());
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
    hashBytes(hash, profile.regionalOperations.activeBaseNodeId.value());
    hashBytes(hash, profile.regionalOperations.technologyCore.instanceId);
    hashBytes(
        hash,
        profile.regionalOperations.technologyCore.baseSiteDefinitionId.value());
    for (const auto &[siteId, state] :
         profile.regionalOperations.baseSites)
    {
        hashBytes(hash, siteId.value());
        hashInteger(hash, state.unlocked ? 1U : 0U);
        hashInteger(hash, state.uniqueFeatureRepaired ? 1U : 0U);
    }
    for (const auto &[outpostId, state] :
         profile.regionalOperations.outposts)
    {
        hashBytes(hash, outpostId.value());
        hashInteger(hash, state.unlocked ? 1U : 0U);
        hashInteger(hash, state.established ? 1U : 0U);
        hashInteger(hash, state.disrupted ? 1U : 0U);
        for (std::uint32_t count : state.assignedStaff)
        {
            hashInteger(hash, count);
        }
        hashInteger(hash, state.shortcutOperationsSinceRestoration);
    }
    hashInteger(hash, profile.baseSiege.raidThreatUnits);
    hashInteger(hash, profile.baseSiege.populationThreatUnits);
    hashInteger(hash, profile.baseSiege.siteThreatUnits);
    hashInteger(hash, profile.baseSiege.resolvedDayCount);
    hashInteger(hash, profile.baseSiege.safeUntilWorldMinute);
    hashInteger(hash, profile.baseSiege.warningActive ? 1U : 0U);
    hashInteger(hash, profile.baseSiege.warningRemainingSeconds);
    hashInteger(hash, profile.baseSiege.siegeSequence);
    hashInteger(hash, profile.baseSiege.autoDefensePresetSaved ? 1U : 0U);
    hashInteger(hash, static_cast<std::uint32_t>(
        profile.baseSiege.lastOutcome));
    hashInteger(hash, profile.baseSiege.lastSecuritySpent);
    hashInteger(hash, profile.baseSiege.lastPopulationLost);
    hashInteger(hash, profile.baseConstruction.materialUnits);
    hashInteger(hash, profile.baseConstruction.dormitoryLevel);
    hashInteger(hash, profile.baseConstruction.kitchenWaterLevel);
    hashInteger(hash, profile.baseConstruction.workshopLevel);
    hashInteger(hash, profile.baseConstruction.medicalLevel);
    for (const auto &[definitionId, placement] :
         profile.baseConstruction.facilities)
    {
        hashBytes(hash, definitionId.value());
        hashInteger(hash, static_cast<std::uint32_t>(placement));
    }
    for (const auto &[definitionId, reserveStarted] :
         profile.baseConstruction.facilityReserveStartedWorldMinutes)
    {
        hashBytes(hash, definitionId.value());
        hashInteger(hash, reserveStarted);
    }
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
    for (const auto &[recordId, record] : profile.lostRaidRecords)
    {
        hashBytes(hash, recordId);
        hashBytes(hash, record.raidId);
        hashBytes(hash, record.settlementId);
        hashBytes(hash, record.mapDefinitionId.value());
        hashBytes(hash, record.difficulty);
        hashInteger(hash, static_cast<std::uint32_t>(record.outcome));
        hashInteger(hash, record.createdWorldMinute);
        hashInteger(hash, record.subsequentRaidSettlementCount);
    }
    hashInteger(hash, profile.nextRecoveryTaskId);
    if (profile.recoveryTask.has_value())
    {
        const RecoveryTask &task = *profile.recoveryTask;
        hashInteger(hash, task.taskId);
        hashBytes(hash, task.sourceRecord.recordId);
        hashBytes(hash, task.sourceRecord.raidId);
        hashBytes(hash, task.sourceRecord.settlementId);
        hashBytes(hash, task.sourceRecord.mapDefinitionId.value());
        hashBytes(hash, task.sourceRecord.difficulty);
        hashInteger(hash, static_cast<std::uint32_t>(
            task.sourceRecord.outcome));
        hashInteger(hash, task.sourceRecord.createdWorldMinute);
        hashInteger(hash,
            task.sourceRecord.subsequentRaidSettlementCount);
        hashInteger(hash, task.paidCurrency);
        hashInteger(hash, task.startedWorldMinute);
        hashInteger(hash, task.completionWorldMinute);
        hashInteger(hash, task.readyForCollection ? 1U : 0U);
        for (AssetInstanceId assetId : task.recoveredAssetIds)
        {
            hashInteger(hash, assetId);
        }
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
        else if (const auto *service =
                     std::get_if<BaseServiceAssetLocation>(&asset.location))
        {
            hashInteger(hash, 4U);
            hashInteger(hash, service->jobId);
        }
        else if (const auto *lost =
                     std::get_if<LostRaidAssetLocation>(&asset.location))
        {
            hashInteger(hash, 5U);
            hashBytes(hash, lost->recordId);
            hashInteger(hash, static_cast<std::uint32_t>(lost->sourceSlot));
        }
        else
        {
            const auto &task =
                std::get<RecoveryTaskAssetLocation>(asset.location);
            hashInteger(hash, 6U);
            hashInteger(hash, task.taskId);
            hashInteger(hash, static_cast<std::uint32_t>(task.sourceSlot));
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
        hashInteger(hash, raid.spatialLayout.layoutVersion);
        hashInteger(hash, raid.spatialLayout.generationAttempt);
        hashInteger(hash, raid.spatialLayout.layoutHash);
        hashInteger(hash, raid.spatialLayout.usedFallback ? 1U : 0U);
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.spatialLayout.fallbackReason));
        for (const RaidDistrictSnapshot &district :
             raid.spatialLayout.districts)
        {
            hashInteger(hash, district.instanceId);
            hashBytes(hash, district.definitionId);
            hashBytes(hash, district.displayName);
            hashInteger(hash, static_cast<std::uint32_t>(district.kind));
            hashFloat(hash, district.labelPosition.x);
            hashFloat(hash, district.labelPosition.y);
            for (const RaidGridSpan &span : district.cells)
            {
                hashInteger(hash, span.row);
                hashInteger(hash, span.firstColumn);
                hashInteger(hash, span.length);
            }
        }
        for (const RaidTerrainSpan &span : raid.spatialLayout.terrainSpans)
        {
            hashInteger(hash, span.row);
            hashInteger(hash, span.firstColumn);
            hashInteger(hash, span.length);
            hashInteger(hash, static_cast<std::uint32_t>(span.kind));
        }
        for (const RaidOutdoorRoadCell &road : raid.spatialLayout.roadCells)
        {
            hashInteger(hash, road.column);
            hashInteger(hash, road.row);
            hashInteger(hash, static_cast<std::uint32_t>(road.kind));
        }
        for (const ContentRect &blocker :
             raid.spatialLayout.ballisticBlockers)
        {
            hashFloat(hash, blocker.position.x);
            hashFloat(hash, blocker.position.y);
            hashFloat(hash, blocker.size.x);
            hashFloat(hash, blocker.size.y);
        }
        for (const RaidOutdoorPropSnapshot &prop : raid.spatialLayout.props)
        {
            hashInteger(hash, prop.instanceId);
            hashInteger(hash, static_cast<std::uint32_t>(prop.kind));
            hashInteger(hash, static_cast<std::uint32_t>(prop.state));
            hashFloat(hash, prop.bounds.position.x);
            hashFloat(hash, prop.bounds.position.y);
            hashFloat(hash, prop.bounds.size.x);
            hashFloat(hash, prop.bounds.size.y);
            hashInteger(hash, prop.quarterTurns);
            hashInteger(hash, prop.collidable ? 1U : 0U);
        }
        for (const RaidAnchorPlacementSnapshot &placement :
             raid.spatialLayout.anchorPlacements)
        {
            hashBytes(hash, placement.id);
            hashInteger(hash, static_cast<std::uint32_t>(placement.kind));
            hashFloat(hash, placement.bounds.position.x);
            hashFloat(hash, placement.bounds.position.y);
            hashFloat(hash, placement.bounds.size.x);
            hashFloat(hash, placement.bounds.size.y);
            hashInteger(hash, placement.districtInstanceId);
        }
        for (const RaidLandmarkPlacementSnapshot &landmark :
             raid.spatialLayout.landmarks)
        {
            hashBytes(hash, landmark.definitionId);
            hashBytes(hash, landmark.displayName);
            hashFloat(hash, landmark.bounds.position.x);
            hashFloat(hash, landmark.bounds.position.y);
            hashFloat(hash, landmark.bounds.size.x);
            hashFloat(hash, landmark.bounds.size.y);
            hashInteger(hash, landmark.districtInstanceId);
            for (const ContentRect &structure : landmark.structures)
            {
                hashFloat(hash, structure.position.x);
                hashFloat(hash, structure.position.y);
                hashFloat(hash, structure.size.x);
                hashFloat(hash, structure.size.y);
            }
            for (Vec2 socket : landmark.roadSockets)
            {
                hashFloat(hash, socket.x);
                hashFloat(hash, socket.y);
            }
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
            hashBytes(hash, loot.resourcePointInstanceId);
            hashInteger(hash, loot.resourcePointSlotIndex);
        }
        for (const RaidInteriorSnapshot &interior : raid.interiors)
        {
            hashBytes(hash, interior.id.value());
            hashBytes(hash, interior.displayName);
            hashInteger(hash, interior.layoutKnown ? 1U : 0U);
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
        if (raid.selfRecovery.has_value())
        {
            const RaidSelfRecoverySnapshot &recovery = *raid.selfRecovery;
            hashBytes(hash, recovery.sourceRecord.recordId);
            hashBytes(hash, recovery.sourceRecord.raidId);
            hashBytes(hash, recovery.sourceRecord.settlementId);
            hashBytes(hash, recovery.sourceRecord.mapDefinitionId.value());
            hashBytes(hash, recovery.sourceRecord.difficulty);
            hashInteger(hash, static_cast<std::uint32_t>(
                recovery.sourceRecord.outcome));
            hashInteger(hash, recovery.sourceRecord.createdWorldMinute);
            hashInteger(hash,
                recovery.sourceRecord.subsequentRaidSettlementCount);
            hashFloat(hash, recovery.cachePosition.x);
            hashFloat(hash, recovery.cachePosition.y);
            hashFloat(hash, recovery.interactionDurationSeconds);
            hashInteger(hash, recovery.opened ? 1U : 0U);
            for (const RaidSelfRecoveryRootSnapshot &root : recovery.roots)
            {
                hashInteger(hash, root.assetId);
                hashInteger(hash, static_cast<std::uint32_t>(root.sourceSlot));
                hashInteger(hash, root.lootSlotIndex);
                hashFloat(hash, root.position.x);
                hashFloat(hash, root.position.y);
            }
        }
        if (raid.outpostRestoration.has_value())
        {
            hashBytes(
                hash,
                raid.outpostRestoration->outpostDefinitionId.value());
            hashInteger(
                hash,
                raid.outpostRestoration->objectiveSecured ? 1U : 0U);
        }
        if (raid.baseSiteClearance.has_value())
        {
            hashBytes(
                hash,
                raid.baseSiteClearance->baseSiteDefinitionId.value());
            hashInteger(
                hash,
                raid.baseSiteClearance->objectiveSecured ? 1U : 0U);
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
        for (const RegionRouteDefinitionId &routeId :
             raid.travel.routeIds)
        {
            hashBytes(hash, routeId.value());
        }
        hashBytes(
            hash,
            raid.travel.startingRegionalOperations.activeBaseNodeId.value());
        hashBytes(
            hash,
            raid.travel.startingRegionalOperations.technologyCore.instanceId);
        hashBytes(
            hash,
            raid.travel.startingRegionalOperations.technologyCore
                .baseSiteDefinitionId.value());
        for (const auto &[siteId, state] :
             raid.travel.startingRegionalOperations.baseSites)
        {
            hashBytes(hash, siteId.value());
            hashInteger(hash, state.unlocked ? 1U : 0U);
            hashInteger(hash, state.uniqueFeatureRepaired ? 1U : 0U);
        }
        for (const auto &[outpostId, state] :
             raid.travel.startingRegionalOperations.outposts)
        {
            hashBytes(hash, outpostId.value());
            hashInteger(hash, state.unlocked ? 1U : 0U);
            hashInteger(hash, state.established ? 1U : 0U);
            hashInteger(hash, state.disrupted ? 1U : 0U);
            for (std::uint32_t count : state.assignedStaff)
            {
                hashInteger(hash, count);
            }
            hashInteger(hash, state.shortcutOperationsSinceRestoration);
        }
        if (raid.basePerimeterSweep.has_value())
        {
            hashBytes(
                hash,
                raid.basePerimeterSweep->baseSiteDefinitionId.value());
            hashInteger(
                hash,
                raid.basePerimeterSweep->threatReductionUnits);
            hashInteger(
                hash,
                raid.basePerimeterSweep->objectiveSecured ? 1U : 0U);
        }
        hashInteger(hash, raid.travel.startingBaseSiege.raidThreatUnits);
        hashInteger(
            hash, raid.travel.startingBaseSiege.populationThreatUnits);
        hashInteger(hash, raid.travel.startingBaseSiege.siteThreatUnits);
        hashInteger(hash, raid.travel.startingBaseSiege.resolvedDayCount);
        hashInteger(hash, raid.travel.startingBaseSiege.safeUntilWorldMinute);
        hashInteger(
            hash, raid.travel.startingBaseSiege.warningActive ? 1U : 0U);
        hashInteger(
            hash, raid.travel.startingBaseSiege.warningRemainingSeconds);
        hashInteger(hash, raid.travel.startingBaseSiege.siegeSequence);
        hashInteger(
            hash,
            raid.travel.startingBaseSiege.autoDefensePresetSaved ? 1U : 0U);
        hashInteger(hash, static_cast<std::uint32_t>(
            raid.travel.startingBaseSiege.lastOutcome));
        hashInteger(hash, raid.travel.startingBaseSiege.lastSecuritySpent);
        hashInteger(hash, raid.travel.startingBaseSiege.lastPopulationLost);
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
                    raid.travel.startingBaseConstruction.kitchenWaterLevel);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.workshopLevel);
        hashInteger(hash,
                    raid.travel.startingBaseConstruction.medicalLevel);
        for (const auto &[definitionId, placement] :
             raid.travel.startingBaseConstruction.facilities)
        {
            hashBytes(hash, definitionId.value());
            hashInteger(hash, static_cast<std::uint32_t>(placement));
        }
        for (const auto &[definitionId, reserveStarted] : raid.travel
                 .startingBaseConstruction
                 .facilityReserveStartedWorldMinutes)
        {
            hashBytes(hash, definitionId.value());
            hashInteger(hash, reserveStarted);
        }
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
        hashBytes(hash,
                  profile.lastRaidResult->lostRaidRecordId.value_or(""));
        hashInteger(
            hash,
            profile.lastRaidResult->baseThreatReducedUnits);
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
