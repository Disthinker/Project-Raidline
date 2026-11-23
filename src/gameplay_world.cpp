#include "developer_test_range.h"
#include "gameplay_world.h"
#include "raid_camera.h"
#include "enemy_attack_contact.h"
#include "navigation_refresh_selection.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "collision.h"
#include "content_registry.h"
#include "hit_resolution.h"
#include "inventory_transfer.h"
#include "raid_space_query.h"

namespace
{
    constexpr float kMaximumEnemyStepTime{1.0F / 120.0F};
    constexpr float kMaximumEnemySubsteps{2048.0F};
    constexpr float kEnemyNavigationRefreshSeconds{0.10F};
    constexpr float kEnemyNavigationGoalRefreshDistance{32.0F};
    constexpr float kMinimumHighRiskSpawnDistance{260.0F};
    constexpr ContentRect kFreightDivider{{700.0F, 160.0F}, {80.0F, 220.0F}};
    constexpr ContentRect kFreightBreachDoor{{700.0F, 210.0F}, {80.0F, 100.0F}};
    constexpr float kFreightBreachReach{72.0F};
    constexpr float kFreightBreachNoiseRadius{300.0F};
    constexpr float kRoadBlockadeWarningSeconds{5.0F};
    constexpr float kRoadBlockadeOccupancyGraceSeconds{5.0F};
    constexpr float kRoadBlockadeReach{72.0F};
    constexpr float kRoadBlockadeBreachNoiseRadius{400.0F};
    constexpr float kFieldIntelligenceDiscoveryRadius{145.0F};
    constexpr float kFieldIntelligenceInteractionRadius{72.0F};
    constexpr float kFieldIntelligenceThreatRadius{180.0F};
    constexpr Vec2 kLostWeaponCorpseHalfSize{26.0F, 18.0F};

    constexpr int kScorePerEnemy{100};

    bool pointInside(
        ContentRect rect,
        Vec2 point) noexcept
    {
        return point.x >= rect.position.x && point.y >= rect.position.y &&
               point.x <= rect.position.x + rect.size.x &&
               point.y <= rect.position.y + rect.size.y;
    }

    Vec2 nearestPointOnRect(const ContentRect &bounds, Vec2 point) noexcept
    {
        return {
            std::clamp(point.x, bounds.position.x, bounds.position.x + bounds.size.x),
            std::clamp(point.y, bounds.position.y, bounds.position.y + bounds.size.y)};
    }

    Rect centeredBounds(Vec2 center, Vec2 halfSize) noexcept
    {
        return {{center.x - halfSize.x, center.y - halfSize.y},
                {2.0F * halfSize.x, 2.0F * halfSize.y}};
    }

    bool lostWeaponCorpseFits(Vec2 center, const RaidSpaceBlockerIndex &blockers) noexcept
    {
        const Rect bounds = centeredBounds(center, kLostWeaponCorpseHalfSize);
        const Vec2 worldSize = blockers.worldSize();
        return pointInside({{}, worldSize}, bounds.position) &&
            pointInside({{}, worldSize},
                {bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y}) &&
            std::none_of(blockers.allBlockerBounds().begin(), blockers.allBlockerBounds().end(),
                [&bounds](const Rect &wall) { return isCollision(bounds, wall); });
    }

    const MapDefinition &defaultMap()
    {
        return defaultV0MapDefinition();
    }

    std::vector<EnemySpawn> makeDefaultEnemySpawns()
    {
        const EnemyDeploymentDefinition &deployment =
            publishedContentRegistry().enemyDeployment(
                defaultMap().enemyDeploymentId);
        std::vector<EnemySpawn> result;
        result.reserve(deployment.enemies.size());
        for (const EnemySpawnDefinition &enemy : deployment.enemies)
        {
            result.push_back(
                EnemySpawn{
                    enemy.position,
                    enemy.size,
                    3});
        }
        return result;
    }

    std::vector<EnemySpawn> makeDefaultEnemySpawns(
        int maximumHealth)
    {
        std::vector<EnemySpawn> result =
            makeDefaultEnemySpawns();
        for (EnemySpawn &enemy : result)
        {
            enemy.maxHealth = maximumHealth;
        }
        return result;
    }

    InventoryGridSize defaultInventorySize()
    {
        return InventoryGridSize{
            defaultMap().defaultInventorySize.width,
            defaultMap().defaultInventorySize.height};
    }

    std::vector<GroundItemSpawn>
    makeDefaultGroundItemSpawns()
    {
        std::vector<GroundItemSpawn> result;
        result.reserve(defaultMap().groundItems.size());
        for (const GroundItemDefinition &item : defaultMap().groundItems)
        {
            const std::optional<ItemId> legacyId =
                legacyItemId(item.itemDefinitionId);
            if (!legacyId.has_value())
            {
                throw std::logic_error{
                    "V0 map content requires a legacy ItemId adapter"};
            }
            result.push_back(
                GroundItemSpawn{
                    *legacyId,
                    item.position,
                    item.quantity});
        }
        return result;
    }

    Rect playerBounds(
        const Player &player)
    {
        const float size =
            player.size();

        return Rect{
            player.position(),
            Vec2{size, size}};
    }

    Vec2 resolveMovementAgainstBlockers(
        Vec2 currentPosition,
        Vec2 actorSize,
        Vec2 requestedPosition,
        const RaidSpaceBlockerIndex &blockerIndex,
        std::vector<std::size_t> &queryScratch,
        std::size_t *blockersExamined) noexcept
    {
        const float minimumX = std::min(currentPosition.x, requestedPosition.x);
        const float minimumY = std::min(currentPosition.y, requestedPosition.y);
        const float maximumX = std::max(
            currentPosition.x + actorSize.x,
            requestedPosition.x + actorSize.x);
        const float maximumY = std::max(
            currentPosition.y + actorSize.y,
            requestedPosition.y + actorSize.y);
        const bool stationary = currentPosition.x == requestedPosition.x &&
            currentPosition.y == requestedPosition.y;
        const auto finitePosition = [](Vec2 position)
        {
            return std::isfinite(position.x) && std::isfinite(position.y);
        };
        std::optional<std::size_t> broadCount;
        if (!stationary && finitePosition(currentPosition) &&
            finitePosition(requestedPosition) && finitePosition(actorSize) &&
            actorSize.x > 0.0F && actorSize.y > 0.0F)
        {
            broadCount = blockerIndex.querySmallMovementCandidateIndices(
                {minimumX, minimumY}, {maximumX, maximumY}, queryScratch);
        }
        if (!broadCount.has_value())
        {
            blockerIndex.queryCandidateIndices(
                Rect{
                    Vec2{minimumX, minimumY},
                    Vec2{maximumX - minimumX, maximumY - minimumY}},
                queryScratch);
        }
        if (blockersExamined != nullptr)
        {
            *blockersExamined += broadCount.has_value() ? *broadCount : queryScratch.size();
        }

        if (stationary)
            return requestedPosition;
        const auto allBounds = blockerIndex.allBlockerBounds();
        const auto *const bounds = allBounds.data();
        if (!broadCount.has_value())
        {
            // General queries still use the original stable sweep filter.
            const auto candidates = queryScratch.size();
            auto *const indices = queryScratch.data();
            [[maybe_unused]] const auto boundCount = allBounds.size();
            std::size_t kept{};
            for (std::size_t i = 0; i < candidates; ++i)
            {
                const auto id = indices[i];
                assert(id < boundCount);
                const auto &wall = bounds[id];
                if (wall.position.x + wall.size.x < minimumX || wall.position.x > maximumX ||
                    wall.position.y + wall.size.y < minimumY || wall.position.y > maximumY)
                {
                    continue;
                }
                indices[kept++] = id;
            }
            queryScratch.resize(kept);
        }

        Rect resolvedBounds{currentPosition, actorSize};
        float resolvedX = requestedPosition.x;
        for (const std::size_t blocker : queryScratch)
        {
            resolvedX = resolveHorizontalCollision(
                resolvedBounds,
                resolvedX,
                bounds[blocker]);
        }
        resolvedBounds.position.x = resolvedX;

        float resolvedY = requestedPosition.y;
        for (const std::size_t blocker : queryScratch)
        {
            resolvedY = resolveVerticalCollision(
                resolvedBounds,
                resolvedY,
                bounds[blocker]);
        }
        return Vec2{resolvedX, resolvedY};
    }

    Vec2 playerCenter(
        const Player &player)
    {
        const float halfSize =
            player.size() / 2.0f;

        const Vec2 position =
            player.position();

        return Vec2{
            position.x + halfSize,
            position.y + halfSize};
    }

    Vec2 enemyCenter(
        const Enemy &enemy)
    {
        const Vec2 position = enemy.position();
        const Vec2 size = enemy.size();

        return Vec2{
            position.x + size.x / 2.0F,
            position.y + size.y / 2.0F};
    }

    float distanceSquared(
        Vec2 first,
        Vec2 second)
    {
        const float deltaX =
            first.x - second.x;

        const float deltaY =
            first.y - second.y;

        return deltaX * deltaX +
               deltaY * deltaY;
    }

    // Door decisions are infrequent. Check the full body corridor so a sightline
    // through a slit cannot move the follower across cover.
    bool fireSupportCanPass(Vec2 from, Vec2 to, const RaidSpaceBlockerIndex &walls)
    {
        constexpr Vec2 half{RaidFireSupportRuntime::kSize.x * 0.5F, RaidFireSupportRuntime::kSize.y * 0.5F};
        for (const Rect wall : walls.allBlockerBounds())
        {
            const Rect expanded{{wall.position.x - half.x, wall.position.y - half.y},
                                {wall.size.x + 2 * half.x, wall.size.y + 2 * half.y}};
            if (raidSpaceSegmentIntersectsBlockerInterior(from, to, expanded))
                return false;
        }
        return true;
    }

    // Never place a companion on the player or an occupied landing when passing a door.
    // Only the original outdoor deployment permits the player's tile as a last resort.
    std::optional<Vec2> fireSupportLanding(Rect player, const RaidSpaceBlockerIndex &blockers,
        std::span<const Enemy> enemies, bool atPortal)
    {
        constexpr float offset = 56.0F;
        constexpr float diagonal = 40.0F;
        constexpr std::array<Vec2, 9> starts{{
            {offset, 0.0F}, {0.0F, offset}, {-offset, 0.0F}, {0.0F, -offset},
            {diagonal, diagonal}, {-diagonal, diagonal},
            {-diagonal, -diagonal}, {diagonal, -diagonal}, {}}};
        constexpr Vec2 half{
            RaidFireSupportRuntime::kSize.x * 0.5F,
            RaidFireSupportRuntime::kSize.y * 0.5F};
        const Vec2 playerOrigin{player.position.x + player.size.x * .5F, player.position.y + player.size.y * .5F};
        const ContentRect world{{}, blockers.worldSize()};
        const auto freeAt = [&](Vec2 displacement) -> std::optional<Vec2>
        {
            const Vec2 center{playerOrigin.x + displacement.x,
                              playerOrigin.y + displacement.y};
            const Rect initial = centeredBounds(center, half);
            // The shared navigation field needs two units of clearance from walls.
            const Rect navigable = atPortal ? centeredBounds(center, {half.x + 2, half.y + 2}) : initial;
            if (!pointInside(world, initial.position) ||
                !pointInside(world, {center.x + half.x, center.y + half.y}) ||
                !blockers.hasLineOfSight(playerOrigin, center) ||
                (atPortal && (isCollision(initial, player) || !fireSupportCanPass(playerOrigin, center, blockers) ||
                    std::ranges::any_of(enemies,
                    [&](const Enemy &occupant) { return !occupant.isDead() && isCollision(initial, occupant.bounds()); }))) ||
                std::ranges::any_of(blockers.allBlockerBounds(),
                    [&](Rect wall) { return isCollision(navigable, wall); }))
            {
                return std::nullopt;
            }
            return initial.position;
        };
        for (const auto offsetFromPlayer : starts)
            if (auto point = freeAt(offsetFromPlayer))
                return point;
        if (atPortal)
        {
            // For small rooms, try a tight shoulder-to-shoulder landing too.
            constexpr std::array<Vec2, 4> nearby{{{34, 0}, {0, 34}, {-34, 0}, {0, -34}}};
            for (const auto offsetFromPlayer : nearby)
                if (auto point = freeAt(offsetFromPlayer))
                    return point;
        }
        return std::nullopt;
    }

}

