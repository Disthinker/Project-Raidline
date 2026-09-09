#include "game_session.h"

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
