#pragma once

#include "item_definition.h"

// Geometry, eligibility, armor and mutations belong to the consumers. This
// query supplies only the approved torso damage before armor at one distance.
[[nodiscard]] int grenadeDamageAtDistance(float distance, const GrenadeUseDefinition &) noexcept;
