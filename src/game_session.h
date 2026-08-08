#pragma once

#include <cstddef>
#include <memory>

#include "gameplay_world.h"
#include "raid_settlement.h"
#include "stash.h"

enum class GameSessionState
{
    InRaid,
    SettlementBlocked,
    BetweenRaids,
};

// 进程内跨 Raid 组合根。它拥有长期 Stash、单局世界、单局结算和
// 不回退的稳定 ID 高水位；不负责 SDL 输入或渲染。
class GameSession
{
public:
    GameSession();

    explicit GameSession(
        InventoryGridSize stashSize);

    void update(
        const GameplayInput &input,
        float deltaTime);

    // 只有完整结算后才能开始下一局。候选世界完整构造成功后才交换，
    // 因此失败不会破坏旧终局、Stash、结算或 Raid 编号。
    [[nodiscard]]
    bool startNextRaid() noexcept;

    [[nodiscard]]
    GameplayWorld &world() noexcept;

    [[nodiscard]]
    const GameplayWorld &world() const noexcept;

    [[nodiscard]]
    Stash &stash() noexcept;

    [[nodiscard]]
    const Stash &stash() const noexcept;

    [[nodiscard]]
    const RaidSettlement &settlement() const noexcept;

    [[nodiscard]]
    GameSessionState state() const noexcept;

    [[nodiscard]]
    bool canStartNextRaid() const noexcept;

    [[nodiscard]]
    std::size_t raidNumber() const noexcept;

    [[nodiscard]]
    ItemInstanceId nextItemInstanceId() const noexcept;

private:
    Stash stash_;
    std::unique_ptr<GameplayWorld> world_;
    RaidSettlement settlement_;
    GameSessionState state_{GameSessionState::InRaid};
    std::size_t raidNumber_{1};
};

[[nodiscard]]
const char *gameSessionStateName(
    GameSessionState state) noexcept;
