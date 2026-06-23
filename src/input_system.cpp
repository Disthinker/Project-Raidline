#include "input_system.h"

void InputSystem::handleEvent(const SDL_Event &event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return;
    }

    auto actionOpt = mapScancodeToAction(event.key.scancode);
    if (!actionOpt.has_value())
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        if (!isActionPressed(*actionOpt))
        {
            justPressedActions_.insert(*actionOpt);
        }
        pressedActions_.insert(*actionOpt);
    }
    else if (event.type == SDL_EVENT_KEY_UP)
    {
        pressedActions_.erase(*actionOpt);
    }
}

bool InputSystem::isActionPressed(GameAction action) const
{
    return pressedActions_.find(action) != pressedActions_.end();
}

bool InputSystem::wasActionJustPressed(GameAction action) const
{
    return justPressedActions_.find(action) != justPressedActions_.end();
}
void InputSystem::endFrame()
{
    justPressedActions_.clear();
}

std::optional<GameAction> InputSystem::mapScancodeToAction(SDL_Scancode scancode) const
{
    switch (scancode)
    {
    case SDL_SCANCODE_W:
        return GameAction::MoveUp;
    case SDL_SCANCODE_S:
        return GameAction::MoveDown;
    case SDL_SCANCODE_A:
        return GameAction::MoveLeft;
    case SDL_SCANCODE_D:
        return GameAction::MoveRight;
    case SDL_SCANCODE_SPACE:
        return GameAction::Fire;
    case SDL_SCANCODE_LSHIFT:
        return GameAction::Dodge;
    default:
        return std::nullopt;
    }
}