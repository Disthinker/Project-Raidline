#pragma once

#include "raid_space_spatial_index.h"
#include "throwable_types.h"

[[nodiscard]] std::optional<ThrownIlluminationLanding> queryThrownIlluminationLanding(
    Vec2 origin, Vec2 aim, const RaidSpaceDefinitionId &,
    const ThrownIlluminationUseDefinition &, const RaidSpaceBlockerIndex &);

// One transient light per activity. Its timer advances across space changes;
// consumers use the frozen landing space to scope observation and rendering.
class ThrownIlluminationRuntime
{
public:
    [[nodiscard]] bool start(const ThrownIlluminationLanding &);
    void advance(float seconds) noexcept;
    [[nodiscard]] bool active() const noexcept { return flight_ || source_; }
    [[nodiscard]] const std::optional<ThrownIlluminationFlight> &flight() const noexcept { return flight_; }
    [[nodiscard]] const std::optional<ThrownIlluminationSource> &source() const noexcept { return source_; }
    [[nodiscard]] bool expiredLastAdvance() const noexcept { return expiredLastAdvance_; }
    void markNightVisionGlareTriggered() noexcept;
    void clear() noexcept;
    [[nodiscard]] ThrownIlluminationCheckpoint checkpoint() const;
    [[nodiscard]] bool restore(const ThrownIlluminationCheckpoint &);

private:
    std::optional<ThrownIlluminationFlight> flight_;
    std::optional<ThrownIlluminationSource> source_;
    bool expiredLastAdvance_{};
};
