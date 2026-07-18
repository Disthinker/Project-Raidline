#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "animation.h"

namespace
{
    // 0.125 == 1 / 8，可以被二进制浮点数精确表示，
    // 适合测试时间边界，减少 0.1f 累积造成的误差。
    constexpr float kFrameDuration{0.125f};
    constexpr float kHalfFrameDuration{0.0625f};

    std::vector<AnimationFrame> makeUniformFrames(
        std::size_t frameCount,
        float durationSeconds = kFrameDuration)
    {
        return std::vector<AnimationFrame>(
            frameCount,
            AnimationFrame{durationSeconds});
    }

    AnimationClip makeUniformClip(
        std::size_t frameCount,
        float durationSeconds = kFrameDuration)
    {
        return AnimationClip{
            makeUniformFrames(frameCount, durationSeconds)};
    }
}

// -----------------------------------------------------------------------------
// AnimationClip
// -----------------------------------------------------------------------------

// AnimationClip 应按原顺序保存所有帧。
TEST(AnimationClipTest, StoresFramesInOrder)
{
    AnimationClip clip{
        std::vector<AnimationFrame>{
            AnimationFrame{0.125f},
            AnimationFrame{0.25f},
            AnimationFrame{0.375f}}};

    ASSERT_EQ(clip.frameCount(), 3u);

    EXPECT_FLOAT_EQ(clip.frame(0).durationSeconds, 0.125f);
    EXPECT_FLOAT_EQ(clip.frame(1).durationSeconds, 0.25f);
    EXPECT_FLOAT_EQ(clip.frame(2).durationSeconds, 0.375f);
}

// 空帧表没有合法的 frame 0，应在构造时被拒绝。
TEST(AnimationClipTest, EmptyClipThrowsInvalidArgument)
{
    const std::vector<AnimationFrame> frames{};

    EXPECT_THROW(
        {
            AnimationClip clip{frames};
            (void)clip;
        },
        std::invalid_argument);
}

// duration 为 0 会让 Animator 的 while 循环无法消耗时间，必须拒绝。
TEST(AnimationClipTest, ZeroDurationThrowsInvalidArgument)
{
    const std::vector<AnimationFrame> frames{
        AnimationFrame{0.125f},
        AnimationFrame{0.0f}};

    EXPECT_THROW(
        {
            AnimationClip clip{frames};
            (void)clip;
        },
        std::invalid_argument);
}

// 负数 duration 没有合理动画语义，必须拒绝。
TEST(AnimationClipTest, NegativeDurationThrowsInvalidArgument)
{
    const std::vector<AnimationFrame> frames{
        AnimationFrame{0.125f},
        AnimationFrame{-0.125f}};

    EXPECT_THROW(
        {
            AnimationClip clip{frames};
            (void)clip;
        },
        std::invalid_argument);
}

// frame(index) 应进行边界检查，而不是发生未定义行为。
TEST(AnimationClipTest, InvalidFrameIndexThrowsOutOfRange)
{
    AnimationClip clip{makeUniformFrames(1)};

    EXPECT_THROW(clip.frame(1), std::out_of_range);
}

// -----------------------------------------------------------------------------
// Animator 基础状态
// -----------------------------------------------------------------------------

// Animator 初始时应位于第 0 帧，且尚未完成。
TEST(AnimatorTest, StartsAtFrameZero)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());
}

// 0 或负数 deltaTime 不应让动画倒退或推进。
TEST(AnimatorTest, NonPositiveDeltaTimeDoesNotAdvance)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    animator.update(0.0f);
    animator.update(-1.0f);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());
}

// 小于当前帧 duration 时，不应切换帧。
TEST(AnimatorTest, DeltaBelowFrameDurationKeepsCurrentFrame)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    animator.update(kHalfFrameDuration);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
}

// 两次 update 的时间必须能够累积。
TEST(AnimatorTest, AccumulatedDeltaAdvancesOneFrame)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    animator.update(kHalfFrameDuration);
    EXPECT_EQ(animator.currentFrameIndex(), 0u);

    animator.update(kHalfFrameDuration);
    EXPECT_EQ(animator.currentFrameIndex(), 1u);
}

// deltaTime 刚好等于当前帧 duration 时，应推进一帧。
TEST(AnimatorTest, DeltaEqualToFrameDurationAdvancesOneFrame)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    animator.update(kFrameDuration);

    EXPECT_EQ(animator.currentFrameIndex(), 1u);
}