GameplayWorld::GameplayWorld()
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          defaultInventorySize(),
          ItemInstanceId{1},
          makeDefaultEnemySpawns(),
          3,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    ItemInstanceId firstItemInstanceId)
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          defaultInventorySize(),
          firstItemInstanceId,
          makeDefaultEnemySpawns(),
          3,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth)
    : GameplayWorld{
          enemyMaxHealth,
          makeDefaultGroundItemSpawns(),
          defaultInventorySize()}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    int playerMaxHealth)
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          defaultInventorySize(),
          ItemInstanceId{1},
          makeDefaultEnemySpawns(enemyMaxHealth),
          playerMaxHealth,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    std::vector<EnemySpawn> initialEnemies,
    int playerMaxHealth)
    : GameplayWorld{
          makeDefaultGroundItemSpawns(),
          defaultInventorySize(),
          ItemInstanceId{1},
          std::move(initialEnemies),
          playerMaxHealth,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(RaidWorldConfig config)
    : GameplayWorld{
          std::vector<GroundItemSpawn>{},
          defaultInventorySize(),
          ItemInstanceId{1},
          std::move(config.initialEnemies),
          config.playerMaximumHealth,
          PlayerHealthOverrideTag{}}
{
    if (!std::isfinite(config.worldSize.x) ||
        !std::isfinite(config.worldSize.y) ||
        config.worldSize.x <= 0.0F || config.worldSize.y <= 0.0F ||
        config.playerCurrentHealth <= 0 ||
        config.playerCurrentHealth > config.playerMaximumHealth ||
        !std::isfinite(config.normalExtractionDurationSeconds) ||
        config.normalExtractionDurationSeconds <= 0.0F)
    {
        throw std::invalid_argument{"RaidWorldConfig is invalid"};
    }
    worldSize_ = config.worldSize;
    if (config.fieldIntelligenceContact &&
        !pointInside(ContentRect{{}, worldSize_}, *config.fieldIntelligenceContact))
    {
        throw std::invalid_argument{"RaidWorldConfig field intelligence contact is invalid"};
    }
    fieldIntelligenceContact_ = config.fieldIntelligenceContact;
    lightConeObservationEnabled_ = config.lightConeObservationEnabled;
    noiseDecoyEnabled_ = config.noiseDecoyEnabled;
    grenadeEnabled_ = config.grenadeEnabled;
    alphaRaidWorld_ = true;
    deferPlayerDamageResolution_ = config.deferPlayerDamageResolution;
    ballisticBlockers_ = std::move(config.ballisticBlockers);
    outdoorLayout_ = std::move(config.outdoorLayout);
    outdoorColumns_ = config.outdoorColumns;
    outdoorRows_ = config.outdoorRows;
    outdoorChunkSizeCells_ = config.outdoorChunkSizeCells;
    if (outdoorLayout_.layoutVersion >= 3U)
    {
        if (outdoorColumns_ == 0U || outdoorRows_ == 0U ||
            outdoorChunkSizeCells_ == 0U)
        {
            throw std::invalid_argument{
                "RaidWorldConfig outdoor presentation grid is invalid"};
        }
        outdoorChunkColumns_ =
            (outdoorColumns_ + outdoorChunkSizeCells_ - 1U) /
            outdoorChunkSizeCells_;
        outdoorChunkRows_ =
            (outdoorRows_ + outdoorChunkSizeCells_ - 1U) /
            outdoorChunkSizeCells_;
        outdoorPresentationChunks_.resize(
            static_cast<std::size_t>(outdoorChunkColumns_) *
            outdoorChunkRows_);
        const auto bucket = [&](std::uint32_t column,
                                std::uint32_t row)
            -> OutdoorPresentationChunk &
        {
            const std::uint32_t chunkColumn = std::min(
                column / outdoorChunkSizeCells_,
                outdoorChunkColumns_ - 1U);
            const std::uint32_t chunkRow = std::min(
                row / outdoorChunkSizeCells_,
                outdoorChunkRows_ - 1U);
            return outdoorPresentationChunks_[
                static_cast<std::size_t>(chunkRow) *
                    outdoorChunkColumns_ + chunkColumn];
        };
        for (std::size_t index{};
             index < outdoorLayout_.terrainSpans.size(); ++index)
        {
            const RaidTerrainSpan &span = outdoorLayout_.terrainSpans[index];
            const std::uint32_t lastColumn = std::min(
                outdoorColumns_ - 1U,
                static_cast<std::uint32_t>(span.firstColumn) +
                    static_cast<std::uint32_t>(span.length) - 1U);
            const std::uint32_t firstChunkColumn =
                span.firstColumn / outdoorChunkSizeCells_;
            const std::uint32_t lastChunkColumn =
                lastColumn / outdoorChunkSizeCells_;
            for (std::uint32_t chunkColumn = firstChunkColumn;
                 chunkColumn <= lastChunkColumn; ++chunkColumn)
            {
                outdoorPresentationChunks_[
                    static_cast<std::size_t>(
                        span.row / outdoorChunkSizeCells_) *
                        outdoorChunkColumns_ + chunkColumn]
                    .terrainSpanIndices.push_back(index);
            }
        }
        for (std::size_t index{};
             index < outdoorLayout_.roadCells.size(); ++index)
        {
            const RaidOutdoorRoadCell &cell =
                outdoorLayout_.roadCells[index];
            bucket(cell.column, cell.row).roadCellIndices.push_back(index);
        }
        const float cellWidth = worldSize_.x /
            static_cast<float>(outdoorColumns_);
        const float cellHeight = worldSize_.y /
            static_cast<float>(outdoorRows_);
        const auto appendRect = [&](ContentRect bounds,
                                    std::size_t index,
                                    auto member)
        {
            const std::uint32_t firstColumn = static_cast<std::uint32_t>(
                std::clamp(std::floor(bounds.position.x / cellWidth),
                           0.0F,
                           static_cast<float>(outdoorColumns_ - 1U)));
            const std::uint32_t lastColumn = static_cast<std::uint32_t>(
                std::clamp(std::floor(
                               (bounds.position.x + bounds.size.x) /
                               cellWidth),
                           0.0F,
                           static_cast<float>(outdoorColumns_ - 1U)));
            const std::uint32_t firstRow = static_cast<std::uint32_t>(
                std::clamp(std::floor(bounds.position.y / cellHeight),
                           0.0F,
                           static_cast<float>(outdoorRows_ - 1U)));
            const std::uint32_t lastRow = static_cast<std::uint32_t>(
                std::clamp(std::floor(
                               (bounds.position.y + bounds.size.y) /
                               cellHeight),
                           0.0F,
                           static_cast<float>(outdoorRows_ - 1U)));
            for (std::uint32_t row = firstRow / outdoorChunkSizeCells_;
                 row <= lastRow / outdoorChunkSizeCells_; ++row)
            {
                for (std::uint32_t column =
                         firstColumn / outdoorChunkSizeCells_;
                     column <= lastColumn / outdoorChunkSizeCells_; ++column)
                {
                    auto &indices = outdoorPresentationChunks_[
                        static_cast<std::size_t>(row) *
                            outdoorChunkColumns_ + column].*member;
                    indices.push_back(index);
                }
            }
        };
        for (std::size_t index{}; index < outdoorLayout_.props.size(); ++index)
            appendRect(outdoorLayout_.props[index].bounds, index,
                       &OutdoorPresentationChunk::propIndices);
        for (std::size_t index{};
             index < outdoorLayout_.landmarks.size(); ++index)
            appendRect(outdoorLayout_.landmarks[index].bounds, index,
                       &OutdoorPresentationChunk::landmarkIndices);
        for (std::size_t index{};
             index < outdoorLayout_.resourcePoints.size(); ++index)
            appendRect(outdoorLayout_.resourcePoints[index].bounds, index,
                       &OutdoorPresentationChunk::resourcePointIndices);
        for (std::size_t index{};
             index < outdoorLayout_.districts.size(); ++index)
        {
            const Vec2 point = outdoorLayout_.districts[index].labelPosition;
            bucket(static_cast<std::uint32_t>(std::clamp(
                       std::floor(point.x / cellWidth), 0.0F,
                       static_cast<float>(outdoorColumns_ - 1U))),
                   static_cast<std::uint32_t>(std::clamp(
                       std::floor(point.y / cellHeight), 0.0F,
                       static_cast<float>(outdoorRows_ - 1U))))
                .districtLabelIndices.push_back(index);
        }
        outdoorTerrainVisitStamps_.resize(
            outdoorLayout_.terrainSpans.size());
        outdoorRoadVisitStamps_.resize(outdoorLayout_.roadCells.size());
        outdoorPropVisitStamps_.resize(outdoorLayout_.props.size());
        outdoorLandmarkVisitStamps_.resize(
            outdoorLayout_.landmarks.size());
        outdoorResourcePointVisitStamps_.resize(
            outdoorLayout_.resourcePoints.size());
        outdoorDistrictVisitStamps_.resize(
            outdoorLayout_.districts.size());
    }
    for (const BallisticBlocker &blocker : ballisticBlockers_)
    {
        if (blocker.id == 0 ||
            !std::isfinite(blocker.bounds.position.x) ||
            !std::isfinite(blocker.bounds.position.y) ||
            !std::isfinite(blocker.bounds.size.x) ||
            !std::isfinite(blocker.bounds.size.y) ||
            blocker.bounds.size.x <= 0.0F ||
            blocker.bounds.size.y <= 0.0F)
        {
            throw std::invalid_argument{
                "RaidWorldConfig ballistic blocker is invalid"};
        }
    }
    interiors_.reserve(config.interiors.size());
    for (RaidInteriorWorldConfig &interiorConfig : config.interiors)
    {
        if (interiorConfig.id.value().empty() ||
            interiorConfig.id == outdoorRaidSpaceId() ||
            !std::isfinite(interiorConfig.worldSize.x) ||
            !std::isfinite(interiorConfig.worldSize.y) ||
            interiorConfig.worldSize.x <= 0.0F ||
            interiorConfig.worldSize.y <= 0.0F ||
            std::any_of(
                interiors_.begin(), interiors_.end(),
                [&](const InteriorRuntime &candidate)
                { return candidate.id == interiorConfig.id; }))
        {
            throw std::invalid_argument{
                "RaidWorldConfig interior identity is invalid"};
        }
        InteriorRuntime interior{
            interiorConfig.id,
            std::move(interiorConfig.displayName),
            interiorConfig.layoutKnown,
            interiorConfig.worldSize,
            interiorConfig.exteriorEntrance,
            interiorConfig.exteriorReturn,
            interiorConfig.interiorSpawn,
            interiorConfig.interiorExit,
            {},
            std::move(interiorConfig.ballisticBlockers)};
        interior.lightEnvironment = interiorConfig.lightEnvironment;
        for (const BallisticBlocker &blocker : interior.ballisticBlockers)
        {
            if (blocker.id == 0 ||
                !std::isfinite(blocker.bounds.position.x) ||
                !std::isfinite(blocker.bounds.position.y) ||
                !std::isfinite(blocker.bounds.size.x) ||
                !std::isfinite(blocker.bounds.size.y) ||
                blocker.bounds.size.x <= 0.0F ||
                blocker.bounds.size.y <= 0.0F)
            {
                throw std::invalid_argument{
                    "RaidWorldConfig interior blocker is invalid"};
            }
        }
        interior.enemies.reserve(interiorConfig.initialEnemies.size());
        for (const EnemySpawn &spawn : interiorConfig.initialEnemies)
        {
            if (!std::isfinite(spawn.position.x) ||
                !std::isfinite(spawn.position.y) ||
                !std::isfinite(spawn.size.x) ||
                !std::isfinite(spawn.size.y) || spawn.size.x <= 0.0F ||
                spawn.size.y <= 0.0F || spawn.maxHealth <= 0 ||
                nextCombatTargetId_ ==
                    std::numeric_limits<CombatTargetId>::max())
            {
                throw std::invalid_argument{
                    "RaidWorldConfig interior enemy is invalid"};
            }
            interior.enemies.spawn(Enemy{
                spawn.position,
                spawn.size,
                Vec2{},
                spawn.maxHealth,
                nextCombatTargetId_++, spawn.torsoArmor, spawn.variant});
            const Vec2 spawnCenter{
                spawn.position.x + spawn.size.x * 0.5F,
                spawn.position.y + spawn.size.y * 0.5F};
            interior.enemies.state(interior.enemies.back().combatTargetId()).encounter = EnemyEncounterRuntime{
                spawn.encounterGroupInstanceId,
                spawn.encounterKind,
                spawn.encounterGroupInstanceId.empty()
                    ? spawnCenter
                    : spawn.encounterHome,
                spawn.patrolPoints,
                0U,
                spawn.ambushActivationDistance};
            interior.enemies.state(interior.enemies.back().combatTargetId()).noiseResponsive = true;
        }
        interior.initialEnemyCount = interior.enemies.size();
        interior.blockerIndex = RaidSpaceBlockerIndex::build(
            interior.worldSize,
            interior.ballisticBlockers);
        if (!interior.blockerIndex.has_value())
        {
            throw std::invalid_argument{
                "RaidWorldConfig interior blocker index is invalid"};
        }
        interiors_.push_back(std::move(interior));
    }
    developerRange_ = config.developerRange;
    for (const auto &spawn : config.volatileBarrels)
    {
        const Rect bounds = volatileBarrelBounds(spawn.center);
        if (!spawn.id || !std::isfinite(spawn.center.x) || !std::isfinite(spawn.center.y) ||
            bounds.position.x < 0 || bounds.position.y < 0 ||
            bounds.position.x + bounds.size.x > config.worldSize.x ||
            bounds.position.y + bounds.size.y > config.worldSize.y ||
            std::ranges::any_of(volatileBarrels_, [&](const auto &other) { return other.id == spawn.id; }))
            throw std::invalid_argument{"Invalid outdoor volatile barrel"};
        volatileBarrels_.push_back({spawn, false});
        intactBarrelTargets_.push_back({spawn.id, bounds});
    }
    pendingBarrelIds_.reserve(volatileBarrels_.size());
    player_ = Player{
        config.playerSpawn.x,
        config.playerSpawn.y,
        config.playerMaximumHealth,
        config.playerCurrentHealth};
    extractionPoint_ = ExtractionPoint{
        config.extractionPoint.position,
        config.extractionPoint.size};

    if (config.rescue.has_value())
    {
        const auto &rescue = *config.rescue;
        if (!std::isfinite(rescue.transferPoint.position.x) ||
            !std::isfinite(rescue.transferPoint.position.y) ||
            !std::isfinite(rescue.transferPoint.size.x) ||
            !std::isfinite(rescue.transferPoint.size.y) ||
            rescue.transferPoint.size.x <= 0.0F ||
            rescue.transferPoint.size.y <= 0.0F ||
            !std::isfinite(rescue.interactionDurationSeconds) ||
            rescue.interactionDurationSeconds <= 0.0F)
        {
            throw std::invalid_argument{
                "RaidWorldConfig rescue settings are invalid"};
        }
        ordinarySurvivorRescuePoint_ = rescue.transferPoint;
        ordinarySurvivorRescueDurationSeconds_ =
            rescue.interactionDurationSeconds;
        if (rescue.escortStart)
        {
            const Vec2 half{RaidCompanionRuntime::kSize.x * 0.5F, RaidCompanionRuntime::kSize.y * 0.5F};
            if (!std::isfinite(rescue.escortStart->x) || !std::isfinite(rescue.escortStart->y) ||
                rescue.escortStart->x < half.x || rescue.escortStart->x > config.worldSize.x - half.x ||
                rescue.escortStart->y < half.y || rescue.escortStart->y > config.worldSize.y - half.y)
                throw std::invalid_argument{"Hospital escort must start within outdoor bounds"};
            hospitalEscortActor_.emplace(Vec2{rescue.escortStart->x - half.x, rescue.escortStart->y - half.y}, 210.0F, 260.0F);
            hospitalEscortActor_->setMovementOrder(FireSupportMovementOrder::Hold);
        }
    }

    if (config.nightRendezvous)
    {
        const auto &bounds = *config.nightRendezvous;
        constexpr float diameter = kRaidNightRendezvousCoverRadius * 2.0F;
        if (!std::isfinite(bounds.position.x) || !std::isfinite(bounds.position.y) ||
            bounds.size.x != diameter || bounds.size.y != diameter ||
            bounds.position.x < 0.0F || bounds.position.y < 0.0F ||
            bounds.position.x + diameter > worldSize_.x ||
            bounds.position.y + diameter > worldSize_.y)
            throw std::invalid_argument{"Raid night rendezvous has invalid bounds"};
        nightRendezvous_ = RaidNightRendezvousView{bounds};
    }
    nightRendezvousUnavailable_ = config.nightRendezvousUnavailable;

    if (config.highRisk.enabled)
    {
        const HighRiskWorldConfig &highRisk = config.highRisk;
        const bool conditionalExtractionEnabled =
            std::isfinite(highRisk.conditionalExtractionDurationSeconds) &&
            highRisk.conditionalExtractionDurationSeconds > 0.0F;
        if (!std::isfinite(highRisk.regularPhaseDurationSeconds) ||
            highRisk.regularPhaseDurationSeconds <= 0.0F ||
            !std::isfinite(highRisk.emergencyExtractionDurationSeconds) ||
            highRisk.emergencyExtractionDurationSeconds <= 0.0F ||
            !std::isfinite(highRisk.conditionalExtractionDurationSeconds) ||
            highRisk.conditionalExtractionDurationSeconds < 0.0F ||
            (conditionalExtractionEnabled &&
             (highRisk.conditionalExtractionMaximumWeightGrams == 0U ||
              !std::isfinite(highRisk.conditionalExtractionPoint.position.x) ||
              !std::isfinite(highRisk.conditionalExtractionPoint.position.y) ||
              !std::isfinite(highRisk.conditionalExtractionPoint.size.x) ||
              !std::isfinite(highRisk.conditionalExtractionPoint.size.y) ||
              highRisk.conditionalExtractionPoint.size.x <= 0.0F ||
              highRisk.conditionalExtractionPoint.size.y <= 0.0F)) ||
            !std::isfinite(highRisk.initialWaveDelaySeconds) ||
            highRisk.initialWaveDelaySeconds <= 0.0F ||
            !std::isfinite(highRisk.waveIntervalSeconds) ||
            highRisk.waveIntervalSeconds <= 0.0F ||
            highRisk.waveSize == 0U ||
            highRisk.activeEnemyCap == 0U ||
            highRisk.waveSize > highRisk.activeEnemyCap ||
            aliveEnemyCount() > highRisk.activeEnemyCap ||
            highRisk.pressureSpawns.empty() ||
            !std::isfinite(highRisk.activationDurationSeconds) ||
            highRisk.activationDurationSeconds <= 0.0F ||
            !std::isfinite(highRisk.activationControlPoint.position.x) ||
            !std::isfinite(highRisk.activationControlPoint.position.y) ||
            !std::isfinite(highRisk.activationControlPoint.size.x) ||
            !std::isfinite(highRisk.activationControlPoint.size.y) ||
            highRisk.activationControlPoint.size.x <= 0.0F ||
            highRisk.activationControlPoint.size.y <= 0.0F ||
            !std::isfinite(highRisk.advancedResourceArea.position.x) ||
            !std::isfinite(highRisk.advancedResourceArea.position.y) ||
            !std::isfinite(highRisk.advancedResourceArea.size.x) ||
            !std::isfinite(highRisk.advancedResourceArea.size.y) ||
            highRisk.advancedResourceArea.size.x <= 0.0F ||
            highRisk.advancedResourceArea.size.y <= 0.0F)
        {
            throw std::invalid_argument{
                "RaidWorldConfig high-risk settings are invalid"};
        }
        for (const EnemySpawn &spawn : highRisk.pressureSpawns)
        {
            if (!std::isfinite(spawn.position.x) ||
                !std::isfinite(spawn.position.y) ||
                !std::isfinite(spawn.size.x) ||
                !std::isfinite(spawn.size.y) ||
                spawn.size.x <= 0.0F ||
                spawn.size.y <= 0.0F ||
                spawn.maxHealth <= 0)
            {
                throw std::invalid_argument{
                    "RaidWorldConfig high-risk pressure spawn is invalid"};
            }
        }

        emergencyExtractionPoint_.emplace(
            highRisk.emergencyExtractionPoint.position,
            highRisk.emergencyExtractionPoint.size);
        if (conditionalExtractionEnabled)
        {
            conditionalExtractionPoint_.emplace(
                highRisk.conditionalExtractionPoint.position,
                highRisk.conditionalExtractionPoint.size);
            conditionalExtractionMaximumWeightGrams_ =
                highRisk.conditionalExtractionMaximumWeightGrams;
        }
        highRiskPressureSpawns_ = highRisk.pressureSpawns;
        highRiskWaveIntervalSeconds_ = highRisk.waveIntervalSeconds;
        highRiskNextWaveSeconds_ = highRisk.initialWaveDelaySeconds;
        highRiskWaveSize_ = highRisk.waveSize;
        highRiskActiveEnemyCap_ = highRisk.activeEnemyCap;
        highRiskControlPoint_ = highRisk.activationControlPoint;
        highRiskAdvancedResourceArea_ = highRisk.advancedResourceArea;
        highRiskActivationDurationSeconds_ = highRisk.activationDurationSeconds;
        nextHighRiskPressureSpawnIndex_ =
            static_cast<std::size_t>(
                highRisk.seed % highRiskPressureSpawns_.size());
    }

    if (config.credentialExtraction)
    {
        const auto &snapshot = *config.credentialExtraction;
        const auto &definition = snapshot.definition;
        const auto interior = std::find_if(interiors_.begin(), interiors_.end(),
            [&](const InteriorRuntime &candidate)
            { return candidate.id == definition.investigationSpaceId; });
        const auto finitePoint = [](Vec2 point)
        { return std::isfinite(point.x) && std::isfinite(point.y); };
        const ContentRect exit = definition.extractionPoint;
        if (!config.highRisk.enabled || !definition.permissionId.valid() ||
            definition.investigationPointId.empty() ||
            definition.investigationSpaceId == outdoorRaidSpaceId() ||
            interior == interiors_.end() ||
            snapshot.rulesVersion != kRaidCredentialExtractionRulesVersion ||
            !finitePoint(definition.investigationPoint) ||
            definition.investigationPoint.x < 0.0F ||
            definition.investigationPoint.y < 0.0F ||
            definition.investigationPoint.x > interior->worldSize.x ||
            definition.investigationPoint.y > interior->worldSize.y ||
            !std::isfinite(definition.investigationRadius) ||
            definition.investigationRadius <= 0.0F ||
            !std::isfinite(definition.investigationDurationSeconds) ||
            definition.investigationDurationSeconds <= 0.0F ||
            !std::isfinite(definition.extractionDurationSeconds) ||
            definition.extractionDurationSeconds <= 0.0F ||
            !finitePoint(exit.position) || !finitePoint(exit.size) ||
            exit.position.x < 0.0F || exit.position.y < 0.0F ||
            exit.size.x <= 0.0F || exit.size.y <= 0.0F ||
            exit.position.x + exit.size.x > config.worldSize.x ||
            exit.position.y + exit.size.y > config.worldSize.y)
        {
            throw std::invalid_argument{
                "RaidWorldConfig credential settings are invalid"};
        }
        credentialExtraction_ = snapshot;
        credentialExtractionPoint_.emplace(exit.position, exit.size);
        if (snapshot.investigationCompleted)
            credentialInvestigationElapsedSeconds_ =
                definition.investigationDurationSeconds;
    }

    raidSession_ = RaidSession{RaidSessionConfig{
        0.0F,
        config.normalExtractionDurationSeconds,
        false,
        HighRiskRaidSessionConfig{
            config.highRisk.enabled,
            config.highRisk.regularPhaseDurationSeconds,
            config.highRisk.emergencyExtractionDurationSeconds,
            config.highRisk.conditionalExtractionDurationSeconds,
            config.highRisk.fuelExtraction},
        credentialExtraction_ && credentialExtraction_->unlockedAtDeploy
            ? credentialExtraction_->definition.extractionDurationSeconds
            : 0.0F}};
    std::vector<Vec2> initialEnemyCenters;
    initialEnemyCenters.reserve(enemies_.size());
    for (const Enemy &enemy : enemies_)
    {
        initialEnemyCenters.push_back(enemyCenter(enemy));
    }
    std::vector<RaidSpecialLocationMapState> specialLocations;
    specialLocations.reserve(interiors_.size());
    for (const InteriorRuntime &interior : interiors_)
    {
        specialLocations.push_back(RaidSpecialLocationMapState{
            interior.id,
            interior.displayName,
            interior.exteriorEntrance,
            false});
    }
    tacticalMap_.configure(
        config.worldSize,
        config.intelligence,
        config.extractionPoint,
        config.highRisk.enabled
            ? std::optional<ContentRect>{
                  config.highRisk.emergencyExtractionPoint}
            : std::nullopt,
        conditionalExtractionPoint_.has_value()
            ? std::optional<ContentRect>{
                  config.highRisk.conditionalExtractionPoint}
            : std::nullopt,
        config.highRisk.enabled
            ? std::optional<ContentRect>{config.highRisk.advancedResourceArea}
            : std::nullopt,
        std::move(initialEnemyCenters),
        std::move(specialLocations),
        std::move(config.tacticalObjectives),
        credentialExtraction_
            ? std::optional<ContentRect>{credentialExtraction_->definition.extractionPoint}
            : std::nullopt);
    tacticalMap_.configureOutdoorLayout(
        outdoorLayout_, outdoorColumns_, outdoorRows_);
    tacticalMap_.revealAround(playerCenter(player_));
    outdoorBlockerIndex_ = RaidSpaceBlockerIndex::build(
        worldSize_,
        ballisticBlockers_);
    if (!outdoorBlockerIndex_.has_value())
    {
        throw std::invalid_argument{
            "RaidWorldConfig outdoor blocker index is invalid"};
    }
    if (config.lostWeaponPursuitFallback || lostWeaponCarrierId_ != kInvalidCombatTargetId)
    {
        if (!config.lostWeaponPursuitFallback ||
            lostWeaponCarrierId_ == kInvalidCombatTargetId ||
            lostWeaponCarrierHalfSize_.x < player_.size() / 2.0F ||
            lostWeaponCarrierHalfSize_.y < player_.size() / 2.0F ||
            !lostWeaponCorpseFits(*config.lostWeaponPursuitFallback, *outdoorBlockerIndex_))
        {
            throw std::invalid_argument{"RaidWorldConfig lost weapon pursuit is invalid"};
        }
        lostWeaponPursuitFallback_ = config.lostWeaponPursuitFallback;
    }
    cacheNavigationFieldsForSpace(
        outdoorRaidSpaceId(),
        worldSize_,
        ballisticBlockers_,
        enemies_.view());
    if (config.fireSupport)
    {
        const auto landing = fireSupportLanding(playerBounds(player_), *outdoorBlockerIndex_, {}, false);
        if (!landing)
            throw std::invalid_argument{"RaidWorldConfig fire support spawn is blocked"};
        fireSupport_.emplace(*config.fireSupport, *landing);
        cacheNavigationFieldForSpace(
            navigationFieldCache_, outdoorRaidSpaceId(), RaidFireSupportRuntime::kSize,
            worldSize_, ballisticBlockers_);
    }
    for (const InteriorRuntime &interior : interiors_)
    {
        cacheNavigationFieldsForSpace(
            interior.id,
            interior.worldSize,
            interior.ballisticBlockers,
            interior.enemies.view());
        if (fireSupport_)
            cacheNavigationFieldForSpace(navigationFieldCache_, interior.id, RaidFireSupportRuntime::kSize,
                interior.worldSize, interior.ballisticBlockers);
    }
    if (config.freightBreachEnabled)
    {
        prepareFreightBreachDoor();
    }
    if (config.roadBlockade)
    {
        if (!config.highRisk.enabled)
            throw std::invalid_argument{"Road blockade requires a high-risk phase"};
        prepareRoadBlockade(*config.roadBlockade);
    }
    if (!raidSession_.start())
    {
        throw std::logic_error{"Alpha Raid session failed to start"};
    }
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems)
    : GameplayWorld{
          enemyMaxHealth,
          std::move(initialGroundItems),
          defaultInventorySize()}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize)
    : GameplayWorld{
          enemyMaxHealth,
          std::move(initialGroundItems),
          inventorySize,
          ItemInstanceId{1}}
{
}

GameplayWorld::GameplayWorld(
    int enemyMaxHealth,
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize,
    ItemInstanceId firstItemInstanceId)
    : GameplayWorld{
          std::move(initialGroundItems),
          inventorySize,
          firstItemInstanceId,
          makeDefaultEnemySpawns(enemyMaxHealth),
          3,
          PlayerHealthOverrideTag{}}
{
}

GameplayWorld::GameplayWorld(
    std::vector<GroundItemSpawn> initialGroundItems,
    InventoryGridSize inventorySize,
    ItemInstanceId firstItemInstanceId,
    std::vector<EnemySpawn> initialEnemies,
    int playerMaxHealth,
    PlayerHealthOverrideTag)
    : player_{
          defaultMap().playerSpawn.x,
          defaultMap().playerSpawn.y,
          playerMaxHealth},
      inventory_{inventorySize},
      storageCabinet_{
          defaultMap().storageCabinet.bounds.position,
          defaultMap().storageCabinet.bounds.size,
          defaultMap().storageCabinet.interactionRange,
          InventoryGridSize{
              defaultMap().storageCabinet.inventorySize.width,
              defaultMap().storageCabinet.inventorySize.height}},
      extractionPoint_{
          defaultMap().extractionPoint.position,
          defaultMap().extractionPoint.size},
      raidSession_{
          RaidSessionConfig{
              defaultMap().raidRules.durationSeconds,
              defaultMap().raidRules.extractionDurationSeconds}},
      nextItemInstanceId_{firstItemInstanceId}
{
    if (firstItemInstanceId == 0)
    {
        throw std::invalid_argument{
            "GameplayWorld first ItemInstanceId must be greater than zero"};
    }

    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();

    if (initialGroundItems.size() >
        static_cast<std::size_t>(
            maximumId - firstItemInstanceId))
    {
        throw std::overflow_error{
            "GameplayWorld does not have enough ItemInstanceId values"};
    }

    if (initialEnemies.size() >
        static_cast<std::size_t>(
            std::numeric_limits<CombatTargetId>::max() -
            nextCombatTargetId_))
    {
        throw std::overflow_error{
            "GameplayWorld does not have enough CombatTargetId values"};
    }
    enemies_.reserve(initialEnemies.size());
    for (const EnemySpawn &spawn : initialEnemies)
    {
        if (!std::isfinite(spawn.position.x) ||
            !std::isfinite(spawn.position.y) ||
            !std::isfinite(spawn.size.x) ||
            !std::isfinite(spawn.size.y) ||
            spawn.size.x <= 0.0F ||
            spawn.size.y <= 0.0F ||
            spawn.maxHealth <= 0)
        {
            throw std::invalid_argument{
                "GameplayWorld EnemySpawn contains invalid values"};
        }

        enemies_.spawn(Enemy{
            spawn.position,
            spawn.size,
            Vec2{},
            spawn.maxHealth,
            nextCombatTargetId_++, spawn.torsoArmor, spawn.variant});
        if (spawn.armedScavenger)
        {
            enemies_.back().markArmedScavenger();
            addArmedScavenger(spawn, enemies_.back().combatTargetId());
        }
        if (!spawn.sourceEnemyId.empty())
            corpseSources_.emplace(enemies_.back().combatTargetId(), RaidCorpseDeathFact{spawn.sourceEnemyId, outdoorRaidSpaceId(), {spawn.size.x * 0.5F, spawn.size.y * 0.5F}});
        if (spawn.lostWeaponCarrier)
        {
            if (lostWeaponCarrierId_ != kInvalidCombatTargetId || !spawn.sourceEnemyId.empty() ||
                spawn.encounterKind != RaidEncounterKind::Patrol || spawn.patrolPoints.size() != 2U)
            {
                throw std::invalid_argument{"RaidWorldConfig lost weapon carrier is invalid"};
            }
            lostWeaponCarrierId_ = enemies_.back().combatTargetId();
            lostWeaponCarrierHalfSize_ = {spawn.size.x / 2.0F, spawn.size.y / 2.0F};
        }
        const Vec2 spawnCenter{
            spawn.position.x + spawn.size.x * 0.5F,
            spawn.position.y + spawn.size.y * 0.5F};
        enemies_.state(enemies_.back().combatTargetId()).encounter = EnemyEncounterRuntime{
            spawn.encounterGroupInstanceId,
            spawn.encounterKind,
            spawn.encounterGroupInstanceId.empty()
                ? spawnCenter
                : spawn.encounterHome,
            spawn.patrolPoints,
            0U,
            spawn.ambushActivationDistance};
        enemies_.state(enemies_.back().combatTargetId()).noiseResponsive = !spawn.armedScavenger;
    }
    initialOutdoorEnemyCount_ = enemies_.size();
    outdoorBlockerIndex_ = RaidSpaceBlockerIndex::build(
        worldSize_,
        ballisticBlockers_);
    if (!outdoorBlockerIndex_.has_value())
    {
        throw std::logic_error{
            "GameplayWorld failed to build its blocker index"};
    }

    groundItems_.reserve(
        initialGroundItems.size());

    for (
        const GroundItemSpawn &spawn :
        initialGroundItems)
    {
        spawnGroundItem(
            spawn.definitionId,
            spawn.position,
            spawn.quantity);
    }

    const bool raidStarted =
        raidSession_.start();

    if (!raidStarted)
    {
        throw std::logic_error{
            "GameplayWorld failed to start its raid session"};
    }
}

void GameplayWorld::spawnGroundItem(
    ItemId definitionId,
    Vec2 position,
    std::uint32_t quantity)
{
    if (nextItemInstanceId_ ==
        std::numeric_limits<ItemInstanceId>::max())
    {
        throw std::overflow_error{
            "GameplayWorld exhausted ItemInstanceId values"};
    }

    // 只有 GroundItem 成功创建后才递增 ID。
    // 若 definitionId 非法，ItemInstance 构造会抛出，
    // 当前 ID 不会被跳过。
    const ItemInstanceId instanceId =
        nextItemInstanceId_;

    groundItems_.emplace_back(
        ItemInstance{
            instanceId,
            definitionId,
            quantity},
        position);

    ++nextItemInstanceId_;
}

std::optional<std::size_t>
GameplayWorld::findPickupCandidate() const
{
    const Rect playerPickupBounds =
        playerBounds(player_);

    const Vec2 center =
        playerCenter(player_);

    std::optional<std::size_t> bestIndex;
    float bestDistanceSquared =
        std::numeric_limits<float>::max();

    for (
        std::size_t index = 0;
        index < groundItems_.size();
        ++index)
    {
        const GroundItem &groundItem =
            groundItems_[index];

        // 正常情况下，失效物品会立即从 groundItems_
        // 删除。这里仍采用 fail-safe 跳过。
        if (!groundItem.item().valid())
        {
            continue;
        }

        if (!isCollision(
                playerPickupBounds,
                groundItem.pickupBounds()))
        {
            continue;
        }

        const float candidateDistanceSquared =
            distanceSquared(
                center,
                groundItem.position());

        // 只在严格更近时替换。
        // 距离相同不会替换，因此保留较早的 vector 下标。
        if (
            !bestIndex.has_value() ||
            candidateDistanceSquared <
                bestDistanceSquared)
        {
            bestIndex = index;
            bestDistanceSquared =
                candidateDistanceSquared;
        }
    }

    return bestIndex;
}

void GameplayWorld::tryPickupOne()
{
    const std::optional<std::size_t> candidate =
        findPickupCandidate();

    if (!candidate.has_value())
    {
        return;
    }

    const std::size_t index =
        *candidate;

    GroundItem &groundItem =
        groundItems_[index];

    // Stack insertion completes planning and capacity reservation before it
    // changes any destination quantity or moves the ground item. Failure keeps
    // both the GroundItem and the inventory unchanged.
    const bool placed =
        tryInsertItemFirstFit(
            inventory_,
            std::move(
                groundItem.itemForTransfer()));

    if (!placed)
    {
        return;
    }

    // 只有所有权已经成功进入 Inventory 后，
    // 才删除 moved-from GroundItem。
    const auto erasePosition =
        groundItems_.begin() +
        static_cast<
            std::vector<GroundItem>::difference_type>(
            index);

    groundItems_.erase(
        erasePosition);
}

