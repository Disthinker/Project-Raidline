#include "base_corpse_domain.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace
{
bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
bool validRecord(const ProfileState &profile, const ContentRegistry &content, const BaseCorpseRecord &record)
{
    if (!record.sourceEnemyId || !finite(record.position) || record.position.x < 0 || record.position.y < 0 ||
        !finite(record.size) || record.size.x <= 0 || record.size.y <= 0 || record.size.x > 256 || record.size.y > 256 ||
        record.corpseId != baseCorpseIdentity(record) || record.activity.activityId.empty()) return false;
    const auto &scope = record.activity;
    if (scope.activity == WeaponActionActivity::Training)
        return !profile.pendingRaid && !profile.activeBaseDefense && scope.baseSiteDefinitionId.value().empty() && record.cycleIndex == 0;
    const auto site = profile.regionalOperations.baseSites.find(scope.baseSiteDefinitionId);
    if (site == profile.regionalOperations.baseSites.end() || !site->second.unlocked) return false;
    try { static_cast<void>(content.regionalBaseSite(scope.baseSiteDefinitionId)); }
    catch (...) { return false; }
    if (scope.activity == WeaponActionActivity::BaseDaily)
    {
        const auto snapshot = profile.homePerimeter.sites.find(scope.baseSiteDefinitionId);
        return scope.activityId == scope.baseSiteDefinitionId.value() && snapshot != profile.homePerimeter.sites.end() &&
            record.cycleIndex == snapshot->second.cycleIndex &&
            std::any_of(snapshot->second.enemies.begin(), snapshot->second.enemies.end(), [&](const auto &enemy) {
                return enemy.localId == record.sourceEnemyId && enemy.health == 0;
            });
    }
    if (scope.activity != WeaponActionActivity::BaseDefense || record.cycleIndex != 0) return false;
    if (profile.activeBaseDefense && profile.activeBaseDefense->eventId == scope.activityId)
        return profile.activeBaseDefense->siteDefinitionId == scope.baseSiteDefinitionId.value() &&
            std::find(profile.activeBaseDefense->killedIds.begin(), profile.activeBaseDefense->killedIds.end(), record.sourceEnemyId) !=
                profile.activeBaseDefense->killedIds.end() &&
            record.position.x <= profile.activeBaseDefense->worldSize.x && record.position.y <= profile.activeBaseDefense->worldSize.y;
    const std::string prefix = profile.profileId + "-base-siege-";
    if (!scope.activityId.starts_with(prefix)) return false;
    std::uint64_t sequence{};
    const auto first = scope.activityId.data() + prefix.size(), last = scope.activityId.data() + scope.activityId.size();
    const auto parsed = std::from_chars(first, last, sequence);
    return parsed.ec == std::errc{} && parsed.ptr == last && sequence > 0 && sequence <= profile.baseSiege.lastResolvedSequence;
}

bool accessible(const BaseCorpseRecord &record, const RaidReloadWorldAccess &access, float range)
{
    const auto base=[](WeaponActionActivity a){return a==WeaponActionActivity::BaseDaily || a==WeaponActionActivity::BaseDefense;};
    const bool sameScope=access.activity && (record.activity==*access.activity ||
        (base(record.activity.activity) && base(access.activity->activity) && record.activity.baseSiteDefinitionId==access.activity->baseSiteDefinitionId));
    if (!sameScope || !finite(access.playerCenter) ||
        !std::isfinite(range) || range <= 0 || !access.observation || !access.currentObservation ||
        access.currentObservation->spaceId != access.spaceId || !access.observation->matches(*access.currentObservation) ||
        !access.observation->matches(access.spaceId, access.playerCenter)) return false;
    const float dx = record.position.x - access.playerCenter.x, dy = record.position.y - access.playerCenter.y;
    return dx * dx + dy * dy <= range * range && access.observation->boundsVisible(
        {{record.position.x - record.size.x * 0.5F, record.position.y - record.size.y * 0.5F}, record.size});
}
}

std::string baseCorpseIdentity(const BaseCorpseRecord &record)
{
    return "base-corpse/" + std::to_string(static_cast<unsigned>(record.activity.activity)) + "/" +
        std::string{record.activity.baseSiteDefinitionId.value()} + "/" + record.activity.activityId + "/" +
        std::to_string(record.cycleIndex) + "/" + std::to_string(record.sourceEnemyId);
}

