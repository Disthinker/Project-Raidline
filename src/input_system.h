#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <unordered_set>

enum class GameAction
{
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Fire,
    Dodge,
    Interact
};

class InputSystem
{
public:
    void handleEvent(const SDL_Event &event);

    [[nodiscard]]
    bool isActionPressed(GameAction action) const;

    [[nodiscard]]
    bool wasActionJustPressed(GameAction action) const;

    void endFrame();

private:
    std::unordered_set<GameAction> pressedActions_;
    std::unordered_set<GameAction> justPressedActions_;

    [[nodiscard]]
    std::optional<GameAction>
    mapScancodeToAction(SDL_Scancode scancode) const;
};