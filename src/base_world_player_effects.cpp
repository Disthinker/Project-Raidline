#include "base_world.h"
#include "grenade_damage.h"
#include "raid_space_query.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
Vec2 center(const Enemy &enemy) {
  return {enemy.position().x + enemy.size().x / 2, enemy.position().y + enemy.size().y / 2};
}
bool same(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
} // namespace

EnemyLifecycle &BaseWorld::activeActors() noexcept {
  return baseDefense_ ? static_cast<EnemyLifecycle &>(baseDefense_->enemies_)
                      : static_cast<EnemyLifecycle &>(perimeterEnemies_);
}
const EnemyLifecycle &BaseWorld::activeActors() const noexcept {
  return baseDefense_ ? static_cast<const EnemyLifecycle &>(baseDefense_->enemies_)
                      : static_cast<const EnemyLifecycle &>(perimeterEnemies_);
}

const RaidSpaceBlockerIndex &BaseWorld::playerEffectsGeometry() const {
  const auto dynamicRevision = baseDefense_ ? baseDefense_->fortifications_.geometryRevision()
                                           : dailyFortifications_.geometryRevision();
  const auto key = placementGeometryRevision_ * 1099511628211ULL ^ dynamicRevision ^
                   (baseDefense_ ? (1ULL << 63) : 0);
  if (!playerEffectsIndex_ || playerEffectsGeometryKey_ != key) {
    std::vector<BallisticBlocker> geometry = movementBlockers_;
    BallisticBlockerId id = std::numeric_limits<BallisticBlockerId>::max();
    for (const auto &fortification : fortifications())
      if (fortification.durability) geometry.push_back({id--, fortification.footprint});
    playerEffectsIndex_ = RaidSpaceBlockerIndex::build(layout_.worldSize, geometry);
    playerEffectsGeometryKey_ = key;
  }
  return *playerEffectsIndex_;
}

void BaseWorld::applyPlayerControl(float seconds) noexcept {
  if (std::isfinite(seconds) && seconds > 0) {
    playerControlRemaining_ = std::max(playerControlRemaining_, seconds);
    playerIsMoving_ = false;
    playerMovementAnimator_.reset();
  }
}
void BaseWorld::applyPlayerImpactSlow(float seconds) noexcept {
  if (std::isfinite(seconds) && seconds > 0)
    playerImpactSlowRemaining_ = std::max(playerImpactSlowRemaining_, seconds);
}
bool BaseWorld::restorePlayerBody(Vec2 position, Vec2 facing, float control, float slow,
                                 bool preserveWeaponState) noexcept {
  const float length = std::hypot(facing.x, facing.y);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || position.x < 0 || position.y < 0 ||
      position.x + playerSize_.x > layout_.worldSize.x || position.y + playerSize_.y > layout_.worldSize.y ||
      !std::isfinite(length) || length < .0001F || !std::isfinite(control) || !std::isfinite(slow) || control < 0 || slow < 0)
    return false;
  playerPosition_ = position; playerFacingDirection_ = {facing.x / length, facing.y / length};
  observationDirection_ = playerFacingDirection_;
  playerControlRemaining_ = control; playerImpactSlowRemaining_ = slow;
  playerIsMoving_ = false;
  playerMovementAnimator_.reset();
  if (!preserveWeaponState)
    shooting_.reanchor({position.x + playerSize_.x / 2, position.y + playerSize_.y / 2},
                      playerFacingDirection_, layout_.worldSize);
  observationCache_ = {};
  return true;
}

