#pragma once

#include <algorithm>
#include <cmath>

inline constexpr float kMaximumFrameDeltaSeconds{0.10F};

// Production frames never try to simulate an arbitrarily long stall in one
// pass. This prevents a slow frame, debugger pause, display transition, or
// remote-session hitch from creating a catch-up spiral on the main thread.
[[nodiscard]] inline float boundedFrameDeltaSeconds(
    float measuredSeconds) noexcept
{
    if (!std::isfinite(measuredSeconds) || measuredSeconds <= 0.0F)
    {
        return 0.0F;
    }
    return std::min(measuredSeconds, kMaximumFrameDeltaSeconds);
}
