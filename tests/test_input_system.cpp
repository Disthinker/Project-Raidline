#include <gtest/gtest.h>

#include "input_system.h"

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
            GameAction::Dodge));

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
    ArrowKeysMapToInventoryDirections)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_UP));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_DOWN));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_LEFT));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RIGHT));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryUp));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryDown));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryLeft));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryRight));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryUp));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryDown));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryLeft));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryRight));
}

TEST(
    InputSystemTest,
    ArrowKeyUpReleasesInventoryDirection)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_LEFT));

    ASSERT_TRUE(
        input.isActionPressed(
            GameAction::InventoryLeft));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_UP,
            SDL_SCANCODE_LEFT));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::InventoryLeft));
}

TEST(
    InputSystemTest,
    HoldingArrowDoesNotRetriggerInventoryDirection)
{
    InputSystem input;

    const SDL_Event keyDown =
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RIGHT);

    input.handleEvent(keyDown);

    ASSERT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryRight));

    input.endFrame();

    input.handleEvent(keyDown);

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryRight));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryRight));
}

TEST(
    InputSystemTest,
    EnterSetsInventoryConfirmJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RETURN));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryConfirm));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryConfirm));
}

TEST(
    InputSystemTest,
    KeypadEnterAlsoConfirmsInventory)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_KP_ENTER));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryConfirm));

    EXPECT_TRUE(
        input.wasActionJustPressed(
            GameAction::InventoryConfirm));
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
    EndFrameClearsInventoryJustPressed)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_UP));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_RETURN));

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_ESCAPE));

    input.endFrame();

    // 按键尚未释放，所以 pressed 状态仍存在。
    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryUp));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryConfirm));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::InventoryCancel));

    // 但单帧 justPressed 已被清理。
    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryUp));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryConfirm));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryCancel));
}

TEST(
    InputSystemTest,
    GameplayMovementKeyDoesNotTriggerInventoryDirection)
{
    InputSystem input;

    input.handleEvent(
        makeKeyEvent(
            SDL_EVENT_KEY_DOWN,
            SDL_SCANCODE_W));

    EXPECT_TRUE(
        input.isActionPressed(
            GameAction::MoveUp));

    EXPECT_FALSE(
        input.isActionPressed(
            GameAction::InventoryUp));

    EXPECT_FALSE(
        input.wasActionJustPressed(
            GameAction::InventoryUp));
}
