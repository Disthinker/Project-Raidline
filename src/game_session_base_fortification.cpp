#include "base_fortification_placement.h"
#include "game_session.h"
#include "base_ground_domain.h"
#include "collision.h"
#include <algorithm>

FortificationReceipt GameSession::repairBaseFortification(const BaseWorld &world, FortificationInstanceId id)
{
    const auto &content = publishedContentRegistry();
    const auto plan = queryFortificationCommand(profile_, content, RepairFortificationCommand{id});
    if (!plan.canCommit) return executeBaseFortification(RepairFortificationCommand{id});
    const auto &record = profile_.baseFortifications.instances.at(id);
    if (record.slot)
    {
        const auto candidates = baseDefensePositionCandidates(world.layout(), world.plotId(),
            *content.findFortification(record.definition));
        const auto &position = candidates[static_cast<std::size_t>(record.slot->side)];
        const auto r = position.footprint;
        const Rect footprint{r.position, r.size};
        bool blocked = position.key != *record.slot || !position.available ||
            isCollision(footprint, {world.playerPosition(), world.playerSize()});
        for (const auto &enemy : world.perimeterEnemies())
            blocked |= isCollision(footprint, {enemy.position(), enemy.size()});
        for (const auto &f : world.facilities())
            if (f.active) blocked |= isCollision(footprint, f.bounds);
        for (const auto &r : world.layout().movementBlockers)
            blocked |= isCollision(footprint, {r.position, r.size});
        for (const auto &a : projectBaseGroundAssets(profile_, record.slot->site))
        {
            const auto &item = content.item(a.definitionId);
            Vec2 size = item.basePlacement ? item.basePlacement->footprint : item.worldRenderSize;
            size.x = std::max(size.x, item.pickupSize.x);
            size.y = std::max(size.y, item.pickupSize.y);
            if (a.orientation == ItemOrientation::Degrees90 || a.orientation == ItemOrientation::Degrees270)
                std::swap(size.x, size.y);
            blocked |= isCollision(footprint, {{a.position.x-size.x/2, a.position.y-size.y/2}, size});
        }
        if (blocked)
        {
            FortificationReceipt receipt;
            receipt.revision = profile_.revision;
            receipt.error = DomainErrorCode::IllegalDestination;
            receipt.message = "Clear the barricade footprint before repair";
            return receipt;
        }
    }
    return executeBaseFortification(RepairFortificationCommand{id});
}

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
