#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "content_registry.h"

// Test-only composition. The shipping registry never reads drafts. Reusing
// legacy rule/deployment scaffolding here does not publish or freeze it as the
// hospital's final content contract.
inline nlohmann::json hospitalContentCandidateJson()
{
    const auto path = std::filesystem::path{__FILE__}.parent_path().parent_path() /
        "assets/content/drafts/hospital_district_v1.json";
    std::ifstream input{path};
    if (!input) throw std::runtime_error{"hospital candidate is missing"};
    const auto candidate = nlohmann::json::parse(input);
    auto root = nlohmann::json::parse(publishedContentJson());
    nlohmann::json map;
    for (const auto &existing : root.at("maps"))
        if (existing.at("id") == candidate.at("base_map")) map = existing;
    if (map.is_null()) throw std::runtime_error{"hospital candidate base is missing"};
    map.merge_patch(candidate.at("map_patch"));
    root["maps"].push_back(map);
    for (const auto &table : candidate.at("loot_tables"))
        root["loot_tables"].push_back(table);
    root["regional_operations"]["nodes"].push_back({
        {"id", "region_node.raid.hospital_district"},
        {"display_name", "Hospital District"}, {"kind", "raid"},
        {"map_definition_id", "map.raid.hospital_district"}});
    root["regional_operations"]["routes"].push_back({
        {"id", "region_route.hospital_direct"}, {"display_name", "Hospital Access"},
        {"from", "region_node.base.greyline_yard"},
        {"to", "region_node.raid.hospital_district"}, {"travel_minutes", 150}});
    return root;
}

inline ContentRegistry hospitalContentCandidate()
{
    return ContentRegistry::fromJson(hospitalContentCandidateJson().dump());
}
