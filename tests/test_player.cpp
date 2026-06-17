#include <gtest/gtest.h>
#include "player.h"

TEST(PlayerTest, MoveRightChangesXPosition)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_D;
    input.handleEvent(event);

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f);

    EXPECT_GT(player.position().x, 100.0f);
    EXPECT_FLOAT_EQ(player.position().y, 100.0f);
}

TEST(PlayerTest, MoveUpChangesYPosition)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    input.handleEvent(event);

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f);

    EXPECT_FLOAT_EQ(player.position().x, 100.0f);
    EXPECT_LT(player.position().y, 100.0f);
}