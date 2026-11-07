#include <gtest/gtest.h>

#include <algorithm>
#include <nlohmann/json.hpp>

#include "alpha_content_ids.h"
#include "lost_raid_domain.h"
#include "raid_lifecycle.h"
#include "raid_visibility_fixture.h"
#include "recovery_task_domain.h"
#include "weapon_ammo_domain.h"

namespace
{
AssetInstanceId handlingFirst(const ProfileState &profile, const ItemDefinitionId &definition)
{
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}

class RaidWeaponHandlingDomainTest : public ::testing::Test
{
protected:
    const ContentRegistry &content{publishedContentRegistry()};
    ProfileState profile;
    AssetInstanceId weapon{}, old{}, fresh{}, rig{}, pack{};
    RaidReloadPlan plan;
    RaidReloadWorldAccess access;

    void SetUp() override
    {
        profile = makeNewAlphaProfile("weapon-handling-domain", content);
        weapon = handlingFirst(profile, alpha_content::rifle);
        rig = handlingFirst(profile, alpha_content::chestRig);
        pack = handlingFirst(profile, alpha_content::backpack);
        old = handlingFirst(profile, alpha_content::magazine);
        ASSERT_NE(weapon, 0U); ASSERT_NE(rig, 0U); ASSERT_NE(pack, 0U); ASSERT_NE(old, 0U);
        profile.assets.findMutable(weapon)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
        profile.assets.findMutable(rig)->location = EquippedAssetLocation{EquipmentSlotKind::ChestRig};
        profile.assets.findMutable(pack)->location = EquippedAssetLocation{EquipmentSlotKind::Backpack};
        profile.assets.findMutable(old)->location = InstalledMagazineLocation{weapon};
        profile.assets.findMutable(old)->magazineRounds.assign(3, MagazineRoundRecord{alpha_content::ammunition, "old-batch"});
        fresh = profile.assets.create(content.item(alpha_content::magazine),
            StoredAssetLocation{ProfileContainerId::compartment(rig, 0), {0, 0}});
        profile.assets.findMutable(fresh)->magazineRounds = {
            {alpha_content::ammunition, "first"}, {alpha_content::ammunition, "second"},
            {alpha_content::ammunition, "third"}, {alpha_content::ammunition, "fourth"}};
        const auto deployed = executeDeploy(profile, content,
            DeployCommand{"handling-raid", "handling-settlement", 123U, MapDefinitionId{"map.v0.test"}, {}, std::nullopt},
            {profile.revision, "handling-deploy"});
        ASSERT_TRUE(deployed.succeeded) << deployed.message;
        RaidReloadRequest request;
        request.binding = {profile.pendingRaid->raidId, outdoorRaidSpaceId(), weapon, "handling-action", fresh, old};
        request.source = RaidReloadSource::SelectedChestRig;
        plan = queryRaidReload(profile, content, request);
        ASSERT_TRUE(plan.canCommit) << plan.message;
        access.spaceId = outdoorRaidSpaceId(); access.playerCenter = {100, 100};
    }

    RaidReloadStageCommand stageCommand(RaidReloadStage stage, RaidReloadKind kind = RaidReloadKind::Normal) const
    {
        return {plan.binding, stage, kind, plan.source, plan.newSource, plan.oldReturn,
            RaidDroppedMagazineLocation{plan.binding.raidId, access.spaceId, access.playerCenter}};
    }

    RaidReloadReceipt stage(RaidReloadStage stage, RaidReloadKind kind = RaidReloadKind::Normal)
    {
        return executeRaidReloadStage(profile, content, stageCommand(stage, kind),
            {profile.revision, plan.binding.actionId + "/" + std::to_string(static_cast<int>(stage))}, access);
    }

    RaidReloadReceipt cancel()
    {
        const RaidDroppedMagazineLocation drop{plan.binding.raidId, access.spaceId, access.playerCenter};
        return executeRaidReloadCancel(profile, content,
            {plan.binding, plan.newSource, drop, drop}, {profile.revision, "handling-cancel"}, access);
    }
};
}

