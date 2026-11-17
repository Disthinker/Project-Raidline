#include "app.h"

#include <fmt/format.h>

namespace
{
constexpr SDL_FRect kOrderPanel{926, 20, 338, 168};
constexpr SDL_FRect kTeammateStatus{926, 20, 338, 32};
constexpr std::array<std::string_view, 4> kOrderLabelsEnglish{
    "Follow", "Hold position", "Hold fire", "Free fire"};
constexpr std::array<std::string_view, 4> kOrderLabelsChinese{
    "跟随", "原地掩护", "停火", "自由射击"};

SDL_FRect fireSupportOrderRect(std::size_t index)
{
    return {kOrderPanel.x + 12.0F + 162.0F * static_cast<float>(index % 2U),
            kOrderPanel.y + 53.0F + 40.0F * static_cast<float>(index / 2U), 152.0F, 34.0F};
}

int fireSupportOrderAction(Vec2 position)
{
    for (std::size_t index = 0; index < kOrderLabelsEnglish.size(); ++index)
        if (uiContains(fireSupportOrderRect(index), position))
            return static_cast<int>(index) + 1;
    return 0;
}
}

bool App::routeFireSupportInput(const SDL_Event &event)
{
    const bool keyPress = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    if (!fireSupportOrdersOpen_)
    {
        if (!keyPress || event.key.scancode != SDL_SCANCODE_F2 ||
            gameFlow_.state() != GameFlowState::Raid || !windowHasInputFocus_ ||
            pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || tacticalMapOpen_ ||
            quickWheelOpen() || selectedThrowable_ || developerWeaponPanelOpen_ ||
            developerRangePanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
            developerPerformanceOverlayOpen_ || raidSaveProtectionVisible() ||
            gameSession_.world().raidSession().state() != RaidSessionState::InRaid)
            return false;
        const auto teammate = gameSession_.world().fireSupport();
        if (!teammate || !teammate->alive()) return false;

        fireSupportOrdersOpen_ = true;
        fireSupportUiBlocksGameplayThisFrame_ = true;
        gameSession_.cancelPlayerControlIntent();
        gameSession_.cancelWeaponLightInput();
        gameSession_.cancelRaidFreightUnlock();
        gameSession_.cancelRaidFreightBreach();
        gameSession_.cancelRaidFuelExtraction();
        cancelMagazineWheel();
        input_.suppressPrimaryPointerUntilRelease();
        syncRaidPointerCapture();
        updateWeaponOpticPointerGate();
        return true;
    }

    if (keyPress && (event.key.scancode == SDL_SCANCODE_F2 ||
                     event.key.scancode == SDL_SCANCODE_ESCAPE))
    {
        closeFireSupportOrders();
        return true;
    }

    int chosenOrder = 0;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            const int action = fireSupportOrderAction({event.button.x, event.button.y});
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                fireSupportPress_.begin(action, {});
            else if (fireSupportPress_.release(action, {}))
                chosenOrder = action;
            input_.suppressPrimaryPointerUntilRelease();
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION && fireSupportPress_.action !=
        fireSupportOrderAction({event.motion.x, event.motion.y}))
        fireSupportPress_.cancel();
    else if (keyPress && event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= SDL_SCANCODE_4)
        chosenOrder = static_cast<int>(event.key.scancode - SDL_SCANCODE_1) + 1;
    else if (keyPress && event.key.scancode == SDL_SCANCODE_F9)
    {
        closeFireSupportOrders();
        return false;
    }

    if (chosenOrder != 0)
        if (const auto teammate = gameSession_.world().fireSupport(); teammate && teammate->alive())
        {
            auto movement = teammate->movementOrder;
            auto fire = teammate->fireOrder;
            switch (chosenOrder)
            {
            case 1: movement = FireSupportMovementOrder::Follow; break;
            case 2: movement = FireSupportMovementOrder::Hold; break;
            case 3: fire = FireSupportFireOrder::HoldFire; break;
            case 4: fire = FireSupportFireOrder::FreeFire; break;
            }
            gameSession_.world().setFireSupportOrders(movement, fire);
        }
    return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
        event.type == SDL_EVENT_MOUSE_WHEEL || event.type == SDL_EVENT_KEY_DOWN;
}

void App::closeFireSupportOrders()
{
    if (!fireSupportOrdersOpen_) return;
    fireSupportOrdersOpen_ = false;
    fireSupportUiBlocksGameplayThisFrame_ = true;
    fireSupportPress_.cancel();
    input_.suppressPrimaryPointerUntilRelease();
    if (input_.isSecondaryPointerPressed()) opticAimBlockedUntilRelease_ = true;
}

