#pragma once

#include <cstddef>
#include <deque>

#include "vec2.h"

// Device-independent mouse gesture used to clear a hidden weapon malfunction.
// A reversal is accepted only after a 36 logical-pixel segment and a direction
// change of at least 120 degrees. Four reversals must occur within one second.
class WeaponClearGesture
{
public:
    [[nodiscard]] bool observe(Vec2 delta, float nowSeconds) noexcept;
    void reset() noexcept;
    [[nodiscard]] std::size_t reversalCount() const noexcept;

private:
    Vec2 segment_{};
    std::deque<float> reversalTimes_;
};
