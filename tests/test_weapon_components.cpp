#include "raid_visibility_fixture.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>
#include "weapon_component_domain.h"
#include "save_repository.h"
#include "game_session.h"
#include "economy_domain.h"
#include "lost_raid_domain.h"
#include "recovery_task_domain.h"
#include "self_recovery_domain.h"
#include "raid_lifecycle.h"

namespace
{
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"};
const ItemDefinitionId barrel{"item.component.precision_barrel"};
const ItemDefinitionId rail{"item.component.rail_handguard"};
const ItemDefinitionId grip{"item.component.vertical_grip"};
const auto &content() { return publishedContentRegistry(); }
AssetInstanceId find(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records()) if (asset.definitionId == definition) return id;
    return 0;
}
InventoryReceipt install(ProfileState &profile, const ItemDefinitionId &definition, const std::string &transaction)
{
    const auto &component = content().item(definition).weaponComponent.value();
    return executeWeaponComponentChange(profile, content(), {find(profile, rifle), component.slot, find(profile, definition)}, {profile.revision, transaction});
}
ProfileState modified()
{
    auto profile = makeNewPublishedProfile("components", content());
    EXPECT_TRUE(install(profile, rail, "rail").succeeded);
    EXPECT_TRUE(install(profile, grip, "grip").succeeded);
    EXPECT_TRUE(install(profile, barrel, "barrel").succeeded);
    return profile;
}
}

TEST(WeaponComponentsTest, InterfaceRejectionPreviewAndFiveAttributeRuntimeContract)
{
    auto profile = makeNewPublishedProfile("component-rules", content());
    const auto original = profileStateFingerprint(profile);
    const auto denied = install(profile, grip, "grip-no-rail");
    EXPECT_FALSE(denied.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), original);
    const auto plan = queryWeaponComponentChange(profile, content(), {find(profile, rifle), WeaponComponentSlot::Barrel, find(profile, barrel)});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), original);
    EXPECT_EQ(plan.after.accuracy, 90U);
    EXPECT_EQ(plan.after.handlingSpeed, 44U);
    EXPECT_FLOAT_EQ(plan.after.effectiveRange, 1000.0F);
    ASSERT_TRUE(install(profile, barrel, "barrel").succeeded);
    EXPECT_EQ(effectiveWeaponUse(profile, content(), find(profile, rifle)), plan.after);
    auto stale = executeWeaponComponentChange(profile, content(), {find(profile, rifle), WeaponComponentSlot::Barrel, 0}, {0, "stale"});
    EXPECT_FALSE(stale.succeeded);
    const auto after = profileStateFingerprint(profile);
    EXPECT_TRUE(install(profile, barrel, "barrel").alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), after);
}

TEST(WeaponComponentsTest, CascadingRemovalPlansAllReturnsAndFullStashRejectsAtomically)
{
    auto profile = modified();
    const auto weapon = find(profile, rifle);
    const WeaponComponentCommand remove{weapon, WeaponComponentSlot::Handguard, 0};
    auto plan = queryWeaponComponentChange(profile, content(), remove);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.returned.size(), 2U);
    const auto &filler = content().item(ItemDefinitionId{"item.component.vertical_grip"});
    while (auto position = findFirstProfileFit(profile, content(), ProfileContainerId::stash(), filler, ItemOrientation::Degrees0))
        static_cast<void>(profile.assets.create(filler, StoredAssetLocation{ProfileContainerId::stash(), *position}));
    const auto full = profileStateFingerprint(profile);
    EXPECT_FALSE(executeWeaponComponentChange(profile, content(), remove, {profile.revision, "full"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), full);
    auto successful = modified();
    ASSERT_TRUE(executeWeaponComponentChange(successful, content(), {find(successful, rifle), WeaponComponentSlot::Handguard, 0}, {successful.revision, "remove"}).succeeded);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(successful.assets.find(find(successful, rail))->location));
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(successful.assets.find(find(successful, grip))->location));
    EXPECT_TRUE(validateProfileState(successful, content()).valid);
}

