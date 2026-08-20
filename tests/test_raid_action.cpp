#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "raid_action.h"

namespace
{
std::vector<AssetInstanceId> assets(
    const ProfileState &profile,
    const ItemDefinitionId &definitionId)
{
    std::vector<AssetInstanceId> result;
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            result.push_back(id);
        }
    }
    return result;
}

void commitInventory(
    ProfileState &profile,
    const InventoryCommand &command,
    std::string transactionId)
{
    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        command,
        CommandContext{profile.revision, std::move(transactionId)});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
}
}

TEST(RaidActionTest, TimedActionCompletesOrInterruptsWithoutPartialCommit)
{
    RaidActionState state;
    ASSERT_TRUE(state.start(HealRaidAction{7, 0.0F, 5.0F}));
    EXPECT_EQ(state.update(2.0F, false), RaidActionAdvance::Running);
    EXPECT_FLOAT_EQ(state.progress(), 0.4F);
    EXPECT_EQ(state.update(0.1F, true), RaidActionAdvance::Interrupted);
    EXPECT_FALSE(state.active().has_value());
    EXPECT_FALSE(state.takeCompleted().has_value());

    ASSERT_TRUE(state.start(ReloadRaidAction{1, 2, 0.0F, 2.0F}));
    EXPECT_EQ(state.update(2.0F, false), RaidActionAdvance::Completed);
    ASSERT_TRUE(state.takeCompleted().has_value());

    ASSERT_TRUE(state.start(LoadMagazineRaidAction{
        2,
        5,
        4,
        0.0F,
        0.8F}));
    EXPECT_EQ(state.update(0.8F, false), RaidActionAdvance::Completed);
    ASSERT_TRUE(state.takeCompleted().has_value());

    ASSERT_TRUE(state.start(MedicalRaidAction{
        9,
        MedicalUseEffect::StopAnyBleeding,
        true,
        false,
        0,
        0,
        0.0F,
        4.0F}));
    EXPECT_EQ(state.update(4.0F, false), RaidActionAdvance::Completed);
    ASSERT_TRUE(state.takeCompleted().has_value());

    ASSERT_TRUE(state.start(UnloadMagazineRaidAction{
        3,
        ProfileContainerId::compartment(4, 0),
        0.0F,
        3.0F}));
    EXPECT_EQ(state.update(3.0F, false), RaidActionAdvance::Completed);
    const auto unloaded = state.takeCompleted();
    ASSERT_TRUE(unloaded.has_value());
    EXPECT_NE(std::get_if<UnloadMagazineRaidAction>(&*unloaded), nullptr);

    ASSERT_TRUE(state.start(WeaponSwitchRaidAction{
        EquipmentSlotKind::PrimaryWeapon,
        EquipmentSlotKind::Sidearm,
        0.0F,
        0.35F}));
    EXPECT_EQ(state.update(0.34F, false), RaidActionAdvance::Running);
    EXPECT_EQ(state.update(0.01F, false), RaidActionAdvance::Completed);
    const auto switched = state.takeCompleted();
    ASSERT_TRUE(switched.has_value());
    const auto *weaponSwitch = std::get_if<WeaponSwitchRaidAction>(&*switched);
    ASSERT_NE(weaponSwitch, nullptr);
    EXPECT_EQ(weaponSwitch->sourceSlot, EquipmentSlotKind::PrimaryWeapon);
    EXPECT_EQ(weaponSwitch->targetSlot, EquipmentSlotKind::Sidearm);
}

TEST(RaidActionTest, ReloadSelectsFullestCompatibleChestMagazine)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-action-reload",
        publishedContentRegistry());
    const auto rifles = assets(profile, alpha_content::rifle);
    const auto chests = assets(profile, alpha_content::chestRig);
    const auto magazines = assets(profile, alpha_content::magazine);
    const auto ammunition = assets(profile, alpha_content::ammunition);
    ASSERT_EQ(rifles.size(), 1U);
    ASSERT_EQ(chests.size(), 1U);
    ASSERT_GE(magazines.size(), 2U);

    commitInventory(profile,
        InventoryEquipCommand{chests.front(), EquipmentSlotKind::ChestRig},
        "equip-chest");
    commitInventory(profile,
        InventoryMoveCommand{
            magazines[0], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests.front(), 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "pocket-mag-0");
    commitInventory(profile,
        InventoryMoveCommand{
            magazines[1], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests.front(), 1),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "pocket-mag-1");
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazines[0], ammunition[0], 10},
        CommandContext{profile.revision, "load-ten"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazines[1], ammunition[1], 20},
        CommandContext{profile.revision, "load-twenty"}).succeeded);

    EXPECT_EQ(selectRaidReloadMagazine(
        profile,
        publishedContentRegistry(),
        rifles.front()),
        magazines[1]);
}

