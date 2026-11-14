#include "raid_corpse_search.h"
#include "raid_credential_extraction.h"
#include "game_session.h"
#include "base_defense_preparation.h"
#include "home_founding_domain.h"

#include "base_construction_domain.h"
#include "base_emergency_power_domain.h"
#include "base_manufacturing_domain.h"
#include "base_morale_domain.h"
#include "base_resident_medical_domain.h"
#include "base_wish_expedition.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "base_world.h"
#include "loadout_progression.h"
#include "stable_random.h"

namespace
{
    // Current Alpha weapons have no suppressor consumer. Keep this gameplay
    // stimulus separate from audio playback; a later weapon-content slice can
    // data-drive per-shot acoustic profiles without changing AI ownership.
    constexpr float kUnsuppressedGunshotNoiseRadius{900.0F};
    constexpr float kRaidContainerReach{72.0F};
    constexpr float kFreightUnlockSeconds{2.0F};
    constexpr float kFreightBreachSeconds{1.5F};
    constexpr float kRoadBlockadeBreachSeconds{3.0F};
    const LoadoutArchetypeDefinitionId kStarterLoadoutArchetype{
        "loadout_archetype.light_scavenger"};

    bool hasCommittedRaidResult(const ProfileState &profile)
    {
        return !profile.pendingRaid && profile.lastRaidResult &&
            profile.lastRaidResult->outcome != RaidResultOutcome::AbnormalQuit &&
            profile.committedSettlements.contains(profile.lastRaidResult->settlementId);
    }

    // Bounded Daily participants, keyed by the existing stable enemy identity.
    // No Registry, currency, ID allocation or whole-Profile assignment here.
    void checkpointPerimeterEnemies(HomePerimeterSiteSnapshot &site,
        const std::vector<HomePerimeterEnemySnapshot> &runtime,
        const std::vector<EnemyRemovalFact> &removals) noexcept
    {
        for (auto &saved : site.enemies)
        {
            if (std::any_of(removals.begin(), removals.end(), [&](const auto &fact) {
                return fact.id == saved.localId && fact.reason == EnemyRemovalReason::Death;
            }))
            {
                saved.health = 0;
                continue;
            }
            const auto live = std::find_if(runtime.begin(), runtime.end(),
                [&](const auto &enemy) { return enemy.localId == saved.localId; });
            if (live != runtime.end())
            {
                saved.health = live->health;
                saved.position = live->position;
            }
        }
    }
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
    if (profileUiCommitPending())
    {
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        return;
    }
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

std::optional<BaseFacilityKind> GameSession::updateBaseWorld(
    BaseWorld &baseWorld,
    const GameplayInput &rawInput,
    float deltaTime)
{
    bindBasePlayerWorld(&baseWorld);
    if(profileUiCommitPending())return std::nullopt;
    GameplayInput input=rawInput;
    lastIncomingDamage_.reset();
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() ||
        state_ != GameSessionState::BetweenRaids)
    {
        return std::nullopt;
    }
    if (baseDefenseActive() && !prepareBaseDefenseFrame(baseWorld))
        return std::nullopt;
    if (!baseDefenseActive() && !prepareBaseDailyFrame(deltaTime))
        return std::nullopt;
    if (baseDefenseActive())
        deltaTime = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.0F, 0.1F) : 0.0F;
    const RegionalBaseSiteDefinitionId perimeterSite{
        baseWorld.siteDefinitionId()};
    const auto existingPerimeter = profile_.homePerimeter.sites.find(perimeterSite);
    const bool needsPerimeter = !profile_.homePerimeter.activeOuting &&
        (existingPerimeter == profile_.homePerimeter.sites.end() ||
         existingPerimeter->second.cycleIndex != homePerimeterCycleIndex(profile_.worldClock));
    if (profile_.homeFounding.established && !baseDefenseActive() && needsPerimeter)
    {
        ProfileState candidate = profile_;
        std::erase_if(candidate.baseCorpses,[&](const auto &corpse){return corpse.activity.activity==WeaponActionActivity::BaseDaily && corpse.activity.baseSiteDefinitionId==perimeterSite;});
        HomePerimeterGenerationContext generation{
            perimeterSite,
            baseWorld.worldSize(),
            baseWorld.baseParcel(),
            baseWorld.layout().movementBlockers};
        const HomePerimeterEnsureReceipt ensured =
            ensureHomePerimeterSnapshot(
                candidate,
                publishedContentRegistry(),
                generation,
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("home-perimeter-generate")});
        if (!ensured.succeeded)
        {
            persistenceMessage_ = ensured.message;
            return std::nullopt;
        }
        if (ensured.changed) {
            auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::BaseSnapshot;
            prepared->restorePerimeterSite=perimeterSite;
            const auto queued=enqueuePreparedProfileUi(std::move(candidate),std::nullopt,ProfileUiCompletionKind::BaseSnapshot,std::nullopt,std::move(prepared));
            if(!queued.accepted || queued.pending)return std::nullopt;
        }
    }
    const auto perimeterSnapshot = profile_.homePerimeter.sites.find(
        perimeterSite);
    baseWorld.configureHomePerimeter(
        perimeterSnapshot == profile_.homePerimeter.sites.end()
            ? nullptr : &perimeterSnapshot->second);
    const auto restorePerimeterRuntime = [&]()
    {
        baseWorld.discardUncommittedShot();
        baseWorld.configureHomePerimeter(nullptr);
        const auto persisted = profile_.homePerimeter.sites.find(perimeterSite);
        if (persisted != profile_.homePerimeter.sites.end())
            baseWorld.configureHomePerimeter(&persisted->second);
    };
    if (!baseDefenseActive())
    {
        baseWorld.configureGroundBlockers(
            projectBaseGroundMovementBlockers(
                profile_,
                publishedContentRegistry(),
                RegionalBaseSiteDefinitionId{baseWorld.siteDefinitionId()}));
        if (!baseWorld.configureFortifications(profile_.baseFortifications, publishedContentRegistry()))
        {
            persistenceMessage_ = "Invalid base fortification projection";
            return std::nullopt;
        }
    }
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        baseCombatElapsedSeconds_ += deltaTime;
    }

    const auto meleeId=equippedAsset(profile_,EquipmentSlotKind::QuickMelee);
    baseWorld.configureMelee(currentMeleeDefinition(),currentMeleeUsable(),meleeId.value_or(0));
    prepareNoiseDecoyInput(input,rawInput);
    const bool meleeSuppressesThisFrame=input.meleeJustPressed || baseWorld.melee().recoverySeconds()>
        (std::isfinite(deltaTime)?std::max(0.0F,deltaTime):0.0F)+0.000001F;
    prepareMeleeInput(input,deltaTime,baseWorld.melee().recoverySeconds());
    if (state_ == GameSessionState::SettlementBlocked) return std::nullopt;
    if(fireSuppressedUntilRelease_){if(rawInput.firePressed || rawInput.fireJustPressed)input.firePressed=input.fireJustPressed=false;else fireSuppressedUntilRelease_=false;}
    if(sprintSuppressedUntilRelease_){if(rawInput.sprint)input.sprint=false;else sprintSuppressedUntilRelease_=false;}
    const float weaponActionDelta=preparePlayerActionInput(input,deltaTime,meleeSuppressesThisFrame);
    if(state_==GameSessionState::SettlementBlocked)return std::nullopt;

    synchronizeActiveBaseWeapon(baseWorld);
    const std::optional<AssetInstanceId> weapon = activeAlphaWeapon();
    GameplayInput simulationInput = input;
    simulationInput.reloadJustPressed = false;
    simulationInput.healJustPressed = false;
    simulationInput.quitRaidJustPressed = false;

    int ammunitionPenetration{};
    int unarmoredDamageBonus{};
    std::optional<ItemDefinitionId> chamberedRound;
    std::optional<WeaponHandlingParameters> weaponHandling;
    if (weapon.has_value())
    {
        const AssetRecord *weaponAsset = profile_.assets.find(*weapon);
        if (weaponAsset != nullptr)
        {
            try
            {
                const ItemDefinition &definition =
                    publishedContentRegistry().item(
                        weaponAsset->definitionId);
                if (definition.weaponUse.has_value())
                {
                    if (const auto index =
                            developerWeaponOverrideIndex(*weapon))
                    {
                        weaponHandling = effectiveDeveloperHandling(
                            developerWeaponOverrides_[*index]);
                    }
                    else
                    {
                        weaponHandling = deriveWeaponHandling(
                            effectiveWeaponUse(profile_, publishedContentRegistry(), weaponAsset->instanceId));
                    }
                }
                if (weaponAsset->chamberedRound)
                    chamberedRound = weaponAsset->chamberedRound->definitionId;
                const auto &ammunitionId =
                    chamberedRound ? chamberedRound
                                   : definition.compatibleAmmunitionDefinitionId;
                if (ammunitionId.has_value())
                {
                    const ItemDefinition &ammunition =
                        publishedContentRegistry().item(*ammunitionId);
                    if (ammunition.ammunitionUse.has_value())
                    {
                        ammunitionPenetration =
                            ammunition.ammunitionUse->penetration;
                        unarmoredDamageBonus =
                            ammunition.ammunitionUse->unarmoredDamageBonus;
                    }
                }
            }
            catch (...)
            {
            }
        }
    }
    baseWorld.configureWeaponAmmunition(ammunitionPenetration, unarmoredDamageBonus);
    baseWorld.setDaylight(worldClockProjection().timeOfDay == WorldTimeOfDay::Day);
    prepareWeaponTrigger(simulationInput,rawInput,weapon,
        !input.inventoryOpen && !input.freezeObservationDirection && !input.throwableModal &&
        !baseWorld.playerControlled() && !fireSuppressedUntilRelease_ && !decoyFireSuppressedUntilRelease_ &&
        !playerThrowFlightActive() && !baseWorld.grenade().active());
    if(raidActionState_.active()) {
        weaponTriggerGate_.cancelPending();simulationInput.firePressed=simulationInput.fireJustPressed=false;
        simulationInput.interactPressed=simulationInput.interactJustPressed=false;
    }
    preparePlayerCombatInput(simulationInput,input,deltaTime,weaponHandling);
    gateOpticInteraction(input, simulationInput, deltaTime);
    advancePlayerLights(updatePlayerLightGesture(input, deltaTime), input, deltaTime);

    std::optional<FireWeaponCommand> pendingFireCommand;
    std::optional<std::string> pendingFireTransaction;
    if (weapon.has_value() && !input.inventoryOpen &&
        (simulationInput.fireJustPressed ||
         simulationInput.firePressed))
    {
        Pcg32 faultRandom{
            profile_.revision ^ 0x776561706f6e2d66ULL,
            weaponFaultSequence_ + 0x626173652d666972ULL};
        const FireWeaponCommand command{
            *weapon,
            faultRandom.bounded(10000U),
            faultRandom.next(), EmptyChamberPolicy::RequireExplicitChamber};
        const WeaponAmmoPlan fire = queryFireWeapon(
            profile_, publishedContentRegistry(), command);
        if (!input.developerInfiniteAmmo && fire.canCommit &&
            fire.result == WeaponAmmoResult::Chambered)
        {
            const WeaponAmmoReceipt chambered = executeFireWeapon(
                profile_,
                publishedContentRegistry(),
                command,
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("base-fire-chamber")});
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
            if (chambered.succeeded &&
                chambered.result == WeaponAmmoResult::Chambered)
            {
                worldClockDirty_ = true;
                presentationEvents_.push_back(
                    GameSessionPresentationEvent::WeaponChambered);
            }
            else
            {
                persistenceMessage_ = chambered.message;
            }
        }
        else if (fire.canCommit &&
                 (fire.result == WeaponAmmoResult::Fired ||
                  (input.developerInfiniteAmmo &&
                   fire.result == WeaponAmmoResult::Chambered)))
        {
            if (!input.developerInfiniteAmmo)
            {
                pendingFireTransaction.emplace(nextRaidTransaction("base-fire"));
                if (pendingFireTransaction->empty())
                {
                    weaponTriggerGate_.cancelUntilRelease();
                    simulationInput.fireJustPressed = simulationInput.firePressed = false;
                }
                else pendingFireCommand = command;
            }
        }
        else
        {
            weaponTriggerGate_.cancelUntilRelease();
            simulationInput.fireJustPressed = false;
            simulationInput.firePressed = false;
            if (fire.result == WeaponAmmoResult::Dry)
            {
                presentationEvents_.push_back(
                    GameSessionPresentationEvent::WeaponDryFire);
            }
        }
    }
    else if (!weapon.has_value() || input.inventoryOpen)
    {
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
    }

    std::optional<IncomingDamageCommand> dailyGrenadeDamage;
    const std::optional<BaseFacilityKind> facility =
        baseWorld.update(simulationInput, deltaTime, [this,&dailyGrenadeDamage]{return processGrenadeExplosion(&dailyGrenadeDamage);});
    if (baseWorld.shotFiredLastUpdate() &&
        !input.developerInfiniteAmmo && pendingFireCommand.has_value())
    {
        const WeaponAmmoReceipt committed = executeFireWeapon(
            profile_,
            publishedContentRegistry(),
            *pendingFireCommand,
            CommandContext{
                profile_.revision,
                *pendingFireTransaction});
        if (!committed.succeeded ||
            committed.firedAmmunitionDefinitionId != chamberedRound ||
            (committed.result != WeaponAmmoResult::Fired &&
             committed.result != WeaponAmmoResult::FiredAndMalfunctioned))
        {
            if (baseDefenseActive())
            {
                baseDefenseSimulationRejected_ = baseDefenseSaveBlocked_ = true;
                persistenceMessage_ = "DEFENSE SIMULATION REJECTED | RETRY RELOADS LAST CHECKPOINT";
                return std::nullopt;
            }
            restorePerimeterRuntime();
            persistenceMessage_ = committed.message.empty()
                ? "base shot could not be saved"
                : committed.message;
            return facility;
        }
        worldClockDirty_ = true;
        ++weaponFaultSequence_;
    }
    if (baseWorld.shotFiredLastUpdate())
    {
        weaponTriggerGate_.acceptedShot();
        const AssetRecord *currentWeapon = weapon.has_value()
            ? profile_.assets.find(*weapon)
            : nullptr;
        if (currentWeapon != nullptr &&
            currentWeapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            weaponClearGesture_.reset();
            persistenceMessage_ = "weapon malfunction";
            weaponTriggerGate_.cancelUntilRelease();
        }
    }

    if (baseDefenseActive())
    {
        finishBaseDefenseFrame(baseWorld, deltaTime);
        if(baseDefenseActive())finishBasePlayerFrame(input,deltaTime,weaponActionDelta);
        return facility;
    }
    if (!profile_.homeFounding.established) {
        finishBasePlayerFrame(input,deltaTime,weaponActionDelta);
        finishBaseDailyFrame();
        return facility;
    }
    std::optional<ProfileState> grenadeCandidate;
    if(dailyGrenadeDamage) {
        grenadeCandidate=profile_;
        if(!homePerimeterOutingActive(*grenadeCandidate,perimeterSite)) {
            // An already armed legal flight can outlive a completed return.
            // Re-enter this real site's outing before resolving its self-hit.
            const auto begun=beginHomePerimeterOuting(*grenadeCandidate,perimeterSite,
                {grenadeCandidate->revision,nextRaidTransaction("home-perimeter-outing")});
            if(!begun.succeeded){persistenceMessage_=begun.message;restorePerimeterRuntime();return facility;}
        }
        const auto damaged=executeIncomingDamageForHomePerimeterCandidate(*grenadeCandidate,publishedContentRegistry(),
            *dailyGrenadeDamage,{grenadeCandidate->revision,nextRaidTransaction("base-grenade-damage")},perimeterSite);
        if(!damaged.succeeded){persistenceMessage_=damaged.message;restorePerimeterRuntime();return facility;}
        lastIncomingDamage_=damaged.resolution;
        if(damaged.resolution.damageApplied>0)baseWorld.applyPlayerImpactSlow(.18F);
    }
    // A daily hit can be followed by a return/rescue save transaction. Keep
    // only the knife participant until that transaction accepts; rejected
    // saves must restore both its wear and the enemy roster, without copying
    // the entire inventory on every attack.
    struct DailyMeleeRollback
    {
        ProfileState &profile;
        bool &dirty;
        const decltype(restorePerimeterRuntime) &restore;
        std::optional<AssetInstanceId> asset;
        std::uint32_t durability{};
        ProfileRevision revision{};
        bool wasDirty{};
        void rollback()
        {
            if (!asset) return;
            profile.assets.findMutable(*asset)->currentDurability=durability;
            profile.revision=revision;dirty=wasDirty;asset.reset();restore();
        }
        ~DailyMeleeRollback(){rollback();}
        void accept() noexcept {asset.reset();}
    } meleeRollback{profile_,worldClockDirty_,restorePerimeterRuntime,
        baseWorld.melee().result().contact ? equippedAsset(profile_,EquipmentSlotKind::QuickMelee):std::nullopt,
        0,profile_.revision,worldClockDirty_};
    if(meleeRollback.asset)meleeRollback.durability=profile_.assets.find(*meleeRollback.asset)->currentDurability;
    ProfileState &meleeProfile=grenadeCandidate?*grenadeCandidate:profile_;
    bool meleeAllowed=meleeProfile.currentHealth>0;
    if(baseWorld.melee().result().attempted)
    {
        if(const auto damage=baseWorld.perimeterDamageObservation();meleeAllowed && damage && damage->baseDamage>0)
        {
            const auto expected=queryIncomingDamage(meleeProfile,publishedContentRegistry(),
                {damage->baseDamage,damage->region,damage->penetration,damage->armorDamage,damage->weakPoint,{}});
            meleeAllowed=expected.canCommit && expected.resolution.damageApplied<meleeProfile.currentHealth;
        }
        if(!commitPlayerMelee(baseWorld,meleeAllowed,grenadeCandidate?&*grenadeCandidate:nullptr)){restorePerimeterRuntime();return facility;}
    }
    const auto runtimeSnapshot = baseWorld.perimeterEnemySnapshots();
    const auto currentSite = profile_.homePerimeter.sites.find(perimeterSite);
    const bool enemyChanged = currentSite != profile_.homePerimeter.sites.end() &&
        std::any_of(currentSite->second.enemies.begin(), currentSite->second.enemies.end(),
            [&](const HomePerimeterEnemySnapshot &saved) {
                if (saved.health > 0 && std::any_of(baseWorld.perimeterRemovalsLastUpdate().begin(),
                    baseWorld.perimeterRemovalsLastUpdate().end(), [&](const EnemyRemovalFact &fact) {
                        return fact.id == saved.localId && fact.reason == EnemyRemovalReason::Death;
                    })) return true;
                const auto live = std::find_if(runtimeSnapshot.begin(), runtimeSnapshot.end(),
                    [&](const auto &enemy) { return enemy.localId == saved.localId; });
                return live != runtimeSnapshot.end() && live->health != saved.health;
            });
    const bool outing = homePerimeterOutingActive(profile_, perimeterSite);
    const auto zone = baseWorld.playerSafetyZone();
    const auto damageObservation = baseWorld.perimeterDamageObservation();
    const bool otherPerimeterFact = dailyGrenadeDamage.has_value() || (damageObservation && damageObservation->baseDamage > 0) ||
        (!outing && zone == HomeRegionSafetyZone::Perimeter) ||
        (outing && zone == HomeRegionSafetyZone::SafeCore) || profile_.currentHealth <= 0;
    // No full Profile copy on idle/movement frames. Movement remains transient;
    // only accepted discrete facts need a candidate, not every frame or shot.
    if (!enemyChanged && !otherPerimeterFact) {
        meleeRollback.accept();
        finishBasePlayerFrame(input,deltaTime,weaponActionDelta);
        finishBaseDailyFrame();
        return facility;
    }
    if (enemyChanged && !otherPerimeterFact)
    {
        if (profile_.revision == std::numeric_limits<ProfileRevision>::max())
        {
            persistenceMessage_ = "Home perimeter revision overflow";
            return facility;
        }
        // Ordinary hits/deaths change only this small, already validated site
        // participant. Keeping a thousand unrelated inventory assets out of
        // the transaction is as important as keeping disk I/O off this frame.
        checkpointPerimeterEnemies(currentSite->second, runtimeSnapshot,
            baseWorld.perimeterRemovalsLastUpdate());
        ++profile_.revision;
        recordBaseEnemyDeaths(profile_);
        worldClockDirty_ = true;
        meleeRollback.accept();
        finishBasePlayerFrame(input,deltaTime,weaponActionDelta);
        finishBaseDailyFrame();
        return facility;
    }
    bool perimeterChanged=dailyGrenadeDamage.has_value();
    ProfileState perimeterCandidate = grenadeCandidate?std::move(*grenadeCandidate):profile_;
    auto candidateSite = perimeterCandidate.homePerimeter.sites.find(
        perimeterSite);
    if (candidateSite != perimeterCandidate.homePerimeter.sites.end())
    {
        checkpointPerimeterEnemies(candidateSite->second, runtimeSnapshot,
            baseWorld.perimeterRemovalsLastUpdate());
        perimeterChanged = perimeterChanged || enemyChanged;
    }

    // Enter the real Daily outing before applying contact damage. Its private
    // candidate may pass through zero HP only until the rescue below resolves.
    const ProfileRevision damageWoundRevision=perimeterCandidate.revision;
    const HomeRegionSafetyZone zoneAfter = baseWorld.playerSafetyZone();
    if (!homePerimeterOutingActive(perimeterCandidate, perimeterSite) &&
        zoneAfter == HomeRegionSafetyZone::Perimeter)
    {
        const HomePerimeterEnsureReceipt begun = beginHomePerimeterOuting(
            perimeterCandidate,
            perimeterSite,
            CommandContext{
                perimeterCandidate.revision,
                nextRaidTransaction("home-perimeter-outing")});
        if (!begun.succeeded)
        {
            persistenceMessage_ = begun.message;
            return facility;
        }
        perimeterChanged = perimeterChanged || begun.changed;
    }

    if (const auto damage = baseWorld.perimeterDamageObservation();
        perimeterCandidate.currentHealth>0 && damage && damage->baseDamage > 0)
    {
        Pcg32 woundRandom{
            damageWoundRevision ^
                perimeterCandidate.worldClock.elapsedWorldMinutes,
            0x686f6d652d776f75ULL};
        const IncomingDamageReceipt damaged = executeIncomingDamageForHomePerimeterCandidate(
            perimeterCandidate,
            publishedContentRegistry(),
            IncomingDamageCommand{
                damage->baseDamage,
                damage->region,
                damage->penetration,
                damage->armorDamage,
                damage->weakPoint,
                WoundRollCommand{
                    damage->woundSource,
                    woundRandom.bounded(10000U),
                    15000U + woundRandom.bounded(10001U)}},
            CommandContext{
                perimeterCandidate.revision,
                nextRaidTransaction("home-perimeter-damage")},perimeterSite);
        if (!damaged.succeeded)
        {
            persistenceMessage_ = damaged.message;
            return facility;
        }
        lastIncomingDamage_=damaged.resolution;
        if(damaged.resolution.damageApplied>0) {
            baseWorld.applyPlayerImpactSlow(0.18F);
        }
        perimeterChanged = true;
    }

    const bool rescue = perimeterCandidate.currentHealth <= 0;
    const bool returned =
        homePerimeterOutingActive(perimeterCandidate, perimeterSite) &&
        zoneAfter == HomeRegionSafetyZone::SafeCore;
    if (rescue || returned)
    {
        if(!prepareDailyOutingEndCandidate(perimeterCandidate,baseWorld,rescue))return facility;
        perimeterChanged = true;
    }
    else if (perimeterChanged &&
             perimeterCandidate.revision == profile_.revision)
    {
        if (perimeterCandidate.revision ==
            std::numeric_limits<ProfileRevision>::max())
        {
            persistenceMessage_ = "Home perimeter revision overflow";
            return facility;
        }
        ++perimeterCandidate.revision;
    }

    // Movement stays runtime-only. Whenever another discrete perimeter fact is
    // already being saved, checkpoint the current finite enemy positions too
    // so process recovery resumes the same encounter without adding per-frame
    // persistence work.
    // Damage/result commands may replace their candidate Profile. Never keep
    // a map iterator into the previous candidate across those transactions.
    candidateSite = perimeterCandidate.homePerimeter.sites.find(perimeterSite);
    if (perimeterChanged &&
        candidateSite != perimeterCandidate.homePerimeter.sites.end())
    {
        checkpointPerimeterEnemies(candidateSite->second, runtimeSnapshot,
            baseWorld.perimeterRemovalsLastUpdate());
    }

    if(perimeterChanged)recordBaseEnemyDeaths(perimeterCandidate);
    if(rescue || returned) {
        const auto valid=validateProfileState(perimeterCandidate,publishedContentRegistry());
        if(!valid.valid){persistenceMessage_=valid.message;return facility;}
        auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::DailyEnd;
        prepared->restorePerimeterSite=perimeterSite;prepared->rescued=rescue;
        prepared->resumeBaseFrame=true;prepared->resumeInput=input;prepared->resumeDelta=deltaTime;prepared->resumeWeaponActionDelta=weaponActionDelta;
        // Queue against the original authority. The immutable candidate owns
        // this frame's accepted wear/damage/tombstones until it is durable.
        const bool hadMelee=meleeRollback.asset.has_value();meleeRollback.rollback();
        if(!hadMelee)restorePerimeterRuntime();
        static_cast<void>(enqueuePreparedProfileUi(std::move(perimeterCandidate),std::nullopt,ProfileUiCompletionKind::DailyEnd,std::nullopt,std::move(prepared)));
        return facility;
    }
    if (perimeterChanged &&
        !commitProfileCandidate(std::move(perimeterCandidate), false))
    {
        restorePerimeterRuntime();
        return facility;
    }
    if (perimeterChanged) worldClockDirty_ = true;
    meleeRollback.accept();
    finishBasePlayerFrame(input,deltaTime,weaponActionDelta);
    finishBaseDailyFrame();
    return facility;
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

