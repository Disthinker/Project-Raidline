#pragma once

#include "base_morale_domain.h"

inline constexpr std::uint32_t kBaseEmergencyPowerBatchMinutes = 180U;
inline constexpr std::uint32_t kBaseEmergencyPowerThreatPeriodMinutes = 60U;

struct StartBaseEmergencyPowerCommand
{
    BaseEmergencyPowerTarget target{};
    AssetInstanceId fuelAssetId{};
    BaseServiceJobId jobId{};
};

struct PauseBaseEmergencyPowerCommand {};
struct ResumeBaseEmergencyPowerCommand { BaseServiceJobId jobId{}; };

using BaseEmergencyPowerCommand = std::variant<
    StartBaseEmergencyPowerCommand,
    PauseBaseEmergencyPowerCommand,
    ResumeBaseEmergencyPowerCommand>;

struct BaseEmergencyPowerPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
};

struct BaseEmergencyPowerReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
};

[[nodiscard]] BaseEmergencyPowerPlan queryBaseEmergencyPower(
    const ProfileState &profile, const BaseEmergencyPowerCommand &command);
[[nodiscard]] BaseEmergencyPowerReceipt executeBaseEmergencyPower(
    ProfileState &profile, const ContentRegistry &content,
    const BaseEmergencyPowerCommand &command, const CommandContext &context);

[[nodiscard]] std::optional<BaseServiceJobId> baseEmergencyPowerJob(
    const ProfileState &profile, BaseEmergencyPowerTarget target) noexcept;
[[nodiscard]] std::optional<AssetInstanceId> baseEmergencyPowerFuel(
    const ProfileState &profile) noexcept;
[[nodiscard]] std::uint64_t remainingBaseEmergencyPowerWork(
    std::uint64_t completionWorldMinute, std::uint32_t bonusMinutes,
    std::uint64_t worldMinute) noexcept;

void pauseBaseEmergencyPower(ProfileState &profile) noexcept;
void pauseBaseEmergencyPowerForFacility(
    ProfileState &profile, const BaseFacilityDefinitionId &facility) noexcept;

struct BaseWorldWorkAdvanceResult
{
    BaseDailyDemandResult demand;
    BaseConstructionAdvanceResult construction;
    BaseManufacturingAdvanceResult manufacturing;
    ResidentTreatmentAdvanceResult residentTreatment;
};

// Called after the owned world clock advances. The original construction and
// job consumers also run when there is no active batch or generator project.
[[nodiscard]] BaseWorldWorkAdvanceResult synchronizeBaseWorkThrough(
    ProfileState &profile, const ContentRegistry &content, std::uint64_t minutesAdvanced);
