#include "raid_squad_domain.h"

#include "base_population_domain.h"
#include "base_workforce_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr auto combat = baseProfessionIndex(BaseResidentProfession::Combat);
const ItemDefinitionId crate{"item.container.base_storage_crate"};
const ItemDefinitionId &weapon(RaidSquadRole role)
{
    if (role == RaidSquadRole::Support) return kRaidSquadSupportWeaponId;
    if (role == RaidSquadRole::Sniper) return kRaidSquadSniperWeaponId;
    return kRaidFireSupportRifleId;
}
const ItemDefinitionId &ammunition(RaidSquadRole role)
{
    if (role == RaidSquadRole::Support) return kRaidSquadSupportAmmoId;
    if (role == RaidSquadRole::Sniper) return kRaidSquadSniperAmmoId;
    return kRaidFireSupportAmmunitionId;
}
bool known(RaidSquadRole role)
{
    return role == RaidSquadRole::Support || role == RaidSquadRole::Sniper || role == RaidSquadRole::Fire;
}
const StoredAssetLocation *in(const AssetRecord *asset, ProfileContainerId container)
{
    const auto *stored = asset ? std::get_if<StoredAssetLocation>(&asset->location) : nullptr;
    return stored && stored->container == container ? stored : nullptr;
}
bool validCargo(const ContentRegistry &content, const AssetRecord &asset, RaidSquadRole role)
{
    const auto &item = content.item(asset.definitionId);
    if (role == RaidSquadRole::Support)
        return item.medicalUse && asset.remainingCharges > 0U &&
            (item.medicalUse->effect == MedicalItemEffect::RestoreHealth ||
             item.medicalUse->effect == MedicalItemEffect::StopLightBleeding ||
             item.medicalUse->effect == MedicalItemEffect::StopAnyBleeding);
    return role == RaidSquadRole::Fire && (item.category == ItemCategory::Magazine ||
        item.category == ItemCategory::Ammunition || item.category == ItemCategory::Throwable);
}
bool availableWeapon(const AssetRegistry &assets, const AssetRecord &asset, RaidSquadRole role)
{
    if (asset.definitionId != weapon(role) || asset.quantity != 1U || !asset.currentDurability ||
        asset.chamberedRound || asset.weaponMalfunction != WeaponMalfunctionType::None) return false;
    return std::ranges::none_of(assets.records(), [&](const auto &entry) {
        const auto *installed = std::get_if<InstalledMagazineLocation>(&entry.second.location);
        return installed && installed->weaponAssetId == asset.instanceId;
    });
}
const RaidSquadMember *member(const PendingRaidSnapshot &raid, RaidSquadRole role)
{
    const auto it = std::ranges::find(raid.squadMembers, role, &RaidSquadMember::role);
    return it == raid.squadMembers.end() ? nullptr : &*it;
}
AssetInstanceId containerFor(const PendingRaidSnapshot &raid, RaidSquadRole role)
{
    if (role == RaidSquadRole::Fire) return raid.fireSupport ? raid.fireSupport->containerAssetId : 0U;
    const auto *entry = member(raid, role);
    return entry ? entry->containerAssetId : 0U;
}
const RaidCorpseContainerAssetLocation *corpse(const ProfileState &profile, AssetInstanceId box)
{
    const auto *asset = profile.assets.find(box);
    return asset ? std::get_if<RaidCorpseContainerAssetLocation>(&asset->location) : nullptr;
}
bool lend(ProfileState &profile, const ContentRegistry &content, AssetInstanceId box,
    AssetInstanceId id, RaidFireSupportLoan &loan)
{
    auto *asset = profile.assets.findMutable(id);
    const auto *from = in(asset, ProfileContainerId::stash());
    if (!from) return false;
    const auto destination = ProfileContainerId::compartment(box, 0U);
    const auto fit = findFirstProfileFit(profile, content, destination, content.item(asset->definitionId), asset->orientation, id);
    if (!fit) return false;
    loan = {id,from->origin,asset->orientation};
    asset->location = StoredAssetLocation{destination, *fit};
    return true;
}
bool payRounds(ProfileState &profile, const ItemDefinitionId &id, unsigned quantity)
{
    std::vector<AssetInstanceId> empty;
    for (const auto &[key, asset] : profile.assets.records())
    {
        if (quantity == 0U) break;
        if (asset.definitionId != id || !in(&asset, ProfileContainerId::stash())) continue;
        const auto take = std::min(quantity, asset.quantity);
        quantity -= take;
        if (take == asset.quantity) empty.push_back(key);
        else profile.assets.findMutable(key)->quantity -= take;
    }
    for (const auto key : empty) static_cast<void>(profile.assets.erase(key));
    return quantity == 0U;
}
}