TEST(WeaponFireModeDomainTest, SixDefinitionsAndCreatedInstancesHaveExplicitLegalModes)
{
    const auto &content = publishedContentRegistry();
    const std::vector<std::pair<const char *, std::vector<WeaponFireMode>>> cases{
        {"item.weapon.rifle_basic", {WeaponFireMode::SemiAutomatic, WeaponFireMode::ThreeRoundBurst, WeaponFireMode::Automatic}},
        {"item.weapon.rifle_5_45_service", {WeaponFireMode::SemiAutomatic, WeaponFireMode::Automatic}},
        {"item.weapon.carbine_5_45_compact", {WeaponFireMode::SemiAutomatic, WeaponFireMode::Automatic}},
        {"item.weapon.pistol_basic", {WeaponFireMode::SemiAutomatic}},
        {"item.weapon.dmr_7_62x51_service", {WeaponFireMode::SemiAutomatic}},
        {"item.weapon.lmg_7_62x51_service", {WeaponFireMode::Automatic}}};
    for (const auto &[id, modes] : cases)
    {
        const auto &definition = content.item(ItemDefinitionId{id});
        ASSERT_TRUE(definition.weaponUse);
        EXPECT_EQ(definition.weaponUse->supportedFireModes, modes);
        AssetRegistry registry;
        const auto assetId = registry.create(definition, StoredAssetLocation{ProfileContainerId::stash(), {0, 0}});
        EXPECT_EQ(registry.find(assetId)->fireMode, definition.weaponUse->defaultFireMode);
        const AssetRegistry copy = registry;
        EXPECT_EQ(copy.find(assetId)->fireMode, registry.find(assetId)->fireMode);
    }
}

TEST(WeaponFireModeDomainTest, MalformedModeDefinitionsAreRejected)
{
    const auto original = nlohmann::json::parse(publishedContentJson());
    for (int variant = 0; variant < 5; ++variant)
    {
        auto root = original;
        auto &items = root.at("items");
        const auto found = std::find_if(items.begin(), items.end(), [](const auto &item) {
            return item.at("id") == "item.weapon.rifle_basic";
        });
        ASSERT_NE(found, items.end());
        auto &use = found->at("weapon_use");
        if (variant == 0) use["supported_fire_modes"] = nlohmann::json::array();
        if (variant == 1) use["supported_fire_modes"] = {"automatic", "automatic"};
        if (variant == 2) use["default_fire_mode"] = "unknown_mode";
        if (variant == 3) use["supported_fire_modes"] = {"semi_automatic"};
        if (variant == 4) use["burst_shot_count"] = 2;
        EXPECT_THROW((void)ContentRegistry::fromJson(root.dump()), ContentRegistryError);
    }
}