void GameplayWorld::update(
    const GameplayInput &input,
    float deltaTime,
    const std::function<bool()> &commitGrenade,
    const std::function<bool()> &beforeSpaceTransition,
    bool portalInteractionRequested,
    const RaidFuelSelector &selectFuel)
{
    spaceTransitionedLastUpdate_ = false;
    alarmTriggeredLastUpdate_ = false;
    if (alarmAlertSeconds_ > 0 && std::isfinite(deltaTime) && deltaTime > 0)
        alarmAlertSeconds_ = std::max(0.0F, alarmAlertSeconds_ - deltaTime);
    nightRendezvousConsumedInteraction_ = false;
    nightRendezvousSignaledLastUpdate_ = false;
    grenadeExplodedLastUpdate_=false;
    if(std::isfinite(deltaTime) && deltaTime>0)grenadeFlashSeconds_=std::max(0.0F,grenadeFlashSeconds_-deltaTime);
    if(grenadeFlashSeconds_<=0)lastGrenadeExplosion_.reset();
    enemyObservationCache_.clear(); // Bound to this simulation frame; never retain removed actors.
    enemyObservationIndexReady_ = false;
    scavengerShotEvents_.clear();
    for (auto &view : scavengerViews_) view.ammunitionChangedLastUpdate = false;
    shooting_.beginFrame(deltaTime);
    melee_.beginFrame(deltaTime);
    enemiesAlertedLastUpdate_ = 0U;
    navigationQueriesLastUpdate_ = 0U;
    simulationWorkloadLastUpdate_ = RaidSimulationWorkload{};
    if (fireSupport_)
        fireSupport_->beginFrame(input.treatingFireSupport);
    if (!raidSession_.isActive())
    {
        melee_.cancelHeavyCharge();
        noiseDecoy_.clear();
        grenade_.clear();
        thrownIllumination_.clear();
        smoke_ = {};
        refreshSmokeSight();
        fireZones_ = {};
        frameFireIntervalCount_ = 0;
        playerFireExposure_ = 0;
        ++fireExposureGeneration_;
        pendingFireTicks_.clear();
        proximityAlarms_ = {};
        alarmAlertSeconds_ = 0;
        localNoiseInvestigations_.clear();
        return;
    }

    if (hasPendingFireDamage() && commitGrenade && (!commitGrenade() || !raidSession_.isActive()))
        return;
    const bool controlsSuppressedAtFrameStart =
        player_.isControlled();

    const Vec2 playerPositionBeforeMovement = player_.position();
    player_.update(
        input,
        deltaTime,
        worldWidth(),
        worldHeight());
    const Vec2 requestedPlayerPosition = player_.position();

    const Vec2 resolvedPlayerPosition = resolveMovementAgainstBlockers(
        playerPositionBeforeMovement,
        Vec2{player_.size(), player_.size()},
        requestedPlayerPosition,
        activeBlockerIndex(),
        blockerQueryScratch_,
        &simulationWorkloadLastUpdate_.movementBlockersExamined);
    static_cast<void>(player_.setPosition(resolvedPlayerPosition));

    if (smoke_.flight() || !smoke_.clouds().empty())
    {
        smoke_.advance(deltaTime);
        refreshSmokeSight();
    }
    const bool exploredOutdoor = inOutdoorRaidSpace();
    const Vec2 explorationCenter = playerCenter(player_);
    if (exploredOutdoor)
    {
        tacticalMap_.revealAround(explorationCenter);
        if (fieldIntelligenceContact_ && !tacticalMap_.discoveredFieldIntelligenceContact())
        {
            if (const auto contact = visibleFieldIntelligenceContact())
                tacticalMap_.discoverFieldIntelligenceContact(*contact);
        }
    }
    if (portalInteractionRequested)
    {
        GameplayInput portalInput = input;
        portalInput.interactJustPressed = true;
        spaceTransitionedLastUpdate_ = tryTransitionRaidSpace(portalInput, beforeSpaceTransition);
    }
    else
        spaceTransitionedLastUpdate_ = tryTransitionRaidSpace(input, beforeSpaceTransition);

    if (spaceTransitionedLastUpdate_ && !smoke_.clouds().empty())
        refreshSmokeSight();

    const Vec2 centerAfterMovement = playerCenter(player_);
    if (fireZones_.flight() || !fireZones_.zones().empty() || frameFireIntervalCount_)
    {
        const float half = player_.size() * 0.5F;
        advanceFireZones(deltaTime, {playerPositionBeforeMovement.x + half, playerPositionBeforeMovement.y + half},
            centerAfterMovement, spaceTransitionedLastUpdate_);
    }
    GameplayInput aimInput = input;
    aimInput.allowWeaponOptics = input.allowWeaponOptics && !controlsSuppressedAtFrameStart;
    const bool freezeObservation = input.freezeObservationDirection || input.inventoryOpen;
    if (freezeObservation && !observationDirectionFrozen_)
        observationDirection_ = weaponAimDirection();
    observationDirectionFrozen_ = freezeObservation;
    shooting_.updateAim(
        aimInput,
        centerAfterMovement,
        player_.facingDirection(),
        activeWorldSize(),
        deltaTime,
        !spaceTransitionedLastUpdate_);
    static_cast<void>(player_.faceDirection(shooting_.aimDirection()));
    if (!observationDirectionFrozen_) observationDirection_ = weaponAimDirection();
    updateObservationLight(deltaTime);
    if (thrownIllumination_.active() || thrownIllumination_.expiredLastAdvance())
    {
        thrownIllumination_.advance(deltaTime);
        updateThrownIlluminationGlare(deltaTime);
    }

    // 撤离使用移动后的 Player 逻辑中心，而不是更大的渲染精灵。
    const float highRiskTimeBeforeUpdate =
        raidSession_.highRiskTimeElapsed();
    const bool playerInEmergencyExtraction =
        inOutdoorRaidSpace() &&
        emergencyExtractionPoint_.has_value() &&
        emergencyExtractionPoint_->contains(playerCenter(player_));
    const bool usesFuel = raidSession_.usesFuelExtraction();
    const bool playerInConditionalZone =
        inOutdoorRaidSpace() &&
        conditionalExtractionPoint_.has_value() &&
        (usesFuel || input.conditionalExtractionEligible) &&
        conditionalExtractionPoint_->contains(playerCenter(player_));
    std::optional<AssetInstanceId> eligibleFuel;
    if (usesFuel && playerInConditionalZone &&
        raidSession_.conditionalExtractionOpen() && input.extractionEligible &&
        !grenade_.active() && !controlsSuppressedAtFrameStart &&
        !player_.isControlled() && !input.inventoryOpen && !input.throwableModal &&
        !input.freezeObservationDirection && !spaceTransitionedLastUpdate_ && selectFuel)
    {
        // An interrupted countdown cannot switch to a different fuel can.
        eligibleFuel = selectFuel(fuelExtractionAsset_);
    }
    const bool playerInConditionalExtraction =
        playerInConditionalZone && (!usesFuel || eligibleFuel.has_value());
    const bool wasCallingSignal =
        raidSession_.extractionRoute() == RaidExtractionRoute::EmergencySignal;
    const bool playerInCredentialExtraction =
        !grenade_.active() && input.extractionEligible && inOutdoorRaidSpace() &&
        !controlsSuppressedAtFrameStart && !player_.isControlled() &&
        !input.inventoryOpen && !input.throwableModal &&
        !input.freezeObservationDirection && !spaceTransitionedLastUpdate_ &&
        credentialExtractionUnlocked() && credentialExtractionPoint_ &&
        credentialExtractionPoint_->contains(playerCenter(player_));
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        signalInterruptedSeconds_ = std::max(0.0F, signalInterruptedSeconds_ - deltaTime);
    }
    // A control that expires during Player::update must still interrupt this call.
    if (controlsSuppressedAtFrameStart)
        static_cast<void>(raidSession_.interruptSignalExtraction());
    raidSession_.update(
        deltaTime,
        !grenade_.active() && input.extractionEligible && inOutdoorRaidSpace() &&
            !player_.isControlled() && extractionPoint_.contains(
                playerCenter(player_)),
        !grenade_.active() && input.extractionEligible && !player_.isControlled() &&
            playerInEmergencyExtraction,
        !grenade_.active() && input.extractionEligible && !player_.isControlled() &&
            playerInConditionalExtraction,
        input.interactJustPressed && !controlsSuppressedAtFrameStart &&
            !spaceTransitionedLastUpdate_ && canCallSignalExtraction(input),
        playerInCredentialExtraction);
    if (usesFuel)
    {
        fuelExtractionAsset_ = raidSession_.extractionRoute() == RaidExtractionRoute::Fuel
            ? eligibleFuel : std::nullopt;
    }
    if (wasCallingSignal && raidSession_.isActive() &&
        raidSession_.extractionRoute() != RaidExtractionRoute::EmergencySignal)
    {
        signalInterruptedSeconds_ = 3.0F;
    }
    if (raidSession_.signalCalledLastUpdate())
    {
        signalInterruptedSeconds_ = 0.0F;
        // One local stimulus per accepted call. Unlike gunshot group alerts,
        // this must not recruit remote members or inactive interior spaces.
        constexpr float signalNoiseRadius = 1200.0F;
        for (Enemy &enemy : activeEnemies())
        {
            if (!enemy.isDead() && distanceSquared(enemyCenter(enemy), centerAfterMovement) <=
                signalNoiseRadius * signalNoiseRadius)
            {
                enemy.hearTarget(centerAfterMovement);
            }
        }
    }
    updateHighRiskActivation(input, deltaTime, centerAfterMovement);
    updateOrdinarySurvivorRescue(input, deltaTime, centerAfterMovement);
    if (raidSession_.phase() == RaidPhase::HighRisk)
    {
        highRiskActivationElapsedSeconds_ = highRiskActivationDurationSeconds_;
    }

    // 终局形成后，本帧不再产生拾取、射击、敌人或命中结果。
    if (!raidSession_.isActive())
    {
        melee_.cancelHeavyCharge();
        thrownIllumination_.clear();
        proximityAlarms_ = {};
        alarmAlertSeconds_ = 0;
        return;
    }

    const float highRiskDeltaTime =
        std::max(0.0F, raidSession_.highRiskTimeElapsed() - highRiskTimeBeforeUpdate);
    if (roadBlockade_)
        updateRoadBlockade(highRiskDeltaTime);
    updateHighRiskPressure(highRiskDeltaTime);

    updateNoiseDecoy(deltaTime);
    updateLocalNoiseInvestigations(deltaTime);
    if(grenadeEnabled_)grenade_.advance(activeRaidSpaceId(),deltaTime);
    // Service-owned Profile damage joins this one immediate simulation event.
    // The callback is never retained by a world/copy. Retire blast victims
    // before taking actor references or permitting their AI/contact this frame.
    if(hasPendingRaidExplosion() && commitGrenade && !commitGrenade())return;
    if(!raidSession_.isActive())return;

    RaidFireSupportRuntime *support = fireSupport_ && fireSupportInteriorIndex_ == activeInteriorIndex_
        ? &*fireSupport_ : nullptr;
    RaidCompanionRuntime *escort = hospitalEscortActor_ && inOutdoorRaidSpace() && !ordinarySurvivorRescueSecured_ &&
        hospitalEscortOrder_ != HospitalEscortState::AwaitingContact ? &*hospitalEscortActor_ : nullptr;
    const bool activeScavengers = !scavengerViews_.empty() && !activeInteriorIndex_;
    const bool enemiesAdvanced = activeScavengers
        ? advanceRaidEnemies<false, true>(deltaTime, nullptr)
        : support || escort
        ? advanceRaidEnemies<true>(deltaTime, support, escort)
        : advanceRaidEnemies<false>(deltaTime, nullptr);
    if (!enemiesAdvanced)
        return;
    if (hasPendingFireDamage() && commitGrenade && !commitGrenade()) return;
    if (!raidSession_.isActive()) return;

    const bool controlsSuppressed =
        controlsSuppressedAtFrameStart ||
        player_.isControlled();
    if (escort && ordinarySurvivorRescueElapsedSeconds_ > 0.0F &&
        (controlsSuppressed || !hospitalEscortAtHandoff() || hospitalEscortThreatened()))
        ordinarySurvivorRescueElapsedSeconds_ = 0.0F;

    if (nightRendezvous_)
        updateNightRendezvous(input, deltaTime, controlsSuppressed);

    if (!spaceTransitionedLastUpdate_ && !controlsSuppressed &&
        !raidSession_.signalCalledLastUpdate() && input.interactJustPressed &&
        !nightRendezvousConsumedInteraction_ && !(escort && ordinarySurvivorRescueInteractionInRange()) &&
        !(input.credentialInvestigationAllowed && credentialInvestigationInRange()))
    {
        tryPickupOne();
    }

    auto &activeEnemySet = activeEnemies();
    const auto &activeBlockerSet = activeBallisticBlockers();
    const WorldShootingAdvance shootingAdvance = shooting_.advanceShots(
        input,
        deltaTime,
        playerCenter(player_),
        player_.size(),
        player_.isMoving(),
        controlsSuppressed,
        activeWorldSize(),
        activeEnemySet,
        activeBlockerSet, inOutdoorRaidSpace() ? std::span<const BallisticBlocker>{intactBarrelTargets_}
                                              : std::span<const BallisticBlocker>{});

    consumeEnemyRemovals(shootingAdvance.removals);
    if (!intactBarrelTargets_.empty() && inOutdoorRaidSpace())
    {
        for (const auto &hit : shooting_.hitResultsLastUpdate())
        {
            if (!hit.environmentObjectId) continue;
            const auto id = static_cast<std::uint32_t>(hit.environmentObjectId);
            if (std::ranges::find(pendingBarrelIds_, id) == pendingBarrelIds_.end())
                pendingBarrelIds_.push_back(id);
        }
        if (!pendingBarrelIds_.empty() && commitGrenade && !commitGrenade()) return;
        if (!raidSession_.isActive()) return;
    }
    if (support)
    {
        const auto advance = support->updateWeapon(
            deltaTime, playerBounds(player_), activeEnemySet, activeBlockerIndex(), activeBlockerSet, activeSmokeSight_);
        consumeEnemyRemovals(advance.removals);
        if (const auto &shot = support->shotEvent())
            emitRaidNoise(shot->origin, shot->noiseRadius);
    }
    if (activeScavengers)
    {
        updateScavengerWeapons(deltaTime);
        if (!raidSession_.isActive()) return;
    }
    melee_.prepare(input.meleeJustPressed,controlsSuppressed || input.inventoryOpen || !raidSession_.isActive(),
        playerCenter(player_),shooting_.aimDirection(),
        activeEnemySet,[&](Vec2 a,Vec2 b){return activeBlockerIndex().hasLineOfSight(a,b);},
        spaceTransitionedLastUpdate_ ? HeavyMeleeInput{} : HeavyMeleeInput::from(input));

    updateCredentialInvestigation(input, deltaTime, controlsSuppressedAtFrameStart);
    if (player_.isControlled() || !raidSession_.isActive())
        shooting_.cancelWeaponOptic();
}

void GameplayWorld::advanceScavengerRetreats(std::span<Enemy *const> actors, float seconds)
{
    if (!(seconds > 0) || !std::isfinite(seconds)) return;
    constexpr float speed = 110.0F;
    for (std::size_t i = 0; i < actors.size(); ++i)
    {
        auto *actor = actors[i];
        auto &weapon = scavengerWeapons_[i];
        auto &view = scavengerViews_[i];
        if (!actor || !weapon.retreatPoint || actor->isDead()) continue;
        checkScavengerRetreat(i, *actor);
        if (!view.retreating) continue;
        const Vec2 start = enemyCenter(*actor);
        const Vec2 goal = *weapon.retreatPoint;
        const auto reachedExit = [&](Vec2 center) {
            return std::hypot(goal.x - center.x, goal.y - center.y) <= kRaidScavengerRetreatExitRadius;
        };
        if (reachedExit(start))
        {
            view.escapeReady = true;
            view.retreatDirection = {};
            continue;
        }
        view.escapeReady = false;
        if (!weapon.nextRetreatWaypoint)
        {
            weapon.retreatRetrySeconds -= seconds;
            if (weapon.retreatRetrySeconds > 0) continue;
            const auto *field = activeNavigationField(actor->size());
            if (field) weapon.nextRetreatWaypoint = field->nextWaypoint(start, goal, 0);
            if (!weapon.nextRetreatWaypoint)
            {
                weapon.retreatRetrySeconds = 0.25F;
                view.retreatDirection = {};
                continue;
            }
        }
        const Vec2 delta{weapon.nextRetreatWaypoint->x - start.x, weapon.nextRetreatWaypoint->y - start.y};
        const float distance = std::hypot(delta.x, delta.y);
        if (distance < 0.05F)
        {
            weapon.nextRetreatWaypoint.reset();
            continue;
        }
        const float stride = std::min(distance, speed * seconds);
        const Vec2 position = actor->position();
        const Vec2 requested{position.x + delta.x * stride / distance, position.y + delta.y * stride / distance};
        const Vec2 next = resolveMovementAgainstBlockers(position, actor->size(), requested, activeBlockerIndex(),
            blockerQueryScratch_, &simulationWorkloadLastUpdate_.movementBlockersExamined);
        if (next.x == position.x && next.y == position.y)
        {
            weapon.nextRetreatWaypoint.reset();
            weapon.retreatRetrySeconds = 0.25F;
            view.retreatDirection = {};
            continue;
        }
        static_cast<void>(actor->setPosition(next));
        weapon.guardCenter = enemyCenter(*actor);
        view.retreatDirection = {delta.x / distance, delta.y / distance};
        if (reachedExit(weapon.guardCenter)) view.escapeReady = true;
        if (stride == distance) weapon.nextRetreatWaypoint.reset();
    }
}

