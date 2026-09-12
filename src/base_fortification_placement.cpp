#include "base_fortification_placement.h"
#include "base_ground_domain.h"
#include "collision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool overlaps(ContentRect a, ContentRect b)
{
    return isCollision({a.position, a.size}, {b.position, b.size});
}
Vec2 center(ContentRect r)
{
    return {r.position.x + r.size.x / 2, r.position.y + r.size.y / 2};
}
bool reachable(const RaidSpaceNavigationField &field, Vec2 from, Vec2 goal)
{
    // Bound malformed geometry/path cycles. This is not an enemy refresh budget
    // and never falls back to claiming a blocked destination is reachable.
    std::vector<Vec2> visited;
    for (unsigned i = 0; i < 256; ++i)
    {
        if (std::hypot(from.x - goal.x, from.y - goal.y) < 0.1F)
            return true;
        const auto next = field.nextWaypoint(from, goal, 0);
        if (!next || std::any_of(visited.begin(), visited.end(), [&](Vec2 p) {
                return std::hypot(next->x - p.x, next->y - p.y) < 0.001F;
            }))
            return false;
        visited.push_back(from);
        from = *next;
    }
    return false;
}
std::array<BaseDefensePosition, 4> positions(const BaseWorld &world,
                                             const FortificationDefinition &definition)
{
    return baseDefensePositionCandidates(world.layout(), world.plotId(), definition);
}

// A successful path entirely inside this window also exists in the full world.
// Clip crossing walls to the boundary; never drop them or treat outside as a
// bypass. This is a conservative construction proof, not a change to AI routing.
struct PassageWindow
{
    Vec2 origin;
    Vec2 size;
    std::vector<BallisticBlocker> blockers;
    Vec2 local(Vec2 p) const
    {
        return {p.x - origin.x, p.y - origin.y};
    }
};
PassageWindow passageWindow(const BaseWorld &world, std::span<const BallisticBlocker> blockers,
                            float margin, bool includePlayer)
{
    const auto &core = world.baseParcel();
    float left = core.position.x - margin, top = core.position.y - margin;
    float right = core.position.x + core.size.x + margin;
    float bottom = core.position.y + core.size.y + margin;
    if (includePlayer)
    {
        const auto p = world.playerPosition();
        left = std::min(left, p.x - 100);
        top = std::min(top, p.y - 100);
        right = std::max(right, p.x + world.playerSize().x + 100);
        bottom = std::max(bottom, p.y + world.playerSize().y + 100);
    }
    left = std::max(0.0F, left);
    top = std::max(0.0F, top);
    right = std::min(world.worldSize().x, right);
    bottom = std::min(world.worldSize().y, bottom);
    PassageWindow result{{left, top}, {right - left, bottom - top}, {}};
    for (const auto &b : blockers)
    {
        const float l = std::max(left, b.bounds.position.x);
        const float t = std::max(top, b.bounds.position.y);
        const float r = std::min(right, b.bounds.position.x + b.bounds.size.x);
        const float d = std::min(bottom, b.bounds.position.y + b.bounds.size.y);
        // Exact-union tiles bound the spatial index's largest-rectangle radius.
        for (float y = t; y < d; y += 256)
            for (float x = l; x < r; x += 256)
                result.blockers.push_back(
                    {static_cast<BallisticBlockerId>(result.blockers.size() + 1),
                     {{x - left, y - top}, {std::min(256.0F, r - x), std::min(256.0F, d - y)}}});
    }
    return result;
}
} // namespace

