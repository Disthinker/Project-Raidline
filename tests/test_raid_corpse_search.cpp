#include "raid_visibility_fixture.h"
#include "alpha_content_ids.h"
#include "game_session.h"
#include "raid_corpse_search.h"
#include "raid_lifecycle.h"
#include "raid_credential_legacy_fixture.h"
#include "raid_runner_legacy_fixture.h"
#include "save_repository.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

namespace
{
const auto &content = publishedContentRegistry();
std::vector<AssetInstanceId> corpses(const ProfileState &p)
{
    std::vector<AssetInstanceId> ids;
    for (const auto &[id, a] : p.assets.records()) if (std::holds_alternative<RaidCorpseContainerAssetLocation>(a.location)) ids.push_back(id);
    return ids;
}
void legacyCorpses(ProfileState &p)
{
    stripCredentialExtractionForLegacySnapshot(p);
    for (auto &loot : p.pendingRaid->loot) if (loot.corpseContainerAssetId)
    {
        p.assets.findMutable(loot.assetId)->location = RaidGroundAssetLocation{p.pendingRaid->raidId, loot.slotIndex};
        loot.corpseContainerAssetId = 0;
    }
    for (auto id : corpses(p)) static_cast<void>(p.assets.erase(id));
    for (auto &enemy : p.pendingRaid->enemies) enemy.sourceEnemyId.clear();
    p.pendingRaid->rulesVersion = "content-beta-loot-economy-29";
}
class RaidCorpseSearchTest : public testing::Test
{
protected:
    ProfileState p;
    AssetInstanceId bag{}, body{}, item{}, owned{};
    RaidCorpseDeathFact death;
    RaidSceneContainerAccess access;
    unsigned sequence{};
    void prepare(const ContentRegistry &deploymentContent)
    {
        p = makeNewAlphaProfile("corpse-contract", deploymentContent);
        bag = p.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}), EquippedAssetLocation{EquipmentSlotKind::Backpack});
        owned = p.assets.create(content.item(alpha_content::medkit), StoredAssetLocation{ProfileContainerId::compartment(bag,0), {0,0}});
        ASSERT_TRUE(executeDeploy(p, deploymentContent, {"corpse-raid", "corpse-settle", 9917, MapDefinitionId{"map.raid.frontier_exchange"}}, {p.revision,"deploy"}).succeeded);
        ASSERT_EQ(corpses(p).size(),3U); body = corpses(p).front();
        const auto &loc = std::get<RaidCorpseContainerAssetLocation>(p.assets.find(body)->location);
        death = {loc.sourceEnemyId, loc.spaceId, {1000,1000}};
        access = {body, loc.spaceId, death.position};
        access.observation=observeAssetContractPoint(p,loc.spaceId,death.position);
        access.currentObservation=access.observation->key();
        const auto items = assetsInContainer(p, ProfileContainerId::compartment(body,0)); ASSERT_EQ(items.size(),1U); item=items.front()->instanceId;
    }
    void SetUp() override { prepare(content); }
    void expose() { ASSERT_TRUE(revealRaidCorpse(p,content,death)); }
    InventoryReceipt move(AssetInstanceId id, ProfileContainerId target, GridPosition pos)
    {
        return executeInventory(p,content,InventoryMoveCommand{id,0,{target,pos}}, {p.revision,"corpse-move-"+std::to_string(++sequence)}, &access);
    }
    void take()
    {
        const auto *a=p.assets.find(item);
        const auto fit=findFirstProfileFit(p,content,ProfileContainerId::compartment(bag,0),content.item(a->definitionId),a->orientation,item);
        ASSERT_TRUE(fit); ASSERT_TRUE(move(item,ProfileContainerId::compartment(bag,0),*fit).succeeded);
    }
};
TEST_F(RaidCorpseSearchTest, ThreeOrdinaryFrozenOriginsHaveOneOwnerAcrossSeeds)
{
    for (auto seed : {1U,2U,7U,9917U})
    {
        auto a=makeNewAlphaProfile("same",content), b=a;
        ASSERT_TRUE(executeDeploy(a,content,{"r","s",seed,MapDefinitionId{"map.raid.frontier_exchange"}},{a.revision,"d"}).succeeded);
        ASSERT_TRUE(executeDeploy(b,content,{"r","s",seed,MapDefinitionId{"map.raid.frontier_exchange"}},{b.revision,"d"}).succeeded);
        EXPECT_EQ(a.pendingRaid->loot,b.pendingRaid->loot); EXPECT_EQ(a.pendingRaid->enemies,b.pendingRaid->enemies);
        EXPECT_EQ(corpses(a).size(),3U); std::size_t origins{};
        for (const auto &loot:a.pendingRaid->loot) if (loot.corpseContainerAssetId)
        {
            ++origins; EXPECT_EQ(raidSceneContainerRoot(a,loot.assetId),loot.corpseContainerAssetId);
            EXPECT_FALSE(loot.requiresHighRisk); EXPECT_EQ(loot.sceneContainerAssetId,0U);
            const auto &loc=std::get<RaidCorpseContainerAssetLocation>(a.assets.find(loot.corpseContainerAssetId)->location);
            EXPECT_FALSE(loc.deathPosition); EXPECT_FALSE(raidCorpseAccessible(a,loot.corpseContainerAssetId,loc.spaceId,{1000,1000}));
        }
        EXPECT_EQ(origins,3U); const auto rolls=a.pendingRaid->loot;
        EXPECT_EQ(a.pendingRaid->rulesVersion,kRaidRunnerRulesVersion);
        const auto historicalContent=contentBeforeRunner();
        auto historical=makeNewAlphaProfile("same",historicalContent);
        ASSERT_TRUE(executeDeploy(historical,historicalContent,{"r","s",seed,MapDefinitionId{"map.raid.frontier_exchange"}},{historical.revision,"d"}).succeeded);
        EXPECT_EQ(historical.pendingRaid->loot,rolls);
        legacyCorpses(historical); EXPECT_TRUE(validateProfileState(historical,content).valid);
        ASSERT_EQ(historical.pendingRaid->loot.size(),rolls.size());
        for (std::size_t i=0;i<rolls.size();++i) { EXPECT_EQ(historical.pendingRaid->loot[i].assetId,rolls[i].assetId); EXPECT_EQ(historical.pendingRaid->loot[i].definitionId,rolls[i].definitionId); EXPECT_EQ(historical.pendingRaid->loot[i].quantity,rolls[i].quantity); }
    }
}
TEST_F(RaidCorpseSearchTest, LiveUnknownAndInvalidDeathAreInaccessibleWithoutMutation)
{
    const auto hash=profileStateFingerprint(p);
    EXPECT_FALSE(queryInventory(p,content,InventoryMoveCommand{item,0,{ProfileContainerId::compartment(bag,0),{2,0}}},&access).canCommit);
    EXPECT_FALSE(revealRaidCorpse(p,content,{"unknown",death.spaceId,death.position}));
    EXPECT_FALSE(revealRaidCorpse(p,content,{death.sourceEnemyId,death.spaceId,{NAN,0}}));
    EXPECT_FALSE(revealRaidCorpse(p,content,{death.sourceEnemyId,RaidSpaceDefinitionId{"raid_space.unknown"},death.position}));
    EXPECT_FALSE(revealRaidCorpse(p,content,{death.sourceEnemyId,death.spaceId,{-1,0}}));
    EXPECT_EQ(profileStateFingerprint(p),hash);
}
TEST_F(RaidCorpseSearchTest, DeathOpensOnceAndRepeatedFactsCannotMoveOrReward)
{
    expose(); const auto hash=profileStateFingerprint(p), high=p.assets.nextAssetId();
    EXPECT_TRUE(revealRaidCorpse(p,content,{death.sourceEnemyId,death.spaceId,{1100,1100}}));
    EXPECT_EQ(profileStateFingerprint(p),hash); EXPECT_EQ(p.assets.nextAssetId(),high);
    EXPECT_TRUE(raidSceneContainerAccessible(p,access)); EXPECT_TRUE(validateProfileState(p,content).valid);
}
TEST_F(RaidCorpseSearchTest, RangeSpaceOtherRootAndWholeBodyRejectAtomically)
{
    expose(); const auto hash=profileStateFingerprint(p);
    const InventoryCommand command=InventoryMoveCommand{item,0,{ProfileContainerId::compartment(bag,0),{2,0}}};
    for(auto invalid : {RaidSceneContainerAccess{body,death.spaceId,{1072.01F,1000}},RaidSceneContainerAccess{body,RaidSpaceDefinitionId{"raid_space.wrong"},death.position},RaidSceneContainerAccess{corpses(p).back(),death.spaceId,death.position}})
        EXPECT_FALSE(executeInventory(p,content,command,{p.revision,"reject"},&invalid).succeeded);
    EXPECT_FALSE(move(body,ProfileContainerId::compartment(bag,0),{2,0}).succeeded);
    EXPECT_FALSE(move(item,ProfileContainerId::stash(),{2,0}).succeeded);
    EXPECT_FALSE(move(item,ProfileContainerId::compartment(corpses(p).back(),0),{2,0}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),hash);
    EXPECT_TRUE(raidCorpseAccessible(p,body,death.spaceId,{1072,1000}));
}
TEST_F(RaidCorpseSearchTest, TakePutBackEmptyReopenAndRetryPreserveInstances)
{
    expose(); take(); EXPECT_TRUE(assetIsCarried(p,item));
    EXPECT_TRUE(assetsInContainer(p,ProfileContainerId::compartment(body,0)).empty());
    const InventoryCommand put=InventoryMoveCommand{owned,0,{ProfileContainerId::compartment(body,0),{8,7}}};
    const auto rev=p.revision; ASSERT_TRUE(executeInventory(p,content,put,{rev,"put"},&access).succeeded);
    const auto hash=profileStateFingerprint(p);
    EXPECT_TRUE(executeInventory(p,content,put,{rev,"put"},&access).alreadyCommitted);
    EXPECT_EQ(profileStateFingerprint(p),hash); EXPECT_EQ(raidSceneContainerRoot(p,owned),body);
    EXPECT_TRUE(validateProfileState(p,content).valid);
}
TEST_F(RaidCorpseSearchTest, FullGridAndStaleRevisionRejectWithoutLoss)
{
    expose(); const auto carried=ProfileContainerId::compartment(bag,0);
    for(int y=0;y<6;++y) for(int x=0;x<9;++x) if(!profileAssetAtCell(p,content,carried,{x,y}))
        static_cast<void>(p.assets.create(content.item(alpha_content::medkit),StoredAssetLocation{carried,{x,y}}));
    const auto hash=profileStateFingerprint(p);
    EXPECT_FALSE(move(item,carried,{0,0}).succeeded);
    EXPECT_FALSE(executeInventory(p,content,InventoryMoveCommand{item,0,{carried,{0,0}}},{p.revision-1,"stale"},&access).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),hash); EXPECT_TRUE(p.assets.find(item));
}
TEST_F(RaidCorpseSearchTest, SettlementOnlyKeepsCarriedTreesAndExcludesCorpseDeposits)
{
    for(auto outcome : {RaidResultOutcome::Extracted,RaidResultOutcome::PlayerDead,RaidResultOutcome::ActiveQuit})
    {
        auto baseline=p; expose(); take(); ASSERT_TRUE(move(owned,ProfileContainerId::compartment(body,0),{8,7}).succeeded);
        const auto result=settlePendingRaid(p,content,"corpse-settle",outcome); ASSERT_TRUE(result.succeeded)<<result.message;
        EXPECT_TRUE(corpses(p).empty()); EXPECT_FALSE(p.assets.find(owned)); EXPECT_TRUE(p.assets.find(item));
        EXPECT_EQ(assetIsCarried(p,item),outcome==RaidResultOutcome::Extracted); EXPECT_TRUE(validateProfileState(p,content).valid);
        const auto hash=profileStateFingerprint(p); EXPECT_TRUE(settlePendingRaid(p,content,"corpse-settle",outcome).alreadyCommitted); EXPECT_EQ(profileStateFingerprint(p),hash);
        p=std::move(baseline);
    }
}
TEST_F(RaidCorpseSearchTest, AbnormalRecoveryRestoresExactRegistryAndHighWater)
{
    const auto original=*p.pendingRaid->startingAssets; expose(); take(); ASSERT_TRUE(move(owned,ProfileContainerId::compartment(body,0),{8,7}).succeeded);
    ASSERT_TRUE(rollbackPendingRaidToBase(p,content).succeeded);
    auto expected=p; expected.assets=original;
    EXPECT_EQ(profileStateFingerprint(p),profileStateFingerprint(expected)); EXPECT_EQ(p.assets.nextAssetId(),original.nextAssetId()); EXPECT_TRUE(corpses(p).empty());
}
TEST_F(RaidCorpseSearchTest, Schema52RoundTripAndLegacy51NeverRetrofitCorpses)
{
    expose(); const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,content.contentVersion()),content);
    ASSERT_TRUE(loaded.profile)<<loaded.message; EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));
    prepare(contentBeforeRunner()); expose();
    p.stashLayoutVersion=0;
    stripCredentialExtractionForLegacySnapshot(p);
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(p,content.contentVersion(),51)),std::invalid_argument);
    legacyCorpses(p); const auto old=deserializeProfileEnvelope(serializeProfileEnvelope(p,content.contentVersion(),51),content);
    ASSERT_TRUE(old.profile)<<old.message; EXPECT_TRUE(corpses(*old.profile).empty()); EXPECT_EQ(old.profile->pendingRaid->loot,p.pendingRaid->loot);
}
TEST_F(RaidCorpseSearchTest, TamperedDuplicateOriginsRootsAndSourcesAreRejected)
{
    const auto original=p;
    p.assets.findMutable(body)->location=RaidCorpseContainerAssetLocation{"wrong",death.spaceId,death.sourceEnemyId,std::nullopt}; EXPECT_FALSE(validateProfileState(p,content).valid); p=original;
    for(auto &e:p.pendingRaid->enemies) if(!e.sourceEnemyId.empty()) {e.sourceEnemyId="unknown";break;} EXPECT_FALSE(validateProfileState(p,content).valid);p=original;
    for(auto &l:p.pendingRaid->loot) if(l.corpseContainerAssetId) {l.corpseContainerAssetId=0;break;} EXPECT_FALSE(validateProfileState(p,content).valid);p=original;
    ASSERT_TRUE(p.assets.erase(body)); EXPECT_FALSE(validateProfileState(p,content).valid);
}
TEST_F(RaidCorpseSearchTest, ActualRuntimeConsumesOneDeathAtFinalPositionAndKeepsStableTargetIds)
{
    RaidWorldConfig config; config.worldSize={2000,2000}; config.playerSpawn={100,100}; config.extractionPoint={{1800,1800},{100,100}};
    EnemySpawn a{{700,700},{50,50},3}, b{{800,800},{50,50},3}; a.sourceEnemyId=death.sourceEnemyId;
    config.initialEnemies={a,b}; GameplayWorld world{config};
    const auto survivor=world.enemies().back().combatTargetId();
    static_cast<void>(const_cast<Enemy&>(world.enemies().front()).takeDamage(3)); world.update({},0.01F);
    ASSERT_EQ(world.pendingCorpseDeaths().size(),1U); EXPECT_FLOAT_EQ(world.pendingCorpseDeaths().front().position.x,725);
    EXPECT_EQ(world.enemies().front().combatTargetId(),survivor); world.update({},0.01F); EXPECT_EQ(world.pendingCorpseDeaths().size(),1U);
    world.acknowledgeCorpseDeath(death.sourceEnemyId); EXPECT_TRUE(world.pendingCorpseDeaths().empty()); world.update({},0.01F); EXPECT_TRUE(world.pendingCorpseDeaths().empty());
}
TEST_F(RaidCorpseSearchTest, PartialMergeSwapAndRotationUseTheSameCorpseAccess)
{
    expose();const auto root=ProfileContainerId::compartment(body,0),carried=ProfileContainerId::compartment(bag,0);
    const auto ammo=p.assets.create(content.item(alpha_content::ammunition),StoredAssetLocation{root,{8,6}},10);
    const auto target=p.assets.create(content.item(alpha_content::ammunition),StoredAssetLocation{carried,{7,5}},5);
    ASSERT_TRUE(executeInventory(p,content,InventoryMoveCommand{ammo,3,{carried,{7,5}}},{p.revision,"partial"},&access).succeeded);
    EXPECT_EQ(p.assets.find(ammo)->quantity,7U);EXPECT_EQ(p.assets.find(target)->quantity,8U);
    const auto swap=p.assets.create(content.item(alpha_content::bandage),StoredAssetLocation{root,{8,7}});
    ASSERT_TRUE(move(owned,root,{8,7}).succeeded);EXPECT_TRUE(assetIsCarried(p,swap));
    const auto gun=p.assets.create(content.item(alpha_content::rifle),StoredAssetLocation{root,{5,4}});
    const auto fit=findFirstProfileFit(p,content,root,content.item(alpha_content::rifle),ItemOrientation::Degrees90,gun);ASSERT_TRUE(fit);
    ASSERT_TRUE(executeInventory(p,content,InventoryMoveCommand{gun,0,{root,*fit},ItemOrientation::Degrees90},{p.revision,"rotate"},&access).succeeded);
    EXPECT_EQ(p.assets.find(gun)->orientation,ItemOrientation::Degrees90);EXPECT_TRUE(validateProfileState(p,content).valid);
}
TEST_F(RaidCorpseSearchTest, FailedFreezeAndDeathRevisionOverflowAreZeroModification)
{
    prepare(contentBeforeRunner());
    legacyCorpses(p);p.pendingRaid->rulesVersion=std::string{kRaidCorpseRulesVersion};
    for(auto &point:p.pendingRaid->spatialLayout.resourcePoints)if(point.kind==RaidResourcePointKind::Ordinary)point.kind=RaidResourcePointKind::HighValue;
    const auto hash=profileStateFingerprint(p);EXPECT_FALSE(freezeRaidCorpseLoot(p,content).valid);EXPECT_EQ(profileStateFingerprint(p),hash);
    // Retryable death facts remain in the runtime queue until accepted.
    p.revision=std::numeric_limits<ProfileRevision>::max();const auto over=profileStateFingerprint(p);
    EXPECT_FALSE(revealRaidCorpse(p,content,death));EXPECT_EQ(profileStateFingerprint(p),over);
}
TEST_F(RaidCorpseSearchTest, NonTrialEnemyDeathsHaveNoCorpseFact)
{
    RaidWorldConfig config;config.worldSize={2000,2000};config.playerSpawn={100,100};config.extractionPoint={{1800,1800},{100,100}};
    config.initialEnemies={EnemySpawn{{700,700},{50,50},3}};GameplayWorld world{config};
    static_cast<void>(const_cast<Enemy&>(world.enemies().front()).takeDamage(3));world.update({},0.01F);
    EXPECT_TRUE(world.pendingCorpseDeaths().empty());EXPECT_TRUE(world.enemies().empty());
}
TEST_F(RaidCorpseSearchTest, MalformedCodecDataIsRejectedAndRetryDoesNotReplaceOriginal)
{
    auto bad=p;std::get<RaidCorpseContainerAssetLocation>(bad.assets.findMutable(body)->location).sourceEnemyId="unknown";
    const auto result=deserializeProfileEnvelope(serializeProfileEnvelope(bad,content.contentVersion()),content);
    EXPECT_FALSE(result.profile);EXPECT_TRUE(validateProfileState(p,content).valid);
    for(auto &e:bad.pendingRaid->enemies)if(e.sourceEnemyId.empty()){e.sourceEnemyId=death.sourceEnemyId;break;}
    EXPECT_FALSE(validateProfileState(bad,content).valid);
}
}