std::uint64_t
GameSession::conditionalExtractionWeightLimitGrams() const noexcept
{
    return world_ == nullptr
        ? 0U
        : world_->conditionalExtractionMaximumWeightGrams();
}

bool GameSession::conditionalExtractionEligible() const
{
    if (raidFuelExtractionEnabled()) return findCarriedRaidFuel(profile_).has_value();
    const std::uint64_t limit = conditionalExtractionWeightLimitGrams();
    return alphaRaidActive_ && limit > 0U &&
           currentRaidCarriedWeightGrams() <= limit;
}

bool GameSession::raidFuelExtractionEnabled() const noexcept
{
    return alphaRaidActive_ && world_ && world_->raidSession().usesFuelExtraction();
}

bool GameSession::raidWorldInteractionAvailable(bool inventoryOpen) const noexcept
{
    return !inventoryOpen && !raidActionState_.active().has_value() &&
        (!world_ || !world_->melee().blocksActions()) &&
        !(selfRecoveryCacheInRange() && profile_.pendingRaid.has_value() &&
          profile_.pendingRaid->selfRecovery.has_value() &&
          !profile_.pendingRaid->selfRecovery->opened);
}

bool GameSession::canCallSignalExtraction(bool inventoryOpen) const noexcept
{
    if (!world_ || !raidWorldInteractionAvailable(inventoryOpen))
        return false;
    GameplayInput input;
    input.inventoryOpen = inventoryOpen;
    input.conditionalExtractionEligible =
        !raidFuelExtractionEnabled() && conditionalExtractionEligible();
    return world_->canCallSignalExtraction(input);
}

RaidFieldTradeAccess GameSession::fieldIntelligenceTradeStatus() const noexcept
{
    if (!alphaRaidActive_ || developerRange_ || !world_ ||
        state_ != GameSessionState::InRaid || !profile_.pendingRaid ||
        !profile_.pendingRaid->fieldIntelligenceTrade ||
        profileUiCommitPending() || inventoryPersistencePending() ||
        !raidWorldInteractionAvailable(false))
        return RaidFieldTradeAccess::Unavailable;
    return world_->fieldIntelligenceTradeStatus();
}

std::optional<RaidFieldTradeView> GameSession::fieldIntelligenceTradeView() const
{
    return queryRaidFieldTrade(profile_, publishedContentRegistry());
}

bool GameSession::sellFieldIntelligenceDocument(AssetInstanceId assetId)
{
    if (fieldIntelligenceTradeStatus() != RaidFieldTradeAccess::Available ||
        (lastIncomingDamage_ && lastIncomingDamage_->damageApplied > 0))
        return false;
    return executeRaidFieldDocumentSale(profile_, assetId);
}

bool GameSession::buyFieldIntelligenceOffer(std::size_t index)
{
    if (fieldIntelligenceTradeStatus() != RaidFieldTradeAccess::Available ||
        (lastIncomingDamage_ && lastIncomingDamage_->damageApplied > 0))
        return false;
    const auto view = fieldIntelligenceTradeView();
    if (!view || index >= view->offers.size() ||
        !executeRaidFieldIntelligencePurchase(profile_, publishedContentRegistry(), index))
        return false;

    const auto &offer = view->offers[index];
    if (offer.mapId == profile_.pendingRaid->mapDefinitionId)
        world_->grantFieldIntelligence(offer.category);
    return true;
}

void GameSession::cancelRaidCredentialInteraction() noexcept
{
    if (world_) world_->interruptCredentialInteraction();
}

void GameSession::cancelRaidFuelExtraction() noexcept
{
    if (world_) world_->interruptFuelExtraction();
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
    if (!drainProfileWritersForReplacement())
        throw std::runtime_error{"Cannot change repository with an unsaved Base checkpoint"};
    baseDefenseWriter_.reset();
    baseDefenseWorld_ = nullptr;
    weaponLightBindingRevision_=0; weaponLightBinding_={};
    nightVisionBindingRevision_.reset();
    selectedTacticalDevice_ = TacticalDeviceKind::WeaponLight;
    cancelWeaponLightInput();
    freightUnlock_ = {};
    freightBreach_ = {};
    saveRepository_.emplace(std::move(directory));
    inventoryPersistence_.reset();
    profileUiPersistence_.reset();++profileUiEpoch_;
    lastSaveLoadStatus_ = SaveLoadStatus::NotFound;
    persistenceMessage_.clear();
}

bool GameSession::hasSavedProfile() const
{
    return saveRepository_.has_value() &&
           saveRepository_->primaryExists();
}

