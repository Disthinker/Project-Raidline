#pragma once

#include "base_population_domain.h"
#include "base_workforce_domain.h"
#include "ui_item_context_presentation.h"

enum class UiDormitoryTab { Residents, Rest };
enum class UiDormitoryControl {
    Residents, Rest, Rations, AutoFill, StartExpansion, CancelExpansion,
    Hour1, Hour6, Hour12, StartRest, Close
};
enum class UiDormitoryEventKind { Down, Up, Move, Wheel };
struct UiDormitoryEvent {
    UiDormitoryEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiDormitoryProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    WorldClockProjection clock;
    BasePopulationProjection population;
    BaseResourceBundle dailyDemand;
    std::uint32_t foodStock{};
    BaseWorkforceProjection workforce;
    BaseWorkforcePlan autoFill;
    BaseConstructionProjection construction;
    std::optional<BaseConstructionProjectDefinition> project;
    std::optional<ActiveBaseConstructionProject> activeProject;
    bool ownExpansion{}, expansionComplete{};
    BaseConstructionPlan start, cancel;
    std::optional<std::uint32_t> restHours;
    BaseRestPlan rest;
};
struct UiDormitoryPress {
    UiDormitoryControl control{};
    UiLanguage language{};
    std::string action;
};
struct UiDormitoryState {
    UiDormitoryTab tab{UiDormitoryTab::Residents};
    bool rations{};
    std::optional<std::uint32_t> restHours;
    std::optional<UiDormitoryPress> press;
    int detailsScroll{}, detailsMaximum{}, feedbackScroll{}, feedbackMaximum{};
};
[[nodiscard]] UiDormitoryProjection projectUiDormitory(
    const ProfileState &, const ContentRegistry &, std::optional<std::uint32_t>);
[[nodiscard]] SDL_FRect uiDormitoryControlRect(UiDormitoryControl) noexcept;
[[nodiscard]] SDL_FRect uiDormitoryDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiDormitoryFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiDormitoryControl> hitUiDormitory(Vec2, const UiDormitoryState &,
    const UiDormitoryProjection &);
[[nodiscard]] bool uiDormitoryEnabled(UiDormitoryControl, const UiDormitoryProjection &);
[[nodiscard]] std::string uiDormitoryUnavailable(UiDormitoryControl, const UiDormitoryProjection &);
[[nodiscard]] std::string uiDormitoryActionKey(UiDormitoryControl, const UiDormitoryProjection &);
