#include <gtest/gtest.h>

#include <algorithm>

#include "alpha_content_ids.h"
#include "base_ground_domain.h"

namespace
{
const RegionalBaseSiteDefinitionId kGreyline{
    "regional_base_site.greyline_yard"};
const ItemDefinitionId kBaseStorageCrate{
    "item.container.base_storage_crate"};

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

BaseGroundAccess placementAccess(
    Vec2 dropPosition,
    ContentRect parcel = {{200.0F, 200.0F}, {600.0F, 500.0F}},
    std::vector<ContentRect> blockers = {})
{
    BaseGroundAccess result = access();
    result.dropPosition = dropPosition;
    result.placementContext = BaseGroundPlacementContext{
        parcel, std::move(blockers)};
    return result;
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

TEST(BaseGroundDomainTest, OpenContainerScopeMovesItemsBothWaysAndIsIdempotent)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-ground-container-transfer", content);
    const AssetInstanceId carriedBackpack = firstAsset(
        profile, alpha_content::backpack);
    ASSERT_NE(carriedBackpack, 0U);
    ASSERT_TRUE(executeInventory(
        profile,
        content,
        InventoryEquipCommand{
            carriedBackpack, EquipmentSlotKind::Backpack},
        CommandContext{profile.revision, "equip-carried-pack"}).succeeded);

    const ItemDefinition &groundPackDefinition = content.item(
        alpha_content::backpack);
    const AssetInstanceId groundPack = profile.assets.create(
        groundPackDefinition,
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}});
    const AssetInstanceId medkit = profile.assets.create(
        content.item(alpha_content::medkit),
        StoredAssetLocation{
            ProfileContainerId::compartment(groundPack, 0),
            GridPosition{0, 0}});
    const ProfileContainerId carried = ProfileContainerId::compartment(
        carriedBackpack, 0);
    const InventoryCommand take = InventoryMoveCommand{
        medkit,
        0,
        StoredAssetLocation{carried, GridPosition{0, 0}},
        ItemOrientation::Degrees0};
    const std::uint64_t beforeQuery = profileStateFingerprint(profile);
    const InventoryPlan plan = queryBaseGroundContainerInventory(
        profile, content, groundPack, access(), take);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    const ProfileRevision revision = profile.revision;
    const InventoryReceipt stale = executeBaseGroundContainerInventory(
        profile,
        content,
        groundPack,
        access(),
        take,
        CommandContext{revision + 1U, "stale-ground-container"});
    EXPECT_FALSE(stale.succeeded);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    InventoryReceipt receipt = executeBaseGroundContainerInventory(
        profile,
        content,
        groundPack,
        access(),
        take,
        CommandContext{revision, "take-ground-medkit"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        std::get<StoredAssetLocation>(
            profile.assets.find(medkit)->location).container,
        carried);
    const std::uint64_t committed = profileStateFingerprint(profile);
    receipt = executeBaseGroundContainerInventory(
        profile,
        content,
        groundPack,
        access(),
        take,
        CommandContext{revision, "take-ground-medkit"});
    EXPECT_TRUE(receipt.succeeded);
    EXPECT_TRUE(receipt.alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), committed);

    const InventoryCommand putBack = InventoryMoveCommand{
        medkit,
        0,
        StoredAssetLocation{
            ProfileContainerId::compartment(groundPack, 0),
            GridPosition{0, 0}},
        ItemOrientation::Degrees0};
    receipt = executeBaseGroundContainerInventory(
        profile,
        content,
        groundPack,
        access(),
        putBack,
        CommandContext{profile.revision, "return-ground-medkit"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        std::get<StoredAssetLocation>(
            profile.assets.find(medkit)->location).container.ownerAssetId,
        groundPack);
}

