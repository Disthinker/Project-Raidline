#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <unordered_set>

enum class GameAction
{
    // 游戏世界输入。
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Fire,
    Reload,
    Heal,
    SelectWeapon1,
    SelectWeapon2,
    SelectWeapon3,
    Sprint,
    Interact,

    // 背包开关。
    ToggleInventory,

    // 背包 UI 输入。
    InventoryCancel,

    // 主菜单、基地和 Raid 结果页的主操作确认。
    ScreenConfirm
};

class InputSystem
{
public:
    void handleEvent(
        const SDL_Event &event);

    [[nodiscard]]
    bool isActionPressed(
        GameAction action) const;

    [[nodiscard]]
    bool wasActionJustPressed(
        GameAction action) const;

    [[nodiscard]] bool wasActionJustReleased(
        GameAction action) const;

    // 背包鼠标快捷操作需要在 SDL 事件到达时记录 Ctrl 快照。
    // 左右 Ctrl 任意一个仍按下时都返回 true。
    [[nodiscard]]
    bool isControlPressed() const noexcept;

    [[nodiscard]]
    bool isShiftPressed() const noexcept;

    [[nodiscard]]
    bool isPrimaryPointerPressed() const noexcept;

    [[nodiscard]]
    bool wasPrimaryPointerJustPressed() const noexcept;

    [[nodiscard]]
    bool isSecondaryPointerPressed() const noexcept;

    // A UI layer calls this after consuming a left click. If the physical
    // button is still held, gameplay remains suppressed until its matching up.
    void suppressPrimaryPointerUntilRelease() noexcept;

    void endFrame();

private:
    std::unordered_set<GameAction>
        pressedActions_;

    std::unordered_set<GameAction>
        justPressedActions_;

    std::unordered_set<GameAction>
        justReleasedActions_;

    // 保留原始按键状态，使未映射为 GameAction 的修饰键仍可查询。
    std::unordered_set<SDL_Scancode>
        pressedScancodes_;

    bool primaryPointerPhysicallyPressed_{};
    bool primaryPointerPressed_{};
    bool primaryPointerJustPressed_{};
    bool primaryPointerSuppressedUntilRelease_{};
    bool secondaryPointerPressed_{};

    [[nodiscard]]
    std::optional<GameAction>
    mapScancodeToAction(
        SDL_Scancode scancode) const;
};
