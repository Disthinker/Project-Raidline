#include "loadout_progression.h"

#include <algorithm>
#include <array>
#include <limits>
#include <span>

namespace
{
    bool contains(
        const std::vector<ItemDefinitionId> &ids,
        const ItemDefinitionId &id) noexcept
    {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }

    std::optional<AssetInstanceId> matchingEquippedAsset(
        const ProfileState &profile,
        const std::vector<ItemDefinitionId> &allowed,
        std::span<const EquipmentSlotKind> slots) noexcept
    {
        for (const EquipmentSlotKind slot : slots)
        {
            const auto id = equippedAsset(profile, slot);
            if (!id.has_value())
            {
                continue;
            }
            const AssetRecord *asset = profile.assets.find(*id);
            if (asset != nullptr && contains(allowed, asset->definitionId))
            {
                return id;
            }
        }
        return std::nullopt;
    }

    bool matchingEquipment(
        const ProfileState &profile,
        EquipmentSlotKind slot,
        const std::vector<ItemDefinitionId> &allowed) noexcept
    {
        const auto id = equippedAsset(profile, slot);
        if (!id.has_value())
        {
            return false;
        }
        const AssetRecord *asset = profile.assets.find(*id);
        return asset != nullptr && contains(allowed, asset->definitionId);
    }

    std::uint32_t compatibleRounds(
        const ProfileState &profile,
        const ContentRegistry &content,
        AssetInstanceId weaponAssetId)
    {
        const AssetRecord *weapon = profile.assets.find(weaponAssetId);
        if (weapon == nullptr)
        {
            return 0U;
        }

        std::uint64_t total{};
        if (weapon->chamberedRound.has_value() &&
            content.ammunitionFitsWeapon(
                weapon->chamberedRound->definitionId,
                weapon->definitionId))
        {
            ++total;
        }
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (!assetIsCarried(profile, id))
            {
                continue;
            }
            const ItemDefinition &definition = content.item(asset.definitionId);
            if (definition.category == ItemCategory::Ammunition &&
                content.ammunitionFitsWeapon(
                    asset.definitionId,
                    weapon->definitionId))
            {
                total += asset.quantity;
            }
            else if (definition.category == ItemCategory::Magazine &&
                     content.magazineFitsWeapon(
                         asset.definitionId,
                         weapon->definitionId))
            {
                total += static_cast<std::uint64_t>(std::count_if(
                    asset.magazineRounds.begin(),
                    asset.magazineRounds.end(),
                    [&](const MagazineRoundRecord &round)
                    {
                        return content.ammunitionFitsWeapon(
                            round.definitionId,
                            weapon->definitionId);
                    }));
            }
        }
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
            total,
            std::numeric_limits<std::uint32_t>::max()));
    }
}

LoadoutReadinessProjection projectLoadoutReadiness(
    const ProfileState &profile,
    const ContentRegistry &content,
    const LoadoutArchetypeDefinitionId &archetypeId)
{
    const LoadoutArchetypeDefinition &archetype =
        content.loadoutArchetype(archetypeId);
    LoadoutReadinessProjection result{
        archetype.id,
        archetype.displayName,
        archetype.description,
        archetype.recommendedMapDefinitionId,
        content.map(archetype.recommendedMapDefinitionId).displayName,
        archetype.recommendsHighRisk,
        std::nullopt,
        0U,
        archetype.minimumCompatibleRounds};

    constexpr std::array weaponSlots{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::SecondaryWeapon,
        EquipmentSlotKind::Sidearm};
    result.matchingWeaponAssetId = matchingEquippedAsset(
        profile, archetype.weaponDefinitionIds, weaponSlots);
    if (!result.matchingWeaponAssetId.has_value())
    {
        result.issues.push_back(LoadoutReadinessIssue::Weapon);
    }
    else
    {
        result.compatibleRoundCount = compatibleRounds(
            profile, content, *result.matchingWeaponAssetId);
    }
    if (result.compatibleRoundCount < result.minimumCompatibleRounds)
    {
        result.issues.push_back(LoadoutReadinessIssue::CompatibleAmmunition);
    }

    result.bodyArmorReady = matchingEquipment(
        profile,
        EquipmentSlotKind::BodyArmor,
        archetype.bodyArmorDefinitionIds);
    result.chestRigReady = matchingEquipment(
        profile,
        EquipmentSlotKind::ChestRig,
        archetype.chestRigDefinitionIds);
    result.backpackReady = matchingEquipment(
        profile,
        EquipmentSlotKind::Backpack,
        archetype.backpackDefinitionIds);
    if (!result.bodyArmorReady)
        result.issues.push_back(LoadoutReadinessIssue::BodyArmor);
    if (!result.chestRigReady)
        result.issues.push_back(LoadoutReadinessIssue::ChestRig);
    if (!result.backpackReady)
        result.issues.push_back(LoadoutReadinessIssue::Backpack);

    return result;
}

std::vector<LoadoutReadinessProjection> projectAllLoadoutReadiness(
    const ProfileState &profile,
    const ContentRegistry &content)
{
    std::vector<LoadoutReadinessProjection> result;
    result.reserve(content.loadoutArchetypes().size());
    for (const LoadoutArchetypeDefinition &archetype :
         content.loadoutArchetypes())
    {
        result.push_back(projectLoadoutReadiness(
            profile, content, archetype.id));
    }
    return result;
}