TEST(BaseGroundDomainTest, ContainerScopeRejectsRemoteStashAndOtherGroundRoots)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-ground-container-scope", content);
    const ItemDefinition &packDefinition = content.item(
        alpha_content::backpack);
    const AssetInstanceId firstGround = profile.assets.create(
        packDefinition,
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}});
    const AssetInstanceId secondGround = profile.assets.create(
        packDefinition,
        BaseGroundAssetLocation{kGreyline, Vec2{170.0F, 100.0F}});
    const AssetInstanceId foreignChild = profile.assets.create(
        content.item(alpha_content::medkit),
        StoredAssetLocation{
            ProfileContainerId::compartment(secondGround, 0),
            GridPosition{0, 0}});
    const AssetInstanceId stashAsset = firstAsset(
        profile, alpha_content::rifle);
    ASSERT_NE(stashAsset, 0U);
    const std::uint64_t before = profileStateFingerprint(profile);

    BaseGroundAccess far = access();
    far.playerCenter = Vec2{600.0F, 600.0F};
    EXPECT_FALSE(queryBaseGroundContainerAccess(
        profile, content, firstGround, far).canCommit);

    const InventoryCommand foreignMove = InventoryMoveCommand{
        foreignChild,
        0,
        StoredAssetLocation{
            ProfileContainerId::compartment(firstGround, 0),
            GridPosition{1, 0}},
        ItemOrientation::Degrees0};
    EXPECT_FALSE(queryBaseGroundContainerInventory(
        profile, content, firstGround, access(), foreignMove).canCommit);

    const InventoryCommand stashMove = InventoryMoveCommand{
        stashAsset,
        0,
        StoredAssetLocation{
            ProfileContainerId::compartment(firstGround, 0),
            GridPosition{1, 0}},
        ItemOrientation::Degrees0};
    const InventoryReceipt receipt = executeBaseGroundContainerInventory(
        profile,
        content,
        firstGround,
        access(),
        stashMove,
        CommandContext{profile.revision, "illegal-stash-access"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST(BaseGroundDomainTest, ContainerScopeCanQuickEquipVisibleChild)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-ground-container-equip", content);
    const AssetInstanceId groundPack = profile.assets.create(
        content.item(alpha_content::backpack),
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}});
    const AssetInstanceId helmet = profile.assets.create(
        content.item(alpha_content::helmet),
        StoredAssetLocation{
            ProfileContainerId::compartment(groundPack, 0),
            GridPosition{0, 0}});
    const InventoryReceipt receipt = executeBaseGroundContainerInventory(
        profile,
        content,
        groundPack,
        access(),
        InventoryEquipCommand{helmet, EquipmentSlotKind::Helmet},
        CommandContext{profile.revision, "equip-ground-helmet"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(
        std::get<EquippedAssetLocation>(
            profile.assets.find(helmet)->location).slot,
        EquipmentSlotKind::Helmet);
}

TEST(BaseGroundDomainTest,
     PlaceableStorageValidatesParcelRotationAndOverlapWithoutMutation)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-placeable-validation", content);
    const ItemDefinition &definition = content.item(kBaseStorageCrate);
    const auto origin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId crate = profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});

    const BaseGroundCommand legal = DropBaseGroundAssetCommand{
        crate,
        0,
        ItemOrientation::Degrees0,
        placementAccess(Vec2{300.0F, 300.0F})};
    const std::uint64_t beforeQuery = profileStateFingerprint(profile);
    EXPECT_TRUE(queryBaseGround(profile, content, legal).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    const BaseGroundAccess narrowParcel = placementAccess(
        Vec2{300.0F, 300.0F},
        ContentRect{{220.0F, 235.0F}, {160.0F, 130.0F}});
    EXPECT_TRUE(queryBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            crate, 0, ItemOrientation::Degrees0, narrowParcel}).canCommit);
    EXPECT_FALSE(queryBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            crate, 0, ItemOrientation::Degrees90, narrowParcel}).canCommit);

    const BaseGroundAccess blocked = placementAccess(
        Vec2{300.0F, 300.0F},
        ContentRect{{200.0F, 200.0F}, {600.0F, 500.0F}},
        {ContentRect{{360.0F, 250.0F}, {80.0F, 80.0F}}});
    const BaseGroundReceipt blockedReceipt = executeBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            crate, 0, ItemOrientation::Degrees0, blocked},
        CommandContext{profile.revision, "blocked-crate"});
    EXPECT_FALSE(blockedReceipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforeQuery);

    BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        legal,
        CommandContext{profile.revision, "place-crate"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const std::uint64_t placed = profileStateFingerprint(profile);

    const auto secondOrigin = findFirstProfileFit(
        profile,
        content,
        ProfileContainerId::stash(),
        definition,
        ItemOrientation::Degrees0);
    ASSERT_TRUE(secondOrigin.has_value());
    const AssetInstanceId second = profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), *secondOrigin});
    const std::uint64_t beforeOverlap = profileStateFingerprint(profile);
    receipt = executeBaseGround(
        profile,
        content,
        DropBaseGroundAssetCommand{
            second,
            0,
            ItemOrientation::Degrees0,
            placementAccess(Vec2{360.0F, 300.0F})},
        CommandContext{profile.revision, "overlap-crate"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforeOverlap);
    EXPECT_NE(profileStateFingerprint(profile), placed);
}

