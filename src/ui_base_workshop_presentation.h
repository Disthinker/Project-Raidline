#pragma once

#include "base_construction_domain.h"
#include "base_manufacturing_domain.h"
#include "base_workforce_domain.h"
#include "ui_item_context_presentation.h"

inline constexpr std::size_t kUiWorkshopPageSize = 6;
enum class UiWorkshopControl { Main, Cancel, Facilities, Staffing, Upgrade, Close, Previous, Next };
enum class UiWorkshopEventKind { Down, Up, Move, Wheel };
struct UiWorkshopEvent { UiWorkshopEventKind kind{}; Vec2 position{}; int delta{}; };
struct UiWorkshopHit {
    std::optional<BaseManufacturingRecipeDefinitionId> recipe;
    UiWorkshopControl control{UiWorkshopControl::Main};
    bool operator==(const UiWorkshopHit &) const = default;
};
struct UiWorkshopRecipe {
    BaseManufacturingRecipeDefinitionId id;
    ItemDefinitionId output;
    std::uint32_t quantity{};
    std::string shortName, fullName, materials;
    std::uint32_t workers{};
};
struct UiWorkshopProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    bool installed{};
    std::uint32_t level{};
    std::vector<UiWorkshopRecipe> recipes;
    std::optional<BaseManufacturingRecipeDefinitionId> selection;
    std::optional<BaseManufacturingOrder> order;
    BaseManufacturingProjection production;
    BaseManufacturingStartPlan start;
    BaseManufacturingReturnPlan returns;
    BaseWorkforceProjection workforce;
    BaseWorkforcePlan staffing;
    std::optional<BaseConstructionProjectDefinitionId> upgradeId;
    BaseConstructionPlan upgrade;
    bool cancelUpgrade{}, upgradeComplete{};
    std::optional<ActiveBaseConstructionProject> activeUpgrade;
};
struct UiWorkshopPress { UiWorkshopHit hit; UiLanguage language{}; std::string action; };
struct UiWorkshopState {
    std::optional<BaseManufacturingRecipeDefinitionId> selection;
    std::size_t page{};
    bool facilities{};
    int detailScroll{}, detailMaximum{}, feedbackScroll{}, feedbackMaximum{};
    std::optional<UiWorkshopPress> press;
};
[[nodiscard]] UiWorkshopProjection projectUiWorkshop(const ProfileState &, const ContentRegistry &, UiLanguage,
    std::optional<BaseManufacturingRecipeDefinitionId>);
[[nodiscard]] std::string uiWorkshopActionKey(const UiWorkshopHit &, const UiWorkshopProjection &);
[[nodiscard]] bool uiWorkshopEnabled(UiWorkshopControl, const UiWorkshopProjection &);
[[nodiscard]] std::size_t uiWorkshopPageCount(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopControlRect(UiWorkshopControl) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopRecipeRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiWorkshopDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiWorkshopFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiWorkshopHit> hitUiWorkshop(Vec2, const UiWorkshopState &, const UiWorkshopProjection &);
