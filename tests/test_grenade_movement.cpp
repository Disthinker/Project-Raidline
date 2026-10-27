#include "collision.h"
#include "gameplay_world.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
struct MovementCase
{
    const char *name;
    Vec2 start;
    int horizontal{}, vertical{};
    float seconds{0.5F};
};

std::vector<BallisticBlocker> denseWalls()
{
    // Deliberately start with a farther wall before the nearer one. The
    // reference always traverses the original content order on each axis.
    std::vector<BallisticBlocker> walls{
        {1, {{360, 220}, {4, 180}}},
        {2, {{320, 220}, {4, 180}}},
        {3, {{260, 340}, {160, 4}}},
        {4, {{0, 96}, {128, 4}}},
        {5, {{96, 0}, {4, 128}}}};
    // Every small wall shares one 128-unit cell. Most are irrelevant to an
    // individual actor sweep, while still belonging to its broad-phase bucket.
    for (std::uint64_t i = 0; i < 100; ++i)
        walls.push_back({i + 6,
            {{334 + static_cast<float>(i % 10) * 5,
                 294 + static_cast<float>(i / 10) * 5}, {3, 3}}});
    return walls;
}

GameplayInput moveInput(const MovementCase &test)
{
    GameplayInput input;
    input.moveLeft = test.horizontal < 0;
    input.moveRight = test.horizontal > 0;
    input.moveUp = test.vertical < 0;
    input.moveDown = test.vertical > 0;
    return input;
}

Vec2 fullScanPosition(Vec2 start, float size, Vec2 requested,
    const std::vector<BallisticBlocker> &walls)
{
    Rect body{start, {size, size}};
    float x = requested.x;
    for (const auto &wall : walls)
        x = resolveHorizontalCollision(body, x, wall.bounds);
    body.position.x = x;
    float y = requested.y;
    for (const auto &wall : walls)
        y = resolveVerticalCollision(body, y, wall.bounds);
    return {x, y};
}

void checkWorldMovement(const MovementCase &test, const std::vector<BallisticBlocker> &walls)
{
    SCOPED_TRACE(test.name);
    RaidWorldConfig config;
    config.worldSize = {768, 640};
    config.playerSpawn = test.start;
    config.extractionPoint = {{720, 600}, {16, 16}};
    config.ballisticBlockers = walls;
    GameplayWorld world{config};
    ASSERT_TRUE(world.enemies().empty());

    // Use the production Player's current speed, dimensions, normalization,
    // timers and world clamps. Do not recreate movement arithmetic in the test.
    Player independent{test.start.x, test.start.y,
        config.playerMaximumHealth, config.playerCurrentHealth};
    const auto input = moveInput(test);
    independent.update(input, test.seconds, config.worldSize.x, config.worldSize.y);
    const auto requested = independent.position();
    const auto expected = fullScanPosition(test.start, independent.size(), requested, walls);

    // The metric remains the conservative bucket count before any movement
    // narrow phase, including when there is no requested displacement.
    const auto index = RaidSpaceBlockerIndex::build(config.worldSize, walls);
    ASSERT_TRUE(index);
    const Vec2 minimum{std::min(test.start.x, requested.x), std::min(test.start.y, requested.y)};
    const Vec2 maximum{std::max(test.start.x + independent.size(), requested.x + independent.size()),
        std::max(test.start.y + independent.size(), requested.y + independent.size())};
    std::vector<std::size_t> originalCandidates;
    index->queryCandidateIndices({minimum, {maximum.x - minimum.x, maximum.y - minimum.y}}, originalCandidates);
    if (test.horizontal == 0 && test.vertical == 0) ASSERT_GT(originalCandidates.size(), 0U);

    world.update(input, test.seconds);
    EXPECT_EQ(world.player().position().x, expected.x);
    EXPECT_EQ(world.player().position().y, expected.y);
    EXPECT_EQ(world.simulationWorkloadLastUpdate().movementBlockersExamined, originalCandidates.size());
}
}

TEST(GrenadeMovementTest, DenseWorldMatchesOriginalFullScanAcrossDirectionsSlidingOverlapAndEdges)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const std::vector<MovementCase> cases{
        {"right through multiple walls", {260, 270}, 1, 0},
        {"left through multiple walls", {400, 270}, -1, 0},
        {"down through horizontal wall", {270, 270}, 0, 1},
        {"up through horizontal wall", {270, 400}, 0, -1},
        {"diagonal down-right sliding", {260, 270}, 1, 1},
        {"diagonal up-left sliding", {400, 400}, -1, -1},
        {"diagonal down-left sliding", {400, 270}, -1, 1},
        {"diagonal up-right sliding", {260, 400}, 1, -1},
        {"start overlapping vertical wall", {319, 270}, 1, 0},
        {"start overlapping horizontal wall", {270, 339}, 0, -1},
        {"exactly touching vertical wall", {288, 270}, 1, 0},
        {"parallel to touching vertical wall", {288, 270}, 0, 1, 0.05F},
        {"next float before vertical contact", {std::nextafter(288.0F, -infinity), 270}, 1, 0},
        {"next float after vertical contact", {std::nextafter(288.0F, infinity), 270}, 1, 0},
        {"next float before horizontal contact", {270, std::nextafter(308.0F, -infinity)}, 0, 1},
        {"next float after horizontal contact", {270, std::nextafter(308.0F, infinity)}, 0, 1},
        {"long step across thin walls", {80, 270}, 1, 0, 2.0F},
        {"long reverse step across thin walls", {600, 270}, -1, 0, 2.0F},
        {"clamp at top-left world edge", {1, 1}, -1, -1, 1.0F},
        {"clamp at bottom-right world edge", {734, 606}, 1, 1, 1.0F},
        {"stationary frame retains bucket statistics", {260, 270}, 0, 0, 1.0F / 60.0F}};
    auto walls = denseWalls();
    for (const auto &test : cases) checkWorldMovement(test, walls);
    std::reverse(walls.begin(), walls.end());
    for (const auto &test : cases) checkWorldMovement(test, walls);
}
