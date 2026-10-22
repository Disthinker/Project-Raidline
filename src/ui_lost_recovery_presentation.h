#pragma once

#include <optional>
#include <string>
#include <vector>

#include "lost_raid_domain.h"
#include "recovery_task_domain.h"
#include "ui_item_context_presentation.h"

enum class UiLostRecoveryTab { Records, Task };
enum class UiLostRecoveryControl {
    Records, Task, Record, Previous, Next, Start, SelfRecovery,
    ClearSelfRecovery, Collect, Cancel, ConfirmCancel, KeepTask, Close
};
enum class UiLostRecoveryEventKind { Down, Up, Move, Wheel };
struct UiLostRecoveryEvent {
    UiLostRecoveryEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiLostRecoveryHit {
    UiLostRecoveryControl control{};
    std::size_t index{}; // Absolute query order; identity is always recordId/taskId.
    std::string recordId;
    RecoveryTaskId taskId{};
    bool operator==(const UiLostRecoveryHit &) const = default;
};
struct UiLostRecoveryPress {
    UiLostRecoveryHit hit;
    UiLanguage language{};
    std::string action, contextKey;
};
struct UiLostRecoveryState {
    UiLostRecoveryTab tab{UiLostRecoveryTab::Records};
    std::optional<std::string> selectedRecordId;
    std::size_t recordPage{};
    std::optional<UiLostRecoveryPress> press;
    std::optional<std::string> confirmationKey;
    int assetScroll{}, assetMaximum{}, feedbackScroll{}, feedbackMaximum{};
};
struct UiLostRecoveryAsset {
    AssetInstanceId id{};
    ItemDefinitionId definitionId;
    std::string name;
    std::uint32_t quantity{};
    bool ownershipRoot{};
    bool operator==(const UiLostRecoveryAsset &) const = default;
};
// No concrete result is exposed before the domain task becomes ready.
struct UiLostRecoveryTask {
    RecoveryTaskId taskId{};
    std::string recordId;
    MapDefinitionId mapDefinitionId;
    std::string mapDisplayName;
    std::uint32_t paidCurrency{};
    std::uint64_t startedWorldMinute{}, completionWorldMinute{}, remainingWorldMinutes{};
    bool readyForCollection{};
    std::uint32_t totalAssetCount{};
    std::optional<std::uint32_t> recoveredAssetCount;
};
struct UiLostRecoveryProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    UiLanguage language{};
    std::uint32_t currency{};
    bool pendingRaid{};
    std::vector<LostRaidRecordProjection> records;
    std::size_t recordPage{};
    std::optional<std::string> selectedRecordId;
    std::optional<LostRaidRecordProjection> selectedRecord;
    std::vector<UiLostRecoveryAsset> assets;
    std::uint64_t totalQuantity{};
    RecoveryTaskQuote quote;
    // Published service metadata, never failed-query default zero values.
    std::optional<std::uint32_t> serviceFee, durationMinutes;
    bool mapValid{}, selectedSelfTarget{}, selectionExpired{};
    std::optional<std::string> selfRecoveryRecordId;
    std::optional<UiLostRecoveryTask> task;
    std::string contextKey;
};

[[nodiscard]] UiLostRecoveryProjection projectUiLostRecovery(
    const ProfileState &, const ContentRegistry &, UiLanguage,
    const std::optional<std::string> &selfRecoveryRecordId,
    const UiLostRecoveryState &);
[[nodiscard]] std::string uiLostRecoveryViewKey(const UiLostRecoveryState &);
[[nodiscard]] std::string uiLostRecoveryActionKey(
    const UiLostRecoveryHit &, const UiLostRecoveryState &, const UiLostRecoveryProjection &);
[[nodiscard]] bool uiLostRecoveryEnabled(
    UiLostRecoveryControl, const UiLostRecoveryState &, const UiLostRecoveryProjection &,
    std::size_t index = 0);
[[nodiscard]] std::string uiLostRecoveryUnavailable(
    const UiLostRecoveryHit &, const UiLostRecoveryState &, const UiLostRecoveryProjection &);
[[nodiscard]] std::size_t uiLostRecoveryPageSize() noexcept;
[[nodiscard]] std::size_t uiLostRecoveryPageCount(std::size_t count) noexcept;
[[nodiscard]] std::size_t uiLostRecoveryAssetVisibleRows() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryPanelRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryControlRect(UiLostRecoveryControl) noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryRecordRect(std::size_t visibleRow) noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryListRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryAssetsRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryAssetRect(std::size_t visibleRow) noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryPlanRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryTaskRect() noexcept;
[[nodiscard]] SDL_FRect uiLostRecoveryFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiLostRecoveryHit> hitUiLostRecovery(
    Vec2, const UiLostRecoveryState &, const UiLostRecoveryProjection &);
