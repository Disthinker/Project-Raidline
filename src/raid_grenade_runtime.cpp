#include "raid_grenade_runtime.h"

#include "raid_noise_decoy_runtime.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
bool finite(Vec2 point) { return std::isfinite(point.x) && std::isfinite(point.y); }
}

std::optional<GrenadeLanding> queryGrenadeLanding(Vec2 origin, Vec2 aim,
    const RaidSpaceDefinitionId &space, const GrenadeUseDefinition &use, const RaidSpaceBlockerIndex &index)
{
    if (!use.valid()) return std::nullopt;
    // Both real consumers use the existing indexed, two-dimensional first-wall
    // stop. This does not add high arcs, bounces or a second wall definition.
    NoiseDecoyUseDefinition ray;
    ray.range = use.range;
    ray.flightSeconds = use.flightSeconds;
    const auto landing = queryNoiseDecoyLanding(origin, aim, space, ray, index);
    if (!landing) return std::nullopt;
    return GrenadeLanding{landing->origin, landing->point, landing->space, use};
}

bool RaidGrenadeRuntime::start(const GrenadeLanding &landing)
{
    if (active() || nextExplosionId_ == std::numeric_limits<std::uint64_t>::max() ||
        !landing.space.valid() || !finite(landing.origin) || !finite(landing.point) || !landing.use.valid())
        return false;
    flight_ = GrenadeFlight{landing, 0.0};
    return true;
}

void RaidGrenadeRuntime::advance(const RaidSpaceDefinitionId &space, float seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0F || explosion_) return;
    double remaining = seconds;
    if (flight_)
    {
        if (flight_->landing.space != space) return;
        const double untilLanding = std::max(0.0, flight_->landing.use.flightSeconds - flight_->elapsed);
        if (remaining + 0.000001 < untilLanding)
        {
            flight_->elapsed += remaining;
            return;
        }
        remaining = std::max(0.0, remaining - untilLanding);
        fuse_ = GrenadeFuse{std::move(flight_->landing), 0.0};
        flight_.reset();
    }
    if (!fuse_ || fuse_->landing.space != space) return;
    const double untilExplosion = std::max(0.0, fuse_->landing.use.fuseSeconds - fuse_->elapsed);
    if (remaining + 0.000001 < untilExplosion)
    {
        fuse_->elapsed += remaining;
        return;
    }
    explosion_ = GrenadeExplosion{nextExplosionId_++, std::move(fuse_->landing)};
    fuse_.reset();
}

bool RaidGrenadeRuntime::active() const noexcept
{
    return flight_.has_value() || fuse_.has_value() || explosion_.has_value();
}

void RaidGrenadeRuntime::acknowledgeExplosion() noexcept
{
    explosion_.reset();
}

void RaidGrenadeRuntime::clear() noexcept
{
    flight_.reset();
    fuse_.reset();
    explosion_.reset();
}
