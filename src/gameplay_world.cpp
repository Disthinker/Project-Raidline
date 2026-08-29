#include "gameplay_world.h"

#include <algorithm>
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
    constexpr float kLegacyShotExtent{8.0f};
    constexpr float kMaximumEnemyStepTime{1.0F / 120.0F};
    constexpr float kMaximumEnemySubsteps{2048.0F};
    constexpr float kEnemyNavigationRefreshSeconds{0.10F};
    constexpr float kEnemyNavigationGoalRefreshDistance{32.0F};
    constexpr std::size_t kMaximumNavigationQueriesPerEnemySubstep{1U};
    constexpr float kMinimumHighRiskSpawnDistance{260.0F};

    constexpr int kScorePerEnemy{100};

    bool pointInside(
        ContentRect rect,
        Vec2 point) noexcept
    {
        return point.x >= rect.position.x && point.y >= rect.position.y &&
               point.x <= rect.position.x + rect.size.x &&
               point.y <= rect.position.y + rect.size.y;
    }

    float distanceToWorldBoundary(
        Vec2 origin,
        Vec2 direction,
        Vec2 worldSize) noexcept
    {
        float distance = std::numeric_limits<float>::infinity();
        const auto consider = [&](float candidate)
        {
            if (std::isfinite(candidate) && candidate > 0.0F)
            {
                distance = std::min(distance, candidate);
            }
        };

        if (direction.x > 0.0F)
        {
            consider((worldSize.x - origin.x) / direction.x);
        }
        else if (direction.x < 0.0F)
        {
            consider((0.0F - origin.x) / direction.x);
        }
        if (direction.y > 0.0F)
        {
            consider((worldSize.y - origin.y) / direction.y);
        }
        else if (direction.y < 0.0F)
        {
            consider((0.0F - origin.y) / direction.y);
        }

        return distance;
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
        blockerIndex.queryCandidateIndices(
            Rect{
                Vec2{minimumX, minimumY},
                Vec2{maximumX - minimumX, maximumY - minimumY}},
            queryScratch);
        if (blockersExamined != nullptr)
        {
            *blockersExamined += queryScratch.size();
        }

        Rect resolvedBounds{currentPosition, actorSize};
        float resolvedX = requestedPosition.x;
        for (const std::size_t blocker : queryScratch)
        {
            resolvedX = resolveHorizontalCollision(
                resolvedBounds,
                resolvedX,
                blockerIndex.blockerBounds(blocker));
        }
        resolvedBounds.position.x = resolvedX;

        float resolvedY = requestedPosition.y;
        for (const std::size_t blocker : queryScratch)
        {
            resolvedY = resolveVerticalCollision(
                resolvedBounds,
                resolvedY,
                blockerIndex.blockerBounds(blocker));
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

    std::optional<ShotAimIntent> aimIntentAt(
        Vec2 aimPoint,
        Vec2 shotOrigin,
        const std::vector<Enemy> &enemies) noexcept
    {
        const Enemy *selected{};
        std::optional<HitRegion> selectedRegion;
        float selectedDistanceSquared =
            std::numeric_limits<float>::infinity();

        for (const Enemy &enemy : enemies)
        {
            if (enemy.isDead() ||
                enemy.combatTargetId() == kInvalidCombatTargetId)
            {
                continue;
            }
            const std::optional<HitRegion> region =
                hitRegionAtPoint(enemy.bounds(), aimPoint);
            if (!region.has_value())
            {
                continue;
            }
            const float candidateDistance = distanceSquared(
                shotOrigin,
                enemyCenter(enemy));
            if (candidateDistance >= selectedDistanceSquared)
            {
                continue;
            }
            selected = &enemy;
            selectedRegion = region;
            selectedDistanceSquared = candidateDistance;
        }

        if (selected == nullptr || !selectedRegion.has_value())
        {
            return std::nullopt;
        }
        return ShotAimIntent{
            selected->combatTargetId(),
            *selectedRegion,
            false};
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
            interior.enemies.emplace_back(
                spawn.position,
                spawn.size,
                Vec2{},
                spawn.maxHealth,
                nextCombatTargetId_++);
        }
        interior.initialEnemyCount = interior.enemies.size();
        interior.enemyNavigation.resize(interior.enemies.size());
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
    }

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

    raidSession_ = RaidSession{RaidSessionConfig{
        0.0F,
        config.normalExtractionDurationSeconds,
        false,
        HighRiskRaidSessionConfig{
            config.highRisk.enabled,
            config.highRisk.regularPhaseDurationSeconds,
            config.highRisk.emergencyExtractionDurationSeconds,
            config.highRisk.conditionalExtractionDurationSeconds}}};
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
        std::move(specialLocations));
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
    cacheNavigationFieldsForSpace(
        outdoorRaidSpaceId(),
        worldSize_,
        ballisticBlockers_,
        enemies_);
    for (const InteriorRuntime &interior : interiors_)
    {
        cacheNavigationFieldsForSpace(
            interior.id,
            interior.worldSize,
            interior.ballisticBlockers,
            interior.enemies);
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
      nextItemInstanceId_{firstItemInstanceId},
      particleSystem_{
          0xC0FFEEu,
          ParticleBurstConfig{}}
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

        enemies_.emplace_back(
            spawn.position,
            spawn.size,
            Vec2{},
            spawn.maxHealth,
            nextCombatTargetId_++);
    }
    initialOutdoorEnemyCount_ = enemies_.size();
    enemyNavigation_.resize(enemies_.size());
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
    float deltaTime)
{
    spaceTransitionedLastUpdate_ = false;
    hitResultsLastUpdate_.clear();
    shotFiredLastUpdate_ = false;
    enemiesAlertedLastUpdate_ = 0U;
    navigationQueriesLastUpdate_ = 0U;
    simulationWorkloadLastUpdate_ = RaidSimulationWorkload{};
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        shotFeedbackPresentation_.update(deltaTime);
        for (TracerPresentationSegment &tracer : tracerPresentations_)
        {
            tracer.ageSeconds += deltaTime;
            tracer.remainingSeconds = std::max(
                0.0F,
                tracer.remainingSeconds - deltaTime);
        }
        std::erase_if(
            tracerPresentations_,
            [](const TracerPresentationSegment &tracer)
            {
                return tracer.remainingSeconds <= 0.0F;
            });
    }
    if (!raidSession_.isActive())
    {
        return;
    }

    // 先更新上一帧已经存在的粒子。
    particleSystem_.update(
        deltaTime);

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

    const bool exploredOutdoor = inOutdoorRaidSpace();
    const Vec2 explorationCenter = playerCenter(player_);
    if (exploredOutdoor)
    {
        tacticalMap_.revealAround(explorationCenter);
    }
    spaceTransitionedLastUpdate_ = tryTransitionRaidSpace(input);

    const Vec2 centerAfterMovement = playerCenter(player_);
    Vec2 desiredAimPosition{
        centerAfterMovement.x + player_.facingDirection().x * 400.0F,
        centerAfterMovement.y + player_.facingDirection().y * 400.0F};
    if (!spaceTransitionedLastUpdate_ && input.aimWorldPosition.has_value())
    {
        desiredAimPosition = *input.aimWorldPosition;
    }
    weaponAim_.update(
        desiredAimPosition,
        centerAfterMovement,
        activeWorldSize(),
        input.aimDownSights,
        deltaTime,
        input.aimMotionDelta,
        AimControlMode::Direct);
    if (!spaceTransitionedLastUpdate_ && input.aimWorldBounds.has_value())
    {
        weaponAim_.constrainToBounds(*input.aimWorldBounds);
    }
    static_cast<void>(player_.faceDirection(weaponAim_.actualDirection()));

    // 撤离使用移动后的 Player 逻辑中心，而不是更大的渲染精灵。
    const float highRiskTimeBeforeUpdate =
        raidSession_.highRiskTimeElapsed();
    const bool playerInEmergencyExtraction =
        inOutdoorRaidSpace() &&
        emergencyExtractionPoint_.has_value() &&
        emergencyExtractionPoint_->contains(playerCenter(player_));
    const bool playerInConditionalExtraction =
        inOutdoorRaidSpace() &&
        conditionalExtractionPoint_.has_value() &&
        input.conditionalExtractionEligible &&
        conditionalExtractionPoint_->contains(playerCenter(player_));
    raidSession_.update(
        deltaTime,
        input.extractionEligible && inOutdoorRaidSpace() &&
            !player_.isControlled() && extractionPoint_.contains(
                playerCenter(player_)),
        input.extractionEligible && !player_.isControlled() &&
            playerInEmergencyExtraction,
        input.extractionEligible && !player_.isControlled() &&
            playerInConditionalExtraction);
    updateHighRiskActivation(input, deltaTime, centerAfterMovement);
    updateOrdinarySurvivorRescue(input, deltaTime, centerAfterMovement);
    if (raidSession_.phase() == RaidPhase::HighRisk)
    {
        highRiskActivationElapsedSeconds_ = highRiskActivationDurationSeconds_;
    }

    // 终局形成后，本帧不再产生拾取、射击、敌人或命中结果。
    if (!raidSession_.isActive())
    {
        return;
    }

    updateHighRiskPressure(
        std::max(
            0.0F,
            raidSession_.highRiskTimeElapsed() -
                highRiskTimeBeforeUpdate));

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

    std::vector<Enemy> &activeEnemySet = activeEnemies();
    std::vector<EnemyNavigationRuntime> &activeNavigationSet =
        activeEnemyNavigation();
    if (activeNavigationSet.size() != activeEnemySet.size())
    {
        std::terminate();
    }
    const std::vector<BallisticBlocker> &activeBlockerSet =
        activeBallisticBlockers();
    const RaidSpaceBlockerIndex &activeStaticBlockers =
        activeBlockerIndex();
    simulationWorkloadLastUpdate_.activeEnemies = activeEnemySet.size();
    simulationWorkloadLastUpdate_.activeBlockers =
        activeStaticBlockers.blockerCount();
    simulationWorkloadLastUpdate_.enemySubsteps = enemySubsteps;

    struct EnemyNavigationIntent
    {
        bool targetVisible{};
        bool refreshRequired{};
        bool selectedForRefresh{};
        std::optional<Vec2> reachableGoal;
    };
    std::vector<EnemyNavigationIntent> navigationIntents(
        activeEnemySet.size());
    for (std::size_t step{0U};
         step < enemySubsteps;
         ++step)
    {
        if (std::isfinite(enemyStepTime) && enemyStepTime > 0.0F)
        {
            enemyDamageProtectionRemainingSeconds_ = std::max(
                0.0F,
                enemyDamageProtectionRemainingSeconds_ - enemyStepTime);
        }
        const Vec2 playerPosition =
            playerCenter(player_);
        std::vector<EnemySquadMemberSnapshot> enemySnapshots;
        enemySnapshots.reserve(activeEnemySet.size());
        for (const Enemy &enemy : activeEnemySet)
        {
            enemySnapshots.push_back(
                EnemySquadMemberSnapshot{
                    enemyCenter(enemy),
                    !enemy.isDead(),
                    enemy.awarenessState(),
                    enemy.attackPhase(),
                    enemy.hasAttackOpportunity(playerPosition)});
        }

        EnemySquadDecisionMetrics squadMetrics;
        const std::vector<EnemyTacticalDirective> directives =
            enemySquadCoordinator_.decide(
                enemySnapshots,
                playerPosition,
                &squadMetrics);
        simulationWorkloadLastUpdate_.neighborCandidatesExamined +=
            squadMetrics.neighborCandidatesExamined;

        const auto hasLineOfSight = [&](Vec2 start, Vec2 end)
        {
            std::size_t blockerTests{};
            const bool visible = activeStaticBlockers.hasLineOfSight(
                start,
                end,
                &blockerTests);
            simulationWorkloadLastUpdate_.lineOfSightBlockersExamined +=
                blockerTests;
            return visible;
        };

        std::size_t refreshRequiredCount{};
        for (std::size_t enemyIndex{};
             enemyIndex < activeEnemySet.size();
             ++enemyIndex)
        {
            EnemyNavigationIntent &intent = navigationIntents[enemyIndex];
            intent = EnemyNavigationIntent{};
            Enemy &enemy = activeEnemySet[enemyIndex];
            EnemyNavigationRuntime &navigation =
                activeNavigationSet[enemyIndex];
            if (enemy.isDead())
            {
                navigation = EnemyNavigationRuntime{};
                continue;
            }

            const Vec2 enemyPosition = enemyCenter(enemy);
            intent.targetVisible = hasLineOfSight(
                enemyPosition,
                playerPosition);
            const std::optional<Vec2> navigationGoal = intent.targetVisible
                ? std::optional<Vec2>{playerPosition}
                : enemy.lastKnownTargetPosition();
            if (!navigationGoal.has_value())
            {
                navigation = EnemyNavigationRuntime{};
                continue;
            }

            const Vec2 worldSize = activeWorldSize();
            const Vec2 halfEnemySize{
                enemy.size().x * 0.5F,
                enemy.size().y * 0.5F};
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
                enemy.attackPhase() == EnemyAttackPhase::Idle &&
                (!navigation.initialized || goalMoved || perceptionChanged ||
                 navigation.refreshRemainingSeconds <= 0.0F);
            refreshRequiredCount += intent.refreshRequired ? 1U : 0U;
        }

        std::size_t selectedRefreshCount{};
        std::size_t &scheduleCursor = activeNavigationScheduleCursor();
        if (!activeEnemySet.empty())
        {
            scheduleCursor %= activeEnemySet.size();
            for (std::size_t scanned{};
                 scanned < activeEnemySet.size() &&
                 selectedRefreshCount <
                     kMaximumNavigationQueriesPerEnemySubstep;
                 ++scanned)
            {
                const std::size_t candidate =
                    (scheduleCursor + scanned) % activeEnemySet.size();
                if (!navigationIntents[candidate].refreshRequired)
                {
                    continue;
                }
                navigationIntents[candidate].selectedForRefresh = true;
                ++selectedRefreshCount;
                scheduleCursor = (candidate + 1U) % activeEnemySet.size();
            }
        }
        else
        {
            scheduleCursor = 0U;
        }
        simulationWorkloadLastUpdate_.navigationRefreshesDeferred +=
            refreshRequiredCount - selectedRefreshCount;

        for (std::size_t enemyIndex{0U};
             enemyIndex < activeEnemySet.size();
             ++enemyIndex)
        {
            Enemy &enemy = activeEnemySet[enemyIndex];
            const EnemyAwarenessState awarenessBefore =
                enemy.awarenessState();
            const Vec2 enemyPositionBeforeMovement = enemy.position();
            const Vec2 enemyPosition = enemyCenter(enemy);
            const EnemyNavigationIntent &intent =
                navigationIntents[enemyIndex];
            if (enemy.isDead())
            {
                static_cast<void>(enemy.updateTowardsTarget(
                    playerPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    worldWidth(),
                    worldHeight(),
                    false,
                    std::nullopt));
                continue;
            }

            std::optional<Vec2> navigationTarget;
            if (intent.reachableGoal.has_value())
            {
                EnemyNavigationRuntime &navigation =
                    activeNavigationSet[enemyIndex];
                if (intent.selectedForRefresh)
                {
                    ++navigationQueriesLastUpdate_;
                    ++simulationWorkloadLastUpdate_.navigationQueries;
                    RaidSpaceNavigationField *navigationField =
                        activeNavigationField(enemy.size());
                    navigation.waypoint = navigationField == nullptr
                        ? std::nullopt
                        : navigationField->nextWaypoint(
                              enemyPosition,
                              *intent.reachableGoal,
                              enemy.navigationGoalTolerance(
                                  intent.targetVisible));
                    if (!navigation.waypoint.has_value())
                    {
                        navigation.waypoint = enemyPosition;
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

            static_cast<void>(
                enemy.updateTowardsTarget(
                    playerPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    worldWidth(),
                    worldHeight(),
                    intent.targetVisible,
                    navigationTarget));
            const Vec2 resolvedEnemyPosition = resolveMovementAgainstBlockers(
                enemyPositionBeforeMovement,
                enemy.size(),
                enemy.position(),
                activeStaticBlockers,
                blockerQueryScratch_,
                &simulationWorkloadLastUpdate_.movementBlockersExamined);
            static_cast<void>(enemy.setPosition(resolvedEnemyPosition));
            if (awarenessBefore != EnemyAwarenessState::Alerted &&
                enemy.awarenessState() == EnemyAwarenessState::Alerted)
            {
                ++enemiesAlertedLastUpdate_;
            }

            if (enemy.hasGrabContactOpportunity())
            {
                const std::optional<Rect> grabHitbox =
                    enemy.attackHitbox();
                if (!grabHitbox.has_value() ||
                    !hasLineOfSight(
                        enemyCenter(enemy),
                        playerCenter(player_)) ||
                    !isCollision(
                        *grabHitbox,
                        playerBounds(player_)))
                {
                    continue;
                }

                // Grab itself deals no damage. Contact atomically changes the
                // owned attack state to the Bite follow-up, then consumes that
                // follow-up once before mutating Player/Raid state.
                if (!enemy.confirmGrabContact())
                {
                    std::terminate();
                }

                const std::optional<EnemyAttackConfig> biteConfig =
                    enemy.attackConfig();
                if (!biteConfig.has_value() ||
                    enemy.attackType() != EnemyAttackType::Bite ||
                    !enemy.consumeAttackHit())
                {
                    std::terminate();
                }

                if (resolveEnemyAttackDamage(
                        EnemyAttackType::Bite,
                        biteConfig->damage))
                {
                    return;
                }

                if (biteConfig->controlDuration > 0.0F)
                {
                    static_cast<void>(
                        player_.applyControl(
                            biteConfig->controlDuration));
                }
                continue;
            }

            if (!enemy.hasAttackHitOpportunity())
            {
                continue;
            }

            const std::optional<Rect> hitbox =
                enemy.attackHitbox();
            const std::optional<EnemyAttackConfig> attackConfig =
                enemy.attackConfig();

            if (!hitbox.has_value() ||
                !attackConfig.has_value() ||
                !hasLineOfSight(
                    enemyCenter(enemy),
                    playerCenter(player_)) ||
                !isCollision(
                    *hitbox,
                    playerBounds(player_)))
            {
                continue;
            }

            // Consume before mutating Player/Raid so a large frame cannot
            // submit the same attack hit twice.
            if (!enemy.consumeAttackHit())
            {
                std::terminate();
            }

            if (resolveEnemyAttackDamage(
                    *enemy.attackType(),
                    attackConfig->damage))
            {
                return;
            }

            if (attackConfig->controlDuration > 0.0F)
            {
                static_cast<void>(
                    player_.applyControl(
                        attackConfig->controlDuration));
            }
        }
    }

    const bool controlsSuppressed =
        controlsSuppressedAtFrameStart ||
        player_.isControlled();

    if (!spaceTransitionedLastUpdate_ && !controlsSuppressed &&
        input.interactJustPressed)
    {
        tryPickupOne();
    }

    const std::optional<ShotSpec> shot =
        weaponFire_.update(
            !controlsSuppressed &&
                !input.sprint &&
                (input.firePressed || input.fireJustPressed),
            weaponAim_.actualDirection(),
            deltaTime,
            WeaponFireContext{
                .moving = player_.isMoving(),
                .sprinting = input.sprint && player_.isMoving(),
                .aimDownSightsProgress =
                    weaponAim_.aimDownSightsProgress(),
                .aimDistance = weaponAim_.aimDistance(),
                .distanceSpreadFactor =
                    weaponAim_.distanceSpreadFactor(),
                .overEffectiveRangeFactor =
                    weaponAim_.overEffectiveRangeFactor(),
                .reticleControlSpeed =
                    weaponAim_.reticleControlSpeed(),
                .forceMaximumSpread =
                    input.forceMaximumWeaponSpread});

    if (shot.has_value())
    {
        shotFiredLastUpdate_ = true;
        const Vec2 center = playerCenter(player_);
        const float muzzleDistance =
            player_.size() / 2.0F +
            kLegacyShotExtent / 2.0F;

        const Vec2 shotOrigin{
            center.x + shot->direction.x * muzzleDistance,
            center.y + shot->direction.y * muzzleDistance};

        const float boundaryDistance = distanceToWorldBoundary(
            shotOrigin,
            shot->direction,
            activeWorldSize());
        const float maximumDistance = std::max(
            0.001F,
            std::min(boundaryDistance, weaponMaximumRange_));

        const ShotResolution resolution = resolveShotCommand(
            ShotCommand{
                nextShotId_,
                shotOrigin,
                shot->direction,
                weaponLogicalBallisticSpeed_,
                kLegacyShotExtent,
                std::max(
                    1,
                    static_cast<int>(std::lround(
                        static_cast<float>(weaponBaseDamage_) *
                        weaponAim_.damageMultiplier()))),
                maximumDistance,
                aimIntentAt(
                    weaponAim_.actualWorldPosition(),
                    shotOrigin,
                    activeEnemySet)});

        if (!resolution.accepted())
        {
            std::terminate();
        }

        logicalBallistics_.emplace_back(
            resolution,
            weaponTracerStyle_,
            weaponTracerLength_,
            weaponTracerOpacity_,
            weaponTracerLifetimeSeconds_);
        if (!shotFeedbackPresentation_.recordAcceptedShot(
                resolution.shotId,
                resolution.origin,
                resolution.direction))
        {
            std::terminate();
        }
        weaponAim_.applyShotRecoil(shotOrigin);
        ++nextShotId_;
    }

    std::vector<ShotCollisionCandidate> collisionCandidates;
    collisionCandidates.reserve(logicalBallistics_.size());
    for (LogicalBallisticFlight &flight : logicalBallistics_)
    {
        const LogicalBallisticAdvance advance = flight.advance(deltaTime);
        const float travelled = std::hypot(
            advance.end.x - advance.start.x,
            advance.end.y - advance.start.y);
        if (flight.tracerStyle() != TracerStyle::None &&
            travelled > 0.0001F)
        {
            // A tracer may span several completed simulation steps, but it
            // never extends ahead of the distance already travelled.
            const float visibleLength = std::min(
                flight.tracerLength(),
                flight.distanceTravelled());
            const Vec2 visibleStart{
                advance.end.x - flight.direction().x * visibleLength,
                advance.end.y - flight.direction().y * visibleLength};
            TracerPresentationSegment segment{
                flight.shotId(),
                visibleStart,
                advance.end,
                flight.direction(),
                flight.tracerStyle(),
                flight.tracerOpacity(),
                flight.tracerLifetimeSeconds(),
                flight.tracerLifetimeSeconds(),
                0.0F};
            const auto existing = std::find_if(
                tracerPresentations_.begin(),
                tracerPresentations_.end(),
                [&](const TracerPresentationSegment &candidate)
                {
                    return candidate.shotId == flight.shotId();
                });
            if (existing == tracerPresentations_.end())
            {
                tracerPresentations_.push_back(segment);
            }
            else
            {
                segment.ageSeconds = existing->ageSeconds;
                *existing = segment;
            }
        }
        collisionCandidates.push_back(
            ShotCollisionCandidate{
                flight.shotId(),
                advance.start,
                advance.end,
                flight.collisionExtent(),
                flight.damage(),
                flight.aimIntent()});
    }

    HitResolutionResult hitResult = resolveShotHits(
        collisionCandidates,
        activeEnemySet,
        activeBlockerSet);

    // Hit resolution removes dead Enemy objects. Keep the one-to-one
    // navigation runtime in the same transaction so the next simulation frame
    // cannot observe mismatched parallel arrays and terminate.
    for (auto removed = hitResult.removedEnemyIndices.rbegin();
         removed != hitResult.removedEnemyIndices.rend();
         ++removed)
    {
        if (*removed >= activeNavigationSet.size())
        {
            std::terminate();
        }
        activeNavigationSet.erase(
            activeNavigationSet.begin() +
            static_cast<std::vector<EnemyNavigationRuntime>::difference_type>(
                *removed));
    }
    if (activeNavigationSet.size() != activeEnemySet.size())
    {
        std::terminate();
    }

    // A large simulation step can resolve a collision before the requested
    // advance endpoint. Clamp presentation to that authoritative hit so the
    // longer streak never appears past an obstacle or enemy.
    for (const HitResult &hit : hitResult.hits)
    {
        const auto flight = std::find_if(
            logicalBallistics_.begin(),
            logicalBallistics_.end(),
            [&](const LogicalBallisticFlight &candidate)
            {
                return candidate.shotId() == hit.shotId;
            });
        const auto tracer = std::find_if(
            tracerPresentations_.begin(),
            tracerPresentations_.end(),
            [&](const TracerPresentationSegment &candidate)
            {
                return candidate.shotId == hit.shotId;
            });
        if (flight == logicalBallistics_.end() ||
            tracer == tracerPresentations_.end())
        {
            continue;
        }

        const float hitDistance = std::hypot(
            hit.position.x - flight->origin().x,
            hit.position.y - flight->origin().y);
        const float visibleLength = std::min(
            flight->tracerLength(),
            hitDistance);
        tracer->end = hit.position;
        tracer->start = Vec2{
            hit.position.x - flight->direction().x * visibleLength,
            hit.position.y - flight->direction().y * visibleLength};
    }

    std::erase_if(
        logicalBallistics_,
        [&](const LogicalBallisticFlight &flight)
        {
            return std::find(
                       hitResult.consumedShotIds.begin(),
                       hitResult.consumedShotIds.end(),
                       flight.shotId()) !=
                   hitResult.consumedShotIds.end();
        });

    for (const LogicalBallisticFlight &flight : logicalBallistics_)
    {
        if (!flight.reachedImpact())
        {
            continue;
        }
        hitResult.hits.push_back(
            HitResult{
                flight.shotId(),
                HitTargetKind::Ground,
                flight.impactPosition(),
                0,
                false,
                HitRegion::Torso,
                HitSemantic::Normal});
    }

    std::erase_if(
        logicalBallistics_,
        [](const LogicalBallisticFlight &flight)
        {
            return flight.reachedImpact();
        });

    // 每次有效命中都生成粒子，
    // 与本次命中是否致命无关。
    for (
        const HitResult &hit :
        hitResult.hits)
    {
        particleSystem_.emitImpact(
            hit.position);
    }
    hitResultsLastUpdate_ = hitResult.hits;

    // enemiesKilled 是 std::size_t。
    // Score 使用 int，因此在职责边界上显式转换。
    const int killedEnemyCount =
        static_cast<int>(
            hitResult.enemiesKilled);

    score_ +=
        killedEnemyCount *
        kScorePerEnemy;

}

const Player &
GameplayWorld::player() const
{
    return player_;
}

const std::vector<LogicalBallisticFlight> &
GameplayWorld::logicalBallistics() const
{
    return logicalBallistics_;
}

std::vector<ShotPresentationSnapshot>
GameplayWorld::shotPresentationSnapshots() const
{
    std::vector<ShotPresentationSnapshot> snapshots;
    snapshots.reserve(tracerPresentations_.size());

    for (const TracerPresentationSegment &tracer : tracerPresentations_)
    {
        const float lifetimeRatio = tracer.lifetimeSeconds > 0.0F
            ? std::clamp(
                  tracer.remainingSeconds / tracer.lifetimeSeconds,
                  0.0F,
                  1.0F)
            : 0.0F;
        constexpr float kTau{6.28318530718F};
        constexpr float kFlickerFrequencyHz{24.0F};
        const float flicker = 0.55F + 0.45F *
            (0.5F + 0.5F * std::sin(
                tracer.ageSeconds * kTau * kFlickerFrequencyHz +
                static_cast<float>(tracer.shotId % 17U) * 1.618F));
        snapshots.push_back(
            ShotPresentationSnapshot{
                tracer.shotId,
                tracer.start,
                tracer.end,
                tracer.direction,
                tracer.style,
                tracer.opacity * std::sqrt(lifetimeRatio) * flicker});
    }

    return snapshots;
}

std::vector<ShotFeedbackPresentationSnapshot>
GameplayWorld::shotFeedbackPresentationSnapshots() const
{
    return shotFeedbackPresentation_.snapshots();
}

Vec2 GameplayWorld::normalizedShotScreenShakeOffset() const noexcept
{
    return shotFeedbackPresentation_.normalizedScreenShakeOffset();
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
    if (!interior.layoutKnown)
    {
        return std::nullopt;
    }
    return RaidInteriorMapProjection{
        interior.id,
        interior.displayName,
        interior.worldSize,
        interior.interiorExit,
        interior.ballisticBlockers};
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
    return particleSystem_.particles();
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

const RaidSession &
GameplayWorld::raidSession() const noexcept
{
    return raidSession_;
}

const RaidTacticalMapState &
GameplayWorld::tacticalMap() const noexcept
{
    return tacticalMap_;
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
    return inOutdoorRaidSpace() && ordinarySurvivorRescuePoint_.has_value() &&
           !ordinarySurvivorRescueSecured_ && raidSession_.isActive() &&
           pointInside(*ordinarySurvivorRescuePoint_, playerCenter(player_));
}

bool GameplayWorld::ordinarySurvivorRescueReady() const noexcept
{
    return ordinarySurvivorRescueInteractionInRange() &&
           ordinarySurvivorRescueDurationSeconds_ > 0.0F &&
           ordinarySurvivorRescueElapsedSeconds_ >=
               ordinarySurvivorRescueDurationSeconds_;
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
    return weaponFire_.spreadDegrees();
}

float GameplayWorld::weaponVisualRecoilPixels() const noexcept
{
    const Vec2 velocity = weaponAim_.recoilPresentationVelocity();
    return std::min(
        18.0F,
        std::sqrt(
            velocity.x * velocity.x + velocity.y * velocity.y) *
            0.02F);
}

WeaponAccuracyProjection
GameplayWorld::weaponAccuracyProjection() const noexcept
{
    const float distance = weaponAim_.aimDistance();
    const float spread = weaponFire_.spreadDegreesAtDistance(distance);
    // worldRadius is the authoritative maximum lateral displacement where
    // the shot ray crosses the current aim distance. The reticle reads this
    // same radius; the fixed ten-pixel difference is only center clearance.
    const float worldRadius = weaponFire_.spreadRadiusAtDistance(distance);
    return WeaponAccuracyProjection{
        weaponAim_.actualWorldPosition(),
        distance,
        spread,
        weaponFire_.contextualMinimumSpreadDegrees(),
        weaponFire_.contextualMaximumSpreadDegrees(),
        worldRadius,
        10.0F + worldRadius,
        weaponAim_.beyondEffectiveRange()};
}

Vec2 GameplayWorld::weaponAimWorldPosition() const noexcept
{
    return weaponAim_.actualWorldPosition();
}

Vec2 GameplayWorld::weaponAimDirection() const noexcept
{
    return weaponAim_.actualDirection();
}

float GameplayWorld::weaponAimDownSightsProgress() const noexcept
{
    return weaponAim_.aimDownSightsProgress();
}

bool GameplayWorld::weaponAimBeyondEffectiveRange() const noexcept
{
    return weaponAim_.beyondEffectiveRange();
}

bool GameplayWorld::weaponAimBeyondMaximumRange() const noexcept
{
    return weaponAim_.beyondMaximumRange();
}

const std::vector<HitResult> &
GameplayWorld::hitResultsLastUpdate() const noexcept
{
    return hitResultsLastUpdate_;
}

bool GameplayWorld::shotFiredLastUpdate() const noexcept
{
    return shotFiredLastUpdate_;
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
    const std::vector<EnemyNavigationRuntime> &navigation =
        activeEnemyNavigation();
    std::vector<std::uint64_t> counts;
    counts.reserve(navigation.size());
    for (const EnemyNavigationRuntime &runtime : navigation)
    {
        counts.push_back(runtime.refreshCount);
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
    const WeaponFireConfig fireConfig{
        definition.shotIntervalSeconds,
        handling.minimumSpreadDegrees,
        handling.maximumSpreadDegrees,
        handling.spreadPerShotDegrees,
        handling.recoveryDelaySeconds,
        handling.spreadRecoveryDegreesPerSecond,
        handling.aimDownSightsAccuracyMultiplier,
        handling.aimDownSightsStabilityMultiplier,
        handling.movingSpreadFraction,
        handling.sprintingSpreadFraction,
        handling.reticleMotionSpreadDegreesPerSecond,
        120.0F,
        1800.0F,
        handling.nearDistanceSpreadScale,
        handling.distanceBloomAtEffectiveRange,
        1.50F};
    const WeaponAimConfig aimConfig{
        handling.maximumReticleSpeed,
        handling.reticleControlAcceleration,
        handling.recoilInitialSpeed,
        handling.recoilDeceleration,
        handling.recoilLateralRatio,
        handling.recoilBendDurationSeconds,
        handling.aimDownSightsDurationSeconds,
        definition.effectiveRange,
        definition.maximumRange};
    if (preserveWeaponFireTransientState)
    {
        weaponFire_.reconfigure(fireConfig);
    }
    else
    {
        weaponFire_ = WeaponFireState{fireConfig};
    }
    // A weapon change owns fire cadence and spread state, but it does not own
    // the player's world-space aiming point. Reconfiguring the existing aim
    // state keeps the relative-input anchor, displaced reticle position and
    // any bounded recoil motion continuous across a completed switch.
    weaponAim_.reconfigure(aimConfig);
    weaponBaseDamage_ = definition.baseDamage;
    weaponMaximumRange_ = definition.maximumRange;
    weaponLogicalBallisticSpeed_ = definition.logicalBallisticSpeed;
    weaponTracerStyle_ = TracerStyle::Weak;
    weaponTracerLength_ = handling.weakTracerLength;
    weaponTracerOpacity_ = handling.weakTracerOpacity;
    weaponTracerLifetimeSeconds_ = handling.weakTracerLifetimeSeconds;
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

    if (!killed)
    {
        return false;
    }

    const bool markedDead =
        raidSession_.markPlayerDead();

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

void GameplayWorld::emitPlayerNoise(float radius) noexcept
{
    if (!raidSession_.isActive() || !std::isfinite(radius) || radius <= 0.0F)
    {
        return;
    }
    const Vec2 source = playerCenter(player_);
    const float radiusSquared = radius * radius;
    for (Enemy &enemy : activeEnemies())
    {
        const Vec2 center = enemyCenter(enemy);
        const float dx = center.x - source.x;
        const float dy = center.y - source.y;
        if (dx * dx + dy * dy <= radiusSquared)
        {
            enemy.hearTarget(source);
        }
    }
}

bool GameplayWorld::resolveEnemyAttackDamage(
    EnemyAttackType type,
    int legacyDamage)
{
    if (!deferPlayerDamageResolution_)
    {
        if (legacyDamage > 0 &&
            enemyDamageProtectionRemainingSeconds_ > 0.0F)
        {
            return false;
        }
        if (legacyDamage > 0)
        {
            enemyDamageProtectionRemainingSeconds_ =
                kEnemyDamageProtectionDurationSeconds;
        }
        return damagePlayer(legacyDamage);
    }

    const EnemyAttackCombatDamage damage = enemyAttackCombatDamage(type);
    if (damage.baseDamage <= 0)
    {
        return false;
    }
    if (enemyDamageProtectionRemainingSeconds_ > 0.0F)
    {
        return false;
    }
    enemyDamageProtectionRemainingSeconds_ =
        kEnemyDamageProtectionDurationSeconds;
    pendingPlayerDamageObservations_.push_back(
        PlayerDamageObservation{
            damage.baseDamage,
            damage.region,
            damage.penetration,
            damage.armorDamage,
            damage.weakPoint,
            type == EnemyAttackType::Scratch
                ? WoundSource::Scratch
                : type == EnemyAttackType::Bite
                    ? WoundSource::Bite
                    : WoundSource::None});
    return false;
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

std::vector<Enemy> &GameplayWorld::activeEnemies() noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemies
        : enemies_;
}

const std::vector<Enemy> &GameplayWorld::activeEnemies() const noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemies
        : enemies_;
}

std::vector<GameplayWorld::EnemyNavigationRuntime> &
GameplayWorld::activeEnemyNavigation() noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemyNavigation
        : enemyNavigation_;
}

const std::vector<GameplayWorld::EnemyNavigationRuntime> &
GameplayWorld::activeEnemyNavigation() const noexcept
{
    return activeInteriorIndex_.has_value()
        ? interiors_[*activeInteriorIndex_].enemyNavigation
        : enemyNavigation_;
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
    const auto found = std::find_if(
        navigationFieldCache_.begin(),
        navigationFieldCache_.end(),
        [&](const NavigationFieldCache &candidate)
        {
            return candidate.spaceId == spaceId &&
                   candidate.actorSize.x == actorSize.x &&
                   candidate.actorSize.y == actorSize.y;
        });
    if (found != navigationFieldCache_.end())
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
    navigationFieldCache_.push_back(
        NavigationFieldCache{
            spaceId,
            actorSize,
            std::move(*field)});
    return &navigationFieldCache_.back().field;
}

void GameplayWorld::cacheNavigationFieldsForSpace(
    const RaidSpaceDefinitionId &spaceId,
    Vec2 worldSize,
    std::span<const BallisticBlocker> blockers,
    std::span<const Enemy> enemies)
{
    for (const Enemy &enemy : enemies)
    {
        const Vec2 actorSize = enemy.size();
        const bool alreadyCached = std::any_of(
            navigationFieldCache_.begin(),
            navigationFieldCache_.end(),
            [&](const NavigationFieldCache &candidate)
            {
                return candidate.spaceId == spaceId &&
                       candidate.actorSize.x == actorSize.x &&
                       candidate.actorSize.y == actorSize.y;
            });
        if (alreadyCached)
        {
            continue;
        }
        std::optional<RaidSpaceNavigationField> field =
            RaidSpaceNavigationField::build(
                actorSize,
                worldSize,
                blockers,
                2.0F);
        if (field.has_value())
        {
            navigationFieldCache_.push_back(
                NavigationFieldCache{
                    spaceId,
                    actorSize,
                    std::move(*field)});
        }
    }
}

std::size_t GameplayWorld::outdoorAliveEnemyCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        enemies_.begin(), enemies_.end(),
        [](const Enemy &enemy) { return !enemy.isDead(); }));
}

