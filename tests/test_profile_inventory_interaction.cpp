#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "profile_inventory_interaction.h"

namespace
{
InventoryPointerItemGeometry geometry()
{
    return InventoryPointerItemGeometry{
        ItemOrientation::Degrees0,
        InventoryFootprint{2, 1},
        true,
        MousePosition{1.25F, 0.25F}};
}

AssetInstanceId findDefinition(
    const ProfileState &profile,
    ItemDefinitionId definitionId)
{
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == definitionId)
        {
            return id;
        }
    }
    return 0;
}
}

TEST(ProfileInventoryInteractionTest, ClickWithoutDragDoesNotCreateRequest)
{
    ProfileInventoryInteractionState state;
    const ProfileDragSource source{
        7,
        3,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{2, 3}},
        0,
        ItemOrientation::Degrees0};
    ASSERT_TRUE(state.beginPointerPress(
        source, GridPosition{2, 3}, GridPosition{3, 3}, MousePosition{100, 100}, geometry()));

    EXPECT_FALSE(state.releasePointer(
        MousePosition{102, 101},
        StoredCellTarget{StoredAssetLocation{
            ProfileContainerId::stash(), GridPosition{4, 4}}}).has_value());
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Idle);
}

TEST(ProfileInventoryInteractionTest, DragPreservesLockedQuantityAndExactTarget)
{
    ProfileInventoryInteractionState state;
    ProfileDragSource source{
        9,
        4,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{2, 3}},
        4,
        ItemOrientation::Degrees0};
    ASSERT_TRUE(state.beginPointerPress(
        source, GridPosition{2, 3}, GridPosition{3, 3}, MousePosition{100, 100}, geometry()));
    EXPECT_EQ(state.pointerPhase(), InventoryPointerPhase::Pressed);
    state.updatePointerPosition(
        MousePosition{105, 100},
        StoredCellTarget{StoredAssetLocation{
            ProfileContainerId::compartment(20, 0), GridPosition{1, 2}}});

    const auto request = state.releasePointer(
        MousePosition{105, 100}, state.hoveredTarget());
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->source.quantity, 4U);
    EXPECT_EQ(
        std::get<StoredCellTarget>(request->target).location,
        (StoredAssetLocation{ProfileContainerId::compartment(20, 0), GridPosition{1, 2}}));
}

TEST(ProfileInventoryInteractionTest, RotationChangesGhostAndRequest)
{
    ProfileInventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        ProfileDragSource{
            11,
            5,
            EquippedAssetLocation{EquipmentSlotKind::Backpack},
            0,
            ItemOrientation::Degrees0},
        GridPosition{}, GridPosition{}, MousePosition{50, 50}, geometry()));
    state.updatePointerPosition(
        MousePosition{54, 50},
        EquipmentSlotTarget{EquipmentSlotKind::Backpack});
    ASSERT_TRUE(state.rotatePointerItemClockwise());

    const auto visual = state.activeDragVisual();
    ASSERT_TRUE(visual.has_value());
    EXPECT_EQ(visual->orientation, ItemOrientation::Degrees90);
    const auto request = state.releasePointer(MousePosition{54, 50}, state.hoveredTarget());
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->source.orientation, ItemOrientation::Degrees90);
}

TEST(ProfileInventoryInteractionTest, CancelAndInvalidReleaseAreZeroIntent)
{
    ProfileInventoryInteractionState state;
    ASSERT_TRUE(state.beginPointerPress(
        ProfileDragSource{
            13,
            6,
            InstalledMagazineLocation{8},
            0,
            ItemOrientation::Degrees0},
        GridPosition{}, GridPosition{}, MousePosition{10, 10}, geometry()));
    state.updatePointerPosition(MousePosition{14, 10}, std::nullopt);
    EXPECT_FALSE(state.releasePointer(MousePosition{14, 10}, std::nullopt).has_value());
    EXPECT_FALSE(state.source().has_value());
}

TEST(ProfileInventoryInteractionTest, UnrelatedRevisionCanRequeryButLocationChangeInvalidatesSource)
{
    ProfileState profile;
    profile.revision = 12;
    const ItemDefinition definition{
        alpha_content::ammunition,
        ItemId::Ammo9mm,
        "Ammo",
        ItemCategory::Ammunition,
        1,
        1,
        false,
        60};
    const AssetInstanceId id = profile.assets.create(
        definition,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{1, 1}},
        10);
    const ProfileDragSource source{
        id,
        12,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{1, 1}},
        0,
        ItemOrientation::Degrees0};
    EXPECT_TRUE(profileDragSourceMatches(profile, source));

    ++profile.revision;
    EXPECT_TRUE(profileDragSourceMatches(profile, source));
    profile.assets.findMutable(id)->location = StoredAssetLocation{
        ProfileContainerId::stash(), GridPosition{2, 1}};
    EXPECT_FALSE(profileDragSourceMatches(profile, source));
}

