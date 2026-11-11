#include "game_session.h"
#include "base_world.h"
#include "base_morale_domain.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace {
bool hasCommittedRaidResult(const ProfileState &profile)
{
    return !profile.pendingRaid && profile.lastRaidResult &&
        profile.lastRaidResult->outcome!=RaidResultOutcome::AbnormalQuit &&
        profile.committedSettlements.contains(profile.lastRaidResult->settlementId);
}
}

bool GameSession::prepareDailyOutingEndCandidate(ProfileState &candidate,BaseWorld &baseWorld,bool rescued)
{
    const auto &content=publishedContentRegistry();
    const RegionalBaseSiteDefinitionId site{baseWorld.siteDefinitionId()};
    if(alphaRaidActive_ || candidate.pendingRaid || candidate.activeBaseDefense ||
       !candidate.homeFounding.established) {persistenceMessage_="DAILY OUTING UNAVAILABLE";return false;}
    if(!homePerimeterOutingActive(candidate,site)) {
        if(!rescued){persistenceMessage_="DAILY RETURN REQUIRES ITS ACTIVE OUTING";return false;}
        const auto begun=beginHomePerimeterOuting(candidate,site,
            {candidate.revision,nextRaidTransaction("home-perimeter-outing")});
        if(!begun.succeeded){persistenceMessage_=begun.message;return false;}
    }
    const auto result=completeHomePerimeterOuting(candidate,rescued,
        {candidate.revision,nextRaidTransaction(rescued?"home-perimeter-rescue":"home-perimeter-return")});
    if(!result.succeeded){persistenceMessage_=result.message;return false;}
    if(rescued) {
        const auto key=baseWorld.observationKey();
        const auto recovered=recoverBaseReloadInCandidate(candidate,content,raidReloadAccess(key));
        if(!recovered.succeeded){persistenceMessage_=recovered.message;return false;}
        if(candidate.basePlayerEffects)candidate.basePlayerEffects->medical.reset();
    }
    static_cast<void>(synchronizeBaseDailySystemsThrough(candidate,content));
    static_cast<void>(applyBaseConstructionThrough(candidate,content));
    static_cast<void>(applyBaseManufacturingThrough(candidate,content));
    static_cast<void>(applyResidentTreatmentThrough(candidate));
    static_cast<void>(applyRecoveryTaskThrough(candidate));
    if(!captureBasePlayerEffects(candidate))return false;
    if(rescued && candidate.basePlayerEffects) {
        auto &effects=*candidate.basePlayerEffects;
        effects.medical.reset();effects.playerPosition=baseWorld.medicalRespawnPosition();
        effects.controlRemainingSeconds=effects.impactSlowRemainingSeconds=0;
        effects.dailyDamageProtectionSeconds=.25F;
    }
    const auto valid=validateProfileState(candidate,content);
    if(!valid.valid){persistenceMessage_=valid.message;return false;}
    return true;
}

bool GameSession::prepareBaseBoundaryInCandidate(ProfileState &candidate,bool requireFinishedThrowables,bool allowOwnedUiOperation)
{
    if(profileUiCommitPending() && !allowOwnedUiOperation){persistenceMessage_="WAIT FOR THE ACTIVE SAVE";return false;}
    if(inventoryPersistencePending()) {
        persistenceMessage_="WAIT FOR THE ACTIVE SAVE";return false;
    }
    if(!basePlayerWorld_ || alphaRaidActive_ || candidate.pendingRaid)return true;
    if(requireFinishedThrowables && (basePlayerWorld_->grenade().active() ||
        basePlayerWorld_->noiseDecoy().flight() || basePlayerWorld_->noiseDecoy().source())) {
        persistenceMessage_="WAIT FOR ACTIVE THROWABLE EFFECT";return false;
    }
    if(!captureBasePlayerEffects(candidate))return false;
    if(candidate.baseReloadRecovery) {
        const auto key=basePlayerWorld_->observationKey();
        const auto receipt=recoverBaseReloadInCandidate(candidate,publishedContentRegistry(),raidReloadAccess(key));
        if(!receipt.succeeded){persistenceMessage_=receipt.message;return false;}
    }
    if(candidate.basePlayerEffects)candidate.basePlayerEffects->medical.reset();
    const auto validation=validateProfileState(candidate,publishedContentRegistry());
    if(!validation.valid){persistenceMessage_=validation.message;return false;}
    return true;
}

