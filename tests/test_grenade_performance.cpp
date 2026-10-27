#include "alpha_content_ids.h"
#include "game_session.h"
#include "noise_decoy_fixture.h"
#include "raid_grenade_runtime.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// Keep this small accessor identical in test_grenade_session.cpp. Tests
// stage legal deployments; production services still own every mutation tested.
struct GrenadeTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }
    static void betweenRaids(GameSession &session) { session.state_ = GameSessionState::BetweenRaids; }
    static void replaceWorld(GameSession &session, RaidWorldConfig config)
    { session.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static EnemyLifecycle &actors(GameplayWorld &world) { return world.activeEnemies(); }
};

namespace
{
const ItemDefinitionId kGrenade{"item.throwable.frag_grenade_basic"};
constexpr Vec2 kBlast{500, 500};

bool overlaps(Rect left, Rect right)
{
    return left.position.x < right.position.x + right.size.x &&
        left.position.x + left.size.x > right.position.x &&
        left.position.y < right.position.y + right.size.y &&
        left.position.y + left.size.y > right.position.y;
}

AssetInstanceId stagePerformanceRaid(GameSession &session)
{
    const auto &content = publishedContentRegistry();
    auto &profile = GrenadeTestAccess::profile(session);
    profile = makeNewPublishedProfile("grenade-performance", content);
    profile.tutorial = TutorialProgress::Complete;
    equipDecoyTestRig(profile, content);
    const auto rig = equippedAsset(profile, EquipmentSlotKind::ChestRig);
    if (!rig) return 0;
    const auto &pockets = content.item(profile.assets.find(*rig)->definitionId).containerCompartments;
    std::optional<ProfileContainerId> destination;
    for (std::uint32_t index = 0; index < pockets.size(); ++index)
        if (pockets[index].pocketKind == ContainerPocketKind::General)
        { destination = ProfileContainerId::compartment(*rig, index); break; }
    if (!destination) return 0;
    const auto &definition = content.item(kGrenade);
    const auto fit = findFirstProfileFit(profile, content, *destination, definition, ItemOrientation::Degrees0);
    if (!fit) return 0;
    const auto id = profile.assets.create(definition, StoredAssetLocation{*destination, *fit}, 2);
    GrenadeTestAccess::betweenRaids(session);
    EXPECT_TRUE(session.deployAlpha(42, MapDefinitionId{"map.raid.frontier_exchange"}))
        << session.persistenceMessage();
    return id;
}

RaidWorldConfig performanceArena(int count, const std::vector<std::string> &corpseSources)
{
    RaidWorldConfig config;
    config.worldSize = {2000, 1200};
    config.playerSpawn = {384, 484}; // Center is 100 units from the blast: real profile self damage.
    config.playerMaximumHealth = config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.extractionPoint = {{1800, 1000}, {100, 100}};
    config.noiseDecoyEnabled = config.grenadeEnabled = true;
    config.lightConeObservationEnabled = true;
    const auto armor = publishedContentRegistry().enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    const int columns = count == 32 ? 8 : 10;
    const int rows = (count + columns - 1) / columns;
    for (int index = 0; index < count; ++index)
    {
        const Vec2 center{kBlast.x + (index % columns - (columns - 1) / 2.0F) * 16,
            kBlast.y + (index / columns - (rows - 1) / 2.0F) * 16};
        EnemySpawn enemy{{center.x - 4, center.y - 4}, {8, 8}, index >= 3 && index % 3 == 0 ? 100 : 12};
        if (index >= 3 && index % 3 == 0) enemy.torsoArmor = armor;
        if (static_cast<std::size_t>(index) < corpseSources.size())
            enemy.sourceEnemyId = corpseSources[static_cast<std::size_t>(index)];
        config.initialEnemies.push_back(std::move(enemy));
    }
    // A real separating wall inside the damage radius, between enemy columns.
    config.ballisticBlockers.push_back({1, {{500, 508}, {2, 88}}});
    for (int index = 0; index < 2500 && config.ballisticBlockers.size() < 220; ++index)
    {
        const Rect wall{{402.0F + (index % 34) * 6, 402.0F + (index / 34) * 6}, {3, 3}};
        // Preserve the actual throw ray, explosion point, player bounds, and
        // the upper-left corpse targets. The other quadrants retain dense cover.
        if ((wall.position.x < 500 && wall.position.y < 500) ||
            overlaps(wall, {{396, 496}, {109, 8}}) ||
            overlaps(wall, {{384, 484}, {32, 32}})) continue;
        if (std::any_of(config.initialEnemies.begin(), config.initialEnemies.end(),
            [&](const EnemySpawn &enemy) { return overlaps(wall, {enemy.position, enemy.size}); })) continue;
        config.ballisticBlockers.push_back({static_cast<std::uint64_t>(index + 2), wall});
    }
    return config;
}
}

