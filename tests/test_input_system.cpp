#include <gtest/gtest.h>

#include <array>

#include "input_system.h"
#include "pause_menu.h"
#include "raid_pointer_capture.h"

namespace
{
    SDL_Event makeKeyEvent(
        Uint32 eventType,
        SDL_Scancode scancode)
    {
        SDL_Event event{};
        event.type = eventType;
        event.key.scancode = scancode;
        return event;
    }

    SDL_Event makePrimaryPointerEvent(Uint32 eventType)
    {
        SDL_Event event{};
        event.type = eventType;
        event.button.button = SDL_BUTTON_LEFT;
        return event;
    }

    void expectNoActionPressed(
        const InputSystem &input)
    {
        constexpr std::array kActions{
            GameAction::MoveUp,
            GameAction::MoveDown,
            GameAction::MoveLeft,
            GameAction::MoveRight,
            GameAction::Fire,
            GameAction::Reload,
            GameAction::Heal,
            GameAction::SelectWeapon1,
            GameAction::SelectWeapon2,
            GameAction::SelectWeapon3,
            GameAction::Sprint,
            GameAction::Interact,
            GameAction::ToggleInventory,
            GameAction::ToggleTacticalMap,
            GameAction::InventoryCancel,
            GameAction::ScreenConfirm,
        };

        for (const GameAction action : kActions)
        {
            EXPECT_FALSE(input.isActionPressed(action));
            EXPECT_FALSE(input.wasActionJustPressed(action));
            EXPECT_FALSE(input.wasActionJustReleased(action));
        }
    }
}

TEST(
    InputSystemTest,
    WKeyDownPressesMoveUp)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_W));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::MoveUp));
}

TEST(
    InputSystemTest,
    WKeyUpReleasesMoveUp)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_W));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_W));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::MoveUp));
}

TEST(
    InputSystemTest,
    SpaceKeyDownPressesFire)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_SPACE));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Fire));
}

TEST(
    InputSystemTest,
    IgnoresUnmappedKey)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_P));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::MoveUp));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::Fire));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::Sprint));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::Interact));
}

TEST(
    InputSystemTest,
    SpaceKeyDownSetsFireJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_SPACE));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Fire));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::Fire));
}

TEST(
    InputSystemTest,
    EndFrameClearsJustPressedButKeepsPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_SPACE));

    input.endFrame();

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Fire));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::Fire));
}

TEST(
    InputSystemTest,
    RepeatedFireKeyDownWhileHeldDoesNotRetrigger)
{
    InputSystem input;

    const SDL_Event keyDown =
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_SPACE);

    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::Fire));

    input.endFrame();
    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Fire));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::Fire));
}

TEST(
    InputSystemTest,
    FKeyDownSetsInteractJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_F));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Interact));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::Interact));
}

TEST(InputSystemTest, RaidResourceKeysMapToReloadAndHeal)
{
    InputSystem input;
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_R));
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_5));

    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Reload));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Heal));
}

TEST(InputSystemTest, NumberKeysSelectThreeWeaponSlots)
{
    InputSystem input;
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_1));
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_2));
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_3));

    EXPECT_TRUE(input.wasActionJustPressed(GameAction::SelectWeapon1));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::SelectWeapon2));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::SelectWeapon3));
}

TEST(InputSystemTest, HealReleaseIsReportedForOneFrame)
{
    InputSystem input;
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_5));
    input.endFrame();
    input.handleEvent(makeKeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_5));

    EXPECT_FALSE(input.isActionPressed(GameAction::Heal));
    EXPECT_TRUE(input.wasActionJustReleased(GameAction::Heal));
    input.endFrame();
    EXPECT_FALSE(input.wasActionJustReleased(GameAction::Heal));
}

TEST(
    InputSystemTest,
    EKeyDownAlsoSetsInteractJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_E));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Interact));
    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::Interact));
}

TEST(
    InputSystemTest,
    EndFrameClearsInteractJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_F));

    input.endFrame();

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Interact));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::Interact));
}

TEST(
    InputSystemTest,
    HoldingFDoesNotRetriggerInteract)
{
    InputSystem input;

    const SDL_Event keyDown =
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_F);

    input.handleEvent(keyDown);
    input.endFrame();
    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::Interact));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::Interact));
}

TEST(
    InputSystemTest,
    ReleasingAndPressingFAgainRetriggersInteract)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_F));

    input.endFrame();

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_F));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::Interact));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_F));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::Interact));
}

