#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

enum class BaseOperationOverviewKind
{
    OutputReady,
    Construction,
    ResidentTreatment,
    Manufacturing,
    StaffingGap
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

[[nodiscard]] BaseOperationsOverviewProjection
projectBaseOperationsOverview(
    const ProfileState &profile,
    const ContentRegistry &content);
