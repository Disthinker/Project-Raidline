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

enum class RaidPhase
{
    Regular,
    HighRisk,
};

enum class RaidExtractionRoute
{
    None,
    Normal,
    EmergencySignal,
};

struct HighRiskRaidSessionConfig
{
    bool enabled{};
    float regularPhaseDurationSeconds{};
    float emergencyExtractionDurationSeconds{};
};

struct RaidSessionConfig
{
    float raidDurationSeconds{180.0F};
    float extractionDurationSeconds{3.0F};
    bool hardTimeLimit{true};
    HighRiskRaidSessionConfig highRisk;
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

    void update(
        float deltaTime,
        bool playerInNormalExtractionPoint,
        bool playerInEmergencyExtractionPoint) noexcept;

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

    [[nodiscard]] RaidPhase phase() const noexcept;

    [[nodiscard]] RaidExtractionRoute extractionRoute() const noexcept;

    [[nodiscard]] bool normalExtractionOpen() const noexcept;

    [[nodiscard]] bool normalExtractionGraceActive() const noexcept;

    [[nodiscard]] bool emergencyExtractionOpen() const noexcept;

    [[nodiscard]] bool enteredHighRiskLastUpdate() const noexcept;

    [[nodiscard]] float highRiskTimeElapsed() const noexcept;

private:
    RaidSessionConfig config_;
    RaidSessionState state_{RaidSessionState::Preparing};
    float raidTimeRemaining_{};
    float extractionTimeElapsed_{};
    float highRiskTimeElapsed_{};
    RaidPhase phase_{RaidPhase::Regular};
    RaidExtractionRoute extractionRoute_{RaidExtractionRoute::None};
    bool normalExtractionGraceActive_{};
    bool enteredHighRiskLastUpdate_{};

    [[nodiscard]] float activeExtractionDuration() const noexcept;
    void cancelExtraction() noexcept;
    void updateContinuousHighRisk(float deltaTime) noexcept;
};

[[nodiscard]]
const char *raidSessionStateName(
    RaidSessionState state) noexcept;

[[nodiscard]] const char *raidPhaseName(RaidPhase phase) noexcept;