bool GameplayWorld::tryTransitionRaidSpace(
    const GameplayInput &input) noexcept
{
    if (!input.interactJustPressed || input.inventoryOpen ||
        player_.isControlled() || !raidSession_.isActive())
    {
        return false;
    }
    const Vec2 center = playerCenter(player_);
    if (activeInteriorIndex_.has_value())
    {
        InteriorRuntime &interior = interiors_[*activeInteriorIndex_];
        if (!pointInside(interior.interiorExit, center))
        {
            return false;
        }
        const Vec2 returnPosition = interior.exteriorReturn;
        activeInteriorIndex_.reset();
        static_cast<void>(player_.setPosition(returnPosition));
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
        activeInteriorIndex_ = static_cast<std::size_t>(
            std::distance(interiors_.begin(), interior));
        static_cast<void>(player_.setPosition(interior->interiorSpawn));
    }
    clearSpatialTransientPresentation();
    weaponAim_.reanchor(
        playerCenter(player_),
        player_.facingDirection(),
        activeWorldSize());
    return true;
}

void GameplayWorld::clearSpatialTransientPresentation() noexcept
{
    logicalBallistics_.clear();
    tracerPresentations_.clear();
    particleSystem_.clear();
    shotFeedbackPresentation_.reset();
    hitResultsLastUpdate_.clear();
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
            enemies_.emplace_back(
                candidate.position,
                candidate.size,
                Vec2{},
                candidate.maxHealth,
                nextCombatTargetId_++);
            enemyNavigation_.emplace_back();
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