template <bool CompanionsEnabled, bool ScavengersEnabled>
bool GameplayWorld::advanceRaidEnemies(float deltaTime, RaidFireSupportRuntime *support, RaidCompanionRuntime *escort)
{
    // Bounded substeps keep attack phase transitions and one-shot hit
    // opportunities observable even when a render frame is long.
    std::size_t enemySubsteps{1U};
    if (std::isfinite(deltaTime) &&
        deltaTime > 0.0F)
    {
        const float requestedSubsteps = std::ceil(
            deltaTime / kMaximumEnemyStepTime);
        enemySubsteps = static_cast<std::size_t>(
            std::clamp(
                requestedSubsteps,
                1.0F,
                kMaximumEnemySubsteps));
    }

    const float enemyStepTime =
        deltaTime /
        static_cast<float>(enemySubsteps);

    auto &activeEnemySet = activeEnemies();
    const RaidSpaceBlockerIndex &activeStaticBlockers =
        activeBlockerIndex();
    const bool soundActionsEnabled = noiseDecoyEnabled_ || grenadeEnabled_ ||
        !localNoiseInvestigations_.empty();
    simulationWorkloadLastUpdate_.activeEnemies = activeEnemySet.size();
    simulationWorkloadLastUpdate_.activeBlockers =
        activeStaticBlockers.blockerCount();
    simulationWorkloadLastUpdate_.enemySubsteps = enemySubsteps;

    struct EnemyNavigationIntent
    {
        Rect boundsBeforeMovement{};
        EnemyNavigationRuntime *navigation{};
        bool targetVisible{};
        bool refreshRequired{};
        bool allowUnawareNavigation{};
        std::optional<Vec2> reachableGoal;
    };
    const auto enemyCount = activeEnemySet.size();
    unsigned alarmMask = proximityAlarms_.armedIn(activeRaidSpaceId());
    // The roster stays in place until all substeps finish.
    std::vector<Enemy *> guardActors;
    std::vector<InfectedGuardTarget> infectedGuardTargets;
    if constexpr (ScavengersEnabled)
    {
        guardActors.resize(scavengerViews_.size());
        infectedGuardTargets.resize(enemyCount);
        scavengerSightSamples_.resize(enemyCount * (guardActors.size() + 1));
        for (auto &actor : activeEnemySet)
            if (actor.isArmedScavenger())
                for (std::size_t i{}; i < scavengerViews_.size(); ++i)
                    if (actor.combatTargetId() == scavengerViews_[i].id)
                        guardActors[i] = &actor;
    }
    std::vector<EnemyNavigationIntent> navigationIntents(enemyCount);
    auto *const intents = navigationIntents.data();
    // The solo specialization allocates and queries no companion state.
    struct CompanionTarget { RaidCompanionRuntime *actor{}; bool visible{}; };
    std::vector<CompanionTarget> companionTargets;
    if constexpr (CompanionsEnabled)
        companionTargets.resize(enemyCount);
    const auto hasLineOfSight = [&](Vec2 start, Vec2 end)
    {
        std::size_t blockerTests{};
        const bool visible = activeStaticBlockers.hasLineOfSight(
            start, end, &blockerTests);
        simulationWorkloadLastUpdate_.lineOfSightBlockersExamined += blockerTests;
        return visible;
    };
    const auto canSee = [&](Vec2 start, Vec2 end)
    {
        return !activeSmokeSight_.blocks(start, end) && hasLineOfSight(start, end);
    };

    for (std::size_t step{0U};
         step < enemySubsteps;
         ++step)
    {
        const std::size_t firstFirePulse = pendingFireTicks_.size();
        double fireFrom{}, fireUntil{};
        if (frameFireIntervalCount_)
        {
            const double stepSeconds = static_cast<double>(deltaTime) / static_cast<double>(enemySubsteps);
            fireFrom = step * stepSeconds;
            fireUntil = (step + 1) * stepSeconds;
            const float fraction = static_cast<float>((static_cast<double>(step) + 0.5) / static_cast<double>(enemySubsteps));
            const Vec2 playerSample{std::lerp(firePlayerFrameStart_.x, firePlayerFrameEnd_.x, fraction),
                std::lerp(firePlayerFrameStart_.y, firePlayerFrameEnd_.y, fraction)};
            collectFireDamage(playerSample, kInvalidCombatTargetId, playerFireExposure_, fireFrom, fireUntil, firstFirePulse);
        }
        if (std::isfinite(enemyStepTime) && enemyStepTime > 0.0F)
        {
            enemyDamageProtectionRemainingSeconds_ = std::max(
                0.0F,
                enemyDamageProtectionRemainingSeconds_ - enemyStepTime);
        }
        const Vec2 playerPosition =
            playerCenter(player_);
        const Vec2 worldSize = activeWorldSize();
        if constexpr (ScavengersEnabled) advanceScavengerRetreats(guardActors, enemyStepTime);
        if constexpr (CompanionsEnabled)
        {
            const auto moveCompanion = [&](RaidCompanionRuntime &actor, float stopDistance, bool following) {
                const Rect before = actor.bounds();
                const auto *field = following ? activeNavigationField(before.size) : nullptr;
                const Vec2 requested = actor.prepareMovement(
                    playerPosition, field, activeStaticBlockers, enemyStepTime, stopDistance);
                if (requested.x != before.position.x || requested.y != before.position.y)
                    actor.setPosition(resolveMovementAgainstBlockers(before.position, before.size, requested,
                        activeStaticBlockers, blockerQueryScratch_, &simulationWorkloadLastUpdate_.movementBlockersExamined));
            };
            if (support) moveCompanion(*support, 57.0F, true);
            if (escort)
            {
                const float stopDistance = pointInside(*ordinarySurvivorRescuePoint_, playerPosition) ? 12.0F : 40.0F;
                moveCompanion(*escort, stopDistance, hospitalEscortOrder_ == HospitalEscortState::Following);
            }
        }
        std::vector<EnemySquadMemberSnapshot> enemySnapshots;
        enemySnapshots.reserve(enemyCount);
        if constexpr (ScavengersEnabled)
            for (auto &weapon : scavengerWeapons_)
                weapon.contactProtection = std::max(0.0F, weapon.contactProtection - enemyStepTime);
        auto *nextIntent = intents;
        for (const Enemy &enemy : activeEnemySet)
        {
            auto &intent = *nextIntent++;
            intent = EnemyNavigationIntent{};
            intent.boundsBeforeMovement = enemy.bounds();
            const auto &bounds = intent.boundsBeforeMovement;
            const Vec2 position{
                bounds.position.x + bounds.size.x / 2.0F,
                bounds.position.y + bounds.size.y / 2.0F};
            if (alarmMask && enemyStepTime > 0.0F && !enemy.isDead() &&
                proximityAlarms_.observe(position, alarmMask, hasLineOfSight))
            {
                alarmTriggeredLastUpdate_ = true;
                alarmAlertSeconds_ = 2.0F;
            }
            const auto attackPhase = enemy.attackPhase();
            Vec2 targetPosition = playerPosition;
            if constexpr (CompanionsEnabled)
            {
                auto &choice = companionTargets[enemySnapshots.size()];
                choice = {};
                if (attackPhase != EnemyAttackPhase::Idle)
                {
                    if (support && support->hasAttackLock(enemy.combatTargetId())) choice.actor = support;
                    else if (escort && escort->hasAttackLock(enemy.combatTargetId())) choice.actor = escort;
                }
                else if (!enemy.isDead())
                {
                    const float playerDistance = distanceSquared(position, playerPosition);
                    const float acquire = enemy.acquireTargetDistance();
                    float closest = playerDistance;
                    const auto consider = [&](RaidCompanionRuntime *actor) {
                        if (!actor || !actor->alive()) return;
                        const Vec2 candidate = actor->center();
                        const float distance = distanceSquared(position, candidate);
                        if ((distance < closest || (!choice.actor && distance <= acquire * acquire &&
                                !canSee(position, playerPosition))) && canSee(position, candidate))
                        {
                            choice = {actor, true};
                            closest = distance;
                        }
                    };
                    consider(support);
                    if (escort) consider(escort);
                }
                if (choice.actor) targetPosition = choice.actor->center();
            }
            if constexpr (ScavengersEnabled)
            {
                auto &choice = infectedGuardTargets[enemySnapshots.size()];
                auto *sight = scavengerSightSamples_.data() + enemySnapshots.size() * (guardActors.size() + 1);
                choice = selectInfectedGuardTarget(enemy, position, playerPosition, guardActors, sight);
                if (choice.guard >= 0) targetPosition = scavengerWeapons_[choice.guard].guardCenter;
            }
            enemySnapshots.push_back(
                EnemySquadMemberSnapshot{
                    position,
                    !enemy.isDead() && (!ScavengersEnabled || !enemy.isArmedScavenger()),
                    enemy.awarenessState(),
                    attackPhase,
                    enemy.hasAttackOpportunity(targetPosition)});
        }

        const auto *const snapshots = enemySnapshots.data();
        EnemySquadDecisionMetrics squadMetrics;
        const std::vector<EnemyTacticalDirective> directives =
            activeEnemySet.squad().decide(
                enemySnapshots,
                playerPosition,
                &squadMetrics);
        simulationWorkloadLastUpdate_.neighborCandidatesExamined +=
            squadMetrics.neighborCandidatesExamined;

        std::size_t refreshRequiredCount{};
        for (std::size_t enemyIndex{};
             enemyIndex < enemyCount;
             ++enemyIndex)
        {
            EnemyNavigationIntent &intent = intents[enemyIndex];
            const auto &snapshot = snapshots[enemyIndex];
            Enemy &enemy = activeEnemySet[enemyIndex];
            const auto targetId = enemy.combatTargetId();
            auto &attachedState = activeEnemySet.state(targetId);
            if (frameFireIntervalCount_ && !enemy.isDead())
            {
                if (attachedState.fireExposureGeneration != fireExposureGeneration_)
                {
                    attachedState.fireExposureGeneration = fireExposureGeneration_;
                    attachedState.fireExposure = 0;
                }
                collectFireDamage(snapshot.position, targetId, attachedState.fireExposure, fireFrom, fireUntil, firstFirePulse);
            }
            EnemyNavigationRuntime &navigation =
                attachedState.navigation;
            EnemyEncounterRuntime &encounter =
                attachedState.encounter;
            if (!snapshot.alive)
            {
                navigation = EnemyNavigationRuntime{};
                continue;
            }

            const Vec2 enemyPosition = snapshot.position;
            Vec2 targetPosition = playerPosition;
            if constexpr (CompanionsEnabled)
            {
                if (const auto *actor = companionTargets[enemyIndex].actor)
                    targetPosition = actor->center();
            }
            if constexpr (ScavengersEnabled)
                if (const int choice = infectedGuardTargets[enemyIndex].guard; choice >= 0)
                    targetPosition = scavengerWeapons_[choice].guardCenter;
            const bool hasNoiseInvestigation=noiseDecoy_.investigations().contains(targetId) ||
                localNoiseInvestigations_.contains(targetId);
            const bool investigatingNoise=soundActionsEnabled && hasNoiseInvestigation;
            const bool outsideAcquisition=distanceSquared(enemyPosition,targetPosition) >
                enemy.acquireTargetDistance()*enemy.acquireTargetDistance();
            // Unaware actors cannot acquire a player beyond acquisition range;
            // sound investigators also have an independent navigation goal.
            // Skip only irrelevant geometry. Ordinary searches and confirmed
            // pursuit keep their accepted visibility/lose-distance policy.
            const bool existingRouteNeedsSight=!investigatingNoise &&
                (enemy.lastKnownTargetPosition().has_value() ||
                 (snapshot.awareness==EnemyAwarenessState::Unaware &&
                  ((encounter.kind==RaidEncounterKind::Patrol && !encounter.patrolPoints.empty()) ||
                   (encounter.kind==RaidEncounterKind::Guard && distanceSquared(enemyPosition,encounter.home)>64.0F*64.0F))));
            const bool skipNoiseSight=soundActionsEnabled && !existingRouteNeedsSight && outsideAcquisition &&
                (snapshot.awareness==EnemyAwarenessState::Unaware ||
                 (investigatingNoise && snapshot.awareness==EnemyAwarenessState::Searching)) &&
                snapshot.attackPhase==EnemyAttackPhase::Idle;
            if constexpr (CompanionsEnabled)
            {
                const auto choice = companionTargets[enemyIndex];
                if (choice.visible)
                    intent.targetVisible = true;
                else if (choice.actor && !choice.actor->alive())
                    intent.targetVisible = false;
                else
                    intent.targetVisible = !skipNoiseSight && canSee(enemyPosition,targetPosition);
            }
            else if constexpr (ScavengersEnabled)
            {
                const auto &choice = infectedGuardTargets[enemyIndex];
                if (choice.guard >= 0 && snapshot.attackPhase == EnemyAttackPhase::Idle)
                    intent.targetVisible = true; // Already verified in this substep.
                else if (choice.guard < 0 && choice.playerVisible.has_value())
                    intent.targetVisible = !skipNoiseSight && *choice.playerVisible;
                else
                {
                    auto &sample = scavengerSightSamples_[enemyIndex * (guardActors.size() + 1) + (choice.guard + 1)];
                    intent.targetVisible = !skipNoiseSight && scavengerSight(enemyPosition, targetPosition, sample);
                }
            }
            else
            {
                intent.targetVisible = !skipNoiseSight && canSee(enemyPosition,targetPosition);
            }
            if (encounter.kind == RaidEncounterKind::Ambush &&
                snapshot.awareness == EnemyAwarenessState::Unaware &&
                distanceSquared(enemyPosition, targetPosition) >
                    encounter.ambushActivationDistance *
                        encounter.ambushActivationDistance)
            {
                intent.targetVisible = false;
            }
            std::optional<Vec2> passiveGoal;
            if (snapshot.awareness == EnemyAwarenessState::Unaware &&
                !hasNoiseInvestigation)
            {
                if (encounter.kind == RaidEncounterKind::Patrol &&
                    !encounter.patrolPoints.empty())
                {
                    encounter.patrolPointIndex %= encounter.patrolPoints.size();
                    if (distanceSquared(
                            enemyPosition,
                            encounter.patrolPoints[encounter.patrolPointIndex]) <=
                        48.0F * 48.0F)
                    {
                        encounter.patrolPointIndex =
                            (encounter.patrolPointIndex + 1U) %
                            encounter.patrolPoints.size();
                    }
                    passiveGoal =
                        encounter.patrolPoints[encounter.patrolPointIndex];
                }
                else if (encounter.kind == RaidEncounterKind::Guard &&
                         distanceSquared(enemyPosition, encounter.home) >
                             64.0F * 64.0F)
                {
                    passiveGoal = encounter.home;
                }
            }
            const bool visibleTargetIsRelevant = intent.targetVisible &&
                (snapshot.awareness != EnemyAwarenessState::Unaware ||
                 distanceSquared(enemyPosition, targetPosition) <=
                     enemy.acquireTargetDistance() *
                         enemy.acquireTargetDistance());
            const auto investigation = investigatingNoise
                ? noiseInvestigationGoal(enemy, intent.targetVisible, targetPosition)
                : std::optional<Vec2>{};
            const std::optional<Vec2> navigationGoal = investigation ? investigation : visibleTargetIsRelevant
                ? std::optional<Vec2>{targetPosition}
                : enemy.lastKnownTargetPosition().has_value()
                ? enemy.lastKnownTargetPosition()
                : passiveGoal;
            intent.allowUnawareNavigation = investigation.has_value() || (passiveGoal.has_value() &&
                !visibleTargetIsRelevant && !enemy.lastKnownTargetPosition().has_value());
            if (!navigationGoal.has_value())
            {
                navigation = EnemyNavigationRuntime{};
                continue;
            }

            const Vec2 halfEnemySize{
                intent.boundsBeforeMovement.size.x * 0.5F,
                intent.boundsBeforeMovement.size.y * 0.5F};
            // The roster is fixed until all enemy substeps complete. Consume
            // this stable-ID lookup in the execution pass of this substep.
            intent.navigation = &navigation;
            intent.reachableGoal = Vec2{
                std::clamp(
                    navigationGoal->x,
                    halfEnemySize.x,
                    worldSize.x - halfEnemySize.x),
                std::clamp(
                    navigationGoal->y,
                    halfEnemySize.y,
                    worldSize.y - halfEnemySize.y)};
            navigation.refreshRemainingSeconds = std::max(
                0.0F,
                navigation.refreshRemainingSeconds - enemyStepTime);
            const bool goalMoved = !navigation.goal.has_value() ||
                distanceSquared(*navigation.goal, *intent.reachableGoal) >=
                    kEnemyNavigationGoalRefreshDistance *
                        kEnemyNavigationGoalRefreshDistance;
            const bool perceptionChanged = navigation.initialized &&
                navigation.targetVisible != intent.targetVisible;
            intent.refreshRequired =
                snapshot.attackPhase == EnemyAttackPhase::Idle &&
                (!navigation.initialized || goalMoved || perceptionChanged ||
                 navigation.refreshRemainingSeconds <= 0.0F);
            refreshRequiredCount += intent.refreshRequired ? 1U : 0U;
        }

        std::size_t &scheduleCursor = activeNavigationScheduleCursor();
        const auto refreshSelection = selectNavigationRefresh(
            activeEnemySet.view(), scheduleCursor, enemyCount,
            [&](std::size_t candidate) { return navigationIntents[candidate].refreshRequired; });
        scheduleCursor = activeEnemySet.empty() ? 0U : refreshSelection.nextCursor;
        const std::size_t selectedRefreshCount = refreshSelection.target ? 1U : 0U;
        simulationWorkloadLastUpdate_.navigationRefreshesDeferred +=
            refreshRequiredCount - selectedRefreshCount;

        for (std::size_t enemyIndex{0U};
             enemyIndex < enemyCount;
             ++enemyIndex)
        {
            Enemy &enemy = activeEnemySet[enemyIndex];
            const auto &snapshot = snapshots[enemyIndex];
            EnemyNavigationIntent &intent = intents[enemyIndex];
            if constexpr (ScavengersEnabled)
                if (enemy.isArmedScavenger()) continue;
            if (!snapshot.alive)
            {
                static_cast<void>(enemy.updateTowardsTarget(
                    playerPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    worldSize.x,
                    worldSize.y,
                    false,
                    std::nullopt));
                continue;
            }
            if constexpr (CompanionsEnabled)
            {
                if (companionTargets[enemyIndex].visible && !companionTargets[enemyIndex].actor->alive())
                {
                    continue;
                }
            }

            std::optional<Vec2> navigationTarget;
            if (intent.reachableGoal.has_value())
            {
                EnemyNavigationRuntime &navigation = *intent.navigation;
                intent.navigation = nullptr;
                if (refreshSelection.target == enemy.combatTargetId())
                {
                    ++navigationQueriesLastUpdate_;
                    ++simulationWorkloadLastUpdate_.navigationQueries;
                    RaidSpaceNavigationField *navigationField =
                        activeNavigationField(intent.boundsBeforeMovement.size);
                    navigation.waypoint = navigationField == nullptr
                        ? std::nullopt
                        : navigationField->nextWaypoint(
                              snapshot.position,
                              *intent.reachableGoal,
                              enemy.navigationGoalTolerance(intent.targetVisible));
                    if (!navigation.waypoint.has_value())
                    {
                        navigation.waypoint = snapshot.position;
                    }
                    navigation.goal = *intent.reachableGoal;
                    navigation.targetVisible = intent.targetVisible;
                    navigation.initialized = true;
                    ++navigation.refreshCount;
                    navigation.refreshRemainingSeconds =
                        kEnemyNavigationRefreshSeconds;
                }
                navigationTarget = navigation.waypoint;
            }

            Vec2 targetPosition = playerPosition;
            if constexpr (CompanionsEnabled)
            {
                if (const auto *actor = companionTargets[enemyIndex].actor) targetPosition = actor->center();
            }
            if constexpr (ScavengersEnabled)
                if (const int choice = infectedGuardTargets[enemyIndex].guard; choice >= 0)
                    targetPosition = scavengerWeapons_[choice].guardCenter;
            static_cast<void>(
                enemy.updateTowardsTarget(
                    targetPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    worldSize.x,
                    worldSize.y,
                    intent.targetVisible,
                    navigationTarget,
                    intent.allowUnawareNavigation));
            const Vec2 resolvedEnemyPosition = resolveMovementAgainstBlockers(
                intent.boundsBeforeMovement.position,
                intent.boundsBeforeMovement.size,
                enemy.position(),
                activeStaticBlockers,
                blockerQueryScratch_,
                &simulationWorkloadLastUpdate_.movementBlockersExamined);
            static_cast<void>(enemy.setPosition(resolvedEnemyPosition));
            if (snapshot.awareness != EnemyAwarenessState::Alerted &&
                enemy.awarenessState() == EnemyAwarenessState::Alerted)
            {
                ++enemiesAlertedLastUpdate_;
            }

            if constexpr (CompanionsEnabled)
            {
                if (auto *actor = companionTargets[enemyIndex].actor)
                {
                    const auto updatedPhase = enemy.attackPhase();
                    if (snapshot.attackPhase == EnemyAttackPhase::Idle &&
                        updatedPhase != EnemyAttackPhase::Idle)
                    {
                        actor->lockAttack(enemy.combatTargetId());
                    }
                    else if (updatedPhase == EnemyAttackPhase::Idle &&
                             snapshot.attackPhase != EnemyAttackPhase::Idle)
                    {
                        actor->forgetAttack(enemy.combatTargetId());
                    }

                    const Rect bounds = actor->bounds();
                    const auto contact = resolveEnemyAttackContact(
                        enemy, bounds, actor->alive(), actor->contactProtection(), [&] {
                            if (enemy.attackType() == EnemyAttackType::LowLunge)
                            {
                                const auto corridor = lowLungeTelegraph(enemy);
                                return corridor && corridor->overlaps(bounds) &&
                                    hasLineOfSight(corridor->origin, targetPosition);
                            }
                            return hasLineOfSight(enemyCenter(enemy), targetPosition);
                        });
                    if (contact.damage)
                        actor->applyContact(*contact.damage, contact.controlDuration);
                    continue;
                }
            }

            if constexpr (ScavengersEnabled)
            {
                if (const int choice = infectedGuardTargets[enemyIndex].guard; choice >= 0)
                {
                    auto &weapon = scavengerWeapons_[choice];
                    if (snapshot.attackPhase == EnemyAttackPhase::Idle &&
                        enemy.attackPhase() != EnemyAttackPhase::Idle)
                        weapon.attackLocks.push_back(enemy.combatTargetId());
                    else if (enemy.attackPhase() == EnemyAttackPhase::Idle)
                        std::erase(weapon.attackLocks, enemy.combatTargetId());
                    if (!guardActors[choice]) continue;
                    Enemy &guard = *guardActors[choice];
                    const auto contact = resolveEnemyAttackContact(enemy, guard.bounds(), !guard.isDead(),
                        weapon.contactProtection, [&] {
                            if (enemy.attackType() == EnemyAttackType::LowLunge)
                            {
                                const auto corridor = lowLungeTelegraph(enemy);
                                return corridor && corridor->overlaps(guard.bounds()) &&
                                    hasLineOfSight(corridor->origin, targetPosition);
                            }
                            return hasLineOfSight(enemyCenter(enemy), targetPosition);
                        });
                    if (contact.damage)
                    {
                        const auto &hit = *contact.damage;
                        const auto result = resolveCombatDamage({hit.baseDamage, hit.region, hit.penetration,
                            hit.armorDamage, hit.weakPoint, std::nullopt});
                        if (result.resolved()) static_cast<void>(guard.takeDamage(result.damageApplied));
                    }
                    continue;
                }
            }
            const auto contact = resolveEnemyAttackContact(
                enemy, playerBounds(player_), !player_.isDead(),
                enemyDamageProtectionRemainingSeconds_, [&] {
                    if (enemy.attackType() == EnemyAttackType::LowLunge)
                    {
                        const auto corridor = lowLungeTelegraph(enemy);
                        return corridor && corridor->overlaps(playerBounds(player_)) &&
                            hasLineOfSight(corridor->origin, playerCenter(player_));
                    }
                    return hasLineOfSight(enemyCenter(enemy), playerCenter(player_));
                });
            if (!contact.damage)
                continue;

            // Legacy V0 owns its three-HP player. Profile-backed Raid instead
            // forwards the accepted fact to the existing armor/wound consumer.
            if (deferPlayerDamageResolution_)
                pendingPlayerDamageObservations_.push_back(*contact.damage);
            else if (damagePlayer(contact.legacyDamage))
                return false;

            if (contact.controlDuration > 0.0F)
            {
                static_cast<void>(
                    player_.applyControl(
                        contact.controlDuration));
                if (raidSession_.interruptSignalExtraction())
                    signalInterruptedSeconds_ = 3.0F;
                static_cast<void>(raidSession_.interruptCredentialExtraction());
                if (raidSession_.usesFuelExtraction()) interruptFuelExtraction();
            }
        }
    }

    if constexpr (ScavengersEnabled) consumeEnemyRemovals(activeEnemySet.removeDead());
    return true;
}

void GameplayWorld::acknowledgeCorpseDeath(std::string_view sourceId)
{
    std::erase_if(pendingCorpseDeaths_, [sourceId](const auto &fact) { return fact.sourceEnemyId == sourceId; });
}

std::optional<Vec2> GameplayWorld::lostWeaponCarrierDeath() const noexcept
{
    return lostWeaponCarrierDeath_;
}

const Player &
GameplayWorld::player() const
{
    return player_;
}

const std::vector<LogicalBallisticFlight> &
GameplayWorld::logicalBallistics() const
{
    return shooting_.logicalBallistics();
}

std::vector<ShotPresentationSnapshot>
GameplayWorld::shotPresentationSnapshots() const
{
    return shooting_.shotPresentationSnapshots();
}

std::vector<ShotFeedbackPresentationSnapshot>
GameplayWorld::shotFeedbackPresentationSnapshots() const
{
    return shooting_.shotFeedbackPresentationSnapshots();
}

Vec2 GameplayWorld::normalizedShotScreenShakeOffset() const noexcept
{
    return shooting_.normalizedScreenShakeOffset();
}

const std::vector<Enemy> &
GameplayWorld::enemies() const
{
    return activeEnemies();
}

const std::vector<BallisticBlocker> &
GameplayWorld::ballisticBlockers() const noexcept
{
    return activeBallisticBlockers();
}

std::optional<EnemyLowLungeCorridor>
GameplayWorld::lowLungeTelegraph(const Enemy &enemy) const noexcept
{
    auto corridor = enemy.lowLungeCorridor();
    if (corridor)
    {
        for (const auto &blocker : activeBallisticBlockers())
        {
            corridor->clipBefore(blocker.bounds);
            if (corridor->reach <= 0.0F)
                return std::nullopt;
        }
    }
    return corridor;
}

const RaidSpaceDefinitionId &
GameplayWorld::activeRaidSpaceId() const noexcept
{
    if (activeInteriorIndex_.has_value())
    {
        return interiors_[*activeInteriorIndex_].id;
    }
    return outdoorRaidSpaceId();
}

bool GameplayWorld::inOutdoorRaidSpace() const noexcept
{
    return !activeInteriorIndex_.has_value();
}

std::string_view GameplayWorld::activeRaidSpaceDisplayName() const noexcept
{
    if (activeInteriorIndex_.has_value())
    {
        return interiors_[*activeInteriorIndex_].displayName;
    }
    return "Outdoor";
}

Vec2 GameplayWorld::raidSpaceWorldSize() const noexcept
{
    return activeWorldSize();
}

void GameplayWorld::updateObservationLight(float seconds)
{
    const auto environment =
        activeInteriorIndex_
            ? interiors_[*activeInteriorIndex_].lightEnvironment
            : (raidDaylight_ ? RaidLightEnvironment::DayOutdoor
                             : RaidLightEnvironment::NightOutdoor);
    const auto target = raidSightParameters(environment);
    if (observationLightSpace_ != activeRaidSpaceId())
    {
        observationLightSpace_ = activeRaidSpaceId();
        observationLightEnvironment_ = environment;
        observationParameters_ = observationTransitionStart_ = target;
        observationLightTransitionSeconds_ = 0.5F;
        return;
    }
    if (observationLightEnvironment_ != environment)
    {
        observationLightEnvironment_ = environment;
        observationTransitionStart_ = observationParameters_;
        observationLightTransitionSeconds_ = 0;
    }
    if (std::isfinite(seconds) && seconds > 0)
        observationLightTransitionSeconds_ =
            std::min(0.5F, observationLightTransitionSeconds_ + seconds);
    const float t = observationLightTransitionSeconds_ / 0.5F;
    observationParameters_ = {
        observationTransitionStart_.angleDegrees +
            (target.angleDegrees - observationTransitionStart_.angleDegrees) *
                t,
        observationTransitionStart_.maximumDistance +
            (target.maximumDistance -
             observationTransitionStart_.maximumDistance) *
                t};
}

RaidLightEnvironment GameplayWorld::currentObservationEnvironment() const noexcept
{
    if (!lightConeObservationEnabled()) return RaidLightEnvironment::WeakIndoor;
    return activeInteriorIndex_ ? interiors_[*activeInteriorIndex_].lightEnvironment :
        (raidDaylight_ ? RaidLightEnvironment::DayOutdoor : RaidLightEnvironment::NightOutdoor);
}

RaidSightParameters GameplayWorld::currentObservationParameters() const noexcept
{
    const auto optic = activeWeaponOptic();
    if (!lightConeObservationEnabled_ && optic == WeaponOpticMode::None)
        return {360, 0};
    auto ambient = observationLightSpace_ == activeRaidSpaceId()
        ? observationParameters_ : raidSightParameters(currentObservationEnvironment());
    if (nightVisionOutdoor_.maximumDistance > 0)
    {
        const auto environment = currentObservationEnvironment();
        if (environment != RaidLightEnvironment::DayOutdoor)
        {
            const auto powered = environment == RaidLightEnvironment::WeakIndoor
                ? nightVisionIndoor_ : nightVisionOutdoor_;
            ambient.angleDegrees = std::max(ambient.angleDegrees, powered.angleDegrees);
            ambient.maximumDistance = std::max(ambient.maximumDistance, powered.maximumDistance);
        }
    }
    if (optic == WeaponOpticMode::None)
        return ambient;
    const auto environment = inOutdoorRaidSpace()
        ? currentObservationEnvironment() : RaidLightEnvironment::WeakIndoor;
    return raidOpticSightParameters(ambient, environment, optic);
}

Vec2 GameplayWorld::currentObservationDirection() const noexcept
{
    if (!lightConeObservationEnabled()) return {1, 0};
    Vec2 direction = observationDirectionFrozen_ ? observationDirection_ : weaponAimDirection();
    const float length=std::hypot(direction.x,direction.y);
    return std::isfinite(length) && length>0.0001F ? Vec2{direction.x/length,direction.y/length} : observationDirection_;
}

void GameplayWorld::setNightVision(RaidSightParameters outdoor, RaidSightParameters indoor) noexcept
{
    nightVisionOutdoor_ = nightVisionSupported() ? outdoor : RaidSightParameters{};
    nightVisionIndoor_ = nightVisionSupported() ? indoor : RaidSightParameters{};
}

void GameplayWorld::setWeaponLight(std::uint64_t device, std::uint64_t battery, float angle, float distance, float radius) noexcept
{
    const bool active = lightConeObservationEnabled_ && device && battery && std::isfinite(angle) && angle>0 && angle<=180 && std::isfinite(distance) && distance>0 && distance<=1500;
    weaponLightDevice_=active?device:0; weaponLightBattery_=active?battery:0;
    weaponLightAngle_=active?angle:0; weaponLightDistance_=active?distance:0;
    const float background=active && std::isfinite(radius)?std::clamp(radius,0.0F,200.0F):0;
    if (background!=weaponLightBackgroundRadius_) indoorObservationCacheReady_=false;
    weaponLightBackgroundRadius_=background;
}

