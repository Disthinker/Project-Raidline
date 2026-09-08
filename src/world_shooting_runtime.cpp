#include "world_shooting_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
float distanceSquared(Vec2 first, Vec2 second) noexcept
{
    const float dx = first.x - second.x;
    const float dy = first.y - second.y;
    return dx * dx + dy * dy;
}

Vec2 enemyCenter(const Enemy &enemy) noexcept
{
    const Vec2 position = enemy.position();
    const Vec2 size = enemy.size();
    return {position.x + size.x * 0.5F, position.y + size.y * 0.5F};
}

std::optional<ShotAimIntent> aimIntentAt(
    Vec2 aimPoint,
    Vec2 shotOrigin,
    const std::vector<Enemy> &targets) noexcept
{
    const Enemy *selected{};
    std::optional<HitRegion> selectedRegion;
    float selectedDistanceSquared = std::numeric_limits<float>::infinity();
    for (const Enemy &target : targets)
    {
        if (target.isDead() ||
            target.combatTargetId() == kInvalidCombatTargetId)
        {
            continue;
        }
        const std::optional<HitRegion> region =
            hitRegionAtPoint(target.bounds(), aimPoint);
        if (!region.has_value())
        {
            continue;
        }
        const float candidateDistance = distanceSquared(
            shotOrigin, enemyCenter(target));
        if (candidateDistance >= selectedDistanceSquared)
        {
            continue;
        }
        selected = &target;
        selectedRegion = region;
        selectedDistanceSquared = candidateDistance;
    }
    if (selected == nullptr || !selectedRegion.has_value())
    {
        return std::nullopt;
    }
    return ShotAimIntent{
        selected->combatTargetId(), *selectedRegion, false};
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
}

WorldShootingRuntime::WorldShootingRuntime()
    : particleSystem_{0xC0FFEEu, ParticleBurstConfig{}}
{
}

