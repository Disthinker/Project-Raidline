#include "noise_decoy_domain.h"
#include "noise_decoy_fixture.h"
#include "raid_noise_decoy_runtime.h"
#include "alpha_content_ids.h"
#include "economy_domain.h"
#include "game_session.h"
#include "save_repository.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <random>

namespace {
const auto &content(){return publishedContentRegistry();}
const auto &use(){return *content().item(alpha_content::noiseDecoy).noiseDecoyUse;}
AssetInstanceId add(ProfileState &p,unsigned count=3,bool rig=true){
    equipDecoyTestRig(p,content());
    const auto chest=equippedAsset(p,EquipmentSlotKind::ChestRig);EXPECT_TRUE(chest);
    const auto &d=content().item(alpha_content::noiseDecoy);
    ProfileContainerId container=ProfileContainerId::stash();
    if(rig && chest){const auto &pockets=content().item(p.assets.find(*chest)->definitionId).containerCompartments;
        for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General){container=ProfileContainerId::compartment(*chest,n);break;}}
    const auto fit=findFirstProfileFit(p,content(),container,d,ItemOrientation::Degrees0);EXPECT_TRUE(fit);
    return fit ? p.assets.create(d,StoredAssetLocation{container,*fit},count):0;
}
RaidSpaceBlockerIndex index(std::vector<BallisticBlocker> walls={}){return *RaidSpaceBlockerIndex::build({2000,1200},walls);}
NoiseDecoyLanding landing(Vec2 point={550,250}){return {{100,250},point,outdoorRaidSpaceId(),use()};}
RaidWorldConfig config(int count=1){
    RaidWorldConfig c;c.noiseDecoyEnabled=true;c.worldSize={2000,1200};c.playerSpawn={100,100};
    c.extractionPoint={{1800,1050},{100,100}};c.playerMaximumHealth=c.playerCurrentHealth=100;
    for(int n=0;n<count;++n)c.initialEnemies.push_back({{700.0F+(n%10)*35,150.0F+(n/10)*45},{20,20},100});
    return c;
}
}

