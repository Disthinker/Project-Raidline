#include "raid_session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

RaidSession::RaidSession(
    RaidSessionConfig config)
    : config_{config},
      raidTimeRemaining_{
          config.raidDurationSeconds}
{
    if (!std::isfinite(config_.raidDurationSeconds) ||
        config_.raidDurationSeconds <= 0.0F ||
        !std::isfinite(config_.extractionDurationSeconds) ||
        config_.extractionDurationSeconds <= 0.0F)
    {
        throw std::invalid_argument{
            "Raid session durations must be finite and positive"};
    }
}

bool RaidSession::start() noexcept
{
    if (state_ != RaidSessionState::Preparing)
    {
        return false;
    }

    state_ = RaidSessionState::InRaid;
    return true;
}

void RaidSession::update(
    float deltaTime,
    bool playerInExtractionPoint) noexcept
{
    if (!isActive())
    {
        return;
    }

    if (playerInExtractionPoint)
    {
        if (state_ == RaidSessionState::InRaid)
        {
            state_ = RaidSessionState::Extracting;
            extractionTimeElapsed_ = 0.0F;
        }
    }
    else if (state_ == RaidSessionState::Extracting)
    {
        state_ = RaidSessionState::InRaid;
        extractionTimeElapsed_ = 0.0F;
    }

    if (!std::isfinite(deltaTime) ||
        deltaTime <= 0.0F)
    {
        return;
    }

    if (state_ == RaidSessionState::Extracting)
    {
        const float extractionTimeRemaining =
            config_.extractionDurationSeconds -
            extractionTimeElapsed_;

        // Whichever event occurs first within this update owns the terminal
        // result. An exact tie intentionally belongs to raid timeout.
        if (extractionTimeRemaining < raidTimeRemaining_ &&
            deltaTime >= extractionTimeRemaining)
        {
            extractionTimeElapsed_ =
                config_.extractionDurationSeconds;
            raidTimeRemaining_ -=
                extractionTimeRemaining;
            state_ = RaidSessionState::Extracted;
            return;
        }
    }

    const float appliedTime =
        std::min(
            deltaTime,
            raidTimeRemaining_);

    raidTimeRemaining_ -= appliedTime;

    if (state_ == RaidSessionState::Extracting)
    {
        extractionTimeElapsed_ =
            std::min(
                config_.extractionDurationSeconds,
                extractionTimeElapsed_ + appliedTime);
    }

    if (raidTimeRemaining_ <= 0.0F)
    {
        raidTimeRemaining_ = 0.0F;
        extractionTimeElapsed_ = 0.0F;
        state_ = RaidSessionState::RaidEnded;
    }
}

bool RaidSession::markPlayerDead() noexcept
{
    if (!isActive())
    {
        return false;
    }

    extractionTimeElapsed_ = 0.0F;
    state_ = RaidSessionState::PlayerDead;
    return true;
}

RaidSessionState RaidSession::state() const noexcept
{
    return state_;
}

bool RaidSession::isActive() const noexcept
{
    return state_ == RaidSessionState::InRaid ||
           state_ == RaidSessionState::Extracting;
}

bool RaidSession::isTerminal() const noexcept
{
    return state_ == RaidSessionState::Extracted ||
           state_ == RaidSessionState::PlayerDead ||
           state_ == RaidSessionState::RaidEnded;
}

float RaidSession::raidTimeRemaining() const noexcept
{
    return raidTimeRemaining_;
}

float RaidSession::extractionTimeElapsed() const noexcept
{
    return extractionTimeElapsed_;
}

float RaidSession::extractionDuration() const noexcept
{
    return config_.extractionDurationSeconds;
}

float RaidSession::extractionProgress() const noexcept
{
    if (state_ == RaidSessionState::Extracted)
    {
        return 1.0F;
    }

    if (state_ != RaidSessionState::Extracting)
    {
        return 0.0F;
    }

    return std::clamp(
        extractionTimeElapsed_ /
            config_.extractionDurationSeconds,
        0.0F,
        1.0F);
}

const char *raidSessionStateName(
    RaidSessionState state) noexcept
{
    switch (state)
    {
    case RaidSessionState::Preparing:
        return "PREPARING";
    case RaidSessionState::InRaid:
        return "IN RAID";
    case RaidSessionState::Extracting:
        return "EXTRACTING";
    case RaidSessionState::Extracted:
        return "EXTRACTED";
    case RaidSessionState::PlayerDead:
        return "PLAYER DEAD";
    case RaidSessionState::RaidEnded:
        return "RAID ENDED";
    }

    return "UNKNOWN";
}