// Animator 必须读取每一帧各自的 duration，不能假定所有帧相同。
TEST(AnimatorTest, UsesEachFramesOwnDuration)
{
    AnimationClip clip{
        std::vector<AnimationFrame>{
            AnimationFrame{0.125f},
            AnimationFrame{0.25f},
            AnimationFrame{0.375f}}};

    Animator animator{
        std::move(clip),
        AnimationPlayMode::Loop};

    // 消耗第 0 帧的 0.125 秒，进入第 1 帧，
    // 并在第 1 帧中保留 0.125 秒。
    animator.update(0.25f);

    EXPECT_EQ(animator.currentFrameIndex(), 1u);

    // 第 1 帧总时长为 0.25 秒，
    // 再增加 0.125 秒后刚好进入第 2 帧。
    animator.update(0.125f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
}

// 大 deltaTime 应通过 while 一次跨过多个帧。
TEST(AnimatorTest, LargeDeltaAdvancesMultipleFrames)
{
    Animator animator{
        makeUniformClip(4),
        AnimationPlayMode::Loop};

    // 0.3125 = 2 × 0.125 + 0.0625
    animator.update(0.3125f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_FALSE(animator.isFinished());
}

// -----------------------------------------------------------------------------
// Loop
// -----------------------------------------------------------------------------

// Loop 播放完最后一帧后，应回到第 0 帧。
TEST(AnimatorTest, LoopWrapsFromLastFrameToFirst)
{
    Animator animator{
        makeUniformClip(4),
        AnimationPlayMode::Loop};

    // 一轮总时长：4 × 0.125 = 0.5 秒。
    animator.update(0.5f);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());
}

// Loop 回绕后必须保留未消耗的剩余时间。
TEST(AnimatorTest, LoopPreservesRemainingTimeAfterWrap)
{
    Animator animator{
        makeUniformClip(4),
        AnimationPlayMode::Loop};

    // 完整一轮 0.5 秒，再在第 0 帧中播放 0.0625 秒。
    animator.update(0.5625f);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);

    // 如果上一次的 0.0625 秒被正确保留，
    // 再增加 0.0625 秒就应该进入第 1 帧。
    animator.update(0.0625f);

    EXPECT_EQ(animator.currentFrameIndex(), 1u);
}

// 一个 update 可以跨过多轮 Loop。
TEST(AnimatorTest, LoopCanCrossMultipleCycles)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    // 每帧 0.125 秒。
    // 1.0 秒会跨过 8 个帧边界。
    // 8 % 3 == 2，因此最终位于 frame 2。
    animator.update(1.0f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_FALSE(animator.isFinished());
}

// 只有一帧的 Loop 动画也必须安全，不能越界或卡死。
TEST(AnimatorTest, SingleFrameLoopRemainsOnFrameZero)
{
    Animator animator{
        makeUniformClip(1),
        AnimationPlayMode::Loop};

    animator.update(10.0f);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());
}

// -----------------------------------------------------------------------------
// Once
// -----------------------------------------------------------------------------

// Once 进入最后一帧时，最后一帧尚未播放完，所以不应立刻 finished。
TEST(AnimatorTest, OnceEntersLastFrameBeforeFinishing)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Once};

    // 0.25 秒刚好播放完前两帧，进入第 2 帧。
    animator.update(0.25f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_FALSE(animator.isFinished());
}

// Once 应在最后一帧也完整播放一个 duration 后才 finished。
TEST(AnimatorTest, OnceFinishesAfterLastFrameDuration)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Once};

    // 三帧总时长：3 × 0.125 = 0.375 秒。
    animator.update(0.375f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_TRUE(animator.isFinished());
}

// 超大的 deltaTime 也只能让 Once 停在最后一个合法索引。
TEST(AnimatorTest, LargeDeltaFinishesOnceAtLastFrame)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Once};

    animator.update(100.0f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_TRUE(animator.isFinished());
}

// Once 完成后，后续 update 不应再改变状态。
TEST(AnimatorTest, FinishedOnceIgnoresFurtherUpdates)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Once};

    animator.update(0.375f);

    ASSERT_EQ(animator.currentFrameIndex(), 2u);
    ASSERT_TRUE(animator.isFinished());

    animator.update(10.0f);

    EXPECT_EQ(animator.currentFrameIndex(), 2u);
    EXPECT_TRUE(animator.isFinished());
}

// 只有一帧的 Once 动画，也应先显示该帧一个完整 duration。
TEST(AnimatorTest, SingleFrameOnceFinishesAfterItsDuration)
{
    Animator animator{
        makeUniformClip(1),
        AnimationPlayMode::Once};

    animator.update(kHalfFrameDuration);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());

    animator.update(kHalfFrameDuration);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_TRUE(animator.isFinished());
}

// -----------------------------------------------------------------------------
// reset
// -----------------------------------------------------------------------------

// reset 应恢复 frame index，并清除之前残留的累计时间。
TEST(AnimatorTest, ResetClearsFrameIndexAndAccumulatedTime)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Loop};

    // 进入 frame 1，并在该帧内保留 0.0625 秒。
    animator.update(0.1875f);

    ASSERT_EQ(animator.currentFrameIndex(), 1u);

    animator.reset();

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());

    // reset 后累计时间应为 0。
    // 增加半帧时间后仍应留在 frame 0。
    animator.update(kHalfFrameDuration);

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
}

// reset 应清除 Once 的 finished 状态。
TEST(AnimatorTest, ResetClearsFinishedState)
{
    Animator animator{
        makeUniformClip(3),
        AnimationPlayMode::Once};

    animator.update(0.375f);

    ASSERT_TRUE(animator.isFinished());
    ASSERT_EQ(animator.currentFrameIndex(), 2u);

    animator.reset();

    EXPECT_EQ(animator.currentFrameIndex(), 0u);
    EXPECT_FALSE(animator.isFinished());
}