void App::renderFireSupportWorld()
{
    const auto teammate = gameSession_.world().fireSupport();
    if (!teammate || !teammate->alive() || !teammate->outdoor || !teammate->visible) return;

    const auto &bounds = teammate->bounds;
    const float middle = bounds.position.x + bounds.size.x * 0.5F;
    const float top = bounds.position.y;
    const SDL_FRect head{middle - 6.0F, top + 2.0F, 12.0F, 10.0F};
    const SDL_FRect vest{middle - 9.0F, top + 12.0F, 18.0F, bounds.size.y - 14.0F};
    SDL_SetRenderDrawColor(renderer_, 98, 170, 151, 255);
    SDL_RenderFillRect(renderer_, &head);
    SDL_SetRenderDrawColor(renderer_, 51, 91, 85, 255);
    SDL_RenderFillRect(renderer_, &vest);
    SDL_SetRenderDrawColor(renderer_, 164, 230, 208, 255);
    SDL_RenderRect(renderer_, &vest);

    if (!fireSupportOrdersOpen_ && !inventoryOverlayState_.isOpen() && teammate->inInteractionRange &&
        !tacticalMapOpen_ && !quickWheelOpen() && visibleWeaponOptic() != WeaponOpticMode::High &&
        !gameSession_.world().raidSpacePortalInteractionInRange() &&
        !gameSession_.world().ordinarySurvivorRescueInteractionInRange() &&
        !gameSession_.canCallSignalExtraction() && gameSession_.canTakeFireSupportMagazine() &&
        !gameSession_.nearbyRaidSceneContainer())
        renderWorldLabel(vest.x, top - 22.0F, "F: TAKE TEAMMATE MAGAZINE");
}

void App::renderFireSupportOrders()
{
    if (gameFlow_.state() != GameFlowState::Raid || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || tacticalMapOpen_ || developerWeaponPanelOpen_ ||
        developerRangePanelOpen_ || developerPerformanceOverlayOpen_ || raidSaveProtectionVisible())
        return;
    const auto teammate = gameSession_.world().fireSupport();
    if (!teammate) return;

    const auto language = uiTextRenderer_.language();
    const auto tr = [language](std::string_view text) { return localizeUiText(language, text); };
    const bool chinese = language == UiLanguage::SimplifiedChinese;
    const auto &labels = chinese ? kOrderLabelsChinese : kOrderLabelsEnglish;
    const auto slot = fireSupportOrdersOpen_ ? kOrderPanel : kTeammateStatus;
    uiFinalPanel(renderer_, slot, !teammate->alive());
    std::string summary = chinese ? "火力队友" : "Fire support";
    if (teammate->alive())
    {
        summary += fmt::format("  {}/{}", teammate->health, kFireSupportMaxHealth);
        if (!teammate->outdoor) summary += " · " + tr("WAITING OUTSIDE");
        else if (!fireSupportOrdersOpen_)
        {
            summary += fmt::format(" · {} / {}",
                labels[teammate->movementOrder == FireSupportMovementOrder::Follow ? 0U : 1U],
                labels[teammate->fireOrder == FireSupportFireOrder::HoldFire ? 2U : 3U]);
            summary += " · F2";
        }
    }
    else summary += " · " + tr("TEAMMATE KILLED");
    uiFinalText(renderer_, uiTextRenderer_, {slot.x + 12, slot.y + 6, slot.w - 24, 24}, summary, 13);
    if (!fireSupportOrdersOpen_ || !teammate->alive()) return;

    const std::array selected{teammate->movementOrder == FireSupportMovementOrder::Follow,
        teammate->movementOrder == FireSupportMovementOrder::Hold,
        teammate->fireOrder == FireSupportFireOrder::HoldFire,
        teammate->fireOrder == FireSupportFireOrder::FreeFire};
    for (std::size_t index = 0; index < labels.size(); ++index)
        renderFoundationButton(8950 + static_cast<int>(index), fireSupportOrderRect(index),
            fmt::format("{}  {}", index + 1U, labels[index]), true, false, selected[index], {}, 13);
    uiFinalText(renderer_, uiTextRenderer_,
        {slot.x + 12, slot.y + 138, slot.w - 24, 23}, "F2 / ESC CLOSE TEAMMATE ORDERS", 12);
}
