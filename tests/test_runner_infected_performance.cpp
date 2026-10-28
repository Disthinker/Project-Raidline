#include "enemy_variant.h"
#include "game_session.h"
#include "noise_decoy_fixture.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

// Keep this accessor identical in test_runner_infected_performance.cpp. It
// stages inputs only; update, shooting, observation and retirement remain real.
struct RunnerInfectedTestAccess
{
    static ProfileState &profile(GameSession &session) { return session.profile_; }
    static void betweenRaids(GameSession &session) { session.state_ = GameSessionState::BetweenRaids; }
    static void replaceWorld(GameSession &session, RaidWorldConfig config)
    { session.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static auto &actors(GameplayWorld &world) { return world.activeEnemies(); }
    static WorldShootingRuntime &shooting(GameplayWorld &world) { return world.shooting_; }
};

namespace
{
using Clock = std::chrono::steady_clock;
constexpr Vec2 center{1216, 800};
bool overlaps(Rect a, Rect b)
{
    return a.position.x < b.position.x + b.size.x && a.position.x + a.size.x > b.position.x &&
        a.position.y < b.position.y + b.size.y && a.position.y + a.size.y > b.position.y;
}
RaidWorldConfig mixedArena(int count)
{
    RaidWorldConfig config;
    config.worldSize = {3000, 1800}; config.playerSpawn = {1200, 784};
    config.extractionPoint = {{2750, 1500}, {100, 100}};
    config.playerMaximumHealth = config.playerCurrentHealth = 100;
    config.deferPlayerDamageResolution = true;
    config.lightConeObservationEnabled = true;
    const auto armor = publishedContentRegistry().enemyCombatDefinition(
        EnemyCombatDefinitionId{"enemy.infected.armored"}).torsoArmor;
    // All actors are in the real perception/navigation workload. Alternating
    // rings keep the population and dense near-player cover at both sizes.
    for (int n = 0; n < count; ++n)
    {
        const float angle = n * 6.28318530718F / count;
        const float radius = 260 + (n % 3) * 35.0F;
        EnemySpawn enemy{{center.x + std::cos(angle) * radius - 10,
            center.y + std::sin(angle) * radius - 10}, {20, 20}, n % 3 == 0 ? 8 : 12};
        if (n % 3 == 0) enemy.variant = runnerEnemyVariantSnapshot();
        else if (n % 3 == 1)
        { enemy.torsoArmor = armor; enemy.variant = ordinaryEnemyVariantSnapshot(true); }
        else enemy.variant = ordinaryEnemyVariantSnapshot();
        config.initialEnemies.push_back(std::move(enemy));
    }
    // Local dense cover, not distant blockers excluded by the spatial query.
    // Keep legal starting bounds and several sight lanes, plus corner detours.
    config.ballisticBlockers = {{1, {{1320, 680}, {2, 85}}},
        {2, {{1320, 765}, {65, 2}}}};
    for (int n = 0; n < 2500 && config.ballisticBlockers.size() < 220; ++n)
    {
        const Rect wall{{866.0F + (n % 35) * 20, 450.0F + (n / 35) * 20}, {5, 8}};
        if (overlaps(wall, {{center.x - 45, center.y - 45}, {90, 90}})) continue;
        if (std::any_of(config.initialEnemies.begin(), config.initialEnemies.end(),
            [&](const auto &enemy) { return overlaps(wall, {enemy.position, enemy.size}); })) continue;
        if (std::any_of(config.ballisticBlockers.begin(), config.ballisticBlockers.end(),
            [&](const auto &blocker) { return overlaps(wall, blocker.bounds); })) continue;
        config.ballisticBlockers.push_back({static_cast<std::uint64_t>(n + 3), wall});
    }
    return config;
}
void queueDeath(GameplayWorld &world, CombatTargetId id)
{
    const auto *enemy = RunnerInfectedTestAccess::actors(world).find(id);
    if (!enemy) throw std::runtime_error("missing mixed performance target");
    const Vec2 p{enemy->position().x + enemy->size().x / 2,
        enemy->position().y + enemy->size().y / 2};
    auto state = RunnerInfectedTestAccess::shooting(world).checkpoint();
    LogicalFlightCheckpoint flight;
    flight.id = state.nextShotId++; flight.origin = flight.position = checkpointPoint(p);
    flight.direction = {1, 0}; flight.impact = {p.x + 100, p.y};
    flight.speed = 6000; flight.extent = 1; flight.maximumDistance = 100;
    flight.damage = 10000; flight.penetration = 1000; flight.tracerLifetime = 0.05F;
    state.flights.push_back(flight);
    if (!RunnerInfectedTestAccess::shooting(world).restoreCheckpoint(state))
        throw std::runtime_error("invalid mixed performance shot");
}
}

TEST(RunnerInfectedPerformanceTest, RealMixedPursuitObservationAndDeathStayWithinThreeMilliseconds)
{
    for (int count : {32, 100})
    {
        SCOPED_TRACE(count);
        GameSession session;
        auto &profile = RunnerInfectedTestAccess::profile(session);
        profile = makeNewPublishedProfile("runner-performance", publishedContentRegistry());
        profile.tutorial = TutorialProgress::Complete;
        equipDecoyTestRig(profile, publishedContentRegistry());
        RunnerInfectedTestAccess::betweenRaids(session);
        ASSERT_TRUE(session.deployAlpha(42, MapDefinitionId{"map.raid.frontier_exchange"}))
            << session.persistenceMessage();
        const auto baseline = session.profile();
        const auto config = mixedArena(count);
        ASSERT_EQ(config.initialEnemies.size(), static_cast<std::size_t>(count));
        ASSERT_EQ(config.ballisticBlockers.size(), 220U);
        ASSERT_GE(std::count_if(config.initialEnemies.begin(), config.initialEnemies.end(),
            [](const auto &spawn) { return isRunnerEnemyVariant(spawn.variant); }), count / 3);
        ASSERT_TRUE(RaidSpaceBlockerIndex::build(config.worldSize, config.ballisticBlockers));
        std::vector<double> samples;
        struct FrameDiagnosis
        {
            double update{}, observation{}, actorQueries{};
            RaidObservationWork work;
            std::uint64_t observationBuilds{}, actorSolves{};
        };
        std::vector<FrameDiagnosis> diagnosis;
        std::size_t observed = 0, fastPursuers = 0, navigating = 0, wallTests = 0;
        for (int iteration = 0; iteration < 160; ++iteration)
        {
            // Index construction and input staging stay outside the timer.
            // Every sample retains the full mixed population and 220 blockers;
            // the measured session frame also consumes one actual death.
            RunnerInfectedTestAccess::profile(session) = baseline;
            RunnerInfectedTestAccess::replaceWorld(session, config);
            auto &world = session.world();
            for (auto &enemy : RunnerInfectedTestAccess::actors(world)) enemy.hearTarget(center);
            const std::array<std::size_t, 3> victimIndices{0U,
                static_cast<std::size_t>(count / 2), static_cast<std::size_t>(count - 1)};
            const auto victim = world.enemies()[victimIndices[iteration % 3]].combatTargetId();
            queueDeath(world, victim);
            GameplayInput input;
            const float heading = iteration * 0.09F;
            input.aimWorldPosition = {center.x + std::cos(heading) * 500,
                center.y + std::sin(heading) * 500};
            input.aimWorldBounds = Rect{{}, config.worldSize};

            const auto start = Clock::now();
            session.update(input, 1.0F / 60);
            const auto updateEnd = Clock::now();
            const auto &view = world.raidObservation();
            const auto observationEnd = Clock::now();
            std::size_t visible = 0;
            for (const auto &enemy : world.enemies())
                visible += world.raidEnemyObservable(enemy.combatTargetId()) ? 1U : 0U;
            const auto actorQueriesEnd = Clock::now();
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (iteration >= 10)
            {
                samples.push_back(elapsed);
                diagnosis.push_back({
                    std::chrono::duration<double, std::milli>(updateEnd - start).count(),
                    std::chrono::duration<double, std::milli>(observationEnd - updateEnd).count(),
                    std::chrono::duration<double, std::milli>(actorQueriesEnd - observationEnd).count(),
                    world.observationWork(), world.indoorObservationBuildCount(),
                    world.enemyObservationSolveCount()});
            }

            ASSERT_EQ(session.state(), GameSessionState::InRaid) << session.persistenceMessage();
            ASSERT_TRUE(view.validGeometry());
            EXPECT_EQ(world.enemies().size(), static_cast<std::size_t>(count - 1));
            EXPECT_FALSE(RunnerInfectedTestAccess::actors(world).hasState(victim));
            EXPECT_GT(world.score(), 0);
            EXPECT_TRUE(world.pendingCorpseDeaths().empty());
            EXPECT_EQ(world.simulationWorkloadLastUpdate().activeBlockers, 220U);
            EXPECT_EQ(world.simulationWorkloadLastUpdate().activeEnemies, static_cast<std::size_t>(count));
            EXPECT_GT(world.simulationWorkloadLastUpdate().enemySubsteps, 0U);
            EXPECT_EQ(session.profile().pendingRaid->enemies, baseline.pendingRaid->enemies);
            EXPECT_EQ(session.profile().pendingRaid->loot, baseline.pendingRaid->loot);
            observed += visible;
            navigating += world.simulationWorkloadLastUpdate().navigationQueries;
            wallTests += world.simulationWorkloadLastUpdate().movementBlockersExamined;
            for (const auto &enemy : world.enemies())
                fastPursuers += isRunnerEnemyVariant(enemy.variant()) &&
                    enemy.attackPhase() == EnemyAttackPhase::Idle && enemy.movementSpeed() > 269 ? 1U : 0U;
        }
        ASSERT_EQ(samples.size(), 150U);
        ASSERT_EQ(diagnosis.size(), samples.size());
        EXPECT_GT(observed, 0U);
        EXPECT_GT(fastPursuers, 0U);
        EXPECT_GT(navigating, 0U);
        EXPECT_GT(wallTests, 0U);
        auto sorted = samples; std::sort(sorted.begin(), sorted.end());
        RecordProperty("runner_" + std::to_string(count) + "_mixed_update_p95_ms", std::to_string(sorted[142]));
        EXPECT_LE(sorted[142], 3.0) << "enemies=" << count << " blockers=220 samples=150";
        if (const char *directory = std::getenv("RAIDLINE_RUNNER_QA_DIR"))
        {
            std::filesystem::create_directories(directory);
            std::ofstream output(std::filesystem::path(directory) / ("runner-" + std::to_string(count) + ".csv"));
            output << "mixed_update_ms,update_ms,observation_ms,actorqueries_ms,local_blockers,rays,blocker_tests,observation_builds,actor_solves\n";
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                const auto &frame = diagnosis[index];
                output << samples[index] << ',' << frame.update << ',' << frame.observation << ','
                    << frame.actorQueries << ',' << frame.work.localBlockers << ',' << frame.work.rays << ','
                    << frame.work.blockerTests << ',' << frame.observationBuilds << ',' << frame.actorSolves << '\n';
            }
        }
    }
}
