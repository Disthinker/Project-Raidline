#pragma once

#include <cstdint>

#include "item_definition.h"

using ItemInstanceId = std::uint64_t;

// 世界或背包中一个具体且唯一的物品实例。
class ItemInstance
{
public:
    ItemInstance(
        ItemInstanceId instanceId,
        ItemId definitionId);

    ~ItemInstance() = default;

    ItemInstance(const ItemInstance &) = delete;
    ItemInstance &operator=(const ItemInstance &) = delete;

    ItemInstance(ItemInstance &&other) noexcept;
    ItemInstance &operator=(ItemInstance &&other) noexcept;

    [[nodiscard]]
    ItemInstanceId instanceId() const noexcept;

    [[nodiscard]]
    ItemId definitionId() const noexcept;

    // 正常构造后的实例有效；
    // 从该对象移动后，它进入明确的无效状态。
    [[nodiscard]]
    bool valid() const noexcept;

private:
    static constexpr ItemInstanceId kInvalidInstanceId{0};

    ItemInstanceId instanceId_{kInvalidInstanceId};
    ItemId definitionId_{ItemId::Count};
};