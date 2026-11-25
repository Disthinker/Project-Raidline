#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include "player.h"

// 向右移动
TEST(PlayerTest, MoveRightChangesXPosition)
{
    GameplayInput input{};

    input.moveRight = true;

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_GT(player.position().x, 100.0f);
    EXPECT_FLOAT_EQ(player.position().y, 100.0f);
}

// 向上移动
TEST(PlayerTest, MoveUpChangesYPosition)
{
    GameplayInput input{};

    input.moveUp = true;

    Player player(100.0f, 100.0f);
    player.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_FLOAT_EQ(player.position().x, 100.0f);
    EXPECT_LT(player.position().y, 100.0f);
}

TEST(PlayerTest, SprintMovesFiftyPercentFartherThanNormalMovement)
{
    GameplayInput normalInput{};
    normalInput.moveRight = true;
    GameplayInput sprintInput = normalInput;
    sprintInput.sprint = true;

    Player normalPlayer(100.0F, 100.0F);
    Player sprintingPlayer(100.0F, 100.0F);
    normalPlayer.update(normalInput, 0.5F, 1280.0F, 720.0F);
    sprintingPlayer.update(sprintInput, 0.5F, 1280.0F, 720.0F);

    const float normalDistance = normalPlayer.position().x - 100.0F;
    const float sprintDistance = sprintingPlayer.position().x - 100.0F;
    EXPECT_FLOAT_EQ(sprintDistance, normalDistance * 1.5F);
}

// 移动距离与 deltaTime 成比例
TEST(PlayerTest, MovementScalesWithDeltaTime)
{
    GameplayInput input{};

    input.moveRight = true;

    Player shortFramePlayer(100.0f, 100.0f);
    Player longFramePlayer(100.0f, 100.0f);

    shortFramePlayer.update(input, 0.5f, 1280.0f, 720.0f);
    longFramePlayer.update(input, 1.0f, 1280.0f, 720.0f);

    EXPECT_GT(longFramePlayer.position().x, shortFramePlayer.position().x);
}

// 斜向速度归一化
TEST(PlayerTest, DiagonalMovementHasSameSpeedAsStraightMovement)
{
    GameplayInput rightInput{};

    rightInput.moveRight = true;

    GameplayInput diagonalInput;

    diagonalInput.moveRight = true;
    diagonalInput.moveUp = true;

    Player rightPlayer(500.0f, 300.0f);
    Player diagonalPlayer(500.0f, 300.0f);

    rightPlayer.update(rightInput, 1.0f, 1280.0f, 720.0f);
    diagonalPlayer.update(diagonalInput, 1.0f, 1280.0f, 720.0f);

    const float rightDistance = rightPlayer.position().x - 500.0f;

    const float diagonalDx = diagonalPlayer.position().x - 500.0f;
    const float diagonalDy = diagonalPlayer.position().y - 300.0f;
    const float diagonalDistance = std::sqrt(
        diagonalDx * diagonalDx + diagonalDy * diagonalDy);

    EXPECT_NEAR(diagonalDistance, rightDistance, 0.001f);
}