const std::shared_ptr<const RaidIlluminationCoverage> &
GameplayWorld::activeThrownIlluminationCoverage() const
{
    static const std::shared_ptr<const RaidIlluminationCoverage> inactive;
    const auto &source = thrownIllumination_.source();
    if (!source || !raidSession_.isActive() || source->landing.space != activeRaidSpaceId())
        return inactive;
    const auto &landing = source->landing;
    return getRaidIlluminationCoverage(thrownIlluminationCoverage_, activeBlockerIndex(),
        landing.space, activeGeometryRevision_, landing.effectIdentity, landing.point, landing.use.radius);
}

const std::array<RaidVisibilityProjection, 2> &
GameplayWorld::thrownIlluminationBackgroundObservations() const
{
    static const std::array<RaidVisibilityProjection, 2> inactive;
    const auto &coverage = activeThrownIlluminationCoverage();
    return coverage ? coverage->fans : inactive;
}

RaidIlluminationObservationKey GameplayWorld::currentThrownIlluminationObservation() const
{
    // Use the map's own observation capability, not an optic temporarily
    // enabling observation on a map whose ordinary sight rules stay unchanged.
    if (!thrownIllumination_.source() || !lightConeObservationEnabled_ ||
        !isDarkForNightVision())
        return {};
    const auto &coverage = activeThrownIlluminationCoverage();
    if (!coverage)
        return {};
    const auto &optic = weaponOpticView();
    return raidIlluminationObservationKey(coverage, optic ? &*optic : nullptr,
        weaponAimWorldPosition());
}

float GameplayWorld::thrownIlluminationGlareSeconds() const noexcept
{
    const auto &source = thrownIllumination_.source();
    return source && source->nightVisionGlareTriggered &&
        source->landing.space == activeRaidSpaceId() && nightVisionOutdoor_.maximumDistance > 0 &&
        isDarkForNightVision() ? thrownIlluminationGlareSeconds_ : 0.0F;
}

void GameplayWorld::updateThrownIlluminationGlare(float seconds)
{
    const auto &source = thrownIllumination_.source();
    if (!source)
    {
        thrownIlluminationGlareSeconds_ = 0.0F;
        return;
    }
    thrownIlluminationGlareSeconds_ = std::max(
        0.0F, thrownIlluminationGlareSeconds_ - seconds);
    if (source->nightVisionGlareTriggered || source->landing.space != activeRaidSpaceId() ||
        nightVisionOutdoor_.maximumDistance <= 0 || !isDarkForNightVision())
        return;
    const auto center = playerCenter(player_);
    const auto point = source->landing.point;
    const float dx = point.x - center.x, dy = point.y - center.y;
    if (dx * dx + dy * dy > 120.0F * 120.0F)
        return;
    const auto &observation = raidObservation();
    if (!observation.illuminationKey.insideLens(point) || !observation.pointVisible(point))
        return;
    thrownIllumination_.markNightVisionGlareTriggered();
    thrownIlluminationGlareSeconds_ = 0.35F;
}

RaidObservationKey GameplayWorld::raidObservationKey() const
{
    const auto parameters = currentObservationParameters();
    return {activeRaidSpaceId(),
            playerCenter(player_),
            currentObservationDirection(),
            parameters.angleDegrees,
            parameters.maximumDistance,
            currentObservationEnvironment(),
            activeGeometryRevision_, weaponLightDevice_, weaponLightBattery_, weaponLightAngle_, weaponLightDistance_,
            currentThrownIlluminationObservation(), activeSmokeSight_};
}

const RaidVisibilityProjection &GameplayWorld::indoorObservation() const
{
    const Vec2 center = playerCenter(player_);
    if (!isAlphaRaidWorld())
    {
        indoorObservationCache_.valid = false;
        indoorObservationCacheReady_ = false;
        return indoorObservationCache_;
    }
    const auto direction = currentObservationDirection();
    const auto parameters = currentObservationParameters();
    const auto illumination = currentThrownIlluminationObservation();
    // Compare borrowed space identity and scalar parameters on the hot path.
    // Only a rebuild or a submitted command needs an owned context string.
    if (!indoorObservationCacheReady_ ||
        indoorObservationCache_.spaceId != activeRaidSpaceId() ||
        indoorObservationCache_.observer.x != center.x ||
        indoorObservationCache_.observer.y != center.y ||
        indoorObservationCache_.direction.x != direction.x ||
        indoorObservationCache_.direction.y != direction.y ||
        indoorObservationCache_.angleDegrees != parameters.angleDegrees ||
        indoorObservationCache_.maximumDistance != parameters.maximumDistance ||
        indoorObservationCache_.environment !=
            currentObservationEnvironment() ||
        indoorObservationCache_.geometryRevision != activeGeometryRevision_ || indoorObservationCache_.lightAssetId != weaponLightDevice_ ||
        indoorObservationCache_.lightBatteryId != weaponLightBattery_ || indoorObservationCache_.lightAngleDegrees != weaponLightAngle_ ||
        indoorObservationCache_.lightDistance != weaponLightDistance_ ||
        indoorObservationCache_.illuminationKey != illumination)
    {
        const RaidObservationKey key{activeRaidSpaceId(), center, direction, parameters.angleDegrees,
            parameters.maximumDistance, currentObservationEnvironment(), activeGeometryRevision_, weaponLightDevice_,
            weaponLightBattery_, weaponLightAngle_, weaponLightDistance_, illumination, activeSmokeSight_};
        enemyObservationCache_.clear();
        enemyObservationIndexReady_ = false;
        if (lightConeObservationEnabled())
            indoorObservationCache_ = buildRaidSpaceConeVisibility(
                activeBlockerIndex(), key, &observationWork_);
        else if (!inOutdoorRaidSpace())
        {
            indoorObservationCache_ = buildRaidSpaceVisibility(
                activeBlockerIndex(), activeRaidSpaceId(), center);
            indoorObservationCache_.geometryRevision = activeGeometryRevision_;
        }
        else
        {
            // Out-of-scope activities retain their existing unobstructed
            // presentation, but submissions still receive a current context.
            indoorObservationCache_ = {};
            indoorObservationCache_.spaceId = activeRaidSpaceId();
            indoorObservationCache_.observer = center;
            indoorObservationCache_.worldSize = activeWorldSize();
            indoorObservationCache_.geometryRevision = activeGeometryRevision_;
            const auto size = activeWorldSize();
            indoorObservationCache_.boundary = {
                {0, 0}, {size.x, 0}, size, {0, size.y}};
            indoorObservationCache_.valid = true;
        }
        if (illumination.source)
            addRaidIlluminatedObservation(indoorObservationCache_, activeBlockerIndex(),
                key, &observationWork_);
        indoorObservationCache_.smoke = activeSmokeSight_;
        observationBoundsQuery_ =
            RaidVisibilityBoundsQuery{indoorObservationCache_};
        weaponLightBackgroundCache_ = {};
        if (weaponLightDevice_ && weaponLightBackgroundRadius_>0)
            for (std::size_t side=0; side<2; ++side)
            {
                RaidObservationKey near{activeRaidSpaceId(), center, {side ? -1.0F : 1.0F,0}, 180,
                                        weaponLightBackgroundRadius_, currentObservationEnvironment(),activeGeometryRevision_};
                RaidObservationWork nearWork;
                weaponLightBackgroundCache_[side]=buildRaidSpaceConeVisibility(activeBlockerIndex(),near,&nearWork);
                observationWork_.localBlockers+=nearWork.localBlockers; observationWork_.rays+=nearWork.rays;
                observationWork_.blockerTests+=nearWork.blockerTests;
            }
        ++indoorObservationBuildCount_;
        indoorObservationCacheReady_ = true;
    }
    return indoorObservationCache_;
}

bool GameplayWorld::raidBoundsObservable(Rect bounds) const
{
    if (!isAlphaRaidWorld())
        return true;
    static_cast<void>(indoorObservation());
    return observationBoundsQuery_.boundsVisible(bounds);
}

HitFeedbackPresentationSnapshot GameplayWorld::observableHitFeedback() const
{
    auto feedback = hitFeedbackPresentation();
    if (!isAlphaRaidWorld() || !lightConeObservationEnabled())
        return feedback;
    const auto visible = [&](const std::optional<HitFeedbackSource> &source)
    {
        if (!source || source->targetId == kInvalidCombatTargetId ||
            !raidObservation().pointVisible(source->position))
            return false;
        // Removed targets retain the actual impact fact; living targets must
        // still be observable at their current position. Never infer a hit.
        return source->targetKilled || raidEnemyObservable(source->targetId);
    };
    if (!visible(feedback.semanticSource))
    {
        feedback.remainingSeconds = 0;
        feedback.semantic = HitSemantic::Normal;
    }
    if (!visible(feedback.armorSource))
    {
        feedback.armorFeedbackSeconds = 0;
        feedback.armorBroken = false;
    }
    return feedback;
}

bool GameplayWorld::raidEnemyObservable(CombatTargetId id) const
{
    if (!lightConeObservationEnabled())
    {
        const Enemy *found = activeEnemies().find(id);
        return found && raidBoundsObservable(found->bounds());
    }
    static_cast<void>(raidObservation()); // Refresh pose/light/space before
                                          // cached eligibility.
    if (!enemyObservationIndexReady_)
    {
        enemyObservationCache_.reserve(activeEnemies().size());
        for (std::size_t position = 0; position < activeEnemies().size();
             ++position)
            enemyObservationCache_.push_back(
                {activeEnemies()[position].combatTargetId(),
                 {},
                 false,
                 false,
                 position});
        std::sort(enemyObservationCache_.begin(), enemyObservationCache_.end(),
                  [](const auto &a, const auto &b) { return a.id < b.id; });
        enemyObservationIndexReady_ = true;
    }
    const auto cached = std::lower_bound(
        enemyObservationCache_.begin(), enemyObservationCache_.end(), id,
        [](const auto &entry, CombatTargetId target)
        { return entry.id < target; });
    if (cached == enemyObservationCache_.end() || cached->id != id)
        return false;
    if (cached->position >= activeEnemies().size())
        return false;
    const Enemy *found = &activeEnemies()[cached->position];
    if (found->combatTargetId() != id || found->isDead())
        return false;
    const auto bounds = found->bounds();
    if (cached->computed)
    {
        const auto previous = cached->bounds;
        if (previous.position.x == bounds.position.x &&
            previous.position.y == bounds.position.y &&
            previous.size.x == bounds.size.x &&
            previous.size.y == bounds.size.y)
            return cached->observable;
    }
    ++enemyObservationSolveCount_;
    const bool observable = observationBoundsQuery_.boundsVisible(bounds);
    cached->bounds = bounds;
    cached->observable = observable;
    cached->computed = true;
    return observable;
}

std::optional<Rect> GameplayWorld::visibleLostWeaponCarrier() const
{
    if (!lostWeaponPursuitFallback_ || lostWeaponCarrierDeath_ ||
        !inOutdoorRaidSpace() || !raidSession_.isActive() || player_.isDead())
    {
        return std::nullopt;
    }
    const Enemy *carrier = enemies_.find(lostWeaponCarrierId_);
    if (!carrier || !activeBlockerIndex().hasLineOfSight(playerCenter(player_), enemyCenter(*carrier)) ||
        !raidEnemyObservable(lostWeaponCarrierId_))
    {
        return std::nullopt;
    }
    return carrier->bounds();
}

bool GameplayWorld::lostWeaponCorpseObservable() const
{
    if (!lostWeaponCarrierDeath_ || !inOutdoorRaidSpace() || !raidSession_.isActive() ||
        player_.isDead())
    {
        return false;
    }
    const Vec2 point = *lostWeaponCarrierDeath_;
    return activeBlockerIndex().hasLineOfSight(playerCenter(player_), point) &&
        raidBoundsObservable(centeredBounds(point, kLostWeaponCorpseHalfSize));
}

std::vector<RaidSpacePortalProjection>
GameplayWorld::visibleRaidSpacePortals() const
{
    std::vector<RaidSpacePortalProjection> result;
    const Vec2 center = playerCenter(player_);
    if (activeInteriorIndex_.has_value())
    {
        const InteriorRuntime &interior = interiors_[*activeInteriorIndex_];
        result.push_back(RaidSpacePortalProjection{
            interior.id,
            "Outdoor",
            interior.interiorExit,
            true,
            pointInside(interior.interiorExit, center)});
        return result;
    }
    result.reserve(interiors_.size());
    for (const InteriorRuntime &interior : interiors_)
    {
        if (tacticalMap_.specialLocationVisible(interior.id))
        {
            result.push_back(RaidSpacePortalProjection{
                interior.id,
                interior.displayName,
                interior.exteriorEntrance,
                false,
                pointInside(interior.exteriorEntrance, center)});
        }
    }
    return result;
}

bool GameplayWorld::raidSpacePortalInteractionInRange() const noexcept
{
    if (!raidSession_.isActive())
    {
        return false;
    }
    const Vec2 center = playerCenter(player_);
    if (activeInteriorIndex_.has_value())
    {
        return pointInside(
            interiors_[*activeInteriorIndex_].interiorExit, center);
    }
    return std::any_of(
        interiors_.begin(), interiors_.end(),
        [&](const InteriorRuntime &interior)
        { return pointInside(interior.exteriorEntrance, center); });
}

bool GameplayWorld::spaceTransitionedLastUpdate() const noexcept
{
    return spaceTransitionedLastUpdate_;
}

std::optional<RaidInteriorMapProjection>
GameplayWorld::activeInteriorMapProjection() const noexcept
{
    if (!activeInteriorIndex_.has_value())
    {
        return std::nullopt;
    }
    const InteriorRuntime &interior = interiors_[*activeInteriorIndex_];
    const Vec2 center{player_.position().x + player_.size() * 0.5F,
                      player_.position().y + player_.size() * 0.5F};
    constexpr float localRadius = 180.0F;
    const Vec2 minimum{std::max(0.0F, center.x - localRadius),
                       std::max(0.0F, center.y - localRadius)};
    const Vec2 maximum{std::min(interior.worldSize.x, center.x + localRadius),
                       std::min(interior.worldSize.y, center.y + localRadius)};
    return RaidInteriorMapProjection{
        interior.id,
        interior.displayName,
        interior.worldSize,
        interior.interiorExit,
        interior.ballisticBlockers,
        interior.layoutKnown ? ContentRect{{}, interior.worldSize}
            : ContentRect{minimum, {maximum.x - minimum.x, maximum.y - minimum.y}},
        interior.layoutKnown};
}

void GameplayWorld::prepareFreightBreachDoor()
{
    const auto room = std::find_if(interiors_.begin(), interiors_.end(),
        [](const InteriorRuntime &interior) { return interior.id == kFreightCrateSpaceId; });
    if (room == interiors_.end())
    {
        throw std::invalid_argument{"Freight breach requires its freight interior"};
    }
    PreparedFreightBreach open;
    open.interiorIndex = static_cast<std::size_t>(room - interiors_.begin());
    open.blockers = room->ballisticBlockers;
    const auto divider = std::find_if(open.blockers.begin(), open.blockers.end(),
        [](const BallisticBlocker &blocker)
        {
            const auto &bounds = blocker.bounds;
            return bounds.position.x == kFreightDivider.position.x &&
                bounds.position.y == kFreightDivider.position.y &&
                bounds.size.x == kFreightDivider.size.x &&
                bounds.size.y == kFreightDivider.size.y;
        });
    const auto lastId = std::max_element(open.blockers.begin(), open.blockers.end(),
        [](const BallisticBlocker &left, const BallisticBlocker &right) { return left.id < right.id; });
    if (divider == open.blockers.end() ||
        lastId->id == std::numeric_limits<BallisticBlockerId>::max())
    {
        throw std::invalid_argument{"Freight breach divider is invalid"};
    }
    const auto lowerId = lastId->id + 1U;
    divider->bounds.size.y = kFreightBreachDoor.position.y - kFreightDivider.position.y;
    const float lowerY = kFreightBreachDoor.position.y + kFreightBreachDoor.size.y;
    open.blockers.push_back({lowerId, {{kFreightDivider.position.x, lowerY},
        {kFreightDivider.size.x, kFreightDivider.position.y + kFreightDivider.size.y - lowerY}}});
    open.blockerIndex = RaidSpaceBlockerIndex::build(room->worldSize, open.blockers);
    if (!open.blockerIndex)
    {
        throw std::invalid_argument{"Freight breach blocker index is invalid"};
    }
    for (const auto &closed : navigationFieldCache_)
    {
        if (closed.spaceId != room->id)
        {
            continue;
        }
        auto field = RaidSpaceNavigationField::build(closed.actorSize, room->worldSize, open.blockers, 2.0F);
        if (!field)
        {
            throw std::invalid_argument{"Freight breach navigation field is invalid"};
        }
        open.navigationFields.push_back({room->id, closed.actorSize, std::move(*field)});
    }
    preparedFreightBreach_ = std::move(open);
}

std::optional<ContentRect> GameplayWorld::freightBreachDoorBounds() const noexcept
{
    if (!preparedFreightBreach_ || activeInteriorIndex_ != preparedFreightBreach_->interiorIndex)
    {
        return std::nullopt;
    }
    return kFreightBreachDoor;
}

std::optional<ContentRect> GameplayWorld::nearbyFreightBreachDoor() const
{
    const auto door = freightBreachDoorBounds();
    if (!door || !raidSession_.isActive() || player_.isDead() || player_.isControlled())
    {
        return std::nullopt;
    }
    const Vec2 center = playerCenter(player_);
    const float left = door->position.x;
    const float right = left + door->size.x;
    if (center.x >= left && center.x <= right)
    {
        return std::nullopt;
    }
    const bool fromLeft = center.x < left;
    const float faceX = fromLeft ? left : right;
    const Vec2 face{faceX, std::clamp(center.y, door->position.y, door->position.y + door->size.y)};
    if (distanceSquared(center, face) > kFreightBreachReach * kFreightBreachReach)
    {
        return std::nullopt;
    }
    // Observe the near surface without casting into the closed divider itself.
    const Vec2 visibleFace{face.x + (fromLeft ? -1.0F : 1.0F), face.y};
    if (!activeBlockerIndex().hasLineOfSight(center, visibleFace))
    {
        return std::nullopt;
    }
    static_cast<void>(raidObservation());
    return observationBoundsQuery_.pointVisible(visibleFace) ? door : std::nullopt;
}

bool GameplayWorld::tryOpenFreightBreachDoor(const std::function<bool()> &commitWedge)
{
    const auto door = nearbyFreightBreachDoor();
    if (!door || !commitWedge)
    {
        return false;
    }
    auto &room = interiors_[*activeInteriorIndex_];
    const Vec2 noise{door->position.x + door->size.x * 0.5F,
                     door->position.y + door->size.y * 0.5F};
    // Prepare nodes before the asset transaction so publishing the pulse and
    // installing the prebuilt geometry need no allocations after consumption.
    decltype(localNoiseInvestigations_) listeners;
    for (const Enemy &enemy : room.enemies)
    {
        const auto id = enemy.combatTargetId();
        if (!enemy.isDead() && room.enemies.state(id).noiseResponsive &&
            enemy.awarenessState() != EnemyAwarenessState::Alerted &&
            enemy.attackPhase() == EnemyAttackPhase::Idle &&
            distanceSquared(enemyCenter(enemy), noise) <= kFreightBreachNoiseRadius * kFreightBreachNoiseRadius)
        {
            listeners.emplace(id, LocalNoiseInvestigation{room.id, noise, 0.0F, false});
        }
    }
    if (!commitWedge())
    {
        return false;
    }

    auto &open = *preparedFreightBreach_;
    room.ballisticBlockers = std::move(open.blockers);
    room.blockerIndex = std::move(open.blockerIndex);
    for (auto &field : navigationFieldCache_)
    {
        if (field.spaceId != room.id)
        {
            continue;
        }
        const auto prepared = std::find_if(open.navigationFields.begin(), open.navigationFields.end(),
            [&](const NavigationFieldCache &entry)
            { return entry.actorSize.x == field.actorSize.x && entry.actorSize.y == field.actorSize.y; });
        if (prepared != open.navigationFields.end())
        {
            field.field = std::move(prepared->field);
        }
    }
    // No stale field may survive if an unprepared footprint was queried
    // before the door opened. Normal freight actors have prebuilt fields.
    std::erase_if(navigationFieldCache_, [&](const NavigationFieldCache &field)
    {
        return field.spaceId == room.id &&
            std::none_of(open.navigationFields.begin(), open.navigationFields.end(),
                [&](const NavigationFieldCache &entry)
                { return entry.actorSize.x == field.actorSize.x && entry.actorSize.y == field.actorSize.y; });
    });
    preparedFreightBreach_.reset();
    activeGeometryRevision_ = ++room.geometryRevision;
    indoorObservationCacheReady_ = false;
    enemyObservationIndexReady_ = false;
    for (Enemy &enemy : room.enemies)
    {
        const auto id = enemy.combatTargetId();
        auto &navigation = room.enemies.state(id).navigation;
        navigation.waypoint.reset();
        navigation.initialized = false;
        if (auto listener = listeners.find(id); listener != listeners.end())
        {
            noiseDecoy_.forget(id);
            enemy.investigateSound(noise);
            listener->second.remaining = enemy.searchTimeRemaining();
            localNoiseInvestigations_.erase(id);
        }
    }
    localNoiseInvestigations_.merge(listeners);
    return true;
}

void GameplayWorld::prepareRoadBlockade(const RaidRoadBlockadeSnapshot &snapshot)
{
    const auto &bounds = snapshot.bounds;
    const Vec2 maximum{bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y};
    if (!std::isfinite(bounds.size.x) || !std::isfinite(bounds.size.y) ||
        bounds.size.x <= 0.0F || bounds.size.y <= 0.0F ||
        !pointInside(ContentRect{{}, worldSize_}, bounds.position) ||
        !pointInside(ContentRect{{}, worldSize_}, maximum))
    {
        throw std::invalid_argument{"Road blockade bounds are invalid"};
    }

    BallisticBlockerId nextId = 0;
    for (const BallisticBlocker &blocker : ballisticBlockers_)
        nextId = std::max(nextId, blocker.id);
    if (nextId == std::numeric_limits<BallisticBlockerId>::max())
        throw std::invalid_argument{"Road blockade blocker ID is unavailable"};

    RoadBlockadeRuntime blockade;
    blockade.bounds = bounds;
    blockade.inactiveBlockers = ballisticBlockers_;
    blockade.inactiveBlockers.push_back({nextId + 1, {bounds.position, bounds.size}});
    blockade.inactiveBlockerIndex = RaidSpaceBlockerIndex::build(worldSize_, blockade.inactiveBlockers);
    if (!blockade.inactiveBlockerIndex)
        throw std::invalid_argument{"Road blockade blocker index is invalid"};

    const auto prepareFootprint = [&](Vec2 size)
    {
        const auto &outdoor = outdoorRaidSpaceId();
        cacheNavigationFieldForSpace(navigationFieldCache_, outdoor, size, worldSize_, ballisticBlockers_);
        cacheNavigationFieldForSpace(blockade.closedNavigationFields, outdoor, size, worldSize_,
            blockade.inactiveBlockers);
    };
    for (const Enemy &enemy : enemies_)
        prepareFootprint(enemy.size());
    for (const EnemySpawn &spawn : highRiskPressureSpawns_)
        prepareFootprint(spawn.size);
    if (fireSupport_) prepareFootprint(RaidFireSupportRuntime::kSize);
    roadBlockade_ = std::move(blockade);
}

bool GameplayWorld::roadBlockadeOccupied() const noexcept
{
    const ContentRect &bounds = roadBlockade_->bounds;
    const Rect blockade{bounds.position, bounds.size};
    if (inOutdoorRaidSpace() && !player_.isDead() && isCollision(playerBounds(player_), blockade))
        return true;

    if (fireSupport_ && !fireSupportInteriorIndex_ && isCollision(fireSupport_->bounds(), blockade))
        return true;
    return std::any_of(enemies_.begin(), enemies_.end(), [&](const Enemy &enemy)
    {
        return !enemy.isDead() && isCollision(enemy.bounds(), blockade);
    });
}

