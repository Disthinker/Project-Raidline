#include "ui_lost_recovery_presentation.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using C = UiLostRecoveryControl;
using T = UiLostRecoveryTab;

// Stable identities may contain separators. Length-prefix each part instead
// of making ambiguous concatenations of record names and action arguments.
struct Key {
    std::string value;
    void add(std::string_view text) {
        value += std::to_string(text.size());
        value += ':';
        value.append(text);
        value += ';';
    }
    template<class V> void number(V number) {
        if constexpr (std::is_enum_v<V>)
            add(std::to_string(static_cast<std::underlying_type_t<V>>(number)));
        else
            add(std::to_string(number));
    }
};

void addOptional(Key &key, const std::optional<std::string> &value) {
    key.number(value.has_value());
    if (value) key.add(*value);
}

std::string contextKey(const ProfileState &profile, const UiLostRecoveryProjection &p) {
    Key key;
    key.add(p.profileId);
    key.number(p.revision);
    key.number(p.worldMinute);
    key.number(p.language);
    key.number(p.currency);
    key.number(p.pendingRaid);
    // Reuse the existing read-only fingerprint on projection refresh, never
    // per animation frame. This also binds capacity/ownership changes which
    // occurred between Down and Up without a revision increment.
    key.number(profileStateFingerprint(profile));
    addOptional(key, p.selfRecoveryRecordId);
    addOptional(key, p.selectedRecordId);
    key.number(p.mapValid);
    key.number(p.records.size());
    for (const auto &record : p.records) {
        key.add(record.recordId);
        key.add(record.mapDefinitionId.value());
        key.add(record.mapDisplayName);
        key.add(record.difficulty);
        key.number(record.outcome);
        key.number(record.createdWorldMinute);
        key.number(record.subsequentRaidSettlementCount);
        key.number(record.retainedSettlementsRemaining);
        key.number(record.assets.size());
        for (const auto &asset : record.assets) {
            key.number(asset.assetId);
            key.add(asset.definitionId.value());
            key.number(asset.quantity);
            key.number(asset.ownershipRoot);
        }
    }
    key.number(p.serviceFee.has_value());
    if (p.serviceFee) key.number(*p.serviceFee);
    key.number(p.durationMinutes.has_value());
    if (p.durationMinutes) key.number(*p.durationMinutes);
    key.number(p.quote.canCommit);
    key.number(p.quote.error);
    key.add(p.quote.message);
    key.number(p.quote.revision);
    key.add(p.quote.recordId);
    key.number(p.quote.serviceFee);
    key.number(p.quote.durationMinutes);
    key.number(p.quote.highTendencyAssets);
    key.number(p.quote.lowTendencyAssets);
    key.number(p.task.has_value());
    if (p.task) {
        const auto &task = *p.task;
        key.number(task.taskId);
        key.add(task.recordId);
        key.add(task.mapDefinitionId.value());
        key.number(task.paidCurrency);
        key.number(task.startedWorldMinute);
        key.number(task.completionWorldMinute);
        key.number(task.remainingWorldMinutes);
        key.number(task.readyForCollection);
        key.number(task.totalAssetCount);
        key.number(task.recoveredAssetCount.has_value());
        if (task.recoveredAssetCount) key.number(*task.recoveredAssetCount);
    }
    return std::move(key.value);
}

bool confirming(const UiLostRecoveryState &s, const UiLostRecoveryProjection &p) {
    return p.task.has_value() && s.confirmationKey && *s.confirmationKey == p.contextKey;
}

UiLostRecoveryHit actionHit(C control, const UiLostRecoveryProjection &p) {
    UiLostRecoveryHit hit{control};
    if (control == C::Start || control == C::SelfRecovery || control == C::ClearSelfRecovery)
        hit.recordId = p.selectedRecordId.value_or("");
    if (control == C::Collect || control == C::Cancel ||
        control == C::ConfirmCancel || control == C::KeepTask) {
        if (p.task) {
            hit.taskId = p.task->taskId;
            hit.recordId = p.task->recordId;
        }
    }
    return hit;
}
}

