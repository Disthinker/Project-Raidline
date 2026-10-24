#include "raid_credential_extraction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "raid_visibility.h"

namespace
{
bool finite(Vec2 point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool sameInvestigation(
    const RaidCredentialExtractionDefinition &frozen,
    const RaidCredentialExtractionDefinition &published) noexcept
{
    // The independent outdoor exit is generated once during Deploy. Every
    // investigation parameter, including its original interior, stays exact.
    return frozen.permissionId == published.permissionId &&
        frozen.investigationPointId == published.investigationPointId &&
        frozen.investigationSpaceId == published.investigationSpaceId &&
        frozen.investigationPoint.x == published.investigationPoint.x &&
        frozen.investigationPoint.y == published.investigationPoint.y &&
        frozen.investigationRadius == published.investigationRadius &&
        frozen.investigationDurationSeconds ==
            published.investigationDurationSeconds &&
        frozen.extractionPoint.size.x == published.extractionPoint.size.x &&
        frozen.extractionPoint.size.y == published.extractionPoint.size.y &&
        frozen.extractionDurationSeconds == published.extractionDurationSeconds;
}
}

ProfileValidationResult queryRaidCredentialInvestigation(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RaidCredentialInvestigationFact &fact)
{
    if (!profile.pendingRaid || !profile.pendingRaid->credentialExtraction)
        return {false, "active Raid has no credential investigation"};
    const PendingRaidSnapshot &raid = *profile.pendingRaid;
    const RaidCredentialExtractionSnapshot &snapshot =
        *raid.credentialExtraction;
    if (raid.selfRecovery || raid.outpostRestoration ||
        raid.baseSiteClearance || raid.basePerimeterSweep ||
        snapshot.rulesVersion != kRaidCredentialExtractionRulesVersion)
        return {false, "credential investigation is unavailable for this Raid"};
    const MapDefinition *map{};
    try
    {
        map = &content.map(raid.mapDefinitionId);
    }
    catch (...)
    {
        return {false, "credential investigation map does not exist"};
    }
    if (!map->credentialExtraction ||
        !sameInvestigation(snapshot.definition, *map->credentialExtraction))
        return {false, "credential investigation does not match its map"};
    const auto &definition = snapshot.definition;
    if (fact.investigationPointId != definition.investigationPointId ||
        fact.spaceId != definition.investigationSpaceId ||
        !finite(fact.playerPosition) || profile.currentHealth <= 0)
        return {false, "credential investigation fact is invalid"};
    const auto interior = std::find_if(
        raid.interiors.begin(), raid.interiors.end(),
        [&](const RaidInteriorSnapshot &room) { return room.id == fact.spaceId; });
    if (interior == raid.interiors.end() ||
        fact.playerPosition.x < 0 || fact.playerPosition.y < 0 ||
        fact.playerPosition.x > interior->worldSize.x ||
        fact.playerPosition.y > interior->worldSize.y ||
        !finite(definition.investigationPoint) ||
        !std::isfinite(definition.investigationRadius) ||
        definition.investigationRadius <= 0 ||
        std::hypot(fact.playerPosition.x - definition.investigationPoint.x,
                   fact.playerPosition.y - definition.investigationPoint.y) >
            definition.investigationRadius)
        return {false, "credential investigation is outside interaction range"};
    for (const ContentRect &blocker : interior->ballisticBlockers)
        if (raidSightSegmentIntersectsRectInterior(
                fact.playerPosition, definition.investigationPoint,
                {blocker.position, blocker.size}))
            return {false, "credential investigation is blocked"};
    return {true, {}};
}

ProfileValidationResult recordRaidCredentialInvestigation(
    ProfileState &profile,
    const ContentRegistry &content,
    const RaidCredentialInvestigationFact &fact)
{
    const ProfileValidationResult eligibility =
        queryRaidCredentialInvestigation(profile, content, fact);
    if (!eligibility.valid)
        return eligibility;
    // A replay must not hide an invalid frozen contract, even though it does
    // not need another revision or transaction identity.
    const ProfileValidationResult current = validateProfileState(profile, content);
    if (!current.valid)
        return current;
    if (profile.pendingRaid->credentialExtraction->investigationCompleted)
        return {true, {}};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, "profile revision cannot advance"};
    ProfileState candidate = profile;
    candidate.pendingRaid->credentialExtraction->investigationCompleted = true;
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(candidate, content);
    if (!validation.valid)
        return validation;
    profile = std::move(candidate);
    return {true, {}};
}
