#include "game_session.h"
#include "base_world.h"
#include <algorithm>
#include <cmath>
#include <type_traits>

float GameSession::preparePlayerActionInput(GameplayInput &input,float deltaTime,bool meleeSuppressed)
{
    float advance=deltaTime;
    const auto context=playerGameplayContext();
    if(input.reloadGestureJustPressed) cancelPlayerControlIntent();
    if(input.cycleFireModeJustPressed && context.activeWeapon && !context.controlled && !context.dead && !input.inventoryOpen)
    {
        const auto current=weaponFireMode(*context.activeWeapon);
        const auto *asset=profile_.assets.find(*context.activeWeapon);
        if(current && asset) {
            const auto &modes=publishedContentRegistry().item(asset->definitionId).weaponUse->supportedFireModes;
            const auto found=std::find(modes.begin(),modes.end(),*current);
            if(modes.size()>1 && found!=modes.end()) {
                const auto next=modes[(static_cast<std::size_t>(found-modes.begin())+1)%modes.size()];
                static_cast<void>(executeProfileWeaponAmmo(SetWeaponFireModeCommand{*context.activeWeapon,next},nextRaidTransaction("fire-mode")));
            }
        }
    }
    if(input.weaponSlotJustPressed && isWeaponEquipmentSlot(*input.weaponSlotJustPressed) &&
        *input.weaponSlotJustPressed!=activeWeaponSlot_ && equippedAsset(profile_,*input.weaponSlotJustPressed))
    {
        bool interruptOnly{};
        if(raidActionState_.active()) {
            interruptOnly=std::holds_alternative<MedicalRaidAction>(*raidActionState_.active()) ||
                std::holds_alternative<WeaponMaintenanceRaidAction>(*raidActionState_.active()) ||
                std::holds_alternative<ArmorMaintenanceRaidAction>(*raidActionState_.active());
            if(!cancelActiveRaidAction()){state_=GameSessionState::SettlementBlocked;return 0;}
        }
        cancelPlayerControlIntent();
        if(!interruptOnly)static_cast<void>(startAlphaWeaponSwitch(*input.weaponSlotJustPressed));
    }
    const bool interruptReload=input.reloadGestureJustPressed || input.reloadJustPressed;
    bool interrupted{};
    if(interruptReload && raidActionState_.active() &&
        !std::holds_alternative<ReloadRaidAction>(*raidActionState_.active()) &&
        !std::holds_alternative<ChamberRaidAction>(*raidActionState_.active()))
    {
        interrupted=true;
        const bool medical=std::holds_alternative<MedicalRaidAction>(*raidActionState_.active()) || std::holds_alternative<HealRaidAction>(*raidActionState_.active());
        if(!cancelActiveRaidAction()){state_=GameSessionState::SettlementBlocked;return 0;}
        if(medical)presentationEvents_.push_back(GameSessionPresentationEvent::MedicalInterrupted);
    }
    if(!interrupted && context.activeWeapon && !context.dead && !context.controlled && !input.inventoryOpen &&
        !input.sprint && !input.firePressed && !input.fireJustPressed && !meleeSuppressed && input.raidReloadIntents.size()<=2)
    {
        bool startedThisFrame{};
        for(const auto &intent:input.raidReloadIntents)
        {
            if(intent.expectedWeaponId!=*context.activeWeapon || !std::isfinite(intent.ageSeconds) || intent.ageSeconds<0)continue;
            if(intent.kind==RaidReloadInputKind::UpgradeEmergency)
            {
                if(!intent.gestureId || !std::isfinite(intent.doubleTapSeconds) || intent.doubleTapSeconds<0 || intent.doubleTapSeconds>0.25)continue;
                auto *action=raidActionState_.activeMutable();
                if(auto *reload=action?std::get_if<ReloadRaidAction>(action):nullptr) {
                    if(reload->gestureId==intent.gestureId)static_cast<void>(upgradeAlphaReloadEmergency(*context.activeWeapon));
                } else if(const auto *chamber=action?std::get_if<ChamberRaidAction>(action):nullptr;
                    chamber && chamber->gestureId==intent.gestureId && chamber->weaponAssetId==*context.activeWeapon)
                {
                    // A second R chooses the emergency replacement even with
                    // an empty chamber. Its raw-event interval is authoritative.
                    const float elapsed=chamber->elapsedSeconds;
                    const auto candidate=selectRaidReloadMagazine(profile_,publishedContentRegistry(),*context.activeWeapon);
                    if(candidate) {
                        raidActionState_.cancel();
                        if(beginAlphaReload(*context.activeWeapon,*candidate,RaidReloadSource::AutomaticChestRig)) {
                            auto *reload=std::get_if<ReloadRaidAction>(raidActionState_.activeMutable());
                            reload->gestureId=intent.gestureId;reload->elapsedSeconds=elapsed;
                            static_cast<void>(upgradeAlphaReloadEmergency(*context.activeWeapon));
                        }
                    }
                }
                continue;
            }
            if(raidActionState_.active())continue;
            bool started{};
            if(intent.kind==RaidReloadInputKind::Selected && intent.magazineId)
                started=beginAlphaReload(*context.activeWeapon,*intent.magazineId,RaidReloadSource::SelectedChestRig);
            else if(intent.kind==RaidReloadInputKind::Automatic) {
                const auto *gun=profile_.assets.find(*context.activeWeapon);
                const auto magazine=installedMagazine(profile_,*context.activeWeapon);
                if(gun && !gun->chamberedRound && magazine && magazineRoundCount(profile_,*magazine))started=startAlphaChamber(*context.activeWeapon);
                else if(const auto candidate=selectRaidReloadMagazine(profile_,publishedContentRegistry(),*context.activeWeapon))
                    started=beginAlphaReload(*context.activeWeapon,*candidate,RaidReloadSource::AutomaticChestRig);
            }
            if(started) {
                if(auto *action=raidActionState_.activeMutable()) {
                    if(auto *r=std::get_if<ReloadRaidAction>(action))r->gestureId=intent.gestureId;
                    if(auto *c=std::get_if<ChamberRaidAction>(action))c->gestureId=intent.gestureId;
                }
                cancelPlayerControlIntent();
                if(!startedThisFrame){advance=std::min(std::max(0.0F,deltaTime),intent.ageSeconds);startedThisFrame=true;}
            }
        }
    }
    if(!raidActionState_.active() && !input.inventoryOpen && input.healJustPressed) {
        const auto preferred=profile_.medicalStatus.bleeding==BleedingSeverity::Heavy?MedicalUseEffect::StopAnyBleeding:
            profile_.medicalStatus.bleeding==BleedingSeverity::Light?MedicalUseEffect::StopLightBleeding:MedicalUseEffect::RestoreHealth;
        auto medical=selectQuickMedicalAsset(profile_,publishedContentRegistry(),preferred);
        if(!medical && preferred==MedicalUseEffect::StopLightBleeding)medical=selectQuickMedicalAsset(profile_,publishedContentRegistry(),MedicalUseEffect::StopAnyBleeding);
        if(medical)static_cast<void>(startAlphaMedical(*medical));
    }
    return advance;
}

