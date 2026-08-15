#pragma once

enum class RaidSessionState
{
    Preparing,
    InRaid,
    Extracting,
    Extracted,
    PlayerDead,
    RaidEnded,
};

struct RaidSessionConfig
{
    float raidDurationSeconds{180.0F};
    float extractionDurationSeconds{3.0F};
    bool hardTimeLimit{true};
};

class RaidSession
{
public:
    explicit RaidSession(
        RaidSessionConfig config = {});

    [[nodiscard]]
    bool start() noexcept;

    // playerInExtractionPoint is a frame-level observation supplied by the
    // world. Non-positive or non-finite delta time never advances clocks, but
    // entering or leaving the point still changes the extraction state.
    void update(
        float deltaTime,
        bool playerInExtractionPoint) noexcept;

    [[nodiscard]]
    bool markPlayerDead() noexcept;

    [[nodiscard]]
    RaidSessionState state() const noexcept;

    [[nodiscard]]
    bool isActive() const noexcept;

    [[nodiscard]]
    bool isTerminal() const noexcept;

    [[nodiscard]]
    float raidTimeRemaining() const noexcept;

    [[nodiscard]]
    float extractionTimeElapsed() const noexcept;

    [[nodiscard]]
    float extractionDuration() const noexcept;

    [[nodiscard]]
    float extractionProgress() const noexcept;

private:
    RaidSessionConfig config_;
    RaidSessionState state_{RaidSessionState::Preparing};
    float raidTimeRemaining_{};
    float extractionTimeElapsed_{};
};

[[nodiscard]]
const char *raidSessionStateName(
    RaidSessionState state) noexcept;