// 测试左边界x
TEST(PlayerTest, MoveLeftStopsAtZero)
{
    GameplayInput leftInput{};
    leftInput.moveLeft = true;

    Player leftPlayer(1.0f, 100.0f);                     // 初始化玩家在左侧边界
    leftPlayer.update(leftInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(leftPlayer.position().x, 0.0f);
}

// 测试右边界x
TEST(PlayerTest, MoveRightStopsAtWorldRightEdge)
{
    GameplayInput rightInput{};
    rightInput.moveRight = true;

    Player rightPlayer(1270.0f, 100.0f);                   // 初始化玩家在右侧边界
    rightPlayer.update(rightInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(rightPlayer.position().x, 1280.0f - rightPlayer.size());
}

// 测试上边界y
TEST(PlayerTest, MoveUpStopsAtZero)
{
    GameplayInput upInput{};
    upInput.moveUp = true;

    Player upPlayer(1.0f, 1.0f);                     // 初始化玩家在上边界
    upPlayer.update(upInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(upPlayer.position().y, 0.0f);
}

// 测试下边界y
TEST(PlayerTest, MoveDownStopsAtWorldBottomEdge)
{
    GameplayInput downInput{};
    downInput.moveDown = true;

    Player downPlayer(1.0f, 710.0f);                     // 初始化玩家在下边界
    downPlayer.update(downInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(downPlayer.position().y, 720.0f - downPlayer.size());
}

// 无输入不移动
TEST(PlayerTest, NoInputDoesNotMoveInsideBounds)
{
    GameplayInput noInput{};
    Player noInputPlayer(640.0f, 360.0f);                 // 初始化玩家在中心位置
    noInputPlayer.update(noInput, 1.0f, 1280.0f, 720.0f); // 更新玩家位置

    EXPECT_FLOAT_EQ(noInputPlayer.position().y, 360.0f);
    EXPECT_FLOAT_EQ(noInputPlayer.position().x, 640.0f);
}

// 同时按A、D，不移动
TEST(PlayerTest, OppositeHorizontalInputsCancelMovement)
{
    GameplayInput oppositeInputs{};
    oppositeInputs.moveLeft = true;
    oppositeInputs.moveRight = true;
    Player oppositeInputPlayer(640.0f, 360.0f);                        // 初始化玩家在中心位置
    oppositeInputPlayer.update(oppositeInputs, 1.0f, 1280.0f, 720.0f); // 更新玩家位置
    // 验证玩家没有移动
    EXPECT_FLOAT_EQ(oppositeInputPlayer.position().y, 360.0f);
    EXPECT_FLOAT_EQ(oppositeInputPlayer.position().x, 640.0f);
}

// 初始化位置超出边界时，不按键即可修复位置
TEST(PlayerTest, ClampsBottomRightPositionEvenWithoutInput)
{
    GameplayInput inputs{};
    Player player(1300.0f, 750.0f);               // 初始化玩家在右下角外的位置
    player.update(inputs, 1.0f, 1280.0f, 720.0f); // 更新玩家位置
    // 验证玩家被限制在边界内
    EXPECT_FLOAT_EQ(player.position().y, 720.0f - player.size());
    EXPECT_FLOAT_EQ(player.position().x, 1280.0f - player.size());
}

// 初始 facingDirection 是 (0, -1)
TEST(PlayerTest, InitialFacingDirectionIsUp)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().y, -1.0f);
}

// 向右移动后 facingDirection 是 (1, 0)
TEST(PlayerTest, MoveRightUpdatesFacingDirection)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    inputs.moveRight = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 1.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().y, 0.0f);
}

// 向上移动后 facingDirection 是 (0, -1)
TEST(PlayerTest, MoveUpUpdatesFacingDirection)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    inputs.moveUp = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().y, -1.0f);
}

// W + D 后 facingDirection 长度约为 1
TEST(PlayerTest, DiagonalMovementUpdatesFacingDirectionToNormalizedDirection)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    inputs.moveUp = true;
    inputs.moveRight = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 1.0f / std::sqrt(2));
    EXPECT_FLOAT_EQ(player.facingDirection().y, -1.0f / std::sqrt(2));
}

// 无输入 update 后 facingDirection 保持不变
TEST(PlayerTest, NoInputKeepsPreviousFacingDirection)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    inputs.moveRight = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    inputs.moveRight = false;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 1.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().y, 0.0f);
}

// A + D 互相抵消时不更新 facingDirection
TEST(PlayerTest, OppositeHorizontalInputsDoNotChangeFacingDirection)
{
    GameplayInput inputs{};
    Player player(640.0f, 360.0f);
    inputs.moveDown = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    inputs.moveDown = false;
    inputs.moveLeft = true;
    inputs.moveRight = true;
    player.update(inputs, 1.0f, 1280.0f, 720.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().x, 0.0f);
    EXPECT_FLOAT_EQ(player.facingDirection().y, 1.0f);
}

