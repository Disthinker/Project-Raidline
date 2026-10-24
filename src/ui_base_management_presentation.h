#pragma once

#include "base_construction_domain.h"
#include "base_facility_management.h"
#include "ui_foundation.h"
#include "world_clock.h"

// Client-only facts for the existing construction catalogue. Identity/order
// match the original command adapter; these rows never authorize a command.
enum class UiBaseConstructionCardKind { Project, ReserveFacility, Placeable };
struct UiBaseConstructionCard {
    UiBaseConstructionCardKind kind{};
    std::string identity, title, facts, timing, action, explanation;
    bool enabled{}, active{}, complete{};
    std::optional<AssetInstanceId> firstOwnedAsset;
};
[[nodiscard]] std::vector<UiBaseConstructionCard> projectUiBaseConstructionCards(
    const ProfileState &, const ContentRegistry &, bool purchase, UiLanguage);
[[nodiscard]] std::string uiBaseManagementText(UiLanguage, std::string_view english,
    std::string_view chinese);
[[nodiscard]] std::string uiBaseFacilityTitle(BaseFacilityKind, UiLanguage);
// World labels stay short; full titles and facts remain in the existing pages.
[[nodiscard]] std::string uiBaseFacilityShortName(BaseFacilityKind, UiLanguage);
// These glyphs consume existing facts only. Visual bounds never replace the
// facility's access/work geometry; renderer transforms and pen are preserved.
// A matching worker/resident adds at most one hollow/filled presence figure.
void renderUiBaseEntranceGlyph(SDL_Renderer *, SDL_FRect visualBounds,
    const BaseFacilityWorldServiceProjection &);
void renderUiBaseWorkSocketGlyph(SDL_Renderer *,
    const BaseFacilityWorkSocketProjection &,
    const BaseFacilityWorldServiceProjection &,
    const std::optional<BaseFacilityWorkerWorldProjection> &worker = {},
    const std::optional<BaseResidentWorldProjection> &residents = {},
    const std::optional<BaseResourceFlowWorldProjection> &resourceFlow = {});
[[nodiscard]] std::string uiBaseFacilityStatus(BaseFacilityOperationalStatus, UiLanguage);
[[nodiscard]] std::string uiBaseQuickActionTitle(BaseFacilityQuickActionKind, UiLanguage);
[[nodiscard]] std::string uiBaseOperationTitle(BaseOperationOverviewKind, UiLanguage);
[[nodiscard]] std::string uiBaseMoraleTrendTitle(BaseMoraleTrend, UiLanguage);
[[nodiscard]] std::string uiBaseRemainingTime(std::uint64_t minutes, UiLanguage);
[[nodiscard]] SDL_FRect uiBaseManagementExplanationRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseManagementFeedbackRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseManagementInspectorRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseManagementDetailsButtonRect() noexcept;
// Line offset is transient UI state. Text is fully wrapped before clipping;
// wheel routing changes only this offset, never the underlying action.
void renderUiBaseManagementFeedback(SDL_Renderer *, UiTextRenderer &,
    SDL_FRect, std::string_view, float &lineOffset);

struct UiBaseKitchenFacts {
    bool installed{};
    std::uint32_t level{};
    BaseResourceBundle pool, demand, lastShortfall;
    BaseOperationalProjection operations;
    WorldClockProjection clock;
    std::uint64_t dailyCycles{};
};
[[nodiscard]] UiBaseKitchenFacts projectUiBaseKitchenFacts(const ProfileState &,
    const ContentRegistry &);