TEST_F(RaidWeaponHandlingDomainTest, InstanceModeUsesStableIdAndRejectsUnsupportedModeWithoutMutation)
{
    const auto before = profileStateFingerprint(profile);
    ASSERT_TRUE(executeWeaponAmmo(profile, content,
        SetWeaponFireModeCommand{weapon, WeaponFireMode::ThreeRoundBurst}, {profile.revision, "set-burst"}).succeeded);
    EXPECT_EQ(queryWeaponFireMode(profile, content, weapon), WeaponFireMode::ThreeRoundBurst);
    EXPECT_NE(profileStateFingerprint(profile), before);
    const auto pistol = handlingFirst(profile, alpha_content::pistol);
    profile.assets.findMutable(pistol)->location = EquippedAssetLocation{EquipmentSlotKind::Sidearm};
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(executeWeaponAmmo(profile, content, SetWeaponFireModeCommand{pistol, WeaponFireMode::Automatic},
        {profile.revision, "illegal-mode"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(RaidWeaponHandlingDomainTest, RaidRequiresExplicitChamberButNormalShotFeedsAndBaseTriggerStillChambers)
{
    FireWeaponCommand fire{weapon}; fire.emptyChamberPolicy = EmptyChamberPolicy::RequireExplicitChamber;
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_EQ(queryFireWeapon(profile, content, fire).result, WeaponAmmoResult::NeedsChamber);
    EXPECT_EQ(executeFireWeapon(profile, content, fire, {profile.revision, "raid-trigger"}).result, WeaponAmmoResult::NeedsChamber);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    ASSERT_TRUE(executeWeaponAmmo(profile, content, ChamberWeaponCommand{weapon}, {profile.revision, "explicit-chamber"}).succeeded);
    EXPECT_EQ(magazineRoundCount(profile, old), 2U);
    EXPECT_EQ(executeFireWeapon(profile, content, fire, {profile.revision, "raid-shot"}).result, WeaponAmmoResult::Fired);
    EXPECT_EQ(magazineRoundCount(profile, old), 1U);
    EXPECT_TRUE(profile.assets.find(weapon)->chamberedRound);
    profile.assets.findMutable(weapon)->chamberedRound.reset();
    EXPECT_EQ(executeFireWeapon(profile, content, FireWeaponCommand{weapon}, {profile.revision, "base-trigger"}).result,
              WeaponAmmoResult::Chambered);
}

TEST_F(RaidWeaponHandlingDomainTest, EachPhysicalStepPreservesIdsAndOrderedRounds)
{
    const auto nextId = profile.assets.nextAssetId();
    const auto rounds = profile.assets.find(fresh)->magazineRounds;
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    EXPECT_TRUE(assetIsCarried(profile, fresh));
    EXPECT_TRUE(std::holds_alternative<RaidReloadHeldMagazineLocation>(profile.assets.find(fresh)->location));
    const auto removed = stage(RaidReloadStage::RemoveOld);
    ASSERT_TRUE(removed.succeeded); EXPECT_TRUE(removed.magazineRemoved);
    EXPECT_FALSE(installedMagazine(profile, weapon));
    EXPECT_TRUE(std::holds_alternative<RaidReloadHeldMagazineLocation>(profile.assets.find(old)->location));
    const auto inserted = stage(RaidReloadStage::InsertNew);
    ASSERT_TRUE(inserted.succeeded); EXPECT_TRUE(inserted.magazineInserted);
    EXPECT_EQ(installedMagazine(profile, weapon), fresh);
    EXPECT_EQ(profile.assets.find(fresh)->magazineRounds, rounds);
    ASSERT_TRUE(stage(RaidReloadStage::StowOld).succeeded);
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(old)->location), plan.oldReturn->location);
    const auto chambered = stage(RaidReloadStage::Chamber);
    ASSERT_TRUE(chambered.succeeded); EXPECT_TRUE(chambered.chamberedRound);
    EXPECT_EQ(profile.assets.find(weapon)->chamberedRound, std::optional<MagazineRoundRecord>{rounds.front()});
    EXPECT_EQ(profile.assets.find(fresh)->magazineRounds.front(), rounds[1]);
    EXPECT_EQ(profile.assets.nextAssetId(), nextId);
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(RaidWeaponHandlingDomainTest, DuplicateAndStaleStagePreserveEveryParticipant)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    const auto fingerprint = profileStateFingerprint(profile);
    const auto duplicate = stage(RaidReloadStage::TakeNew);
    EXPECT_TRUE(duplicate.alreadyCommitted); EXPECT_FALSE(duplicate.magazineRemoved);
    EXPECT_FALSE(duplicate.magazineInserted); EXPECT_TRUE(duplicate.droppedMagazineAssetIds.empty());
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    const auto stale = executeRaidReloadStage(profile, content, stageCommand(RaidReloadStage::RemoveOld),
        {profile.revision - 1, "stale-stage"}, access);
    EXPECT_EQ(stale.error, DomainErrorCode::StaleRevision);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_FALSE(stage(RaidReloadStage::InsertNew).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(RaidWeaponHandlingDomainTest, ModeRevisionDoesNotInvalidateUnchangedStageParticipants)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(profile, content, SetWeaponFireModeCommand{weapon, WeaponFireMode::SemiAutomatic},
        {profile.revision, "mode-during-reload"}).succeeded);
    EXPECT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
}

TEST_F(RaidWeaponHandlingDomainTest, CancellationBeforeTakingMagazineIsZeroModification)
{
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_TRUE(cancel().succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(RaidWeaponHandlingDomainTest, DoubleHeldCancellationReturnsNewBeforeFindingOldStorage)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    ASSERT_TRUE(cancel().succeeded);
    EXPECT_FALSE(installedMagazine(profile, weapon));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile.assets.find(fresh)->location), plan.newSource.location);
    const auto *storedOld = std::get_if<StoredAssetLocation>(&profile.assets.find(old)->location);
    ASSERT_NE(storedOld, nullptr); EXPECT_NE(*storedOld, plan.newSource.location);
    EXPECT_EQ(magazineRoundCount(profile, old), 3U); EXPECT_EQ(magazineRoundCount(profile, fresh), 4U);
}

TEST_F(RaidWeaponHandlingDomainTest, CancellationAfterInsertionKeepsNewMagazineInstalled)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::InsertNew).succeeded);
    ASSERT_TRUE(cancel().succeeded);
    EXPECT_EQ(installedMagazine(profile, weapon), fresh);
    EXPECT_FALSE(profile.assets.find(weapon)->chamberedRound);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(old)->location));
}

