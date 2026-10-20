#pragma once

#include "base_construction_domain.h"
#include "base_medical_service_domain.h"
#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"
#include "ui_foundation.h"

enum class UiBaseMedicalTab { Personal, Residents };
enum class UiBaseMedicalControl { Personal, Residents, Details, Facilities, Staffing, Upgrade, Main, Close };
enum class UiBaseMedicalEventKind { Down, Up, Move, Wheel };
struct UiBaseMedicalEvent {
    UiBaseMedicalEventKind kind{};
    Vec2 position{};
    int delta{};
};
struct UiBaseMedicalSupplyRow {
    ItemDefinitionId definitionId;
    std::string name;
    std::uint64_t quantity{};
};
struct UiBaseMedicalProjection {
    std::string profileId;
    ProfileRevision revision{};
    std::uint64_t worldMinute{};
    std::uint32_t currency{}, level{};
    int health{};
    MedicalStatusState medical;
    BaseMedicalServicePlan player;
    ResidentTreatmentPlan resident;
    BaseResidentMedicalProjection population;
    BaseWorkforceProjection workforce;
    BaseWorkforcePlan staffing;
    std::optional<BaseConstructionProjectDefinitionId> upgradeId;
    BaseConstructionPlan upgrade;
    bool cancelUpgrade{}, upgradeComplete{};
    std::optional<ActiveBaseConstructionProject> activeUpgrade;
    std::optional<ActiveResidentTreatment> activeTreatment;
    std::vector<UiBaseMedicalSupplyRow> supplies;
};
[[nodiscard]] UiBaseMedicalProjection projectUiBaseMedical(
    const ProfileState &, const ContentRegistry &, UiLanguage);
// Bind an explicit press to the semantic action, never a label or vector index.
[[nodiscard]] std::string uiBaseMedicalActionKey(UiBaseMedicalControl, UiBaseMedicalTab, const UiBaseMedicalProjection &);
[[nodiscard]] bool uiBaseMedicalEnabled(UiBaseMedicalControl, UiBaseMedicalTab, const UiBaseMedicalProjection &);
[[nodiscard]] std::string uiBaseMedicalDuration(std::uint64_t, UiLanguage);
struct UiBaseMedicalPress {
    UiBaseMedicalControl control{};
    UiBaseMedicalTab tab{};
    UiLanguage language{};
    std::string action;
};
struct UiBaseMedicalState {
    UiBaseMedicalTab tab{};
    std::array<bool, 2> details{false, true};
    bool facilities{};
    int detailScroll{}, detailMaximum{}, feedbackScroll{}, feedbackMaximum{};
    std::optional<UiBaseMedicalPress> press;
};
[[nodiscard]] SDL_FRect uiBaseMedicalControlRect(UiBaseMedicalControl) noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalDetailsRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalFeedbackRect() noexcept;
[[nodiscard]] std::optional<UiBaseMedicalControl> hitUiBaseMedical(Vec2, const UiBaseMedicalState &) noexcept;
