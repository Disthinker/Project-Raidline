#pragma once

#include "content_registry.h"
#include <algorithm>
#include <nlohmann/json.hpp>

inline nlohmann::json contentBeforeHospitalMedicalReserveJson()
{
    auto document = nlohmann::json::parse(publishedContentJson());
    document["content_version"] = kEmergencyPowerContentVersion;
    for (auto &map : document.at("maps"))
        if (map.at("id") == std::string{kHospitalFloorplanMapId.value()})
            map.erase("rescue");
    return document;
}

inline nlohmann::json contentBeforeEmergencyPowerJson()
{
    auto document = contentBeforeHospitalMedicalReserveJson();
    document["content_version"] = kFreightBreachContentVersion;
    auto &construction = document["base_construction"];
    std::erase_if(construction["facilities"].get_ref<nlohmann::json::array_t &>(), [](const auto &facility) {
        return facility.at("id") == std::string{kBaseGeneratorFacilityId.value()};
    });
    std::erase_if(construction["projects"].get_ref<nlohmann::json::array_t &>(), [](const auto &project) {
        return project.at("id") == std::string{kBaseGeneratorProjectId.value()};
    });
    return document;
}

inline nlohmann::json contentBeforeFreightBreachJson()
{
    auto document = contentBeforeEmergencyPowerJson();
    document["content_version"] = kHospitalFloorplanRoutesContentVersion;
    auto &items = document["items"];
    std::erase_if(items.get_ref<nlohmann::json::array_t &>(), [](const auto &item) {
        return item.at("id") == std::string{kFreightBreachWedgesId.value()};
    });
    auto &recipes = document["base_manufacturing"]["recipes"];
    std::erase_if(recipes.get_ref<nlohmann::json::array_t &>(), [](const auto &recipe) {
        return recipe.at("id") == std::string{kFreightBreachRecipeId.value()};
    });
    return document;
}

inline nlohmann::json contentBeforeHospitalFloorplanRoutesJson()
{
    auto document = contentBeforeFreightBreachJson();
    document["content_version"] = kRaidBackpackRecoveryContentVersion;
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kHospitalFloorplanItemId.value()};
    }), items.end());
    return document;
}

inline ContentRegistry contentBeforeHospitalFloorplanRoutes()
{
    return ContentRegistry::fromJson(contentBeforeHospitalFloorplanRoutesJson().dump());
}

inline ContentRegistry contentBeforeRaidBackpackRecovery()
{
    auto document = contentBeforeHospitalFloorplanRoutesJson();
    document["content_version"] = kThrownIlluminationContentVersion;
    return ContentRegistry::fromJson(document.dump());
}

inline nlohmann::json contentBeforeThrownIlluminationJson()
{
    auto document = contentBeforeHospitalFloorplanRoutesJson();
    document["content_version"] = std::string{kArmorReadinessContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kThrownIlluminationItemId.value()};
    }), items.end());
    auto &recipes = document["base_manufacturing"]["recipes"];
    recipes.erase(std::remove_if(recipes.begin(), recipes.end(), [](const auto &recipe) {
        return recipe.at("id") == std::string{kThrownIlluminationRecipeId.value()};
    }), recipes.end());
    return document;
}

inline ContentRegistry contentBeforeThrownIllumination()
{
    return ContentRegistry::fromJson(contentBeforeThrownIlluminationJson().dump());
}

inline nlohmann::json contentBeforeArmorReadinessJson()
{
    auto document = contentBeforeThrownIlluminationJson();
    document["content_version"] = std::string{kFuelExtractionContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kArmorRepairBlueprintId.value()};
    }), items.end());
    auto &recipes = document["base_manufacturing"]["recipes"];
    recipes.erase(std::remove_if(recipes.begin(), recipes.end(), [](const auto &recipe) {
        return recipe.at("id") == std::string{kArmorRepairRecipeId.value()};
    }), recipes.end());
    return document;
}

inline ContentRegistry contentBeforeArmorReadiness()
{
    return ContentRegistry::fromJson(contentBeforeArmorReadinessJson().dump());
}

inline nlohmann::json contentBeforeFuelExtractionJson()
{
    auto document = contentBeforeArmorReadinessJson();
    document["content_version"] = std::string{kCrossMapFreightContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kRaidFuelCanId.value()};
    }), items.end());
    return document;
}

inline ContentRegistry contentBeforeFuelExtraction()
{
    return ContentRegistry::fromJson(contentBeforeFuelExtractionJson().dump());
}

inline nlohmann::json contentBeforeCrossMapFreightJson()
{
    auto document = contentBeforeFuelExtractionJson();
    document["content_version"] = std::string{kHospitalNightOperationsContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kFreightCrateKeyId.value()};
    }), items.end());
    return document;
}

inline ContentRegistry contentBeforeCrossMapFreight()
{
    return ContentRegistry::fromJson(contentBeforeCrossMapFreightJson().dump());
}

inline nlohmann::json contentBeforeHospitalNightOperationsJson()
{
    auto document = contentBeforeCrossMapFreightJson();
    document["content_version"] = std::string{kAmmoLoopContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kHospitalNightVisionId.value()};
    }), items.end());
    return document;
}

inline ContentRegistry contentBeforeHospitalNightOperations()
{
    return ContentRegistry::fromJson(contentBeforeHospitalNightOperationsJson().dump());
}

inline nlohmann::json contentBeforeAmmoLoopJson()
{
    auto document = contentBeforeHospitalNightOperationsJson();
    document["content_version"] = std::string{kOpticsContentVersion};
    auto &items = document["items"];
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) {
        return item.at("id") == std::string{kExpanding545AmmunitionId.value()};
    }), items.end());
    auto &recipes = document["base_manufacturing"]["recipes"];
    recipes.erase(std::remove_if(recipes.begin(), recipes.end(), [](const auto &recipe) {
        return recipe.at("id") == "base_manufacturing.ammunition_5_45x39_expanding";
    }), recipes.end());
    return document;
}

inline ContentRegistry contentBeforeAmmoLoop()
{
    return ContentRegistry::fromJson(contentBeforeAmmoLoopJson().dump());
}

inline ContentRegistry contentBeforeOptics()
{
    auto document = contentBeforeAmmoLoopJson();
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
