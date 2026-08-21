#pragma once

#include <cstdint>

// Stable only for the lifetime of one Raid simulation. It is never an asset
// ID and is not persisted by the current non-resumable Alpha Raid contract.
using CombatTargetId = std::uint64_t;

inline constexpr CombatTargetId kInvalidCombatTargetId{0};
