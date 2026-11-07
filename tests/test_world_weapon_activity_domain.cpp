#include <gtest/gtest.h>

#include "alpha_content_ids.h"
#include "base_corpse_domain.h"
#include "base_ground_domain.h"
#include "home_perimeter_domain.h"
#include "raid_visibility_fixture.h"

namespace
{
AssetInstanceId worldFirst(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records()) if (asset.definitionId == definition) return id;
    return 0;
}

class WorldWeaponActivityDomainTest : public testing::Test
{
protected:
    const ContentRegistry &content{publishedContentRegistry()};
    ProfileState profile;
    AssetInstanceId weapon{}, fresh{}, old{}, rig{};
    WeaponActionScope scope;
    RaidReloadPlan plan;
    RaidReloadWorldAccess access;

    void SetUp() override
    {
        profile = makeNewAlphaProfile("world-weapon-activity", content);
        weapon = worldFirst(profile, alpha_content::rifle);
        old = worldFirst(profile, alpha_content::magazine);
        rig = worldFirst(profile, alpha_content::chestRig);
        const auto pack = worldFirst(profile, alpha_content::backpack);
        ASSERT_NE(weapon, 0U); ASSERT_NE(old, 0U); ASSERT_NE(rig, 0U);
        profile.assets.findMutable(weapon)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
        profile.assets.findMutable(rig)->location = EquippedAssetLocation{EquipmentSlotKind::ChestRig};
        profile.assets.findMutable(pack)->location = EquippedAssetLocation{EquipmentSlotKind::Backpack};
        profile.assets.findMutable(old)->location = InstalledMagazineLocation{weapon};
        profile.assets.findMutable(old)->magazineRounds.assign(3, {alpha_content::ammunition, "old"});
        fresh = profile.assets.create(content.item(alpha_content::magazine),
            StoredAssetLocation{ProfileContainerId::compartment(rig, 0), {0, 0}});
        profile.assets.findMutable(fresh)->magazineRounds = {{alpha_content::ammunition, "first"}, {alpha_content::ammunition, "second"}};
        const auto site = profile.homeFounding.established ? profile.regionalOperations.technologyCore.baseSiteDefinitionId : kFoundingRegion;
        scope = {WeaponActionActivity::BaseDaily, site, std::string{site.value()}};
        access.activity = scope; access.worldSize = {25600, 14400}; access.playerCenter = {100, 100};
        access.spaceId = outdoorRaidSpaceId();
        preparePlan();
    }

    void preparePlan()
    {
        RaidReloadBinding binding{"", {}, weapon, "world-reload", fresh, old, scope};
        plan = queryRaidReload(profile, content, {binding, RaidReloadKind::Normal, RaidReloadSource::SelectedChestRig});
        ASSERT_TRUE(plan.canCommit) << plan.message;
    }

    RaidReloadReceipt stage(RaidReloadStage stage, RaidReloadKind kind = RaidReloadKind::Normal)
    {
        RaidReloadStageCommand command{plan.binding, stage, kind, plan.source, plan.newSource, plan.oldReturn, {}};
        if (kind == RaidReloadKind::Emergency && stage == RaidReloadStage::RemoveOld)
        {
            if (scope.activity == WeaponActionActivity::Training)
                command.worldDrop = TrainingDroppedMagazineLocation{scope.activityId, access.playerCenter};
            else command.worldDrop = BaseGroundAssetLocation{scope.baseSiteDefinitionId, access.playerCenter};
        }
        return executeRaidReloadStage(profile, content, command,
            {profile.revision, "world-stage/" + std::to_string(static_cast<int>(stage))}, access);
    }
};
}

TEST_F(WorldWeaponActivityDomainTest, BaseTakesAndRecoversOriginalMagazineWithoutPendingRaid)
{
    const auto nextId = profile.assets.nextAssetId();
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(profile.baseReloadRecovery);
    EXPECT_FALSE(profile.pendingRaid);
    EXPECT_TRUE(std::holds_alternative<BaseReloadHeldMagazineLocation>(profile.assets.find(fresh)->location));
    EXPECT_TRUE(assetIsCarried(profile, fresh));
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    EXPECT_FALSE(installedMagazine(profile, weapon));
    const auto recovery = recoverBaseReloadInCandidate(profile, content, access);
    ASSERT_TRUE(recovery.succeeded) << recovery.message;
    EXPECT_FALSE(profile.baseReloadRecovery);
    EXPECT_FALSE(installedMagazine(profile, weapon));
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(fresh)->location));
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(old)->location));
    EXPECT_EQ(profile.assets.nextAssetId(), nextId);
    EXPECT_EQ(magazineRoundCount(profile, fresh), 2U); EXPECT_EQ(magazineRoundCount(profile, old), 3U);
}

