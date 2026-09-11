#pragma once

#include "definition_id.h"
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

// A structure is not a carryable Registry asset, an enemy or a vector slot.
struct FortificationInstanceId
{
    std::uint64_t value{};
    friend auto operator<=>(const FortificationInstanceId &,
                            const FortificationInstanceId &) = default;
};

enum class DefenseSide : std::uint32_t
{
    West,
    East,
    North,
    South
};
struct DefenseSlotKey
{
    RegionalBaseSiteDefinitionId site;
    std::string plot;
    DefenseSide side{DefenseSide::West};
    friend auto operator<=>(const DefenseSlotKey &, const DefenseSlotKey &) = default;
};

struct FortificationDefinition
{
    FortificationDefinitionId id;
    std::string displayName;
    std::uint32_t maximumDurability{};
    std::uint32_t buildMaterialUnits{};
    std::uint32_t repairPerMaterialUnit{};
    float length{};
    float depth{};
};

struct FortificationRecord
{
    FortificationDefinitionId definition;
    std::uint32_t durability{};
    // nullopt is the one common reserve, including after Base migration.
    std::optional<DefenseSlotKey> slot;
    friend bool operator==(const FortificationRecord &, const FortificationRecord &) = default;
};

struct BaseFortificationState
{
    std::uint64_t nextInstanceId{1};
    std::map<FortificationInstanceId, FortificationRecord> instances;
    friend bool operator==(const BaseFortificationState &,
                           const BaseFortificationState &) = default;
};

inline const FortificationDefinitionId kWoodBarricadeDefinition{"fortification.wood_barricade"};
