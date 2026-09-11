#include "base_defense_preparation.h"
#include "base_construction_domain.h"
#include "base_ground_domain.h"
#include "base_defense_positions.h"
#include "base_facility_layout_domain.h"
#include "base_siege_domain.h"
#include "base_world.h"

BaseDefenseWarningSnapshot prepareBaseDefenseWarning(const ProfileState &p,
                                                     const ContentRegistry &content)
{
    BaseDefenseWarningSnapshot warning{baseSiegeEventId(p), {}};
    const auto site = p.regionalOperations.technologyCore.baseSiteDefinitionId;
    const auto found = p.homeFounding.plots.find(site);
    const std::string plot = found == p.homeFounding.plots.end() ? "" : found->second;
    BaseWorld world;
    world.configureSite(site.value(), {}, plot);
    std::vector<BaseFacilitySpatialOverride> overrides;
    // Facility placement uses the same Profile projection as GameFlow; the
    // temporary preparation world has no daily enemies or gameplay ownership.
    for (const auto &f : projectBaseFacilityLayout(p, site, world.baseParcel()))
        for (const auto &[id, kind] : std::initializer_list<std::pair<const char *, BaseFacilityKind>>{
                 {"base_facility.warehouse", BaseFacilityKind::Storage},
                 {"base_facility.medical", BaseFacilityKind::Medical},
                 {"base_facility.dormitory", BaseFacilityKind::Dormitory},
                 {"base_facility.kitchen_water", BaseFacilityKind::KitchenWater},
                 {"base_facility.workshop", BaseFacilityKind::Workshop}})
            if (f.facilityDefinitionId.value() == id)
                overrides.push_back({kind, f.worldCenter, baseFacilityInstalled(p, f.facilityDefinitionId)});
    world.configureSite(site.value(), std::move(overrides), plot);
    world.configureGroundBlockers(projectBaseGroundMovementBlockers(p, content, site));
    BaseDefenseSnapshot s;
    s.eventId = warning.eventId;
    s.siegeSequence = p.baseSiege.siegeSequence;
    s.rulesVersion = kFortifiedBaseDefenseRulesVersion;
    // FNV over explicit identity only: inventory/revision/countdown cannot reroll lanes.
    s.seed = 14695981039346656037ULL;
    for (const auto part : std::initializer_list<std::string_view>{s.eventId, site.value(), plot})
    {
        for (unsigned char c : part) { s.seed ^= c; s.seed *= 1099511628211ULL; }
        s.seed ^= 255U; s.seed *= 1099511628211ULL;
    }
    s.frozenPopulation = p.basePopulation.ordinaryResidents;
    s.frozenMoraleTier = static_cast<std::uint32_t>(p.baseMorale.tier);
    s.frozenSiteThreat = content.regionalBaseSite(site).dailyBaseThreatUnits;
    for (const auto &[id, record] : p.baseFortifications.instances)
    {
        if (!record.slot) continue;
        const auto *definition = content.findFortification(record.definition);
        if (!definition) return warning;
        const auto slots = baseDefensePositionCandidates(world.layout(), plot, *definition);
        const auto side = static_cast<std::size_t>(record.slot->side);
        if (side >= slots.size() || slots[side].key != *record.slot || !slots[side].available)
            return warning;
        const auto &bounds = slots[side].footprint;
        s.fortifications.push_back({id, record.definition, *record.slot,
            {bounds.position, bounds.size}, definition->maximumDurability,
            definition->maximumDurability, definition->maximumDurability});
    }
    // Prove with every installed remnant repaired, so allowed warning repairs
    // cannot close a previously accepted route. No source world is rebuilt.
    warning.layout = world.prepareBaseDefenseSnapshot(std::move(s),
        content.enemyCombatDefinition(ordinaryInfectedDefinitionId()));
    return warning;
}