namespace
{
    constexpr float kPlayerAnimationTestWorldWidth{1280.0f};
    constexpr float kPlayerAnimationTestWorldHeight{720.0f};

    // 作者的走路首帧持续 110 ms。
    constexpr float kBelowPlayerMoveFrameDuration{0.055f};
    constexpr float kPastPlayerMoveFrameDuration{0.110f};
}

// Player 初始时应为 Idle，移动动画停在第 0 帧。
TEST(PlayerTest, InitialAnimationStateIsIdle)
{
    Player player(640.0f, 360.0f);

    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// 有效的向右输入应让 Player 进入 Moving。
TEST(PlayerTest, MoveRightSetsMovingState)
{
    GameplayInput input{};
    input.moveRight = true;

    Player player(640.0f, 360.0f);

    player.update(
        input,
        0.01f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_TRUE(player.isMoving());

    // 0.01 秒小于首帧 0.11 秒，因此仍处于 frame 0。
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// 多次 update 的时间应在 Player 内部 Animator 中持续累积。
TEST(PlayerTest, MovementTimeAccumulatesAndAdvancesAnimationFrame)
{
    GameplayInput input{};
    input.moveRight = true;

    Player player(640.0f, 360.0f);

    player.update(
        input,
        kBelowPlayerMoveFrameDuration,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    ASSERT_TRUE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);

    player.update(
        input,
        kBelowPlayerMoveFrameDuration,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    // 0.055 + 0.055 = 0.110，恰好进入下一帧。
    EXPECT_EQ(player.currentAnimationFrameIndex(), 1u);
}

// 一直没有移动输入时，Player 保持 Idle，动画不应自行推进。
TEST(PlayerTest, NoInputKeepsIdleAnimationAtFirstFrame)
{
    GameplayInput noInput{};
    Player player(640.0f, 360.0f);

    player.update(
        noInput,
        1.0f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// 从 Moving 变为 Idle 时，移动动画应重置到第 0 帧。
TEST(PlayerTest, StoppingMovementResetsAnimation)
{
    GameplayInput moveRight{};
    moveRight.moveRight = true;

    Player player(640.0f, 360.0f);

    player.update(
        moveRight,
        kPastPlayerMoveFrameDuration,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    ASSERT_TRUE(player.isMoving());
    ASSERT_EQ(player.currentAnimationFrameIndex(), 1u);

    GameplayInput noInput{};

    player.update(
        noInput,
        0.01f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// 左右输入互相抵消后，没有有效移动方向，不应启动移动动画。
TEST(PlayerTest, OppositeHorizontalInputsDoNotStartMovementAnimation)
{
    GameplayInput input{};
    input.moveLeft = true;
    input.moveRight = true;

    Player player(640.0f, 360.0f);

    player.update(
        input,
        1.0f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

TEST(PlayerTest, StartsWithThreeHealth)
{
    const Player player(640.0f, 360.0f);

    EXPECT_EQ(player.health(), 3);
    EXPECT_EQ(player.maxHealth(), 3);
    EXPECT_FALSE(player.isDead());
}

TEST(PlayerTest, DamageUsesOwnedHealthState)
{
    Player player(640.0f, 360.0f);

    EXPECT_FALSE(player.takeDamage(1));
    EXPECT_EQ(player.health(), 2);
    EXPECT_FALSE(player.isDead());

    EXPECT_TRUE(player.takeDamage(2));
    EXPECT_EQ(player.health(), 0);
    EXPECT_TRUE(player.isDead());
    EXPECT_FALSE(player.isImpactSlowed());
}

TEST(PlayerTest, DamageAfterDeathDoesNotReportAnotherDeath)
{
    Player player(640.0f, 360.0f);

    ASSERT_TRUE(player.takeDamage(3));
    EXPECT_FALSE(player.takeDamage(1));
    EXPECT_EQ(player.health(), 0);
}

// 垂直移动同样属于 Moving；是否有对应美术资源由 App 渲染层决定。
TEST(PlayerTest, VerticalInputSetsMovingState)
{
    GameplayInput input{};
    input.moveUp = true;

    Player player(640.0f, 360.0f);

    player.update(
        input,
        0.01f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_TRUE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// 作者的走路循环有 8 帧，应能完整循环回到 frame 0。
TEST(PlayerTest, WalkingAnimationLoopsAcrossEightAuthoredFrames)
{
    GameplayInput input{};
    input.moveRight = true;

    Player player(640.0f, 360.0f);

    // 两个 450 ms 的支撑阶段组成完整循环。
    // 使用 0.91 秒可完整跨过一轮，并在 frame 0 中保留少量时间。
    player.update(
        input,
        0.91f,
        kPlayerAnimationTestWorldWidth,
        kPlayerAnimationTestWorldHeight);

    EXPECT_TRUE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 0u);
}

// Player 顶着边界继续按移动键时，位置虽然不再变化，
// 但仍存在移动意图，因此移动动画应该继续推进。
TEST(PlayerTest, MovementIntentAtBoundaryStillAdvancesAnimation)
{
    GameplayInput input{};
    input.moveLeft = true;

    Player player(0.0f, 100.0f);

    player.update(
        input,
        0.11f,
        1280.0f,
        720.0f);

    EXPECT_FLOAT_EQ(player.position().x, 0.0f);
    EXPECT_TRUE(player.isMoving());
    EXPECT_EQ(player.currentAnimationFrameIndex(), 1u);
}

// Player 向右移动后停止时，应回到 Idle 和 frame 0，
// 同时保留最后一次有效的向右朝向。
// 停步后使用单独的右向站姿。
TEST(
    PlayerTest,
    StoppingAfterRightMovementKeepsRightFacingAtFirstFrame)
{
    GameplayInput moveRight{};
    moveRight.moveRight = true;

    Player player(640.0f, 360.0f);

    player.update(
        moveRight,
        0.11f,
        1280.0f,
        720.0f);

    ASSERT_TRUE(player.isMoving());
    ASSERT_EQ(
        player.currentAnimationFrameIndex(),
        1u);
    ASSERT_FLOAT_EQ(
        player.facingDirection().x,
        1.0f);
    ASSERT_FLOAT_EQ(
        player.facingDirection().y,
        0.0f);

    GameplayInput noInput{};

    player.update(
        noInput,
        0.01f,
        1280.0f,
        720.0f);

    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(
        player.currentAnimationFrameIndex(),
        0u);
    EXPECT_FLOAT_EQ(
        player.facingDirection().x,
        1.0f);
    EXPECT_FLOAT_EQ(
        player.facingDirection().y,
        0.0f);
}

TEST(PlayerTest, ExplicitAimDirectionIsNormalized)
{
    Player player{100.0F, 100.0F};

    ASSERT_TRUE(player.faceDirection(Vec2{3.0F, 4.0F}));
    EXPECT_FLOAT_EQ(player.facingDirection().x, 0.6F);
    EXPECT_FLOAT_EQ(player.facingDirection().y, 0.8F);
}

TEST(PlayerTest, ZeroAimDirectionPreservesPreviousFacing)
{
    Player player{100.0F, 100.0F};
    ASSERT_TRUE(player.faceDirection(Vec2{1.0F, 0.0F}));

    EXPECT_FALSE(player.faceDirection(Vec2{}));
    EXPECT_FLOAT_EQ(player.facingDirection().x, 1.0F);
    EXPECT_FLOAT_EQ(player.facingDirection().y, 0.0F);
}

TEST(PlayerTest, ControlSuppressesMovementForTheWholeExpiryFrame)
{
    Player player{100.0F, 100.0F};
    ASSERT_TRUE(player.applyControl(0.50F));

    GameplayInput input{};
    input.moveRight = true;
    player.update(input, 0.50F, 1280.0F, 720.0F);

    EXPECT_FLOAT_EQ(player.position().x, 100.0F);
    EXPECT_FALSE(player.isControlled());
    EXPECT_FLOAT_EQ(player.controlRemaining(), 0.0F);

    player.update(input, 0.10F, 1280.0F, 720.0F);
    EXPECT_GT(player.position().x, 100.0F);
}

TEST(PlayerTest, RepeatedControlKeepsTheLongerRemainingDuration)
{
    Player player{100.0F, 100.0F};
    ASSERT_TRUE(player.applyControl(0.75F));
    ASSERT_TRUE(player.applyControl(0.25F));
    EXPECT_FLOAT_EQ(player.controlRemaining(), 0.75F);

    ASSERT_TRUE(player.applyControl(1.00F));
    EXPECT_FLOAT_EQ(player.controlRemaining(), 1.00F);
}

TEST(PlayerTest, InvalidControlAndControlAfterDeathAreRejected)
{
    Player player{100.0F, 100.0F};

    EXPECT_FALSE(player.applyControl(0.0F));
    EXPECT_FALSE(player.applyControl(-1.0F));
    EXPECT_FALSE(player.applyControl(
        std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(player.isControlled());

    ASSERT_TRUE(player.takeDamage(player.health()));
    EXPECT_FALSE(player.applyControl(0.75F));
}

TEST(PlayerTest, NonLethalHitBrieflySlowsMovementThenFullyRecovers)
{
    Player player{100.0F, 100.0F};
    ASSERT_FALSE(player.takeDamage(1));
    ASSERT_TRUE(player.isImpactSlowed());
    EXPECT_FLOAT_EQ(player.impactSlowRemaining(), 0.18F);

    GameplayInput moveRight{};
    moveRight.moveRight = true;
    player.update(moveRight, 0.10F, 1280.0F, 720.0F);
    EXPECT_NEAR(player.position().x, 106.72F, 0.0001F);
    EXPECT_NEAR(player.impactSlowRemaining(), 0.08F, 0.0001F);

    player.update(moveRight, 0.08F, 1280.0F, 720.0F);
    EXPECT_FALSE(player.isImpactSlowed());
    const float slowedPosition = player.position().x;

    player.update(moveRight, 0.10F, 1280.0F, 720.0F);
    EXPECT_NEAR(
        player.position().x - slowedPosition,
        24.0F,
        0.0001F);
}

TEST(PlayerTest, EachAuthoredWalkFrameHasItsOwnStartAndLandsOnTheOppositeFoot)
{
    constexpr std::array starts{.001F, .110F, .225F, .335F, .450F, .560F, .675F, .785F};
    for (const bool left : {false, true})
    {
        for (std::size_t i = 0; i < starts.size(); ++i)
        {
            SCOPED_TRACE(::testing::Message() << "left=" << left << ", walk frame=" << i);
            const bool near = i < 4;
            const auto phase = static_cast<std::uint8_t>(i % 4);
            const auto start = near ? PlayerMotionClip::WalkToRunNear : PlayerMotionClip::WalkToRunFar;
            Player player{640, 360};
            GameplayInput input{};
            input.moveLeft = left;
            input.moveRight = !left;
            auto step = [&](float dt) { player.update(input, dt, 1280, 720); };
            step(starts[i]);
            ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, i, left}));
            input.sprint = true;
            const float beforeX = player.position().x;
            step(.054F);
            ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{start, 0, left, phase}));
            EXPECT_NEAR(std::abs(player.position().x - beforeX), .054F * 360.0F, .0001F);

            // Crossing a portal relocates the player, then resolves aim in the new space.
            ASSERT_TRUE(player.setPosition({400, 200}));
            ASSERT_TRUE(player.faceDirection({left ? 1.0F : -1.0F, 0}));
            EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{start, 0, !left, phase}));
            step(.001F);
            ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{start, 1, left, phase}));
            step(.045F);
            ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Run, near ? 3U : 0U, left}));
            input.sprint = false;
            step(.001F);
            EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, near ? 4U : 0U, left}));
        }
    }
}

