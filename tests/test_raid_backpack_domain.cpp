#include "alpha_content_ids.h"
#include "raid_lifecycle.h"
#include "raid_visibility_fixture.h"
#include "weapon_ammo_domain.h"

#include <algorithm>
#include <array>

#include <gtest/gtest.h>

namespace
{
const auto &content = publishedContentRegistry();
const ItemDefinitionId packId{"item.container.backpack_expedition"};
const MapDefinitionId greyline{"map.v0.test"};

std::uint64_t assetFingerprint(const ProfileState &profile)
{
    ProfileState assetsOnly;
    assetsOnly.assets = profile.assets;
    return profileStateFingerprint(assetsOnly);
}

struct ObservedPosition
{
    RaidVisibilityProjection projection;
    RaidObservationKey key;
    RaidReloadWorldAccess access;

    ObservedPosition(const ProfileState &profile, const RaidSpaceDefinitionId &space, Vec2 point)
        : projection(observeAssetContractPoint(profile, space, point)),
          key(projection.key()),
          access{space, point, &projection, &key,
                 WeaponActionScope{WeaponActionActivity::Raid, {}, profile.pendingRaid->raidId},
                 projection.worldSize}
    {}
};

struct RaidBackpackDomainTest : testing::Test
{
    ProfileState profile = makeNewAlphaProfile("raid-backpack-domain", content);
    AssetInstanceId pack{}, gun{}, magazine{}, fuel{};
    std::uint64_t beforeDeploy{};

    void SetUp() override
    {
        pack = profile.assets.create(content.item(packId),
            EquippedAssetLocation{EquipmentSlotKind::Backpack});
        const auto compartment = ProfileContainerId::compartment(pack, 0);
        const auto &definition = content.item(alpha_content::rifle);
        const auto cell = findFirstProfileFit(profile, content, compartment,
            definition, ItemOrientation::Degrees0);
        ASSERT_TRUE(cell);
        gun = profile.assets.create(definition, StoredAssetLocation{compartment, *cell});
        magazine = profile.assets.create(content.item(alpha_content::magazine),
            InstalledMagazineLocation{gun});
        profile.assets.findMutable(magazine)->magazineRounds.push_back({alpha_content::ammunition});
        profile.assets.findMutable(gun)->chamberedRound = MagazineRoundRecord{alpha_content::ammunition};
        beforeDeploy = assetFingerprint(profile);
    }

    void deploy(const MapDefinitionId &map = kFreightCrateMapId)
    {
        const auto receipt = executeDeploy(profile, content,
            {"pack-raid", "pack-result", 1U, map}, {profile.revision, "pack-deploy"});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
        ASSERT_TRUE(profile.pendingRaid);
        EXPECT_TRUE(profile.pendingRaid->backpackRecoveryEnabled);
        ASSERT_TRUE(profile.pendingRaid->startingAssets);
    }

    Vec2 dropPoint() const
    {
        const auto spawn = profile.pendingRaid->playerSpawn;
        return {spawn.x + 16.0F, spawn.y + 16.0F};
    }

    InventoryReceipt drop(std::string transaction = "pack-drop")
    {
        const auto point = dropPoint();
        const ObservedPosition view(profile, outdoorRaidSpaceId(), point);
        return dropRaidBackpack(profile, content, pack, point,
            {profile.revision, std::move(transaction)}, view.access);
    }
};
}

