#include "raid_lifecycle.h"
#include "raid_optics_legacy_fixture.h"
#include "raid_visibility_fixture.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <set>

namespace
{
const auto &content = publishedContentRegistry();
const ItemDefinitionId backpackId{"item.container.backpack_expedition"};

DeployReceipt deploy(ProfileState &profile, const ContentRegistry &registry,
    const MapDefinitionId &map, std::uint64_t seed)
{
    return executeDeploy(profile, registry, {"freight-raid", "freight-result", seed, map},
        {profile.revision, "freight-deploy"});
}

std::size_t sceneContainerCount(const ProfileState &profile)
{
    return std::count_if(profile.assets.records().begin(), profile.assets.records().end(), [](const auto &entry) {
        return std::holds_alternative<RaidSceneContainerAssetLocation>(entry.second.location);
    });
}

std::uint64_t assetFingerprint(const ProfileState &profile)
{
    ProfileState result;
    result.assets = profile.assets;
    return profileStateFingerprint(result);
}

struct FreightFixture
{
    ProfileState profile = makeNewAlphaProfile("freight-contract", content);
    AssetInstanceId bag{}, key{}, box{}, supply{};
    ProfileContainerId carried;
    RaidSceneContainerAccess access;
    std::uint64_t beforeRaid{};

    void setup(bool carryKey)
    {
        bag = profile.assets.create(content.item(backpackId), EquippedAssetLocation{EquipmentSlotKind::Backpack});
        carried = ProfileContainerId::compartment(bag, 0);
        const auto keyContainer = carryKey ? carried : ProfileContainerId::stash();
        const auto &keyDefinition = content.item(kFreightCrateKeyId);
        const auto cell = findFirstProfileFit(profile, content, keyContainer, keyDefinition, ItemOrientation::Degrees0);
        ASSERT_TRUE(cell);
        key = profile.assets.create(keyDefinition, StoredAssetLocation{keyContainer, *cell});
        beforeRaid = assetFingerprint(profile);
        const auto result = deploy(profile, content, kFreightCrateMapId, 9917);
        ASSERT_TRUE(result.succeeded) << result.message;
        ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->sealedFreightCrate);
        box = profile.pendingRaid->sealedFreightCrate->containerAssetId;
        const auto &scene = std::get<RaidSceneContainerAssetLocation>(profile.assets.find(box)->location);
        access = {box, scene.spaceId, scene.position};
        access.observation = observeFrozenRaidInterior(profile, scene.spaceId, scene.position);
        access.currentObservation = access.observation->key();
        const auto items = assetsInContainer(profile, ProfileContainerId::compartment(box, 0));
        ASSERT_EQ(items.size(), 1U);
        supply = items.front()->instanceId;
        EXPECT_EQ(items.front()->definitionId, kFreightCrateSupplyId);
        ASSERT_TRUE(raidSceneContainerObservable(profile, access));
    }
};
}

