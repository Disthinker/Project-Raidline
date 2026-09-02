#include "home_perimeter_domain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "stable_random.h"

namespace
{
bool finitePoint(Vec2 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool inside(Vec2 point, const ContentRect &rect) noexcept
{
    return point.x >= rect.position.x && point.y >= rect.position.y &&
        point.x <= rect.position.x + rect.size.x &&
        point.y <= rect.position.y + rect.size.y;
}

ContentRect expanded(const ContentRect &rect, float amount) noexcept
{
    return {{rect.position.x - amount, rect.position.y - amount},
            {rect.size.x + amount * 2.0F,
             rect.size.y + amount * 2.0F}};
}

bool overlaps(const ContentRect &left, const ContentRect &right) noexcept
{
    return left.position.x < right.position.x + right.size.x &&
        left.position.x + left.size.x > right.position.x &&
        left.position.y < right.position.y + right.size.y &&
        left.position.y + left.size.y > right.position.y;
}

std::uint64_t stableSiteSeed(
    const RegionalBaseSiteDefinitionId &site,
    std::uint64_t cycle) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char value : site.value())
    {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    hash ^= cycle + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

bool validPlacement(
    Vec2 position,
    Vec2 size,
    const HomePerimeterGenerationContext &context,
    const std::vector<ContentRect> &accepted) noexcept
{
    const ContentRect bounds{position, size};
    if (!finitePoint(position) || position.x < 40.0F || position.y < 40.0F ||
        position.x + size.x > context.worldSize.x - 40.0F ||
        position.y + size.y > context.worldSize.y - 40.0F ||
        overlaps(bounds, expanded(context.safeCore,
                                  kHomePerimeterTransitionWidth + 160.0F)))
        return false;
    for (const ContentRect &blocker : context.blockers)
        if (overlaps(bounds, expanded(blocker, 30.0F)))
            return false;
    for (const ContentRect &other : accepted)
        if (overlaps(bounds, expanded(other, 180.0F)))
            return false;
    return true;
}

Vec2 generatedPosition(
    Pcg32 &random,
    Vec2 size,
    const HomePerimeterGenerationContext &context,
    std::vector<ContentRect> &accepted)
{
    const auto range = [](float value) noexcept
    {
        return static_cast<std::uint32_t>(std::max(1.0F, std::floor(value)));
    };
    for (std::uint32_t attempt = 0; attempt < 1536U; ++attempt)
    {
        const float distance = kHomePerimeterTransitionWidth + 220.0F +
            static_cast<float>(random.bounded(1500U));
        const std::uint32_t side = random.bounded(4U);
        Vec2 candidate{};
        if (side == 0U || side == 1U)
        {
            candidate.x = side == 0U
                ? context.safeCore.position.x - distance - size.x
                : context.safeCore.position.x + context.safeCore.size.x +
                    distance;
            candidate.y = context.safeCore.position.y - distance * 0.35F +
                static_cast<float>(random.bounded(range(
                    context.safeCore.size.y + distance * 0.7F)));
        }
        else
        {
            candidate.y = side == 2U
                ? context.safeCore.position.y - distance - size.y
                : context.safeCore.position.y + context.safeCore.size.y +
                    distance;
            candidate.x = context.safeCore.position.x - distance * 0.35F +
                static_cast<float>(random.bounded(range(
                    context.safeCore.size.x + distance * 0.7F)));
        }
        if (validPlacement(candidate, size, context, accepted))
        {
            accepted.push_back({candidate, size});
            return candidate;
        }
    }
    for (std::uint32_t attempt = 0; attempt < 512U; ++attempt)
    {
        const Vec2 candidate{
            40.0F + static_cast<float>(random.bounded(
                range(context.worldSize.x - size.x - 80.0F))),
            40.0F + static_cast<float>(random.bounded(
                range(context.worldSize.y - size.y - 80.0F)))};
        if (validPlacement(candidate, size, context, accepted))
        {
            accepted.push_back({candidate, size});
            return candidate;
        }
    }
    throw std::runtime_error{"Home perimeter has no legal placement"};
}

HomePerimeterEnsureReceipt failure(
    const ProfileState &profile,
    std::string message)
{
    return {false, false, profile.revision, std::move(message)};
}
}

HomeRegionSafetyZone queryHomeRegionSafetyZone(
    Vec2 point,
    const ContentRect &safeCore) noexcept
{
    if (!finitePoint(point))
        return HomeRegionSafetyZone::Perimeter;
    if (inside(point, safeCore))
        return HomeRegionSafetyZone::SafeCore;
    if (inside(point, expanded(safeCore, kHomePerimeterTransitionWidth)))
        return HomeRegionSafetyZone::TransitionBuffer;
    return HomeRegionSafetyZone::Perimeter;
}

std::uint64_t homePerimeterCycleIndex(
    const WorldClockState &clock) noexcept
{
    return clock.elapsedWorldMinutes / kHomePerimeterRefreshMinutes;
}

HomePerimeterEnsureReceipt ensureHomePerimeterSnapshot(
    ProfileState &profile,
    const ContentRegistry &content,
    const HomePerimeterGenerationContext &context,
    const CommandContext &command)
{
    if (command.expectedRevision != profile.revision ||
        command.transactionId.empty())
        return failure(profile, "Home perimeter command is stale");
    if (profile.committedTransactions.contains(command.transactionId))
        return {true, false, profile.revision, {}};
    if (context.baseSiteDefinitionId.value().empty() ||
        !finitePoint(context.worldSize) || context.worldSize.x < 2000.0F ||
        context.worldSize.y < 2000.0F)
        return failure(profile, "Home perimeter geometry is invalid");

    const std::uint64_t cycle = homePerimeterCycleIndex(profile.worldClock);
    const auto current = profile.homePerimeter.sites.find(
        context.baseSiteDefinitionId);
    if (current != profile.homePerimeter.sites.end() &&
        current->second.cycleIndex == cycle)
        return {true, false, profile.revision, {}};
    if (profile.homePerimeter.activeOuting.has_value())
        return {true, false, profile.revision, {}};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return failure(profile, "Profile revision overflow");

    HomePerimeterSiteSnapshot snapshot;
    snapshot.baseSiteDefinitionId = context.baseSiteDefinitionId;
    snapshot.cycleIndex = cycle;
    snapshot.seed = stableSiteSeed(context.baseSiteDefinitionId, cycle);
    Pcg32 random{snapshot.seed, 0x686f6d652d706572ULL};
    std::vector<ContentRect> accepted;

    if (current != profile.homePerimeter.sites.end())
    {
        for (const AssetInstanceId assetId : current->second.lootAssetIds)
        {
            const AssetRecord *asset = profile.assets.find(assetId);
            const auto *ground = asset == nullptr
                ? nullptr
                : std::get_if<BaseGroundAssetLocation>(&asset->location);
            if (ground != nullptr &&
                ground->baseSiteDefinitionId == context.baseSiteDefinitionId)
                static_cast<void>(profile.assets.erase(assetId));
        }
    }

    const std::uint32_t enemyCount = 7U + random.bounded(4U);
    for (std::uint32_t index = 0; index < enemyCount; ++index)
    {
        const Vec2 position = generatedPosition(
            random, {50.0F, 50.0F}, context, accepted);
        snapshot.enemies.push_back(HomePerimeterEnemySnapshot{
            index + 1U, position, position, {50.0F, 50.0F}, 3, 3});
    }

    const ItemDefinitionId lootDefinitions[] = {
        ItemDefinitionId{"item.loot.canned_meal"},
        ItemDefinitionId{"item.loot.cleaning_supplies"},
        ItemDefinitionId{"item.loot.scrap_parts"}};
    const std::uint32_t lootCount = 2U + random.bounded(2U);
    for (std::uint32_t index = 0; index < lootCount; ++index)
    {
        const ItemDefinitionId &definitionId =
            lootDefinitions[random.bounded(3U)];
        const ItemDefinition &definition = content.item(definitionId);
        const Vec2 position = generatedPosition(
            random, {48.0F, 48.0F}, context, accepted);
        const AssetInstanceId assetId = profile.assets.create(
            definition,
            BaseGroundAssetLocation{
                context.baseSiteDefinitionId, position},
            1U);
        snapshot.lootAssetIds.push_back(assetId);
    }

    profile.homePerimeter.sites.insert_or_assign(
        context.baseSiteDefinitionId, std::move(snapshot));
    profile.committedTransactions.insert(command.transactionId);
    ++profile.revision;
    return {true, true, profile.revision, {}};
}

HomePerimeterEnsureReceipt beginHomePerimeterOuting(
    ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId,
    const CommandContext &command)
{
    if (command.expectedRevision != profile.revision ||
        command.transactionId.empty())
        return failure(profile, "Home perimeter command is stale");
    if (profile.committedTransactions.contains(command.transactionId))
        return {true, false, profile.revision, {}};
    if (profile.homePerimeter.activeOuting.has_value())
    {
        const bool same =
            profile.homePerimeter.activeOuting->baseSiteDefinitionId ==
                siteDefinitionId;
        return same
            ? HomePerimeterEnsureReceipt{true, false, profile.revision, {}}
            : failure(profile, "Another Home perimeter outing is active");
    }
    const auto snapshot = profile.homePerimeter.sites.find(siteDefinitionId);
    if (snapshot == profile.homePerimeter.sites.end())
        return failure(profile, "Home perimeter snapshot is missing");
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return failure(profile, "Profile revision overflow");
    profile.homePerimeter.activeOuting = HomePerimeterOutingState{
        command.transactionId, siteDefinitionId, snapshot->second.cycleIndex};
    profile.committedTransactions.insert(command.transactionId);
    ++profile.revision;
    return {true, true, profile.revision, {}};
}

HomePerimeterResultReceipt completeHomePerimeterOuting(
    ProfileState &profile,
    bool rescued,
    const CommandContext &command)
{
    if (command.transactionId.empty())
        return {false, false, 0U, profile.revision,
                "Home perimeter result ID is empty"};
    if (profile.homePerimeter.committedResults.contains(command.transactionId))
        return {true, true, 0U, profile.revision, {}};
    if (command.expectedRevision != profile.revision ||
        !profile.homePerimeter.activeOuting.has_value())
        return {false, false, 0U, profile.revision,
                "Home perimeter result is stale"};
    if (profile.revision == std::numeric_limits<ProfileRevision>::max())
        return {false, false, 0U, profile.revision,
                "Profile revision overflow"};
    const std::uint64_t minutes = rescued
        ? kHomePerimeterRescueMinutes : kHomePerimeterReturnMinutes;
    const WorldClockAdvanceResult advanced =
        advanceWorldClock(profile.worldClock, minutes);
    if (advanced.minutesApplied != minutes)
        return {false, false, 0U, profile.revision,
                "Home perimeter time overflow"};
    if (rescued)
    {
        profile.currentHealth = std::clamp(
            kHomePerimeterRescueHealth, 1, 100);
        profile.medicalStatus = {};
    }
    profile.homePerimeter.activeOuting.reset();
    profile.homePerimeter.committedResults.insert(command.transactionId);
    profile.committedTransactions.insert(command.transactionId);
    ++profile.revision;
    return {true, false, minutes, profile.revision, {}};
}

bool homePerimeterOutingActive(
    const ProfileState &profile,
    const RegionalBaseSiteDefinitionId &siteDefinitionId) noexcept
{
    return profile.homePerimeter.activeOuting.has_value() &&
        profile.homePerimeter.activeOuting->baseSiteDefinitionId ==
            siteDefinitionId;
}