void GameplayWorld::switchRoadBlockadeGeometry() noexcept
{
    auto &blockade = *roadBlockade_;
    ballisticBlockers_.swap(blockade.inactiveBlockers);
    outdoorBlockerIndex_.swap(blockade.inactiveBlockerIndex);
    ++outdoorGeometryRevision_;
    if (inOutdoorRaidSpace())
    {
        activeGeometryRevision_ = outdoorGeometryRevision_;
        indoorObservationCacheReady_ = false;
        enemyObservationIndexReady_ = false;
    }
    for (const Enemy &enemy : enemies_)
    {
        auto &navigation = enemies_.state(enemy.combatTargetId()).navigation;
        navigation.waypoint.reset();
        navigation.initialized = false;
    }
}

void GameplayWorld::setRoadBlockadePhase(RaidRoadBlockadePhase phase) noexcept
{
    roadBlockade_->phase = phase;
    tacticalMap_.updateDiscoveredRoadBlockadePhase(phase);
}

void GameplayWorld::updateRoadBlockade(float highRiskDeltaTime)
{
    if (raidSession_.phase() != RaidPhase::HighRisk)
        return;

    auto &blockade = *roadBlockade_;
    if (blockade.phase == RaidRoadBlockadePhase::Preparing)
    {
        blockade.secondsRemaining = kRoadBlockadeWarningSeconds + kRoadBlockadeOccupancyGraceSeconds;
        setRoadBlockadePhase(RaidRoadBlockadePhase::Warning);
    }
    if (blockade.phase == RaidRoadBlockadePhase::Warning)
    {
        blockade.secondsRemaining = std::max(0.0F, blockade.secondsRemaining - highRiskDeltaTime);
        if (blockade.secondsRemaining <= kRoadBlockadeOccupancyGraceSeconds)
        {
            if (!roadBlockadeOccupied())
            {
                switchRoadBlockadeGeometry();
                setRoadBlockadePhase(RaidRoadBlockadePhase::Closed);
            }
            else if (blockade.secondsRemaining <= 0.0F)
            {
                setRoadBlockadePhase(RaidRoadBlockadePhase::Skipped);
            }
        }
    }
    if ((blockade.phase == RaidRoadBlockadePhase::Warning ||
         blockade.phase == RaidRoadBlockadePhase::Closed) &&
        inOutdoorRaidSpace() && !tacticalMap_.discoveredRoadBlockade() &&
        roadBlockadeFaceObservable(playerCenter(player_)))
    {
        tacticalMap_.observeRoadBlockade({blockade.bounds, blockade.phase});
    }
}

bool GameplayWorld::roadBlockadeFaceObservable(Vec2 center, std::optional<float> maximumReach) const
{
    const ContentRect &bounds = roadBlockade_->bounds;
    const Vec2 closest = nearestPointOnRect(bounds, center);
    if (lightConeObservationEnabled())
    {
        float maximum = std::max(currentObservationParameters().maximumDistance, weaponLightDistance_);
        if (thrownIllumination_.source())
        {
            maximum = std::max(maximum,
                raidSightParameters(RaidLightEnvironment::DayOutdoor).maximumDistance);
        }
        if (distanceSquared(center, closest) > maximum * maximum)
            return false;
    }

    const auto sampleVisible = [&](Vec2 point)
    {
        const float distanceToFaceSquared = distanceSquared(center, point);
        if (maximumReach && distanceToFaceSquared > *maximumReach * *maximumReach)
            return false;
        // Sample just in front of the face, toward the observer.
        const float offset = std::max(1.0F, std::sqrt(distanceToFaceSquared));
        const Vec2 sample{point.x + (center.x - point.x) / offset,
            point.y + (center.y - point.y) / offset};
        if (!activeBlockerIndex().hasLineOfSight(center, sample))
            return false;
        static_cast<void>(raidObservation());
        return observationBoundsQuery_.pointVisible(sample);
    };
    if (sampleVisible(closest))
        return true;

    // A wall or a cone boundary can hide the nearest point while exposing
    // another part of the face.
    const Vec2 farCorner{bounds.position.x + bounds.size.x, bounds.position.y + bounds.size.y};
    const bool outsideHorizontal = center.x < bounds.position.x || center.x > farCorner.x;
    if (outsideHorizontal &&
        (sampleVisible({closest.x, bounds.position.y}) || sampleVisible({closest.x, farCorner.y})))
        return true;
    const bool outsideVertical = center.y < bounds.position.y || center.y > farCorner.y;
    return outsideVertical &&
        (sampleVisible({bounds.position.x, closest.y}) || sampleVisible({farCorner.x, closest.y}));
}

std::optional<RaidRoadBlockadeView> GameplayWorld::roadBlockade() const
{
    if (!roadBlockade_)
        return std::nullopt;

    const auto &blockade = *roadBlockade_;
    RaidRoadBlockadeView view{blockade.phase, blockade.bounds};
    if (blockade.phase == RaidRoadBlockadePhase::Warning)
    {
        view.awaitingClear = blockade.secondsRemaining <= kRoadBlockadeOccupancyGraceSeconds;
        view.phaseSecondsRemaining = view.awaitingClear
            ? blockade.secondsRemaining
            : blockade.secondsRemaining - kRoadBlockadeOccupancyGraceSeconds;
    }
    if (!inOutdoorRaidSpace() || !raidSession_.isActive() || player_.isDead() ||
        (blockade.phase != RaidRoadBlockadePhase::Warning &&
         blockade.phase != RaidRoadBlockadePhase::Closed))
    {
        return view;
    }

    const Vec2 center = playerCenter(player_);
    if (blockade.phase == RaidRoadBlockadePhase::Closed)
    {
        view.nearby = distanceSquared(center, nearestPointOnRect(blockade.bounds, center)) <=
            kRoadBlockadeReach * kRoadBlockadeReach;
        view.canInteract = view.nearby && !player_.isControlled() &&
            roadBlockadeFaceObservable(center, kRoadBlockadeReach);
    }
    view.visible = view.canInteract || roadBlockadeFaceObservable(center);
    return view;
}

GameplayWorld::LocalNoiseListeners GameplayWorld::prepareOutdoorLocalNoise(
    Vec2 origin, float radius) const
{
    LocalNoiseListeners listeners;
    const auto &space = outdoorRaidSpaceId();
    for (const Enemy &enemy : enemies_)
    {
        const auto id = enemy.combatTargetId();
        if (!enemy.isDead() && enemies_.state(id).noiseResponsive &&
            enemy.awarenessState() != EnemyAwarenessState::Alerted &&
            enemy.attackPhase() == EnemyAttackPhase::Idle &&
            distanceSquared(enemyCenter(enemy), origin) <= radius * radius)
        {
            listeners.emplace(id, LocalNoiseInvestigation{space, origin, 0.0F, false});
        }
    }
    return listeners;
}

void GameplayWorld::publishOutdoorLocalNoise(LocalNoiseListeners listeners, Vec2 origin)
{
    for (Enemy &enemy : enemies_)
    {
        const auto id = enemy.combatTargetId();
        const auto listener = listeners.find(id);
        if (listener == listeners.end())
            continue;
        noiseDecoy_.forget(id);
        enemy.investigateSound(origin);
        listener->second.remaining = enemy.searchTimeRemaining();
        localNoiseInvestigations_.erase(id);
    }
    localNoiseInvestigations_.merge(listeners);
}

bool GameplayWorld::tryOpenRoadBlockade(const std::function<bool()> &commitWedge)
{
    const auto view = roadBlockade();
    if (!view || !view->canInteract || !commitWedge)
        return false;

    const Vec2 noise{view->bounds.position.x + view->bounds.size.x * 0.5F,
        view->bounds.position.y + view->bounds.size.y * 0.5F};
    // Reserve the local sound event before touching the asset. On success,
    // all resulting world changes use the prepared geometry and map nodes.
    auto listeners = prepareOutdoorLocalNoise(noise, kRoadBlockadeBreachNoiseRadius);
    if (!commitWedge())
        return false;

    switchRoadBlockadeGeometry();
    setRoadBlockadePhase(RaidRoadBlockadePhase::Breached);
    publishOutdoorLocalNoise(std::move(listeners), noise);
    return true;
}

void GameplayWorld::confirmRaidInteriorLayout(const RaidSpaceDefinitionId &spaceId) noexcept
{
    for (InteriorRuntime &interior : interiors_)
    {
        if (interior.id == spaceId)
        {
            interior.layoutKnown = true;
            return;
        }
    }
}

const RaidOutdoorPresentationProjection &GameplayWorld::outdoorPresentation(
    ContentRect visibleWorldBounds) const
{
    if (!inOutdoorRaidSpace() || outdoorLayout_.layoutVersion < 3U ||
        outdoorPresentationChunks_.empty() ||
        !std::isfinite(visibleWorldBounds.position.x) ||
        !std::isfinite(visibleWorldBounds.position.y) ||
        !std::isfinite(visibleWorldBounds.size.x) ||
        !std::isfinite(visibleWorldBounds.size.y) ||
        visibleWorldBounds.size.x <= 0.0F ||
        visibleWorldBounds.size.y <= 0.0F)
    {
        outdoorPresentationCache_ = {};
        outdoorPresentationCacheValid_ = false;
        return outdoorPresentationCache_;
    }

    const float cellWidth = worldSize_.x /
        static_cast<float>(outdoorColumns_);
    const float cellHeight = worldSize_.y /
        static_cast<float>(outdoorRows_);
    const float bufferX = cellWidth * 2.0F;
    const float bufferY = cellHeight * 2.0F;
    const float firstX = std::clamp(
        visibleWorldBounds.position.x - bufferX, 0.0F, worldSize_.x);
    const float firstY = std::clamp(
        visibleWorldBounds.position.y - bufferY, 0.0F, worldSize_.y);
    const float lastX = std::clamp(
        visibleWorldBounds.position.x + visibleWorldBounds.size.x + bufferX,
        0.0F, worldSize_.x);
    const float lastY = std::clamp(
        visibleWorldBounds.position.y + visibleWorldBounds.size.y + bufferY,
        0.0F, worldSize_.y);
    const float chunkWidth = cellWidth *
        static_cast<float>(outdoorChunkSizeCells_);
    const float chunkHeight = cellHeight *
        static_cast<float>(outdoorChunkSizeCells_);
    const std::uint32_t firstChunkColumn = std::min(
        static_cast<std::uint32_t>(firstX / chunkWidth),
        outdoorChunkColumns_ - 1U);
    const std::uint32_t lastChunkColumn = std::min(
        static_cast<std::uint32_t>(lastX / chunkWidth),
        outdoorChunkColumns_ - 1U);
    const std::uint32_t firstChunkRow = std::min(
        static_cast<std::uint32_t>(firstY / chunkHeight),
        outdoorChunkRows_ - 1U);
    const std::uint32_t lastChunkRow = std::min(
        static_cast<std::uint32_t>(lastY / chunkHeight),
        outdoorChunkRows_ - 1U);

    if (outdoorPresentationCacheValid_ &&
        firstChunkColumn == outdoorPresentationFirstChunkColumn_ &&
        lastChunkColumn == outdoorPresentationLastChunkColumn_ &&
        firstChunkRow == outdoorPresentationFirstChunkRow_ &&
        lastChunkRow == outdoorPresentationLastChunkRow_)
    {
        return outdoorPresentationCache_;
    }

    outdoorPresentationCacheValid_ = true;
    outdoorPresentationFirstChunkColumn_ = firstChunkColumn;
    outdoorPresentationLastChunkColumn_ = lastChunkColumn;
    outdoorPresentationFirstChunkRow_ = firstChunkRow;
    outdoorPresentationLastChunkRow_ = lastChunkRow;
    ++outdoorPresentationVisitSequence_;
    if (outdoorPresentationVisitSequence_ == 0U)
    {
        std::fill(outdoorTerrainVisitStamps_.begin(),
                  outdoorTerrainVisitStamps_.end(), 0U);
        std::fill(outdoorRoadVisitStamps_.begin(),
                  outdoorRoadVisitStamps_.end(), 0U);
        std::fill(outdoorPropVisitStamps_.begin(),
                  outdoorPropVisitStamps_.end(), 0U);
        std::fill(outdoorLandmarkVisitStamps_.begin(),
                  outdoorLandmarkVisitStamps_.end(), 0U);
        std::fill(outdoorResourcePointVisitStamps_.begin(),
                  outdoorResourcePointVisitStamps_.end(), 0U);
        std::fill(outdoorDistrictVisitStamps_.begin(),
                  outdoorDistrictVisitStamps_.end(), 0U);
        outdoorPresentationVisitSequence_ = 1U;
    }
    const std::uint32_t visitSequence = outdoorPresentationVisitSequence_;
    auto &result = outdoorPresentationCache_;
    result.terrainSpans.clear();
    result.roadCells.clear();
    result.props.clear();
    result.resourcePoints.clear();
    result.labels.clear();
    result.queriedChunkCount = 0U;
    result.cacheRevision = ++outdoorPresentationCacheRevision_;
    for (std::uint32_t row = firstChunkRow; row <= lastChunkRow; ++row)
    {
        for (std::uint32_t column = firstChunkColumn;
             column <= lastChunkColumn; ++column)
        {
            ++result.queriedChunkCount;
            const OutdoorPresentationChunk &chunk =
                outdoorPresentationChunks_[
                    static_cast<std::size_t>(row) *
                        outdoorChunkColumns_ + column];
            for (const std::size_t index : chunk.terrainSpanIndices)
                if (outdoorTerrainVisitStamps_[index] != visitSequence)
                {
                    outdoorTerrainVisitStamps_[index] = visitSequence;
                    result.terrainSpans.push_back(
                        outdoorLayout_.terrainSpans[index]);
                }
            for (const std::size_t index : chunk.roadCellIndices)
                if (outdoorRoadVisitStamps_[index] != visitSequence)
                {
                    outdoorRoadVisitStamps_[index] = visitSequence;
                    result.roadCells.push_back(
                        outdoorLayout_.roadCells[index]);
                }
            for (const std::size_t index : chunk.propIndices)
                if (outdoorPropVisitStamps_[index] != visitSequence)
                {
                    outdoorPropVisitStamps_[index] = visitSequence;
                    result.props.push_back(outdoorLayout_.props[index]);
                }
            for (const std::size_t index : chunk.landmarkIndices)
                if (outdoorLandmarkVisitStamps_[index] != visitSequence)
                {
                    outdoorLandmarkVisitStamps_[index] = visitSequence;
                    const auto &landmark = outdoorLayout_.landmarks[index];
                    result.labels.push_back({
                        landmark.displayName,
                        {landmark.bounds.position.x +
                             landmark.bounds.size.x * 0.5F,
                         landmark.bounds.position.y + 20.0F},
                        true});
                }
            for (const std::size_t index : chunk.resourcePointIndices)
                if (outdoorResourcePointVisitStamps_[index] != visitSequence)
                {
                    outdoorResourcePointVisitStamps_[index] = visitSequence;
                    result.resourcePoints.push_back(
                        outdoorLayout_.resourcePoints[index]);
                }
            for (const std::size_t index : chunk.districtLabelIndices)
                if (outdoorDistrictVisitStamps_[index] != visitSequence)
                {
                    outdoorDistrictVisitStamps_[index] = visitSequence;
                    result.labels.push_back({
                        outdoorLayout_.districts[index].displayName,
                        outdoorLayout_.districts[index].labelPosition,
                        false});
                }
        }
    }
    return result;
}

const std::vector<Particle> &
GameplayWorld::particles() const
{
    return shooting_.particles();
}

const std::vector<GroundItem> &
GameplayWorld::groundItems() const noexcept
{
    return groundItems_;
}

GridInventory &
GameplayWorld::inventory() noexcept
{
    return inventory_;
}

const GridInventory &
GameplayWorld::inventory() const noexcept
{
    return inventory_;
}

GridInventory &
GameplayWorld::containerInventory() noexcept
{
    return storageCabinet_.inventory();
}

const GridInventory &
GameplayWorld::containerInventory() const noexcept
{
    return storageCabinet_.inventory();
}

const StorageCabinet &
GameplayWorld::storageCabinet() const noexcept
{
    return storageCabinet_;
}

const ExtractionPoint &
GameplayWorld::extractionPoint() const noexcept
{
    return extractionPoint_;
}

const std::optional<ExtractionPoint> &
GameplayWorld::emergencyExtractionPoint() const noexcept
{
    return emergencyExtractionPoint_;
}

const std::optional<ExtractionPoint> &
GameplayWorld::conditionalExtractionPoint() const noexcept
{
    return conditionalExtractionPoint_;
}

std::uint64_t
GameplayWorld::conditionalExtractionMaximumWeightGrams() const noexcept
{
    return conditionalExtractionMaximumWeightGrams_;
}

const std::optional<ExtractionPoint> &
GameplayWorld::credentialExtractionPoint() const noexcept
{
    return credentialExtractionPoint_;
}

bool GameplayWorld::credentialExtractionUnlocked() const noexcept
{
    return credentialExtraction_ && credentialExtraction_->unlockedAtDeploy;
}

const std::optional<RaidCredentialExtractionSnapshot> &
GameplayWorld::credentialExtractionSnapshot() const noexcept
{
    return credentialExtraction_;
}

bool GameplayWorld::credentialInvestigationInRange() const
{
    if (!credentialExtraction_ || credentialExtraction_->investigationCompleted ||
        !raidSession_.isActive() || player_.isDead() || player_.isControlled())
        return false;
    const auto &definition = credentialExtraction_->definition;
    const Vec2 center = playerCenter(player_);
    return activeRaidSpaceId() == definition.investigationSpaceId &&
        distanceSquared(center, definition.investigationPoint) <=
            definition.investigationRadius * definition.investigationRadius &&
        activeBlockerIndex().hasLineOfSight(center, definition.investigationPoint) &&
        raidBoundsObservable(Rect{
            {definition.investigationPoint.x - 10.0F,
             definition.investigationPoint.y - 10.0F}, {20.0F, 20.0F}});
}

float GameplayWorld::credentialInvestigationProgress() const noexcept
{
    if (!credentialExtraction_) return 0.0F;
    return std::clamp(credentialInvestigationElapsedSeconds_ /
        credentialExtraction_->definition.investigationDurationSeconds, 0.0F, 1.0F);
}

std::optional<RaidCredentialInvestigationFact>
GameplayWorld::pendingCredentialInvestigation() const
{
    return pendingCredentialInvestigation_;
}

void GameplayWorld::acknowledgeCredentialInvestigation() noexcept
{
    if (credentialExtraction_ && pendingCredentialInvestigation_)
        credentialExtraction_->investigationCompleted = true;
    pendingCredentialInvestigation_.reset();
}

void GameplayWorld::cancelCredentialInvestigationInteraction() noexcept
{
    if (!credentialExtraction_ || !credentialExtraction_->investigationCompleted)
        credentialInvestigationElapsedSeconds_ = 0.0F;
    pendingCredentialInvestigation_.reset();
}

void GameplayWorld::interruptCredentialInteraction() noexcept
{
    cancelCredentialInvestigationInteraction();
    static_cast<void>(raidSession_.interruptCredentialExtraction());
}

void GameplayWorld::interruptFuelExtraction() noexcept
{
    if (raidSession_.interruptFuelExtraction()) fuelExtractionAsset_.reset();
}

std::optional<AssetInstanceId> GameplayWorld::completedFuelExtractionAsset() const noexcept
{
    if (raidSession_.state() != RaidSessionState::Extracted ||
        raidSession_.extractionRoute() != RaidExtractionRoute::Fuel)
        return std::nullopt;
    return fuelExtractionAsset_;
}

const RaidSession &
GameplayWorld::raidSession() const noexcept
{
    return raidSession_;
}

bool GameplayWorld::triggerHighRiskForDeveloper() noexcept
{
    return raidSession_.triggerHighRisk();
}

const RaidTacticalMapState &
GameplayWorld::tacticalMap() const noexcept
{
    return tacticalMap_;
}

std::optional<Vec2> GameplayWorld::visibleFieldIntelligenceContact() const noexcept
{
    if (!fieldIntelligenceContact_ || !inOutdoorRaidSpace() ||
        !raidSession_.isActive() || player_.isDead())
    {
        return std::nullopt;
    }

    const Vec2 center = playerCenter(player_);
    if (distanceSquared(center, *fieldIntelligenceContact_) >
            kFieldIntelligenceDiscoveryRadius * kFieldIntelligenceDiscoveryRadius ||
        smokeBlocksSight(center, *fieldIntelligenceContact_) ||
        !activeBlockerIndex().hasLineOfSight(center, *fieldIntelligenceContact_))
    {
        return std::nullopt;
    }
    return fieldIntelligenceContact_;
}

RaidFieldTradeAccess GameplayWorld::fieldIntelligenceTradeStatus() const noexcept
{
    if (!fieldIntelligenceContact_ || !inOutdoorRaidSpace() ||
        !raidSession_.isActive() || player_.isDead() || player_.isControlled())
    {
        return RaidFieldTradeAccess::Unavailable;
    }

    const Vec2 center = playerCenter(player_);
    if (distanceSquared(center, *fieldIntelligenceContact_) >
        kFieldIntelligenceInteractionRadius * kFieldIntelligenceInteractionRadius)
    {
        return RaidFieldTradeAccess::OutOfRange;
    }

    const auto &blockers = activeBlockerIndex();
    if (smokeBlocksSight(center, *fieldIntelligenceContact_) || !blockers.hasLineOfSight(center, *fieldIntelligenceContact_))
        return RaidFieldTradeAccess::Obstructed;

    for (const Enemy &enemy : activeEnemies())
    {
        if (enemy.isDead())
            continue;
        const Vec2 target = enemyCenter(enemy);
        if (distanceSquared(center, target) <=
                kFieldIntelligenceThreatRadius * kFieldIntelligenceThreatRadius &&
            !smokeBlocksSight(center, target) && blockers.hasLineOfSight(center, target))
        {
            return RaidFieldTradeAccess::Threatened;
        }
    }
    return RaidFieldTradeAccess::Available;
}

void GameplayWorld::grantFieldIntelligence(RaidIntelligenceCategory category) noexcept
{
    tacticalMap_.grantIntelligence(category);
}

std::size_t GameplayWorld::aliveEnemyCount() const noexcept
{
    const std::vector<Enemy> &enemySet = activeEnemies();
    return static_cast<std::size_t>(
        std::count_if(
            enemySet.begin(),
            enemySet.end(),
            [](const Enemy &enemy)
            {
                return !enemy.isDead();
            }));
}

std::size_t GameplayWorld::aliveInitialEnemyCount() const noexcept
{
    const auto countAlivePrefix = [](
        const std::vector<Enemy> &enemies,
        std::size_t initialCount)
    {
        const std::size_t count = std::min(initialCount, enemies.size());
        return static_cast<std::size_t>(std::count_if(
            enemies.begin(), enemies.begin() + count,
            [](const Enemy &enemy) { return !enemy.isDead(); }));
    };

    std::size_t alive = countAlivePrefix(
        enemies_, initialOutdoorEnemyCount_);
    for (const InteriorRuntime &interior : interiors_)
    {
        alive += countAlivePrefix(
            interior.enemies, interior.initialEnemyCount);
    }
    return alive;
}

std::uint32_t GameplayWorld::highRiskPressureWaveCount() const noexcept
{
    return highRiskPressureWaveCount_;
}

std::uint32_t GameplayWorld::highRiskActiveEnemyCap() const noexcept
{
    return highRiskActiveEnemyCap_;
}

float GameplayWorld::highRiskNextWaveSeconds() const noexcept
{
    return std::max(0.0F, highRiskNextWaveSeconds_);
}

const std::optional<ContentRect> &
GameplayWorld::highRiskControlPoint() const noexcept
{
    return highRiskControlPoint_;
}

const std::optional<ContentRect> &
GameplayWorld::highRiskAdvancedResourceArea() const noexcept
{
    return highRiskAdvancedResourceArea_;
}

float GameplayWorld::highRiskControlProgress() const noexcept
{
    if (highRiskActivationDurationSeconds_ <= 0.0F)
    {
        return 0.0F;
    }
    return std::clamp(highRiskActivationElapsedSeconds_ /
                          highRiskActivationDurationSeconds_,
                      0.0F,
                      1.0F);
}

float GameplayWorld::highRiskControlTimeRemaining() const noexcept
{
    return std::max(0.0F,
                    highRiskActivationDurationSeconds_ -
                        highRiskActivationElapsedSeconds_);
}

bool GameplayWorld::highRiskControlInteractionInRange() const noexcept
{
    if (!inOutdoorRaidSpace() || !highRiskControlPoint_.has_value() ||
        raidSession_.phase() != RaidPhase::Regular || !raidSession_.isActive())
    {
        return false;
    }
    return pointInside(*highRiskControlPoint_, playerCenter(player_));
}

