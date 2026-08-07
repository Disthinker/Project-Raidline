#pragma once

#include <optional>

#include "grid_inventory.h"

// SDL 无关的逻辑鼠标位置。
// App 负责把 SDL_Event 中的坐标转换成该值。
struct MousePosition
{
    float x{};
    float y{};

    friend bool operator==(
        const MousePosition &,
        const MousePosition &) = default;
};

enum class InventoryPointerEventType
{
    Motion,
    LeftButtonDown,
    LeftButtonUp
};

// App 将 SDL 事件转换成该值类型后再交给背包交互层。
// 这样帧级仲裁和核心测试都不需要依赖 SDL_Event。
struct InventoryPointerEvent
{
    InventoryPointerEventType type{};
    MousePosition position{};

    friend bool operator==(
        const InventoryPointerEvent &,
        const InventoryPointerEvent &) = default;
};

enum class InventoryFrameControlAction
{
    None,
    OpenInventory,
    CloseInventory,
    CancelInteraction
};

struct InventoryFrameInputDecision
{
    InventoryFrameControlAction controlAction{
        InventoryFrameControlAction::None};
    bool processPointerEvents{};
    bool processKeyboardInput{};

    friend bool operator==(
        const InventoryFrameInputDecision &,
        const InventoryFrameInputDecision &) = default;
};

// 固定一帧内的输入优先级：Tab > Esc > pointer > keyboard。
// 高优先级控制动作会丢弃本帧暂存的 pointer 事件，避免 release
// 在取消或关闭背包之前提交 GridInventory 写入。
[[nodiscard]]
InventoryFrameInputDecision decideInventoryFrameInput(
    bool inventoryOpen,
    bool toggleInventoryJustPressed,
    bool cancelJustPressed) noexcept;

// 背包格子的屏幕布局。
//
// 该值对象集中负责屏幕坐标到格子坐标的转换，
// 让输入命中测试与绘制共享同一套几何参数。
class InventoryGridLayout
{
public:
    InventoryGridLayout(
        float gridX,
        float gridY,
        float cellSize,
        InventoryGridSize gridSize);

    [[nodiscard]]
    float gridX() const noexcept;

    [[nodiscard]]
    float gridY() const noexcept;

    [[nodiscard]]
    float cellSize() const noexcept;

    [[nodiscard]]
    InventoryGridSize gridSize() const noexcept;

    // 左、上边界包含；右、下边界不包含。
    [[nodiscard]]
    std::optional<GridPosition> screenToGrid(
        MousePosition position) const noexcept;

private:
    float gridX_{};
    float gridY_{};
    float cellSize_{};
    InventoryGridSize gridSize_{};
};

// 背包交互当前所处的逻辑模式。
//
// 该状态不依赖键盘、鼠标或 SDL。
// Enter、Esc、鼠标点击等输入由 App 翻译为成员函数调用。
enum class InventoryInteractionMode
{
    Browsing,
    PlacingItem
};

enum class InventoryPointerPhase
{
    Idle,
    Pressed,
    Dragging
};

struct InventoryMoveRequest
{
    ItemInstanceId instanceId{};
    GridPosition destination{};

    friend bool operator==(
        const InventoryMoveRequest &,
        const InventoryMoveRequest &) = default;
};

class InventoryInteractionState
{
public:
    explicit InventoryInteractionState(
        InventoryGridSize gridSize);

    [[nodiscard]]
    InventoryInteractionMode mode() const noexcept;

    // 键盘浏览焦点。
    // 未来鼠标 hoveredCell 不应与该字段混用。
    [[nodiscard]]
    GridPosition focusedCell() const noexcept;

    // 当前键盘放置物品或鼠标持久选择的稳定 ID。
    // Browsing 模式下可由一次有效鼠标点击保留。
    [[nodiscard]]
    std::optional<ItemInstanceId>
    selectedInstanceId() const noexcept;

    [[nodiscard]]
    InventoryPointerPhase pointerPhase() const noexcept;

    [[nodiscard]]
    std::optional<GridPosition>
    hoveredCell() const noexcept;