void GameSession::advancePlayerAction(const GameplayInput &input, float deltaTime, float weaponActionDelta)
{
    if (raidActionState_.active().has_value())
    {
        const bool controlled = playerGameplayContext().controlled;
        const bool tookDamage = lastIncomingDamage_.has_value() &&
            lastIncomingDamage_->damageApplied > 0;
        const bool interrupted = controlled || input.inventoryOpen ||
            std::visit(
                [&input, tookDamage](const auto &action)
                {
                    using Action = std::decay_t<decltype(action)>;
                    if constexpr (std::is_same_v<Action, ReloadRaidAction> ||
                                  std::is_same_v<Action, ChamberRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.healJustPressed || input.sprint ||
                               input.meleeJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, LoadMagazineRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) ||
                               input.healJustPressed;
                    }
                    else if constexpr (std::is_same_v<Action, HealRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               input.reloadJustPressed || input.reloadGestureJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, MedicalRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) || input.sprint;
                    }
                    else if constexpr (
                        std::is_same_v<Action, UnloadMagazineRaidAction>)
                    {
                        return input.firePressed || input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, WeaponMaintenanceRaidAction>)
                    {
                        return tookDamage || input.sprint ||
                               input.firePressed || input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, ArmorMaintenanceRaidAction>)
                    {
                        return tookDamage || input.sprint || input.firePressed ||
                               input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) ||
                               input.healJustPressed;
                    }
                    else if constexpr (
                        std::is_same_v<Action, WeaponSwitchRaidAction>)
                    {
                        return input.sprint || input.firePressed ||
                               input.fireJustPressed ||
                               (input.reloadJustPressed || input.reloadGestureJustPressed) ||
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
        RaidActionAdvance advance = RaidActionAdvance::Idle;
        const auto *stagedReload = std::get_if<ReloadRaidAction>(&*raidActionState_.active());
        if (stagedReload && !stagedReload->plan.binding.actionId.empty())
        {
            if (!advanceRaidWeaponAction(weaponActionDelta, interrupted))
            {
                state_ = GameSessionState::SettlementBlocked;
                return;
            }
        }
        else
            advance = raidActionState_.update(
                (std::holds_alternative<ChamberRaidAction>(*raidActionState_.active()) || stagedReload)
                    ? weaponActionDelta : deltaTime, interrupted);
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
                if (const auto *rangeReload = std::get_if<ReloadRaidAction>(&*completed);
                    rangeReload && developerRange_ && !profile_.pendingRaid)
                {
                    static_cast<void>(executeProfileWeaponAmmo(
                        InstallMagazineAndChamberCommand{rangeReload->weaponAssetId, rangeReload->magazineAssetId},
                        nextRaidTransaction("range-reload")));
                }
                else if (const auto *chamber =
                        std::get_if<ChamberRaidAction>(&*completed))
                {
                    ProfileState candidate = profile_;
                    const WeaponAmmoReceipt receipt = executeWeaponAmmo(
                        candidate,
                        publishedContentRegistry(),
                        ChamberWeaponCommand{chamber->weaponAssetId},
                        CommandContext{
                            profile_.revision,
                            nextRaidTransaction("chamber-action")});
                    if (receipt.succeeded)
                    {
                        static_cast<void>(commitProfileCandidate(
                            std::move(candidate),
                            false));
                        if (!receipt.alreadyCommitted)
                            presentationEvents_.push_back(GameSessionPresentationEvent::WeaponChambered);
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
                    candidate.currentHealth = profile_.currentHealth;
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
                        if (alphaRaidActive_) static_cast<void>(world_->restorePlayerHealth(receipt.healedAmount));
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
                    candidate.currentHealth = profile_.currentHealth;
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
                        if (alphaRaidActive_ && receipt.healedAmount > 0)
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
                        if (alphaRaidActive_) synchronizeActiveAlphaWeapon();
                        else if (basePlayerWorld_) { configuredBaseWeaponAssetId_.reset(); synchronizeActiveBaseWeapon(*basePlayerWorld_); }
                        presentationEvents_.push_back(
                            GameSessionPresentationEvent::WeaponEquipped);
                    }
                }
            }
        }
    }

}

