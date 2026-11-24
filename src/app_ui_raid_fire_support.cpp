#include "app.h"

#include <algorithm>
#include <iterator>
#include <fmt/format.h>

namespace
{
constexpr SDL_FRect kOrderPanel{926, 20, 338, 200};
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

enum FireSupportTreatmentControl { OpenTreatment = 5, PreviousKit, NextKit, BackToOrders, ConfirmTreatment };

SDL_FRect treatmentControlRect(int control)
{
    switch (control) {
    case OpenTreatment: return {938, 153, 314, 34};
    case PreviousKit: return {938, 94, 152, 32};
    case NextKit: return {1100, 94, 152, 32};
    case BackToOrders: return {938, 173, 152, 34};
    case ConfirmTreatment: return {1100, 173, 152, 34};
    default: return {};
    }
}

int fireSupportOrderAction(Vec2 position, bool treatment)
{
    if (!treatment) {
        for (std::size_t index = 0; index < kOrderLabelsEnglish.size(); ++index)
            if (uiContains(fireSupportOrderRect(index), position))
                return static_cast<int>(index) + 1;
    }
    const int first = treatment ? PreviousKit : OpenTreatment;
    const int last = treatment ? ConfirmTreatment : OpenTreatment;
    for (int control = first; control <= last; ++control)
        if (uiContains(treatmentControlRect(control), position)) return control;
    return 0;
}

std::optional<AssetInstanceId> cycleTreatmentKit(const std::vector<AssetInstanceId> &kits,
    std::optional<AssetInstanceId> selected, bool forward)
{
    if (kits.empty()) return std::nullopt;
    auto at = selected ? std::find(kits.begin(), kits.end(), *selected) : kits.end();
    if (at == kits.end()) return kits.front();
    if (forward) return std::next(at) == kits.end() ? kits.front() : *std::next(at);
    return at == kits.begin() ? kits.back() : *std::prev(at);
}
}