TEST(PlayerTest, AbortingALateWalkToRunRestoresTheSameWalkPoseAndClearsTheEntry)
{
    Player player{640, 360};
    GameplayInput input{};
    input.moveLeft = true;
    auto step = [&](float dt) { player.update(input, dt, 1280, 720); };
    step(.8F);
    ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 7, true}));
    input.sprint = true;
    step(.005F);
    ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::WalkToRunFar, 0, true, 3}));
    input.sprint = false;
    step(.005F);
    ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 7, true}));
    input.sprint = true;
    step(.05F);
    input.moveLeft = false;
    step(.001F);
    ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Idle, 0, true}));
    input.moveRight = true;
    step(.001F);
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::StandToRun, 0, false}));
}

TEST(PlayerTest, AuthoredWalkHonorsAllEightFrameDurations)
{
    GameplayInput input{};
    input.moveRight = true;
    Player player{640, 360};
    constexpr std::array<float, 8> durations{.110F, .115F, .110F, .115F, .110F, .115F, .110F, .115F};
    for (std::size_t i = 0; i < durations.size(); ++i)
    {
        player.update(input, durations[i] - .001F, 1280, 720);
        EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, i, false}));
        player.update(input, .001F, 1280, 720);
    }
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 0, false}));
}

TEST(PlayerTest, StandStartAndRunHonorEveryAuthoredFrameBoundary)
{
    Player player{150, 150};
    GameplayInput input{};
    input.moveRight = true;
    input.sprint = true;
    auto step = [&](float dt) { player.update(input, dt, 2000, 1200); };
    step(.054F);
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::StandToRun, 0, false}));
    step(.001F);
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::StandToRun, 1, false}));
    step(.055F);
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::StandToRun, 2, false}));
    step(.045F);
    constexpr std::array<float, 6> runSeconds{.09F, .115F, .09F, .09F, .115F, .09F};
    for (std::size_t i = 0; i < runSeconds.size(); ++i)
    {
        ASSERT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Run, i, false}));
        step(runSeconds[i]);
    }
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Run, 0, false}));
}

