#include "weapon_handling_fixture.h"
#include "enemy_variant.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

namespace
{
constexpr Vec2 playerCenter{1216, 800};
bool handlingOverlap(Rect a, Rect b)
{
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
        a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
RaidWorldConfig handlingMixedArena(int count)
{
    auto config = handlingArena();
    const auto armor = publishedContentRegistry().enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    for (int i = 0; i != count; ++i)
    {
        const float angle = static_cast<float>(i) * 6.28318530718F / static_cast<float>(count);
        const float radius = 260 + (i % 3) * 35.0F;
        EnemySpawn enemy{{playerCenter.x + std::cos(angle) * radius - 10,
            playerCenter.y + std::sin(angle) * radius - 10}, {20, 20}, i % 3 == 0 ? 8 : 12};
        if (i % 3 == 0) enemy.variant = runnerEnemyVariantSnapshot();
        else if (i % 3 == 1) { enemy.torsoArmor = armor; enemy.variant = ordinaryEnemyVariantSnapshot(true); }
        else enemy.variant = ordinaryEnemyVariantSnapshot();
        config.initialEnemies.push_back(enemy);
    }
    config.ballisticBlockers = {{1, {{1320, 680}, {2, 85}}}, {2, {{1320, 765}, {65, 2}}}};
    for (int i = 0; i < 2500 && config.ballisticBlockers.size() < 220; ++i)
    {
        Rect wall{{866.0F + (i % 35) * 20, 450.0F + (i / 35) * 20}, {5, 8}};
        if (handlingOverlap(wall, {{playerCenter.x - 45, playerCenter.y - 45}, {90, 90}})) continue;
        if (std::any_of(config.initialEnemies.begin(), config.initialEnemies.end(),
            [&](const auto &enemy) { return handlingOverlap(wall, {enemy.position, enemy.size}); })) continue;
        if (std::any_of(config.ballisticBlockers.begin(), config.ballisticBlockers.end(),
            [&](const auto &blocker) { return handlingOverlap(wall, blocker.bounds); })) continue;
        config.ballisticBlockers.push_back({static_cast<std::uint64_t>(i + 3), wall});
    }
    return config;
}
}

TEST(WeaponHandlingPerformanceTest, RealShootingReloadStagesAndDroppedPickupStayWithinThreeMilliseconds)
{
    using Clock = std::chrono::steady_clock;
    for (int count : {32, 100})
    {
        SCOPED_TRACE(count);
        GameSession session; const auto ids = prepareHandlingSession(session);
        auto baseline = session.profile();
        // A chamber step is real work in both routes, rather than a no-op.
        baseline.assets.findMutable(ids.weapon)->chamberedRound.reset();
        const auto config = handlingMixedArena(count);
        ASSERT_EQ(config.initialEnemies.size(), static_cast<std::size_t>(count));
        ASSERT_EQ(config.ballisticBlockers.size(), 220U);
        for (const auto &spawn : config.initialEnemies)
            ASSERT_TRUE(spawn.variant && validEnemyVariantVitals(
                *spawn.variant, spawn.maxHealth, spawn.torsoArmor));
        std::vector<double> samples; std::array<unsigned, 11> covered{};
        std::size_t observed{}, shots{}, droppedQueries{}, picked{};
        for (int iteration = 0; iteration != 160; ++iteration)
        {
            WeaponHandlingTestAccess::reset(session, baseline, config);
            for (auto &enemy : WeaponHandlingTestAccess::actors(session.world())) enemy.hearTarget(playerCenter);
            const unsigned scenario = static_cast<unsigned>(iteration % 11);
            GameplayInput input;
            input.aimWorldPosition = Vec2{playerCenter.x + 500, playerCenter.y};
            input.aimWorldBounds = Rect{{}, config.worldSize};
            if (scenario == 0)
            {
                // Supply the pre-existing real chamber for a true fired shot.
                WeaponHandlingTestAccess::profile(session).assets.findMutable(ids.weapon)->chamberedRound =
                    MagazineRoundRecord{alpha_content::ammunition, std::nullopt};
                input.firePressed = input.fireJustPressed = true;
            }
            else
            {
                ASSERT_TRUE(session.startAlphaReload(ids.weapon, ids.spareMagazine));
                const bool urgent = scenario >= 6;
                if (urgent) ASSERT_TRUE(session.upgradeAlphaReloadEmergency(ids.weapon));
                constexpr std::array<float, 10> deadlines{0.2F, 0.6F, 1.2F, 1.6F, 2.0F,
                    0.2F, 0.35F, 0.8F, 1.2F, 0.35F};
                const float target = deadlines[scenario - 1];
                if (scenario == 10)
                {
                    ASSERT_TRUE(WeaponHandlingTestAccess::stages(session, target));
                    GameplayInput cancel; cancel.inventoryOpen = true;
                    // Cancel only the already-taken new magazine, before the measured pickup frame.
                    session.update(cancel, 0);
                    input.interactJustPressed = true;
                }
                else
                {
                    ASSERT_TRUE(WeaponHandlingTestAccess::stages(session, target - 1.0F / 120.0F));
                }
            }
            const auto revision = session.profile().revision;
            const auto rounds = magazineRoundCount(session.profile(), ids.oldMagazine);
            const auto start = Clock::now();
            session.update(input, 1.0F / 60.0F);
            const auto &view = session.world().raidObservation();
            for (const auto &enemy : session.world().enemies())
                observed += session.world().raidEnemyObservable(enemy.combatTargetId()) ? 1U : 0U;
            const auto drops = session.droppedRaidMagazineIds();
            droppedQueries += drops.size();
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (iteration >= 10) { samples.push_back(elapsed); ++covered[scenario]; }
            ASSERT_EQ(session.state(), GameSessionState::InRaid) << session.persistenceMessage();
            ASSERT_TRUE(view.validGeometry());
            // Workload records the full population BEFORE shot resolution.
            // A real shot may retire its target; do not make it invulnerable
            // merely to keep the post-resolution roster at its initial size.
            EXPECT_EQ(session.world().simulationWorkloadLastUpdate().activeEnemies, static_cast<std::size_t>(count));
            EXPECT_EQ(session.world().simulationWorkloadLastUpdate().activeBlockers, 220U);
            if (scenario == 0)
            {
                ASSERT_TRUE(session.world().shotFiredLastUpdate()); ++shots;
                EXPECT_EQ(magazineRoundCount(session.profile(), ids.oldMagazine), rounds - 1U);
                EXPECT_GE(session.world().enemies().size(), static_cast<std::size_t>(count - 1));
                EXPECT_LE(session.world().enemies().size(), static_cast<std::size_t>(count));
                if (session.world().enemies().size() < static_cast<std::size_t>(count))
                    EXPECT_GT(session.world().score(), 0);
            }
            else if (scenario == 10)
            {
                ASSERT_TRUE(assetIsCarried(session.profile(), ids.oldMagazine)); ++picked;
                EXPECT_TRUE(drops.empty());
            }
            else EXPECT_GT(session.profile().revision, revision); // Real stage transaction committed.
            if (scenario != 0)
                EXPECT_EQ(session.world().enemies().size(), static_cast<std::size_t>(count));
        }
        ASSERT_EQ(samples.size(), 150U);
        for (auto coverage : covered) EXPECT_GT(coverage, 0U);
        EXPECT_GT(shots, 0U); EXPECT_GT(picked, 0U); EXPECT_GT(droppedQueries, 0U); EXPECT_GT(observed, 0U);
        std::sort(samples.begin(), samples.end());
        std::cout << "weapon_handling enemies=" << count
            << " blockers=220 p95_ms=" << samples[142]
            << " samples=" << samples.size() << " p95_index=142"
            << " shots=" << shots << " picked=" << picked
            << " droppedQueries=" << droppedQueries
            << " counters_include_warmup=true coverage=";
        for (std::size_t scenario = 0; scenario < covered.size(); ++scenario)
            std::cout << (scenario ? "," : "") << scenario << ':' << covered[scenario];
        std::cout << '\n';
        RecordProperty("weapon_handling_" + std::to_string(count) + "_p95_ms", std::to_string(samples[142]));
        EXPECT_LT(samples[142], 3.0) << "enemies=" << count << " blockers=220 samples=150 p95_index=142";
    }
}
