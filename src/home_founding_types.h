#pragma once

#include "content_registry.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Local plots are not regional route nodes. Empty map means legacy layout.
struct HomeFoundingState {
  bool established{true};
  bool hintsDismissed{true};
  std::map<RegionalBaseSiteDefinitionId, std::string> plots;
  // Version 1 is the unchanged legacy Home Region; version 2 freezes
  // this slice's three local plots and authored access corridors.
  std::uint32_t layoutVersion{1};
  friend bool operator==(const HomeFoundingState &,
                         const HomeFoundingState &) = default;
};

struct HomePlotDefinition {
  std::string_view id;
  std::string_view name;
  std::string_view description;
  ContentRect bounds;
  Vec2 corePosition;
  std::vector<ContentRect> fixedBlockers;
};

inline const RegionalBaseSiteDefinitionId kFoundingRegion{
    "regional_base_site.greyline_yard"};
inline const ContentRect kFoundingCamp{{2800.0F, 1960.0F}, {320.0F, 300.0F}};
[[nodiscard]] const std::array<HomePlotDefinition, 3> &homePlotDefinitions();
[[nodiscard]] const HomePlotDefinition *homePlotDefinition(std::string_view id);
