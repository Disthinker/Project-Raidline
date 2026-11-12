#include "game_session.h"
#include "base_world.h"
#include "grenade_domain.h"
#include "profile_combat_domain.h"
#include "thrown_illumination_domain.h"
#include <exception>

bool GameSession::canSelectThrowable() const
{
    if(profileUiCommitPending())return false;
    const auto context=playerGameplayContext();
    if(!context.available || context.dead || context.controlled)return false;
    if (alphaRaidActive_)
        return world_->grenadeEnabled() && !world_->grenade().active() &&
            !world_->noiseDecoy().flight() && !world_->thrownIllumination().flight() &&
            world_->melee().recoverySeconds() <= 0;
    return basePlayerWorld_ && !basePlayerWorld_->grenade().active() &&
        !basePlayerWorld_->noiseDecoy().flight() && !basePlayerWorld_->thrownIllumination().flight() &&
        basePlayerWorld_->melee().recoverySeconds() <= 0;
}
bool GameSession::beginThrowableSelection()
{
    if(!canSelectThrowable()){persistenceMessage_="THROWABLE UNAVAILABLE HERE";return false;}
    if(raidActionState_.active()) {
        if (!cancelActiveRaidAction()) { state_=GameSessionState::SettlementBlocked; return false; }
        persistenceMessage_="ACTION CANCELLED | PRESS 4 AGAIN";return false;
    }
    return true;
}
InventoryReceipt GameSession::throwThrowable(AssetInstanceId id, Vec2 aim, const CommandContext &context)
{
    if (!context.transactionId.empty() && profile_.committedTransactions.contains(context.transactionId))
        return {true, true, DomainErrorCode::None, {}, profile_.revision};
    const auto &content = publishedContentRegistry();
    const auto *asset = profile_.assets.find(id);
    const auto *definition = asset ? &content.item(asset->definitionId) : nullptr;
    if (definition && definition->noiseDecoyUse)
        return throwNoiseDecoy(id, aim, context);

    const auto reject = [&](const char *message) {
        persistenceMessage_ = message;
        return InventoryReceipt{false, false, DomainErrorCode::IllegalDestination, message, profile_.revision};
    };
    if (!canSelectThrowable() || raidActionState_.active())
        return reject("THROWABLE UNAVAILABLE HERE");
    if (inventoryPersistencePending())
    {
        requestInventoryPersistenceBarrier();
        return reject("INVENTORY SAVE PENDING");
    }
    if (context.expectedRevision != profile_.revision)
        return reject("THROWABLE INVENTORY CHANGED");

    const auto finish = [&](const InventoryReceipt &receipt, const char *success) {
        if (receipt.succeeded && !receipt.alreadyCommitted)
        {
            worldClockDirty_ = true;
            cancelPlayerControlIntent();
            // Base checkpoints persist this effect and the inventory together.
            static_cast<void>(captureBasePlayerEffects(profile_));
            persistenceMessage_ = success;
        }
        else if (!receipt.succeeded)
            persistenceMessage_ = receipt.message;
    };
    if (definition && definition->thrownIlluminationUse)
    {
        if (!thrownIlluminationInChestRig(profile_, content, id))
            return reject("NO CHEST RIG ILLUMINATION");
        const auto &current = alphaRaidActive_
            ? world_->thrownIllumination() : basePlayerWorld_->thrownIllumination();
        if (current.active()) return reject("ILLUMINATION ALREADY ACTIVE");

        const auto &use = *definition->thrownIlluminationUse;
        auto landing = alphaRaidActive_
            ? world_->previewThrownIllumination(aim, use)
            : basePlayerWorld_->previewThrownIllumination(aim, use);
        if (!landing) return reject("INVALID ILLUMINATION LANDING");
        landing->effectIdentity = context.transactionId;
        ThrownIlluminationRuntime prepared;
        if (!prepared.start(*landing)) return reject("INVALID ILLUMINATION COMMAND");

        auto receipt = consumeThrownIllumination(profile_, content, id, context);
        if (receipt.succeeded && !receipt.alreadyCommitted)
        {
            if (alphaRaidActive_) world_->commitThrownIllumination(std::move(prepared));
            else basePlayerWorld_->commitThrownIllumination(std::move(prepared));
        }
        finish(receipt, "ILLUMINATION THROWN");
        return receipt;
    }
    if (!grenadeInChestRig(profile_, content, id)) return reject("NO CHEST RIG GRENADE");
    const auto &use = *definition->grenadeUse;
    auto landing = alphaRaidActive_ ? world_->previewGrenade(aim, use) : basePlayerWorld_->previewGrenade(aim, use);
    if (!landing) return reject("INVALID GRENADE LANDING");
    landing->effectIdentity = context.transactionId;
    auto prepared = alphaRaidActive_ ? world_->grenade() : basePlayerWorld_->grenade();
    if (!prepared.start(*landing)) return reject("THROWABLE UNAVAILABLE HERE");

    auto receipt = consumeGrenade(profile_, content, id, context);
    if (receipt.succeeded && !receipt.alreadyCommitted)
    {
        if (alphaRaidActive_) world_->commitGrenade(std::move(prepared));
        else basePlayerWorld_->commitGrenade(std::move(prepared));
    }
    finish(receipt, "GRENADE THROWN");
    return receipt;
}
bool GameSession::processGrenadeExplosion(std::optional<IncomingDamageCommand> *dailyDamage)
{
    if(!alphaRaidActive_) {
        if(!basePlayerWorld_ || !basePlayerWorld_->grenade().pendingExplosion())return true;
        const auto plan=basePlayerWorld_->prepareGrenadeExplosion(profile_.currentHealth);
        if(!plan || !basePlayerWorld_->validatePreparedGrenadeExplosion(*plan)) {persistenceMessage_="GRENADE RESOLUTION BLOCKED";return false;}
        if(plan->playerDamage) {
            const auto &damage=*plan->playerDamage;
            const IncomingDamageCommand command{damage.baseDamage,damage.region,damage.penetration,damage.armorDamage,false,{}};
            if(!baseDefenseActive()) {
                // Daily rescue owns the complete private damage/result
                // transaction in updateBaseWorld. Never publish transient 0HP.
                const auto query=queryIncomingDamage(profile_,publishedContentRegistry(),command);
                if(!dailyDamage || !query.canCommit){persistenceMessage_=query.message.empty()?"DAILY GRENADE DAMAGE UNAVAILABLE":query.message;return false;}
                *dailyDamage=command;
            } else {
                const auto receipt=executeIncomingDamageInSimulation(profile_,publishedContentRegistry(),command,
                    {profile_.revision,nextRaidTransaction("base-grenade-damage")});
                if(!receipt.succeeded){persistenceMessage_=receipt.message;return false;}
                lastIncomingDamage_=receipt.resolution;
                if(receipt.resolution.damageApplied>0)basePlayerWorld_->applyPlayerImpactSlow(0.18F);
            }
        }
        if(!basePlayerWorld_->commitPreparedGrenadeExplosion(*plan))std::terminate();
        worldClockDirty_=true;persistenceMessage_="GRENADE EXPLODED";return true;
    }
    if(!world_->raidSession().isActive()){world_->commitGrenade({});return true;}
    if(!world_->grenade().pendingExplosion())return true;
    const auto plan=world_->prepareGrenadeExplosion();
    if(!plan || !world_->validatePreparedGrenadeExplosion(*plan)) {
        persistenceMessage_="GRENADE RESOLUTION BLOCKED";state_=GameSessionState::SettlementBlocked;return false;
    }
    int playerDamage{};
    if(plan->playerDamage) {
        const auto &damage=*plan->playerDamage;
        // The deployed Profile is already validated. This command checks every
        // changed participant and allocates its ledger entry before no-throw
        // mutations; copying the unrelated frozen megamap is not a damage step.
        const auto receipt=executeIncomingDamageInSimulation(profile_,publishedContentRegistry(),
            {damage.baseDamage,damage.region,damage.penetration,damage.armorDamage,false,{}},
            {profile_.revision,nextRaidTransaction("grenade-damage")});
        if(!receipt.succeeded) {
            persistenceMessage_="GRENADE RESOLUTION BLOCKED";state_=GameSessionState::SettlementBlocked;return false;
        }
        playerDamage=receipt.resolution.damageApplied;lastIncomingDamage_=receipt.resolution;
    }
    // No world update/callback intervenes between validation and this commit.
    if(!world_->commitPreparedGrenadeExplosion(*plan))std::terminate();
    if(playerDamage>0)static_cast<void>(world_->damagePlayer(playerDamage));
    persistenceMessage_="GRENADE EXPLODED";
    return true;
}
