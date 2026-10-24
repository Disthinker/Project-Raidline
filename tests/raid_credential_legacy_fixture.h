#pragma once

#include <algorithm>
#include <stdexcept>

#include "profile_state.h"

// Test data only: represent a pending Raid frozen before schema 57. Keep all
// previous layout, rolls, asset owners and high-water marks. Production saves
// must continue to reject a lossy downgrade of any credential fact.
inline void stripCredentialExtractionForLegacySnapshot(ProfileState &profile)
{
    if (!profile.pendingRaid)
        return;
    auto &raid = *profile.pendingRaid;
    if (!raid.credentialExtraction)
        return;
    if (!profile.raidExtractionPermissions.empty() ||
        raid.credentialExtraction->unlockedAtDeploy ||
        raid.credentialExtraction->investigationCompleted)
        throw std::logic_error{"legacy fixture cannot discard earned credential facts"};
    const auto isCredentialAnchor = [](const RaidAnchorPlacementSnapshot &anchor)
    {
        return anchor.id == kRaidAnchorCredentialExtraction &&
            anchor.kind == RaidMapAnchorKind::CredentialExtraction;
    };
    if (std::count_if(raid.spatialLayout.anchorPlacements.begin(),
            raid.spatialLayout.anchorPlacements.end(), isCredentialAnchor) != 1)
        throw std::logic_error{"legacy fixture expects exactly one credential anchor"};
    std::erase_if(raid.spatialLayout.anchorPlacements, isCredentialAnchor);
    raid.credentialExtraction.reset();
    raid.spatialLayout.layoutHash = raidMapLayoutHash(raid.spatialLayout);
}
