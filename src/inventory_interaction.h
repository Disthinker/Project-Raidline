#pragma once

#include <optional>
#include <variant>

#include "grid_inventory.h"

struct MousePosition
{
    float x{};
    float y{};

    friend bool operator==(
        const MousePosition &,
        const MousePosition &) = default;
};

struct InventoryScreenRect
{
    float x{};
    float y{};
    float width{};
    float height{};

    [[nodiscard]]
    bool contains(MousePosition position) const noexcept;

    friend bool operator==(
        const InventoryScreenRect &,
        const InventoryScreenRect &) = default;
};

[[nodiscard]]
InventoryScreenRect makeRightEdgeInventoryDropZone(
    float screenWidth,
    float screenHeight,
    float zoneWidth);

enum class InventoryPointerEventType
{
    Motion,
    LeftButtonDown,
    LeftButtonUp
};

struct InventoryPointerEvent
{
    InventoryPointerEventType type{};
    MousePosition position{};

    friend bool operator==(
        const InventoryPointerEvent &,
        const InventoryPointerEvent &) = default;
};

// F 在按键事件到达时采样当前鼠标位置；Ctrl+右键使用点击位置。
// App 先刷新 hover，再解析源格，因此界面打开后无需先晃动鼠标。
struct InventoryQuickTransferEvent
{
    std::optional<MousePosition> pointerPosition;

    friend bool operator==(
        const InventoryQuickTransferEvent &,
        const InventoryQuickTransferEvent &) = default;
};

struct InventoryRotateEvent
{
    friend bool operator==(
        const InventoryRotateEvent &,
        const InventoryRotateEvent &) = default;
};

struct InventoryPartialTransferEvent
{
    MousePosition pointerPosition{};
    bool controlPressed{};
    bool shiftPressed{};

    friend bool operator==(
        const InventoryPartialTransferEvent &,
        const InventoryPartialTransferEvent &) = default;
};

using InventoryUiEvent = std::variant<
    InventoryPointerEvent,
    InventoryQuickTransferEvent,
    InventoryRotateEvent,
    InventoryPartialTransferEvent>;

enum class InventoryFrameControlAction
{
    None,
    OpenInventory,
    CloseInventory,
    CancelInteraction
};

enum class InventoryOverlayMode
{
    Closed,
    PlayerOnly,
    Container
};

class InventoryOverlayState
{
public:
    [[nodiscard]]
    InventoryOverlayMode mode() const noexcept;

    [[nodiscard]]
    bool isOpen() const noexcept;

    [[nodiscard]]
    bool showsExternalContainer() const noexcept;

    void openPlayerInventory() noexcept;
    void openContainerInventory() noexcept;
    void close() noexcept;

private:
    InventoryOverlayMode mode_{InventoryOverlayMode::Closed};
};

struct InventoryFrameInputDecision
{
    InventoryFrameControlAction controlAction{
        InventoryFrameControlAction::None};
    bool processUiEvents{};

    friend bool operator==(
        const InventoryFrameInputDecision &,
        const InventoryFrameInputDecision &) = default;
};

// 固定一帧内的输入优先级：Tab > Esc > pointer。
[[nodiscard]]
InventoryFrameInputDecision decideInventoryFrameInput(
    bool inventoryOpen,
    bool toggleInventoryJustPressed,
    bool cancelJustPressed) noexcept;

struct InventoryContainerInteractionDecision
{
    bool openContainer{};
    bool suppressGameplayInput{};

    friend bool operator==(
        const InventoryContainerInteractionDecision &,
        const InventoryContainerInteractionDecision &) = default;
};

[[nodiscard]]
InventoryContainerInteractionDecision
decideInventoryContainerInteraction(
    bool inventoryOpen,
    bool inventoryControlConsumedFrame,
    bool cabinetInRange,
    bool interactJustPressed) noexcept;

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

enum class InventoryContainerId
{
    Player,
    External
};

struct InventoryGridLocation
{
    InventoryContainerId container{
        InventoryContainerId::Player};
    GridPosition cell{};

    friend bool operator==(
        const InventoryGridLocation &,
        const InventoryGridLocation &) = default;
};

struct InventoryItemSelection
{
    InventoryContainerId container{
        InventoryContainerId::Player};
    ItemInstanceId instanceId{};

    friend bool operator==(
        const InventoryItemSelection &,
        const InventoryItemSelection &) = default;
};

enum class InventoryPointerPhase
{
    Idle,
    Pressed,
    Dragging
};

struct InventoryPointerItemGeometry
{
    ItemOrientation orientation{ItemOrientation::Degrees0};
    InventoryFootprint footprint{1, 1};
    bool canRotate{};
    MousePosition grabOffsetInCells{};

    friend bool operator==(
        const InventoryPointerItemGeometry &,
        const InventoryPointerItemGeometry &) = default;
};

struct InventoryDragVisual
{
    MousePosition pointerPosition{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    InventoryFootprint footprint{1, 1};
    MousePosition grabOffsetInCells{};
    std::optional<std::uint32_t> selectedQuantity;

    friend bool operator==(
        const InventoryDragVisual &,
        const InventoryDragVisual &) = default;
};

// Device-independent pointer gesture shared by legacy GridInventory and the
// Profile/AssetRegistry inventory screen. It owns no item or container state.
class InventoryDragGesture
{
public:
    [[nodiscard]] InventoryPointerPhase phase() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::optional<MousePosition> dragDelta() const noexcept;
    [[nodiscard]] std::optional<InventoryDragVisual> visual() const noexcept;
    [[nodiscard]] GridPosition grabOffset() const noexcept;

