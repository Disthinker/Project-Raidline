#include "game_session.h"
#include "base_world.h"

#include <algorithm>
#include <cmath>

void GameSession::bindBasePlayerWorld(BaseWorld *world) noexcept { basePlayerWorld_ = world; }

bool GameSession::playerThrowFlightActive() const noexcept
{
    if (alphaRaidActive_ && world_)
        return world_->noiseDecoy().flight().has_value() || world_->grenade().flight().has_value() ||
            world_->thrownIllumination().flight().has_value();
    return basePlayerWorld_ && (basePlayerWorld_->noiseDecoy().flight().has_value() ||
        basePlayerWorld_->grenade().flight().has_value() ||
        basePlayerWorld_->thrownIllumination().flight().has_value());
}

PlayerGameplayContext GameSession::playerGameplayContext() const noexcept
{
    PlayerGameplayContext result;
    result.activeWeapon = activeAlphaWeapon();
    result.dead = profile_.currentHealth <= 0;
    if (alphaRaidActive_ && world_)
    {
        result.available = state_ == GameSessionState::InRaid && world_->raidSession().isActive();
        result.activity = developerRange_
            ? WeaponActionScope{WeaponActionActivity::Training, {}, profile_.profileId}
            : WeaponActionScope{WeaponActionActivity::Raid, {}, profile_.pendingRaid ? profile_.pendingRaid->raidId : std::string{}};
        const auto p = world_->player().position();
        const float size = world_->player().size();
        result.playerCenter = {p.x + size / 2, p.y + size / 2};
        result.playerSize = {size, size};
        result.facingDirection = world_->weaponAimDirection();
        result.worldSize = world_->playerWorldSize();
        result.spaceId = world_->activeRaidSpaceId();
        result.controlled = world_->player().isControlled();
        result.dead = world_->player().isDead();
    }
    else if (basePlayerWorld_ && !profile_.pendingRaid && state_ == GameSessionState::BetweenRaids)
    {
        const auto &w = *basePlayerWorld_;
        const RegionalBaseSiteDefinitionId site{w.siteDefinitionId()};
        result.available = true;
        result.activity = {baseDefenseActive() ? WeaponActionActivity::BaseDefense : WeaponActionActivity::BaseDaily,
            site, baseDefenseActive() ? profile_.activeBaseDefense->eventId : std::string{site.value()}};
        result.playerSize = w.playerSize();
        const auto p = w.playerPosition();
        result.playerCenter = {p.x + result.playerSize.x / 2, p.y + result.playerSize.y / 2};
        result.facingDirection = w.weaponAimDirection();
        result.worldSize = w.worldSize();
        result.spaceId = w.activeSpaceId();
        result.controlled = w.playerControlled();
        result.stashAccessible = w.canAccessStash();
        result.coreBuildAvailable = w.playerSafetyZone() == HomeRegionSafetyZone::SafeCore;
    }
    return result;
}

void GameSession::cancelPlayerControlIntent() noexcept
{
    weaponTriggerGate_.cancelUntilRelease();
    sprintFireIntentPending_ = false;
    sprintFireReadyRemaining_ = 0;
    weaponClearGesture_.reset();
}

std::optional<BaseCorpseRecord> GameSession::nearbyBaseCorpse() const
{
    const auto context = playerGameplayContext();
    if (!context.available || (!basePlayerWorld_ && !developerRange_) || (alphaRaidActive_ && !developerRange_) || context.dead || context.controlled) return std::nullopt;
    const auto key = developerRange_?world_->raidObservationKey():basePlayerWorld_->observationKey();
    const auto records = nearbyBaseCorpses(profile_, raidReloadAccess(key));
    if (records.empty()) return std::nullopt;
    return *std::min_element(records.begin(), records.end(), [&](const auto &a, const auto &b) {
        const auto distance = [&](Vec2 p) { const float x=p.x-context.playerCenter.x,y=p.y-context.playerCenter.y; return x*x+y*y; };
        return distance(a.position) < distance(b.position);
    });
}

InventoryPlan GameSession::queryBaseCorpseInspection(std::string_view corpseId) const
{
    if ((!basePlayerWorld_ && !developerRange_) || (alphaRaidActive_ && !developerRange_) || !playerGameplayContext().available)
        return {false, DomainErrorCode::IllegalDestination, "Base corpse is not accessible", profile_.revision};
    const auto key = developerRange_?world_->raidObservationKey():basePlayerWorld_->observationKey();
    return ::queryBaseCorpseInspection(profile_, publishedContentRegistry(), corpseId, raidReloadAccess(key));
}

