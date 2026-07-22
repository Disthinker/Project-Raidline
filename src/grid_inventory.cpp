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

    // 先验证有符号整数为正，再转换为无符号类型。
    const auto width =
        static_cast<std::size_t>(size_.width);
    const auto height =
        static_cast<std::size_t>(size_.height);

    // optional 默认构造为 nullopt，因此所有格子初始为空。
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

bool GridInventory::canPlace(
    ItemId definitionId,
    GridPosition origin) const
{
    const ItemDefinition &definition =
        itemDefinition(definitionId);

    return canPlaceDefinition(definition, origin);
}

std::optional<GridPosition> GridInventory::findFirstFit(
    ItemId definitionId) const
{
    // 在开始搜索前只查询一次定义。
    // definitionId 非法时会立即抛出 out_of_range。
    const ItemDefinition &definition =
        itemDefinition(definitionId);

    // row-major：
    // 先遍历第一行的所有 x，再进入下一行。
    for (int y = 0; y < size_.height; ++y)
    {
        for (int x = 0; x < size_.width; ++x)
        {
            const GridPosition candidate{x, y};

            if (canPlaceDefinition(definition, candidate))
            {
                return candidate;
            }
        }
    }

    return std::nullopt;
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

bool GridInventory::canPlaceDefinition(
    const ItemDefinition &definition,
    GridPosition origin) const noexcept
{
    if (!isWithinBounds(origin))
    {
        return false;
    }

    const int itemWidth =
        definition.inventoryWidthCells;
    const int itemHeight =
        definition.inventoryHeightCells;

    // ItemDefinition 目录正常情况下不会包含非正尺寸。
    // 这里仍采用 fail-closed：异常定义不可放置。
    if (itemWidth <= 0 || itemHeight <= 0)
    {
        return false;
    }

    // 物品本身比背包更大，任何 origin 都不合法。
    if (itemWidth > size_.width ||
        itemHeight > size_.height)
    {
        return false;
    }

    // 使用减法检查右边界和下边界，
    // 避免 origin + itemSize 的潜在整数溢出。
    if (origin.x > size_.width - itemWidth)
    {
        return false;
    }

    if (origin.y > size_.height - itemHeight)
    {
        return false;
    }

    // 检查物品 footprint 覆盖的每一个格子。
    for (int offsetY = 0;
         offsetY < itemHeight;
         ++offsetY)
    {
        for (int offsetX = 0;
             offsetX < itemWidth;
             ++offsetX)
        {
            const GridPosition coveredPosition{
                origin.x + offsetX,
                origin.y + offsetY};

            if (cells_[indexOf(coveredPosition)].has_value())
            {
                return false;
            }
        }
    }

    return true;
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