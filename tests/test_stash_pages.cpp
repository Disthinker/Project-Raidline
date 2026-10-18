#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "weapon_ammo_domain.h"

namespace
{
const ContentRegistry &content()
{
    return publishedContentRegistry();
}

ProfileState emptyProfile(std::uint32_t layoutVersion = 1U)
{
    ProfileState profile = makeNewAlphaProfile("stash-pages", content());
    profile.assets = AssetRegistry{};
    profile.stashLayoutVersion = layoutVersion;
    return profile;
}

AssetInstanceId store(
    ProfileState &profile,
    const ItemDefinitionId &definitionId,
    GridPosition origin,
    ProfileContainerId container = ProfileContainerId::stash(),
    std::uint32_t quantity = 1U)
{
    return profile.assets.create(
        content().item(definitionId),
        StoredAssetLocation{container, origin}, quantity);
}

void expectSamePayload(const AssetRecord &actual, const AssetRecord &before)
{
    EXPECT_EQ(actual.instanceId, before.instanceId);
    EXPECT_EQ(actual.definitionId, before.definitionId);
    EXPECT_EQ(actual.quantity, before.quantity);
    EXPECT_EQ(actual.orientation, before.orientation);
    EXPECT_EQ(actual.remainingCharges, before.remainingCharges);
    EXPECT_EQ(actual.currentMaximumDurability, before.currentMaximumDurability);
    EXPECT_EQ(actual.currentDurability, before.currentDurability);
    EXPECT_EQ(actual.reliefBatchId, before.reliefBatchId);
    EXPECT_EQ(actual.magazineRounds, before.magazineRounds);
    EXPECT_EQ(actual.chamberedRound, before.chamberedRound);
    EXPECT_EQ(actual.weaponMalfunction, before.weaponMalfunction);
    EXPECT_EQ(actual.deviceState, before.deviceState);
}

void expectRejectedWithoutMutation(
    ProfileState &profile, const InventoryCommand &command)
{
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const AssetInstanceId highWater = profile.assets.nextAssetId();
    const ProfileRevision revision = profile.revision;
    const auto plan = queryInventory(profile, content(), command);
    EXPECT_FALSE(plan.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    const auto receipt = executeInventory(
        profile, content(), command,
        CommandContext{profile.revision, "page-rejection"});
    EXPECT_FALSE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.succeeded, plan.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_EQ(profile.revision, revision);
    EXPECT_FALSE(profile.committedTransactions.contains("page-rejection"));
}

void expectAmmoRejectedWithoutMutation(
    ProfileState &profile, const WeaponAmmoCommand &command)
{
    const std::uint64_t fingerprint = profileStateFingerprint(profile);
    const AssetInstanceId highWater = profile.assets.nextAssetId();
    const ProfileRevision revision = profile.revision;
    const auto plan = queryWeaponAmmo(profile, content(), command);
    EXPECT_FALSE(plan.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    const auto receipt = executeWeaponAmmo(
        profile, content(), command,
        CommandContext{profile.revision, "ammo-page-rejection"});
    EXPECT_FALSE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(receipt.succeeded, plan.canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_EQ(profile.revision, revision);
    EXPECT_FALSE(profile.committedTransactions.contains("ammo-page-rejection"));
}
}

TEST(StashPagesTest, FootprintsStayInsideEachOfSixPages)
{
    for (int pageY = 0; pageY < 2; ++pageY)
    {
        for (int pageX = 0; pageX < 3; ++pageX)
        {
            const GridPosition origin{pageX * 8, pageY * 8};
            EXPECT_TRUE(stashFootprintFitsPage(origin, InventoryFootprint{8, 8}));
            EXPECT_TRUE(stashFootprintFitsPage(
                GridPosition{origin.x + 7, origin.y + 7},
                InventoryFootprint{1, 1}));
            EXPECT_FALSE(stashFootprintFitsPage(
                GridPosition{origin.x + 7, origin.y},
                InventoryFootprint{2, 1}));
            EXPECT_FALSE(stashFootprintFitsPage(
                GridPosition{origin.x, origin.y + 7},
                InventoryFootprint{1, 2}));
        }
    }
}

TEST(StashPagesTest, FootprintRejectsInvalidDimensionsAndGlobalBounds)
{
    for (const GridPosition origin :
         {GridPosition{-1, 0}, GridPosition{0, -1},
          GridPosition{24, 0}, GridPosition{0, 16},
          GridPosition{std::numeric_limits<int>::max(), 0}})
    {
        EXPECT_FALSE(stashFootprintFitsPage(origin, InventoryFootprint{1, 1}));
    }
    for (const InventoryFootprint footprint :
         {InventoryFootprint{0, 1}, InventoryFootprint{1, 0},
          InventoryFootprint{-1, 1}, InventoryFootprint{1, -1},
          InventoryFootprint{9, 1}, InventoryFootprint{1, 9},
          InventoryFootprint{std::numeric_limits<int>::max(), 1}})
    {
        EXPECT_FALSE(stashFootprintFitsPage(GridPosition{}, footprint));
    }
}

TEST(StashPagesTest, NewProfilesAndPublishedCatalogRespectPageBoundaries)
{
    for (const ProfileState &profile : {
             makeNewAlphaProfile("new-alpha-pages", content()),
             makeNewPublishedProfile("new-published-pages", content())})
    {
        EXPECT_EQ(profile.stashLayoutVersion, 1U);
        ASSERT_TRUE(validateProfileState(profile, content()).valid);
        for (const auto *asset : assetsInContainer(profile, ProfileContainerId::stash()))
        {
            const auto &location = std::get<StoredAssetLocation>(asset->location);
            EXPECT_TRUE(stashFootprintFitsPage(
                location.origin,
                inventoryFootprint(content().item(asset->definitionId), asset->orientation)))
                << asset->instanceId;
        }
        EXPECT_TRUE(assetsInContainer(profile, ProfileContainerId::legacyOverflow()).empty());
    }
}

TEST(StashPagesTest, SharedPlacementRejectsCrossPageFootprintAndChecksRotation)
{
    ProfileState profile = emptyProfile();
    const ItemDefinition &rifle = content().item(alpha_content::rifle);
    EXPECT_FALSE(profilePlacementFits(profile, content(), ProfileContainerId::stash(),
        GridPosition{5, 0}, rifle, ItemOrientation::Degrees0));
    EXPECT_TRUE(profilePlacementFits(profile, content(), ProfileContainerId::stash(),
        GridPosition{5, 0}, rifle, ItemOrientation::Degrees90));
    EXPECT_TRUE(profilePlacementFits(profile, content(), ProfileContainerId::stash(),
        GridPosition{0, 5}, rifle, ItemOrientation::Degrees0));
    EXPECT_FALSE(profilePlacementFits(profile, content(), ProfileContainerId::stash(),
        GridPosition{0, 5}, rifle, ItemOrientation::Degrees90));
}

TEST(StashPagesTest, AutomaticFitFinishesFirstPageBeforeConsideringNextPage)
{
    ProfileState profile = emptyProfile();
    for (int x = 0; x < 8; ++x)
        static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{x, 0}));
    EXPECT_EQ(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
        content().item(alpha_content::ammunition), ItemOrientation::Degrees0),
        (GridPosition{0, 1}));
    for (int y = 1; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{x, y}));
    EXPECT_EQ(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
        content().item(alpha_content::ammunition), ItemOrientation::Degrees0),
        (GridPosition{8, 0}));
}