bool GameSession::captureBasePlayerEffects(ProfileState &candidate) const
{
    if (!basePlayerWorld_ || alphaRaidActive_ || candidate.pendingRaid) return true;
    const auto context = playerGameplayContext();
    if (!context.available) return true;
    BasePlayerEffectsRecovery value;
    value.scope = context.activity;
    value.worldSize = context.worldSize;
    value.playerPosition = basePlayerWorld_->playerPosition();
    value.facingDirection = context.facingDirection;
    value.controlRemainingSeconds = basePlayerWorld_->playerControlRemaining();
    value.impactSlowRemainingSeconds = basePlayerWorld_->playerImpactSlowRemaining();
    value.medicalTickAccumulatorSeconds = medicalTickAccumulatorSeconds_;
    value.grenade = basePlayerWorld_->grenade().checkpoint();
    value.noiseDecoy = basePlayerWorld_->noiseDecoy().checkpoint();
    value.thrownIllumination = basePlayerWorld_->thrownIllumination().checkpoint();
    if (!baseDefenseActive())
    {
        value.dailyCommandSequence=raidCommandSequence_; value.dailyWeaponFaultSequence=weaponFaultSequence_;
        value.dailyMedicalRandomSequence=medicalRandomSequence_; value.dailyWoundRandomSequence=woundRandomSequence_;
        value.dailyActiveWeaponSlot=activeWeaponSlot_;
        value.dailyDamageProtectionSeconds=basePlayerWorld_->perimeterDamageProtectionRemainingSeconds_;
    }
    if (raidActionState_.active())
        if (const auto *medical=std::get_if<MedicalRaidAction>(&*raidActionState_.active()))
            value.medical=BaseMedicalActionRecovery{medical->medicalAssetId,medical->effect,medical->chargeConsumed,
                medical->healedAmount,medical->maximumHealing,medical->elapsedSeconds,medical->durationSeconds};
    candidate.basePlayerEffects=std::move(value);
    return validateBasePlayerEffectsRecovery(*candidate.basePlayerEffects,candidate,publishedContentRegistry()).valid;
}

bool GameSession::restoreBasePlayerEffects(BaseWorld &world)
{
    bindBasePlayerWorld(&world);
    if (!profile_.basePlayerEffects)
    {
        world.commitThrownIllumination({});
        return true;
    }
    const auto saved=*profile_.basePlayerEffects;
    if (!weaponActionScopeMatches(profile_,saved.scope) ||
        !validateBasePlayerEffectsRecovery(saved,profile_,publishedContentRegistry()).valid ||
        saved.worldSize.x!=world.worldSize().x || saved.worldSize.y!=world.worldSize().y) return false;
    RaidGrenadeRuntime grenade;
    RaidNoiseDecoyRuntime decoy;
    ThrownIlluminationRuntime illumination;
    const auto validIlluminationSpace = [&](const ThrownIlluminationLanding &landing) {
        return landing.space == world.activeSpaceId() &&
            queryHomeRegionSafetyZone(landing.origin, world.baseParcel()) != HomeRegionSafetyZone::SafeCore &&
            queryHomeRegionSafetyZone(landing.point, world.baseParcel()) != HomeRegionSafetyZone::SafeCore;
    };
    if (!grenade.restore(saved.grenade) || !decoy.restore(saved.noiseDecoy) ||
        !illumination.restore(saved.thrownIllumination) ||
        (saved.thrownIllumination.flight && !validIlluminationSpace(saved.thrownIllumination.flight->landing)) ||
        (saved.thrownIllumination.source && !validIlluminationSpace(saved.thrownIllumination.source->landing)) ||
        !world.restorePlayerBody(saved.playerPosition,saved.facingDirection,saved.controlRemainingSeconds,saved.impactSlowRemainingSeconds,baseDefenseActive())) return false;
    world.commitGrenade(std::move(grenade));
    world.commitNoiseDecoy(std::move(decoy));
    world.commitThrownIllumination(std::move(illumination));
    medicalTickAccumulatorSeconds_=saved.medicalTickAccumulatorSeconds;
    if (saved.scope.activity==WeaponActionActivity::BaseDaily)
    {
        raidCommandSequence_=std::max(raidCommandSequence_,saved.dailyCommandSequence);
        weaponFaultSequence_=saved.dailyWeaponFaultSequence; medicalRandomSequence_=saved.dailyMedicalRandomSequence;
        woundRandomSequence_=saved.dailyWoundRandomSequence; activeWeaponSlot_=saved.dailyActiveWeaponSlot;
        world.perimeterDamageProtectionRemainingSeconds_=saved.dailyDamageProtectionSeconds;
    }
    // Applied healing and consumed charges are Profile facts. An interrupted
    // action is not resumed, and a trigger group is never checkpointed.
    raidActionState_.cancel(); weaponTriggerGate_.reset();
    return true;
}

