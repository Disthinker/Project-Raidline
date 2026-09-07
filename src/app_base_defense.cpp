#include "app.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace
{
constexpr SDL_FRect kDefenseAbandon{978.0F, 161.0F, 282.0F, 32.0F};
constexpr SDL_FRect kDefenseRetry{978.0F, 197.0F, 282.0F, 32.0F};
bool inside(SDL_FRect r, float x, float y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
} // namespace

bool App::handleBaseDefenseControls()
{
    if (gameSession_.baseDefenseActive())
        baseDefenseObservedActive_ = true;
    else if (baseDefenseObservedActive_)
    {
        baseDefenseObservedActive_ = false;
        baseDefenseResultVisible_ = true;
    }
    if (!gameSession_.baseDefenseActive())
    {
        baseDefenseAbandonArmed_ = false;
        if (baseDefenseResultVisible_)
        {
            for (const auto &click : pendingBaseClicks_)
                if (inside(SDL_FRect{510, 440, 260, 44}, click.position.x, click.position.y))
                    baseDefenseResultVisible_ = false;
            if (input_.wasActionJustPressed(GameAction::InventoryCancel))
                baseDefenseResultVisible_ = false;
            pendingBaseClicks_.clear();
            input_.suppressPrimaryPointerUntilRelease();
            return true;
        }
        return false;
    }
    bool consumed{};
    for (const auto &click : pendingBaseClicks_)
    {
        if (gameSession_.baseDefenseSaveBlocked() &&
            inside(kDefenseRetry, click.position.x, click.position.y))
        {
            consumed = true;
            static_cast<void>(gameSession_.retryBaseDefenseSave());
            uiMessage_ = gameSession_.persistenceMessage();
        }
        else
            baseDefenseAbandonArmed_ = false;
    }
    if (consumed)
    {
        pendingBaseClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
    }
    // Block gameplay while a storage error is unresolved, but leave F10,
    // Esc and the explicit retry/quit barriers available.
    if (gameSession_.baseDefenseSaveBlocked())
    {
        BaseInput paused;
        static_cast<void>(gameSession_.updateBaseWorld(gameFlow_.baseWorld(), paused, 0.0F));
        pendingInventoryUiEvents_.clear();
        pendingBaseClicks_.clear();
        pendingBaseRightClicks_.clear();
        return true;
    }
    return consumed;
}

void App::renderBaseDefenseWorld()
{
    const auto *state = gameFlow_.baseWorld().baseDefenseState();
    if (!state)
        return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const auto &corridor : state->corridors)
    {
        const SDL_FRect area{corridor.position.x, corridor.position.y, corridor.size.x,
                             corridor.size.y};
        SDL_SetRenderDrawColor(renderer_, 208, 123, 54, 32);
        SDL_RenderFillRect(renderer_, &area);
        SDL_SetRenderDrawColor(renderer_, 215, 147, 81, 140);
        SDL_RenderRect(renderer_, &area);
    }
    for (const auto &zone : state->coreDefenseZones)
    {
        const SDL_FRect area{zone.position.x, zone.position.y, zone.size.x, zone.size.y};
        SDL_SetRenderDrawColor(renderer_, 225, 68, 46, 110);
        SDL_RenderFillRect(renderer_, &area);
        SDL_SetRenderDrawColor(renderer_, 244, 167, 116, 240);
        SDL_RenderRect(renderer_, &area);
        uiTextRenderer_.render(renderer_, area.x, area.y - 22.0F, "CORE DEFENSE LINE");
    }
    for (const auto &wave : state->wavePlans)
    {
        SDL_SetRenderDrawColor(renderer_, 210, 158, 68, 100);
        for (std::size_t i{1}; i < wave.route.size(); ++i)
            SDL_RenderLine(renderer_, wave.route[i - 1].x, wave.route[i - 1].y, wave.route[i].x,
                           wave.route[i].y);
        uiTextRenderer_.render(renderer_, wave.entry.x, wave.entry.y - 30.0F, "SIEGE APPROACH");
    }
}