    [[nodiscard]] bool begin(
        GridPosition itemOrigin,
        GridPosition clickedCell,
        MousePosition position,
        InventoryPointerItemGeometry geometry,
        std::optional<std::uint32_t> selectedQuantity = std::nullopt) noexcept;

    void update(MousePosition position) noexcept;
    [[nodiscard]] bool rotateClockwise() noexcept;
    void reset() noexcept;

private:
    InventoryPointerPhase phase_{InventoryPointerPhase::Idle};
    std::optional<MousePosition> pressPosition_;
    std::optional<MousePosition> currentPosition_;
    GridPosition grabOffset_{0, 0};
    InventoryPointerItemGeometry geometry_{};
    std::optional<std::uint32_t> selectedQuantity_;
};

struct InventoryQuickTransferRequest
{
    InventoryGridLocation source{};

    friend bool operator==(
        const InventoryQuickTransferRequest &,
        const InventoryQuickTransferRequest &) = default;
};

enum class InventoryPartialTransferMode
{
    One,
    Half
};

[[nodiscard]]
std::optional<InventoryPartialTransferMode>
decideInventoryPartialTransferMode(
    bool controlPressed,
    bool shiftPressed) noexcept;

[[nodiscard]]
std::uint32_t inventoryPartialTransferQuantity(
    InventoryPartialTransferMode mode,
    std::uint32_t availableQuantity) noexcept;

// 快捷转移只在双容器界面、Idle 且存在 hover 时产生请求。
// 这里只决定 UI 意图，不查询或修改 GridInventory。
[[nodiscard]]
std::optional<InventoryQuickTransferRequest>
decideInventoryQuickTransfer(
    InventoryOverlayMode overlayMode,
    InventoryPointerPhase pointerPhase,
    std::optional<InventoryGridLocation> hoveredLocation) noexcept;

struct InventoryPlacementRequest
{
    InventoryItemSelection source{};
    InventoryGridLocation destination{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    std::optional<std::uint32_t> selectedQuantity;

    friend bool operator==(
        const InventoryPlacementRequest &,
        const InventoryPlacementRequest &) = default;
};

struct InventoryDropRequest
{
    InventoryItemSelection source{};
    ItemOrientation orientation{ItemOrientation::Degrees0};
    std::optional<std::uint32_t> selectedQuantity;

    friend bool operator==(
        const InventoryDropRequest &,
        const InventoryDropRequest &) = default;
};

using InventoryPointerRequest = std::variant<
    InventoryPlacementRequest,
    InventoryDropRequest>;

class InventoryInteractionState
{
public:
    InventoryInteractionState() = default;

    [[nodiscard]]
    std::optional<InventoryItemSelection>
    selectedItem() const noexcept;

    [[nodiscard]]
    InventoryPointerPhase pointerPhase() const noexcept;

    [[nodiscard]]
    std::optional<InventoryGridLocation>
    hoveredLocation() const noexcept;

    [[nodiscard]]
    std::optional<InventoryGridLocation>
    activePreviewLocation() const noexcept;

    [[nodiscard]]
    bool pointerOverDropZone() const noexcept;

    [[nodiscard]]
    bool pointerGestureActive() const noexcept;

    [[nodiscard]]
    std::optional<MousePosition>
    pointerDragDelta() const noexcept;

    [[nodiscard]]
    std::optional<InventoryDragVisual>
    activeDragVisual() const noexcept;

    void updatePointerPosition(
        MousePosition position,
        std::optional<InventoryGridLocation> gridLocation,
        bool overDropZone) noexcept;

    // itemOrigin 与 clickedCell 均属于 selection.container。
    [[nodiscard]]
    bool beginPointerPress(
        InventoryItemSelection selection,
        GridPosition itemOrigin,
        GridPosition clickedCell,
        MousePosition position) noexcept;

    [[nodiscard]]
    bool beginPointerPress(
        InventoryItemSelection selection,
        GridPosition itemOrigin,
        GridPosition clickedCell,
        MousePosition position,
        InventoryPointerItemGeometry geometry) noexcept;

    // Modifier-left locks quantity at pointer-down, then uses the same four
    // pixel drag threshold. The source stack is unchanged until commit.
    [[nodiscard]]
    bool beginQuantityPointerDrag(
        InventoryItemSelection selection,
        GridPosition itemOrigin,
        GridPosition clickedCell,
        MousePosition position,
        InventoryPointerItemGeometry geometry,
        std::uint32_t selectedQuantity) noexcept;

    // 仅修改拖拽候选；不提交 ItemInstance 或 GridInventory。
    [[nodiscard]]
    bool rotatePointerItemClockwise() noexcept;

    // 仅生成请求，不直接修改 GridInventory 或 GameplayWorld。
    [[nodiscard]]
    std::optional<InventoryPointerRequest> releasePointer(
        MousePosition position,
        std::optional<InventoryGridLocation> gridLocation,
        bool overDropZone) noexcept;

    // Esc 使用：取消手势，但保留持久选择与 hover。
    void cancelPointerGesture() noexcept;

    void clearSelection() noexcept;

    // 关闭背包时清除所有瞬态交互状态。
    void reset() noexcept;

private:
    std::optional<InventoryItemSelection> selectedItem_;
    std::optional<InventoryGridLocation> hoveredLocation_;
    InventoryDragGesture gesture_;
    std::optional<InventoryGridLocation> pointerPreviewLocation_;
    bool pointerOverDropZone_{};

    void resetPointerGesture() noexcept;
};