unsigned livingSpecialistCount(const ProfileState &profile) noexcept
{
    return static_cast<unsigned>(std::ranges::count_if(profile.squadRoster, [](const auto &entry) {
        return entry.first != RaidSquadRole::Fire && entry.second.status != RaidSquadResidentStatus::Missing;
    }));
}

unsigned availableRaidSquadResidents(const ProfileState &profile, RaidSquadRole role) noexcept
{
    if (!known(role) || (profile.pendingRaid && containerFor(*profile.pendingRaid,role))) return 0U;
    const auto workforce = projectBaseWorkforce(profile);
    if (!workforce.availableResidents) return 0U;
    const auto freeCombat = workforce.availableByProfession[combat];
    // Jobs count residents by profession. Assign anonymous residents first, then
    // named residents in roster order. Keep each idle place attached to one role.
    unsigned namedAtHome{}, laterInRoster{};
    for (const auto &[named,resident] : profile.squadRoster)
    {
        if (resident.status != RaidSquadResidentStatus::Ready ||
            (profile.pendingRaid && containerFor(*profile.pendingRaid,named))) continue;
        ++namedAtHome;
        if (named > role) ++laterInRoster;
    }
    const auto it = profile.squadRoster.find(role);
    if (it != profile.squadRoster.end())
        return it->second.status == RaidSquadResidentStatus::Ready && freeCombat > laterInRoster ? 1U : 0U;
    if (role != RaidSquadRole::Fire || freeCombat <= namedAtHome) return 0U;
    return std::min(workforce.availableResidents,freeCombat - namedAtHome);
}

RaidSquadOptions queryRaidSquadLoadout(const ProfileState &profile, const ContentRegistry &content,
    const MapDefinitionId &map, RaidSquadRole role)
{
    RaidSquadOptions result;
    result.requiredRounds = role == RaidSquadRole::Sniper ? 20U : 30U;
    if (!known(role) || !isRaidFireSupportMap(map) || !supportsRaidFireSupportContent(content.contentVersion())) return result;
    result.residentAvailable = availableRaidSquadResidents(profile,role) != 0U;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (!in(&asset, ProfileContainerId::stash())) continue;
        if (asset.definitionId == ammunition(role))
            result.preparationRounds += std::min(asset.quantity, std::numeric_limits<unsigned>::max() - result.preparationRounds);
        if (availableWeapon(profile.assets, asset, role)) result.weapons.push_back(id);
        if (validCargo(content, asset, role)) result.cargo.push_back(id);
    }
    return result;
}

bool canRecruitRaidSpecialist(const ProfileState &profile, const MapDefinitionId &map, RaidSquadRole role)
{
    if (!((role == RaidSquadRole::Support && map == kRaidFireSupportMapId) ||
          (role == RaidSquadRole::Sniper && map == kRaidFireSupportReplacementMapId))) return false;
    const auto it = profile.squadRoster.find(role);
    const auto now = profile.pendingRaid ? profile.pendingRaid->travel.startingWorldClock.elapsedWorldMinutes :
        profile.worldClock.elapsedWorldMinutes;
    return it == profile.squadRoster.end() || (it->second.status == RaidSquadResidentStatus::Missing &&
        now >= it->second.replacementReadyMinute);
}

std::string raidSquadSourceId(std::string_view base, RaidSquadRole role)
{
    if (role == RaidSquadRole::Support) return std::string{base} + ":squad-support";
    if (role == RaidSquadRole::Sniper) return std::string{base} + ":squad-sniper";
    return raidFireSupportCorpseSourceId(base);
}
LostRaidRecordKind raidSquadLossKind(RaidSquadRole role) noexcept
{
    if (role == RaidSquadRole::Support) return LostRaidRecordKind::SpecialistSupport;
    if (role == RaidSquadRole::Sniper) return LostRaidRecordKind::SpecialistSniper;
    return LostRaidRecordKind::FireSupport;
}