void App::renderBaseDefenseHud()
{
    if (!gameSession_.baseDefenseActive())
    {
        if (!baseDefenseResultVisible_)
            return;
        const auto &result = gameSession_.profile().baseSiege;
        const bool defended = result.lastOutcome == BaseSiegeOutcome::Defended;
        const SDL_FRect panel{330, 230, 620, 275};
        SDL_SetRenderDrawColor(renderer_, 28, 35, 30, 255);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 207, 208, 153, 255);
        SDL_RenderRect(renderer_, &panel);
        uiTextRenderer_.render(renderer_, 370, 260,
                               defended ? "BASE DEFENDED | SAFETY PERIOD STARTED"
                                        : "BASE DEFENSE FAILED SOFTLY | RECOVERY PERIOD STARTED");
        uiTextRenderer_.render(
            renderer_, 370, 305,
            defended ? "SUCCESS: MATERIAL +8, MORALE SUPPORT, 7 SAFE DAYS"
                     : "FAILURE: LIMITED PUBLIC LOSS, 12 SAFE DAYS, NO PERSONAL GEAR LOSS");
        uiTextRenderer_.render(renderer_, 370, 345,
                               "ITEMS KEPT IN PLACE | SPENT AMMO AND MEDICINE REMAIN SPENT");
        const auto losses = fmt::format("SECURITY SPENT {} | RESIDENTS LOST {}",
                                        result.lastSecuritySpent, result.lastPopulationLost);
        uiTextRenderer_.render(renderer_, 370, 381, losses.c_str());
        const SDL_FRect close{510, 440, 260, 44};
        SDL_SetRenderDrawColor(renderer_, 49, 86, 62, 255);
        SDL_RenderFillRect(renderer_, &close);
        uiTextRenderer_.render(renderer_, 530, 454, "CONTINUE GAME");
        return;
    }
    const auto *state = gameFlow_.baseWorld().baseDefenseState();
    if (!state)
        state = &*gameSession_.profile().activeBaseDefense;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_FRect panel{330.0F, 74.0F, 620.0F, 116.0F};
    SDL_SetRenderDrawColor(renderer_, 35, 24, 17, 232);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 244, 201, 142, 255);
    SDL_RenderRect(renderer_, &panel);
    std::size_t total{};
    for (const auto &wave : state->wavePlans)
        total += wave.enemyIds.size();
    const auto status = fmt::format(
        "BASE DEFENSE | WAVE {}/{} | PROCESSED {}/{} | BREACH {}/{}",
        std::min(state->currentWave + 1U, static_cast<std::uint32_t>(state->wavePlans.size())),
        state->wavePlans.size(), state->killedIds.size() + state->breachedIds.size(), total,
        state->breachedIds.size(), state->breachLimit);
    uiTextRenderer_.render(renderer_, 345.0F, 86.0F, status.c_str());
    uiTextRenderer_.render(renderer_, 345.0F, 109.0F,
                           "INTERCEPT OUTSIDE CORE | RED LINE CONTACT COUNTS AS A BREACH");
    uiTextRenderer_.render(renderer_, 345.0F, 132.0F,
                           "CORE IS SAFE; HIDING DOES NOT STOP THE ATTACK | ESC PAUSES");
    const auto &world = gameFlow_.baseWorld();
    const Vec2 player = world.playerPosition();
    std::string approaches{"APPROACHES: "};
    for (std::size_t i{}; i < std::min<std::size_t>(2, state->wavePlans.size()); ++i)
    {
        const auto d = state->wavePlans[i].entry;
        if (i)
            approaches += " / ";
        approaches += std::abs(d.x - player.x) > std::abs(d.y - player.y)
                          ? (d.x > player.x ? "EAST" : "WEST")
                          : (d.y > player.y ? "SOUTH" : "NORTH");
    }
    uiTextRenderer_.render(renderer_, 345.0F, 155.0F, approaches.c_str());
    if (state->currentWave < state->wavePlans.size())
    {
        const auto countdown = fmt::format("NEXT WAVE IN {}s",
            static_cast<unsigned>(std::ceil(std::max(0.0F,
                state->wavePlans[state->currentWave].releaseSeconds - state->elapsedSeconds))));
        uiTextRenderer_.render(renderer_, 690.0F, 155.0F, countdown.c_str());
    }
    uiTextRenderer_.render(renderer_, kDefenseAbandon.x + 10.0F, kDefenseAbandon.y + 9.0F,
                           "ESC MENU: SAVE / ABANDON DEFENSE");
    if (gameSession_.baseDefenseSaveBlocked())
    {
        SDL_SetRenderDrawColor(renderer_, 145, 66, 40, 255);
        SDL_RenderFillRect(renderer_, &kDefenseRetry);
        uiTextRenderer_.render(renderer_, kDefenseRetry.x + 10.0F, kDefenseRetry.y + 9.0F,
                               "SAVE PROTECTION PAUSED | RETRY");
        uiTextRenderer_.render(renderer_, 345.0F, 211.0F,
                               gameSession_.persistenceMessage().c_str());
    }
    if (developerPerformanceOverlayOpen_)
    {
        const auto runtime = world.baseDefenseMetrics();
        const auto simulation = fmt::format(
            "BASE SIM {:.2f}ms | NAV {:.2f}ms / {} | ACTORS {}", runtime.updateMilliseconds,
            runtime.navigationMilliseconds, runtime.navigationQueries, runtime.activeEnemies);
        uiTextRenderer_.render(renderer_, 16.0F, 562.0F, simulation.c_str());
        const auto s = gameSession_.baseDefenseCheckpointStatus();
        const auto text =
            fmt::format("BASE CHECKPOINT | COPY {:.2f}ms | JSON {:.2f}ms | COMMIT {:.2f}ms | LAG "
                        "{:.0f}ms | {}/{}",
                        s.lastCopyMilliseconds, s.lastWriteMetrics.serializationMilliseconds,
                        s.lastWriteMetrics.commitMilliseconds, s.durabilityLagMilliseconds,
                        s.durableGeneration, s.requestedGeneration);
        uiTextRenderer_.render(renderer_, 16.0F, 580.0F, text.c_str());
    }
}
