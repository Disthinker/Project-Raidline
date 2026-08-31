#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "maintenance_domain.h"

namespace
{
AssetInstanceId firstAsset(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    const auto found = std::find_if(
        profile.assets.records().begin(),
        profile.assets.records().end(),
        [&definitionId](const auto &entry)
        {
            return entry.second.definitionId == definitionId;
        });
    return found == profile.assets.records().end() ? 0 : found->first;
}
}

TEST(MaintenanceDomainTest, BaseMaintenanceRestoresCurrentWithoutMaximumLoss)
{
    ProfileState profile = makeNewAlphaProfile(
        "maintenance-base", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::weaponMaintenanceKit);
    profile.assets.findMutable(rifle)->currentDurability = 8000;

    const WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile.revision, "base-maintenance"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.restoredDurabilityCenti, 2000U);
    EXPECT_EQ(receipt.consumedCapacityCenti, 2000U);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, 10000U);
    EXPECT_EQ(profile.assets.find(rifle)->currentMaximumDurability, 10000U);
    ASSERT_NE(profile.assets.find(kit), nullptr);
    EXPECT_EQ(profile.assets.find(kit)->remainingCharges, 500U);
}

TEST(MaintenanceDomainTest, RaidMaintenanceReducesMaximumAndIsAtomic)
{
    ProfileState profile = makeNewAlphaProfile(
        "maintenance-raid", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::weaponMaintenanceKit);
    profile.assets.findMutable(rifle)->currentDurability = 5000;

    const WeaponMaintenancePlan plan = queryWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Raid});
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.actionDurationMs, 8000U);
    EXPECT_EQ(plan.restoredDurabilityCenti, 2500U);
    EXPECT_EQ(plan.currentMaximumAfterCenti, 9750U);

    const WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Raid},
        CommandContext{profile.revision, "raid-maintenance"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, 7500U);
    EXPECT_EQ(profile.assets.find(rifle)->currentMaximumDurability, 9750U);
    EXPECT_EQ(profile.assets.find(kit), nullptr);
}

TEST(MaintenanceDomainTest, ClearingOnlyAMalfunctionConsumesMinimumCapacity)
{
    ProfileState profile = makeNewAlphaProfile(
        "maintenance-fault", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::weaponMaintenanceKit);
    profile.assets.findMutable(rifle)->weaponMalfunction =
        WeaponMalfunctionType::Stovepipe;

    const WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile.revision, "clear-only"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_TRUE(receipt.clearedMalfunction);
    EXPECT_EQ(receipt.restoredDurabilityCenti, 0U);
    EXPECT_EQ(receipt.consumedCapacityCenti, 1U);
    EXPECT_EQ(profile.assets.find(kit)->remainingCharges, 2499U);
    EXPECT_EQ(profile.assets.find(rifle)->weaponMalfunction,
              WeaponMalfunctionType::None);
}

TEST(MaintenanceDomainTest, RejectionPreservesStateAndStableIdHighWater)
{
    ProfileState profile = makeNewAlphaProfile(
        "maintenance-reject", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::weaponMaintenanceKit);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const AssetInstanceId nextId = profile.assets.nextAssetId();

    const WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile.revision, "unneeded"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), nextId);
}

TEST(MaintenanceDomainTest, RaidRepairAtMaximumFloorCanRestoreCurrentOnly)
{
    ProfileState profile = makeNewAlphaProfile(
        "maintenance-floor", publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::weaponMaintenanceKit);
    AssetRecord *weapon = profile.assets.findMutable(rifle);
    weapon->currentMaximumDurability = 2000;
    weapon->currentDurability = 1000;

    const WeaponMaintenanceReceipt receipt = executeWeaponMaintenance(
        profile,
        publishedContentRegistry(),
        WeaponMaintenanceCommand{
            kit, rifle, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Raid},
        CommandContext{profile.revision, "floor-repair"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(rifle)->currentMaximumDurability, 2000U);
    EXPECT_EQ(profile.assets.find(rifle)->currentDurability, 2000U);
    EXPECT_EQ(receipt.restoredDurabilityCenti, 1000U);
}

TEST(MaintenanceDomainTest, BaseArmorRepairUsesSoftMaterialCostAndMaximumLoss)
{
    ProfileState profile = makeNewAlphaProfile(
        "armor-maintenance-base", publishedContentRegistry());
    const AssetInstanceId armor = firstAsset(
        profile, alpha_content::bodyArmor);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::armorMaintenanceKit);
    profile.assets.findMutable(armor)->currentDurability = 50;

    const ArmorMaintenanceReceipt receipt = executeArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, armor, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile.revision, "base-armor-maintenance"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.restoredDurability, 50U);
    EXPECT_EQ(receipt.consumedCapacityCenti, 5000U);
    EXPECT_EQ(receipt.currentMaximumBefore, 120U);
    EXPECT_EQ(receipt.currentMaximumAfter, 115U);
    EXPECT_EQ(profile.assets.find(armor)->currentDurability, 100U);
    EXPECT_EQ(profile.assets.find(armor)->currentMaximumDurability, 115U);
    EXPECT_EQ(profile.assets.find(kit), nullptr);
}

