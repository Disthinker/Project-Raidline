#include "app.h"
#include "base_siege_warning_layout.h"
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

void App::syncBaseSiegeWarningUi()
{
    const auto &profile = gameSession_.profile();
    if (!profile.baseSiege.warningActive || gameSession_.baseDefenseActive())
    {
        siegeWarningProfileId_.clear();
        siegeWarningSequence_ = 0;
        siegeWarningDismissed_ = false;
        pendingSiegeWarningAction_.reset();
        return;
    }
    if (siegeWarningProfileId_ != profile.profileId ||
        siegeWarningSequence_ != profile.baseSiege.siegeSequence)
    {
        siegeWarningProfileId_ = profile.profileId;
        siegeWarningSequence_ = profile.baseSiege.siegeSequence;
        siegeWarningDismissed_ = false;
        pendingSiegeWarningAction_.reset();
    }
}

bool App::baseSiegeWarningVisible() const
{
    return gameFlow_.state() == GameFlowState::Base &&
           gameSession_.profile().baseSiege.warningActive &&
           !gameSession_.baseDefenseActive() && !siegeWarningDismissed_;
}

bool App::routeBaseSiegeWarningEvent(const SDL_Event &event)
{
    if (gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked()) return false;
    if (gameFlow_.state() != GameFlowState::Base ||
        !gameSession_.profile().baseSiege.warningActive || gameSession_.baseDefenseActive())
        return false;
    const bool visible = baseSiegeWarningVisible();
    const bool key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    const bool click = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       event.button.button == SDL_BUTTON_LEFT;
    std::optional<SiegeWarningAction> action;
    if (key && (event.key.scancode == SDL_SCANCODE_F6 ||
                (visible && event.key.scancode == SDL_SCANCODE_ESCAPE)))
        action = visible ? SiegeWarningAction::Dismiss : SiegeWarningAction::Reopen;
    else if (click && visible)
    {
        namespace layout = base_siege_warning_layout;
        if (inside(layout::close, event.button.x, event.button.y)) action = SiegeWarningAction::Dismiss;
        else if (inside(layout::manual, event.button.x, event.button.y)) action = SiegeWarningAction::Manual;
        else if (inside(layout::automatic, event.button.x, event.button.y)) action = SiegeWarningAction::Automatic;
    }
    else if (click && !relativeMouseModeActive_ &&
             inside(base_siege_warning_layout::banner, event.button.x, event.button.y))
        action = SiegeWarningAction::Reopen;
    if (action && !pendingSiegeWarningAction_)
        pendingSiegeWarningAction_ = action; // first action wins, including double clicks
    if (visible || action || siegeWarningBlocksGameplayThisFrame_)
    {
        siegeWarningBlocksGameplayThisFrame_ = true;
        input_.suppressPrimaryPointerUntilRelease();
        return true;
    }
    return false;
}