void GameSession::cancelWeaponOptics() noexcept
{
    if (world_) world_->cancelWeaponOptic();
    if (basePlayerWorld_) basePlayerWorld_->cancelWeaponOptic();
}

bool GameSession::willOpenHighOptic(const GameplayInput &input, float deltaTime) const noexcept
{
    if (raidActionState_.active() &&
        std::holds_alternative<WeaponSwitchRaidAction>(*raidActionState_.active()))
        return false;
    return alphaRaidActive_
        ? world_ && world_->willOpenHighOptic(input, deltaTime)
        : basePlayerWorld_ && basePlayerWorld_->willOpenHighOptic(input, deltaTime);
}

bool GameSession::blocksInteractionForOptics(const GameplayInput &input, float deltaTime) const noexcept
{
    if (!input.interactPressed && !input.interactJustPressed)
        return false;
    if ((opticInteractionNeedsRelease_ && !input.interactJustPressed) ||
        willOpenHighOptic(input, deltaTime))
        return true;
    // Sprint-to-fire can remove sprint before the world updates aim. Include
    // that possibility for the client's earlier container-opening decision.
    if (input.sprint && (input.fireJustPressed || input.firePressed || sprintFireIntentPending_))
    {
        GameplayInput ready = input;
        ready.sprint = false;
        return willOpenHighOptic(ready, deltaTime);
    }
    return false;
}