bool App::routeFireSupportInput(const SDL_Event &event)
{
    const bool keyPress = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    const bool slotKey = keyPress && event.key.scancode >= SDL_SCANCODE_F1 && event.key.scancode <= SDL_SCANCODE_F4;
    const auto slotRole = [&]() -> std::optional<RaidSquadRole> {
        const auto slot = static_cast<int>(event.key.scancode) - static_cast<int>(SDL_SCANCODE_F2);
        if (slot < 0) return {};
        const auto &raid = gameSession_.profile().pendingRaid;
        if (raid && !raid->squadOrder.empty()) {
            if (static_cast<std::size_t>(slot) < raid->squadOrder.size()) return raid->squadOrder[slot];
            return {};
        }
        return slot == 0 ? std::optional{RaidSquadRole::Fire} : std::nullopt;
    };
    if (!fireSupportOrdersOpen_)
    {
        if (!slotKey ||
            gameFlow_.state() != GameFlowState::Raid || !windowHasInputFocus_ ||
            pauseMenu_.isOpen() || inventoryOverlayState_.isOpen() || tacticalMapOpen_ ||
            quickWheelOpen() || selectedThrowable_ || developerWeaponPanelOpen_ ||
            developerRangePanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
            developerPerformanceOverlayOpen_ || raidSaveProtectionVisible() ||
            gameSession_.world().raidSession().state() != RaidSessionState::InRaid)
            return false;
        squadCommandRole_ = slotRole();
        if (event.key.scancode != SDL_SCANCODE_F1 && !squadCommandRole_) return false;
        if (!squadOrdersAvailable()) return false;
        squadCargoIndex_ = 0;
        squadOrderFeedback_.clear();

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

    if (slotKey && slotRole() != squadCommandRole_) {
        const auto next = slotRole();
        if (next || event.key.scancode == SDL_SCANCODE_F1) {
            const auto old = squadCommandRole_;
            squadCommandRole_ = next;
            if (!squadOrdersAvailable()) squadCommandRole_ = old;
            else {
                fireSupportTreatmentOpen_ = false;
                fireSupportTreatmentKit_.reset();
                squadCargoIndex_ = 0;
                squadOrderFeedback_.clear();
                fireSupportPress_.cancel();
            }
        }
        return true;
    }
    if (keyPress && ((slotKey && (squadCommandRole_ || event.key.scancode == SDL_SCANCODE_F1)) ||
        (event.key.scancode == SDL_SCANCODE_ESCAPE && !fireSupportTreatmentOpen_)))
    {
        closeFireSupportOrders();
        return true;
    }

    int chosenOrder = 0;
    const bool squadPanel = specialistCommandPanel() && !fireSupportTreatmentOpen_;
    const auto actionAt = [&](Vec2 point) {
        return squadPanel ? squadActionAt(point) : fireSupportOrderAction(point, fireSupportTreatmentOpen_);
    };
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            int action = actionAt({event.button.x, event.button.y});
            std::string context = squadPanel ? squadActionKey(action) : std::string{};
            if (!squadPanel && action == ConfirmTreatment) {
                if (!fireSupportTreatmentKit_ || !gameSession_.queryFireSupportTreatment(*fireSupportTreatmentKit_).canCommit)
                    action = 0;
                else {
                    const auto *asset = gameSession_.profile().assets.find(*fireSupportTreatmentKit_);
                    context = fmt::format("{}:{}:{}", *fireSupportTreatmentKit_, asset->remainingCharges,
                        gameSession_.world().fireSupportHealth().value_or(0));
                }
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                fireSupportPress_.begin(action, context);
            else if (fireSupportPress_.release(action, context))
                chosenOrder = action;
            input_.suppressPrimaryPointerUntilRelease();
        }
        else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                 fireSupportTreatmentOpen_) chosenOrder = BackToOrders;
    }
    else if (event.type == SDL_EVENT_MOUSE_MOTION && fireSupportPress_.action !=
        actionAt({event.motion.x, event.motion.y}))
        fireSupportPress_.cancel();
    else if (keyPress && !fireSupportTreatmentOpen_ &&
             event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= (squadPanel ? SDL_SCANCODE_9 : SDL_SCANCODE_5))
        chosenOrder = static_cast<int>(event.key.scancode - SDL_SCANCODE_1) + 1;
    else if (keyPress && fireSupportTreatmentOpen_) {
        switch (event.key.scancode) {
        case SDL_SCANCODE_LEFT: chosenOrder = PreviousKit; break;
        case SDL_SCANCODE_RIGHT: chosenOrder = NextKit; break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: chosenOrder = ConfirmTreatment; break;
        case SDL_SCANCODE_ESCAPE: chosenOrder = BackToOrders; break;
        default: break;
        }
    }
    if (keyPress && event.key.scancode == SDL_SCANCODE_F9)
    {
        closeFireSupportOrders();
        return false;
    }

    if (squadPanel && chosenOrder) {
        handleSquadOrder(chosenOrder);
    } else if (chosenOrder >= OpenTreatment) {
        fireSupportPress_.cancel();
        if (chosenOrder == BackToOrders) {
            fireSupportTreatmentOpen_ = false;
            fireSupportTreatmentKit_.reset();
        } else if (chosenOrder == ConfirmTreatment) {
            if (fireSupportTreatmentKit_ && gameSession_.startFireSupportTreatment(*fireSupportTreatmentKit_)) {
                fireSupportTreatmentStartedThisFrame_ = true;
                closeFireSupportOrders();
            }
        } else {
            const auto kits = gameSession_.fireSupportTreatmentKits();
            fireSupportTreatmentKit_ = cycleTreatmentKit(kits,
                chosenOrder == OpenTreatment ? std::nullopt : fireSupportTreatmentKit_, chosenOrder != PreviousKit);
            fireSupportTreatmentOpen_ = true;
        }
    } else if (chosenOrder != 0)
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
    fireSupportTreatmentOpen_ = false;
    fireSupportTreatmentKit_.reset();
    squadCommandRole_.reset();
    squadOrderFeedback_.clear();
    fireSupportUiBlocksGameplayThisFrame_ = true;
    fireSupportPress_.cancel();
    input_.suppressPrimaryPointerUntilRelease();
    if (input_.isSecondaryPointerPressed()) opticAimBlockedUntilRelease_ = true;
}

void App::renderFireSupportWorld()
{
    for (const auto role : {RaidSquadRole::Fire, RaidSquadRole::Support, RaidSquadRole::Sniper}) {
    const auto teammate = gameSession_.world().squadMember(role);
    if (!teammate || !teammate->alive() || !teammate->visible) continue;

    const auto &bounds = teammate->bounds;
    const float middle = bounds.position.x + bounds.size.x * 0.5F;
    const float top = bounds.position.y;
    const SDL_FRect head{middle - 6.0F, top + 2.0F, 12.0F, 10.0F};
    const SDL_FRect vest{middle - 9.0F, top + 12.0F, 18.0F, bounds.size.y - 14.0F};
    SDL_SetRenderDrawColor(renderer_, 98, 170, 151, 255);
    SDL_RenderFillRect(renderer_, &head);
    const auto color = role == RaidSquadRole::Support ? SDL_Color{88, 125, 167, 255} :
        role == RaidSquadRole::Sniper ? SDL_Color{126, 111, 76, 255} : SDL_Color{51, 91, 85, 255};
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
    SDL_RenderFillRect(renderer_, &vest);
    SDL_SetRenderDrawColor(renderer_, 164, 230, 208, 255);
    SDL_RenderRect(renderer_, &vest);

    if (role != RaidSquadRole::Fire) {
        const bool zh = uiTextRenderer_.language() == UiLanguage::SimplifiedChinese;
        renderWorldLabel(vest.x, top - 20.0F, role == RaidSquadRole::Support ?
            (zh ? "支援" : "Medic") : (zh ? "狙击" : "Sniper"));
    }
    if (role == RaidSquadRole::Fire && !fireSupportOrdersOpen_ && !inventoryOverlayState_.isOpen() && teammate->inInteractionRange &&
        !tacticalMapOpen_ && !quickWheelOpen() && visibleWeaponOptic() != WeaponOpticMode::High &&
        !gameSession_.world().raidSpacePortalInteractionInRange() &&
        !gameSession_.world().ordinarySurvivorRescueInteractionInRange() &&
        !gameSession_.canCallSignalExtraction() && gameSession_.canTakeFireSupportMagazine() &&
        !gameSession_.nearbyRaidSceneContainer())
        renderWorldLabel(vest.x, top - 22.0F, "F: TAKE TEAMMATE MAGAZINE");
    }
}