TEST_F(WorldWeaponActivityDomainTest, BaseCompletesTheSameFivePhysicalStepsAndClearsRecovery)
{
    for (auto phase : {RaidReloadStage::TakeNew, RaidReloadStage::RemoveOld, RaidReloadStage::InsertNew,
                       RaidReloadStage::StowOld, RaidReloadStage::Chamber})
    {
        const auto result = stage(phase);
        ASSERT_TRUE(result.succeeded) << result.message;
        EXPECT_TRUE(validateProfileState(profile, content).valid);
    }
    EXPECT_EQ(installedMagazine(profile, weapon), fresh);
    EXPECT_FALSE(profile.baseReloadRecovery);
    EXPECT_EQ(profile.assets.find(weapon)->chamberedRound->reliefBatchId, "first");
    EXPECT_EQ(profile.assets.find(fresh)->magazineRounds.front().reliefBatchId, "second");
}

TEST_F(WorldWeaponActivityDomainTest, BaseEmergencyDropPersistsAsBaseGroundAndNeverAsRaidOwnership)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew, RaidReloadKind::Emergency).succeeded);
    const auto removed = stage(RaidReloadStage::RemoveOld, RaidReloadKind::Emergency);
    ASSERT_TRUE(removed.succeeded); ASSERT_EQ(removed.droppedMagazineAssetIds, std::vector<AssetInstanceId>{old});
    const auto *drop = std::get_if<BaseGroundAssetLocation>(&profile.assets.find(old)->location);
    ASSERT_NE(drop, nullptr); EXPECT_EQ(drop->baseSiteDefinitionId, scope.baseSiteDefinitionId);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    ASSERT_TRUE(stage(RaidReloadStage::InsertNew, RaidReloadKind::Emergency).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::Chamber, RaidReloadKind::Emergency).succeeded);
    EXPECT_FALSE(profile.baseReloadRecovery); EXPECT_FALSE(profile.pendingRaid);
}

TEST_F(WorldWeaponActivityDomainTest, MissingRecoveryAndForeignActivityCannotStealHeldMagazines)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    auto corrupted = profile;
    corrupted.baseReloadRecovery.reset();
    EXPECT_FALSE(validateProfileState(corrupted, content).valid);
    const auto before = profileStateFingerprint(profile);
    auto wrong = access;
    wrong.activity->activityId = "other-site";
    const auto result = executeRaidReloadStage(profile, content,
        {plan.binding, RaidReloadStage::RemoveOld, plan.kind, plan.source, plan.newSource, plan.oldReturn, {}},
        {profile.revision, "foreign-world"}, wrong);
    EXPECT_FALSE(result.succeeded); EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST_F(WorldWeaponActivityDomainTest, GenericDropAndInventoryCannotBypassBaseHeldOwnership)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    const auto before = profileStateFingerprint(profile);
    EXPECT_FALSE(executeInventory(profile, content,
        InventoryMoveCommand{fresh, 0, plan.newSource.location, plan.newSource.orientation},
        {profile.revision, "held-move"}).succeeded);
    BaseGroundAccess ground{scope.baseSiteDefinitionId, access.playerCenter, access.playerCenter, false, 84};
    EXPECT_FALSE(executeBaseGround(profile, content, DropBaseGroundAssetCommand{fresh, 0, ItemOrientation::Degrees0, ground},
        {profile.revision, "held-drop"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), before);
}

TEST_F(WorldWeaponActivityDomainTest, TrainingHasItsOwnHeldAndDroppedIdentitiesWithoutPendingRaid)
{
    scope = {WeaponActionActivity::Training, {}, "isolated-range-session"};
    access.activity = scope;
    preparePlan();
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew, RaidReloadKind::Emergency).succeeded);
    EXPECT_TRUE(std::holds_alternative<TrainingHeldMagazineLocation>(profile.assets.find(fresh)->location));
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld, RaidReloadKind::Emergency).succeeded);
    EXPECT_TRUE(std::holds_alternative<TrainingDroppedMagazineLocation>(profile.assets.find(old)->location));
    EXPECT_FALSE(profile.baseReloadRecovery); EXPECT_FALSE(profile.pendingRaid);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
    auto view = observeAssetContractPoint(profile, access.spaceId, access.playerCenter);
    const auto key = view.key(); access.observation = &view; access.currentObservation = &key;
    ASSERT_TRUE(pickupDroppedMagazine(profile, content, old, {profile.revision, "training-pickup"}, access).succeeded);
    EXPECT_EQ(magazineRoundCount(profile, old), 3U);
}

