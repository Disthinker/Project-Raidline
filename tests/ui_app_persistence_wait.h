#pragma once

#include "game_session.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <sstream>

namespace raidline::test {

inline std::string describeUiProfileChange(const ProfileState &before, const ProfileState &after)
{
    auto effectsOnly = before;
    effectsOnly.basePlayerEffects = after.basePlayerEffects;
    std::ostringstream text;
    text << "revision " << before.revision << " -> " << after.revision
         << ", health " << before.currentHealth << " -> " << after.currentHealth
         << ", minutes " << before.worldClock.elapsedWorldMinutes << " -> " << after.worldClock.elapsedWorldMinutes
         << ", transactions " << before.committedTransactions.size() << " -> " << after.committedTransactions.size()
         << ", only basePlayerEffects changed: "
         << (profileStateFingerprint(effectsOnly) == profileStateFingerprint(after));
    const auto oldBytes=serializeProfileEnvelope(before,publishedContentRegistry().contentVersion());
    const auto newBytes=serializeProfileEnvelope(after,publishedContentRegistry().contentVersion());
    const auto mismatch=std::mismatch(oldBytes.begin(),oldBytes.end(),newBytes.begin(),newBytes.end());
    const auto index=static_cast<std::size_t>(mismatch.first-oldBytes.begin());
    const auto start=index>100?index-100:0;
    text << "\nfirst serialized difference " << index
         << "\nbefore: " << oldBytes.substr(start,260)
         << "\nafter: " << newBytes.substr(start,260);
    return text.str();
}

// Legacy UI tests assert the final result of a completed click. Keep their
// real input route, then let the App consume durable (or protected failure)
// completions before reading authority. Pending-state tests use raw frames.
template<class Pending, class Pump>
bool waitForUiAppPersistence(GameSession &session, Pending clientPending, Pump pump)
{
    if (session.inventoryPersistencePending())
        session.requestInventoryPersistenceBarrier();
    const auto deadline = SDL_GetTicksNS() + 5'000'000'000ULL;
    const auto pending = [&] {
        return clientPending() || session.profileUiCommitPending() ||
            session.inventoryPersistencePending();
    };
    while (pending())
    {
        pump();
        // A protected failure is a completed attempt, never successful IO.
        // Retain it so the test's existing rejection assertions can inspect it.
        if (session.baseDailySaveBlocked() || session.baseDefenseSaveBlocked())
            return true;
        if (!pending())
            return true;
        if (SDL_GetTicksNS() >= deadline)
            return false;
        SDL_Delay(1);
    }
    return true;
}

} // namespace raidline::test