const std::optional<ContentRect> &
GameplayWorld::ordinarySurvivorRescuePoint() const noexcept
{
    return ordinarySurvivorRescuePoint_;
}

float GameplayWorld::ordinarySurvivorRescueProgress() const noexcept
{
    if (ordinarySurvivorRescueDurationSeconds_ <= 0.0F)
    {
        return 0.0F;
    }
    return std::clamp(
        ordinarySurvivorRescueElapsedSeconds_ /
            ordinarySurvivorRescueDurationSeconds_,
        0.0F,
        1.0F);
}

float GameplayWorld::ordinarySurvivorRescueTimeRemaining() const noexcept
{
    return std::max(
        0.0F,
        ordinarySurvivorRescueDurationSeconds_ -
            ordinarySurvivorRescueElapsedSeconds_);
}

bool GameplayWorld::ordinarySurvivorRescueInteractionInRange() const noexcept
{
    if (hospitalEscortActor_)
        return raidSession_.isActive() && !ordinarySurvivorRescueSecured_ &&
            (nearHospitalEscort() || hospitalEscortAtHandoff());
    return inOutdoorRaidSpace() && ordinarySurvivorRescuePoint_.has_value() &&
           !ordinarySurvivorRescueSecured_ && raidSession_.isActive() &&
           pointInside(*ordinarySurvivorRescuePoint_, playerCenter(player_));
}

bool GameplayWorld::ordinarySurvivorRescueReady() const noexcept
{
    if (ordinarySurvivorRescueDurationSeconds_ <= 0.0F ||
        ordinarySurvivorRescueElapsedSeconds_ < ordinarySurvivorRescueDurationSeconds_)
        return false;
    return ordinarySurvivorRescueInteractionInRange() &&
        (!hospitalEscortActor_ || (hospitalEscortAtHandoff() && !hospitalEscortThreatened()));
}

bool GameplayWorld::nearHospitalEscort() const noexcept
{
    if (!inOutdoorRaidSpace() || !hospitalEscortActor_ || !hospitalEscortActor_->alive()) return false;
    const Vec2 player = playerCenter(player_), survivor = hospitalEscortActor_->center();
    constexpr float reach = RaidCompanionRuntime::kInteractionReach;
    return distanceSquared(player, survivor) <= reach * reach && !smokeBlocksSight(player, survivor) &&
        activeBlockerIndex().hasLineOfSight(player, survivor);
}

bool GameplayWorld::hospitalEscortAtHandoff() const noexcept
{
    return inOutdoorRaidSpace() && hospitalEscortActor_ && hospitalEscortActor_->alive() &&
        hospitalEscortOrder_ != HospitalEscortState::AwaitingContact &&
        pointInside(*ordinarySurvivorRescuePoint_, playerCenter(player_)) &&
        pointInside(*ordinarySurvivorRescuePoint_, hospitalEscortActor_->center());
}

bool GameplayWorld::hospitalEscortThreatened() const noexcept
{
    if (!inOutdoorRaidSpace() || !hospitalEscortActor_ || !hospitalEscortActor_->alive() || ordinarySurvivorRescueSecured_)
        return false;
    const Vec2 player = playerCenter(player_), survivor = hospitalEscortActor_->center();
    const auto &geometry = activeBlockerIndex();
    for (const auto &enemy : activeEnemies())
    {
        if (enemy.isDead()) continue;
        const Vec2 threat = enemyCenter(enemy);
        constexpr float rangeSquared = 180.0F * 180.0F;
        if ((distanceSquared(threat, survivor) <= rangeSquared && !smokeBlocksSight(threat, survivor) && geometry.hasLineOfSight(threat, survivor)) ||
            (distanceSquared(threat, player) <= rangeSquared && !smokeBlocksSight(threat, player) && geometry.hasLineOfSight(threat, player)))
            return true;
    }
    return false;
}

std::optional<HospitalEscortView> GameplayWorld::hospitalEscort() const
{
    if (!hospitalEscortActor_) return std::nullopt;
    HospitalEscortView view{hospitalEscortActor_->center(), *ordinarySurvivorRescuePoint_,
        hospitalEscortActor_->health(), hospitalEscortOrder_};
    if (ordinarySurvivorRescueSecured_) view.state = HospitalEscortState::Delivered;
    else if (!hospitalEscortActor_->alive()) view.state = HospitalEscortState::Down;
    view.outdoor = inOutdoorRaidSpace();
    view.inInteractionRange = !ordinarySurvivorRescueSecured_ && nearHospitalEscort();
    view.visible = view.outdoor && (view.inInteractionRange || raidBoundsObservable(hospitalEscortActor_->bounds()));
    view.atHandoff = hospitalEscortAtHandoff();
    view.threatened = view.visible && !ordinarySurvivorRescueSecured_ && hospitalEscortThreatened();
    return view;
}

void GameplayWorld::confirmOrdinarySurvivorRescue() noexcept
{
    if (ordinarySurvivorRescueReady())
    {
        ordinarySurvivorRescueSecured_ = true;
    }
}

void GameplayWorld::cancelOrdinarySurvivorRescueInteraction() noexcept
{
    if (!ordinarySurvivorRescueSecured_)
    {
        ordinarySurvivorRescueElapsedSeconds_ = 0.0F;
    }
}

float GameplayWorld::weaponSpreadDegrees() const noexcept
{
    return shooting_.spreadDegrees();
}

float GameplayWorld::weaponVisualRecoilPixels() const noexcept
{
    return shooting_.visualRecoilPixels();
}

WeaponAccuracyProjection
GameplayWorld::weaponAccuracyProjection() const noexcept
{
    return shooting_.accuracyProjection();
}

Vec2 GameplayWorld::weaponAimWorldPosition() const noexcept
{
    return shooting_.aimWorldPosition();
}

Vec2 GameplayWorld::weaponAimDirection() const noexcept
{
    return shooting_.aimDirection();
}

float GameplayWorld::weaponAimDownSightsProgress() const noexcept
{
    return shooting_.aimDownSightsProgress();
}

bool GameplayWorld::weaponAimBeyondEffectiveRange() const noexcept
{
    return shooting_.aimBeyondEffectiveRange();
}

bool GameplayWorld::weaponAimBeyondMaximumRange() const noexcept
{
    return shooting_.aimBeyondMaximumRange();
}

const std::vector<HitResult> &
GameplayWorld::hitResultsLastUpdate() const noexcept
{
    return shooting_.hitResultsLastUpdate();
}

bool GameplayWorld::shotFiredLastUpdate() const noexcept
{
    return shooting_.shotFiredLastUpdate();
}

WeaponAcousticsDefinition GameplayWorld::shotAcousticsLastUpdate() const noexcept
{
    return shooting_.shotAcousticsLastUpdate();
}

std::size_t GameplayWorld::enemiesAlertedLastUpdate() const noexcept
{
    return enemiesAlertedLastUpdate_;
}

std::size_t GameplayWorld::navigationQueriesLastUpdate() const noexcept
{
    return navigationQueriesLastUpdate_;
}

const RaidSimulationWorkload &
GameplayWorld::simulationWorkloadLastUpdate() const noexcept
{
    return simulationWorkloadLastUpdate_;
}

std::vector<std::uint64_t> GameplayWorld::navigationRefreshCounts() const
{
    std::vector<std::uint64_t> counts;
    counts.reserve(activeEnemies().size());
    for (const Enemy &enemy : activeEnemies())
    {
        counts.push_back(activeEnemies().state(enemy.combatTargetId()).navigation.refreshCount);
    }
    return counts;
}

void GameplayWorld::configureWeaponFire(
    const WeaponUseDefinition &definition)
{
    const WeaponHandlingParameters handling =
        deriveWeaponHandling(definition);
    configureWeaponFire(definition, handling, false);
}

void GameplayWorld::configureWeaponFire(
    const WeaponUseDefinition &definition,
    const WeaponHandlingParameters &handling,
    bool preserveWeaponFireTransientState)
{
    shooting_.configureWeapon(
        definition, handling, preserveWeaponFireTransientState);
}

void GameplayWorld::configureWeaponAmmunition(
    int penetration, int unarmoredDamageBonus) noexcept
{
    shooting_.configureAmmunition(penetration, unarmoredDamageBonus);
}

bool GameplayWorld::isAlphaRaidWorld() const noexcept
{
    return alphaRaidWorld_;
}

bool GameplayWorld::restorePlayerHealth(int amount)
{
    if (!raidSession_.isActive() || player_.isDead() || amount <= 0)
    {
        return false;
    }
    return player_.restoreHealth(amount) > 0;
}

bool GameplayWorld::markPlayerDead() noexcept
{
    if (!raidSession_.isActive() ||
        player_.isDead())
    {
        return false;
    }

    // 兼容既有领域命令，但不绕过 Player 唯一拥有的 Health。
    // 活动玩家的当前生命严格大于 0，因此不会触发非法伤害异常。
    return damagePlayer(
        player_.health());
}

bool GameplayWorld::damagePlayer(int damage)
{
    if (!raidSession_.isActive())
    {
        return false;
    }

    const bool killed =
        player_.takeDamage(damage);

    if (damage > 0 && raidSession_.phase() == RaidPhase::Regular)
    {
        highRiskActivationElapsedSeconds_ = 0.0F;
    }
    if (damage > 0)
        cancelCredentialInvestigationInteraction();

    if (!killed)
    {
        return false;
    }

    const bool markedDead =
        raidSession_.markPlayerDead();
    proximityAlarms_ = {};
    alarmAlertSeconds_ = 0;
    alarmTriggeredLastUpdate_ = false;
    melee_.cancelHeavyCharge();
    thrownIllumination_.clear();

    if (!markedDead)
    {
        // 活动态检查与 Health 的首次致死转换之间没有其他状态写入；
        // 到达这里意味着内部不变量已被破坏。
        std::terminate();
    }

    return true;
}

std::vector<PlayerDamageObservation>
GameplayWorld::takePlayerDamageObservations()
{
    std::vector<PlayerDamageObservation> observations;
    observations.swap(pendingPlayerDamageObservations_);
    return observations;
}

bool GameplayWorld::canCallSignalExtraction(const GameplayInput &input) const noexcept
{
    return !grenade_.active() && input.extractionEligible && !input.inventoryOpen &&
        !raidSpacePortalInteractionInRange() &&
        !(input.conditionalExtractionEligible && conditionalExtractionPoint_.has_value() &&
          conditionalExtractionPoint_->contains(playerCenter(player_))) &&
        inOutdoorRaidSpace() && raidSession_.emergencyExtractionOpen() &&
        raidSession_.state() == RaidSessionState::InRaid &&
        !player_.isControlled() && emergencyExtractionPoint_.has_value() &&
        emergencyExtractionPoint_->contains(playerCenter(player_)) &&
        !ordinarySurvivorRescueInteractionInRange();
}

void GameplayWorld::emitPlayerNoise(float radius) noexcept
{
    emitRaidNoise(playerCenter(player_), radius);
}

bool GameplayWorld::setFireSupportOrders(
    FireSupportMovementOrder movement, FireSupportFireOrder fire) noexcept
{
    if (!fireSupport_ || fireSupportInteriorIndex_ != activeInteriorIndex_ ||
        !fireSupport_->alive() || !raidSession_.isActive())
        return false;
    fireSupport_->setOrders(movement, fire);
    return true;
}

std::optional<FireSupportView> GameplayWorld::fireSupport() const
{
    if (!fireSupport_)
        return std::nullopt;
    auto result = fireSupport_->view();
    result.outdoor = inOutdoorRaidSpace();
    result.sameSpace = fireSupportInteriorIndex_ == activeInteriorIndex_;
    result.inInteractionRange = canInteractWithFireSupport();
    result.visible = result.sameSpace &&
        (result.inInteractionRange || raidBoundsObservable(result.bounds));
    return result;
}

std::optional<int> GameplayWorld::fireSupportHealth() const noexcept
{
    return fireSupport_ ? std::optional{fireSupport_->view().health} : std::nullopt;
}

bool GameplayWorld::fireSupportTreatmentBlocked() const noexcept
{
    return !fireSupport_ || fireSupport_->treatmentBlocked();
}

void GameplayWorld::restoreFireSupportHealth(int amount)
{
    if (fireSupport_)
        fireSupport_->restoreHealth(amount);
}

const RaidSpaceDefinitionId &GameplayWorld::fireSupportSpaceId() const noexcept
{
    return fireSupportInteriorIndex_ ? interiors_[*fireSupportInteriorIndex_].id : outdoorRaidSpaceId();
}

std::optional<Vec2> GameplayWorld::fireSupportDeathPosition() const noexcept
{
    return fireSupport_ && !fireSupport_->alive()
        ? std::optional{fireSupport_->center()} : std::nullopt;
}

bool GameplayWorld::canInteractWithFireSupport() const noexcept
{
    if (!fireSupport_ || !raidSession_.isActive() || fireSupportInteriorIndex_ != activeInteriorIndex_ ||
        player_.isDead() || player_.isControlled())
    {
        return false;
    }
    const Vec2 center = playerCenter(player_);
    const Vec2 target = fireSupport_->center();
    constexpr float reach = RaidFireSupportRuntime::kInteractionReach;
    return distanceSquared(center, target) <= reach * reach && !smokeBlocksSight(center, target) &&
        activeBlockerIndex().hasLineOfSight(center, target);
}

std::optional<FireSupportShotEvent> GameplayWorld::fireSupportShotLastUpdate() const noexcept
{
    return fireSupport_ ? fireSupport_->shotEvent() : std::nullopt;
}

void GameplayWorld::emitRaidNoise(Vec2 source, float radius) noexcept
{
    if (!raidSession_.isActive() || !std::isfinite(radius) || radius <= 0.0F)
    {
        return;
    }
    const float radiusSquared = radius * radius;
    auto &enemies = activeEnemies();
    std::vector<std::string_view> alertedGroups;
    for (std::size_t index{}; index < enemies.size(); ++index)
    {
        Enemy &enemy = enemies[index];
        if (enemy.isArmedScavenger()) continue;
        const Vec2 center = enemyCenter(enemy);
        const float dx = center.x - source.x;
        const float dy = center.y - source.y;
        if (dx * dx + dy * dy <= radiusSquared)
        {
            enemy.hearTarget(source);
            const std::string &group = enemies.state(enemies[index].combatTargetId()).encounter.groupInstanceId;
            if (!group.empty() &&
                std::find(alertedGroups.begin(), alertedGroups.end(), group) ==
                    alertedGroups.end())
            {
                alertedGroups.push_back(group);
            }
        }
    }
    if (alertedGroups.empty())
    {
        return;
    }
    for (std::size_t index{}; index < enemies.size(); ++index)
    {
        const std::string &group = enemies.state(enemies[index].combatTargetId()).encounter.groupInstanceId;
        if (!group.empty() &&
            std::find(alertedGroups.begin(), alertedGroups.end(), group) !=
                alertedGroups.end())
        {
            if (!enemies[index].isArmedScavenger()) enemies[index].hearTarget(source);
        }
    }
}

bool GameplayWorld::canInteractWithContainer() const noexcept
{
    return raidSession_.isActive() &&
           !player_.isControlled() &&
           storageCabinet_.canInteract(
        playerBounds(player_));
}

bool GameplayWorld::searchStorageCabinet()
{
    return searchStorageCabinet(
        lootRandom_);
}

bool GameplayWorld::searchStorageCabinet(
    LootRandomSource &random)
{
    if (!canInteractWithContainer())
    {
        return false;
    }

    if (storageCabinet_.isSearched())
    {
        return true;
    }

    if (!storageCabinet_.inventory().placedItems().empty())
    {
        return false;
    }

    const std::vector<LootStack> loot =
        defaultStorageCabinetLootTable().roll(random);

    const ItemInstanceId maximumId =
        std::numeric_limits<ItemInstanceId>::max();

    if (loot.size() >
        static_cast<std::size_t>(
            maximumId - nextItemInstanceId_))
    {
        return false;
    }

    GridInventory generatedInventory{
        InventoryGridSize{
            storageCabinet_.inventory().width(),
            storageCabinet_.inventory().height()}};

    generatedInventory.reserveForAdditionalItems(
        loot.size());

    ItemInstanceId candidateId =
        nextItemInstanceId_;

    for (const LootStack &stack : loot)
    {
        if (itemInstanceIdExists(candidateId))
        {
            return false;
        }

        ItemInstance item{
            candidateId,
            stack.definitionId,
            stack.quantity};

        const std::optional<GridPosition> origin =
            generatedInventory.findFirstFit(
                stack.definitionId);

        if (!origin.has_value() ||
            !generatedInventory.tryPlace(
                std::move(item),
                *origin))
        {
            return false;
        }

        ++candidateId;
    }

    if (!storageCabinet_.tryCommitSearchResult(
            std::move(generatedInventory)))
    {
        return false;
    }

    nextItemInstanceId_ = candidateId;
    return true;
}

bool GameplayWorld::itemInstanceIdExists(
    ItemInstanceId instanceId) const noexcept
{
    if (instanceId == 0 ||
        inventory_.quantityOf(instanceId).has_value() ||
        storageCabinet_.inventory()
            .quantityOf(instanceId)
            .has_value())
    {
        return true;
    }

    return std::any_of(
        groundItems_.begin(),
        groundItems_.end(),
        [instanceId](const GroundItem &groundItem)
        {
            return groundItem.item().instanceId() ==
                   instanceId;
        });
}

bool GameplayWorld::dropInventoryItem(
    ItemInstanceId instanceId)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end())
    {
        return false;
    }

    return dropInventoryItem(
        instanceId,
        placedIt->item.orientation());
}

bool GameplayWorld::dropInventoryItem(
    ItemInstanceId instanceId,
    ItemOrientation orientation)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end())
    {
        return false;
    }

    return dropInventoryItemQuantity(
        instanceId,
        placedIt->item.quantity(),
        orientation);
}

bool GameplayWorld::dropInventoryItemQuantity(
    ItemInstanceId instanceId,
    std::uint32_t quantity,
    ItemOrientation orientation)
{
    const auto placedIt = std::find_if(
        inventory_.placedItems().begin(),
        inventory_.placedItems().end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });

    if (placedIt == inventory_.placedItems().end() ||
        quantity == 0 ||
        quantity > placedIt->item.quantity())
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(
            placedIt->item.definitionId());

    if (!canUseItemOrientation(
            definition,
            orientation))
    {
        return false;
    }

    const GridPosition sourceOrigin = placedIt->origin;
    const std::uint32_t sourceQuantity =
        placedIt->item.quantity();
    const bool partial = quantity < sourceQuantity;

    const Vec2 center = playerCenter(player_);
    const float playerFeetY =
        player_.position().y + player_.size();
    const Vec2 renderSize =
        orientedSize(
            definition.worldRenderSize,
            orientation);
    const float halfWidth = renderSize.x / 2.0f;
    const float halfHeight = renderSize.y / 2.0f;

    const Vec2 dropPosition{
        std::clamp(
            center.x,
            halfWidth,
            worldWidth() - halfWidth),
        std::clamp(
            playerFeetY,
            halfHeight,
            worldHeight() - halfHeight),
    };

    std::optional<ItemInstance> splitItem;
    if (partial)
    {
        if (nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
        {
            return false;
        }

        const bool splitIdAlreadyExists =
            inventory_.quantityOf(nextItemInstanceId_).has_value() ||
            storageCabinet_.inventory()
                .quantityOf(nextItemInstanceId_)
                .has_value() ||
            std::any_of(
                groundItems_.begin(),
                groundItems_.end(),
                [this](const GroundItem &groundItem)
                {
                    return groundItem.item().instanceId() ==
                           nextItemInstanceId_;
                });

        if (splitIdAlreadyExists)
        {
            return false;
        }

        splitItem.emplace(
            nextItemInstanceId_,
            placedIt->item.definitionId(),
            quantity);

        if (!splitItem->trySetOrientation(orientation))
        {
            return false;
        }
    }

    // 唯一可能抛出的容量增长发生在修改玩家背包之前。
    groundItems_.reserve(
        groundItems_.size() + 1);

    if (partial)
    {
        if (!inventory_.trySetItemQuantity(
                instanceId,
                sourceQuantity - quantity))
        {
            std::terminate();
        }

        groundItems_.emplace_back(
            std::move(*splitItem),
            dropPosition);
        ++nextItemInstanceId_;
        return true;
    }

    std::optional<ItemInstance> removed =
        inventory_.remove(instanceId);

    if (!removed.has_value())
    {
        return false;
    }

    if (!removed->trySetOrientation(orientation))
    {
        const bool restored = inventory_.tryPlace(
            std::move(*removed),
            sourceOrigin);

        if (!restored)
        {
            std::terminate();
        }

        return false;
    }

    // GroundItem 构造和已有元素移动均为 noexcept；reserve 后不再分配。
    groundItems_.emplace_back(
        std::move(*removed),
        dropPosition);

    return true;
}

bool GameplayWorld::transferInventoryItemQuantity(
    bool sourceIsPlayerInventory,
    ItemInstanceId instanceId,
    std::uint32_t quantity)
{
    GridInventory &source = sourceIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();
    GridInventory &destination = sourceIsPlayerInventory
        ? storageCabinet_.inventory()
        : inventory_;

    const std::optional<std::uint32_t> sourceQuantity =
        source.quantityOf(instanceId);

    if (sourceQuantity.has_value() &&
        quantity < *sourceQuantity &&
        nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
    {
        return false;
    }

    const QuantityTransferResult result =
        tryTransferItemQuantityFirstFit(
            source,
            destination,
            instanceId,
            quantity,
            nextItemInstanceId_);

    if (result.consumedSplitInstanceId)
    {
        ++nextItemInstanceId_;
    }

    return result.succeeded;
}

bool GameplayWorld::placeInventoryItemQuantity(
    bool sourceIsPlayerInventory,
    bool destinationIsPlayerInventory,
    ItemInstanceId instanceId,
    std::uint32_t quantity,
    GridPosition destinationOrigin,
    ItemOrientation destinationOrientation)
{
    GridInventory &source = sourceIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();
    GridInventory &destination = destinationIsPlayerInventory
        ? inventory_
        : storageCabinet_.inventory();

    const std::optional<std::uint32_t> sourceQuantity =
        source.quantityOf(instanceId);

    if (sourceQuantity.has_value() &&
        quantity < *sourceQuantity &&
        nextItemInstanceId_ ==
            std::numeric_limits<ItemInstanceId>::max())
    {
        return false;
    }

    const QuantityTransferResult result =
        tryPlaceItemQuantityAt(
            source,
            destination,
            instanceId,
            quantity,
            destinationOrigin,
            destinationOrientation,
            nextItemInstanceId_);

    if (result.consumedSplitInstanceId)
    {
        ++nextItemInstanceId_;
    }

    return result.succeeded;
}

int GameplayWorld::score() const noexcept
{
    return score_;
}

ItemInstanceId
GameplayWorld::nextItemInstanceId() const noexcept
{
    return nextItemInstanceId_;
}

float GameplayWorld::worldWidth() const noexcept
{
    return activeWorldSize().x;
}

float GameplayWorld::worldHeight() const noexcept
{
    return activeWorldSize().y;
}

Vec2 GameplayWorld::activeWorldSize() const noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].worldSize
        : worldSize_;
}

EnemyRoster<GameplayWorld::EnemyAttachedState> &GameplayWorld::activeEnemies() noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemies
        : enemies_;
}

const EnemyRoster<GameplayWorld::EnemyAttachedState> &GameplayWorld::activeEnemies() const noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemies
        : enemies_;
}

const std::vector<BallisticBlocker> &
GameplayWorld::activeBallisticBlockers() const noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].ballisticBlockers
        : ballisticBlockers_;
}

const RaidSpaceBlockerIndex &
GameplayWorld::activeBlockerIndex() const noexcept
{
    const std::optional<RaidSpaceBlockerIndex> &index =
        activeInteriorIndex_.has_value()
            ? interiors_[*activeInteriorIndex_].blockerIndex
            : outdoorBlockerIndex_;
    if (!index.has_value())
    {
        std::terminate();
    }
    return *index;
}

std::size_t &GameplayWorld::activeNavigationScheduleCursor() noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].navigationScheduleCursor
        : outdoorNavigationScheduleCursor_;
}

