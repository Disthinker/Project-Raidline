#pragma once

#include <cstddef>
#include <vector>

// 帧持续时间（秒）
struct AnimationFrame
{
    float durationSeconds{};
};

// 动画播放模式
enum class AnimationPlayMode
{
    Loop,
    Once
};

// 动画剪辑类
class AnimationClip
{
public:
    explicit AnimationClip(std::vector<AnimationFrame> frames);

    std::size_t frameCount() const;
    const AnimationFrame &frame(std::size_t index) const;

private:
    std::vector<AnimationFrame> frames_;
};

// 动画器类
class Animator
{
public:
    Animator(AnimationClip clip, AnimationPlayMode playMode);

    void update(float deltaTime);
    void reset();

    std::size_t currentFrameIndex() const;
    bool isFinished() const;

private:
    AnimationClip clip_;
    AnimationPlayMode playMode_;
    std::size_t currentFrameIndex_{0};
    float timeInCurrentFrame_{0.0f};
    bool finished_{false};
};