bool GameSession::startNewProfile(std::string profileId, bool survey)
{
    if (!drainProfileWritersForReplacement()) return false;
    baseDefenseWriter_.reset();
    baseDefenseWorld_ = nullptr;
    baseDefenseSaveBlocked_ = false;
    pendingBaseDefenseEnd_.reset();
    pendingBaseDefenseSettlement_.reset();pendingBaseDefenseSettlementGeneration_=0;
    ProfileState candidate;
    std::string saveMessage;
    try
    {
        candidate = (survey ? makeNewHomeProfile : makeNewAlphaProfile)(
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
    nightVisionBindingRevision_.reset();
    selectedTacticalDevice_ = TacticalDeviceKind::WeaponLight;
    cancelWeaponLightInput();
    inventoryPersistence_.reset();
    profileUiPersistence_.reset();++profileUiEpoch_;
    raidActionState_.cancel();
    activeRaidRecoveryProfile_.reset();
    resetWorldClockRuntime();
    configuredBaseWeaponAssetId_.reset();
    developerWeaponOverrides_.clear();
    deferredRaidBasePresentationEvents_.clear();
    alphaRaidActive_ = false;
    recoveredAbandonedRaid_ = false;
    state_ = GameSessionState::BetweenRaids;
    raidNumber_ = 1;
    lastSaveLoadStatus_ = SaveLoadStatus::LoadedPrimary;
    persistenceMessage_ = std::move(saveMessage);
    return true;
}

bool GameSession::establishHome(std::string_view plotId,
    const RegionalBaseSiteDefinitionId &region, Vec2 playerCenter)
{
    ProfileState candidate = profile_;
    const auto receipt = executeHomeFounding(candidate, publishedContentRegistry(),
        plotId, region, playerCenter,
        CommandContext{profile_.revision, nextRaidTransaction("home-founding")});
    if (!receipt.succeeded) { persistenceMessage_ = receipt.message; return false; }
    return receipt.alreadyCommitted || commitProfileCandidate(std::move(candidate));
}

bool GameSession::dismissHomeHints()
{
    // Raid UI may hide hints transiently, but must never checkpoint an active
    // Raid merely to save a presentation preference.
    if (alphaRaidActive_ || profile_.pendingRaid) return false;
    if (profile_.homeFounding.hintsDismissed) return true;
    if (profile_.revision == std::numeric_limits<ProfileRevision>::max()) return false;
    ProfileState candidate = profile_;
    candidate.homeFounding.hintsDismissed = true;
    ++candidate.revision;
    return commitProfileCandidate(std::move(candidate));
}

bool GameSession::finishFirstRaidHints()
{
    if (alphaRaidActive_ || !hasCommittedRaidResult(profile_)) return false;
    if (profile_.homeFounding.hintsDismissed &&
        profile_.tutorial == TutorialProgress::Complete) return true;
    if (profile_.revision == std::numeric_limits<ProfileRevision>::max()) return false;
    ProfileState candidate = profile_;
    candidate.homeFounding.hintsDismissed = true;
    candidate.tutorial = TutorialProgress::Complete;
    ++candidate.revision;
    return commitProfileCandidate(std::move(candidate));
}

bool GameSession::continueProfile()
{
    if (!drainProfileWritersForReplacement()) return false;
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
    const bool requiresStashMigration = candidate.stashLayoutVersion == 0;
    bool profileRequiresSave{};
    if (result.migratedChestRigLayout)
    {
        const auto preserved = saveRepository_->preserveChestRigMigrationSources(result.status);
        if (!preserved.succeeded) { persistenceMessage_ = preserved.message; return false; }
        profileRequiresSave = true;
    }
    bool recoveredPendingRaid{};
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
        profileRequiresSave = true;
        recoveredPendingRaid = true;
    }
    if (requiresStashMigration)
    {
        const ProfileValidationResult migration = migrateLegacyStashPages(
            candidate, publishedContentRegistry());
        if (!migration.valid)
        {
            persistenceMessage_ = migration.message;
            return false;
        }
        const SaveWriteResult preserved =
            saveRepository_->preserveStashMigrationSources(result.status);
        if (!preserved.succeeded)
        {
            persistenceMessage_ = preserved.message;
            return false;
        }
        profileRequiresSave = true;
    }
    const bool previousDevelopmentCatalog =
        candidate.committedTransactions.contains(
            "bootstrap.warehouse_catalog.content_53") &&
        !candidate.committedTransactions.contains(
            "bootstrap.warehouse_catalog.content_54");
    if (previousDevelopmentCatalog)
    {
        const WarehouseCatalogGrantReceipt catalog =
            grantPublishedWarehouseCatalog(
                candidate,
                publishedContentRegistry());
        if (!catalog.succeeded)
        {
            if (!catalog.capacityBlocked)
            {
                persistenceMessage_ = catalog.message;
                return false;
            }
            persistenceMessage_ = catalog.message +
                "; clear Stash space and restart to retry";
        }
        profileRequiresSave = profileRequiresSave ||
            (catalog.succeeded && !catalog.alreadyGranted);
    }
    if (profileRequiresSave)
    {
        const SaveWriteResult saved = saveRepository_->save(
            candidate,
            publishedContentRegistry().contentVersion());
        if (!saved.succeeded)
        {
            persistenceMessage_ = saved.message;
            return false;
        }
        persistenceMessage_ = saved.message;
    }
    weaponLightBindingRevision_=0; weaponLightBinding_={};
    nightVisionBindingRevision_.reset();
    selectedTacticalDevice_ = TacticalDeviceKind::WeaponLight;
    cancelWeaponLightInput();
    baseDefenseWriter_.reset();
    baseDefenseWorld_ = nullptr;
    baseDefenseSaveBlocked_ = false;
    pendingBaseDefenseEnd_.reset();
    pendingBaseDefenseSettlement_.reset();pendingBaseDefenseSettlementGeneration_=0;
    profile_ = std::move(candidate);
    inventoryPersistence_.reset();
    profileUiPersistence_.reset();++profileUiEpoch_;
    raidActionState_.cancel();
    activeRaidRecoveryProfile_.reset();
    resetWorldClockRuntime();
    configuredBaseWeaponAssetId_.reset();
    developerWeaponOverrides_.clear();
    deferredRaidBasePresentationEvents_.clear();
    alphaRaidActive_ = false;
    recoveredAbandonedRaid_ = recoveredPendingRaid;
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
    std::optional<RegionalBaseSiteDefinitionId> basePerimeterSweepId,
    const RaidDeploymentProgressCallback &progress)
{
    if(!drainBaseDailyCheckpoint())return false;
    ProfileState candidate,recoveryProfile;
    auto prepared=prepareAlphaDeployment({seed,std::move(mapDefinitionId),intelligence,std::move(selfRecoveryRecordId),
        std::move(outpostRestorationId),std::move(baseSiteClearanceId),std::move(basePerimeterSweepId)},candidate,recoveryProfile,progress);
    if(!prepared)return false;
    reportRaidDeploymentProgress(progress,.93F,RaidDeploymentProgressStage::SavingProfile);
    if(saveRepository_) {
        const auto saved=saveRepository_->save(recoveryProfile,publishedContentRegistry().contentVersion());
        if(!saved.succeeded){persistenceMessage_=saved.message;return false;}
        persistenceMessage_=saved.message;
    }
    if(!commitProfileCandidate(std::move(candidate),false))return false;
    completePreparedPlayerTransition(prepared);
    reportRaidDeploymentProgress(progress,1.0F,RaidDeploymentProgressStage::Complete);
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

RaidOperationProjection GameSession::raidOperationProjection() const noexcept
{
    const bool active = alphaRaidActive_ && profile_.pendingRaid.has_value() &&
        profile_.pendingRaid && profile_.pendingRaid->basePerimeterSweep.has_value();
    return RaidOperationProjection{
        active,
        active && basePerimeterSweepObjectiveSecured_};
}

std::optional<RaidHighRiskCrisisProjection>
GameSession::raidHighRiskCrisisProjection() const noexcept
{
    if (!alphaRaidActive_ || !profile_.pendingRaid.has_value() ||
        !profile_.pendingRaid->highRiskCrisis.has_value())
    {
        return std::nullopt;
    }
    const PendingRaidSnapshot &raid = *profile_.pendingRaid;
    const RaidHighRiskCrisisSnapshot &crisis = *raid.highRiskCrisis;
    const bool active = world_ != nullptr &&
        world_->raidSession().phase() == RaidPhase::HighRisk;
    std::array<Vec2, 8> pressureSpawnCenters{};
    const std::size_t pressureSpawnCount = std::min(
        pressureSpawnCenters.size(), crisis.pressureSpawns.size());
    for (std::size_t index{}; index < pressureSpawnCount; ++index)
    {
        const RaidHighRiskPressureSpawnSnapshot &spawn =
            crisis.pressureSpawns[index];
        pressureSpawnCenters[index] = {
            spawn.position.x + spawn.size.x * 0.5F,
            spawn.position.y + spawn.size.y * 0.5F};
    }
    return RaidHighRiskCrisisProjection{
        crisis.definitionId,
        crisis.displayName,
        crisis.warning,
        crisis.districtInstanceId,
        crisis.resourcePointInstanceId,
        crisis.focusArea,
        crisis.initialWaveDelaySeconds,
        crisis.waveIntervalSeconds,
        crisis.waveSize,
        crisis.activeEnemyCap,
        pressureSpawnCount,
        pressureSpawnCenters,
        active && world_ != nullptr
            ? world_->highRiskNextWaveSeconds()
            : crisis.initialWaveDelaySeconds,
        active || raid.intelligence.has(RaidIntelligenceCategory::Enemy),
        active};
}

bool GameSession::triggerDeveloperHighRisk() noexcept
{
    if(profileUiCommitPending())return false;
    if (!alphaRaidActive_ || world_ == nullptr ||
        !world_->raidSession().isActive())
    {
        return false;
    }
    return world_->triggerHighRiskForDeveloper();
}

bool GameSession::activeQuitAlphaRaid()
{
    if(profileUiCommitPending())return false;
    return alphaRaidActive_ && settleAlphaRaid(RaidResultOutcome::ActiveQuit);
}

bool GameSession::startAlphaReload(
    AssetInstanceId weaponAssetId,
    AssetInstanceId magazineAssetId)
{
    return beginAlphaReload(weaponAssetId, magazineAssetId, RaidReloadSource::ExplicitStored);
}

std::optional<WeaponFireMode> GameSession::weaponFireMode(
    AssetInstanceId weaponAssetId) const noexcept
{
    return queryWeaponFireMode(profile_, publishedContentRegistry(), weaponAssetId);
}

bool GameSession::beginAlphaReload(
    AssetInstanceId weaponAssetId, AssetInstanceId magazineAssetId,
    RaidReloadSource source)
{
    const auto context = playerGameplayContext();
    if(profileUiCommitPending())return false;
    if(inventoryPersistencePending()){requestInventoryPersistenceBarrier();persistenceMessage_="INVENTORY SAVE PENDING";return false;}
    if (!context.available || context.dead || context.controlled || playerThrowFlightActive() ||
        raidActionState_.active().has_value() ||
        activeAlphaWeapon() != std::optional<AssetInstanceId>{weaponAssetId})
    {
        return false;
    }
    RaidReloadBinding binding{profile_.pendingRaid ? profile_.pendingRaid->raidId : std::string{},
        context.spaceId, weaponAssetId, nextRaidTransaction("reload-action"), magazineAssetId,
        installedMagazine(profile_, weaponAssetId)};
    if (!profile_.pendingRaid) binding.activity = context.activity;
    const auto plan = queryRaidReload(profile_, publishedContentRegistry(),
        RaidReloadRequest{binding, RaidReloadKind::Normal, source, context.stashAccessible});
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
            2.0F / handlingMultiplier, plan, handlingMultiplier, 0});
    if (started)
    {
        presentationEvents_.push_back(
            GameSessionPresentationEvent::ReloadStarted);
    }
    return started;
}

bool GameSession::startAlphaChamber(AssetInstanceId weaponAssetId)
{
    const auto context=playerGameplayContext();
    if (!context.available || context.dead || context.controlled || playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending() || raidActionState_.active() ||
        activeAlphaWeapon() != std::optional<AssetInstanceId>{weaponAssetId}) return false;
    const auto plan = queryWeaponAmmo(profile_, publishedContentRegistry(),
        ChamberWeaponCommand{weaponAssetId});
    if (!plan.canCommit) return false;
    const float efficiency = hasPain(profile_.medicalStatus) &&
        !painIsSuppressed(profile_.medicalStatus) ? 0.9F : 1.0F;
    return raidActionState_.start(ChamberRaidAction{weaponAssetId, 0, 0.5F / efficiency});
}

bool GameSession::upgradeAlphaReloadEmergency(AssetInstanceId expectedWeaponId)
{
    auto *active = raidActionState_.activeMutable();
    auto *reload = active ? std::get_if<ReloadRaidAction>(active) : nullptr;
    if (!reload || reload->weaponAssetId != expectedWeaponId ||
        reload->plan.kind != RaidReloadKind::Normal ||
        !reload->plan.binding.oldMagazineAssetId ||
        installedMagazine(profile_, expectedWeaponId) !=
            reload->plan.binding.oldMagazineAssetId) return false;
    reload->plan.kind = RaidReloadKind::Emergency;
    reload->plan.oldReturn.reset();
    reload->durationSeconds = 1.2F / reload->handlingMultiplier;
    return true;
}

RaidDroppedMagazineLocation GameSession::raidMagazineDrop() const
{
    const Vec2 p = world_->player().position();
    const float half = world_->player().size() * 0.5F;
    return {profile_.pendingRaid->raidId, world_->activeRaidSpaceId(),
        {p.x + half, p.y + half}};
}

RaidReloadWorldAccess GameSession::raidReloadAccess(const RaidObservationKey &key) const
{
    const auto context=playerGameplayContext();
    RaidReloadWorldAccess access{context.spaceId, context.playerCenter,
        alphaRaidActive_ ? &world_->raidObservation() : basePlayerWorld_ ? &basePlayerWorld_->observation() : nullptr, &key};
    if (!profile_.pendingRaid) access.activity=context.activity;
    access.worldSize=context.worldSize;access.stashAccessible=context.stashAccessible;
    return access;
}

void GameSession::recordRaidReloadReceipt(const RaidReloadReceipt &receipt)
{
    if (!receipt.succeeded || receipt.alreadyCommitted) return;
    if (receipt.magazineRemoved)
        presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineRemoved);
    if (receipt.magazineInserted)
        presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineInserted);
    if (receipt.chamberedRound)
        presentationEvents_.push_back(GameSessionPresentationEvent::WeaponChambered);
    for (auto id : receipt.droppedMagazineAssetIds)
        if (std::find(droppedRaidMagazineIds_.begin(), droppedRaidMagazineIds_.end(), id) ==
            droppedRaidMagazineIds_.end()) droppedRaidMagazineIds_.push_back(id);
}

bool GameSession::cancelActiveRaidAction()
{
    const auto *active = raidActionState_.activeMutable();
    const auto *reload = active ? std::get_if<ReloadRaidAction>(active) : nullptr;
    if (reload && reload->nextStage != 0U && !reload->plan.binding.actionId.empty())
    {
        const auto context=playerGameplayContext();
        if (!context.available) return false;
        const auto key = alphaRaidActive_ ? world_->raidObservationKey() : basePlayerWorld_->observationKey();
        RaidReloadCancelCommand command{reload->plan.binding, reload->plan.newSource};
        if (profile_.pendingRaid) command.newFallbackDrop=command.oldFallbackDrop=raidMagazineDrop();
        else {
            const AssetLocation drop=developerRange_ ? AssetLocation{TrainingDroppedMagazineLocation{context.activity.activityId,context.playerCenter}}
                : AssetLocation{BaseGroundAssetLocation{context.activity.baseSiteDefinitionId,context.playerCenter}};
            command.newWorldFallbackDrop=command.oldWorldFallbackDrop=drop;
        }
        const auto receipt = executeRaidReloadCancel(profile_, publishedContentRegistry(), command,
            {profile_.revision, reload->plan.binding.actionId + "/cancel"}, raidReloadAccess(key));
        if (!receipt.succeeded)
        {
            persistenceMessage_ = receipt.message;
            return false;
        }
        recordRaidReloadReceipt(receipt);
    }
    raidActionState_.cancel();
    return true;
}

bool GameSession::advanceRaidWeaponAction(float deltaTime, bool interrupted)
{
    auto *active = raidActionState_.activeMutable();
    if (!active) return true;
    if (interrupted) return cancelActiveRaidAction();
    auto *reload = std::get_if<ReloadRaidAction>(active);
    if (!reload) return true;
    if(inventoryPersistencePending()){requestInventoryPersistenceBarrier();return true;}
    if (!std::isfinite(deltaTime) || deltaTime <= 0) return true;
    reload->elapsedSeconds = std::min(reload->durationSeconds,
        reload->elapsedSeconds + deltaTime);
    constexpr std::array<float, 5> normal{0.2F, 0.6F, 1.2F, 1.6F, 2.0F};
    constexpr std::array<float, 5> emergency{0.2F, 0.35F, 0.8F, 0.8F, 1.2F};
    while (reload->nextStage < normal.size())
    {
        const auto index = reload->nextStage;
        const bool urgent = reload->plan.kind == RaidReloadKind::Emergency;
        const float deadline = (urgent ? emergency[index] : normal[index]) /
            reload->handlingMultiplier;
        if (reload->elapsedSeconds + 0.000001F < deadline) break;
        const auto stage = static_cast<RaidReloadStage>(index);
        if ((stage == RaidReloadStage::RemoveOld && !reload->plan.binding.oldMagazineAssetId) ||
            (stage == RaidReloadStage::StowOld && (urgent || !reload->plan.binding.oldMagazineAssetId)))
        {
            ++reload->nextStage;
            continue;
        }
        const auto context=playerGameplayContext();
        const auto key = alphaRaidActive_ ? world_->raidObservationKey() : basePlayerWorld_->observationKey();
        RaidReloadStageCommand command{reload->plan.binding, stage, reload->plan.kind,
            reload->plan.source, reload->plan.newSource, reload->plan.oldReturn, std::nullopt};
        if (stage == RaidReloadStage::RemoveOld && urgent) {
            if (profile_.pendingRaid) command.drop=raidMagazineDrop();
            else command.worldDrop=developerRange_ ? AssetLocation{TrainingDroppedMagazineLocation{context.activity.activityId,context.playerCenter}}
                : AssetLocation{BaseGroundAssetLocation{context.activity.baseSiteDefinitionId,context.playerCenter}};
        }
        const auto receipt = executeRaidReloadStage(profile_, publishedContentRegistry(), command,
            {profile_.revision, reload->plan.binding.actionId + "/" + std::to_string(index)},
            raidReloadAccess(key));
        if (!receipt.succeeded)
        {
            persistenceMessage_ = receipt.message;
            return cancelActiveRaidAction();
        }
        recordRaidReloadReceipt(receipt);
        ++reload->nextStage;
    }
    if (reload->nextStage == normal.size())
    {
        raidActionState_.cancel();
        presentationEvents_.push_back(GameSessionPresentationEvent::ReloadCompleted);
    }
    return true;
}

