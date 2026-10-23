#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base_migration_domain.h"
#include "base_site_feature_domain.h"
#include "base_siege_domain.h"
#include "regional_operations_domain.h"
#include "ui_foundation.h"

enum class UiRegionalOperationsTab { Routes, Base, Sweep };
enum class UiRegionalOperationsControl {
    Routes, Base, Sweep, Close, Outpost, Site, Previous, Next,
    Establish, Assign, Recall, Restore, ClearSite, Migrate, RepairFeature,
    OpenConstruction, OpenReserve, DeploySweep
};
enum class UiRegionalOperationsEventKind { Down, Up, Move, Wheel };
struct UiRegionalOperationsEvent {
    UiRegionalOperationsEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiRegionalOperationsHit {
    UiRegionalOperationsControl control{};
    std::size_t index{}; // Absolute row index; stable IDs define the command.
    RegionalOutpostDefinitionId outpostId;
    RegionalBaseSiteDefinitionId siteId;
    bool operator==(const UiRegionalOperationsHit &) const = default;
};
struct UiRegionalOperationsPress {
    UiRegionalOperationsHit hit;
    UiLanguage language{};
    std::string action, contextKey;
};
struct UiRegionalOperationsState {
    UiRegionalOperationsTab tab{UiRegionalOperationsTab::Routes};
    std::optional<RegionalOutpostDefinitionId> selectedOutpostId;
    std::optional<RegionalBaseSiteDefinitionId> selectedSiteId;
    std::size_t listPage{};
    std::optional<UiRegionalOperationsPress> press;
    std::optional<std::string> confirmationKey;
    int detailsScroll{}, detailsMaximum{}, feedbackScroll{}, feedbackMaximum{};
};
struct UiRegionalOperationsRoute {
    MapDefinitionId mapDefinitionId;
    std::string mapDisplayName;
    RegionalRoutePlan plan;
};
struct UiRegionalOperationsOutpost {
    RegionalOutpostDefinitionId id;
    RegionNodeDefinitionId nodeId;
    MapDefinitionId restorationMapDefinitionId;
    std::string name;
    bool stateValid{}, unlocked{}, established{}, disrupted{}, activeBase{}, online{};
    std::uint32_t assignedStaff{}, requiredStaff{}, safeOperations{}, usedOperations{}, remainingSafeOperations{};
    RegionalOutpostPlan establish;
    RegionalOutpostStaffingPlan assign, recall;
    bool canRestore{};
    std::string restoreMessage;
};
struct UiRegionalOperationsSite {
    RegionalBaseSiteDefinitionId id;
    RegionNodeDefinitionId nodeId;
    std::optional<RegionalOutpostDefinitionId> outpostId;
    std::optional<MapDefinitionId> clearanceMapDefinitionId;
    std::string name, advantage, disadvantage, uniqueFeature;
    RegionalBaseSiteTier tier{};
    bool stateValid{}, unlocked{}, activeBase{}, featureRepaired{}, featureActive{};
    // Immutable published metadata is distinct from failed-query default zeros.
    std::uint32_t migrationMinutes{}, repairMaterialUnits{}, repairDurationMinutes{},
        repairWorkerCount{1U}, manufacturingDurationPercent{100U}, coreFacilitySlots{};
    RegionalBaseSiteClearancePlan clearance;
    BaseMigrationPlan migration;
    BaseSiteFeatureRepairPlan repair;
};
struct UiRegionalOperationsProjection {
    std::string profileId, activeBaseName, technologyCoreId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    UiLanguage language{};
    bool pendingRaid{}, siegeWarning{}, defenseActive{}, selectionExpired{};
    RegionNodeDefinitionId activeBaseNodeId;
    std::optional<RegionalBaseSiteDefinitionId> activeBaseSiteId;
    std::vector<UiRegionalOperationsRoute> routes;
    std::vector<UiRegionalOperationsOutpost> outposts;
    std::vector<UiRegionalOperationsSite> sites;
    std::optional<RegionalOutpostDefinitionId> selectedOutpostId;
    std::optional<RegionalBaseSiteDefinitionId> selectedSiteId;
    std::optional<UiRegionalOperationsOutpost> selectedOutpost;
    std::optional<UiRegionalOperationsSite> selectedSite;
    std::size_t listPage{};
    std::uint32_t maximumEstablishedOutposts{}, establishedOutposts{}, availableResidents{},
        availableWorkers{}, materialUnits{};
    bool kitchenWaterOwned{}, workshopInReserve{};
    BaseThreatProjection threat;
    BasePerimeterSweepPlan sweep;
    std::string contextKey;
};

[[nodiscard]] UiRegionalOperationsProjection projectUiRegionalOperations(
    const ProfileState &, const ContentRegistry &, UiLanguage,
    const UiRegionalOperationsState &);
[[nodiscard]] std::string uiRegionalOperationsViewKey(const UiRegionalOperationsState &);
[[nodiscard]] std::string uiRegionalOperationsActionKey(
    const UiRegionalOperationsHit &, const UiRegionalOperationsState &,
    const UiRegionalOperationsProjection &);
[[nodiscard]] bool uiRegionalOperationsEnabled(
    UiRegionalOperationsControl, const UiRegionalOperationsState &,
    const UiRegionalOperationsProjection &, std::size_t index = 0);
[[nodiscard]] std::string uiRegionalOperationsUnavailable(
    const UiRegionalOperationsHit &, const UiRegionalOperationsState &,
    const UiRegionalOperationsProjection &);
[[nodiscard]] std::size_t uiRegionalOperationsPageSize() noexcept;
[[nodiscard]] std::size_t uiRegionalOperationsPageCount(std::size_t count) noexcept;
[[nodiscard]] std::size_t uiRegionalOperationsListCount(
    const UiRegionalOperationsState &, const UiRegionalOperationsProjection &) noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsPanelRect() noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsControlRect(UiRegionalOperationsControl) noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsRowRect(std::size_t visibleRow) noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsListRect() noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiRegionalOperationsFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiRegionalOperationsHit> hitUiRegionalOperations(
    Vec2, const UiRegionalOperationsState &, const UiRegionalOperationsProjection &);
