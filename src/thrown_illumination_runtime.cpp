#include "thrown_illumination_runtime.h"

#include "raid_noise_decoy_runtime.h"

#include <cmath>
#include <utility>

namespace
{
bool validElapsed(double elapsed, float duration)
{
    return std::isfinite(elapsed) && elapsed >= 0.0 && elapsed < duration;
}
}

std::optional<ThrownIlluminationLanding> queryThrownIlluminationLanding(
    Vec2 origin, Vec2 aim, const RaidSpaceDefinitionId &space,
    const ThrownIlluminationUseDefinition &use, const RaidSpaceBlockerIndex &index)
{
    if (!use.valid()) return std::nullopt;
    NoiseDecoyUseDefinition ray;
    ray.range = use.maximumRange;
    ray.flightSeconds = use.flightSeconds;
    const auto landing = queryNoiseDecoyLanding(origin, aim, space, ray, index);
    if (!landing) return std::nullopt;
    return ThrownIlluminationLanding{landing->origin, landing->point, landing->space, use, {}};
}

bool ThrownIlluminationRuntime::start(const ThrownIlluminationLanding &landing)
{
    if (active() || !landing.valid()) return false;
    flight_ = ThrownIlluminationFlight{landing, 0.0};
    expiredLastAdvance_ = false;
    return true;
}

void ThrownIlluminationRuntime::advance(float seconds) noexcept
{
    expiredLastAdvance_ = false;
    if (!std::isfinite(seconds) || seconds <= 0.0F) return;
    double remaining = seconds;
    if (flight_)
    {
        const double untilLanding = flight_->landing.use.flightSeconds - flight_->elapsed;
        if (remaining < untilLanding)
        {
            flight_->elapsed += remaining;
            return;
        }
        remaining -= untilLanding;
        source_ = ThrownIlluminationSource{std::move(flight_->landing), 0.0};
        flight_.reset();
    }
    if (!source_) return;
    source_->elapsed += remaining;
    if (source_->elapsed >= source_->landing.use.durationSeconds)
    {
        source_.reset();
        expiredLastAdvance_ = true;
    }
}

void ThrownIlluminationRuntime::markNightVisionGlareTriggered() noexcept
{
    if (source_) source_->nightVisionGlareTriggered = true;
}

void ThrownIlluminationRuntime::clear() noexcept
{
    flight_.reset();
    source_.reset();
    expiredLastAdvance_ = false;
}

ThrownIlluminationCheckpoint ThrownIlluminationRuntime::checkpoint() const
{
    return {flight_, source_};
}

bool ThrownIlluminationRuntime::restore(const ThrownIlluminationCheckpoint &saved)
{
    if ((saved.flight && saved.source) ||
        (saved.flight && (!saved.flight->landing.valid() ||
            !validElapsed(saved.flight->elapsed, saved.flight->landing.use.flightSeconds))) ||
        (saved.source && (!saved.source->landing.valid() ||
            !validElapsed(saved.source->elapsed, saved.source->landing.use.durationSeconds))))
        return false;
    ThrownIlluminationRuntime candidate;
    candidate.flight_ = saved.flight;
    candidate.source_ = saved.source;
    *this = std::move(candidate);
    return true;
}