TEST(
    InputSystemTest,
    TabKeyDownSetsToggleInventoryJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_TAB));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::ToggleInventory));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::ToggleInventory));
}

TEST(InputSystemTest, MKeyTogglesTacticalMapOnlyOnceWhileHeld)
{
    InputSystem input;
    const SDL_Event keyDown = makeKeyEvent(
        SDL_EVENT_KEY_DOWN, SDL_SCANCODE_M);
    input.handleEvent(keyDown);
    EXPECT_TRUE(input.isActionPressed(GameAction::ToggleTacticalMap));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::ToggleTacticalMap));

    input.endFrame();
    input.handleEvent(keyDown);
    EXPECT_FALSE(input.wasActionJustPressed(GameAction::ToggleTacticalMap));
}

TEST(
    InputSystemTest,
    HoldingTabDoesNotRetriggerToggleInventory)
{
    InputSystem input;

    const SDL_Event keyDown =
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_TAB);

    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::ToggleInventory));

    input.endFrame();
    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::ToggleInventory));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::ToggleInventory));
}

TEST(
    InputSystemTest,
    ReleasingAndPressingTabAgainRetriggersToggle)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_TAB));

    input.endFrame();

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_TAB));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::ToggleInventory));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_TAB));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::ToggleInventory));
}

TEST(
    InputSystemTest,
    ArrowKeysAndNKeyAreUnmapped)
{
    InputSystem input;

    constexpr std::array kRemovedInventoryKeys{
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_N,
    };

    for (const SDL_Scancode scancode : kRemovedInventoryKeys)
    {
        input.handleEvent(
            makeKeyEvent(
                SDL_EVENT_KEY_DOWN,
                scancode));
    }

    expectNoActionPressed(input);
}

TEST(
    InputSystemTest,
    EscapeSetsInventoryCancelJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_ESCAPE));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryCancel));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryCancel));
}

TEST(
    InputSystemTest,
    EndFrameClearsInventoryCancelJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_ESCAPE));

    input.endFrame();

    // 按键尚未释放，所以 pressed 状态仍存在。
    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryCancel));

    // 但单帧 justPressed 已被清理。
    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryCancel));
}

TEST(
    InputSystemTest,
    ReturnKeySetsScreenConfirmOnlyOnceWhileHeld)
{
    InputSystem input;
    const SDL_Event keyDown =
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RETURN);

    input.handleEvent(keyDown);

    EXPECT_TRUE(input.isActionPressed(
        GameAction::ScreenConfirm));
    EXPECT_TRUE(input.wasActionJustPressed(
        GameAction::ScreenConfirm));

    input.endFrame();
    input.handleEvent(keyDown);

    EXPECT_TRUE(input.isActionPressed(
        GameAction::ScreenConfirm));
    EXPECT_FALSE(input.wasActionJustPressed(
        GameAction::ScreenConfirm));
}

TEST(
    InputSystemTest,
    KeypadEnterMapsToScreenConfirm)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_KP_ENTER));

    EXPECT_TRUE(input.isActionPressed(
        GameAction::ScreenConfirm));
    EXPECT_TRUE(input.wasActionJustPressed(
        GameAction::ScreenConfirm));
}

TEST(
    InputSystemTest,
    GameplayMovementKeyStillMapsNormally)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_W));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::MoveUp));
}

TEST(
    InputSystemTest,
    LeftControlTracksModifierWithoutGameplayAction)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_LCTRL));

    EXPECT_TRUE(input.isControlPressed());
    expectNoActionPressed(input);

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_LCTRL));

    EXPECT_FALSE(input.isControlPressed());
}

TEST(
    InputSystemTest,
    EitherControlKeyKeepsModifierPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_LCTRL));
    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RCTRL));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_LCTRL));

    EXPECT_TRUE(input.isControlPressed());

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_RCTRL));

    EXPECT_FALSE(input.isControlPressed());
}

TEST(InputSystemTest, EitherShiftKeyTracksModifierSnapshot)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_LSHIFT));
    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RSHIFT));

    EXPECT_TRUE(input.isShiftPressed());
    EXPECT_TRUE(input.isActionPressed(GameAction::Sprint));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_LSHIFT));
    EXPECT_TRUE(input.isShiftPressed());
    EXPECT_TRUE(input.isActionPressed(GameAction::Sprint));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_RSHIFT));
    EXPECT_FALSE(input.isShiftPressed());
    EXPECT_FALSE(input.isActionPressed(GameAction::Sprint));
}

