#pragma once

#include "raid_smoke_sight.h"
#include "raid_timed_field_runtime.h"

using SmokeLanding = RaidFieldLanding<SmokeUseDefinition>;
using SmokeFlight = RaidFieldFlight<SmokeUseDefinition>;
using SmokeCloud = RaidFieldZone<SmokeUseDefinition>;

class RaidSmokeRuntime : public RaidTimedFieldRuntime<SmokeUseDefinition>
{
public:
    [[nodiscard]] std::span<const SmokeCloud> clouds() const noexcept { return zones(); }
    [[nodiscard]] RaidSmokeSight sight(const RaidSpaceDefinitionId &space) const noexcept
    {
        static_assert(kCapacity == RaidSmokeSight::kCapacity);
        RaidSmokeSight result;
        for (const auto &cloud : clouds())
            if (cloud.space == space) result.circles[result.count++] = {cloud.point, cloud.radius};
        return result;
    }
};