void GameSession::completeProfileUiCommand(const ProfileUiCommandResult &result)
{
    const auto &facts=result.facts;
    if(facts.medicalCompleted)presentationEvents_.push_back(GameSessionPresentationEvent::MedicalCompleted);
    if(facts.resetWorldClockAccumulator){pendingWorldSeconds_=0;worldClockCheckpointElapsedSeconds_=0;}
    if(facts.clearWeaponOverrides)developerWeaponOverrides_.clear();
    if(facts.resetWeaponConfiguration){configuredWeaponAssetId_.reset();configuredBaseWeaponAssetId_.reset();}
    if(facts.releaseWeaponTrigger)cancelPlayerControlIntent();
    if(facts.weaponMagazineRemoved)presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineRemoved);
    if(facts.weaponMagazineInserted)presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineInserted);
    if(facts.weaponChambered)presentationEvents_.push_back(GameSessionPresentationEvent::WeaponChambered);
    if(facts.restCompletion)recordBaseCompletionEvents(facts.restCompletion->construction,
        facts.restCompletion->manufacturing,facts.restCompletion->residentTreatment,false);
    // Tutorial/state changes were prepared in the private candidate. Geometry
    // refresh is owned by GameFlow after it consumes the durable completion.
}

std::shared_ptr<PreparedPlayerTransition> GameSession::prepareAlphaDeployment(
    const AlphaDeployRequest &request,ProfileState &candidate,ProfileState &recoveryProfile,
    const RaidDeploymentProgressCallback &progress)
{
    const auto seed=request.seed;auto mapDefinitionId=request.mapDefinitionId;
    const auto intelligence=request.intelligence;auto selfRecoveryRecordId=request.selfRecoveryRecordId;
    auto outpostRestorationId=request.outpostRestorationId;auto baseSiteClearanceId=request.baseSiteClearanceId;
    auto basePerimeterSweepId=request.basePerimeterSweepId;
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() || baseDefenseActive() || seed == 0 ||
        mapDefinitionId.value().empty())
    {
        return {};
    }
    const std::size_t number = profile_.committedSettlements.size() + 1U;
    recoveryProfile = profile_;
    if (!prepareBaseBoundaryInCandidate(recoveryProfile,true)) return {};
    if (recoveryProfile.basePlayerEffects)
        recoveryProfile.basePlayerEffects->thrownIllumination = {};
    // Reopening after a committed result may skip its result-screen button.
    // A second deployment acknowledges that guide in the same clean Base
    // checkpoint as deployment; rejection or save failure commits neither.
    if (!recoveryProfile.homeFounding.hintsDismissed &&
        hasCommittedRaidResult(recoveryProfile))
    {
        if (recoveryProfile.revision == std::numeric_limits<ProfileRevision>::max()) return {};
        recoveryProfile.homeFounding.hintsDismissed = true;
        recoveryProfile.tutorial = TutorialProgress::Complete;
        ++recoveryProfile.revision;
    }
    candidate = recoveryProfile;
    const std::string raidId = candidate.profileId + "-raid-" +
        std::to_string(number);
    const std::string settlementId = candidate.profileId + "-settlement-" +
        std::to_string(number);
    reportRaidDeploymentProgress(
        progress, 0.08F, RaidDeploymentProgressStage::Validating);
    const DeployReceipt receipt = executeDeploy(
        candidate,
        publishedContentRegistry(),
        DeployCommand{
            raidId,
            settlementId,
            seed,
            std::move(mapDefinitionId),
            intelligence,
            std::move(selfRecoveryRecordId),
            std::move(outpostRestorationId),
            std::move(baseSiteClearanceId),
            std::move(basePerimeterSweepId)},
        CommandContext{
            recoveryProfile.revision,
            "deploy:" + raidId},
        progress);
    if (!receipt.succeeded || !candidate.pendingRaid.has_value())
    {
        persistenceMessage_ = receipt.message;
        return {};
    }

    std::unique_ptr<GameplayWorld> candidateWorld;
    try
    {
        reportRaidDeploymentProgress(
            progress, 0.82F, RaidDeploymentProgressStage::BuildingWorld);
        const PendingRaidSnapshot &snapshot = *candidate.pendingRaid;
        std::vector<EnemySpawn> enemies;
        enemies.reserve(snapshot.enemies.size());
        for (const RaidEnemySnapshot &enemy : snapshot.enemies)
        {
            if (enemy.spaceId != outdoorRaidSpaceId())
            {
                continue;
            }
            const auto group = std::find_if(
                snapshot.encounterGroups.begin(),
                snapshot.encounterGroups.end(),
                [&](const RaidEncounterGroupSnapshot &candidate)
                {
                    return candidate.instanceId ==
                        enemy.encounterGroupInstanceId;
                });
            EnemySpawn spawn{
                enemy.position,
                enemy.size,
                enemy.maximumHealth};
            spawn.torsoArmor = enemy.torsoArmor;
            spawn.variant = enemy.variant;
            spawn.sourceEnemyId = enemy.sourceEnemyId;
            if (group != snapshot.encounterGroups.end())
            {
                spawn.encounterGroupInstanceId = group->instanceId;
                spawn.encounterKind = group->kind;
                spawn.encounterHome = group->homePosition;
                spawn.patrolPoints = group->patrolPoints;
                spawn.ambushActivationDistance = group->activationDistance;
            }
            enemies.push_back(std::move(spawn));
        }
        const MapDefinition &map =
            publishedContentRegistry().map(snapshot.mapDefinitionId);
        std::vector<BallisticBlocker> blockers;
        blockers.reserve(snapshot.spatialLayout.ballisticBlockers.size());
        for (std::size_t index = 0;
             index < snapshot.spatialLayout.ballisticBlockers.size();
             ++index)
        {
            const ContentRect &definition =
                snapshot.spatialLayout.ballisticBlockers[index];
            blockers.push_back(BallisticBlocker{
                static_cast<BallisticBlockerId>(index + 1U),
                Rect{definition.position, definition.size}});
        }
        std::vector<EnemySpawn> pressureSpawns;
        if (snapshot.highRiskCrisis.has_value())
        {
            pressureSpawns.reserve(
                snapshot.highRiskCrisis->pressureSpawns.size());
            for (const RaidHighRiskPressureSpawnSnapshot &spawn :
                 snapshot.highRiskCrisis->pressureSpawns)
            {
                pressureSpawns.push_back(EnemySpawn{
                    spawn.position, spawn.size, spawn.maximumHealth});
            }
        }
        else
        {
            pressureSpawns.reserve(map.highRisk.pressureSpawns.size());
            for (std::size_t index{};
                 index < map.highRisk.pressureSpawns.size(); ++index)
            {
                const EnemySpawnDefinition &spawn =
                    map.highRisk.pressureSpawns[index];
                const RaidAnchorPlacementSnapshot *placement =
                    findRaidAnchorPlacement(
                        snapshot.spatialLayout,
                        raidIndexedAnchorId("pressure", index));
                pressureSpawns.push_back(EnemySpawn{
                    placement != nullptr
                        ? placement->bounds.position
                        : spawn.position,
                    spawn.size,
                    spawn.maximumHealth});
            }
        }

        const auto frozenRegion = [&](std::string_view id,
                                      ContentRect fallback)
        {
            const RaidAnchorPlacementSnapshot *placement =
                findRaidAnchorPlacement(snapshot.spatialLayout, id);
            return placement != nullptr ? placement->bounds : fallback;
        };

        RaidWorldConfig worldConfig;
        worldConfig.noiseDecoyEnabled = !snapshot.selfRecovery && !snapshot.outpostRestoration &&
            !snapshot.baseSiteClearance && !snapshot.basePerimeterSweep;
        worldConfig.grenadeEnabled = worldConfig.noiseDecoyEnabled;
        worldConfig.lightConeObservationEnabled =
            candidate.pendingRaid->mapDefinitionId == MapDefinitionId{"map.raid.frontier_exchange"} ||
            candidate.pendingRaid->mapDefinitionId == MapDefinitionId{"map.raid.hospital_district"};
        worldConfig.worldSize = map.worldSize;
        worldConfig.playerSpawn = snapshot.playerSpawn;
        worldConfig.extractionPoint = snapshot.extractionPoint;
        worldConfig.initialEnemies = std::move(enemies);
        worldConfig.playerMaximumHealth = 100;
        worldConfig.playerCurrentHealth = candidate.currentHealth;
        worldConfig.deferPlayerDamageResolution = true;
        worldConfig.ballisticBlockers = std::move(blockers);
        worldConfig.outdoorLayout = snapshot.spatialLayout;
        worldConfig.outdoorColumns = map.proceduralOutdoor.columns;
        worldConfig.outdoorRows = map.proceduralOutdoor.rows;
        worldConfig.outdoorChunkSizeCells =
            map.proceduralOutdoor.chunkSizeCells;
        worldConfig.interiors.reserve(snapshot.interiors.size());
        for (const RaidInteriorSnapshot &interior : snapshot.interiors)
        {
            RaidInteriorWorldConfig runtime;
            runtime.id = interior.id;
            runtime.displayName = interior.displayName;
            runtime.layoutKnown = interior.layoutKnown;
            runtime.worldSize = interior.worldSize;
            runtime.exteriorEntrance = interior.exteriorEntrance;
            runtime.exteriorReturn = interior.exteriorReturn;
            runtime.interiorSpawn = interior.interiorSpawn;
            runtime.interiorExit = interior.interiorExit;
            runtime.ballisticBlockers.reserve(
                interior.ballisticBlockers.size());
            for (std::size_t index{};
                 index < interior.ballisticBlockers.size(); ++index)
            {
                runtime.ballisticBlockers.push_back(BallisticBlocker{
                    static_cast<BallisticBlockerId>(index + 1U),
                    Rect{interior.ballisticBlockers[index].position,
                         interior.ballisticBlockers[index].size}});
            }
            for (const RaidEnemySnapshot &enemy : snapshot.enemies)
            {
                if (enemy.spaceId == interior.id)
                {
                    runtime.initialEnemies.push_back(EnemySpawn{
                        enemy.position,
                        enemy.size,
                        enemy.maximumHealth});
                    runtime.initialEnemies.back().torsoArmor = enemy.torsoArmor;
                    runtime.initialEnemies.back().variant = enemy.variant;
                }
            }
            worldConfig.interiors.push_back(std::move(runtime));
        }
        worldConfig.normalExtractionDurationSeconds =
            map.raidRules.extractionDurationSeconds;
        worldConfig.credentialExtraction = snapshot.credentialExtraction;
        worldConfig.intelligence = snapshot.intelligence;
        if (snapshot.rescue.has_value() && !snapshot.rescue->secured)
        {
            worldConfig.rescue = RaidWorldConfig::OrdinarySurvivorRescue{
                snapshot.rescue->transferPoint,
                snapshot.rescue->interactionDurationSeconds};
        }
        const RaidHighRiskCrisisSnapshot *crisis =
            snapshot.highRiskCrisis.has_value()
            ? &*snapshot.highRiskCrisis
            : nullptr;
        worldConfig.highRisk = HighRiskWorldConfig{
            map.highRisk.enabled,
            map.highRisk.regularPhaseDurationSeconds,
            frozenRegion(kRaidAnchorEmergencyExtraction,
                         map.highRisk.emergencyExtractionPoint),
            map.highRisk.emergencyExtractionDurationSeconds,
            crisis != nullptr
                ? crisis->initialWaveDelaySeconds
                : map.highRisk.initialWaveDelaySeconds,
            crisis != nullptr
                ? crisis->waveIntervalSeconds
                : map.highRisk.waveIntervalSeconds,
            crisis != nullptr ? crisis->waveSize : map.highRisk.waveSize,
            crisis != nullptr
                ? crisis->activeEnemyCap
                : map.highRisk.activeEnemyCap,
            std::move(pressureSpawns),
            frozenRegion(kRaidAnchorHighRiskControl,
                         map.highRisk.activationControlPoint),
            map.highRisk.activationDurationSeconds,
            crisis != nullptr
                ? crisis->focusArea
                : frozenRegion(kRaidAnchorAdvancedResource,
                               map.highRisk.advancedResourceArea),
            snapshot.seed,
            frozenRegion(kRaidAnchorConditionalExtraction,
                         map.highRisk.conditionalExtractionPoint),
            map.highRisk.conditionalExtractionDurationSeconds,
            map.highRisk.conditionalExtractionMaximumWeightGrams,
            snapshot.fuelExtractionLootAssetId.has_value()};
        if (map.highRisk.enabled)
        {
            worldConfig.tacticalObjectives.push_back(
                RaidTacticalObjective{
                    RaidTacticalObjectiveKind::HighRiskControl,
                    frozenRegion(
                        kRaidAnchorHighRiskControl,
                        map.highRisk.activationControlPoint),
                    RaidTacticalObjectiveVisibility::Explored});
        }
        if (snapshot.rescue.has_value() && !snapshot.rescue->secured)
        {
            worldConfig.tacticalObjectives.push_back(
                RaidTacticalObjective{
                    RaidTacticalObjectiveKind::Rescue,
                    snapshot.rescue->transferPoint,
                    RaidTacticalObjectiveVisibility::Briefed});
        }
        if (snapshot.selfRecovery.has_value())
        {
            constexpr Vec2 markerSize{96.0F, 96.0F};
            const Vec2 cache = snapshot.selfRecovery->cachePosition;
            const ContentRect marker{
                {std::clamp(
                     cache.x - markerSize.x * 0.5F,
                     0.0F,
                     map.worldSize.x - markerSize.x),
                 std::clamp(
                     cache.y - markerSize.y * 0.5F,
                     0.0F,
                     map.worldSize.y - markerSize.y)},
                markerSize};
            worldConfig.tacticalObjectives.push_back(
                RaidTacticalObjective{
                    RaidTacticalObjectiveKind::SelfRecovery,
                    marker,
                    RaidTacticalObjectiveVisibility::Briefed});
        }
        candidateWorld =
            std::make_unique<GameplayWorld>(std::move(worldConfig));
    }
    catch (const std::exception &error)
    {
        persistenceMessage_ = error.what();
        return {};
    }
    auto prepared=std::make_shared<PreparedPlayerTransition>();
    prepared->kind=PreparedPlayerTransition::Kind::Deploy;
    prepared->world=std::move(candidateWorld);prepared->raidRecoveryProfile=recoveryProfile;prepared->raidNumber=number;
    return prepared;
}

