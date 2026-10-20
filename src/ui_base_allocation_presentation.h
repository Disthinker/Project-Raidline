#pragma once

#include <array>
#include "base_construction_domain.h"
#include "base_morale_domain.h"
#include "base_population_domain.h"
#include "base_resource_domain.h"
#include "base_wish_expedition.h"
#include "ui_item_context_presentation.h"

enum class UiAllocationTab { Overview, Supply, Wishes, Materials };
enum class UiAllocationControl {
    Overview, Supply, Wishes, Materials, Close, Details,
    Food, Medical, Recreation, Security, Row, Wish, Previous, Next,
    Main, Focus, ClearFocus, Filter
};
enum class UiAllocationEventKind { Down, Up, Move, Wheel };
struct UiAllocationEvent { UiAllocationEventKind kind{}; Vec2 position{}; int delta{}; };

// Copies only the identity and ownership proof of a displayed asset, never a
// Registry pointer. The parent locations also invalidate a moved container.
struct UiAllocationAsset {
    AssetInstanceId id{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{}, contribution{};
    ItemOrientation orientation{};
    std::vector<std::pair<AssetInstanceId, AssetLocation>> ownership;
    std::string name, source;
    bool operator==(const UiAllocationAsset &) const = default;
};
[[nodiscard]] bool sameUiAllocationAsset(const UiAllocationAsset &, const UiAllocationAsset &);
struct UiAllocationRow {
    ItemDefinitionId definitionId;
    AssetInstanceId instanceId{};
    std::string name, source;
    std::uint64_t quantity{};
    std::uint32_t unit{};
    std::optional<BaseSupplyCategory> assignment;
    std::vector<UiAllocationAsset> assets;
    std::uint64_t selectedQuantity{};
};
struct UiAllocationHit {
    UiAllocationControl control{};
    std::size_t index{};
    ItemDefinitionId definitionId;
    AssetInstanceId assetId{};
    std::optional<BaseWishInstanceId> wish;
    bool operator==(const UiAllocationHit &) const = default;
};
struct UiAllocationPress {
    UiAllocationHit hit;
    UiLanguage language{};
    std::string action;
    std::vector<UiAllocationAsset> subjects;
};
struct UiAllocationState {
    UiAllocationTab tab{UiAllocationTab::Overview};
    BaseSupplyCategory category{BaseSupplyCategory::Food};
    std::array<std::size_t, 4> pages{};
    std::optional<ItemDefinitionId> supplySelection;
    std::optional<BaseWishInstanceId> wish;
    std::vector<UiAllocationAsset> contributions;
    std::optional<UiAllocationAsset> material;
    bool details{}, focusOnly{};
    std::optional<UiAllocationPress> press;
    int detailsScroll{}, detailsMaximum{}, feedbackScroll{}, feedbackMaximum{};
};
struct UiAllocationProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{}, cycle{}, remainingWishMinutes{}, missedWishes{}, eventDays{};
    UiLanguage language{};
    WorldClockProjection clock;
    BaseResourceBundle demand, lastShortfall;
    BaseSupplyReadinessProjection readiness;
    BaseOperationalProjection operations;
    std::array<BaseOperationalTier, 4> tiers{};
    BaseMoraleState morale;
    std::string eventName;
    int eventEffect{};
    std::uint32_t productionPercent{}, frozenPopulation{}, materialStock{}, materialMaximum{};
    std::vector<BasePriorityProjection> wishes;
    std::optional<BaseWishInstanceId> selectedWish, focus;
    std::vector<UiAllocationRow> rows;
    std::vector<UiAllocationAsset> chosen;
    std::optional<UiAllocationAsset> material;
    std::optional<ItemDefinitionId> supplySelection;
    std::optional<BaseSupplyCategory> currentAssignment, targetAssignment;
    BaseSupplyAssignmentPlan assignment;
    BasePriorityPlan submission;
    BaseWishFocusPlan focusPlan, clearFocusPlan;
    ConstructionMaterialPlan processing;
    std::uint32_t selectedContribution{}, requiredContribution{};
    std::uint64_t materialOutput{};
    bool selectionExpired{}, materialExpired{}, wishExpired{};
};

[[nodiscard]] UiAllocationProjection projectUiAllocation(
    const ProfileState &, const ContentRegistry &, UiLanguage, const UiAllocationState &);
[[nodiscard]] std::string uiAllocationViewKey(const UiAllocationState &);
[[nodiscard]] std::string uiAllocationActionKey(const UiAllocationHit &, const UiAllocationState &, const UiAllocationProjection &);
[[nodiscard]] std::vector<UiAllocationAsset> uiAllocationSubjects(const UiAllocationHit &, const UiAllocationProjection &);
[[nodiscard]] bool uiAllocationEnabled(UiAllocationControl, const UiAllocationState &, const UiAllocationProjection &);
[[nodiscard]] std::string uiAllocationUnavailable(UiAllocationControl, const UiAllocationState &, const UiAllocationProjection &);
[[nodiscard]] std::size_t uiAllocationPageSize(UiAllocationTab) noexcept;
[[nodiscard]] std::size_t uiAllocationPageCount(std::size_t, UiAllocationTab) noexcept;
[[nodiscard]] SDL_FRect uiAllocationControlRect(UiAllocationControl) noexcept;
[[nodiscard]] SDL_FRect uiAllocationListRect(UiAllocationTab) noexcept;
[[nodiscard]] SDL_FRect uiAllocationRowRect(UiAllocationTab, std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiAllocationWishRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiAllocationDetailsRect(UiAllocationTab) noexcept;
[[nodiscard]] SDL_FRect uiAllocationFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiAllocationHit> hitUiAllocation(Vec2, const UiAllocationState &, const UiAllocationProjection &);