TEST(WeaponComponentsTest, SchemaRoundTripLegacyDefaultsAndInvalidGraphs)
{
    auto profile = modified();
    const auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content().contentVersion()), content());
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), profileStateFingerprint(profile));
    profile.stashLayoutVersion = 0U;
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(profile, content().contentVersion(), 48)), std::invalid_argument);
    auto old = makeNewPublishedProfile("legacy", content());
    old.stashLayoutVersion = 0U;
    for (const auto &definition : {barrel, rail, grip}) static_cast<void>(old.assets.erase(find(old, definition)));
    auto migrated = deserializeProfileEnvelope(serializeProfileEnvelope(old, "hospital-raid-theme-content-62", 48), content());
    ASSERT_TRUE(migrated.profile) << migrated.message;
    EXPECT_EQ(effectiveWeaponUse(*migrated.profile, content(), find(old, rifle)), content().item(rifle).weaponUse.value());
    auto corrupt = profile;
    corrupt.assets.findMutable(find(corrupt, rail))->location = InstalledWeaponComponentLocation{999999};
    EXPECT_FALSE(validateProfileState(corrupt, content()).valid);
    corrupt = profile;
    static_cast<void>(corrupt.assets.create(content().item(grip), InstalledWeaponComponentLocation{find(corrupt, rifle)}));
    EXPECT_FALSE(validateProfileState(corrupt, content()).valid);
    for (const auto &[id, asset] : old.assets.records())
        if (content().item(asset.definitionId).weaponUse)
            EXPECT_EQ(effectiveWeaponUse(old, content(), id), *content().item(asset.definitionId).weaponUse);
}