TEST(BaseGroundDomainTest,
     PlaceableStorageMustBeEmptyAndReturnsPreciselyToStash)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-placeable-pickup", content);
    const AssetInstanceId crate = profile.assets.create(
        content.item(kBaseStorageCrate),
        BaseGroundAssetLocation{kGreyline, Vec2{160.0F, 100.0F}});
    const AssetInstanceId medkit = profile.assets.create(
        content.item(alpha_content::medkit),
        StoredAssetLocation{
            ProfileContainerId::compartment(crate, 0),
            GridPosition{0, 0}});
    ASSERT_TRUE(validateProfileState(profile, content).valid);

    const std::uint64_t before = profileStateFingerprint(profile);
    BaseGroundReceipt receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{crate, access()},
        CommandContext{profile.revision, "pickup-nonempty-crate"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);

    ASSERT_TRUE(profile.assets.erase(medkit));
    BaseGroundAccess remoteWarehouse = access(false);
    const std::uint64_t beforeRemote = profileStateFingerprint(profile);
    receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{crate, remoteWarehouse},
        CommandContext{profile.revision, "pickup-away-from-warehouse"});
    EXPECT_FALSE(receipt.succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforeRemote);

    receipt = executeBaseGround(
        profile,
        content,
        PickupBaseGroundAssetCommand{crate, access()},
        CommandContext{profile.revision, "pickup-empty-crate"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    const auto *stored = std::get_if<StoredAssetLocation>(
        &profile.assets.find(crate)->location);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->container, ProfileContainerId::stash());
}

TEST(BaseGroundDomainTest, PlaceableStorageProjectsOrientedMovementBlocker)
{
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState profile = makeNewAlphaProfile(
        "base-placeable-blocker", content);
    const AssetInstanceId crate = profile.assets.create(
        content.item(kBaseStorageCrate),
        BaseGroundAssetLocation{kGreyline, Vec2{400.0F, 500.0F}});
    profile.assets.findMutable(crate)->orientation =
        ItemOrientation::Degrees90;

    const std::vector<ContentRect> blockers =
        projectBaseGroundMovementBlockers(profile, content, kGreyline);
    ASSERT_EQ(blockers.size(), 1U);
    EXPECT_FLOAT_EQ(blockers.front().position.x, 344.0F);
    EXPECT_FLOAT_EQ(blockers.front().position.y, 420.0F);
    EXPECT_FLOAT_EQ(blockers.front().size.x, 112.0F);
    EXPECT_FLOAT_EQ(blockers.front().size.y, 160.0F);
}