TEST(StashPagesTest, AutomaticFitVisitsAllSixPagesAndReportsFullWarehouse)
{
    ProfileState profile = emptyProfile();
    const ItemDefinition &ammunition = content().item(alpha_content::ammunition);
    for (int page = 0; page < 6; ++page)
    {
        const GridPosition pageOrigin{(page % 3) * 8, (page / 3) * 8};
        EXPECT_EQ(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
            ammunition, ItemOrientation::Degrees0), pageOrigin);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                static_cast<void>(store(profile, alpha_content::ammunition,
                    GridPosition{pageOrigin.x + x, pageOrigin.y + y}));
    }
    EXPECT_EQ(profile.assets.records().size(), 384U);
    EXPECT_FALSE(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
        ammunition, ItemOrientation::Degrees0).has_value());
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, AdjacentFreeCellsAcrossPagesCannotFitOneItem)
{
    ProfileState profile = emptyProfile();
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 24; ++x)
            if (y != 0 || (x != 7 && x != 8))
                static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{x, y}));
    EXPECT_FALSE(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
        content().item(alpha_content::pistol), ItemOrientation::Degrees0).has_value());
    EXPECT_EQ(findFirstProfileFit(profile, content(), ProfileContainerId::stash(),
        content().item(alpha_content::ammunition), ItemOrientation::Degrees0),
        (GridPosition{7, 0}));
}