TEST(WeaponComponentsTest, RecyclingWeaponConsumesItsComponentsOnce)
{
    auto profile = modified();
    const auto weapon = find(profile, rifle), part = find(profile, grip);
    EXPECT_FALSE(executeEconomy(profile, content(), RecycleCommand{part}, {profile.revision, "loose"}).succeeded);
    const auto currency = profile.currency;
    auto receipt = executeEconomy(profile, content(), RecycleCommand{weapon}, {profile.revision, "whole"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(part), nullptr);
    EXPECT_EQ(profile.assets.find(weapon), nullptr);
    EXPECT_EQ(profile.currency - currency, 145U + 60U + 35U + 45U);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_TRUE(executeEconomy(profile, content(), RecycleCommand{weapon}, {profile.revision, "whole"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(WeaponComponentsTest, DeathReloadAndBothRecoveryPathsPreserveTheSameComponentTree)
{
    auto profile = modified();
    const auto weapon = find(profile, rifle), part = find(profile, grip);
    ASSERT_TRUE(executeInventory(profile, content(), InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon}, {profile.revision, "equip"}).succeeded);
    const auto backpack = find(profile, ItemDefinitionId{"item.container.backpack_small"});
    ASSERT_TRUE(executeInventory(profile, content(), InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack}, {profile.revision, "backpack"}).succeeded);
    ASSERT_TRUE(executeDeploy(profile, content(), DeployCommand{"component-raid", "component-loss", 7301, MapDefinitionId{"map.v0.test"}, {}, std::nullopt}, {profile.revision, "deploy"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(profile, content(), "component-loss", RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_EQ(lostRaidRecordForAsset(profile, part), std::optional<std::string>{"component-loss"});
    EXPECT_FALSE(assetIsBaseAccessible(profile, part));
    for (bool npc : {false, true})
    {
        SCOPED_TRACE(npc ? "NPC recovery" : "self recovery");
        auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content().contentVersion()), content());
        ASSERT_TRUE(loaded.profile) << loaded.message;
        auto state = *loaded.profile;
        if (npc)
        {
            const auto source = state;
            bool found = false;
            for (RecoveryTaskId trial = 1; trial <= 32; ++trial)
            {
                state = source;
                state.nextRecoveryTaskId = trial;
                auto start = executeStartRecoveryTask(state, content(), "component-loss", {state.revision, "recover"});
                ASSERT_TRUE(start.succeeded) << start.message;
                const bool recovered = state.recoveryTask->recoveredAssetIds.contains(weapon);
                for (const auto &definition : {barrel, rail, grip})
                    EXPECT_EQ(state.recoveryTask->recoveredAssetIds.contains(find(state, definition)), recovered);
                if (recovered) { found = true; break; }
                state.worldClock.elapsedWorldMinutes = state.recoveryTask->completionWorldMinute;
                ASSERT_TRUE(applyRecoveryTaskThrough(state).becameReady);
                ASSERT_TRUE(executeCollectRecoveryTask(state, content(), {state.revision, "not-found"}).succeeded);
                EXPECT_EQ(state.assets.find(part), nullptr);
                EXPECT_EQ(state.assets.find(weapon), nullptr);
            }
            ASSERT_TRUE(found);
            state.worldClock.elapsedWorldMinutes = state.recoveryTask->completionWorldMinute;
            ASSERT_TRUE(applyRecoveryTaskThrough(state).becameReady);
            ASSERT_TRUE(executeCollectRecoveryTask(state, content(), {state.revision, "collect"}).succeeded);
        }
        else
        {
            ASSERT_TRUE(executeDeploy(state, content(), DeployCommand{"recovery", "recovered", 9403, MapDefinitionId{"map.v0.test"}, {}, "component-loss"}, {state.revision, "self"}).succeeded);
            ASSERT_TRUE(openObservedRaidSelfRecovery(state, content(), {state.revision, "open"}).succeeded);
            ASSERT_TRUE(pickupObservedRaidLoot(state, content(), weapon, {state.revision, "pickup"}).succeeded);
            ASSERT_TRUE(settlePendingRaid(state, content(), "recovered", RaidResultOutcome::Extracted).succeeded);
        }
        ASSERT_NE(state.assets.find(part), nullptr);
        EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(state.assets.find(part)->location).weaponAssetId, weapon);
        EXPECT_EQ(effectiveWeaponUse(state, content(), weapon), effectiveWeaponUse(profile, content(), weapon));
        EXPECT_TRUE(validateProfileState(state, content()).valid);
    }
}

TEST(WeaponComponentsTest, SessionPersistsChangesAndFailedSavePreservesProfile)
{
    const auto root = std::filesystem::temp_directory_path() / ("raidline-components-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    GameSession session;
    session.configurePersistence(root);
    auto initial = makeNewPublishedProfile("component-session", content());
    ASSERT_TRUE(SaveRepository(root).save(initial, content().contentVersion()).succeeded);
    ASSERT_TRUE(session.continueProfile());
    const auto weapon = find(session.profile(), rifle);
    ASSERT_TRUE(session.executeProfileInventory(InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon}, "equip-active").succeeded);
    const auto baseline = session.developerWeaponTuning();
    ASSERT_TRUE(baseline);
    ASSERT_TRUE(session.executeBaseWeaponComponentChange({weapon, WeaponComponentSlot::Barrel, find(session.profile(), barrel)}, "install").succeeded);
    const auto changed = session.developerWeaponTuning();
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->weaponUse.accuracy, 90U);
    EXPECT_LT(changed->weaponUse.handlingSpeed, baseline->weaponUse.handlingSpeed);
    GameSession reopened;
    reopened.configurePersistence(root);
    ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(effectiveWeaponUse(reopened.profile(), content(), weapon).accuracy, 90U);
    const auto before = profileStateFingerprint(reopened.profile());
    // A file where the save directory must be makes persistence fail without touching user data.
    const auto blocked = root / "blocked";
    { std::ofstream file(blocked); file << "blocked"; }
    reopened.configurePersistence(blocked);
    auto receipt = reopened.executeBaseWeaponComponentChange({weapon, WeaponComponentSlot::Barrel, 0}, "failed-save");
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(reopened.profile()), before);
}

TEST(WeaponComponentsTest, PublishedLootCanBeExtractedInstalledAndRolledBackWithoutDuplicating)
{
    bool found = false;
    for (std::uint64_t seed = 1; seed <= 32 && !found; ++seed)
    {
        auto profile = makeNewPublishedProfile("component-loot", content());
        const auto weapon = find(profile, rifle);
        const auto backpack = find(profile, ItemDefinitionId{"item.container.backpack_small"});
        ASSERT_TRUE(executeInventory(profile, content(), InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack}, {profile.revision, "backpack"}).succeeded);
        ASSERT_TRUE(executeDeploy(profile, content(), DeployCommand{"loot", "extract", seed, MapDefinitionId{"map.raid.industrial"}, {}, std::nullopt}, {profile.revision, "deploy"}).succeeded);
        AssetInstanceId generated{};
        for (const auto &loot : profile.pendingRaid->loot) if (loot.definitionId == rail) { generated = loot.assetId; break; }
        if (!generated) continue;
        found = true;
        ASSERT_TRUE(pickupObservedRaidLoot(profile, content(), generated, {profile.revision, "loot-pickup"}).succeeded);
        ASSERT_TRUE(settlePendingRaid(profile, content(), "extract", RaidResultOutcome::Extracted).succeeded);
        ASSERT_TRUE(executeWeaponComponentChange(profile, content(), {weapon, WeaponComponentSlot::Handguard, generated}, {profile.revision, "install-found"}).succeeded);
        ASSERT_EQ(std::get<InstalledWeaponComponentLocation>(profile.assets.find(generated)->location).weaponAssetId, weapon);
        ASSERT_TRUE(executeInventory(profile, content(), InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon}, {profile.revision, "equip"}).succeeded);
        const auto use = effectiveWeaponUse(profile, content(), weapon);
        ASSERT_TRUE(executeDeploy(profile, content(), DeployCommand{"pending", "unused", 99, MapDefinitionId{"map.v0.test"}, {}, std::nullopt}, {profile.revision, "second-deploy"}).succeeded);
        auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile, content().contentVersion()), content());
        ASSERT_TRUE(loaded.profile) << loaded.message;
        ASSERT_TRUE(rollbackPendingRaidToBase(*loaded.profile, content()).succeeded);
        EXPECT_EQ(effectiveWeaponUse(*loaded.profile, content(), weapon), use);
        EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(loaded.profile->assets.find(generated)->location).weaponAssetId, weapon);
    }
    EXPECT_TRUE(found) << "Ashworks must expose a real component source";
    for (const auto &table : content().lootTables())
        if (table.id.value().starts_with("loot.hospital."))
            for (const auto &entry : table.entries)
                EXPECT_FALSE(content().item(entry.itemDefinitionId).weaponComponent);
}

