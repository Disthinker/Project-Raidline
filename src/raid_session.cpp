#include "raid_session.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

RaidSession::RaidSession(
    RaidSessionConfig config)
    : config_{config},
      raidTimeRemaining_{
          config.highRisk.enabled
              ? config.highRisk.regularPhaseDurationSeconds
              : config.raidDurationSeconds}
{
    if ((config_.hardTimeLimit && config_.highRisk.enabled) ||
        (config_.hardTimeLimit &&
         (!std::isfinite(config_.raidDurationSeconds) ||
          config_.raidDurationSeconds <= 0.0F)) ||
        !std::isfinite(config_.extractionDurationSeconds) ||
        config_.extractionDurationSeconds <= 0.0F ||
        (config_.highRisk.enabled &&
         (!std::isfinite(config_.highRisk.regularPhaseDurationSeconds) ||
          config_.highRisk.regularPhaseDurationSeconds <= 0.0F ||
          !std::isfinite(
              config_.highRisk.emergencyExtractionDurationSeconds) ||
          config_.highRisk.emergencyExtractionDurationSeconds <= 0.0F)))
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
    update(deltaTime, playerInExtractionPoint, false);
}

void RaidSession::update(
    float deltaTime,
    bool playerInNormalExtractionPoint,
    bool playerInEmergencyExtractionPoint) noexcept
{
    enteredHighRiskLastUpdate_ = false;
    if (!isActive())
    {
        return;
    }

    if (state_ == RaidSessionState::Extracting)
    {
        const bool remainsInRoute =
            extractionRoute_ == RaidExtractionRoute::Normal
                ? playerInNormalExtractionPoint
                : playerInEmergencyExtractionPoint;
        if (!remainsInRoute)
        {
            cancelExtraction();
        }
    }

    if (state_ == RaidSessionState::InRaid)
    {
        if (normalExtractionOpen() && playerInNormalExtractionPoint)
        {
            state_ = RaidSessionState::Extracting;
            extractionRoute_ = RaidExtractionRoute::Normal;
        }
        else if (emergencyExtractionOpen() &&
                 playerInEmergencyExtractionPoint)
        {
            state_ = RaidSessionState::Extracting;
            extractionRoute_ = RaidExtractionRoute::EmergencySignal;
        }
    }

    if (!std::isfinite(deltaTime) ||
        deltaTime <= 0.0F)
    {
        return;
    }

    if (config_.highRisk.enabled)
    {
        updateContinuousHighRisk(deltaTime);
        return;
    }

    if (state_ == RaidSessionState::Extracting)
    {
        const float extractionTimeRemaining =
            activeExtractionDuration() -
            extractionTimeElapsed_;

        // Whichever event occurs first within this update owns the terminal
        // result. An exact tie intentionally belongs to raid timeout.
        if ((!config_.hardTimeLimit ||
             extractionTimeRemaining < raidTimeRemaining_) &&
            deltaTime >= extractionTimeRemaining)
        {
            extractionTimeElapsed_ =
                activeExtractionDuration();
            if (config_.hardTimeLimit)
            {
                raidTimeRemaining_ -= extractionTimeRemaining;
            }
            state_ = RaidSessionState::Extracted;
            return;
        }
    }

    if (!config_.hardTimeLimit)
    {
        if (state_ == RaidSessionState::Extracting)
        {
            extractionTimeElapsed_ = std::min(
                activeExtractionDuration(),
                extractionTimeElapsed_ + deltaTime);
        }
        return;
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
                activeExtractionDuration(),
                extractionTimeElapsed_ + appliedTime);
    }

    if (raidTimeRemaining_ <= 0.0F)
    {
        raidTimeRemaining_ = 0.0F;
        cancelExtraction();
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
    extractionRoute_ = RaidExtractionRoute::None;
    normalExtractionGraceActive_ = false;
    state_ = RaidSessionState::PlayerDead;
    return true;
}

bool RaidSession::triggerHighRisk() noexcept
{
    if (!isActive() || !config_.highRisk.enabled ||
        phase_ != RaidPhase::Regular)
    {
        return false;
    }
    enterHighRisk();
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
    return activeExtractionDuration();
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
            activeExtractionDuration(),
        0.0F,
        1.0F);
}