bool App::updateBaseSiegeWarning(float deltaTime)
{
    syncBaseSiegeWarningUi();
    const bool wasVisible = baseSiegeWarningVisible();
    const auto action = pendingSiegeWarningAction_;
    pendingSiegeWarningAction_.reset();
    if (action)
    {
        switch (*action)
        {
        case SiegeWarningAction::Dismiss: siegeWarningDismissed_ = true; break;
        case SiegeWarningAction::Reopen: siegeWarningDismissed_ = false; break;
        case SiegeWarningAction::Manual:
            uiMessage_ = gameSession_.startBaseRealtimeDefense(gameFlow_.baseWorld())
                ? "BASE DEFENSE STARTED | INTERCEPT OUTSIDE THE CORE"
                : gameSession_.persistenceMessage();
            break;
        case SiegeWarningAction::Automatic:
        {
            const auto receipt = gameSession_.executeBaseAutoDefense(
                nextProfileTransactionId("base-auto-defense"));
            uiMessage_ = receipt.succeeded
                ? receipt.outcome == BaseSiegeOutcome::Defended
                    ? "BASE DEFENDED | SAFETY PERIOD STARTED"
                    : "BASE DEFENSE FAILED SOFTLY | RECOVERY PERIOD STARTED"
                : receipt.message;
            gameAudio_.play(receipt.succeeded ? SoundEventId::UiConfirm : SoundEventId::UiDeny);
            break;
        }
        }
    }
    // A deliberate choice wins over the automatic preset at the same deadline.
    // On a rejected choice retain the warning/message for retry, without auto-settling.
    if (!action || *action == SiegeWarningAction::Dismiss || *action == SiegeWarningAction::Reopen)
        gameSession_.advanceBaseWorldClock(deltaTime);
    syncBaseSiegeWarningUi();
    const bool visible = baseSiegeWarningVisible();
    if (wasVisible || visible)
    {
        basePlacementState_.reset();
        baseFixedFacilityPlacementState_.reset();
        baseConstructionPanelOpen_ = false;
        resetBaseFinalDetailsUi();
        deactivateBaseBuildCamera();
        baseFacilityContextMenu_.reset();
        tacticalMapOpen_ = false;
        closeInventory();
        gameFlow_.closeBaseFacility();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        resetLostRecoveryUi();
        profileContextMenu_.reset();
    }
    if (wasVisible || visible || action || siegeWarningBlocksGameplayThisFrame_)
    {
        pendingBaseClicks_.clear();
        pendingBaseRightClicks_.clear();
        pendingInventoryUiEvents_.clear();
        pendingProfileRightClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return true;
    }
    return false;
}

