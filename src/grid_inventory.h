#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "item_instance.h"

struct GridPosition
{
    int x{};
    int y{};
};

struct InventoryGridSize
{
    int width{};
    int height{};
};

struct PlacedItem
{
    ItemInstance item;
    GridPosition origin;
};

class GridInventory
{
public:
    explicit GridInventory(InventoryGridSize size);

    [[nodiscard]]
    int width() const noexcept;

    [[nodiscard]]
    int height() const noexcept;

    [[nodiscard]]
    std::size_t cellCount() const noexcept;

    [[nodiscard]]
    std::optional<ItemInstanceId> occupantAt(
        GridPosition position) const noexcept;

    [[nodiscard]]
    const std::vector<PlacedItem> &placedItems() const noexcept;

private:
    [[nodiscard]]
    bool isWithinBounds(GridPosition position) const noexcept;

    // 只允许对已确认合法的坐标调用。
    [[nodiscard]]
    std::size_t indexOf(GridPosition position) const noexcept;

    InventoryGridSize size_;
    std::vector<std::optional<ItemInstanceId>> cells_;
    std::vector<PlacedItem> placedItems_;
};