#pragma once

#include "game_session.h"
#include <chrono>
#include <thread>

// Existing final-state regressions await the real storage acknowledgement.
// Pending-state tests deliberately keep their own held writer and frame pump.
[[nodiscard]] inline bool waitForProfileUiDurability(GameSession &session)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    do {
        session.updateInventoryPersistence(0);
        const auto daily = session.baseDailyCheckpointStatus();
        const auto defense = session.baseDefenseCheckpointStatus();
        if (daily.failed || defense.failed) {
            // Consume a failure published just after the poll above too.
            session.updateInventoryPersistence(0);
            return false;
        }
        if (!session.profileUiCommitPending() && !session.inventoryPersistencePending() &&
            !daily.outstanding() && !defense.outstanding()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
