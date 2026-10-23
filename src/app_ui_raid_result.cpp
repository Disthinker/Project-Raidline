#include "app.h"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

#include "first_raid_guidance.h"

namespace {
using C = UiRaidResultControl;
using K = UiRaidResultEventKind;

void appendResultLine(std::string &value, const std::string &line) {
    if (line.empty()) return;
    if (!value.empty()) value += '\n';
    value += line;
}
}

void App::resetRaidResultUi() {
    raidResultUi_ = {};
    raidResultProjection_ = {};
    raidResultProjectionKey_.clear();
    raidResultViewKey_.clear();
    raidResultFeedback_.clear();
    pendingRaidResultEvents_.clear();
    raidResultConfirmRequested_ = false;
    raidResultConfirmConsumedThisFrame_ = false;
}

void App::refreshRaidResultUi(bool force) {
    const auto &profile = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    const bool testRange = gameSession_.developerRangeActive();
    const auto context = uiRaidResultContextKey(
        profile.profileId, profile.lastRaidResult, language, testRange) +
        (profile.homeFounding.hintsDismissed ? "|hints:hidden" : "|hints:visible");
    const bool changed = context != raidResultProjectionKey_;
    if (changed) {
        // A click belongs to one exact result, language and view. Entry also
        // discards any input that was already held when combat ended.
        raidResultUi_ = {};
        pendingRaidResultEvents_.clear();
        raidResultConfirmRequested_ = false;
        raidResultFeedback_.clear();
        if (gameFlow_.state() == GameFlowState::RaidResult &&
            gameSession_.world().isAlphaRaidWorld()) {
            // Combat overlays own pointer capture while open. A completed
            // Raid releases those transient views before showing its result.
            closeInventory();
            medicalWheelOpen_ = false;
            medicalWheelOptions_.clear();
            cancelThrowableUi();
            tacticalMapOpen_ = false;
        }
        input_.suppressPrimaryPointerUntilRelease();
    }
    const auto view = uiRaidResultViewKey(raidResultUi_);
    if (!force && !changed && raidResultViewKey_ == view) return;
    raidResultProjection_ = projectUiRaidResult(profile.profileId,
        profile.lastRaidResult, publishedContentRegistry(), language,
        raidResultUi_, testRange);
    raidResultUi_.page = raidResultProjection_.page;
    raidResultProjectionKey_ = context;
    raidResultViewKey_ = uiRaidResultViewKey(raidResultUi_);
    ++raidResultProjectionGeneration_;
}

bool App::handleRaidResultConfirm() {
    if (gameFlow_.state() != GameFlowState::RaidResult ||
        !gameSession_.world().isAlphaRaidWorld() || pauseMenu_.isOpen() ||
        developerWeaponPanelOpen_ || developerWeaponPanelBlocksGameplayThisFrame_ ||
        !windowHasInputFocus_ || raidResultInputCancelledThisFrame_ ||
        raidResultConfirmConsumedThisFrame_) return false;

    const auto previous = raidResultProjectionKey_;
    refreshRaidResultUi(true);
    if (previous.empty() || previous != raidResultProjectionKey_) return false;
    // The original flow owns hint acknowledgement, saving and spawn choice.
    // A failed save remains here; no other event in this frame can retry it.
    raidResultConfirmConsumedThisFrame_ = true;
    raidResultUi_.press.reset();
    const bool transitioned = handleScreenConfirm();
    input_.suppressPrimaryPointerUntilRelease();
    if (transitioned) {
        resetRaidResultUi();
        raidResultConfirmConsumedThisFrame_ = true;
        return true;
    }
    raidResultFeedback_ = localizeUiText(uiTextRenderer_.language(), uiMessage_);
    const auto &reason = gameSession_.persistenceMessage();
    if (!reason.empty() && reason != uiMessage_)
        appendResultLine(raidResultFeedback_,
            localizeUiText(uiTextRenderer_.language(), reason));
    raidResultUi_.feedbackScroll = 0;
    refreshRaidResultUi(true);
    return false;
}