std::vector<AssetInstanceId> GameSession::droppedRaidMagazineIds() const
{
    std::vector<AssetInstanceId> result;
    if (!world_ || (!profile_.pendingRaid && !developerRange_)) return result;
    for (auto id : droppedRaidMagazineIds_)
        if (const auto *asset = profile_.assets.find(id)) {
            if (const auto *drop=std::get_if<RaidDroppedMagazineLocation>(&asset->location);
                drop && profile_.pendingRaid && drop->raidId==profile_.pendingRaid->raidId && drop->spaceId==world_->activeRaidSpaceId())result.push_back(id);
            else if(const auto *drop=std::get_if<TrainingDroppedMagazineLocation>(&asset->location);
                drop && developerRange_ && drop->sessionId==profile_.profileId)result.push_back(id);
        }
    return result;
}

void GameSession::prepareWeaponTrigger(GameplayInput &simulation, const GameplayInput &input,
    std::optional<AssetInstanceId> weapon, bool available)
{
    const auto mode = weapon ? weaponFireMode(*weapon) : std::nullopt;
    unsigned count{};
    if (weapon && mode)
        if (const auto *asset = profile_.assets.find(*weapon))
            count = publishedContentRegistry().item(asset->definitionId).weaponUse->burstShotCount;
    const auto intent = weaponTriggerGate_.prepare(weapon.value_or(0),
        mode.value_or(WeaponFireMode::SemiAutomatic), count,
        input.firePressed || input.firePhysicallyPressed, input.fireJustPressed, available && mode.has_value());
    simulation.firePressed = intent.pressed;
    simulation.fireJustPressed = intent.justPressed;
}

bool GameSession::startAlphaLoadMagazine(
    AssetInstanceId ammunitionAssetId,
    AssetInstanceId magazineAssetId,
    std::uint32_t quantity)
{
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    const AssetRecord *ammunition = profile_.assets.find(ammunitionAssetId);
    const AssetRecord *magazine = profile_.assets.find(magazineAssetId);
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value() ||
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
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
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
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value())
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
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value())
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
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value())
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
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value() ||
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
            deriveWeaponHandling(effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId))
                    .switchDurationSeconds /
                handlingMultiplier});
    }
    catch (...)
    {
        return false;
    }
}

WeaponLightProjection GameSession::activeWeaponLight() const
{
    const auto weapon = activeAlphaWeapon().value_or(0);
    if (weaponLightBindingRevision_ != profile_.revision || weaponLightBinding_.weaponAssetId != weapon)
    {
        weaponLightBinding_ = findWeaponLight(profile_, publishedContentRegistry(), weapon);
        weaponLightBindingRevision_ = profile_.revision;
    }
    const bool switching = raidActionState_.active() && std::holds_alternative<WeaponSwitchRaidAction>(*raidActionState_.active());
    const bool allowed = playerGameplayContext().available && !playerGameplayContext().dead && !switching;
    return queryWeaponLight(profile_, publishedContentRegistry(), weaponLightBinding_, allowed);
}

bool GameSession::nightVisionAvailable() const noexcept
{
    return alphaRaidActive_ && !developerRange_ && state_ == GameSessionState::InRaid &&
        world_ && world_->raidSession().isActive() && world_->nightVisionSupported() &&
        !world_->player().isDead();
}

NightVisionBinding GameSession::equippedNightVision() const
{
    if (nightVisionBindingRevision_ != profile_.revision)
    {
        const auto helmet = equippedAsset(profile_, EquipmentSlotKind::Helmet).value_or(0);
        nightVisionBinding_ = findNightVision(profile_, publishedContentRegistry(), helmet);
        nightVisionBindingRevision_ = profile_.revision;
    }
    return nightVisionBinding_;
}

NightVisionProjection GameSession::activeNightVision() const
{
    if (!nightVisionAvailable()) return {};
    const auto binding = equippedNightVision();
    if (!binding.deviceAssetId) return {};
    return queryNightVision(profile_, publishedContentRegistry(), binding,
        world_->isDarkForNightVision());
}

void GameSession::synchronizeNightVision(float seconds)
{
    if (!nightVisionAvailable()) return;
    const auto binding = equippedNightVision();
    const auto &content = publishedContentRegistry();
    if (binding.deviceAssetId && seconds > 0 &&
        world_->isDarkForNightVision())
        advanceNightVision(profile_, content, binding, true, seconds);

    // Retain the supplied power state through the movement update, so a portal
    // can immediately choose the new room's light without another fan build.
    const auto powered = binding.deviceAssetId
        ? queryNightVision(profile_, content, binding, true) : NightVisionProjection{};
    if (powered.output)
        world_->setNightVision({powered.vision.outdoorAngleDegrees, powered.vision.outdoorDistance},
                               {powered.vision.indoorAngleDegrees, powered.vision.indoorDistance});
    else
        world_->setNightVision({}, {});
}

void GameSession::synchronizeWeaponLight(float seconds)
{
    if (!playerGameplayContext().available) return;
    auto projection = activeWeaponLight();
    const bool switching = raidActionState_.active() && std::holds_alternative<WeaponSwitchRaidAction>(*raidActionState_.active());
    const bool allowed = playerGameplayContext().available && !playerGameplayContext().dead && !switching;
    advanceWeaponLight(profile_, publishedContentRegistry(), projection.binding, allowed, seconds);
    projection = activeWeaponLight();
    const auto setLight = [&](auto &consumer) { consumer.setWeaponLight(projection.output ? projection.binding.deviceAssetId : 0,
                           projection.output ? projection.binding.batteryAssetId : 0,
                           projection.output ? projection.light.angleDegrees : 0,
                           projection.output ? projection.light.maximumDistance : 0,
                           projection.output ? projection.light.backgroundRadius : 0); };
    if(alphaRaidActive_) setLight(*world_); else if(basePlayerWorld_) setLight(*basePlayerWorld_);
}

InventoryReceipt GameSession::toggleWeaponLight()
{
    const auto light = activeWeaponLight();
    if (!playerGameplayContext().available || !light.binding.deviceAssetId)
        return {false, false, DomainErrorCode::IllegalDestination, "NO ACTIVE WEAPON LIGHT", profile_.revision};
    auto candidate = profile_;
    auto receipt = executeDeviceSwitch(candidate, publishedContentRegistry(), light.binding.deviceAssetId,
                                      {candidate.revision, nextRaidTransaction("weapon-light")});
    if (receipt.succeeded && !commitProfileCandidate(std::move(candidate), false))
        return {false, false, DomainErrorCode::InvalidProfile, persistenceMessage_, profile_.revision};
    synchronizeWeaponLight();
    if (receipt.succeeded) persistenceMessage_ = activeWeaponLight().switchOn ? "WEAPON LIGHT INTENT ON" : "WEAPON LIGHT OFF";
    return receipt;
}

InventoryReceipt GameSession::toggleNightVision()
{
    const auto binding = nightVisionAvailable() ? equippedNightVision() : NightVisionBinding{};
    if (!binding.deviceAssetId)
        return {false, false, DomainErrorCode::IllegalDestination, "NO EQUIPPED NIGHT VISION", profile_.revision};

    auto candidate = profile_;
    auto receipt = executeDeviceSwitch(candidate, publishedContentRegistry(), binding.deviceAssetId,
                                      {candidate.revision, nextRaidTransaction("night-vision")});
    if (receipt.succeeded && !commitProfileCandidate(std::move(candidate), false))
        return {false, false, DomainErrorCode::InvalidProfile, persistenceMessage_, profile_.revision};
    if (receipt.succeeded)
    {
        synchronizeNightVision();
        persistenceMessage_ = activeNightVision().switchOn ? "NIGHT VISION ON" : "NIGHT VISION OFF";
    }
    return receipt;
}

InventoryReceipt GameSession::selectTacticalDevice(TacticalDeviceKind kind)
{
    const bool open = tacticalDeviceWheelOpen_;
    cancelWeaponLightInput();
    if (!open || !nightVisionAvailable() || profileUiCommitPending())
        return {false, false, DomainErrorCode::IllegalDestination, "TACTICAL DEVICE SELECTION UNAVAILABLE", profile_.revision};

    auto receipt = kind == TacticalDeviceKind::NightVision ? toggleNightVision() : toggleWeaponLight();
    if (receipt.succeeded) selectedTacticalDevice_ = kind;
    return receipt;
}

InventoryReceipt GameSession::changeDeviceBattery(DeviceBatteryCommand command, std::string transactionId)
{
    if(profileUiCommitPending())return {false,false,DomainErrorCode::IllegalDestination,"PROFILE SAVE PENDING",profile_.revision};
    const auto context=playerGameplayContext();
    if ((alphaRaidActive_ && !context.available) || (!context.available && (profile_.pendingRaid || profile_.activeBaseDefense)))
        return {false, false, DomainErrorCode::IllegalDestination, "BATTERY CHANGE UNAVAILABLE", profile_.revision};
    const bool raid=context.available && context.activity.activity==WeaponActionActivity::Raid;
    DeviceBatteryAccess access{raid};
    if(context.available) {
        access.activity=context.activity;
        access.stashAccessible=context.activity.activity==WeaponActionActivity::BaseDaily && context.stashAccessible;
    }
    if (raid && profile_.pendingRaid)
    {
        const auto p = world_->player().position(); const auto half = world_->player().size() * 0.5F;
        access.overflow = RaidDroppedBatteryLocation{profile_.pendingRaid->raidId, world_->activeRaidSpaceId(), {p.x+half,p.y+half}};
    }
    auto candidate = profile_;
    auto receipt = executeDeviceBatteryChange(candidate, publishedContentRegistry(), command, access,
                                             {candidate.revision, std::move(transactionId)});
    if (!receipt.succeeded) { persistenceMessage_=receipt.message; return receipt; }
    if (!commitProfileCandidate(std::move(candidate), !context.available))
        return {false, false, DomainErrorCode::InvalidProfile, persistenceMessage_, profile_.revision};
    if(context.available)worldClockDirty_=true;
    synchronizeWeaponLight();
    synchronizeNightVision();
    return receipt;
}

bool GameSession::observeAlphaWeaponClearMotion(Vec2 delta)
{
    if(profileUiCommitPending())return false;
    const bool inBase = !alphaRaidActive_ &&
        !profile_.pendingRaid.has_value() &&
        state_ == GameSessionState::BetweenRaids;
    if ((!alphaRaidActive_ && !inBase) ||
        (alphaRaidActive_ && raidActionState_.active().has_value()))
    {
        weaponClearGesture_.reset();
        return false;
    }
    const auto weapon = activeAlphaWeapon();
    const AssetRecord *record = weapon.has_value()
        ? profile_.assets.find(*weapon)
        : nullptr;
    const auto light = activeWeaponLight();
    const bool lampFault = playerGameplayContext().available && light.switchOn && light.interrupted && light.remainingMs;
    const bool weaponFault = record && record->weaponMalfunction != WeaponMalfunctionType::None;
    if (!weaponFault && !lampFault) { weaponClearGesture_.reset(); return false; }
    const float elapsedSeconds = alphaRaidActive_
        ? raidElapsedSeconds_ : baseCombatElapsedSeconds_;
    if (!weaponClearGesture_.observe(delta, elapsedSeconds))
    {
        return false;
    }
    ProfileState candidate = profile_;
    const bool lampRestored = lampFault && restoreWeaponLight(candidate, publishedContentRegistry(), light.binding);
    bool gunRestored = false;
    if (weaponFault)
        gunRestored = executeWeaponAmmo(candidate, publishedContentRegistry(), ClearWeaponMalfunctionCommand{*weapon},
            {candidate.revision, nextRaidTransaction("clear-malfunction")}).succeeded;
    if (lampRestored && !gunRestored)
    {
        if (candidate.revision == std::numeric_limits<ProfileRevision>::max()) return false;
        ++candidate.revision;
    }
    weaponClearGesture_.reset();
    const bool cleared = (lampRestored || gunRestored) && commitProfileCandidate(std::move(candidate), false);
    if (cleared) synchronizeWeaponLight();
    if (cleared)
    {
        if (inBase)
        {
            worldClockDirty_ = true;
        }
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MalfunctionCleared);
    }
    return cleared;
}

bool GameSession::startAlphaUnloadMagazine(
    AssetInstanceId magazineAssetId)
{
    if (playerThrowFlightActive() || profileUiCommitPending() || inventoryPersistencePending()) return false;
    if (!playerGameplayContext().available || playerGameplayContext().dead || playerGameplayContext().controlled || raidActionState_.active().has_value())
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
    if (!alphaRaidActive_ &&
        (profile_.pendingRaid.has_value() ||
         state_ != GameSessionState::BetweenRaids))
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
            effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId),
            deriveWeaponHandling(effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId)),
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
    if(profileUiCommitPending())return false;
    const bool activeRaid = alphaRaidActive_ && world_ != nullptr &&
        world_->raidSession().isActive();
    const bool activeBase = !alphaRaidActive_ &&
        !profile_.pendingRaid.has_value() &&
        state_ == GameSessionState::BetweenRaids;
    if ((!activeRaid && !activeBase) ||
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
            effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId),
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

    if (activeRaid)
    {
        auto use = entry.weaponUse;
        use.optic = effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId).optic;
        world_->configureWeaponFire(
            use, effectiveDeveloperHandling(entry), true);
        configuredWeaponAssetId_ = *weapon;
        configuredBaseWeaponAssetId_.reset();
    }
    else
    {
        configuredBaseWeaponAssetId_.reset();
    }
    return true;
}

bool GameSession::resetDeveloperWeaponTuning()
{
    if(profileUiCommitPending())return false;
    const bool activeRaid = alphaRaidActive_ && world_ != nullptr &&
        world_->raidSession().isActive();
    const bool activeBase = !alphaRaidActive_ &&
        !profile_.pendingRaid.has_value() &&
        state_ == GameSessionState::BetweenRaids;
    if (!activeRaid && !activeBase)
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
        if (activeRaid)
        {
            world_->configureWeaponFire(
                effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId),
                deriveWeaponHandling(effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId)),
                true);
            configuredWeaponAssetId_ = *weapon;
            configuredBaseWeaponAssetId_.reset();
        }
        else
        {
            configuredBaseWeaponAssetId_.reset();
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

RaidSceneContainerAccess GameSession::raidSceneAccess(AssetInstanceId id) const
{
    const auto p=world_->player().position();
    const float half=world_->player().size()*0.5F;
    RaidSceneContainerAccess access{id,world_->activeRaidSpaceId(),{p.x+half,p.y+half},std::nullopt};
    access.observation=world_->raidObservation();
    access.currentObservation=world_->raidObservationKey();
    return access;
}

bool GameSession::raidSceneContainerAvailable(AssetInstanceId id) const
{
    if (!alphaRaidActive_ || !world_ || !world_->raidSession().isActive() || world_->player().isDead() || world_->player().isControlled()) return false;
    return raidSceneContainerAccessible(profile_, raidSceneAccess(id));
}

bool GameSession::canOpenRaidSceneContainer(AssetInstanceId id) const
{
    return raidSceneContainerAvailable(id) && raidWorldInteractionAvailable(false) && !world_->raidSpacePortalInteractionInRange();
}

std::optional<AssetInstanceId> GameSession::nearbyRaidSceneContainer() const
{
    const auto target = nearestObservableRaidSceneContainer();
    return target && !raidSceneContainerLocked(profile_, *target) ? target : std::nullopt;
}

std::optional<AssetInstanceId> GameSession::nearestObservableRaidSceneContainer() const
{
    std::optional<AssetInstanceId> result;
    float closest = kRaidContainerReach * kRaidContainerReach;
    if (!alphaRaidActive_ || !world_ || !world_->raidSession().isActive() ||
        world_->player().isDead() || world_->player().isControlled() ||
        !raidWorldInteractionAvailable(false) || world_->raidSpacePortalInteractionInRange())
        return result;

    const auto p = world_->player().position();
    const float half = world_->player().size() * 0.5F;
    for (const auto &[id, asset] : profile_.assets.records())
    {
        Vec2 position{};
        if (const auto *scene = std::get_if<RaidSceneContainerAssetLocation>(&asset.location))
            position = scene->position;
        else if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location);
                 corpse && corpse->deathPosition)
            position = *corpse->deathPosition;
        else
            continue;
        const float dx = position.x - p.x - half, dy = position.y - p.y - half;
        const float distance = dx * dx + dy * dy;
        if (distance <= closest && raidSceneContainerObservable(profile_, raidSceneAccess(id)))
        {
            closest = distance;
            result = id;
        }
    }
    return result;
}