WorldShootingCheckpoint WorldShootingRuntime::checkpoint() const
{
    WorldShootingCheckpoint s;
    s.initialized = true; s.nextShotId = nextShotId_;
    const auto &f = weaponFire_.config_;
    s.fireConfig = {f.shotInterval, f.minimumSpreadDegrees, f.maximumSpreadDegrees,
        f.spreadPerShotDegrees, f.recoveryDelay, f.spreadRecoveryDegreesPerSecond,
        f.aimDownSightsAccuracyMultiplier, f.aimDownSightsStabilityMultiplier,
        f.movingSpreadFraction, f.sprintingSpreadFraction,
        f.reticleMotionSpreadDegreesPerSecond, f.reticleMotionSoftThreshold,
        f.reticleMotionFullSpeed, f.nearDistanceSpreadScale,
        f.distanceBloomAtEffectiveRange, f.overEffectiveRangeSpreadMultiplier};
    s.fireState = {weaponFire_.cooldownRemaining_, weaponFire_.spreadDegrees_,
        weaponFire_.contextualMinimumSpreadDegrees_, weaponFire_.contextualMaximumSpreadDegrees_,
        weaponFire_.recoveryDelayRemaining_, weaponFire_.movementBloomFraction_,
        weaponFire_.reticleMotionBloomFraction_, weaponFire_.shotBloomFraction_,
        weaponFire_.distanceBloomFraction_, weaponFire_.combinedBloomFraction_,
        weaponFire_.lastAimDownSightsProgress_, weaponFire_.lastDistanceSpreadFactor_,
        weaponFire_.lastOverEffectiveRangeFactor_};
    s.spreadSeed = f.spreadSeed; s.spreadRandomState = weaponFire_.random_.state_;
    s.spreadRandomIncrement = weaponFire_.random_.increment_;
    s.burstShotCount = weaponFire_.burstShotCount_;
    const auto &a = weaponAim_.config_;
    s.aimConfig = {a.maximumReticleSpeed, a.controlAcceleration, a.recoilInitialSpeed,
        a.recoilDeceleration, a.recoilLateralRatio, a.recoilBendDurationSeconds,
        a.aimDownSightsDurationSeconds, a.effectiveRange, a.maximumRange};
    s.aimVectors = {checkpointPoint(weaponAim_.currentWorldPosition_),
        checkpointPoint(weaponAim_.targetWorldPosition_), checkpointPoint(weaponAim_.inputWorldPosition_),
        checkpointPoint(weaponAim_.shootingOrigin_), checkpointPoint(weaponAim_.worldSize_),
        checkpointPoint(weaponAim_.controlVelocity_), checkpointPoint(weaponAim_.recoilVelocity_),
        checkpointPoint(weaponAim_.recoilTargetDirection_), checkpointPoint(weaponAim_.lastDirection_)};
    s.aimDownSightsProgress = weaponAim_.aimDownSightsProgress_;
    s.recoilBendRemaining = weaponAim_.recoilBendRemainingSeconds_;
    s.aimControlMode = static_cast<std::uint32_t>(weaponAim_.controlMode_);
    s.aimInitialized = weaponAim_.initialized_; s.recoilSeed = a.recoilSeed;
    s.recoilRandomState = weaponAim_.recoilRandom_.state_;
    s.recoilRandomIncrement = weaponAim_.recoilRandom_.increment_;
    s.weaponDamage = weaponBaseDamage_; s.weaponPenetration = weaponPenetration_;
    s.maximumRange = weaponMaximumRange_; s.logicalSpeed = weaponLogicalBallisticSpeed_;
    s.tracerStyle = static_cast<std::uint32_t>(weaponTracerStyle_);
    s.tracerLength = weaponTracerLength_; s.tracerOpacity = weaponTracerOpacity_;
    s.tracerLifetime = weaponTracerLifetimeSeconds_;
    s.flights.reserve(logicalBallistics_.size());
    for (const auto &flight : logicalBallistics_) {
        LogicalFlightCheckpoint v;
        v.id = flight.shotId_; v.origin = checkpointPoint(flight.origin_);
        v.position = checkpointPoint(flight.currentPosition_); v.direction = checkpointPoint(flight.direction_);
        v.impact = checkpointPoint(flight.impactPosition_); v.speed = flight.speed_;
        v.extent = flight.collisionExtent_; v.travelled = flight.distanceTravelled_;
        v.maximumDistance = flight.maximumDistance_; v.damage = flight.damage_; v.penetration = flight.penetration_;
        if (flight.aimIntent_) {
            v.aimedTarget = flight.aimIntent_->targetId;
            v.aimedRegion = static_cast<std::uint32_t>(flight.aimIntent_->region);
            v.weakPoint = flight.aimIntent_->weakPoint;
        }
        v.tracerStyle = static_cast<std::uint32_t>(flight.tracerStyle_);
        v.tracerLength = flight.tracerLength_; v.tracerOpacity = flight.tracerOpacity_;
        v.tracerLifetime = flight.tracerLifetimeSeconds_;
        s.flights.push_back(v);
    }
    return s;
}

