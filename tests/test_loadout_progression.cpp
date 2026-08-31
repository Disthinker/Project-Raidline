#include <gtest/gtest.h>

#include "loadout_progression.h"

namespace
{
    AssetInstanceId equip(
        ProfileState &profile,
        const ContentRegistry &content,
        const char *definitionId,
        EquipmentSlotKind slot)
    {
        return profile.assets.create(
            content.item(ItemDefinitionId{definitionId}),
            EquippedAssetLocation{slot});
    }

    ProfileState makeBalancedLoadout(const ContentRegistry &content)
    {
        ProfileState profile;
        const AssetInstanceId weapon = equip(
            profile,
            content,
            "item.weapon.rifle_5_45_service",
            EquipmentSlotKind::PrimaryWeapon);
        equip(
            profile,
            content,
            "item.protective_gear.body_armor_basic",
            EquipmentSlotKind::BodyArmor);
        const AssetInstanceId rig = equip(
            profile,
            content,
            "item.container.chest_rig_patrol",
            EquipmentSlotKind::ChestRig);
        equip(
            profile,
            content,
            "item.container.backpack_field",
            EquipmentSlotKind::Backpack);

        const AssetInstanceId installed = profile.assets.create(
            content.item(ItemDefinitionId{"item.magazine.5_45x39_30"}),
            InstalledMagazineLocation{weapon});
        AssetRecord *installedRecord = profile.assets.findMutable(installed);
        installedRecord->magazineRounds.assign(
            30U,
            MagazineRoundRecord{
                ItemDefinitionId{"item.ammunition.5_45x39_standard"},
                std::nullopt});

        const AssetInstanceId spare = profile.assets.create(
            content.item(ItemDefinitionId{"item.magazine.5_45x39_30"}),
            StoredAssetLocation{
                ProfileContainerId::compartment(rig, 0U),
                GridPosition{}});
        AssetRecord *spareRecord = profile.assets.findMutable(spare);
        spareRecord->magazineRounds.assign(
            29U,
            MagazineRoundRecord{
                ItemDefinitionId{"item.ammunition.5_45x39_enhanced"},
                std::nullopt});
        profile.assets.findMutable(weapon)->chamberedRound =
            MagazineRoundRecord{
                ItemDefinitionId{"item.ammunition.5_45x39_standard"},
                std::nullopt};
        return profile;
    }
}

TEST(LoadoutProgressionTest, PublishedRolesHaveStableTargets)
{
    const ContentRegistry &content = publishedContentRegistry();
    ASSERT_EQ(content.loadoutArchetypes().size(), 3U);

    const auto &light = content.loadoutArchetype(
        LoadoutArchetypeDefinitionId{
            "loadout_archetype.light_scavenger"});
    EXPECT_EQ(light.recommendedMapDefinitionId,
              MapDefinitionId{"map.raid.riverside"});
    EXPECT_FALSE(light.recommendsHighRisk);

    const auto &heavy = content.loadoutArchetype(
        LoadoutArchetypeDefinitionId{
            "loadout_archetype.heavy_specialist"});
    EXPECT_EQ(heavy.recommendedMapDefinitionId,
              MapDefinitionId{"map.raid.frontier_exchange"});
    EXPECT_TRUE(heavy.recommendsHighRisk);
}

TEST(LoadoutProgressionTest, ProjectionCountsOnlyCarriedCompatibleRoundsOnce)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeBalancedLoadout(content);
    static_cast<void>(profile.assets.create(
        content.item(ItemDefinitionId{"item.ammunition.5_45x39_standard"}),
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{20, 0}},
        60U));
    static_cast<void>(profile.assets.create(
        content.item(ItemDefinitionId{"item.ammunition.9mm_basic"}),
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{21, 0}},
        60U));

    const std::uint64_t before = profileStateFingerprint(profile);
    const LoadoutReadinessProjection projection = projectLoadoutReadiness(
        profile,
        content,
        LoadoutArchetypeDefinitionId{
            "loadout_archetype.balanced_operator"});

    EXPECT_TRUE(projection.ready());
    EXPECT_EQ(projection.compatibleRoundCount, 60U);
    EXPECT_EQ(projection.minimumCompatibleRounds, 60U);
    EXPECT_EQ(projection.recommendedMapDefinitionId,
              MapDefinitionId{"map.raid.industrial"});
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(LoadoutProgressionTest, ProjectionReportsEachRealLoadoutGap)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile;
    equip(
        profile,
        content,
        "item.weapon.pistol_basic",
        EquipmentSlotKind::Sidearm);
    const AssetInstanceId rig = equip(
        profile,
        content,
        "item.container.chest_rig_small",
        EquipmentSlotKind::ChestRig);
    equip(
        profile,
        content,
        "item.container.backpack_small",
        EquipmentSlotKind::Backpack);
    static_cast<void>(profile.assets.create(
        content.item(ItemDefinitionId{"item.ammunition.9mm_basic"}),
        StoredAssetLocation{
            ProfileContainerId::compartment(rig, 2U), GridPosition{}},
        29U));

    const LoadoutReadinessProjection projection = projectLoadoutReadiness(
        profile,
        content,
        LoadoutArchetypeDefinitionId{
            "loadout_archetype.light_scavenger"});

    EXPECT_FALSE(projection.ready());
    EXPECT_EQ(projection.compatibleRoundCount, 29U);
    EXPECT_EQ(projection.issues,
              (std::vector{
                  LoadoutReadinessIssue::CompatibleAmmunition,
                  LoadoutReadinessIssue::BodyArmor}));
    EXPECT_TRUE(projection.chestRigReady);
    EXPECT_TRUE(projection.backpackReady);
}

TEST(LoadoutProgressionTest, AllRoleProjectionPreservesPublishedOrder)
{
    const ContentRegistry &content = publishedContentRegistry();
    const ProfileState profile = makeBalancedLoadout(content);
    const auto projections = projectAllLoadoutReadiness(profile, content);
    ASSERT_EQ(projections.size(), 3U);
    EXPECT_EQ(projections[0].archetypeId.value(),
              "loadout_archetype.light_scavenger");
    EXPECT_EQ(projections[1].archetypeId.value(),
              "loadout_archetype.balanced_operator");
    EXPECT_EQ(projections[2].archetypeId.value(),
              "loadout_archetype.heavy_specialist");
    EXPECT_TRUE(projections[1].ready());
}