TEST(GrenadePerformanceTest, RealSessionExplosionWithArmorCorpsesAndDenseCoverStaysWithinThreeMilliseconds)
{
    using Clock = std::chrono::steady_clock;
    for (int count : {32, 100})
    {
        SCOPED_TRACE(count);
        GameSession session;
        const auto id = stagePerformanceRaid(session);
        ASSERT_NE(id, 0U);
        ASSERT_TRUE(session.profile().pendingRaid);
        std::vector<std::string> corpseSources;
        for (const auto &[assetId, asset] : session.profile().assets.records())
        {
            static_cast<void>(assetId);
            if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
                corpseSources.push_back(corpse->sourceEnemyId);
        }
        ASSERT_EQ(corpseSources.size(), 3U);
        const auto baseline = session.profile();
        const auto config = performanceArena(count, corpseSources);
        ASSERT_EQ(config.initialEnemies.size(), static_cast<std::size_t>(count));
        ASSERT_EQ(config.ballisticBlockers.size(), 220U);
        const auto geometry = RaidSpaceBlockerIndex::build(config.worldSize, config.ballisticBlockers);
        ASSERT_TRUE(geometry);
        std::size_t blocked = 0, unblocked = 0, wallTests = 0;
        for (const auto &enemy : config.initialEnemies)
        {
            const Vec2 center{enemy.position.x + enemy.size.x / 2, enemy.position.y + enemy.size.y / 2};
            ASSERT_LT(std::hypot(center.x - kBlast.x, center.y - kBlast.y), 160);
            if (geometry->hasLineOfSight(kBlast, center, &wallTests)) ++unblocked;
            else ++blocked;
        }
        ASSERT_GT(blocked, 0U);
        ASSERT_GT(unblocked, 3U);
        ASSERT_GT(wallTests, 0U);

        std::vector<double> samples;
        for (int iteration = 0; iteration < 160; ++iteration)
        {
            // Restore legal frozen assets and construct spatial indexes outside
            // the timer. The measured frame performs all production consumers.
            GrenadeTestAccess::profile(session) = baseline;
            GrenadeTestAccess::replaceWorld(session, config);
            ASSERT_TRUE(session.throwThrowable(id, kBlast,
                {session.profile().revision, "grenade-perf-" + std::to_string(iteration)}).succeeded);
            auto runtime = session.world().grenade();
            runtime.advance(outdoorRaidSpaceId(), 1.99F);
            ASSERT_TRUE(runtime.fuse());
            ASSERT_FALSE(runtime.pendingExplosion());
            session.world().commitGrenade(std::move(runtime));

            const auto start = Clock::now();
            session.update({}, 1.0F / 60.0F);
            const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (iteration >= 10) samples.push_back(milliseconds);

            ASSERT_EQ(session.state(), GameSessionState::InRaid) << session.persistenceMessage();
            ASSERT_TRUE(session.world().lastGrenadeExplosion());
            EXPECT_FALSE(session.world().grenade().active());
            EXPECT_LT(session.profile().currentHealth, 100);
            EXPECT_EQ(session.world().player().health(), session.profile().currentHealth);
            EXPECT_GT(session.world().score(), 0);
            EXPECT_LT(session.world().enemies().size(), static_cast<std::size_t>(count));
            EXPECT_FALSE(session.world().enemies().empty());
            EXPECT_EQ(session.world().simulationWorkloadLastUpdate().activeBlockers, 220U);
            EXPECT_TRUE(session.world().pendingCorpseDeaths().empty());
            std::size_t exposed = 0, damagedArmor = 0;
            for (const auto &[assetId, asset] : session.profile().assets.records())
            {
                static_cast<void>(assetId);
                if (const auto *corpse = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location))
                    if (corpse->deathPosition) ++exposed;
            }
            for (const auto &enemy : session.world().enemies())
                if (enemy.torsoArmor() && enemy.torsoArmor()->durability <
                    publishedContentRegistry().enemyCombatDefinition(
                        EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor->durability)
                    ++damagedArmor;
            EXPECT_EQ(exposed, 3U);
            EXPECT_GT(damagedArmor, 0U);
            EXPECT_EQ(session.profile().pendingRaid->loot, baseline.pendingRaid->loot);
            EXPECT_EQ(session.profile().pendingRaid->enemies, baseline.pendingRaid->enemies);
        }
        ASSERT_EQ(samples.size(), 150U);
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        RecordProperty("grenade_" + std::to_string(count) + "_explosion_p95_ms", std::to_string(sorted[142]));
        EXPECT_LE(sorted[142], 3.0) << "enemies=" << count << " blockers=220 samples=150";
        if (const char *directory = std::getenv("RAIDLINE_GRENADE_QA_DIR"))
        {
            std::filesystem::create_directories(directory);
            std::ofstream output(std::filesystem::path(directory) / ("grenade-" + std::to_string(count) + ".csv"));
            output << "explosion_ms\n";
            for (double sample : samples) output << sample << '\n';
        }
    }
}