bool WorldShootingRuntime::restoreCheckpoint(const WorldShootingCheckpoint &s)
{
    if (!validateWorldShootingCheckpoint(s)) return false;
    if (!s.initialized) return true;
    WorldShootingRuntime candidate;
    const auto &f = s.fireConfig;
    candidate.weaponFire_ = WeaponFireState{WeaponFireConfig{
        f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9],
        f[10], f[11], f[12], f[13], f[14], f[15], s.spreadSeed}};
    auto &fire = candidate.weaponFire_;
    const auto &v = s.fireState;
    fire.cooldownRemaining_ = v[0]; fire.spreadDegrees_ = v[1];
    fire.contextualMinimumSpreadDegrees_ = v[2]; fire.contextualMaximumSpreadDegrees_ = v[3];
    fire.recoveryDelayRemaining_ = v[4]; fire.movementBloomFraction_ = v[5];
    fire.reticleMotionBloomFraction_ = v[6]; fire.shotBloomFraction_ = v[7];
    fire.distanceBloomFraction_ = v[8]; fire.combinedBloomFraction_ = v[9];
    fire.lastAimDownSightsProgress_ = v[10]; fire.lastDistanceSpreadFactor_ = v[11];
    fire.lastOverEffectiveRangeFactor_ = v[12]; fire.burstShotCount_ = s.burstShotCount;
    fire.random_.state_ = s.spreadRandomState; fire.random_.increment_ = s.spreadRandomIncrement;
    const auto &a = s.aimConfig;
    candidate.weaponAim_ = WeaponAimState{WeaponAimConfig{
        a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], s.recoilSeed}};
    auto &aim = candidate.weaponAim_;
    aim.currentWorldPosition_ = runtimePoint(s.aimVectors[0]);
    aim.targetWorldPosition_ = runtimePoint(s.aimVectors[1]);
    aim.inputWorldPosition_ = runtimePoint(s.aimVectors[2]);
    aim.shootingOrigin_ = runtimePoint(s.aimVectors[3]);
    aim.worldSize_ = runtimePoint(s.aimVectors[4]);
    aim.controlVelocity_ = runtimePoint(s.aimVectors[5]);
    aim.recoilVelocity_ = runtimePoint(s.aimVectors[6]);
    aim.recoilTargetDirection_ = runtimePoint(s.aimVectors[7]);
    aim.lastDirection_ = runtimePoint(s.aimVectors[8]);
    aim.aimDownSightsProgress_ = s.aimDownSightsProgress;
    aim.recoilBendRemainingSeconds_ = s.recoilBendRemaining;
    aim.controlMode_ = static_cast<AimControlMode>(s.aimControlMode);
    aim.initialized_ = s.aimInitialized; aim.recoilRandom_.state_ = s.recoilRandomState;
    aim.recoilRandom_.increment_ = s.recoilRandomIncrement;
    candidate.nextShotId_ = s.nextShotId; candidate.weaponBaseDamage_ = s.weaponDamage;
    candidate.weaponPenetration_ = s.weaponPenetration; candidate.weaponMaximumRange_ = s.maximumRange;
    candidate.weaponLogicalBallisticSpeed_ = s.logicalSpeed;
    candidate.weaponTracerStyle_ = static_cast<TracerStyle>(s.tracerStyle);
    candidate.weaponTracerLength_ = s.tracerLength; candidate.weaponTracerOpacity_ = s.tracerOpacity;
    candidate.weaponTracerLifetimeSeconds_ = s.tracerLifetime;
    for (const auto &vflight : s.flights) {
        std::optional<ShotAimIntent> intent;
        if (vflight.aimedTarget) intent = ShotAimIntent{vflight.aimedTarget,
            static_cast<HitRegion>(vflight.aimedRegion), vflight.weakPoint};
        const auto resolution = resolveShotCommand(ShotCommand{vflight.id,
            runtimePoint(vflight.origin), runtimePoint(vflight.direction), vflight.speed,
            vflight.extent, vflight.damage, vflight.maximumDistance, intent, vflight.penetration});
        if (!resolution.accepted()) return false;
        LogicalBallisticFlight flight{resolution, static_cast<TracerStyle>(vflight.tracerStyle),
            vflight.tracerLength, vflight.tracerOpacity, vflight.tracerLifetime};
        // Validation above verifies direction and geometry. Preserve the
        // original float bits: resolving a saved direction again can normalize
        // it by another ULP and alter subsequent swept collision segments.
        flight.origin_ = runtimePoint(vflight.origin);
        flight.direction_ = runtimePoint(vflight.direction);
        flight.speed_ = vflight.speed;
        flight.maximumDistance_ = vflight.maximumDistance;
        flight.currentPosition_ = runtimePoint(vflight.position);
        flight.impactPosition_ = runtimePoint(vflight.impact);
        flight.distanceTravelled_ = vflight.travelled;
        candidate.logicalBallistics_.push_back(flight);
    }
    *this = std::move(candidate);
    return true;
}

