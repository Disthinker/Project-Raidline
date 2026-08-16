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

TEST(ProfileInventoryInteractionTest, RevisionOrLocationChangeInvalidatesSource)
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
    EXPECT_FALSE(profileDragSourceMatches(profile, source));
    profile.revision = 12;
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
