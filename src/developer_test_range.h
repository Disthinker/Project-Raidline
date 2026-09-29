#pragma once
#include "content_registry.h"
#include <array>

inline constexpr Vec2 kDeveloperRangeSize{2048, 1280};
inline constexpr Vec2 kDeveloperRangeConsole{220, 980};
inline constexpr std::array<Vec2, 3> kDeveloperRangePads{{{700, 420}, {1150, 420}, {1650, 420}}};

// Panel choices come from published capabilities, never UI names.
[[nodiscard]] std::vector<ItemDefinitionId> developerRangeWeapons();
[[nodiscard]] std::vector<ItemDefinitionId> developerRangeAmmunition(const ItemDefinitionId &weapon);
[[nodiscard]] std::vector<ItemDefinitionId> developerRangeArmor();
[[nodiscard]] std::vector<ItemDefinitionId> developerRangeSupplies();
