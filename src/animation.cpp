#include "animation.h"

#include <stdexcept>
#include <utility>

AnimationClip::AnimationClip(std::vector<AnimationFrame> frames)
    : frames_(std::move(frames))
{
    if (frames_.empty())
    {
        throw std::invalid_argument("AnimationClip requires at least one frame");
    }
    for (const AnimationFrame &frame : frames_)
    {
        if (frame.durationSeconds <= 0.0f)
        {
            throw std::invalid_argument("AnimationFrame duration must be greater than zero");
        }
    }
}

std::size_t AnimationClip::frameCount() const
{
    return frames_.size();
}

const AnimationFrame &AnimationClip::frame(std::size_t index) const
{
    return frames_.at(index);
}

Animator::Animator(
    AnimationClip clip,
    AnimationPlayMode playMode)
    : clip_(std::move(clip)),
      playMode_(playMode)
{
}

void Animator::update(float deltaTime)
{
    if (deltaTime <= 0.0f || finished_)
    {
        return;
    }
    timeInCurrentFrame_ += deltaTime;
    while (!finished_)
    {
        float currentDuration = clip_.frame(currentFrameIndex_).durationSeconds;
        if (timeInCurrentFrame_ < currentDuration)
            break;
        timeInCurrentFrame_ -= currentDuration;
        if (currentFrameIndex_ + 1 < clip_.frameCount())
        {
            currentFrameIndex_ += 1;
            continue;
        }
        if (playMode_ == AnimationPlayMode::Loop)
        {
            currentFrameIndex_ = 0;
            continue;
        }
        currentFrameIndex_ = clip_.frameCount() - 1;
        timeInCurrentFrame_ = 0;
        finished_ = true;
    }
}

void Animator::reset()
{
    timeInCurrentFrame_ = 0.0f;
    currentFrameIndex_ = 0;
    finished_ = false;
}

std::size_t Animator::currentFrameIndex() const
{
    return currentFrameIndex_;
}

bool Animator::isFinished() const
{
    return finished_;
}