bool GameSession::isRaidFreightCrate(AssetInstanceId id) const noexcept
{
    return profile_.pendingRaid && profile_.pendingRaid->sealedFreightCrate &&
        profile_.pendingRaid->sealedFreightCrate->containerAssetId == id;
}

std::optional<AssetInstanceId> GameSession::nearbyLockedFreightCrate() const
{
    if (state_ != GameSessionState::InRaid || !profile_.pendingRaid ||
        !profile_.pendingRaid->sealedFreightCrate || !world_)
        return std::nullopt;
    const auto &crate = *profile_.pendingRaid->sealedFreightCrate;
    if (crate.unlocked) return std::nullopt;

    // The frozen root identifies the sole new interactable. Check its room
    // and distance before any scene scan or observation work.
    const auto *asset = profile_.assets.find(crate.containerAssetId);
    const auto *scene = asset ? std::get_if<RaidSceneContainerAssetLocation>(&asset->location) : nullptr;
    if (!scene || scene->spaceId != world_->activeRaidSpaceId()) return std::nullopt;
    const auto position = world_->player().position();
    const float half = world_->player().size() * 0.5F;
    const float dx = scene->position.x - position.x - half;
    const float dy = scene->position.y - position.y - half;
    if (dx * dx + dy * dy > kRaidContainerReach * kRaidContainerReach) return std::nullopt;

    const auto nearest = nearestObservableRaidSceneContainer();
    return nearest == crate.containerAssetId ? nearest : std::nullopt;
}

std::optional<RaidFreightUnlockProjection> GameSession::raidFreightUnlock() const
{
    const auto target = nearbyLockedFreightCrate();
    if (!target) return std::nullopt;
    const float progress = freightUnlock_.target == target
        ? freightUnlock_.elapsedSeconds / kFreightUnlockSeconds : 0.0F;
    return RaidFreightUnlockProjection{*target, raidFreightCrateKeyCarried(profile_), progress};
}

std::optional<AssetInstanceId> GameSession::takeOpenedRaidFreightCrate() noexcept
{
    return std::exchange(freightUnlock_.opened, std::nullopt);
}

void GameSession::cancelRaidFreightUnlock() noexcept
{
    freightUnlock_.target.reset();
    freightUnlock_.opened.reset();
    freightUnlock_.elapsedSeconds = 0.0F;
    // Once this gesture chose the lock, it cannot become a different world
    // interaction simply because a modal or loss of sight interrupted it.
}

void GameSession::prepareRaidFreightUnlock(
    const GameplayInput &rawInput, GameplayInput &input, GameplayInput &simulationInput)
{
    if ((!profile_.pendingRaid || !profile_.pendingRaid->sealedFreightCrate) &&
        !freightUnlock_.consumesInteraction)
        return;

    const bool modal = input.inventoryOpen || input.freezeObservationDirection || input.throwableModal;
    if (rawInput.interactJustPressed || (!rawInput.interactPressed && !modal))
    {
        cancelRaidFreightUnlock();
        freightUnlock_.consumesInteraction = false;
    }

    const bool conflict = modal || input.fireJustPressed || input.firePressed ||
        input.meleeJustPressed || input.meleePressed || input.reloadJustPressed ||
        input.reloadGestureJustPressed || !input.raidReloadIntents.empty() ||
        input.healJustPressed || input.weaponSlotJustPressed.has_value() ||
        world_->player().isControlled();
    if (!freightUnlock_.consumesInteraction && simulationInput.interactJustPressed && !modal)
    {
        if (const auto target = nearbyLockedFreightCrate())
        {
            freightUnlock_.consumesInteraction = true;
            if (!raidFreightCrateKeyCarried(profile_))
                persistenceMessage_ = "FREIGHT CRATE REQUIRES KEY";
            else if (simulationInput.interactPressed && !conflict)
                freightUnlock_.target = target;
        }
    }

    if (!freightUnlock_.consumesInteraction) return;
    if (!simulationInput.interactPressed || conflict) cancelRaidFreightUnlock();
    input.interactJustPressed = input.interactPressed = false;
    simulationInput.interactJustPressed = simulationInput.interactPressed = false;
    simulationInput.credentialInvestigationAllowed = false;
}

void GameSession::advanceRaidFreightUnlock(float deltaTime)
{
    if (!freightUnlock_.target) return;
    if (world_->spaceTransitionedLastUpdate() || nearbyLockedFreightCrate() != freightUnlock_.target)
    {
        cancelRaidFreightUnlock();
        return;
    }
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F) return;
    freightUnlock_.elapsedSeconds += deltaTime;
    if (freightUnlock_.elapsedSeconds < kFreightUnlockSeconds) return;

    const AssetInstanceId target = *freightUnlock_.target;
    cancelRaidFreightUnlock();
    const auto receipt = unlockRaidFreightCrate(profile_, raidSceneAccess(target),
        {profile_.revision, nextRaidTransaction("unlock-freight-crate")});
    if (receipt.succeeded) freightUnlock_.opened = target;
    else persistenceMessage_ = receipt.message;
}

bool GameSession::freightBreachToolCarried(AssetInstanceId id) const noexcept
{
    const auto *asset = profile_.assets.find(id);
    return asset && asset->definitionId == kFreightBreachWedgesId &&
        asset->quantity == 1U && std::holds_alternative<StoredAssetLocation>(asset->location) &&
        assetIsCarried(profile_, id);
}

std::optional<AssetInstanceId> GameSession::carriedFreightBreachTool() const
{
    const auto &assets = profile_.assets.records();
    const auto found = std::find_if(assets.begin(), assets.end(), [&](const auto &entry) {
        return entry.second.definitionId == kFreightBreachWedgesId &&
            freightBreachToolCarried(entry.first);
    });
    if (found == assets.end())
        return std::nullopt;
    return found->first;
}

std::optional<RaidFreightBreachProjection> GameSession::raidFreightBreach() const
{
    if (state_ != GameSessionState::InRaid || !profile_.pendingRaid ||
        !profile_.pendingRaid->freightBreachEnabled || !world_->nearbyFreightBreachDoor())
        return std::nullopt;
    const bool holding = freightBreach_.target == RaidBreachTarget::FreightDoor &&
        freightBreach_.tool && freightBreachToolCarried(*freightBreach_.tool);
    return RaidFreightBreachProjection{
        holding || carriedFreightBreachTool().has_value(),
        holding ? freightBreach_.elapsedSeconds / kFreightBreachSeconds : 0.0F};
}

bool GameSession::roadBlockadeBreachAvailable() const
{
    const auto blockade = world_->roadBlockade();
    return blockade && blockade->canInteract && raidWorldInteractionAvailable(false) &&
        world_->raidSession().state() == RaidSessionState::InRaid &&
        !world_->raidSpacePortalInteractionInRange() &&
        !canCallSignalExtraction() && !world_->credentialInvestigationInRange() &&
        !world_->highRiskControlInteractionInRange() &&
        !world_->ordinarySurvivorRescueInteractionInRange();
}

std::optional<RaidFreightBreachProjection> GameSession::raidRoadBlockadeBreach() const
{
    if (state_ != GameSessionState::InRaid || !roadBlockadeBreachAvailable())
        return std::nullopt;
    const bool holding = freightBreach_.target == RaidBreachTarget::RoadBlockade &&
        freightBreach_.tool && freightBreachToolCarried(*freightBreach_.tool);
    if (!holding && (nearbyRaidLoot() || nearbyRaidSceneContainer()))
        return std::nullopt;
    return RaidFreightBreachProjection{
        holding || carriedFreightBreachTool().has_value(),
        holding ? freightBreach_.elapsedSeconds / kRoadBlockadeBreachSeconds : 0.0F};
}

void GameSession::cancelRaidFreightBreach() noexcept
{
    freightBreach_.tool.reset();
    freightBreach_.elapsedSeconds = 0.0F;
    freightBreach_.startingHealth = 0;
}

void GameSession::prepareRaidFreightBreach(
    const GameplayInput &rawInput, GameplayInput &input, GameplayInput &simulationInput)
{
    if (!profile_.pendingRaid || (!profile_.pendingRaid->freightBreachEnabled &&
        !(profile_.pendingRaid->highRiskCrisis && profile_.pendingRaid->highRiskCrisis->roadBlockade)))
        return;
    const bool modal = input.inventoryOpen || input.freezeObservationDirection || input.throwableModal;
    if (freightBreach_.consumesInteraction &&
        (rawInput.interactJustPressed || (!rawInput.interactPressed && !modal)))
    {
        cancelRaidFreightBreach();
        freightBreach_.consumesInteraction = false;
    }
    if (!freightBreach_.consumesInteraction && !simulationInput.interactJustPressed)
        return;
    const bool conflict = modal || !raidWorldInteractionAvailable(input.inventoryOpen) ||
        rawInput.fireJustPressed || rawInput.firePressed || simulationInput.fireJustPressed ||
        simulationInput.firePressed || rawInput.meleeJustPressed || rawInput.meleePressed ||
        rawInput.reloadJustPressed || rawInput.reloadGestureJustPressed ||
        !rawInput.raidReloadIntents.empty() || rawInput.healJustPressed ||
        rawInput.weaponSlotJustPressed.has_value() || playerThrowFlightActive() ||
        world_->player().isControlled() || world_->player().isDead();
    if (!freightBreach_.consumesInteraction && !conflict && simulationInput.interactJustPressed)
    {
        const bool atFreightDoor = profile_.pendingRaid->freightBreachEnabled &&
            world_->nearbyFreightBreachDoor().has_value();
        const bool atRoadBlockade = !atFreightDoor && roadBlockadeBreachAvailable() &&
            !nearbyRaidLoot() && !nearbyRaidSceneContainer();
        if (atFreightDoor || atRoadBlockade)
        {
            if (const auto tool = carriedFreightBreachTool())
            {
                freightBreach_.consumesInteraction = true;
                if (simulationInput.interactPressed)
                {
                    freightBreach_.tool = tool;
                    freightBreach_.startingHealth = world_->player().health();
                    freightBreach_.target = atRoadBlockade
                        ? RaidBreachTarget::RoadBlockade : RaidBreachTarget::FreightDoor;
                }
            }
            else if (atRoadBlockade || !nearbyRaidLoot())
            {
                freightBreach_.consumesInteraction = true;
                persistenceMessage_ = atRoadBlockade
                    ? "ROAD BREACH REQUIRES WEDGES" : "FREIGHT BREACH REQUIRES WEDGES";
            }
        }
    }
    if (!freightBreach_.consumesInteraction)
        return;
    if (!simulationInput.interactPressed || conflict)
        cancelRaidFreightBreach();
    // An interrupted hold never becomes a new world interaction.
    input.interactJustPressed = input.interactPressed = false;
    simulationInput.interactJustPressed = simulationInput.interactPressed = false;
    simulationInput.credentialInvestigationAllowed = false;
}

void GameSession::advanceRaidFreightBreach(float deltaTime)
{
    if (!freightBreach_.tool)
        return;
    const bool roadBlockade = freightBreach_.target == RaidBreachTarget::RoadBlockade;
    const bool targetAvailable = roadBlockade
        ? roadBlockadeBreachAvailable()
        : world_->nearbyFreightBreachDoor().has_value();
    if (world_->spaceTransitionedLastUpdate() || world_->player().isControlled() ||
        world_->player().health() < freightBreach_.startingHealth ||
        (lastIncomingDamage_ && lastIncomingDamage_->damageApplied > 0) ||
        !raidWorldInteractionAvailable(false) || !targetAvailable ||
        !freightBreachToolCarried(*freightBreach_.tool))
    {
        cancelRaidFreightBreach();
        return;
    }
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
        return;
    freightBreach_.elapsedSeconds += deltaTime;
    if (freightBreach_.elapsedSeconds < (roadBlockade ? kRoadBlockadeBreachSeconds : kFreightBreachSeconds))
        return;

    const AssetInstanceId tool = *freightBreach_.tool;
    cancelRaidFreightBreach();
    if (inventoryPersistencePending())
    {
        requestInventoryPersistenceBarrier();
        persistenceMessage_ = "INVENTORY SAVE PENDING";
        return;
    }
    if (profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        persistenceMessage_ = "REVISION LIMIT";
        return;
    }
    const auto transaction = nextRaidTransaction(roadBlockade ? "breach-road-blockade" : "breach-freight-door");
    if (transaction.empty())
        return;
    const auto commitWedge = [&] {
        if (!freightBreachToolCarried(tool))
            return false;
        // A wedge is a single childless carried asset. The journal allocation
        // and world preparation precede the nonthrowing removal and switch.
        const auto [entry, inserted] = profile_.committedTransactions.insert(transaction);
        if (!inserted)
            return false;
        if (!profile_.assets.erase(tool))
        {
            profile_.committedTransactions.erase(entry);
            return false;
        }
        ++profile_.revision;
        return true;
    };
    const bool opened = roadBlockade ? world_->tryOpenRoadBlockade(commitWedge)
                                    : world_->tryOpenFreightBreachDoor(commitWedge);
    if (opened)
        persistenceMessage_ = roadBlockade ? "ROAD BREACH OPENED" : "FREIGHT BREACH OPENED";
}

InventoryPlan GameSession::queryRaidSceneContainerInventory(AssetInstanceId id, const InventoryCommand &command) const
{
    if (!raidSceneContainerAvailable(id)) return {false, DomainErrorCode::IllegalDestination, "Scene container is no longer accessible", profile_.revision};
    const RaidSceneContainerAccess access=raidSceneAccess(id);
    return queryInventory(profile_, publishedContentRegistry(), command, &access);
}

InventoryReceipt GameSession::executeRaidSceneContainerInventory(AssetInstanceId id, const InventoryCommand &command, std::string transactionId)
{
    if (!raidSceneContainerAvailable(id)) return {false, false, DomainErrorCode::IllegalDestination, "Scene container is no longer accessible", profile_.revision};
    const RaidSceneContainerAccess access=raidSceneAccess(id);
    return executeInventory(profile_, publishedContentRegistry(), command,
        {profile_.revision, std::move(transactionId)}, &access);
}

InventoryReceipt GameSession::executeProfileInventory(
    const InventoryCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_)
    {
        return executeInventory(
            profile_,
            publishedContentRegistry(),
            command,
            CommandContext{profile_.revision, std::move(transactionId)});
    }

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
    if (!commitProfileCandidate(std::move(candidate)))
        return InventoryReceipt{false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision};
    if (!alphaRaidActive_)
    {
        refreshLoadoutTutorial();
    }
    return receipt;
}

BaseGroundReceipt GameSession::executeBaseGroundAsset(
    const BaseGroundCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground assets cannot be changed during a Raid",
            profile_.revision, 0};
    }
    ProfileState candidate = profile_;
    BaseGroundReceipt receipt = ::executeBaseGround(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BaseGroundReceipt{
            false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision, 0};
    }
    return receipt;
}

BaseFacilityLayoutReceipt GameSession::executeBaseFacilityLayout(
    const RepositionBaseFacilityCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        return BaseFacilityLayoutReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base facilities cannot be moved during a Raid",
            profile_.revision, command.facilityDefinitionId};
    }
    ProfileState candidate = profile_;
    BaseFacilityLayoutReceipt receipt = ::executeBaseFacilityLayout(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BaseFacilityLayoutReceipt{
            false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision,
            command.facilityDefinitionId};
    }
    return receipt;
}

BaseFacilityLayoutReceipt GameSession::executeInstallBaseFacilityAt(
    const InstallBaseFacilityAtCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        return BaseFacilityLayoutReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base facilities cannot be installed during a Raid",
            profile_.revision, command.facilityDefinitionId};
    }
    ProfileState candidate = profile_;
    BaseFacilityLayoutReceipt receipt = ::executeInstallBaseFacilityAt(
        candidate,
        publishedContentRegistry(),
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return BaseFacilityLayoutReceipt{
            false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision,
            command.facilityDefinitionId};
    }
    return receipt;
}

