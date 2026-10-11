#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include "alpha_content_ids.h"
#include "game_session.h"
#include "raid_lifecycle.h"
#include "save_repository.h"

namespace {
const auto &content = publishedContentRegistry();
const MapDefinitionId frontier{"map.raid.frontier_exchange"};
std::uint64_t assetHash(const AssetRegistry &assets) {
    ProfileState p; p.assets = assets; return profileStateFingerprint(p);
}
std::vector<AssetInstanceId> boxes(const ProfileState &p) {
    std::vector<AssetInstanceId> result;
    for (const auto &[id,a] : p.assets.records())
        if (std::holds_alternative<RaidSceneContainerAssetLocation>(a.location)) result.push_back(id);
    return result;
}
class RaidSceneContainerTest : public testing::Test {
protected:
    ProfileState p;
    AssetInstanceId bag{}, owned{}, box{};
    RaidSceneContainerAccess access;
    std::uint64_t baseline{};
    unsigned sequence{};
    void SetUp() override {
        p = makeNewAlphaProfile("scene-contract", content);
        bag = p.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}), EquippedAssetLocation{EquipmentSlotKind::Backpack});
        owned = p.assets.create(content.item(alpha_content::medkit), StoredAssetLocation{ProfileContainerId::compartment(bag,0),{0,0}});
        baseline = assetHash(p.assets);
        ASSERT_TRUE(executeDeploy(p, content, {"scene-raid","scene-settle",9917,frontier},{p.revision,"scene-deploy"}).succeeded);
        const auto ids = boxes(p); ASSERT_EQ(ids.size(),2U); box=ids.front();
        const auto scene=std::get<RaidSceneContainerAssetLocation>(p.assets.find(box)->location);
        access = {box, scene.spaceId, scene.position};
    }
    InventoryReceipt move(AssetInstanceId id, ProfileContainerId target, GridPosition position, std::uint32_t quantity=0) {
        return executeInventory(p,content,InventoryMoveCommand{id,quantity,{target,position},ItemOrientation::Degrees0},
            {p.revision,"scene-move-"+std::to_string(++sequence)}, &access);
    }
    AssetInstanceId firstContent() { return assetsInContainer(p,ProfileContainerId::compartment(box,0)).front()->instanceId; }
    void take(AssetInstanceId id) {
        const auto *a=p.assets.find(id);
        const auto fit=findFirstProfileFit(p,content,ProfileContainerId::compartment(bag,0),content.item(a->definitionId),a->orientation,id);
        ASSERT_TRUE(fit); const auto r=move(id,ProfileContainerId::compartment(bag,0),*fit); ASSERT_TRUE(r.succeeded)<<r.message;
    }
};
TEST_F(RaidSceneContainerTest, FrozenRollsHaveOneOwnerAndNoAdditionalRewardsAcrossSeeds) {
    for (std::uint64_t seed=1; seed<=8; ++seed) {
        auto a=makeNewAlphaProfile("stable",content), b=a;
        ASSERT_TRUE(executeDeploy(a,content,{"r","s",seed,frontier},{a.revision,"d"}).succeeded);
        ASSERT_TRUE(executeDeploy(b,content,{"r","s",seed,frontier},{b.revision,"d"}).succeeded);
        EXPECT_EQ(profileStateFingerprint(a),profileStateFingerprint(b));
        EXPECT_EQ(a.pendingRaid->loot,b.pendingRaid->loot);
        EXPECT_EQ(boxes(a).size(),2U);
        std::size_t moved{};
        for (const auto &loot:a.pendingRaid->loot) if (loot.sceneContainerAssetId) {
            ++moved; EXPECT_EQ(raidSceneContainerRoot(a,loot.assetId),loot.sceneContainerAssetId);
            EXPECT_FALSE(std::holds_alternative<RaidGroundAssetLocation>(a.assets.find(loot.assetId)->location));
            EXPECT_FALSE(loot.collected);
        }
        EXPECT_EQ(moved,4U);
        auto legacy=a;
        for (auto &loot:legacy.pendingRaid->loot) if (loot.sceneContainerAssetId) {
            legacy.assets.findMutable(loot.assetId)->location=RaidGroundAssetLocation{legacy.pendingRaid->raidId,loot.slotIndex};
            loot.sceneContainerAssetId=0;
        }
        for (auto id:boxes(legacy)) ASSERT_TRUE(legacy.assets.erase(id));
        legacy.pendingRaid->startingAssets.reset();
        EXPECT_TRUE(validateProfileState(legacy,content).valid);
        EXPECT_EQ(legacy.pendingRaid->loot.size(),a.pendingRaid->loot.size());
        for (std::size_t i=0;i<a.pendingRaid->loot.size();++i) {
            EXPECT_EQ(legacy.pendingRaid->loot[i].quantity,a.pendingRaid->loot[i].quantity);
            EXPECT_EQ(legacy.pendingRaid->loot[i].definitionId,a.pendingRaid->loot[i].definitionId);
        }
        const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(legacy,content.contentVersion(),50),content);
        ASSERT_TRUE(loaded.profile)<<loaded.message; EXPECT_TRUE(boxes(*loaded.profile).empty());
        EXPECT_FALSE(loaded.profile->pendingRaid->startingAssets);
        EXPECT_EQ(loaded.profile->pendingRaid->loot,legacy.pendingRaid->loot);
    }
}
TEST_F(RaidSceneContainerTest, DistanceSpaceOtherBoxAndWholeRootRejectWithoutMutation) {
    const auto item=firstContent();
    const InventoryCommand c=InventoryMoveCommand{item,0,{ProfileContainerId::compartment(bag,0),{2,0}}};
    const auto hash=profileStateFingerprint(p);
    EXPECT_FALSE(queryInventory(p,content,c).canCommit);
    for (auto invalid : {RaidSceneContainerAccess{box,outdoorRaidSpaceId(),access.playerCenter},
         RaidSceneContainerAccess{box,access.spaceId,{access.playerCenter.x+72.01F,access.playerCenter.y}},
         RaidSceneContainerAccess{boxes(p).back(),access.spaceId,access.playerCenter},
         RaidSceneContainerAccess{box,access.spaceId,{NAN,0}}}) {
        EXPECT_FALSE(queryInventory(p,content,c,&invalid).canCommit);
        EXPECT_FALSE(executeInventory(p,content,c,{p.revision,"reject"},&invalid).succeeded);
        EXPECT_EQ(profileStateFingerprint(p),hash);
    }
    EXPECT_FALSE(move(item,ProfileContainerId::compartment(boxes(p).back(),0),{5,5}).succeeded);
    EXPECT_FALSE(move(box,ProfileContainerId::compartment(bag,0),{2,0}).succeeded);
    EXPECT_FALSE(move(item,ProfileContainerId::stash(),{0,0}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),hash);
    const auto exact=RaidSceneContainerAccess{box,access.spaceId,{access.playerCenter.x+72,access.playerCenter.y}};
    EXPECT_TRUE(raidSceneContainerAccessible(p,exact));
}
TEST_F(RaidSceneContainerTest, TakeDepositReopenAndRetryKeepTheSameInstances) {
    const auto item=firstContent(); take(item);
    ASSERT_TRUE(assetIsCarried(p,item));
    const InventoryCommand put=InventoryMoveCommand{owned,0,{ProfileContainerId::compartment(box,0),{8,7}}};
    const auto rev=p.revision;
    const auto r=executeInventory(p,content,put,{rev,"deposit-owned"},&access);
    ASSERT_TRUE(r.succeeded)<<r.message;
    EXPECT_FALSE(assetIsCarried(p,owned)); EXPECT_EQ(raidSceneContainerRoot(p,owned),box);
    const auto hash=profileStateFingerprint(p);
    EXPECT_TRUE(raidSceneContainerAccessible(p,access));
    EXPECT_EQ(profileStateFingerprint(p),hash);
    const auto retry=executeInventory(p,content,put,{rev,"deposit-owned"},&access);
    EXPECT_TRUE(retry.succeeded); EXPECT_TRUE(retry.alreadyCommitted); EXPECT_EQ(profileStateFingerprint(p),hash);
    EXPECT_TRUE(validateProfileState(p,content).valid);
}
TEST_F(RaidSceneContainerTest, PartialMergeSwapRotationAndCapacityRemainAtomic) {
    const auto root=ProfileContainerId::compartment(box,0), carried=ProfileContainerId::compartment(bag,0);
    const auto source=p.assets.create(content.item(alpha_content::ammunition),StoredAssetLocation{root,{9,6}},10);
    const auto target=p.assets.create(content.item(alpha_content::ammunition),StoredAssetLocation{carried,{8,5}},5);
    ASSERT_TRUE(move(source,carried,{8,5},3).succeeded);
    EXPECT_EQ(p.assets.find(source)->quantity,7U); EXPECT_EQ(p.assets.find(target)->quantity,8U);
    const auto swap=p.assets.create(content.item(alpha_content::bandage),StoredAssetLocation{root,{9,7}});
    ASSERT_TRUE(move(owned,root,{9,7}).succeeded);
    EXPECT_EQ(std::get<StoredAssetLocation>(p.assets.find(swap)->location).container,carried);
    const auto rotate=p.assets.create(content.item(alpha_content::rifle),StoredAssetLocation{root,{5,5}});
    const auto rotatedFit=findFirstProfileFit(p,content,root,content.item(alpha_content::rifle),ItemOrientation::Degrees90,rotate);
    ASSERT_TRUE(rotatedFit);
    const auto rotated=executeInventory(p,content,InventoryMoveCommand{rotate,0,{root,*rotatedFit},ItemOrientation::Degrees90},{p.revision,"rotate"},&access);
    ASSERT_TRUE(rotated.succeeded)<<rotated.message;
    EXPECT_EQ(p.assets.find(rotate)->orientation,ItemOrientation::Degrees90);
    const auto hash=profileStateFingerprint(p);
    EXPECT_FALSE(move(source,carried,{99,99}).succeeded);
    EXPECT_FALSE(executeInventory(p,content,InventoryMoveCommand{source,1,{carried,{0,0}}},{p.revision-1,"stale"},&access).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),hash);
    EXPECT_TRUE(validateProfileState(p,content).valid);
}
TEST_F(RaidSceneContainerTest, FullCarriedGridRejectsLargeTransferWithoutChangingAnyParticipant) {
    const auto carried=ProfileContainerId::compartment(bag,0);
    for (int y=0;y<6;++y) for (int x=0;x<9;++x)
        if (!profileAssetAtCell(p,content,carried,{x,y}))
            static_cast<void>(p.assets.create(content.item(alpha_content::medkit),StoredAssetLocation{carried,{x,y}}));
    const auto source=p.assets.create(content.item(alpha_content::rifle),StoredAssetLocation{ProfileContainerId::compartment(box,0),{5,5}});
    const auto hash=profileStateFingerprint(p);
    EXPECT_FALSE(move(source,carried,{0,0}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),hash);
    EXPECT_EQ(raidSceneContainerRoot(p,source),box);
}
TEST_F(RaidSceneContainerTest, SuccessfulReturnDiscardsDepositedAndUntakenAssetsOnly) {
    const auto item=firstContent(); take(item); ASSERT_TRUE(move(owned,ProfileContainerId::compartment(box,0),{8,7}).succeeded);
    const auto r=settlePendingRaid(p,content,"scene-settle",RaidResultOutcome::Extracted);
    ASSERT_TRUE(r.succeeded)<<r.message; EXPECT_TRUE(boxes(p).empty()); EXPECT_FALSE(p.assets.find(owned)); EXPECT_TRUE(assetIsCarried(p,item));
    EXPECT_TRUE(validateProfileState(p,content).valid);
    const auto hash=profileStateFingerprint(p);
    EXPECT_TRUE(settlePendingRaid(p,content,"scene-settle",RaidResultOutcome::Extracted).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p),hash);
}
TEST_F(RaidSceneContainerTest, FailureRecordsContainCarriedAssetsAndExcludeDeposits) {
    const auto item=firstContent(); take(item); ASSERT_TRUE(move(owned,ProfileContainerId::compartment(box,0),{8,7}).succeeded);
    for (const auto outcome : {RaidResultOutcome::PlayerDead,RaidResultOutcome::ActiveQuit}) {
        auto failed=p; const auto r=settlePendingRaid(failed,content,"scene-settle",outcome);
        ASSERT_TRUE(r.succeeded)<<r.message; EXPECT_TRUE(boxes(failed).empty()); EXPECT_FALSE(failed.assets.find(owned));
        const auto *record=failed.assets.find(item); ASSERT_NE(record,nullptr); EXPECT_FALSE(assetIsCarried(failed,item));
        EXPECT_TRUE(validateProfileState(failed,content).valid);
    }
}
TEST_F(RaidSceneContainerTest, Schema51RoundtripAndAbnormalRollbackRestoreExactOriginalAssetGraph) {
    take(firstContent()); ASSERT_TRUE(move(owned,ProfileContainerId::compartment(box,0),{8,7}).succeeded);
    auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,content.contentVersion()),content);
    ASSERT_TRUE(loaded.profile)<<loaded.message; EXPECT_EQ(profileStateFingerprint(p),profileStateFingerprint(*loaded.profile));
    EXPECT_THROW(serializeProfileEnvelope(p,content.contentVersion(),50),std::invalid_argument);
    const auto r=rollbackPendingRaidToBase(*loaded.profile,content); ASSERT_TRUE(r.succeeded)<<r.message;
    EXPECT_EQ(assetHash(loaded.profile->assets),baseline); EXPECT_TRUE(boxes(*loaded.profile).empty());
    EXPECT_TRUE(validateProfileState(*loaded.profile,content).valid);
}
TEST_F(RaidSceneContainerTest, InvalidFrozenRootsAndRollbackGraphFailClosed) {
    auto corrupt=p;
    corrupt.assets.findMutable(box)->location=RaidSceneContainerAssetLocation{"wrong",access.spaceId,access.playerCenter};
    EXPECT_FALSE(validateProfileState(corrupt,content).valid);
    corrupt=p; ASSERT_TRUE(corrupt.assets.erase(box)); EXPECT_FALSE(validateProfileState(corrupt,content).valid);
    corrupt=p; corrupt.pendingRaid->startingAssets->findMutable(owned)->quantity=0;
    EXPECT_FALSE(validateProfileState(corrupt,content).valid);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(corrupt,content.contentVersion()),content);
    EXPECT_FALSE(loaded.profile);
}
TEST(RaidSceneContainerSessionTest, RealSessionRejectsRemoteCommandsAndDoesNotExposeAnotherSpace) {
    GameSession session; ASSERT_TRUE(session.startNewProfile("scene-session")); ASSERT_TRUE(session.deployAlpha(9917,frontier));
    const auto id=boxes(session.profile()).front(); EXPECT_FALSE(session.raidSceneContainerAvailable(id)); EXPECT_FALSE(session.nearbyRaidSceneContainer());
    const auto hash=profileStateFingerprint(session.profile());
    const auto item=assetsInContainer(session.profile(),ProfileContainerId::compartment(id,0)).front()->instanceId;
    const InventoryCommand c=InventoryMoveCommand{item,0,{ProfileContainerId::stash(),{0,0}}};
    EXPECT_FALSE(session.queryRaidSceneContainerInventory(id,c).canCommit);
    EXPECT_FALSE(session.executeRaidSceneContainerInventory(id,c,"remote").succeeded);
    EXPECT_FALSE(session.executeProfileInventory(c,"raw-remote").succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()),hash);
}
}
