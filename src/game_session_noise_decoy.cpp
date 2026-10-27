#include "game_session.h"

bool GameSession::canSelectNoiseDecoy() const
{
    return alphaRaidActive_ && !developerRange_ && state_==GameSessionState::InRaid &&
        profile_.pendingRaid && world_ && world_->noiseDecoyEnabled() && world_->raidSession().isActive() &&
        !world_->player().isDead() && !world_->player().isControlled() &&
        !world_->noiseDecoy().flight() && !world_->grenade().active() && world_->melee().recoverySeconds()<=0;
}
bool GameSession::beginNoiseDecoySelection()
{
    if(!canSelectNoiseDecoy()){persistenceMessage_="DECOY UNAVAILABLE HERE";return false;}
    if(raidActionState_.active()){
        raidActionState_.cancel();persistenceMessage_="ACTION CANCELLED | PRESS 4 AGAIN";return false;}
    return true;
}
InventoryReceipt GameSession::throwNoiseDecoy(AssetInstanceId id,Vec2 aim,const CommandContext &context)
{
    if(!context.transactionId.empty() && profile_.committedTransactions.contains(context.transactionId))
        return {true,true,DomainErrorCode::None,{},profile_.revision};
    const auto reject=[&](const char *message){persistenceMessage_=message;
        return InventoryReceipt{false,false,DomainErrorCode::IllegalDestination,message,profile_.revision};};
    if(!canSelectNoiseDecoy() || raidActionState_.active())return reject("DECOY UNAVAILABLE HERE");
    if(context.expectedRevision!=profile_.revision)return reject("DECOY INVENTORY CHANGED");
    if(!noiseDecoyInChestRig(profile_,publishedContentRegistry(),id))return reject("NO CHEST RIG DECOY");
    const auto &use=*publishedContentRegistry().item(profile_.assets.find(id)->definitionId).noiseDecoyUse;
    const auto landing=world_->previewNoiseDecoy(aim,use);
    if(!landing)return reject("INVALID DECOY LANDING");
    // Prepare all runtime allocations before committing the inventory. No
    // world/sound/registry participant changes on any rejected command.
    auto prepared=world_->noiseDecoy();
    if(!prepared.start(*landing))return reject("DECOY UNAVAILABLE HERE");
    auto receipt=consumeNoiseDecoy(profile_,publishedContentRegistry(),id,context);
    if(receipt.succeeded && !receipt.alreadyCommitted){
        world_->commitNoiseDecoy(std::move(prepared));persistenceMessage_="DECOY THROWN";
        weaponClearGesture_.reset();sprintFireIntentPending_=false;sprintFireReadyRemaining_=0;
    }else if(!receipt.succeeded)persistenceMessage_=receipt.message;
    return receipt;
}
void GameSession::prepareNoiseDecoyInput(GameplayInput &input,const GameplayInput &raw)
{
    const bool blocked=input.throwableModal || world_->noiseDecoy().flight().has_value() || world_->grenade().flight().has_value();
    if(!raw.firePressed && !raw.fireJustPressed)decoyFireSuppressedUntilRelease_=false;
    if(blocked && (raw.firePressed || raw.fireJustPressed))decoyFireSuppressedUntilRelease_=true;
    if(blocked || decoyFireSuppressedUntilRelease_)input.firePressed=input.fireJustPressed=false;
    if(!blocked)return;
    cancelWeaponLightInput();
    input.sprint=false;input.aimDownSights=false;input.reloadJustPressed=input.healJustPressed=false;
    input.interactPressed=input.interactJustPressed=false;input.weaponSlotJustPressed.reset();
    input.meleeJustPressed=input.meleePressed=false;
    input.lightJustPressed=input.lightJustReleased=input.lightPressed=false;
    input.quitRaidJustPressed=false;
    sprintFireIntentPending_=false;sprintFireReadyRemaining_=0;weaponClearGesture_.reset();
}
