#pragma once

#include "base_construction_domain.h"
#include "base_facility_layout_domain.h"
#include "base_ground_domain.h"
#include "base_manufacturing_domain.h"
#include "base_medical_service_domain.h"
#include "base_migration_domain.h"
#include "base_population_domain.h"
#include "base_resident_medical_domain.h"
#include "base_resource_domain.h"
#include "base_service_domain.h"
#include "base_siege_domain.h"
#include "base_site_feature_domain.h"
#include "base_workforce_domain.h"
#include "economy_domain.h"
#include "base_fortification_placement.h"
#include "home_founding_domain.h"
#include "maintenance_domain.h"
#include "medical_domain.h"
#include "raid_action.h"
#include "tactical_device_domain.h"
#include "weapon_ammo_domain.h"
#include "weapon_component_domain.h"
#include "raid_intelligence_domain.h"
#include "recovery_task_domain.h"
#include "regional_operations_domain.h"
#include <optional>
#include <string>
#include <variant>

class BaseWorld;

// Native domain commands are reused directly. Only operations that had no
// value command (or shared one shape for two operations) need these adapters.
struct ProfileUiAssignWorkerCommand { BaseFacilityStaffingKind facility{}; };
struct ProfileUiClearWorkerCommand { BaseFacilityStaffingKind facility{}; };
struct ProfileUiAutoFillWorkersCommand {};
struct ProfileUiWishFocusCommand { std::optional<BaseWishInstanceId> focus; };
struct ProfileUiCancelManufacturingCommand {};
struct ProfileUiCollectManufacturingCommand {};
struct ProfileUiCollectGunsmithCommand {};
struct ProfileUiStartRecoveryCommand { std::string recordId; };
struct ProfileUiCancelRecoveryCommand {};
struct ProfileUiCollectRecoveryCommand {};
struct ProfileUiAutoDefenseCommand {};
struct ProfileUiPlaceablePurchaseCommand { ItemDefinitionId definitionId; };
struct ProfileUiHintsCommand { bool finishFirstRaid{}; };
struct ProfileUiBaseMedicalCommand { AssetInstanceId assetId{}; };
struct ProfileUiBaseHealCommand { AssetInstanceId assetId{}; };
struct ProfileUiEstablishHomeCommand { std::string plotId; RegionalBaseSiteDefinitionId region; Vec2 playerCenter; };

using ProfileUiCommand = std::variant<
    EconomyCommand,
    BaseRestCommand,
    ContributeBaseAssetCommand,
    SetBaseSupplyAssignmentCommand,
    ContributeConstructionMaterialCommand,
    StartBaseConstructionCommand,
    CancelBaseConstructionCommand,
    BaseMigrationCommand,
    BaseSiteFeatureRepairCommand,
    InstallBaseFacilityCommand,
    EstablishRegionalOutpostCommand,
    RegionalOutpostStaffingCommand,
    SubmitBasePriorityCommand,
    StartBaseManufacturingCommand,
    BaseMedicalServiceCommand,
    StartResidentTreatmentCommand,
    StartGunsmithMaintenanceCommand,
    RaidIntelligencePurchaseCommand,
    RaidInteriorIntelligencePurchaseCommand,
    BaseGroundCommand,
    RepositionBaseFacilityCommand,
    InstallBaseFacilityAtCommand,
    ProfileUiAssignWorkerCommand,
    ProfileUiClearWorkerCommand,
    ProfileUiAutoFillWorkersCommand,
    ProfileUiWishFocusCommand,
    ProfileUiCancelManufacturingCommand,
    ProfileUiCollectManufacturingCommand,
    ProfileUiCollectGunsmithCommand,
    ProfileUiStartRecoveryCommand,
    ProfileUiCancelRecoveryCommand,
    ProfileUiCollectRecoveryCommand,
    ProfileUiAutoDefenseCommand,
    ProfileUiPlaceablePurchaseCommand,
    WeaponAmmoCommand,
    WeaponMaintenanceCommand,
    ArmorMaintenanceCommand,
    WeaponComponentCommand,
    DeviceBatteryCommand,
    FortificationCommand,
    InstallFortificationCommand,
    ProfileUiBaseMedicalCommand,
    ProfileUiBaseHealCommand,
    ProfileUiEstablishHomeCommand,
    ProfileUiHintsCommand>;