TEST(NoiseDecoyTest, DefinitionIsIndependentStackAndDoesNotEnterFreeGrant){
    const auto &d=content().item(alpha_content::noiseDecoy);ASSERT_TRUE(d.noiseDecoyUse);
    EXPECT_EQ(d.category,ItemCategory::Throwable);EXPECT_FALSE(d.weaponUse);EXPECT_FALSE(d.meleeUse);EXPECT_FALSE(d.battery);
    EXPECT_EQ(d.maxStackSize,3U);EXPECT_EQ(d.unitWeightGrams,250U);EXPECT_EQ(d.marketBuyPrice,40U);EXPECT_EQ(d.marketRecyclePrice,10U);
    auto p=makeNewPublishedProfile("no-grant",content());for(const auto &[id,a]:p.assets.records())EXPECT_NE(a.definitionId,alpha_content::noiseDecoy);
}
TEST(NoiseDecoyTest, ExactStackConsumptionAndReplayHaveOneEffect){
    auto p=makeNewPublishedProfile("consume",content());const auto id=add(p);ASSERT_TRUE(validateProfileState(p,content()).valid);
    const CommandContext ctx{p.revision,"one"};auto receipt=consumeNoiseDecoy(p,content(),id,ctx);ASSERT_TRUE(receipt.succeeded);
    EXPECT_EQ(p.assets.find(id)->quantity,2U);const auto after=profileStateFingerprint(p);
    EXPECT_TRUE(consumeNoiseDecoy(p,content(),id,ctx).alreadyCommitted);EXPECT_EQ(profileStateFingerprint(p),after);
    ASSERT_TRUE(consumeNoiseDecoy(p,content(),id,{p.revision,"two"}).succeeded);
    ASSERT_TRUE(consumeNoiseDecoy(p,content(),id,{p.revision,"three"}).succeeded);EXPECT_EQ(p.assets.find(id),nullptr);
    EXPECT_TRUE(validateProfileState(p,content()).valid);
}
TEST(NoiseDecoyTest, StaleEmptyMissingAndStashRejectWithoutMutation){
    auto p=makeNewPublishedProfile("reject",content());const auto id=add(p);const auto stored=add(p,1,false);const auto before=profileStateFingerprint(p);
    EXPECT_FALSE(consumeNoiseDecoy(p,content(),id,{p.revision+1,"stale"}).succeeded);
    EXPECT_FALSE(consumeNoiseDecoy(p,content(),id,{p.revision,""}).succeeded);
    EXPECT_FALSE(consumeNoiseDecoy(p,content(),999999,{p.revision,"missing"}).succeeded);
    EXPECT_FALSE(consumeNoiseDecoy(p,content(),stored,{p.revision,"stash"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(p),before);EXPECT_EQ(chestRigNoiseDecoys(p,content()),std::vector<AssetInstanceId>{id});
}
TEST(NoiseDecoyTest, UnequippedRigCannotSupplyThrowable){
    auto p=makeNewPublishedProfile("unequip",content());const auto id=add(p);const auto rig=*equippedAsset(p,EquipmentSlotKind::ChestRig);
    const auto &d=content().item(p.assets.find(rig)->definitionId);const auto fit=findFirstProfileFit(p,content(),ProfileContainerId::stash(),d,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    ASSERT_TRUE(executeInventory(p,content(),InventoryMoveCommand{rig,0,{ProfileContainerId::stash(),*fit}},{p.revision,"store-rig"}).succeeded);
    EXPECT_FALSE(noiseDecoyInChestRig(p,content(),id));const auto before=profileStateFingerprint(p);
    EXPECT_FALSE(consumeNoiseDecoy(p,content(),id,{p.revision,"unequipped"}).succeeded);EXPECT_EQ(profileStateFingerprint(p),before);
}
TEST(NoiseDecoyTest, Schema54RoundTripKeepsGenericItemAndOldProfileGetsNoGrant){
    auto p=makeNewPublishedProfile("save54",content());p.stashLayoutVersion=0;const auto id=add(p,2);
    const auto rig=*equippedAsset(p,EquipmentSlotKind::ChestRig);
    p.assets.findMutable(id)->location=StoredAssetLocation{ProfileContainerId::compartment(rig,0),{2,0}};
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,content().contentVersion(),54),content());ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));EXPECT_EQ(loaded.profile->assets.find(id)->quantity,2U);
    auto old=makeNewPublishedProfile("old54",content());old.stashLayoutVersion=0;const auto legacy=deserializeProfileEnvelope(serializeProfileEnvelope(old,"quick-melee-self-defense-content-66",54),content());ASSERT_TRUE(legacy.profile)<<legacy.message;
    EXPECT_EQ(profileStateFingerprint(old),profileStateFingerprint(*legacy.profile));for(const auto &[key,a]:legacy.profile->assets.records())EXPECT_NE(a.definitionId,alpha_content::noiseDecoy);
}
TEST(NoiseDecoyTest, PurchaseAndRecycleUseExistingPricesWithoutFreeRestock){
    auto p=makeNewPublishedProfile("economy",content());const auto money=p.currency;
    const auto receipt=executeEconomy(p,content(),PurchaseCommand{alpha_content::noiseDecoy,3},{p.revision,"buy"});ASSERT_TRUE(receipt.succeeded)<<receipt.message;
    EXPECT_EQ(p.currency,money-120);AssetInstanceId id{};for(const auto &[key,a]:p.assets.records())if(a.definitionId==alpha_content::noiseDecoy)id=key;
    ASSERT_NE(id,0U);EXPECT_EQ(p.assets.find(id)->quantity,3U);
    ASSERT_TRUE(executeEconomy(p,content(),RecycleCommand{id},{p.revision,"recycle"}).succeeded);EXPECT_EQ(p.currency,money-90);EXPECT_EQ(p.assets.find(id),nullptr);
}
TEST(NoiseDecoyTest, LandingClampsDistanceWorldEdgeAndFirstWall){
    auto clear=index();auto p=queryNoiseDecoyLanding({100,250},{1800,250},outdoorRaidSpaceId(),use(),clear);ASSERT_TRUE(p);EXPECT_FLOAT_EQ(p->point.x,550);
    p=queryNoiseDecoyLanding({1900,250},{2200,250},outdoorRaidSpaceId(),use(),clear);ASSERT_TRUE(p);EXPECT_FLOAT_EQ(p->point.x,1996);
    auto blocked=index({{1,{{300,0},{20,1000}}},{2,{{500,0},{20,1000}}}});
    p=queryNoiseDecoyLanding({100,250},{800,250},outdoorRaidSpaceId(),use(),blocked);ASSERT_TRUE(p);EXPECT_LT(p->point.x,300.0F);EXPECT_GT(p->point.x,292.0F);EXPECT_TRUE(blocked.hasLineOfSight(p->origin,p->point));
}
TEST(NoiseDecoyTest, InvalidAndInsideWallLandingCannotStart){
    const auto walls=index({{1,{{90,200},{40,100}}}});
    EXPECT_FALSE(queryNoiseDecoyLanding({100,250},{800,250},outdoorRaidSpaceId(),use(),walls));
    EXPECT_FALSE(queryNoiseDecoyLanding({100,250},{100,250},outdoorRaidSpaceId(),use(),index()));
    EXPECT_FALSE(queryNoiseDecoyLanding({-1,250},{100,250},outdoorRaidSpaceId(),use(),index()));
    EXPECT_FALSE(queryNoiseDecoyLanding({100,250},{std::numeric_limits<float>::quiet_NaN(),250},outdoorRaidSpaceId(),use(),index()));
}
TEST(NoiseDecoyTest, HalfSecondFlightAndSixImmediateThenOneSecondPulses){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));EXPECT_FALSE(runtime.start(landing()));
    runtime.advance(outdoorRaidSpaceId(),.49F);EXPECT_TRUE(runtime.flight());EXPECT_FALSE(runtime.source());
    runtime.advance(outdoorRaidSpaceId(),.01F);ASSERT_TRUE(runtime.source());EXPECT_FALSE(runtime.flight());EXPECT_EQ(runtime.pulsesLastUpdate(),1U);
    unsigned pulses=1;for(int n=0;n<5;++n){runtime.advance(outdoorRaidSpaceId(),1);pulses+=runtime.pulsesLastUpdate();}
    EXPECT_EQ(pulses,6U);EXPECT_TRUE(runtime.source());runtime.advance(outdoorRaidSpaceId(),1);EXPECT_FALSE(runtime.source());EXPECT_EQ(runtime.pulsesLastUpdate(),0U);
}
TEST(NoiseDecoyTest, InactiveSpacePauseAndInvalidTimeCannotAdvance){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));const RaidSpaceDefinitionId other{"space.test.other"};
    runtime.advance(other,100);ASSERT_TRUE(runtime.flight());EXPECT_DOUBLE_EQ(runtime.flight()->elapsed,0);
    runtime.advance(outdoorRaidSpaceId(),0);runtime.advance(outdoorRaidSpaceId(),-1);runtime.advance(outdoorRaidSpaceId(),std::numeric_limits<float>::infinity());EXPECT_FALSE(runtime.source());
    runtime.advance(outdoorRaidSpaceId(),.5F);const auto age=runtime.source()->elapsed;runtime.advance(other,100);EXPECT_DOUBLE_EQ(runtime.source()->elapsed,age);EXPECT_EQ(runtime.pulsesLastUpdate(),0U);
}
TEST(NoiseDecoyTest, LargeTimeStepHasAtMostSixPulsesAndNoRemainingSource){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),100);
    EXPECT_EQ(runtime.pulsesLastUpdate(),6U);EXPECT_FALSE(runtime.source());EXPECT_FALSE(runtime.flight());
}
TEST(NoiseDecoyTest, HearingUsesRadiusOcclusionAndEligibilityWithoutGroupPropagation){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),.5F);
    const std::vector<NoiseDecoyListener> listeners{{1,{550,750},{600,750},true},{2,{550,750.1F},{},true},{3,{700,250},{},false},{4,{800,250},{},true},{5,{550,350},{},true}};
    runtime.hearPulses(listeners,index({{1,{{700,0},{20,600}}}}));
    EXPECT_TRUE(runtime.investigations().contains(1));EXPECT_FALSE(runtime.investigations().contains(2));EXPECT_FALSE(runtime.investigations().contains(3));EXPECT_FALSE(runtime.investigations().contains(4));EXPECT_TRUE(runtime.investigations().contains(5));
    const auto goal=runtime.investigationGoal(1,outdoorRaidSpaceId(),{550,750});ASSERT_TRUE(goal);EXPECT_FLOAT_EQ(goal->x,550);EXPECT_FLOAT_EQ(goal->y,250);
}
TEST(NoiseDecoyTest, ExpiryReleasesToOriginalPostAndArrivalPrunesBinding){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),.5F);
    std::vector<NoiseDecoyListener> listeners{{7,{650,250},{900,250},true}};runtime.hearPulses(listeners,index());runtime.advance(outdoorRaidSpaceId(),6);
    auto goal=runtime.investigationGoal(7,outdoorRaidSpaceId(),{600,250});ASSERT_TRUE(goal);EXPECT_FLOAT_EQ(goal->x,900);
    EXPECT_FALSE(runtime.investigationGoal(7,outdoorRaidSpaceId(),{900,250}));EXPECT_TRUE(runtime.investigations().empty());
}
TEST(NoiseDecoyTest, NewLandingReplacesOldSpaceAndRetiresItsInvestigations){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),.5F);
    runtime.hearPulses(std::vector<NoiseDecoyListener>{{1,{600,250},{900,250},true}},index());
    auto next=landing({900,400});next.space=RaidSpaceDefinitionId{"space.test.room"};ASSERT_TRUE(runtime.start(next));
    const auto old=runtime.source()->id;runtime.advance(next.space,.5F);ASSERT_TRUE(runtime.source());EXPECT_GT(runtime.source()->id,old);EXPECT_EQ(runtime.source()->landing.space,next.space);
    EXPECT_TRUE(runtime.investigations().at(1).returning);EXPECT_FALSE(runtime.investigationGoal(1,next.space,{600,250}));
}
TEST(NoiseDecoyTest, RemovedIdentityCannotRemainATargetOrClearOtherSpace){
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),.5F);
    runtime.hearPulses(std::vector<NoiseDecoyListener>{{1,{600,250},{900,250},true},{4,{650,250},{900,250},true}},index());
    runtime.prune(outdoorRaidSpaceId(),std::vector<CombatTargetId>{4});EXPECT_FALSE(runtime.investigations().contains(1));EXPECT_TRUE(runtime.investigations().contains(4));
    runtime.forget(4);EXPECT_TRUE(runtime.investigations().empty());runtime.clear();EXPECT_FALSE(runtime.source());EXPECT_FALSE(runtime.flight());
}
TEST(NoiseDecoyTest, RealWorldInvestigatesLandingWithoutGrantingPlayerAwareness){
    GameplayWorld world(config());const auto id=world.enemies().front().combatTargetId();const auto before=world.enemies().front().position();
    const auto plan=world.previewNoiseDecoy({550,160},use());ASSERT_TRUE(plan);RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));
    world.update({},.5F);ASSERT_TRUE(world.noiseDecoy().source());ASSERT_TRUE(world.noiseDecoy().investigations().contains(id));
    EXPECT_LT(world.enemies().front().position().x,before.x);EXPECT_EQ(world.enemies().front().awarenessState(),EnemyAwarenessState::Unaware);EXPECT_FALSE(world.enemies().front().lastKnownTargetPosition());
    const auto point=world.noiseDecoy().investigations().at(id).point;GameplayInput move;move.moveDown=true;world.update(move,.1F);
    EXPECT_FLOAT_EQ(world.noiseDecoy().investigations().at(id).point.x,point.x);EXPECT_FLOAT_EQ(world.noiseDecoy().investigations().at(id).point.y,point.y);
}
TEST(NoiseDecoyTest, RealVisualDiscoveryWinsOverDecoyAndNoImmediateDisengage){
    auto c=config();c.playerSpawn={500,100};GameplayWorld world(c);const auto plan=world.previewNoiseDecoy({900,120},use());ASSERT_TRUE(plan);
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));world.update({},.5F);
    EXPECT_EQ(world.enemies().front().awarenessState(),EnemyAwarenessState::Alerted);EXPECT_TRUE(world.noiseDecoy().investigations().empty());
}
TEST(NoiseDecoyTest, DisabledActivitiesRejectBeforePreparingRuntime){
    auto c=config();c.noiseDecoyEnabled=false;GameplayWorld baseLike(c);EXPECT_FALSE(baseLike.previewNoiseDecoy({500,250},use()));
    c.noiseDecoyEnabled=true;c.developerRange=true;GameplayWorld range(c);EXPECT_FALSE(range.previewNoiseDecoy({500,250},use()));
}