TEST(WeaponComponentsTest, OpticSupplyAndInstallUseTheReceiverWithoutFreeGrants)
{
    const auto &registry = content();
    const ItemDefinitionId low{"item.component.optic_1_5x_5_45"};
    auto profile = makeNewPublishedProfile("optic-supply", registry);
    ASSERT_NE(find(profile, rifle), 0U);
    EXPECT_EQ(find(profile, low), 0U);
    EXPECT_EQ(find(profile, kFixedRaidOpticId), 0U);
    auto ordinary = makeNewAlphaProfile("optic-grant", registry);
    ASSERT_TRUE(grantDeveloperWarehouseCatalog(ordinary, registry).succeeded);
    EXPECT_EQ(find(ordinary, low), 0U);
    EXPECT_EQ(find(ordinary, kFixedRaidOpticId), 0U);

    profile.currency = 300U;
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeEconomy(profile, registry, PurchaseCommand{kFixedRaidOpticId},
        {profile.revision, "no-high-supply"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
    const auto buy = executeEconomy(profile, registry, PurchaseCommand{low},
        {profile.revision, "buy-optic"});
    ASSERT_TRUE(buy.succeeded);
    EXPECT_EQ(buy.currencyDelta, -120);
    EXPECT_EQ(profile.currency, 180U);
    const auto optic = find(profile, low);
    ASSERT_NE(optic, 0U);
    const auto bought = profileStateFingerprint(profile);
    EXPECT_FALSE(executeEconomy(profile, registry, RecycleCommand{optic},
        {profile.revision, "no-optic-recycle"}).succeeded);
    EXPECT_FALSE(executeWeaponComponentChange(profile, registry,
        {find(profile, ItemDefinitionId{"item.weapon.rifle_basic"}), WeaponComponentSlot::Optic, optic},
        {profile.revision, "wrong-rifle"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), bought);

    const auto weapon = find(profile, rifle);
    auto expected = effectiveWeaponUse(profile, registry, weapon);
    EXPECT_EQ(expected.optic, WeaponOpticMode::None);
    EXPECT_TRUE(expected.providesOpticMount);
    ASSERT_TRUE(install(profile, low, "optic-no-handguard").succeeded);
    expected.optic = WeaponOpticMode::Low;
    EXPECT_EQ(effectiveWeaponUse(profile, registry, weapon), expected);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(profile.assets.find(optic)->location).weaponAssetId, weapon);
}

TEST(WeaponComponentsTest, FixedOpticSurvivesExtractionSwapSaveAndCapacityRejection)
{
    const auto &registry = content();
    const ItemDefinitionId low{"item.component.optic_1_5x_5_45"};
    auto profile = makeNewPublishedProfile("optic-extract", registry);
    profile.currency = 300U;
    ASSERT_TRUE(executeEconomy(profile, registry, PurchaseCommand{low},
        {profile.revision, "buy-low"}).succeeded);
    const auto lowInstance = find(profile, low);
    const auto weapon = find(profile, rifle);
    ASSERT_TRUE(install(profile, low, "install-low").succeeded);
    const auto backpack = find(profile, ItemDefinitionId{"item.container.backpack_small"});
    ASSERT_TRUE(executeInventory(profile, registry, InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        {profile.revision, "equip-backpack"}).succeeded);
    const auto deployed = executeDeploy(profile, registry,
        {"optic-raid", "optic-extract", 7712201U, MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "optic-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    const auto highInstance = find(profile, kFixedRaidOpticId);
    ASSERT_NE(highInstance, 0U);
    const auto pickedUp = pickupObservedRaidLoot(profile, registry, highInstance, {profile.revision, "pick-optic"});
    ASSERT_TRUE(pickedUp.succeeded) << pickedUp.message;
    ASSERT_TRUE(settlePendingRaid(profile, registry, "optic-extract", RaidResultOutcome::Extracted).succeeded);
    const auto highWater = profile.assets.nextAssetId();
    const auto equipped = executeWeaponComponentChange(profile, registry,
        {weapon, WeaponComponentSlot::Optic, highInstance}, {profile.revision, "install-recovered-high"});
    ASSERT_TRUE(equipped.succeeded) << equipped.message;
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(lowInstance)->location));

    auto loaded = deserializeProfileEnvelope(serializeProfileEnvelope(profile, registry.contentVersion()), registry);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    auto &restored = *loaded.profile;
    EXPECT_EQ(profileStateFingerprint(restored), profileStateFingerprint(profile));
    EXPECT_EQ(effectiveWeaponUse(restored, registry, weapon).optic, WeaponOpticMode::High);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(restored.assets.find(highInstance)->location).weaponAssetId, weapon);

    auto full = restored;
    const auto &filler = registry.item(low);
    while (const auto position = findFirstProfileFit(full, registry, ProfileContainerId::stash(), filler, ItemOrientation::Degrees0))
        static_cast<void>(full.assets.create(filler, StoredAssetLocation{ProfileContainerId::stash(), *position}));
    const auto before = profileStateFingerprint(full);
    const WeaponComponentCommand remove{weapon, WeaponComponentSlot::Optic, 0};
    const auto rejected = executeWeaponComponentChange(full, registry, remove, {full.revision, "full-remove"});
    EXPECT_EQ(rejected.error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(full), before);

    ASSERT_TRUE(executeWeaponComponentChange(restored, registry, remove, {restored.revision, "remove-high"}).succeeded);
    EXPECT_EQ(effectiveWeaponUse(restored, registry, weapon).optic, WeaponOpticMode::None);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(restored.assets.find(highInstance)->location));
    EXPECT_EQ(restored.assets.nextAssetId(), highWater);
}

