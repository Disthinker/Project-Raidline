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

namespace
{
    constexpr float kLegacyShotExtent{8.0f};
    constexpr float kMaximumEnemyStepTime{1.0F / 120.0F};
    constexpr float kMaximumEnemySubsteps{2048.0F};

    constexpr int kScorePerEnemy{100};

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
        config.playerCurrentHealth > config.playerMaximumHealth)
    {
        throw std::invalid_argument{"RaidWorldConfig is invalid"};
    }
    worldSize_ = config.worldSize;
    alphaRaidWorld_ = true;
    deferPlayerDamageResolution_ = config.deferPlayerDamageResolution;
    ballisticBlockers_ = std::move(config.ballisticBlockers);
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
    player_ = Player{
        config.playerSpawn.x,
        config.playerSpawn.y,
        config.playerMaximumHealth,
        config.playerCurrentHealth};
    extractionPoint_ = ExtractionPoint{
        config.extractionPoint.position,
        config.extractionPoint.size};
    raidSession_ = RaidSession{RaidSessionConfig{0.0F, 3.0F, false}};
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
    hitResultsLastUpdate_.clear();
    shotFiredLastUpdate_ = false;
    enemiesAlertedLastUpdate_ = 0U;
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
    for (const BallisticBlocker &blocker : ballisticBlockers_)
    {
        if (isCollision(playerBounds(player_), blocker.bounds))
        {
            static_cast<void>(
                player_.setPosition(playerPositionBeforeMovement));
            break;
        }
    }

    const Vec2 centerAfterMovement = playerCenter(player_);
    Vec2 desiredAimPosition{
        centerAfterMovement.x + player_.facingDirection().x * 400.0F,
        centerAfterMovement.y + player_.facingDirection().y * 400.0F};
    if (input.aimWorldPosition.has_value())
    {
        desiredAimPosition = *input.aimWorldPosition;
    }
    weaponAim_.update(
        desiredAimPosition,
        centerAfterMovement,
        worldSize_,
        input.aimDownSights,
        deltaTime,
        input.aimMotionDelta,
        AimControlMode::Direct);
    static_cast<void>(player_.faceDirection(weaponAim_.actualDirection()));

    // 撤离使用移动后的 Player 逻辑中心，而不是更大的渲染精灵。
    raidSession_.update(
        deltaTime,
        !player_.isControlled() && extractionPoint_.contains(
            playerCenter(player_)));

    // 终局形成后，本帧不再产生拾取、射击、敌人或命中结果。
    if (!raidSession_.isActive())
    {
        return;
    }

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

    for (std::size_t step{0U};
         step < enemySubsteps;
         ++step)
    {
        const Vec2 playerPosition =
            playerCenter(player_);
        std::vector<EnemySquadMemberSnapshot> enemySnapshots;
        enemySnapshots.reserve(enemies_.size());
        for (const Enemy &enemy : enemies_)
        {
            enemySnapshots.push_back(
                EnemySquadMemberSnapshot{
                    enemyCenter(enemy),
                    !enemy.isDead(),
                    enemy.awarenessState(),
                    enemy.attackPhase()});
        }

        const std::vector<EnemyTacticalDirective> directives =
            enemySquadCoordinator_.decide(
                enemySnapshots,
                playerPosition);

        for (std::size_t enemyIndex{0U};
             enemyIndex < enemies_.size();
             ++enemyIndex)
        {
            Enemy &enemy = enemies_[enemyIndex];
            const EnemyAwarenessState awarenessBefore =
                enemy.awarenessState();

            static_cast<void>(
                enemy.updateTowardsTarget(
                    playerPosition,
                    directives[enemyIndex],
                    enemyStepTime,
                    worldWidth(),
                    worldHeight()));
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

    if (!controlsSuppressed &&
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
            worldSize_);
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
                    enemies_)});

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
        enemies_,
        ballisticBlockers_);

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
    return enemies_;
}

const std::vector<BallisticBlocker> &
GameplayWorld::ballisticBlockers() const noexcept
{
    return ballisticBlockers_;
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

const RaidSession &
GameplayWorld::raidSession() const noexcept
{
    return raidSession_;
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
    for (Enemy &enemy : enemies_)
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
        return damagePlayer(legacyDamage);
    }

    const EnemyAttackCombatDamage damage = enemyAttackCombatDamage(type);
    if (damage.baseDamage <= 0)
    {
        return false;
    }
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
    return worldSize_.x;
}

float GameplayWorld::worldHeight() const noexcept
{
    return worldSize_.y;
}