struct NoiseDecoyTestAccess {
    static AssetInstanceId stage(GameSession &s){
        s.profile_=makeNewPublishedProfile("service-decoy",content());s.profile_.tutorial=TutorialProgress::Complete;
        const auto id=add(s.profile_);s.state_=GameSessionState::BetweenRaids;EXPECT_TRUE(s.deployAlpha(19))<<s.persistenceMessage();
        s.world_=std::make_unique<GameplayWorld>(config());return id;
    }
    static bool realDeployment(GameSession &s,const MapDefinitionId &map){
        s.profile_=makeNewPublishedProfile("production-decoy",content());s.profile_.tutorial=TutorialProgress::Complete;
        s.state_=GameSessionState::BetweenRaids;return s.deployAlpha(42,map);
    }
    static std::size_t pressure(GameSession &s){return s.world_->spawnHighRiskPressureWave();}
    static void removeActor(GameplayWorld &world,CombatTargetId id){
        for(auto &enemy:world.enemies_)if(enemy.combatTargetId()==id)static_cast<void>(enemy.takeDamage(enemy.health(),EnemyImpactReaction::None));
        const auto removals=world.enemies_.removeDead();world.consumeEnemyRemovals(removals);
    }
    static bool responsive(const GameSession &s,CombatTargetId id){return s.world_->enemies_.state(id).noiseResponsive;}
    static void action(GameSession &s,RaidAction action){ASSERT_TRUE(s.raidActionState_.start(std::move(action)));}
};
TEST(NoiseDecoyTest, ServiceCommitsExactItemAndOneFlightAtomicallyAndReplayDoesNotSpawn){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);const auto before=session.profile().assets.find(id)->quantity;
    const CommandContext ctx{session.profile().revision,"service-throw"};const auto result=session.throwNoiseDecoy(id,{550,160},ctx);ASSERT_TRUE(result.succeeded)<<result.message;
    EXPECT_EQ(session.profile().assets.find(id)->quantity,before-1);ASSERT_TRUE(session.world().noiseDecoy().flight());
    EXPECT_TRUE(session.throwNoiseDecoy(id,{900,600},ctx).alreadyCommitted);EXPECT_FLOAT_EQ(session.world().noiseDecoy().flight()->landing.point.x,550);
    const auto fingerprint=profileStateFingerprint(session.profile());EXPECT_FALSE(session.throwNoiseDecoy(id,{550,160},{session.profile().revision,"second"}).succeeded);EXPECT_EQ(profileStateFingerprint(session.profile()),fingerprint);
}
TEST(NoiseDecoyTest, ServiceInvalidAimAndStaleRevisionChangeNeitherAssetsNorSource){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);const auto before=profileStateFingerprint(session.profile());
    const auto p=session.world().player().position();const float half=session.world().player().size()/2;
    EXPECT_FALSE(session.throwNoiseDecoy(id,{p.x+half,p.y+half},{session.profile().revision,"empty-ray"}).succeeded);
    EXPECT_FALSE(session.throwNoiseDecoy(id,{550,160},{session.profile().revision+1,"stale"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()),before);EXPECT_FALSE(session.world().noiseDecoy().flight());EXPECT_FALSE(session.world().noiseDecoy().source());
}
TEST(NoiseDecoyTest, SelectionFirstCancelsExistingActionAndRequiresAnotherSelection){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);const auto before=session.profile().assets.find(id)->quantity;
    NoiseDecoyTestAccess::action(session,ReloadRaidAction{});EXPECT_FALSE(session.beginNoiseDecoySelection());EXPECT_TRUE(session.beginNoiseDecoySelection());EXPECT_EQ(session.profile().assets.find(id)->quantity,before);
}
TEST(NoiseDecoyTest, ThrowModalAndFlightBlockCombatWithoutAddingMeleeRecovery){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);GameplayInput input;input.throwableModal=true;input.firePressed=input.fireJustPressed=input.meleePressed=input.meleeJustPressed=input.sprint=true;
    session.update(input,0);EXPECT_FALSE(session.world().shotFiredLastUpdate());EXPECT_FALSE(session.world().melee().result().attempted);
    ASSERT_TRUE(session.throwNoiseDecoy(id,{550,160},{session.profile().revision,"block"}).succeeded);input.throwableModal=false;
    session.update(input,.1F);EXPECT_FALSE(session.world().shotFiredLastUpdate());EXPECT_FALSE(session.world().melee().result().attempted);EXPECT_EQ(session.world().melee().recoverySeconds(),0);
}
TEST(NoiseDecoyTest, ConsumedItemAndRemainingStackKeepFrozenPendingValidAndRoundTrip){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);ASSERT_TRUE(session.throwNoiseDecoy(id,{550,160},{session.profile().revision,"persist"}).succeeded);
    EXPECT_TRUE(validateProfileState(session.profile(),content()).valid);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(session.profile(),content().contentVersion()),content());ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(loaded.profile->assets.find(id)->quantity,2U);EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(session.profile()));
}
TEST(NoiseDecoyTest, BatchedHearingMatchesCanonicalSightAcrossGeometryAndBoundaryCases){
    std::mt19937 random(20261017);
    std::uniform_int_distribution<int> x(-100,2100),y(-100,1300),extent(2,160),offset(-600,600);
    std::vector<BallisticBlocker> walls{{1,{{750,0},{30,1200}}},{2,{{0,640},{2000,12}}}};
    for(int n=0;n<220;++n)walls.push_back({static_cast<std::uint64_t>(n+3),
        {{static_cast<float>(x(random)),static_cast<float>(y(random))},
         {static_cast<float>(extent(random)),static_cast<float>(extent(random))}}});
    std::size_t audible{},occluded{};
    for(float cellSize:{64.0F,128.0F,256.0F}){
        const auto geometry=RaidSpaceBlockerIndex::build({2000,1200},walls,cellSize);ASSERT_TRUE(geometry);
        for(Vec2 source:std::vector<Vec2>{{500,500},{128,128},{1996,1196},{4,4},{1000,250},{256,768}}){
            SCOPED_TRACE(::testing::Message()<<"cell="<<cellSize<<" source="<<source.x<<','<<source.y);
            RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing(source)));runtime.advance(outdoorRaidSpaceId(),.5F);
            std::vector<NoiseDecoyListener> listeners;
            const auto addListener=[&](Vec2 center,bool eligible=true){
                const auto id=static_cast<CombatTargetId>(listeners.size()+1);
                listeners.push_back({id,center,{static_cast<float>(id),42},eligible});
            };
            for(int n=0;n<600;++n)addListener({source.x+offset(random),source.y+offset(random)},n%11!=0);
            for(Vec2 delta:std::vector<Vec2>{{0,0},{500,0},{500.00006F,0},{500.01F,0},{-500,0},{0,500},{0,-500}})
                addListener({source.x+delta.x,source.y+delta.y});
            // Include exact cell and wall boundaries, including sources inside cover.
            for(const auto &wall:walls){addListener(wall.bounds.position);
                addListener({wall.bounds.position.x+wall.bounds.size.x,wall.bounds.position.y+wall.bounds.size.y});}
            addListener({std::numeric_limits<float>::quiet_NaN(),source.y});
            listeners.push_back({0,source,{},true});
            runtime.hearPulses(listeners,*geometry);
            const float radius=use().radius;
            for(const auto &listener:listeners){
                if(!listener.id)continue;
                const auto p=listener.center;
                const bool finite=std::isfinite(p.x)&&std::isfinite(p.y);
                const bool binned=finite && std::floor(p.x/128)>=std::floor((source.x-radius)/128) &&
                    std::floor(p.x/128)<=std::floor((source.x+radius)/128) &&
                    std::floor(p.y/128)>=std::floor((source.y-radius)/128) &&
                    std::floor(p.y/128)<=std::floor((source.y+radius)/128);
                const bool inRange=listener.eligible && binned && std::hypot(p.x-source.x,p.y-source.y)<=radius+.0001F;
                const bool expected=inRange && geometry->hasLineOfSight(source,p);
                const auto actual=runtime.investigations().find(listener.id);
                ASSERT_EQ(actual!=runtime.investigations().end(),expected)<<"listener="<<listener.id;
                if(inRange){if(expected)++audible;else ++occluded;}
                if(expected){const auto &fact=actual->second;EXPECT_EQ(fact.space,outdoorRaidSpaceId());
                    EXPECT_EQ(fact.source,runtime.source()->id);EXPECT_FALSE(fact.returning);
                    EXPECT_FLOAT_EQ(fact.point.x,source.x);EXPECT_FLOAT_EQ(fact.point.y,source.y);
                    EXPECT_FLOAT_EQ(fact.home.x,listener.home.x);EXPECT_FLOAT_EQ(fact.home.y,listener.home.y);}
            }
            EXPECT_FALSE(runtime.investigations().contains(0));
        }
    }
    EXPECT_GT(audible,0U);EXPECT_GT(occluded,0U);
}
TEST(NoiseDecoyTest, HearingUsesCurrentGeometryOnEveryPulse){
    const auto blocked=index({{1,{{650,0},{20,1200}}}}),clear=index();
    const std::vector<NoiseDecoyListener> listeners{{1,{800,250},{900,250},true}};
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(landing()));runtime.advance(outdoorRaidSpaceId(),.5F);
    runtime.hearPulses(listeners,blocked);EXPECT_TRUE(runtime.investigations().empty());
    runtime.advance(outdoorRaidSpaceId(),1);runtime.hearPulses(listeners,clear);ASSERT_TRUE(runtime.investigations().contains(1));
    runtime.forget(1);runtime.advance(outdoorRaidSpaceId(),1);runtime.hearPulses(listeners,blocked);EXPECT_TRUE(runtime.investigations().empty());
    ASSERT_TRUE(runtime.start(landing({900,250})));runtime.advance(outdoorRaidSpaceId(),.5F);
    runtime.hearPulses(listeners,blocked);ASSERT_TRUE(runtime.investigations().contains(1));
    EXPECT_FLOAT_EQ(runtime.investigations().at(1).point.x,900);
}
TEST(NoiseDecoyTest, RealPulseAndReturnNavigationKeepThreeMillisecondBudgetFor32And100){
    using Clock=std::chrono::steady_clock;
    for(int count:{32,100}){
        auto c=config(count);
        for(int n=0;n<1800 && c.ballisticBlockers.size()<220;++n){
            const Rect wall{{540.0F+(n%30)*16,230.0F+(n/30)*16},{4,4}};
            const bool occupied=std::any_of(c.initialEnemies.begin(),c.initialEnemies.end(),[&](const auto &e){
                return wall.position.x<e.position.x+e.size.x && wall.position.x+wall.size.x>e.position.x &&
                    wall.position.y<e.position.y+e.size.y && wall.position.y+wall.size.y>e.position.y;});
            if(!occupied)c.ballisticBlockers.push_back({static_cast<std::uint64_t>(n+1),wall});
        }
        ASSERT_EQ(c.ballisticBlockers.size(),220U);
        const auto geometry=RaidSpaceBlockerIndex::build(c.worldSize,c.ballisticBlockers);ASSERT_TRUE(geometry);
        std::size_t blocked{},wallTests{};
        for(const auto &e:c.initialEnemies)if(!geometry->hasLineOfSight({550,160},{e.position.x+e.size.x/2,e.position.y+e.size.y/2},&wallTests))++blocked;
        EXPECT_GT(blocked,0U);EXPECT_GT(wallTests,0U); // Dense cover is inside the real sound queries.
        GameplayWorld world(c);std::vector<double> pulseSamples,returnSamples;
        for(int n=0;n<160;++n){
            RaidNoiseDecoyRuntime runtime;const auto plan=world.previewNoiseDecoy({550,160},use());ASSERT_TRUE(plan);ASSERT_TRUE(runtime.start(*plan));runtime.advance(outdoorRaidSpaceId(),.49F);world.commitNoiseDecoy(std::move(runtime));
            const auto start=Clock::now();world.update({},1.0F/60.0F);const auto pulse=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            auto ending=world.noiseDecoy();ASSERT_TRUE(ending.source());ending.advance(outdoorRaidSpaceId(),static_cast<float>(5.99-ending.source()->elapsed));world.commitNoiseDecoy(std::move(ending));
            const auto second=Clock::now();world.update({},1.0F/60.0F);const auto returning=std::chrono::duration<double,std::milli>(Clock::now()-second).count();
            if(n>=10){pulseSamples.push_back(pulse);returnSamples.push_back(returning);}
        }
        for(int phase=0;phase<2;++phase){auto sorted=phase==0?pulseSamples:returnSamples;std::sort(sorted.begin(),sorted.end());ASSERT_EQ(sorted.size(),150U);
            const std::string name=phase==0?"pulse":"return";
            RecordProperty("decoy_"+std::to_string(count)+"_"+name+"_p95_ms",std::to_string(sorted[142]));
            EXPECT_LT(sorted[142],3.0)<<"enemies="<<count<<" phase="<<name<<" samples="<<sorted.size();}
        if(const char *dir=std::getenv("RAIDLINE_DECOY_QA_DIR")){std::filesystem::create_directories(dir);std::ofstream out(std::filesystem::path(dir)/("decoy-"+std::to_string(count)+".csv"));out<<"pulse_ms,return_ms\n";for(std::size_t i=0;i<pulseSamples.size();++i)out<<pulseSamples[i]<<','<<returnSamples[i]<<'\n';}
    }
}