void App::handleRaidResultEvent(const UiRaidResultEvent &event) {
    if (gameFlow_.state() != GameFlowState::RaidResult ||
        !gameSession_.world().isAlphaRaidWorld() || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        raidResultInputCancelledThisFrame_) {
        raidResultUi_.press.reset();
        return;
    }
    if (raidResultConfirmConsumedThisFrame_) return;
    refreshRaidResultUi(event.kind == K::Down || event.kind == K::Up);
    auto &state = raidResultUi_;
    const auto &projection = raidResultProjection_;
    const auto page = [&](int delta) {
        state.page = static_cast<std::size_t>(std::clamp(
            static_cast<int>(state.page) + delta, 0,
            static_cast<int>(projection.pageCount) - 1));
        state.press.reset();
        refreshRaidResultUi(true);
    };
    if (event.kind == K::Wheel) {
        state.press.reset();
        if (!event.delta) return;
        const auto layout = layoutUiRaidResult();
        if (uiContains(layout.detailsViewport, event.position)) {
            state.detailsScroll = std::clamp(state.detailsScroll + event.delta * 2,
                0, state.detailsMaximum);
        } else if (uiContains(layout.feedback, event.position)) {
            state.feedbackScroll = std::clamp(state.feedbackScroll + event.delta * 2,
                0, state.feedbackMaximum);
        } else if (uiContains(layout.records, event.position)) {
            page(event.delta);
        } else return;
        raidResultConfirmConsumedThisFrame_ = true;
        return;
    }
    const auto hit = hitUiRaidResult(event.position, state, projection);
    if (event.kind == K::Move) {
        if (state.press && hit != state.press->hit) state.press.reset();
        return;
    }
    if (event.kind == K::Down) {
        state.press.reset();
        if (hit && uiRaidResultEnabled(hit->control, state, projection))
            state.press = UiRaidResultPress{*hit, uiTextRenderer_.language(),
                uiRaidResultActionKey(*hit, state, projection), projection.contextKey};
        return;
    }
    const auto press = std::move(state.press);
    state.press.reset();
    if (!press || !hit || *hit != press->hit ||
        press->language != uiTextRenderer_.language() ||
        press->contextKey != projection.contextKey ||
        press->action != uiRaidResultActionKey(*hit, state, projection) ||
        !uiRaidResultEnabled(hit->control, state, projection)) return;
    if (hit->control == C::Return) {
        static_cast<void>(handleRaidResultConfirm());
        return;
    }
    raidResultConfirmConsumedThisFrame_ = true;
    if (hit->control == C::Previous || hit->control == C::Next) {
        page(hit->control == C::Previous ? -1 : 1);
    } else if (hit->control == C::Details) {
        state.details = !state.details;
        state.detailsScroll = 0;
        refreshRaidResultUi(true);
    }
}

void App::updateRaidResultUi(float deltaTime) {
    static_cast<void>(deltaTime);
    raidResultConfirmConsumedThisFrame_ = false;
    if (gameFlow_.state() != GameFlowState::RaidResult ||
        !gameSession_.world().isAlphaRaidWorld()) {
        resetRaidResultUi();
        return;
    }
    if (pauseMenu_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        raidResultInputCancelledThisFrame_) {
        raidResultUi_.press.reset();
        pendingRaidResultEvents_.clear();
        raidResultConfirmRequested_ = false;
        return;
    }
    refreshRaidResultUi();
    const bool confirm = std::exchange(raidResultConfirmRequested_, false);
    const auto events = std::move(pendingRaidResultEvents_);
    pendingRaidResultEvents_.clear();
    for (const auto &event : events) {
        if (raidResultConfirmConsumedThisFrame_) break;
        handleRaidResultEvent(event);
    }
    if (confirm && !raidResultConfirmConsumedThisFrame_)
        static_cast<void>(handleRaidResultConfirm());
}

