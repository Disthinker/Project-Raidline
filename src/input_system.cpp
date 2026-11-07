#include "input_system.h"

#include <algorithm>
#include <cmath>

double RaidReloadGesture::time(double now) noexcept {
    if (std::isfinite(now)) lastEventTime_ = std::max(lastEventTime_, now);
    return lastEventTime_;
}
std::vector<RaidReloadGestureEvent> RaidReloadGesture::press(double now, bool actionActive) {
    now=time(now);
    if(held_)return {};
    held_=true;
    if(hasFirstRelease_ && now-firstReleaseAt_<=0.25+1e-9) {
        consumed_=true;pending_=false;hasFirstRelease_=false;
        return {{RaidReloadGestureKind::UpgradeEmergency,now,gestureId_,now-firstReleaseAt_}};
    }
    hasFirstRelease_=false;consumed_=actionActive;pending_=!actionActive;
    gestureId_=++sequence_;
    if(!gestureId_)gestureId_=++sequence_;
    pressedAt_=now;wheel_=false;
    return {};
}
std::vector<RaidReloadGestureEvent> RaidReloadGesture::advance(double now) {
    // A render/update tick may run ahead of a queued SDL edge. It must never
    // rewrite the edge's timestamp or lengthen the double-tap interval.
    if(!std::isfinite(now))return {};
    if(held_ && pending_ && !consumed_ && !wheel_ && now-pressedAt_>=0.35-1e-9) {
        wheel_=true;
        return {{RaidReloadGestureKind::OpenWheel,pressedAt_+0.35,gestureId_}};
    }
    return {};
}
std::vector<RaidReloadGestureEvent> RaidReloadGesture::release(double now) {
    now=time(now);
    if(!held_)return {};
    auto result=advance(now);
    held_=false;
    if(!consumed_ && pending_) {
        // A delayed short release can arrive after advance opened a preview.
        // The physical hold duration still decides which action it requests.
        const bool longHold=now-pressedAt_>=0.35-1e-9;
        result.push_back({longHold?RaidReloadGestureKind::Selected:RaidReloadGestureKind::Automatic,now,gestureId_});
        hasFirstRelease_=!longHold;firstReleaseAt_=now;
    }
    consumed_=pending_=wheel_=false;
    return result;
}
void RaidReloadGesture::cancel(bool physicallyHeld) noexcept {
    held_=physicallyHeld;consumed_=physicallyHeld;pending_=wheel_=hasFirstRelease_=false;
}

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
        secondaryPointerPressed_ = false;
        secondaryPointerJustPressed_ = false;
        reloadTransitions_.clear();
        return;
    }

    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.button == SDL_BUTTON_RIGHT)
    {
        if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN && !secondaryPointerPressed_)
            secondaryPointerJustPressed_=true;
        secondaryPointerPressed_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
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
        if (!isActionPressed(*action) && !event.key.repeat)
        {
            justPressedActions_.insert(
                *action);
            if(*action==GameAction::Reload && !event.key.repeat)
                reloadTransitions_.push_back({true,static_cast<double>(event.common.timestamp?event.common.timestamp:SDL_GetTicksNS())/1e9});
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
        if(*action==GameAction::Reload)
            reloadTransitions_.push_back({false,static_cast<double>(event.common.timestamp?event.common.timestamp:SDL_GetTicksNS())/1e9});
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

bool InputSystem::isSecondaryPointerPressed() const noexcept
{
    return secondaryPointerPressed_;
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
    secondaryPointerJustPressed_ = false;
    reloadTransitions_.clear();
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

    case SDL_SCANCODE_B:
        return GameAction::CycleFireMode;

    case SDL_SCANCODE_4:
    case SDL_SCANCODE_KP_4:
        return GameAction::SelectThrowable;

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

    case SDL_SCANCODE_M:
        return GameAction::ToggleTacticalMap;

    case SDL_SCANCODE_U:
        return GameAction::WeaponLight;
    case SDL_SCANCODE_V:
        return GameAction::QuickMelee;

    case SDL_SCANCODE_ESCAPE:
        return GameAction::InventoryCancel;

    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        return GameAction::ScreenConfirm;

    default:
        return std::nullopt;
    }
}