void WorldShootingRuntime::beginFrame(float deltaTime) noexcept
{
    hitResultsLastUpdate_.clear();
    shotFiredLastUpdate_ = false;
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0F)
    {
        return;
    }
    shotFeedbackPresentation_.update(deltaTime);
    particleSystem_.update(deltaTime);
    for (TracerPresentationSegment &tracer : tracerPresentations_)
    {
        tracer.ageSeconds += deltaTime;
        tracer.remainingSeconds = std::max(
            0.0F, tracer.remainingSeconds - deltaTime);
    }
    std::erase_if(
        tracerPresentations_,
        [](const TracerPresentationSegment &tracer)
        {
            return tracer.remainingSeconds <= 0.0F;
        });
}

void WorldShootingRuntime::updateAim(
    const GameplayInput &input,
    Vec2 shooterCenter,
    Vec2 fallbackDirection,
    Vec2 worldSize,
    float deltaTime,
    bool allowPointerInput) noexcept
{
    Vec2 desired{
        shooterCenter.x + fallbackDirection.x * 400.0F,
        shooterCenter.y + fallbackDirection.y * 400.0F};
    if (allowPointerInput && input.aimWorldPosition.has_value())
    {
        desired = *input.aimWorldPosition;
    }
    weaponAim_.update(
        desired,
        shooterCenter,
        worldSize,
        input.aimDownSights,
        deltaTime,
        input.aimMotionDelta,
        AimControlMode::Direct);
    if (allowPointerInput && input.aimWorldBounds.has_value())
    {
        weaponAim_.constrainToBounds(*input.aimWorldBounds);
    }
}

