#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "inventory_domain.h"

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
}

TEST(InventoryDomainTest, StaleRevisionRejectsWithoutMutation)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const std::uint64_t fingerprint = profileStateFingerprint(profile);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision + 1, "stale"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST(InventoryDomainTest, EquipmentCommandsUseStableLocations)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "equip-rifle"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::PrimaryWeapon),
        rifle);
    EXPECT_EQ(profile.revision, 2U);
    EXPECT_TRUE(profile.committedTransactions.contains("equip-rifle"));
}

TEST(InventoryDomainTest, LongGunAndSidearmUseDistinctCompatibleSlots)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-multi-weapon",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const AssetInstanceId pistol = firstAsset(profile, alpha_content::pistol);

    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{rifle, EquipmentSlotKind::SecondaryWeapon},
        CommandContext{profile.revision, "equip-secondary"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{pistol, EquipmentSlotKind::Sidearm},
        CommandContext{profile.revision, "equip-sidearm"}).succeeded);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::SecondaryWeapon), rifle);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Sidearm), pistol);

    const std::uint64_t before = profileStateFingerprint(profile);
    const InventoryReceipt rejected = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{pistol, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "wrong-sidearm-slot"});
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(InventoryDomainTest, ProtectiveGearUsesDedicatedAtomicEquipmentSlots)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-armor",
        publishedContentRegistry());
    const AssetInstanceId helmet = firstAsset(profile, alpha_content::helmet);
    const AssetInstanceId bodyArmor = firstAsset(
        profile,
        alpha_content::bodyArmor);

    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{helmet, EquipmentSlotKind::Helmet},
        CommandContext{profile.revision, "equip-helmet"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{bodyArmor, EquipmentSlotKind::BodyArmor},
        CommandContext{profile.revision, "equip-body-armor"}).succeeded);

    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Helmet), helmet);
    EXPECT_EQ(
        equippedAsset(profile, EquipmentSlotKind::BodyArmor),
        bodyArmor);
    const std::uint64_t before = profileStateFingerprint(profile);
    const InventoryReceipt rejected = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{helmet, EquipmentSlotKind::BodyArmor},
        CommandContext{profile.revision, "wrong-armor-slot"});
    EXPECT_FALSE(rejected.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(InventoryDomainTest, EqualFootprintAssetsSwapAtomically)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const auto magazines = assets(profile, alpha_content::magazine);
    ASSERT_GE(magazines.size(), 2U);
    const auto firstBefore = std::get<StoredAssetLocation>(
        profile.assets.find(magazines[0])->location);
    const auto secondBefore = std::get<StoredAssetLocation>(
        profile.assets.find(magazines[1])->location);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            magazines[0],
            0,
            secondBefore,
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "swap-magazines"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        std::get<StoredAssetLocation>(
            profile.assets.find(magazines[0])->location),
        secondBefore);
    EXPECT_EQ(
        std::get<StoredAssetLocation>(
            profile.assets.find(magazines[1])->location),
        firstBefore);
}

TEST(InventoryDomainTest, LockedPartialQuantityCreatesOneStableSplit)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const AssetInstanceId ammunition =
        firstAsset(profile, alpha_content::ammunition);
    const AssetInstanceId splitId = profile.assets.nextAssetId();

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            ammunition,
            1,
            StoredAssetLocation{
                ProfileContainerId::stash(),
                GridPosition{18, 11}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "split-one"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_NE(profile.assets.find(splitId), nullptr);
    EXPECT_EQ(profile.assets.find(splitId)->quantity, 1U);
    EXPECT_EQ(profile.assets.find(ammunition)->quantity, 59U);
    EXPECT_EQ(profile.assets.nextAssetId(), splitId + 1U);
}

