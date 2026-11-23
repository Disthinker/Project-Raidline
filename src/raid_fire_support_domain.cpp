#include "raid_fire_support_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "base_population_domain.h"
#include "base_workforce_domain.h"

namespace
{
const ItemDefinitionId kCorpseDefinitionId{"item.container.base_storage_crate"};
constexpr EquipmentSlotKind kRifleLossSlot = EquipmentSlotKind::PrimaryWeapon;
constexpr EquipmentSlotKind kMagazineLossSlot = EquipmentSlotKind::ChestRig;
constexpr std::size_t kCombatIndex = baseProfessionIndex(BaseResidentProfession::Combat);

const StoredAssetLocation *stashLocation(const AssetRecord &asset) noexcept
{
    const auto *stored = std::get_if<StoredAssetLocation>(&asset.location);
    return stored && stored->container == ProfileContainerId::stash() ? stored : nullptr;
}

bool lendableRifle(const AssetRegistry &assets, const AssetRecord &asset)
{
    if (asset.definitionId != kRaidFireSupportRifleId || asset.quantity != 1U ||
        !asset.currentDurability || asset.chamberedRound ||
        asset.weaponMalfunction != WeaponMalfunctionType::None)
        return false;
    return std::none_of(assets.records().begin(), assets.records().end(),
        [&](const auto &entry) {
            const auto *magazine = std::get_if<InstalledMagazineLocation>(&entry.second.location);
            return magazine && magazine->weaponAssetId == asset.instanceId;
        });
}

bool preparedMagazine(const ContentRegistry &content, const AssetRecord &asset) noexcept
{
    return asset.quantity == 1U && asset.magazineRounds.size() <= kRaidFireSupportPreparationRoundCount &&
        content.magazineFitsWeapon(asset.definitionId, kRaidFireSupportRifleId) &&
        std::all_of(asset.magazineRounds.begin(), asset.magazineRounds.end(),
            [](const MagazineRoundRecord &round) { return round.definitionId == kRaidFireSupportAmmunitionId; });
}

bool wasLoanedFromStash(const AssetRegistry &starting, const RaidFireSupportLoan &loan) noexcept
{
    const auto *asset = starting.find(loan.assetId);
    const auto *stored = asset ? stashLocation(*asset) : nullptr;
    return stored && stored->origin == loan.returnOrigin && asset->orientation == loan.originalOrientation;
}

bool ownedBy(const AssetRecord *asset, AssetInstanceId containerId) noexcept
{
    const auto *stored = asset ? std::get_if<StoredAssetLocation>(&asset->location) : nullptr;
    return stored && stored->container == ProfileContainerId::compartment(containerId, 0U);
}

const RaidCorpseContainerAssetLocation *corpseLocation(const ProfileState &profile) noexcept
{
    if (!profile.pendingRaid || !profile.pendingRaid->fireSupport)
        return nullptr;
    const auto *asset = profile.assets.find(profile.pendingRaid->fireSupport->containerAssetId);
    const auto *corpse = asset ? std::get_if<RaidCorpseContainerAssetLocation>(&asset->location) : nullptr;
    return corpse && corpse->sourceKind == RaidCorpseSourceKind::FireSupport ? corpse : nullptr;
}

bool validCorpsePosition(const ContentRegistry &content, const PendingRaidSnapshot &raid,
                        const RaidSpaceDefinitionId &space, Vec2 position)
{
    Vec2 bounds;
    if (space == outdoorRaidSpaceId())
        bounds = content.map(raid.mapDefinitionId).worldSize;
    else
    {
        const auto interior = std::find_if(raid.interiors.begin(), raid.interiors.end(),
            [&](const RaidInteriorSnapshot &room) { return room.id == space; });
        if (interior == raid.interiors.end())
            return false;
        bounds = interior->worldSize;
    }
    return std::isfinite(position.x) && std::isfinite(position.y) &&
        position.x >= 0.0F && position.y >= 0.0F && position.x <= bounds.x && position.y <= bounds.y;
}

std::string loadoutError(RaidFireSupportLoadoutFailure failure)
{
    switch (failure)
    {
    case RaidFireSupportLoadoutFailure::IneligibleMap: return "FIRE SUPPORT REQUIRES AN ELIGIBLE ORDINARY RAID";
    case RaidFireSupportLoadoutFailure::NoAvailableCombatResident: return "NO AVAILABLE COMBAT RESIDENT";
    case RaidFireSupportLoadoutFailure::InsufficientPreparationRounds: return "NEED 30 STANDARD 5.45 ROUNDS IN STASH";
    case RaidFireSupportLoadoutFailure::InvalidRifle: return "SELECT AN EMPTY SERVICE 5.45 RIFLE FROM STASH";
    case RaidFireSupportLoadoutFailure::InvalidSpareMagazine: return "SELECT A COMPATIBLE MAGAZINE WITH STANDARD 5.45 ROUNDS";
    case RaidFireSupportLoadoutFailure::None: return {};
    }
    return {};
}

bool replacementOriginMatches(const ProfileState &profile, std::string_view source,
                              std::uint64_t worldMinute) noexcept
{
    return profile.fireSupportReplacement && profile.fireSupportReplacement->deathSettlementId == source &&
        profile.fireSupportReplacement->readyWorldMinute <= worldMinute &&
        profile.basePopulation.professionResidents[kCombatIndex] == 0U &&
        profile.committedRescues.contains(kRaidFireSupportReplacementRescueId);
}

bool overlapped(GridPosition a, InventoryFootprint aSize, GridPosition b, InventoryFootprint bSize) noexcept
{
    return a.x < static_cast<std::int64_t>(b.x) + bSize.width &&
        b.x < static_cast<std::int64_t>(a.x) + aSize.width &&
        a.y < static_cast<std::int64_t>(b.y) + bSize.height &&
        b.y < static_cast<std::int64_t>(a.y) + aSize.height;
}
}

