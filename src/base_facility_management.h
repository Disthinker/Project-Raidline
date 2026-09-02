#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base_resource_domain.h"
#include "base_world.h"
#include "content_registry.h"
#include "profile_state.h"

enum class BaseFacilityOperationalStatus
{
    Operational,
    Reserve,
    Unavailable
};

enum class BaseFacilityTaskKind
{
    Idle,
    Construction,
    Manufacturing,
    OutputReady,
    ResidentTreatment
};

enum class BaseFacilityQuickActionKind
{
    OpenFunction,
    AssignBestWorker,
    ClearWorker,
    StartUpgrade,
    CancelUpgrade,
    CollectManufacturing,
    CancelManufacturing,
    StartResidentTreatment,
    AutoFillWorkers
};

struct BaseFacilityQuickActionProjection
{
    BaseFacilityQuickActionKind kind{
        BaseFacilityQuickActionKind::OpenFunction};
    bool canCommit{};
    std::string message;
    std::optional<BaseConstructionProjectDefinitionId>
        constructionProjectId;
};

struct BaseFacilityManagementProjection
{
    BaseFacilityKind kind{BaseFacilityKind::Storage};
    BaseFacilityOperationalStatus status{
        BaseFacilityOperationalStatus::Operational};
    std::optional<std::uint32_t> level;
    bool staffingApplicable{};
    std::optional<BaseResidentProfession> assignedWorker;
    BaseFacilityTaskKind task{BaseFacilityTaskKind::Idle};
    std::uint64_t remainingMinutes{};
    std::vector<BaseFacilityQuickActionProjection> quickActions;
};

enum class BaseFacilityWorldServiceStatus
{
    Ready,
    Working,
    OutputReady,
    NeedsStaff,
    Blocked
};

struct BaseFacilityWorldServiceProjection
{
    BaseFacilityKind facility{BaseFacilityKind::Storage};
    BaseFacilityWorldServiceStatus status{
        BaseFacilityWorldServiceStatus::Ready};
    BaseFacilityTaskKind task{BaseFacilityTaskKind::Idle};
    std::uint64_t remainingMinutes{};
    std::optional<BaseFacilityWorkSocketKind> activeWorkSocket;
};

enum class BaseFacilityWorkerWorldStatus
{
    Missing,
    Idle,
    Working,
    Paused
};

struct BaseFacilityWorkerWorldProjection
{
    BaseFacilityKind facility{BaseFacilityKind::Workshop};
    BaseFacilityWorkSocketKind workSocket{
        BaseFacilityWorkSocketKind::WorkshopBench};
    BaseFacilityWorkerWorldStatus status{
        BaseFacilityWorkerWorldStatus::Missing};
    std::optional<BaseResidentProfession> profession;
    BaseFacilityTaskKind task{BaseFacilityTaskKind::Idle};
    std::uint64_t remainingMinutes{};
};

enum class BaseResidentWorldStatus
{
    Empty,
    Stable,
    Injured,
    Overcrowded
};

struct BaseResidentWorldProjection
{
    BaseFacilityKind facility{BaseFacilityKind::Dormitory};
    BaseFacilityWorkSocketKind workSocket{
        BaseFacilityWorkSocketKind::DormitoryBunk};
    BaseResidentWorldStatus status{BaseResidentWorldStatus::Empty};
    std::uint32_t residents{};
    std::uint32_t healthyResidents{};
    std::uint32_t injuredResidents{};
    std::uint32_t bedCapacity{};
    std::uint32_t bedShortfall{};
    std::uint32_t availableResidents{};
    std::uint32_t assignedResidents{};
    std::uint32_t constructionResidents{};
};

enum class BaseResourceFlowWorldStatus
{
    Empty,
    Available,
    Prepared,
    Shortage
};

struct BaseResourceFlowWorldProjection
{
    BaseFacilityKind facility{BaseFacilityKind::Storage};
    BaseFacilityWorkSocketKind workSocket{
        BaseFacilityWorkSocketKind::StorageHandling};
    BaseResourceFlowWorldStatus status{
        BaseResourceFlowWorldStatus::Empty};
    BaseSupplyReadinessProjection readiness;
};

enum class BaseOperationOverviewKind
{
    OutputReady,
    Construction,
    ResidentTreatment,
    Manufacturing,
    StaffingGap,
    ResourceShortage,
    ResidentPressure,
    BaseWish
};

struct BaseOperationOverviewEntry
{
    BaseFacilityKind facility{BaseFacilityKind::Storage};
    BaseOperationOverviewKind kind{BaseOperationOverviewKind::Construction};
    std::uint64_t remainingMinutes{};
    bool paused{};
};

struct BaseOperationsOverviewProjection
{
    std::vector<BaseOperationOverviewEntry> entries;
};

[[nodiscard]] BaseFacilityManagementProjection
projectBaseFacilityManagement(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind);

[[nodiscard]] BaseFacilityWorldServiceProjection
projectBaseFacilityWorldService(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind);

[[nodiscard]] std::optional<BaseFacilityWorkerWorldProjection>
projectBaseFacilityWorkerWorldStatus(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind);

[[nodiscard]] std::optional<BaseResidentWorldProjection>
projectBaseResidentWorldStatus(
    const ProfileState &profile,
    BaseFacilityKind kind) noexcept;

[[nodiscard]] std::optional<BaseResourceFlowWorldProjection>
projectBaseResourceFlowWorldStatus(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind) noexcept;

[[nodiscard]] BaseOperationsOverviewProjection
projectBaseOperationsOverview(
    const ProfileState &profile,
    const ContentRegistry &content);