bool isRaidSquadLossRoot(const ContentRegistry &content, LostRaidRecordKind kind, const AssetRecord &asset, EquipmentSlotKind slot)
{
    if (kind == LostRaidRecordKind::FireSupport)
        return isRaidFireSupportLossRoot(content,asset,slot) ||
            ((slot == EquipmentSlotKind::Backpack || slot == EquipmentSlotKind::SecondaryWeapon) &&
             validCargo(content,asset,RaidSquadRole::Fire));
    if (kind == LostRaidRecordKind::SpecialistSniper)
        return slot == EquipmentSlotKind::PrimaryWeapon && asset.definitionId == kRaidSquadSniperWeaponId;
    return kind == LostRaidRecordKind::SpecialistSupport &&
        ((slot == EquipmentSlotKind::Sidearm && asset.definitionId == kRaidSquadSupportWeaponId) ||
         ((slot == EquipmentSlotKind::ChestRig || slot == EquipmentSlotKind::Backpack) &&
          validCargo(content,asset,RaidSquadRole::Support)));
}

OrdinarySurvivorAdmissionReceipt executeRaidSpecialistAdmission(ProfileState &profile, const ContentRegistry &content,
    RaidSquadRole role, const CommandContext &context, const PendingRaidSnapshot *checkpointMission)
{
    auto fail = [&](RaidRescueError error, std::string message) {
        return OrdinarySurvivorAdmissionReceipt{false,false,error,std::move(message),profile.revision};
    };
    const auto *mission = profile.pendingRaid ? &*profile.pendingRaid : checkpointMission;
    const auto &map = role == RaidSquadRole::Support ? kRaidFireSupportMapId : kRaidFireSupportReplacementMapId;
    if (!mission || !known(role) || role == RaidSquadRole::Fire || !supportsRaidFireSupportContent(content.contentVersion()) ||
        mission->specialistRescue != role || !mission->rescue || mission->mapDefinitionId != map ||
        context.transactionId != raidSquadSourceId(mission->settlementId,role) + ":admission")
        return fail(RaidRescueError::InvalidCommand,"no matching specialist rescue");
    if (profile.committedTransactions.contains(context.transactionId))
        return {true,true,RaidRescueError::None,{},profile.revision};
    if (context.expectedRevision != profile.revision) return fail(RaidRescueError::StaleRevision,"stale rescue");
    if (!canRecruitRaidSpecialist(profile,map,role) || mission->rescue->secured)
        return fail(RaidRescueError::InvalidCommand,"specialist unavailable");
    if (profile.basePopulation.ordinaryResidents >= kMaximumOrdinaryResidents)
        return fail(RaidRescueError::PopulationOverflow,"no population capacity");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return fail(RaidRescueError::RevisionOverflow,"profile revision exhausted");
    auto candidate = profile;
    auto [it, fresh] = candidate.squadRoster.try_emplace(role);
    if (!fresh && it->second.generation == std::numeric_limits<unsigned>::max())
        return fail(RaidRescueError::RevisionOverflow,"specialist generation exhausted");
    if (!fresh) ++it->second.generation;
    it->second.status = RaidSquadResidentStatus::Ready;
    it->second.replacementReadyMinute = 0U;
    auto &population = candidate.basePopulation;
    ++population.ordinaryResidents;
    ++population.professionResidents[combat];
    if (candidate.pendingRaid) candidate.pendingRaid->rescue->secured = true;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto check = validateProfileState(candidate,content);
    if (!check.valid) return fail(RaidRescueError::InvalidProfile,check.message);
    const auto count = population.ordinaryResidents;
    const auto beds = population.bedCapacity;
    profile = std::move(candidate);
    return {true,false,RaidRescueError::None,{},profile.revision,1U,0U,count,profile.basePopulation.injuredResidents,
        count > beds ? count - beds : 0U,dailyRationsForResidents(count,profile.rationPolicy)};
}

OrdinarySurvivorAdmissionPlan queryRaidSpecialistAdmission(const ProfileState &profile, const MapDefinitionId &map,
    RaidSquadRole role)
{
    OrdinarySurvivorAdmissionPlan plan;
    const auto &p = profile.basePopulation;
    plan.revision = profile.revision;
    plan.residentsBefore = plan.residentsAfter = p.ordinaryResidents;
    plan.injuredResidentsAfter = p.injuredResidents;
    plan.bedCapacity = p.bedCapacity;
    const auto &raid = profile.pendingRaid;
    if (raid && raid->mapDefinitionId == map && raid->specialistRescue == role && raid->rescue &&
        raid->rescue->secured && profile.committedTransactions.contains(raidSquadSourceId(raid->settlementId,role) + ":admission"))
    {
        plan.canCommit = true;
        plan.alreadyCommitted = true;
    }
    else if (!canRecruitRaidSpecialist(profile,map,role) || (raid &&
        (raid->specialistRescue != role || !raid->rescue || raid->rescue->secured)))
    {
        plan.error = RaidRescueError::InvalidCommand;
        plan.message = "specialist unavailable";
    }
    else if (p.ordinaryResidents >= kMaximumOrdinaryResidents) plan.error = RaidRescueError::PopulationOverflow;
    else if (profile.revision == std::numeric_limits<ProfileRevision>::max()) plan.error = RaidRescueError::RevisionOverflow;
    else { plan.canCommit = true; ++plan.residentsAfter; }
    plan.bedShortfallAfter = plan.residentsAfter > p.bedCapacity ? plan.residentsAfter - p.bedCapacity : 0U;
    plan.dailyRationsAfter = dailyRationsForResidents(plan.residentsAfter,profile.rationPolicy);
    return plan;
}