ProfileUiSubmission GameSession::requestDeployAlpha(const AlphaDeployRequest &request,
    const RaidDeploymentProgressCallback &progress)
{
    if(profileUiCommitPending() || inventoryPersistencePending())
        return {0,false,false,DomainErrorCode::IllegalDestination,"WAIT FOR THE ACTIVE SAVE"};
    ProfileState candidate,recovery;
    auto prepared=prepareAlphaDeployment(request,candidate,recovery,progress);
    if(!prepared)return {0,false,false,DomainErrorCode::IllegalDestination,persistenceMessage_.empty()?"DEPLOYMENT IS NOT AVAILABLE":persistenceMessage_};
    prepared->requirePhysicalRelease=true;
    reportRaidDeploymentProgress(progress,.93F,RaidDeploymentProgressStage::SavingProfile);
    return enqueuePreparedProfileUi(std::move(candidate),std::move(recovery),ProfileUiCompletionKind::Deploy,std::nullopt,std::move(prepared));
}

ProfileUiSubmission GameSession::requestRaidSettlement(RaidResultOutcome outcome)
{
    if(profileUiCommitPending() || developerRange_)return {0,false,false,DomainErrorCode::IllegalDestination,"A PLAYER TRANSITION IS ALREADY ACTIVE"};
    ProfileState candidate;
    if(!prepareRaidSettlementCandidate(outcome,candidate))return {0,false,false,DomainErrorCode::IllegalDestination,persistenceMessage_};
    auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::RaidSettlement;
    return enqueuePreparedProfileUi(std::move(candidate),std::nullopt,ProfileUiCompletionKind::RaidSettlement,std::nullopt,std::move(prepared));
}

