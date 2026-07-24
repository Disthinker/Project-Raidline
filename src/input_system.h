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
    InventoryUp,
    InventoryDown,
    InventoryLeft,
    InventoryRight,
    InventoryConfirm,
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

    void endFrame();

private:
    std::unordered_set<GameAction>
        pressedActions_;

    std::unordered_set<GameAction>
        justPressedActions_;

    [[nodiscard]]
    std::optional<GameAction>
    mapScancodeToAction(
        SDL_Scancode scancode) const;
};