InventoryReceipt GameSession::executeBaseGroundContainerInventory(
    AssetInstanceId containerAssetId,
    const BaseGroundAccess &access,
    const InventoryCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        return InventoryReceipt{
            false, false, DomainErrorCode::IllegalDestination,
            "Base ground containers cannot be changed during a Raid",
            profile_.revision};
    }
    ProfileState candidate = profile_;
    InventoryReceipt receipt = ::executeBaseGroundContainerInventory(
        candidate,
        publishedContentRegistry(),
        containerAssetId,
        access,
        command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded)
        return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        return InventoryReceipt{
            false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision};
    }
    refreshLoadoutTutorial();
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
    if (!commitProfileCandidate(std::move(candidate)))
        return EconomyReceipt{false, false, DomainErrorCode::InvalidProfile,
            persistenceMessage_, profile_.revision, 0};
    return receipt;
}

GameSession::BasePlaceablePurchaseReceipt
GameSession::purchaseBasePlaceable(
    ItemDefinitionId definitionId,
    std::string transactionId)
{
    const ItemDefinition &definition =
        publishedContentRegistry().item(definitionId);
    if (!definition.basePlacement.has_value() ||
        definition.marketBuyPrice == 0U || definition.maxStackSize != 1U)
    {
        return BasePlaceablePurchaseReceipt{
            false,
            DomainErrorCode::IllegalDestination,
            "definition is not available in the Base build catalog",
            profile_.revision,
            0,
            0};
    }

    const AssetInstanceId createdId = profile_.assets.nextAssetId();
    const EconomyReceipt economy = executeProfileEconomy(
        PurchaseCommand{definitionId, 1U}, std::move(transactionId));
    if (!economy.succeeded)
    {
        return BasePlaceablePurchaseReceipt{
            false,
            economy.error,
            economy.message,
            economy.revision,
            0,
            economy.currencyDelta};
    }
    const AssetRecord *created = profile_.assets.find(createdId);
    if (created == nullptr || created->definitionId != definitionId)
    {
        // executeProfileEconomy is synchronous and a quantity-one placeable
        // must create exactly this stable ID. Treat any contract violation as
        // a hard service error instead of making the App infer another asset.
        return BasePlaceablePurchaseReceipt{
            false,
            DomainErrorCode::InvalidProfile,
            "Base placeable purchase did not create the expected asset",
            profile_.revision,
            0,
            economy.currencyDelta};
    }
    return BasePlaceablePurchaseReceipt{
        true,
        DomainErrorCode::None,
        {},
        profile_.revision,
        createdId,
        economy.currencyDelta};
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
    if(!preparePlayerActivityBoundary())return {false,false,DomainErrorCode::IllegalDestination,persistenceMessage_,profile_.revision};
    ProfileState candidate = profile_;
    candidate.basePlayerEffects.reset();
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

BasePriorityReceipt GameSession::executeBaseWishFocus(
    std::optional<BaseWishInstanceId> focus, std::string transactionId)
{
    ProfileState candidate = profile_;
    auto receipt = ::executeBaseWishFocus(candidate, publishedContentRegistry(),
        focus, CommandContext{profile_.revision, std::move(transactionId)});
    if (receipt.succeeded && !receipt.alreadyCommitted &&
        !commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

BasePriorityReceipt GameSession::executeBasePrioritySubmission(
    BasePriorityDefinitionId priorityDefinitionId,
    std::vector<AssetInstanceId> assetIds,
    std::string transactionId)
{
    ProfileState candidate = profile_;
    BasePriorityReceipt receipt = ::executeBasePrioritySubmission(
        candidate,
        publishedContentRegistry(),
        SubmitBasePriorityCommand{
            std::move(priorityDefinitionId), std::move(assetIds)},
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

WeaponAmmoReceipt GameSession::executeProfileWeaponAmmo(
    const WeaponAmmoCommand &command,
    std::string transactionId)
{
    if (alphaRaidActive_ && playerThrowFlightActive())
        return {false,false,DomainErrorCode::IllegalDestination,
            world_->thrownIllumination().flight() ? "THROWABLE UNAVAILABLE HERE" : "DECOY UNAVAILABLE HERE",
            profile_.revision,WeaponAmmoResult::Dry,std::nullopt};
    const auto weaponId = std::visit([](const auto &typed) -> std::optional<AssetInstanceId> {
        if constexpr (requires { typed.weaponAssetId; }) return typed.weaponAssetId;
        else return std::nullopt;
    }, command);
    const auto oldMagazine = weaponId ? installedMagazine(profile_, *weaponId) : std::nullopt;
    const auto *oldWeapon = weaponId ? profile_.assets.find(*weaponId) : nullptr;
    const bool oldChambered = oldWeapon && oldWeapon->chamberedRound.has_value();
    const auto oldMode = oldWeapon ? oldWeapon->fireMode : std::nullopt;
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
            !playerGameplayContext().available))
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
    if(playerGameplayContext().available && !alphaRaidActive_)worldClockDirty_=true;
    if (receipt.alreadyCommitted) return receipt;
    if (weaponId)
    {
        const auto newMagazine = installedMagazine(profile_, *weaponId);
        if (oldMagazine != newMagazine)
        {
            if (oldMagazine) presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineRemoved);
            if (newMagazine) presentationEvents_.push_back(GameSessionPresentationEvent::WeaponMagazineInserted);
        }
        const auto *now = profile_.assets.find(*weaponId);
        if (!oldChambered && now && now->chamberedRound)
            presentationEvents_.push_back(GameSessionPresentationEvent::WeaponChambered);
    }
    switch (receipt.result)
    {
    case WeaponAmmoResult::Loaded:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MagazineLoaded);
        break;
    case WeaponAmmoResult::Unloaded:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::MagazineUnloaded);
        break;
    case WeaponAmmoResult::Uninstalled:
        break;
    case WeaponAmmoResult::Installed:
    case WeaponAmmoResult::InstalledAndChambered:
        presentationEvents_.push_back(
            GameSessionPresentationEvent::ReloadCompleted);
        break;
    case WeaponAmmoResult::Chambered:
        break;
    case WeaponAmmoResult::FireModeChanged:
        if (weaponId == activeAlphaWeapon() && oldMode != weaponFireMode(*weaponId))
            weaponTriggerGate_.cancelUntilRelease();
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
    recordBaseCompletionEvents(
        receipt.construction,
        receipt.manufacturing,
        receipt.residentTreatment,
        false);
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

InventoryReceipt GameSession::executeBaseWeaponComponentChange(const WeaponComponentCommand &command, std::string transactionId)
{
    ProfileState candidate = profile_;
    auto receipt = executeWeaponComponentChange(candidate, publishedContentRegistry(), command,
        CommandContext{profile_.revision, std::move(transactionId)});
    if (!receipt.succeeded) return receipt;
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    else
    {
        configuredWeaponAssetId_.reset();
        configuredBaseWeaponAssetId_.reset();
        // Developer tuning must not mask a newly committed player modification.
        developerWeaponOverrides_.clear();
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

void GameSession::advanceBaseWorldClock(float deltaTime, bool allowAutomaticAdmission)
{
    if(profileUiCommitPending())return;
    // The safe, pre-base survey has no operating Base or catch-up debt.
    // This is a pause of the existing clock, not a second tutorial clock.
    if (!profile_.homeFounding.established) return;
    // During real-time defense the complete clock/combat checkpoint advances
    // together in updateBaseWorld, including while an inventory is open.
    if (baseDefenseActive()) return;
    if (alphaRaidActive_ || profile_.pendingRaid.has_value() ||
        state_ != GameSessionState::BetweenRaids)
    {
        return;
    }
    advanceBaseSiegeFromSimulation(deltaTime);
    advanceWorldClockFromSimulation(deltaTime, true, allowAutomaticAdmission);
}

WarehouseCatalogGrantReceipt GameSession::grantDeveloperWarehouseCatalog()
{
    ProfileState candidate = profile_;
    WarehouseCatalogGrantReceipt receipt =
        ::grantDeveloperWarehouseCatalog(
            candidate,
            publishedContentRegistry());
    if (!receipt.succeeded || receipt.alreadyGranted)
    {
        return receipt;
    }
    if (!commitProfileCandidate(std::move(candidate)))
    {
        receipt.succeeded = false;
        receipt.message = persistenceMessage_;
        receipt.revision = profile_.revision;
    }
    return receipt;
}

bool GameSession::developerWarehouseCatalogGranted() const noexcept
{
    return profile_.committedTransactions.contains(
        "developer.warehouse_catalog.content_63");
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
    if(profileUiCommitPending())return baseDefenseActive()?checkpointBaseDefense(true):checkpointBaseDaily(true);
    if (!alphaRaidActive_ && !preparePlayerActivityBoundary(false)) return false;
    if (baseDefenseActive()) return checkpointBaseDefense(true);
    if (alphaRaidActive_ || profile_.pendingRaid.has_value())
    {
        persistenceMessage_ =
            "active Raid time can only be saved by Settlement";
        return false;
    }
    return retryBaseDailySave();
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
    bool allowPeriodicCheckpoint,
    bool allowAutomaticAdmission)
{
    if (profileUiCommitPending() || baseDailySaveBlocked_ || !std::isfinite(deltaTime) || deltaTime <= 0.0F)
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
        if (!allowPeriodicCheckpoint &&
            ((alphaRaidActive_ && profile_.pendingRaid.has_value()) || baseDefenseActive()))
        {
            // The active Raid owns an already validated, in-memory Profile.
            // Copying that Profile once per world minute also copied the full
            // frozen megamap (several MiB) and produced a deterministic
            // ~70 ms main-thread hitch every real second. Advance the owned
            // runtime state in place instead. Each time consumer is an
            // idempotent domain operation; Settlement still performs the
            // ordinary full candidate validation and atomic save.
            const WorldClockAdvanceResult advanced =
                advanceWorldClock(profile_.worldClock, minutes);
            if (advanced.minutesApplied == 0U)
            {
                persistenceMessage_ = "active Raid world clock overflow";
                pendingWorldSeconds_ = scaledSeconds;
                return;
            }
            const auto [demand, construction, manufacturing, residentTreatment] =
                synchronizeBaseWorkThrough(profile_, publishedContentRegistry(), advanced.minutesApplied);
            const RecoveryTaskAdvanceResult recovery =
                applyRecoveryTaskThrough(profile_);
            const bool revisionRequired =
                construction.completed || manufacturing.completed ||
                residentTreatment.completed || recovery.becameReady;
            if (revisionRequired)
            {
                if (profile_.revision ==
                    std::numeric_limits<ProfileRevision>::max())
                {
                    persistenceMessage_ =
                        "active Raid timed completion revision overflow";
                    state_ = GameSessionState::SettlementBlocked;
                    pendingWorldSeconds_ = scaledSeconds;
                    return;
                }
                ++profile_.revision;
            }
            recordBaseCompletionEvents(
                construction,
                manufacturing,
                residentTreatment,
                alphaRaidActive_);
            static_cast<void>(demand);
            worldClockDirty_ = true;
            pendingWorldSeconds_ = remainingWorldSeconds;
            return;
        }

        ProfileState candidate = profile_;
        const WorldClockAdvanceResult advanced =
            advanceWorldClock(candidate.worldClock, minutes);
        if (advanced.minutesApplied > 0U)
        {
            const auto [demand, construction, manufacturing, residentTreatment] =
                synchronizeBaseWorkThrough(candidate, publishedContentRegistry(), advanced.minutesApplied);
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
            const bool treatmentStarted = allowAutomaticAdmission &&
                candidate.residentMedicalReservePolicy.automaticAdmission && basePlayerWorld_ &&
                basePlayerWorld_->playerSafetyZone() == HomeRegionSafetyZone::SafeCore &&
                basePlayerWorld_->siteDefinitionId() == candidate.regionalOperations.technologyCore.baseSiteDefinitionId.value() &&
                tryStartAutomaticResidentTreatment(candidate, publishedContentRegistry());
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
                residentTreatment.completed || recovery.becameReady || treatmentStarted)
            {
                auto prepared=std::make_shared<PreparedPlayerTransition>();
                prepared->kind=PreparedPlayerTransition::Kind::TimedBaseCompletion;
                prepared->construction=construction;prepared->manufacturing=manufacturing;
                prepared->residentTreatment=residentTreatment;prepared->pendingWorldSeconds=remainingWorldSeconds;
                const auto queued=enqueuePreparedProfileUi(std::move(candidate),std::nullopt,
                    ProfileUiCompletionKind::TimedBaseCompletion,std::nullopt,std::move(prepared));
                if(!queued.accepted)pendingWorldSeconds_=scaledSeconds;
                return;
            }
            else
            {
                if(!commitProfileCandidate(std::move(candidate),false)) {pendingWorldSeconds_=scaledSeconds;return;}
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
        static_cast<void>(checkpointBaseDaily(false));
    }
}

void GameSession::advanceBaseSiegeFromSimulation(float deltaTime)
{
    if (baseDefenseActive() || baseDailySaveBlocked_) return;
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F ||
        alphaRaidActive_ || profile_.pendingRaid.has_value() ||
        state_ != GameSessionState::BetweenRaids)
    {
        return;
    }

    const float sourceAccumulator=baseSiegeWarningSecondAccumulator_;
    float warningAccumulator=sourceAccumulator;
    ProfileState candidate = profile_;
    const bool activated = activateBaseSiegeWarningIfEligible(candidate);
    bool changed = activated;
    if (activated)
    {
        candidate.baseDefenseWarning = prepareBaseDefenseWarning(candidate, publishedContentRegistry());
        if (candidate.revision ==
            std::numeric_limits<ProfileRevision>::max())
        {
            persistenceMessage_ = "Base siege warning revision overflow";
            return;
        }
        ++candidate.revision;
        warningAccumulator = 0.0F;
    }

    if (candidate.baseSiege.warningActive &&
        candidate.baseSiege.warningRemainingSeconds > 0U)
    {
        warningAccumulator += deltaTime;
        const std::uint32_t wholeSeconds = static_cast<std::uint32_t>(
            std::floor(warningAccumulator));
        if (wholeSeconds > 0U)
        {
            changed = advanceBaseSiegeWarning(candidate, wholeSeconds) ||
                changed;
            warningAccumulator -=
                static_cast<float>(wholeSeconds);
        }
    }

    if (!changed)
    {
        baseSiegeWarningSecondAccumulator_=warningAccumulator;return;
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
        if(!receipt.succeeded){persistenceMessage_=receipt.message;return;}
        auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::BaseSnapshot;
        prepared->warningAccumulatorSeconds=0.0F;
        const auto queued=enqueuePreparedProfileUi(std::move(candidate),std::nullopt,ProfileUiCompletionKind::BaseSnapshot,std::nullopt,std::move(prepared));
        if(!queued.accepted)baseSiegeWarningSecondAccumulator_=sourceAccumulator+deltaTime;
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
        auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::BaseSnapshot;
        prepared->warningAccumulatorSeconds=warningAccumulator;
        const auto queued=enqueuePreparedProfileUi(std::move(candidate),std::nullopt,ProfileUiCompletionKind::BaseSnapshot,std::nullopt,std::move(prepared));
        if(!queued.accepted)baseSiegeWarningSecondAccumulator_=sourceAccumulator+deltaTime;
        return;
    }
    baseSiegeWarningSecondAccumulator_=warningAccumulator;
    profile_ = std::move(candidate);
    worldClockDirty_ = true;
}

void GameSession::resetWorldClockRuntime() noexcept
{
    weaponTriggerGate_.reset();
    freightUnlock_ = {};
    freightBreach_ = {};
    droppedRaidMagazineIds_.clear();
    droppedRaidBackpackIds_.clear();
    pendingWorldSeconds_ = 0.0;
    worldClockCheckpointElapsedSeconds_ = 0.0F;
    worldClockDirty_ = false;
    baseSiegeWarningSecondAccumulator_ = 0.0F;
}

void GameSession::recordBaseCompletionEvents(
    const BaseConstructionAdvanceResult &construction,
    const BaseManufacturingAdvanceResult &manufacturing,
    const ResidentTreatmentAdvanceResult &residentTreatment,
    bool deferUntilRaidSettlement)
{
    auto &events = deferUntilRaidSettlement
        ? deferredRaidBasePresentationEvents_
        : presentationEvents_;
    if (construction.completed)
    {
        switch (construction.target)
        {
        case BaseFacilityUpgradeTarget::Dormitory:
            events.push_back(
                GameSessionPresentationEvent::BaseDormitoryUpgradeCompleted);
            break;
        case BaseFacilityUpgradeTarget::KitchenWater:
            events.push_back(
                GameSessionPresentationEvent::BaseKitchenWaterUpgradeCompleted);
            break;
        case BaseFacilityUpgradeTarget::Workshop:
            events.push_back(
                GameSessionPresentationEvent::BaseWorkshopUpgradeCompleted);
            break;
        case BaseFacilityUpgradeTarget::Medical:
            events.push_back(
                GameSessionPresentationEvent::BaseMedicalUpgradeCompleted);
            break;
        case BaseFacilityUpgradeTarget::Generator:
            break;
        }
    }
    if (manufacturing.completed)
    {
        events.push_back(manufacturing.researchCompleted
            ? GameSessionPresentationEvent::BaseArmorRepairResearchCompleted
            : GameSessionPresentationEvent::BaseManufacturingCompleted);
    }
    if (residentTreatment.completed)
    {
        events.push_back(
            GameSessionPresentationEvent::BaseResidentTreatmentCompleted);
    }
}

void GameSession::publishDeferredRaidBasePresentationEvents()
{
    presentationEvents_.insert(
        presentationEvents_.end(),
        deferredRaidBasePresentationEvents_.begin(),
        deferredRaidBasePresentationEvents_.end());
    deferredRaidBasePresentationEvents_.clear();
}

void GameSession::updateAlphaRaid(
    const GameplayInput &rawInput,
    float deltaTime)
{
    GameplayInput input=rawInput;
    float weaponActionDelta = deltaTime;
    lastIncomingDamage_.reset();
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        raidElapsedSeconds_ += deltaTime;
    }
    if (!developerRange_ && !profile_.pendingRaid.has_value())
    {
        freightUnlock_ = {};
        freightBreach_ = {};
        alphaRaidActive_ = false;
        outpostRestorationObjectiveSecured_ = false;
        baseSiteClearanceObjectiveSecured_ = false;
        basePerimeterSweepObjectiveSecured_ = false;
        state_ = GameSessionState::BetweenRaids;
        return;
    }
    if (!developerRange_ && alphaRaidActive_ && world_->raidSession().isActive())
    {
        advanceWorldClockFromSimulation(deltaTime, false);
    }
    synchronizeActiveAlphaWeapon();
    if (input.quitRaidJustPressed && alphaRaidActive_)
    {
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        if (developerRange_) state_ = GameSessionState::BetweenRaids;
        else static_cast<void>(requestActiveQuitAlphaRaid());
        return;
    }
    const RaidSessionState currentState = world_->raidSession().state();
    if (currentState == RaidSessionState::Extracted)
    {
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        if(developerRange_)static_cast<void>(settleAlphaRaid(RaidResultOutcome::Extracted));else static_cast<void>(requestRaidSettlement(RaidResultOutcome::Extracted));
        return;
    }
    if (currentState == RaidSessionState::PlayerDead)
    {
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        if(developerRange_)static_cast<void>(settleAlphaRaid(RaidResultOutcome::PlayerDead));else static_cast<void>(requestRaidSettlement(RaidResultOutcome::PlayerDead));
        return;
    }
    const auto meleeId=equippedAsset(profile_,EquipmentSlotKind::QuickMelee);
    world_->configureMelee(currentMeleeDefinition(),currentMeleeUsable(),meleeId.value_or(0));
    prepareNoiseDecoyInput(input,rawInput);
    const bool toggleLight = updatePlayerLightGesture(input, deltaTime);
    if (tacticalDeviceWheelOpen_)
    {
        // A wheel opened on this frame already owns the controls. Keep the
        // aim anchor and physical trigger latch across the modal boundary.
        GameplayInput wheelInput;
        wheelInput.aimWorldPosition = input.aimWorldPosition;
        wheelInput.aimWorldBounds = input.aimWorldBounds;
        wheelInput.aimViewport = input.aimViewport;
        wheelInput.aimMotionDelta = Vec2{};
        wheelInput.allowWeaponOptics = false;
        wheelInput.firePhysicallyPressed = input.firePhysicallyPressed || input.firePressed;
        wheelInput.freezeObservationDirection = true;
        input = std::move(wheelInput);
        cancelPlayerControlIntent();
    }
    const bool meleeSuppressesThisFrame = input.meleeJustPressed ||
        world_->melee().recoverySeconds() >
            (std::isfinite(deltaTime) ? std::max(0.0F, deltaTime) : 0.0F) + 0.000001F;
    prepareMeleeInput(input,deltaTime,world_->melee().recoverySeconds());
    if (state_ == GameSessionState::SettlementBlocked) return;
    if (input.inventoryOpen || input.sprint || input.firePressed ||
        input.fireJustPressed || input.reloadJustPressed ||
        input.healJustPressed || input.weaponSlotJustPressed.has_value() ||
        world_->player().isControlled())
    {
        weaponClearGesture_.reset();
    }

    weaponActionDelta = preparePlayerActionInput(input, deltaTime, meleeSuppressesThisFrame);
    if (state_ == GameSessionState::SettlementBlocked) return;
    const auto weapon = activeAlphaWeapon();

    GameplayInput simulationInput = input;
    const bool usesFuel = raidFuelExtractionEnabled();
    simulationInput.conditionalExtractionEligible =
        !usesFuel && conditionalExtractionEligible();
    simulationInput.reloadJustPressed = false;
    simulationInput.healJustPressed = false;
    simulationInput.quitRaidJustPressed = false;
    int ammunitionPenetration{};
    int unarmoredDamageBonus{};
    std::optional<ItemDefinitionId> chamberedRound;
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
                if (definition.weaponUse.has_value())
                {
                    weaponHandling = deriveWeaponHandling(
                        effectiveWeaponUse(profile_, publishedContentRegistry(), weaponAsset->instanceId));
                }
                if (weaponAsset->chamberedRound)
                    chamberedRound = weaponAsset->chamberedRound->definitionId;
                const auto &ammunitionId =
                    chamberedRound ? chamberedRound
                                   : definition.compatibleAmmunitionDefinitionId;
                if (ammunitionId.has_value())
                {
                    const ItemDefinition &ammunition =
                        publishedContentRegistry().item(*ammunitionId);
                    if (ammunition.ammunitionUse.has_value())
                    {
                        ammunitionPenetration =
                            ammunition.ammunitionUse->penetration;
                        unarmoredDamageBonus =
                            ammunition.ammunitionUse->unarmoredDamageBonus;
                    }
                }
            }
            catch (...)
            {
            }
        }
    }
    world_->configureWeaponAmmunition(ammunitionPenetration, unarmoredDamageBonus);
    prepareWeaponTrigger(simulationInput, rawInput, weapon,
        !input.inventoryOpen && !input.freezeObservationDirection && !input.throwableModal &&
        !world_->player().isControlled() &&
        !fireSuppressedUntilRelease_ && !decoyFireSuppressedUntilRelease_ &&
        !playerThrowFlightActive());
    if (input.inventoryOpen || raidActionState_.active().has_value())
    {
        if (raidActionState_.active()) weaponTriggerGate_.cancelPending();
        simulationInput.fireJustPressed = false;
        simulationInput.firePressed = false;
        sprintFireIntentPending_ = false;
        sprintFireReadyRemaining_ = 0.0F;
    }
    if (!raidWorldInteractionAvailable(input.inventoryOpen))
    {
        simulationInput.interactJustPressed = false;
        simulationInput.interactPressed = false;
    }
    // Existing scene storage and loose items retain the same F priority.
    // Only query them around the new point, outside the enemy/vision hot path.
    simulationInput.credentialInvestigationAllowed =
        raidWorldInteractionAvailable(input.inventoryOpen) &&
        (!world_->credentialInvestigationInRange() ||
         (!nearbyRaidSceneContainer() && !nearbyRaidLoot()));
    if (fireSuppressedUntilRelease_)
    {
        if (rawInput.firePressed)
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
        if (rawInput.sprint)
        {
            simulationInput.sprint = false;
        }
        else
        {
            sprintSuppressedUntilRelease_ = false;
        }
    }

    preparePlayerCombatInput(simulationInput,input,deltaTime,weaponHandling);
    gateOpticInteraction(input, simulationInput, deltaTime);
    prepareRaidFreightUnlock(rawInput, input, simulationInput);
    prepareRaidFreightBreach(rawInput, input, simulationInput);

    std::optional<FireWeaponCommand> pendingFireCommand;
    std::optional<std::string> pendingFireTransaction;
    if (weapon.has_value() &&
        !raidActionState_.active().has_value() &&
        !input.inventoryOpen &&
        (simulationInput.fireJustPressed ||
         simulationInput.firePressed))
    {
        Pcg32 faultRandom{
            (profile_.pendingRaid ? profile_.pendingRaid->seed : 1ULL) ^ 0x776561706f6e2d66ULL,
            weaponFaultSequence_ + 0x6661756c742d726fULL};
        const FireWeaponCommand fireCommand{
            *weapon,
            faultRandom.bounded(10000U),
            faultRandom.next(), developerRange_ && input.developerInfiniteAmmo ? EmptyChamberPolicy::ChamberOnTrigger
                : EmptyChamberPolicy::RequireExplicitChamber};
        const WeaponAmmoPlan fire = queryFireWeapon(
            profile_,
            publishedContentRegistry(),
            fireCommand);
        if (fire.canCommit && (fire.result == WeaponAmmoResult::Fired ||
            (developerRange_ && input.developerInfiniteAmmo && fire.result == WeaponAmmoResult::Chambered)))
        {
            if (!input.developerInfiniteAmmo)
            {
                pendingFireTransaction.emplace(nextRaidTransaction("fire"));
                if (pendingFireTransaction->empty())
                {
                    weaponTriggerGate_.cancelUntilRelease();
                    simulationInput.fireJustPressed = simulationInput.firePressed = false;
                }
                else pendingFireCommand = fireCommand;
            }
        }
        else
        {
            weaponTriggerGate_.cancelUntilRelease();
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

    world_->setRaidDaylight(worldClockProjection().timeOfDay == WorldTimeOfDay::Day);
    advancePlayerLights(toggleLight, input, deltaTime);
    const bool restorationActive =
        profile_.pendingRaid && profile_.pendingRaid->outpostRestoration.has_value();
    const bool siteClearanceActive =
        profile_.pendingRaid && profile_.pendingRaid->baseSiteClearance.has_value();
    const bool perimeterSweepActive =
        profile_.pendingRaid && profile_.pendingRaid->basePerimeterSweep.has_value();
    const bool weaponActionPortal = input.interactJustPressed && !input.inventoryOpen &&
        raidActionState_.active() &&
        (std::holds_alternative<ReloadRaidAction>(*raidActionState_.active()) ||
         std::holds_alternative<ChamberRaidAction>(*raidActionState_.active()));
    GameplayWorld::RaidFuelSelector selectFuel;
    if (usesFuel)
    {
        selectFuel = [this](std::optional<AssetInstanceId> bound) {
            if (!bound) return findCarriedRaidFuel(profile_);
            return isCarriedRaidFuel(profile_, *bound) ? bound : std::nullopt;
        };
    }
    world_->update(simulationInput, deltaTime, [this]{return processGrenadeExplosion();},
        [this] {
            if (cancelActiveRaidAction()) return true;
            state_ = GameSessionState::SettlementBlocked;
            return false;
        }, weaponActionPortal, selectFuel);
    if(!world_->raidSession().isActive())world_->commitNoiseDecoy({});
    const bool meleeFrame=world_->melee().result().attempted;
    if(meleeFrame)
    {
        applyAlphaIncomingDamage();
        if(!commitPlayerMelee(*world_,profile_.currentHealth>0 && !world_->player().isControlled()))
        {state_=GameSessionState::SettlementBlocked;return;}
    }
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
        if (!input.developerInfiniteAmmo && !pendingFireCommand.has_value())
        {
            std::terminate();
        }
        if (pendingFireCommand.has_value())
        {
            const WeaponAmmoReceipt committed = executeFireWeapon(
                profile_,
                publishedContentRegistry(),
                *pendingFireCommand,
                CommandContext{
                    profile_.revision,
                    *pendingFireTransaction});
            if (!committed.succeeded ||
                committed.firedAmmunitionDefinitionId != chamberedRound ||
                (committed.result != WeaponAmmoResult::Fired &&
                 committed.result !=
                     WeaponAmmoResult::FiredAndMalfunctioned))
            {
                std::terminate();
            }
        }
        weaponTriggerGate_.acceptedShot();
        world_->emitPlayerNoise(kUnsuppressedGunshotNoiseRadius *
            world_->shotAcousticsLastUpdate().noiseRadiusMultiplier);
        if (!input.developerInfiniteAmmo)
        {
            ++weaponFaultSequence_;
        }
        const AssetRecord *currentWeapon = weapon.has_value()
            ? profile_.assets.find(*weapon)
            : nullptr;
        if (currentWeapon != nullptr &&
            currentWeapon->weaponMalfunction != WeaponMalfunctionType::None)
        {
            weaponClearGesture_.reset();
            persistenceMessage_ = "weapon malfunction";
            weaponTriggerGate_.cancelUntilRelease();
        }
    }

    if(!meleeFrame)applyAlphaIncomingDamage();
    if(state_==GameSessionState::SettlementBlocked || !processGrenadeExplosion())return;
    if(developerRange_) {
        for(const auto &fact:world_->trainingCorpseDeaths()) {
            BaseCorpseRecord corpse{{},playerGameplayContext().activity,fact.id,0,fact.center,fact.size};
            const auto id=baseCorpseIdentity(corpse);
            static_cast<void>(recordBaseCorpseDeath(profile_,publishedContentRegistry(),std::move(corpse),{profile_.revision,id}));
        }
        world_->acknowledgeTrainingCorpseDeaths();
    }
    const auto corpseDeaths = world_->pendingCorpseDeaths(); // Own values across Profile commits and queue erasure.
    for (const auto &fact : corpseDeaths)
        if (world_->grenadeExplodedLastUpdate()
            ? revealRaidCorpseInSimulation(profile_, publishedContentRegistry(), fact)
            : revealRaidCorpse(profile_, publishedContentRegistry(), fact))
            world_->acknowledgeCorpseDeath(fact.sourceEnemyId);
    if(world_->grenade().active()) {
        const auto pos=world_->player().position();const float half=world_->player().size()/2;
        const Vec2 center{pos.x+half,pos.y+half};
        const auto at=[&](const auto &point){return point && point->contains(center);};
        if((input.interactJustPressed && world_->raidSpacePortalInteractionInRange()) ||
           (world_->inOutdoorRaidSpace() && (world_->extractionPoint().contains(center) ||
             at(world_->emergencyExtractionPoint()) || at(world_->conditionalExtractionPoint()) ||
             at(world_->credentialExtractionPoint()))))
            persistenceMessage_="LIVE GRENADE | WAIT FOR EXPLOSION";
    }
    if (lastIncomingDamage_.has_value() &&
        lastIncomingDamage_->damageApplied > 0)
    {
        world_->cancelOrdinarySurvivorRescueInteraction();
    }
    else if (world_->ordinarySurvivorRescueReady())
    {
        static_cast<void>(requestOrdinarySurvivorRescue());
        if(profileUiCommitPending())return;
    }
    advanceAlphaMedicalStatus(deltaTime);

    if (const auto fact = world_->pendingCredentialInvestigation())
    {
        if (world_->player().isDead() || world_->player().isControlled() ||
            (lastIncomingDamage_ && lastIncomingDamage_->damageApplied > 0))
        {
            world_->cancelCredentialInvestigationInteraction();
        }
        else
        {
            ProfileState candidate = profile_;
            const auto recorded = recordRaidCredentialInvestigation(
                candidate, publishedContentRegistry(), *fact);
            if (!recorded.valid ||
                !commitProfileCandidate(std::move(candidate), false))
            {
                persistenceMessage_ = recorded.message;
                state_ = GameSessionState::SettlementBlocked;
                return;
            }
            world_->acknowledgeCredentialInvestigation();
        }
    }

    if (profile_.pendingRaid && profile_.pendingRaid->selfRecovery.has_value() &&
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
            const auto currentObservation = world_->raidObservationKey();
            const InventoryReceipt opened = executeOpenRaidSelfRecovery(
                candidate,
                publishedContentRegistry(),
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("self-recovery-open")},
                &world_->raidObservation(), &currentObservation);
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
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        if(developerRange_)static_cast<void>(settleAlphaRaid(RaidResultOutcome::PlayerDead));else static_cast<void>(requestRaidSettlement(RaidResultOutcome::PlayerDead));
        return;
    }
    if (world_->player().isControlled()) weaponTriggerGate_.cancelUntilRelease();
    if (world_->raidSession().state() == RaidSessionState::Extracted)
    {
        cancelRaidFreightUnlock();
        cancelRaidFreightBreach();
        if(developerRange_)static_cast<void>(settleAlphaRaid(RaidResultOutcome::Extracted));else static_cast<void>(requestRaidSettlement(RaidResultOutcome::Extracted));
        return;
    }

    advancePlayerAction(input, deltaTime, weaponActionDelta);
    if (state_ == GameSessionState::SettlementBlocked) return;

    synchronizeWeaponLight(); // A completed switch affects observation before this frame can interact.
    synchronizeNightVision();
    advanceRaidFreightUnlock(deltaTime);
    advanceRaidFreightBreach(deltaTime);
    if (input.interactJustPressed &&
        !world_->spaceTransitionedLastUpdate() &&
        !world_->raidSession().signalCalledLastUpdate() &&
        !raidActionState_.active().has_value() &&
        !world_->highRiskControlInteractionInRange() &&
        !(simulationInput.credentialInvestigationAllowed &&
          world_->credentialInvestigationInRange()) &&
        !world_->ordinarySurvivorRescueInteractionInRange())
    {
        if (const auto loot = nearbyRaidLoot())
        {
            const bool recoveringPack = std::find(droppedRaidBackpackIds_.begin(),
                droppedRaidBackpackIds_.end(), *loot) != droppedRaidBackpackIds_.end();
            if (recoveringPack &&
                (!raidWorldInteractionAvailable(input.inventoryOpen) || world_->player().isControlled()))
                return;
            const auto currentObservation = world_->raidObservationKey();
            if (tryLearnHospitalFloorplan(*loot, input.inventoryOpen, currentObservation))
                return;
            auto reloadAccess = raidReloadAccess(currentObservation);
            if (recoveringPack)
                reloadAccess.activity = playerGameplayContext().activity;
            const InventoryReceipt receipt = pickupRaidLoot(
                profile_,
                publishedContentRegistry(),
                *loot,
                CommandContext{
                    profile_.revision,
                    nextRaidTransaction("pickup")},
                &world_->raidObservation(), &currentObservation, &reloadAccess);
            if (receipt.succeeded)
            {
                std::erase(droppedRaidMagazineIds_, *loot);
                if (recoveringPack)
                {
                    std::erase(droppedRaidBackpackIds_, *loot);
                    persistenceMessage_ = "BACKPACK RECOVERED";
                }
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
        100 - profile_.currentHealth);
    if (requested <= 0)
    {
        return true;
    }

    if (!action.chargeConsumed)
    {
        ProfileState candidate = profile_;
        candidate.currentHealth = profile_.currentHealth;
        if(candidate.basePlayerEffects && candidate.basePlayerEffects->medical)
            candidate.basePlayerEffects->medical->chargeConsumed=true;
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
        profile_.currentHealth + requested);
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
    const int restored = !alphaRaidActive_ || world_->restorePlayerHealth(requested) ? requested : 0;
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
            (candidate.pendingRaid ? candidate.pendingRaid->seed : 1ULL) ^ medicalRandomSequence_,
            0x7061696e2d736372ULL};
        const MedicalAdvanceResult advanced = advanceMedicalStatus(
            candidate.medicalStatus,
            candidate.currentHealth,
            100,
            15000U + random.bounded(10001U));
        if (developerRange_ && candidate.currentHealth == 0)
        {
            static_cast<void>(world_->damagePlayer(world_->player().health()));
            state_ = GameSessionState::BetweenRaids;
            return;
        }
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
        if (advanced.healthLost > 0 && alphaRaidActive_)
        {
            static_cast<void>(world_->damagePlayer(advanced.healthLost));
        }
        if (advanced.screamed)
        {
            if(alphaRaidActive_)world_->emitPlayerNoise(300.0F);
            else if(basePlayerWorld_)basePlayerWorld_->emitNoise(playerGameplayContext().playerCenter,300.0F);
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

        // A disposable range has no persisted death state. Resolve lethal damage
        // through the same domain, then leave without manufacturing a Raid result.
        if (developerRange_)
        {
            const auto plan = queryIncomingDamage(profile_, publishedContentRegistry(),
                IncomingDamageCommand{observation.baseDamage, observation.region,
                    observation.penetration, observation.armorDamage, observation.weakPoint, {}});
            if (plan.canCommit && plan.resolution.damageApplied >= profile_.currentHealth)
            {
                lastIncomingDamage_ = plan.resolution;
                static_cast<void>(world_->damagePlayer(plan.resolution.damageApplied));
                state_ = GameSessionState::BetweenRaids;
                break;
            }
        }
        ProfileState candidate = profile_;
        const std::uint64_t woundSequence = ++woundRandomSequence_;
        Pcg32 woundRandom{
            (profile_.pendingRaid ? profile_.pendingRaid->seed : 1ULL) ^ woundSequence,
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

bool GameSession::prepareRaidSettlementCandidate(RaidResultOutcome outcome,ProfileState &candidate)
{
    if (!profile_.pendingRaid.has_value())
    {
        return false;
    }
    candidate = profile_;
    // Extraction resolves held magazines in the SAME durability candidate.
    // Failed/active-quit loss goes directly through Held -> Retained in the
    // lifecycle transaction; normal cancellation would wrongly drop assets.
    if (outcome == RaidResultOutcome::Extracted)
    {
        const auto *active = raidActionState_.activeMutable();
        if (const auto *reload = active ? std::get_if<ReloadRaidAction>(active) : nullptr;
            reload && reload->nextStage != 0U)
        {
            const auto key = world_->raidObservationKey();
            const auto drop = raidMagazineDrop();
            const auto cancelled = executeRaidReloadCancel(candidate, publishedContentRegistry(),
                {reload->plan.binding, reload->plan.newSource, drop, drop},
                {candidate.revision, reload->plan.binding.actionId + "/cancel"}, raidReloadAccess(key));
            if (!cancelled.succeeded)
            {
                persistenceMessage_ = cancelled.message;
                state_ = GameSessionState::SettlementBlocked;
                return false;
            }
        }
    }
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
    std::optional<RaidFuelExtractionCompletion> fuelCompletion;
    if (outcome == RaidResultOutcome::Extracted &&
        world_->raidSession().extractionRoute() == RaidExtractionRoute::Fuel)
    {
        const auto fuel = world_->completedFuelExtractionAsset();
        if (!fuel)
        {
            persistenceMessage_ = "FUEL EXTRACTION PAYMENT UNAVAILABLE";
            state_ = GameSessionState::SettlementBlocked;
            return false;
        }
        fuelCompletion = RaidFuelExtractionCompletion{candidate.pendingRaid->raidId, *fuel};
    }
    const RaidSettlementReceipt receipt = settlePendingRaid(
        candidate,
        publishedContentRegistry(),
        candidate.pendingRaid->settlementId,
        outcome,
        fuelCompletion);
    if (!receipt.succeeded)
    {
        persistenceMessage_ = receipt.message;
        state_ = GameSessionState::SettlementBlocked;
        return false;
    }
    return true;
}

bool GameSession::settleAlphaRaid(RaidResultOutcome outcome)
{
    if(developerRange_){state_=GameSessionState::BetweenRaids;return true;}
    ProfileState candidate;if(!prepareRaidSettlementCandidate(outcome,candidate))return false;
    if(!commitProfileCandidate(std::move(candidate))){state_=GameSessionState::SettlementBlocked;return false;}
    auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::RaidSettlement;
    completePreparedPlayerTransition(prepared);return true;
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

bool GameSession::prepareOrdinarySurvivorRescueCandidate(ProfileState &raidCandidate,ProfileState &recoveryCandidate)
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
    raidCandidate = profile_;
    recoveryCandidate = *activeRaidRecoveryProfile_;
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

    return true;
}

bool GameSession::secureOrdinarySurvivorRescue()
{
    ProfileState raidCandidate,recoveryCandidate;
    if(!prepareOrdinarySurvivorRescueCandidate(raidCandidate,recoveryCandidate))return false;
    if(saveRepository_) {
        const auto saved=saveRepository_->save(recoveryCandidate,publishedContentRegistry().contentVersion());
        if(!saved.succeeded){persistenceMessage_=saved.message;world_->cancelOrdinarySurvivorRescueInteraction();return false;}
        persistenceMessage_=saved.message;
    }
    profile_=std::move(raidCandidate);
    auto prepared=std::make_shared<PreparedPlayerTransition>();prepared->kind=PreparedPlayerTransition::Kind::Rescue;
    prepared->raidRecoveryProfile=std::move(recoveryCandidate);completePreparedPlayerTransition(prepared);return true;
}

std::string GameSession::nextRaidTransaction(std::string_view prefix)
{
    const std::string owner = profile_.pendingRaid.has_value()
        ? profile_.pendingRaid->raidId
        : profile_.activeBaseDefense.has_value()
        ? profile_.activeBaseDefense->eventId
        : profile_.profileId;
    const std::string stem = std::string{prefix} + ":" + owner + ":";
    while (raidCommandSequence_ != std::numeric_limits<std::uint64_t>::max())
    {
        const std::string transaction = stem + std::to_string(++raidCommandSequence_);
        if (profile_.committedTransactions.contains(transaction)) continue;
        if (prefix == "reload-action")
        {
            // A reload commits stage children, rather than its parent ID.
            const std::string family = transaction + "/";
            const auto child = profile_.committedTransactions.lower_bound(family);
            if (child != profile_.committedTransactions.end() && child->starts_with(family)) continue;
        }
        return transaction;
    }
    persistenceMessage_ = "TRANSACTION IDS EXHAUSTED";
    return {};
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
        if (!active) world_->clearWeaponOptic();
        return;
    }
    configuredWeaponAssetId_ = active;
    weaponClearGesture_.reset();
    world_->clearWeaponOptic();
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
                auto use = entry.weaponUse;
                use.optic = effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId).optic;
                world_->configureWeaponFire(
                    use, effectiveDeveloperHandling(entry), false);
            }
            else
            {
                world_->configureWeaponFire(effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId));
            }
        }
    }
    catch (...)
    {
        configuredWeaponAssetId_.reset();
    }
}

