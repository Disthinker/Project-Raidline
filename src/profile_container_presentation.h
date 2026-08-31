#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "item_definition.h"

struct ProfileCompartmentPlacement
{
    std::size_t compartmentIndex{};
    float x{};
    float y{};
    float cellSize{};
    std::string label;
};

struct ProfileCompartmentLayout
{
    std::vector<ProfileCompartmentPlacement> placements;
    float bottom{};
};

[[nodiscard]] ProfileCompartmentLayout layoutProfileCompartments(
    const ItemDefinition &definition,
    float startX,
    float startY,
    float maximumX,
    float cellSize,
    bool useSingleBackpackLabel);
