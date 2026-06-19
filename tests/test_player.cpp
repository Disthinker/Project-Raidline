#include <gtest/gtest.h>
#include <cmath>
#include "player.h"

void pressKey(InputSystem& input, SDL_Scancode scancode)
{
    SDL_Event event {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    input.handleEvent(event);
}

// 向右移动
TEST(PlayerTest, MoveRightChangesXPosition)
{
    InputSystem input;

    pressKey(input, SDL_SCANCODE_D);

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_GT(player.position().x, 100.0f);
    EXPECT_FLOAT_EQ(player.position().y, 100.0f);
}

// 向上移动
TEST(PlayerTest, MoveUpChangesYPosition)
{
    InputSystem input;

    pressKey(input, SDL_SCANCODE_W);

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_FLOAT_EQ(player.position().x, 100.0f);
    EXPECT_LT(player.position().y, 100.0f);
}

// 移动距离与 deltaTime 成比例
TEST(PlayerTest, MovementScalesWithDeltaTime)
{
    InputSystem input;

    pressKey(input, SDL_SCANCODE_D);

    Player shortFramePlayer(100.0f, 100.0f);
    Player longFramePlayer(100.0f, 100.0f);

    shortFramePlayer.update(input, 0.5f, 1280.0f, 720.0f);
    longFramePlayer.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_GT(longFramePlayer.position().x, shortFramePlayer.position().x);
}

// 斜向速度归一化
TEST(PlayerTest, DiagonalMovementHasSameSpeedAsStraightMovement)
{
    InputSystem rightInput;

    pressKey(rightInput, SDL_SCANCODE_D);

    InputSystem diagonalInput;

    pressKey(diagonalInput, SDL_SCANCODE_D);

    pressKey(diagonalInput, SDL_SCANCODE_W);

    Player rightPlayer(500.0f, 300.0f);
    Player diagonalPlayer(500.0f, 300.0f);

    rightPlayer.update(rightInput, 1.0f, 1280.0f, 720.0f);
    diagonalPlayer.update(diagonalInput, 1.0f, 1280.0f, 720.0f);

    const float rightDistance = rightPlayer.position().x - 500.0f;

    const float diagonalDx = diagonalPlayer.position().x - 500.0f;
    const float diagonalDy = diagonalPlayer.position().y - 300.0f;
    const float diagonalDistance = std::sqrt(
        diagonalDx * diagonalDx + diagonalDy * diagonalDy
    );

    EXPECT_NEAR(diagonalDistance, rightDistance, 0.001f);
}

// 测试左边界x
TEST(PlayerTest, MoveLeftStopsAtZero)
{
    InputSystem leftInput;
    pressKey(leftInput, SDL_SCANCODE_A);

    Player leftPlayer(1.0f, 100.0f); // 初始化玩家在左侧边界
    leftPlayer.update(leftInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(leftPlayer.position().x, 0.0f);  
}

// 测试右边界x
TEST(PlayerTest, MoveRightStopsAtWorldRightEdge)
{
    InputSystem rightInput;
    pressKey(rightInput, SDL_SCANCODE_D);

    Player rightPlayer(1270.0f, 100.0f); // 初始化玩家在右侧边界
    rightPlayer.update(rightInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(rightPlayer.position().x, 1280.0f - rightPlayer.size());
}

// 测试上边界y
TEST(PlayerTest, MoveUpStopsAtZero)
{
    InputSystem upInput;
    pressKey(upInput, SDL_SCANCODE_W);


    Player upPlayer(1.0f, 1.0f); // 初始化玩家在上边界
    upPlayer.update(upInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(upPlayer.position().y, 0.0f);
}

// 测试下边界y
TEST(PlayerTest, MoveDownStopsAtWorldBottomEdge)
{
    InputSystem downInput;
    pressKey(downInput, SDL_SCANCODE_S);

    Player downPlayer(1.0f, 710.0f); // 初始化玩家在下边界
    downPlayer.update(downInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(downPlayer.position().y, 720.0f - downPlayer.size());
}

// 无输入不移动
TEST(PlayerTest, NoInputDoesNotMoveInsideBounds)
{
    InputSystem noInput;
    Player noInputPlayer(640.0f, 360.0f); // 初始化玩家在中心位置
    noInputPlayer.update(noInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(noInputPlayer.position().y, 360.0f);
    EXPECT_FLOAT_EQ(noInputPlayer.position().x, 640.0f);
}

// 同时按A、D，不移动
TEST(PlayerTest, OppositeHorizontalInputsCancelMovement)
{
    InputSystem oppositeInputs;
    pressKey(oppositeInputs, SDL_SCANCODE_A); // 按下A键
    pressKey(oppositeInputs, SDL_SCANCODE_D); // 同时按下D键
    Player oppositeInputPlayer(640.0f, 360.0f); // 初始化玩家在中心位置
    oppositeInputPlayer.update(oppositeInputs, 1.0f, 1280.0f, 720.0f); // 更新玩家位置
    // 验证玩家没有移动
    EXPECT_FLOAT_EQ(oppositeInputPlayer.position().y, 360.0f);
    EXPECT_FLOAT_EQ(oppositeInputPlayer.position().x, 640.0f);
}

// 初始化位置超出边界时，不按键即可修复位置
TEST(PlayerTest, ClampsBottomRightPositionEvenWithoutInput)
{
    InputSystem inputs;
    Player player(1300.0f, 750.0f); // 初始化玩家在右下角外的位置
    player.update(inputs, 1.0f, 1280.0f, 720.0f); // 更新玩家位置
    // 验证玩家被限制在边界内
    EXPECT_FLOAT_EQ(player.position().y, 720.0f - player.size());
    EXPECT_FLOAT_EQ(player.position().x, 1280.0f - player.size());
}