TEST(NoiseDecoyTest, PublishedFrontierAndHospitalEnableOrdinaryInitialEnemiesOnly){
    for(const char *name:{"map.raid.frontier_exchange","map.raid.hospital_district"}){
        GameSession session;ASSERT_TRUE(NoiseDecoyTestAccess::realDeployment(session,MapDefinitionId{name}))<<session.persistenceMessage();
        EXPECT_TRUE(session.world().noiseDecoyEnabled());for(const auto &e:session.world().enemies())EXPECT_TRUE(NoiseDecoyTestAccess::responsive(session,e.combatTargetId()));
        if(std::string_view{name}=="map.raid.frontier_exchange"){
            const auto initial=session.world().enemies().size();ASSERT_GT(NoiseDecoyTestAccess::pressure(session),0U);
            for(std::size_t n=initial;n<session.world().enemies().size();++n)EXPECT_FALSE(NoiseDecoyTestAccess::responsive(session,session.world().enemies()[n].combatTargetId()));
        }
    }
}
TEST(NoiseDecoyTest, ContentRejectsWrongCategoryNegativeRangeAndMixedCapabilities){
    const auto original=nlohmann::json::parse(publishedContentJson());
    for(int mode=0;mode<5;++mode){auto data=original;auto &d=data["items"][0];ASSERT_EQ(d["id"],"item.throwable.noise_decoy");
        if(mode==0)d["category"]="weapon";
        if(mode==1)d["noise_decoy_use"]["range"]=-1;
        if(mode==2)d["noise_decoy_use"]["pulse_interval_seconds"]=0;
        if(mode==3)d["max_stack_size"]=4;
        if(mode==4)d["maximum_charges"]=1;
        EXPECT_THROW(static_cast<void>(ContentRegistry::fromJson(data.dump())),ContentRegistryError)<<mode;
    }
}

