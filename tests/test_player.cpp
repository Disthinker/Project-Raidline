#include <gtest/gtest.h>
#include "player.h"

// 向右移动
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

// 向上移动
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

// 移动距离与 deltaTime 成比例
TEST(PlayerTest, MovementScalesWithDeltaTime)
{
    InputSystem input;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_D;
    input.handleEvent(event);

    Player shortFramePlayer(100.0f, 100.0f);
    Player longFramePlayer(100.0f, 100.0f);

    shortFramePlayer.update(input, 0.5f);
    longFramePlayer.update(input, 1.0f);

    EXPECT_GT(longFramePlayer.position().x, shortFramePlayer.position().x);
}