TEST(RaidFreightDomainTest, RiversideFreightKeyStaysAtFirstSelectedCentralLootAcrossSpawnPairs)
{
    const auto historical = contentBeforeCrossMapFreight();
    const auto &slots = content.map(kFreightKeySourceMapId).raidLootSlots;
    std::set<std::pair<std::string, std::string>> combinations;
    for (const auto seed : std::array{1U, 14U, 113U, 3U, 12U, 357U, 6U, 8U, 152U})
    {
        SCOPED_TRACE(seed);
        auto old = makeNewAlphaProfile("riverside-freight", historical);
        auto current = makeNewAlphaProfile("riverside-freight", content);
        const auto oldResult = deploy(old, historical, kFreightKeySourceMapId, seed);
        ASSERT_TRUE(oldResult.succeeded) << oldResult.message;
        const auto result = deploy(current, content, kFreightKeySourceMapId, seed);
        ASSERT_TRUE(result.succeeded) << result.message;
        ASSERT_TRUE(current.pendingRaid && old.pendingRaid);
        const auto &raid = *current.pendingRaid;
        ASSERT_TRUE(raid.freightKeyLootSlotId);
        EXPECT_FALSE(raid.sealedFreightCrate);
        EXPECT_FALSE(old.pendingRaid->freightKeyLootSlotId);
        EXPECT_EQ(raid.enemies, old.pendingRaid->enemies);
        EXPECT_EQ(raid.spatialLayout, old.pendingRaid->spatialLayout);
        EXPECT_EQ(raid.spawnExtractionPairId, old.pendingRaid->spawnExtractionPairId);
        combinations.emplace(raid.spawnExtractionPairId, *raid.freightKeyLootSlotId);
        auto expected = old.pendingRaid->loot;
        const auto source = std::min_element(expected.begin(), expected.end(), [&](const auto &a, const auto &b) {
            const auto centralOrder = [&](const auto &loot) {
                return !loot.requiresHighRisk && loot.slotIndex < slots.size() && slots[loot.slotIndex].route == "central"
                    ? loot.slotIndex : std::numeric_limits<std::uint32_t>::max();
            };
            return centralOrder(a) < centralOrder(b);
        });
        ASSERT_NE(source, expected.end());
        ASSERT_LT(source->slotIndex, slots.size());
        EXPECT_EQ(slots[source->slotIndex].route, "central");
        EXPECT_EQ(slots[source->slotIndex].id, *raid.freightKeyLootSlotId);
        source->definitionId = kFreightCrateKeyId;
        source->quantity = 1;
        const auto document = std::find_if(expected.begin(), expected.end(), [&](const auto &loot) {
            return !loot.requiresHighRisk && loot.slotIndex < slots.size() &&
                slots[loot.slotIndex].route == "resource";
        });
        ASSERT_NE(document, expected.end());
        document->definitionId = kFieldIntelligenceDocumentId;
        document->quantity = 1;
        EXPECT_EQ(raid.loot, expected);
        EXPECT_TRUE(validateProfileState(old, content).valid);

        current.pendingRaid->freightKeyLootSlotId.reset();
        EXPECT_FALSE(validateProfileState(current, content).valid);
    }
    EXPECT_EQ(combinations.size(), 9U);
}

TEST(RaidFreightDomainTest, FrontierAddsOnlySealedFourthSlotAndPreservesLegacyBoxes)
{
    const auto freightContent = contentBeforeFuelExtraction();
    const auto historical = contentBeforeCrossMapFreight();
    for (const auto seed : {1U, 9917U})
    {
        SCOPED_TRACE(seed);
        auto old = makeNewAlphaProfile("frontier-freight", historical);
        auto current = makeNewAlphaProfile("frontier-freight", freightContent);
        const auto oldResult = deploy(old, historical, kFreightCrateMapId, seed);
        ASSERT_TRUE(oldResult.succeeded) << oldResult.message;
        const auto result = deploy(current, freightContent, kFreightCrateMapId, seed);
        ASSERT_TRUE(result.succeeded) << result.message;
        ASSERT_TRUE(current.pendingRaid && old.pendingRaid);
        ASSERT_TRUE(current.pendingRaid->sealedFreightCrate);
        const auto &raid = *current.pendingRaid;
        const auto &sealed = *raid.sealedFreightCrate;
        EXPECT_FALSE(sealed.unlocked);
        EXPECT_FALSE(raid.freightKeyLootSlotId);
        EXPECT_EQ(raid.spatialLayout, old.pendingRaid->spatialLayout);
        EXPECT_EQ(raid.enemies, old.pendingRaid->enemies);
        EXPECT_EQ(raid.encounterGroups, old.pendingRaid->encounterGroups);
        EXPECT_EQ(sceneContainerCount(old), 2U);
        EXPECT_EQ(sceneContainerCount(current), 3U);
        for (const auto &[id, asset] : old.assets.records())
            if (std::holds_alternative<RaidSceneContainerAssetLocation>(asset.location))
            {
                ASSERT_NE(current.assets.find(id), nullptr);
                EXPECT_EQ(current.assets.find(id)->location, asset.location);
            }

        auto expected = old.pendingRaid->loot;
        const auto source = std::find_if(expected.begin(), expected.end(), [](const auto &loot) {
            return loot.spaceId == kFreightCrateSpaceId && loot.position.x == 1050 && loot.position.y == 240;
        });
        ASSERT_NE(source, expected.end());
        EXPECT_EQ(source->sceneContainerAssetId, 0U);
        source->sceneContainerAssetId = sealed.containerAssetId;
        source->definitionId = kFreightCrateSupplyId;
        source->quantity = 1;
        EXPECT_EQ(raid.loot, expected);
        EXPECT_EQ(raidSceneContainerRoot(current, source->assetId), sealed.containerAssetId);
        EXPECT_TRUE(validateProfileState(old, content).valid);

        current.pendingRaid->sealedFreightCrate.reset();
        EXPECT_FALSE(validateProfileState(current, content).valid);
    }
}

