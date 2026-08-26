#pragma once

#include "base_resource_domain.h"

struct ResidentMedicalSupplySelection
{
    AssetInstanceId assetId{};
    ItemDefinitionId definitionId;
    std::uint32_t quantity{};
    std::uint32_t contribution{};
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
    std::vector<ResidentMedicalSupplySelection> supplies;
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

[[nodiscard]] ResidentTreatmentPlan queryStartResidentTreatment(
    const ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &command = {});

[[nodiscard]] ResidentTreatmentReceipt executeStartResidentTreatment(
    ProfileState &profile,
    const ContentRegistry &content,
    const StartResidentTreatmentCommand &command,
    const CommandContext &context);

// Applies a due job to an already-owned candidate Profile. The enclosing time
// command owns revision advancement and persistence.
[[nodiscard]] ResidentTreatmentAdvanceResult applyResidentTreatmentThrough(
    ProfileState &profile) noexcept;