UiLostRecoveryProjection projectUiLostRecovery(
    const ProfileState &profile, const ContentRegistry &content, UiLanguage language,
    const std::optional<std::string> &selfRecoveryRecordId,
    const UiLostRecoveryState &state) {
    UiLostRecoveryProjection result;
    result.profileId = profile.profileId;
    result.revision = profile.revision;
    result.worldMinute = profile.worldClock.elapsedWorldMinutes;
    result.language = language;
    result.currency = profile.currency;
    result.pendingRaid = profile.pendingRaid.has_value();
    result.selfRecoveryRecordId = selfRecoveryRecordId;
    result.records = queryLostRaidRecords(profile, content);
    result.recordPage = std::min(state.recordPage, uiLostRecoveryPageCount(result.records.size()) - 1U);
    if (state.selectedRecordId) {
        const auto found = std::find_if(result.records.begin(), result.records.end(),
            [&](const auto &record) { return record.recordId == *state.selectedRecordId; });
        result.selectionExpired = found == result.records.end();
        if (found != result.records.end()) {
            result.selectedRecordId = found->recordId;
            result.selectedRecord = *found;
            result.selectedSelfTarget = selfRecoveryRecordId == result.selectedRecordId;
            const auto map = std::find_if(content.maps().begin(), content.maps().end(),
                [&](const auto &definition) { return definition.id == found->mapDefinitionId; });
            if (map != content.maps().end()) {
                result.mapValid = true;
                // queryStartRecoveryTask returns before filling its numbers
                // for insufficient funds/another task. These are the same
                // published service values, not the failed query's zeros.
                result.serviceFee = map->recovery.serviceFee;
                result.durationMinutes = map->recovery.durationMinutes;
            }
            result.quote = queryStartRecoveryTask(profile, content, found->recordId);
            result.assets.reserve(found->assets.size());
            for (const auto &asset : found->assets) {
                const auto &definition = content.item(asset.definitionId);
                result.assets.push_back({asset.assetId, asset.definitionId,
                    compactUiItemName(definition, language), asset.quantity, asset.ownershipRoot});
                result.totalQuantity += asset.quantity;
            }
        }
    }
    if (const auto task = queryRecoveryTask(profile, content)) {
        UiLostRecoveryTask projection;
        projection.taskId = task->taskId;
        projection.recordId = task->recordId;
        projection.mapDefinitionId = task->mapDefinitionId;
        projection.mapDisplayName = task->mapDisplayName;
        projection.paidCurrency = task->paidCurrency;
        projection.startedWorldMinute = task->startedWorldMinute;
        projection.completionWorldMinute = task->completionWorldMinute;
        projection.remainingWorldMinutes = task->remainingWorldMinutes;
        projection.readyForCollection = task->readyForCollection;
        projection.totalAssetCount = task->totalAssetCount;
        if (task->readyForCollection) projection.recoveredAssetCount = task->recoveredAssetCount;
        result.task = std::move(projection);
    }
    result.contextKey = contextKey(profile, result);
    return result;
}

std::string uiLostRecoveryViewKey(const UiLostRecoveryState &state) {
    Key key;
    key.number(state.tab);
    addOptional(key, state.selectedRecordId);
    key.number(state.recordPage);
    key.number(state.assetScroll);
    key.number(state.feedbackScroll);
    addOptional(key, state.confirmationKey);
    return std::move(key.value);
}

std::string uiLostRecoveryActionKey(const UiLostRecoveryHit &hit,
    const UiLostRecoveryState &state, const UiLostRecoveryProjection &projection) {
    Key key;
    key.add(projection.contextKey);
    key.add(uiLostRecoveryViewKey(state));
    key.number(hit.control);
    key.number(hit.index);
    key.add(hit.recordId);
    key.number(hit.taskId);
    return std::move(key.value);
}

bool uiLostRecoveryEnabled(C control, const UiLostRecoveryState &state,
    const UiLostRecoveryProjection &p, std::size_t index) {
    switch (control) {
    case C::Record: return state.tab == T::Records && index < p.records.size();
    case C::Previous: return state.tab == T::Records && p.recordPage > 0U;
    case C::Next: return state.tab == T::Records &&
        p.recordPage + 1U < uiLostRecoveryPageCount(p.records.size());
    case C::Start: return state.tab == T::Records && p.selectedRecord && p.quote.canCommit;
    case C::SelfRecovery: return state.tab == T::Records && p.selectedRecord &&
        p.mapValid && !p.pendingRaid && !p.selectedSelfTarget;
    case C::ClearSelfRecovery: return state.tab == T::Records && p.selectedRecord && p.selectedSelfTarget;
    case C::Collect: return state.tab == T::Task && p.task && p.task->readyForCollection;
    case C::Cancel: return state.tab == T::Task && p.task && !confirming(state, p);
    case C::ConfirmCancel: case C::KeepTask: return state.tab == T::Task && confirming(state, p);
    default: return true;
    }
}

