#pragma once
#include "game_session.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

// Observation alone is timed: no deployment, AI, clock, or renderer cost is
// hidden in this number. Actor counts are test queries, never production
// spawns.
inline void measureCone(const RaidSpaceBlockerIndex &index,
                        RaidSpaceDefinitionId space, Vec2 origin,
                        RaidLightEnvironment env, const std::string &name)
{
    std::ofstream output;
    if (const char *dir = std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
    {
        std::filesystem::create_directories(dir);
        output.open(std::filesystem::path{dir} / "observation-performance.csv",
                    std::ios::app);
    }
    for (int count : {32, 100})
    {
        std::vector<double> samples;
        RaidObservationWork work;
        volatile std::size_t seen{};
        const auto parameters = raidSightParameters(env);
        for (int frame = 0; frame < 160; ++frame)
        {
            const float a = frame * 0.09F;
            RaidObservationKey key{
                space,
                {origin.x + 3 * std::sin(a), origin.y + 3 * std::cos(a)},
                {std::cos(a), std::sin(a)},
                parameters.angleDegrees,
                parameters.maximumDistance,
                env,
                1};
            const auto start = std::chrono::steady_clock::now();
            const auto view = buildRaidSpaceConeVisibility(index, key, &work);
            // Include construction/validation in the timed region, just as the
            // production world rebuild does. Target count and budget stay
            // fixed.
            const RaidVisibilityBoundsQuery query{view};
            for (int n = 0; n < count; ++n)
            {
                const float angle = n * 6.28318530718F / count;
                const float radius = 100 + (n % 7) * 110.0F;
                const Rect bounds{
                    {key.observer.x + std::cos(angle) * radius - 25,
                     key.observer.y + std::sin(angle) * radius - 25},
                    {50, 50}};
                seen = seen + (query.boundsVisible(bounds) ? 1 : 0);
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - start)
                                     .count();
            if (frame >= 10)
                samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        const auto p95 =
            samples[static_cast<std::size_t>(std::ceil(samples.size() * 0.95)) -
                    1];
        if (output)
            output << name << ',' << count << ',' << samples.size() << ','
                   << p95 << ',' << samples.back() << ','
                   << index.blockerCount() << ',' << work.localBlockers << ','
                   << work.rays << ',' << work.blockerTests << '\n';
        testing::Test::RecordProperty(
            (name + "_" + std::to_string(count) + "_p95_ms").c_str(),
            std::to_string(p95));
        EXPECT_LE(p95, 3.0)
            << name << " observation P95 for " << count << " targets";
    }
}
