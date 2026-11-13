#include "app.h"

#include <utility>

#include <fmt/format.h>

namespace
{
constexpr std::size_t kSellDocumentRow = 0;
constexpr std::size_t kTradeRowCount = 4;
constexpr SDL_FRect kTradePanel{816.0F, 92.0F, 448.0F, 338.0F};
constexpr SDL_Color kTradeText{222, 231, 226, 255};
constexpr SDL_Color kTradeSecondary{171, 195, 188, 255};
constexpr SDL_Color kTradeAccent{126, 224, 200, 255};

SDL_FRect tradeRow(std::size_t row)
{
    return {kTradePanel.x + 16.0F, kTradePanel.y + 66.0F + 52.0F * static_cast<float>(row),
            kTradePanel.w - 32.0F, 48.0F};
}

SDL_FRect tradeButton(std::size_t row)
{
    const auto bounds = tradeRow(row);
    return {bounds.x + bounds.w - 100.0F, bounds.y + 4.0F, 96.0F, 40.0F};
}

std::optional<std::size_t> tradeButtonAt(Vec2 point)
{
    for (std::size_t row{}; row < kTradeRowCount; ++row)
        if (uiContains(tradeButton(row), point))
            return row;
    return std::nullopt;
}

const char *fieldTradeAccessFeedback(RaidFieldTradeAccess access)
{
    switch (access)
    {
    case RaidFieldTradeAccess::OutOfRange: return "Trade interrupted · too far";
    case RaidFieldTradeAccess::Obstructed: return "Trade interrupted · view blocked";
    case RaidFieldTradeAccess::Threatened: return "Trade unavailable · hostiles nearby";
    case RaidFieldTradeAccess::Unavailable: return "Trade unavailable";
    case RaidFieldTradeAccess::Available: return "";
    }
    return "";
}

const char *fieldOfferCategoryName(RaidIntelligenceCategory category)
{
    switch (category)
    {
    case RaidIntelligenceCategory::Transport: return "Transport map";
    case RaidIntelligenceCategory::Resource: return "Resource manifest";
    case RaidIntelligenceCategory::Enemy: return "Enemy dossier";
    case RaidIntelligenceCategory::Count: return "";
    }
    return "";
}
}

bool App::routeFieldIntelligenceTradeInput(const SDL_Event &event)
{
    auto &trade = fieldIntelligenceTradeUi_;
    if (!trade.open)
    {
        if (gameFlow_.state() == GameFlowState::Raid && event.type == SDL_EVENT_KEY_DOWN &&
            !event.key.repeat && event.key.scancode == SDL_SCANCODE_E)
            trade.interactRequested = true;
        return false;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            const auto row = tradeButtonAt({event.button.x, event.button.y});
            const int action = row ? static_cast<int>(*row) + 1 : 0;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                trade.press.begin(action, {});
                if (row)
                    trade.focusedRow = *row;
            }
            else if (trade.press.release(action, {}) && !trade.pendingRow)
            {
                trade.pendingRow = row;
            }
            input_.suppressPrimaryPointerUntilRelease();
        }
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_WHEEL)
        return true;
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
        return false;

    switch (event.key.scancode)
    {
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_E:
        trade.closeRequested = true;
        break;
    case SDL_SCANCODE_UP:
        trade.focusedRow = (trade.focusedRow + kTradeRowCount - 1) % kTradeRowCount;
        break;
    case SDL_SCANCODE_DOWN:
        trade.focusedRow = (trade.focusedRow + 1) % kTradeRowCount;
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (!trade.pendingRow)
            trade.pendingRow = trade.focusedRow;
        break;
    case SDL_SCANCODE_F9:
        return false; // Keep the existing live performance overlay available.
    default:
        break;
    }
    return true; // Movement keys still reach InputSystem before this route.
}

bool App::tryOpenFieldIntelligenceTrade()
{
    auto &trade = fieldIntelligenceTradeUi_;
    if (!trade.interactRequested || quickWheelOpen() || selectedThrowable_ ||
        !windowHasInputFocus_ || gameSession_.raidActionState().active() ||
        gameSession_.world().raidSession().state() != RaidSessionState::InRaid)
        return false;

    const auto point = gameSession_.world().visibleFieldIntelligenceContact();
    if (!point)
        return false;

    const auto access = gameSession_.fieldIntelligenceTradeStatus();
    if (access == RaidFieldTradeAccess::OutOfRange)
        return false;
    if (access != RaidFieldTradeAccess::Available || !gameSession_.fieldIntelligenceTradeView())
    {
        uiMessage_ = access == RaidFieldTradeAccess::Available
            ? "Trade unavailable" : fieldTradeAccessFeedback(access);
        return true;
    }

    trade = {};
    trade.open = true;
    gameSession_.cancelPlayerControlIntent();
    gameSession_.cancelWeaponLightInput();
    cancelMagazineWheel();
    updateWeaponOpticPointerGate();
    input_.suppressPrimaryPointerUntilRelease();
    return true;
}

