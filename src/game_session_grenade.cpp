#include "game_session.h"
#include "grenade_domain.h"
#include "profile_combat_domain.h"
#include <exception>

bool GameSession::canSelectThrowable() const
{
    return alphaRaidActive_ && !developerRange_ && state_==GameSessionState::InRaid && profile_.pendingRaid &&
        world_ && world_->raidSession().isActive() && (world_->grenadeEnabled() || world_->noiseDecoyEnabled()) &&
        !world_->player().isDead() && !world_->player().isControlled() && !world_->grenade().active() &&
        !world_->noiseDecoy().flight() && world_->melee().recoverySeconds()<=0;
}
bool GameSession::beginThrowableSelection()
{
    if(!canSelectThrowable()){persistenceMessage_="THROWABLE UNAVAILABLE HERE";return false;}
    if(raidActionState_.active()) {
        raidActionState_.cancel();persistenceMessage_="ACTION CANCELLED | PRESS 4 AGAIN";return false;
    }
    return true;
}
InventoryReceipt GameSession::throwThrowable(AssetInstanceId id,Vec2 aim,const CommandContext &context)
{
    if(!context.transactionId.empty() && profile_.committedTransactions.contains(context.transactionId))
        return {true,true,DomainErrorCode::None,{},profile_.revision};
    const auto *asset=profile_.assets.find(id);
    if(asset && publishedContentRegistry().item(asset->definitionId).noiseDecoyUse)
        return throwNoiseDecoy(id,aim,context);
    const auto reject=[&](const char *message) {
        persistenceMessage_=message;
        return InventoryReceipt{false,false,DomainErrorCode::IllegalDestination,message,profile_.revision};
    };
    if(!canSelectThrowable() || !world_->grenadeEnabled() || raidActionState_.active())return reject("THROWABLE UNAVAILABLE HERE");
    if(context.expectedRevision!=profile_.revision)return reject("THROWABLE INVENTORY CHANGED");
    if(!grenadeInChestRig(profile_,publishedContentRegistry(),id))return reject("NO CHEST RIG GRENADE");
    const auto &use=*publishedContentRegistry().item(asset->definitionId).grenadeUse;
    const auto landing=world_->previewGrenade(aim,use);
    if(!landing)return reject("INVALID GRENADE LANDING");
    auto prepared=world_->grenade();
    if(!prepared.start(*landing))return reject("THROWABLE UNAVAILABLE HERE");
    auto receipt=consumeGrenade(profile_,publishedContentRegistry(),id,context);
    if(receipt.succeeded && !receipt.alreadyCommitted) {
        world_->commitGrenade(std::move(prepared));persistenceMessage_="GRENADE THROWN";
        weaponClearGesture_.reset();sprintFireIntentPending_=false;sprintFireReadyRemaining_=0;
    } else if(!receipt.succeeded)persistenceMessage_=receipt.message;
    return receipt;
}
bool GameSession::processGrenadeExplosion()
{
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