bool hasLivingRaidFireSupport(const ProfileState &profile) noexcept
{
    const auto *corpse = corpseLocation(profile);
    return corpse && !corpse->deathPosition;
}

bool isRaidFireSupportMap(const MapDefinitionId &map) noexcept
{
    return map == kRaidFireSupportMapId || map == kHospitalFloorplanMapId || map == kRaidFireSupportReplacementMapId;
}

RaidFireSupportLoadoutAvailability queryRaidFireSupportLoadout(
    const ProfileState &profile, const ContentRegistry &content, const MapDefinitionId &map,
    const std::optional<RaidFireSupportLoadout> &selected)
{
    RaidFireSupportLoadoutAvailability result;
    result.mapEligible = isRaidFireSupportMap(map) && supportsRaidFireSupportContent(content.contentVersion());
    if (!result.mapEligible)
    {
        result.failure = RaidFireSupportLoadoutFailure::IneligibleMap;
        return result;
    }

    const auto workforce = projectBaseWorkforce(profile);
    result.availableCombatResidents = std::min(workforce.availableResidents,
        workforce.availableByProfession[kCombatIndex]);
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (!stashLocation(asset))
            continue;
        if (asset.definitionId == kRaidFireSupportAmmunitionId)
            result.preparationRoundCount += std::min(asset.quantity,
                std::numeric_limits<std::uint32_t>::max() - result.preparationRoundCount);
        if (lendableRifle(profile.assets, asset))
            result.rifleAssetIds.push_back(id);
        if (preparedMagazine(content, asset))
            result.spareMagazines.push_back({id, static_cast<std::uint32_t>(asset.magazineRounds.size())});
    }
    if (!result.availableCombatResidents)
        result.failure = RaidFireSupportLoadoutFailure::NoAvailableCombatResident;
    else if (result.preparationRoundCount < kRaidFireSupportPreparationRoundCount)
        result.failure = RaidFireSupportLoadoutFailure::InsufficientPreparationRounds;
    else if (result.rifleAssetIds.empty() || (selected &&
        std::find(result.rifleAssetIds.begin(), result.rifleAssetIds.end(), selected->rifleAssetId) == result.rifleAssetIds.end()))
        result.failure = RaidFireSupportLoadoutFailure::InvalidRifle;
    else if (selected && selected->spareMagazineAssetId &&
        std::none_of(result.spareMagazines.begin(), result.spareMagazines.end(), [&](const auto &option) {
            return option.assetId == *selected->spareMagazineAssetId;
        }))
        result.failure = RaidFireSupportLoadoutFailure::InvalidSpareMagazine;
    return result;
}

