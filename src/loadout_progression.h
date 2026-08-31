#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "content_registry.h"
#include "profile_state.h"

enum class LoadoutReadinessIssue
{
    Weapon,
    CompatibleAmmunition,
    BodyArmor,
    ChestRig,
    Backpack
};

struct LoadoutReadinessProjection
{
    LoadoutArchetypeDefinitionId archetypeId;
    std::string displayName;
    std::string description;
    MapDefinitionId recommendedMapDefinitionId;
    std::string recommendedMapDisplayName;
    bool recommendsHighRisk{};
    std::optional<AssetInstanceId> matchingWeaponAssetId;
    std::uint32_t compatibleRoundCount{};
    std::uint32_t minimumCompatibleRounds{};
    bool bodyArmorReady{};
    bool chestRigReady{};
    bool backpackReady{};
    std::vector<LoadoutReadinessIssue> issues;

    [[nodiscard]] bool ready() const noexcept
    {
        return issues.empty();
    }
};

// Pure projection: no inventory move, revision change, or ID allocation.
[[nodiscard]] LoadoutReadinessProjection projectLoadoutReadiness(
    const ProfileState &profile,
    const ContentRegistry &content,
    const LoadoutArchetypeDefinitionId &archetypeId);

[[nodiscard]] std::vector<LoadoutReadinessProjection>
projectAllLoadoutReadiness(
    const ProfileState &profile,
    const ContentRegistry &content);
