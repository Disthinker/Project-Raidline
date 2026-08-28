#include "game_session.h"

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resident_medical_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "base_world.h"
#include "stable_random.h"

namespace
{
    // Current Alpha weapons have no suppressor consumer. Keep this gameplay
    // stimulus separate from audio playback; a later weapon-content slice can
    // data-drive per-shot acoustic profiles without changing AI ownership.
    constexpr float kUnsuppressedGunshotNoiseRadius{900.0F};
}

GameSession::GameSession()
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    InventoryGridSize stashSize)
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{stashSize},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    std::vector<EnemySpawn> firstRaidEnemies)
    : profile_{makeNewAlphaProfile(
          "in-memory-profile",
          publishedContentRegistry())},
      stash_{},
      world_{std::make_unique<GameplayWorld>(
          std::move(firstRaidEnemies),
          3)}
{
}

void GameSession::update(
    const GameplayInput &input,
    float deltaTime)
{
    if (alphaRaidActive_ ||
        (profile_.pendingRaid.has_value() &&
         state_ == GameSessionState::SettlementBlocked))
    {
        updateAlphaRaid(input, deltaTime);
        return;
    }
    if (state_ == GameSessionState::BetweenRaids)
    {
        return;
    }

    world_->update(input, deltaTime);
    const RaidSettlementAttempt attempt =
        settlement_.settle(
            world_->raidSession().state(),
            world_->inventory(),
            stash_);

    if (attempt == RaidSettlementAttempt::Completed ||
        attempt == RaidSettlementAttempt::AlreadyCompleted)
    {
        state_ = GameSessionState::BetweenRaids;
        return;
    }

    state_ = attempt == RaidSettlementAttempt::Blocked
        ? GameSessionState::SettlementBlocked
        : GameSessionState::InRaid;
}

bool GameSession::startNextRaid() noexcept
{
    if (!canStartNextRaid() ||
        raidNumber_ ==
            std::numeric_limits<std::size_t>::max())
    {
        return false;
    }

    // 直接读取当前世界的高水位，避免调用方在本帧通过受控库存命令
    // 分配新 ID 后尚未来得及 update 时使用旧快照。
    const ItemInstanceId candidateFirstId =
        world_->nextItemInstanceId();

    std::unique_ptr<GameplayWorld> candidate;

    try
    {
        candidate =
            std::make_unique<GameplayWorld>(
                candidateFirstId);
    }
    catch (...)
    {
        return false;
    }

    world_.swap(candidate);
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    ++raidNumber_;
    return true;
}

GameplayWorld &GameSession::world() noexcept
{
    return *world_;
}

const GameplayWorld &GameSession::world() const noexcept
{
    return *world_;
}

Stash &GameSession::stash() noexcept
{
    return stash_;
}

const Stash &GameSession::stash() const noexcept
{
    return stash_;
}

const RaidSettlement &
GameSession::settlement() const noexcept
{
    return settlement_;
}

std::uint64_t GameSession::currentRaidCarriedWeightGrams() const noexcept
{
    return carriedWeightGrams(profile_, publishedContentRegistry());
}

std::uint64_t
GameSession::conditionalExtractionWeightLimitGrams() const noexcept
{
    return world_ == nullptr
        ? 0U
        : world_->conditionalExtractionMaximumWeightGrams();
}

bool GameSession::conditionalExtractionEligible() const noexcept
{
    const std::uint64_t limit = conditionalExtractionWeightLimitGrams();
    return alphaRaidActive_ && limit > 0U &&
           currentRaidCarriedWeightGrams() <= limit;
}

GameSessionState GameSession::state() const noexcept
{
    return state_;
}

bool GameSession::canStartNextRaid() const noexcept
{
    return state_ == GameSessionState::BetweenRaids &&
           settlement_.isComplete() &&
           raidNumber_ <
               std::numeric_limits<std::size_t>::max();
}

std::size_t GameSession::raidNumber() const noexcept
{
    return raidNumber_;
}

ItemInstanceId
GameSession::nextItemInstanceId() const noexcept
{
    return world_->nextItemInstanceId();
}

void GameSession::configurePersistence(
    std::filesystem::path directory)
{
    saveRepository_.emplace(std::move(directory));
    lastSaveLoadStatus_ = SaveLoadStatus::NotFound;
    persistenceMessage_.clear();
}

bool GameSession::hasSavedProfile() const
{
    return saveRepository_.has_value() &&
           saveRepository_->primaryExists();
}

bool GameSession::startNewProfile(std::string profileId)
{
    ProfileState candidate;
    std::string saveMessage;
    try
    {
        candidate = makeNewAlphaProfile(
            std::move(profileId),
            publishedContentRegistry());
    }
    catch (const std::exception &error)
    {
        persistenceMessage_ = error.what();
        return false;
    }

    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            persistenceMessage_ = result.message;
            return false;
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    activeRaidRecoveryProfile_.reset();
    resetWorldClockRuntime();
    developerWeaponOverrides_.clear();
    alphaRaidActive_ = false;
    recoveredAbandonedRaid_ = false;
    state_ = GameSessionState::BetweenRaids;
    raidNumber_ = 1;
    lastSaveLoadStatus_ = SaveLoadStatus::LoadedPrimary;
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

bool GameSession::continueProfile()
{
    if (!saveRepository_.has_value())
    {
        persistenceMessage_ = "persistence is not configured";
        return false;
    }
    SaveLoadResult result = saveRepository_->load(publishedContentRegistry());
    lastSaveLoadStatus_ = result.status;
    persistenceMessage_ = result.message;
    if (!result.profile.has_value())
    {
        return false;
    }
    ProfileState candidate = std::move(*result.profile);
    recoveredAbandonedRaid_ = false;
    if (candidate.pendingRaid.has_value())
    {
        const RaidRollbackReceipt rolledBack = rollbackPendingRaidToBase(
            candidate,
            publishedContentRegistry());
        if (!rolledBack.succeeded)
        {
            persistenceMessage_ = rolledBack.message;
            return false;
        }
        const SaveWriteResult saved = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!saved.succeeded)
        {
            persistenceMessage_ = saved.message;
            return false;
        }
        recoveredAbandonedRaid_ = true;
    }
    profile_ = std::move(candidate);
    activeRaidRecoveryProfile_.reset();
    resetWorldClockRuntime();
    developerWeaponOverrides_.clear();
    alphaRaidActive_ = false;
    state_ = GameSessionState::BetweenRaids;
    raidNumber_ = profile_.committedSettlements.size() + 1U;
    return true;
}

bool GameSession::deployAlpha(
    std::uint64_t seed,
    MapDefinitionId mapDefinitionId,
    RaidIntelligenceLoadout intelligence,
    std::optional<std::string> selfRecoveryRecordId,
    std::optional<RegionalOutpostDefinitionId> outpostRestorationId,
    std::optional<RegionalBaseSiteDefinitionId> baseSiteClearanceId,
    std::optional<RegionalBaseSiteDefinitionId> basePerimeterSweepId)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() || seed == 0 ||
        mapDefinitionId.value().empty())
    {
        return false;
    }
    const std::size_t number = profile_.committedSettlements.size() + 1U;
    ProfileState recoveryProfile = profile_;
    ProfileState candidate = profile_;
    const std::string raidId = candidate.profileId + "-raid-" +
        std::to_string(number);
    const std::string settlementId = candidate.profileId + "-settlement-" +
        std::to_string(number);
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
            profile_.revision,
            "deploy:" + raidId});
    if (!receipt.succeeded || !candidate.pendingRaid.has_value())
    {
        persistenceMessage_ = receipt.message;
        return false;
    }

    std::unique_ptr<GameplayWorld> candidateWorld;
    try
    {
        const PendingRaidSnapshot &snapshot = *candidate.pendingRaid;
        std::vector<EnemySpawn> enemies;
        enemies.reserve(snapshot.enemies.size());
        for (const RaidEnemySnapshot &enemy : snapshot.enemies)
        {
            if (enemy.spaceId != outdoorRaidSpaceId())
            {
                continue;
            }
            enemies.push_back(EnemySpawn{
                enemy.position,
                enemy.size,
                enemy.maximumHealth});
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
        pressureSpawns.reserve(map.highRisk.pressureSpawns.size());
        for (const EnemySpawnDefinition &spawn :
             map.highRisk.pressureSpawns)
        {
            pressureSpawns.push_back(EnemySpawn{
                spawn.position,
                spawn.size,
                spawn.maximumHealth});
        }

        RaidWorldConfig worldConfig;
        worldConfig.worldSize = map.worldSize;
        worldConfig.playerSpawn = snapshot.playerSpawn;
        worldConfig.extractionPoint = snapshot.extractionPoint;
        worldConfig.initialEnemies = std::move(enemies);
        worldConfig.playerMaximumHealth = 100;
        worldConfig.playerCurrentHealth = candidate.currentHealth;
        worldConfig.deferPlayerDamageResolution = true;
        worldConfig.ballisticBlockers = std::move(blockers);
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
                }
            }
            worldConfig.interiors.push_back(std::move(runtime));
        }
        worldConfig.normalExtractionDurationSeconds =
            map.raidRules.extractionDurationSeconds;
        worldConfig.intelligence = snapshot.intelligence;
        if (snapshot.rescue.has_value() && !snapshot.rescue->secured)
        {
            worldConfig.rescue = RaidWorldConfig::OrdinarySurvivorRescue{
                snapshot.rescue->transferPoint,
                snapshot.rescue->interactionDurationSeconds};
        }
        worldConfig.highRisk = HighRiskWorldConfig{
            map.highRisk.enabled,
            map.highRisk.regularPhaseDurationSeconds,
            map.highRisk.emergencyExtractionPoint,
            map.highRisk.emergencyExtractionDurationSeconds,
            map.highRisk.initialWaveDelaySeconds,
            map.highRisk.waveIntervalSeconds,
            map.highRisk.waveSize,
            map.highRisk.activeEnemyCap,
            std::move(pressureSpawns),
            map.highRisk.activationControlPoint,
            map.highRisk.activationDurationSeconds,
            map.highRisk.advancedResourceArea,
            snapshot.seed,
            map.highRisk.conditionalExtractionPoint,
            map.highRisk.conditionalExtractionDurationSeconds,
            map.highRisk.conditionalExtractionMaximumWeightGrams};
        candidateWorld =
            std::make_unique<GameplayWorld>(std::move(worldConfig));
    }
    catch (const std::exception &error)
    {
        persistenceMessage_ = error.what();
        return false;
    }
    std::string saveMessage;
    if (saveRepository_.has_value())
    {
        const SaveWriteResult saved = saveRepository_->save(
            profile_,
            publishedContentRegistry().contentVersion());
        if (!saved.succeeded)
        {
            persistenceMessage_ = saved.message;
            return false;
        }
        saveMessage = saved.message;
    }
    worldClockDirty_ = false;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    if (!commitProfileCandidate(std::move(candidate), false))
    {
        return false;
    }
    persistenceMessage_ = std::move(saveMessage);
    world_.swap(candidateWorld);
    activeRaidRecoveryProfile_ = std::move(recoveryProfile);
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    raidNumber_ = number;
    alphaRaidActive_ = true;
    recoveredAbandonedRaid_ = false;
    raidActionState_.cancel();
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
    synchronizeActiveAlphaWeapon();
    return true;
}

bool GameSession::outpostRestorationObjectiveSecured() const noexcept
{
    return outpostRestorationObjectiveSecured_;
}

bool GameSession::baseSiteClearanceObjectiveSecured() const noexcept
{
    return baseSiteClearanceObjectiveSecured_;
}

bool GameSession::basePerimeterSweepObjectiveSecured() const noexcept
{
    return basePerimeterSweepObjectiveSecured_;
}

bool GameSession::activeQuitAlphaRaid()
{
    return alphaRaidActive_ && settleAlphaRaid(RaidResultOutcome::ActiveQuit);
}

