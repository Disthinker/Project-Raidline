#include <gtest/gtest.h>
#include "input_system.h"

TEST(InputSystemTest, PressMoveUpOnWKeyDown)
{
    InputSystem input;
    
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    
    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::MoveUp));
}

TEST(InputSystemTest, PressMoveUpOnWKeyUp)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::MoveUp));

    event = {};
    event.type = SDL_EVENT_KEY_UP;
    event.key.scancode = SDL_SCANCODE_W;
    input.handleEvent(event);

    EXPECT_FALSE(input.isActionPressed(GameAction::MoveUp));
}

TEST(InputSystemTest, PressFireOnSpaceKeyDown)
{
    InputSystem input;
    
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    
    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
}

TEST(InputSystemTest, IgnoreUnmappedKey)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_P;

    input.handleEvent(event);

    EXPECT_FALSE(input.isActionPressed(GameAction::MoveUp));
    EXPECT_FALSE(input.isActionPressed(GameAction::Fire));
    EXPECT_FALSE(input.isActionPressed(GameAction::Dodge));
}


// Space KeyDown 产生 justPressed
TEST(InputSystemTest, SpaceKeyDownSetsFireJustPressed)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;

    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Fire));
}

// endFrame 后 justPressed 消失，但 pressed 仍保留
TEST(InputSystemTest, EndFrameClearsJustPressedButKeepsPressed)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;

    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Fire));

    input.endFrame();

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_FALSE(input.wasActionJustPressed(GameAction::Fire));
}

// 持续按住时重复 KeyDown 不再次 justPressed
TEST(InputSystemTest, RepeatedKeyDownWhileHeldDoesNotSetJustPressedAgain)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;

    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_TRUE(input.wasActionJustPressed(GameAction::Fire));

    input.endFrame();

    input.handleEvent(event); // 按住时重复 KeyDown 不再次 justPressed

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
    EXPECT_FALSE(input.wasActionJustPressed(GameAction::Fire));
}