void BaseWorld::setDaylight(bool day) noexcept { daylight_ = day; }
void BaseWorld::setWeaponLight(std::uint64_t device, std::uint64_t battery,
                              float angle, float distance, float radius) noexcept {
  const bool active = device && battery && std::isfinite(angle) && angle > 0 && angle <= 180 &&
                      std::isfinite(distance) && distance > 0 && distance <= 1500;
  lightDevice_ = active ? device : 0; lightBattery_ = active ? battery : 0;
  lightAngle_ = active ? angle : 0; lightDistance_ = active ? distance : 0;
  const float background = active && std::isfinite(radius) ? std::clamp(radius, 0.0F, 200.0F) : 0;
  if (background != lightBackgroundRadius_) observationCache_ = {};
  lightBackgroundRadius_ = background;
}
RaidObservationKey BaseWorld::observationKey() const {
  static_cast<void>(playerEffectsGeometry());
  const auto environment = daylight_ ? RaidLightEnvironment::DayOutdoor : RaidLightEnvironment::NightOutdoor;
  const auto parameters = raidOpticSightParameters(raidSightParameters(environment), environment, activeWeaponOptic());
  return {spaceId_, {playerPosition_.x + playerSize_.x / 2, playerPosition_.y + playerSize_.y / 2},
          observationDirection_, parameters.angleDegrees, parameters.maximumDistance, environment,
          playerEffectsGeometryKey_, lightDevice_, lightBattery_, lightAngle_, lightDistance_};
}
const RaidVisibilityProjection &BaseWorld::observation() const {
  const auto key = observationKey();
  if (!observationCache_.valid || !observationCache_.hasSameParameters(key)) {
    observationCache_ = buildRaidSpaceConeVisibility(playerEffectsGeometry(), key);
    observationBounds_ = RaidVisibilityBoundsQuery{observationCache_};
    enemyObservationCache_.clear();
    lightBackgroundCache_ = {};
    if (lightBackgroundRadius_ > 0)
      for (std::size_t side = 0; side < 2; ++side) {
        auto near = key;
        near.direction = {side ? -1.0F : 1.0F, 0};
        near.angleDegrees = 180; near.maximumDistance = lightBackgroundRadius_;
        near.lightAssetId = near.lightBatteryId = 0;
        near.lightAngleDegrees = near.lightDistance = 0;
        lightBackgroundCache_[side] = buildRaidSpaceConeVisibility(playerEffectsGeometry(), near);
      }
  }
  return observationCache_;
}
const std::array<RaidVisibilityProjection, 2> &BaseWorld::weaponLightBackgroundObservations() const {
  static_cast<void>(observation()); return lightBackgroundCache_;
}
bool BaseWorld::boundsObservable(Rect bounds) const {
  static_cast<void>(observation()); return observationBounds_.boundsVisible(bounds);
}
bool BaseWorld::enemyObservable(CombatTargetId id) const {
  const auto *enemy = activeActors().find(id);
  return enemy && boundsObservable(enemy->bounds());
}

std::optional<NoiseDecoyLanding> BaseWorld::previewNoiseDecoy(Vec2 aim, const NoiseDecoyUseDefinition &use) const {
  const Vec2 origin{playerPosition_.x + playerSize_.x / 2, playerPosition_.y + playerSize_.y / 2};
  if (playerControlled() || noiseDecoy_.flight() || grenade_.active() || melee_.recoverySeconds() > 0 ||
      queryHomeRegionSafetyZone(origin, layout_.baseParcel) == HomeRegionSafetyZone::SafeCore) return {};
  auto landing = queryNoiseDecoyLanding(origin, aim, spaceId_, use, playerEffectsGeometry());
  if (landing && queryHomeRegionSafetyZone(landing->point, layout_.baseParcel) == HomeRegionSafetyZone::SafeCore) return {};
  return landing;
}
std::optional<GrenadeLanding> BaseWorld::previewGrenade(Vec2 aim, const GrenadeUseDefinition &use) const {
  const Vec2 origin{playerPosition_.x + playerSize_.x / 2, playerPosition_.y + playerSize_.y / 2};
  if (playerControlled() || noiseDecoy_.flight() || grenade_.active() || melee_.recoverySeconds() > 0 ||
      queryHomeRegionSafetyZone(origin, layout_.baseParcel) == HomeRegionSafetyZone::SafeCore) return {};
  auto landing = queryGrenadeLanding(origin, aim, spaceId_, use, playerEffectsGeometry());
  if (landing && queryHomeRegionSafetyZone(landing->point, layout_.baseParcel) == HomeRegionSafetyZone::SafeCore) return {};
  return landing;
}

void BaseWorld::advanceThrowables(float seconds) {
  grenadeExplodedLastUpdate_ = false;
  grenadeFlashSeconds_ = std::max(0.0F, grenadeFlashSeconds_ - std::max(0.0F, seconds));
  grenade_.advance(spaceId_, seconds);
  noiseDecoy_.advance(spaceId_, seconds);
  const bool pulse = noiseDecoy_.pulsesLastUpdate() > 0;
  const bool bound = !noiseDecoy_.investigations().empty();
  if (!pulse && !bound) return;
  std::vector<NoiseDecoyListener> listeners;
  std::vector<CombatTargetId> alive;
  for (const auto &enemy : activeActors()) if (!enemy.isDead()) {
    if (bound) alive.push_back(enemy.combatTargetId());
    if (!pulse) continue;
    Vec2 home = center(enemy);
    if (baseDefense_) {
      if (const auto *wave = baseDefense_->waveFor(enemy.combatTargetId())) home = wave->target;
    } else home = perimeterEnemies_.state(enemy.combatTargetId());
    listeners.push_back({enemy.combatTargetId(), center(enemy), home,
        enemy.awarenessState() != EnemyAwarenessState::Alerted && enemy.attackPhase() == EnemyAttackPhase::Idle});
  }
  if (bound) noiseDecoy_.prune(spaceId_, alive);
  if (pulse) noiseDecoy_.hearPulses(listeners, playerEffectsGeometry());
}
void BaseWorld::emitNoise(Vec2 source, float radius) {
  for (auto &enemy : activeActors())
    if (!enemy.isDead() && std::hypot(center(enemy).x - source.x, center(enemy).y - source.y) <= radius) {
      noiseDecoy_.forget(enemy.combatTargetId());
      enemy.investigateSound(source);
    }
}