ProfileValidationResult prepareRaidFireSupportInCandidate(
    ProfileState &candidate, const ContentRegistry &content, const RaidFireSupportLoadout &loadout)
{
    if (!candidate.pendingRaid || !candidate.pendingRaid->startingAssets || candidate.pendingRaid->fireSupport)
        return {false, "FIRE SUPPORT DEPLOYMENT BASELINE IS MISSING"};
    auto &raid = *candidate.pendingRaid;
    const auto available = queryRaidFireSupportLoadout(candidate, content, raid.mapDefinitionId, loadout);
    if (available.failure != RaidFireSupportLoadoutFailure::None)
        return {false, loadoutError(available.failure)};
    if (candidate.assets.nextAssetId() == std::numeric_limits<AssetInstanceId>::max())
        return {false, "FIRE SUPPORT ASSET ID SPACE IS EXHAUSTED"};

    RaidFireSupportSnapshot support;
    support.containerAssetId = candidate.assets.create(content.item(kCorpseDefinitionId),
        RaidCorpseContainerAssetLocation{raid.raidId, outdoorRaidSpaceId(),
            raidFireSupportCorpseSourceId(raid.raidId), std::nullopt, RaidCorpseSourceKind::FireSupport});
    const auto compartment = ProfileContainerId::compartment(support.containerAssetId, 0U);
    const auto lend = [&](AssetInstanceId id, RaidFireSupportLoan &loan) {
        auto *asset = candidate.assets.findMutable(id);
        const auto origin = findFirstProfileFit(candidate, content, compartment,
            content.item(asset->definitionId), asset->orientation, id);
        if (!origin)
            return false;
        loan = {id, stashLocation(*asset)->origin, asset->orientation};
        asset->location = StoredAssetLocation{compartment, *origin};
        return true;
    };
    if (!lend(loadout.rifleAssetId, support.rifle))
        return {false, "FIRE SUPPORT RIFLE DOES NOT FIT THE LOAN CONTAINER"};
    if (loadout.spareMagazineAssetId)
    {
        support.spareMagazine.emplace();
        if (!lend(*loadout.spareMagazineAssetId, *support.spareMagazine))
            return {false, "FIRE SUPPORT MAGAZINE DOES NOT FIT THE LOAN CONTAINER"};
    }

    std::vector<AssetInstanceId> spentStacks;
    unsigned remaining = kRaidFireSupportPreparationRoundCount;
    for (const auto &[id, asset] : candidate.assets.records())
    {
        if (!remaining)
            break;
        if (asset.definitionId != kRaidFireSupportAmmunitionId || !stashLocation(asset))
            continue;
        const auto take = std::min(asset.quantity, remaining);
        remaining -= take;
        if (take == asset.quantity)
            spentStacks.push_back(id);
        else
            candidate.assets.findMutable(id)->quantity -= take;
    }
    for (const auto id : spentStacks)
        static_cast<void>(candidate.assets.erase(id));
    raid.fireSupport = std::move(support);
    return {true, {}};
}

std::string raidFireSupportCorpseSourceId(std::string_view raidId)
{
    return std::string{raidId} + ":fire-support";
}

bool raidFireSupportReservedStashOverlap(const ProfileState &profile, const ContentRegistry &content,
    GridPosition origin, const ItemDefinition &definition, ItemOrientation orientation,
    std::optional<AssetInstanceId> ignoredAsset) noexcept
{
    if (!profile.pendingRaid || !profile.pendingRaid->fireSupport)
        return false;
    const auto &raid = *profile.pendingRaid;
    if (!raid.startingAssets)
        return true;
    const auto footprint = inventoryFootprint(definition, orientation);
    const auto intersects = [&](const RaidFireSupportLoan &loan) {
        if (ignoredAsset == loan.assetId)
            return false;
        const auto *original = raid.startingAssets->find(loan.assetId);
        return !original || overlapped(origin, footprint, loan.returnOrigin,
            inventoryFootprint(content.item(original->definitionId), loan.originalOrientation));
    };
    try
    {
        const auto &support = *raid.fireSupport;
        return intersects(support.rifle) || (support.spareMagazine && intersects(*support.spareMagazine));
    }
    catch (...)
    {
        return true;
    }
}

