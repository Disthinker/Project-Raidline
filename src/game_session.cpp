#include "game_session.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "base_world.h"
#include "stable_random.h"

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
    alphaRaidActive_ = false;
    state_ = GameSessionState::BetweenRaids;
    raidNumber_ = profile_.committedSettlements.size() + 1U;
    return true;
}

bool GameSession::deployAlpha(std::uint64_t seed)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() || seed == 0)
    {
        return false;
    }
    const std::size_t number = profile_.committedSettlements.size() + 1U;
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
            MapDefinitionId{"map.v0.test"}},
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
            enemies.push_back(EnemySpawn{
                enemy.position,
                enemy.size,
                enemy.maximumHealth});
        }
        candidateWorld = std::make_unique<GameplayWorld>(RaidWorldConfig{
            publishedContentRegistry().map(snapshot.mapDefinitionId).worldSize,
            snapshot.playerSpawn,
            snapshot.extractionPoint,
            std::move(enemies),
            100,
            candidate.currentHealth,
            true});
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
    if (!commitProfileCandidate(std::move(candidate), false))
    {
        return false;
    }
    persistenceMessage_ = std::move(saveMessage);
    world_.swap(candidateWorld);
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    raidNumber_ = number;
    alphaRaidActive_ = true;
    recoveredAbandonedRaid_ = false;
    raidActionState_.cancel();
    medicalTickAccumulatorSeconds_ = 0.0F;
    medicalRandomSequence_ = 0;
    woundRandomSequence_ = 0;
    fireSuppressedUntilRelease_ = false;
    sprintSuppressedUntilRelease_ = false;
    return true;
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
        equippedAsset(profile_, EquipmentSlotKind::PrimaryWeapon) !=
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
    return plan.canCommit && raidActionState_.start(
        ReloadRaidAction{
            weaponAssetId,
            magazineAssetId,
            0.0F,
            2.0F / handlingMultiplier});
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
    return plan.canCommit && raidActionState_.start(MedicalRaidAction{
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

SaveLoadStatus GameSession::lastSaveLoadStatus() const noexcept
{
    return lastSaveLoadStatus_;
}

const std::string &GameSession::persistenceMessage() const noexcept
{
    return persistenceMessage_;
}

void GameSession::updateAlphaRaid(
    const GameplayInput &input,
    float deltaTime)
{
    lastIncomingDamage_.reset();
    if (!profile_.pendingRaid.has_value())
    {
        alphaRaidActive_ = false;
        state_ = GameSessionState::BetweenRaids;
        return;
    }
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

    const auto weapon = equippedAsset(
        profile_,
        EquipmentSlotKind::PrimaryWeapon);
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
    simulationInput.reloadJustPressed = false;
    simulationInput.healJustPressed = false;
    simulationInput.quitRaidJustPressed = false;
    if (input.inventoryOpen || raidActionState_.active().has_value())
    {
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
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
    if (hasPain(profile_.medicalStatus) &&
        !painIsSuppressed(profile_.medicalStatus))
    {
        simulationInput.movementSpeedMultiplier *= 0.9F;
    }
    if (raidActionState_.active().has_value())
    {
        if (const auto *medical = std::get_if<MedicalRaidAction>(
                &*raidActionState_.active());
            medical != nullptr && medical->slowMovement)
        {
            simulationInput.movementSpeedMultiplier *= 0.45F;
            simulationInput.sprint = false;
        }
    }

    std::optional<ProfileState> firedCandidate;
    if (weapon.has_value() &&
        !raidActionState_.active().has_value() &&
        !input.inventoryOpen &&
        (input.firePressed || input.fireJustPressed))
    {
        ProfileState candidate = profile_;
        WeaponAmmoReceipt fire = executeWeaponAmmo(
            candidate,
            publishedContentRegistry(),
            FireWeaponCommand{*weapon},
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
        }
        else if (fire.succeeded && fire.result == WeaponAmmoResult::Fired)
        {
            firedCandidate = std::move(candidate);
        }
        else
        {
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
        }
    }
    else if (!weapon.has_value())
    {
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
    }

    world_->update(simulationInput, deltaTime);
    if (world_->shotFiredLastUpdate())
    {
        if (!firedCandidate.has_value())
        {
            std::terminate();
        }
        static_cast<void>(commitProfileCandidate(
            std::move(*firedCandidate),
            false));
    }

    applyAlphaIncomingDamage();
    advanceAlphaMedicalStatus(deltaTime);

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
        const bool interrupted = controlled || input.inventoryOpen ||
            std::visit(
                [&input](const auto &action)
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
                    else
                    {
                        return false;
                    }
                },
                *raidActionState_.active());
        const bool medicalWasActive =
            std::holds_alternative<MedicalRaidAction>(
                *raidActionState_.active());
        if (interrupted && medicalWasActive)
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
        const RaidActionAdvance advance = raidActionState_.update(
            deltaTime,
            interrupted);
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
                    }
                    else
                    {
                        persistenceMessage_ = receipt.message;
                    }
                }
            }
        }
    }

    if (input.interactJustPressed &&
        !raidActionState_.active().has_value())
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
    medicalTickAccumulatorSeconds_ = 0.0F;
    fireSuppressedUntilRelease_ = false;
    sprintSuppressedUntilRelease_ = false;
    alphaRaidActive_ = false;
    state_ = GameSessionState::BetweenRaids;
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
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

void GameSession::refreshLoadoutTutorial()
{
    if (profile_.tutorial != TutorialProgress::PrepareLoadout ||
        !equippedAsset(profile_, EquipmentSlotKind::PrimaryWeapon).has_value() ||
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