TEST(RaidFreightDomainTest, RiversideRecoveryReturnsTheOriginalKeyWithoutRollingAnother)
{
    auto profile = makeNewAlphaProfile("freight-recovery", content);
    const auto bag = profile.assets.create(content.item(backpackId),
        EquippedAssetLocation{EquipmentSlotKind::Backpack});
    const auto deployment = deploy(profile, content, kFreightKeySourceMapId, 113U);
    ASSERT_TRUE(deployment.succeeded) << deployment.message;
    const auto &loot = profile.pendingRaid->loot;
    const auto found = std::find_if(loot.begin(), loot.end(), [](const auto &entry) {
        return entry.definitionId == kFreightCrateKeyId;
    });
    ASSERT_NE(found, loot.end());
    const auto key = found->assetId;
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, key, {profile.revision, "pick-key"}).succeeded);
    ASSERT_TRUE(raidFreightCrateKeyCarried(profile));
    const auto death = settlePendingRaid(profile, content, "freight-result", RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(death.succeeded) << death.message;
    ASSERT_TRUE(profile.lastRaidResult && profile.lastRaidResult->lostRaidRecordId);
    EXPECT_FALSE(raidFreightCrateKeyCarried(profile));

    const DeployCommand recover{"freight-recovery-raid", "freight-recovery-result",
        113U, kFreightKeySourceMapId, {}, profile.lastRaidResult->lostRaidRecordId};
    const auto recovered = executeDeploy(profile, content, recover, {profile.revision, "deploy-key-recovery"});
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    ASSERT_TRUE(profile.pendingRaid && profile.pendingRaid->selfRecovery);
    EXPECT_FALSE(profile.pendingRaid->freightKeyLootSlotId);
    EXPECT_TRUE(std::none_of(profile.pendingRaid->loot.begin(), profile.pendingRaid->loot.end(), [](const auto &entry) {
        return entry.definitionId == kFreightCrateKeyId;
    }));
    EXPECT_FALSE(raidFreightCrateKeyCarried(profile));
    ASSERT_TRUE(openObservedRaidSelfRecovery(profile, content, {profile.revision, "open-key-cache"}).succeeded);
    ASSERT_TRUE(pickupObservedRaidLoot(profile, content, bag, {profile.revision, "recover-key-bag"}).succeeded);
    ASSERT_NE(profile.assets.find(key), nullptr);
    EXPECT_TRUE(assetIsCarried(profile, key));
    EXPECT_TRUE(raidFreightCrateKeyCarried(profile));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST(RaidFreightDomainTest, LockedContentsRejectTakingAndStashedKey)
{
    FreightFixture f;
    ASSERT_NO_FATAL_FAILURE(f.setup(false));
    const auto &locked = f.profile.pendingRaid->sealedFreightCrate;
    ASSERT_TRUE(locked);
    EXPECT_TRUE(raidSceneContainerLocked(f.profile, f.box));
    EXPECT_FALSE(raidSceneContainerAccessible(f.profile, f.access));
    EXPECT_FALSE(raidFreightCrateKeyCarried(f.profile));
    const auto fingerprint = profileStateFingerprint(f.profile);
    const InventoryCommand take = InventoryMoveCommand{f.supply, 0, {f.carried, {0, 0}}};
    EXPECT_FALSE(queryInventory(f.profile, content, take, &f.access).canCommit);
    EXPECT_FALSE(executeInventory(f.profile, content, take, {f.profile.revision, "locked-take"}, &f.access).succeeded);
    EXPECT_FALSE(pickupObservedRaidLoot(f.profile, content, f.supply, {f.profile.revision, "locked-pickup"}).succeeded);
    EXPECT_EQ(unlockRaidFreightCrate(f.profile, f.access, {f.profile.revision, "stash-key"}).error, DomainErrorCode::MissingAsset);
    EXPECT_EQ(profileStateFingerprint(f.profile), fingerprint);
}

TEST(RaidFreightDomainTest, UnlockChecksCurrentObservationAndConsumesNeitherKeyNorExtraRevision)
{
    FreightFixture f;
    ASSERT_NO_FATAL_FAILURE(f.setup(true));
    ASSERT_TRUE(raidFreightCrateKeyCarried(f.profile));
    const auto fingerprint = profileStateFingerprint(f.profile);
    const InventoryCommand deposit = InventoryMoveCommand{f.key, 0, {ProfileContainerId::compartment(f.box, 0), {2, 0}}};
    EXPECT_FALSE(queryInventory(f.profile, content, deposit, &f.access).canCommit);
    EXPECT_FALSE(executeInventory(f.profile, content, deposit, {f.profile.revision, "locked-deposit"}, &f.access).succeeded);
    const CommandContext unlock{f.profile.revision, "open-sealed"};
    auto stale = f.access;
    ++stale.currentObservation->geometryRevision;
    EXPECT_FALSE(unlockRaidFreightCrate(f.profile, stale, unlock).succeeded);

    const std::array<BallisticBlocker, 1> walls{{{1, {{1020, 80}, {5, 320}}}}};
    const auto blocker = RaidSpaceBlockerIndex::build(Vec2{1120, 520}, walls);
    ASSERT_TRUE(blocker);
    RaidSceneContainerAccess hidden{f.box, f.access.spaceId, {1000, 240}};
    hidden.observation = buildRaidSpaceVisibility(*blocker, hidden.spaceId, hidden.playerCenter);
    hidden.currentObservation = hidden.observation->key();
    EXPECT_FALSE(raidSceneContainerObservable(f.profile, hidden));
    EXPECT_FALSE(unlockRaidFreightCrate(f.profile, hidden, unlock).succeeded);

    RaidSceneContainerAccess far{f.box, f.access.spaceId, {f.access.playerCenter.x - 72.01F, f.access.playerCenter.y}};
    far.observation = observeFrozenRaidInterior(f.profile, far.spaceId, far.playerCenter);
    far.currentObservation = far.observation->key();
    EXPECT_FALSE(unlockRaidFreightCrate(f.profile, far, unlock).succeeded);
    EXPECT_EQ(profileStateFingerprint(f.profile), fingerprint);

    const auto opened = unlockRaidFreightCrate(f.profile, f.access, unlock);
    ASSERT_TRUE(opened.succeeded) << opened.message;
    EXPECT_EQ(opened.revision, unlock.expectedRevision + 1U);
    EXPECT_TRUE(raidSceneContainerAccessible(f.profile, f.access));
    EXPECT_TRUE(raidFreightCrateKeyCarried(f.profile));
    const auto openFingerprint = profileStateFingerprint(f.profile);
    EXPECT_TRUE(unlockRaidFreightCrate(f.profile, f.access, unlock).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(f.profile), openFingerprint);

    const InventoryCommand take = InventoryMoveCommand{f.supply, 0, {f.carried, {1, 0}}};
    ASSERT_TRUE(executeInventory(f.profile, content, take, {f.profile.revision, "take-supply"}, &f.access).succeeded);
    EXPECT_TRUE(assetIsCarried(f.profile, f.supply));
    EXPECT_TRUE(assetIsCarried(f.profile, f.key));
    EXPECT_TRUE(assetsInContainer(f.profile, ProfileContainerId::compartment(f.box, 0)).empty());
    EXPECT_TRUE(raidSceneContainerAccessible(f.profile, f.access));
    EXPECT_TRUE(validateProfileState(f.profile, content).valid);

    auto restored = f.profile;
    const auto rollback = rollbackPendingRaidToBase(restored, content);
    ASSERT_TRUE(rollback.succeeded) << rollback.message;
    EXPECT_EQ(assetFingerprint(restored), f.beforeRaid);
    EXPECT_EQ(sceneContainerCount(restored), 0U);
    EXPECT_FALSE(restored.assets.find(f.supply));
    EXPECT_TRUE(assetIsCarried(restored, f.key));

    for (const auto outcome : {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead})
    {
        auto result = f.profile;
        const auto settled = settlePendingRaid(result, content, "freight-result", outcome);
        ASSERT_TRUE(settled.succeeded) << settled.message;
        EXPECT_EQ(sceneContainerCount(result), 0U);
        EXPECT_TRUE(validateProfileState(result, content).valid);
        ASSERT_NE(result.assets.find(f.key), nullptr);
        ASSERT_NE(result.assets.find(f.supply), nullptr);
        EXPECT_EQ(assetIsCarried(result, f.key), outcome == RaidResultOutcome::Extracted);
        EXPECT_EQ(assetIsCarried(result, f.supply), outcome == RaidResultOutcome::Extracted);
    }
}
