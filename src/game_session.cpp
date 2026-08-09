#include "game_session.h"

#include <limits>
#include <utility>

GameSession::GameSession()
    : stash_{},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    InventoryGridSize stashSize)
    : stash_{stashSize},
      world_{std::make_unique<GameplayWorld>()}
{
}

GameSession::GameSession(
    std::vector<EnemySpawn> firstRaidEnemies)
    : stash_{},
      world_{std::make_unique<GameplayWorld>(
          std::move(firstRaidEnemies),
          3)}
{
}

void GameSession::update(
    const GameplayInput &input,
    float deltaTime)
{
    if (state_ == GameSessionState::BetweenRaids)
    {
        return;
    }

    world_->update(input, deltaTime);
    const RaidSettlementAttempt attempt =
        settlement_.settle(
            world_->raidSession().state(),
            world_->inventory(),
            stash_);

    if (attempt == RaidSettlementAttempt::Completed ||
        attempt == RaidSettlementAttempt::AlreadyCompleted)
    {
        state_ = GameSessionState::BetweenRaids;
        return;
    }

    state_ = attempt == RaidSettlementAttempt::Blocked
        ? GameSessionState::SettlementBlocked
        : GameSessionState::InRaid;
}

bool GameSession::startNextRaid() noexcept
{
    if (!canStartNextRaid() ||
        raidNumber_ ==
            std::numeric_limits<std::size_t>::max())
    {
        return false;
    }

    // 直接读取当前世界的高水位，避免调用方在本帧通过受控库存命令
    // 分配新 ID 后尚未来得及 update 时使用旧快照。
    const ItemInstanceId candidateFirstId =
        world_->nextItemInstanceId();

    std::unique_ptr<GameplayWorld> candidate;

    try
    {
        candidate =
            std::make_unique<GameplayWorld>(
                candidateFirstId);
    }
    catch (...)
    {
        return false;
    }

    world_.swap(candidate);
    settlement_ = RaidSettlement{};
    state_ = GameSessionState::InRaid;
    ++raidNumber_;
    return true;
}

GameplayWorld &GameSession::world() noexcept
{
    return *world_;
}

const GameplayWorld &GameSession::world() const noexcept
{
    return *world_;
}

Stash &GameSession::stash() noexcept
{
    return stash_;
}

const Stash &GameSession::stash() const noexcept
{
    return stash_;
}

const RaidSettlement &
GameSession::settlement() const noexcept
{
    return settlement_;
}

GameSessionState GameSession::state() const noexcept
{
    return state_;
}

bool GameSession::canStartNextRaid() const noexcept
{
    return state_ == GameSessionState::BetweenRaids &&
           settlement_.isComplete() &&
           raidNumber_ <
               std::numeric_limits<std::size_t>::max();
}

std::size_t GameSession::raidNumber() const noexcept
{
    return raidNumber_;
}

ItemInstanceId
GameSession::nextItemInstanceId() const noexcept
{
    return world_->nextItemInstanceId();
}

const char *gameSessionStateName(
    GameSessionState state) noexcept
{
    switch (state)
    {
    case GameSessionState::InRaid:
        return "InRaid";
    case GameSessionState::SettlementBlocked:
        return "SettlementBlocked";
    case GameSessionState::BetweenRaids:
        return "BetweenRaids";
    }

    return "Unknown";
}