bool GameSession::preparePlayerActivityBoundary(bool requireFinishedThrowables)
{
    if(inventoryPersistencePending()) {
        requestInventoryPersistenceBarrier();updateInventoryPersistence(0);
        if(inventoryPersistencePending() || !inventoryPersistenceBarrierReady()) {
            persistenceMessage_="INVENTORY SAVE PENDING";return false;
        }
    }
    cancelPlayerControlIntent();
    if(!cancelActiveRaidAction())return false;
    if(basePlayerWorld_ && !alphaRaidActive_) {
        if(requireFinishedThrowables && (basePlayerWorld_->grenade().active() || basePlayerWorld_->noiseDecoy().flight() || basePlayerWorld_->noiseDecoy().source())) {
            persistenceMessage_="WAIT FOR ACTIVE THROWABLE EFFECT";return false;
        }
        if(!captureBasePlayerEffects(profile_))return false;
        if(profile_.basePlayerEffects)profile_.basePlayerEffects->medical.reset();
        worldClockDirty_=true;
    }
    return true;
}

bool GameSession::restoreBasePlayerRuntime(BaseWorld &world)
{
    if(profileUiCommitPending())return false;
    if(!restoreBasePlayerEffects(world))return false;
    if(!profile_.baseReloadRecovery)return true;
    const auto key=world.observationKey();
    ProfileState candidate=profile_;
    const auto recovered=recoverBaseReloadInCandidate(candidate,publishedContentRegistry(),raidReloadAccess(key));
    if(!recovered.succeeded){persistenceMessage_=recovered.message;return false;}
    if(candidate.basePlayerEffects)candidate.basePlayerEffects->medical.reset();
    if(!commitProfileCandidate(std::move(candidate)))return false;
    return !baseDefenseActive() || checkpointBaseDefense(true);
}

void GameSession::recordBaseEnemyDeaths(ProfileState &candidate)
{
    if(!basePlayerWorld_ || alphaRaidActive_)return;
    const auto context=playerGameplayContext();
    const auto site=candidate.homePerimeter.sites.find(context.activity.baseSiteDefinitionId);
    const std::uint64_t cycle=context.activity.activity==WeaponActionActivity::BaseDaily && site!=candidate.homePerimeter.sites.end()?site->second.cycleIndex:0;
    for(const auto &fact:basePlayerWorld_->baseEnemyDeathsLastUpdate()) {
        BaseCorpseRecord corpse{{},context.activity,fact.id,cycle,fact.center,fact.size};
        const auto id=baseCorpseIdentity(corpse);
        const auto receipt=recordBaseCorpseDeath(candidate,publishedContentRegistry(),std::move(corpse),{candidate.revision,id});
        if(!receipt.succeeded)persistenceMessage_=receipt.message;
    }
}

bool GameSession::drainProfileWritersForReplacement()
{
    if(profileUiCommitPending()){persistenceMessage_="WAIT FOR THE ACTIVE SAVE";return false;}
    if(inventoryPersistencePending())requestInventoryPersistenceBarrier();
    if(!drainBaseDailyCheckpoint())return false;
    if(baseDefenseWriter_) {
        const auto result=baseDefenseWriter_->flush();
        if(!result.succeeded){persistenceMessage_=result.message;return false;}
        updateInventoryPersistence(0);
        if(inventoryPersistencePending() || !inventoryPersistenceBarrierReady())return false;
        baseDefenseWriter_.reset();
    }
    if(inventoryPersistencePending())return false;
    return true;
}