TEST_F(RaidWeaponHandlingDomainTest, EmergencyDropsOriginalMagazineAndCancellationDoesNotRecallIt)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew, RaidReloadKind::Emergency).succeeded);
    const auto removed = stage(RaidReloadStage::RemoveOld, RaidReloadKind::Emergency);
    ASSERT_TRUE(removed.succeeded); ASSERT_EQ(removed.droppedMagazineAssetIds, std::vector<AssetInstanceId>{old});
    const auto dropped = profile.assets.find(old)->location;
    access.playerCenter = {150, 100};
    ASSERT_TRUE(cancel().succeeded);
    EXPECT_EQ(profile.assets.find(old)->location, dropped);
    EXPECT_EQ(magazineRoundCount(profile, old), 3U);
}

TEST_F(RaidWeaponHandlingDomainTest, ExplicitEmptyMagazineFinishesWithoutInventingChamberedRound)
{
    profile.assets.findMutable(fresh)->magazineRounds.clear();
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::InsertNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::StowOld).succeeded);
    const auto completed = stage(RaidReloadStage::Chamber);
    EXPECT_TRUE(completed.succeeded); EXPECT_FALSE(completed.chamberedRound);
    EXPECT_FALSE(profile.assets.find(weapon)->chamberedRound);
}

TEST_F(RaidWeaponHandlingDomainTest, AutomaticSelectionExcludesEmptyButWheelListsIt)
{
    profile.assets.findMutable(fresh)->magazineRounds.clear();
    RaidReloadRequest request{plan.binding}; request.binding.newMagazineAssetId = 0;
    request.source = RaidReloadSource::AutomaticChestRig;
    EXPECT_FALSE(queryRaidReload(profile, content, request).canCommit);
    const auto choices = queryRaidReloadMagazines(profile, content, weapon);
    const auto choice = std::find_if(choices.begin(), choices.end(), [&](const auto &item) { return item.magazineAssetId == fresh; });
    ASSERT_NE(choice, choices.end()); EXPECT_EQ(choice->roundCount, 0U); EXPECT_TRUE(choice->eligible);
}

TEST_F(RaidWeaponHandlingDomainTest, PickupRequiresIndependentCurrentWorldPositionAndPreservesBatchOrder)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew, RaidReloadKind::Emergency).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld, RaidReloadKind::Emergency).succeeded);
    const auto rounds = profile.assets.find(old)->magazineRounds;
    auto view = observeAssetContractPoint(profile, access.spaceId, access.playerCenter);
    auto key = view.key(); access.observation = &view; access.currentObservation = &key;
    auto forged = access; forged.playerCenter.x += 200;
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(pickupDroppedMagazine(profile, content, old, {profile.revision, "forged-pickup"}, forged).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    ASSERT_TRUE(pickupDroppedMagazine(profile, content, old, {profile.revision, "real-pickup"}, access).succeeded);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(old)->location));
    EXPECT_EQ(profile.assets.find(old)->magazineRounds, rounds);
}

TEST_F(RaidWeaponHandlingDomainTest, FailedRaidRetainsBothHeldMagazinesWithTheOriginalLostWeaponTree)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    ASSERT_TRUE(retainRaidReloadMagazinesForLossInCandidate(profile, content, plan.binding.raidId).valid);
    LostRaidRecord record;
    record.recordId = "retained-test"; record.raidId = plan.binding.raidId; record.settlementId = "retained-test";
    record.mapDefinitionId = profile.pendingRaid->mapDefinitionId;
    profile.lostRaidRecords.emplace(record.recordId, record);
    profile.assets.findMutable(weapon)->location = LostRaidAssetLocation{record.recordId, EquipmentSlotKind::PrimaryWeapon};
    EXPECT_TRUE(validateRaidMagazineLocations(profile, content).valid);
    EXPECT_EQ(lostRaidRecordForAsset(profile, fresh), record.recordId);
    EXPECT_EQ(lostRaidRecordForAsset(profile, old), record.recordId);
    EXPECT_FALSE(installedMagazine(profile, weapon));
    profile.assets.findMutable(weapon)->location = EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon};
    EXPECT_FALSE(validateRaidMagazineLocations(profile, content).valid);
    ASSERT_TRUE(resolveRetainedRaidMagazinesInCandidate(profile, content, weapon, true).valid);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(fresh)->location));
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(old)->location));
}