TEST_F(RaidBackpackDomainTest, DropAndPickupPreserveEveryInstanceAndSuspendFuelEligibility)
{
    ASSERT_NO_FATAL_FAILURE(deploy());
    ASSERT_TRUE(profile.pendingRaid->fuelExtractionLootAssetId);
    fuel = *profile.pendingRaid->fuelExtractionLootAssetId;
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, fuel,
        {profile.revision, "pack-loot-fuel"}).succeeded);
    ASSERT_TRUE(isCarriedRaidFuel(profile, fuel));
    const auto contents = assetFingerprint(profile);
    const auto grams = carriedWeightGrams(profile, content);
    const auto nextId = profile.assets.nextAssetId();
    const auto dropped = drop();
    ASSERT_TRUE(dropped.succeeded) << dropped.message;
    EXPECT_FALSE(dropped.alreadyCommitted);
    EXPECT_FALSE(equippedAsset(profile, EquipmentSlotKind::Backpack));
    EXPECT_EQ(raidDroppedBackpackRoot(profile, fuel), pack);
    EXPECT_EQ(raidDroppedBackpackRoot(profile, magazine), pack);
    EXPECT_FALSE(isCarriedRaidFuel(profile, fuel));
    EXPECT_FALSE(findCarriedRaidFuel(profile));
    EXPECT_EQ(carriedWeightGrams(profile, content), 0U);
    EXPECT_EQ(profile.assets.nextAssetId(), nextId);
    const auto valid = validateProfileState(profile, content);
    EXPECT_TRUE(valid.valid) << valid.message;

    const auto ground = profileStateFingerprint(profile);
    EXPECT_TRUE(drop().alreadyCommitted);
    EXPECT_FALSE(settlePendingRaid(profile, content, "pack-result", RaidResultOutcome::Extracted,
        RaidFuelExtractionCompletion{"pack-raid", fuel}).succeeded);
    EXPECT_FALSE(executeInventory(profile, content, InventoryEquipCommand{pack, EquipmentSlotKind::Backpack},
        {profile.revision, "stale-equip-pack"}).succeeded);
    EXPECT_FALSE(executeInventory(profile, content, InventoryEquipCommand{gun, EquipmentSlotKind::PrimaryWeapon},
        {profile.revision, "stale-equip-weapon"}).succeeded);
    EXPECT_FALSE(queryInventory(profile, content, InventoryMoveCommand{magazine, 0,
        StoredAssetLocation{ProfileContainerId::stash(), {0, 0}}}).canCommit);
    const auto stashed = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) {
            const auto *stored = std::get_if<StoredAssetLocation>(&entry.second.location);
            return entry.second.definitionId == alpha_content::ammunition && stored &&
                stored->container.kind == ProfileContainerKind::Stash;
        });
    ASSERT_NE(stashed, profile.assets.records().end());
    const auto destination = ProfileContainerId::compartment(pack, 0);
    const auto fit = findFirstProfileFit(profile, content, destination,
        content.item(stashed->second.definitionId), ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    EXPECT_FALSE(queryInventory(profile, content, InventoryMoveCommand{stashed->first, 0,
        StoredAssetLocation{destination, *fit}}).canCommit);
    EXPECT_EQ(profileStateFingerprint(profile), ground);

    const ObservedPosition view(profile, outdoorRaidSpaceId(), dropPoint());
    const auto pickup = pickupRaidLoot(profile, content, pack, {profile.revision, "pack-pickup"},
        &view.projection, &view.key, &view.access);
    ASSERT_TRUE(pickup.succeeded) << pickup.message;
    EXPECT_TRUE(pickupRaidBackpack(profile, content, pack,
        {profile.revision, "pack-pickup"}, view.access).alreadyCommitted);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Backpack), pack);
    EXPECT_EQ(assetFingerprint(profile), contents);
    EXPECT_EQ(carriedWeightGrams(profile, content), grams);
    EXPECT_TRUE(isCarriedRaidFuel(profile, fuel));

    const auto paid = settlePendingRaid(profile, content, "pack-result", RaidResultOutcome::Extracted,
        RaidFuelExtractionCompletion{"pack-raid", fuel});
    ASSERT_TRUE(paid.succeeded) << paid.message;
    EXPECT_FALSE(profile.assets.find(fuel));
    EXPECT_TRUE(profile.assets.find(pack));
    EXPECT_TRUE(profile.assets.find(gun));
    EXPECT_TRUE(profile.assets.find(magazine));
    EXPECT_TRUE(profile.lastRaidResult->fuelExtractionPaid);
}

