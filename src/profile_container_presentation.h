#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "item_definition.h"
#include "inventory_interaction.h"
#include "profile_state.h"

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

[[nodiscard]] ProfileCompartmentLayout
layoutProfileCompartments(const ItemDefinition &definition, float startX,
                          float startY, float maximumX, float cellSize,
                          bool useSingleBackpackLabel);

// Client-only projection. Only warehouse views follow isolated storage pages;
// other containers retain their continuous physical dimensions.
struct ProfileInventoryViewport
{
    InventoryGridSize size{};
    InventoryGridSize window{8, 8};
    GridPosition origin{};
    float x{}, y{}, cellSize{64.0F};
    bool isolatedPages{};
    [[nodiscard]] int pageCount() const noexcept;
    [[nodiscard]] int page() const noexcept;
    [[nodiscard]] InventoryScreenRect clip() const noexcept;
    [[nodiscard]] InventoryScreenRect
    bounds(GridPosition cell, InventoryFootprint footprint) const noexcept;
    [[nodiscard]] std::optional<GridPosition>
    cellAt(MousePosition point) const noexcept;
    void clamp() noexcept;
    void selectPage(int pageIndex) noexcept;
    void pan(int columns, int rows) noexcept;
    void reveal(GridPosition cell, InventoryFootprint footprint) noexcept;
};

enum class ProfileInventoryTab
{
    Equipment,
    ChestRig,
    Backpack
};
struct ProfileInventoryPresentation
{
    ProfileInventoryTab tab{ProfileInventoryTab::Equipment};
    bool returns{};
    bool legacyOverflow{};
    std::uint32_t externalCompartment{};
    int chestScroll{};
    std::optional<ProfileContainerId> lastTarget;
    std::optional<AssetInstanceId> locateAsset;
    std::optional<ProfileContainerId> returnContainer;
    GridPosition returnOrigin{};
    struct Position
    {
        ProfileContainerId container;
        GridPosition origin;
    };
    std::vector<Position> positions;
    [[nodiscard]] GridPosition
    originFor(ProfileContainerId container) const noexcept;
    void remember(ProfileContainerId container, GridPosition origin);
};

// The returned rectangle describes the unrotated texture before center
// rotation.
[[nodiscard]] InventoryScreenRect
fitInventoryTexture(float textureWidth, float textureHeight,
                    InventoryScreenRect bounds, ItemOrientation orientation,
                    float padding = 6.0F) noexcept;