TEST_F(WorldWeaponActivityDomainTest, BaseStashSourceRequiresCurrentAccessAndCannotReturnRemotely)
{
    const auto origin = findFirstProfileFit(profile, content, ProfileContainerId::stash(),
        content.item(alpha_content::magazine), ItemOrientation::Degrees0, fresh);
    ASSERT_TRUE(origin);
    profile.assets.findMutable(fresh)->location = StoredAssetLocation{ProfileContainerId::stash(), *origin};
    RaidReloadRequest request{plan.binding, RaidReloadKind::Normal, RaidReloadSource::ExplicitStored};
    EXPECT_FALSE(queryRaidReload(profile, content, request).canCommit);
    request.stashAccessible = true;
    plan = queryRaidReload(profile, content, request);
    ASSERT_TRUE(plan.canCommit) << plan.message;
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(stage(RaidReloadStage::TakeNew).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    access.stashAccessible = true;
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    access.stashAccessible = false;
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    const auto recovered = recoverBaseReloadInCandidate(profile, content, access);
    ASSERT_TRUE(recovered.succeeded) << recovered.message;
    const auto *stored = std::get_if<StoredAssetLocation>(&profile.assets.find(fresh)->location);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->container.kind, ProfileContainerKind::AssetCompartment);
    EXPECT_TRUE(assetIsCarried(profile, fresh));
}

TEST_F(WorldWeaponActivityDomainTest, RecoveryRejectsMalformedOriginalCellsWithoutMutatingAssets)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    auto changed = profile;
    changed.baseReloadRecovery->newSource.origin.x = -1;
    EXPECT_FALSE(validateBaseMagazineRecovery(changed, content).valid);
    changed = profile;
    changed.baseReloadRecovery->oldReturn->origin.y = -1;
    EXPECT_FALSE(validateBaseMagazineRecovery(changed, content).valid);
    EXPECT_TRUE(validateBaseMagazineRecovery(profile, content).valid);
}

TEST_F(WorldWeaponActivityDomainTest, EmptyBaseCorpseUsesAnExistingDeadSourceAndCreatesNoRewards)
{
    HomePerimeterGenerationContext generation{scope.baseSiteDefinitionId, access.worldSize, {{12000, 6000}, {1000, 1000}}, {}};
    const auto ensured = ensureHomePerimeterSnapshot(profile, content, generation, {profile.revision, "corpse-period"});
    ASSERT_TRUE(ensured.succeeded) << ensured.message;
    auto &site = profile.homePerimeter.sites.at(scope.baseSiteDefinitionId);
    ASSERT_FALSE(site.enemies.empty());
    auto &enemy = site.enemies.front(); enemy.health = 0;
    BaseCorpseRecord corpse{"", scope, enemy.localId, site.cycleIndex,
        {enemy.position.x + enemy.size.x * 0.5F, enemy.position.y + enemy.size.y * 0.5F}, enemy.size};
    const auto nextId = profile.assets.nextAssetId(), count = profile.assets.records().size();
    const auto recorded = recordBaseCorpseDeath(profile, content, corpse, {profile.revision, "corpse-death"});
    ASSERT_TRUE(recorded.succeeded) << recorded.message;
    EXPECT_EQ(profile.assets.nextAssetId(), nextId); EXPECT_EQ(profile.assets.records().size(), count);
    EXPECT_TRUE(validateBaseCorpseRecords(profile, content).valid);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_TRUE(recordBaseCorpseDeath(profile, content, corpse, {profile.revision, "corpse-death-repeat"}).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    access.playerCenter = corpse.position;
    auto view = observeAssetContractPoint(profile, access.spaceId, access.playerCenter);
    const auto key = view.key(); access.observation = &view; access.currentObservation = &key;
    const auto query = queryBaseCorpseInspection(profile, content, baseCorpseIdentity(corpse), access);
    EXPECT_TRUE(query.canCommit); EXPECT_EQ(query.message, "EMPTY CORPSE");
    ++site.cycleIndex;
    pruneBaseCorpseRecordsInCandidate(profile);
    EXPECT_TRUE(profile.baseCorpses.empty());
}