WorldShootingAdvance WorldShootingRuntime::advanceShots(
    const GameplayInput &input,
    float deltaTime,
    Vec2 shooterCenter,
    float shooterExtent,
    bool shooterMoving,
    bool controlsSuppressed,
    Vec2 worldSize,
    EnemyLifecycle &targets,
    const std::vector<BallisticBlocker> &blockers)
{
    const std::optional<ShotSpec> shot = weaponFire_.update(
        !controlsSuppressed && !input.sprint &&
            (input.firePressed || input.fireJustPressed),
        weaponAim_.actualDirection(),
        deltaTime,
        WeaponFireContext{
            .moving = shooterMoving,
            .sprinting = input.sprint && shooterMoving,
            .aimDownSightsProgress = weaponAim_.aimDownSightsProgress(),
            .aimDistance = weaponAim_.aimDistance(),
            .distanceSpreadFactor = weaponAim_.distanceSpreadFactor(),
            .overEffectiveRangeFactor =
                weaponAim_.overEffectiveRangeFactor(),
            .reticleControlSpeed = weaponAim_.reticleControlSpeed(),
            .forceMaximumSpread = input.forceMaximumWeaponSpread});

    if (shot.has_value())
    {
        shotFiredLastUpdate_ = true;
        const float muzzleDistance = shooterExtent * 0.5F +
            kShotExtent * 0.5F;
        const Vec2 shotOrigin{
            shooterCenter.x + shot->direction.x * muzzleDistance,
            shooterCenter.y + shot->direction.y * muzzleDistance};
        const float maximumDistance = std::max(
            0.001F,
            std::min(
                distanceToWorldBoundary(
                    shotOrigin, shot->direction, worldSize),
                weaponMaximumRange_));
        const ShotResolution resolution = resolveShotCommand(ShotCommand{
            nextShotId_,
            shotOrigin,
            shot->direction,
            weaponLogicalBallisticSpeed_,
            kShotExtent,
            std::max(
                1,
                static_cast<int>(std::lround(
                    static_cast<float>(weaponBaseDamage_) *
                    weaponAim_.damageMultiplier()))),
            maximumDistance,
            aimIntentAt(
                weaponAim_.actualWorldPosition(), shotOrigin, targets),
            weaponPenetration_});
        if (!resolution.accepted())
        {
            throw std::logic_error{"World shooting produced an invalid shot"};
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
            throw std::logic_error{"World shooting feedback rejected shot"};
        }
        weaponAim_.applyShotRecoil(shotOrigin);
        ++nextShotId_;
    }

    std::vector<ShotCollisionCandidate> candidates;
    candidates.reserve(logicalBallistics_.size());
    for (LogicalBallisticFlight &flight : logicalBallistics_)
    {
        const LogicalBallisticAdvance advance = flight.advance(deltaTime);
        const float travelled = std::hypot(
            advance.end.x - advance.start.x,
            advance.end.y - advance.start.y);
        if (flight.tracerStyle() != TracerStyle::None &&
            travelled > 0.0001F)
        {
            const float visibleLength = std::min(
                flight.tracerLength(), flight.distanceTravelled());
            TracerPresentationSegment segment{
                flight.shotId(),
                {advance.end.x - flight.direction().x * visibleLength,
                 advance.end.y - flight.direction().y * visibleLength},
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
        candidates.push_back(ShotCollisionCandidate{
            flight.shotId(),
            advance.start,
            advance.end,
            flight.collisionExtent(),
            flight.damage(),
            flight.aimIntent(),
            flight.penetration()});
    }

    HitResolutionResult resolved = resolveShotHits(
        candidates, targets, blockers);
    for (const HitResult &hit : resolved.hits)
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
            flight->tracerLength(), hitDistance);
        tracer->end = hit.position;
        tracer->start = {
            hit.position.x - flight->direction().x * visibleLength,
            hit.position.y - flight->direction().y * visibleLength};
    }
    std::erase_if(
        logicalBallistics_,
        [&](const LogicalBallisticFlight &flight)
        {
            return std::find(
                       resolved.consumedShotIds.begin(),
                       resolved.consumedShotIds.end(),
                       flight.shotId()) != resolved.consumedShotIds.end();
        });
    for (const LogicalBallisticFlight &flight : logicalBallistics_)
    {
        if (flight.reachedImpact())
        {
            resolved.hits.push_back(HitResult{
                flight.shotId(),
                HitTargetKind::Ground,
                flight.impactPosition(),
                0,
                false,
                HitRegion::Torso,
                HitSemantic::Normal});
        }
    }
    std::erase_if(
        logicalBallistics_,
        [](const LogicalBallisticFlight &flight)
        {
            return flight.reachedImpact();
        });
    for (const HitResult &hit : resolved.hits)
    {
        particleSystem_.emitImpact(hit.position);
    }
    hitResultsLastUpdate_ = std::move(resolved.hits);
    return WorldShootingAdvance{
        std::move(resolved.removals), resolved.enemiesKilled};
}

void WorldShootingRuntime::configureWeapon(
    const WeaponUseDefinition &definition,
    const WeaponHandlingParameters &handling,
    bool preserveTransientState)
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
    if (preserveTransientState)
    {
        weaponFire_.reconfigure(fireConfig);
    }
    else
    {
        weaponFire_ = WeaponFireState{fireConfig};
    }
    weaponAim_.reconfigure(aimConfig);
    weaponBaseDamage_ = definition.baseDamage;
    weaponMaximumRange_ = definition.maximumRange;
    weaponLogicalBallisticSpeed_ = definition.logicalBallisticSpeed;
    weaponTracerStyle_ = TracerStyle::Weak;
    weaponTracerLength_ = handling.weakTracerLength;
    weaponTracerOpacity_ = handling.weakTracerOpacity;
    weaponTracerLifetimeSeconds_ = handling.weakTracerLifetimeSeconds;
}

void WorldShootingRuntime::configureWeapon(
    const WeaponUseDefinition &definition)
{
    configureWeapon(definition, deriveWeaponHandling(definition), false);
}

void WorldShootingRuntime::configureAmmunition(int penetration) noexcept
{
    weaponPenetration_ = std::max(0, penetration);
}

