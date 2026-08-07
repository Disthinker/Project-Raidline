#pragma once

#include <cstddef>
#include <cstdint>

#include "raid_session.h"
#include "stash.h"

enum class RaidSettlementState
{
    Pending,
    Blocked,
    Extracted,
    PlayerDead,
    RaidEnded,
};

enum class RaidSettlementAttempt
{
    NotTerminal,
    Completed,
    AlreadyCompleted,
    Blocked,
};

struct RaidSettlementSummary
{
    std::size_t stackCount{};
    std::uint64_t unitCount{};

    friend bool operator==(
        const RaidSettlementSummary &,
        const RaidSettlementSummary &) = default;
};

// 将一次 Raid 的终局状态提交到局外所有权边界。
// 成功结算后状态保持不变；Blocked 保留原背包并允许重试。
class RaidSettlement
{
public:
    RaidSettlement();

    explicit RaidSettlement(
        InventoryGridSize stashSize);

    [[nodiscard]]
    RaidSettlementAttempt settle(
        RaidSessionState raidState,
        GridInventory &playerInventory);

    [[nodiscard]]
    RaidSettlementState state() const noexcept;

    [[nodiscard]]
    bool isComplete() const noexcept;

    [[nodiscard]]
    RaidSettlementSummary summary() const noexcept;

    [[nodiscard]]
    Stash &stash() noexcept;

    [[nodiscard]]
    const Stash &stash() const noexcept;

private:
    RaidSettlementState state_{
        RaidSettlementState::Pending};
    RaidSettlementSummary summary_{};
    Stash stash_;
};

[[nodiscard]]
const char *raidSettlementStateName(
    RaidSettlementState state) noexcept;