TEST(NoiseDecoyTest, SearchingEnemyKeepsPlayerMemorySeparateFromInvestigatedPoint){
    auto c=config();c.playerSpawn={100,234};c.initialEnemies.front().position={500,234};
    c.ballisticBlockers={{1,{{300,180},{30,500}}}};GameplayWorld world(c);
    auto &enemy=const_cast<Enemy&>(world.enemies().front());enemy.hearTarget({520,500});world.update({},.01F);
    ASSERT_EQ(enemy.awarenessState(),EnemyAwarenessState::Searching);const auto memory=enemy.lastKnownTargetPosition();ASSERT_TRUE(memory);
    const auto plan=world.previewNoiseDecoy({350,100},use());ASSERT_TRUE(plan);RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));world.update({},.5F);
    ASSERT_TRUE(world.noiseDecoy().investigations().contains(enemy.combatTargetId()));ASSERT_TRUE(enemy.lastKnownTargetPosition());
    EXPECT_FLOAT_EQ(enemy.lastKnownTargetPosition()->x,memory->x);EXPECT_FLOAT_EQ(enemy.lastKnownTargetPosition()->y,memory->y);
}
TEST(NoiseDecoyTest, RealIdleEnemyReturnsToOriginalPostAfterSourceEnds){
    GameplayWorld world(config());const auto home=world.enemies().front().position();const auto plan=world.previewNoiseDecoy({550,160},use());ASSERT_TRUE(plan);
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));
    for(int n=0;n<600;++n)world.update({},1.0F/60.0F);
    EXPECT_FALSE(world.noiseDecoy().source());EXPECT_TRUE(world.noiseDecoy().investigations().empty());
    EXPECT_LT(std::hypot(world.enemies().front().position().x-home.x,world.enemies().front().position().y-home.y),25);
    EXPECT_EQ(world.enemies().front().awarenessState(),EnemyAwarenessState::Unaware);
}
TEST(NoiseDecoyTest, ActiveQuitClearsTransientSourceAndRetainsExistingFailurePolicy){
    GameSession session;const auto id=NoiseDecoyTestAccess::stage(session);ASSERT_TRUE(session.throwNoiseDecoy(id,{550,160},{session.profile().revision,"quit"}).succeeded);
    ASSERT_TRUE(session.activeQuitAlphaRaid())<<session.persistenceMessage();EXPECT_FALSE(session.world().noiseDecoy().flight());EXPECT_FALSE(session.world().noiseDecoy().source());EXPECT_TRUE(session.world().noiseDecoy().investigations().empty());
    EXPECT_FALSE(session.profile().pendingRaid);EXPECT_TRUE(validateProfileState(session.profile(),content()).valid);
}

