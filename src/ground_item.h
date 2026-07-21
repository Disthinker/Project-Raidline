#pragma once

#include "item_instance.h"
#include "rect.h"
#include "vec2.h"

// 地面上的物品实体。
// position 表示物品拾取区域的世界中心点。
class GroundItem
{
public:
    GroundItem(
        ItemInstance item,
        Vec2 position);

    ~GroundItem() = default;

    GroundItem(const GroundItem &) = delete;
    GroundItem &operator=(const GroundItem &) = delete;

    GroundItem(GroundItem &&) noexcept = default;
    GroundItem &operator=(GroundItem &&) noexcept = default;

    [[nodiscard]]
    const ItemInstance &item() const noexcept;

    // 返回世界中心位置，而不是左上角。
    [[nodiscard]]
    Vec2 position() const noexcept;

    // Rect 使用左上角位置，因此需要从中心点转换。
    [[nodiscard]]
    Rect pickupBounds() const;

    // 把内部 ItemInstance 的所有权移交给调用方。
    // 调用后 GroundItem 应立即从容器中删除。
    [[nodiscard]]
    ItemInstance takeItem() noexcept;

private:
    ItemInstance item_;
    Vec2 position_{};
};