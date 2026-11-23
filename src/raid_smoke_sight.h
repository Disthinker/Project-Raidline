#pragma once

#include "vec2.h"
#include <algorithm>
#include <array>
#include <cstddef>

struct RaidSmokeCircle
{
    Vec2 center{};
    float radius{};
};

// Purely optical. Movement, projectiles, hearing and physical contact all use
// their original geometry even when this screen intersects their path.
struct RaidSmokeSight
{
    static constexpr std::size_t kCapacity = 2;
    static constexpr float kContactDistance = 32.0F;
    std::array<RaidSmokeCircle, kCapacity> circles{};
    std::size_t count{};

    [[nodiscard]] bool blocks(Vec2 from, Vec2 to) const noexcept
    {
        if (!count) return false;
        if (count > kCapacity) return true;
        const float x = to.x - from.x, y = to.y - from.y;
        const float lengthSquared = x * x + y * y;
        if (lengthSquared <= kContactDistance * kContactDistance) return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto &circle = circles[i];
            const float cx = circle.center.x - from.x, cy = circle.center.y - from.y;
            const float t = std::clamp((cx * x + cy * y) / lengthSquared, 0.0F, 1.0F);
            const float dx = cx - x * t, dy = cy - y * t;
            if (dx * dx + dy * dy < circle.radius * circle.radius) return true;
        }
        return false;
    }

    friend bool operator==(const RaidSmokeSight &a, const RaidSmokeSight &b) noexcept
    {
        if (a.count != b.count || a.count > kCapacity) return false;
        for (std::size_t i = 0; i < a.count; ++i)
            if (a.circles[i].center.x != b.circles[i].center.x ||
                a.circles[i].center.y != b.circles[i].center.y ||
                a.circles[i].radius != b.circles[i].radius) return false;
        return true;
    }
};
