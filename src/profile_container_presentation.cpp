#include "profile_container_presentation.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

ProfileCompartmentLayout layoutProfileCompartments(
    const ItemDefinition &definition,
    float startX,
    float startY,
    float maximumX,
    float cellSize,
    bool useSingleBackpackLabel)
{
    if (maximumX <= startX || cellSize <= 0.0F)
    {
        throw std::invalid_argument{"profile compartment layout is invalid"};
    }

    ProfileCompartmentLayout result;
    result.bottom = startY;
    float x = startX;
    float y = startY;
    float rowHeight{};
    std::size_t magazineIndex{};
    std::size_t generalIndex{};
    for (std::size_t index = 0;
         index < definition.containerCompartments.size();
         ++index)
    {
        const ContainerCompartmentDefinition &compartment =
            definition.containerCompartments[index];
        const float width = compartment.width * cellSize;
        const float height = compartment.height * cellSize;
        if (x > startX && x + width > maximumX)
        {
            x = startX;
            y += rowHeight + 24.0F;
            rowHeight = 0.0F;
        }

        std::string label;
        if (useSingleBackpackLabel &&
            definition.containerCompartments.size() == 1U)
        {
            label = "BACKPACK";
        }
        else if (compartment.pocketKind ==
                 ContainerPocketKind::MagazineOnly)
        {
            label = "MAG " + std::to_string(++magazineIndex);
        }
        else
        {
            label = "UTIL " + std::to_string(++generalIndex);
        }

        result.placements.push_back(ProfileCompartmentPlacement{
            index,
            x,
            y,
            cellSize,
            std::move(label)});
        x += width + 8.0F;
        rowHeight = std::max(rowHeight, height);
        result.bottom = std::max(result.bottom, y + height);
    }
    return result;
}
