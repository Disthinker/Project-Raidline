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

[[nodiscard]] BaseFacilityManagementProjection
projectBaseFacilityManagement(
    const ProfileState &profile,
    const ContentRegistry &content,
    BaseFacilityKind kind);
