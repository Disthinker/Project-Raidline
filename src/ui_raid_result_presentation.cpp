#include "ui_raid_result_presentation.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>

#include "ui_item_context_presentation.h"

namespace {
using C = UiRaidResultControl;

// Historical identifiers and result text can contain separators. Prefixing
// each field by length keeps identities unambiguous without reading assets.
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

bool normalOutcome(RaidResultOutcome outcome) noexcept {
    return outcome == RaidResultOutcome::Extracted ||
        outcome == RaidResultOutcome::PlayerDead ||
        outcome == RaidResultOutcome::ActiveQuit;
}

std::string signedNumber(std::int64_t value) {
    return (value > 0 ? "+" : "") + std::to_string(value);
}
}

std::string uiRaidResultContextKey(const std::string &profileId,
    const std::optional<LastRaidResult> &result, UiLanguage language, bool testRange) {
    Key key;
    key.add(profileId);
    key.number(language);
    key.number(testRange);
    key.number(result.has_value());
    if (result) {
        key.add(result->settlementId);
        key.number(result->outcome);
        key.number(result->returnedItemDefinitionIds.size());
        for (const auto &id : result->returnedItemDefinitionIds) key.add(id.value());
        key.number(result->currencyDelta);
        key.number(result->travelMinutesApplied);
        key.number(result->rescuedOrdinaryResidents);
        key.number(result->rescuedInjuredResidents);
        key.number(result->lostRaidRecordId.has_value());
        if (result->lostRaidRecordId) key.add(*result->lostRaidRecordId);
        key.number(result->baseThreatReducedUnits);
        key.number(result->unlockedExtractionPermissions.size());
        for (const auto &id : result->unlockedExtractionPermissions) key.add(id.value());
        key.number(result->wishReturn.has_value());
        if (result->wishReturn) {
            const auto &wish = *result->wishReturn;
            key.number(wish.focus.wish.cycleIndex);
            key.add(wish.focus.wish.definitionId.value());
            key.number(wish.focus.category);
            key.number(wish.focus.requiredContribution);
            key.add(wish.focus.assessmentVersion);
            key.number(wish.itemCount);
            key.number(wish.contribution);
            key.number(wish.expired);
        }
    }
    return std::move(key.value);
}