TEST(StashPagesTest, CrossPageMoveAndRotationRejectWithoutAnyMutation)
{
    ProfileState profile = emptyProfile();
    const auto rifle = store(profile, alpha_content::rifle, GridPosition{});
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{rifle, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{7, 0}},
        ItemOrientation::Degrees0});
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{rifle, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{0, 5}},
        ItemOrientation::Degrees90});
}

TEST(StashPagesTest, WholeItemCanMoveToEveryOtherPage)
{
    ProfileState profile = emptyProfile();
    const auto rifle = store(profile, alpha_content::rifle, GridPosition{});
    const auto highWater = profile.assets.nextAssetId();
    for (int page = 1; page < 6; ++page)
    {
        const GridPosition target{(page % 3) * 8 + 4, (page / 3) * 8 + 6};
        const ProfileRevision beforeRevision = profile.revision;
        const auto receipt = executeInventory(profile, content(), InventoryMoveCommand{
            rifle, 0, StoredAssetLocation{ProfileContainerId::stash(), target},
            ItemOrientation::Degrees0}, CommandContext{profile.revision, "move-page-" + std::to_string(page)});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(rifle)->location).origin, target);
        EXPECT_EQ(profile.assets.nextAssetId(), highWater);
        EXPECT_EQ(profile.revision, beforeRevision + 1U);
        EXPECT_EQ(profile.assets.records().size(), 1U);
    }
}

TEST(StashPagesTest, SwapRejectsWhenOnlyReturnedParticipantWouldCrossPage)
{
    ProfileState profile = emptyProfile();
    const auto pistol = store(profile, alpha_content::pistol, GridPosition{6, 0});
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{0, 8}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{pistol, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{0, 8}},
        ItemOrientation::Degrees0});
}

