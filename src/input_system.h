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
    Dodge,
    Interact,

    // 背包开关。
    ToggleInventory,

    // 背包 UI 输入。
    InventoryCancel
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

    // 背包鼠标快捷操作需要在 SDL 事件到达时记录 Ctrl 快照。
    // 左右 Ctrl 任意一个仍按下时都返回 true。
    [[nodiscard]]
    bool isControlPressed() const noexcept;

    [[nodiscard]]
    bool isShiftPressed() const noexcept;

    void endFrame();

private:
    std::unordered_set<GameAction>
        pressedActions_;

    std::unordered_set<GameAction>
        justPressedActions_;

    // 保留原始按键状态，使未映射为 GameAction 的修饰键仍可查询。
    std::unordered_set<SDL_Scancode>
        pressedScancodes_;

    [[nodiscard]]
    std::optional<GameAction>
    mapScancodeToAction(
        SDL_Scancode scancode) const;
};
