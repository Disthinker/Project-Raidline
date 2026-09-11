#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "alpha_content_ids.h"
#include "base_defense_checkpoint_writer.h"
#include "base_defense_runtime.h"
#include "base_defense_ownership.h"
#include "base_siege_domain.h"
#include "inventory_domain.h"
#include "medical_domain.h"
#include "profile_combat_domain.h"
#include "weapon_ammo_domain.h"

namespace
{
using Clock = std::chrono::steady_clock;

double elapsedMilliseconds(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>{Clock::now() - start}.count();
}

double percentile(std::vector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    return values[std::min(
        values.size() - 1U,
        static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U)))];
}

AssetInstanceId findDefinition(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition)
            return id;
    return 0U;
}

struct PerformanceSaveDirectory
{
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("raidline-defense-performance-" + std::to_string(Clock::now().time_since_epoch().count()));
    ~PerformanceSaveDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void runDefensePerformance(bool fortified)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("defense-performance", content);
    profile.baseSiege.safeUntilWorldMinute = profile.worldClock.elapsedWorldMinutes;
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold;
    ASSERT_TRUE(activateBaseSiegeWarningIfEligible(profile));
    const auto rifle = findDefinition(profile, alpha_content::rifle);
    ASSERT_NE(rifle, 0U);
    ASSERT_TRUE(executeInventory(profile, content,
                                 InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
                                 {profile.revision, "perf-equip"})
                    .succeeded);

