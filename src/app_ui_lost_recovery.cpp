#include "app.h"

#include <algorithm>
#include <fmt/format.h>

namespace {
using C = UiLostRecoveryControl;
using K = UiLostRecoveryEventKind;
using T = UiLostRecoveryTab;

std::string selectionKey(const std::optional<std::string> &id) {
    return id ? fmt::format("{}:{}", id->size(), *id) : "-";
}
}

void App::resetLostRecoveryUi() {
    lostRecoveryUi_ = {};
    lostRecoveryProjection_ = {};
    lostRecoveryProjectionRevision_.reset();
    lostRecoveryViewKey_.clear();
    lostRecoveryFeedback_.clear();
    pendingLostRecoveryEvents_.clear();
}

void App::refreshLostRecoveryUi(bool force) {
    const auto &profile = gameSession_.profile();
    const auto language = uiTextRenderer_.language();
    if (lostRecoveryProjectionRevision_ &&
        lostRecoveryProjection_.profileId != profile.profileId) resetLostRecoveryUi();
    auto &state = lostRecoveryUi_;
    if (lostRecoveryProjectionRevision_ && lostRecoveryProjection_.language != language) {
        state.press.reset();
        state.confirmationKey.reset();
        state.assetScroll = state.feedbackScroll = 0;
        lostRecoveryFeedback_.clear();
    }
    const auto viewKey = uiLostRecoveryViewKey(state) + selectionKey(selectedRaidSelfRecoveryRecordId_);
    if (!force && lostRecoveryProjectionRevision_ &&
        *lostRecoveryProjectionRevision_ == profile.revision &&
        lostRecoveryProjection_.worldMinute == profile.worldClock.elapsedWorldMinutes &&
        lostRecoveryProjection_.language == language && lostRecoveryViewKey_ == viewKey) return;

    const auto previousSelection = state.selectedRecordId;
    lostRecoveryProjection_ = projectUiLostRecovery(profile, publishedContentRegistry(), language,
        selectedRaidSelfRecoveryRecordId_, state);
    const auto &projection = lostRecoveryProjection_;
    state.selectedRecordId = projection.selectedRecordId;
    state.recordPage = projection.recordPage;
    if (previousSelection != state.selectedRecordId) state.assetScroll = 0;
    if (state.confirmationKey && *state.confirmationKey != projection.contextKey)
        state.confirmationKey.reset();
    state.assetMaximum = std::max(0, static_cast<int>(projection.assets.size()) -
        static_cast<int>(uiLostRecoveryAssetVisibleRows()));
    state.assetScroll = std::clamp(state.assetScroll, 0, state.assetMaximum);
    lostRecoveryViewKey_ = uiLostRecoveryViewKey(state) + selectionKey(selectedRaidSelfRecoveryRecordId_);
    lostRecoveryProjectionRevision_ = profile.revision;
    ++lostRecoveryProjectionGeneration_;
}

