#include "base_morale_domain.h"
#include "base_world.h"
#include "game_session.h"
#include "stable_random.h"

#include <algorithm>
#include <cmath>
#include <limits>

bool GameSession::baseDefenseActive() const noexcept
{
    return profile_.activeBaseDefense.has_value();
}

bool GameSession::baseDefenseSaveBlocked() const noexcept { return baseDefenseSaveBlocked_; }

BaseDefenseCheckpointStatus GameSession::baseDefenseCheckpointStatus() const
{
    return baseDefenseWriter_ ? baseDefenseWriter_->snapshot() : BaseDefenseCheckpointStatus{};
}

bool GameSession::triggerDeveloperBaseSiegeWarning()
{
    if (alphaRaidActive_ || profile_.pendingRaid || baseDefenseActive() ||
        !profile_.homeFounding.established ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
        return false;
    if (profile_.baseSiege.warningActive)
        return true;
    ProfileState candidate = profile_;
    candidate.baseSiege.safeUntilWorldMinute = candidate.worldClock.elapsedWorldMinutes;
    candidate.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    candidate.baseSiege.populationThreatUnits = 0U;
    candidate.baseSiege.siteThreatUnits = 0U;
    if (!activateBaseSiegeWarningIfEligible(candidate))
        return false;
    ++candidate.revision;
    return commitProfileCandidate(std::move(candidate));
}

bool GameSession::startBaseRealtimeDefense(BaseWorld &world)
{
    if (baseDefenseActive())
        return restoreBaseDefenseRuntime(world);
    if (alphaRaidActive_ || profile_.pendingRaid || state_ != GameSessionState::BetweenRaids ||
        !profile_.baseSiege.warningActive)
        return false;
    BaseDefenseSnapshot inputs;
    inputs.eventId = baseSiegeEventId(profile_);
    inputs.siegeSequence = profile_.baseSiege.siegeSequence;
    inputs.siteDefinitionId = world.siteDefinitionId();
    inputs.plotId = world.plotId();
    inputs.seed = profileStateFingerprint(profile_) ^ 0x626173652d736965ULL;
    if (inputs.seed == 0U)
        inputs.seed = 1U;
    inputs.frozenPopulation = profile_.basePopulation.ordinaryResidents;
    inputs.frozenMoraleTier = static_cast<std::uint32_t>(profile_.baseMorale.tier);
    for (const auto &site : publishedContentRegistry().regionalOperations().baseSites)
        if (site.nodeId == profile_.regionalOperations.activeBaseNodeId)
            inputs.frozenSiteThreat = site.dailyBaseThreatUnits;
    world.configureGroundBlockers(
        projectBaseGroundMovementBlockers(profile_, publishedContentRegistry(),
                                          RegionalBaseSiteDefinitionId{world.siteDefinitionId()}));
    // Equipment can change while a facility UI pauses Base simulation. Freeze
    // the equipped weapon, not the previous/default runtime configuration.
    synchronizeActiveBaseWeapon(world);
    auto prepared = world.prepareBaseDefenseSnapshot(std::move(inputs),
        publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    if (!prepared)
    {
        persistenceMessage_ = "NO LEGAL DEFENSE APPROACH | AUTO DEFENSE REMAINS AVAILABLE";
        return false;
    }
    prepared->activeWeaponSlot = static_cast<std::uint32_t>(activeWeaponSlot_);
    prepared->commandSequence = raidCommandSequence_;
    prepared->weaponFaultSequence = weaponFaultSequence_;
    prepared->medicalRandomSequence = medicalRandomSequence_;
    prepared->woundRandomSequence = woundRandomSequence_;
    prepared->medicalTickAccumulatorSeconds = medicalTickAccumulatorSeconds_;
    prepared->pendingWorldSeconds = pendingWorldSeconds_;
    prepared->baseCombatElapsedSeconds = baseCombatElapsedSeconds_;
    ProfileState candidate = profile_;
    const auto receipt =
        executeBaseRealtimeDefenseStart(candidate, publishedContentRegistry(), *prepared,
                                        {profile_.revision, prepared->eventId + ":start"});
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        return false;
    }
    if (!commitProfileCandidate(std::move(candidate)))
        return false;
    return restoreBaseDefenseRuntime(world);
}

bool GameSession::restoreBaseDefenseRuntime(BaseWorld &world)
{
    if (!baseDefenseActive())
    {
        world.clearBaseDefense();
        baseDefenseWorld_ = nullptr;
        return true;
    }
    const auto &saved = *profile_.activeBaseDefense;
    activeWeaponSlot_ = static_cast<EquipmentSlotKind>(saved.activeWeaponSlot);
    raidCommandSequence_ = std::max(raidCommandSequence_, saved.commandSequence);
    weaponFaultSequence_ = saved.weaponFaultSequence;
    medicalRandomSequence_ = saved.medicalRandomSequence;
    woundRandomSequence_ = saved.woundRandomSequence;
    pendingWorldSeconds_ = saved.pendingWorldSeconds;
    baseCombatElapsedSeconds_ = saved.baseCombatElapsedSeconds;
    medicalTickAccumulatorSeconds_ = saved.medicalTickAccumulatorSeconds;
    configuredBaseWeaponAssetId_.reset();
    synchronizeActiveBaseWeapon(world);
    if (!world.resumeBaseDefense(saved))
    {
        baseDefenseSaveBlocked_ = true;
        persistenceMessage_ = "BASE DEFENSE RESTORE FAILED | SAVE PRESERVED";
        return false;
    }
    baseDefenseWorld_ = &world;
    baseDefenseSaveBlocked_ = false;
    baseDefenseSimulationRejected_ = false;
    baseDefenseLagPause_ = false;
    pendingBaseDefenseEnd_.reset();
    std::size_t planned{};
    for (const auto &wave : saved.wavePlans)
        planned += wave.enemyIds.size();
    if (profile_.currentHealth <= 0)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::PlayerDown;
    else if (saved.breachedIds.size() >= saved.breachLimit)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::Breached;
    else if (saved.killedIds.size() + saved.breachedIds.size() == planned)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::Completed;
    baseDefenseCheckpointElapsed_ = 0.0F;
    if (saveRepository_ && !baseDefenseWriter_)
        baseDefenseWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
    return true;
}

void GameSession::captureBaseDefenseCheckpoint(ProfileState &candidate) const
{
    if (!candidate.activeBaseDefense || !baseDefenseWorld_)
        return;
    if (auto snapshot = baseDefenseWorld_->baseDefenseCheckpoint())
    {
        snapshot->activeWeaponSlot = static_cast<std::uint32_t>(activeWeaponSlot_);
        snapshot->commandSequence = raidCommandSequence_;
        snapshot->weaponFaultSequence = weaponFaultSequence_;
        snapshot->medicalRandomSequence = medicalRandomSequence_;
        snapshot->woundRandomSequence = woundRandomSequence_;
        snapshot->pendingWorldSeconds = pendingWorldSeconds_;
        snapshot->baseCombatElapsedSeconds = baseCombatElapsedSeconds_;
        snapshot->medicalTickAccumulatorSeconds = medicalTickAccumulatorSeconds_;
        candidate.activeBaseDefense = std::move(snapshot);
    }
}

bool GameSession::checkpointBaseDefense(bool wait)
{
    if (!baseDefenseActive())
        return true;
    if (baseDefenseSimulationRejected_)
        return false;
    if (wait && pendingBaseDefenseEnd_)
        return finalizeBaseDefense(*pendingBaseDefenseEnd_);
    captureBaseDefenseCheckpoint(profile_);
    if (!baseDefenseWriter_ && saveRepository_)
        baseDefenseWriter_ = std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
    if (baseDefenseWriter_)
    {
        try
        {
            static_cast<void>(
                baseDefenseWriter_->request(profile_, publishedContentRegistry().contentVersion()));
            if (wait)
            {
                if (baseDefenseWriter_->snapshot().failed)
                    static_cast<void>(baseDefenseWriter_->retry());
                const auto result = baseDefenseWriter_->flush();
                if (!result.succeeded)
                {
                    baseDefenseSaveBlocked_ = true;
                    persistenceMessage_ = result.message;
                    return false;
                }
            }
        }
        catch (const std::exception &error)
        {
            baseDefenseSaveBlocked_ = true;
            persistenceMessage_ = error.what();
            return false;
        }
    }
    baseDefenseCheckpointElapsed_ = 0.0F;
    worldClockDirty_ = false;
    return true;
}

bool GameSession::prepareBaseDefenseFrame(BaseWorld &world)
{
    if (baseDefenseWorld_ != &world || !world.baseDefenseActive())
        if (!restoreBaseDefenseRuntime(world))
            return false;
    const auto status = baseDefenseCheckpointStatus();
    if (status.failed || status.durabilityLagMilliseconds >= 1000.0)
    {
        baseDefenseSaveBlocked_ = true;
        baseDefenseLagPause_ = !status.failed;
        persistenceMessage_ =
            status.failed ? status.message : "BASE DEFENSE WAITING FOR CHECKPOINT";
    }
    // A slow (but successful) device resumes automatically. A failed device
    // requires explicit retry, and the simulation does not run ahead of it.
    else if (baseDefenseLagPause_ && !status.outstanding() && !status.failed)
    {
        baseDefenseSaveBlocked_ = false;
        baseDefenseLagPause_ = false;
    }
    if (baseDefenseSaveBlocked_)
        return false;
    if (pendingBaseDefenseEnd_)
    {
        static_cast<void>(finalizeBaseDefense(*pendingBaseDefenseEnd_));
        return false;
    }
    return true;
}

void GameSession::finishBaseDefenseFrame(BaseWorld &world, float deltaTime)
{
    if (world.baseDefenseDamageLastUpdate() > 0)
    {
        Pcg32 wounds{profile_.activeBaseDefense->seed,
                     woundRandomSequence_ + 0x73696567652d6874ULL};
        const auto attackType =
            world.baseDefenseAttackTypeLastUpdate().value_or(EnemyAttackType::Scratch);
        const auto attack = enemyAttackCombatDamage(attackType);
        const auto receipt = executeIncomingDamageInSimulation(
            profile_, publishedContentRegistry(),
            {world.baseDefenseDamageLastUpdate(), attack.region, attack.penetration,
             attack.armorDamage, attack.weakPoint,
             WoundRollCommand{attackType == EnemyAttackType::Bite ? WoundSource::Bite
                                                                  : WoundSource::Scratch,
                              wounds.bounded(10000U), 15000U + wounds.bounded(10001U)}},
            {profile_.revision, nextRaidTransaction("base-defense-hit")});
        if (!receipt.succeeded)
        {
            baseDefenseSaveBlocked_ = true;
            baseDefenseSimulationRejected_ = true;
            persistenceMessage_ = "DEFENSE SIMULATION REJECTED | RETRY RELOADS LAST CHECKPOINT";
            return;
        }
        ++woundRandomSequence_;
        presentationEvents_.push_back(receipt.resolution.damageApplied >= 25
                                          ? GameSessionPresentationEvent::PlayerHurtHeavy
                                          : GameSessionPresentationEvent::PlayerHurtLight);
    }
    // The same medical rules as Raid, with only their small participants staged.
    // A Base defense has no PendingRaid and must not call the Raid-only ticker.
    if (hasPain(profile_.medicalStatus) || profile_.medicalStatus.painkillerRemainingMs > 0)
    {
        medicalTickAccumulatorSeconds_ =
            std::min(1.0F, medicalTickAccumulatorSeconds_ + std::max(0.0F, deltaTime));
        while (medicalTickAccumulatorSeconds_ >= 0.1F && profile_.currentHealth > 0)
        {
            if (profile_.revision == std::numeric_limits<ProfileRevision>::max())
            {
                baseDefenseSimulationRejected_ = baseDefenseSaveBlocked_ = true;
                persistenceMessage_ = "DEFENSE SIMULATION REJECTED | RETRY RELOADS LAST CHECKPOINT";
                return;
            }
            medicalTickAccumulatorSeconds_ -= 0.1F;
            Pcg32 random{profile_.activeBaseDefense->seed ^ ++medicalRandomSequence_,
                         0x7061696e2d736372ULL};
            static_cast<void>(advanceMedicalStatus(profile_.medicalStatus, profile_.currentHealth,
                                                   100U, 15000U + random.bounded(10001U)));
            ++profile_.revision;
        }
    }
    advanceWorldClockFromSimulation(deltaTime, false);
    const auto *state = world.baseDefenseState();
    if (!state)
        return;
    std::size_t planned{};
    for (const auto &wave : state->wavePlans)
        planned += wave.enemyIds.size();
    if (profile_.currentHealth <= 0)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::PlayerDown;
    else if (state->breachedIds.size() >= state->breachLimit)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::Breached;
    else if (state->killedIds.size() + state->breachedIds.size() == planned)
        pendingBaseDefenseEnd_ = BaseDefenseEndReason::Completed;
    if (pendingBaseDefenseEnd_)
    {
        static_cast<void>(finalizeBaseDefense(*pendingBaseDefenseEnd_));
        return;
    }
    baseDefenseCheckpointElapsed_ += std::max(0.0F, deltaTime);
    if (baseDefenseCheckpointElapsed_ >= 0.25F)
        static_cast<void>(checkpointBaseDefense(false));
}

bool GameSession::finalizeBaseDefense(BaseDefenseEndReason reason)
{
    if (!baseDefenseActive())
        return true;
    if (baseDefenseSimulationRejected_)
        return false;
    captureBaseDefenseCheckpoint(profile_);
    ProfileState candidate = profile_;
    const auto receipt = executeBaseRealtimeDefenseSettlement(
        candidate, publishedContentRegistry(), profile_.activeBaseDefense->eventId, reason,
        {profile_.revision, nextRaidTransaction("base-defense-settle")});
    if (!receipt.succeeded)
    {
        baseDefenseSaveBlocked_ = true;
        persistenceMessage_ = receipt.message;
        return false;
    }
    static_cast<void>(synchronizeBaseDailySystemsThrough(candidate, publishedContentRegistry()));
    static_cast<void>(applyBaseConstructionThrough(candidate, publishedContentRegistry()));
    static_cast<void>(applyBaseManufacturingThrough(candidate, publishedContentRegistry()));
    static_cast<void>(applyResidentTreatmentThrough(candidate));
    static_cast<void>(applyRecoveryTaskThrough(candidate));
    if (saveRepository_)
    {
        try
        {
            if (!baseDefenseWriter_)
                baseDefenseWriter_ =
                    std::make_unique<BaseDefenseCheckpointWriter>(*saveRepository_);
            static_cast<void>(baseDefenseWriter_->request(
                candidate, publishedContentRegistry().contentVersion()));
            if (baseDefenseWriter_->snapshot().failed)
                static_cast<void>(baseDefenseWriter_->retry());
            const auto saved = baseDefenseWriter_->flush();
            if (!saved.succeeded)
            {
                baseDefenseSaveBlocked_ = true;
                persistenceMessage_ = saved.message;
                return false;
            }
        }
        catch (const std::exception &error)
        {
            baseDefenseSaveBlocked_ = true;
            persistenceMessage_ = error.what();
            return false;
        }
    }
    profile_ = std::move(candidate);
    baseDefenseWriter_.reset();
    if (baseDefenseWorld_)
    {
        baseDefenseWorld_->clearBaseDefense();
        if (reason == BaseDefenseEndReason::PlayerDown)
            baseDefenseWorld_->resetAtMedicalPoint();
    }
    baseDefenseWorld_ = nullptr;
    baseDefenseSaveBlocked_ = false;
    pendingBaseDefenseEnd_.reset();
    worldClockDirty_ = false;
    persistenceMessage_ = receipt.outcome == BaseSiegeOutcome::Defended
                              ? "BASE DEFENDED | SAFETY PERIOD STARTED"
                              : "BASE DEFENSE FAILED SOFTLY | RECOVERY PERIOD STARTED";
    return true;
}

bool GameSession::abandonBaseRealtimeDefense()
{
    if (!baseDefenseActive())
        return false;
    pendingBaseDefenseEnd_ = BaseDefenseEndReason::Abandoned;
    return finalizeBaseDefense(*pendingBaseDefenseEnd_);
}

bool GameSession::retryBaseDefenseSave()
{
    if (baseDefenseSimulationRejected_)
    {
        // Never persist a frame whose ammo/damage transaction was rejected.
        // Explicit retry restores the last whole durable checkpoint, including
        // assets and combat together, just like interrupted-process recovery.
        BaseWorld *world = baseDefenseWorld_;
        if (!world || !saveRepository_)
            return false;
        return continueProfile() && restoreBaseDefenseRuntime(*world);
    }
    if (pendingBaseDefenseEnd_)
        return finalizeBaseDefense(*pendingBaseDefenseEnd_);
    const bool saved = checkpointBaseDefense(true);
    if (saved)
        baseDefenseSaveBlocked_ = false;
    return saved;
}
