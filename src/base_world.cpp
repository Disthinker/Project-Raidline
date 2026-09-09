#include "base_world.h"
#include "enemy_attack_contact.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

#include "collision.h"

namespace
{
bool pointInside(Vec2 point, const ContentRect &rect) noexcept
{
    return point.x >= rect.position.x && point.y >= rect.position.y &&
        point.x <= rect.position.x + rect.size.x &&
        point.y <= rect.position.y + rect.size.y;
}

AnimationClip makeBasePlayerMoveClip()
{
    return AnimationClip{
        std::vector<AnimationFrame>(6, AnimationFrame{0.10F})};
}

float resolveHorizontalMovement(
    Vec2 position,
    Vec2 size,
    float desiredX,
    const RaidSpaceBlockerIndex &index,
    const std::vector<std::size_t> &candidates) noexcept
{
    float resolvedX = desiredX;
    for (const std::size_t candidate : candidates)
    {
        resolvedX = resolveHorizontalCollision(
            Rect{position, size},
            resolvedX,
            index.blockerBounds(candidate));
    }
    return resolvedX;
}

float resolveVerticalMovement(
    Vec2 position,
    Vec2 size,
    float desiredY,
    const RaidSpaceBlockerIndex &index,
    const std::vector<std::size_t> &candidates) noexcept
{
    float resolvedY = desiredY;
    for (const std::size_t candidate : candidates)
    {
        resolvedY = resolveVerticalCollision(
            Rect{position, size},
            resolvedY,
            index.blockerBounds(candidate));
    }
    return resolvedY;
}

bool finiteRect(ContentRect bounds) noexcept
{
    return std::isfinite(bounds.position.x) &&
        std::isfinite(bounds.position.y) &&
        std::isfinite(bounds.size.x) && std::isfinite(bounds.size.y) &&
        bounds.size.x > 0.0F && bounds.size.y > 0.0F;
}

std::string defenseLayoutIdentity(const HomeRegionLayout &layout,
    const std::vector<BallisticBlocker> &blockers)
{
    std::uint64_t h=layout.layoutHash;
    for (const auto &b:blockers) {
        for (float f:{b.bounds.position.x,b.bounds.position.y,b.bounds.size.x,b.bounds.size.y}) {
            h^=std::bit_cast<std::uint32_t>(f);h*=1099511628211ULL;
        }
    }
    return "home-defense-layout-"+std::to_string(h);
}
}

BaseWorld::BaseWorld()
    : playerMovementAnimator_{
          makeBasePlayerMoveClip(),
          AnimationPlayMode::Loop}
{
    rebuildSite("regional_base_site.greyline_yard");
}

void BaseWorld::configureSite(std::string_view siteDefinitionId)
{
    configureSite(siteDefinitionId, {});
}

BaseFacilityAccessGeometry baseFacilityAccessGeometry(
    const BaseFacility &facility) noexcept
{
    return projectBaseFacilityAccessGeometry(
        {facility.bounds.position.x + facility.bounds.size.x * 0.5F,
         facility.bounds.position.y + facility.bounds.size.y * 0.5F},
        facility.bounds.size);
}

std::optional<BaseFacilityWorkSocketProjection>
baseFacilityWorkSocket(const BaseFacility &facility) noexcept
{
    std::optional<BaseFacilityDefinitionId> definitionId;
    switch (facility.kind)
    {
    case BaseFacilityKind::Storage:
        definitionId = BaseFacilityDefinitionId{"base_facility.warehouse"};
        break;
    case BaseFacilityKind::Medical:
        definitionId = BaseFacilityDefinitionId{"base_facility.medical"};
        break;
    case BaseFacilityKind::Dormitory:
        definitionId = BaseFacilityDefinitionId{"base_facility.dormitory"};
        break;
    case BaseFacilityKind::KitchenWater:
        definitionId = BaseFacilityDefinitionId{
            "base_facility.kitchen_water"};
        break;
    case BaseFacilityKind::Workshop:
        definitionId = BaseFacilityDefinitionId{"base_facility.workshop"};
        break;
    case BaseFacilityKind::Supply:
    case BaseFacilityKind::Allocation:
    case BaseFacilityKind::RaidGate:
        return std::nullopt;
    }
    return projectBaseFacilityWorkSocket(
        *definitionId,
        {facility.bounds.position.x + facility.bounds.size.x * 0.5F,
         facility.bounds.position.y + facility.bounds.size.y * 0.5F},
        facility.bounds.size);
}