TEST(StashPagesTest, EquipmentSwapRejectsCrossPageReturnOfLargerWeapon)
{
    ProfileState profile = emptyProfile();
    const auto carbine = store(profile, ItemDefinitionId{"item.weapon.carbine_5_45_compact"}, GridPosition{5, 0});
    static_cast<void>(profile.assets.create(content().item(alpha_content::rifle),
        EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile,
        InventoryEquipCommand{carbine, EquipmentSlotKind::PrimaryWeapon});
}

TEST(StashPagesTest, BaseIntakeAndLargeContainerKeepTheirOriginalDimensions)
{
    ProfileState profile = emptyProfile();
    const auto &rifle = content().item(alpha_content::rifle);
    EXPECT_EQ(profileContainerSize(profile, content(), ProfileContainerId::baseIntake()),
        (InventoryGridSize{20, 12}));
    EXPECT_TRUE(profilePlacementFits(profile, content(), ProfileContainerId::baseIntake(),
        GridPosition{7, 7}, rifle, ItemOrientation::Degrees0));
    const auto largeBag = profile.assets.create(content().item(ItemDefinitionId{"item.container.backpack_expedition"}),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto container = ProfileContainerId::compartment(largeBag, 0);
    const auto size = profileContainerSize(profile, content(), container);
    EXPECT_GT(size.width, 8);
    EXPECT_TRUE(profilePlacementFits(profile, content(), container,
        GridPosition{7, 0}, content().item(alpha_content::pistol), ItemOrientation::Degrees0));
}

TEST(StashPagesTest, LegacyLayoutAllowsOldCrossPagePositionUntilExplicitMigration)
{
    ProfileState profile = emptyProfile(0U);
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{7, 7}));
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
    profile.stashLayoutVersion = 1U;
    EXPECT_FALSE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, MigrationLeavesAlreadyContainedItemsAtOriginalCoordinates)
{
    ProfileState profile = emptyProfile(0U);
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{4, 6}));
    static_cast<void>(store(profile, alpha_content::pistol, GridPosition{22, 15}));
    const auto before = profile.assets.records();
    const auto revision = profile.revision;
    const auto highWater = profile.assets.nextAssetId();
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    EXPECT_EQ(profile.stashLayoutVersion, 1U);
    EXPECT_EQ(profile.revision, revision + 1U);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    for (const auto &[id, asset] : before)
    {
        ASSERT_NE(profile.assets.find(id), nullptr);
        expectSamePayload(*profile.assets.find(id), asset);
        EXPECT_EQ(profile.assets.find(id)->location, asset.location);
    }
    EXPECT_TRUE(assetsInContainer(profile, ProfileContainerId::legacyOverflow()).empty());
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, MigrationUsesStableIdOrderAndPrefersOriginalPage)
{
    ProfileState profile = emptyProfile(0U);
    const auto first = store(profile, alpha_content::rifle, GridPosition{7, 0});
    const auto second = store(profile, alpha_content::rifle, GridPosition{15, 0});
    const auto vertical = store(profile, alpha_content::rifle, GridPosition{16, 7});
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(first)->location).origin, (GridPosition{0, 0}));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(second)->location).origin, (GridPosition{8, 0}));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(vertical)->location).origin, (GridPosition{16, 0}));
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, MigrationSearchesOtherPagesWhenOriginalPageCannotFit)
{
    ProfileState profile = emptyProfile(0U);
    const auto rifle = store(profile, alpha_content::rifle, GridPosition{7, 0});
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (x != 7 || y > 1)
                static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{x, y}));
    const auto before = profile.assets.records();
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(rifle)->location).origin, (GridPosition{8, 0}));
    for (const auto &[id, asset] : before)
        if (id != rifle)
            EXPECT_EQ(profile.assets.find(id)->location, asset.location);
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, MigrationAssignsSharedFreeSpaceInStableAssetIdOrder)
{
    ProfileState profile = emptyProfile(0U);
    // The first stable ID has a later original row. Position ordering must not
    // override identity ordering when both roots compete for the same page.
    const auto first = store(profile, alpha_content::rifle, GridPosition{7, 4});
    const auto second = store(profile, alpha_content::rifle, GridPosition{7, 0});
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(first)->location).origin,
        (GridPosition{0, 0}));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(second)->location).origin,
        (GridPosition{4, 0}));
}

TEST(StashPagesTest, FullLegacyFragmentationPreservesEveryItemInWithdrawOnlyOverflow)
{
    ProfileState profile = emptyProfile(0U);
    std::set<AssetInstanceId> rifles;
    for (int y = 0; y < 16; y += 2)
        rifles.insert(store(profile, alpha_content::rifle, GridPosition{7, y}));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 24; ++x)
            if (x < 7 || x > 10)
                static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{x, y}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto before = profile.assets.records();
    const auto highWater = profile.assets.nextAssetId();
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    EXPECT_EQ(profile.assets.records().size(), before.size());
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    const auto overflow = assetsInContainer(profile, ProfileContainerId::legacyOverflow());
    ASSERT_EQ(overflow.size(), rifles.size());
    for (const auto &[id, asset] : before)
    {
        const auto *actual = profile.assets.find(id);
        ASSERT_NE(actual, nullptr);
        expectSamePayload(*actual, asset);
        const auto &oldLocation = std::get<StoredAssetLocation>(asset.location);
        const auto &newLocation = std::get<StoredAssetLocation>(actual->location);
        EXPECT_EQ(newLocation.origin, oldLocation.origin);
        EXPECT_EQ(newLocation.container, rifles.contains(id)
            ? ProfileContainerId::legacyOverflow() : ProfileContainerId::stash());
    }
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
    const auto fingerprint = profileStateFingerprint(profile);
    const auto revision = profile.revision;
    ASSERT_TRUE(migrateLegacyStashPages(profile, content()).valid);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.revision, revision);
}

