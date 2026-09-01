#include "base_facility_layout_domain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
struct SpatialFacilityDefault
{
    const char *definitionId;
    Vec2 normalizedCenter;
};

constexpr std::array<SpatialFacilityDefault, 4U> kSpatialFacilities{{
    {"base_facility.warehouse", {0.14375F, 0.29464287F}},
    {"base_facility.medical", {0.85625F, 0.7589286F}},
    {"base_facility.dormitory", {0.371875F, 0.7723214F}},
    {"base_facility.workshop", {0.628125F, 0.5401786F}},
}};

bool finitePositive(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        value.x > 0.0F && value.y > 0.0F;
}

bool finitePoint(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finiteRect(const ContentRect &value) noexcept
{
    return finitePoint(value.position) && finitePositive(value.size);
}

bool rectInside(const ContentRect &inner, const ContentRect &outer) noexcept
{
    return finiteRect(inner) && finiteRect(outer) &&
        inner.position.x >= outer.position.x &&
        inner.position.y >= outer.position.y &&
        inner.position.x + inner.size.x <=
            outer.position.x + outer.size.x &&
        inner.position.y + inner.size.y <=
            outer.position.y + outer.size.y;
}

bool rectsOverlap(const ContentRect &left, const ContentRect &right) noexcept
{
    return left.position.x < right.position.x + right.size.x &&
        left.position.x + left.size.x > right.position.x &&
        left.position.y < right.position.y + right.size.y &&
        left.position.y + left.size.y > right.position.y;
}

ContentRect centeredBounds(Vec2 center, Vec2 footprint) noexcept
{
    return ContentRect{
        {center.x - footprint.x * 0.5F,
         center.y - footprint.y * 0.5F},
        footprint};
}

std::optional<Vec2> defaultNormalizedCenter(
    const BaseFacilityDefinitionId &definitionId) noexcept
{
    const auto found = std::find_if(
        kSpatialFacilities.begin(), kSpatialFacilities.end(),
        [&](const SpatialFacilityDefault &candidate)
        { return definitionId.value() == candidate.definitionId; });
    return found == kSpatialFacilities.end()
        ? std::nullopt
        : std::optional<Vec2>{found->normalizedCenter};
}

BaseFacilityLayoutReceipt failure(
    DomainErrorCode error,
    std::string message,
    ProfileRevision revision,
    BaseFacilityDefinitionId definitionId)
{
    return BaseFacilityLayoutReceipt{
        false, false, error, std::move(message), revision,
        std::move(definitionId)};
}
}

bool isSpatialBaseFacility(
    const BaseFacilityDefinitionId &definitionId) noexcept
{
    return defaultNormalizedCenter(definitionId).has_value();
}

void initializeBaseFacilityLayouts(
    ProfileState &profile,
    const ContentRegistry &content)
{
    for (const RegionalBaseSiteDefinition &site :
         content.regionalOperations().baseSites)
    {
        auto &siteLayout = profile.baseFacilityLayout.placements[site.id];
        for (const SpatialFacilityDefault &facility : kSpatialFacilities)
        {
            const BaseFacilityDefinitionId definitionId{
                facility.definitionId};
            if (profile.baseConstruction.facilities.contains(definitionId))
                siteLayout.try_emplace(
                    definitionId, facility.normalizedCenter);
        }
    }
}

