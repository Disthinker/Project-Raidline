#include "sound_event.h"

#include <array>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace
{
    using Json = nlohmann::json;

    constexpr std::array<std::string_view, soundEventCount()> kEventNames{
        "weapon.pistol.fire",
        "weapon.rifle.fire",
        "weapon.fire.tail.outdoor",
        "weapon.mag.out",
        "weapon.mag.in",
        "weapon.chamber",
        "weapon.dry_fire",
        "weapon.malfunction.clear",
        "ui.confirm",
        "ui.deny",
        "inventory.pickup",
        "inventory.equip",
        "inventory.move_or_place",
        "medical.start",
        "medical.complete",
        "medical.interrupt",
        "player.hurt.light",
        "player.hurt.heavy",
        "infected.alert",
        "infected.hit",
        "infected.death",
        "impact.enemy",
        "impact.obstacle",
        "impact.ground",
        "ambience.base.safe_low",
        "ambience.raid.urban_low",
    };

    bool validRelativeWavePath(const std::filesystem::path &path)
    {
        if (path.empty() || path.is_absolute() ||
            path.extension() != ".wav")
        {
            return false;
        }
        for (const auto &component : path)
        {
            if (component == "..")
            {
                return false;
            }
        }
        return true;
    }

    float requiredFiniteFloat(const Json &value, const char *key)
    {
        if (!value.contains(key) || !value.at(key).is_number())
        {
            throw std::runtime_error(std::string{"missing numeric field: "} + key);
        }
        const float result = value.at(key).get<float>();
        if (!std::isfinite(result))
        {
            throw std::runtime_error(std::string{"non-finite field: "} + key);
        }
        return result;
    }
}
std::string_view soundEventName(SoundEventId id) noexcept
{
    const std::size_t index = static_cast<std::size_t>(id);
    return index < kEventNames.size() ? kEventNames[index] : "unknown";
}

std::optional<SoundEventId> parseSoundEventId(std::string_view name) noexcept
{
    for (std::size_t index{}; index < kEventNames.size(); ++index)
    {
        if (kEventNames[index] == name)
        {
            return static_cast<SoundEventId>(index);
        }
    }
    return std::nullopt;
}

SoundBankParseResult parseSoundBankJson(std::string_view json)
{
    try
    {
        const Json root = Json::parse(json);
        if (!root.is_object() || root.value("schema_version", 0) != 1)
        {
            throw std::runtime_error("sound bank requires schema_version 1");
        }
        SoundBankDefinition bank;
        bank.bankId = root.value("bank_id", std::string{});
        if (bank.bankId.empty())
        {
            throw std::runtime_error("sound bank requires a non-empty bank_id");
        }
        bank.masterGain = requiredFiniteFloat(root, "master_gain");
        if (bank.masterGain < 0.0F || bank.masterGain > 1.0F)
        {
            throw std::runtime_error("master_gain must be between 0 and 1");
        }
        if (!root.contains("events") || !root.at("events").is_array())
        {
            throw std::runtime_error("sound bank requires an events array");
        }

        std::unordered_set<std::size_t> seen;
        for (const Json &value : root.at("events"))
        {
            if (!value.is_object())
            {
                throw std::runtime_error("sound event must be an object");
            }
            const std::string name = value.value("id", std::string{});
            const auto id = parseSoundEventId(name);
            if (!id.has_value())
            {
                throw std::runtime_error("unknown sound event ID: " + name);
            }
            const std::size_t index = static_cast<std::size_t>(*id);
            if (!seen.insert(index).second)
            {
                throw std::runtime_error("duplicate sound event ID: " + name);
            }
            if (!value.contains("variants") ||
                !value.at("variants").is_array() ||
                value.at("variants").empty())
            {
                throw std::runtime_error("sound event requires variants: " + name);
            }

            SoundEventDefinition definition;
            definition.id = *id;
            for (const Json &variant : value.at("variants"))
            {
                if (!variant.is_string())
                {
                    throw std::runtime_error("sound variant must be a path: " + name);
                }
                std::filesystem::path path{variant.get<std::string>()};
                if (!validRelativeWavePath(path))
                {
                    throw std::runtime_error("invalid sound variant path: " + name);
                }
                definition.variants.push_back(std::move(path));
            }
            definition.gain = requiredFiniteFloat(value, "gain");
            definition.cooldownSeconds =
                requiredFiniteFloat(value, "cooldown_seconds");
            if (definition.gain < 0.0F || definition.gain > 2.0F ||
                definition.cooldownSeconds < 0.0F ||
                definition.cooldownSeconds > 10.0F)
            {
                throw std::runtime_error("sound event gain/cooldown out of range: " + name);
            }
            if (!value.contains("max_instances") ||
                !value.at("max_instances").is_number_unsigned())
            {
                throw std::runtime_error("sound event requires max_instances: " + name);
            }
            definition.maxInstances = value.at("max_instances").get<std::size_t>();
            if (definition.maxInstances == 0U || definition.maxInstances > 32U)
            {
                throw std::runtime_error("sound event max_instances out of range: " + name);
            }
            definition.loop = value.value("loop", false);
            const bool ambience = name.starts_with("ambience.");
            if (definition.loop != ambience)
            {
                throw std::runtime_error(
                    "only ambience events may loop and all ambience must loop: " + name);
            }
            bank.events.push_back(std::move(definition));
        }
        if (seen.size() != soundEventCount())
        {
            throw std::runtime_error("sound bank must define every stable event exactly once");
        }
        return SoundBankParseResult{std::move(bank), {}};
    }
    catch (const std::exception &error)
    {
        return SoundBankParseResult{std::nullopt, error.what()};
    }
}

SoundBankParseResult loadSoundBankDefinition(
    const std::filesystem::path &manifestPath)
{
    std::ifstream input{manifestPath, std::ios::binary};
    if (!input)
    {
        return SoundBankParseResult{
            std::nullopt,
            "could not open sound bank: " + manifestPath.string()};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    SoundBankParseResult parsed = parseSoundBankJson(contents.str());
    if (!parsed.succeeded())
    {
        parsed.message = manifestPath.string() + ": " + parsed.message;
    }
    return parsed;
}