struct ProfileUiPlaceablePurchaseReceipt
{
    bool succeeded{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    AssetInstanceId assetId{};
    std::int64_t currencyDelta{};
    bool alreadyCommitted{};
};

using ProfileUiReceipt = std::variant<
    EconomyReceipt,
    BaseRestReceipt,
    BaseResourceReceipt,
    BaseSupplyAssignmentReceipt,
    ConstructionMaterialReceipt,
    BaseConstructionReceipt,
    BaseMigrationReceipt,
    BaseSiteFeatureRepairReceipt,
    InstallBaseFacilityReceipt,
    RegionalOutpostReceipt,
    RegionalOutpostStaffingReceipt,
    BasePriorityReceipt,
    BaseManufacturingReceipt,
    BaseMedicalServiceReceipt,
    ResidentTreatmentReceipt,
    GunsmithMaintenanceReceipt,
    GunsmithCollectionReceipt,
    RaidIntelligencePurchaseReceipt,
    RaidInteriorIntelligencePurchaseReceipt,
    BaseGroundReceipt,
    BaseFacilityLayoutReceipt,
    BaseWorkforceReceipt,
    RecoveryTaskReceipt,
    BaseAutoDefenseReceipt,
    ProfileUiPlaceablePurchaseReceipt,
    WeaponAmmoReceipt,
    WeaponMaintenanceReceipt,
    ArmorMaintenanceReceipt,
    InventoryReceipt,
    FortificationReceipt,
    MedicalUseReceipt,
    HealReceipt,
    HomeFoundingReceipt>;

// Facts are published only after the prepared candidate becomes durable.
// They never contain a second Profile, a callback, or mutable world ownership.
struct ProfileUiPresentationFacts
{
    bool medicalCompleted{};
    bool refreshLoadoutTutorial{};
    bool resetWorldClockAccumulator{};
    bool synchronizeBaseSite{};
    bool resetWeaponConfiguration{};
    bool clearWeaponOverrides{};
    bool releaseWeaponTrigger{};
    bool weaponMagazineRemoved{};
    bool weaponMagazineInserted{};
    bool weaponChambered{};
    std::optional<BaseRestReceipt> restCompletion;
};
struct ProfileUiCommandResult
{
    ProfileUiReceipt receipt;
    std::optional<AssetInstanceId> affectedAssetId;
    ProfileUiPresentationFacts facts;
};
struct ProfileUiPrepared
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileUiCommandResult result;
};

[[nodiscard]] bool profileUiReceiptSucceeded(const ProfileUiReceipt &) noexcept;
[[nodiscard]] bool profileUiReceiptAlreadyCommitted(const ProfileUiReceipt &) noexcept;
[[nodiscard]] DomainErrorCode profileUiReceiptError(const ProfileUiReceipt &) noexcept;
[[nodiscard]] const std::string &profileUiReceiptMessage(const ProfileUiReceipt &) noexcept;
[[nodiscard]] ProfileRevision profileUiReceiptRevision(const ProfileUiReceipt &) noexcept;

// The caller owns an isolated candidate and discards it on failure. This
// dispatcher performs the selected domain transaction exactly once; persistence
// and world/activity transitions remain the GameSession owner's responsibility.
[[nodiscard]] ProfileUiPrepared prepareProfileUiCommand(
    ProfileState &privateCandidate, const ContentRegistry &, const ProfileUiCommand &,
    const CommandContext &, const BaseWorld *currentWorld = nullptr);