TEST(PlayerTest, LongUpdateCarriesTimeAcrossStartAndRunLoops)
{
    Player large{640, 360}, small{640, 360};
    GameplayInput input{};
    input.moveLeft = true;
    input.sprint = true;
    large.update(input, 1.45F, 1280, 720);
    for (int i = 0; i < 145; ++i)
        small.update(input, .010F, 1280, 720);
    EXPECT_EQ(large.motionFrame(), small.motionFrame());
    EXPECT_EQ(large.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Run, 1, true}));
}

TEST(PlayerTest, FinalAimSelectsSideWithoutAdvancingOrRestartingTheMotion)
{
    Player player{640, 360};
    GameplayInput input{};
    input.moveRight = true;
    player.update(input, .225F, 1280, 720, Player::FacingSource::ResolvedAim);
    ASSERT_TRUE(player.faceDirection({-1, 0}));
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 2, true}));
    player.update(input, .020F, 1280, 720, Player::FacingSource::ResolvedAim);
    ASSERT_TRUE(player.faceDirection({0, 1}));
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 2, true}));
    ASSERT_TRUE(player.faceDirection({1, 0}));
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Walk, 2, false}));
}

TEST(PlayerTest, StartingSprintCanBeCancelledAndControlledImmediately)
{
    Player player{640, 360};
    GameplayInput input{};
    input.moveLeft = true;
    input.sprint = true;
    player.update(input, .012F, 1280, 720);
    input.sprint = false;
    player.update(input, .010F, 1280, 720);
    ASSERT_EQ(player.motionFrame().clip, PlayerMotionClip::Walk);
    input.sprint = true;
    player.update(input, .030F, 1280, 720);
    ASSERT_EQ(player.motionFrame().clip, PlayerMotionClip::WalkToRunNear);
    ASSERT_TRUE(player.applyControl(.3F));
    EXPECT_EQ(player.motionFrame(), (PlayerMotionFrame{PlayerMotionClip::Idle, 0, true}));
    player.update(input, .2F, 1280, 720);
    EXPECT_FALSE(player.isMoving());
    EXPECT_EQ(player.motionFrame().clip, PlayerMotionClip::Idle);
}
