#pragma once

#include "rect.h"
#include <cstdint>

inline constexpr float kRaidVolatileBarrelRadius = 16.0F;
inline constexpr float kRaidVolatileBarrelBlastRadius = 160.0F;

struct RaidVolatileBarrelSpawn
{
    std::uint32_t id{};
    Vec2 center{};
    friend bool operator==(const RaidVolatileBarrelSpawn &a, const RaidVolatileBarrelSpawn &b) noexcept
    { return a.id == b.id && a.center.x == b.center.x && a.center.y == b.center.y; }
};

struct RaidVolatileBarrelView : RaidVolatileBarrelSpawn
{
    bool spent{};
};

[[nodiscard]] inline Rect volatileBarrelBounds(Vec2 center) noexcept
{
    return {{center.x - kRaidVolatileBarrelRadius, center.y - kRaidVolatileBarrelRadius},
        {2.0F * kRaidVolatileBarrelRadius, 2.0F * kRaidVolatileBarrelRadius}};
}