ProfileValidationResult validateRaidFireSupportStashReservations(const ProfileState &profile,
    const ContentRegistry &content, const AssetRegistry &assets)
{
    if (!profile.pendingRaid || !profile.pendingRaid->fireSupport)
        return {true, {}};
    for (const auto &[id, asset] : assets.records())
    {
        static_cast<void>(id);
        const auto *stored = stashLocation(asset);
        if (stored && raidFireSupportReservedStashOverlap(profile, content, stored->origin,
                content.item(asset.definitionId), asset.orientation))
            return {false, "FIRE SUPPORT RETURN SPACE IS RESERVED"};
    }
    return {true, {}};
}

ProfileValidationResult validateRaidFireSupportCorpse(const ProfileState &profile, const ContentRegistry &content)
{
    if (!profile.pendingRaid || !profile.pendingRaid->fireSupport)
        return {true, {}};
    const auto &raid = *profile.pendingRaid;
    const auto &support = *raid.fireSupport;
    const auto *root = profile.assets.find(support.containerAssetId);
    const auto *corpse = corpseLocation(profile);
    if (!root || !corpse || root->definitionId != kCorpseDefinitionId || root->quantity != 1U ||
        corpse->raidId != raid.raidId || (!corpse->deathPosition && corpse->spaceId != outdoorRaidSpaceId()) ||
        corpse->sourceEnemyId != raidFireSupportCorpseSourceId(raid.raidId) ||
        !isRaidFireSupportMap(raid.mapDefinitionId) ||
        !supportsRaidFireSupportContent(content.contentVersion()) || raid.selfRecovery ||
        raid.outpostRestoration || raid.baseSiteClearance || raid.basePerimeterSweep ||
        !raid.startingAssets || root->instanceId < raid.startingAssets->nextAssetId() ||
        (corpse->deathPosition && !validCorpsePosition(content, raid, corpse->spaceId, *corpse->deathPosition)))
        return {false, "fire support corpse ownership is invalid"};

    const auto &starting = *raid.startingAssets;
    if (!wasLoanedFromStash(starting, support.rifle) ||
        !lendableRifle(starting, *starting.find(support.rifle.assetId)))
        return {false, "fire support rifle loan origin is invalid"};
    if (support.spareMagazine && (!wasLoanedFromStash(starting, *support.spareMagazine) ||
        !preparedMagazine(content, *starting.find(support.spareMagazine->assetId))))
        return {false, "fire support magazine loan origin is invalid"};

    const auto *rifle = profile.assets.find(support.rifle.assetId);
    if (!corpse->deathPosition && !ownedBy(rifle, support.containerAssetId))
        return {false, "living fire support lost its rifle"};
    if (ownedBy(rifle, support.containerAssetId) &&
        (!lendableRifle(profile.assets, *rifle) || rifle->orientation != support.rifle.originalOrientation ||
            rifle->currentDurability != starting.find(rifle->instanceId)->currentDurability ||
            rifle->currentMaximumDurability != starting.find(rifle->instanceId)->currentMaximumDurability))
        return {false, "fire support rifle changed inside the loan container"};
    if (support.spareMagazine)
    {
        const auto *spare = profile.assets.find(support.spareMagazine->assetId);
        if (ownedBy(spare, support.containerAssetId) &&
            (!preparedMagazine(content, *spare) || spare->orientation != support.spareMagazine->originalOrientation ||
                spare->magazineRounds != starting.find(spare->instanceId)->magazineRounds))
            return {false, "fire support ammunition changed inside the loan container"};
    }
    for (const auto &[id, asset] : profile.assets.records())
        if (ownedBy(&asset, support.containerAssetId) && id != support.rifle.assetId &&
            (!support.spareMagazine || id != support.spareMagazine->assetId))
            return {false, "fire support owns an asset outside its frozen loans"};
    return validateRaidFireSupportStashReservations(profile, content, profile.assets);
}

