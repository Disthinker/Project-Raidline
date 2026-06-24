#pragma once

#include <SDL3/SDL.h>
#include <unordered_set>
#include <optional>

enum class GameAction
{
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Fire,
    Dodge
};

class InputSystem
{
public:
    void handleEvent(const SDL_Event &event);
    bool isActionPressed(GameAction action) const;
    bool wasActionJustPressed(GameAction action) const;
    void endFrame();

private:
    std::unordered_set<GameAction> pressedActions_;
    std::unordered_set<GameAction> justPressedActions_;

    std::optional<GameAction> mapScancodeToAction(SDL_Scancode scancode) const;
};