void App::handleLostRecoveryEvent(const UiLostRecoveryEvent &event) {
    if (gameFlow_.state() != GameFlowState::Base ||
        gameFlow_.activeBaseFacility() != BaseFacilityKind::RaidGate ||
        !lostRaidRecordsOpen_ || regionalOperationsOpen_ || pauseMenu_.isOpen() ||
        inventoryOverlayState_.isOpen() || developerWeaponPanelOpen_ ||
        developerWeaponPanelBlocksGameplayThisFrame_ || !windowHasInputFocus_ ||
        baseSiegeWarningVisible() || siegeWarningBlocksGameplayThisFrame_ ||
        gameSession_.baseDefenseSaveBlocked() || gameSession_.baseDailySaveBlocked() ||
        baseDefenseResultVisible_) {
        lostRecoveryUi_.press.reset();
        lostRecoveryUi_.confirmationKey.reset();
        return;
    }
    refreshLostRecoveryUi(event.kind == K::Down || event.kind == K::Up);
    auto &state = lostRecoveryUi_;
    const auto &projection = lostRecoveryProjection_;
    const auto tr = [&](std::string_view value) { return localizeUiText(uiTextRenderer_.language(), value); };
    const auto feedback = [&](std::string value) {
        lostRecoveryFeedback_ = std::move(value);
        state.feedbackScroll = 0;
        uiMessage_ = lostRecoveryFeedback_;
    };
    const auto page = [&](int delta) {
        const auto count = uiLostRecoveryPageCount(projection.records.size());
        state.recordPage = static_cast<std::size_t>(std::clamp(
            static_cast<int>(state.recordPage) + delta, 0, static_cast<int>(count) - 1));
        state.press.reset();
        state.confirmationKey.reset();
    };
    if (event.kind == K::Wheel) {
        state.press.reset();
        state.confirmationKey.reset();
        if (uiContains(uiLostRecoveryFeedbackRect(), event.position))
            state.feedbackScroll = std::clamp(state.feedbackScroll + event.delta * 2, 0, state.feedbackMaximum);
        else if (state.tab == T::Records && uiContains(uiLostRecoveryAssetsRect(), event.position))
            state.assetScroll = std::clamp(state.assetScroll + event.delta * 2, 0, state.assetMaximum);
        else if (state.tab == T::Records && uiContains(uiLostRecoveryListRect(), event.position)) page(event.delta);
        return;
    }
    const auto hit = hitUiLostRecovery(event.position, state, projection);
    if (event.kind == K::Move) {
        if (state.press && hit != state.press->hit) state.press.reset();
        return;
    }
    if (event.kind == K::Down) {
        state.press.reset();
        if (hit) state.press = UiLostRecoveryPress{*hit, uiTextRenderer_.language(),
            uiLostRecoveryActionKey(*hit, state, projection), projection.contextKey};
        return;
    }
    const auto press = std::move(state.press);
    state.press.reset();
    if (!press || !hit || *hit != press->hit || press->language != uiTextRenderer_.language()) return;
    if (press->contextKey != projection.contextKey ||
        press->action != uiLostRecoveryActionKey(*hit, state, projection)) {
        state.confirmationKey.reset();
        feedback(tr("RECOVERY CHANGED - REVIEW AND CLICK AGAIN"));
        return;
    }

    const auto control = hit->control;
    if (control == C::Close) {
        resetLostRecoveryUi();
        resetBaseDeploymentUi();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        uiMessage_.clear();
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == C::Records || control == C::Task) {
        state.tab = control == C::Records ? T::Records : T::Task;
        state.confirmationKey.reset();
        state.feedbackScroll = 0;
        refreshLostRecoveryUi(true);
        return;
    }
    if (control == C::Previous || control == C::Next) {
        page(control == C::Previous ? -1 : 1);
        return;
    }
    if (control == C::Record) {
        state.selectedRecordId = hit->recordId;
        state.assetScroll = state.feedbackScroll = 0;
        state.confirmationKey.reset();
        lostRecoveryFeedback_.clear();
        uiMessage_.clear();
        refreshLostRecoveryUi(true);
        return;
    }
    if (!uiLostRecoveryEnabled(control, state, projection, hit->index)) {
        feedback(tr(uiLostRecoveryUnavailable(*hit, state, projection)));
        gameAudio_.play(SoundEventId::UiDeny);
        return;
    }
    if (control == C::Cancel) {
        state.confirmationKey = projection.contextKey;
        feedback({});
        return;
    }
    if (control == C::KeepTask) {
        state.confirmationKey.reset();
        feedback({});
        return;
    }
    if (control == C::ClearSelfRecovery) {
        selectedRaidSelfRecoveryRecordId_.reset();
        feedback("RAID SELF-RECOVERY TARGET CLEARED");
        refreshLostRecoveryUi(true);
        return;
    }
    if (control == C::SelfRecovery) {
        const auto record = *projection.selectedRecord;
        const auto &maps = publishedContentRegistry().maps();
        const auto map = std::find_if(maps.begin(), maps.end(),
            [&](const auto &value) { return value.id == record.mapDefinitionId; });
        if (map == maps.end()) {
            feedback("LOST RAID MAP IS UNAVAILABLE");
            return;
        }
        selectedRaidMapIndex_ = static_cast<std::size_t>(map - maps.begin());
        selectedRaidSelfRecoveryRecordId_ = record.recordId;
        selectedRaidIntelligence_ = {};
        deploymentWarningArmed_ = false;
        resetBaseDeploymentUi();
        resetLostRecoveryUi();
        lostRaidRecordsOpen_ = false;
        regionalOperationsOpen_ = false;
        uiMessage_ = "RAID SELF-RECOVERY TARGET SET | RESERVE CARRY SPACE";
        input_.suppressPrimaryPointerUntilRelease();
        return;
    }
    if (control == C::Start) {
        const std::string recordId = hit->recordId;
        const auto receipt = gameSession_.startRecoveryTask(recordId,
            nextProfileTransactionId("recovery-start"));
        if (receipt.succeeded) {
            if (selectedRaidSelfRecoveryRecordId_ == std::optional<std::string>{recordId})
                selectedRaidSelfRecoveryRecordId_.reset();
            state.selectedRecordId.reset();
            state.assetScroll = 0;
            state.tab = T::Task;
        }
        state.confirmationKey.reset();
        feedback(receipt.succeeded ? "RECOVERY TASK STARTED" : receipt.message);
        refreshLostRecoveryUi(true);
        return;
    }
    if (control == C::Collect) {
        const auto receipt = gameSession_.collectRecoveryTask(nextProfileTransactionId("recovery-collect"));
        state.confirmationKey.reset();
        feedback(receipt.succeeded ? fmt::format("RECOVERY COLLECTED | {} RETURNED | {} LOST",
            receipt.recoveredAssetCount, receipt.lostAssetCount) : receipt.message);
        refreshLostRecoveryUi(true);
        return;
    }
    if (control == C::ConfirmCancel) {
        const std::string recordId = projection.task->recordId;
        const auto receipt = gameSession_.cancelRecoveryTask(nextProfileTransactionId("recovery-cancel"));
        state.confirmationKey.reset();
        if (receipt.succeeded) {
            state.tab = T::Records;
            state.selectedRecordId = recordId;
            state.assetScroll = 0;
            const auto records = gameSession_.lostRaidRecordProjections();
            const auto found = std::find_if(records.begin(), records.end(),
                [&](const auto &value) { return value.recordId == recordId; });
            if (found != records.end()) state.recordPage =
                static_cast<std::size_t>(found - records.begin()) / uiLostRecoveryPageSize();
        }
        feedback(receipt.succeeded ? "RECOVERY CANCELLED | NO REFUND" : receipt.message);
        refreshLostRecoveryUi(true);
    }
}

