#include "game_flow.h"

GameFlow::GameFlow() = default;

GameFlow::GameFlow(
    InventoryGridSize stashSize)
    : gameSession_{stashSize}
{
}

bool GameFlow::startGame() noexcept
{
    if (state_ != GameFlowState::MainMenu)
    {
        return false;
    }

    state_ = GameFlowState::Base;
    return true;
}

bool GameFlow::deploy() noexcept
{
    if (state_ != GameFlowState::Base)
    {
        return false;
    }

    if (firstDeploymentPending_)
    {
        if (gameSession_.state() !=
                GameSessionState::InRaid ||
            gameSession_.raidNumber() != 1U)
        {
            return false;
        }

        firstDeploymentPending_ = false;
        state_ = GameFlowState::Raid;
        return true;
    }

    if (!gameSession_.startNextRaid())
    {
        return false;
    }

    state_ = GameFlowState::Raid;
    return true;
}

void GameFlow::update(
    const GameplayInput &input,
    float deltaTime)
{
    if (state_ != GameFlowState::Raid)
    {
        return;
    }

    gameSession_.update(input, deltaTime);

    if (gameSession_.state() ==
        GameSessionState::BetweenRaids)
    {
        state_ = GameFlowState::RaidResult;
    }
}

bool GameFlow::returnToBase() noexcept
{
    if (state_ != GameFlowState::RaidResult ||
        gameSession_.state() !=
            GameSessionState::BetweenRaids)
    {
        return false;
    }

    state_ = GameFlowState::Base;
    return true;
}

GameFlowState GameFlow::state() const noexcept
{
    return state_;
}

bool GameFlow::isRaidScreen() const noexcept
{
    return state_ == GameFlowState::Raid;
}

GameSession &GameFlow::gameSession() noexcept
{
    return gameSession_;
}

const GameSession &
GameFlow::gameSession() const noexcept
{
    return gameSession_;
}

const char *gameFlowStateName(
    GameFlowState state) noexcept
{
    switch (state)
    {
    case GameFlowState::MainMenu:
        return "MainMenu";
    case GameFlowState::Base:
        return "Base";
    case GameFlowState::Raid:
        return "Raid";
    case GameFlowState::RaidResult:
        return "RaidResult";
    }

    return "Unknown";
}