TEST(WeaponComponentsTest, ReplacingSameSlotReturnsOldIdentityWithoutStackingModifiers)
{
    auto profile = modified();
    const auto old = find(profile, barrel), weapon = find(profile, rifle);
    const auto &definition = content().item(barrel);
    const auto position = findFirstProfileFit(profile, content(), ProfileContainerId::stash(), definition, ItemOrientation::Degrees0);
    ASSERT_TRUE(position);
    const auto replacement = profile.assets.create(definition, StoredAssetLocation{ProfileContainerId::stash(), *position});
    const auto highWater = profile.assets.nextAssetId();
    const auto use = effectiveWeaponUse(profile, content(), weapon);
    const auto plan = queryWeaponComponentChange(profile, content(), {weapon, WeaponComponentSlot::Barrel, replacement});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    ASSERT_EQ(plan.returned.size(), 1U);
    EXPECT_EQ(plan.returned.front().assetId, old);
    ASSERT_TRUE(executeWeaponComponentChange(profile, content(), {weapon, WeaponComponentSlot::Barrel, replacement}, {profile.revision, "replace"}).succeeded);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_EQ(effectiveWeaponUse(profile, content(), weapon), use);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(old)->location));
    EXPECT_TRUE(std::holds_alternative<InstalledWeaponComponentLocation>(profile.assets.find(replacement)->location));
}