RaidSpaceNavigationField *
GameplayWorld::activeNavigationField(Vec2 actorSize)
{
    const RaidSpaceDefinitionId &spaceId = activeRaidSpaceId();
    auto &cache = inOutdoorRaidSpace() && roadBlockade_ &&
            roadBlockade_->phase == RaidRoadBlockadePhase::Closed
        ? roadBlockade_->closedNavigationFields
        : navigationFieldCache_;
    const auto found = std::find_if(
        cache.begin(),
        cache.end(),
        [&](const NavigationFieldCache &candidate)
        {
            return candidate.spaceId == spaceId &&
                   candidate.actorSize.x == actorSize.x &&
                   candidate.actorSize.y == actorSize.y;
        });
    if (found != cache.end())
    {
        return &found->field;
    }

    std::optional<RaidSpaceNavigationField> field =
        RaidSpaceNavigationField::build(
            actorSize,
            activeWorldSize(),
            activeBallisticBlockers(),
            2.0F);
    if (!field.has_value())
    {
        return nullptr;
    }
    cache.push_back(NavigationFieldCache{spaceId, actorSize, std::move(*field)});
    return &cache.back().field;
}

void GameplayWorld::cacheNavigationFieldsForSpace(
    const RaidSpaceDefinitionId &spaceId,
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers,
    std::span<const Enemy> enemies)
{
    for (const Enemy &enemy : enemies)
        cacheNavigationFieldForSpace(navigationFieldCache_, spaceId, enemy.size(), worldSize, blockers);
}

void GameplayWorld::cacheNavigationFieldForSpace(
    std::vector<NavigationFieldCache> &cache,
    const RaidSpaceDefinitionId &spaceId,
    Vec2 actorSize,
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers)
{
    const bool alreadyCached = std::any_of(cache.begin(), cache.end(),
        [&](const NavigationFieldCache &candidate)
        {
            return candidate.spaceId == spaceId &&
                candidate.actorSize.x == actorSize.x &&
                candidate.actorSize.y == actorSize.y;
        });
    if (alreadyCached)
        return;

    if (auto field = RaidSpaceNavigationField::build(actorSize, worldSize, blockers, 2.0F))
        cache.push_back(NavigationFieldCache{spaceId, actorSize, std::move(*field)});
}

std::size_t GameplayWorld::outdoorAliveEnemyCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        enemies_.begin(), enemies_.end(),
        [](const Enemy &enemy) { return !enemy.isDead(); }));
}

bool GameplayWorld::tryTransitionRaidSpace(
    const GameplayInput &input,
    const std::function<bool()> &beforeSpaceTransition)
{
    if (grenade_.active() || !input.interactJustPressed || input.inventoryOpen ||
        player_.isControlled() || !raidSession_.isActive())
    {
        return false;
    }
    const Vec2 center = playerCenter(player_);
    const auto transferSupport = [&](ContentRect entrance, std::optional<std::size_t> destination,
        Vec2 playerLanding, const RaidSpaceBlockerIndex &destinationBlockers, std::span<const Enemy> occupants)
    {
        if (!fireSupport_ || fireSupportInteriorIndex_ != activeInteriorIndex_ || !fireSupport_->alive() ||
            fireSupport_->view().movementOrder != FireSupportMovementOrder::Follow)
            return;
        constexpr float reach = 96.0F;
        const auto companion = fireSupport_->center();
        const auto gate = nearestPointOnRect(entrance, companion);
        const auto &walls = activeBlockerIndex();
        if (distanceSquared(companion, gate) > reach * reach ||
            !fireSupportCanPass(companion, gate, walls) || !walls.hasLineOfSight(companion, center))
            return;
        const auto landing = fireSupportLanding({playerLanding, {player_.size(), player_.size()}},
            destinationBlockers, occupants, true);
        if (!landing) return;

        // A windup already aimed at this actor must never strike a different
        // target after the old space is re-entered. Held actors keep their locks.
        for (Enemy &enemy : activeEnemies())
            if (fireSupport_->hasAttackLock(enemy.combatTargetId()))
                enemy.cancelAttackOnDepartedTarget();
        fireSupport_->leaveSpace();
        fireSupport_->setPosition(*landing);
        fireSupportInteriorIndex_ = destination;
    };
    if (activeInteriorIndex_.has_value())
    {
        InteriorRuntime &interior = interiors_[*activeInteriorIndex_];
        if (!pointInside(interior.interiorExit, center))
        {
            return false;
        }
        if (beforeSpaceTransition && !beforeSpaceTransition()) return false;
        transferSupport(interior.interiorExit, std::nullopt, interior.exteriorReturn, *outdoorBlockerIndex_, enemies_.view());
        activeInteriorIndex_.reset();
        activeGeometryRevision_ = outdoorGeometryRevision_;
        static_cast<void>(player_.setPosition(interior.exteriorReturn));
    }
    else
    {
        const auto interior = std::find_if(
            interiors_.begin(), interiors_.end(),
            [&](const InteriorRuntime &candidate)
            { return pointInside(candidate.exteriorEntrance, center); });
        if (interior == interiors_.end())
        {
            return false;
        }
        if (beforeSpaceTransition && !beforeSpaceTransition()) return false;
        const auto destination = static_cast<std::size_t>(std::distance(interiors_.begin(), interior));
        transferSupport(interior->exteriorEntrance, destination, interior->interiorSpawn, *interior->blockerIndex,
                        interior->enemies.view());
        activeInteriorIndex_ = destination;
        activeGeometryRevision_ = interior->geometryRevision;
        static_cast<void>(player_.setPosition(interior->interiorSpawn));
    }
    clearSpatialTransientPresentation();
    shooting_.relocate(
        center,
        playerCenter(player_),
        activeWorldSize(),
        input.aimViewport);
    return true;
}

void GameplayWorld::clearSpatialTransientPresentation() noexcept
{
    melee_.cancelHeavyCharge();
    shooting_.clearSpatialTransientPresentation();
    shooting_.cancelWeaponOptic();
    clearScavengerAim();
    pendingPlayerDamageObservations_.clear();
}

void GameplayWorld::updateHighRiskPressure(float highRiskDeltaTime)
{
    if (raidSession_.phase() != RaidPhase::HighRisk ||
        !std::isfinite(highRiskDeltaTime) ||
        highRiskDeltaTime <= 0.0F ||
        highRiskPressureSpawns_.empty())
    {
        return;
    }

    highRiskNextWaveSeconds_ -= highRiskDeltaTime;
    while (highRiskNextWaveSeconds_ <= 0.0F &&
           outdoorAliveEnemyCount() < highRiskActiveEnemyCap_)
    {
        if (spawnHighRiskPressureWave() == 0U)
        {
            // Keep the pressure due. A later frame can retry after the player
            // or living enemies have moved away from every legal spawn.
            highRiskNextWaveSeconds_ = 0.0F;
            return;
        }
        ++highRiskPressureWaveCount_;
        highRiskNextWaveSeconds_ += highRiskWaveIntervalSeconds_;
    }
}

void GameplayWorld::updateHighRiskActivation(
    const GameplayInput &input,
    float deltaTime,
    Vec2 playerPosition)
{
    if (!inOutdoorRaidSpace() || !highRiskControlPoint_.has_value() ||
        raidSession_.phase() != RaidPhase::Regular || !raidSession_.isActive())
    {
        return;
    }

    const bool canContinue =
        !player_.isControlled() && !input.inventoryOpen &&
        input.interactPressed &&
        pointInside(*highRiskControlPoint_, playerPosition);
    if (!canContinue)
    {
        highRiskActivationElapsedSeconds_ = 0.0F;
        return;
    }
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }

    highRiskActivationElapsedSeconds_ =
        std::min(highRiskActivationDurationSeconds_,
                 highRiskActivationElapsedSeconds_ + deltaTime);
    if (highRiskActivationElapsedSeconds_ >= highRiskActivationDurationSeconds_)
    {
        static_cast<void>(raidSession_.triggerHighRisk());
    }
}

void GameplayWorld::updateOrdinarySurvivorRescue(
    const GameplayInput &input,
    float deltaTime,
    Vec2 playerPosition)
{
    if (hospitalEscortActor_)
    {
        const bool held = input.interactPressed || input.interactJustPressed;
        if (!held) hospitalEscortCommandHeld_ = false;
        if (!inOutdoorRaidSpace() || ordinarySurvivorRescueSecured_ || !hospitalEscortActor_->alive() ||
            !raidSession_.isActive() || player_.isControlled() || input.inventoryOpen || input.throwableModal ||
            input.freezeObservationDirection || spaceTransitionedLastUpdate_ || !held)
        {
            ordinarySurvivorRescueElapsedSeconds_ = 0.0F;
            return;
        }
        if (hospitalEscortAtHandoff())
        {
            if (!hospitalEscortCommandHeld_ && input.interactPressed && !hospitalEscortThreatened() &&
                std::isfinite(deltaTime) && deltaTime > 0.0F)
                ordinarySurvivorRescueElapsedSeconds_ = std::min(ordinarySurvivorRescueDurationSeconds_,
                    ordinarySurvivorRescueElapsedSeconds_ + deltaTime);
            else
                ordinarySurvivorRescueElapsedSeconds_ = 0.0F;
            return;
        }
        ordinarySurvivorRescueElapsedSeconds_ = 0.0F;
        if (input.interactJustPressed && !hospitalEscortCommandHeld_ && nearHospitalEscort())
        {
            hospitalEscortCommandHeld_ = true;
            hospitalEscortOrder_ = hospitalEscortOrder_ == HospitalEscortState::Following
                ? HospitalEscortState::Waiting : HospitalEscortState::Following;
            hospitalEscortActor_->setMovementOrder(hospitalEscortOrder_ == HospitalEscortState::Following
                ? FireSupportMovementOrder::Follow : FireSupportMovementOrder::Hold);
        }
        return;
    }
    if (!inOutdoorRaidSpace() || !ordinarySurvivorRescuePoint_.has_value() ||
        ordinarySurvivorRescueSecured_ || !raidSession_.isActive())
    {
        return;
    }

    const bool canContinue =
        !player_.isControlled() && !input.inventoryOpen &&
        input.interactPressed &&
        pointInside(*ordinarySurvivorRescuePoint_, playerPosition);
    if (!canContinue)
    {
        ordinarySurvivorRescueElapsedSeconds_ = 0.0F;
        return;
    }
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }
    ordinarySurvivorRescueElapsedSeconds_ = std::min(
        ordinarySurvivorRescueDurationSeconds_,
        ordinarySurvivorRescueElapsedSeconds_ + deltaTime);
}

std::optional<RaidNightRendezvousView> GameplayWorld::nightRendezvousView() const noexcept
{
    if (!nightRendezvous_)
        return std::nullopt;
    auto view = *nightRendezvous_;
    view.visible = view.visible && inOutdoorRaidSpace();
    view.inRange = view.inRange && inOutdoorRaidSpace() && raidSession_.isActive();
    return view;
}

bool GameplayWorld::nightRendezvousReady() const noexcept
{
    return nightRendezvous_ && nightRendezvous_->ready && !nightRendezvous_->secured &&
        nightRendezvous_->inRange && !nightRendezvous_->interactionBlocked &&
        inOutdoorRaidSpace() && raidSession_.isActive() && !player_.isDead() &&
        !player_.isControlled();
}

bool GameplayWorld::takeNightRendezvousCommitRequest() noexcept
{
    const bool requested = std::exchange(nightRendezvousCommitRequested_, false);
    return requested && nightRendezvousReady();
}

void GameplayWorld::confirmNightRendezvous() noexcept
{
    if (nightRendezvous_ && nightRendezvous_->ready)
    {
        nightRendezvous_->secured = true;
        nightRendezvous_->inProgress = false;
    }
}

void GameplayWorld::updateNightRendezvous(
    const GameplayInput &input, float deltaTime, bool controlsSuppressed)
{
    constexpr float coverRadius = kRaidNightRendezvousCoverRadius;
    constexpr float localNoiseRadius{400.0F};
    auto &task = *nightRendezvous_;
    const Vec2 center{task.bounds.position.x + coverRadius,
                      task.bounds.position.y + coverRadius};
    const Vec2 position = playerCenter(player_);
    const float distance = distanceSquared(position, center);
    const bool outdoors = inOutdoorRaidSpace();
    task.inRange = outdoors && distance <= coverRadius * coverRadius;
    task.visible = outdoors && distance <= localNoiseRadius * localNoiseRadius &&
        activeBlockerIndex().hasLineOfSight(position, center) &&
        (!lightConeObservationEnabled() || raidBoundsObservable(
            Rect{{center.x - 12.0F, center.y - 12.0F}, {24.0F, 24.0F}}));
    task.discovered = task.discovered || task.visible;
    task.interactionBlocked = controlsSuppressed || player_.isDead() ||
        input.inventoryOpen || input.throwableModal || input.freezeObservationDirection ||
        input.nightRendezvousInteractionBlocked || spaceTransitionedLastUpdate_;
    task.enemyInRange = false;

    if (task.secured)
        return;
    if (!task.inRange)
    {
        if (!task.ready)
        {
            task.inProgress = false;
            task.remainingSeconds = kRaidNightRendezvousDurationSeconds;
            task.progress = 0.0F;
        }
        return;
    }
    if (task.ready)
    {
        if (input.interactJustPressed && !task.interactionBlocked)
        {
            nightRendezvousConsumedInteraction_ = true;
            nightRendezvousCommitRequested_ = true;
        }
        return;
    }

    const auto &enemies = activeEnemies();
    task.enemyInRange = std::any_of(enemies.begin(), enemies.end(), [&](const Enemy &enemy)
    {
        return !enemy.isDead() &&
            distanceSquared(enemyCenter(enemy), center) <= coverRadius * coverRadius;
    });
    if (task.interactionBlocked)
        return;
    if (!task.inProgress)
    {
        if (!input.interactJustPressed || !task.visible)
            return;
        task.inProgress = true;
        nightRendezvousConsumedInteraction_ = true;
        if (!task.signalSent)
        {
            publishOutdoorLocalNoise(prepareOutdoorLocalNoise(center, localNoiseRadius), center);
            task.signalSent = true;
            nightRendezvousSignaledLastUpdate_ = true;
        }
        return;
    }
    if (task.enemyInRange || !std::isfinite(deltaTime) || deltaTime <= 0.0F)
        return;

    task.remainingSeconds = std::max(0.0F, task.remainingSeconds - deltaTime);
    task.progress = 1.0F - task.remainingSeconds / kRaidNightRendezvousDurationSeconds;
    if (task.remainingSeconds == 0.0F)
    {
        task.ready = true;
        nightRendezvousCommitRequested_ = true;
    }
}

void GameplayWorld::updateCredentialInvestigation(
    const GameplayInput &input, float deltaTime,
    bool controlsSuppressedAtFrameStart)
{
    if (!credentialExtraction_ || credentialExtraction_->investigationCompleted)
        return;
    if (controlsSuppressedAtFrameStart || !input.credentialInvestigationAllowed ||
        !input.interactPressed || input.inventoryOpen || input.throwableModal ||
        input.freezeObservationDirection || spaceTransitionedLastUpdate_ ||
        raidSpacePortalInteractionInRange() || !credentialInvestigationInRange())
    {
        cancelCredentialInvestigationInteraction();
        return;
    }
    if (pendingCredentialInvestigation_ || !std::isfinite(deltaTime) || deltaTime <= 0.0F)
        return;
    const auto &definition = credentialExtraction_->definition;
    credentialInvestigationElapsedSeconds_ = std::min(
        definition.investigationDurationSeconds,
        credentialInvestigationElapsedSeconds_ + deltaTime);
    if (credentialInvestigationElapsedSeconds_ >= definition.investigationDurationSeconds)
    {
        pendingCredentialInvestigation_ = RaidCredentialInvestigationFact{
            definition.investigationPointId, activeRaidSpaceId(), playerCenter(player_)};
    }
}

std::size_t GameplayWorld::spawnHighRiskPressureWave()
{
    if (nextCombatTargetId_ ==
        std::numeric_limits<CombatTargetId>::max())
    {
        return 0U;
    }

    const std::size_t available =
        static_cast<std::size_t>(highRiskActiveEnemyCap_) -
        outdoorAliveEnemyCount();
    const std::size_t requested = std::min(
        static_cast<std::size_t>(highRiskWaveSize_),
        available);
    std::size_t spawned{};

    for (std::size_t member = 0U; member < requested; ++member)
    {
        bool found{};
        for (std::size_t attempt = 0U;
             attempt < highRiskPressureSpawns_.size();
             ++attempt)
        {
            const EnemySpawn &candidate =
                highRiskPressureSpawns_[nextHighRiskPressureSpawnIndex_];
            nextHighRiskPressureSpawnIndex_ =
                (nextHighRiskPressureSpawnIndex_ + 1U) %
                highRiskPressureSpawns_.size();
            if (!canSpawnHighRiskEnemy(candidate))
            {
                continue;
            }
            enemies_.spawn(Enemy{
                candidate.position,
                candidate.size,
                Vec2{},
                candidate.maxHealth,
                nextCombatTargetId_++, candidate.torsoArmor, candidate.variant});
            const ContentRect pressureArea =
                highRiskAdvancedResourceArea_.value_or(ContentRect{
                    candidate.position, candidate.size});
            const Vec2 pressureTarget{
                pressureArea.position.x + pressureArea.size.x * 0.5F,
                pressureArea.position.y + pressureArea.size.y * 0.5F};
            enemies_.state(enemies_.back().combatTargetId()).encounter = EnemyEncounterRuntime{
                {},
                RaidEncounterKind::Guard,
                pressureTarget};
            ++spawned;
            found = true;
            break;
        }
        if (!found ||
            nextCombatTargetId_ ==
                std::numeric_limits<CombatTargetId>::max())
        {
            break;
        }
    }
    return spawned;
}

bool GameplayWorld::canSpawnHighRiskEnemy(
    const EnemySpawn &spawn) const noexcept
{
    const Rect candidate{spawn.position, spawn.size};
    if (candidate.position.x < 0.0F ||
        candidate.position.y < 0.0F ||
        candidate.position.x + candidate.size.x > worldSize_.x ||
        candidate.position.y + candidate.size.y > worldSize_.y)
    {
        return false;
    }

    const Vec2 spawnCenter{
        spawn.position.x + spawn.size.x * 0.5F,
        spawn.position.y + spawn.size.y * 0.5F};
    if (inOutdoorRaidSpace())
    {
        const Vec2 playerPosition = playerCenter(player_);
        const float deltaX = spawnCenter.x - playerPosition.x;
        const float deltaY = spawnCenter.y - playerPosition.y;
        if (deltaX * deltaX + deltaY * deltaY <
            kMinimumHighRiskSpawnDistance *
                kMinimumHighRiskSpawnDistance)
        {
            return false;
        }
    }

    for (const BallisticBlocker &blocker : ballisticBlockers_)
    {
        if (isCollision(candidate, blocker.bounds))
        {
            return false;
        }
    }
    return std::none_of(
        enemies_.begin(),
        enemies_.end(),
        [&candidate](const Enemy &enemy)
        {
            return !enemy.isDead() &&
                   isCollision(candidate, enemy.bounds());
        });
}


bool GameplayWorld::resetDeveloperRangeEnemies(const EnemyCombatDefinition &definition,
    std::size_t pad, std::size_t count)
{
    if (!developerRange_ || !raidSession_.isActive() || pad >= kDeveloperRangePads.size() ||
        count > 5 || nextCombatTargetId_ >= std::numeric_limits<CombatTargetId>::max() - count ||
        definition.maximumHealth <= 0 || (definition.torsoArmor && !definition.torsoArmor->valid())) return false;
    EnemyRoster<EnemyAttachedState> candidate;
    auto nextId = nextCombatTargetId_;
    for (std::size_t i = 0; i < count; ++i)
    {
        const Vec2 position{kDeveloperRangePads[pad].x + static_cast<float>(i % 3) * 80,
            kDeveloperRangePads[pad].y + static_cast<float>(i / 3) * 90};
        candidate.spawn(Enemy{position, {50, 50}, {}, definition.maximumHealth, nextId++, definition.torsoArmor});
        candidate.state(candidate.back().combatTargetId()).encounter.home = position;
    }
    enemyObservationCache_.clear();
    enemies_ = std::move(candidate);
    trainingCorpseDeaths_.clear();
    enemyObservationIndexReady_ = false;
    nextCombatTargetId_ = nextId;
    initialOutdoorEnemyCount_ = enemies_.size();
    navigationFieldCache_.clear();
    clearSpatialTransientPresentation();
    return true;
}

void GameplayWorld::consumeEnemyRemovals(std::span<const EnemyRemovalFact> deaths)
{
    for (const auto &death:deaths)
    {
        if (fireSupport_)
            fireSupport_->forgetAttack(death.id);
        if (hospitalEscortActor_)
            hospitalEscortActor_->forgetAttack(death.id);
        for (std::size_t i{}; i < scavengerViews_.size(); ++i)
        {
            auto &weapon = scavengerWeapons_[i];
            std::erase(weapon.attackLocks, death.id);
            if (death.id == scavengerViews_[i].id)
            {
                scavengerViews_[i].alive = false;
                scavengerViews_[i].escaped = death.reason == EnemyRemovalReason::ObjectiveReached;
                scavengerViews_[i].retreating = scavengerViews_[i].escapeReady = false;
                scavengerViews_[i].retreatDirection = {};
                scavengerViews_[i].aiming = scavengerViews_[i].reloading = false;
                weapon.target.reset(); // Existing flight keeps its frozen identity and trajectory.
            }
        }
        noiseDecoy_.forget(death.id);
        localNoiseInvestigations_.erase(death.id);
        if (death.reason!=EnemyRemovalReason::Death)
        {
            corpseSources_.erase(death.id);
            continue;
        }
        if(developerRange_)trainingCorpseDeaths_.push_back({death.id,{death.position.x+25.0F,death.position.y+25.0F},{50,50}});
        score_+=kScorePerEnemy;
        if (lostWeaponPursuitFallback_ && death.id == lostWeaponCarrierId_ && !lostWeaponCarrierDeath_)
        {
            lostWeaponCarrierDeath_ = lostWeaponCorpseSearchPoint({
                death.position.x + lostWeaponCarrierHalfSize_.x,
                death.position.y + lostWeaponCarrierHalfSize_.y});
        }
        const auto source=corpseSources_.find(death.id);
        if(source==corpseSources_.end())continue;
        auto fact=source->second;fact.position.x+=death.position.x;fact.position.y+=death.position.y;
        pendingCorpseDeaths_.push_back(std::move(fact));corpseSources_.erase(source);
    }
}

Vec2 GameplayWorld::lostWeaponCorpseSearchPoint(Vec2 deathCenter) const noexcept
{
    const auto &blockers = *outdoorBlockerIndex_;
    const float halfPlayer = player_.size() / 2.0F;
    const Vec2 size = blockers.worldSize();
    const Rect standingBounds = centeredBounds(deathCenter, {halfPlayer, halfPlayer});
    if (pointInside({{halfPlayer, halfPlayer}, {size.x - player_.size(), size.y - player_.size()}},
                    deathCenter) &&
        std::none_of(blockers.allBlockerBounds().begin(), blockers.allBlockerBounds().end(),
            [&standingBounds](const Rect &wall) { return isCollision(standingBounds, wall); }))
    {
        // The carrier used the same blockers and is at least as large as the
        // player. Its death point is reachable along the path it took; nearby
        // offsets are allowed only along a clear sweep of the player's body.
        constexpr std::array<Vec2, 9> offsets{{
            {}, {4.0F, 0.0F}, {-4.0F, 0.0F}, {0.0F, 4.0F}, {0.0F, -4.0F},
            {4.0F, 4.0F}, {4.0F, -4.0F}, {-4.0F, 4.0F}, {-4.0F, -4.0F}}};
        for (const Vec2 offset : offsets)
        {
            const Vec2 candidate{deathCenter.x + offset.x, deathCenter.y + offset.y};
            if (!lostWeaponCorpseFits(candidate, blockers))
                continue;
            const Rect sweep{
                {std::min(deathCenter.x, candidate.x) - halfPlayer,
                 std::min(deathCenter.y, candidate.y) - halfPlayer},
                {player_.size() + std::abs(offset.x), player_.size() + std::abs(offset.y)}};
            if (std::none_of(blockers.allBlockerBounds().begin(), blockers.allBlockerBounds().end(),
                [&sweep](const Rect &wall) { return isCollision(sweep, wall); }))
                return candidate;
        }
    }
    return *lostWeaponPursuitFallback_;
}

bool GameplayWorld::commitPreparedMelee()
{
    if(player_.isDead() || player_.isControlled() || !raidSession_.isActive()) {melee_.cancelPrepared();return false;}
    if(!melee_.commitPrepared(activeEnemies()))return false;
    consumeEnemyRemovals(melee_.removals());
    // The observable target cache must never retain a removed actor.
    if(!melee_.removals().empty()){enemyObservationCache_.clear();enemyObservationIndexReady_=false;}
    return true;
}
