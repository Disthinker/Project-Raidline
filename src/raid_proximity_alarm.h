#pragma once

#include "item_definition.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>

struct RaidAlarmPlacement
{
    Vec2 point{};
    RaidSpaceDefinitionId space;
    ProximityAlarmUseDefinition use;
};
struct RaidAlarmDevice : RaidAlarmPlacement { bool triggered{}; };

class RaidProximityAlarmRuntime
{
public:
    [[nodiscard]] bool canPlace() const noexcept { return armed_ < devices_.size(); }
    [[nodiscard]] std::span<const RaidAlarmDevice> entries() const noexcept { return {devices_.data(), count_}; }
    [[nodiscard]] bool canPlaceAt(const RaidAlarmPlacement &placement) const noexcept
    {
        if (!canPlace() || !placement.space.valid() || !placement.use.valid() ||
            !std::isfinite(placement.point.x) || !std::isfinite(placement.point.y)) return false;
        for (const auto &device : entries())
            if (!device.triggered && tooClose(device, placement)) return false;
        return true;
    }
    bool place(RaidAlarmPlacement placement)
    {
        if (!canPlaceAt(placement)) return false;
        std::size_t slot = count_;
        for (std::size_t i = 0; i < count_; ++i)
            if (devices_[i].triggered && (slot == devices_.size() || tooClose(devices_[i], placement)))
                slot = i;
        devices_[slot] = RaidAlarmDevice{std::move(placement), false};
        if (slot == count_) ++count_;
        ++armed_;
        return true;
    }

    [[nodiscard]] unsigned armedIn(const RaidSpaceDefinitionId &space) const noexcept
    {
        unsigned mask{};
        if (armed_)
            for (std::size_t i = 0; i < count_; ++i)
                if (!devices_[i].triggered && devices_[i].space == space) mask |= 1U << i;
        return mask;
    }

    template<class Unobstructed> bool observe(Vec2 target, unsigned &mask, Unobstructed &&unobstructed)
    {
        bool fired = false;
        for (std::size_t i = 0; i < count_; ++i)
        {
            if (!(mask & (1U << i))) continue;
            auto &device = devices_[i];
            if (device.triggered) { mask &= ~(1U << i); continue; }
            const float dx = target.x - device.point.x, dy = target.y - device.point.y;
            const float radius = device.use.triggerRadius;
            if (dx * dx + dy * dy > radius * radius || !unobstructed(device.point, target)) continue;
            device.triggered = fired = true;
            --armed_;
            mask &= ~(1U << i);
        }
        return fired;
    }
private:
    static bool tooClose(const RaidAlarmDevice &device, const RaidAlarmPlacement &placement) noexcept
    {
        if (device.space != placement.space) return false;
        const float limit = std::max(device.use.minimumSpacing, placement.use.minimumSpacing);
        const float dx = device.point.x - placement.point.x, dy = device.point.y - placement.point.y;
        return dx * dx + dy * dy < limit * limit;
    }
    std::array<RaidAlarmDevice, 2> devices_{};
    std::size_t count_{}, armed_{};
};