TEST(StashPagesTest, MigrationPreservesWeaponAndContainerOwnershipSubtreesAndAllState)
{
    ProfileState profile = emptyProfile(0U);
    const ItemDefinitionId serviceRifle{"item.weapon.rifle_5_45_service"};
    const ItemDefinitionId rifleAmmo{"item.ammunition.5_45x39_standard"};
    const auto rifle = store(profile, serviceRifle, GridPosition{7, 0});
    const auto backpack = store(profile, alpha_content::backpack, GridPosition{15, 4});
    const auto medkit = store(profile, alpha_content::medkit, GridPosition{}, ProfileContainerId::compartment(backpack, 0));
    profile.assets.findMutable(medkit)->remainingCharges = 2U;
    const auto magazine = profile.assets.create(content().item(ItemDefinitionId{"item.magazine.5_45x39_30"}),
        InstalledMagazineLocation{rifle});
    profile.assets.findMutable(magazine)->magazineRounds = {
        MagazineRoundRecord{rifleAmmo, "old-relief"}, MagazineRoundRecord{rifleAmmo, std::nullopt}};
    auto *weapon = profile.assets.findMutable(rifle);
    weapon->currentMaximumDurability = 9000U;
    weapon->currentDurability = 7600U;
    weapon->chamberedRound = MagazineRoundRecord{rifleAmmo, std::nullopt};
    weapon->weaponMalfunction = WeaponMalfunctionType::Stovepipe;
    static_cast<void>(profile.assets.create(content().item(ItemDefinitionId{"item.component.precision_barrel"}),
        InstalledWeaponComponentLocation{rifle}));
    static_cast<void>(profile.assets.create(content().item(ItemDefinitionId{"item.component.rail_handguard"}),
        InstalledWeaponComponentLocation{rifle}));
    const auto light = profile.assets.create(content().item(ItemDefinitionId{"item.component.weapon_flashlight_basic"}),
        InstalledWeaponComponentLocation{rifle});
    const auto battery = profile.assets.create(content().item(ItemDefinitionId{"item.battery.universal"}),
        InstalledDeviceBatteryLocation{light});
    profile.assets.findMutable(battery)->remainingCharges = 1234U;
    profile.assets.findMutable(light)->deviceState = TacticalDeviceState{true, false, 123U, 4U, 0.25};
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto before = profile.assets.records();
    const auto highWater = profile.assets.nextAssetId();
    const auto result = migrateLegacyStashPages(profile, content());
    ASSERT_TRUE(result.valid) << result.message;
    ASSERT_EQ(profile.assets.records().size(), before.size());
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    for (const auto &[id, asset] : before)
    {
        const auto *actual = profile.assets.find(id);
        ASSERT_NE(actual, nullptr);
        expectSamePayload(*actual, asset);
        if (id != rifle && id != backpack)
            EXPECT_EQ(actual->location, asset.location);
    }
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, FailedLegacyMigrationIsAtomicAndDoesNotAdvanceHighWater)
{
    ProfileState profile = emptyProfile(0U);
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{7, 0}));
    static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{7, 0}));
    const auto fingerprint = profileStateFingerprint(profile);
    const auto revision = profile.revision;
    const auto highWater = profile.assets.nextAssetId();
    const auto result = migrateLegacyStashPages(profile, content());
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(profile.stashLayoutVersion, 0U);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.revision, revision);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
}

TEST(StashPagesTest, LegacyMigrationRejectsRevisionOverflowWithoutMutation)
{
    ProfileState profile = emptyProfile(0U);
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{7, 0}));
    profile.revision = std::numeric_limits<ProfileRevision>::max();
    const auto fingerprint = profileStateFingerprint(profile);
    const auto result = migrateLegacyStashPages(profile, content());
    EXPECT_FALSE(result.valid);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(profile.stashLayoutVersion, 0U);
}