TEST(WeaponComponentsTest, InvalidContentCannotPublishComponentCapabilities)
{
    const auto original = nlohmann::json::parse(publishedContentJson());
    for (const std::string fault : {"unknown-platform", "wrong-slot", "bad-stat", "stackable"})
    {
        auto json = original;
        auto it=std::find_if(json["items"].begin(),json["items"].end(),[](const auto &item){return item.contains("weapon_component");});
        ASSERT_NE(it,json["items"].end());
        auto &part = *it;
        ASSERT_TRUE(part.contains("weapon_component"));
        if (fault == "unknown-platform") part["weapon_component"]["weapon"] = "item.weapon.missing";
        if (fault == "wrong-slot") part["weapon_component"]["slot"] = "unknown";
        if (fault == "bad-stat") part["weapon_component"]["accuracy"] = -2147483648;
        if (fault == "stackable") part["max_stack_size"] = 2;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(json.dump())), std::exception) << fault;
    }
}

TEST(WeaponComponentsTest, RaidRuntimeConsumesEffectiveAccuracy)
{
    const auto root = std::filesystem::temp_directory_path() / ("raidline-component-runtime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    float baseline{};
    for (bool upgrade : {false, true})
    {
        auto profile = makeNewPublishedProfile("runtime", content());
        const auto weapon = find(profile, rifle);
        if (upgrade) ASSERT_TRUE(install(profile, barrel, "upgrade").succeeded);
        ASSERT_TRUE(executeInventory(profile, content(), InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon}, {profile.revision, "equip"}).succeeded);
        ASSERT_TRUE(SaveRepository(root).save(profile, content().contentVersion()).succeeded);
        GameSession session;
        session.configurePersistence(root);
        ASSERT_TRUE(session.continueProfile());
        ASSERT_TRUE(session.deployAlpha(123, MapDefinitionId{"map.v0.test"}));
        GameplayInput input;
        const auto position = session.world().player().position();
        input.aimWorldPosition = Vec2{position.x + 400, position.y};
        session.update(input, 0.016F);
        const auto projection = session.world().weaponAccuracyProjection();
        if (!upgrade) baseline = projection.maximumSpreadDegrees;
        else EXPECT_LT(projection.maximumSpreadDegrees, baseline);
    }
}