void WorldShootingRuntime::reanchor(
    Vec2 shooterCenter,
    Vec2 direction,
    Vec2 worldSize) noexcept
{
    weaponAim_.reanchor(shooterCenter, direction, worldSize);
}

void WorldShootingRuntime::clearSpatialTransientPresentation() noexcept
{
    logicalBallistics_.clear();
    tracerPresentations_.clear();
    particleSystem_.clear();
    shotFeedbackPresentation_.reset();
    hitResultsLastUpdate_.clear();
}

const std::vector<LogicalBallisticFlight> &
WorldShootingRuntime::logicalBallistics() const noexcept
{
    return logicalBallistics_;
}

std::vector<ShotPresentationSnapshot>
WorldShootingRuntime::shotPresentationSnapshots() const
{
    std::vector<ShotPresentationSnapshot> snapshots;
    snapshots.reserve(tracerPresentations_.size());
    constexpr float kTau{6.28318530718F};
    constexpr float kFlickerFrequencyHz{24.0F};
    for (const TracerPresentationSegment &tracer : tracerPresentations_)
    {
        const float lifetimeRatio = tracer.lifetimeSeconds > 0.0F
            ? std::clamp(
                  tracer.remainingSeconds / tracer.lifetimeSeconds,
                  0.0F,
                  1.0F)
            : 0.0F;
        const float flicker = 0.55F + 0.45F *
            (0.5F + 0.5F * std::sin(
                tracer.ageSeconds * kTau * kFlickerFrequencyHz +
                static_cast<float>(tracer.shotId % 17U) * 1.618F));
        snapshots.push_back(ShotPresentationSnapshot{
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
WorldShootingRuntime::shotFeedbackPresentationSnapshots() const
{
    return shotFeedbackPresentation_.snapshots();
}

Vec2 WorldShootingRuntime::normalizedScreenShakeOffset() const noexcept
{
    return shotFeedbackPresentation_.normalizedScreenShakeOffset();
}

const std::vector<Particle> &WorldShootingRuntime::particles() const noexcept
{
    return particleSystem_.particles();
}

const std::vector<HitResult> &
WorldShootingRuntime::hitResultsLastUpdate() const noexcept
{
    return hitResultsLastUpdate_;
}

bool WorldShootingRuntime::shotFiredLastUpdate() const noexcept
{
    return shotFiredLastUpdate_;
}

float WorldShootingRuntime::spreadDegrees() const noexcept
{
    return weaponFire_.spreadDegrees();
}

float WorldShootingRuntime::visualRecoilPixels() const noexcept
{
    const Vec2 velocity = weaponAim_.recoilPresentationVelocity();
    return std::min(
        18.0F,
        std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y) *
            0.02F);
}

WeaponAccuracyProjection
WorldShootingRuntime::accuracyProjection() const noexcept
{
    const float distance = weaponAim_.aimDistance();
    const float worldRadius = weaponFire_.spreadRadiusAtDistance(distance);
    return WeaponAccuracyProjection{
        weaponAim_.actualWorldPosition(),
        distance,
        weaponFire_.spreadDegreesAtDistance(distance),
        weaponFire_.contextualMinimumSpreadDegrees(),
        weaponFire_.contextualMaximumSpreadDegrees(),
        worldRadius,
        10.0F + worldRadius,
        weaponAim_.beyondEffectiveRange()};
}

Vec2 WorldShootingRuntime::aimWorldPosition() const noexcept
{
    return weaponAim_.actualWorldPosition();
}

Vec2 WorldShootingRuntime::aimDirection() const noexcept
{
    return weaponAim_.actualDirection();
}

float WorldShootingRuntime::aimDownSightsProgress() const noexcept
{
    return weaponAim_.aimDownSightsProgress();
}

bool WorldShootingRuntime::aimBeyondEffectiveRange() const noexcept
{
    return weaponAim_.beyondEffectiveRange();
}

bool WorldShootingRuntime::aimBeyondMaximumRange() const noexcept
{
    return weaponAim_.beyondMaximumRange();
}
