#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "alpha_content_ids.h"
#include "chest_rig_layout_migration.h"
#include "game_session.h"
#include "raid_lifecycle.h"
#include "save_repository.h"

namespace {
ProfileState fullLegacyRig(std::string_view name) {
    const auto &c=publishedContentRegistry();
    auto p=makeNewAlphaProfile("chest-layout",c);p.chestRigLayoutVersion=0;
    const auto &d=c.item(ItemDefinitionId{"item.container.chest_rig_"+std::string{name}});
    const auto rig=p.assets.create(d,EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    const auto pockets=legacyChestRigCompartments(d);
    for(std::uint32_t index=0;index<pockets.size();++index) {
        const auto &pocket=pockets[index];
        const auto &item=c.item(pocket.pocketKind==ContainerPocketKind::MagazineOnly?alpha_content::magazine:alpha_content::bandage);
        if(pocket.pocketKind==ContainerPocketKind::MagazineOnly) {
            const auto id=p.assets.create(item,StoredAssetLocation{ProfileContainerId::compartment(rig,index),{0,0}});
            p.assets.findMutable(id)->magazineRounds={
                {alpha_content::ammunition,std::nullopt},
                {ItemDefinitionId{"item.ammunition.9mm_enhanced"},std::nullopt},
                {alpha_content::ammunition,std::nullopt}};
        }
        else for(int y=0;y<pocket.height;++y)for(int x=0;x<pocket.width;++x)
            p.assets.create(item,StoredAssetLocation{ProfileContainerId::compartment(rig,index),{x,y}});
    }
    return p;
}
struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("raidline-chest-layout-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){std::filesystem::create_directory(path);}
    ~TemporaryDirectory(){std::error_code e;std::filesystem::remove_all(path,e);}
};
}
TEST(ChestRigLayoutTest, AllThreeFullLegacyRigsMigrateWithoutLossOrRepacking) {
    const auto &c=publishedContentRegistry();
    for(auto name:{"small","patrol","assault"}) {
        SCOPED_TRACE(name);auto p=fullLegacyRig(name);ASSERT_TRUE(validateProfileState(p,c).valid);
        const auto before=p;const auto high=p.assets.nextAssetId();
        const auto receipt=migrateLegacyChestRigLayout(p,c);ASSERT_TRUE(receipt.valid)<<receipt.message;
        EXPECT_EQ(p.chestRigLayoutVersion,1);EXPECT_EQ(p.assets.nextAssetId(),high);
        ASSERT_EQ(p.assets.records().size(),before.assets.records().size());
        for(const auto &[id,old]:before.assets.records()) {
            const auto *now=p.assets.find(id);ASSERT_NE(now,nullptr);
            EXPECT_EQ(now->definitionId,old.definitionId);EXPECT_EQ(now->quantity,old.quantity);
            EXPECT_EQ(now->remainingCharges,old.remainingCharges);EXPECT_EQ(now->orientation,old.orientation);
            EXPECT_EQ(now->currentDurability,old.currentDurability);EXPECT_EQ(now->magazineRounds,old.magazineRounds);
            if(const auto *stored=std::get_if<StoredAssetLocation>(&now->location);
               stored && stored->container.kind==ProfileContainerKind::AssetCompartment)EXPECT_EQ(stored->container.compartmentIndex,0);
        }
        const auto fingerprint=profileStateFingerprint(p);
        EXPECT_TRUE(migrateLegacyChestRigLayout(p,c).valid);EXPECT_EQ(profileStateFingerprint(p),fingerprint);
        const auto reverse=legacyChestRigSnapshot(p);
        EXPECT_EQ(profileStateFingerprint(reverse),profileStateFingerprint(before));
    }
}
TEST(ChestRigLayoutTest, InvalidOldPocketIsRejectedWithoutMutation) {
    const auto &c=publishedContentRegistry();auto p=fullLegacyRig("small");
    const auto rig=*equippedAsset(p,EquipmentSlotKind::ChestRig);
    const auto old=assetsInContainer(p,ProfileContainerId::compartment(rig,0)).front()->instanceId;
    p.assets.findMutable(old)->location=StoredAssetLocation{ProfileContainerId::compartment(rig,0),{1,0}};
    const auto fingerprint=profileStateFingerprint(p);
    EXPECT_FALSE(migrateLegacyChestRigLayout(p,c).valid);EXPECT_EQ(profileStateFingerprint(p),fingerprint);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,"raid-noise-decoy-content-67",55),c);
    EXPECT_FALSE(loaded.profile);EXPECT_EQ(p.chestRigLayoutVersion,0);
}
TEST(ChestRigLayoutTest, LegacySaveUpgradesOnceAndPreservesExactSourceBeforeWrite) {
    const auto &c=publishedContentRegistry();TemporaryDirectory temp;SaveRepository saves{temp.path};
    const auto old=fullLegacyRig("assault");const auto bytes=serializeProfileEnvelope(old,"raid-noise-decoy-content-67",55);
    {std::ofstream out(saves.primaryPath(),std::ios::binary);out<<bytes;}
    auto loaded=saves.load(c);ASSERT_TRUE(loaded.profile)<<loaded.message;EXPECT_TRUE(loaded.migratedChestRigLayout);
    ASSERT_TRUE(saves.preserveChestRigMigrationSources(loaded.status).succeeded);
    std::size_t snapshots{};
    for(const auto &entry:std::filesystem::directory_iterator(temp.path))if(entry.path().filename().string().starts_with("profile.pre-chest-rig-v2.")) {
        std::ifstream in(entry.path(),std::ios::binary);const std::string actual{std::istreambuf_iterator<char>{in},{}};
        EXPECT_EQ(actual,bytes);++snapshots;
    }
    EXPECT_EQ(snapshots,1);
    ASSERT_TRUE(saves.save(*loaded.profile,c.contentVersion()).succeeded);
    const auto again=saves.load(c);ASSERT_TRUE(again.profile);EXPECT_FALSE(again.migratedChestRigLayout);
    EXPECT_EQ(profileStateFingerprint(*again.profile),profileStateFingerprint(*loaded.profile));
    const auto json=nlohmann::json::parse(serializeProfileEnvelope(*again.profile,c.contentVersion()));
    EXPECT_EQ(json["schema_version"],56);EXPECT_EQ(json["payload"]["chest_rig_layout_version"],1);
}
TEST(ChestRigLayoutTest, CorruptPrimaryRecoversLegacyBackupAndKeepsBothSources) {
    const auto &c=publishedContentRegistry();TemporaryDirectory temp;SaveRepository saves{temp.path};
    const auto bytes=serializeProfileEnvelope(fullLegacyRig("patrol"),"raid-noise-decoy-content-67",55);
    {std::ofstream out(saves.primaryPath());out<<"broken";}
    {std::ofstream out(saves.backupPath(),std::ios::binary);out<<bytes;}
    const auto loaded=saves.load(c);ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(loaded.status,SaveLoadStatus::RecoveredBackup);EXPECT_TRUE(loaded.migratedChestRigLayout);
    ASSERT_TRUE(saves.preserveChestRigMigrationSources(loaded.status).succeeded);
    std::size_t snapshots{};for(const auto &e:std::filesystem::directory_iterator(temp.path))if(e.path().filename().string().find("pre-chest-rig-v2.")!=std::string::npos)++snapshots;
    EXPECT_EQ(snapshots,2);
}
TEST(ChestRigLayoutTest, PendingRaidAndStartingAssetSnapshotMigrateTogether) {
    const auto &c=publishedContentRegistry();auto old=fullLegacyRig("patrol");
    const auto deployed=executeDeploy(old,c,DeployCommand{"old-chest-raid","old-chest-settlement",41,MapDefinitionId{"map.raid.frontier_exchange"}},
        {old.revision,"deploy-old-chest"});
    ASSERT_TRUE(deployed.succeeded)<<deployed.message;ASSERT_TRUE(old.pendingRaid->startingAssets);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(old,"raid-noise-decoy-content-67",55),c);
    ASSERT_TRUE(loaded.profile)<<loaded.message;
    auto candidate=*loaded.profile;EXPECT_EQ(candidate.pendingRaid->raidId,old.pendingRaid->raidId);
    const auto rollback=rollbackPendingRaidToBase(candidate,c);ASSERT_TRUE(rollback.succeeded)<<rollback.message;
    EXPECT_EQ(candidate.chestRigLayoutVersion,1);EXPECT_FALSE(candidate.pendingRaid);
    EXPECT_TRUE(validateProfileState(candidate,c).valid);
    const auto rig=*equippedAsset(candidate,EquipmentSlotKind::ChestRig);
    const auto &d=c.item(candidate.assets.find(rig)->definitionId);
    EXPECT_EQ(d.containerCompartments.size(),1);
    for(const auto *a:assetsInContainer(candidate,ProfileContainerId::compartment(rig,0))) {
        const auto *prior=old.pendingRaid->startingAssets->find(a->instanceId);ASSERT_NE(prior,nullptr);
        EXPECT_EQ(a->quantity,prior->quantity);EXPECT_EQ(a->remainingCharges,prior->remainingCharges);
    }
}
TEST(ChestRigLayoutTest, UnifiedGeneralSpaceAcceptsToolsAndRejectsLossyLegacySerialization) {
    const auto &c=publishedContentRegistry();auto p=makeNewAlphaProfile("mixed-chest",c);
    const auto rig=p.assets.create(c.item(alpha_content::chestRig),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    p.assets.create(c.item(alpha_content::bandage),StoredAssetLocation{ProfileContainerId::compartment(rig,0),{0,0}});
    ASSERT_TRUE(validateProfileState(p,c).valid);
    EXPECT_THROW(serializeProfileEnvelope(p,"raid-noise-decoy-content-67",55),std::invalid_argument);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,c.contentVersion()),c);
    ASSERT_TRUE(loaded.profile)<<loaded.message;EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));
}
TEST(ChestRigLayoutTest, RotatedItemInUnequippedRigKeepsItsExactInstanceAndOrientation) {
    const auto &c=publishedContentRegistry();auto old=fullLegacyRig("patrol");
    const auto rig=*equippedAsset(old,EquipmentSlotKind::ChestRig);
    std::vector<AssetInstanceId> remove;
    for(const auto *a:assetsInContainer(old,ProfileContainerId::compartment(rig,4)))remove.push_back(a->instanceId);
    for(auto id:remove)ASSERT_TRUE(old.assets.erase(id));
    const auto rotated=old.assets.create(c.item(alpha_content::armorMaintenanceKit),StoredAssetLocation{ProfileContainerId::compartment(rig,4),{0,0}});
    old.assets.findMutable(rotated)->orientation=ItemOrientation::Degrees90;
    old.assets.findMutable(rotated)->remainingCharges=317;
    const auto fit=findFirstProfileFit(old,c,ProfileContainerId::stash(),c.item(old.assets.find(rig)->definitionId),ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);old.assets.findMutable(rig)->location=StoredAssetLocation{ProfileContainerId::stash(),*fit};
    ASSERT_TRUE(validateProfileState(old,c).valid)<<validateProfileState(old,c).message;
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(old,"raid-noise-decoy-content-67",55),c);ASSERT_TRUE(loaded.profile)<<loaded.message;
    const auto *a=loaded.profile->assets.find(rotated);ASSERT_NE(a,nullptr);
    EXPECT_EQ(a->orientation,ItemOrientation::Degrees90);EXPECT_EQ(a->remainingCharges,317);
    EXPECT_EQ(std::get<StoredAssetLocation>(a->location).origin,(GridPosition{0,2}));
    EXPECT_EQ(profileStateFingerprint(legacyChestRigSnapshot(*loaded.profile)),profileStateFingerprint(old));
}
TEST(ChestRigLayoutTest, LostRaidRootAndAllItsChildrenStayOwnedAfterMigration) {
    const auto &c=publishedContentRegistry();auto old=fullLegacyRig("small");
    ASSERT_TRUE(executeDeploy(old,c,DeployCommand{"lost-layout","lost-layout-settlement",42,MapDefinitionId{"map.raid.frontier_exchange"}},
        {old.revision,"lost-layout-deploy"}).succeeded);
    const auto receipt=settlePendingRaid(old,c,"lost-layout-settlement",RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(receipt.succeeded)<<receipt.message;ASSERT_EQ(old.lostRaidRecords.size(),1);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(old,"raid-noise-decoy-content-67",55),c);
    ASSERT_TRUE(loaded.profile)<<loaded.message;EXPECT_EQ(loaded.profile->lostRaidRecords,old.lostRaidRecords);
    EXPECT_EQ(profileStateFingerprint(legacyChestRigSnapshot(*loaded.profile)),profileStateFingerprint(old));
}
TEST(ChestRigLayoutTest, FailedImmutableBackupPreventsContinueFromReplacingProfileOrOriginalFile) {
    const auto &c=publishedContentRegistry();TemporaryDirectory temp;SaveRepository saves{temp.path};
    const auto bytes=serializeProfileEnvelope(fullLegacyRig("small"),"raid-noise-decoy-content-67",55);
    {std::ofstream out(saves.primaryPath(),std::ios::binary);out<<bytes;}
    ASSERT_TRUE(saves.preserveChestRigMigrationSources(SaveLoadStatus::LoadedPrimary).succeeded);
    for(const auto &e:std::filesystem::directory_iterator(temp.path))if(e.path().filename().string().starts_with("profile.pre-chest-rig-v2.")) {
        std::ofstream out(e.path(),std::ios::binary|std::ios::trunc);out<<"immutable collision";
    }
    GameSession session;session.configurePersistence(temp.path);const auto before=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.continueProfile());EXPECT_EQ(profileStateFingerprint(session.profile()),before);
    std::ifstream in(saves.primaryPath(),std::ios::binary);const std::string actual{std::istreambuf_iterator<char>{in},{}};
    EXPECT_EQ(actual,bytes);
}
TEST(ChestRigLayoutTest, GeneralRigRejectsANonemptyNestedContainerButAcceptsTheEmptyOne) {
    const auto &c=publishedContentRegistry();auto p=makeNewAlphaProfile("nesting-contract",c);p.assets=AssetRegistry{};
    const auto rig=p.assets.create(c.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    const auto bag=p.assets.create(c.item(alpha_content::backpack),StoredAssetLocation{ProfileContainerId::stash(),{0,0}});
    const auto medicine=p.assets.create(c.item(alpha_content::bandage),StoredAssetLocation{ProfileContainerId::compartment(bag,0),{0,0}});
    ASSERT_TRUE(validateProfileState(p,c).valid);const auto before=profileStateFingerprint(p);
    const InventoryMoveCommand nest{bag,0,{ProfileContainerId::compartment(rig,0),{0,0}},ItemOrientation::Degrees0};
    EXPECT_FALSE(executeInventory(p,c,nest,{p.revision,"reject-nesting"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),before);
    ASSERT_TRUE(executeInventory(p,c,InventoryMoveCommand{medicine,0,{ProfileContainerId::stash(),{5,0}}},
        {p.revision,"empty-bag"}).succeeded);
    EXPECT_TRUE(executeInventory(p,c,nest,{p.revision,"nest-empty"}).succeeded);
    EXPECT_TRUE(validateProfileState(p,c).valid);
}
