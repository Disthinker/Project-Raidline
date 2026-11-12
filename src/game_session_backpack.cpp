#include "game_session.h"

#include <algorithm>
#include <type_traits>

namespace
{
std::vector<AssetInstanceId> backpackActionParticipants(const RaidAction &active)
{
    return std::visit([](const auto &action) {
        using Action = std::decay_t<decltype(action)>;
        std::vector<AssetInstanceId> assets;
        if constexpr (requires { action.weaponAssetId; }) assets.push_back(action.weaponAssetId);
        if constexpr (requires { action.magazineAssetId; }) assets.push_back(action.magazineAssetId);
        if constexpr (requires { action.ammunitionAssetId; }) assets.push_back(action.ammunitionAssetId);
        if constexpr (requires { action.medkitAssetId; }) assets.push_back(action.medkitAssetId);
        if constexpr (requires { action.medicalAssetId; }) assets.push_back(action.medicalAssetId);
        if constexpr (requires { action.kitAssetId; }) assets.push_back(action.kitAssetId);
        if constexpr (requires { action.armorAssetId; }) assets.push_back(action.armorAssetId);
        if constexpr (std::is_same_v<Action, ReloadRaidAction>)
        {
            if (action.plan.binding.oldMagazineAssetId)
                assets.push_back(*action.plan.binding.oldMagazineAssetId);
            assets.push_back(action.plan.newSource.location.container.ownerAssetId);
            if (action.plan.oldReturn)
                assets.push_back(action.plan.oldReturn->location.container.ownerAssetId);
        }
        if constexpr (std::is_same_v<Action, UnloadMagazineRaidAction>)
            assets.push_back(action.destination.ownerAssetId);
        return assets;
    }, active);
}

bool overlaps(Rect a, Rect b) noexcept
{
    return a.position.x < b.position.x + b.size.x && b.position.x < a.position.x + a.size.x &&
        a.position.y < b.position.y + b.size.y && b.position.y < a.position.y + a.size.y;
}
}

InventoryReceipt GameSession::dropRaidBackpack(AssetInstanceId id, const CommandContext &command)
{
    if (!command.transactionId.empty() && profile_.committedTransactions.contains(command.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile_.revision};

    const auto reject = [&](const char *message) {
        return InventoryReceipt{false, false, DomainErrorCode::IllegalDestination, message, profile_.revision};
    };
    if (!alphaRaidActive_ || !world_ || developerRange_ || !profile_.pendingRaid ||
        !profile_.pendingRaid->backpackRecoveryEnabled)
        return reject("BACKPACK DROP UNAVAILABLE");

    const auto context = playerGameplayContext();
    if (!context.available || context.dead || context.controlled || profileUiCommitPending())
        return reject("BACKPACK DROP UNAVAILABLE");

    const Rect bounds = raidBackpackGroundBounds(context.playerCenter);
    if (bounds.position.x < 0 || bounds.position.y < 0 ||
        bounds.position.x + bounds.size.x > context.worldSize.x ||
        bounds.position.y + bounds.size.y > context.worldSize.y ||
        std::any_of(world_->ballisticBlockers().begin(), world_->ballisticBlockers().end(),
            [bounds](const BallisticBlocker &blocker) { return overlaps(bounds, blocker.bounds); }))
        return reject("NO ROOM TO DROP BACKPACK");

    const auto key = world_->raidObservationKey();
    auto access = raidReloadAccess(key);
    access.activity = context.activity;
    const auto &activeAction = raidActionState_.active();
    const auto locked = activeAction ? backpackActionParticipants(*activeAction)
                                     : std::vector<AssetInstanceId>{};
    droppedRaidBackpackIds_.reserve(droppedRaidBackpackIds_.size() + 1);
    auto receipt = ::dropRaidBackpack(profile_, publishedContentRegistry(), id,
        context.playerCenter, command, access, locked);
    if (receipt.succeeded && !receipt.alreadyCommitted)
        droppedRaidBackpackIds_.push_back(id);
    return receipt;
}

std::optional<Vec2> GameSession::observableRaidBackpackPosition(AssetInstanceId id) const
{
    if (!world_ || !profile_.pendingRaid || !profile_.pendingRaid->backpackRecoveryEnabled)
        return std::nullopt;
    const auto *asset = profile_.assets.find(id);
    const auto *drop = asset ? std::get_if<RaidDroppedBackpackLocation>(&asset->location) : nullptr;
    if (!drop || drop->raidId != profile_.pendingRaid->raidId ||
        drop->spaceId != world_->activeRaidSpaceId())
        return std::nullopt;
    const auto &view = world_->raidObservation();
    const bool atObserver = drop->position.x == view.observer.x && drop->position.y == view.observer.y;
    if (!atObserver && (!world_->raidBoundsObservable(raidBackpackGroundBounds(drop->position)) ||
        !view.pointVisible(drop->position)))
        return std::nullopt;
    return drop->position;
}
