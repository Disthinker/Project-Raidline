#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "base_ground_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreyline{
    "regional_base_site.greyline_yard"};

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

BaseGroundAccess access(bool stashAccessible = true)
{
    return BaseGroundAccess{
        kGreyline,
        Vec2{120.0F, 100.0F},
        Vec2{160.0F, 100.0F},
        stashAccessible,
        84.0F};
}
}

TEST(BaseGroundDomainTest, StashDropIsPureThenCommitsAndIsIdempotent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-ground-drop", content);
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    ASSERT_NE(rifle, 0U);
    const std::uint64_t beforeQuery = profileStateFingerprint(profile);
    const BaseGroundCommand command = DropBaseGroundAssetCommand{
        rifle, 0, ItemOrientation::Degrees0, access()};

    const BaseGroundPlan plan = queryBaseGround(profile, content, command);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    const ProfileRevision revision = profile.revision;
    BaseGroundReceipt receipt = executeBaseGround(
        profile, content, command,
        CommandContext{revision, "drop-rifle"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto *ground = std::get_if<BaseGroundAssetLocation>(
        &profile.assets.find(rifle)->location);
    ASSERT_NE(ground, nullptr);
    EXPECT_EQ(ground->baseSiteDefinitionId, kGreyline);
    EXPECT_FLOAT_EQ(ground->position.x, 160.0F);
    const std::uint64_t committed = profileStateFingerprint(profile);

    receipt = executeBaseGround(
        profile, content, command,
        CommandContext{revision, "drop-rifle"});
    EXPECT_TRUE(receipt.succeeded);
    EXPECT_TRUE(receipt.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);
}

TEST(BaseGroundDomainTest, RemoteStashDropAndWrongSiteRejectWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-ground-reject", content);
    const AssetInstanceId rifle = firstAsset(profile, alpha_content::rifle);
    const std::uint64_t before = profileStateFingerprint(profile);

    BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            rifle, 0, ItemOrientation::Degrees0, access(false)},
        CommandContext{profile.revision, "remote-drop"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    BaseGroundAccess wrong = access();
    wrong.baseSiteDefinitionId = RegionalBaseSiteDefinitionId{
        "regional_base_site.ashworks_logistics_yard"};
    receipt = executeBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            rifle, 0, ItemOrientation::Degrees0, wrong},
        CommandContext{profile.revision, "wrong-site"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(BaseGroundDomainTest, PartialStackGetsStableGroundInstance)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-ground-split", content);
    const ItemDefinition &ammunition = content.item(alpha_content::ammunition);
    const auto origin = findFirstProfileFit(
        profile, content, ProfileContainerId::stash(), ammunition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId stack = profile.assets.create(
        ammunition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin},
        12);
    const AssetInstanceId nextBefore = profile.assets.nextAssetId();

    const BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            stack, 5, ItemOrientation::Degrees0, access()},
        CommandContext{profile.revision, "split-ammo"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.affectedAssetId, nextBefore);
    EXPECT_EQ(profile.assets.find(stack)->quantity, 7U);
    ASSERT_NE(profile.assets.find(nextBefore), nullptr);
    EXPECT_EQ(profile.assets.find(nextBefore)->quantity, 5U);
    EXPECT_TRUE(std::holds_alternative<BaseGroundAssetLocation>(
        profile.assets.find(nextBefore)->location));
}

TEST(BaseGroundDomainTest, PickupMergesIntoCarriedStackAndChecksRange)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-ground-pickup", content);
    const AssetInstanceId backpack = firstAsset(
        profile, alpha_content::backpack);
    ASSERT_TRUE(executeInventory(
        profile,
        content,
        InventoryEquipCommand{backpack, EquipmentSlotKind::Backpack},
        CommandContext{profile.revision, "equip-pack"}).succeeded);

    const ItemDefinition &ammunition = content.item(alpha_content::ammunition);
    const ProfileContainerId pack = ProfileContainerId::compartment(
        backpack, 0);
    const auto origin = findFirstProfileFit(
        profile, content, pack, ammunition, ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId carried = profile.assets.create(
        ammunition, StoredAssetLocation{pack, *origin}, 7);
    const AssetInstanceId ground = profile.assets.create(
        ammunition,
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}},
        5);
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    BaseGroundAccess tooFar = access();
    tooFar.playerCenter = Vec2{400.0F, 400.0F};
    const std::uint64_t before = profileStateFingerprint(profile);
    BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{ground, tooFar},
        CommandContext{profile.revision, "far-pickup"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{ground, access()},
        CommandContext{profile.revision, "near-pickup"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.affectedAssetId, carried);
    EXPECT_EQ(profile.assets.find(carried)->quantity, 12U);
    EXPECT_EQ(profile.assets.find(ground), nullptr);
}

TEST(BaseGroundDomainTest, GroundContainerWithContentsQuickEquipsAsOneTree)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile("base-ground-tree", content);
    const ItemDefinition &backpackDefinition = content.item(
        alpha_content::backpack);
    const AssetInstanceId backpack = profile.assets.create(
        backpackDefinition,
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}});
    const ItemDefinition &medkitDefinition = content.item(
        alpha_content::medkit);
    const AssetInstanceId child = profile.assets.create(
        medkitDefinition,
        StoredAssetLocation{
            ProfileContainerId::compartment(backpack, 0),
            GridPosition{0, 0}});
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{backpack, access()},
        CommandContext{profile.revision, "pickup-tree"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        std::get<EquippedAssetLocation>(
            profile.assets.find(backpack)->location).slot,
        EquipmentSlotKind::Backpack);
    const auto childLocation = std::get<StoredAssetLocation>(
        profile.assets.find(child)->location);
    EXPECT_EQ(childLocation.container.ownerAssetId, backpack);
}