ProfileUiSubmission GameSession::requestActiveQuitAlphaRaid()
{
    if(!alphaRaidActive_)return {0,false,false,DomainErrorCode::IllegalDestination,"NO ACTIVE RAID"};
    return requestRaidSettlement(RaidResultOutcome::ActiveQuit);
}

ProfileUiSubmission GameSession::requestOrdinarySurvivorRescue()
{
    if(profileUiCommitPending())return {0,false,false,DomainErrorCode::IllegalDestination,"A PLAYER TRANSITION IS ALREADY ACTIVE"};
    ProfileState live,recovery;
    if(!prepareOrdinarySurvivorRescueCandidate(live,recovery))return {0,false,false,DomainErrorCode::IllegalDestination,persistenceMessage_};
    auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::Rescue;
    prepared->raidRecoveryProfile=recovery;
    return enqueuePreparedProfileUi(std::move(live),std::move(recovery),ProfileUiCompletionKind::Rescue,std::nullopt,std::move(prepared));
}

void GameSession::completePreparedPlayerTransition(const std::shared_ptr<PreparedPlayerTransition> &prepared)
{
    if(!prepared)return;
    if(prepared->kind==PreparedPlayerTransition::Kind::Deploy) {
    freightUnlock_ = {};
    if (basePlayerWorld_)
        basePlayerWorld_->commitThrownIllumination({});
    world_=std::move(prepared->world);
    world_->setRaidDaylight(worldClockProjection().timeOfDay == WorldTimeOfDay::Day);
    activeRaidRecoveryProfile_ = std::move(prepared->raidRecoveryProfile);
    deferredRaidBasePresentationEvents_.clear();
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    raidNumber_ = prepared->raidNumber;
    alphaRaidActive_ = true;
    recoveredAbandonedRaid_ = false;
    raidActionState_.cancel();
    weaponTriggerGate_.reset();
    droppedRaidMagazineIds_.clear();
    medicalTickAccumulatorSeconds_ = 0.0F;
    medicalRandomSequence_ = 0;
    woundRandomSequence_ = 0;
    weaponFaultSequence_ = 0;
    raidElapsedSeconds_ = 0.0F;
    selfRecoveryInteractionSeconds_ = 0.0F;
    outpostRestorationObjectiveSecured_ = false;
    baseSiteClearanceObjectiveSecured_ = false;
    basePerimeterSweepObjectiveSecured_ = false;
    weaponClearGesture_.reset();
    fireSuppressedUntilRelease_ = false;
    sprintSuppressedUntilRelease_ = false;
    sprintFireIntentPending_ = false;
    sprintFireReadyRemaining_ = 0.0F;
    activeWeaponSlot_ = EquipmentSlotKind::PrimaryWeapon;
    configuredWeaponAssetId_.reset();
    nightVisionBindingRevision_.reset();
    cancelWeaponLightInput();
    synchronizeActiveAlphaWeapon();
    synchronizeNightVision();
    if(prepared->requirePhysicalRelease)cancelPlayerControlIntent();
    } else if(prepared->kind==PreparedPlayerTransition::Kind::RaidSettlement) {
    freightUnlock_ = {};
    cancelWeaponLightInput();
    world_->commitNoiseDecoy({});
    world_->commitGrenade({});
    world_->commitThrownIllumination({});
    decoyFireSuppressedUntilRelease_=false;
    raidActionState_.cancel();
    weaponTriggerGate_.reset();
    droppedRaidMagazineIds_.clear();
    weaponClearGesture_.reset();
    raidElapsedSeconds_ = 0.0F;
    selfRecoveryInteractionSeconds_ = 0.0F;
    outpostRestorationObjectiveSecured_ = false;
    baseSiteClearanceObjectiveSecured_ = false;
    basePerimeterSweepObjectiveSecured_ = false;
    medicalTickAccumulatorSeconds_ = 0.0F;
    fireSuppressedUntilRelease_ = false;
    sprintSuppressedUntilRelease_ = false;
    sprintFireIntentPending_ = false;
    sprintFireReadyRemaining_ = 0.0F;
    activeWeaponSlot_ = EquipmentSlotKind::PrimaryWeapon;
    configuredWeaponAssetId_.reset();
    activeRaidRecoveryProfile_.reset();
    alphaRaidActive_ = false;
    publishDeferredRaidBasePresentationEvents();
    state_ = GameSessionState::BetweenRaids;
    } else if(prepared->kind==PreparedPlayerTransition::Kind::Rescue) {
        activeRaidRecoveryProfile_=std::move(prepared->raidRecoveryProfile);
        world_->confirmOrdinarySurvivorRescue();presentationEvents_.push_back(GameSessionPresentationEvent::RescueSecured);
    } else if(prepared->kind==PreparedPlayerTransition::Kind::DefenseStart) {
        raidActionState_.cancel();cancelPlayerControlIntent();
        if(basePlayerWorld_ && !restoreBaseDefenseRuntime(*basePlayerWorld_))baseDefenseSaveBlocked_=true;
    } else if(prepared->kind==PreparedPlayerTransition::Kind::TimedBaseCompletion) {
        pendingWorldSeconds_=prepared->pendingWorldSeconds;
        recordBaseCompletionEvents(prepared->construction,prepared->manufacturing,prepared->residentTreatment,false);
    } else if(prepared->kind==PreparedPlayerTransition::Kind::BaseSnapshot || prepared->kind==PreparedPlayerTransition::Kind::DailyEnd) {
        if(prepared->warningAccumulatorSeconds)baseSiegeWarningSecondAccumulator_=*prepared->warningAccumulatorSeconds;
        if(basePlayerWorld_ && prepared->restorePerimeterSite) {
            basePlayerWorld_->configureHomePerimeter(nullptr);
            const auto site=profile_.homePerimeter.sites.find(*prepared->restorePerimeterSite);
            if(site!=profile_.homePerimeter.sites.end())basePlayerWorld_->configureHomePerimeter(&site->second);
        }
        if(prepared->rescued && basePlayerWorld_) {
            raidActionState_.cancel();cancelPlayerControlIntent();basePlayerWorld_->resetAtMedicalPoint();
            if(profile_.basePlayerEffects) {
                static_cast<void>(basePlayerWorld_->restorePlayerBody(
                    profile_.basePlayerEffects->playerPosition,profile_.basePlayerEffects->facingDirection,0,0));
                basePlayerWorld_->perimeterDamageProtectionRemainingSeconds_=
                    profile_.basePlayerEffects->dailyDamageProtectionSeconds;
            }
        }
        if(prepared->resumeBaseFrame) {
            finishBasePlayerFrame(prepared->resumeInput,prepared->resumeDelta,prepared->resumeWeaponActionDelta);
            finishBaseDailyFrame();
        }
    }
}