UiRaidResultProjection projectUiRaidResult(const std::string &profileId,
    const std::optional<LastRaidResult> &historicalResult, const ContentRegistry &content,
    UiLanguage language, const UiRaidResultState &state, bool testRange) {
    const auto text = [language](std::string_view key) { return localizeUiText(language, key); };
    UiRaidResultProjection p;
    p.profileId = profileId;
    p.language = language;
    p.testRange = testRange;
    p.contextKey = uiRaidResultContextKey(profileId, historicalResult, language, testRange);
    p.title = text("RAID RESULT");
    p.recordHeading = text("RETURNED RECORDS");
    if (testRange) {
        p.status = text("TEST SESSION ENDED");
        p.message = text("TEST RANGE HAS NO RAID SETTLEMENT");
        p.recordNote = text("NO RETURNED RECORDS FOR THIS TEST SESSION");
        return p;
    }
    if (!historicalResult) {
        p.status = text("NO RESULT AVAILABLE");
        p.message = text("NO RAID RESULT IS AVAILABLE TO DISPLAY");
        return p;
    }
    const auto &result = *historicalResult;
    p.settlementId = result.settlementId;
    p.outcome = result.outcome;
    if (!normalOutcome(result.outcome)) {
        // Abnormal exit is a recovery path, not a fourth normal settlement.
        // Defensive compatibility must not show stale items as fresh returns.
        p.legacyOutcome = true;
        p.status = text("NO NORMAL RAID RESULT");
        p.message = text("THIS RECORD HAS NO NORMAL SETTLEMENT SUMMARY");
        return p;
    }
    p.hasResult = true;
    p.extracted = result.outcome == RaidResultOutcome::Extracted;
    p.status = text(p.extracted ? "EXTRACTED" :
        result.outcome == RaidResultOutcome::PlayerDead ? "PLAYER DEAD" : "RAID ABANDONED");
    p.currencyDelta = result.currencyDelta;
    p.travelMinutesApplied = result.travelMinutesApplied;
    p.rescuedOrdinaryResidents = result.rescuedOrdinaryResidents;
    p.rescuedInjuredResidents = result.rescuedInjuredResidents;
    p.baseThreatReducedUnits = result.baseThreatReducedUnits;
    p.lostRaidRecordId = result.lostRaidRecordId;
    p.wishReturn = result.wishReturn;

    const auto rescued = static_cast<std::uint64_t>(p.rescuedOrdinaryResidents) +
        static_cast<std::uint64_t>(p.rescuedInjuredResidents);
    if (rescued > 0U)
        p.summaryLines.push_back(text("SECURED RESIDENTS") + ": " + std::to_string(rescued));
    if (p.baseThreatReducedUnits > 0U)
        p.summaryLines.push_back(text("ACTUAL BASE THREAT REDUCTION") + ": -" +
            std::to_string(p.baseThreatReducedUnits));
    // A zero is available in details without occupying a primary summary row.
    if (p.currencyDelta != 0)
        p.summaryLines.push_back(text("CURRENCY CHANGE") + ": " + signedNumber(p.currencyDelta));
    if (p.extracted && !result.unlockedExtractionPermissions.empty())
        p.summaryLines.push_back(text("FRONTIER TRANSIT PERMIT REGISTERED"));
    if (p.summaryLines.empty()) p.summaryLines.push_back(text("SETTLEMENT RECORDED"));

    p.detailLines.push_back(text("RETURN / REGROUP TRAVEL") + ": " +
        std::to_string(p.travelMinutesApplied) + text(" MIN"));
    p.detailLines.push_back(text("NOT THE RAID PLAY DURATION"));
    p.detailLines.push_back(text("SECURED ORDINARY RESIDENTS") + ": " +
        std::to_string(p.rescuedOrdinaryResidents));
    p.detailLines.push_back(text("SECURED INJURED RESIDENTS") + ": " +
        std::to_string(p.rescuedInjuredResidents));
    if (p.baseThreatReducedUnits > 0U)
        p.detailLines.push_back(text("ACTUAL BASE THREAT REDUCTION") + ": -" +
            std::to_string(p.baseThreatReducedUnits));
    p.detailLines.push_back(text("CURRENCY CHANGE") + ": " + signedNumber(p.currencyDelta));

    if (p.extracted) {
        p.message = text("ITEMS REMAIN IN THEIR ORIGINAL SLOTS");
        p.recordNote = text("RECORDS INCLUDE ORIGINAL GEAR; NOT NEW LOOT OR STACK QUANTITIES");
        p.returned.reserve(result.returnedItemDefinitionIds.size());
        for (std::size_t index{}; index < result.returnedItemDefinitionIds.size(); ++index) {
            const auto &id = result.returnedItemDefinitionIds[index];
            UiRaidResultItem row{index, id, text("UNKNOWN ITEM"), false};
            const auto definition = std::find_if(content.items().begin(), content.items().end(),
                [&id](const auto &item) { return item.definitionId == id; });
            if (definition != content.items().end()) {
                row.name = compactUiItemName(*definition, language);
                row.knownDefinition = true;
            }
            p.returned.push_back(std::move(row));
        }
        p.recordHeading += " · " + std::to_string(p.returned.size());
        if (p.returned.empty()) p.message = text("NO RETURNED DEFINITION RECORDS");
        p.detailLines.push_back(text("RETURNED RECORDS") + ": " + std::to_string(p.returned.size()));
        p.detailLines.push_back(text("REPEATED DEFINITIONS ARE SEPARATE RECORDS"));
        p.detailLines.push_back(text("BROWSING DOES NOT MOVE OR CLAIM ITEMS"));
    } else {
        p.recordHeading = text("LOSS SUMMARY");
        p.message = text("ALL CARRIED ASSETS WERE LOST");
        p.recordNote = text(p.lostRaidRecordId ? "LOST RECORD CREATED; USE EXISTING RECOVERY" :
            "NO LOST RAID RECORD WAS CREATED");
        p.detailLines.push_back(p.recordNote);
        p.detailLines.push_back(text("LOSS ITEMS ARE NOT STORED IN THIS RESULT"));
    }

    if (p.wishReturn) {
        const auto &wish = *p.wishReturn;
        std::string name = text("WISH RETURN");
        const auto definition = std::find_if(content.basePriorities().begin(), content.basePriorities().end(),
            [&wish](const auto &item) { return item.id == wish.focus.wish.definitionId; });
        if (definition != content.basePriorities().end()) name += " · " + text(definition->displayName);
        p.detailLines.push_back(std::move(name));
        p.detailLines.push_back(text("WISH RETURN ITEM COUNT") + ": " + std::to_string(wish.itemCount));
        p.detailLines.push_back(text("RECORDED WISH CONTRIBUTION") + ": " +
            std::to_string(wish.contribution) + "/" + std::to_string(wish.focus.requiredContribution));
        p.detailLines.push_back(text(wish.expired
            ? (p.extracted ? "WISH ENDED | ITEMS STAY IN PLACE" : "WISH ENDED | NO RETURNED CONTRIBUTION")
            : wish.contribution >= wish.focus.requiredContribution
                ? "WISH STOCK SUFFICIENT | SUBMIT MANUALLY IN BASE"
                : "WISH STOCK INSUFFICIENT | NO EXTRA PENALTY"));
    }

    p.pageCount = uiRaidResultPageCount(p.returned.size());
    p.page = std::min(state.page, p.pageCount - 1U);
    return p;
}