TEST(NoiseDecoyTest, RealRemovalFactImmediatelyClearsFirstMiddleLastInvestigatorOnly){
    for(std::size_t which:{0U,1U,2U}){
        GameplayWorld world(config(3));const auto plan=world.previewNoiseDecoy({550,160},use());ASSERT_TRUE(plan);
        RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));world.update({},.5F);
        ASSERT_EQ(world.noiseDecoy().investigations().size(),3U);const auto id=world.enemies()[which].combatTargetId();
        NoiseDecoyTestAccess::removeActor(world,id);EXPECT_EQ(world.enemies().size(),2U);EXPECT_FALSE(world.noiseDecoy().investigations().contains(id));EXPECT_EQ(world.noiseDecoy().investigations().size(),2U);
        for(const auto &survivor:world.enemies()){EXPECT_EQ(survivor.health(),100);EXPECT_TRUE(world.noiseDecoy().investigations().contains(survivor.combatTargetId()));}
    }
}

TEST(NoiseDecoyTest, FarInvestigatorStillDiscoversPlayerWhenAcquisitionBecomesRelevant){
    GameplayWorld world(config());const auto plan=world.previewNoiseDecoy({550,160},use());ASSERT_TRUE(plan);
    RaidNoiseDecoyRuntime runtime;ASSERT_TRUE(runtime.start(*plan));world.commitNoiseDecoy(std::move(runtime));world.update({},.5F);
    ASSERT_FALSE(world.noiseDecoy().investigations().empty());ASSERT_EQ(world.enemies().front().awarenessState(),EnemyAwarenessState::Unaware);
    EXPECT_TRUE(const_cast<Player&>(world.player()).setPosition({500,116}));world.update({},1.0F/60.0F);
    EXPECT_EQ(world.enemies().front().awarenessState(),EnemyAwarenessState::Alerted);EXPECT_TRUE(world.noiseDecoy().investigations().empty());
}

TEST(NoiseDecoyTest, DistanceQueryCullingPreservesExistingPatrolWithoutASource){
    auto c=config();auto &spawn=c.initialEnemies.front();spawn.encounterGroupInstanceId="test-patrol";spawn.encounterKind=RaidEncounterKind::Patrol;
    spawn.encounterHome={710,160};spawn.patrolPoints={{710,160},{850,500}};c.ballisticBlockers={{1,{{760,260},{35,150}}}};
    GameplayWorld enabled(c);c.noiseDecoyEnabled=false;GameplayWorld baseline(c);
    for(int n=0;n<360;++n){enabled.update({},1.0F/60.0F);baseline.update({},1.0F/60.0F);
        EXPECT_FLOAT_EQ(enabled.enemies().front().position().x,baseline.enemies().front().position().x);EXPECT_FLOAT_EQ(enabled.enemies().front().position().y,baseline.enemies().front().position().y);
        EXPECT_EQ(enabled.enemies().front().awarenessState(),baseline.enemies().front().awarenessState());}
}
