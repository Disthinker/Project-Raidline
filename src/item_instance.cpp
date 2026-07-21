#include "item_instance.h"

#include <stdexcept>
#include <utility>

ItemInstance::ItemInstance(
    ItemInstanceId instanceId,
    ItemId definitionId)
    : instanceId_{instanceId},
      definitionId_{definitionId}
{
    if (instanceId_ == kInvalidInstanceId)
    {
        throw std::invalid_argument{
            "ItemInstanceId must be greater than zero"};
    }

    // 查询定义的同时验证 definitionId。
    // ItemId::Count 和其他非法枚举值都会抛出异常。
    static_cast<void>(
        itemDefinition(definitionId_));
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
              ItemId::Count)}
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

bool ItemInstance::valid() const noexcept
{
    return instanceId_ != kInvalidInstanceId &&
           definitionId_ != ItemId::Count;
}