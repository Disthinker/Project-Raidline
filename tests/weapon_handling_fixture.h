#pragma once

#include "alpha_content_ids.h"
#include "game_session.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

struct WeaponHandlingTestAccess
{
    static ProfileState &profile(GameSession &s) { return s.profile_; }
    static std::uint64_t commandSequence(const GameSession &s){return s.raidCommandSequence_;}
    static void commandSequence(GameSession &s,std::uint64_t value){s.raidCommandSequence_=value;}
    static void world(GameSession &s, RaidWorldConfig config)
    {
        s.world_ = std::make_unique<GameplayWorld>(std::move(config));
        s.configuredWeaponAssetId_.reset();
        s.synchronizeActiveAlphaWeapon();
    }
    static void reset(GameSession &s, const ProfileState &profile, RaidWorldConfig config)
    {
        s.profile_ = profile; s.state_ = GameSessionState::InRaid;
        s.raidActionState_.cancel(); s.weaponTriggerGate_.reset();
        s.droppedRaidMagazineIds_.clear();
        s.fireSuppressedUntilRelease_ = s.sprintSuppressedUntilRelease_ = false;
        s.sprintFireIntentPending_ = false; s.sprintFireReadyRemaining_ = 0;
        world(s, std::move(config));
    }
    static bool stages(GameSession &s, float seconds) { return s.advanceRaidWeaponAction(seconds, false); }
    static ReloadRaidAction &reload(GameSession &s)
    { return std::get<ReloadRaidAction>(*s.raidActionState_.activeMutable()); }
    static WorldShootingCheckpoint shooting(const GameplayWorld &w) { return w.shooting_.checkpoint(); }
    static bool settle(GameSession &s, RaidResultOutcome outcome) { return s.settleAlphaRaid(outcome); }
    static void dead(GameSession &s) { static_cast<void>(s.world_->raidSession_.markPlayerDead()); }
    static auto &actors(GameplayWorld &w) { return w.activeEnemies(); }
    static bool restoreMeleeRecovery(GameplayWorld &w, float seconds)
    { return w.melee_.restoreRecovery(seconds); }
};

struct WeaponHandlingLoadout
{
    AssetInstanceId weapon{}, oldMagazine{}, spareMagazine{}, rig{};
};

inline std::vector<AssetInstanceId> handlingAssets(const ProfileState &p, const ItemDefinitionId &def)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : p.assets.records()) if (asset.definitionId == def) result.push_back(id);
    return result;
}

inline void handlingRequire(bool condition, const std::string &message)
{ if (!condition) throw std::runtime_error(message); }

inline RaidWorldConfig handlingArena()
{
    RaidWorldConfig config;
    config.worldSize = {3000, 1800}; config.playerSpawn = {1200, 784};
    config.extractionPoint = {{2750, 1500}, {100, 100}};
    config.playerMaximumHealth = config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.lightConeObservationEnabled = true;
    return config;
}

inline WeaponHandlingLoadout prepareHandlingSession(GameSession &session)
{
    handlingRequire(session.startNewProfile("weapon-handling-runtime"), "start profile failed");
    const auto guns = handlingAssets(session.profile(), alpha_content::rifle);
    const auto mags = handlingAssets(session.profile(), alpha_content::magazine);
    const auto ammo = handlingAssets(session.profile(), alpha_content::ammunition);
    const auto rigs = handlingAssets(session.profile(), alpha_content::chestRig);
    const auto packs = handlingAssets(session.profile(), alpha_content::backpack);
    handlingRequire(!guns.empty() && mags.size() >= 2 && ammo.size() >= 2 && !rigs.empty() && !packs.empty(), "missing starter assets");
    const auto equip = [&](AssetInstanceId id, EquipmentSlotKind slot, const char *tx) {
        handlingRequire(session.executeProfileInventory(InventoryEquipCommand{id, slot}, tx).succeeded, tx);
    };
    equip(guns[0], EquipmentSlotKind::PrimaryWeapon, "handling-equip-gun");
    equip(rigs[0], EquipmentSlotKind::ChestRig, "handling-equip-rig");
    equip(packs[0], EquipmentSlotKind::Backpack, "handling-equip-pack");
    for (unsigned i = 0; i != 2; ++i)
    {
        handlingRequire(session.executeProfileWeaponAmmo(LoadMagazineCommand{mags[i], ammo[i], i ? 20U : 10U},
            "handling-load-" + std::to_string(i)).succeeded, "load magazine failed");
        handlingRequire(session.executeProfileInventory(InventoryMoveCommand{mags[i], 0,
            {ProfileContainerId::compartment(rigs[0], 0), {static_cast<int>(i), 0}}, ItemOrientation::Degrees0},
            "handling-carry-" + std::to_string(i)).succeeded, "carry magazine failed");
    }
    handlingRequire(session.executeProfileWeaponAmmo(InstallMagazineAndChamberCommand{guns[0], mags[0]},
        "handling-install").succeeded, "install failed");
    handlingRequire(session.deployAlpha(42, MapDefinitionId{"map.raid.frontier_exchange"}), session.persistenceMessage());
    WeaponHandlingTestAccess::world(session, handlingArena());
    static_cast<void>(session.takePresentationEvents());
    return {guns[0], mags[0], mags[1], rigs[0]};
}

inline GameplayInput handlingReloadIntent(AssetInstanceId weapon,
    RaidReloadInputKind kind = RaidReloadInputKind::Automatic,
    std::optional<AssetInstanceId> magazine = std::nullopt, float age = 0)
{
    GameplayInput input;
    input.raidReloadIntents.push_back({kind, weapon, magazine, age});
    input.raidReloadIntents.back().gestureId=1;
    input.raidReloadIntents.back().doubleTapSeconds=kind==RaidReloadInputKind::UpgradeEmergency ? .1 : 0;
    return input;
}
