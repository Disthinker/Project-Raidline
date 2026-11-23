#pragma once

#include "item_definition.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <utility>

template<class Use> struct RaidFieldLanding
{
    Vec2 origin{}, point{};
    RaidSpaceDefinitionId space;
    Use use;
};
template<class Use> struct RaidFieldFlight : RaidFieldLanding<Use> { double elapsed{}; };
template<class Use> struct RaidFieldZone
{
    Vec2 point{};
    RaidSpaceDefinitionId space;
    float radius{};
    double remainingSeconds{};
    Use use;
};

// Two fields may coexist across all spaces. A flight reserves its landing
// slot, so consuming an item cannot later fail because the field list filled.
template<class Use> class RaidTimedFieldRuntime
{
public:
    static constexpr std::size_t kCapacity = 2;
    [[nodiscard]] bool canStart() const noexcept { return !flight_ && count_ < zones_.size(); }
    [[nodiscard]] const std::optional<RaidFieldFlight<Use>> &flight() const noexcept { return flight_; }
    [[nodiscard]] std::span<const RaidFieldZone<Use>> zones() const noexcept { return {zones_.data(), count_}; }

    bool start(RaidFieldLanding<Use> landing)
    {
        if (!canStart() || !landing.space.valid() || !landing.use.valid() ||
            !std::isfinite(landing.origin.x) || !std::isfinite(landing.origin.y) ||
            !std::isfinite(landing.point.x) || !std::isfinite(landing.point.y)) return false;
        flight_ = RaidFieldFlight<Use>{std::move(landing), 0.0};
        return true;
    }

    // Return each active interval within this advance, including a flight
    // landing and expiring entirely inside one long frame.
    template<class Observe> void advance(float seconds, Observe &&observe) noexcept
    {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        std::size_t kept{};
        for (std::size_t i = 0; i < count_; ++i)
        {
            observe(zones_[i], 0.0, std::min<double>(seconds, zones_[i].remainingSeconds));
            zones_[i].remainingSeconds -= seconds;
            if (zones_[i].remainingSeconds <= 0) continue;
            if (i != kept) zones_[kept] = std::move(zones_[i]);
            ++kept;
        }
        count_ = kept;
        if (!flight_) return;
        const double untilLanding = flight_->use.flightSeconds - flight_->elapsed;
        flight_->elapsed += seconds;
        if (flight_->elapsed < flight_->use.flightSeconds) return;
        RaidFieldZone<Use> zone{flight_->point, std::move(flight_->space), flight_->use.radius,
            flight_->use.durationSeconds, flight_->use};
        const double activeTime = seconds - untilLanding;
        observe(zone, untilLanding, std::min<double>(seconds, untilLanding + zone.remainingSeconds));
        zone.remainingSeconds -= activeTime;
        if (zone.remainingSeconds > 0) zones_[count_++] = std::move(zone);
        flight_.reset();
    }

    void advance(float seconds) noexcept
    { advance(seconds, [](const auto &, double, double) {}); }

private:
    std::optional<RaidFieldFlight<Use>> flight_;
    std::array<RaidFieldZone<Use>, kCapacity> zones_{};
    std::size_t count_{};
};