void App::renderRaidResult() {
    if (!renderer_) return;
    refreshRaidResultUi();
    const auto &projection = raidResultProjection_;
    auto &state = raidResultUi_;
    const auto layout = layoutUiRaidResult();
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
    const auto fill = [&](SDL_FRect bounds, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, &bounds);
    };
    const auto text = [&](SDL_FRect bounds, std::string_view value,
                          int size = 14, int lines = 1) {
        SDL_SetRenderDrawColor(renderer_, 222, 231, 226, 255);
        uiTextRenderer_.renderBox(renderer_, bounds, value,
            {size, UiTextAlign::Left, lines});
    };
    const auto button = [&](C control, std::string_view label, bool selected = false) {
        renderFoundationButton(8900 + static_cast<int>(control),
            uiRaidResultControlRect(control), label,
            uiRaidResultEnabled(control, state, projection), false, selected, {},
            control == C::Return ? 18 : 14);
    };
    const auto scroll = [&](SDL_FRect bounds, const std::string &value,
                            int &offset, int &maximum, int size = 14) {
        const auto lines = wrapUiText(value, bounds.w - 18, 0,
            [&](auto line) { return uiTextRenderer_.measure(line, size); });
        const float lineHeight = static_cast<float>(size + 7);
        const auto visible = std::max(1, static_cast<int>(bounds.h / lineHeight));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int row = 0; row < visible && row + offset < static_cast<int>(lines.size()); ++row)
            text({bounds.x, bounds.y + row * lineHeight, bounds.w - 18, lineHeight},
                lines[row + offset].text, size);
        if (maximum) {
            fill({bounds.x + bounds.w - 4, bounds.y, 3, bounds.h}, {51, 69, 69, 255});
            fill({bounds.x + bounds.w - 4,
                bounds.y + (bounds.h - 16) * offset / maximum, 3, 16},
                {113, 191, 179, 255});
        }
    };

    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_, &previous);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill({0, 0, 1280, 720}, {12, 20, 23, 255});
    fill(layout.panel, {21, 29, 31, 252});
    fill({layout.panel.x, layout.panel.y, layout.panel.w, 4},
        projection.extracted ? SDL_Color{113, 191, 179, 255} : SDL_Color{167, 144, 116, 255});
    fill({layout.records.x - 12, layout.records.y - 8,
        layout.records.w + 24, layout.records.h + 16}, {27, 38, 40, 255});
    fill({layout.detailsViewport.x - 12, layout.detailsViewport.y - 8,
        layout.detailsViewport.w + 24, layout.detailsViewport.h + 16}, {27, 38, 40, 255});
    fill({layout.panel.x + 16, 578, layout.panel.w - 32, 78}, {25, 35, 37, 255});

    text(layout.title, projection.title, 24);
    text(layout.status, projection.status, 20);
    // Every summary fact occupies its own measured row in this shared box.
    // These are frozen settlement fields, never inferred prices or new loot.
    float summaryY = layout.summary.y;
    for (const auto &line : projection.summaryLines) {
        if (summaryY + 20 > layout.summary.y + layout.summary.h) break;
        text({layout.summary.x, summaryY, layout.summary.w, 20}, line);
        summaryY += 21;
    }
    text(layout.recordHeading, projection.recordHeading, 16);
    text(layout.recordNote, projection.recordNote, 12, 2);
    const auto first = projection.page * uiRaidResultPageSize();
    for (std::size_t row = 0;
         row < uiRaidResultPageSize() && first + row < projection.returned.size(); ++row) {
        const auto &item = projection.returned[first + row];
        const auto bounds = layout.rows[row];
        fill(bounds, {33, 46, 47, 255});
        const SDL_FRect image{bounds.x + 8, bounds.y + 6, 74, bounds.h - 12};
        if (item.knownDefinition) renderBaseSupplyImage(item.definitionId, image);
        else {
            fill(image, {44, 59, 58, 255});
            text({image.x + 28, image.y + image.h * 0.5F - 10, 26, 20}, "—");
        }
        text({bounds.x + 94, bounds.y + 14, bounds.w - 108, bounds.h - 22},
            item.name, 14, 2);
    }
    if (projection.returned.empty())
        text({layout.records.x + 18, layout.records.y + 70,
            layout.records.w - 36, 156}, projection.message, 16, 6);
    button(C::Previous, "<");
    button(C::Next, ">");
    text(layout.pageLabel, fmt::format("{} / {}", projection.page + 1, projection.pageCount), 12);
    button(C::Details, state.details ? "COLLAPSE" : "DETAILS", state.details);

    std::string details;
    if (state.details) {
        for (const auto &line : projection.detailLines) appendResultLine(details, line);
    } else appendResultLine(details, projection.message);
    // The old floating result hint card is consumed here, in the same scroll
    // viewport as other detail. Its profile-backed hide/acknowledge rules stay.
    if (projection.hasResult && !projection.testRange &&
        firstRaidGuidanceStage(gameSession_.profile()) ==
        FirstRaidGuidanceStage::Return) {
        appendResultLine(details, tr("FIRST RAID GUIDANCE | H HIDE HINTS"));
        for (const auto &line : projectFirstRaidReturnGuidance(gameSession_.profile()))
            appendResultLine(details, tr(line));
    }
    scroll(layout.detailsViewport, details, state.detailsScroll, state.detailsMaximum);
    std::string feedback = tr(raidResultFeedback_);
    if (feedback.empty() && uiMessage_ == "hint acknowledgement save failed")
        feedback = tr(uiMessage_);
    if (feedback.empty()) feedback = tr("RETURN TO BASE | ENTER");
    scroll(layout.feedback, feedback, state.feedbackScroll, state.feedbackMaximum, 12);
    button(C::Return, "RETURN TO BASE");
    SDL_SetRenderDrawBlendMode(renderer_, previous);
}
