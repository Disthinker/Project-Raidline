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
    persistentAlphaMode_ = false;
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
    persistentAlphaMode_ = true;
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
    persistentAlphaMode_ = true;
    state_ = gameSession_.recoveredAbandonedRaid()
        ? GameFlowState::RaidResult
        : GameFlowState::Base;
    activeBaseFacility_.reset();
    baseWorld_.resetAtMedicalPoint();
    return true;
}

bool GameFlow::deploy(
    MapDefinitionId mapDefinitionId,
    RaidIntelligenceLoadout intelligence,
    std::optional<std::string> selfRecoveryRecordId,
    std::optional<RegionalOutpostDefinitionId>
        outpostRestorationId,
    std::optional<RegionalBaseSiteDefinitionId>
        baseSiteClearanceId,
    std::optional<RegionalBaseSiteDefinitionId>
        basePerimeterSweepId) noexcept
{
    if (state_ != GameFlowState::Base)
    {
        return false;
    }

    if (persistentAlphaMode_)
    {
        std::uint64_t seed = profileStateFingerprint(
            gameSession_.profile()) ^ 0x726169646c696e65ULL;
        if (seed == 0)
        {
            seed = 1;
        }
        if (!gameSession_.deployAlpha(
                seed,
                std::move(mapDefinitionId),
                intelligence,
                std::move(selfRecoveryRecordId),
                std::move(outpostRestorationId),
                std::move(baseSiteClearanceId),
                std::move(basePerimeterSweepId)))
        {
            return false;
        }
        activeBaseFacility_.reset();
        state_ = GameFlowState::Raid;
        return true;
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

    const bool extracted = persistentAlphaMode_
        ? gameSession_.profile().lastRaidResult.has_value() &&
          gameSession_.profile().lastRaidResult->outcome ==
              RaidResultOutcome::Extracted
        : gameSession_.settlement().state() == RaidSettlementState::Extracted;
    if (extracted)
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

bool GameFlow::returnToMainMenu() noexcept
{
    if (state_ != GameFlowState::Base && state_ != GameFlowState::Raid)
    {
        return false;
    }
    // An active persistent Raid deliberately remains uncommitted in memory.
    // Continue reloads the pre-Raid save and follows the existing idempotent
    // rollback path; no implicit success/failure settlement occurs here.
    activeBaseFacility_.reset();
    state_ = GameFlowState::MainMenu;
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
