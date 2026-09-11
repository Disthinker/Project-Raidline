#include "base_defense_serialization.h"
#include "combat_runtime_checkpoint_json.h"
#include <nlohmann/json.hpp>
#include <limits>

namespace
{
using Json = nlohmann::json;
Json point(Vec2 v) { return Json::array({v.x, v.y}); }
Vec2 readPoint(const Json &j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
Json rect(Rect r) { return {point(r.position), point(r.size)}; }
Rect readRect(const Json &j) { return {readPoint(j.at(0)), readPoint(j.at(1))}; }
std::uint64_t readUnsigned(const Json &j, std::uint64_t maximum)
{
    if ((!j.is_number_unsigned() && !(j.is_number_integer() && j.get<std::int64_t>() >= 0)) ||
        j.get<std::uint64_t>() > maximum) throw std::runtime_error("Invalid structure integer");
    return j.get<std::uint64_t>();
}
std::uint32_t read32(const Json &j)
{ return static_cast<std::uint32_t>(readUnsigned(j, std::numeric_limits<std::uint32_t>::max())); }
} // namespace

nlohmann::json baseDefenseSnapshotJson(const BaseDefenseSnapshot &s)
{
    Json waves = Json::array(), corridors = Json::array(), zones = Json::array(),
         contacts = Json::array();
    Json blockers = Json::array();
    for (const auto &r : s.movementBlockers)
        blockers.push_back(rect(r));
    for (const auto &r : s.corridors)
        corridors.push_back(rect(r));
    for (const auto &r : s.coreDefenseZones)
        zones.push_back(rect(r));
    for (const auto &w : s.wavePlans)
    {
        Json route = Json::array();
        for (const auto p : w.route)
            route.push_back(point(p));
        waves.push_back({{"release", w.releaseSeconds},
                         {"entry", point(w.entry)},
                         {"target", point(w.target)},
                         {"route", route},
                         {"enemy_ids", w.enemyIds},
                         {"enemy_max_health", w.enemyMaxHealth}});
    }
    for (const auto &c : s.contacts)
        contacts.push_back({{"enemy_id", c.enemyId}, {"seconds", c.seconds}});
    Json result{{"event_id", s.eventId},
            {"siege_sequence", s.siegeSequence},
            {"mode", "realtime"},
            {"rules_version", s.rulesVersion},
            {"site", s.siteDefinitionId},
            {"plot", s.plotId},
            {"layout", s.layoutIdentity},
            {"seed", s.seed},
            {"world_size", point(s.worldSize)},
            {"safe_core", rect(s.safeCore)},
            {"movement_blockers", blockers},
            {"corridors", corridors},
            {"defense_zones", zones},
            {"waves", waves},
            {"frozen_population", s.frozenPopulation},
            {"frozen_morale", s.frozenMoraleTier},
            {"frozen_site_threat", s.frozenSiteThreat},
            {"breach_limit", s.breachLimit},
            {"maximum_active", s.maximumActiveEnemies},
            {"layout_hash", s.layoutHash},
            {"elapsed", s.elapsedSeconds},
            {"current_wave", s.currentWave},
            {"spawned_enemy_count", s.spawnedEnemyCount},
            {"next_spawn_delay", s.nextSpawnDelay},
            {"navigation_schedule_cursor", s.navigationScheduleCursor},
            {"attack_schedule_cursor", s.attackScheduleCursor},
            {"reserved_attackers", s.reservedAttackers},
            {"killed", s.killedIds},
            {"breached", s.breachedIds},
            {"contacts", contacts},
            {"enemies", s.enemies},
            {"player_position", point(s.playerPosition)},
            {"damage_protection", s.damageProtectionSeconds},
            {"shooting", s.shooting},
            {"active_weapon_slot", s.activeWeaponSlot},
            {"command_sequence", s.commandSequence},
            {"weapon_fault_sequence", s.weaponFaultSequence},
            {"medical_random_sequence", s.medicalRandomSequence},
            {"wound_random_sequence", s.woundRandomSequence},
            {"pending_world_seconds", s.pendingWorldSeconds},
            {"base_combat_elapsed", s.baseCombatElapsedSeconds},
            {"medical_tick_accumulator", s.medicalTickAccumulatorSeconds}};
    if (s.rulesVersion == kFortifiedBaseDefenseRulesVersion)
    {
        auto structures = Json::array(), bindings = Json::array();
        for (const auto &f : s.fortifications)
            structures.push_back({{"id", f.id.value}, {"definition", f.definition.value()},
                {"site", f.slot.site.value()}, {"plot", f.slot.plot},
                {"side", static_cast<unsigned>(f.slot.side)}, {"footprint", rect(f.footprint)},
                {"maximum", f.maximumDurability}, {"initial", f.initialDurability},
                {"durability", f.durability}});
        for (const auto &b : s.fortificationAttacks)
            bindings.push_back({{"enemy", b.enemyId}, {"target", b.target.value}});
        result["fortifications"] = std::move(structures);
        result["fortification_attacks"] = std::move(bindings);
        result["fortification_geometry_revision"] = s.fortificationGeometryRevision;
    }
    return result;
}

BaseDefenseSnapshot parseBaseDefenseSnapshotJson(const nlohmann::json &j)
{
    if (j.at("mode").get<std::string>() != "realtime")
        throw std::runtime_error("Defense mode is invalid");
    BaseDefenseSnapshot s;
    s.eventId = j.at("event_id").get<std::string>();
    s.siegeSequence = j.at("siege_sequence").get<std::uint64_t>();
    s.rulesVersion = j.at("rules_version").get<std::uint32_t>();
    s.siteDefinitionId = j.at("site").get<std::string>();
    s.plotId = j.at("plot").get<std::string>();
    s.layoutIdentity = j.at("layout").get<std::string>();
    s.seed = j.at("seed").get<std::uint64_t>();
    s.worldSize = readPoint(j.at("world_size"));
    s.safeCore = readRect(j.at("safe_core"));
    for (const auto &r : j.at("movement_blockers"))
        s.movementBlockers.push_back(readRect(r));
    for (const auto &r : j.at("corridors"))
        s.corridors.push_back(readRect(r));
    for (const auto &r : j.at("defense_zones"))
        s.coreDefenseZones.push_back(readRect(r));
    for (const auto &w : j.at("waves"))
    {
        BaseDefenseWaveSnapshot wave;
        wave.releaseSeconds = w.at("release").get<float>();
        wave.entry = readPoint(w.at("entry"));
        wave.target = readPoint(w.at("target"));
        for (const auto &p : w.at("route"))
            wave.route.push_back(readPoint(p));
        wave.enemyIds = w.at("enemy_ids").get<std::vector<std::uint64_t>>();
        wave.enemyMaxHealth = w.at("enemy_max_health").get<int>();
        s.wavePlans.push_back(std::move(wave));
    }
    s.frozenPopulation = j.at("frozen_population").get<std::uint32_t>();
    s.frozenMoraleTier = j.at("frozen_morale").get<std::uint32_t>();
    s.frozenSiteThreat = j.at("frozen_site_threat").get<std::uint32_t>();
    s.breachLimit = j.at("breach_limit").get<std::uint32_t>();
    s.maximumActiveEnemies = j.at("maximum_active").get<std::uint32_t>();
    s.layoutHash = j.at("layout_hash").get<std::uint64_t>();
    s.elapsedSeconds = j.at("elapsed").get<float>();
    s.currentWave = j.at("current_wave").get<std::uint32_t>();
    s.spawnedEnemyCount = j.at("spawned_enemy_count").get<std::uint32_t>();
    s.nextSpawnDelay = j.at("next_spawn_delay").get<float>();
    s.navigationScheduleCursor = j.at("navigation_schedule_cursor").get<std::uint32_t>();
    s.attackScheduleCursor = j.at("attack_schedule_cursor").get<std::uint32_t>();
    s.reservedAttackers = j.at("reserved_attackers").get<std::vector<std::uint64_t>>();
    s.killedIds = j.at("killed").get<std::vector<std::uint64_t>>();
    s.breachedIds = j.at("breached").get<std::vector<std::uint64_t>>();
    for (const auto &c : j.at("contacts"))
        s.contacts.push_back({c.at("enemy_id").get<std::uint64_t>(), c.at("seconds").get<float>()});
    s.enemies = j.at("enemies").get<std::vector<EnemyRuntimeCheckpoint>>();
    s.playerPosition = readPoint(j.at("player_position"));
    s.damageProtectionSeconds = j.at("damage_protection").get<float>();
    s.shooting = j.at("shooting").get<WorldShootingCheckpoint>();
    s.activeWeaponSlot = j.at("active_weapon_slot").get<std::uint32_t>();
    s.commandSequence = j.at("command_sequence").get<std::uint64_t>();
    s.weaponFaultSequence = j.at("weapon_fault_sequence").get<std::uint64_t>();
    s.medicalRandomSequence = j.at("medical_random_sequence").get<std::uint64_t>();
    s.woundRandomSequence = j.at("wound_random_sequence").get<std::uint64_t>();
    s.pendingWorldSeconds = j.at("pending_world_seconds").get<double>();
    s.baseCombatElapsedSeconds = j.at("base_combat_elapsed").get<float>();
    s.medicalTickAccumulatorSeconds = j.at("medical_tick_accumulator").get<float>();
    if (s.rulesVersion == kFortifiedBaseDefenseRulesVersion)
    {
        const auto &structures = j.at("fortifications"), &bindings = j.at("fortification_attacks");
        if (!structures.is_array() || structures.size() > 4 || !bindings.is_array() ||
            bindings.size() > kBaseDefenseMaximumActiveEnemies)
            throw std::runtime_error("Unbounded structure checkpoint");
        for (const auto &f : structures)
        {
            const auto side = read32(f.at("side"));
            if (side > 3) throw std::runtime_error("Invalid structure side");
            s.fortifications.push_back({
                {readUnsigned(f.at("id"), UINT64_MAX)},
                FortificationDefinitionId{f.at("definition").get<std::string>()},
                {RegionalBaseSiteDefinitionId{f.at("site").get<std::string>()},
                 f.at("plot").get<std::string>(), static_cast<DefenseSide>(side)},
                readRect(f.at("footprint")), read32(f.at("maximum")), read32(f.at("initial")),
                read32(f.at("durability"))});
        }
        for (const auto &b : bindings)
            s.fortificationAttacks.push_back({readUnsigned(b.at("enemy"), UINT64_MAX),
                                              {readUnsigned(b.at("target"), UINT64_MAX)}});
        s.fortificationGeometryRevision = read32(j.at("fortification_geometry_revision"));
    }
    else if (j.contains("fortifications") || j.contains("fortification_attacks") ||
             j.contains("fortification_geometry_revision"))
        throw std::runtime_error("Legacy defense cannot acquire structures during load");
    std::string message;
    if (!validateBaseDefenseSnapshot(s, message))
        throw std::runtime_error(message);
    return s;
}
