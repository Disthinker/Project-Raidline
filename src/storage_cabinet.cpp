#include "storage_cabinet.h"

#include <cmath>
#include <stdexcept>
#include <utility>

#include "collision.h"

StorageCabinet::StorageCabinet(
    Vec2 position,
    Vec2 size,
    float interactionPadding,
    InventoryGridSize inventorySize)
    : position_{position},
      size_{size},
      interactionPadding_{interactionPadding},
      inventory_{inventorySize}
{
    if (!std::isfinite(position_.x) ||
        !std::isfinite(position_.y) ||
        !std::isfinite(size_.x) ||
        !std::isfinite(size_.y) ||
        !std::isfinite(interactionPadding_) ||
        size_.x <= 0.0F ||
        size_.y <= 0.0F ||
        interactionPadding_ < 0.0F)
    {
        throw std::invalid_argument(
            "Storage cabinet geometry must be finite and valid");
    }

    const float interactionX =
        position_.x - interactionPadding_;
    const float interactionY =
        position_.y - interactionPadding_;
    const float interactionWidth =
        size_.x + interactionPadding_ * 2.0F;
    const float interactionHeight =
        size_.y + interactionPadding_ * 2.0F;

    if (!std::isfinite(interactionX) ||
        !std::isfinite(interactionY) ||
        !std::isfinite(interactionWidth) ||
        !std::isfinite(interactionHeight))
    {
        throw std::invalid_argument(
            "Storage cabinet interaction bounds must be finite");
    }
}

Vec2 StorageCabinet::position() const noexcept
{
    return position_;
}

Vec2 StorageCabinet::size() const noexcept
{
    return size_;
}

Rect StorageCabinet::bounds() const noexcept
{
    return Rect{position_, size_};
}

bool StorageCabinet::canInteract(Rect actorBounds) const noexcept
{
    const Rect interactionBounds{
        Vec2{
            position_.x - interactionPadding_,
            position_.y - interactionPadding_},
        Vec2{
            size_.x + interactionPadding_ * 2.0F,
            size_.y + interactionPadding_ * 2.0F}};

    return isCollision(interactionBounds, actorBounds);
}

bool StorageCabinet::isSearched() const noexcept
{
    return searched_;
}

bool StorageCabinet::tryCommitSearchResult(
    GridInventory &&searchResult) noexcept
{
    if (searched_ ||
        !inventory_.placedItems().empty() ||
        searchResult.width() != inventory_.width() ||
        searchResult.height() != inventory_.height())
    {
        return false;
    }

    inventory_ = std::move(searchResult);
    searched_ = true;
    return true;
}

GridInventory &StorageCabinet::inventory() noexcept
{
    return inventory_;
}

const GridInventory &StorageCabinet::inventory() const noexcept
{
    return inventory_;
}
