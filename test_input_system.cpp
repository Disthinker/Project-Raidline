#include <gtest/gtest.h>
#include "input_system.h"

TEST(InputSystemTest, PressMoveUpOnWKeyDown)
{
    InputSystem input;
    
    SDL_Event event;
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
    
    SDL_Event event;
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_SPACE;
    
    input.handleEvent(event);

    EXPECT_TRUE(input.isActionPressed(GameAction::Fire));
}