bool App::handleBaseDefenseControls()
{
    if (!gameSession_.baseDefenseActive() && gameSession_.baseDailySaveBlocked())
    {
        for (const auto &click : pendingBaseClicks_)
            if (inside(kDefenseRetry, click.position.x, click.position.y))
                static_cast<void>(gameSession_.retryBaseDailySave());
        // Poll lag completion without advancing actors or consuming ammunition.
        static_cast<void>(gameSession_.updateBaseWorld(gameFlow_.baseWorld(), {}, 0.0F));
        pendingInventoryUiEvents_.clear();
        pendingBaseClicks_.clear();
        pendingBaseRightClicks_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return true;
    }
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
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const auto &f : gameFlow_.baseWorld().fortifications())
    {
        const auto r = f.footprint;
        const SDL_FRect area{r.position.x, r.position.y, r.size.x, r.size.y};
        SDL_SetRenderDrawColor(renderer_, 132, 96, 54, f.durability ? 235 : 75);
        SDL_RenderFillRect(renderer_, &area);
        SDL_SetRenderDrawColor(renderer_, 214, 176, 108, 255);
        SDL_RenderRect(renderer_, &area);
        const std::string label = std::string{f.durability ? "WOOD BARRICADE " : "BARRICADE REMNANT "} +
            std::to_string(f.durability) + "/" + std::to_string(f.maximumDurability);
        uiTextRenderer_.render(renderer_, area.x, area.y - 22.0F, label.c_str());
    }
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
    const auto language=uiTextRenderer_.language();const auto tr=[&](auto en,auto zh){return uiBaseManagementText(language,en,zh);};
    const bool active=gameSession_.baseDefenseActive();
    const bool blocked=active?gameSession_.baseDefenseSaveBlocked():gameSession_.baseDailySaveBlocked();
    if(blocked) {
        renderFoundationButton(20600,kDefenseRetry,tr("Save paused · retry","保存暂停 · 重试"),true,true,false,{},14);
        const auto box=uiBaseDefenseFeedbackRect();uiFinalPanel(renderer_,box,true);
        uiFinalTextAt(renderer_,uiTextRenderer_,box.x+14,box.y+10,tr("SAVE FAILED · world protected","保存失败 · 已暂停保护"),box.w-28,15);
        const auto &message=gameSession_.persistenceMessage();
        if(baseFinalFeedbackKey_!=message){baseFinalFeedbackKey_=message;baseFinalFeedbackScroll_=0;}
        renderUiBaseManagementFeedback(renderer_,uiTextRenderer_,{box.x+14,box.y+42,box.w-28,box.h-54},message,baseFinalFeedbackScroll_);
    }
    if(!active) {
        if(baseDefenseResultVisible_ && !blocked) {
            const auto facts=projectUiBaseDefenseResultFacts(gameSession_.profile().baseSiege,language);
            const auto box=uiBaseDefenseResultPanel();uiFinalPanel(renderer_,box,!facts.defended);
            uiFinalTextAt(renderer_,uiTextRenderer_,354,250,facts.title,572,22);
            uiFinalTextAt(renderer_,uiTextRenderer_,354,295,facts.summary,572,14,2);
            uiFinalTextAt(renderer_,uiTextRenderer_,354,340,facts.retained,572,14,2);
            uiFinalTextAt(renderer_,uiTextRenderer_,354,394,facts.losses,572,14);
            renderFoundationButton(20601,{510,440,260,44},tr("Continue","继续游戏"),true,false,false,{},17);
        }
        if(developerPerformanceOverlayOpen_) {
            const auto s=gameSession_.baseDailyCheckpointStatus();
            uiFinalTextAt(renderer_,uiTextRenderer_,16,580,fmt::format("BASE CHECKPOINT · Copy {:.2f}ms · JSON {:.2f}ms · Commit {:.2f}ms · Lag {:.0f}ms · {}/{}",
                s.lastCopyMilliseconds,s.lastWriteMetrics.serializationMilliseconds,s.lastWriteMetrics.commitMilliseconds,s.durabilityLagMilliseconds,s.durableGeneration,s.requestedGeneration),1248,12);
        }
        return;
    }
    const auto *state=gameFlow_.baseWorld().baseDefenseState();
    if(!state && gameSession_.profile().activeBaseDefense)state=&*gameSession_.profile().activeBaseDefense;
    if(!state)return;
    const auto facts=projectUiBaseDefenseFacts(*state,gameFlow_.baseWorld().playerPosition(),language);
    const SDL_FRect panel{330,74,620,116};uiFinalPanel(renderer_,panel,true);
    uiFinalTextAt(renderer_,uiTextRenderer_,345,82,facts.title+" · "+facts.breach,590,15);
    uiFinalTextAt(renderer_,uiTextRenderer_,345,108,facts.progress+" · "+facts.nextWave,590,13);
    uiFinalTextAt(renderer_,uiTextRenderer_,345,132,tr("Intercept outside core · Red-line contact counts as a breach","在核心外拦截 · 敌人触及红线即计突破"),590,13);
    uiFinalTextAt(renderer_,uiTextRenderer_,345,157,facts.approaches+tr(" · Hiding does not stop attack"," · 躲避不会阻止进攻"),590,12);
    uiFinalText(renderer_,uiTextRenderer_,kDefenseAbandon,tr("Esc · pause / abandon defense","Esc · 暂停 / 放弃防守"),12);
    if(developerPerformanceOverlayOpen_) {
        const auto m=gameFlow_.baseWorld().baseDefenseMetrics();const auto s=gameSession_.baseDefenseCheckpointStatus();
        uiFinalTextAt(renderer_,uiTextRenderer_,16,562,fmt::format("BASE SIM {:.2f}ms · Nav {:.2f}ms / {} · Actors {}",m.updateMilliseconds,m.navigationMilliseconds,m.navigationQueries,m.activeEnemies),1248,12);
        uiFinalTextAt(renderer_,uiTextRenderer_,16,580,fmt::format("BASE CHECKPOINT · Copy {:.2f}ms · JSON {:.2f}ms · Commit {:.2f}ms · Lag {:.0f}ms · {}/{}",
            s.lastCopyMilliseconds,s.lastWriteMetrics.serializationMilliseconds,s.lastWriteMetrics.commitMilliseconds,s.durabilityLagMilliseconds,s.durableGeneration,s.requestedGeneration),1248,12);
    }
    SDL_SetRenderDrawBlendMode(renderer_,SDL_BLENDMODE_NONE);
}