bool GameSession::startAlphaReload(
    AssetInstanceId weaponAssetId,
    AssetInstanceId magazineAssetId)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value() ||
        activeAlphaWeapon() !=
            std::optional<AssetInstanceId>{weaponAssetId} ||
        !assetIsCarried(profile_, magazineAssetId))
    {
        return false;
    }
    const WeaponAmmoPlan plan = queryWeaponAmmo(
        profile_,
        publishedContentRegistry(),
        InstallMagazineAndChamberCommand{
            weaponAssetId,
            magazineAssetId});
    const float handlingMultiplier =
        hasPain(profile_.medicalStatus) &&
                !painIsSuppressed(profile_.medicalStatus)
            ? 0.9F
            : 1.0F;
    const bool started = plan.canCommit && raidActionState_.start(
        ReloadRaidAction{
            weaponAssetId,
            magazineAssetId,
            0.0F,
            2.0F / handlingMultiplier});
    if (started)
    {
        presentationEvents_.push_back(
            GameSessionPresentationEvent::ReloadStarted);
    }
    return started;
}

bool GameSession::startAlphaLoadMagazine(
    AssetInstanceId ammunitionAssetId,
    AssetInstanceId magazineAssetId,
    std::uint32_t quantity)
{
    const AssetRecord *ammunition = profile_.assets.find(ammunitionAssetId);
    const AssetRecord *magazine = profile_.assets.find(magazineAssetId);
    if (!alphaRaidActive_ || raidActionState_.active().has_value() ||
        ammunition == nullptr || magazine == nullptr ||
        !assetIsCarried(profile_, ammunitionAssetId) ||
        !assetIsCarried(profile_, magazineAssetId))
    {
        return false;
    }
    const WeaponAmmoPlan plan = queryWeaponAmmo(
        profile_,
        publishedContentRegistry(),
        LoadMagazineCommand{
            magazineAssetId,
            ammunitionAssetId,
            quantity});
    if (!plan.canCommit)
    {
        return false;
    }
    try
    {
        const std::uint32_t capacity = publishedContentRegistry()
            .item(magazine->definitionId).magazineCapacity;
        const std::uint32_t available = capacity - static_cast<std::uint32_t>(
            magazine->magazineRounds.size());
        const std::uint32_t requested = quantity == 0
            ? std::min(available, ammunition->quantity)
            : quantity;
        const float duration = std::clamp(
            static_cast<float>(requested) * 0.2F,
            0.5F,
            6.0F);
        const float handlingMultiplier =
            hasPain(profile_.medicalStatus) &&
                    !painIsSuppressed(profile_.medicalStatus)
                ? 0.9F
                : 1.0F;
        return raidActionState_.start(
            LoadMagazineRaidAction{
                magazineAssetId,
                ammunitionAssetId,
                quantity,
                0.0F,
                duration / handlingMultiplier});
    }
    catch (...)
    {
        return false;
    }
}

bool GameSession::startAlphaHeal(AssetInstanceId medkitAssetId)
{
    const AssetRecord *asset = profile_.assets.find(medkitAssetId);
    if (asset == nullptr)
    {
        return false;
    }
    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        return definition.medicalUse.has_value() &&
               definition.medicalUse->effect ==
                   MedicalItemEffect::RestoreHealth &&
               startAlphaMedical(medkitAssetId);
    }
    catch (...)
    {
        return false;
    }
}

bool GameSession::startAlphaMedical(AssetInstanceId medicalAssetId)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value())
    {
        return false;
    }
    const MedicalUsePlan plan = queryMedicalUse(
        profile_,
        publishedContentRegistry(),
        medicalAssetId,
        MedicalAccess::CarriedOnly);
    const AssetRecord *asset = profile_.assets.find(medicalAssetId);
    if (!plan.canCommit || asset == nullptr)
    {
        return false;
    }
    const ItemDefinition &definition = publishedContentRegistry().item(
        asset->definitionId);
    const bool started = plan.canCommit && raidActionState_.start(MedicalRaidAction{
        medicalAssetId,
        plan.effect,
        plan.slowMovement,
        false,
        0,
        plan.effect == MedicalUseEffect::RestoreHealth
            ? static_cast<int>(definition.medicalUse->effectMagnitude)
            : 0,
        0.0F,
        static_cast<float>(plan.durationMs) / 1000.0F});
    if (started)
    {
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MedicalStarted);
    }
    return started;
}

bool GameSession::startAlphaWeaponMaintenance(
    AssetInstanceId kitAssetId,
    AssetInstanceId weaponAssetId)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value())
    {
        return false;
    }
    const WeaponMaintenancePlan plan = queryWeaponMaintenance(
        profile_,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kitAssetId,
            weaponAssetId,
            MaintenanceAccess::CarriedOnly,
            MaintenanceLocation::Raid});
    return plan.canCommit && raidActionState_.start(
        WeaponMaintenanceRaidAction{
            kitAssetId,
            weaponAssetId,
            0.0F,
            static_cast<float>(plan.actionDurationMs) / 1000.0F});
}

bool GameSession::startAlphaArmorMaintenance(
    AssetInstanceId kitAssetId,
    AssetInstanceId armorAssetId)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value())
    {
        return false;
    }
    const ArmorMaintenancePlan plan = queryArmorMaintenance(
        profile_,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kitAssetId,
            armorAssetId,
            MaintenanceAccess::CarriedOnly,
            MaintenanceLocation::Raid});
    return plan.canCommit && raidActionState_.start(
        ArmorMaintenanceRaidAction{
            kitAssetId,
            armorAssetId,
            0.0F,
            static_cast<float>(plan.actionDurationMs) / 1000.0F});
}

bool GameSession::startAlphaWeaponSwitch(EquipmentSlotKind targetSlot)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value() ||
        !isWeaponEquipmentSlot(targetSlot) ||
        targetSlot == activeWeaponSlot_)
    {
        return false;
    }
    const auto target = equippedAsset(profile_, targetSlot);
    const AssetRecord *asset = target.has_value()
        ? profile_.assets.find(*target)
        : nullptr;
    if (asset == nullptr)
    {
        return false;
    }
    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        if (!definition.weaponUse.has_value())
        {
            return false;
        }
        const float handlingMultiplier =
            hasPain(profile_.medicalStatus) &&
                    !painIsSuppressed(profile_.medicalStatus)
                ? 0.9F
                : 1.0F;
        return raidActionState_.start(WeaponSwitchRaidAction{
            activeWeaponSlot_,
            targetSlot,
            0.0F,
            deriveWeaponHandling(*definition.weaponUse)
                    .switchDurationSeconds /
                handlingMultiplier});
    }
    catch (...)
    {
        return false;
    }
}

bool GameSession::observeAlphaWeaponClearMotion(Vec2 delta)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value())
    {
        weaponClearGesture_.reset();
        return false;
    }
    const auto weapon = activeAlphaWeapon();
    const AssetRecord *record = weapon.has_value()
        ? profile_.assets.find(*weapon)
        : nullptr;
    if (record == nullptr ||
        record->weaponMalfunction == WeaponMalfunctionType::None)
    {
        weaponClearGesture_.reset();
        return false;
    }
    if (!weaponClearGesture_.observe(delta, raidElapsedSeconds_))
    {
        return false;
    }
    ProfileState candidate = profile_;
    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
        candidate,
        publishedContentRegistry(),
        ClearWeaponMalfunctionCommand{*weapon},
        CommandContext{
            profile_.revision,
            nextRaidTransaction("clear-malfunction")});
    weaponClearGesture_.reset();
    const bool cleared = receipt.succeeded &&
        commitProfileCandidate(std::move(candidate), false);
    if (cleared)
    {
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MalfunctionCleared);
    }
    return cleared;
}

bool GameSession::startAlphaUnloadMagazine(
    AssetInstanceId magazineAssetId)
{
    if (!alphaRaidActive_ || raidActionState_.active().has_value())
    {
        return false;
    }
    const auto destination = selectRaidMagazineUnloadDestination(
        profile_,
        publishedContentRegistry(),
        magazineAssetId);
    const float handlingMultiplier =
        hasPain(profile_.medicalStatus) &&
                !painIsSuppressed(profile_.medicalStatus)
            ? 0.9F
            : 1.0F;
    return destination.has_value() && raidActionState_.start(
        UnloadMagazineRaidAction{
            magazineAssetId,
            *destination,
            0.0F,
            3.0F / handlingMultiplier});
}

bool GameSession::alphaRaidActive() const noexcept
{
    return alphaRaidActive_;
}

bool GameSession::recoveredAbandonedRaid() const noexcept
{
    return recoveredAbandonedRaid_;
}

const ProfileState &GameSession::profile() const noexcept
{
    return profile_;
}

EquipmentSlotKind GameSession::activeAlphaWeaponSlot() const noexcept
{
    return activeWeaponSlot_;
}

std::optional<AssetInstanceId> GameSession::activeAlphaWeapon() const noexcept
{
    return isWeaponEquipmentSlot(activeWeaponSlot_)
        ? equippedAsset(profile_, activeWeaponSlot_)
        : std::nullopt;
}

