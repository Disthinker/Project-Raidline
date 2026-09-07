#pragma once

#include <cstdint>

// Stable within one combat activity, never an asset ID. Raid still uses its
// non-resumable contract; Base defense freezes the activity's IDs in its own
// resumable checkpoint so attacks and logical hits cannot change ownership.
using CombatTargetId = std::uint64_t;

inline constexpr CombatTargetId kInvalidCombatTargetId{0};