TEST(MaintenanceDomainTest, RaidArmorRepairUsesCompositeCostAndSixSecondPlan)
{
    ProfileState profile = makeNewAlphaProfile(
        "armor-maintenance-raid", publishedContentRegistry());
    const AssetInstanceId helmet = firstAsset(
        profile, alpha_content::helmet);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::armorMaintenanceKit);
    profile.assets.findMutable(helmet)->currentDurability = 60;

    const ArmorMaintenancePlan plan = queryArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, helmet, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Raid});

    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.actionDurationMs, 6000U);
    EXPECT_EQ(plan.restoredDurability, 33U);
    EXPECT_EQ(plan.consumedCapacityCenti, 4950U);
    EXPECT_EQ(plan.currentMaximumAfter, 93U);
}

TEST(MaintenanceDomainTest, HeavyMetalArmorUsesHighestRepairPointCost)
{
    ProfileState profile = makeNewPublishedProfile(
        "heavy-armor-maintenance",
        publishedContentRegistry());
    const AssetInstanceId armor = firstAsset(
        profile,
        ItemDefinitionId{"item.protective_gear.body_armor_heavy"});
    const AssetInstanceId kit = firstAsset(
        profile,
        alpha_content::armorMaintenanceKit);
    ASSERT_NE(armor, 0U);
    ASSERT_NE(kit, 0U);
    profile.assets.findMutable(armor)->currentDurability = 150U;

    const ArmorMaintenancePlan plan = queryArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, armor, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base});

    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(plan.restoredDurability, 25U);
    EXPECT_EQ(plan.consumedCapacityCenti, 5000U);
    EXPECT_EQ(plan.currentMaximumBefore, 180U);
    EXPECT_EQ(plan.currentMaximumAfter, 177U);
}

TEST(MaintenanceDomainTest, ArmorRepairAtMaximumFloorRestoresWithoutFurtherLoss)
{
    ProfileState profile = makeNewAlphaProfile(
        "armor-maintenance-floor", publishedContentRegistry());
    const AssetInstanceId armor = firstAsset(
        profile, alpha_content::bodyArmor);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::armorMaintenanceKit);
    AssetRecord *record = profile.assets.findMutable(armor);
    record->currentMaximumDurability = 24;
    record->currentDurability = 10;

    const ArmorMaintenanceReceipt receipt = executeArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, armor, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Raid},
        CommandContext{profile.revision, "floor-armor-maintenance"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.restoredDurability, 14U);
    EXPECT_EQ(receipt.currentMaximumAfter, 24U);
    EXPECT_EQ(profile.assets.find(armor)->currentDurability, 24U);
}

TEST(MaintenanceDomainTest, ArmorMaintenanceRejectionPreservesProfile)
{
    ProfileState profile = makeNewAlphaProfile(
        "armor-maintenance-reject", publishedContentRegistry());
    const AssetInstanceId helmet = firstAsset(
        profile, alpha_content::helmet);
    const AssetInstanceId kit = firstAsset(
        profile, alpha_content::armorMaintenanceKit);
    profile.assets.findMutable(helmet)->currentDurability = 90;
    profile.assets.findMutable(kit)->remainingCharges = 149;
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const AssetInstanceId nextId = profile.assets.nextAssetId();

    const ArmorMaintenanceReceipt receipt = executeArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, helmet, MaintenanceAccess::AnyOwned,
            MaintenanceLocation::Base},
        CommandContext{profile.revision, "insufficient-armor-maintenance"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), nextId);

    const ArmorMaintenancePlan carried = queryArmorMaintenance(
        profile,
        publishedContentRegistry(),
        ArmorMaintenanceCommand{
            kit, helmet, MaintenanceAccess::CarriedOnly,
            MaintenanceLocation::Raid});
    EXPECT_FALSE(carried.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}
