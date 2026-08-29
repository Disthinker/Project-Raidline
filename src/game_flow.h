#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "base_world.h"
#include "game_session.h"

enum class GameFlowState
{
    MainMenu,
    Base,
    Raid,
    RaidResult,
};

// SDL 无关的顶层游戏流程。它只仲裁屏幕级状态与 Raid 生命周期，
// GameSession 继续唯一拥有 Stash、GameplayWorld、结算和稳定 ID 高水位。
class GameFlow
{
public:
    GameFlow();

    explicit GameFlow(
        InventoryGridSize stashSize);

    [[nodiscard]]
    bool startGame() noexcept;

    void configurePersistence(std::filesystem::path directory);

    [[nodiscard]] bool startNewGame(std::string profileId);
    [[nodiscard]] bool continueGame();

    [[nodiscard]]
    bool deploy(
        MapDefinitionId mapDefinitionId = MapDefinitionId{"map.v0.test"},
        RaidIntelligenceLoadout intelligence = {},
        std::optional<std::string> selfRecoveryRecordId = std::nullopt,
        std::optional<RegionalOutpostDefinitionId>
            outpostRestorationId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            baseSiteClearanceId = std::nullopt,
        std::optional<RegionalBaseSiteDefinitionId>
            basePerimeterSweepId = std::nullopt,
        const RaidDeploymentProgressCallback &progress = {}) noexcept;

    void updateBase(
        const BaseInput &input,
        float deltaTime) noexcept;

    void closeBaseFacility() noexcept;

    [[nodiscard]] std::optional<BaseFacilityKind>
    activeBaseFacility() const noexcept;

    [[nodiscard]] BaseWorld &baseWorld() noexcept;
    [[nodiscard]] const BaseWorld &baseWorld() const noexcept;

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]]
    bool returnToBase() noexcept;

    [[nodiscard]]
    bool returnToMainMenu() noexcept;

    [[nodiscard]]
    GameFlowState state() const noexcept;

    [[nodiscard]]
    bool isRaidScreen() const noexcept;

    [[nodiscard]]
    GameSession &gameSession() noexcept;

    [[nodiscard]]
    const GameSession &gameSession() const noexcept;

private:
    GameSession gameSession_;
    BaseWorld baseWorld_;
    GameFlowState state_{GameFlowState::MainMenu};
    bool firstDeploymentPending_{true};
    bool persistentAlphaMode_{};
    std::optional<BaseFacilityKind> activeBaseFacility_;
};

[[nodiscard]]
const char *gameFlowStateName(
    GameFlowState state) noexcept;