void GameSession::gateOpticInteraction(GameplayInput &input, GameplayInput &simulationInput, float deltaTime) noexcept
{
    if (!input.interactPressed || input.interactJustPressed)
        opticInteractionNeedsRelease_ = false;
    if ((!input.interactPressed && !input.interactJustPressed) ||
        (!opticInteractionNeedsRelease_ && !willOpenHighOptic(simulationInput, deltaTime)))
        return;
    opticInteractionNeedsRelease_ = true;
    input.interactJustPressed = input.interactPressed = false;
    simulationInput.interactJustPressed = simulationInput.interactPressed = false;
}

void GameSession::preparePlayerCombatInput(GameplayInput &simulationInput,const GameplayInput &input,float deltaTime,const std::optional<WeaponHandlingParameters> &weaponHandling)
{
    const auto weapon=activeAlphaWeapon();
    const bool directFireIntent =
        simulationInput.fireJustPressed ||
        simulationInput.firePressed;
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
        if (!weapon.has_value() || input.inventoryOpen || input.freezeObservationDirection || input.throwableModal ||
            raidActionState_.active().has_value() ||
            playerGameplayContext().controlled)
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
        const auto &action = *raidActionState_.active();
        if (std::holds_alternative<WeaponSwitchRaidAction>(action))
            simulationInput.allowWeaponOptics = false;
        const bool scopedChamber = std::holds_alternative<ChamberRaidAction>(action) &&
            simulationInput.aimDownSights && simulationInput.allowWeaponOptics &&
            (alphaRaidActive_
                ? world_->configuredWeaponOptic() != WeaponOpticMode::None
                : basePlayerWorld_ && basePlayerWorld_->configuredWeaponOptic() != WeaponOpticMode::None);
        simulationInput.forceMaximumWeaponSpread =
            std::holds_alternative<ReloadRaidAction>(action) || scopedChamber;
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

}

void GameSession::preparePlayerLightInput(const GameplayInput &input,float deltaTime)
{
    if (input.inventoryOpen || input.freezeObservationDirection)
    { lightKeyPending_=false; lightKeyHeldSeconds_=0; lightLongPressConsumed_=false; }
    else
    {
        if (input.lightJustPressed) { lightKeyPending_=true; lightKeyHeldSeconds_=0; lightLongPressConsumed_=false; }
        if (lightKeyPending_ && std::isfinite(deltaTime) && deltaTime>0) lightKeyHeldSeconds_ += deltaTime;
        if (lightKeyPending_ && lightKeyHeldSeconds_>=0.35F && !lightLongPressConsumed_)
        { lightLongPressConsumed_=true; persistenceMessage_="ONLY WEAPON LIGHT | TAP U"; }
        if (input.lightJustReleased)
        {
            if (lightKeyPending_ && !lightLongPressConsumed_) { const auto receipt=toggleWeaponLight(); if (!receipt.succeeded) persistenceMessage_=receipt.message; }
            lightKeyPending_=false; lightKeyHeldSeconds_=0;
        }
    }
    synchronizeWeaponLight(deltaTime);
}

void GameSession::finishBasePlayerFrame(const GameplayInput &input,float deltaTime,float weaponActionDelta)
{
    if(pendingBaseDefenseEnd_)return;
    if(!baseDefenseActive())advanceAlphaMedicalStatus(deltaTime);
    if(profileUiCommitPending())return;
    if(profile_.currentHealth<=0) {
        cancelPlayerControlIntent();
        if(!cancelActiveRaidAction()) { persistenceMessage_="BASE ACTION RECOVERY BLOCKED"; return; }
    } else advancePlayerAction(input,deltaTime,weaponActionDelta);
    synchronizeWeaponLight();
    if(captureBasePlayerEffects(profile_))worldClockDirty_=true;
    else persistenceMessage_="BASE PLAYER EFFECT CHECKPOINT REJECTED";
}