FortificationPlacementPlan queryFortificationPlacement(const ProfileState &profile,
                                                       const ContentRegistry &content,
                                                       const BaseWorld &world,
                                                       const InstallFortificationCommand &command)
{
    FortificationPlacementPlan plan;
    plan.revision = profile.revision;
    plan.instance = command.instance;
    auto fail = [&](FortificationPlacementFailure reason, DomainErrorCode code,
                    const char *message) {
        plan.placementFailure = reason;
        plan.error = code;
        plan.message = message;
        return plan;
    };
    if (!profile.homeFounding.established || profile.pendingRaid || profile.activeBaseDefense ||
        profile.baseSiege.warningActive || world.baseDefenseActive())
        return fail(FortificationPlacementFailure::ActivityLocked,
                    DomainErrorCode::IllegalDestination, "Fortification layout is frozen");
    if (!validateBaseFortifications(profile, content).valid ||
        profile.revision == std::numeric_limits<ProfileRevision>::max())
        return fail(FortificationPlacementFailure::InvalidState, DomainErrorCode::InvalidProfile,
                    "Invalid fortification state or revision");
    const auto &site = profile.regionalOperations.technologyCore.baseSiteDefinitionId;
    const auto selectedPlot = profile.homeFounding.plots.find(site);
    const std::string plot =
        selectedPlot == profile.homeFounding.plots.end() ? "" : selectedPlot->second;
    if (command.slot.site != site || command.slot.site.value() != world.siteDefinitionId() ||
        command.slot.plot != plot || world.plotId() != plot ||
        static_cast<std::uint32_t>(command.slot.side) > 3)
        return fail(FortificationPlacementFailure::WrongSiteOrPlot,
                    DomainErrorCode::IllegalDestination,
                    "Refresh the current Base and fixed position");
    const auto found = profile.baseFortifications.instances.find(command.instance);
    if (found == profile.baseFortifications.instances.end())
        return fail(FortificationPlacementFailure::MissingInstance, DomainErrorCode::MissingAsset,
                    "Fortification instance no longer exists");
    const auto &record = found->second;
    const auto &definition = *content.findFortification(record.definition);
    plan.durabilityAfter = record.durability;
    const auto candidates = positions(world, definition);
    const auto &position = candidates[static_cast<std::size_t>(command.slot.side)];
    plan.footprint = position.footprint;
    if (!position.available)
        return fail(FortificationPlacementFailure::UnavailablePosition,
                    DomainErrorCode::IllegalDestination,
                    "Fixed position has no legal static clearance");

    std::vector<ContentRect> reserved{ContentRect{world.playerPosition(), world.playerSize()}};
    for (const auto &enemy : world.perimeterEnemies())
        reserved.push_back({enemy.position(), enemy.size()});
    std::vector<BallisticBlocker> blockers;
    auto addBlocker = [&](ContentRect r) {
        blockers.push_back(
            {static_cast<BallisticBlockerId>(blockers.size() + 1), {r.position, r.size}});
    };
    for (const auto &r : world.layout().movementBlockers)
        addBlocker(r);
    std::vector<Vec2> destinations{position.insideApproach, position.outsideApproach};
    for (const auto &facility : world.facilities())
    {
        if (!facility.active)
            continue;
        addBlocker({facility.bounds.position, facility.bounds.size});
        const auto access = baseFacilityAccessGeometry(facility);
        reserved.push_back(access.workZone);
        reserved.push_back(access.interactionZone);
        // An entrance marker is 18 units from the wall, less than half a
        // player's height. Prove an actual actor center inside the service zone.
        destinations.push_back(center(access.interactionZone));
        destinations.back().y =
            facility.bounds.position.y + facility.bounds.size.y + world.playerSize().y / 2 + 4;
    }
    for (const auto &r : projectBaseGroundMovementBlockers(profile, content, site))
        addBlocker(r);
    for (const auto &asset : projectBaseGroundAssets(profile, site))
    {
        const auto &item = content.item(asset.definitionId);
        Vec2 size = item.basePlacement ? item.basePlacement->footprint : item.worldRenderSize;
        size.x = std::max(size.x, item.pickupSize.x);
        size.y = std::max(size.y, item.pickupSize.y);
        if (asset.orientation == ItemOrientation::Degrees90 ||
            asset.orientation == ItemOrientation::Degrees270)
            std::swap(size.x, size.y);
        reserved.push_back({{asset.position.x - size.x / 2, asset.position.y - size.y / 2}, size});
    }
    for (const auto &[id, other] : profile.baseFortifications.instances)
    {
        if (id == command.instance || !other.slot)
            continue;
        if (*other.slot == command.slot)
            return fail(FortificationPlacementFailure::OccupiedSlot, DomainErrorCode::Capacity,
                        "Fixed position already owns another fortification");
        const auto otherPositions = positions(world, *content.findFortification(other.definition));
        const auto &otherPosition = otherPositions[static_cast<std::size_t>(other.slot->side)];
        if (!otherPosition.available)
            return fail(FortificationPlacementFailure::InvalidState,
                        DomainErrorCode::InvalidProfile,
                        "Installed fortification has invalid static geometry");
        // Prove the fully repaired layout, including zero-HP remnants. Repair
        // cannot later turn a legal passage into a sealed map.
        addBlocker(otherPosition.footprint);
    }
    if (std::any_of(reserved.begin(), reserved.end(),
                    [&](ContentRect r) { return overlaps(position.circulation, r); }) ||
        std::any_of(blockers.begin(), blockers.end(), [&](const BallisticBlocker &b) {
            return overlaps(position.circulation, {b.bounds.position, b.bounds.size});
        }))
        return fail(FortificationPlacementFailure::OccupiedClearance, DomainErrorCode::Capacity,
                    "Position or side passage overlaps an actor, facility or ground asset");
    addBlocker(position.footprint);
    const auto playerWindow = passageWindow(world, blockers, 600, true);
    const auto playerNavigation = RaidSpaceNavigationField::build(
        world.playerSize(), playerWindow.size, playerWindow.blockers, 0);
    const Vec2 player = playerWindow.local(center({world.playerPosition(), world.playerSize()}));
    if (!playerNavigation)
        return fail(FortificationPlacementFailure::PlayerOrFacilityUnreachable,
                    DomainErrorCode::IllegalDestination, "Player passage geometry is invalid");
    for (const auto target : destinations)
    {
        if (!reachable(*playerNavigation, player, playerWindow.local(target)))
            return fail(FortificationPlacementFailure::PlayerOrFacilityUnreachable,
                        DomainErrorCode::IllegalDestination,
                        "Installation would leave a required passage unreachable");
        ++plan.verifiedPlayerDestinations;
    }

    // Use the real defense adapter, including its protected-buffer mask, not
    // only an unmasked navigation field. No roster is installed into BaseWorld.
    BaseDefenseSnapshot proof;
    proof.eventId = "fortification-placement-proof";
    proof.siegeSequence = 1;
    proof.siteDefinitionId = world.siteDefinitionId();
    proof.plotId = world.plotId();
    proof.layoutIdentity = "placement-proof";
    proof.seed = 0x706c6163656d656eULL;
    const auto defenseWindow = passageWindow(world, blockers, 1800, false);
    proof.worldSize = defenseWindow.size;
    proof.safeCore = {defenseWindow.local(world.baseParcel().position), world.baseParcel().size};
    proof.playerPosition = defenseWindow.local(world.playerPosition());
    // Outside this proof window the real player is farther from every eligible
    // entry, not closer. Clamp only this immutable proof's player coordinate.
    proof.playerPosition.x = std::clamp(proof.playerPosition.x, 0.0F, proof.worldSize.x);
    proof.playerPosition.y = std::clamp(proof.playerPosition.y, 0.0F, proof.worldSize.y);
    proof.frozenPopulation = profile.basePopulation.ordinaryResidents;
    proof.frozenMoraleTier = static_cast<std::uint32_t>(profile.baseMorale.tier);
    const auto prepared =
        BaseDefenseRuntime::prepare(std::move(proof), defenseWindow.blockers,
                                    content.enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    if (!prepared)
        return fail(FortificationPlacementFailure::NoDefenseApproach,
                    DomainErrorCode::IllegalDestination,
                    "Installation would block all legal defense approaches");
    plan.verifiedDefenseApproaches = prepared->coreDefenseZones.size();
    plan.canCommit = true;
    return plan;
}

FortificationReceipt executeFortificationPlacement(ProfileState &profile,
                                                   const ContentRegistry &content,
                                                   const BaseWorld &world,
                                                   const InstallFortificationCommand &command,
                                                   const CommandContext &context)
{
    FortificationReceipt receipt;
    receipt.revision = profile.revision;
    receipt.instance = command.instance;
    if (context.transactionId.empty())
    {
        receipt.error = DomainErrorCode::InvalidTransaction;
        receipt.message = "Transaction ID is required";
        return receipt;
    }
    if (profile.committedTransactions.contains(context.transactionId))
    {
        receipt.succeeded = receipt.alreadyCommitted = true;
        receipt.instance = {};
        return receipt;
    }
    if (context.expectedRevision != profile.revision)
    {
        receipt.error = DomainErrorCode::StaleRevision;
        receipt.message = "Refresh the fortification placement preview";
        return receipt;
    }
    const auto plan = queryFortificationPlacement(profile, content, world, command);
    if (!plan.canCommit)
    {
        receipt.error = plan.error;
        receipt.message = plan.message;
        return receipt;
    }
    ProfileState candidate = profile;
    candidate.baseFortifications.instances.at(command.instance).slot = command.slot;
    candidate.committedTransactions.insert(context.transactionId);
    ++candidate.revision;
    const auto validation = validateProfileState(candidate, content);
    if (!validation.valid)
    {
        receipt.error = DomainErrorCode::InvalidProfile;
        receipt.message = validation.message;
        return receipt;
    }
    profile = std::move(candidate);
    receipt.succeeded = true;
    receipt.revision = profile.revision;
    return receipt;
}
