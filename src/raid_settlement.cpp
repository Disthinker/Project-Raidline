#include "raid_settlement.h"

namespace
{

    RaidSettlementSummary summarize(
        const GridInventory &inventory) noexcept
    {
        RaidSettlementSummary result{
            inventory.placedItems().size(),
            0};

        for (const PlacedItem &placed :
             inventory.placedItems())
        {
            result.unitCount +=
                placed.item.quantity();
        }

        return result;
    }

}

RaidSettlement::RaidSettlement() = default;

RaidSettlement::RaidSettlement(
    InventoryGridSize stashSize)
    : stash_(stashSize)
{
}

RaidSettlementAttempt RaidSettlement::settle(
    RaidSessionState raidState,
    GridInventory &playerInventory)
{
    if (isComplete())
    {
        return RaidSettlementAttempt::AlreadyCompleted;
    }

    switch (raidState)
    {
    case RaidSessionState::Preparing:
    case RaidSessionState::InRaid:
    case RaidSessionState::Extracting:
        return RaidSettlementAttempt::NotTerminal;

    case RaidSessionState::Extracted:
        summary_ = summarize(playerInventory);

        if (!stash_.tryStoreAll(playerInventory))
        {
            state_ = RaidSettlementState::Blocked;
            return RaidSettlementAttempt::Blocked;
        }

        state_ = RaidSettlementState::Extracted;
        return RaidSettlementAttempt::Completed;

    case RaidSessionState::PlayerDead:
        summary_ = summarize(playerInventory);
        playerInventory.clear();
        state_ = RaidSettlementState::PlayerDead;
        return RaidSettlementAttempt::Completed;

    case RaidSessionState::RaidEnded:
        summary_ = summarize(playerInventory);
        playerInventory.clear();
        state_ = RaidSettlementState::RaidEnded;
        return RaidSettlementAttempt::Completed;
    }

    return RaidSettlementAttempt::NotTerminal;
}

RaidSettlementState RaidSettlement::state() const noexcept
{
    return state_;
}

bool RaidSettlement::isComplete() const noexcept
{
    return state_ == RaidSettlementState::Extracted ||
           state_ == RaidSettlementState::PlayerDead ||
           state_ == RaidSettlementState::RaidEnded;
}

RaidSettlementSummary RaidSettlement::summary() const noexcept
{
    return summary_;
}

Stash &RaidSettlement::stash() noexcept
{
    return stash_;
}

const Stash &RaidSettlement::stash() const noexcept
{
    return stash_;
}

const char *raidSettlementStateName(
    RaidSettlementState state) noexcept
{
    switch (state)
    {
    case RaidSettlementState::Pending:
        return "Pending";
    case RaidSettlementState::Blocked:
        return "Blocked";
    case RaidSettlementState::Extracted:
        return "Extracted";
    case RaidSettlementState::PlayerDead:
        return "PlayerDead";
    case RaidSettlementState::RaidEnded:
        return "RaidEnded";
    }

    return "Unknown";
}