ProfileValidationResult prepareRaidSquadInCandidate(ProfileState &candidate, const ContentRegistry &content,
    const std::vector<RaidSquadLoadout> &loadouts)
{
    if (loadouts.empty()) return {true,{}};
    if (!candidate.pendingRaid || !candidate.pendingRaid->startingAssets || loadouts.size() > 3U)
        return {false,"invalid squad deployment"};
    auto &raid = *candidate.pendingRaid;
    std::set<RaidSquadRole> roles;
    std::set<AssetInstanceId> selected;
    if (raid.fireSupport) { roles.insert(RaidSquadRole::Fire); raid.squadOrder.push_back(RaidSquadRole::Fire); }
    for (const auto &loadout : loadouts)
    {
        const auto role = loadout.role;
        const auto options = queryRaidSquadLoadout(candidate,content,raid.mapDefinitionId,role);
        if (!roles.insert(role).second || !options.residentAvailable || loadout.cargoAssetIds.size() > 2U ||
            std::ranges::find(options.weapons,loadout.weaponAssetId) == options.weapons.end() ||
            !selected.insert(loadout.weaponAssetId).second)
            return {false,"invalid squad member, weapon or cargo capacity"};
        for (const auto id : loadout.cargoAssetIds)
            if (!selected.insert(id).second || std::ranges::find(options.cargo,id) == options.cargo.end())
                return {false,"selected squad cargo is unavailable"};
        if (candidate.assets.nextAssetId() == std::numeric_limits<AssetInstanceId>::max())
            return {false,"no more asset identifiers"};
        const auto box = candidate.assets.create(content.item(crate), RaidCorpseContainerAssetLocation{
            raid.raidId,outdoorRaidSpaceId(),raidSquadSourceId(raid.raidId,role),std::nullopt,RaidCorpseSourceKind::FireSupport});
        RaidFireSupportLoan gun;
        if (!lend(candidate,content,box,loadout.weaponAssetId,gun)) return {false,"squad weapon did not fit"};
        std::vector<RaidFireSupportLoan> cargo;
        for (const auto id : loadout.cargoAssetIds)
        {
            if (!lend(candidate,content,box,id,cargo.emplace_back())) return {false,"squad cargo did not fit"};
        }
        if (role == RaidSquadRole::Fire)
        {
            raid.fireSupport = RaidFireSupportSnapshot{box,gun,std::nullopt};
            raid.fireSupportCargo = std::move(cargo);
        }
        else raid.squadMembers.push_back({role,box,gun,std::move(cargo),100});
        raid.squadOrder.push_back(role);
    }
    for (const auto &loadout : loadouts)
        if (!payRounds(candidate,ammunition(loadout.role),loadout.role == RaidSquadRole::Sniper ? 20U : 30U))
            return {false,"insufficient unreserved preparation rounds"};
    return {true,{}};
}