TEST_F(RaidBackpackDomainTest, RejectsWrongProofBlockedPlacementLockedAssetsAndOccupiedSlot)
{
    ASSERT_NO_FATAL_FAILURE(deploy());
    const auto point = dropPoint();
    ObservedPosition view(profile, outdoorRaidSpaceId(), point);
    const auto unchanged = profileStateFingerprint(profile);
    const auto request = CommandContext{profile.revision, "try-drop"};
    const std::array locked{magazine};
    EXPECT_FALSE(dropRaidBackpack(profile, content, pack, point, request, view.access, locked).succeeded);
    EXPECT_FALSE(dropRaidBackpack(profile, content, pack, {0, 0}, request, view.access).succeeded);
    view.access.activity->activityId = "previous-raid";
    EXPECT_FALSE(dropRaidBackpack(profile, content, pack, point, request, view.access).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), unchanged);
    const auto dropped = drop();
    ASSERT_TRUE(dropped.succeeded) << dropped.message;

    const auto beforePickup = profileStateFingerprint(profile);
    const auto pickup = CommandContext{profile.revision, "try-pickup"};
    const auto room = profile.pendingRaid->interiors.front();
    const ObservedPosition differentSpace(profile, room.id, room.interiorSpawn);
    EXPECT_FALSE(pickupRaidBackpack(profile, content, pack, pickup, differentSpace.access).succeeded);
    const ObservedPosition farAway(profile, outdoorRaidSpaceId(), {point.x - 73.0F, point.y});
    EXPECT_FALSE(pickupRaidBackpack(profile, content, pack, pickup, farAway.access).succeeded);
    const ObservedPosition facingAway(profile, outdoorRaidSpaceId(), {point.x + 36.0F, point.y});
    EXPECT_FALSE(pickupRaidBackpack(profile, content, pack, pickup, facingAway.access).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), beforePickup);

    const auto second = profile.assets.create(content.item(alpha_content::backpack),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const ObservedPosition close(profile, outdoorRaidSpaceId(), point);
    const auto occupied = profileStateFingerprint(profile);
    EXPECT_EQ(pickupRaidBackpack(profile, content, pack, pickup, close.access).error, DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(profile), occupied);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Backpack), second);
}

TEST_F(RaidBackpackDomainTest, AbandonedTreeNeverEntersLossAndRollbackRestoresTheExactLoadout)
{
    ASSERT_NO_FATAL_FAILURE(deploy(greyline));
    const auto dropped = drop();
    ASSERT_TRUE(dropped.succeeded) << dropped.message;

    for (const auto outcome : {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead,
             RaidResultOutcome::ActiveQuit})
    {
        auto ended = profile;
        const auto result = settlePendingRaid(ended, content, "pack-result", outcome);
        ASSERT_TRUE(result.succeeded) << result.message;
        EXPECT_FALSE(ended.assets.find(pack));
        EXPECT_FALSE(ended.assets.find(gun));
        EXPECT_FALSE(ended.assets.find(magazine));
        EXPECT_FALSE(ended.pendingRaid);
        EXPECT_TRUE(ended.lostRaidRecords.empty());
        const auto once = profileStateFingerprint(ended);
        EXPECT_TRUE(settlePendingRaid(ended, content, "pack-result", outcome).alreadyCommitted);
        EXPECT_EQ(profileStateFingerprint(ended), once);
    }

    const auto recovered = rollbackPendingRaidToBase(profile, content);
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    EXPECT_EQ(assetFingerprint(profile), beforeDeploy);
    EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Backpack), pack);
}

