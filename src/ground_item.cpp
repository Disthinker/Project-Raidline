#include "ground_item.h"

#include <utility>

GroundItem::GroundItem(
    ItemInstance item,
    Vec2 position)
    : item_{std::move(item)},
      position_{position}
{
}

const ItemInstance &
GroundItem::item() const noexcept
{
    return item_;
}

ItemInstance &
GroundItem::itemForTransfer() noexcept
{
    return item_;
}

Vec2 GroundItem::position() const noexcept
{
    return position_;
}

Rect GroundItem::pickupBounds() const
{
    const ItemDefinition &definition =
        itemDefinition(
            item_.definitionId());

    const Vec2 pickupSize =
        definition.pickupSize;

    return Rect{
        Vec2{
            position_.x -
                pickupSize.x / 2.0f,
            position_.y -
                pickupSize.y / 2.0f},
        pickupSize};
}

ItemInstance
GroundItem::takeItem() noexcept
{
    return std::move(item_);
}