void setRaidSquadHealthInSimulation(ProfileState &p, RaidSquadRole role, int health)
{
    if (!p.pendingRaid || !containerFor(*p.pendingRaid,role)) return;
    auto &raid = *p.pendingRaid;
    health = std::clamp(health,0,100);
    if (role == RaidSquadRole::Fire) raid.fireSupportHealth = health;
    else
        for (auto &m : raid.squadMembers)
            if (m.role == role) m.health = health;
}
bool revealRaidSquadCorpseInSimulation(ProfileState &p, const ContentRegistry &content, RaidSquadRole role,
    const RaidSpaceDefinitionId &space, Vec2 point)
{
    if (role == RaidSquadRole::Fire)
    {
        if (!revealRaidFireSupportCorpseInSimulation(p,content,space,point)) return false;
        setRaidSquadHealthInSimulation(p,role,0);
        return true;
    }
    if (!p.pendingRaid || !std::isfinite(point.x) || !std::isfinite(point.y)) return false;
    auto &raid = *p.pendingRaid;
    const auto box = containerFor(raid,role);
    const auto *c = corpse(p,box);
    if (!c) return false;
    if (c->deathPosition) return c->spaceId == space && c->deathPosition->x == point.x && c->deathPosition->y == point.y;
    Vec2 bounds = content.map(raid.mapDefinitionId).worldSize;
    if (space != outdoorRaidSpaceId())
    {
        const auto room = std::ranges::find(raid.interiors,space,&RaidInteriorSnapshot::id);
        if (room == raid.interiors.end()) return false;
        bounds = room->worldSize;
    }
    if (point.x < 0 || point.y < 0 || point.x > bounds.x || point.y > bounds.y ||
        p.revision == std::numeric_limits<ProfileRevision>::max()) return false;
    auto &location = std::get<RaidCorpseContainerAssetLocation>(p.assets.findMutable(box)->location);
    location.deathPosition = point;
    location.spaceId = space;
    setRaidSquadHealthInSimulation(p,role,0);
    ++p.revision;
    return true;
}

namespace
{
InventoryReceipt transferCargo(ProfileState &profile, const ContentRegistry &content, RaidSquadRole role,
    AssetInstanceId id, bool consume, const CommandContext &ctx)
{
    const auto fail = [&](DomainErrorCode err, std::string msg) {
        return InventoryReceipt{false,false,err,std::move(msg),profile.revision};
    };
    if (ctx.transactionId.empty()) return fail(DomainErrorCode::InvalidTransaction,"missing squad transaction");
    if (profile.committedTransactions.contains(ctx.transactionId)) return {true,true,DomainErrorCode::None,{},profile.revision};
    if (ctx.expectedRevision != profile.revision) return fail(DomainErrorCode::StaleRevision,"stale squad transaction");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max()) return fail(DomainErrorCode::RevisionOverflow,"revision exhausted");
    if (!profile.pendingRaid) return fail(DomainErrorCode::IllegalDestination,"no active squad");
    const auto &raid = *profile.pendingRaid;
    const auto box = containerFor(raid,role);
    const auto *container = corpse(profile,box);
    const auto *asset = profile.assets.find(id);
    if (!box || !container || container->deathPosition || !in(asset,ProfileContainerId::compartment(box,0U)) ||
        !validCargo(content,*asset,role)) return fail(DomainErrorCode::MissingAsset,"squad cargo unavailable");
    auto candidate = profile;
    auto *borrowed = candidate.assets.findMutable(id);
    if (consume)
    {
        if (role != RaidSquadRole::Support || borrowed->remainingCharges == 0U)
            return fail(DomainErrorCode::InvalidQuantity,"invalid medical charge");
        if (--borrowed->remainingCharges == 0U) static_cast<void>(candidate.assets.erase(id));
    }
    else
    {
        const auto pack = equippedAsset(candidate,EquipmentSlotKind::Backpack);
        const auto to = ProfileContainerId::compartment(pack.value_or(0U),0U);
        const auto fit = pack ? findFirstProfileFit(candidate,content,to,content.item(borrowed->definitionId),
            borrowed->orientation,id) : std::nullopt;
        if (!fit) return fail(DomainErrorCode::Capacity,"no space in backpack");
        borrowed->location = StoredAssetLocation{to,*fit};
    }
    candidate.committedTransactions.insert(ctx.transactionId);
    ++candidate.revision;
    const auto checked = validateProfileState(candidate,content);
    if (!checked.valid) return fail(DomainErrorCode::InvalidProfile,checked.message);
    profile = std::move(candidate);
    return {true,false,DomainErrorCode::None,{},profile.revision};
}
}
InventoryReceipt takeRaidSquadCargo(ProfileState &p,const ContentRegistry &content,RaidSquadRole role,AssetInstanceId id,
    const CommandContext &ctx) { return transferCargo(p,content,role,id,false,ctx); }
InventoryReceipt consumeRaidSquadMedical(ProfileState &p,const ContentRegistry &content,AssetInstanceId id,
    const CommandContext &ctx) { return transferCargo(p,content,RaidSquadRole::Support,id,true,ctx); }