void GameSession::synchronizeActiveBaseWeapon(BaseWorld &baseWorld)
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
    if (configuredBaseWeaponAssetId_ == active)
    {
        if (!active) baseWorld.clearWeaponOptic();
        return;
    }
    configuredBaseWeaponAssetId_ = active;
    weaponClearGesture_.reset();
    baseWorld.clearWeaponOptic();
    if (!active.has_value())
    {
        return;
    }
    const AssetRecord *asset = profile_.assets.find(*active);
    if (asset == nullptr)
    {
        configuredBaseWeaponAssetId_.reset();
        return;
    }
    try
    {
        const ItemDefinition &definition =
            publishedContentRegistry().item(asset->definitionId);
        if (!definition.weaponUse.has_value())
        {
            configuredBaseWeaponAssetId_.reset();
            return;
        }
        if (const auto index = developerWeaponOverrideIndex(*active))
        {
            const DeveloperWeaponOverride &entry =
                developerWeaponOverrides_[*index];
            auto use = entry.weaponUse;
            use.optic = effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId).optic;
            baseWorld.configureWeaponFire(
                use, effectiveDeveloperHandling(entry), false);
        }
        else
        {
            baseWorld.configureWeaponFire(effectiveWeaponUse(profile_, publishedContentRegistry(), asset->instanceId));
        }
    }
    catch (...)
    {
        configuredBaseWeaponAssetId_.reset();
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
    if (!profile_.pendingRaid.has_value() && !developerRange_)
    {
        return std::nullopt;
    }
    const Vec2 playerPosition = world_->player().position();
    const float half = world_->player().size() / 2.0F;
    const Vec2 playerCenter{playerPosition.x + half, playerPosition.y + half};
    std::optional<AssetInstanceId> result;
    float bestDistance = 72.0F * 72.0F;
    if(profile_.pendingRaid)for (const RaidLootSnapshot &loot : profile_.pendingRaid->loot)
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
    for (auto id : droppedRaidMagazineIds())
    {
        const auto *asset = profile_.assets.find(id);
        std::optional<Vec2> point;
        if(asset) {
            if(const auto *drop=std::get_if<RaidDroppedMagazineLocation>(&asset->location))point=drop->position;
            if(const auto *drop=std::get_if<TrainingDroppedMagazineLocation>(&asset->location))point=drop->position;
        }
        if(!point || !world_->raidBoundsObservable({{point->x-12,point->y-12},{24,24}}))continue;
        const float dx=point->x-playerCenter.x,dy=point->y-playerCenter.y;
        if (dx * dx + dy * dy <= bestDistance)
        { bestDistance = dx * dx + dy * dy; result = id; }
    }
    for (const auto &[id, asset] : profile_.assets.records())
    {
        const auto *drop=std::get_if<RaidDroppedBatteryLocation>(&asset.location);
        if (!drop || drop->spaceId!=world_->activeRaidSpaceId() || !world_->raidBoundsObservable({{drop->position.x-12,drop->position.y-12},{24,24}})) continue;
        const float dx=drop->position.x-playerCenter.x, dy=drop->position.y-playerCenter.y;
        if (dx*dx+dy*dy<=bestDistance) { bestDistance=dx*dx+dy*dy; result=id; }
    }
    for (const auto id : droppedRaidBackpackIds_)
    {
        const auto point = observableRaidBackpackPosition(id);
        if (!point) continue;
        const float dx = point->x - playerCenter.x;
        const float dy = point->y - playerCenter.y;
        const float distance = dx * dx + dy * dy;
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            result = id;
        }
    }
    return result;
}