TEST(InputSystemTest, PrimaryPointerDownCreatesHeldAndEdgeState)
{
    InputSystem input;
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));

    EXPECT_TRUE(input.isPrimaryPointerPressed());
    EXPECT_TRUE(input.wasPrimaryPointerJustPressed());

    input.endFrame();
    EXPECT_TRUE(input.isPrimaryPointerPressed());
    EXPECT_FALSE(input.wasPrimaryPointerJustPressed());
}

TEST(InputSystemTest, PrimaryPointerUpClearsHeldState)
{
    InputSystem input;
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_UP));

    EXPECT_FALSE(input.isPrimaryPointerPressed());
}

TEST(InputSystemTest, SuppressedUiClickCannotLeakBeforePhysicalRelease)
{
    InputSystem input;
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
    input.suppressPrimaryPointerUntilRelease();

    EXPECT_FALSE(input.isPrimaryPointerPressed());
    EXPECT_FALSE(input.wasPrimaryPointerJustPressed());

    input.endFrame();
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
    EXPECT_FALSE(input.isPrimaryPointerPressed());

    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_UP));
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
    EXPECT_TRUE(input.isPrimaryPointerPressed());
    EXPECT_TRUE(input.wasPrimaryPointerJustPressed());
}

TEST(InputSystemTest, FocusLossClearsKeyboardAndPointerState)
{
    InputSystem input;
    input.handleEvent(
        makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W));
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));
    SDL_Event secondary{};
    secondary.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    secondary.button.button = SDL_BUTTON_RIGHT;
    input.handleEvent(secondary);

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.handleEvent(focusLost);

    EXPECT_FALSE(input.isActionPressed(GameAction::MoveUp));
    EXPECT_FALSE(input.isPrimaryPointerPressed());
    EXPECT_FALSE(input.isSecondaryPointerPressed());
    EXPECT_FALSE(input.wasPrimaryPointerJustPressed());
}

TEST(InputSystemTest, RightPointerButtonDoesNotArmPrimaryFire)
{
    InputSystem input;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_RIGHT;

    input.handleEvent(event);

    EXPECT_TRUE(input.isSecondaryPointerPressed());
    EXPECT_FALSE(input.isPrimaryPointerPressed());
    EXPECT_FALSE(input.wasPrimaryPointerJustPressed());

    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    input.handleEvent(event);
    EXPECT_FALSE(input.isSecondaryPointerPressed());
}

TEST(InputSystemTest, PointerSuppressionDoesNotClearSpaceFire)
{
    InputSystem input;
    input.handleEvent(
        makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_SPACE));
    input.handleEvent(
        makePrimaryPointerEvent(SDL_EVENT_MOUSE_BUTTON_DOWN));

    input.suppressPrimaryPointerUntilRelease();

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Fire));
    EXPECT_FALSE(input.isPrimaryPointerPressed());
}

TEST(InputSystemTest, RaidPointerCaptureRequiresUnobstructedFocusedGameplay)
{
    RaidPointerCaptureContext context{
        true, true, false, false, false, false, false, true};
    EXPECT_TRUE(shouldCaptureRaidPointer(context));

    context.inventoryOpen = true;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
    context.inventoryOpen = false;
    context.medicalWheelOpen = true;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
    context.medicalWheelOpen = false;
    context.developerPanelOpen = true;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
    context.developerPanelOpen = false;
    context.pauseMenuOpen = true;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
    context.pauseMenuOpen = false;
    context.tacticalMapOpen = true;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
    context.tacticalMapOpen = false;
    context.windowHasInputFocus = false;
    EXPECT_FALSE(shouldCaptureRaidPointer(context));
}

TEST(InputSystemTest, PauseMenuEscapeReturnsFromSettingsThenCloses)
{
    PauseMenuState menu;
    EXPECT_FALSE(menu.isOpen());
    EXPECT_FALSE(menu.handleEscape());

    menu.open();
    menu.showSettings();
    ASSERT_TRUE(menu.isOpen());
    ASSERT_TRUE(menu.settingsOpen());
    EXPECT_TRUE(menu.handleEscape());
    EXPECT_TRUE(menu.isOpen());
    EXPECT_FALSE(menu.settingsOpen());
    EXPECT_TRUE(menu.handleEscape());
    EXPECT_FALSE(menu.isOpen());
}
