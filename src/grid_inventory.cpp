#include "grid_inventory.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

PlacedItem::PlacedItem(
    ItemInstance &&itemValue,
    GridPosition originValue) noexcept
    : item{std::move(itemValue)},
      origin{originValue}
{
}

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

    const auto width =
        static_cast<std::size_t>(size_.width);

    const auto height =
        static_cast<std::size_t>(size_.height);

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

    // 普通放置不允许目标 footprint 出现任何占用者。
    return canPlaceDefinition(
        definition,
        origin,
        std::nullopt);
}

std::optional<GridPosition>
GridInventory::findFirstFit(
    ItemId definitionId) const
{
    const ItemDefinition &definition =
        itemDefinition(definitionId);

    for (int y = 0; y < size_.height; ++y)
    {
        for (int x = 0; x < size_.width; ++x)
        {
            const GridPosition candidate{x, y};

            if (canPlaceDefinition(
                    definition,
                    candidate,
                    std::nullopt))
            {
                return candidate;
            }
        }
    }

    return std::nullopt;
}

bool GridInventory::canMove(
    ItemInstanceId instanceId,
    GridPosition newOrigin) const
{
    const auto placedIt =
        std::find_if(
            placedItems_.begin(),
            placedItems_.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       instanceId;
            });

    if (placedIt == placedItems_.end())
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(
            placedIt->item.definitionId());

    // 检查目标 footprint 时允许出现当前物品自己的 ID。
    //
    // 因此物品向旁边移动一格时，新旧 footprint
    // 重叠的部分不会被错误判断为冲突。
    return canPlaceDefinition(
        definition,
        newOrigin,
        std::optional<ItemInstanceId>{
            instanceId});
}

bool GridInventory::tryPlace(
    ItemInstance &&item,
    GridPosition origin)
{
    // moved-from ItemInstance 不能再次进入背包。
    if (!item.valid())
    {
        return false;
    }

    const ItemInstanceId instanceId =
        item.instanceId();

    const ItemId definitionId =
        item.definitionId();

    // 同一个稳定 ID 不能在背包中出现两次。
    if (containsInstanceId(instanceId))
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(definitionId);

    // 先完成全部验证。
    // canPlaceDefinition 不修改任何状态。
    if (!canPlaceDefinition(
            definition,
            origin,
            std::nullopt))
    {
        return false;
    }

    // emplace_back 调用前的 std::move 只做类型转换。
    // 真正的移动发生在 PlacedItem 构造时。
    //
    // 如果 vector 分配内存失败，构造尚未发生，
    // 输入 item 仍保持有效。
    placedItems_.emplace_back(
        std::move(item),
        origin);

    // emplace_back 成功后才写 cells_。
    // 此处只有 optional<uint64_t> 赋值，不会抛出异常。
    setFootprintOccupant(
        definition,
        origin,
        std::optional<ItemInstanceId>{
            instanceId});

    return true;
}

std::optional<ItemInstance>
GridInventory::remove(
    ItemInstanceId instanceId)
{
    const auto placedIt =
        std::find_if(
            placedItems_.begin(),
            placedItems_.end(),
            [instanceId](const PlacedItem &placed)
            {
                return placed.item.instanceId() ==
                       instanceId;
            });

    if (placedIt == placedItems_.end())
    {
        return std::nullopt;
    }

    // erase 会使当前元素及其后的引用、指针和迭代器失效。
    // 因此必须先读取 origin 和 definition。
    const GridPosition origin =
        placedIt->origin;

    const ItemId definitionId =
        placedIt->item.definitionId();

    const ItemDefinition &definition =
        itemDefinition(definitionId);

    // 先清除该物品占用的全部格子。
    setFootprintOccupant(
        definition,
        origin,
        std::nullopt);

    // 在 erase 前移出真正的 ItemInstance。
    ItemInstance removedItem{
        std::move(placedIt->item)};

    // erase 后不再访问 placedIt。
    placedItems_.erase(placedIt);

    return std::optional<ItemInstance>{
        std::move(removedItem)};
}

std::optional<ItemInstanceId>
GridInventory::occupantAt(
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
    GridPosition origin,
    std::optional<ItemInstanceId> allowedOccupant) const noexcept
{
    if (!isWithinBounds(origin))
    {
        return false;
    }

    const int itemWidth =
        definition.inventoryWidthCells;

    const int itemHeight =
        definition.inventoryHeightCells;

    if (itemWidth <= 0 || itemHeight <= 0)
    {
        return false;
    }

    if (itemWidth > size_.width ||
        itemHeight > size_.height)
    {
        return false;
    }

    if (origin.x > size_.width - itemWidth)
    {
        return false;
    }

    if (origin.y > size_.height - itemHeight)
    {
        return false;
    }

    for (
        int offsetY = 0;
        offsetY < itemHeight;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < itemWidth;
            ++offsetX)
        {
            const GridPosition coveredPosition{
                origin.x + offsetX,
                origin.y + offsetY};

            const std::optional<ItemInstanceId> occupant =
                cells_[indexOf(coveredPosition)];

            // 空格始终允许。
            //
            // occupied 且 occupant == allowedOccupant 时，
            // 表示该格由正在移动的物品自己占用，也允许。
            //
            // 其他占用者一律拒绝。
            if (
                occupant.has_value() &&
                occupant != allowedOccupant)
            {
                return false;
            }
        }
    }

    return true;
}

bool GridInventory::containsInstanceId(
    ItemInstanceId instanceId) const noexcept
{
    return std::any_of(
        placedItems_.begin(),
        placedItems_.end(),
        [instanceId](const PlacedItem &placed)
        {
            return placed.item.instanceId() ==
                   instanceId;
        });
}

void GridInventory::setFootprintOccupant(
    const ItemDefinition &definition,
    GridPosition origin,
    std::optional<ItemInstanceId> occupant) noexcept
{
    for (
        int offsetY = 0;
        offsetY < definition.inventoryHeightCells;
        ++offsetY)
    {
        for (
            int offsetX = 0;
            offsetX < definition.inventoryWidthCells;
            ++offsetX)
        {
            const GridPosition coveredPosition{
                origin.x + offsetX,
                origin.y + offsetY};

            cells_[indexOf(coveredPosition)] =
                occupant;
        }
    }
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