void App::closeFieldIntelligenceTrade(std::string_view feedback)
{
    if (!fieldIntelligenceTradeUi_.open)
        return;
    fieldIntelligenceTradeUi_ = {};
    input_.suppressPrimaryPointerUntilRelease();
    if (input_.isSecondaryPointerPressed())
        opticAimBlockedUntilRelease_ = true;
    if (!feedback.empty())
        uiMessage_ = feedback;
}

void App::updateFieldIntelligenceTradeAfterWorld()
{
    auto &trade = fieldIntelligenceTradeUi_;
    if (!trade.open)
        return;
    if (trade.closeRequested || gameFlow_.state() != GameFlowState::Raid)
    {
        closeFieldIntelligenceTrade();
        return;
    }
    if (const auto &damage = gameSession_.lastIncomingDamage();
        damage && damage->damageApplied > 0)
    {
        closeFieldIntelligenceTrade("Trade interrupted by a hit");
        return;
    }
    const auto access = gameSession_.fieldIntelligenceTradeStatus();
    if (access != RaidFieldTradeAccess::Available)
    {
        closeFieldIntelligenceTrade(fieldTradeAccessFeedback(access));
        return;
    }

    const auto action = std::exchange(trade.pendingRow, std::nullopt);
    if (!action)
        return;
    const auto view = gameSession_.fieldIntelligenceTradeView();
    if (!view)
    {
        closeFieldIntelligenceTrade("Trade unavailable");
        return;
    }

    if (*action == kSellDocumentRow)
    {
        if (view->fileSold)
            trade.feedback = "One document already sold this raid";
        else if (!view->sellableFile)
            trade.feedback = "Carry a document to sell it";
        else
            trade.feedback = gameSession_.sellFieldIntelligenceDocument(*view->sellableFile)
                ? "Document sold" : "Trade conditions changed · try again";
        return;
    }

    const auto offerIndex = *action - 1;
    const auto &offer = view->offers[offerIndex];
    if (offer.purchased)
        trade.feedback = "Intelligence already active or purchased";
    else if (view->balance < offer.price)
        trade.feedback = "Insufficient field funds";
    else if (gameSession_.buyFieldIntelligenceOffer(offerIndex))
        trade.feedback = offer.mapId == kFieldIntelligenceSourceMapId
            ? "Intelligence active in this raid" : "Purchased · extract to archive";
    else
        trade.feedback = "Trade conditions changed · try again";
}

void App::suppressFieldIntelligenceTradeControls(GameplayInput &gameplayInput)
{
    input_.suppressPrimaryPointerUntilRelease();
    gameSession_.cancelPlayerControlIntent();
    GameplayInput tradeInput;
    tradeInput.moveUp = gameplayInput.moveUp;
    tradeInput.moveDown = gameplayInput.moveDown;
    tradeInput.moveLeft = gameplayInput.moveLeft;
    tradeInput.moveRight = gameplayInput.moveRight;
    tradeInput.firePhysicallyPressed = gameplayInput.firePhysicallyPressed;
    tradeInput.aimWorldPosition = gameSession_.world().weaponAimWorldPosition();
    tradeInput.aimViewport = gameplayInput.aimViewport;
    tradeInput.aimWorldBounds = gameplayInput.aimWorldBounds;
    tradeInput.aimMotionDelta = Vec2{};
    tradeInput.allowWeaponOptics = false;
    tradeInput.freezeObservationDirection = true;
    gameplayInput = std::move(tradeInput);
}

void App::renderFieldIntelligenceContact()
{
    if (const auto point = gameSession_.world().visibleFieldIntelligenceContact())
    {
        const SDL_FRect plaque{point->x - 10.0F, point->y - 7.0F, 20.0F, 14.0F};
        uiFinalPanel(renderer_, plaque);
        SDL_SetRenderDrawColor(renderer_, kTradeAccent.r, kTradeAccent.g, kTradeAccent.b, 255);
        SDL_RenderLine(renderer_, point->x - 5.0F, point->y - 2.0F, point->x + 5.0F, point->y - 2.0F);
        SDL_RenderLine(renderer_, point->x - 5.0F, point->y + 2.0F, point->x + 2.0F, point->y + 2.0F);
        const auto &player = gameSession_.world().player();
        const float dx = player.position().x + player.size() / 2.0F - point->x;
        const float dy = player.position().y + player.size() / 2.0F - point->y;
        renderWorldLabel(point->x + 16.0F, point->y - 9.0F,
            !fieldIntelligenceTradeUi_.open && dx * dx + dy * dy <= 72.0F * 72.0F
                ? "E  Intelligence contact" : "Intelligence contact");
    }
}