    std::vector<AssetInstanceId> magazines;
    for (int index = 0; index < 5; ++index)
    {
        const auto &definition = content.item(alpha_content::magazine);
        const auto cell = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
                                              definition, ItemOrientation::Degrees0);
        ASSERT_TRUE(cell);
        const auto id = profile.assets.create(
            definition, StoredAssetLocation{ProfileContainerId::stash(), *cell});
        profile.assets.findMutable(id)->magazineRounds.assign(
            definition.magazineCapacity, {alpha_content::ammunition, std::nullopt});
        magazines.push_back(id);
    }
    ASSERT_TRUE(executeWeaponAmmo(profile, content,
                                  InstallMagazineAndChamberCommand{rifle, magazines.front()},
                                  {profile.revision, "perf-load"})
                    .succeeded);
    std::size_t nextMagazine = 1;

    const auto &ammo = content.item(alpha_content::ammunition);
    const auto firstCell = findFirstProfileFit(profile, content, ProfileContainerId::stash(), ammo,
                                               ItemOrientation::Degrees0);
    ASSERT_TRUE(firstCell);
    const auto movingAsset = profile.assets.create(
        ammo, StoredAssetLocation{ProfileContainerId::stash(), *firstCell}, 1);
    const auto secondCell = findFirstProfileFit(profile, content, ProfileContainerId::stash(), ammo,
                                                ItemOrientation::Degrees0);
    ASSERT_TRUE(secondCell);
    std::vector<AssetInstanceId> medkits;
    for (int index = 0; index < 10; ++index)
        medkits.push_back(profile.assets.create(
            content.item(alpha_content::medkit),
            BaseGroundAssetLocation{
                RegionalBaseSiteDefinitionId{"regional_base_site.greyline_yard"},
                {5500.0F + static_cast<float>(index), 5100.0F}}));
    while (profile.assets.records().size() < 1000U)
    {
        const float offset = static_cast<float>(profile.assets.records().size());
        static_cast<void>(
            profile.assets.create(ammo,
                                  BaseGroundAssetLocation{RegionalBaseSiteDefinitionId{
                                                              "regional_base_site.greyline_yard"},
                                                          {5000.0F + offset, 5000.0F}},
                                  1));
    }

    std::vector<BallisticBlocker> blockers;
    for (std::uint64_t index = 0; index < 1000U; ++index)
        blockers.push_back({index + 1U,
                            {{20.0F + static_cast<float>(index % 40U) * 45.0F,
                              20.0F + static_cast<float>(index / 40U) * 45.0F},
                             {24.0F, 24.0F}}});
    BaseDefenseSnapshot inputs;
    inputs.eventId = baseSiegeEventId(profile);
    inputs.siegeSequence = profile.baseSiege.siegeSequence;
    inputs.siteDefinitionId = "regional_base_site.greyline_yard";
    inputs.layoutIdentity = "defense-performance-frozen-layout";
    inputs.worldSize = {8000.0F, 8000.0F};
    inputs.safeCore = {{3000.0F, 3000.0F}, {1000.0F, 1000.0F}};
    inputs.seed = 81443U;
    inputs.playerPosition = {3500.0F, 3500.0F};
    inputs.frozenPopulation = profile.basePopulation.ordinaryResidents;
    inputs.frozenMoraleTier = static_cast<std::uint32_t>(profile.baseMorale.tier);
    inputs.frozenSiteThreat =
        content.regionalBaseSite(RegionalBaseSiteDefinitionId{inputs.siteDefinitionId})
            .dailyBaseThreatUnits;
    if (fortified)
    {
        inputs.rulesVersion = kFortifiedBaseDefenseRulesVersion;
        for (const auto &b : blockers) inputs.movementBlockers.push_back(b.bounds);
        for (std::uint32_t side = 0; side < 4; ++side)
        {
            const FortificationInstanceId id{side + 1U};
            const DefenseSlotKey slot{RegionalBaseSiteDefinitionId{inputs.siteDefinitionId}, "",
                                      static_cast<DefenseSide>(side)};
            const Rect footprint = side < 2
                ? Rect{{side == 0 ? 2760.0F : 4240.0F, 3420}, {48, 160}}
                : Rect{{3420, side == 2 ? 2760.0F : 4240.0F}, {160, 48}};
            inputs.fortifications.push_back({id, kWoodBarricadeDefinition, slot, footprint, 120, 120, 120});
            profile.baseFortifications.instances.emplace(id, FortificationRecord{kWoodBarricadeDefinition, 120, slot});
        }
        profile.baseFortifications.nextInstanceId = 5;
    }
    auto prepared = BaseDefenseRuntime::prepare(inputs, blockers, publishedContentRegistry().enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    ASSERT_TRUE(prepared);
    if (fortified) profile.baseDefenseWarning = BaseDefenseWarningSnapshot{inputs.eventId, *prepared};
    ASSERT_TRUE(executeBaseRealtimeDefenseStart(profile, content, *prepared,
                                                {profile.revision, "perf-defense-start"})
                    .succeeded);

    // Resume a valid mid-wave checkpoint at the admitted concurrency cap.
    // This avoids counting several seconds of empty spawn warmup as load.
    auto checkpoint = *prepared;
    checkpoint.elapsedSeconds = checkpoint.wavePlans[1].releaseSeconds;
    checkpoint.currentWave = 1U;
    checkpoint.spawnedEnemyCount = 16U;
    const Vec2 entry = checkpoint.wavePlans.front().entry;
    const Vec2 target = checkpoint.wavePlans.front().target;
    const float length = std::hypot(entry.x - target.x, entry.y - target.y);
    const Vec2 outward{(entry.x - target.x) / length, (entry.y - target.y) / length};
    const Vec2 player{entry.x + outward.x * 330.0F, entry.y + outward.y * 330.0F};
    checkpoint.playerPosition = player;
    std::size_t actorIndex{};
    for (const auto &wave : checkpoint.wavePlans)
    {
        for (const auto id : wave.enemyIds)
        {
            if (actorIndex >= 16U)
                break;
            const Vec2 position{entry.x + static_cast<float>(actorIndex % 4U) * 60.0F - 100.0F,
                                entry.y + static_cast<float>(actorIndex / 4U) * 60.0F - 100.0F};
            checkpoint.enemies.push_back(Enemy{position, {32.0F, 48.0F}, {}, wave.enemyMaxHealth, id}.checkpoint());
            ++actorIndex;
        }
    }
    WorldShootingRuntime shooting;
    shooting.configureWeapon(*content.item(alpha_content::rifle).weaponUse);
    checkpoint.shooting = shooting.checkpoint();
    profile.activeBaseDefense = checkpoint;
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    BaseDefenseRuntime runtime;
    ASSERT_TRUE(runtime.resume(checkpoint, blockers));
    ASSERT_EQ(runtime.enemies().size(), 16U);

    PerformanceSaveDirectory directory;
    SaveRepository repository{directory.path};
    BaseDefenseCheckpointWriter writer{repository};
    std::vector<double> frames, copies, simulation, profileClones;
    frames.reserve(360U);
    simulation.reserve(360U);
    copies.reserve(120U);
    std::size_t shots{}, inventoryMoves{}, damageEvents{}, medicalUses{}, nextMedkit{};
    bool alternate{};
    constexpr float deltaTime = 1.0F / 60.0F;
    for (int frame = 0; frame < 360; ++frame)
    {
        const auto frameStart = Clock::now();
        GameplayInput input;
        input.firePressed = true;
        input.aimWorldPosition =
            Vec2{player.x + outward.x * 1400.0F, player.y + outward.y * 1400.0F};
        const auto ready = queryFireWeapon(profile, content, FireWeaponCommand{rifle});
        if (ready.result == WeaponAmmoResult::Dry)
        {
            ASSERT_LT(nextMagazine, magazines.size());
            ASSERT_TRUE(executeWeaponAmmo(
                            profile, content,
                            InstallMagazineAndChamberCommand{rifle, magazines[nextMagazine++]},
                            {profile.revision, "perf-reload-" + std::to_string(frame)})
                            .succeeded);
        }
        shooting.beginFrame(deltaTime);
        shooting.updateAim(input, {player.x + 16.0F, player.y + 24.0F}, outward,
                           checkpoint.worldSize, deltaTime);
        runtime.advance(input, deltaTime, player, {32.0F, 48.0F}, false, shooting, blockers);
        simulation.push_back(runtime.metrics().updateMilliseconds);
        if (shooting.shotFiredLastUpdate())
        {
            const auto fired =
                executeFireWeapon(profile, content, FireWeaponCommand{rifle},
                                  {profile.revision, "perf-fire-" + std::to_string(frame)});
            ASSERT_TRUE(fired.succeeded) << fired.message;
            ASSERT_EQ(fired.result, WeaponAmmoResult::Fired);
            ++shots;
        }
        if (runtime.damageLastUpdate() > 0)
        {
            const auto hit = executeIncomingDamageInSimulation(
                profile, content, {runtime.damageLastUpdate(), HitRegion::Torso, 0, 1, false},
                {profile.revision, "perf-hit-" + std::to_string(frame)});
            ASSERT_TRUE(hit.succeeded) << hit.message;
            ++damageEvents;
        }
        ASSERT_GT(profile.currentHealth, 0);
        if (profile.currentHealth <= 82)
        {
            while (nextMedkit < medkits.size() && !profile.assets.find(medkits[nextMedkit]))
                ++nextMedkit;
            ASSERT_LT(nextMedkit, medkits.size());
            const auto healed =
                executeMedicalUse(profile, content, medkits[nextMedkit], MedicalAccess::AnyOwned,
                                  {profile.revision, "perf-heal-" + std::to_string(frame)});
            ASSERT_TRUE(healed.succeeded) << healed.message;
            ++medicalUses;
        }
        const InventoryMoveCommand move{
            movingAsset, 0, {ProfileContainerId::stash(), alternate ? *firstCell : *secondCell}};
        ASSERT_TRUE(queryInventory(profile, content, move).canCommit);
        if (frame % 30 == 0)
        {
            ASSERT_TRUE(
                executeInventory(profile, content, move,
                                 {profile.revision, "perf-inventory-" + std::to_string(frame)})
                    .succeeded);
            alternate = !alternate;
            ++inventoryMoves;
        }
        // Stress more frequently than the production 250ms schedule. Copy
        // percentiles include the runtime DTO assembly, not just queue admission.
        if (frame % 3 == 0)
        {
            const auto copyStart = Clock::now();
            if (fortified)
            {
                ASSERT_TRUE(captureOwnedDefenseCheckpoint(profile, runtime.checkpoint(shooting), content));
            }
            else
                profile.activeBaseDefense = runtime.checkpoint(shooting);
            static_cast<void>(writer.request(profile, content.contentVersion()));
            copies.push_back(elapsedMilliseconds(copyStart));
            profileClones.push_back(writer.snapshot().lastCopyMilliseconds);
        }
        frames.push_back(elapsedMilliseconds(frameStart));
    }
    if (fortified)
    {
        ASSERT_TRUE(captureOwnedDefenseCheckpoint(profile, runtime.checkpoint(shooting), content));
    }
    else
        profile.activeBaseDefense = runtime.checkpoint(shooting);
    static_cast<void>(writer.request(profile, content.contentVersion()));
    const auto durable = writer.flush();
    ASSERT_TRUE(durable.succeeded) << durable.message;
    const auto loaded = repository.load(content);
    ASSERT_TRUE(loaded.profile) << loaded.message;
    EXPECT_EQ(profileStateFingerprint(profile), profileStateFingerprint(*loaded.profile));
    EXPECT_GE(shots, 20U);
    EXPECT_GE(inventoryMoves, 10U);
    EXPECT_GE(damageEvents, 4U);
    EXPECT_GT(medicalUses, 0U);
    const double copyP95 = percentile(copies, 0.95);
    const double copyP99 = percentile(copies, 0.99);
    const double slowestSimulation = *std::max_element(simulation.begin(), simulation.end());
    std::cout << "Base defense stress: 16 enemies, 1000 blockers/assets; shots=" << shots
              << " inventory=" << inventoryMoves << " damage=" << damageEvents
              << " medical=" << medicalUses << "; checkpoint copy P95/P99=" << copyP95 << '/'
              << copyP99 << "ms (Profile clone P95=" << percentile(profileClones, 0.95) << ')'
              << "ms; sim max=" << slowestSimulation
              << "ms; main-iteration (no SDL render) P95/P99/max=" << percentile(frames, 0.95)
              << '/' << percentile(frames, 0.99) << '/'
              << *std::max_element(frames.begin(), frames.end())
              << "ms; coalesced=" << writer.snapshot().coalescedRequests << '\n';
    EXPECT_LT(copyP95, 4.0);
    EXPECT_LT(copyP99, 8.0);
    EXPECT_LT(slowestSimulation, 25.0);
}

TEST(BaseDefensePerformanceTest, SixteenEnemiesThousandAssetsAndBlockersWithFireInventoryAndSave)
{
    runDefensePerformance(false);
}

TEST(BaseDefensePerformanceTest, FortifiedWarningAndOwnerWritebackKeepOriginalCheckpointBudgets)
{
    runDefensePerformance(true);
}
} // namespace