void App::renderLostRaidRecords() {
    if (!renderer_) return;
    refreshLostRecoveryUi();
    const auto &projection = lostRecoveryProjection_;
    auto &state = lostRecoveryUi_;
    const auto language = uiTextRenderer_.language();
    const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
    const auto fill = [&](SDL_FRect bounds, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer_, &bounds);
    };
    const auto text = [&](SDL_FRect bounds, std::string_view value, int size = 14, int lines = 1) {
        SDL_SetRenderDrawColor(renderer_, 222, 231, 226, 255);
        uiTextRenderer_.renderBox(renderer_, bounds, value, {size, UiTextAlign::Left, lines});
    };
    const auto button = [&](C control, std::string_view label, bool selected = false, bool danger = false) {
        renderFoundationButton(8600 + static_cast<int>(control), uiLostRecoveryControlRect(control), label,
            uiLostRecoveryEnabled(control, state, projection), danger, selected, {}, 14);
    };
    const auto duration = [&](std::uint64_t minutes) {
        return language == UiLanguage::English
            ? fmt::format("{}h {:02}m", minutes / kWorldMinutesPerHour, minutes % kWorldMinutesPerHour)
            : fmt::format("{}小时{:02}分", minutes / kWorldMinutesPerHour, minutes % kWorldMinutesPerHour);
    };
    const auto timestamp = [&](std::uint64_t minutes) {
        const auto clock = projectWorldClock(WorldClockState{minutes});
        return language == UiLanguage::English
            ? fmt::format("DAY {} {:02}:{:02}", clock.day, clock.hour, clock.minute)
            : fmt::format("第{}天 {:02}:{:02}", clock.day, clock.hour, clock.minute);
    };
    const auto retention = [&](const LostRaidRecordProjection &record, bool compact) {
        if (record.retainedSettlementsRemaining == 0U)
            return tr(compact ? "FINAL OPPORTUNITY" : "FINAL OPPORTUNITY | NEXT RAID SETTLEMENT DESTROYS RECORD");
        return compact ? fmt::format("{} {}", tr("UNTIL FINAL OPPORTUNITY"), record.retainedSettlementsRemaining)
            : fmt::format("{} {} {}", tr("UNTIL FINAL OPPORTUNITY"), record.retainedSettlementsRemaining, tr("RAID SETTLEMENTS"));
    };
    const auto scroll = [&](SDL_FRect bounds, const std::string &value, int &offset, int &maximum) {
        const auto lines = wrapUiText(value, bounds.w - 16, 0,
            [&](auto line) { return uiTextRenderer_.measure(line, 14); });
        constexpr float height = 21;
        const auto visible = std::max(1, static_cast<int>(bounds.h / height));
        maximum = std::max(0, static_cast<int>(lines.size()) - visible);
        offset = std::clamp(offset, 0, maximum);
        for (int row = 0; row < visible && row + offset < static_cast<int>(lines.size()); ++row)
            text({bounds.x, bounds.y + row * height, bounds.w - 16, height}, lines[row + offset].text);
        if (maximum) {
            fill({bounds.x + bounds.w - 4, bounds.y, 3, bounds.h}, {51, 69, 69, 255});
            fill({bounds.x + bounds.w - 4, bounds.y + (bounds.h - 12) * offset / maximum, 3, 12}, {113, 191, 179, 255});
        }
    };

    SDL_BlendMode previous{};
    SDL_GetRenderDrawBlendMode(renderer_, &previous);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(uiLostRecoveryPanelRect(), {21, 29, 31, 252});
    fill({50, 604, 1176, 54}, {25, 35, 37, 255});
    text({62, 66, 888, 32}, "LOST RAID RECORDS & RECOVERY", 22);
    text({1018, 72, 196, 22}, fmt::format("{}  ¥{}", tr("CURRENCY"), projection.currency));
    button(C::Records, "LOST RECORDS", state.tab == T::Records);
    button(C::Task, "NPC COMMISSION", state.tab == T::Task);

    if (state.tab == T::Records) {
        fill(uiLostRecoveryListRect(), {27, 38, 40, 255});
        fill(uiLostRecoveryDetailsRect(), {27, 38, 40, 255});
        fill(uiLostRecoveryPlanRect(), {27, 38, 40, 255});
        const auto first = projection.recordPage * uiLostRecoveryPageSize();
        for (std::size_t row = 0; row < uiLostRecoveryPageSize() && first + row < projection.records.size(); ++row) {
            const auto &record = projection.records[first + row];
            const auto bounds = uiLostRecoveryRecordRect(row);
            const bool selected = projection.selectedRecordId == std::optional<std::string>{record.recordId};
            renderFoundationButton(8700 + static_cast<int>(row), bounds, {}, true,
                record.retainedSettlementsRemaining == 0U, selected, tr(record.mapDisplayName), 14);
            text({bounds.x + 14, bounds.y + 5, bounds.w - 28, 21}, tr(record.mapDisplayName));
            text({bounds.x + 14, bounds.y + 29, bounds.w - 28, 17}, retention(record, true), 12);
        }
        if (projection.records.empty()) {
            text({76, 186, 252, 76}, "NO RECOVERABLE LOST LOADOUTS", 14, 3);
            text({76, 272, 252, 94}, "DEATH OR ACTIVE QUIT CREATES A RECORD WHEN GEAR IS CARRIED", 12, 4);
        }
        text({62, 532, 280, 20}, fmt::format("{} / {} · {} {}", projection.recordPage + 1,
            uiLostRecoveryPageCount(projection.records.size()), projection.records.size(), tr("ENTRIES")), 12);
        button(C::Previous, "<");
        button(C::Next, ">");

        if (projection.selectedRecord) {
            const auto &record = *projection.selectedRecord;
            text({378, 174, 490, 46}, tr(record.mapDisplayName), 16, 2);
            const auto outcome = tr(record.outcome == RaidResultOutcome::PlayerDead ? "PLAYER DEAD" : "RAID ABANDONED");
            text({378, 220, 490, 21}, fmt::format("{} · {} · {}", outcome,
                tr(fmt::format("DIFFICULTY {}", record.difficulty)), timestamp(record.createdWorldMinute)), 12);
            text({378, 246, 490, 20}, fmt::format("{} {} · {} {}", tr("ENTRIES"), projection.assets.size(),
                tr("TOTAL QUANTITY"), projection.totalQuantity), 12);
            const auto visible = uiLostRecoveryAssetVisibleRows();
            for (std::size_t row = 0; row < visible && row + static_cast<std::size_t>(state.assetScroll) < projection.assets.size(); ++row) {
                const auto &asset = projection.assets[row + static_cast<std::size_t>(state.assetScroll)];
                const auto bounds = uiLostRecoveryAssetRect(row);
                fill(bounds, {31, 44, 45, 255});
                renderBaseSupplyImage(asset.definitionId, {bounds.x + 4, bounds.y + 3, 42, 32});
                text({bounds.x + 56, bounds.y + 10, 326, 20}, asset.name);
                text({bounds.x + 394, bounds.y + 10, 80, 20}, fmt::format("×{}", asset.quantity));
            }
            if (state.assetMaximum) {
                const auto bounds = uiLostRecoveryAssetsRect();
                fill({bounds.x + bounds.w - 5, bounds.y + 8, 3, bounds.h - 16}, {51, 69, 69, 255});
                fill({bounds.x + bounds.w - 5, bounds.y + 8 + (bounds.h - 30) * state.assetScroll / state.assetMaximum, 3, 14}, {113, 191, 179, 255});
            }
        } else text({378, 188, 490, 64}, "SELECT A LOST RAID RECORD", 16, 2);

        text({914, 174, 288, 24}, "RECOVERY OPTIONS", 16);
        if (projection.selectedRecord) {
            text({914, 210, 288, 23}, projection.serviceFee
                ? fmt::format("{}  ¥{}", tr("NPC SERVICE FEE"), *projection.serviceFee)
                : fmt::format("{}  —", tr("NPC SERVICE FEE")));
            text({914, 242, 288, 23}, projection.durationMinutes
                ? fmt::format("{}  {}", tr("GAME TIME"), duration(*projection.durationMinutes))
                : fmt::format("{}  —", tr("GAME TIME")));
            if (projection.quote.canCommit) {
                text({914, 280, 288, 23}, fmt::format("{}  {}", tr("HIGH TENDENCY"), projection.quote.highTendencyAssets));
                text({914, 308, 288, 23}, fmt::format("{}  {}", tr("LOW TENDENCY"), projection.quote.lowTendencyAssets));
                text({914, 338, 288, 44}, "NPC RECOVERY TENDENCY IS NOT A GUARANTEE", 12, 2);
            } else text({914, 280, 288, 23}, "UNAVAILABLE");
            text({914, 388, 288, 56}, retention(*projection.selectedRecord, false), 12, 3);
        } else text({914, 212, 288, 62}, "SELECT A LOST RAID RECORD", 14, 3);
        button(C::Start, "COMMISSION NPC");
        button(projection.selectedSelfTarget ? C::ClearSelfRecovery : C::SelfRecovery,
            projection.selectedSelfTarget ? "CLEAR RECOVERY TARGET" : "SET RECOVERY TARGET", projection.selectedSelfTarget);
    } else {
        fill(uiLostRecoveryTaskRect(), {27, 38, 40, 255});
        if (projection.task) {
            const auto &task = *projection.task;
            text({78, 174, 1118, 52}, tr(task.mapDisplayName), 18, 2);
            text({78, 230, 1118, 30}, task.readyForCollection ? "READY TO COLLECT" : "COMMISSION ACTIVE", 18);
            text({78, 280, 500, 26}, fmt::format("{}  ¥{}", tr("PAID"), task.paidCurrency));
            text({604, 280, 592, 26}, fmt::format("{}  {}", tr("TIME REMAINING"), duration(task.remainingWorldMinutes)));
            if (task.readyForCollection && task.recoveredAssetCount) {
                text({78, 326, 1118, 28}, fmt::format("{}  {} / {} {}", tr("RECOVERY RESULT"),
                    *task.recoveredAssetCount, task.totalAssetCount, tr("ENTRIES")), 16);
                text({78, 368, 1118, 28}, "RESERVE STASH SPACE BEFORE COLLECTION");
            } else {
                text({78, 326, 1118, 28}, "NPC RESULT IS SHOWN ONLY WHEN READY");
                if (!gameSession_.baseDefenseActive()) text({78, 368, 1118, 28}, "GAME TIME PAUSES ON THIS PAGE");
            }
            const bool confirming = state.confirmationKey && *state.confirmationKey == projection.contextKey;
            if (confirming) text({78, 418, 1118, 38}, "NO REFUND - THE RECORD AND ALL ITS ASSETS WILL BE RESTORED", 14, 2);
            button(C::Collect, "COLLECT RECOVERED ASSETS");
            if (confirming) {
                button(C::ConfirmCancel, "CONFIRM CANCEL", false, true);
                button(C::KeepTask, "KEEP TASK");
            } else button(C::Cancel, "CANCEL | NO REFUND", false, true);
        } else {
            text({78, 186, 1118, 36}, "NO NPC COMMISSION", 18);
            text({78, 242, 1118, 40}, "SELECT ONE COMPLETE RECORD TO COMMISSION NPC RECOVERY", 14, 2);
        }
    }
    std::string reason = tr(lostRecoveryFeedback_);
    if (reason.empty() && state.tab == T::Records && projection.selectedRecord && !projection.quote.canCommit)
        reason = tr(projection.quote.message);
    // Full names are transient read-only hints. Real operation/query feedback
    // keeps priority, and hovering never changes the cached domain projection.
    if (reason.empty() && state.tab == T::Records && windowHasInputFocus_ &&
        uiPointerInside_ && pointerWorldPosition_) {
        const auto point = *pointerWorldPosition_;
        const auto first = projection.recordPage * uiLostRecoveryPageSize();
        for (std::size_t row = 0; row < uiLostRecoveryPageSize() && first + row < projection.records.size(); ++row) {
            if (!uiContains(uiLostRecoveryRecordRect(row), point)) continue;
            const auto &record = projection.records[first + row];
            reason = tr(record.mapDisplayName);
            break;
        }
        for (std::size_t row = 0; reason.empty() && row < uiLostRecoveryAssetVisibleRows() &&
             row + static_cast<std::size_t>(state.assetScroll) < projection.assets.size(); ++row) {
            if (!uiContains(uiLostRecoveryAssetRect(row), point)) continue;
            const auto &asset = projection.assets[row + static_cast<std::size_t>(state.assetScroll)];
            reason = tr(publishedContentRegistry().item(asset.definitionId).displayName);
        }
        if (!reason.empty()) state.feedbackScroll = 0;
    }
    if (reason.empty() && state.tab == T::Records) reason = tr("READ-ONLY ITEMS");
    scroll(uiLostRecoveryFeedbackRect(), reason, state.feedbackScroll, state.feedbackMaximum);
    button(C::Close, "BACK");
    SDL_SetRenderDrawBlendMode(renderer_, previous);
}