void App::renderFieldIntelligenceTrade()
{
    if (!fieldIntelligenceTradeUi_.open)
        return;
    if (const auto view = gameSession_.fieldIntelligenceTradeView())
        drawFieldIntelligenceTradePanel(*view);
}

void App::drawFieldIntelligenceTradePanel(const RaidFieldTradeView &view)
{
    const auto &trade = fieldIntelligenceTradeUi_;
    const auto write = [&](const SDL_FRect &bounds, std::string_view label, int size,
                           SDL_Color color = kTradeText, UiTextAlign alignment = UiTextAlign::Left)
    {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        uiTextRenderer_.renderBox(renderer_, bounds, label, {size, alignment, 1});
    };

    uiFinalPanel(renderer_, kTradePanel);
    const float x = kTradePanel.x + 16.0F;
    const float y = kTradePanel.y;
    const auto language = uiTextRenderer_.language();
    const bool chinese = language == UiLanguage::SimplifiedChinese;
    write({x, y + 14.0F, 258.0F, 28.0F}, "Intelligence contact", 18);
    write({x + 258.0F, y + 16.0F, 158.0F, 24.0F}, fmt::format("Field funds {}", view.balance),
          14, kTradeAccent, UiTextAlign::Right);
    write({x, y + 41.0F, 416.0F, 20.0F}, "The raid continues during trade", 12, kTradeSecondary);

    const auto drawRow = [&](std::size_t row, std::string_view name, std::string_view hint,
                             std::uint32_t price, const char *action, bool enabled)
    {
        const auto bounds = tradeRow(row);
        const auto button = tradeButton(row);
        const bool hovered = uiPointerInside_ && pointerWorldPosition_ &&
            uiContains(button, *pointerWorldPosition_);
        const bool focused = row == trade.focusedRow;
        const bool pressed = trade.press.action == static_cast<int>(row) + 1;
        SDL_SetRenderDrawColor(renderer_, 38, 52, 49, 255);
        SDL_RenderRect(renderer_, &bounds);
        write({bounds.x + 8.0F, bounds.y + 5.0F, 249.0F, 21.0F}, name, 14);
        write({bounds.x + 8.0F, bounds.y + 26.0F, 249.0F, 19.0F}, hint, 12, kTradeSecondary);
        write({bounds.x + 264.0F, bounds.y + 12.0F, 40.0F, 23.0F}, fmt::format("{}", price),
              14, kTradeText, UiTextAlign::Right);
        SDL_SetRenderDrawColor(renderer_, enabled && (hovered || pressed) ? 58 : 33,
            enabled && (hovered || pressed) ? 91 : 47,
            enabled && (hovered || pressed) ? 81 : 43, 255);
        SDL_RenderFillRect(renderer_, &button);
        const auto border = focused || (enabled && hovered) ? kTradeAccent : kTradeSecondary;
        SDL_SetRenderDrawColor(renderer_, border.r, border.g, border.b, focused || enabled ? 255 : 120);
        SDL_RenderRect(renderer_, &button);
        write({button.x + 2.0F, button.y + 10.0F, button.w - 4.0F, 20.0F},
              action, 13, enabled ? kTradeText : kTradeSecondary, UiTextAlign::Center);
    };

    drawRow(kSellDocumentRow, "Raw Intelligence Document",
        view.fileSold ? "One document already sold this raid" :
        view.sellableFile ? "Adds to field funds" : "Carry a document to sell it",
        kFieldIntelligenceSalePrice, chinese ? "出售" : "Sell", view.sellableFile.has_value() && !view.fileSold);
    const auto &content = publishedContentRegistry();
    for (std::size_t index{}; index < view.offers.size(); ++index)
    {
        const auto &offer = view.offers[index];
        const bool here = offer.mapId == kFieldIntelligenceSourceMapId;
        const auto &map = content.map(offer.mapId);
        const auto name = localizeUiText(language, fieldOfferCategoryName(offer.category));
        const char *effect = here
            ? (offer.purchased ? "Already active" : "Immediate")
            : (offer.purchased ? "Pending extraction" : "After extraction");
        const auto hint = fmt::format("{} · {}", localizeUiText(language, map.displayName),
            localizeUiText(language, effect));
        drawRow(index + 1, name, hint, offer.price, chinese ? "购买" : "Buy",
            !offer.purchased && offer.price <= view.balance);
    }

    const auto footer = trade.feedback.empty() ? "↑↓ Select · Enter Confirm · Esc Close" : trade.feedback;
    write({x, y + 284.0F, 416.0F, 20.0F}, footer, 12,
          trade.feedback.empty() ? kTradeSecondary : kTradeAccent);
    write({x, y + 308.0F, 416.0F, 20.0F}, "Extract to keep remaining funds", 12, kTradeSecondary);
}