std::string uiRaidResultViewKey(const UiRaidResultState &state) {
    Key key;
    key.number(state.page);
    key.number(state.details);
    key.number(state.detailsScroll);
    key.number(state.feedbackScroll);
    return std::move(key.value);
}

std::string uiRaidResultActionKey(const UiRaidResultHit &hit,
    const UiRaidResultState &state, const UiRaidResultProjection &projection) {
    Key key;
    key.add(projection.contextKey);
    key.add(uiRaidResultViewKey(state));
    key.number(hit.control);
    return std::move(key.value);
}

std::size_t uiRaidResultPageSize() noexcept { return 8U; }
std::size_t uiRaidResultPageCount(std::size_t count) noexcept {
    return count ? 1U + (count - 1U) / uiRaidResultPageSize() : 1U;
}

bool uiRaidResultEnabled(C control, const UiRaidResultState &,
    const UiRaidResultProjection &p) noexcept {
    switch (control) {
    case C::Previous: return p.hasResult && p.extracted && p.page > 0U;
    case C::Next: return p.hasResult && p.extracted && p.page < p.pageCount - 1U;
    case C::Details: return p.hasResult && !p.detailLines.empty();
    case C::Return: return true;
    }
    return false;
}

UiRaidResultLayout layoutUiRaidResult() noexcept {
    UiRaidResultLayout layout;
    layout.panel = {70, 40, 1140, 640};
    layout.title = {94, 62, 650, 36};
    layout.status = {766, 67, 420, 30};
    layout.summary = {94, 110, 1092, 64};
    layout.recordHeading = {94, 190, 696, 24};
    layout.recordNote = {94, 216, 696, 32};
    layout.records = {94, 256, 696, 280};
    layout.detailsViewport = {814, 238, 372, 298};
    layout.feedback = {94, 600, 776, 50};
    layout.previous = {600, 552, 46, 28};
    layout.next = {710, 552, 46, 28};
    layout.pageLabel = {646, 552, 64, 28};
    layout.toggle = {1020, 190, 164, 32};
    layout.returnButton = {904, 606, 260, 42};
    for (std::size_t index{}; index < layout.rows.size(); ++index)
        layout.rows[index] = {94 + 360.0F * static_cast<float>(index % 2U),
            256 + 70.0F * static_cast<float>(index / 2U), 336, 62};
    return layout;
}

SDL_FRect uiRaidResultPanelRect() noexcept { return layoutUiRaidResult().panel; }
SDL_FRect uiRaidResultControlRect(C control) noexcept {
    const auto layout = layoutUiRaidResult();
    switch (control) {
    case C::Previous: return layout.previous;
    case C::Next: return layout.next;
    case C::Details: return layout.toggle;
    case C::Return: return layout.returnButton;
    }
    return {};
}
SDL_FRect uiRaidResultRowRect(std::size_t row) noexcept {
    const auto layout = layoutUiRaidResult();
    return row < layout.rows.size() ? layout.rows[row] : SDL_FRect{};
}
SDL_FRect uiRaidResultDetailsRect() noexcept { return layoutUiRaidResult().detailsViewport; }
SDL_FRect uiRaidResultFeedbackRect() noexcept { return layoutUiRaidResult().feedback; }

std::optional<UiRaidResultHit> hitUiRaidResult(Vec2 point,
    const UiRaidResultState &, const UiRaidResultProjection &) {
    for (auto control : {C::Previous, C::Next, C::Details, C::Return})
        if (uiContains(uiRaidResultControlRect(control), point)) return UiRaidResultHit{control};
    return {};
}