TEST(RaidActionTest, RaidHealRequiresCarriedMedkitAndConsumesOneCharge)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-action-heal",
        publishedContentRegistry());
    const auto chests = assets(profile, alpha_content::chestRig);
    const auto medkits = assets(profile, alpha_content::medkit);
    ASSERT_EQ(chests.size(), 1U);
    ASSERT_EQ(medkits.size(), 2U);
    profile.currentHealth = 40;

    const HealReceipt rejected = executeHeal(
        profile,
        publishedContentRegistry(),
        medkits[0],
        HealAccess::CarriedOnly,
        CommandContext{profile.revision, "heal-from-stash"});
    EXPECT_FALSE(rejected.succeeded);

    commitInventory(profile,
        InventoryEquipCommand{chests.front(), EquipmentSlotKind::ChestRig},
        "equip-heal-chest");
    commitInventory(profile,
        InventoryMoveCommand{
            medkits[0], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chests.front(), 2),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "pocket-medkit");
    EXPECT_EQ(selectQuickMedkit(profile, publishedContentRegistry()), medkits[0]);

    const HealReceipt healed = executeHeal(
        profile,
        publishedContentRegistry(),
        medkits[0],
        HealAccess::CarriedOnly,
        CommandContext{profile.revision, "heal-carried"});
    ASSERT_TRUE(healed.succeeded) << healed.message;
    EXPECT_EQ(healed.healedAmount, 30);
    EXPECT_EQ(profile.currentHealth, 70);
    ASSERT_NE(profile.assets.find(medkits[0]), nullptr);
    EXPECT_EQ(profile.assets.find(medkits[0])->remainingCharges, 2U);
}

TEST(RaidActionTest, MagazineUnloadPrefersBackpackAndRejectsStashAsset)
{
    ProfileState profile = makeNewAlphaProfile(
        "raid-action-unload",
        publishedContentRegistry());
    const auto backpacks = assets(profile, alpha_content::backpack);
    const auto magazines = assets(profile, alpha_content::magazine);
    const auto ammunition = assets(profile, alpha_content::ammunition);
    ASSERT_EQ(backpacks.size(), 1U);
    ASSERT_GE(magazines.size(), 2U);
    ASSERT_GE(ammunition.size(), 1U);

    EXPECT_FALSE(selectRaidMagazineUnloadDestination(
        profile,
        publishedContentRegistry(),
        magazines[0]).has_value());

    commitInventory(profile,
        InventoryEquipCommand{
            backpacks.front(), EquipmentSlotKind::Backpack},
        "equip-unload-backpack");
    commitInventory(profile,
        InventoryMoveCommand{
            magazines[0], 0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpacks.front(), 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        "carry-unload-magazine");
    ASSERT_TRUE(executeWeaponAmmo(
        profile,
        publishedContentRegistry(),
        LoadMagazineCommand{magazines[0], ammunition[0], 10},
        CommandContext{profile.revision, "load-unload-magazine"}).succeeded);

    EXPECT_EQ(
        selectRaidMagazineUnloadDestination(
            profile,
            publishedContentRegistry(),
            magazines[0]),
        ProfileContainerId::compartment(backpacks.front(), 0));

    const ProfileContainerId backpack =
        ProfileContainerId::compartment(backpacks.front(), 0);
    const ItemDefinition &ammunitionDefinition =
        publishedContentRegistry().item(alpha_content::ammunition);
    const InventoryGridSize size = profileContainerSize(
        profile, publishedContentRegistry(), backpack);
    for (int y = 0; y < size.height; ++y)
    {
        for (int x = 0; x < size.width; ++x)
        {
            if (!profileAssetAtCell(
                    profile,
                    publishedContentRegistry(),
                    backpack,
                    GridPosition{x, y}).has_value())
            {
                static_cast<void>(profile.assets.create(
                    ammunitionDefinition,
                    StoredAssetLocation{backpack, GridPosition{x, y}},
                    ammunitionDefinition.maxStackSize));
            }
        }
    }
    const std::uint64_t beforeCapacityQuery =
        profileStateFingerprint(profile);
    EXPECT_FALSE(selectRaidMagazineUnloadDestination(
        profile,
        publishedContentRegistry(),
        magazines[0]).has_value());
    EXPECT_EQ(profileStateFingerprint(profile), beforeCapacityQuery);
}

TEST(RaidActionTest, WeaponMaintenanceUsesEightSecondAtomicTimeline)
{
    RaidActionState state;
    ASSERT_TRUE(state.start(WeaponMaintenanceRaidAction{
        11, 12, 0.0F, 8.0F}));
    EXPECT_EQ(state.update(7.5F, false), RaidActionAdvance::Running);
    EXPECT_NEAR(state.progress(), 0.9375F, 0.0001F);
    EXPECT_EQ(state.update(0.5F, false), RaidActionAdvance::Completed);
    const std::optional<RaidAction> completed = state.takeCompleted();
    ASSERT_TRUE(completed.has_value());
    const auto *maintenance = std::get_if<WeaponMaintenanceRaidAction>(
        &*completed);
    ASSERT_NE(maintenance, nullptr);
    EXPECT_EQ(maintenance->kitAssetId, 11U);
    EXPECT_EQ(maintenance->weaponAssetId, 12U);
}

TEST(RaidActionTest, ArmorMaintenanceUsesSixSecondAtomicTimeline)
{
    RaidActionState state;
    ASSERT_TRUE(state.start(ArmorMaintenanceRaidAction{
        11, 12, 0.0F, 6.0F}));
    EXPECT_EQ(state.update(5.5F, false), RaidActionAdvance::Running);
    EXPECT_EQ(state.update(0.5F, false), RaidActionAdvance::Completed);
    const std::optional<RaidAction> completed = state.takeCompleted();
    ASSERT_TRUE(completed.has_value());
    const auto *maintenance = std::get_if<ArmorMaintenanceRaidAction>(
        &*completed);
    ASSERT_NE(maintenance, nullptr);
    EXPECT_EQ(maintenance->kitAssetId, 11U);
    EXPECT_EQ(maintenance->armorAssetId, 12U);
}