bool GameSession::raidLootAccessible(
    const RaidLootSnapshot &loot) const
{
    if(!world_ || loot.spaceId!=world_->activeRaidSpaceId() ||
       (loot.requiresHighRisk && world_->raidSession().phase()!=RaidPhase::HighRisk))return false;
    const auto *asset=profile_.assets.find(loot.assetId);
    if(!asset)return false;
    const Vec2 size=publishedContentRegistry().item(asset->definitionId).worldRenderSize;
    return world_->raidBoundsObservable({{loot.position.x-size.x*0.5F,loot.position.y-size.y*0.5F},size});
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
    return world_->raidBoundsObservable({{cache.x-30,cache.y-22},{60,44}}) && dx * dx + dy * dy <=
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
    if(profileUiCommitPending()){persistenceMessage_="WAIT FOR THE ACTIVE SAVE";return false;}
    if (inventoryPersistencePending())
    {
        bool changesInventory = persist || candidate.assets.records().size() != profile_.assets.records().size();
        if (!changesInventory)
            for (const auto &[id, asset] : candidate.assets.records())
            {
                const auto *before = profile_.assets.find(id);
                if (!before || before->location != asset.location || before->quantity != asset.quantity ||
                    before->orientation != asset.orientation) { changesInventory = true; break; }
            }
        if (changesInventory)
        {
            requestInventoryPersistenceBarrier();
            updateInventoryPersistence(0);
            // This candidate predates the confirmation above. A fresh retry
            // must be built from the confirmed prefix even when it just saved.
            persistenceMessage_ = "INVENTORY SAVE PENDING | RETRY AFTER CONFIRMATION";
            return false;
        }
    }
    if (baseDefenseActive())
    {
        // Commands join the current coherent Base activity. Only start/end/
        // quit are durability barriers; no inventory or fire callback writes.
        if (baseDefenseSaveBlocked_) return false;
        if (!captureBaseDefenseCheckpoint(candidate))
        {
            baseDefenseSimulationRejected_ = baseDefenseSaveBlocked_ = true;
            persistenceMessage_ = "DEFENSE SIMULATION REJECTED | RETRY RELOADS LAST CHECKPOINT";
            return false;
        }
        profile_ = std::move(candidate);
        worldClockDirty_ = true;
        return true;
    }
    std::string saveMessage;
    if (persist && !drainBaseDailyCheckpoint()) return false;
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
    if (profile_.tutorial != TutorialProgress::PrepareLoadout ||
        profile_.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return;
    }
    const LoadoutReadinessProjection readiness = projectLoadoutReadiness(
        profile_,
        publishedContentRegistry(),
        kStarterLoadoutArchetype);
    if (!readiness.ready())
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
    if (!profile_.homeFounding.established) return;
    TutorialProgress next = profile_.tutorial;
    if (profile_.tutorial == TutorialProgress::FindStorage &&
        facility == BaseFacilityKind::Storage)
    {
        next = TutorialProgress::PrepareLoadout;
    }
    else if (profile_.tutorial == TutorialProgress::FindRaidGate &&
             facility == BaseFacilityKind::RaidGate &&
             profile_.homeFounding.hintsDismissed)
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
