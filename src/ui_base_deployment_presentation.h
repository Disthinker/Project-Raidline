#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "base_wish_expedition.h"
#include "loadout_progression.h"
#include "lost_raid_domain.h"
#include "raid_intelligence_domain.h"
#include "raid_lifecycle.h"
#include "ui_foundation.h"
#include "weapon_supply_projection.h"

enum class UiDeploymentTab { Briefing, Intelligence };
enum class UiDeploymentControl {
    Map, MapPrevious, MapNext, Briefing, Intelligence, BriefingDetails,
    IntelligenceRow, IntelligenceAction, Interior, InteriorAction,
    InteriorPrevious, InteriorNext, LoadoutPrevious, LoadoutNext,
    ReadinessDetails, RecoveryClear, Deploy, Close, Regional, LostRecords
};
enum class UiDeploymentEventKind { Down, Up, Move, Wheel };
struct UiDeploymentEvent {
    UiDeploymentEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiDeploymentHit {
    UiDeploymentControl control{};
    std::size_t index{}; // Absolute published-list index; IDs are the authority.
    MapDefinitionId mapId;
    RaidIntelligenceCategory category{RaidIntelligenceCategory::Transport};
    RaidSpaceDefinitionId interiorId;
    bool operator==(const UiDeploymentHit &) const = default;
};
struct UiDeploymentPress {
    UiDeploymentHit hit;
    UiLanguage language{};
    std::string action, contextKey;
};
struct UiDeploymentState {
    UiDeploymentTab tab{UiDeploymentTab::Briefing};
    std::size_t mapPage{}, interiorPage{};
    std::optional<RaidIntelligenceCategory> selectedIntelligence;
    std::optional<RaidSpaceDefinitionId> selectedInterior;
    bool briefingDetails{}, readinessDetails{};
    std::optional<UiDeploymentPress> press;
    std::optional<std::string> confirmationKey;
    int briefingScroll{}, briefingMaximum{}, readinessScroll{}, readinessMaximum{};
    int feedbackScroll{}, feedbackMaximum{};
};
// Metadata only. No layout, entry, enemy or loot coordinates are projected.
struct UiDeploymentMap {
    MapDefinitionId id;
    std::string name, difficulty, warning, routeProfile;
};
struct UiDeploymentIntelligence {
    RaidIntelligenceCategory category{RaidIntelligenceCategory::Transport};
    std::string name;
    std::uint32_t owned{};
    bool selected{};
    RaidIntelligencePurchasePlan purchase;
    // Failed queries with their default zero do not advertise a free purchase.
    std::optional<std::uint32_t> price;
};
struct UiDeploymentInterior {
    RaidSpaceDefinitionId id;
    std::string name;
    bool known{};
    RaidInteriorIntelligencePurchasePlan purchase;
    std::optional<std::uint32_t> price;
};
struct UiDeploymentProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    UiLanguage language{};
    std::uint32_t currency{};
    std::vector<UiDeploymentMap> maps;
    MapDefinitionId selectedMapId;
    std::size_t selectedMapIndex{}, mapPage{}, interiorPage{};
    bool mapValid{};
    std::string mapName, difficulty, warning, routeProfile, routeName;
    RaidTravelPreview travel;
    std::array<UiDeploymentIntelligence, kRaidIntelligenceCategoryCount> intelligence;
    std::vector<UiDeploymentInterior> interiors;
    std::optional<RaidIntelligenceCategory> selectedIntelligence;
    std::optional<RaidSpaceDefinitionId> selectedInterior;
    std::vector<LoadoutReadinessProjection> loadouts;
    std::size_t selectedLoadoutIndex{};
    std::optional<LoadoutReadinessProjection> selectedLoadout;
    WeaponSupplyProjection weaponSupply;
    LostRaidAgingPreview aging;
    std::optional<std::string> recoveryRecordId;
    std::optional<LostRaidRecordProjection> recovery;
    bool recoveryValid{}, recoveryMapMatches{};
    BaseWishExpeditionRelevanceProjection wish;
    std::string wishFocusName;
    bool cannotFire{}, expiryRisk{}, requiresConfirmation{};
    std::vector<std::string> riskReasons;
    // Binds risk confirmation and presses to current facts, including changes
    // made without a revision bump. Re-query before executing an action.
    std::string contextKey;
};

[[nodiscard]] UiDeploymentProjection projectUiDeployment(
    const ProfileState &, const ContentRegistry &, UiLanguage,
    const MapDefinitionId &, const RaidIntelligenceLoadout &,
    const std::optional<std::string> &recoveryRecordId, std::size_t loadoutIndex,
    const UiDeploymentState &);
[[nodiscard]] std::string uiDeploymentViewKey(const UiDeploymentState &);
[[nodiscard]] std::string uiDeploymentActionKey(
    const UiDeploymentHit &, const UiDeploymentState &, const UiDeploymentProjection &);
[[nodiscard]] bool uiDeploymentEnabled(
    UiDeploymentControl, const UiDeploymentState &, const UiDeploymentProjection &,
    std::size_t index = 0);
[[nodiscard]] std::string uiDeploymentUnavailable(
    const UiDeploymentHit &, const UiDeploymentState &, const UiDeploymentProjection &);
[[nodiscard]] std::size_t uiDeploymentMapPageSize() noexcept;
[[nodiscard]] std::size_t uiDeploymentInteriorPageSize() noexcept;
[[nodiscard]] std::size_t uiDeploymentPageCount(std::size_t count, std::size_t pageSize) noexcept;
[[nodiscard]] SDL_FRect uiDeploymentPanelRect() noexcept;
[[nodiscard]] SDL_FRect uiDeploymentControlRect(UiDeploymentControl) noexcept;
[[nodiscard]] SDL_FRect uiDeploymentMapRect(std::size_t visibleRow) noexcept;
[[nodiscard]] SDL_FRect uiDeploymentIntelligenceRect(std::size_t row, bool action = false) noexcept;
[[nodiscard]] SDL_FRect uiDeploymentInteriorRect(std::size_t visibleRow, bool action = false) noexcept;
[[nodiscard]] SDL_FRect uiDeploymentBriefingRect() noexcept;
[[nodiscard]] SDL_FRect uiDeploymentIntelligenceDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiDeploymentReadinessRect() noexcept;
[[nodiscard]] SDL_FRect uiDeploymentFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiDeploymentHit> hitUiDeployment(
    Vec2, const UiDeploymentState &, const UiDeploymentProjection &);
