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

void GameFlow::configurePersistence(std::filesystem::path directory)
{
    gameSession_.configurePersistence(std::move(directory));
}

bool GameFlow::startNewGame(std::string profileId)
{
    if (state_ != GameFlowState::MainMenu ||
        !gameSession_.startNewProfile(std::move(profileId)))
    {
        return false;
    }
    state_ = GameFlowState::Base;
    activeBaseFacility_.reset();
    baseWorld_.resetAtMedicalPoint();
    return true;
}

bool GameFlow::continueGame()
{
    if (state_ != GameFlowState::MainMenu ||
        !gameSession_.continueProfile())
    {
        return false;
    }
    state_ = GameFlowState::Base;
    activeBaseFacility_.reset();
    baseWorld_.resetAtMedicalPoint();
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
        activeBaseFacility_.reset();
        state_ = GameFlowState::Raid;
        return true;
    }

    if (!gameSession_.startNextRaid())
    {
        return false;
    }

    state_ = GameFlowState::Raid;
    activeBaseFacility_.reset();
    return true;
}

void GameFlow::updateBase(
    const BaseInput &input,
    float deltaTime) noexcept
{
    if (state_ != GameFlowState::Base || activeBaseFacility_.has_value())
    {
        return;
    }
    activeBaseFacility_ = baseWorld_.update(input, deltaTime);
    if (activeBaseFacility_.has_value())
    {
        gameSession_.noteBaseFacility(*activeBaseFacility_);
    }
}

void GameFlow::closeBaseFacility() noexcept
{
    activeBaseFacility_.reset();
}

std::optional<BaseFacilityKind> GameFlow::activeBaseFacility() const noexcept
{
    return activeBaseFacility_;
}

BaseWorld &GameFlow::baseWorld() noexcept
{
    return baseWorld_;
}

const BaseWorld &GameFlow::baseWorld() const noexcept
{
    return baseWorld_;
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

    if (gameSession_.settlement().state() == RaidSettlementState::Extracted)
    {
        baseWorld_.resetAtRaidGate();
    }
    else
    {
        baseWorld_.resetAtMedicalPoint();
    }
    activeBaseFacility_.reset();
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
