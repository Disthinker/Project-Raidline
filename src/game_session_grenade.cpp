#include "game_session.h"
#include "base_world.h"
#include "grenade_domain.h"
#include "profile_combat_domain.h"
#include "thrown_illumination_domain.h"
#include <exception>

bool GameSession::ordinaryRaidThrowablesAllowed() const noexcept
{
    return alphaRaidActive_ && !developerRange_ && profile_.pendingRaid && world_ &&
        state_ == GameSessionState::InRaid && world_->raidSession().isActive() && world_->grenadeEnabled();
}

bool GameSession::canSelectThrowable() const
{
    if(profileUiCommitPending())return false;
    const auto context=playerGameplayContext();
    if(!context.available || context.dead || context.controlled || playerThrowFlightActive())return false;
    if (alphaRaidActive_)
        return world_->grenadeEnabled() && !world_->grenade().active() &&
            world_->melee().recoverySeconds() <= 0;
    return basePlayerWorld_ && !basePlayerWorld_->grenade().active() &&
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
    if (definition && (definition->smokeUse || definition->fireZoneUse) && !ordinaryRaidThrowablesAllowed())
        return reject(definition->smokeUse ? "SMOKE UNAVAILABLE HERE" : "FIRE BOTTLE UNAVAILABLE HERE");
    if (definition && definition->alarmUse && !ordinaryRaidThrowablesAllowed())
        return reject("ALARM UNAVAILABLE HERE");
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
    if (definition && definition->alarmUse)
    {
        if (!proximityAlarmInChestRig(profile_, content, id)) return reject("NO CHEST RIG ALARM");
        if (!world_->proximityAlarms().canPlace()) return reject("ALARM LIMIT REACHED");
        auto placement = world_->previewProximityAlarm(aim, *definition->alarmUse);
        if (!placement) return reject("INVALID ALARM PLACEMENT");
        auto prepared = world_->proximityAlarms();
        if (!prepared.place(std::move(*placement))) return reject("INVALID ALARM PLACEMENT");
        auto receipt = consumeProximityAlarm(profile_, content, id, context);
        if (receipt.succeeded && !receipt.alreadyCommitted)
            world_->commitProximityAlarms(std::move(prepared));
        finish(receipt, "ALARM DEPLOYED");
        return receipt;
    }
    if (definition && definition->smokeUse)
    {
        if (!smokeInChestRig(profile_, content, id))
            return reject("NO CHEST RIG SMOKE");
        if (!world_->smoke().canStart())
            return reject("SMOKE LIMIT REACHED");
        auto landing = world_->previewSmoke(aim, *definition->smokeUse);
        if (!landing) return reject("INVALID SMOKE LANDING");
        auto prepared = world_->smoke();
        if (!prepared.start(std::move(*landing))) return reject("INVALID SMOKE LANDING");
        auto receipt = consumeSmoke(profile_, content, id, context);
        if (receipt.succeeded && !receipt.alreadyCommitted)
            world_->commitSmoke(std::move(prepared));
        finish(receipt, "SMOKE THROWN");
        return receipt;
    }
    if (definition && definition->fireZoneUse)
    {
        if (!fireBottleInChestRig(profile_, content, id))
            return reject("NO CHEST RIG FIRE BOTTLE");
        if (!world_->fireZones().canStart()) return reject("FIRE ZONE LIMIT REACHED");
        auto landing = world_->previewFireZone(aim, *definition->fireZoneUse);
        if (!landing) return reject("INVALID FIRE BOTTLE LANDING");
        auto prepared = world_->fireZones();
        if (!prepared.start(std::move(*landing))) return reject("INVALID FIRE BOTTLE LANDING");
        auto receipt = consumeFireBottle(profile_, content, id, context);
        if (receipt.succeeded && !receipt.alreadyCommitted)
            world_->commitFireZone(std::move(prepared));
        finish(receipt, "FIRE BOTTLE THROWN");
        return receipt;
    }
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
    while (world_->raidSession().isActive() && world_->hasPendingRaidExplosion())
    {
        if (!commitPendingRaidExplosion()) return false;
    }
    while (world_->raidSession().isActive() && world_->hasPendingFireDamage())
    {
        if (!commitPendingFireDamage()) return false;
    }
    return true;
}

std::optional<int> GameSession::applyRaidAreaPlayerDamage(const std::optional<CombatDamageCommand> &damage,
    const char *transactionKind)
{
    if (!damage) return 0;
    const auto receipt = executeIncomingDamageInSimulation(profile_, publishedContentRegistry(),
        {damage->baseDamage, damage->region, damage->penetration, damage->armorDamage, false, {}},
        {profile_.revision, nextRaidTransaction(transactionKind)});
    if (!receipt.succeeded) return std::nullopt;
    lastIncomingDamage_ = receipt.resolution;
    return receipt.resolution.damageApplied;
}

bool GameSession::commitPendingFireDamage()
{
    const auto tick = world_->prepareFireDamageTick();
    if (!tick || !world_->validatePreparedFireDamageTick(*tick))
    {
        persistenceMessage_ = "FIRE ZONE RESOLUTION BLOCKED";
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    const auto damage = applyRaidAreaPlayerDamage(tick->playerDamage, "fire-zone-damage");
    if (!damage)
    {
        persistenceMessage_ = "FIRE ZONE RESOLUTION BLOCKED";
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    if (!world_->commitPreparedFireDamageTick(*tick)) std::terminate();
    if (*damage > 0) static_cast<void>(world_->damagePlayer(*damage));
    return true;
}

bool GameSession::commitPendingRaidExplosion()
{
    const auto plan=world_->prepareGrenadeExplosion();
    if(!plan || !world_->validatePreparedGrenadeExplosion(*plan)) {
        persistenceMessage_="GRENADE RESOLUTION BLOCKED";state_=GameSessionState::SettlementBlocked;return false;
    }
    const auto playerDamage = applyRaidAreaPlayerDamage(plan->playerDamage,
        plan->barrelId ? "barrel-damage" : "grenade-damage");
    if (!playerDamage)
    {
        persistenceMessage_ = "GRENADE RESOLUTION BLOCKED";
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    // No world update/callback intervenes between validation and this commit.
    if(!world_->commitPreparedGrenadeExplosion(*plan))std::terminate();
    if (*playerDamage > 0) static_cast<void>(world_->damagePlayer(*playerDamage));
    persistenceMessage_ = plan->barrelId ? "VOLATILE BARREL DESTROYED" : "GRENADE EXPLODED";
    return true;
}
