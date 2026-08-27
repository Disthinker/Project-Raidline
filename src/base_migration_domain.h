#pragma once

#include "base_construction_domain.h"

struct BaseMigrationCommand
{
    RegionalBaseSiteDefinitionId targetSiteDefinitionId;
};

struct BaseMigrationPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalBaseSiteDefinitionId sourceSiteDefinitionId;
    RegionalBaseSiteDefinitionId targetSiteDefinitionId;
    std::uint32_t migrationMinutes{};
    WorldClockProjection arrival;
    std::vector<BaseFacilityDefinitionId> missingRequiredFacilities;
    std::vector<BaseFacilityDefinitionId> facilitiesEnteringReserve;
};

struct BaseMigrationReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    RegionalBaseSiteDefinitionId sourceSiteDefinitionId;
    RegionalBaseSiteDefinitionId targetSiteDefinitionId;
    std::uint32_t migrationMinutes{};
    WorldClockProjection arrival;
    std::vector<BaseFacilityDefinitionId> facilitiesInReserve;
};

[[nodiscard]] BaseMigrationPlan queryBaseMigration(
    const ProfileState &profile,
    const ContentRegistry &content,
    const BaseMigrationCommand &command) noexcept;

[[nodiscard]] BaseMigrationReceipt executeBaseMigration(
    ProfileState &profile,
    const ContentRegistry &content,
    const BaseMigrationCommand &command,
    const CommandContext &context);