std::optional<DeveloperWeaponTuningSnapshot>
GameSession::developerWeaponTuning() const
{
    if (!alphaRaidActive_)
    {
        return std::nullopt;
    }
    const std::optional<AssetInstanceId> weapon = activeAlphaWeapon();
    if (!weapon.has_value())
    {
        return std::nullopt;
    }
    const AssetRecord *asset = profile_.assets.find(*weapon);
    if (asset == nullptr)
    {
        return std::nullopt;
    }

    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        if (!definition.weaponUse.has_value())
        {
            return std::nullopt;
        }
        if (const auto index = developerWeaponOverrideIndex(*weapon))
        {
            const DeveloperWeaponOverride &entry =
                developerWeaponOverrides_[*index];
            return DeveloperWeaponTuningSnapshot{
                *weapon,
                asset->definitionId,
                entry.weaponUse,
                effectiveDeveloperHandling(entry),
                true};
        }
        return DeveloperWeaponTuningSnapshot{
            *weapon,
            asset->definitionId,
            *definition.weaponUse,
            deriveWeaponHandling(*definition.weaponUse),
            false};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool GameSession::adjustDeveloperWeaponTuning(
    DeveloperWeaponParameter parameter,
    int direction,
    bool coarseStep)
{
    if (!alphaRaidActive_ || world_ == nullptr ||
        !world_->raidSession().isActive() ||
        (direction != -1 && direction != 1) ||
        parameter == DeveloperWeaponParameter::Count)
    {
        return false;
    }
    const std::optional<AssetInstanceId> weapon = activeAlphaWeapon();
    if (!weapon.has_value())
    {
        return false;
    }
    const AssetRecord *asset = profile_.assets.find(*weapon);
    if (asset == nullptr)
    {
        return false;
    }

    const ItemDefinition *definition{};
    try
    {
        definition = &publishedContentRegistry().item(asset->definitionId);
    }
    catch (...)
    {
        return false;
    }
    if (!definition->weaponUse.has_value())
    {
        return false;
    }

    std::optional<std::size_t> index =
        developerWeaponOverrideIndex(*weapon);
    const bool createdOverride = !index.has_value();
    if (!index.has_value())
    {
        developerWeaponOverrides_.push_back(DeveloperWeaponOverride{
            *weapon,
            *definition->weaponUse,
            {}});
        index = developerWeaponOverrides_.size() - 1U;
    }
    DeveloperWeaponOverride &entry = developerWeaponOverrides_[*index];
    const DeveloperWeaponOverride previousEntry = entry;
    const WeaponHandlingParameters previousHandling =
        effectiveDeveloperHandling(entry);

    const auto adjustFloat = [direction](
                                 float value,
                                 float fineStep,
                                 float coarse,
                                 float minimum,
                                 float maximum,
                                 bool useCoarse)
    {
        return std::clamp(
            value + static_cast<float>(direction) *
                        (useCoarse ? coarse : fineStep),
            minimum,
            maximum);
    };
    const auto adjustAttribute = [direction, coarseStep](std::uint32_t value)
    {
        const int step = coarseStep ? 5 : 1;
        return static_cast<std::uint32_t>(std::clamp(
            static_cast<int>(value) + direction * step,
            0,
            100));
    };

    switch (parameter)
    {
    case DeveloperWeaponParameter::RecoilControl:
        entry.weaponUse.recoilControl =
            adjustAttribute(entry.weaponUse.recoilControl);
        break;
    case DeveloperWeaponParameter::Stability:
        entry.weaponUse.stability =
            adjustAttribute(entry.weaponUse.stability);
        break;
    case DeveloperWeaponParameter::HandlingSpeed:
        entry.weaponUse.handlingSpeed =
            adjustAttribute(entry.weaponUse.handlingSpeed);
        break;
    case DeveloperWeaponParameter::Ergonomics:
        entry.weaponUse.ergonomics =
            adjustAttribute(entry.weaponUse.ergonomics);
        break;
    case DeveloperWeaponParameter::Accuracy:
        entry.weaponUse.accuracy =
            adjustAttribute(entry.weaponUse.accuracy);
        break;
    case DeveloperWeaponParameter::ShotInterval:
        entry.weaponUse.shotIntervalSeconds = adjustFloat(
            entry.weaponUse.shotIntervalSeconds,
            0.01F,
            0.05F,
            0.03F,
            2.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::BaseDamage:
        entry.weaponUse.baseDamage = std::clamp(
            entry.weaponUse.baseDamage + direction * (coarseStep ? 5 : 1),
            1,
            1000);
        break;
    case DeveloperWeaponParameter::EffectiveRange:
        entry.weaponUse.effectiveRange = adjustFloat(
            entry.weaponUse.effectiveRange,
            10.0F,
            50.0F,
            25.0F,
            entry.weaponUse.maximumRange,
            coarseStep);
        break;
    case DeveloperWeaponParameter::MaximumRange:
        entry.weaponUse.maximumRange = adjustFloat(
            entry.weaponUse.maximumRange,
            10.0F,
            50.0F,
            entry.weaponUse.effectiveRange,
            5000.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::LogicalBallisticSpeed:
        entry.weaponUse.logicalBallisticSpeed = adjustFloat(
            entry.weaponUse.logicalBallisticSpeed,
            100.0F,
            500.0F,
            500.0F,
            20000.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::MaximumReticleSpeed:
        entry.hidden.maximumReticleSpeed = adjustFloat(
            previousHandling.maximumReticleSpeed,
            25.0F,
            100.0F,
            50.0F,
            5000.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::ReticleControlAcceleration:
        entry.hidden.reticleControlAcceleration = adjustFloat(
            previousHandling.reticleControlAcceleration,
            100.0F,
            500.0F,
            100.0F,
            20000.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::SpreadPerShot:
        entry.hidden.spreadPerShotDegrees = adjustFloat(
            previousHandling.spreadPerShotDegrees,
            0.05F,
            0.25F,
            0.0F,
            30.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::RecoilLateralRatio:
        entry.hidden.recoilLateralRatio = adjustFloat(
            previousHandling.recoilLateralRatio,
            0.01F,
            0.05F,
            0.0F,
            1.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::RecoilBendDuration:
        entry.hidden.recoilBendDurationSeconds = adjustFloat(
            previousHandling.recoilBendDurationSeconds,
            0.005F,
            0.025F,
            0.005F,
            0.25F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::MovingSpreadFraction:
        entry.hidden.movingSpreadFraction = adjustFloat(
            previousHandling.movingSpreadFraction,
            0.01F,
            0.05F,
            0.0F,
            previousHandling.sprintingSpreadFraction,
            coarseStep);
        break;
    case DeveloperWeaponParameter::SprintingSpreadFraction:
        entry.hidden.sprintingSpreadFraction = adjustFloat(
            previousHandling.sprintingSpreadFraction,
            0.01F,
            0.05F,
            previousHandling.movingSpreadFraction,
            1.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::ReticleMotionSpreadRate:
        entry.hidden.reticleMotionSpreadDegreesPerSecond = adjustFloat(
            previousHandling.reticleMotionSpreadDegreesPerSecond,
            0.10F,
            0.50F,
            0.0F,
            30.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::NearDistanceSpreadScale:
        entry.hidden.nearDistanceSpreadScale = adjustFloat(
            previousHandling.nearDistanceSpreadScale,
            0.01F,
            0.05F,
            0.0F,
            0.50F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::DistanceBloomAtEffectiveRange:
        entry.hidden.distanceBloomAtEffectiveRange = adjustFloat(
            previousHandling.distanceBloomAtEffectiveRange,
            0.01F,
            0.05F,
            0.0F,
            0.50F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::AdsAccuracyMultiplier:
        entry.hidden.adsAccuracyMultiplier = adjustFloat(
            previousHandling.aimDownSightsAccuracyMultiplier,
            0.01F,
            0.05F,
            0.1F,
            1.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::AdsStabilityMultiplier:
        entry.hidden.adsStabilityMultiplier = adjustFloat(
            previousHandling.aimDownSightsStabilityMultiplier,
            0.01F,
            0.05F,
            0.1F,
            1.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::WeakTracerLength:
        entry.hidden.weakTracerLength = adjustFloat(
            previousHandling.weakTracerLength,
            1.0F,
            5.0F,
            0.0F,
            520.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::WeakTracerOpacity:
        entry.hidden.weakTracerOpacity = adjustFloat(
            previousHandling.weakTracerOpacity,
            0.01F,
            0.05F,
            0.0F,
            1.0F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::WeakTracerLifetime:
        entry.hidden.weakTracerLifetimeSeconds = adjustFloat(
            previousHandling.weakTracerLifetimeSeconds,
            0.005F,
            0.025F,
            0.010F,
            0.250F,
            coarseStep);
        break;
    case DeveloperWeaponParameter::Count:
        return false;
    }

    if (entry == previousEntry)
    {
        if (createdOverride)
        {
            developerWeaponOverrides_.pop_back();
        }
        return false;
    }

    world_->configureWeaponFire(
        entry.weaponUse,
        effectiveDeveloperHandling(entry),
        true);
    configuredWeaponAssetId_ = *weapon;
    return true;
}

bool GameSession::resetDeveloperWeaponTuning()
{
    if (!alphaRaidActive_ || world_ == nullptr)
    {
        return false;
    }
    const std::optional<AssetInstanceId> weapon = activeAlphaWeapon();
    if (!weapon.has_value())
    {
        return false;
    }
    const std::optional<std::size_t> index =
        developerWeaponOverrideIndex(*weapon);
    if (!index.has_value())
    {
        return false;
    }
    const AssetRecord *asset = profile_.assets.find(*weapon);
    if (asset == nullptr)
    {
        return false;
    }
    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        if (!definition.weaponUse.has_value())
        {
            return false;
        }
        developerWeaponOverrides_.erase(
            developerWeaponOverrides_.begin() +
            static_cast<std::ptrdiff_t>(*index));
        world_->configureWeaponFire(
            *definition.weaponUse,
            deriveWeaponHandling(*definition.weaponUse),
            true);
        configuredWeaponAssetId_ = *weapon;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

InventoryReceipt GameSession::executeProfileInventory(
    const InventoryCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    std::string saveMessage;
    InventoryReceipt receipt = executeInventory(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!alphaRaidActive_ && saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            return InventoryReceipt{
                false,
                false,
                DomainErrorCode::InvalidProfile,
                result.message,
                profile_.revision};
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    persistenceMessage_ = std::move(saveMessage);
    if (!alphaRaidActive_)
    {
        refreshLoadoutTutorial();
    }
    return receipt;
}

EconomyReceipt GameSession::executeProfileEconomy(
    const EconomyCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    std::string saveMessage;
    EconomyReceipt receipt = executeEconomy(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            return EconomyReceipt{
                false,
                false,
                DomainErrorCode::InvalidProfile,
                result.message,
                profile_.revision,
                0};
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    persistenceMessage_ = std::move(saveMessage);
    return receipt;
}

BaseResourceReceipt GameSession::executeBaseResourceContribution(
    AssetInstanceId assetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseResourceReceipt receipt = ::executeBaseResourceContribution(
        candidate,
        publishedContentRegistry(),
        ContributeBaseAssetCommand{assetId},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BaseResourceReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision,
            {}};
    }
    return receipt;
}

BaseSupplyAssignmentReceipt GameSession::executeBaseSupplyAssignment(
    ItemDefinitionId definitionId,
    std::optional<BaseSupplyCategory> category,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseSupplyAssignmentReceipt receipt = ::executeBaseSupplyAssignment(
        candidate,
        publishedContentRegistry(),
        SetBaseSupplyAssignmentCommand{
            std::move(definitionId), category},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                persistenceMessage_, profile_.revision};
    }
    return receipt;
}

ConstructionMaterialReceipt
GameSession::executeConstructionMaterialContribution(
    AssetInstanceId assetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    ConstructionMaterialReceipt receipt =
        ::executeConstructionMaterialContribution(
            candidate,
            publishedContentRegistry(),
            ContributeConstructionMaterialCommand{assetId},
            CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return {false, false, DomainErrorCode::InvalidProfile,
                persistenceMessage_, profile_.revision};
    }
    return receipt;
}

BaseConstructionReceipt GameSession::executeStartBaseConstruction(
    BaseConstructionProjectDefinitionId definitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseConstructionReceipt receipt = ::executeStartBaseConstruction(
        candidate,
        publishedContentRegistry(),
        StartBaseConstructionCommand{std::move(definitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseConstructionReceipt GameSession::executeCancelBaseConstruction(
    BaseConstructionProjectDefinitionId definitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseConstructionReceipt receipt = ::executeCancelBaseConstruction(
        candidate,
        publishedContentRegistry(),
        CancelBaseConstructionCommand{std::move(definitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseMigrationReceipt GameSession::executeBaseMigration(
    RegionalBaseSiteDefinitionId targetSiteDefinitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseMigrationReceipt receipt = ::executeBaseMigration(
        candidate,
        publishedContentRegistry(),
        BaseMigrationCommand{std::move(targetSiteDefinitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        return receipt;
    }
    worldClockDirty_ = false;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    return receipt;
}

BaseSiteFeatureRepairReceipt GameSession::executeBaseSiteFeatureRepair(
    RegionalBaseSiteDefinitionId siteDefinitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseSiteFeatureRepairReceipt receipt = ::executeBaseSiteFeatureRepair(
        candidate,
        publishedContentRegistry(),
        BaseSiteFeatureRepairCommand{std::move(siteDefinitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        return receipt;
    }
    worldClockDirty_ = false;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    return receipt;
}

InstallBaseFacilityReceipt GameSession::executeInstallBaseFacility(
    BaseFacilityDefinitionId definitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    InstallBaseFacilityReceipt receipt = ::executeInstallBaseFacility(
        candidate,
        publishedContentRegistry(),
        InstallBaseFacilityCommand{std::move(definitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

RegionalOutpostReceipt GameSession::executeEstablishRegionalOutpost(
    RegionalOutpostDefinitionId definitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RegionalOutpostReceipt receipt = ::executeEstablishRegionalOutpost(
        candidate,
        publishedContentRegistry(),
        EstablishRegionalOutpostCommand{std::move(definitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

RegionalOutpostStaffingReceipt
GameSession::executeRegionalOutpostStaffing(
    RegionalOutpostDefinitionId definitionId,
    bool assign,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RegionalOutpostStaffingReceipt receipt =
        ::executeRegionalOutpostStaffing(
            candidate,
            publishedContentRegistry(),
            RegionalOutpostStaffingCommand{
                std::move(definitionId), assign},
            CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseWorkforceReceipt GameSession::executeAssignBestBaseWorker(
    BaseFacilityStaffingKind facility,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseWorkforceReceipt receipt = ::executeAssignBestBaseWorker(
        candidate,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{facility},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseWorkforceReceipt GameSession::executeClearBaseWorker(
    BaseFacilityStaffingKind facility,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseWorkforceReceipt receipt = ::executeClearBaseWorker(
        candidate,
        publishedContentRegistry(),
        BaseFacilityStaffingCommand{facility},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseWorkforceReceipt GameSession::executeAutoFillBaseWorkers(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseWorkforceReceipt receipt = ::executeAutoFillBaseWorkers(
        candidate,
        publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BasePriorityReceipt GameSession::executeBasePrioritySubmission(
    AssetInstanceId assetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BasePriorityReceipt receipt = ::executeBasePrioritySubmission(
        candidate,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{assetId},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BasePriorityReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision,
            {}};
    }
    return receipt;
}

WeaponAmmoReceipt GameSession::executeProfileWeaponAmmo(
    const WeaponAmmoCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    WeaponAmmoReceipt receipt = executeWeaponAmmo(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(
            std::move(candidate),
            !alphaRaidActive_))
    {
        return WeaponAmmoReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision,
            WeaponAmmoResult::Dry,
            std::nullopt};
    }
    switch (receipt.result)
    {
    case WeaponAmmoResult::Loaded:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MagazineLoaded);
        break;
    case WeaponAmmoResult::Unloaded:
    case WeaponAmmoResult::Uninstalled:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MagazineUnloaded);
        break;
    case WeaponAmmoResult::Installed:
    case WeaponAmmoResult::InstalledAndChambered:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::ReloadCompleted);
        break;
    case WeaponAmmoResult::Chambered:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::WeaponChambered);
        break;
    case WeaponAmmoResult::MalfunctionCleared:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MalfunctionCleared);
        break;
    case WeaponAmmoResult::Dry:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::WeaponDryFire);
        break;
    case WeaponAmmoResult::Fired:
    case WeaponAmmoResult::FiredAndMalfunctioned:
    case WeaponAmmoResult::BlockedByMalfunction:
    case WeaponAmmoResult::Broken:
        break;
    }
    return receipt;
}

HealReceipt GameSession::executeBaseHeal(
    AssetInstanceId medkitAssetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    HealReceipt receipt = executeHeal(
        candidate,
        publishedContentRegistry(),
        medkitAssetId,
        HealAccess::AnyOwned,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return HealReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision,
            0};
    }
    presentationEvents_.push_back(
        GameSessionPresentationEvent::MedicalCompleted);
    return receipt;
}

MedicalUseReceipt GameSession::executeBaseMedical(
    AssetInstanceId medicalAssetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    MedicalUseReceipt receipt = executeMedicalUse(
        candidate,
        publishedContentRegistry(),
        medicalAssetId,
        MedicalAccess::AnyOwned,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return MedicalUseReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision};
    }
    presentationEvents_.push_back(
        GameSessionPresentationEvent::MedicalCompleted);
    return receipt;
}

BaseRestReceipt GameSession::executeBaseRest(
    std::uint32_t hours,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseRestReceipt receipt = ::executeBaseRest(
        candidate,
        publishedContentRegistry(),
        BaseRestCommand{hours},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BaseRestReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision};
    }
    worldClockDirty_ = false;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    return receipt;
}

BaseManufacturingReceipt GameSession::executeStartBaseManufacturing(
    BaseManufacturingRecipeDefinitionId definitionId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseManufacturingReceipt receipt = ::executeStartBaseManufacturing(
        candidate,
        publishedContentRegistry(),
        StartBaseManufacturingCommand{std::move(definitionId)},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseManufacturingReceipt GameSession::executeCancelBaseManufacturing(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseManufacturingReceipt receipt = ::executeCancelBaseManufacturing(
        candidate,
        publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseManufacturingReceipt GameSession::executeCollectBaseManufacturing(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseManufacturingReceipt receipt = ::executeCollectBaseManufacturing(
        candidate,
        publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BaseMedicalServiceReceipt GameSession::executeBasePaidMedicalService(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseMedicalServiceReceipt receipt = executeBaseMedicalService(
        candidate,
        publishedContentRegistry(),
        BaseMedicalServiceCommand{},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        return receipt;
    }
    presentationEvents_.push_back(
        GameSessionPresentationEvent::MedicalCompleted);
    return receipt;
}

ResidentTreatmentReceipt GameSession::executeStartResidentTreatment(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    ResidentTreatmentReceipt receipt = ::executeStartResidentTreatment(
        candidate,
        publishedContentRegistry(),
        StartResidentTreatmentCommand{},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

WeaponMaintenanceReceipt GameSession::executeBaseWeaponMaintenance(
    AssetInstanceId kitAssetId,
    AssetInstanceId weaponAssetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        candidate,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kitAssetId,
            weaponAssetId,
            MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

ArmorMaintenanceReceipt GameSession::executeBaseArmorMaintenance(
    AssetInstanceId kitAssetId,
    AssetInstanceId armorAssetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    ArmorMaintenanceReceipt receipt = executeArmorMaintenance(
        candidate,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kitAssetId,
            armorAssetId,
            MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return ArmorMaintenanceReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision};
    }
    return receipt;
}

GunsmithMaintenanceReceipt GameSession::executeBaseGunsmithMaintenance(
    AssetInstanceId weaponAssetId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    GunsmithMaintenanceReceipt receipt = executeGunsmithMaintenance(
        candidate,
        publishedContentRegistry(),
        StartGunsmithMaintenanceCommand{weaponAssetId},
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

GunsmithCollectionReceipt GameSession::collectBaseGunsmithMaintenance(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    GunsmithCollectionReceipt receipt = executeGunsmithCollection(
        candidate,
        publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

const RaidActionState &GameSession::raidActionState() const noexcept
{
    return raidActionState_;
}

const std::optional<CombatDamageResolution> &
GameSession::lastIncomingDamage() const noexcept
{
    return lastIncomingDamage_;
}

std::vector<GameSessionPresentationEvent>
GameSession::takePresentationEvents()
{
    std::vector<GameSessionPresentationEvent> events;
    events.swap(presentationEvents_);
    return events;
}

SaveLoadStatus GameSession::lastSaveLoadStatus() const noexcept
{
    return lastSaveLoadStatus_;
}

const std::string &GameSession::persistenceMessage() const noexcept
{
    return persistenceMessage_;
}

std::optional<OrdinarySurvivorAdmissionPlan>
GameSession::ordinarySurvivorRescuePlan() const
{
    if (!alphaRaidActive_ || !profile_.pendingRaid.has_value() ||
        !profile_.pendingRaid->rescue.has_value())
    {
        return std::nullopt;
    }
    const RaidRescueSnapshot &rescue = *profile_.pendingRaid->rescue;
    return queryOrdinarySurvivorAdmission(
        profile_,
        OrdinarySurvivorAdmissionCommand{
            rescue.definitionId,
            rescue.ordinaryResidentCount,
            rescue.injuredResidentCount});
}

void GameSession::advanceBaseWorldClock(float deltaTime)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() ||
        state_ != GameSessionState::BetweenRaids)
    {
        return;
    }
    advanceBaseSiegeFromSimulation(deltaTime);
    advanceWorldClockFromSimulation(deltaTime, true);
}

BaseThreatProjection GameSession::baseThreatProjection() const noexcept
{
    return projectBaseThreat(profile_);
}

BaseAutoDefensePlan GameSession::baseAutoDefensePlan() const noexcept
{
    return queryBaseAutoDefense(profile_, publishedContentRegistry());
}

BaseAutoDefenseReceipt GameSession::executeBaseAutoDefense(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BaseAutoDefenseReceipt receipt = ::executeBaseAutoDefense(
        candidate,
        publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
        return receipt;
    }
    baseSiegeWarningSecondAccumulator_ = 0.0F;
    return receipt;
}

bool GameSession::checkpointWorldClock()
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        persistenceMessage_ =
            "active Raid time can only be saved by Settlement";
        return false;
    }
    if (!worldClockDirty_)
    {
        return true;
    }
    const ProfileValidationResult validation =
        validateProfileState(profile_, publishedContentRegistry());
    if (!validation.valid)
    {
        persistenceMessage_ = validation.message;
        return false;
    }
    if (saveRepository_.has_value())
    {
        const SaveWriteResult saved = saveRepository_->save(
            profile_,
            publishedContentRegistry().contentVersion());
        if (!saved.succeeded)
        {
            persistenceMessage_ = saved.message;
            return false;
        }
        persistenceMessage_ = saved.message;
    }
    worldClockDirty_ = false;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    return true;
}

WorldClockProjection GameSession::worldClockProjection() const noexcept
{
    return projectWorldClock(profile_.worldClock);
}

std::optional<RaidTravelPreview> GameSession::raidTravelPreview(
    const MapDefinitionId &mapDefinitionId) const noexcept
{
    try
    {
        return queryRaidTravel(
            profile_,
            publishedContentRegistry(),
            publishedContentRegistry().map(mapDefinitionId));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::vector<LostRaidRecordProjection>
GameSession::lostRaidRecordProjections() const
{
    return queryLostRaidRecords(profile_, publishedContentRegistry());
}

LostRaidAgingPreview GameSession::lostRaidAgingPreview() const noexcept
{
    return queryLostRaidAging(profile_);
}

RecoveryTaskQuote GameSession::recoveryTaskQuote(
    const std::string &recordId) const
{
    return queryStartRecoveryTask(
        profile_, publishedContentRegistry(), recordId);
}

std::optional<RecoveryTaskProjection>
GameSession::recoveryTaskProjection() const
{
    return queryRecoveryTask(profile_, publishedContentRegistry());
}

RecoveryTaskReceipt GameSession::startRecoveryTask(
    const std::string &recordId,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RecoveryTaskReceipt receipt = executeStartRecoveryTask(
        candidate, publishedContentRegistry(), recordId,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

RecoveryTaskReceipt GameSession::cancelRecoveryTask(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RecoveryTaskReceipt receipt = executeCancelRecoveryTask(
        candidate, publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

RecoveryTaskReceipt GameSession::collectRecoveryTask(
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RecoveryTaskReceipt receipt = executeCollectRecoveryTask(
        candidate, publishedContentRegistry(),
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

void GameSession::advanceWorldClockFromSimulation(
    float deltaTime,
    bool allowPeriodicCheckpoint)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }
    const double scaledSeconds =
        pendingWorldSeconds_ +
        static_cast<double>(deltaTime) *
            static_cast<double>(kWorldSecondsPerSimulationSecond);
    const double wholeMinutes = std::floor(
        scaledSeconds / static_cast<double>(kWorldMinutesPerHour));
    const double maximumMinutes = static_cast<double>(
        std::numeric_limits<std::uint64_t>::max());
    const std::uint64_t minutes = wholeMinutes >= maximumMinutes
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(wholeMinutes);
    const double remainingWorldSeconds = minutes ==
            std::numeric_limits<std::uint64_t>::max()
        ? 0.0
        : scaledSeconds -
              static_cast<double>(minutes) *
                  static_cast<double>(kWorldMinutesPerHour);

    if (minutes > 0U)
    {
        ProfileState candidate = profile_;
        const WorldClockAdvanceResult advanced =
            advanceWorldClock(candidate.worldClock, minutes);
        if (advanced.minutesApplied > 0U)
        {
            static_cast<void>(synchronizeBaseDailySystemsThrough(
                candidate,
                publishedContentRegistry()));
            const BaseConstructionAdvanceResult construction =
                applyBaseConstructionThrough(
                    candidate,
                    publishedContentRegistry());
            const BaseManufacturingAdvanceResult manufacturing =
                applyBaseManufacturingThrough(
                    candidate,
                    publishedContentRegistry());
            const ResidentTreatmentAdvanceResult residentTreatment =
                applyResidentTreatmentThrough(candidate);
            const RecoveryTaskAdvanceResult recovery =
                applyRecoveryTaskThrough(candidate);
            if (construction.completed || manufacturing.completed ||
                residentTreatment.completed || recovery.becameReady)
            {
                if (candidate.revision ==
                    std::numeric_limits<ProfileRevision>::max())
                {
                    persistenceMessage_ =
                        "construction completion revision overflow";
                    pendingWorldSeconds_ = scaledSeconds;
                    return;
                }
                ++candidate.revision;
            }
            const ProfileValidationResult validation = validateProfileState(
                candidate,
                publishedContentRegistry());
            if (!validation.valid)
            {
                persistenceMessage_ = validation.message;
                pendingWorldSeconds_ = scaledSeconds;
                return;
            }
            if (construction.completed || manufacturing.completed ||
                residentTreatment.completed || recovery.becameReady)
            {
                if (!commitProfileCandidate(std::move(candidate)))
                {
                    pendingWorldSeconds_ = scaledSeconds;
                    return;
                }
            }
            else
            {
                profile_ = std::move(candidate);
                worldClockDirty_ = true;
            }
        }
    }
    pendingWorldSeconds_ = remainingWorldSeconds;

    if (!allowPeriodicCheckpoint)
    {
        return;
    }
    worldClockCheckpointElapsedSeconds_ = std::min(
        kBaseClockCheckpointIntervalSeconds,
        worldClockCheckpointElapsedSeconds_ + deltaTime);
    if (worldClockDirty_ &&
        worldClockCheckpointElapsedSeconds_ >=
            kBaseClockCheckpointIntervalSeconds)
    {
        static_cast<void>(checkpointWorldClock());
    }
}

void GameSession::advanceBaseSiegeFromSimulation(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F ||
        alphaRaidActive_ || profile_.pendingRaid.has_value() ||
        state_ != GameSessionState::BetweenRaids)
    {
        return;
    }

    ProfileState candidate = profile_;
    const bool activated = activateBaseSiegeWarningIfEligible(candidate);
    bool changed = activated;
    if (activated)
    {
        if (candidate.revision ==
            std::numeric_limits<ProfileRevision>::max())
        {
            persistenceMessage_ = "Base siege warning revision overflow";
            return;
        }
        ++candidate.revision;
        baseSiegeWarningSecondAccumulator_ = 0.0F;
    }

    if (candidate.baseSiege.warningActive &&
        candidate.baseSiege.warningRemainingSeconds > 0U)
    {
        baseSiegeWarningSecondAccumulator_ += deltaTime;
        const std::uint32_t wholeSeconds = static_cast<std::uint32_t>(
            std::floor(baseSiegeWarningSecondAccumulator_));
        if (wholeSeconds > 0U)
        {
            changed = advanceBaseSiegeWarning(candidate, wholeSeconds) ||
                changed;
            baseSiegeWarningSecondAccumulator_ -=
                static_cast<float>(wholeSeconds);
        }
    }

    if (!changed)
    {
        return;
    }

    if (candidate.baseSiege.warningActive &&
        candidate.baseSiege.warningRemainingSeconds == 0U &&
        candidate.baseSiege.autoDefensePresetSaved)
    {
        const std::string transactionId =
            candidate.profileId + "-base-siege-auto-" +
            std::to_string(candidate.baseSiege.siegeSequence);
        const BaseAutoDefenseReceipt receipt = ::executeBaseAutoDefense(
            candidate,
            publishedContentRegistry(),
            CommandContext{candidate.revision, transactionId});
        if (!receipt.succeeded ||
            !commitProfileCandidate(std::move(candidate)))
        {
            persistenceMessage_ = receipt.message.empty()
                ? persistenceMessage_
                : receipt.message;
        }
        baseSiegeWarningSecondAccumulator_ = 0.0F;
        return;
    }

    const ProfileValidationResult validation = validateProfileState(
        candidate, publishedContentRegistry());
    if (!validation.valid)
    {
        persistenceMessage_ = validation.message;
        return;
    }
    if (activated)
    {
        static_cast<void>(commitProfileCandidate(std::move(candidate)));
        return;
    }
    profile_ = std::move(candidate);
    worldClockDirty_ = true;
}

void GameSession::resetWorldClockRuntime() noexcept
{
    pendingWorldSeconds_ = 0.0;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    worldClockDirty_ = false;
    baseSiegeWarningSecondAccumulator_ = 0.0F;
}

void GameSession::updateAlphaRaid(
    const GameplayInput &input,
    float deltaTime)
{
    lastIncomingDamage_.reset();
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        raidElapsedSeconds_ += deltaTime;
    }
    if (!profile_.pendingRaid.has_value())
    {
        alphaRaidActive_ = false;
        outpostRestorationObjectiveSecured_ = false;
        baseSiteClearanceObjectiveSecured_ = false;
        basePerimeterSweepObjectiveSecured_ = false;
        state_ = GameSessionState::BetweenRaids;
        return;
    }
    if (alphaRaidActive_ && world_->raidSession().isActive())
    {
        advanceWorldClockFromSimulation(deltaTime, false);
    }
    synchronizeActiveAlphaWeapon();
    if (input.quitRaidJustPressed && alphaRaidActive_)
    {
        static_cast<void>(activeQuitAlphaRaid());
        return;
    }
    const RaidSessionState currentState = world_->raidSession().state();
    if (currentState == RaidSessionState::Extracted)
    {
        static_cast<void>(settleAlphaRaid(RaidResultOutcome::Extracted));
        return;
    }
    if (currentState == RaidSessionState::PlayerDead)
    {
        static_cast<void>(settleAlphaRaid(RaidResultOutcome::PlayerDead));
        return;
    }
    if (input.inventoryOpen || input.sprint || input.firePressed ||
        input.fireJustPressed || input.reloadJustPressed ||
        input.healJustPressed || input.weaponSlotJustPressed.has_value() ||
        world_->player().isControlled())
    {
        weaponClearGesture_.reset();
    }

    if (input.weaponSlotJustPressed.has_value() &&
        isWeaponEquipmentSlot(*input.weaponSlotJustPressed) &&
        *input.weaponSlotJustPressed != activeWeaponSlot_ &&
        equippedAsset(profile_, *input.weaponSlotJustPressed).has_value())
    {
        bool interruptOnly{};
        if (raidActionState_.active().has_value())
        {
            interruptOnly =
                std::holds_alternative<MedicalRaidAction>(
                    *raidActionState_.active()) ||
                std::holds_alternative<WeaponMaintenanceRaidAction>(
                    *raidActionState_.active()) ||
                std::holds_alternative<ArmorMaintenanceRaidAction>(
                    *raidActionState_.active());
            raidActionState_.cancel();
        }
        if (!interruptOnly)
        {
            static_cast<void>(startAlphaWeaponSwitch(
                *input.weaponSlotJustPressed));
        }
    }

    const auto weapon = activeAlphaWeapon();
    if (!raidActionState_.active().has_value() && !input.inventoryOpen)
    {
        if (input.reloadJustPressed && weapon.has_value())
        {
            if (const auto magazine = selectRaidReloadMagazine(
                    profile_,
                    publishedContentRegistry(),
                    *weapon))
            {
                static_cast<void>(startAlphaReload(*weapon, *magazine));
            }
        }
        else if (input.healJustPressed)
        {
            MedicalUseEffect preferred = MedicalUseEffect::RestoreHealth;
            if (profile_.medicalStatus.bleeding == BleedingSeverity::Heavy)
            {
                preferred = MedicalUseEffect::StopAnyBleeding;
            }
            else if (profile_.medicalStatus.bleeding == BleedingSeverity::Light)
            {
                preferred = MedicalUseEffect::StopLightBleeding;
            }
            if (const auto medical = selectQuickMedicalAsset(
                    profile_, publishedContentRegistry(), preferred))
            {
                static_cast<void>(startAlphaMedical(*medical));
            }
            else if (preferred == MedicalUseEffect::StopLightBleeding)
            {
                if (const auto medical = selectQuickMedicalAsset(
                        profile_, publishedContentRegistry(),
                        MedicalUseEffect::StopAnyBleeding))
                {
                    static_cast<void>(startAlphaMedical(*medical));
                }
            }
        }
    }

    GameplayInput simulationInput = input;
    simulationInput.conditionalExtractionEligible =
        conditionalExtractionEligible();
    simulationInput.reloadJustPressed = false;
    simulationInput.healJustPressed = false;
    simulationInput.quitRaidJustPressed = false;
    bool automaticFire{};
    std::optional<WeaponHandlingParameters> weaponHandling;
    if (weapon.has_value())
    {
        const AssetRecord *weaponAsset = profile_.assets.find(*weapon);
        if (weaponAsset != nullptr)
        {
            try
            {
                const ItemDefinition &definition =
                    publishedContentRegistry().item(weaponAsset->definitionId);
                automaticFire = definition.weaponUse.has_value() &&
                    definition.weaponUse->automaticFire;
                if (definition.weaponUse.has_value())
                {
                    weaponHandling = deriveWeaponHandling(
                        *definition.weaponUse);
                }
            }
            catch (...)
            {
                automaticFire = false;
            }
        }
    }
    if (!automaticFire)
    {
        simulationInput.firePressed = false;
    }
    if (input.inventoryOpen || raidActionState_.active().has_value())
    {
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
        simulationInput.interactJustPressed = false;
        simulationInput.interactPressed = false;
        sprintFireIntentPending_ = false;
        sprintFireReadyRemaining_ = 0.0F;
    }
    const bool selfRecoveryOwnsInteraction =
        selfRecoveryCacheInRange() && profile_.pendingRaid.has_value() &&
        profile_.pendingRaid->selfRecovery.has_value() &&
        !profile_.pendingRaid->selfRecovery->opened;
    if (selfRecoveryOwnsInteraction)
    {
        simulationInput.interactJustPressed = false;
        simulationInput.interactPressed = false;
    }
    if (fireSuppressedUntilRelease_)
    {
        if (input.firePressed)
        {
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
        }
        else
        {
            fireSuppressedUntilRelease_ = false;
        }
    }
    if (sprintSuppressedUntilRelease_)
    {
        if (input.sprint)
        {
            simulationInput.sprint = false;
        }
        else
        {
            sprintSuppressedUntilRelease_ = false;
        }
    }

    const bool directFireIntent =
        simulationInput.fireJustPressed ||
        (automaticFire && simulationInput.firePressed);
    if (simulationInput.sprint && directFireIntent &&
        weaponHandling.has_value() && !input.inventoryOpen &&
        !raidActionState_.active().has_value())
    {
        sprintFireIntentPending_ = true;
        sprintFireReadyRemaining_ =
            weaponHandling->sprintReadyDurationSeconds;
        sprintSuppressedUntilRelease_ = true;
        simulationInput.sprint = false;
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
    }
    else if (sprintFireIntentPending_)
    {
        if (!weapon.has_value() || input.inventoryOpen ||
            raidActionState_.active().has_value() ||
            world_->player().isControlled())
        {
            sprintFireIntentPending_ = false;
            sprintFireReadyRemaining_ = 0.0F;
        }
        else
        {
            simulationInput.sprint = false;
            if (std::isfinite(deltaTime) && deltaTime > 0.0F)
            {
                sprintFireReadyRemaining_ = std::max(
                    0.0F,
                    sprintFireReadyRemaining_ - deltaTime);
            }
            if (sprintFireReadyRemaining_ <= 0.0F)
            {
                simulationInput.fireJustPressed = true;
                simulationInput.firePressed = false;
                sprintFireIntentPending_ = false;
            }
            else
            {
                simulationInput.fireJustPressed = false;
                simulationInput.firePressed = false;
            }
        }
    }

    if (simulationInput.sprint)
    {
        simulationInput.aimDownSights = false;
    }
    else if (simulationInput.aimDownSights && weaponHandling.has_value())
    {
        simulationInput.movementSpeedMultiplier *=
            weaponHandling->aimDownSightsMovementMultiplier;
    }
    if (hasPain(profile_.medicalStatus) &&
        !painIsSuppressed(profile_.medicalStatus))
    {
        simulationInput.movementSpeedMultiplier *= 0.9F;
    }
    if (raidActionState_.active().has_value())
    {
        simulationInput.forceMaximumWeaponSpread =
            std::holds_alternative<ReloadRaidAction>(
                *raidActionState_.active());
        const bool slowMovement = std::visit(
            [](const auto &action)
            {
                using Action = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<Action, MedicalRaidAction>)
                {
                    return action.slowMovement;
                }
                return std::is_same_v<Action, WeaponMaintenanceRaidAction> ||
                       std::is_same_v<Action, ArmorMaintenanceRaidAction>;
            },
            *raidActionState_.active());
        if (slowMovement)
        {
            simulationInput.movementSpeedMultiplier *= 0.45F;
            simulationInput.sprint = false;
        }
    }

    std::optional<ProfileState> firedCandidate;
    if (weapon.has_value() &&
        !raidActionState_.active().has_value() &&
        !input.inventoryOpen &&
        (simulationInput.fireJustPressed ||
         (automaticFire && simulationInput.firePressed)))
    {
        ProfileState candidate = profile_;
        Pcg32 faultRandom{
            profile_.pendingRaid->seed ^ 0x776561706f6e2d66ULL,
            weaponFaultSequence_ + 0x6661756c742d726fULL};
        WeaponAmmoReceipt fire = executeWeaponAmmo(
            candidate,
            publishedContentRegistry(),
            FireWeaponCommand{
                *weapon,
                faultRandom.bounded(10000U),
                faultRandom.next()},
            CommandContext{
                profile_.revision,
                nextRaidTransaction("fire")});
        if (fire.succeeded && fire.result == WeaponAmmoResult::Chambered)
        {
            static_cast<void>(commitProfileCandidate(
                std::move(candidate),
                false));
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
            presentationEvents_.push_back(
                GameSessionPresentationEvent::WeaponChambered);
        }
        else if (fire.succeeded &&
                 (fire.result == WeaponAmmoResult::Fired ||
                  fire.result == WeaponAmmoResult::FiredAndMalfunctioned))
        {
            firedCandidate = std::move(candidate);
        }
        else
        {
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
            if (fire.result == WeaponAmmoResult::Dry)
            {
                presentationEvents_.push_back(
                    GameSessionPresentationEvent::WeaponDryFire);
            }
        }
    }
    else if (!weapon.has_value())
    {
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
    }

    const bool restorationActive =
        profile_.pendingRaid->outpostRestoration.has_value();
    const bool siteClearanceActive =
        profile_.pendingRaid->baseSiteClearance.has_value();
    const bool perimeterSweepActive =
        profile_.pendingRaid->basePerimeterSweep.has_value();
    world_->update(simulationInput, deltaTime);
    if (restorationActive && !outpostRestorationObjectiveSecured_ &&
        world_->aliveInitialEnemyCount() == 0U)
    {
        outpostRestorationObjectiveSecured_ = true;
    }
    if (siteClearanceActive && !baseSiteClearanceObjectiveSecured_ &&
        world_->aliveInitialEnemyCount() == 0U)
    {
        baseSiteClearanceObjectiveSecured_ = true;
    }
    if (perimeterSweepActive && !basePerimeterSweepObjectiveSecured_ &&
        world_->aliveInitialEnemyCount() == 0U)
    {
        basePerimeterSweepObjectiveSecured_ = true;
    }
    if (world_->shotFiredLastUpdate())
    {
        if (!firedCandidate.has_value())
        {
            std::terminate();
        }
        static_cast<void>(commitProfileCandidate(
            std::move(*firedCandidate),
            false));
        world_->emitPlayerNoise(kUnsuppressedGunshotNoiseRadius);
        ++weaponFaultSequence_;
        const AssetRecord *currentWeapon = weapon.has_value()
            ? profile_.assets.find(*weapon)
            : nullptr;
        if (currentWeapon != nullptr &&
            currentWeapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            weaponClearGesture_.reset();
            persistenceMessage_ = "weapon malfunction";
        }
    }

    applyAlphaIncomingDamage();
    if (lastIncomingDamage_.has_value() &&
        lastIncomingDamage_->damageApplied > 0)
    {
        world_->cancelOrdinarySurvivorRescueInteraction();
    }
    else if (world_->ordinarySurvivorRescueReady())
    {
        static_cast<void>(secureOrdinarySurvivorRescue());
    }
    advanceAlphaMedicalStatus(deltaTime);

    if (profile_.pendingRaid->selfRecovery.has_value() &&
        !profile_.pendingRaid->selfRecovery->opened)
    {
        const bool canContinue = selfRecoveryCacheInRange() &&
            input.interactPressed && !input.inventoryOpen &&
            !raidActionState_.active().has_value() &&
            !world_->player().isControlled();
        if (canContinue && std::isfinite(deltaTime) && deltaTime > 0.0F)
        {
            selfRecoveryInteractionSeconds_ += deltaTime;
        }
        else
        {
            selfRecoveryInteractionSeconds_ = 0.0F;
        }
        const float duration = profile_.pendingRaid->selfRecovery
            ->interactionDurationSeconds;
        if (selfRecoveryInteractionSeconds_ >= duration)
        {
            ProfileState candidate = profile_;
            const InventoryReceipt opened = executeOpenRaidSelfRecovery(
                candidate,
                publishedContentRegistry(),
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("self-recovery-open")});
            if (opened.succeeded)
            {
                static_cast<void>(commitProfileCandidate(
                    std::move(candidate), false));
            }
            else
            {
                persistenceMessage_ = opened.message;
            }
            selfRecoveryInteractionSeconds_ = 0.0F;
        }
    }
    else
    {
        selfRecoveryInteractionSeconds_ = 0.0F;
    }

    if (world_->raidSession().state() == RaidSessionState::PlayerDead)
    {
        raidActionState_.cancel();
        static_cast<void>(settleAlphaRaid(RaidResultOutcome::PlayerDead));
        return;
    }
    if (world_->raidSession().state() == RaidSessionState::Extracted)
    {
        raidActionState_.cancel();
        static_cast<void>(settleAlphaRaid(RaidResultOutcome::Extracted));
        return;
    }

    if (raidActionState_.active().has_value())
    {
        const bool controlled = world_->player().isControlled();
        const bool tookDamage = lastIncomingDamage_.has_value() &&
            lastIncomingDamage_->damageApplied > 0;
        const bool interrupted = controlled || input.inventoryOpen ||
            std::visit(
                [&input, tookDamage](const auto &action)
                {
                    using Action = std::decay_t<decltype(action)>;
                    if constexpr (std::is_same_v<Action, ReloadRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, LoadMagazineRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed ||
                               input.healJustPressed;
                    }
                    else if constexpr (std::is_same_v<Action, HealRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, MedicalRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed || input.sprint;
                    }
                    else if constexpr (
                        std::is_same_v<Action, UnloadMagazineRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, WeaponMaintenanceRaidAction>)
                    {
                        return tookDamage || input.sprint ||
                               input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, ArmorMaintenanceRaidAction>)
                    {
                        return tookDamage || input.sprint || input.firePressed ||
                               input.fireJustPressed ||
                               input.reloadJustPressed ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, WeaponSwitchRaidAction>)
                    {
                        return input.sprint || input.firePressed ||
                               input.fireJustPressed ||
                               input.reloadJustPressed ||
                               input.healJustPressed;
                    }
                    else
                    {
                        return false;
                    }
                },
                *raidActionState_.active());
        const bool suppressCombatAfterInterrupt =
            std::holds_alternative<MedicalRaidAction>(
                *raidActionState_.active()) ||
            std::holds_alternative<WeaponMaintenanceRaidAction>(
                *raidActionState_.active()) ||
            std::holds_alternative<ArmorMaintenanceRaidAction>(
                *raidActionState_.active()) ||
            std::holds_alternative<WeaponSwitchRaidAction>(
                *raidActionState_.active());
        if (interrupted && suppressCombatAfterInterrupt)
        {
            fireSuppressedUntilRelease_ =
                fireSuppressedUntilRelease_ || input.firePressed ||
                input.fireJustPressed;
            sprintSuppressedUntilRelease_ =
                sprintSuppressedUntilRelease_ || input.sprint;
        }
        if (!interrupted)
        {
            if (RaidAction *active = raidActionState_.activeMutable())
            {
                if (auto *medical = std::get_if<MedicalRaidAction>(active);
                    medical != nullptr &&
                    !advanceContinuousHealing(*medical, deltaTime))
                {
                    raidActionState_.cancel();
                    state_ = GameSessionState::SettlementBlocked;
                    return;
                }
            }
        }
        const bool medicalAction =
            std::holds_alternative<HealRaidAction>(
                *raidActionState_.active()) ||
            std::holds_alternative<MedicalRaidAction>(
                *raidActionState_.active());
        const RaidActionAdvance advance = raidActionState_.update(
            deltaTime,
            interrupted);
        if (advance == RaidActionAdvance::Interrupted && medicalAction)
        {
            presentationEvents_.push_back(
                GameSessionPresentationEvent::MedicalInterrupted);
        }
        if (advance == RaidActionAdvance::Completed)
        {
            const std::optional<RaidAction> completed =
                raidActionState_.takeCompleted();
            if (completed.has_value())
            {
                if (const auto *reload =
                        std::get_if<ReloadRaidAction>(&*completed))
                {
                    ProfileState candidate = profile_;
                    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
                        candidate,
                        publishedContentRegistry(),
                        InstallMagazineAndChamberCommand{
                            reload->weaponAssetId,
                            reload->magazineAssetId},
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("reload")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate),
                            false));
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::ReloadCompleted);
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *load =
                             std::get_if<LoadMagazineRaidAction>(&*completed))
                {
                    ProfileState candidate = profile_;
                    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
                        candidate,
                        publishedContentRegistry(),
                        LoadMagazineCommand{
                            load->magazineAssetId,
                            load->ammunitionAssetId,
                            load->quantity},
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("load-magazine")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate),
                            false));
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::MagazineLoaded);
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *heal =
                             std::get_if<HealRaidAction>(&*completed))
                {
                    ProfileState candidate = profile_;
                    candidate.currentHealth = world_->player().health();
                    const HealReceipt receipt = executeHeal(
                        candidate,
                        publishedContentRegistry(),
                        heal->medkitAssetId,
                        HealAccess::CarriedOnly,
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("heal")});
                    if (receipt.succeeded && commitProfileCandidate(
                            std::move(candidate),
                            false))
                    {
                        static_cast<void>(
                            world_->restorePlayerHealth(receipt.healedAmount));
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::MedicalCompleted);
                    }
                    else if (!receipt.succeeded)
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *medical =
                             std::get_if<MedicalRaidAction>(&*completed))
                {
                    if (medical->effect == MedicalUseEffect::RestoreHealth)
                    {
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::MedicalCompleted);
                        return;
                    }
                    ProfileState candidate = profile_;
                    candidate.currentHealth = world_->player().health();
                    const MedicalUseReceipt receipt = executeMedicalUse(
                        candidate,
                        publishedContentRegistry(),
                        medical->medicalAssetId,
                        MedicalAccess::CarriedOnly,
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("medical")});
                    if (receipt.succeeded && commitProfileCandidate(
                            std::move(candidate), false))
                    {
                        if (receipt.healedAmount > 0)
                        {
                            static_cast<void>(world_->restorePlayerHealth(
                                receipt.healedAmount));
                        }
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::MedicalCompleted);
                    }
                    else if (!receipt.succeeded)
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *unload =
                             std::get_if<UnloadMagazineRaidAction>(&*completed))
                {
                    ProfileState candidate = profile_;
                    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
                        candidate,
                        publishedContentRegistry(),
                        UnloadMagazineCommand{
                            unload->magazineAssetId,
                            unload->destination},
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("unload-magazine")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate),
                            false));
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::MagazineUnloaded);
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *maintenance =
                             std::get_if<WeaponMaintenanceRaidAction>(
                                 &*completed))
                {
                    ProfileState candidate = profile_;
                    const WeaponMaintenanceReceipt receipt =
                        executeWeaponMaintenance(
                            candidate,
                            publishedContentRegistry(),
                            WeaponMaintenanceCommand{
                                maintenance->kitAssetId,
                                maintenance->weaponAssetId,
                                MaintenanceAccess::CarriedOnly,
                                MaintenanceLocation::Raid},
                            CommandContext{
                                profile_.revision,
                                nextRaidTransaction("weapon-maintenance")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate), false));
                        weaponClearGesture_.reset();
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *maintenance =
                             std::get_if<ArmorMaintenanceRaidAction>(
                                 &*completed))
                {
                    ProfileState candidate = profile_;
                    const ArmorMaintenanceReceipt receipt =
                        executeArmorMaintenance(
                            candidate,
                            publishedContentRegistry(),
                            ArmorMaintenanceCommand{
                                maintenance->kitAssetId,
                                maintenance->armorAssetId,
                                MaintenanceAccess::CarriedOnly,
                                MaintenanceLocation::Raid},
                            CommandContext{
                                profile_.revision,
                                nextRaidTransaction("armor-maintenance")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate), false));
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
                else if (const auto *weaponSwitch =
                             std::get_if<WeaponSwitchRaidAction>(&*completed))
                {
                    if (equippedAsset(profile_, weaponSwitch->targetSlot)
                            .has_value())
                    {
                        activeWeaponSlot_ = weaponSwitch->targetSlot;
                        configuredWeaponAssetId_.reset();
                        synchronizeActiveAlphaWeapon();
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::WeaponEquipped);
                    }
                }
            }
        }
    }

    if (input.interactJustPressed &&
        !world_->spaceTransitionedLastUpdate() &&
        !raidActionState_.active().has_value() &&
        !world_->highRiskControlInteractionInRange() &&
        !world_->ordinarySurvivorRescueInteractionInRange())
    {
        if (const auto loot = nearbyRaidLoot())
        {
            ProfileState candidate = profile_;
            const InventoryReceipt receipt = pickupRaidLoot(
                candidate,
                publishedContentRegistry(),
                *loot,
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("pickup")});
            if (receipt.succeeded)
            {
                static_cast<void>(commitProfileCandidate(
                    std::move(candidate),
                    false));
                presentationEvents_.push_back(
                    GameSessionPresentationEvent::LootPickedUp);
            }
            else
            {
                persistenceMessage_ = receipt.message;
            }
        }
    }
}

bool GameSession::advanceContinuousHealing(
    MedicalRaidAction &action,
    float deltaTime)
{
    if (action.effect != MedicalUseEffect::RestoreHealth ||
        !std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return true;
    }
    const float nextElapsed = std::min(
        action.durationSeconds,
        action.elapsedSeconds + deltaTime);
    const int targetHealing = static_cast<int>(std::floor(
        static_cast<float>(action.maximumHealing) *
        (nextElapsed / action.durationSeconds) + 0.0001F));
    const int requested = std::min(
        std::max(0, targetHealing - action.healedAmount),
        100 - world_->player().health());
    if (requested <= 0)
    {
        return true;
    }

    if (!action.chargeConsumed)
    {
        ProfileState candidate = profile_;
        candidate.currentHealth = world_->player().health();
        const MedicalUseReceipt consumed = beginContinuousHealing(
            candidate,
            publishedContentRegistry(),
            action.medicalAssetId,
            MedicalAccess::CarriedOnly,
            CommandContext{
                profile_.revision,
                nextRaidTransaction("medical-heal-start")});
        if (!consumed.succeeded ||
            !commitProfileCandidate(std::move(candidate), false))
        {
            persistenceMessage_ = consumed.message.empty()
                ? "medical charge could not be committed"
                : consumed.message;
            return false;
        }
        action.chargeConsumed = true;
    }

    ProfileState candidate = profile_;
    if (candidate.revision == std::numeric_limits<ProfileRevision>::max())
    {
        persistenceMessage_ = "continuous healing revision overflow";
        return false;
    }
    candidate.currentHealth = std::min(
        100,
        world_->player().health() + requested);
    ++candidate.revision;
    const ProfileValidationResult validation = validateProfileState(
        candidate,
        publishedContentRegistry());
    if (!validation.valid ||
        !commitProfileCandidate(std::move(candidate), false))
    {
        persistenceMessage_ = validation.message.empty()
            ? "continuous healing could not be committed"
            : validation.message;
        return false;
    }
    const int restored = world_->restorePlayerHealth(requested)
        ? requested
        : 0;
    if (restored != requested)
    {
        persistenceMessage_ = "continuous healing world sync failed";
        return false;
    }
    action.healedAmount += restored;
    return true;
}

void GameSession::advanceAlphaMedicalStatus(float deltaTime)
{
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F ||
        (!hasPain(profile_.medicalStatus) &&
         profile_.medicalStatus.painkillerRemainingMs == 0))
    {
        return;
    }
    medicalTickAccumulatorSeconds_ = std::min(
        1.0F,
        medicalTickAccumulatorSeconds_ + deltaTime);
    while (medicalTickAccumulatorSeconds_ >= 0.1F)
    {
        medicalTickAccumulatorSeconds_ -= 0.1F;
        ProfileState candidate = profile_;
        ++medicalRandomSequence_;
        Pcg32 random{
            candidate.pendingRaid->seed ^ medicalRandomSequence_,
            0x7061696e2d736372ULL};
        const MedicalAdvanceResult advanced = advanceMedicalStatus(
            candidate.medicalStatus,
            candidate.currentHealth,
            100,
            15000U + random.bounded(10001U));
        if (candidate.medicalStatus == profile_.medicalStatus &&
            candidate.currentHealth == profile_.currentHealth)
        {
            continue;
        }
        if (candidate.revision == std::numeric_limits<ProfileRevision>::max())
        {
            state_ = GameSessionState::SettlementBlocked;
            persistenceMessage_ = "medical status revision overflow";
            return;
        }
        ++candidate.revision;
        if (!commitProfileCandidate(std::move(candidate), false))
        {
            state_ = GameSessionState::SettlementBlocked;
            return;
        }
        if (advanced.healthLost > 0)
        {
            static_cast<void>(world_->damagePlayer(advanced.healthLost));
        }
        if (advanced.screamed)
        {
            world_->emitPlayerNoise(300.0F);
        }
    }
}

void GameSession::applyAlphaIncomingDamage()
{
    for (const PlayerDamageObservation &observation :
         world_->takePlayerDamageObservations())
    {
        if (!world_->raidSession().isActive())
        {
            break;
        }

        ProfileState candidate = profile_;
        const std::uint64_t woundSequence = ++woundRandomSequence_;
        Pcg32 woundRandom{
            profile_.pendingRaid->seed ^ woundSequence,
            0x776f756e642d726fULL};
        const IncomingDamageReceipt receipt = executeIncomingDamage(
            candidate,
            publishedContentRegistry(),
            IncomingDamageCommand{
                observation.baseDamage,
                observation.region,
                observation.penetration,
                observation.armorDamage,
                observation.weakPoint,
                WoundRollCommand{
                    observation.woundSource,
                    woundRandom.bounded(10000U),
                    15000U + woundRandom.bounded(10001U)}},
            CommandContext{
                profile_.revision,
                nextRaidTransaction("incoming-damage")});
        if (!receipt.succeeded ||
            !commitProfileCandidate(std::move(candidate), false))
        {
            persistenceMessage_ = receipt.message.empty()
                ? "incoming damage could not be committed"
                : receipt.message;
            state_ = GameSessionState::SettlementBlocked;
            break;
        }

        lastIncomingDamage_ = receipt.resolution;

        static_cast<void>(world_->damagePlayer(
            receipt.resolution.damageApplied));
    }
}

bool GameSession::settleAlphaRaid(RaidResultOutcome outcome)
{
    if (!profile_.pendingRaid.has_value())
    {
        return false;
    }
    ProfileState candidate = profile_;
    candidate.currentHealth = std::max(1, world_->player().health());
    if (candidate.pendingRaid->outpostRestoration.has_value())
    {
        candidate.pendingRaid->outpostRestoration->objectiveSecured =
            outpostRestorationObjectiveSecured_;
    }
    if (candidate.pendingRaid->baseSiteClearance.has_value())
    {
        candidate.pendingRaid->baseSiteClearance->objectiveSecured =
            baseSiteClearanceObjectiveSecured_;
    }
    if (candidate.pendingRaid->basePerimeterSweep.has_value())
    {
        candidate.pendingRaid->basePerimeterSweep->objectiveSecured =
            basePerimeterSweepObjectiveSecured_;
    }
    const RaidSettlementReceipt receipt = settlePendingRaid(
        candidate,
        publishedContentRegistry(),
        candidate.pendingRaid->settlementId,
        outcome);
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    raidActionState_.cancel();
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
    state_ = GameSessionState::BetweenRaids;
    return true;
}

RaidIntelligencePurchaseReceipt GameSession::purchaseRaidIntelligence(
    const RaidIntelligencePurchaseCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RaidIntelligencePurchaseReceipt receipt =
        executeRaidIntelligencePurchase(
            candidate,
            publishedContentRegistry(),
            command,
            CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return RaidIntelligencePurchaseReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision};
    }
    return receipt;
}

RaidInteriorIntelligencePurchaseReceipt
GameSession::purchaseRaidInteriorIntelligence(
    const RaidInteriorIntelligencePurchaseCommand &command,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    RaidInteriorIntelligencePurchaseReceipt receipt =
        executeRaidInteriorIntelligencePurchase(
            candidate,
            publishedContentRegistry(),
            command,
            CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return RaidInteriorIntelligencePurchaseReceipt{
            false,
            false,
            DomainErrorCode::InvalidProfile,
            persistenceMessage_,
            profile_.revision,
            0U};
    }
    return receipt;
}

bool GameSession::secureOrdinarySurvivorRescue()
{
    if (!alphaRaidActive_ || !activeRaidRecoveryProfile_.has_value() ||
        !profile_.pendingRaid.has_value() ||
        !profile_.pendingRaid->rescue.has_value() ||
        profile_.pendingRaid->rescue->secured ||
        !world_->ordinarySurvivorRescueReady())
    {
        return false;
    }

    const RaidRescueSnapshot rescue = *profile_.pendingRaid->rescue;
    const OrdinarySurvivorAdmissionCommand command{
        rescue.definitionId,
        rescue.ordinaryResidentCount,
        rescue.injuredResidentCount};
    ProfileState raidCandidate = profile_;
    ProfileState recoveryCandidate = *activeRaidRecoveryProfile_;
    const OrdinarySurvivorAdmissionReceipt recoveryReceipt =
        executeOrdinarySurvivorAdmission(
            recoveryCandidate,
            publishedContentRegistry(),
            command,
            CommandContext{
                recoveryCandidate.revision,
                "rescue-checkpoint:" +
                    std::string{rescue.definitionId.value()}});
    if (!recoveryReceipt.succeeded)
    {
        persistenceMessage_ = recoveryReceipt.message;
        world_->cancelOrdinarySurvivorRescueInteraction();
        return false;
    }
    raidCandidate.pendingRaid->rescue->secured = true;
    const OrdinarySurvivorAdmissionReceipt raidReceipt =
        executeOrdinarySurvivorAdmission(
            raidCandidate,
            publishedContentRegistry(),
            command,
            CommandContext{
                raidCandidate.revision,
                nextRaidTransaction("rescue")});
    if (!raidReceipt.succeeded || !raidCandidate.pendingRaid.has_value() ||
        !raidCandidate.pendingRaid->rescue.has_value())
    {
        persistenceMessage_ = raidReceipt.message;
        world_->cancelOrdinarySurvivorRescueInteraction();
        return false;
    }
    const ProfileValidationResult validation = validateProfileState(
        raidCandidate,
        publishedContentRegistry());
    if (!validation.valid)
    {
        persistenceMessage_ = validation.message;
        world_->cancelOrdinarySurvivorRescueInteraction();
        return false;
    }

    std::string saveMessage;
    if (saveRepository_.has_value())
    {
        const SaveWriteResult saved = saveRepository_->save(
            recoveryCandidate,
            publishedContentRegistry().contentVersion());
        if (!saved.succeeded)
        {
            persistenceMessage_ = saved.message;
            world_->cancelOrdinarySurvivorRescueInteraction();
            return false;
        }
        saveMessage = saved.message;
    }
    activeRaidRecoveryProfile_ = std::move(recoveryCandidate);
    profile_ = std::move(raidCandidate);
    persistenceMessage_ = std::move(saveMessage);
    world_->confirmOrdinarySurvivorRescue();
    presentationEvents_.push_back(
        GameSessionPresentationEvent::RescueSecured);
    return true;
}

std::string GameSession::nextRaidTransaction(std::string_view prefix)
{
    ++raidCommandSequence_;
    return std::string{prefix} + ":" +
        (profile_.pendingRaid.has_value()
             ? profile_.pendingRaid->raidId
             : profile_.profileId) + ":" +
        std::to_string(raidCommandSequence_);
}

void GameSession::synchronizeActiveAlphaWeapon()
{
    std::optional<AssetInstanceId> active = activeAlphaWeapon();
    if (!active.has_value())
    {
        for (EquipmentSlotKind slot : {
                 EquipmentSlotKind::PrimaryWeapon,
                 EquipmentSlotKind::SecondaryWeapon,
                 EquipmentSlotKind::Sidearm})
        {
            if (const auto candidate = equippedAsset(profile_, slot))
            {
                activeWeaponSlot_ = slot;
                active = candidate;
                break;
            }
        }
    }
    if (configuredWeaponAssetId_ == active)
    {
        return;
    }
    configuredWeaponAssetId_ = active;
    weaponClearGesture_.reset();
    if (!active.has_value())
    {
        return;
    }
    const AssetRecord *asset = profile_.assets.find(*active);
    if (asset == nullptr)
    {
        configuredWeaponAssetId_.reset();
        return;
    }
    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        if (definition.weaponUse.has_value())
        {
            if (const auto index = developerWeaponOverrideIndex(*active))
            {
                const DeveloperWeaponOverride &entry =
                    developerWeaponOverrides_[*index];
                world_->configureWeaponFire(
                    entry.weaponUse,
                    effectiveDeveloperHandling(entry),
                    false);
            }
            else
            {
                world_->configureWeaponFire(*definition.weaponUse);
            }
        }
    }
    catch (...)
    {
        configuredWeaponAssetId_.reset();
    }
}

std::optional<std::size_t>
GameSession::developerWeaponOverrideIndex(
    AssetInstanceId weaponAssetId) const noexcept
{
    for (std::size_t index{}; index < developerWeaponOverrides_.size(); ++index)
    {
        if (developerWeaponOverrides_[index].weaponAssetId == weaponAssetId)
        {
            return index;
        }
    }
    return std::nullopt;
}

WeaponHandlingParameters GameSession::effectiveDeveloperHandling(
    const DeveloperWeaponOverride &override) const noexcept
{
    WeaponHandlingParameters handling = deriveWeaponHandling(override.weaponUse);
    const DeveloperWeaponHiddenOverrides &hidden = override.hidden;
    if (hidden.maximumReticleSpeed.has_value())
    {
        handling.maximumReticleSpeed = *hidden.maximumReticleSpeed;
    }
    if (hidden.reticleControlAcceleration.has_value())
    {
        handling.reticleControlAcceleration =
            *hidden.reticleControlAcceleration;
    }
    if (hidden.spreadPerShotDegrees.has_value())
    {
        handling.spreadPerShotDegrees = *hidden.spreadPerShotDegrees;
    }
    if (hidden.recoilLateralRatio.has_value())
    {
        handling.recoilLateralRatio = *hidden.recoilLateralRatio;
    }
    if (hidden.recoilBendDurationSeconds.has_value())
    {
        handling.recoilBendDurationSeconds =
            *hidden.recoilBendDurationSeconds;
    }
    if (hidden.movingSpreadFraction.has_value())
    {
        handling.movingSpreadFraction = *hidden.movingSpreadFraction;
    }
    if (hidden.sprintingSpreadFraction.has_value())
    {
        handling.sprintingSpreadFraction = *hidden.sprintingSpreadFraction;
    }
    if (hidden.reticleMotionSpreadDegreesPerSecond.has_value())
    {
        handling.reticleMotionSpreadDegreesPerSecond =
            *hidden.reticleMotionSpreadDegreesPerSecond;
    }
    if (hidden.nearDistanceSpreadScale.has_value())
    {
        handling.nearDistanceSpreadScale = *hidden.nearDistanceSpreadScale;
    }
    if (hidden.distanceBloomAtEffectiveRange.has_value())
    {
        handling.distanceBloomAtEffectiveRange =
            *hidden.distanceBloomAtEffectiveRange;
    }
    if (hidden.adsAccuracyMultiplier.has_value())
    {
        handling.aimDownSightsAccuracyMultiplier =
            *hidden.adsAccuracyMultiplier;
    }
    if (hidden.adsStabilityMultiplier.has_value())
    {
        handling.aimDownSightsStabilityMultiplier =
            *hidden.adsStabilityMultiplier;
    }
    if (hidden.weakTracerLength.has_value())
    {
        handling.weakTracerLength = *hidden.weakTracerLength;
    }
    if (hidden.weakTracerOpacity.has_value())
    {
        handling.weakTracerOpacity = *hidden.weakTracerOpacity;
    }
    if (hidden.weakTracerLifetimeSeconds.has_value())
    {
        handling.weakTracerLifetimeSeconds =
            *hidden.weakTracerLifetimeSeconds;
    }
    return handling;
}

std::optional<AssetInstanceId> GameSession::nearbyRaidLoot() const
{
    if (!profile_.pendingRaid.has_value())
    {
        return std::nullopt;
    }
    const Vec2 playerPosition = world_->player().position();
    const float half = world_->player().size() / 2.0F;
    const Vec2 playerCenter{playerPosition.x + half, playerPosition.y + half};
    std::optional<AssetInstanceId> result;
    float bestDistance = 72.0F * 72.0F;
    for (const RaidLootSnapshot &loot : profile_.pendingRaid->loot)
    {
        if (!raidLootAccessible(loot))
        {
            continue;
        }
        const AssetRecord *asset = profile_.assets.find(loot.assetId);
        if (asset == nullptr ||
            !std::holds_alternative<RaidGroundAssetLocation>(asset->location))
        {
            continue;
        }
        const float dx = loot.position.x - playerCenter.x;
        const float dy = loot.position.y - playerCenter.y;
        const float distance = dx * dx + dy * dy;
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            result = loot.assetId;
        }
    }
    return result;
}

bool GameSession::raidLootAccessible(
    const RaidLootSnapshot &loot) const noexcept
{
    return world_ != nullptr &&
        loot.spaceId == world_->activeRaidSpaceId() &&
        (!loot.requiresHighRisk ||
         world_->raidSession().phase() == RaidPhase::HighRisk);
}

bool GameSession::selfRecoveryCacheInRange() const noexcept
{
    if (!alphaRaidActive_ || world_ == nullptr ||
        !profile_.pendingRaid.has_value() ||
        !profile_.pendingRaid->selfRecovery.has_value() ||
        profile_.pendingRaid->selfRecovery->opened ||
        world_->activeRaidSpaceId() != outdoorRaidSpaceId())
    {
        return false;
    }
    const Vec2 playerPosition = world_->player().position();
    const float half = world_->player().size() * 0.5F;
    const Vec2 playerCenter{
        playerPosition.x + half,
        playerPosition.y + half};
    const Vec2 cache = profile_.pendingRaid->selfRecovery->cachePosition;
    const float dx = cache.x - playerCenter.x;
    const float dy = cache.y - playerCenter.y;
    constexpr float kInteractionRadius = 82.0F;
    return dx * dx + dy * dy <=
        kInteractionRadius * kInteractionRadius;
}

std::optional<RaidSelfRecoveryProjection>
GameSession::raidSelfRecoveryProjection() const noexcept
{
    if (!profile_.pendingRaid.has_value() ||
        !profile_.pendingRaid->selfRecovery.has_value())
    {
        return std::nullopt;
    }
    const RaidSelfRecoverySnapshot &recovery =
        *profile_.pendingRaid->selfRecovery;
    const float progress = recovery.opened
        ? 1.0F
        : std::clamp(
              selfRecoveryInteractionSeconds_ /
                  recovery.interactionDurationSeconds,
              0.0F,
              1.0F);
    return RaidSelfRecoveryProjection{
        recovery.sourceRecord.recordId,
        recovery.sourceRecord.mapDefinitionId,
        recovery.cachePosition,
        recovery.roots.size(),
        recovery.opened,
        selfRecoveryCacheInRange(),
        progress};
}

bool GameSession::commitProfileCandidate(
    ProfileState candidate,
    bool persist)
{
    std::string saveMessage;
    if (persist && saveRepository_.has_value())
    {
        const SaveWriteResult result = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!result.succeeded)
        {
            persistenceMessage_ = result.message;
            return false;
        }
        saveMessage = result.message;
    }
    profile_ = std::move(candidate);
    if (persist)
    {
        worldClockDirty_ = false;
        worldClockCheckpointElapsedSeconds_ = 0.0F;
    }
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

void GameSession::refreshLoadoutTutorial()
{
    const bool hasWeapon =
        equippedAsset(profile_, EquipmentSlotKind::PrimaryWeapon).has_value() ||
        equippedAsset(profile_, EquipmentSlotKind::SecondaryWeapon).has_value() ||
        equippedAsset(profile_, EquipmentSlotKind::Sidearm).has_value();
    if (profile_.tutorial != TutorialProgress::PrepareLoadout ||
        !hasWeapon ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return;
    }
    ProfileState candidate = profile_;
    candidate.tutorial = TutorialProgress::FindRaidGate;
    ++candidate.revision;
    static_cast<void>(commitProfileCandidate(std::move(candidate)));
}

void GameSession::noteBaseFacility(BaseFacilityKind facility)
{
    TutorialProgress next = profile_.tutorial;
    if (profile_.tutorial == TutorialProgress::FindStorage &&
        facility == BaseFacilityKind::Storage)
    {
        next = TutorialProgress::PrepareLoadout;
    }
    else if (profile_.tutorial == TutorialProgress::FindRaidGate &&
             facility == BaseFacilityKind::RaidGate)
    {
        next = TutorialProgress::Complete;
    }
    if (next == profile_.tutorial ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return;
    }
    ProfileState candidate = profile_;
    candidate.tutorial = next;
    ++candidate.revision;
    static_cast<void>(commitProfileCandidate(std::move(candidate)));
}

const char *gameSessionStateName(
    GameSessionState state) noexcept
{
    switch (state)
    {
    case GameSessionState::InRaid:
        return "InRaid";
    case GameSessionState::SettlementBlocked:
        return "SettlementBlocked";
    case GameSessionState::BetweenRaids:
        return "BetweenRaids";
    }

    return "Unknown";
}