TEST_F(RaidBackpackDomainTest, ValidatesFrozenCapabilitySpaceAndPreDeployProvenance)
{
    ASSERT_NO_FATAL_FAILURE(deploy(greyline));
    auto legacy = profile;
    legacy.pendingRaid->backpackRecoveryEnabled = false;
    legacy.pendingRaid->startingAssets.reset();
    const auto validation = validateProfileState(legacy, content);
    ASSERT_TRUE(validation.valid) << validation.message;
    const ObservedPosition view(legacy, outdoorRaidSpaceId(), dropPoint());
    EXPECT_FALSE(dropRaidBackpack(legacy, content, pack, dropPoint(),
        {legacy.revision, "legacy-drop"}, view.access).succeeded);

    const auto dropped = drop();
    ASSERT_TRUE(dropped.succeeded) << dropped.message;
    auto forged = profile;
    auto &forgedDrop = std::get<RaidDroppedBackpackLocation>(forged.assets.findMutable(pack)->location);
    forgedDrop.spaceId = RaidSpaceDefinitionId{"raid_space.invalid"};
    EXPECT_FALSE(validateProfileState(forged, content).valid);
    forged = profile;
    std::get<RaidDroppedBackpackLocation>(forged.assets.findMutable(pack)->location).position = {0, 0};
    EXPECT_FALSE(validateProfileState(forged, content).valid);
    forged = profile;
    const auto &dropLocation = std::get<RaidDroppedBackpackLocation>(forged.assets.find(pack)->location);
    static_cast<void>(forged.assets.create(content.item(packId), dropLocation));
    EXPECT_FALSE(validateProfileState(forged, content).valid);
}

TEST(RaidBackpackLootOriginTest, CollectedBackpackCanBeDroppedAndRecoveredWithoutAnInitialRoot)
{
    for (std::uint64_t seed = 1; seed <= 8; ++seed)
    {
        auto profile = makeNewAlphaProfile("found-backpack", content);
        const auto deployed = executeDeploy(profile, content,
            {"found-pack-raid", "found-pack-result", seed, kFreightCrateMapId},
            {profile.revision, "found-pack-deploy"});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        const auto &raid = *profile.pendingRaid;
        const auto found = std::find_if(raid.loot.begin(), raid.loot.end(), [](const auto &loot) {
            return !loot.sceneContainerAssetId && !loot.corpseContainerAssetId &&
                itemCanEquipInSlot(content.item(loot.definitionId), EquipmentSlotKind::Backpack);
        });
        if (found == raid.loot.end())
            continue;
        const auto id = found->assetId;
        EXPECT_FALSE(raid.startingAssets->find(id));
        ASSERT_TRUE(pickupObservedRaidLoot(profile, content, id, {profile.revision, "pick-found-pack"}).succeeded);
        ASSERT_EQ(equippedAsset(profile, EquipmentSlotKind::Backpack), id);
        const Vec2 center{raid.playerSpawn.x + 16.0F, raid.playerSpawn.y + 16.0F};
        const ObservedPosition view(profile, outdoorRaidSpaceId(), center);
        const auto dropped = dropRaidBackpack(profile, content, id, center,
            {profile.revision, "drop-found-pack"}, view.access);
        ASSERT_TRUE(dropped.succeeded) << dropped.message;
        const auto valid = validateProfileState(profile, content);
        EXPECT_TRUE(valid.valid) << valid.message;
        auto forged = profile;
        const auto forgedLoot = std::find_if(forged.pendingRaid->loot.begin(), forged.pendingRaid->loot.end(),
            [id](const auto &loot) { return loot.assetId == id; });
        ASSERT_NE(forgedLoot, forged.pendingRaid->loot.end());
        forgedLoot->collected = false;
        EXPECT_FALSE(validateProfileState(forged, content).valid);
        ASSERT_TRUE(pickupRaidBackpack(profile, content, id, {profile.revision, "retake-found-pack"},
            view.access).succeeded);
        EXPECT_EQ(equippedAsset(profile, EquipmentSlotKind::Backpack), id);
        return;
    }
    FAIL() << "Frontier fixtures must include a frozen ground backpack";
}