void App::renderFireSupportOrders()
{
    if (gameFlow_.state() != GameFlowState::Raid || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || tacticalMapOpen_ || developerWeaponPanelOpen_ ||
        developerRangePanelOpen_ || developerPerformanceOverlayOpen_ || raidSaveProtectionVisible())
        return;
    const auto &raid = gameSession_.profile().pendingRaid;
    if (!fireSupportTreatmentOpen_ && ((fireSupportOrdersOpen_ && specialistCommandPanel()) ||
        (!fireSupportOrdersOpen_ && raid && !raid->squadOrder.empty()))) {
        renderSquadOrders();
        return;
    }
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
        if (!teammate->sameSpace) summary += " · " + tr("WAITING IN ANOTHER AREA");
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

    if (fireSupportTreatmentOpen_) {
        const auto kits = gameSession_.fireSupportTreatmentKits();
        const auto selected = fireSupportTreatmentKit_
            ? std::find(kits.begin(), kits.end(), *fireSupportTreatmentKit_) : kits.end();
        const auto id = selected == kits.end() ? AssetInstanceId{} : *selected;
        const auto *asset = gameSession_.profile().assets.find(id);
        const auto plan = gameSession_.queryFireSupportTreatment(id);
        const auto kitName = asset
            ? tr(publishedContentRegistry().item(asset->definitionId).displayName) : tr("No medical kit");
        const auto itemLine = asset
            ? fmt::format("{}   {} / {}   ·   {} {}", kitName, selected-kits.begin()+1,
                kits.size(), asset->remainingCharges, chinese ? "次可用" : "uses left") : kitName;
        uiFinalText(renderer_, uiTextRenderer_, {938, 58, 314, 27}, itemLine, 14);
        const auto controls = [&](int action, const std::string &label, bool enabled) {
            renderFoundationButton(8950 + action, treatmentControlRect(action), label, enabled, false, false, {}, 13);
        };
        const bool canCycle = kits.size() > 1 || (!asset && !kits.empty());
        controls(PreviousKit, "←  " + tr("Previous kit"), canCycle);
        controls(NextKit, tr("Next kit") + "  →", canCycle);
        const auto detail = plan.canCommit
            ? fmt::format("{} · {:.1f} {}", chinese ? "用 1 次" : "Uses 1 charge",
                plan.durationMs / 1000.0F, chinese ? "秒" : "s")
            : tr(plan.message);
        uiFinalText(renderer_, uiTextRenderer_, {938, 134, 314, 32}, detail, 13);
        controls(BackToOrders, "Esc  " + tr("Return to orders"), true);
        controls(ConfirmTreatment, "Enter  " + tr("Start treatment"), plan.canCommit);
        return;
    }

    const std::array selected{teammate->movementOrder == FireSupportMovementOrder::Follow,
        teammate->movementOrder == FireSupportMovementOrder::Hold,
        teammate->fireOrder == FireSupportFireOrder::HoldFire,
        teammate->fireOrder == FireSupportFireOrder::FreeFire};
    for (std::size_t index = 0; index < labels.size(); ++index)
        renderFoundationButton(8950 + static_cast<int>(index), fireSupportOrderRect(index),
            fmt::format("{}  {}", index + 1U, labels[index]), true, false, selected[index], {}, 13);
    renderFoundationButton(8950 + OpenTreatment, treatmentControlRect(OpenTreatment),
        "5  " + tr("Treat teammate"), true, false, false, {}, 13);
    uiFinalText(renderer_, uiTextRenderer_,
        {slot.x + 12, slot.y + 171, slot.w - 24, 23}, "F2 / ESC CLOSE TEAMMATE ORDERS", 12);
}
