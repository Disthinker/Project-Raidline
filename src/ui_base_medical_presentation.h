#pragma once

#include "base_construction_domain.h"
#include "base_medical_service_domain.h"
#include "base_resident_medical_domain.h"
#include "base_workforce_domain.h"
#include "ui_foundation.h"

enum class UiBaseMedicalTab { Personal, Residents };
enum class UiBaseMedicalControl
{
    Personal, Residents, Details, Facilities, Staffing, Upgrade, Main, Close,
    ReserveCell, WarehouseRow, PreviousWarehouse, NextWarehouse, ReserveTransfer,
    Priority, AutoAdmission, MoveEarlier, MoveLater
};
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
    std::uint64_t charges{};
};
struct UiBaseMedicalAsset
{
    AssetInstanceId id{};
    StoredAssetLocation location;
    InventoryFootprint footprint;
    std::string name;
    std::uint32_t charges{};
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
    ResidentMedicalReservePolicy reservePolicy;
    std::vector<UiBaseMedicalAsset> warehouseMedicine;
    std::vector<UiBaseMedicalAsset> reserveMedicine;
};
[[nodiscard]] UiBaseMedicalProjection projectUiBaseMedical(
    const ProfileState &, const ContentRegistry &, UiLanguage);
// Bind an explicit press to the semantic action, never a label or vector index.
[[nodiscard]] std::string uiBaseMedicalActionKey(UiBaseMedicalControl, UiBaseMedicalTab, const UiBaseMedicalProjection &);
[[nodiscard]] bool uiBaseMedicalEnabled(UiBaseMedicalControl, UiBaseMedicalTab, const UiBaseMedicalProjection &);
[[nodiscard]] std::string uiBaseMedicalDuration(std::uint64_t, UiLanguage);
struct UiBaseMedicalHit
{
    UiBaseMedicalControl control{};
    std::size_t index{};

    friend bool operator==(const UiBaseMedicalHit &, const UiBaseMedicalHit &) = default;
};

struct UiBaseMedicalPress {
    UiBaseMedicalHit hit;
    UiBaseMedicalTab tab{};
    UiLanguage language{};
    std::string action;
};
struct UiBaseMedicalState {
    UiBaseMedicalTab tab{};
    std::array<bool, 2> details{false, true};
    bool facilities{};
    bool priorityOpen{};
    std::size_t warehousePage{};
    std::optional<AssetInstanceId> selectedMedicine;
    std::optional<GridPosition> reserveDestination;
    int detailScroll{}, detailMaximum{}, feedbackScroll{}, feedbackMaximum{};
    std::optional<UiBaseMedicalPress> press;
};
inline constexpr std::size_t kUiBaseMedicalWarehouseRows = 6;
inline constexpr float kUiBaseMedicalReserveCellSize = 56.0F;
struct UiBaseMedicalTransferProjection
{
    std::optional<MoveMedicalReserveAssetCommand> command;
    std::string message;
    bool canCommit{};
};
[[nodiscard]] const UiBaseMedicalAsset *uiBaseMedicalAsset(
    const UiBaseMedicalProjection &, std::optional<AssetInstanceId>) noexcept;
[[nodiscard]] const UiBaseMedicalAsset *uiBaseMedicalReserveAssetAt(
    const UiBaseMedicalProjection &, GridPosition) noexcept;
[[nodiscard]] UiBaseMedicalTransferProjection projectUiBaseMedicalTransfer(
    const ProfileState &, const ContentRegistry &, const UiBaseMedicalState &,
    const UiBaseMedicalProjection &);
[[nodiscard]] SDL_FRect uiBaseMedicalControlRect(UiBaseMedicalControl,
    UiBaseMedicalTab tab = UiBaseMedicalTab::Personal) noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalDetailsRect(UiBaseMedicalTab tab = UiBaseMedicalTab::Personal) noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalFeedbackRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalReserveRect() noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalWarehouseRowRect(std::size_t) noexcept;
[[nodiscard]] SDL_FRect uiBaseMedicalPriorityButtonRect(UiBaseMedicalControl, std::size_t) noexcept;
[[nodiscard]] GridPosition uiBaseMedicalReserveCell(std::size_t) noexcept;
[[nodiscard]] std::size_t uiBaseMedicalWarehousePageCount(const UiBaseMedicalProjection &) noexcept;
[[nodiscard]] std::optional<UiBaseMedicalHit> hitUiBaseMedical(Vec2, const UiBaseMedicalState &) noexcept;