BaseFacilityLayoutPlan queryBaseFacilityLayout(
    const ProfileState &profile,
    const ContentRegistry &content,
    const RepositionBaseFacilityCommand &command)
{
    BaseFacilityLayoutPlan result{
        false, DomainErrorCode::IllegalDestination,
        "Base facility placement is not valid", profile.revision,
        command.facilityDefinitionId, {}};
    if (profile.pendingRaid.has_value() ||
        command.access.baseSiteDefinitionId !=
            profile.regionalOperations.technologyCore.baseSiteDefinitionId ||
        !isSpatialBaseFacility(command.facilityDefinitionId) ||
        !finitePoint(command.worldCenter) ||
        !finitePositive(command.footprint) ||
        !finiteRect(command.access.baseParcel))
    {
        return result;
    }
    try
    {
        static_cast<void>(content.baseFacility(
            command.facilityDefinitionId));
        static_cast<void>(content.regionalBaseSite(
            command.access.baseSiteDefinitionId));
    }
    catch (...)
    {
        return result;
    }
    const auto owned = profile.baseConstruction.facilities.find(
        command.facilityDefinitionId);
    if (owned == profile.baseConstruction.facilities.end() ||
        owned->second !=
            BaseConstructionState::FacilityPlacement::Installed)
    {
        result.message = "Base facility is not installed";
        return result;
    }
    const ContentRect proposed = centeredBounds(
        command.worldCenter, command.footprint);
    if (!rectInside(proposed, command.access.baseParcel) ||
        std::any_of(
            command.access.blockers.begin(),
            command.access.blockers.end(),
            [&](const ContentRect &blocker)
            { return finiteRect(blocker) && rectsOverlap(proposed, blocker); }))
    {
        result.message = "Base facility placement is blocked";
        return result;
    }
    result.canCommit = true;
    result.error = DomainErrorCode::None;
    result.message.clear();
    result.normalizedCenter = {
        (command.worldCenter.x - command.access.baseParcel.position.x) /
            command.access.baseParcel.size.x,
        (command.worldCenter.y - command.access.baseParcel.position.y) /
            command.access.baseParcel.size.y};
    return result;
}

BaseFacilityLayoutReceipt executeBaseFacilityLayout(
    ProfileState &profile,
    const ContentRegistry &content,
    const RepositionBaseFacilityCommand &command,
    const CommandContext &context)
{
    if (context.transactionId.empty())
    {
        return failure(
            DomainErrorCode::InvalidTransaction,
            "Base facility transaction id is required",
            profile.revision, command.facilityDefinitionId);
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        return BaseFacilityLayoutReceipt{
            true, true, DomainErrorCode::None, {}, profile.revision,
            command.facilityDefinitionId};
    }
    if (context.expectedRevision != profile.revision)
    {
        return failure(
            DomainErrorCode::StaleRevision,
            "Base facility layout changed; refresh and retry",
            profile.revision, command.facilityDefinitionId);
    }
    const BaseFacilityLayoutPlan plan = queryBaseFacilityLayout(
        profile, content, command);
    if (!plan.canCommit)
        return failure(
            plan.error, plan.message, profile.revision,
            command.facilityDefinitionId);
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
    {
        return failure(
            DomainErrorCode::RevisionOverflow,
            "Profile revision overflow",
            profile.revision, command.facilityDefinitionId);
    }
    profile.baseFacilityLayout
        .placements[command.access.baseSiteDefinitionId]
        [command.facilityDefinitionId] = plan.normalizedCenter;
    profile.committedTransactions.insert(context.transactionId);
    ++profile.revision;
    return BaseFacilityLayoutReceipt{
        true, false, DomainErrorCode::None, {}, profile.revision,
        command.facilityDefinitionId};
}

std::vector<BaseFacilitySpatialProjection> projectBaseFacilityLayout(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    const ContentRect &baseParcel)
{
    std::vector<BaseFacilitySpatialProjection> result;
    const auto site = profile.baseFacilityLayout.placements.find(
        siteDefinitionId);
    if (site == profile.baseFacilityLayout.placements.end() ||
        !finiteRect(baseParcel))
        return result;
    result.reserve(site->second.size());
    for (const auto &[definitionId, normalized] : site->second)
    {
        if (!isSpatialBaseFacility(definitionId) ||
            !finitePoint(normalized))
            continue;
        result.push_back(BaseFacilitySpatialProjection{
            definitionId,
            {baseParcel.position.x + normalized.x * baseParcel.size.x,
             baseParcel.position.y + normalized.y * baseParcel.size.y}});
    }
    return result;
}