std::string uiLostRecoveryUnavailable(const UiLostRecoveryHit &hit,
    const UiLostRecoveryState &state, const UiLostRecoveryProjection &p) {
    if (uiLostRecoveryEnabled(hit.control, state, p, hit.index)) return {};
    switch (hit.control) {
    case C::Start: return p.selectedRecord ? p.quote.message : "SELECT A LOST RAID RECORD";
    case C::SelfRecovery: case C::ClearSelfRecovery:
        if (!p.selectedRecord) return "SELECT A LOST RAID RECORD";
        return !p.mapValid ? "LOST RAID MAP IS UNAVAILABLE" : "recovery task is unavailable during a Raid";
    case C::Collect: return p.task ? "RECOVERY TASK IS STILL IN PROGRESS" : "no recovery task is active";
    case C::Cancel: case C::ConfirmCancel: case C::KeepTask: return "no recovery task is active";
    default: return {};
    }
}

std::size_t uiLostRecoveryPageSize() noexcept { return 6U; }
std::size_t uiLostRecoveryPageCount(std::size_t count) noexcept {
    return count ? 1U + (count - 1U) / uiLostRecoveryPageSize() : 1U;
}
std::size_t uiLostRecoveryAssetVisibleRows() noexcept { return 6U; }
SDL_FRect uiLostRecoveryPanelRect() noexcept { return {40, 50, 1200, 620}; }
SDL_FRect uiLostRecoveryControlRect(C control) noexcept {
    switch (control) {
    case C::Records: return {62, 108, 180, 36};
    case C::Task: return {250, 108, 180, 36};
    case C::Previous: return {62, 558, 130, 32};
    case C::Next: return {212, 558, 130, 32};
    case C::Start: return {902, 454, 312, 36};
    case C::SelfRecovery: case C::ClearSelfRecovery: return {902, 500, 312, 36};
    case C::Collect: return {62, 476, 252, 40};
    case C::Cancel: return {326, 476, 252, 40};
    case C::ConfirmCancel: return {590, 476, 252, 40};
    case C::KeepTask: return {854, 476, 252, 40};
    case C::Close: return {1082, 612, 132, 40};
    default: return {};
    }
}
SDL_FRect uiLostRecoveryRecordRect(std::size_t row) noexcept {
    return {62, 164 + 58.0F * static_cast<float>(row), 280, 52};
}
SDL_FRect uiLostRecoveryListRect() noexcept { return {62, 164, 280, 362}; }
SDL_FRect uiLostRecoveryDetailsRect() noexcept { return {366, 164, 514, 376}; }
SDL_FRect uiLostRecoveryAssetsRect() noexcept { return {366, 274, 514, 266}; }
SDL_FRect uiLostRecoveryAssetRect(std::size_t row) noexcept {
    return {378, 282 + 42.0F * static_cast<float>(row), 490, 38};
}
SDL_FRect uiLostRecoveryPlanRect() noexcept { return {902, 164, 312, 376}; }
SDL_FRect uiLostRecoveryTaskRect() noexcept { return {62, 164, 1152, 378}; }
SDL_FRect uiLostRecoveryFeedbackRect() noexcept { return {366, 558, 848, 44}; }

std::optional<UiLostRecoveryHit> hitUiLostRecovery(Vec2 point,
    const UiLostRecoveryState &state, const UiLostRecoveryProjection &p) {
    for (auto control : {C::Records, C::Task, C::Close})
        if (uiContains(uiLostRecoveryControlRect(control), point)) return UiLostRecoveryHit{control};
    if (state.tab == T::Records) {
        for (auto control : {C::Previous, C::Next, C::Start})
            if (uiContains(uiLostRecoveryControlRect(control), point)) return actionHit(control, p);
        const auto self = p.selectedSelfTarget ? C::ClearSelfRecovery : C::SelfRecovery;
        if (uiContains(uiLostRecoveryControlRect(self), point)) return actionHit(self, p);
        const auto first = p.recordPage * uiLostRecoveryPageSize();
        for (std::size_t row{}; row < uiLostRecoveryPageSize() && first + row < p.records.size(); ++row) {
            if (!uiContains(uiLostRecoveryRecordRect(row), point)) continue;
            UiLostRecoveryHit hit{C::Record, first + row};
            hit.recordId = p.records[hit.index].recordId;
            return hit;
        }
    } else {
        if (uiContains(uiLostRecoveryControlRect(C::Collect), point)) return actionHit(C::Collect, p);
        if (confirming(state, p)) {
            for (auto control : {C::ConfirmCancel, C::KeepTask})
                if (uiContains(uiLostRecoveryControlRect(control), point)) return actionHit(control, p);
        } else if (uiContains(uiLostRecoveryControlRect(C::Cancel), point)) {
            return actionHit(C::Cancel, p);
        }
    }
    return {};
}
