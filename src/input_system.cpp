#include "input_system.h"

void InputSystem::handleEvent(
    const SDL_Event &event)
{
    if (
        event.type != SDL_EVENT_KEY_DOWN &&
        event.type != SDL_EVENT_KEY_UP)
    {
        return;
    }

    const std::optional<GameAction> action =
        mapScancodeToAction(
            event.key.scancode);

    if (!action.has_value())
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        // 只有从“未按下”变为“按下”时，
        // 才产生一次 justPressed。
        if (!isActionPressed(*action))
        {
            justPressedActions_.insert(
                *action);
        }

        pressedActions_.insert(*action);
        return;
    }

    pressedActions_.erase(*action);
}

bool InputSystem::isActionPressed(
    GameAction action) const
{
    return pressedActions_.find(action) !=
           pressedActions_.end();
}

bool InputSystem::wasActionJustPressed(
    GameAction action) const
{
    return justPressedActions_.find(action) !=
           justPressedActions_.end();
}

void InputSystem::endFrame()
{
    // justPressed 只保留一帧。
    // pressed 状态等到对应 KeyUp 才清除。
    justPressedActions_.clear();
}

std::optional<GameAction>
InputSystem::mapScancodeToAction(
    SDL_Scancode scancode) const
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

    case SDL_SCANCODE_F:
        return GameAction::Interact;

    case SDL_SCANCODE_TAB:
        return GameAction::ToggleInventory;

    default:
        return std::nullopt;
    }
}