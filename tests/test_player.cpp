#include <gtest/gtest.h>
#include <cmath>
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

// 斜向速度归一化
TEST(PlayerTest, DiagonalMovementHasSameSpeedAsStraightMovement)
{
    InputSystem rightInput;

    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_D;
    rightInput.handleEvent(event);

    InputSystem diagonalInput;

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_D;
    diagonalInput.handleEvent(event);

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_W;
    diagonalInput.handleEvent(event);

    Player rightPlayer(100.0f, 100.0f);
    Player diagonalPlayer(100.0f, 100.0f);

    rightPlayer.update(rightInput, 1.0f);
    diagonalPlayer.update(diagonalInput, 1.0f);

    const float rightDistance = rightPlayer.position().x - 100.0f;

    const float diagonalDx = diagonalPlayer.position().x - 100.0f;
    const float diagonalDy = diagonalPlayer.position().y - 100.0f;
    const float diagonalDistance = std::sqrt(
        diagonalDx * diagonalDx + diagonalDy * diagonalDy
    );

    EXPECT_NEAR(diagonalDistance, rightDistance, 0.001f);
}