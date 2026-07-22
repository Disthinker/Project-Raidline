#include "grid_inventory.h"

#include <stdexcept>

GridInventory::GridInventory(InventoryGridSize size)
    : size_{size}
{
    // TODO 1：
    // width 和 height 必须都大于 0。
    // 非法时抛出 std::invalid_argument。
    //
    // 验证通过后，将 cells_ 调整为 width * height 个空格子。
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
    // TODO 2：
    // 越界返回 std::nullopt。
    // 合法时返回对应 cells_ 元素。
    return std::nullopt;
}

const std::vector<PlacedItem> &
GridInventory::placedItems() const noexcept
{
    return placedItems_;
}

bool GridInventory::isWithinBounds(
    GridPosition position) const noexcept
{
    // TODO 3：
    // 同时检查负坐标、右边界和下边界。
    return false;
}

std::size_t GridInventory::indexOf(
    GridPosition position) const noexcept
{
    // TODO 4：
    // 使用 row-major 的二维转一维公式。
    // 只有坐标合法后才能转换为 size_t。
    return 0;
}