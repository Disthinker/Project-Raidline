#include "grid_inventory.h"

#include <stdexcept>

GridInventory::GridInventory(InventoryGridSize size)
    : size_{size}
{
    if (size_.width <= 0)
    {
        throw std::invalid_argument(
            "GridInventory width must be positive");
    }

    if (size_.height <= 0)
    {
        throw std::invalid_argument(
            "GridInventory height must be positive");
    }

    // 必须先验证有符号整数为正，再转换为 size_t。
    const auto width =
        static_cast<std::size_t>(size_.width);
    const auto height =
        static_cast<std::size_t>(size_.height);

    // std::optional 默认构造后为 nullopt，
    // 因此所有格子初始都是空的。
    cells_.resize(width * height);
}

int GridInventory::width() const noexcept
{
    return size_.width;
}

int GridInventory::height() const noexcept
{
    return size_.height;
}

std::size_t GridInventory::cellCount() const noexcept
{
    return cells_.size();
}

std::optional<ItemInstanceId> GridInventory::occupantAt(
    GridPosition position) const noexcept
{
    if (!isWithinBounds(position))
    {
        return std::nullopt;
    }

    return cells_[indexOf(position)];
}

const std::vector<PlacedItem> &
GridInventory::placedItems() const noexcept
{
    return placedItems_;
}

bool GridInventory::isWithinBounds(
    GridPosition position) const noexcept
{
    return position.x >= 0 &&
           position.y >= 0 &&
           position.x < size_.width &&
           position.y < size_.height;
}

std::size_t GridInventory::indexOf(
    GridPosition position) const noexcept
{
    const auto x =
        static_cast<std::size_t>(position.x);
    const auto y =
        static_cast<std::size_t>(position.y);
    const auto width =
        static_cast<std::size_t>(size_.width);

    return y * width + x;
}