ProfileValidationResult settleRaidSquadLoansInCandidate(ProfileState &candidate, const ContentRegistry &content,
    RaidResultOutcome outcome, std::map<RaidSquadRole,RaidFireSupportResult> &results, RaidFireSupportResult *fire)
{
    const auto &raid = *candidate.pendingRaid;
    const auto settle = [&](RaidSquadRole role, AssetInstanceId box, const RaidFireSupportLoan &loan,
        EquipmentSlotKind slot, RaidFireSupportResult &result) {
        auto *asset = candidate.assets.findMutable(loan.assetId);
        if (!in(asset,ProfileContainerId::compartment(box,0U))) return true;
        if (result.died)
        {
            const auto recordId = raidSquadSourceId(raid.settlementId,role);
            if (!result.lostRecordId)
            {
                if (!candidate.lostRaidRecords.emplace(recordId,LostRaidRecord{
                    recordId,raid.raidId,raid.settlementId,raid.mapDefinitionId,
                    content.map(raid.mapDefinitionId).operationBriefing.difficulty,outcome,
                    candidate.worldClock.elapsedWorldMinutes,0U,raidSquadLossKind(role)}).second) return false;
                result.lostRecordId = recordId;
            }
            asset->location = LostRaidAssetLocation{recordId,slot};
        }
        else
        {
            if (!profilePlacementFits(candidate,content,ProfileContainerId::stash(),loan.returnOrigin,
                content.item(asset->definitionId),loan.originalOrientation,loan.assetId)) return false;
            asset->location = StoredAssetLocation{ProfileContainerId::stash(),loan.returnOrigin};
            asset->orientation = loan.originalOrientation;
            ++result.returnedLoanCount;
        }
        return true;
    };
    for (const auto &m : raid.squadMembers)
    {
        const auto *binding = corpse(candidate,m.containerAssetId);
        if (!binding) return {false,"specialist container missing"};
        auto &result = results[m.role];
        result.died = binding->deathPosition.has_value();
        if (!settle(m.role,m.containerAssetId,m.weapon,m.role == RaidSquadRole::Support ?
            EquipmentSlotKind::Sidearm : EquipmentSlotKind::PrimaryWeapon,result))
            return {false,"could not settle borrowed specialist weapon"};
        for (std::size_t i{}; i < m.cargo.size(); ++i)
            if (!settle(m.role,m.containerAssetId,m.cargo[i],i == 0U ? EquipmentSlotKind::ChestRig :
                EquipmentSlotKind::Backpack,result)) return {false,"could not settle specialist cargo"};
    }
    if (fire && raid.fireSupport)
        for (std::size_t i{}; i < raid.fireSupportCargo.size(); ++i)
            if (!settle(RaidSquadRole::Fire,raid.fireSupport->containerAssetId,raid.fireSupportCargo[i],
                i == 0U ? EquipmentSlotKind::Backpack : EquipmentSlotKind::SecondaryWeapon,*fire))
                return {false,"could not settle fire support cargo"};
    return {true,{}};
}

ProfileValidationResult applyRaidSquadReturnInCandidate(ProfileState &candidate,const PendingRaidSnapshot &raid,
    const std::map<RaidSquadRole,RaidFireSupportResult> &results,const RaidFireSupportResult *fire)
{
    const auto now = candidate.worldClock.elapsedWorldMinutes;
    auto &p = candidate.basePopulation;
    const auto wound = [&](RaidSquadRole role) {
        if (p.injuredByProfession[combat] >= p.professionResidents[combat]) return false;
        ++p.injuredByProfession[combat]; ++p.injuredResidents;
        candidate.squadRoster[role].status = RaidSquadResidentStatus::Wounded;
        return true;
    };
    if (fire && !fire->died && raid.fireSupportHealth < 50 && !wound(RaidSquadRole::Fire))
        return {false,"cannot register wounded fire support"};
    for (const auto &m : raid.squadMembers)
    {
        auto &resident = candidate.squadRoster.at(m.role);
        const auto &result = results.at(m.role);
        if (result.died)
        {
            if (p.professionResidents[combat] <= p.injuredByProfession[combat] ||
                now > std::numeric_limits<std::uint64_t>::max() - kRaidFireSupportReplacementDelayMinutes)
                return {false,"cannot retire specialist"};
            --p.ordinaryResidents; --p.professionResidents[combat];
            resident.status = RaidSquadResidentStatus::Missing;
            resident.replacementReadyMinute = now + kRaidFireSupportReplacementDelayMinutes;
            if (result.lostRecordId) candidate.lostRaidRecords.at(*result.lostRecordId).createdWorldMinute = now;
        }
        else if (m.health < 50 && !wound(m.role)) return {false,"cannot register wounded specialist"};
    }
    return {true,{}};
}