    // 键盘放置或鼠标拖动当前使用的候选左上角。
    [[nodiscard]]
    std::optional<GridPosition>
    activePreviewOrigin() const noexcept;

    [[nodiscard]]
    bool pointerGestureActive() const noexcept;

    // 仅在鼠标进入 Dragging 后返回从按下点到当前点的逻辑像素位移。
    // App 使用原物品屏幕位置加该位移绘制平滑虚像；
    // 格子候选仍由 activePreviewOrigin() 独立提供。
    [[nodiscard]]
    std::optional<MousePosition>
    pointerDragDelta() const noexcept;

    // PlacingItem 模式下表示候选左上角格子。
    [[nodiscard]]
    GridPosition previewOrigin() const noexcept;

    // 只在 Browsing 模式下移动 focusedCell。
    //
    // 结果被限制在背包格子范围内。
    void moveFocus(
        int deltaX,
        int deltaY) noexcept;

    // 从 Browsing 进入 PlacingItem。
    //
    // instanceId 为 nullopt 或 0 时失败。
    // itemOrigin 是物品当前 placement 的左上角。
    [[nodiscard]]
    bool beginPlacement(
        std::optional<ItemInstanceId> instanceId,
        GridPosition itemOrigin) noexcept;

    // 只在 PlacingItem 模式下移动预览左上角。
    //
    // 这里只限制左上角仍位于网格内。
    // 多格物品是否越界由 GridInventory::canMove 判断，
    // 从而允许 UI 显示红色非法预览。
    void movePreview(
        int deltaX,
        int deltaY) noexcept;

    // App 调用 GridInventory::tryMove 后，
    // 把结果交回状态机。
    //
    // false：保持 PlacingItem，继续调整。
    // true：回到 Browsing，并将焦点移动到新 origin。
    void resolvePlacement(
        bool succeeded) noexcept;

    // 取消当前放置。
    //
    // 原 Inventory 不需要回滚，因为预览期间没有修改它。
    void cancelPlacement() noexcept;

    // 更新鼠标位置和 hover，并在超过阈值后进入拖动。
    // gridCell 为 nullopt 时表示鼠标位于背包网格外。
    void updatePointerPosition(
        MousePosition position,
        std::optional<GridPosition> gridCell) noexcept;

    // 在已占用格上开始一次鼠标手势。
    // itemOrigin 是物品真实左上角，用于保存多格物品的抓取偏移。
    [[nodiscard]]
    bool beginPointerPress(
        std::optional<ItemInstanceId> instanceId,
        std::optional<GridPosition> itemOrigin,
        GridPosition clickedCell,
        MousePosition position) noexcept;

    // 鼠标释放时仅生成移动请求，不修改 GridInventory。
    [[nodiscard]]
    std::optional<InventoryMoveRequest> releasePointer(
        MousePosition position,
        std::optional<GridPosition> gridCell) noexcept;

    // Esc 使用：取消当前鼠标手势，但保留持久选择与 hover。
    void cancelPointerGesture() noexcept;

    // 点击空格等场景使用：清除持久鼠标选择。
    void clearPointerSelection() noexcept;

    // 关闭背包时清除全部瞬态交互状态。
    void reset() noexcept;

private:
    [[nodiscard]]
    GridPosition clampToGrid(
        GridPosition position) const noexcept;

    InventoryGridSize gridSize_;

    InventoryInteractionMode mode_{
        InventoryInteractionMode::Browsing};

    GridPosition focusedCell_{0, 0};

    std::optional<ItemInstanceId>
        selectedInstanceId_;

    GridPosition previewOrigin_{0, 0};

    std::optional<GridPosition>
        hoveredCell_;

    InventoryPointerPhase pointerPhase_{
        InventoryPointerPhase::Idle};

    std::optional<MousePosition>
        pointerPressPosition_;

    std::optional<MousePosition>
        pointerCurrentPosition_;

    GridPosition grabOffset_{0, 0};

    std::optional<GridPosition>
        pointerPreviewOrigin_;

    void resetPointerGesture() noexcept;
};