TEST_F(RaidWeaponHandlingDomainTest, GenericInventoryCannotMoveAnActiveHeldParticipant)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(executeInventory(profile, content,
        InventoryMoveCommand{fresh, 0, plan.newSource.location, plan.newSource.orientation},
        {profile.revision, "bypass-held"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_FALSE(executeWeaponAmmo(profile, content, InstallMagazineCommand{weapon, fresh},
        {profile.revision, "bypass-held-install"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(RaidWeaponHandlingDomainTest, CancelAtEveryPhysicalBoundaryPreservesCompletedSteps)
{
    const auto initial = profile;
    const std::vector<RaidReloadStage> stages{RaidReloadStage::TakeNew, RaidReloadStage::RemoveOld,
        RaidReloadStage::InsertNew, RaidReloadStage::StowOld, RaidReloadStage::Chamber};
    for (std::size_t completed = 0; completed <= stages.size(); ++completed)
    {
        SCOPED_TRACE(completed);
        profile = initial;
        for (std::size_t i = 0; i < completed; ++i) ASSERT_TRUE(stage(stages[i]).succeeded);
        ASSERT_TRUE(cancel().succeeded);
        if (completed < 2) EXPECT_EQ(installedMagazine(profile, weapon), old);
        else if (completed < 3) EXPECT_FALSE(installedMagazine(profile, weapon));
        else EXPECT_EQ(installedMagazine(profile, weapon), fresh);
        EXPECT_EQ(magazineRoundCount(profile, old), 3U);
        EXPECT_EQ(magazineRoundCount(profile, fresh), completed == 5 ? 3U : 4U);
        EXPECT_EQ(profile.assets.find(weapon)->chamberedRound.has_value(), completed == 5);
        for (const auto &[id, asset] : profile.assets.records())
            EXPECT_FALSE(std::holds_alternative<RaidReloadHeldMagazineLocation>(asset.location));
    }
}

TEST_F(RaidWeaponHandlingDomainTest, FullCarriedStorageDropsBothHeldMagazinesWithoutOverwritingOccupants)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    std::vector<AssetInstanceId> blockers;
    for (const auto owner : {rig, pack})
    {
        const auto &definition = content.item(profile.assets.find(owner)->definitionId);
        for (std::size_t i = 0; i < definition.containerCompartments.size(); ++i)
        {
            const auto container = ProfileContainerId::compartment(owner, static_cast<std::uint32_t>(i));
            while (const auto origin = findFirstProfileFit(profile, content, container,
                    content.item(alpha_content::magazine), ItemOrientation::Degrees0))
                blockers.push_back(profile.assets.create(content.item(alpha_content::magazine),
                    StoredAssetLocation{container, *origin}));
        }
    }
    const auto result = cancel();
    ASSERT_TRUE(result.succeeded) << result.message;
    EXPECT_EQ(result.droppedMagazineAssetIds, (std::vector<AssetInstanceId>{fresh, old}));
    EXPECT_TRUE(std::holds_alternative<RaidDroppedMagazineLocation>(profile.assets.find(fresh)->location));
    EXPECT_TRUE(std::holds_alternative<RaidDroppedMagazineLocation>(profile.assets.find(old)->location));
    for (auto id : blockers) EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile.assets.find(id)->location));
    EXPECT_EQ(magazineRoundCount(profile, fresh), 4U); EXPECT_EQ(magazineRoundCount(profile, old), 3U);
}

TEST_F(RaidWeaponHandlingDomainTest, FailedEmergencyDropDoesNotRemoveOldOrMoveAnyAsset)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew, RaidReloadKind::Emergency).succeeded);
    auto command = stageCommand(RaidReloadStage::RemoveOld, RaidReloadKind::Emergency);
    command.drop->position.x += 200;
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(executeRaidReloadStage(profile, content, command,
        {profile.revision, "remote-drop"}, access).succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
    EXPECT_EQ(installedMagazine(profile, weapon), old);
}

TEST_F(RaidWeaponHandlingDomainTest, InvalidInstanceModesAndHeldActionDisagreementAreRejected)
{
    const auto original = profile.assets.find(weapon)->fireMode;
    profile.assets.findMutable(weapon)->fireMode.reset();
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    profile.assets.findMutable(weapon)->fireMode = original;
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    auto &oldHeld = std::get<RaidReloadHeldMagazineLocation>(profile.assets.findMutable(old)->location);
    oldHeld.actionId = "different-action";
    EXPECT_FALSE(validateRaidMagazineLocations(profile, content).valid);
    const auto fingerprint = profileStateFingerprint(profile);
    EXPECT_FALSE(cancel().succeeded);
    EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
}

TEST_F(RaidWeaponHandlingDomainTest, RollbackBaselineRejectsActiveHeldAndDroppedMagazineLocations)
{
    ASSERT_TRUE(rollbackPendingRaidToBase(profile, content).succeeded);
    const auto deployed = executeDeploy(profile, content,
        {"rollback-magazine-raid", "rollback-magazine-result", 1928U, MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "rollback-magazine-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->startingAssets);
    const AssetRegistry baseline = *profile.pendingRaid->startingAssets;
    auto *magazine = profile.pendingRaid->startingAssets->findMutable(fresh);
    ASSERT_NE(magazine, nullptr);
    magazine->location = RaidDroppedMagazineLocation{profile.pendingRaid->raidId, outdoorRaidSpaceId(), {100, 100}};
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    *profile.pendingRaid->startingAssets = baseline;
    magazine = profile.pendingRaid->startingAssets->findMutable(fresh);
    magazine->location = RaidReloadHeldMagazineLocation{profile.pendingRaid->raidId,
        outdoorRaidSpaceId(), weapon, "impossible-pre-raid-action", RaidReloadMagazineRole::NewMagazine};
    EXPECT_FALSE(validateProfileState(profile, content).valid);
    *profile.pendingRaid->startingAssets = baseline;
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}

TEST_F(RaidWeaponHandlingDomainTest, HistoricalRetainedRollbackChildrenRequireUniqueRolesValidRoundsAndOriginalOwner)
{
    ASSERT_TRUE(stage(RaidReloadStage::TakeNew).succeeded);
    ASSERT_TRUE(stage(RaidReloadStage::RemoveOld).succeeded);
    const auto lost = settlePendingRaid(profile, content, "handling-settlement", RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(lost.succeeded) << lost.message;
    ASSERT_TRUE(std::holds_alternative<RetainedRaidMagazineLocation>(profile.assets.find(fresh)->location));
    const auto deployed = executeDeploy(profile, content,
        {"retained-baseline-raid", "retained-baseline-result", 1929U, MapDefinitionId{"map.raid.frontier_exchange"}},
        {profile.revision, "retained-baseline-deploy"});
    ASSERT_TRUE(deployed.succeeded) << deployed.message;
    ASSERT_TRUE(profile.pendingRaid->startingAssets);
    ASSERT_TRUE(validateProfileState(profile, content).valid);
    const AssetRegistry baseline = *profile.pendingRaid->startingAssets;
    for (int corruption = 0; corruption < 4; ++corruption)
    {
        SCOPED_TRACE(corruption);
        auto &assets = *profile.pendingRaid->startingAssets;
        assets = baseline;
        if (corruption == 0)
            std::get<RetainedRaidMagazineLocation>(assets.findMutable(old)->location).role = RaidReloadMagazineRole::NewMagazine;
        if (corruption == 1)
            std::get<LostRaidAssetLocation>(assets.findMutable(weapon)->location).recordId = "missing-rollback-owner";
        if (corruption == 2)
            assets.findMutable(fresh)->magazineRounds.assign(31, MagazineRoundRecord{alpha_content::ammunition, std::nullopt});
        if (corruption == 3)
            std::get<LostRaidAssetLocation>(assets.findMutable(weapon)->location).sourceSlot = EquipmentSlotKind::ChestRig;
        EXPECT_FALSE(validateProfileState(profile, content).valid);
    }
    *profile.pendingRaid->startingAssets = baseline;
    EXPECT_TRUE(validateProfileState(profile, content).valid);
}