bool revealRaidFireSupportCorpseInSimulation(ProfileState &profile, const ContentRegistry &content,
    const RaidSpaceDefinitionId &space, Vec2 position) noexcept
{
    try
    {
        const auto *corpse = corpseLocation(profile);
        if (!corpse)
            return false;
        if (corpse->deathPosition)
            return corpse->spaceId == space && corpse->deathPosition->x == position.x &&
                corpse->deathPosition->y == position.y;
        if (!validCorpsePosition(content, *profile.pendingRaid, space, position))
            return false;
        if (profile.revision == std::numeric_limits<ProfileRevision>::max() ||
            !validateRaidFireSupportCorpse(profile, content).valid)
            return false;
        auto *root = profile.assets.findMutable(profile.pendingRaid->fireSupport->containerAssetId);
        auto &location = std::get<RaidCorpseContainerAssetLocation>(root->location);
        location.spaceId = space;
        location.deathPosition = position;
        ++profile.revision;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

InventoryReceipt takeRaidFireSupportMagazine(ProfileState &profile, const ContentRegistry &content,
    const CommandContext &context)
{
    const auto fail = [&](DomainErrorCode code, const char *message) {
        return InventoryReceipt{false, false, code, message, profile.revision};
    };
    if (context.transactionId.empty())
        return fail(DomainErrorCode::InvalidTransaction, "FIRE SUPPORT TRANSACTION IS MISSING");
    if (profile.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision)
        return fail(DomainErrorCode::StaleRevision, "PROFILE REVISION IS STALE");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return fail(DomainErrorCode::RevisionOverflow, "PROFILE REVISION CANNOT ADVANCE");
    const auto *corpse = corpseLocation(profile);
    if (!corpse || corpse->deathPosition)
        return fail(DomainErrorCode::IllegalDestination, "FIRE SUPPORT IS NOT AVAILABLE FOR RESUPPLY");
    const auto &support = *profile.pendingRaid->fireSupport;
    const auto *spare = support.spareMagazine ? profile.assets.find(support.spareMagazine->assetId) : nullptr;
    if (!ownedBy(spare, support.containerAssetId))
        return fail(DomainErrorCode::MissingAsset, "FIRE SUPPORT HAS NO SPARE MAGAZINE");
    const auto pack = equippedAsset(profile, EquipmentSlotKind::Backpack);
    const auto destination = ProfileContainerId::compartment(pack.value_or(0U), 0U);
    const auto fit = pack ? findFirstProfileFit(profile, content, destination,
        content.item(spare->definitionId), spare->orientation, spare->instanceId) : std::nullopt;
    if (!fit)
        return fail(DomainErrorCode::Capacity, "MAKE ROOM IN YOUR BACKPACK FOR THE SPARE MAGAZINE");

    ProfileState candidate = profile;
    candidate.assets.findMutable(spare->instanceId)->location = StoredAssetLocation{destination, *fit};
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto result = validateProfileState(candidate, content);
    if (!result.valid)
        return {false, false, DomainErrorCode::InvalidProfile, result.message, profile.revision};
    profile = std::move(candidate);
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

bool isRaidFireSupportLossRoot(const ContentRegistry &content, const AssetRecord &asset,
                              EquipmentSlotKind sourceSlot) noexcept
{
    if (sourceSlot == kRifleLossSlot)
        return asset.definitionId == kRaidFireSupportRifleId && asset.quantity == 1U;
    return sourceSlot == kMagazineLossSlot && preparedMagazine(content, asset);
}

std::string raidFireSupportLostRecordId(std::string_view settlementId)
{
    return std::string{settlementId} + ":fire-support";
}

void forgetRaidFireSupportLostRecord(ProfileState &profile, std::string_view recordId) noexcept
{
    if (profile.lastRaidResult && profile.lastRaidResult->fireSupport)
    {
        auto &stored = profile.lastRaidResult->fireSupport->lostRecordId;
        if (stored && *stored == recordId)
            stored.reset();
    }
}

ProfileValidationResult settleRaidFireSupportLoansInCandidate(ProfileState &candidate,
    const ContentRegistry &content, RaidResultOutcome outcome, RaidFireSupportResult &result)
{
    const auto *corpse = corpseLocation(candidate);
    if (!corpse)
        return {false, "settlement has no fire support loan container"};
    const auto &raid = *candidate.pendingRaid;
    const auto &support = *raid.fireSupport;
    result.died = corpse->deathPosition.has_value();
    const auto lostId = result.died ? raidFireSupportLostRecordId(raid.settlementId) : std::string{};
    const auto settleLoan = [&](const RaidFireSupportLoan &loan, EquipmentSlotKind slot) {
        auto *asset = candidate.assets.findMutable(loan.assetId);
        if (!ownedBy(asset, support.containerAssetId))
            return true;
        if (result.died)
        {
            asset->location = LostRaidAssetLocation{lostId, slot};
            result.lostRecordId = lostId;
            return true;
        }
        if (!profilePlacementFits(candidate, content, ProfileContainerId::stash(), loan.returnOrigin,
                content.item(asset->definitionId), loan.originalOrientation, loan.assetId))
            return false;
        asset->location = StoredAssetLocation{ProfileContainerId::stash(), loan.returnOrigin};
        asset->orientation = loan.originalOrientation;
        ++result.returnedLoanCount;
        return true;
    };
    if (!settleLoan(support.rifle, kRifleLossSlot) ||
        (support.spareMagazine && !settleLoan(*support.spareMagazine, kMagazineLossSlot)))
        return {false, "fire support return cell is occupied"};
    if (result.lostRecordId && !candidate.lostRaidRecords.emplace(lostId, LostRaidRecord{
        lostId, raid.raidId, raid.settlementId, raid.mapDefinitionId,
        content.map(raid.mapDefinitionId).operationBriefing.difficulty, outcome,
        candidate.worldClock.elapsedWorldMinutes, 0U, LostRaidRecordKind::FireSupport}).second)
        return {false, "fire support loss record already exists"};
    return {true, {}};
}

ProfileValidationResult applyRaidFireSupportCasualtyInCandidate(ProfileState &candidate,
    std::string_view settlementId)
{
    auto &population = candidate.basePopulation;
    auto &combat = population.professionResidents[kCombatIndex];
    if (combat <= population.injuredByProfession[kCombatIndex] || !population.ordinaryResidents)
        return {false, "fire support casualty has no healthy resident"};
    if (combat == 1U)
    {
        const auto now = candidate.worldClock.elapsedWorldMinutes;
        if (now > std::numeric_limits<std::uint64_t>::max() - kRaidFireSupportReplacementDelayMinutes)
            return {false, "fire support replacement date cannot advance"};
        candidate.fireSupportReplacement = RaidFireSupportReplacement{
            std::string{settlementId}, now + kRaidFireSupportReplacementDelayMinutes};
    }
    --combat;
    --population.ordinaryResidents;
    return {true, {}};
}

std::optional<std::string> readyRaidFireSupportReplacement(const ProfileState &profile,
    const ContentRegistry &content, const MapDefinitionId &map)
{
    if (!supportsRaidFireSupportContent(content.contentVersion()) || map != kRaidFireSupportReplacementMapId ||
        !profile.fireSupportReplacement || !replacementOriginMatches(profile,
            profile.fireSupportReplacement->deathSettlementId, profile.worldClock.elapsedWorldMinutes))
        return std::nullopt;
    return profile.fireSupportReplacement->deathSettlementId;
}

std::string raidFireSupportReplacementTransactionId(std::string_view deathSettlementId)
{
    return "fire-support-replacement:" + std::string{deathSettlementId};
}

OrdinarySurvivorAdmissionPlan queryRaidFireSupportReplacementAdmission(const ProfileState &profile,
    std::string_view deathSettlementId)
{
    const auto &population = profile.basePopulation;
    OrdinarySurvivorAdmissionPlan plan;
    plan.revision = profile.revision;
    plan.residentsBefore = population.ordinaryResidents;
    plan.residentsAfter = population.ordinaryResidents;
    plan.injuredResidentsAfter = population.injuredResidents;
    plan.bedCapacity = population.bedCapacity;
    const auto minute = profile.pendingRaid ? profile.pendingRaid->travel.startingWorldClock.elapsedWorldMinutes :
        profile.worldClock.elapsedWorldMinutes;
    if (deathSettlementId.empty())
    {
        plan.error = RaidRescueError::InvalidCommand;
        plan.message = "FIRE SUPPORT REPLACEMENT TRANSACTION IS INVALID";
    }
    else if (profile.committedTransactions.contains(raidFireSupportReplacementTransactionId(deathSettlementId)))
    {
        plan.canCommit = true;
        plan.alreadyCommitted = true;
    }
    else if (!replacementOriginMatches(profile, deathSettlementId, minute))
    {
        plan.error = RaidRescueError::InvalidCommand;
        plan.message = "FIRE SUPPORT REPLACEMENT IS NOT AVAILABLE";
    }
    else if (profile.pendingRaid && (profile.pendingRaid->mapDefinitionId != kRaidFireSupportReplacementMapId ||
        profile.pendingRaid->fireSupportReplacementSource != deathSettlementId ||
        !profile.pendingRaid->rescue || profile.pendingRaid->rescue->definitionId != kRaidFireSupportReplacementRescueId ||
        profile.pendingRaid->rescue->secured))
    {
        plan.error = RaidRescueError::InvalidCommand;
        plan.message = "RAID DOES NOT OWN THIS FIRE SUPPORT REPLACEMENT";
    }
    else if (population.ordinaryResidents >= kMaximumOrdinaryResidents)
    {
        plan.error = RaidRescueError::PopulationOverflow;
        plan.message = "ORDINARY RESIDENT COUNT CANNOT ADVANCE";
    }
    else if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        plan.error = RaidRescueError::RevisionOverflow;
        plan.message = "PROFILE REVISION CANNOT ADVANCE";
    }
    else
    {
        plan.canCommit = true;
        ++plan.residentsAfter;
    }
    plan.bedShortfallAfter = plan.residentsAfter > plan.bedCapacity ? plan.residentsAfter - plan.bedCapacity : 0U;
    plan.dailyRationsAfter = dailyRationsForResidents(plan.residentsAfter, profile.rationPolicy);
    return plan;
}

OrdinarySurvivorAdmissionReceipt executeRaidFireSupportReplacementAdmission(ProfileState &profile,
    const ContentRegistry &content, std::string_view deathSettlementId, const CommandContext &context)
{
    const auto fail = [&](RaidRescueError error, std::string message) {
        return OrdinarySurvivorAdmissionReceipt{false, false, error, std::move(message), profile.revision};
    };
    if (deathSettlementId.empty() || context.transactionId != raidFireSupportReplacementTransactionId(deathSettlementId) ||
        !supportsRaidFireSupportContent(content.contentVersion()))
        return fail(RaidRescueError::InvalidCommand, "FIRE SUPPORT REPLACEMENT TRANSACTION IS INVALID");
    const auto plan = queryRaidFireSupportReplacementAdmission(profile, deathSettlementId);
    if (plan.alreadyCommitted)
        return {true, true, RaidRescueError::None, {}, profile.revision, 0U, 0U, plan.residentsAfter,
            plan.injuredResidentsAfter, plan.bedShortfallAfter, plan.dailyRationsAfter};
    if (profile.revision != context.expectedRevision)
        return fail(RaidRescueError::StaleRevision, "PROFILE REVISION IS STALE");
    if (!plan.canCommit)
        return fail(plan.error, plan.message);

    ProfileState candidate = profile;
    auto &population = candidate.basePopulation;
    ++population.ordinaryResidents;
    ++population.professionResidents[kCombatIndex];
    if (candidate.pendingRaid)
        candidate.pendingRaid->rescue->secured = true;
    candidate.fireSupportReplacement.reset();
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return fail(RaidRescueError::InvalidProfile, validation.message);
    profile = std::move(candidate);
    return {true, false, RaidRescueError::None, {}, profile.revision, 1U, 0U, plan.residentsAfter,
        plan.injuredResidentsAfter, plan.bedShortfallAfter, plan.dailyRationsAfter};
}
