#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "content_registry.h"
#include "profile_state.h"
#include "ui_foundation.h"

enum class UiRaidResultControl { Previous, Next, Details, Return };
enum class UiRaidResultEventKind { Down, Up, Move, Wheel };
struct UiRaidResultEvent {
    UiRaidResultEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiRaidResultHit {
    UiRaidResultControl control{};
    bool operator==(const UiRaidResultHit &) const = default;
};
struct UiRaidResultPress {
    UiRaidResultHit hit;
    UiLanguage language{};
    std::string action, contextKey;
};
struct UiRaidResultState {
    std::size_t page{};
    bool details{};
    std::optional<UiRaidResultPress> press;
    int detailsScroll{}, detailsMaximum{}, feedbackScroll{}, feedbackMaximum{};
};
struct UiRaidResultItem {
    // One result definition occurrence, not a quantity, asset or current slot.
    std::size_t index{};
    ItemDefinitionId definitionId;
    std::string name;
    bool knownDefinition{};
};
struct UiRaidResultProjection {
    std::string profileId, settlementId, contextKey;
    UiLanguage language{};
    bool hasResult{}, extracted{}, testRange{}, legacyOutcome{};
    RaidResultOutcome outcome{RaidResultOutcome::PlayerDead};
    std::int64_t currencyDelta{};
    std::uint32_t travelMinutesApplied{}, rescuedOrdinaryResidents{},
        rescuedInjuredResidents{}, baseThreatReducedUnits{};
    std::optional<std::string> lostRaidRecordId;
    std::optional<BaseWishReturnSummary> wishReturn;
    std::string title, status, message, recordHeading, recordNote;
    std::vector<std::string> summaryLines, detailLines;
    std::vector<UiRaidResultItem> returned;
    std::size_t page{}, pageCount{1};
};
struct UiRaidResultLayout {
    SDL_FRect panel{}, title{}, status{}, summary{}, recordHeading{}, recordNote{},
        records{}, detailsViewport{}, feedback{}, previous{}, next{}, pageLabel{},
        toggle{}, returnButton{};
    std::array<SDL_FRect, 8> rows{};
};

// Only the historical result and immutable definitions are read. In particular,
// no current inventory/asset ownership is used to reconstruct the result.
[[nodiscard]] UiRaidResultProjection projectUiRaidResult(
    const std::string &profileId, const std::optional<LastRaidResult> &,
    const ContentRegistry &, UiLanguage, const UiRaidResultState &,
    bool testRange = false);
[[nodiscard]] std::string uiRaidResultContextKey(
    const std::string &profileId, const std::optional<LastRaidResult> &,
    UiLanguage, bool testRange = false);
[[nodiscard]] std::string uiRaidResultViewKey(const UiRaidResultState &);
[[nodiscard]] std::string uiRaidResultActionKey(
    const UiRaidResultHit &, const UiRaidResultState &, const UiRaidResultProjection &);
[[nodiscard]] std::size_t uiRaidResultPageSize() noexcept;
[[nodiscard]] std::size_t uiRaidResultPageCount(std::size_t count) noexcept;
[[nodiscard]] bool uiRaidResultEnabled(
    UiRaidResultControl, const UiRaidResultState &, const UiRaidResultProjection &) noexcept;
[[nodiscard]] UiRaidResultLayout layoutUiRaidResult() noexcept;
[[nodiscard]] SDL_FRect uiRaidResultPanelRect() noexcept;
[[nodiscard]] SDL_FRect uiRaidResultControlRect(UiRaidResultControl) noexcept;
[[nodiscard]] SDL_FRect uiRaidResultRowRect(std::size_t visibleRow) noexcept;
[[nodiscard]] SDL_FRect uiRaidResultDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiRaidResultFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiRaidResultHit> hitUiRaidResult(
    Vec2, const UiRaidResultState &, const UiRaidResultProjection &);
