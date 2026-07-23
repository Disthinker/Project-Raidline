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
