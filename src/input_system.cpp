#include "input_system.h"

void InputSystem::handleEvent(
    const SDL_Event &event)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        pressedActions_.clear();
        justPressedActions_.clear();
        justReleasedActions_.clear();
        pressedScancodes_.clear();
        primaryPointerPhysicallyPressed_ = false;
        primaryPointerPressed_ = false;
        primaryPointerJustPressed_ = false;
        primaryPointerSuppressedUntilRelease_ = false;
        return;
    }

    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.button == SDL_BUTTON_LEFT)
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            primaryPointerPhysicallyPressed_ = false;
            primaryPointerPressed_ = false;
            primaryPointerSuppressedUntilRelease_ = false;
            return;
        }

        primaryPointerPhysicallyPressed_ = true;

        if (!primaryPointerSuppressedUntilRelease_ &&
            !primaryPointerPressed_)
        {
            primaryPointerPressed_ = true;
            primaryPointerJustPressed_ = true;
        }
        return;
    }

    if (
        event.type != SDL_EVENT_KEY_DOWN &&
        event.type != SDL_EVENT_KEY_UP)
    {
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        pressedScancodes_.insert(
            event.key.scancode);
    }
    else
    {
        pressedScancodes_.erase(
            event.key.scancode);
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
        //
        // Repeated KEY_DOWN events for a held key do not create
        // repeated justPressed transitions.
        if (!isActionPressed(*action))
        {
            justPressedActions_.insert(
                *action);
        }

        pressedActions_.insert(
            *action);

        return;
    }

    for (SDL_Scancode pressedScancode : pressedScancodes_)
    {
        if (mapScancodeToAction(pressedScancode) == action)
        {
            return;
        }
    }
    if (isActionPressed(*action))
    {
        justReleasedActions_.insert(*action);
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

bool InputSystem::wasActionJustReleased(
    GameAction action) const
{
    return justReleasedActions_.contains(action);
}

bool InputSystem::isControlPressed() const noexcept
{
    return pressedScancodes_.contains(
               SDL_SCANCODE_LCTRL) ||
           pressedScancodes_.contains(
               SDL_SCANCODE_RCTRL);
}

bool InputSystem::isShiftPressed() const noexcept
{
    return pressedScancodes_.contains(
               SDL_SCANCODE_LSHIFT) ||
           pressedScancodes_.contains(
               SDL_SCANCODE_RSHIFT);
}

bool InputSystem::isPrimaryPointerPressed() const noexcept
{
    return primaryPointerPressed_;
}

bool InputSystem::wasPrimaryPointerJustPressed() const noexcept
{
    return primaryPointerJustPressed_;
}

void InputSystem::suppressPrimaryPointerUntilRelease() noexcept
{
    primaryPointerPressed_ = false;
    primaryPointerJustPressed_ = false;
    primaryPointerSuppressedUntilRelease_ =
        primaryPointerPhysicallyPressed_;
}

void InputSystem::endFrame()
{
    // justPressed 只保留一帧。
    // pressed 状态等到对应 KeyUp 才清除。
    justPressedActions_.clear();
    justReleasedActions_.clear();
    primaryPointerJustPressed_ = false;
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

    case SDL_SCANCODE_R:
        return GameAction::Reload;

    case SDL_SCANCODE_5:
    case SDL_SCANCODE_KP_5:
        return GameAction::Heal;

    case SDL_SCANCODE_1:
    case SDL_SCANCODE_KP_1:
        return GameAction::SelectWeapon1;

    case SDL_SCANCODE_2:
    case SDL_SCANCODE_KP_2:
        return GameAction::SelectWeapon2;

    case SDL_SCANCODE_3:
    case SDL_SCANCODE_KP_3:
        return GameAction::SelectWeapon3;

    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        return GameAction::Sprint;

    case SDL_SCANCODE_E:
    case SDL_SCANCODE_F:
        return GameAction::Interact;

    case SDL_SCANCODE_TAB:
        return GameAction::ToggleInventory;

    case SDL_SCANCODE_ESCAPE:
        return GameAction::InventoryCancel;

    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return GameAction::ScreenConfirm;

    default:
        return std::nullopt;
    }
}
