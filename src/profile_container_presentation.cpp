#include "profile_container_presentation.h"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <cmath>

ProfileCompartmentLayout
layoutProfileCompartments(const ItemDefinition &definition, float startX,
                          float startY, float maximumX, float cellSize,
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
    for (std::size_t index = 0; index < definition.containerCompartments.size();
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
        else if (compartment.pocketKind == ContainerPocketKind::MagazineOnly)
        {
            label = "MAG " + std::to_string(++magazineIndex);
        }
        else
        {
            label = "UTIL " + std::to_string(++generalIndex);
        }

        result.placements.push_back(ProfileCompartmentPlacement{
            index, x, y, cellSize, std::move(label)});
        x += width + 8.0F;
        rowHeight = std::max(rowHeight, height);
        result.bottom = std::max(result.bottom, y + height);
    }
    return result;
}

int ProfileInventoryViewport::pageCount() const noexcept
{
    return ((size.width + window.width - 1) / window.width) *
           ((size.height + window.height - 1) / window.height);
}
int ProfileInventoryViewport::page() const noexcept
{
    return origin.y / window.height *
               ((size.width + window.width - 1) / window.width) +
           origin.x / window.width;
}
InventoryScreenRect ProfileInventoryViewport::clip() const noexcept
{
    return {
        x, y, static_cast<float>(std::min(size.width, window.width)) * cellSize,
        static_cast<float>(std::min(size.height, window.height)) * cellSize};
}
InventoryScreenRect
ProfileInventoryViewport::bounds(GridPosition cell,
                                 InventoryFootprint footprint) const noexcept
{
    return {x + (cell.x - origin.x) * cellSize,
            y + (cell.y - origin.y) * cellSize, footprint.width * cellSize,
            footprint.height * cellSize};
}
std::optional<GridPosition>
ProfileInventoryViewport::cellAt(MousePosition point) const noexcept
{
    if (!clip().contains(point))
        return std::nullopt;
    const GridPosition cell{
        origin.x + static_cast<int>(std::floor((point.x - x) / cellSize)),
        origin.y + static_cast<int>(std::floor((point.y - y) / cellSize))};
    if (cell.x < 0 || cell.y < 0 || cell.x >= size.width ||
        cell.y >= size.height)
        return std::nullopt;
    return cell;
}
void ProfileInventoryViewport::clamp() noexcept
{
    // Partial last pages retain their regular origin; cells outside size stay
    // disabled.
    origin.x =
        std::clamp(origin.x, 0,
                   std::max(0, (size.width - 1) / window.width * window.width));
    origin.y = std::clamp(
        origin.y, 0,
        std::max(0, (size.height - 1) / window.height * window.height));
    if (isolatedPages)
        selectPage(page());
}
void ProfileInventoryViewport::selectPage(int index) noexcept
{
    index = std::clamp(index, 0, std::max(0, pageCount() - 1));
    const int columns = (size.width + window.width - 1) / window.width;
    origin = {index % columns * window.width, index / columns * window.height};
}
void ProfileInventoryViewport::pan(int columns, int rows) noexcept
{
    if (isolatedPages)
        return;
    origin.x += columns;
    origin.y += rows;
    clamp();
}
void ProfileInventoryViewport::reveal(GridPosition cell,
                                      InventoryFootprint footprint) noexcept
{
    if (isolatedPages)
    {
        const int columns = (size.width + window.width - 1) / window.width;
        selectPage(cell.y / window.height * columns + cell.x / window.width);
        return;
    }
    origin.x =
        std::clamp(origin.x, cell.x + footprint.width - window.width, cell.x);
    origin.y =
        std::clamp(origin.y, cell.y + footprint.height - window.height, cell.y);
    origin.x = std::clamp(origin.x, 0, std::max(0, size.width - window.width));
    origin.y =
        std::clamp(origin.y, 0, std::max(0, size.height - window.height));
}
GridPosition ProfileInventoryPresentation::originFor(
    ProfileContainerId container) const noexcept
{
    for (const auto &p : positions)
        if (p.container == container)
            return p.origin;
    return {};
}
void ProfileInventoryPresentation::remember(ProfileContainerId container,
                                            GridPosition origin)
{
    for (auto &p : positions)
        if (p.container == container)
        {
            p.origin = origin;
            return;
        }
    positions.push_back({container, origin});
}
InventoryScreenRect fitInventoryTexture(float width, float height,
                                        InventoryScreenRect b,
                                        ItemOrientation orientation,
                                        float padding) noexcept
{
    if (width <= 0 || height <= 0 || b.width <= 0 || b.height <= 0)
        return {b.x, b.y, 0, 0};
    const bool sideways = orientation == ItemOrientation::Degrees90 ||
                          orientation == ItemOrientation::Degrees270;
    const float scale =
        std::max(0.0F, std::min(std::max(0.0F, b.width - 2 * padding) /
                                    (sideways ? height : width),
                                std::max(0.0F, b.height - 2 * padding) /
                                    (sideways ? width : height)));
    return {b.x + (b.width - width * scale) * 0.5F,
            b.y + (b.height - height * scale) * 0.5F, width * scale,
            height * scale};
}