TEST(ProfileInventoryInteractionTest, MagazineUnloadActionRemainsDiscoverable)
{
    ProfileState profile = makeNewAlphaProfile(
        "context-empty-magazine",
        publishedContentRegistry());
    AssetInstanceId magazine{};
    for (const auto &[id, asset] : profile.assets.records())
    {
        if (asset.definitionId == alpha_content::magazine)
        {
            magazine = id;
            break;
        }
    }
    ASSERT_NE(magazine, 0U);
    EXPECT_EQ(
        queryProfileContextAction(
            profile,
            publishedContentRegistry(),
            magazine,
            false),
        ProfileContextActionKind::UnloadMagazine);
    EXPECT_EQ(
        queryProfileContextAction(
            profile,
            publishedContentRegistry(),
            magazine,
            true),
        std::nullopt);

    const auto backpack = equippedAsset(
        profile, EquipmentSlotKind::Backpack);
    ASSERT_FALSE(backpack.has_value());
    const auto backpacks = [&profile]
    {
        std::vector<AssetInstanceId> result;
        for (const auto &[id, asset] : profile.assets.records())
        {
            if (asset.definitionId == alpha_content::backpack)
            {
                result.push_back(id);
            }
        }
        return result;
    }();
    ASSERT_FALSE(backpacks.empty());
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            backpacks.front(), EquipmentSlotKind::Backpack},
        CommandContext{profile.revision, "context-equip-pack"}).succeeded);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryMoveCommand{
            magazine,
            0,
            StoredAssetLocation{
                ProfileContainerId::compartment(backpacks.front(), 0),
                GridPosition{0, 0}},
            ItemOrientation::Degrees0},
        CommandContext{profile.revision, "context-carry-magazine"}).succeeded);
    EXPECT_EQ(
        queryProfileContextAction(
            profile,
            publishedContentRegistry(),
            magazine,
            true),
        ProfileContextActionKind::UnloadMagazine);
}

TEST(ProfileInventoryInteractionTest, QuickTransferEquipsOnlyIntoEmptyCompatibleSlot)
{
    ProfileState profile = makeNewAlphaProfile(
        "quick-equip-profile",
        publishedContentRegistry());
    const AssetInstanceId rifle = findDefinition(
        profile, alpha_content::rifle);
    const AssetInstanceId ammunition = findDefinition(
        profile, alpha_content::ammunition);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(ammunition, 0U);
    const std::uint64_t before = profileStateFingerprint(profile);

    EXPECT_EQ(
        queryProfileQuickEquipTarget(
            profile,
            publishedContentRegistry(),
            rifle),
        (std::optional<EquipmentSlotTarget>{
            EquipmentSlotTarget{EquipmentSlotKind::PrimaryWeapon}}));
    EXPECT_EQ(profileStateFingerprint(profile), before);
    EXPECT_EQ(
        queryProfileQuickEquipTarget(
            profile,
            publishedContentRegistry(),
            ammunition),
        std::nullopt);

    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            rifle,
            EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "quick-equip-rifle"}).succeeded);
    EXPECT_EQ(
        queryProfileQuickEquipTarget(
            profile,
            publishedContentRegistry(),
            rifle),
        std::nullopt);
}

TEST(ProfileInventoryInteractionTest, QuickEquipUsesSecondLongGunThenSidearmSlot)
{
    ProfileState profile = makeNewAlphaProfile(
        "quick-equip-multi-weapon",
        publishedContentRegistry());
    const AssetInstanceId firstRifle = findDefinition(
        profile, alpha_content::rifle);
    const AssetInstanceId pistol = findDefinition(
        profile, alpha_content::pistol);
    ASSERT_NE(firstRifle, 0U);
    ASSERT_NE(pistol, 0U);
    ASSERT_TRUE(executeInventory(
        profile,
        publishedContentRegistry(),
        InventoryEquipCommand{
            firstRifle, EquipmentSlotKind::PrimaryWeapon},
        CommandContext{profile.revision, "quick-equip-first-rifle"})
        .succeeded);

    const ItemDefinition &rifleDefinition = publishedContentRegistry().item(
        alpha_content::rifle);
    const auto origin = findFirstProfileFit(
        profile,
        publishedContentRegistry(),
        ProfileContainerId::stash(),
        rifleDefinition,
        ItemOrientation::Degrees0,
        std::nullopt);
    ASSERT_TRUE(origin.has_value());
    const AssetInstanceId secondRifle = profile.assets.create(
        rifleDefinition,
        StoredAssetLocation{ProfileContainerId::stash(), *origin});

    EXPECT_EQ(
        queryProfileQuickEquipTarget(
            profile, publishedContentRegistry(), secondRifle),
        (std::optional<EquipmentSlotTarget>{
            EquipmentSlotTarget{EquipmentSlotKind::SecondaryWeapon}}));
    EXPECT_EQ(
        queryProfileQuickEquipTarget(
            profile, publishedContentRegistry(), pistol),
        (std::optional<EquipmentSlotTarget>{
            EquipmentSlotTarget{EquipmentSlotKind::Sidearm}}));
}