void BaseWorld::configureSite(
    std::string_view siteDefinitionId,
    std::vector<BaseFacilitySpatialOverride> overrides,
    std::string plotId)
{
    // Queue outcomes remain authoritative in Profile. Their new spatial
    // geometry takes effect after this frozen defense activity concludes.
    if (baseDefense_) return;
    const std::string normalized = siteDefinitionId.empty()
        ? "regional_base_site.greyline_yard" : std::string{siteDefinitionId};
    if (normalized != siteDefinitionId_ || overrides != facilityOverrides_ || plotId != plotId_)
    {
        const bool sameSite = normalized == siteDefinitionId_;
        const Vec2 previousPlayerPosition = playerPosition_;
        facilityOverrides_ = std::move(overrides);
        plotId_ = std::move(plotId);
        rebuildSite(normalized);
        if (sameSite)
        {
            playerPosition_ = previousPlayerPosition;
            shooting_.reanchor(
                {playerPosition_.x + playerSize_.x * 0.5F,
                 playerPosition_.y + playerSize_.y * 0.5F},
                playerFacingDirection_, layout_.worldSize);
        }
    }
}

void BaseWorld::rebuildSite(std::string_view siteDefinitionId)
{
    siteDefinitionId_ = siteDefinitionId;
    groundBlockers_.clear();
    layout_ = plotId_.empty() ? generateHomeRegionLayout(siteDefinitionId_)
        : generateFoundingHomeRegionLayout(siteDefinitionId_, plotId_);
    walkableBounds_ = Rect{{24.0F, 24.0F},
                           {layout_.worldSize.x - 48.0F,
                            layout_.worldSize.y - 48.0F}};
    const Vec2 origin = layout_.baseParcel.position;
    facilities_ = {
        BaseFacility{BaseFacilityKind::Storage,
                     {{origin.x + 80.0F, origin.y + 220.0F},
                      {300.0F, 220.0F}}},
        BaseFacility{BaseFacilityKind::Supply,
                     {{origin.x + 1220.0F, origin.y + 220.0F},
                      {300.0F, 220.0F}}},
        BaseFacility{BaseFacilityKind::Allocation,
                     {{origin.x + 80.0F, origin.y + 760.0F},
                      {300.0F, 180.0F}}},
        BaseFacility{BaseFacilityKind::Medical,
                     {{origin.x + 1220.0F, origin.y + 760.0F},
                      {300.0F, 180.0F}}},
        BaseFacility{BaseFacilityKind::Dormitory,
                     {{origin.x + 460.0F, origin.y + 790.0F},
                      {270.0F, 150.0F}}},
        BaseFacility{BaseFacilityKind::KitchenWater,
                     {{origin.x + 450.0F, origin.y + 310.0F},
                      {300.0F, 180.0F}}, false},
        BaseFacility{BaseFacilityKind::Workshop,
                     {{origin.x + 870.0F, origin.y + 520.0F},
                      {270.0F, 170.0F}}},
        BaseFacility{BaseFacilityKind::RaidGate,
                     {{origin.x + 650.0F, origin.y + 40.0F},
                      {300.0F, 130.0F}}}};
    for (const BaseFacilitySpatialOverride &override : facilityOverrides_)
    {
        const auto facility = std::find_if(
            facilities_.begin(), facilities_.end(),
            [&](const BaseFacility &candidate)
            { return candidate.kind == override.kind; });
        if (facility == facilities_.end() ||
            !std::isfinite(override.worldCenter.x) ||
            !std::isfinite(override.worldCenter.y))
            continue;
        facility->active = override.active;
        facility->bounds.position = {
            override.worldCenter.x - facility->bounds.size.x * 0.5F,
            override.worldCenter.y - facility->bounds.size.y * 0.5F};
    }
    if (surveying())
    {
        for (auto &facility : facilities_) facility.active = false;
        facilities_[0] = BaseFacility{BaseFacilityKind::Storage,
            {{origin.x + 30, origin.y + 30}, {100,80}}, true};
    }
    rebuildCollisionIndex();
    presentationCache_ = {};
    presentationCacheValid_ = false;
    resetAtMedicalPoint();
    shooting_.clearSpatialTransientPresentation();
    baseDefense_.reset();
    perimeterEnemies_.reset();
    perimeterRemovals_.clear();
    perimeterCycleIndex_.reset();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

void BaseWorld::rebuildCollisionIndex()
{
    movementBlockers_.clear();
    BallisticBlockerId id{1U};
    movementBlockers_.reserve(layout_.movementBlockers.size() +
                              facilities_.size() + groundBlockers_.size());
    for (const ContentRect &bounds : layout_.movementBlockers)
        movementBlockers_.push_back(BallisticBlocker{
            id++, Rect{bounds.position, bounds.size}});
    for (const BaseFacility &facility : facilities_)
        if (facility.active)
            movementBlockers_.push_back(
                BallisticBlocker{id++, facility.bounds});
    for (const ContentRect &bounds : groundBlockers_)
        movementBlockers_.push_back(BallisticBlocker{
            id++, Rect{bounds.position, bounds.size}});
    movementBlockerIndex_ = RaidSpaceBlockerIndex::build(
        layout_.worldSize, movementBlockers_, 320.0F);
    if (!movementBlockerIndex_.has_value())
        throw std::logic_error{"Home Region blocker index is invalid"};
}

std::optional<BaseFacilityKind> BaseWorld::update(
    const BaseInput &input,
    float deltaTime)
{
    if (baseDefense_ && std::isfinite(deltaTime)) deltaTime=std::clamp(deltaTime,0.0F,0.1F);
    perimeterDamageObservation_.reset();
    perimeterDamageProtectionRemainingSeconds_ = std::max(
        0.0F,
        perimeterDamageProtectionRemainingSeconds_ -
            std::max(0.0F, deltaTime));
    shooting_.beginFrame(deltaTime);
    if (std::isfinite(deltaTime) && deltaTime > 0.0F)
    {
        Vec2 direction{
            static_cast<float>(input.moveRight) -
                static_cast<float>(input.moveLeft),
            static_cast<float>(input.moveDown) -
                static_cast<float>(input.moveUp)};
        const float lengthSquared =
            direction.x * direction.x + direction.y * direction.y;
        if (lengthSquared > 0.0F)
        {
            const bool wasMoving = playerIsMoving_;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            direction.x *= inverseLength;
            direction.y *= inverseLength;
            if (direction.x != 0.0F)
            {
                playerHorizontalFacing_ = direction.x;
            }
            playerFacingDirection_ = direction;
            if (playerFacingDirection_.x == 0.0F)
            {
                playerFacingDirection_.x = playerHorizontalFacing_;
            }
            playerIsMoving_ = true;
            if (!wasMoving)
            {
                playerMovementAnimator_.reset();
            }
            playerMovementAnimator_.update(deltaTime);
            const float speed = (input.sprint ? 280.0F : 180.0F) *
                std::clamp(input.movementSpeedMultiplier, 0.0F, 1.0F);
            const float maximumY = walkableBounds_.position.y +
                walkableBounds_.size.y - playerSize_.y;
            const float maximumPlayerX = walkableBounds_.position.x +
                walkableBounds_.size.x - playerSize_.x;

            const float desiredX = std::clamp(
                playerPosition_.x + direction.x * speed * deltaTime,
                walkableBounds_.position.x,
                maximumPlayerX);
            const float desiredYForQuery = std::clamp(
                playerPosition_.y + direction.y * speed * deltaTime,
                walkableBounds_.position.y,
                maximumY);
            const Rect queryBounds{
                {std::min(playerPosition_.x, desiredX),
                 std::min(playerPosition_.y, desiredYForQuery)},
                {std::abs(desiredX - playerPosition_.x) + playerSize_.x,
                 std::abs(desiredYForQuery - playerPosition_.y) +
                     playerSize_.y}};
            movementBlockerIndex_->queryCandidateIndices(
                queryBounds, movementCandidates_);
            playerPosition_.x = resolveHorizontalMovement(
                playerPosition_,
                playerSize_,
                desiredX,
                *movementBlockerIndex_,
                movementCandidates_);

            const float desiredY = std::clamp(
                playerPosition_.y + direction.y * speed * deltaTime,
                walkableBounds_.position.y,
                maximumY);
            playerPosition_.y = resolveVerticalMovement(
                playerPosition_,
                playerSize_,
                desiredY,
                *movementBlockerIndex_,
                movementCandidates_);
        }
        else
        {
            playerIsMoving_ = false;
            playerMovementAnimator_.reset();
        }
    }

    const Vec2 playerCenter{
        playerPosition_.x + playerSize_.x * 0.5F,
        playerPosition_.y + playerSize_.y * 0.5F};
    const bool pointerAiming = input.aimWorldPosition.has_value() ||
        input.aimMotionDelta.has_value();
    if (pointerAiming)
    {
        shooting_.updateAim(
            input,
            playerCenter,
            playerFacingDirection_,
            layout_.worldSize,
            deltaTime);
        playerFacingDirection_ = shooting_.aimDirection();
        if (playerFacingDirection_.x != 0.0F)
        {
            playerHorizontalFacing_ = playerFacingDirection_.x;
        }
    }
    else
    {
        shooting_.reanchor(
            playerCenter, playerFacingDirection_, layout_.worldSize);
    }
    if (baseDefense_) {
        baseDefense_->advance(input,deltaTime,playerPosition_,playerSize_,
            playerIsMoving_,shooting_,movementBlockers_);
        return input.interactJustPressed ? interactableFacility() : std::nullopt;
    }
    // Combat order matches Raid: movement/contact first, shots/removal second.
    // No actor reference or index escapes this loop into shot-driven removal.
    const HomeRegionSafetyZone playerZone = playerSafetyZone();
    for (std::size_t index = 0; index < perimeterEnemies_.size(); ++index)
    {
        Enemy &enemy = perimeterEnemies_[index];
        if (enemy.isDead())
            continue;
        const Vec2 before = enemy.position();
        const Vec2 enemyCenter{
            before.x + enemy.size().x * 0.5F,
            before.y + enemy.size().y * 0.5F};
        const Vec2 offset{
            playerCenter.x - enemyCenter.x,
            playerCenter.y - enemyCenter.y};
        const float distanceSquared =
            offset.x * offset.x + offset.y * offset.y;
        const bool playerExposed =
            playerZone == HomeRegionSafetyZone::Perimeter;
        const bool targetVisible = playerExposed &&
            movementBlockerIndex_->hasLineOfSight(enemyCenter, playerCenter) &&
            distanceSquared <= 900.0F * 900.0F;
        if (targetVisible)
            enemy.hearTarget(playerCenter);

        EnemyTacticalDirective directive;
        directive.role = EnemyTacticalRole::Engage;
        directive.canStartAttack = playerExposed;
        const std::optional<Vec2> navigationTarget = playerExposed
            ? std::nullopt
            : std::optional<Vec2>{perimeterEnemies_.state(enemy.combatTargetId())};
        static_cast<void>(enemy.updateTowardsTarget(
            playerExposed ? playerCenter : perimeterEnemies_.state(enemy.combatTargetId()),
            directive,
            deltaTime,
            layout_.worldSize.x,
            layout_.worldSize.y,
            targetVisible,
            navigationTarget,
            !playerExposed));

        Vec2 resolved = enemy.position();
        const Rect queryBounds{
            {std::min(before.x, resolved.x), std::min(before.y, resolved.y)},
            {std::abs(resolved.x - before.x) + enemy.size().x,
             std::abs(resolved.y - before.y) + enemy.size().y}};
        movementBlockerIndex_->queryCandidateIndices(
            queryBounds, movementCandidates_);
        resolved.x = resolveHorizontalMovement(
            before, enemy.size(), resolved.x,
            *movementBlockerIndex_, movementCandidates_);
        resolved.y = resolveVerticalMovement(
            {resolved.x, before.y}, enemy.size(), resolved.y,
            *movementBlockerIndex_, movementCandidates_);
        const Vec2 resolvedCenter{
            resolved.x + enemy.size().x * 0.5F,
            resolved.y + enemy.size().y * 0.5F};
        if (queryHomeRegionSafetyZone(resolvedCenter, layout_.baseParcel) !=
            HomeRegionSafetyZone::Perimeter)
            resolved = before;
        static_cast<void>(enemy.setPosition(resolved));

        const auto contact = resolveEnemyAttackContact(
            enemy, Rect{playerPosition_, playerSize_}, playerExposed,
            perimeterDamageProtectionRemainingSeconds_, [&] {
                const auto p = enemy.position();
                return movementBlockerIndex_->hasLineOfSight(
                    {p.x + enemy.size().x * 0.5F, p.y + enemy.size().y * 0.5F}, playerCenter);
            });
        if (contact.damage) perimeterDamageObservation_ = contact.damage;
    }

    perimeterRemovals_ = shooting_.advanceShots(
        input, deltaTime, playerCenter,
        std::max(playerSize_.x, playerSize_.y), playerIsMoving_, false,
        layout_.worldSize, perimeterEnemies_, movementBlockers_).removals;
    // Only accepted shots alert survivors. Awareness changes now; movement
    // consumes it on the next update, never by replaying the current frame.
    if (shooting_.shotFiredLastUpdate() &&
        playerZone == HomeRegionSafetyZone::Perimeter)
    {
        for (auto &enemy : perimeterEnemies_)
        {
            const Vec2 p = enemy.position();
            const float dx = p.x + enemy.size().x * 0.5F - playerCenter.x;
            const float dy = p.y + enemy.size().y * 0.5F - playerCenter.y;
            if (dx * dx + dy * dy <= 1500.0F * 1500.0F)
                enemy.hearTarget(playerCenter);
        }
    }

    if (input.interactJustPressed)
    {
        return interactableFacility();
    }
    return std::nullopt;
}

void BaseWorld::configureHomePerimeter(
    const HomePerimeterSiteSnapshot *snapshot)
{
    if (snapshot == nullptr)
    {
        if (perimeterCycleIndex_ && !baseDefense_)
            clearSpatialCombatState();
        perimeterEnemies_.reset();
        perimeterRemovals_.clear();
        perimeterCycleIndex_.reset();
        return;
    }
    if (perimeterCycleIndex_.has_value() &&
        *perimeterCycleIndex_ == snapshot->cycleIndex)
        return;

    // Local enemy IDs may be reused by a new cycle. Never carry a previous
    // cycle's aim intent/flight into it; an active Defense owns its own shots.
    if (!baseDefense_)
        clearSpatialCombatState();
    perimeterEnemies_.reset();
    perimeterRemovals_.clear();
    perimeterEnemies_.reserve(snapshot->enemies.size());
    for (const HomePerimeterEnemySnapshot &enemy : snapshot->enemies)
    {
        if (enemy.health <= 0)
        {
            perimeterEnemies_.restoreRetiredIdentity(enemy.localId);
            continue;
        }
        perimeterEnemies_.spawn(Enemy{
            enemy.position,
            enemy.size,
            Vec2{},
            enemy.maximumHealth,
            static_cast<CombatTargetId>(enemy.localId)}, enemy.spawnPosition);
        const int damage = enemy.maximumHealth - enemy.health;
        if (damage > 0)
            static_cast<void>(perimeterEnemies_.back().takeDamage(damage));
    }
    perimeterCycleIndex_ = snapshot->cycleIndex;
}

HomeRegionSafetyZone BaseWorld::playerSafetyZone() const noexcept
{
    return queryHomeRegionSafetyZone(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        layout_.baseParcel);
}

const std::vector<Enemy> &BaseWorld::perimeterEnemies() const noexcept
{
    return perimeterEnemies_;
}

std::vector<HomePerimeterEnemySnapshot>
BaseWorld::perimeterEnemySnapshots() const
{
    std::vector<HomePerimeterEnemySnapshot> result;
    result.reserve(perimeterEnemies_.size());
    for (std::size_t index = 0; index < perimeterEnemies_.size(); ++index)
    {
        const Enemy &enemy = perimeterEnemies_[index];
        result.push_back(HomePerimeterEnemySnapshot{
            static_cast<std::uint32_t>(enemy.combatTargetId()),
            perimeterEnemies_.state(enemy.combatTargetId()),
            enemy.position(),
            enemy.size(),
            enemy.maxHealth(),
            enemy.health()});
    }
    return result;
}

int BaseWorld::perimeterDamageLastUpdate() const noexcept
{
    return perimeterDamageObservation_ ? perimeterDamageObservation_->baseDamage : 0;
}

std::optional<BaseDefenseSnapshot> BaseWorld::prepareBaseDefenseSnapshot(
    BaseDefenseSnapshot s, const EnemyCombatDefinition &enemyDefinition) const
{
    if (baseDefense_ || surveying()) return std::nullopt;
    s.siteDefinitionId=siteDefinitionId_;s.plotId=plotId_;
    s.worldSize=layout_.worldSize;s.safeCore={layout_.baseParcel.position,layout_.baseParcel.size};
    s.layoutIdentity=defenseLayoutIdentity(layout_,movementBlockers_);
    s.movementBlockers.clear();
    for (const auto &blocker:movementBlockers_) s.movementBlockers.push_back(blocker.bounds);
    s.playerPosition=playerPosition_;s.shooting=shooting_.checkpoint();
    // A new activity reuses local target IDs, not the previous activity's shots.
    // Only the candidate is changed; a rejected start preserves the live world.
    s.shooting.flights.clear();
    return BaseDefenseRuntime::prepare(std::move(s),movementBlockers_,enemyDefinition);
}
bool BaseWorld::resumeBaseDefense(const BaseDefenseSnapshot &s)
{
    std::vector<BallisticBlocker> frozen;
    BallisticBlockerId next=1;
    for (auto rect:s.movementBlockers) frozen.push_back({next++,rect});
    if (s.siteDefinitionId!=siteDefinitionId_ || s.plotId!=plotId_ ||
        s.worldSize.x!=layout_.worldSize.x || s.worldSize.y!=layout_.worldSize.y ||
        s.safeCore.position.x!=layout_.baseParcel.position.x ||
        s.safeCore.position.y!=layout_.baseParcel.position.y ||
        s.safeCore.size.x!=layout_.baseParcel.size.x || s.safeCore.size.y!=layout_.baseParcel.size.y ||
        s.layoutIdentity!=defenseLayoutIdentity(layout_,frozen)) return false;
    BaseDefenseRuntime candidate;
    if (!candidate.resume(s,frozen)) return false;
    auto index=RaidSpaceBlockerIndex::build(layout_.worldSize,frozen,320);
    if (!index) return false;
    const Rect playerBody{s.playerPosition,playerSize_};
    if(playerBody.position.x<0 || playerBody.position.y<0 ||
        playerBody.position.x+playerBody.size.x>s.worldSize.x ||
        playerBody.position.y+playerBody.size.y>s.worldSize.y) return false;
    std::vector<std::size_t> playerCandidates;
    index->queryCandidateIndices(playerBody,playerCandidates);
    for(auto i:playerCandidates)
        if(isCollision(playerBody,index->blockerBounds(i))) return false;
    WorldShootingRuntime shootingCandidate=shooting_;
    if (!shootingCandidate.restoreCheckpoint(s.shooting)) return false;
    baseDefense_=std::move(candidate);shooting_=std::move(shootingCandidate);
    movementBlockers_=std::move(frozen);movementBlockerIndex_=std::move(index);
    playerPosition_=s.playerPosition;
    playerFacingDirection_=shooting_.aimDirection();playerIsMoving_=false;
    if(playerFacingDirection_.x!=0) playerHorizontalFacing_=playerFacingDirection_.x;
    playerMovementAnimator_.reset();
    return true;
}
std::optional<BaseDefenseSnapshot> BaseWorld::baseDefenseCheckpoint() const
{
    if (!baseDefense_) return std::nullopt;
    return baseDefense_->checkpoint(shooting_);
}
const BaseDefenseSnapshot *BaseWorld::baseDefenseState() const noexcept
{
    return baseDefense_ ? &baseDefense_->state() : nullptr;
}
bool BaseWorld::baseDefenseActive() const noexcept {return baseDefense_.has_value();}
const std::vector<Enemy> &BaseWorld::baseDefenseEnemies() const noexcept
{
    static const std::vector<Enemy> empty;
    return baseDefense_ ? baseDefense_->enemies() : empty;
}
int BaseWorld::baseDefenseDamageLastUpdate() const noexcept
{
    return baseDefense_ ? baseDefense_->damageLastUpdate() : 0;
}
std::optional<EnemyAttackType> BaseWorld::baseDefenseAttackTypeLastUpdate() const noexcept
{
    return baseDefense_ ? baseDefense_->attackTypeLastUpdate() : std::nullopt;
}
const BaseDefenseRuntimeMetrics &BaseWorld::baseDefenseMetrics() const noexcept
{
    static const BaseDefenseRuntimeMetrics empty;
    return baseDefense_ ? baseDefense_->metrics() : empty;
}
void BaseWorld::clearBaseDefense() noexcept
{
    if (!baseDefense_) return;
    baseDefense_.reset();clearSpatialCombatState();
    rebuildCollisionIndex();
    perimeterDamageProtectionRemainingSeconds_=0.25F;
}

void BaseWorld::clearSpatialCombatState() noexcept
{
    shooting_.clearSpatialTransientPresentation();
    perimeterRemovals_.clear();
    perimeterDamageObservation_.reset();
}

void BaseWorld::resetCombatForProfileLoad() noexcept
{
    clearBaseDefense();
    configureHomePerimeter(nullptr);
    clearSpatialCombatState();
    shooting_ = WorldShootingRuntime{};
    perimeterDamageProtectionRemainingSeconds_ = 0.0F;
}

void BaseWorld::discardUncommittedShot() noexcept
{
    shooting_.clearSpatialTransientPresentation();
}

Vec2 BaseWorld::playerPosition() const noexcept
{
    return playerPosition_;
}

Vec2 BaseWorld::playerSize() const noexcept
{
    return playerSize_;
}

Vec2 BaseWorld::playerFacingDirection() const noexcept
{
    return playerFacingDirection_;
}

bool BaseWorld::playerIsMoving() const noexcept
{
    return playerIsMoving_;
}

std::size_t BaseWorld::playerAnimationFrame() const noexcept
{
    return playerMovementAnimator_.currentFrameIndex();
}

const std::array<BaseFacility, 8> &BaseWorld::facilities() const noexcept
{
    return facilities_;
}

void BaseWorld::configureWeaponFire(const WeaponUseDefinition &definition)
{
    shooting_.configureWeapon(definition);
}

void BaseWorld::configureWeaponFire(
    const WeaponUseDefinition &definition,
    const WeaponHandlingParameters &handling,
    bool preserveWeaponFireTransientState)
{
    shooting_.configureWeapon(
        definition, handling, preserveWeaponFireTransientState);
}

void BaseWorld::configureWeaponAmmunition(int penetration) noexcept
{
    shooting_.configureAmmunition(penetration);
}

std::vector<ShotPresentationSnapshot>
BaseWorld::shotPresentationSnapshots() const
{
    return shooting_.shotPresentationSnapshots();
}

std::vector<ShotFeedbackPresentationSnapshot>
BaseWorld::shotFeedbackPresentationSnapshots() const
{
    return shooting_.shotFeedbackPresentationSnapshots();
}

const std::vector<Particle> &BaseWorld::particles() const noexcept
{
    return shooting_.particles();
}

const std::vector<HitResult> &
BaseWorld::hitResultsLastUpdate() const noexcept
{
    return shooting_.hitResultsLastUpdate();
}

bool BaseWorld::shotFiredLastUpdate() const noexcept
{
    return shooting_.shotFiredLastUpdate();
}

WeaponAccuracyProjection BaseWorld::weaponAccuracyProjection() const noexcept
{
    return shooting_.accuracyProjection();
}

Vec2 BaseWorld::weaponAimWorldPosition() const noexcept
{
    return shooting_.aimWorldPosition();
}

Vec2 BaseWorld::weaponAimDirection() const noexcept
{
    return shooting_.aimDirection();
}

Vec2 BaseWorld::normalizedShotScreenShakeOffset() const noexcept
{
    return shooting_.normalizedScreenShakeOffset();
}

Vec2 BaseWorld::worldSize() const noexcept
{
    return layout_.worldSize;
}

const std::string &BaseWorld::siteDefinitionId() const noexcept
{
    return siteDefinitionId_;
}

const ContentRect &BaseWorld::baseParcel() const noexcept
{
    return layout_.baseParcel;
}

bool BaseWorld::canAccessStash() const noexcept
{
    return playerPosition_.x >= layout_.baseParcel.position.x &&
        playerPosition_.y >= layout_.baseParcel.position.y &&
        playerPosition_.x + playerSize_.x <=
            layout_.baseParcel.position.x + layout_.baseParcel.size.x &&
        playerPosition_.y + playerSize_.y <=
            layout_.baseParcel.position.y + layout_.baseParcel.size.y;
}

std::vector<ContentRect> BaseWorld::basePlacementBlockers() const
{
    std::vector<ContentRect> result = layout_.movementBlockers;
    result.reserve(
        result.size() + facilities_.size() * 2U +
        groundBlockers_.size() + 1U);
    for (const BaseFacility &facility : facilities_)
    {
        if (!facility.active)
            continue;
        result.push_back(ContentRect{
            facility.bounds.position, facility.bounds.size});
        result.push_back(baseFacilityAccessGeometry(facility).workZone);
    }
    for (const ContentRect &bounds : groundBlockers_)
        result.push_back(bounds);
    result.push_back(ContentRect{playerPosition_, playerSize_});
    return result;
}

std::vector<ContentRect> BaseWorld::basePlacementBlockersExcluding(
    BaseFacilityKind excluded) const
{
    std::vector<ContentRect> result = layout_.movementBlockers;
    result.reserve(
        result.size() + facilities_.size() * 2U +
        groundBlockers_.size() + 1U);
    for (const BaseFacility &facility : facilities_)
    {
        if (!facility.active || facility.kind == excluded)
            continue;
        result.push_back(ContentRect{
            facility.bounds.position, facility.bounds.size});
        result.push_back(baseFacilityAccessGeometry(facility).workZone);
    }
    for (const ContentRect &bounds : groundBlockers_)
        result.push_back(bounds);
    result.push_back(ContentRect{playerPosition_, playerSize_});
    return result;
}

void BaseWorld::configureGroundBlockers(
    std::vector<ContentRect> blockers)
{
    if (baseDefense_) return;
    if (blockers == groundBlockers_)
        return;
    groundBlockers_ = std::move(blockers);
    rebuildCollisionIndex();
}

const HomeRegionLayout &BaseWorld::layout() const noexcept
{
    return layout_;
}

const HomeRegionPresentationProjection &BaseWorld::outdoorPresentation(
    ContentRect visibleWorldBounds) const
{
    if (!finiteRect(visibleWorldBounds))
    {
        presentationCache_ = {};
        presentationCacheValid_ = false;
        return presentationCache_;
    }
    const float chunkWorldSize = static_cast<float>(layout_.chunkSizeCells) *
        (layout_.worldSize.x / static_cast<float>(layout_.columns));
    const std::uint32_t chunkColumns =
        (layout_.columns + layout_.chunkSizeCells - 1U) /
        layout_.chunkSizeCells;
    const std::uint32_t chunkRows =
        (layout_.rows + layout_.chunkSizeCells - 1U) /
        layout_.chunkSizeCells;
    const std::uint32_t firstColumn = std::min(
        static_cast<std::uint32_t>(std::max(0.0F,
            visibleWorldBounds.position.x - 160.0F) / chunkWorldSize),
        chunkColumns - 1U);
    const std::uint32_t lastColumn = std::min(
        static_cast<std::uint32_t>((visibleWorldBounds.position.x +
            visibleWorldBounds.size.x + 160.0F) / chunkWorldSize),
        chunkColumns - 1U);
    const std::uint32_t firstRow = std::min(
        static_cast<std::uint32_t>(std::max(0.0F,
            visibleWorldBounds.position.y - 160.0F) / chunkWorldSize),
        chunkRows - 1U);
    const std::uint32_t lastRow = std::min(
        static_cast<std::uint32_t>((visibleWorldBounds.position.y +
            visibleWorldBounds.size.y + 160.0F) / chunkWorldSize),
        chunkRows - 1U);
    if (presentationCacheValid_ &&
        firstColumn == cachedFirstChunkColumn_ &&
        lastColumn == cachedLastChunkColumn_ &&
        firstRow == cachedFirstChunkRow_ && lastRow == cachedLastChunkRow_)
        return presentationCache_;

    cachedFirstChunkColumn_ = firstColumn;
    cachedLastChunkColumn_ = lastColumn;
    cachedFirstChunkRow_ = firstRow;
    cachedLastChunkRow_ = lastRow;
    presentationCacheValid_ = true;
    HomeRegionPresentationProjection next;
    next.cacheRevision = presentationCache_.cacheRevision + 1U;
    next.queriedChunkCount = static_cast<std::size_t>(
        lastColumn - firstColumn + 1U) * (lastRow - firstRow + 1U);
    const ContentRect query{
        {firstColumn * chunkWorldSize, firstRow * chunkWorldSize},
        {(lastColumn - firstColumn + 1U) * chunkWorldSize,
         (lastRow - firstRow + 1U) * chunkWorldSize}};
    const float cellWidth = layout_.worldSize.x /
        static_cast<float>(layout_.columns);
    const float cellHeight = layout_.worldSize.y /
        static_cast<float>(layout_.rows);
    for (const RaidTerrainSpan &span : layout_.terrainSpans)
    {
        const ContentRect bounds{{span.firstColumn * cellWidth,
                                  span.row * cellHeight},
                                 {span.length * cellWidth, cellHeight}};
        if (isCollision(Rect{bounds.position, bounds.size},
                        Rect{query.position, query.size}))
            next.terrainSpans.push_back(span);
    }
    for (const RaidOutdoorRoadCell &road : layout_.roadCells)
    {
        const ContentRect bounds{{road.column * cellWidth,
                                  road.row * cellHeight},
                                 {cellWidth, cellHeight}};
        if (isCollision(Rect{bounds.position, bounds.size},
                        Rect{query.position, query.size}))
            next.roadCells.push_back(road);
    }
    for (const RaidOutdoorPropSnapshot &prop : layout_.props)
        if (isCollision(Rect{prop.bounds.position, prop.bounds.size},
                        Rect{query.position, query.size}))
            next.props.push_back(prop);
    for (const HomeRegionDistrictSnapshot &district : layout_.districts)
        if (isCollision(Rect{district.bounds.position, district.bounds.size},
                        Rect{query.position, query.size}))
            next.districts.push_back(district);
    presentationCache_ = std::move(next);
    return presentationCache_;
}

std::optional<BaseFacilityKind> BaseWorld::interactableFacility() const noexcept
{
    const Vec2 center{
        playerPosition_.x + playerSize_.x / 2.0F,
        playerPosition_.y + playerSize_.y / 2.0F};
    for (const BaseFacility &facility : facilities_)
    {
        if (!facility.active)
            continue;
        if (pointInside(
                center,
                baseFacilityAccessGeometry(facility).interactionZone))
        {
            return facility.kind;
        }
    }
    return std::nullopt;
}

void BaseWorld::resetAtRaidGate() noexcept
{
    clearSpatialCombatState();
    const BaseFacility &gate = facilities_.back();
    playerPosition_ = Vec2{
        gate.bounds.position.x + gate.bounds.size.x * 0.5F -
            playerSize_.x * 0.5F,
        gate.bounds.position.y + gate.bounds.size.y + 28.0F};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

void BaseWorld::resetAtMedicalPoint() noexcept
{
    clearSpatialCombatState();
    const Vec2 origin = layout_.baseParcel.position;
    playerPosition_ = Vec2{origin.x + 780.0F, origin.y + 900.0F};
    if (surveying()) playerPosition_ = {origin.x + 140, origin.y + 150};
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
    shooting_.reanchor(
        {playerPosition_.x + playerSize_.x * 0.5F,
         playerPosition_.y + playerSize_.y * 0.5F},
        playerFacingDirection_,
        layout_.worldSize);
}

const char *baseFacilityName(BaseFacilityKind kind) noexcept
{
    switch (kind)
    {
    case BaseFacilityKind::Storage:
        return "STORAGE & LOADOUT";
    case BaseFacilityKind::Supply:
        return "SUPPLY & RECOVERY";
    case BaseFacilityKind::Allocation:
        return "ALLOCATION & NEEDS";
    case BaseFacilityKind::Medical:
        return "MEDICAL SERVICE";
    case BaseFacilityKind::Dormitory:
        return "DORMITORY & REST";
    case BaseFacilityKind::KitchenWater:
        return "KITCHEN & WATER";
    case BaseFacilityKind::Workshop:
        return "WORKSHOP & PRODUCTION";
    case BaseFacilityKind::RaidGate:
        return "RAID DEPLOYMENT";
    }
    return "UNKNOWN";
}
