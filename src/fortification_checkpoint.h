#pragma once
#include "base_fortification_types.h"
#include "rect.h"
#include <span>

struct FortificationSnapshot
{
    FortificationInstanceId id;
    FortificationDefinitionId definition;
    DefenseSlotKey slot;
    Rect footprint;
    std::uint32_t maximumDurability{};
    std::uint32_t initialDurability{};
    std::uint32_t durability{};
};

// A committed Scratch belongs to one stable target through recovery, even if
// another attacker disables it. It must never turn into a late player hit.
struct FortificationAttackBinding
{
    std::uint64_t enemyId{};
    FortificationInstanceId target;
};

[[nodiscard]] bool validateFortificationCheckpoints(std::span<const FortificationSnapshot>,
                                                    std::uint32_t geometryRevision);
