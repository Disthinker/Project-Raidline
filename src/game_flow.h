#pragma once

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

    [[nodiscard]]
    bool deploy() noexcept;

    void update(
        const GameplayInput &input,
        float deltaTime);

    [[nodiscard]]
    bool returnToBase() noexcept;

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
    GameFlowState state_{GameFlowState::MainMenu};
    bool firstDeploymentPending_{true};
};

[[nodiscard]]
const char *gameFlowStateName(
    GameFlowState state) noexcept;