std::optional<GrenadeExplosionPlan> BaseWorld::prepareGrenadeExplosion(int playerHealth) const {
  if (!grenade_.pendingExplosion() || grenade_.pendingExplosion()->landing.space != spaceId_ || playerHealth <= 0) return {};
  GrenadeExplosionPlan plan;
  plan.explosion = *grenade_.pendingExplosion();
  plan.playerPosition = playerPosition_; plan.playerHealth = playerHealth;
  const auto &landing = plan.explosion.landing;
  const auto &geometry = playerEffectsGeometry();
  const auto damageAt = [&](Vec2 target) {
    const int damage = grenadeDamageAtDistance(std::hypot(target.x - landing.point.x, target.y - landing.point.y), landing.use);
    return damage && geometry.hasLineOfSight(landing.point, target) ? damage : 0;
  };
  for (const auto &enemy : activeActors()) if (!enemy.isDead()) {
    const int raw = damageAt(center(enemy)); if (!raw) continue;
    const auto resolved = resolveCombatDamage({raw, HitRegion::Torso, landing.use.penetration, landing.use.armorDamage, false,
        enemy.torsoArmor() ? std::optional{enemy.torsoArmor()->protection()} : std::nullopt});
    if (!resolved.resolved()) return {};
    plan.enemies.push_back({enemy.combatTargetId(), enemy.position(), enemy.health(), enemy.torsoArmor(), resolved});
  }
  // Existing Base core protection applies to blast damage as to contact.
  const Vec2 playerCenter{playerPosition_.x + playerSize_.x / 2, playerPosition_.y + playerSize_.y / 2};
  const bool exposed = baseDefense_ ? baseDefense_->playerExposed(playerCenter)
      : playerSafetyZone() == HomeRegionSafetyZone::Perimeter;
  if (const int raw = exposed ? damageAt(playerCenter) : 0; raw > 0)
    plan.playerDamage = CombatDamageCommand{raw, HitRegion::Torso, landing.use.penetration, landing.use.armorDamage, false, {}};
  return plan;
}
bool BaseWorld::validatePreparedGrenadeExplosion(const GrenadeExplosionPlan &plan) const {
  if (!grenade_.pendingExplosion() || grenade_.pendingExplosion()->id != plan.explosion.id ||
      plan.explosion.landing.space != spaceId_ || !same(playerPosition_, plan.playerPosition)) return false;
  for (const auto &hit : plan.enemies) {
    const auto *enemy = activeActors().find(hit.id);
    if (!enemy || !same(enemy->position(), hit.position) || enemy->health() != hit.health || enemy->torsoArmor() != hit.armor) return false;
  }
  return true;
}
bool BaseWorld::commitPreparedGrenadeExplosion(const GrenadeExplosionPlan &plan) {
  if (!validatePreparedGrenadeExplosion(plan)) return false;
  auto &actors = activeActors();
  for (const auto &hit : plan.enemies) {
    auto found = std::find_if(actors.begin(), actors.end(), [&](const Enemy &enemy) { return enemy.combatTargetId() == hit.id; });
    found->applyArmorLoss(hit.damage.armorDurabilityLoss);
    static_cast<void>(found->takeDamage(hit.damage.damageApplied, EnemyImpactReaction::None));
    if (found->isDead()) baseEnemyDeaths_.push_back({found->combatTargetId(), center(*found), found->size()});
  }
  const auto removals = actors.removeDead();
  if (baseDefense_) {
    for (const auto &fact : removals) baseDefense_->state_.killedIds.push_back(fact.id);
    baseDefense_->state_.reservedAttackers.clear();
    baseDefense_->synchronizeActorCheckpoints();
  } else perimeterRemovals_.insert(perimeterRemovals_.end(), removals.begin(), removals.end());
  emitNoise(plan.explosion.landing.point, plan.explosion.landing.use.noiseRadius);
  lastGrenadeExplosion_ = plan.explosion; grenadeFlashSeconds_ = .25F; grenadeExplodedLastUpdate_ = true;
  grenade_.acknowledgeExplosion();
  return true;
}