TEST(InventoryDomainTest, WholeStackMergePreservesDestinationStableId)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const auto ammunition = assets(profile, alpha_content::ammunition);
    ASSERT_EQ(ammunition.size(), 2U);
    AssetRecord *large = profile.assets.findMutable(ammunition[0]);
    AssetRecord *small = profile.assets.findMutable(ammunition[1]);
    ASSERT_NE(large, nullptr);
    ASSERT_NE(small, nullptr);
    large->quantity = 40;
    small->quantity = 20;
    const AssetInstanceId largeId = large->instanceId;
    const AssetInstanceId smallId = small->instanceId;
    const StoredAssetLocation largeLocation =
        std::get<StoredAssetLocation>(large->location);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            smallId,
            0,
            largeLocation,
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "merge-ammunition"});

    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    ASSERT_NE(profile.assets.find(largeId), nullptr);
    EXPECT_EQ(profile.assets.find(largeId)->quantity, 60U);
    EXPECT_EQ(profile.assets.find(smallId), nullptr);
}

TEST(InventoryDomainTest, ReliefAndOrdinaryAmmunitionCannotBeMerged)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-relief-provenance",
        publishedContentRegistry());
    const auto ammunition = assets(profile, alpha_content::ammunition);
    ASSERT_EQ(ammunition.size(), 2U);
    profile.assets.findMutable(ammunition[0])->reliefBatchId = "relief-1";
    const StoredAssetLocation destination = std::get<StoredAssetLocation>(
        profile.assets.find(ammunition[1])->location);
    const std::uint64_t before = profileStateFingerprint(profile);

    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            ammunition[0],
            0,
            destination,
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "reject-relief-laundering"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(receipt.error, DomainErrorCode::IllegalDestination);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(InventoryDomainTest, NonEmptyContainerCannotBeNested)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const AssetInstanceId chest = firstAsset(profile, alpha_content::chestRig);
    const AssetInstanceId backpack = firstAsset(profile, alpha_content::backpack);
    const AssetInstanceId medkit = firstAsset(profile, alpha_content::medkit);

    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            medkit,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpack, 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "fill-backpack"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{chest, EquipmentSlotKind::ChestRig},
        CommandContext{profile.revision, "equip-chest"}).succeeded);

    const std::uint64_t before = profileStateFingerprint(profile);
    const InventoryReceipt receipt = executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            backpack,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(chest, 2),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "illegal-nesting"});

    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(InventoryDomainTest, RepeatedTransactionIsIdempotent)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-test",
        publishedContentRegistry());
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const InventoryCommand command =
        InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon};
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        command,
        CommandContext{profile.revision, "same-command"}).succeeded);
    const std::uint64_t before = profileStateFingerprint(profile);

    const InventoryReceipt repeated = executeInventory(
        profile,
        publishedContentRegistry(),
        command,
        CommandContext{1, "same-command"});
    EXPECT_TRUE(repeated.succeeded);
    EXPECT_TRUE(repeated.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(InventoryDomainTest, QueryAndCommandRemainEquivalentAcrossCommandSequence)
{
    ProfileState profile = makeNewAlphaProfile(
        "inventory-sequence",
        publishedContentRegistry());

    for (int step = 0; step < 160; ++step)
    {
        ASSERT_TRUE(validateProfileState(
            profile,
            publishedContentRegistry()).valid);
        ASSERT_FALSE(profile.assets.records().empty());
        const std::size_t selected = static_cast<std::size_t>(step * 17) %
            profile.assets.records().size();
        auto iterator = profile.assets.records().begin();
        std::advance(iterator, static_cast<std::ptrdiff_t>(selected));
        const AssetInstanceId id = iterator->first;
        const AssetRecord &asset = iterator->second;
        const InventoryCommand command = InventoryMoveCommand{
            id,
            0,
            StoredAssetLocation{
                ProfileContainerId::stash(),
                GridPosition{(step * 7) % 20, (step * 11) % 12}},
            step % 3 == 0
                ? rotatedClockwise(asset.orientation)
                : asset.orientation};
        const InventoryPlan plan = queryInventory(
            profile,
            publishedContentRegistry(),
            command);
        const std::uint64_t before = profileStateFingerprint(profile);
        const InventoryReceipt receipt = executeInventory(
            profile,
            publishedContentRegistry(),
            command,
            CommandContext{
                profile.revision,
                "sequence-" + std::to_string(step)});
        EXPECT_EQ(receipt.succeeded, plan.canCommit);
        if (!receipt.succeeded)
        {
            EXPECT_EQ(profileStateFingerprint(profile), before);
        }
    }
    EXPECT_TRUE(validateProfileState(
        profile,
        publishedContentRegistry()).valid);
}