ProfileValidationResult validateBaseCorpseRecords(const ProfileState &profile, const ContentRegistry &content)
{
    try
    {
        std::set<std::string> identities;
        for (const auto &record : profile.baseCorpses)
            if (!validRecord(profile, content, record) || !identities.insert(record.corpseId).second)
                return {false, "Base corpse identity or frozen source is invalid"};
        return {true, {}};
    }
    catch (...) { return {false, "Base corpse references unknown content"}; }
}

InventoryReceipt recordBaseCorpseDeath(ProfileState &profile, const ContentRegistry &content,
    BaseCorpseRecord record, const CommandContext &context)
{
    if (context.transactionId.empty()) return {false, false, DomainErrorCode::InvalidTransaction, "empty corpse transaction", profile.revision};
    if (profile.committedTransactions.contains(context.transactionId)) return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (context.expectedRevision != profile.revision) return {false, false, DomainErrorCode::StaleRevision, "corpse revision is stale", profile.revision};
    if (record.corpseId.empty()) record.corpseId = baseCorpseIdentity(record);
    const auto duplicate = std::find_if(profile.baseCorpses.begin(), profile.baseCorpses.end(), [&](const auto &c) { return c.corpseId == record.corpseId; });
    if (duplicate != profile.baseCorpses.end()) return {true, true, DomainErrorCode::None, {}, profile.revision};
    if (!validRecord(profile, content, record)) return {false, false, DomainErrorCode::InvalidProfile, "invalid corpse death source", profile.revision};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, false, DomainErrorCode::RevisionOverflow, "corpse revision overflow", profile.revision};
    auto records = profile.baseCorpses;
    records.push_back(std::move(record));
    profile.committedTransactions.insert(context.transactionId);
    profile.baseCorpses = std::move(records);
    ++profile.revision;
    return {true, false, DomainErrorCode::None, {}, profile.revision};
}

void pruneBaseCorpseRecordsInCandidate(ProfileState &profile) noexcept
{
    std::erase_if(profile.baseCorpses, [&](const auto &record) {
        if (record.activity.activity == WeaponActionActivity::BaseDaily)
        {
            const auto site = profile.homePerimeter.sites.find(record.activity.baseSiteDefinitionId);
            return site == profile.homePerimeter.sites.end() || site->second.cycleIndex != record.cycleIndex;
        }
        return record.activity.activity == WeaponActionActivity::BaseDefense && profile.activeBaseDefense &&
            record.activity.baseSiteDefinitionId.value() == profile.activeBaseDefense->siteDefinitionId &&
            record.activity.activityId != profile.activeBaseDefense->eventId;
    });
}

std::vector<BaseCorpseRecord> nearbyBaseCorpses(const ProfileState &profile, const RaidReloadWorldAccess &access, float range)
{
    std::vector<BaseCorpseRecord> result;
    if(!access.activity || !weaponActionScopeMatches(profile,*access.activity))return result;
    for (const auto &record : profile.baseCorpses)
        if (accessible(record, access, range)) result.push_back(record);
    std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
        const auto distance = [&](Vec2 p) { const auto dx = p.x - access.playerCenter.x, dy = p.y - access.playerCenter.y; return dx * dx + dy * dy; };
        return distance(a.position) != distance(b.position) ? distance(a.position) < distance(b.position) : a.corpseId < b.corpseId;
    });
    return result;
}

InventoryPlan queryBaseCorpseInspection(const ProfileState &profile, const ContentRegistry &content,
    std::string_view corpseId, const RaidReloadWorldAccess &access, float range)
{
    const auto found = std::find_if(profile.baseCorpses.begin(), profile.baseCorpses.end(), [&](const auto &c) { return c.corpseId == corpseId; });
    if (found == profile.baseCorpses.end()) return {false, DomainErrorCode::MissingAsset, "corpse is unavailable", profile.revision};
    if (!access.activity || !weaponActionScopeMatches(profile,*access.activity) ||
        !validRecord(profile, content, *found) || !accessible(*found, access, range))
        return {false, DomainErrorCode::IllegalDestination, "corpse is outside current observation", profile.revision};
    return {true, DomainErrorCode::None, "EMPTY CORPSE", profile.revision};
}
