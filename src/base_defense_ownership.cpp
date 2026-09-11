#include "base_defense_ownership.h"
#include "base_fortification_domain.h"
#include "base_siege_domain.h"
#include <algorithm>

namespace
{
bool fresh(const BaseDefenseSnapshot &s)
{
    return s.elapsedSeconds == 0 && s.currentWave == 0 && s.spawnedEnemyCount == 0 &&
        s.nextSpawnDelay == 0 && s.navigationScheduleCursor == 0 && s.attackScheduleCursor == 0 &&
        s.enemies.empty() && s.killedIds.empty() && s.breachedIds.empty() &&
        s.contacts.empty() && s.reservedAttackers.empty() && s.fortificationAttacks.empty() &&
        s.fortificationGeometryRevision == 0;
}
}

ProfileValidationResult validateDefenseFortificationOwnership(
    const ProfileState &p, const ContentRegistry &content, const BaseDefenseSnapshot &s)
{
    if (s.rulesVersion == kBaseDefenseRulesVersion) return {true, {}};
    if (s.rulesVersion != kFortifiedBaseDefenseRulesVersion)
        return {false, "Unknown defense ownership rules"};
    std::size_t installed{};
    for (const auto &[id, record] : p.baseFortifications.instances)
        if (record.slot) ++installed;
    if (installed != s.fortifications.size()) return {false, "Defense checkout set differs"};
    for (const auto &f : s.fortifications)
    {
        const auto owner = p.baseFortifications.instances.find(f.id);
        const auto *definition = content.findFortification(f.definition);
        if (owner == p.baseFortifications.instances.end() || !definition ||
            owner->second.definition != f.definition || owner->second.slot != f.slot ||
            owner->second.durability != f.durability ||
            f.maximumDurability != definition->maximumDurability ||
            f.slot.site.value() != s.siteDefinitionId || f.slot.plot != s.plotId)
            return {false, "Defense fortification owner differs"};
    }
    return {true, {}};
}

bool matchesDefenseWarning(const BaseDefenseSnapshot &active, const BaseDefenseSnapshot &warning)
{
    if (active.fortifications.size() != warning.fortifications.size()) return false;
    auto normalized = active;
    // Repair changes only checkout durability. All identities, footprints,
    // routes, waves and static geometry must still match the warning.
    for (std::size_t i = 0; i < normalized.fortifications.size(); ++i)
        normalized.fortifications[i].initialDurability = warning.fortifications[i].initialDurability;
    return baseDefenseLayoutHash(normalized) == warning.layoutHash;
}

ProfileValidationResult validateDefenseWarning(const ProfileState &p, const ContentRegistry &content)
{
    if (!p.baseDefenseWarning) return {true, {}}; // migrated legacy warning
    const auto &warning = *p.baseDefenseWarning;
    if ((!p.baseSiege.warningActive && !p.activeBaseDefense) || p.pendingRaid ||
        warning.eventId != baseSiegeEventId(p) || !p.homeFounding.established)
        return {false, "Defense warning identity or activity differs"};
    if (!warning.layout)
        return p.activeBaseDefense ? ProfileValidationResult{false, "Unprepared defense started"}
                                   : ProfileValidationResult{true, {}};
    const auto &s = *warning.layout;
    const auto site = p.regionalOperations.technologyCore.baseSiteDefinitionId;
    const auto plot = p.homeFounding.plots.find(site);
    std::string message;
    if (s.rulesVersion != kFortifiedBaseDefenseRulesVersion || s.eventId != warning.eventId ||
        s.siegeSequence != p.baseSiege.siegeSequence || s.siteDefinitionId != site.value() ||
        s.plotId != (plot == p.homeFounding.plots.end() ? "" : plot->second) || !fresh(s) ||
        !validateBaseDefenseSnapshot(s, message))
        return {false, "Invalid frozen defense warning: " + message};
    auto owners = s;
    for (auto &f : owners.fortifications)
    {
        if (f.initialDurability != f.maximumDurability || f.durability != f.maximumDurability)
            return {false, "Warning must prove repairable geometry"};
        const auto owner = p.baseFortifications.instances.find(f.id);
        if (owner == p.baseFortifications.instances.end()) return {false, "Missing warning owner"};
        f.durability = owner->second.durability;
    }
    return validateDefenseFortificationOwnership(p, content, owners);
}

bool captureOwnedDefenseCheckpoint(ProfileState &p, BaseDefenseSnapshot next,
                                   const ContentRegistry &content)
{
    if (!p.activeBaseDefense) return false;
    const auto &previous = *p.activeBaseDefense;
    std::string message;
    if (next.eventId != previous.eventId || next.layoutHash != previous.layoutHash ||
        next.elapsedSeconds < previous.elapsedSeconds ||
        !validateBaseDefenseSnapshot(next, message) ||
        !validateDefenseFortificationOwnership(p, content, previous).valid)
        return false;
    for (const auto &f : next.fortifications)
    {
        const auto owner = p.baseFortifications.instances.find(f.id);
        if (owner == p.baseFortifications.instances.end() ||
            f.durability > owner->second.durability) return false;
    }
    // No fallible lookup/allocation after the first mutation; at most 4 owners.
    for (const auto &f : next.fortifications)
        p.baseFortifications.instances.find(f.id)->second.durability = f.durability;
    p.activeBaseDefense = std::move(next);
    return true;
}