RaidPhase RaidSession::phase() const noexcept
{
    return phase_;
}

RaidExtractionRoute RaidSession::extractionRoute() const noexcept
{
    return extractionRoute_;
}

bool RaidSession::normalExtractionOpen() const noexcept
{
    return phase_ == RaidPhase::Regular;
}

bool RaidSession::normalExtractionGraceActive() const noexcept
{
    return normalExtractionGraceActive_;
}

bool RaidSession::emergencyExtractionOpen() const noexcept
{
    return config_.highRisk.enabled && phase_ == RaidPhase::HighRisk;
}

bool RaidSession::enteredHighRiskLastUpdate() const noexcept
{
    return enteredHighRiskLastUpdate_;
}

float RaidSession::highRiskTimeElapsed() const noexcept
{
    return highRiskTimeElapsed_;
}

float RaidSession::activeExtractionDuration() const noexcept
{
    return extractionRoute_ == RaidExtractionRoute::EmergencySignal
        ? config_.highRisk.emergencyExtractionDurationSeconds
        : config_.extractionDurationSeconds;
}

void RaidSession::cancelExtraction() noexcept
{
    if (phase_ == RaidPhase::HighRisk &&
        extractionRoute_ == RaidExtractionRoute::Normal)
    {
        normalExtractionGraceActive_ = false;
    }
    state_ = RaidSessionState::InRaid;
    extractionRoute_ = RaidExtractionRoute::None;
    extractionTimeElapsed_ = 0.0F;
}

void RaidSession::enterHighRisk() noexcept
{
    raidTimeRemaining_ = 0.0F;
    phase_ = RaidPhase::HighRisk;
    enteredHighRiskLastUpdate_ = true;
    if (state_ == RaidSessionState::Extracting &&
        extractionRoute_ == RaidExtractionRoute::Normal)
    {
        normalExtractionGraceActive_ = true;
    }
}

void RaidSession::updateContinuousHighRisk(float deltaTime) noexcept
{
    float remaining = deltaTime;
    while (remaining > 0.0F && isActive())
    {
        const float extractionRemaining =
            state_ == RaidSessionState::Extracting
                ? std::max(
                      0.0F,
                      activeExtractionDuration() -
                          extractionTimeElapsed_)
                : std::numeric_limits<float>::infinity();

        if (phase_ == RaidPhase::Regular)
        {
            const float phaseRemaining = raidTimeRemaining_;
            if (extractionRemaining <= phaseRemaining &&
                extractionRemaining <= remaining)
            {
                extractionTimeElapsed_ = activeExtractionDuration();
                raidTimeRemaining_ = std::max(
                    0.0F,
                    raidTimeRemaining_ - extractionRemaining);
                state_ = RaidSessionState::Extracted;
                return;
            }

            const float applied = std::min(remaining, phaseRemaining);
            raidTimeRemaining_ = std::max(
                0.0F,
                raidTimeRemaining_ - applied);
            if (state_ == RaidSessionState::Extracting)
            {
                extractionTimeElapsed_ += applied;
            }
            remaining -= applied;

            if (raidTimeRemaining_ > 0.0F)
            {
                return;
            }

            enterHighRisk();
            continue;
        }

        if (state_ == RaidSessionState::Extracting &&
            extractionRemaining <= remaining)
        {
            extractionTimeElapsed_ = activeExtractionDuration();
            highRiskTimeElapsed_ += extractionRemaining;
            state_ = RaidSessionState::Extracted;
            return;
        }

        if (state_ == RaidSessionState::Extracting)
        {
            extractionTimeElapsed_ += remaining;
        }
        highRiskTimeElapsed_ += remaining;
        return;
    }
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

const char *raidPhaseName(RaidPhase phase) noexcept
{
    switch (phase)
    {
    case RaidPhase::Regular:
        return "REGULAR";
    case RaidPhase::HighRisk:
        return "HIGH RISK";
    }
    return "UNKNOWN";
}