ProfileValidationResult validateRaidSquad(const ProfileState &profile, const ContentRegistry &content)
{
    const auto &roster = profile.squadRoster;
    unsigned wounded{}, living{};
    for (const auto &[role,r] : roster)
    {
        if (!known(role) || !r.generation || (r.status != RaidSquadResidentStatus::Ready &&
            r.status != RaidSquadResidentStatus::Wounded && r.status != RaidSquadResidentStatus::Missing) ||
            ((r.status == RaidSquadResidentStatus::Missing) != (r.replacementReadyMinute != 0U)))
            return {false,"invalid specialist identity or availability"};
        wounded += r.status == RaidSquadResidentStatus::Wounded;
        living += r.status != RaidSquadResidentStatus::Missing;
    }
    if (wounded > profile.basePopulation.injuredByProfession[combat] || living > profile.basePopulation.professionResidents[combat])
        return {false,"squad roster exceeds resident workforce"};
    if (const auto fire = roster.find(RaidSquadRole::Fire); fire != roster.end())
    {
        const bool missing = fire->second.status == RaidSquadResidentStatus::Missing;
        if (missing != profile.fireSupportReplacement.has_value() ||
            (missing && fire->second.replacementReadyMinute != profile.fireSupportReplacement->readyWorldMinute))
            return {false,"named fire support replacement does not match the vacant slot"};
        if (profile.pendingRaid && profile.pendingRaid->fireSupport && fire->second.status != RaidSquadResidentStatus::Ready)
            return {false,"unavailable fire support cannot be deployed"};
    }
    if (profile.residentMedical.activeTreatment && profile.residentMedical.activeTreatment->squadPatient)
    {
        const auto &job = *profile.residentMedical.activeTreatment;
        const auto it = roster.find(*job.squadPatient);
        if (job.patientProfession != BaseResidentProfession::Combat || it == roster.end() ||
            it->second.status != RaidSquadResidentStatus::Wounded) return {false,"squad treatment has no matching patient"};
    }
    if (profile.lastRaidResult)
        for (const auto &[role,result] : profile.lastRaidResult->squadResults)
        {
            if (!known(role) || role == RaidSquadRole::Fire || result.returnedLoanCount > (role == RaidSquadRole::Support ? 3U : 1U) ||
                (result.died && result.returnedLoanCount) || (!result.died && result.lostRecordId))
                return {false,"invalid specialist settlement"};
            if (result.lostRecordId)
            {
                const auto it = profile.lostRaidRecords.find(*result.lostRecordId);
                if (it == profile.lostRaidRecords.end() || it->second.kind != raidSquadLossKind(role) ||
                    it->second.settlementId != profile.lastRaidResult->settlementId)
                    return {false,"specialist loss record missing"};
            }
        }
    if (!profile.pendingRaid) return {true,{}};
    const auto &raid = *profile.pendingRaid;
    if (raid.squadMembers.empty() && raid.squadOrder.empty() && !raid.specialistRescue &&
        raid.fireSupportCargo.empty() && raid.fireSupportHealth == 100) return {true,{}};
    if (!isRaidFireSupportMap(raid.mapDefinitionId) || !supportsRaidFireSupportContent(content.contentVersion()) ||
        raid.selfRecovery || raid.outpostRestoration || raid.baseSiteClearance || raid.basePerimeterSweep ||
        raid.squadMembers.size() > 2U || raid.fireSupportCargo.size() > 2U || raid.fireSupportHealth < 0 || raid.fireSupportHealth > 100 ||
        (!raid.fireSupport && (!raid.fireSupportCargo.empty() || raid.fireSupportHealth != 100)))
        return {false,"invalid squad activity"};
    if (raid.specialistRescue)
    {
        const auto role = *raid.specialistRescue;
        if (!raid.rescue || raid.fireSupportReplacementSource ||
            !((role == RaidSquadRole::Support && raid.mapDefinitionId == kRaidFireSupportMapId) ||
            (role == RaidSquadRole::Sniper && raid.mapDefinitionId == kRaidFireSupportReplacementMapId)))
            return {false,"invalid specialist rescue site"};
        const bool admitted = profile.committedTransactions.contains(raidSquadSourceId(raid.settlementId,role) + ":admission");
        if (raid.rescue->secured != admitted || (admitted && !roster.contains(role)) ||
            (!admitted && !canRecruitRaidSpecialist(profile,raid.mapDefinitionId,role))) return {false,"invalid specialist rescue progress"};
    }
    std::set<RaidSquadRole> deployed;
    std::set<AssetInstanceId> boxes, loans;
    if (raid.fireSupport)
    {
        deployed.insert(RaidSquadRole::Fire); boxes.insert(raid.fireSupport->containerAssetId);
        loans.insert(raid.fireSupport->rifle.assetId);
        if (raid.fireSupport->spareMagazine) loans.insert(raid.fireSupport->spareMagazine->assetId);
    }
    const auto validLoan = [&](const RaidFireSupportLoan &loan, RaidSquadRole role, AssetInstanceId box, bool gun, bool dead) {
        if (!raid.startingAssets || !loans.insert(loan.assetId).second) return false;
        const auto *original = raid.startingAssets->find(loan.assetId);
        const auto *source = in(original,ProfileContainerId::stash());
        if (!source || source->origin != loan.returnOrigin || original->orientation != loan.originalOrientation ||
            !(gun ? availableWeapon(*raid.startingAssets,*original,role) : validCargo(content,*original,role))) return false;
        const auto *current = profile.assets.find(loan.assetId);
        if (!in(current,ProfileContainerId::compartment(box,0U))) return !gun || dead;
        return current->orientation == loan.originalOrientation && current->definitionId == original->definitionId &&
            current->quantity == original->quantity && current->remainingCharges <= original->remainingCharges &&
            current->magazineRounds == original->magazineRounds && current->currentDurability == original->currentDurability;
    };
    for (const auto &m : raid.squadMembers)
    {
        const auto specialist = roster.find(m.role);
        const auto *root = profile.assets.find(m.containerAssetId);
        const auto *c = corpse(profile,m.containerAssetId);
        if (!known(m.role) || m.role == RaidSquadRole::Fire || !deployed.insert(m.role).second ||
            !boxes.insert(m.containerAssetId).second || specialist == roster.end() ||
            specialist->second.status != RaidSquadResidentStatus::Ready || !root || root->definitionId != crate || !c ||
            !raid.startingAssets || m.containerAssetId < raid.startingAssets->nextAssetId() ||
            c->sourceKind != RaidCorpseSourceKind::FireSupport || c->sourceEnemyId != raidSquadSourceId(raid.raidId,m.role) ||
            c->raidId != raid.raidId || m.health < 0 || m.health > 100 || (c->deathPosition.has_value() != (m.health == 0)) ||
            (!c->deathPosition && c->spaceId != outdoorRaidSpaceId()) || m.cargo.size() > (m.role == RaidSquadRole::Support ? 2U : 0U) ||
            !validLoan(m.weapon,m.role,m.containerAssetId,true,c->deathPosition.has_value()))
            return {false,"invalid specialist deployment binding"};
        if (c->deathPosition)
        {
            auto bounds = content.map(raid.mapDefinitionId).worldSize;
            if (c->spaceId != outdoorRaidSpaceId())
            {
                const auto it = std::ranges::find(raid.interiors,c->spaceId,&RaidInteriorSnapshot::id);
                if (it == raid.interiors.end()) return {false,"specialist corpse has no room"};
                bounds = it->worldSize;
            }
            const auto point = *c->deathPosition;
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.x < 0 || point.y < 0 ||
                point.x > bounds.x || point.y > bounds.y) return {false,"specialist corpse lies outside the space"};
        }
        for (const auto &loan : m.cargo)
            if (!validLoan(loan,m.role,m.containerAssetId,false,c->deathPosition.has_value())) return {false,"invalid specialist cargo"};
        for (const auto &[id,a] : profile.assets.records())
            if (in(&a,ProfileContainerId::compartment(m.containerAssetId,0U)) && id != m.weapon.assetId &&
                std::ranges::none_of(m.cargo,[id](const auto &loan){return loan.assetId == id;}))
                return {false,"unexpected item in specialist container"};
    }
    for (const auto &loan : raid.fireSupportCargo)
        if (!validLoan(loan,RaidSquadRole::Fire,raid.fireSupport->containerAssetId,false,false))
            return {false,"invalid fire support cargo"};
    if (!raid.squadOrder.empty() || !raid.squadMembers.empty())
    {
        const std::set<RaidSquadRole> slots(raid.squadOrder.begin(),raid.squadOrder.end());
        if (slots != deployed || slots.size() != raid.squadOrder.size()) return {false,"squad slots do not match deployed members"};
    }
    return validateRaidFireSupportStashReservations(profile,content,profile.assets);
}