TEST(StashPagesTest, OverflowIsNotAnAutomaticFitDestination)
{
    ProfileState profile = emptyProfile();
    static_cast<void>(store(profile, alpha_content::pistol, GridPosition{}, ProfileContainerId::legacyOverflow()));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    EXPECT_FALSE(findFirstProfileFit(profile, content(), ProfileContainerId::legacyOverflow(),
        content().item(alpha_content::pistol), ItemOrientation::Degrees0).has_value());
}

TEST(StashPagesTest, OverflowRejectsNewItemsRepositionAndStackMerge)
{
    ProfileState profile = emptyProfile();
    const auto incoming = store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::stash(), 3U);
    const auto waiting = store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::legacyOverflow(), 2U);
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{incoming, 0,
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), GridPosition{1, 0}}, ItemOrientation::Degrees0});
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{incoming, 1,
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), GridPosition{}}, ItemOrientation::Degrees0});
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{waiting, 0,
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), GridPosition{1, 0}}, ItemOrientation::Degrees0});
}

TEST(StashPagesTest, OverflowWithdrawalCannotSwapAnItemBackIntoWaitingArea)
{
    ProfileState profile = emptyProfile();
    const auto waiting = store(profile, alpha_content::pistol, GridPosition{}, ProfileContainerId::legacyOverflow());
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{waiting, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{}}, ItemOrientation::Degrees0});
}

TEST(StashPagesTest, OverflowContainerCannotReceiveNewChildren)
{
    ProfileState profile = emptyProfile();
    const auto waitingBag = store(profile, alpha_content::backpack, GridPosition{}, ProfileContainerId::legacyOverflow());
    const auto ammunition = store(profile, alpha_content::ammunition, GridPosition{});
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{ammunition, 0,
        StoredAssetLocation{ProfileContainerId::compartment(waitingBag, 0), GridPosition{}},
        ItemOrientation::Degrees0});
}

TEST(StashPagesTest, OverflowContainerChildCanBeWithdrawnWithoutMovingParent)
{
    ProfileState profile = emptyProfile();
    const auto waitingBag = store(profile, alpha_content::backpack, GridPosition{}, ProfileContainerId::legacyOverflow());
    const auto medkit = store(profile, alpha_content::medkit, GridPosition{}, ProfileContainerId::compartment(waitingBag, 0));
    const auto parentLocation = profile.assets.find(waitingBag)->location;
    const auto highWater = profile.assets.nextAssetId();
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    const auto receipt = executeInventory(profile, content(), InventoryMoveCommand{medkit, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{}}, ItemOrientation::Degrees0},
        CommandContext{profile.revision, "withdraw-child"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(waitingBag)->location, parentLocation);
    EXPECT_EQ(profile.assets.find(medkit)->location,
        (AssetLocation{StoredAssetLocation{ProfileContainerId::stash(), GridPosition{}}}));
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, OverflowContainerChildCannotSwapNewAssetBackIntoItsPocket)
{
    ProfileState profile = emptyProfile();
    const auto waitingBag = store(profile, alpha_content::backpack, GridPosition{}, ProfileContainerId::legacyOverflow());
    const auto waitingPistol = store(profile, alpha_content::pistol, GridPosition{}, ProfileContainerId::compartment(waitingBag, 0));
    static_cast<void>(store(profile, alpha_content::rifle, GridPosition{}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    // Both participants would fit geometrically. The rejection must come
    // from the waiting area's one-way ownership rule, including descendants.
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{waitingPistol, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{}},
        ItemOrientation::Degrees0});
}

TEST(StashPagesTest, OverflowContainerCannotReceiveNewQuantityByStackMerge)
{
    ProfileState profile = emptyProfile();
    const auto waitingBag = store(profile, alpha_content::backpack, GridPosition{}, ProfileContainerId::legacyOverflow());
    static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{},
        ProfileContainerId::compartment(waitingBag, 0), 2U));
    const auto incoming = store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::stash(), 5U);
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile, InventoryMoveCommand{incoming, 1U,
        StoredAssetLocation{ProfileContainerId::compartment(waitingBag, 0), GridPosition{}},
        ItemOrientation::Degrees0});
}

