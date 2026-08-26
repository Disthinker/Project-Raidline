#pragma once

#include "inventory_domain.h"

enum class BaseFacilityStaffingKind
{
    Workshop,
    Medical
};

struct BaseWorkforceProjection
{
    BaseProfessionCounts residentsByProfession{};
    BaseProfessionCounts injuredByProfession{};
    BaseProfessionCounts availableByProfession{};
    std::optional<BaseResidentProfession> workshopWorker;
    std::optional<BaseResidentProfession> medicalWorker;
    std::uint32_t healthyResidents{};
    std::uint32_t assignedResidents{};
    std::uint32_t availableResidents{};
};

struct BaseFacilityStaffingCommand
{
    BaseFacilityStaffingKind facility{BaseFacilityStaffingKind::Workshop};
};

struct BaseWorkforcePlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseWorkforceState resultingState;
};

struct BaseWorkforceReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseWorkforceState state;
};

[[nodiscard]] constexpr std::size_t baseProfessionIndex(
    BaseResidentProfession profession) noexcept
{
    return static_cast<std::size_t>(profession);
}

[[nodiscard]] const char *baseResidentProfessionName(
    BaseResidentProfession profession) noexcept;

[[nodiscard]] std::uint32_t healthyBaseResidents(
    const BasePopulationState &population) noexcept;

[[nodiscard]] std::uint32_t availableBaseWorkers(
    const ProfileState &profile) noexcept;

[[nodiscard]] bool baseFacilityAcceptsProfession(
    BaseFacilityStaffingKind facility,
    BaseResidentProfession profession) noexcept;

[[nodiscard]] std::uint32_t applyBaseFacilityTaskDuration(
    std::uint32_t baseDurationMinutes,
    BaseFacilityStaffingKind facility,
    BaseResidentProfession profession,
    std::uint32_t facilityLevel,
    const BaseWorkforceDefinition &definition) noexcept;

[[nodiscard]] BaseWorkforceProjection projectBaseWorkforce(
    const ProfileState &profile) noexcept;

[[nodiscard]] BaseWorkforcePlan queryAssignBestBaseWorker(
    const ProfileState &profile,
    const BaseFacilityStaffingCommand &command) noexcept;

[[nodiscard]] BaseWorkforceReceipt executeAssignBestBaseWorker(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseFacilityStaffingCommand &command,
    const CommandContext &context);

[[nodiscard]] BaseWorkforcePlan queryClearBaseWorker(
    const ProfileState &profile,
    const BaseFacilityStaffingCommand &command) noexcept;

[[nodiscard]] BaseWorkforceReceipt executeClearBaseWorker(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseFacilityStaffingCommand &command,
    const CommandContext &context);

[[nodiscard]] BaseWorkforcePlan queryAutoFillBaseWorkers(
    const ProfileState &profile) noexcept;

[[nodiscard]] BaseWorkforceReceipt executeAutoFillBaseWorkers(
    ProfileState &profile,
    const ContentRegistry &content,
    const CommandContext &context);
