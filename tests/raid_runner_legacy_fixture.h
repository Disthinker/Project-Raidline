#pragma once

#include "content_registry.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

// Build historical inputs before deployment and fingerprinting. Never erase
// variants from a current Raid merely to make a downgrade appear lossless.
inline nlohmann::json contentJsonBeforeRunner(
    std::string_view version = "raid-fragmentation-grenade-content-72")
{
    auto json = nlohmann::json::parse(publishedContentJson());
    json["content_version"] = std::string{version};
    auto &definitions = json["enemy_combat_definitions"];
    for (auto it = definitions.begin(); it != definitions.end();)
        if (it->at("id") == "enemy.infected.runner") it = definitions.erase(it);
        else ++it;
    for (auto &map : json["maps"])
        if (map.contains("procedural_outdoor"))
            for (auto &group : map["procedural_outdoor"]["encounter_archetypes"])
                group.erase("runner_member_definition");
    return json;
}

inline ContentRegistry contentBeforeRunner(
    std::string_view version = "raid-fragmentation-grenade-content-72")
{
    return ContentRegistry::fromJson(contentJsonBeforeRunner(version).dump());
}
