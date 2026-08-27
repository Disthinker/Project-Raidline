#pragma once

#include "base_construction_domain.h"

struct BaseSiteFeatureRepairCommand
{
    RegionalBaseSiteDefinitionId siteDefinitionId;
};

struct BaseSiteFeatureRepairPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalBaseSiteDefinitionId siteDefinitionId;
    std::uint32_t materialUnits{};
    std::uint32_t workerCount{};
    std::uint32_t durationMinutes{};
    std::uint32_t manufacturingDurationPercent{100U};
    WorldClockProjection arrival;
};

struct BaseSiteFeatureRepairReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalBaseSiteDefinitionId siteDefinitionId;
    std::uint32_t materialUnitsConsumed{};
    std::uint64_t worldMinutesApplied{};
    std::uint32_t manufacturingDurationPercent{100U};
};

[[nodiscard]] BaseSiteFeatureRepairPlan queryBaseSiteFeatureRepair(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseSiteFeatureRepairCommand &command) noexcept;

[[nodiscard]] BaseSiteFeatureRepairReceipt executeBaseSiteFeatureRepair(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseSiteFeatureRepairCommand &command,
    const CommandContext &context);

[[nodiscard]] std::uint32_t activeBaseSiteManufacturingDurationPercent(
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;

[[nodiscard]] std::uint32_t applyActiveBaseSiteManufacturingDuration(
    std::uint32_t durationMinutes,
    const ProfileState &profile,
    const ContentRegistry &content) noexcept;
