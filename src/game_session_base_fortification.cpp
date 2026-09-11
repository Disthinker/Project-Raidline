#include "base_fortification_placement.h"
#include "game_session.h"

FortificationPlacementPlan GameSession::queryBaseFortificationPlacement(
    const BaseWorld &world, const InstallFortificationCommand &command) const
{
    if (alphaRaidActive_ || state_ != GameSessionState::BetweenRaids)
    {
        FortificationPlacementPlan plan;
        plan.revision = profile_.revision;
        plan.instance = command.instance;
        plan.error = DomainErrorCode::IllegalDestination;
        plan.placementFailure = FortificationPlacementFailure::ActivityLocked;
        plan.message = "Fortifications are only available in Base";
        return plan;
    }
    return queryFortificationPlacement(profile_, publishedContentRegistry(), world, command);
}

FortificationReceipt GameSession::installBaseFortification(
    const BaseWorld &world, const InstallFortificationCommand &command,
    ProfileRevision expectedRevision)
{
    FortificationReceipt receipt;
    receipt.revision = profile_.revision;
    if (alphaRaidActive_ || state_ != GameSessionState::BetweenRaids)
    {
        receipt.error = DomainErrorCode::IllegalDestination;
        receipt.message = "Fortifications are only available in Base";
        return receipt;
    }
    ProfileState candidate = profile_;
    receipt = executeFortificationPlacement(
        candidate, publishedContentRegistry(), world, command,
        {expectedRevision, "fortification-install:" + std::to_string(profile_.revision)});
    if (!receipt.succeeded || receipt.alreadyCommitted)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        receipt.instance = {};
    }
    return receipt;
}

FortificationReceipt GameSession::executeBaseFortification(const FortificationCommand &command)
{
    FortificationReceipt receipt;
    receipt.revision = profile_.revision;
    if (alphaRaidActive_ || state_ != GameSessionState::BetweenRaids)
    {
        receipt.error = DomainErrorCode::IllegalDestination;
        receipt.message = "Fortifications are only available in Base";
        return receipt;
    }
    ProfileState candidate = profile_;
    receipt = executeFortificationCommand(
        candidate, publishedContentRegistry(), command,
        {profile_.revision, "fortification:" + std::to_string(profile_.revision)});
    if (!receipt.succeeded || receipt.alreadyCommitted)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        receipt.instance = {};
        receipt.materialSpent = 0;
    }
    return receipt;
}
