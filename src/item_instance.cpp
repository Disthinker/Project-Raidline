#include "item_instance.h"

#include <stdexcept>
#include <utility>

ItemInstance::ItemInstance(
    ItemInstanceId instanceId,
    ItemId definitionId,
    std::uint32_t quantity)
    : instanceId_{instanceId},
      definitionId_{definitionId},
      quantity_{quantity}
{
    if (instanceId_ == kInvalidInstanceId)
    {
        throw std::invalid_argument{
            "ItemInstanceId must be greater than zero"};
    }

    // 查询定义的同时验证 definitionId。
    // ItemId::Count 和其他非法枚举值都会抛出异常。
    const ItemDefinition &definition =
        itemDefinition(definitionId_);

    if (quantity_ == 0 ||
        quantity_ > definition.maxStackSize)
    {
        throw std::invalid_argument{
            "Item quantity must be within its stack limit"};
    }
}

ItemInstance::ItemInstance(
    ItemInstance &&other) noexcept
    : instanceId_{
          std::exchange(
              other.instanceId_,
              kInvalidInstanceId)},
      definitionId_{
          std::exchange(
              other.definitionId_,
              ItemId::Count)},
      orientation_{
          std::exchange(
              other.orientation_,
              ItemOrientation::Degrees0)},
      quantity_{
          std::exchange(
              other.quantity_,
              0)}
{
}

ItemInstance &
ItemInstance::operator=(
    ItemInstance &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    instanceId_ =
        std::exchange(
            other.instanceId_,
            kInvalidInstanceId);

    definitionId_ =
        std::exchange(
            other.definitionId_,
            ItemId::Count);

    orientation_ =
        std::exchange(
            other.orientation_,
            ItemOrientation::Degrees0);

    quantity_ =
        std::exchange(
            other.quantity_,
            0);

    return *this;
}

ItemInstanceId
ItemInstance::instanceId() const noexcept
{
    return instanceId_;
}

ItemId
ItemInstance::definitionId() const noexcept
{
    return definitionId_;
}

ItemOrientation
ItemInstance::orientation() const noexcept
{
    return orientation_;
}

std::uint32_t
ItemInstance::quantity() const noexcept
{
    return quantity_;
}

bool ItemInstance::trySetQuantity(
    std::uint32_t quantity)
{
    if (!valid())
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(definitionId_);

    if (quantity == 0 ||
        quantity > definition.maxStackSize)
    {
        return false;
    }

    quantity_ = quantity;
    return true;
}

bool ItemInstance::trySetOrientation(
    ItemOrientation orientation)
{
    if (!valid())
    {
        return false;
    }

    const ItemDefinition &definition =
        itemDefinition(definitionId_);

    if (!canUseItemOrientation(
            definition,
            orientation))
    {
        return false;
    }

    orientation_ = orientation;
    return true;
}

bool ItemInstance::valid() const noexcept
{
    return instanceId_ != kInvalidInstanceId &&
           definitionId_ != ItemId::Count &&
           quantity_ != 0;
}
