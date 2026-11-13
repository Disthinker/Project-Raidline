#pragma once

#include "base_resource_domain.h"

struct ResidentMedicalSupplySelection
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{};
    std::uint32_t contribution{};
    std::uint32_t charges{};

    friend bool operator==(const ResidentMedicalSupplySelection &,
                           const ResidentMedicalSupplySelection &) = default;
};

struct MoveMedicalReserveAssetCommand
{
    AssetInstanceId assetId{};
    StoredAssetLocation source;
    StoredAssetLocation destination;
};

struct SetResidentMedicalReservePolicyCommand
{
    ResidentMedicalReservePolicy policy;
};

struct MedicalReservePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
};

struct MedicalReserveReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
};

struct BaseResidentMedicalProjection
{
    std::uint32_t ordinaryResidents{};
    std::uint32_t injuredResidents{};
    std::uint32_t healthyResidents{};
    bool treatmentActive{};
    std::uint64_t remainingMinutes{};
};

struct StartResidentTreatmentCommand
{
    std::optional<BaseResidentProfession> expectedPatient;
    std::optional<std::vector<ResidentMedicalSupplySelection>> expectedSupplies;
};

struct ResidentTreatmentPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    std::uint32_t requiredContribution{};
    std::uint32_t plannedContribution{};
    std::uint32_t durationMinutes{};
    BaseResidentProfession patientProfession{BaseResidentProfession::General};
    BaseResidentProfession workerProfession{BaseResidentProfession::General};
    std::vector<ResidentMedicalSupplySelection> supplies;
    bool usesMedicalReserve{};
};

struct ResidentTreatmentReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseServiceJobId jobId{};
    std::uint32_t consumedContribution{};
    std::uint64_t completionWorldMinute{};
};

struct ResidentTreatmentAdvanceResult
{
    bool completed{};
    BaseServiceJobId jobId{};
    std::uint32_t injuredResidentsAfter{};
};

[[nodiscard]] BaseResidentMedicalProjection projectBaseResidentMedical(
    const ProfileState &profile) noexcept;

[[nodiscard]] MedicalReservePlan queryMoveMedicalReserveAsset(
    const ProfileState &profile,
    const ContentRegistry &content,
    const MoveMedicalReserveAssetCommand &command);

[[nodiscard]] MedicalReserveReceipt executeMoveMedicalReserveAsset(
    ProfileState &profile,
    const ContentRegistry &content,
    const MoveMedicalReserveAssetCommand &command,
    const CommandContext &context);

[[nodiscard]] MedicalReserveReceipt executeSetResidentMedicalReservePolicy(
    ProfileState &profile,
    const ContentRegistry &content,
    const SetResidentMedicalReservePolicyCommand &command,
    const CommandContext &context);

[[nodiscard]] ResidentTreatmentPlan queryStartResidentTreatment(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &command = {});

[[nodiscard]] ResidentTreatmentReceipt executeStartResidentTreatment(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &command,
    const CommandContext &context);

// Called only on an isolated candidate at an authorized Base safe-core boundary.
// The enclosing operation owns publication and durable saving.
[[nodiscard]] bool tryStartAutomaticResidentTreatment(
    ProfileState &candidate,
    const ContentRegistry &content);

// Applies a due job to an already-owned candidate Profile. The enclosing time
// command owns revision advancement and persistence.
[[nodiscard]] ResidentTreatmentAdvanceResult applyResidentTreatmentThrough(
    ProfileState &profile) noexcept;
