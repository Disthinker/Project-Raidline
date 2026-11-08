#pragma once

#include "content_registry.h"
#include <nlohmann/json.hpp>

inline ContentRegistry contentBeforeOptics()
{
    auto document = nlohmann::json::parse(publishedContentJson());
    document["content_version"] = std::string{kWeaponHandlingContentVersion};
    auto &items = document["items"];
    for (auto it = items.begin(); it != items.end();)
    {
        const auto id = it->at("id").get<std::string>();
        if (id.starts_with("item.component.optic_"))
        {
            it = items.erase(it);
        }
        else
        {
            if (id == "item.weapon.rifle_5_45_service")
                (*it)["weapon_use"].erase("provides_optic_mount");
            ++it;
        }
    }
    return ContentRegistry::fromJson(document.dump());
}
