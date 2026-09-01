#pragma once

#include <optional>
#include <string>
#include <vector>

#include "inventory_domain.h"

struct BaseFacilityLayoutAccess
{
    RegionalBaseSiteDefinitionId baseSiteDefinitionId;
    ContentRect baseParcel;
    std::vector<ContentRect> blockers;
};

struct RepositionBaseFacilityCommand
{
    BaseFacilityDefinitionId facilityDefinitionId;
    Vec2 worldCenter;
    Vec2 footprint;
    BaseFacilityLayoutAccess access;
};

struct BaseFacilityLayoutPlan
{
    bool canCommit{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseFacilityDefinitionId facilityDefinitionId;
    Vec2 normalizedCenter{};
};

struct BaseFacilityLayoutReceipt
{
    bool succeeded{};
    bool alreadyCommitted{};
    DomainErrorCode error{DomainErrorCode::None};
    std::string message;
    ProfileRevision revision{};
    BaseFacilityDefinitionId facilityDefinitionId;
};

struct BaseFacilitySpatialProjection
{
    BaseFacilityDefinitionId facilityDefinitionId;
    Vec2 worldCenter{};
};

[[nodiscard]] bool isSpatialBaseFacility(
    const BaseFacilityDefinitionId &definitionId) noexcept;

void initializeBaseFacilityLayouts(
    ProfileState &profile,
    const ContentRegistry &content);

[[nodiscard]] BaseFacilityLayoutPlan queryBaseFacilityLayout(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RepositionBaseFacilityCommand &command);

[[nodiscard]] BaseFacilityLayoutReceipt executeBaseFacilityLayout(
    ProfileState &profile,
    const ContentRegistry &content,
    const RepositionBaseFacilityCommand &command,
    const CommandContext &context);

[[nodiscard]] std::vector<BaseFacilitySpatialProjection>
projectBaseFacilityLayout(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    const ContentRect &baseParcel);