TEST(StashPagesTest, OverflowEquipmentSwapCannotReturnNewEquipmentIntoWaitingArea)
{
    ProfileState profile = emptyProfile();
    const auto waiting = store(profile, alpha_content::rifle, GridPosition{}, ProfileContainerId::legacyOverflow());
    static_cast<void>(profile.assets.create(content().item(alpha_content::rifle),
        EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectRejectedWithoutMutation(profile,
        InventoryEquipCommand{waiting, EquipmentSlotKind::PrimaryWeapon});
}

TEST(StashPagesTest, OverflowCanBeWithdrawnAndMergedIntoExistingStashStack)
{
    ProfileState profile = emptyProfile();
    const auto waiting = store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::legacyOverflow(), 2U);
    const auto target = store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::stash(), 3U);
    const auto highWater = profile.assets.nextAssetId();
    const auto receipt = executeInventory(profile, content(), InventoryMoveCommand{waiting, 0,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{}}, ItemOrientation::Degrees0},
        CommandContext{profile.revision, "withdraw-and-merge"});
    ASSERT_TRUE(receipt.succeeded) << receipt.message;
    EXPECT_EQ(profile.assets.find(waiting), nullptr);
    ASSERT_NE(profile.assets.find(target), nullptr);
    EXPECT_EQ(profile.assets.find(target)->quantity, 5U);
    EXPECT_EQ(profile.assets.nextAssetId(), highWater);
    EXPECT_TRUE(assetsInContainer(profile, ProfileContainerId::legacyOverflow()).empty());
    EXPECT_TRUE(validateProfileState(profile, content()).valid);
}

TEST(StashPagesTest, AmmoUnloadCannotBypassOverflowRestrictionByMergingExistingStack)
{
    ProfileState profile = emptyProfile();
    const auto magazine = store(profile, alpha_content::magazine, GridPosition{});
    profile.assets.findMutable(magazine)->magazineRounds = {
        MagazineRoundRecord{alpha_content::ammunition, std::nullopt}};
    static_cast<void>(store(profile, alpha_content::ammunition, GridPosition{}, ProfileContainerId::legacyOverflow(), 2U));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectAmmoRejectedWithoutMutation(profile,
        UnloadMagazineCommand{magazine, ProfileContainerId::legacyOverflow()});
}

TEST(StashPagesTest, MagazineUninstallRejectsOverflowAndCrossPageDestinations)
{
    ProfileState profile = emptyProfile();
    const auto rifle = profile.assets.create(content().item(alpha_content::rifle),
        EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
    static_cast<void>(profile.assets.create(content().item(alpha_content::magazine), InstalledMagazineLocation{rifle}));
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectAmmoRejectedWithoutMutation(profile, UninstallMagazineCommand{rifle,
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), GridPosition{}}, ItemOrientation::Degrees0});
    expectAmmoRejectedWithoutMutation(profile, UninstallMagazineCommand{rifle,
        StoredAssetLocation{ProfileContainerId::stash(), GridPosition{0, 7}}, ItemOrientation::Degrees0});
}

TEST(StashPagesTest, MagazineReplacementCannotSwapInstalledMagazineIntoOverflow)
{
    ProfileState profile = emptyProfile();
    const auto rifle = profile.assets.create(content().item(alpha_content::rifle),
        EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
    static_cast<void>(profile.assets.create(content().item(alpha_content::magazine), InstalledMagazineLocation{rifle}));
    const auto waiting = store(profile, alpha_content::magazine, GridPosition{}, ProfileContainerId::legacyOverflow());
    ASSERT_TRUE(validateProfileState(profile, content()).valid);
    expectAmmoRejectedWithoutMutation(profile, InstallMagazineCommand{rifle, waiting});
}
