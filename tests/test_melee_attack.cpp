#include "melee_attack.h"
#include "world_melee_runtime.h"
#include "alpha_content_ids.h"
#include "base_service_domain.h"
#include "base_defense_serialization.h"
#include "economy_domain.h"
#include "raid_lifecycle.h"
#include "lost_raid_domain.h"
#include "recovery_task_domain.h"
#include "save_repository.h"
#include "game_session.h"
#include "raid_visibility_fixture.h"
#include "base_world.h"
#include "developer_test_range.h"
#include "game_flow.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
const auto &content(){return publishedContentRegistry();}
MeleeUseDefinition use(){return *content().item(alpha_content::utilityKnife).meleeUse;}
MeleeTargetSnapshot target(CombatTargetId id,float x,float y){return {id,{x,y},12,{},0,true};}
void spawn(EnemyRoster<int> &roster,CombatTargetId id,Vec2 center,int hp=12,std::optional<EnemyArmorState> armor={})
{roster.spawn(Enemy{{center.x-1,center.y-1},{2,2},{},hp,id,armor},static_cast<int>(id*10));}
void prepare(WorldMeleeRuntime &m,EnemyRoster<int> &roster,bool request=true,float dt=0)
{m.beginFrame(dt);m.prepare(request,false,{0,0},{1,0},roster,[](Vec2,Vec2){return true;});}
AssetInstanceId knife(ProfileState &p,bool equip=true){
 const auto fit=findFirstProfileFit(p,content(),ProfileContainerId::stash(),content().item(alpha_content::utilityKnife),ItemOrientation::Degrees0);
 EXPECT_TRUE(fit);if(!fit)return 0;
 const auto id=p.assets.create(content().item(alpha_content::utilityKnife),StoredAssetLocation{ProfileContainerId::stash(),*fit});
 if(equip)EXPECT_TRUE(executeInventory(p,content(),InventoryEquipCommand{id,EquipmentSlotKind::QuickMelee},{p.revision,"equip-knife"}).succeeded);
 return id;
}
}

TEST(MeleeAttackTest, FrontConeIsInclusiveAndRearAndBeyondRangeFail){
 const auto u=use();std::vector<MeleeTargetSnapshot> candidates{target(9,72,0)};
 EXPECT_TRUE(resolveMeleeAttack({}, {1,0},u,candidates).contact);
 candidates[0].center={72.01F,0};EXPECT_FALSE(resolveMeleeAttack({}, {1,0},u,candidates).contact);
 candidates[0].center={-10,0};EXPECT_FALSE(resolveMeleeAttack({}, {1,0},u,candidates).contact);
 candidates[0].center={50,50};EXPECT_TRUE(resolveMeleeAttack({}, {1,0},u,candidates).contact);
 candidates[0].center={50,50.1F};EXPECT_FALSE(resolveMeleeAttack({}, {1,0},u,candidates).contact);
}
TEST(MeleeAttackTest, NearestLegalTargetAndStableIdentityBreakTies){
 std::vector<MeleeTargetSnapshot> candidates{target(9,30,0),target(4,20,0),target(2,20,0),target(1,10,0)};
 candidates.back().reachable=false;
 const auto result=resolveMeleeAttack({}, {1,0},use(),candidates);
 ASSERT_TRUE(result.target);EXPECT_EQ(result.target->id,2U);EXPECT_EQ(result.damage.damageApplied,4);
 EXPECT_EQ(result.damage.region,HitRegion::Torso);EXPECT_EQ(result.damage.semantic,HitSemantic::Normal);
}
TEST(MeleeAttackTest, InvalidInputNeverCreatesAnAttack){
 auto u=use();std::vector<MeleeTargetSnapshot> c{target(1,10,0)};
 EXPECT_FALSE(resolveMeleeAttack({}, {},u,c).attempted);
 u.damage=0;EXPECT_FALSE(resolveMeleeAttack({}, {1,0},u,c).attempted);
}
TEST(MeleeAttackTest, ArmorReducesDamageWithoutHeadshotOrPenetration){
 auto t=target(1,10,0);t.armor=ArmorProtectionView{HitRegion::Torso,7,9,10000};
 const std::vector<MeleeTargetSnapshot> c{t};const auto r=resolveMeleeAttack({}, {1,0},use(),c);
 ASSERT_TRUE(r.contact);EXPECT_EQ(r.damage.damageApplied,1);EXPECT_EQ(r.damage.armorDurabilityLoss,1U);
 EXPECT_TRUE(r.damage.armorReducedDamage);EXPECT_EQ(r.damage.semantic,HitSemantic::Normal);
}
TEST(MeleeAttackTest, PreparationIsReadOnlyAndCommitCannotBeRepeated){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true,22);prepare(m,e);
 ASSERT_TRUE(m.result().contact);EXPECT_EQ(e[0].health(),12);EXPECT_EQ(m.result().equipmentAssetId,22U);
 ASSERT_TRUE(m.commitPrepared(e));EXPECT_EQ(e[0].health(),8);EXPECT_FALSE(m.commitPrepared(e));EXPECT_EQ(e[0].health(),8);
 EXPECT_FALSE(e[0].isImpactSlowed());
}
TEST(MeleeAttackTest, BrokenKnifeCanSwingButCannotTouchHealthOrArmor){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),false,22);prepare(m,e);
 ASSERT_TRUE(m.result().attempted);EXPECT_TRUE(m.result().broken);EXPECT_FALSE(m.result().contact);
 EXPECT_TRUE(m.commitPrepared(e));EXPECT_EQ(e[0].health(),12);EXPECT_GT(m.recoverySeconds(),0);
}
TEST(MeleeAttackTest, OneAttackPerEdgeAndRecoverySurvivesConfigurationChanges){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true,22);prepare(m,e);ASSERT_TRUE(m.commitPrepared(e));
 m.configure(use(),true,23);prepare(m,e,true,.3F);EXPECT_FALSE(m.result().attempted);
 m.configure({},false);prepare(m,e,true,.2F);EXPECT_FALSE(m.result().attempted);
 m.configure(use(),true,24);prepare(m,e,true,.15F);EXPECT_TRUE(m.result().attempted);
}
TEST(MeleeAttackTest, MenuAndControlDoNotBypassRecovery){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true);prepare(m,e);ASSERT_TRUE(m.commitPrepared(e));
 m.beginFrame(.2F);m.prepare(true,true,{},{1,0},e,[](Vec2,Vec2){return true;});EXPECT_FALSE(m.result().attempted);
 EXPECT_NEAR(m.recoverySeconds(),.45F,.00001F);EXPECT_FALSE(m.restoreRecovery(-1));EXPECT_FALSE(m.restoreRecovery(11));
 EXPECT_TRUE(m.restoreRecovery(.3F));prepare(m,e,true,.1F);EXPECT_FALSE(m.result().attempted);
}
TEST(MeleeAttackTest, BlockerRejectsContactButNotAirSwing){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true);m.beginFrame(0);
 m.prepare(true,false,{},{1,0},e,[](Vec2,Vec2){return false;});ASSERT_TRUE(m.result().attempted);EXPECT_FALSE(m.result().contact);
 ASSERT_TRUE(m.commitPrepared(e));EXPECT_EQ(e[0].health(),12);
}
TEST(MeleeAttackTest, DeadMovedOrChangedTargetRejectsWithoutDamage){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true);prepare(m,e);
 ASSERT_TRUE(e[0].setPosition({40,0}));EXPECT_FALSE(m.commitPrepared(e));EXPECT_EQ(e[0].health(),12);
}
TEST(MeleeAttackTest, FirstMiddleLastDeathsKeepSurvivingAttachments){
 for(int killed=0;killed<3;++killed){EnemyRoster<int> e;
  for(int i=0;i<3;++i)spawn(e,10+i,{static_cast<float>(i==killed?20:200+i*10),0},4);
  WorldMeleeRuntime m;m.configure(use(),true);prepare(m,e);ASSERT_TRUE(m.commitPrepared(e));
  ASSERT_EQ(e.size(),2U);ASSERT_EQ(m.removals().size(),1U);EXPECT_EQ(m.removals()[0].id,static_cast<CombatTargetId>(10+killed));
  for(const auto &survivor:e){EXPECT_TRUE(e.hasState(survivor.combatTargetId()));EXPECT_EQ(e.state(survivor.combatTargetId()),static_cast<int>(survivor.combatTargetId()*10));}
 }
}
TEST(MeleeAttackTest, ThreeContactsKillOrdinaryInfectedAndNeverRefreshSlow){
 EnemyRoster<int> e;spawn(e,7,{30,0});WorldMeleeRuntime m;m.configure(use(),true);
 for(int i=0;i<3;++i){prepare(m,e,true,i?.65F:0);ASSERT_TRUE(m.commitPrepared(e));if(i<2){ASSERT_EQ(e.size(),1U);EXPECT_FALSE(e[0].isImpactSlowed());}}
 EXPECT_TRUE(e.empty());
}
TEST(MeleeAttackTest, KnifeIsNotAFourthGunAndHasNoStartingGrant){
 const auto &d=content().item(alpha_content::utilityKnife);ASSERT_TRUE(d.meleeUse);EXPECT_FALSE(d.weaponUse);EXPECT_FALSE(d.weaponCondition);
 EXPECT_FALSE(isWeaponEquipmentSlot(EquipmentSlotKind::QuickMelee));EXPECT_TRUE(itemCanEquipInSlot(d,EquipmentSlotKind::QuickMelee));
 for(auto slot:{EquipmentSlotKind::PrimaryWeapon,EquipmentSlotKind::SecondaryWeapon,EquipmentSlotKind::Sidearm})EXPECT_FALSE(itemCanEquipInSlot(d,slot));
 auto p=makeNewPublishedProfile("no-free-knife",content());for(const auto &[id,a]:p.assets.records())EXPECT_NE(a.definitionId,alpha_content::utilityKnife);
}
TEST(MeleeAttackTest, PurchaseEquipWeightAndUnaffordablePurchaseAreAtomic){
 auto p=makeNewPublishedProfile("knife-supply",content());p.currency=1000;
 ASSERT_TRUE(executeEconomy(p,content(),PurchaseCommand{alpha_content::utilityKnife,1},{p.revision,"buy"}).succeeded);EXPECT_EQ(p.currency,880U);
 AssetInstanceId id{};for(const auto &[n,a]:p.assets.records())if(a.definitionId==alpha_content::utilityKnife)id=n;
 ASSERT_NE(id,0U);EXPECT_EQ(p.assets.find(id)->currentDurability,10000U);ASSERT_TRUE(executeInventory(p,content(),InventoryEquipCommand{id,EquipmentSlotKind::QuickMelee},{p.revision,"equip"}).succeeded);
 EXPECT_TRUE(validateProfileState(p,content()).valid);EXPECT_EQ(content().item(alpha_content::utilityKnife).unitWeightGrams,400U);
 p.currency=0;const auto before=profileStateFingerprint(p);EXPECT_FALSE(executeEconomy(p,content(),PurchaseCommand{alpha_content::utilityKnife,1},{p.revision,"no-money"}).succeeded);EXPECT_EQ(before,profileStateFingerprint(p));
}
TEST(MeleeAttackTest, BaseMaintenanceUsesKnifeQuoteAndKeepsSameInstance){
 auto p=makeNewPublishedProfile("knife-repair",content());p.currency=1000;const auto id=knife(p,false);p.assets.findMutable(id)->currentDurability=0;
 const auto plan=queryGunsmithMaintenance(p,content(),{id});ASSERT_TRUE(plan.canCommit)<<plan.message;EXPECT_EQ(plan.quotedCurrency,70U);
 ASSERT_TRUE(executeGunsmithMaintenance(p,content(),{id},{p.revision,"repair"}).succeeded);EXPECT_EQ(p.assets.find(id)->currentDurability,10000U);EXPECT_EQ(p.currency,930U);
 EXPECT_FALSE(queryGunsmithMaintenance(p,content(),{id}).canCommit);EXPECT_TRUE(validateProfileState(p,content()).valid);
}
TEST(MeleeAttackTest, NewSlotAndUsedDurabilityRoundTripAndCannotDowngrade){
 auto p=makeNewPublishedProfile("knife-save",content());const auto id=knife(p);p.assets.findMutable(id)->currentDurability=9950;
 const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,content().contentVersion()),content());ASSERT_TRUE(loaded.profile)<<loaded.message;
 EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));EXPECT_EQ(equippedAsset(*loaded.profile,EquipmentSlotKind::QuickMelee),id);
 p.stashLayoutVersion=0;
 EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(p,content().contentVersion(),53)),std::invalid_argument);
}
TEST(MeleeAttackTest, LegacyProfileLoadsWithoutGrantAndWithEmptyMeleeSlot){
 auto p=makeNewPublishedProfile("legacy53",content());p.stashLayoutVersion=0;const auto encoded=serializeProfileEnvelope(p,"weapon-flashlight-battery-content-65",53);
 const auto loaded=deserializeProfileEnvelope(encoded,content());ASSERT_TRUE(loaded.profile)<<loaded.message;EXPECT_FALSE(equippedAsset(*loaded.profile,EquipmentSlotKind::QuickMelee));
 for(const auto &[id,a]:loaded.profile->assets.records())EXPECT_NE(a.definitionId,alpha_content::utilityKnife);
}
TEST(MeleeAttackTest, MalfunctionAndAmmunitionOnAKnifeAreRejected){
 auto p=makeNewPublishedProfile("invalid-knife",content());const auto id=knife(p);p.assets.findMutable(id)->weaponMalfunction=WeaponMalfunctionType::Stovepipe;EXPECT_FALSE(validateProfileState(p,content()).valid);
 p.assets.findMutable(id)->weaponMalfunction=WeaponMalfunctionType::None;p.assets.findMutable(id)->chamberedRound=MagazineRoundRecord{alpha_content::ammunition,std::nullopt};EXPECT_FALSE(validateProfileState(p,content()).valid);
}
TEST(MeleeAttackTest, DefenseRecoveryFieldKeepsLegacyZeroAndRejectsInvalidData){
 BaseDefenseSnapshot old;const auto before=baseDefenseCheckpointHash(old);old.meleeRecoverySeconds=.65F;EXPECT_NE(before,baseDefenseCheckpointHash(old));
 auto encoded=baseDefenseSnapshotJson(old);EXPECT_EQ(encoded.at("melee_recovery_seconds").get<float>(),.65F);
 old.meleeRecoverySeconds=0;EXPECT_FALSE(baseDefenseSnapshotJson(old).contains("melee_recovery_seconds"));EXPECT_EQ(before,baseDefenseCheckpointHash(old));
}
TEST(MeleeAttackTest, LocalCellQueryUses32And100TargetsWithinOriginal3msGate){
 using Clock=std::chrono::steady_clock;
 for(int count:{32,100}){EnemyRoster<int> e;spawn(e,1,{30,0});for(int i=1;i<count;++i)spawn(e,static_cast<CombatTargetId>(i+1),{3000.0F+i*50,2000});
  WorldMeleeRuntime m;m.configure(use(),true);std::vector<double> samples;
  for(int n=0;n<160;++n){const auto t=Clock::now();prepare(m,e,true,.65F);const auto ms=std::chrono::duration<double,std::milli>(Clock::now()-t).count();if(n>=10)samples.push_back(ms);EXPECT_LE(m.result().candidatesExamined,1U);}
  std::sort(samples.begin(),samples.end());ASSERT_EQ(samples.size(),150U);const double p95=samples[142];EXPECT_LT(p95,3.0)<<count;
  if(const char *dir=std::getenv("RAIDLINE_MELEE_QA_DIR")){std::filesystem::create_directories(dir);std::ofstream out(std::filesystem::path(dir)/("melee-"+std::to_string(count)+".csv"));out<<"sample_ms\n";for(auto value:samples)out<<value<<'\n';}
 }
}

struct MeleeTestAccess {
 static AssetInstanceId stage(GameSession &s){
  EXPECT_TRUE(s.startDeveloperRange());
  RaidWorldConfig cfg;cfg.developerRange=true;cfg.worldSize={1280,720};cfg.playerSpawn={100,100};
  cfg.extractionPoint={{1100,600},{100,100}};cfg.deferPlayerDamageResolution=true;
  cfg.initialEnemies={EnemySpawn{{1000,100},{32,32},12}};
  s.world_=std::make_unique<GameplayWorld>(cfg);const auto id=knife(s.profile_);
  GameplayInput aim;aim.aimWorldPosition={800,116};s.update(aim,.5F);
  EXPECT_TRUE(const_cast<Enemy&>(s.world().enemies().front()).setPosition({160,100}));return id;
 }
 static void action(GameSession &s,RaidAction action){EXPECT_TRUE(s.raidActionState_.start(std::move(action)));}
 static AssetInstanceId faultedGun(GameSession &s){const auto &d=content().item(alpha_content::rifle);
  const auto id=s.profile_.assets.create(d,EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
  s.profile_.assets.findMutable(id)->weaponMalfunction=WeaponMalfunctionType::Stovepipe;return id;}
 static auto sequence(const GameSession &s){return s.weaponFaultSequence_;}
 static AssetInstanceId equipKnife(GameSession &s){return knife(s.profile_);}
 static bool flush(GameSession &s){return s.checkpointBaseDefense(true);}
 static void stagePerimeter(BaseWorld &w){
  HomePerimeterSiteSnapshot site;site.baseSiteDefinitionId=RegionalBaseSiteDefinitionId{w.siteDefinitionId()};site.cycleIndex=4;
  site.enemies.push_back({1,{1054,1010},{1054,1010},{32,32},4,4});w.configureHomePerimeter(&site);
  w.playerPosition_={1000,1000};w.layout_.baseParcel={{5000,5000},{300,300}};
  w.movementBlockers_.clear();w.movementBlockerIndex_=RaidSpaceBlockerIndex::build(w.worldSize(),{});
 }
 static Vec2 stageDefense(BaseWorld &w){
  auto &d=*w.baseDefense_;const Vec2 center=d.state_.wavePlans.front().entry;
  Vec2 offset{w.worldSize().x/2-center.x,w.worldSize().y/2-center.y};
  const float scale=60/std::hypot(offset.x,offset.y);offset.x*=scale;offset.y*=scale;
  w.playerPosition_={center.x+offset.x-20,center.y+offset.y-26};d.state_.playerPosition=w.playerPosition_;
  d.state_.nextSpawnDelay=1000;d.state_.spawnedEnemyCount=1;
  const auto id=d.state_.wavePlans.front().enemyIds.front();
  d.enemies_.spawn(Enemy{{center.x-16,center.y-24},{32,48},{},d.state_.wavePlans.front().enemyMaxHealth,id},{checkpointPoint(center),100,{}});
  d.synchronizeActorCheckpoints();return center;
 }
};
namespace {GameplayInput swing(){GameplayInput input;input.aimWorldPosition={800,116};input.meleeJustPressed=input.meleePressed=true;return input;}}
TEST(MeleeAttackTest, RealRaidConsumerCommitsOneHitAndWearThenRequiresRelease){
 GameSession s;const auto id=MeleeTestAccess::stage(s);auto input=swing();s.update(input,0);
 ASSERT_EQ(s.world().enemies().size(),1U);EXPECT_EQ(s.world().enemies()[0].health(),8);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 input.meleeJustPressed=false;s.update(input,.65F);EXPECT_EQ(s.world().enemies()[0].health(),8);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 s.update({},0);s.update(swing(),0);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9900U);
}
TEST(MeleeAttackTest, SimultaneousGunInputCannotFireOrClearFaultAndHeldFireCannotLeak){
 GameSession s;const auto id=MeleeTestAccess::stage(s);const auto gun=MeleeTestAccess::faultedGun(s);const auto seq=MeleeTestAccess::sequence(s);
 auto input=swing();input.fireJustPressed=input.firePressed=true;s.update(input,0);
 EXPECT_FALSE(s.world().shotFiredLastUpdate());EXPECT_EQ(MeleeTestAccess::sequence(s),seq);EXPECT_EQ(s.profile().assets.find(gun)->weaponMalfunction,WeaponMalfunctionType::Stovepipe);
 EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 input.meleeJustPressed=input.meleePressed=input.fireJustPressed=false;s.update(input,.65F);
 EXPECT_FALSE(s.world().shotFiredLastUpdate());EXPECT_EQ(MeleeTestAccess::sequence(s),seq);
}
TEST(MeleeAttackTest, MedicalAndMaintenanceFirstInputOnlyCancelsThenReleaseIsRequired){
 for(int kind=0;kind<2;++kind){GameSession s;const auto id=MeleeTestAccess::stage(s);
  if(kind==0)MeleeTestAccess::action(s,MedicalRaidAction{id,MedicalUseEffect::RestoreHealth,false,false,0,10,0,10});
  else MeleeTestAccess::action(s,WeaponMaintenanceRaidAction{id,id,0,10});
  s.update(swing(),0);EXPECT_FALSE(s.raidActionState().active());EXPECT_FALSE(s.world().melee().result().attempted);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,10000U);
  auto held=swing();held.meleeJustPressed=false;s.update(held,0);EXPECT_FALSE(s.world().melee().result().attempted);
  s.update({},0);s.update(swing(),0);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 }
}
TEST(MeleeAttackTest, ReloadAndMagazineLoadingCancelAndAttackWithoutDuplicatingRounds){
 for(int kind=0;kind<2;++kind){GameSession s;const auto id=MeleeTestAccess::stage(s);
  if(kind==0)MeleeTestAccess::action(s,ReloadRaidAction{});else MeleeTestAccess::action(s,LoadMagazineRaidAction{});
  s.update(swing(),0);EXPECT_FALSE(s.raidActionState().active());EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);EXPECT_TRUE(validateProfileState(s.profile(),content()).valid);
 }
}
TEST(MeleeAttackTest, InventoryCancelsInputAndDoesNotResetActorRecovery){
 GameSession s;const auto id=MeleeTestAccess::stage(s);s.update(swing(),0);const auto used=s.profile().assets.find(id)->currentDurability;
 auto menu=swing();menu.inventoryOpen=true;s.update(menu,.2F);EXPECT_FALSE(s.world().melee().result().attempted);EXPECT_NEAR(s.world().melee().recoverySeconds(),.45F,.0001F);
 menu.inventoryOpen=false;menu.meleeJustPressed=false;s.update(menu,0);EXPECT_FALSE(s.world().melee().result().attempted);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,used);
}
TEST(MeleeAttackTest, RealRaidZeroDurabilityPreservesEnemyAndWear){
 GameSession s;const auto id=MeleeTestAccess::stage(s);
 auto &p=const_cast<ProfileState&>(s.profile());p.assets.findMutable(id)->currentDurability=0;s.update(swing(),0);
 EXPECT_EQ(s.world().enemies()[0].health(),12);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,0U);EXPECT_TRUE(s.world().melee().result().broken);
}
TEST(MeleeAttackTest, RangeSupplyIncludesKnifeButItNeverEscapesToProductionProfile){
 const auto supplies=developerRangeSupplies();EXPECT_NE(std::find(supplies.begin(),supplies.end(),alpha_content::utilityKnife),supplies.end());
 GameSession s;ASSERT_TRUE(s.startDeveloperRange());ASSERT_TRUE(s.grantDeveloperRangeLoadout(alpha_content::rifle,alpha_content::ammunition,std::nullopt));ASSERT_TRUE(s.grantDeveloperRangeItem(alpha_content::utilityKnife));
 EXPECT_FALSE(s.profile().pendingRaid);EXPECT_TRUE(validateProfileState(s.profile(),content()).valid);
}
namespace {
AssetInstanceId findDefinition(const ProfileState &p,const ItemDefinitionId &d){for(const auto &[id,a]:p.assets.records())if(a.definitionId==d)return id;return 0;}
void deployKnife(ProfileState &p){
 for(auto [d,slot]:{std::pair{alpha_content::rifle,EquipmentSlotKind::PrimaryWeapon},std::pair{alpha_content::backpack,EquipmentSlotKind::Backpack}})
  EXPECT_TRUE(executeInventory(p,content(),InventoryEquipCommand{findDefinition(p,d),slot},{p.revision,std::string{d.value()}}).succeeded);
 EXPECT_TRUE(executeDeploy(p,content(),DeployCommand{"knife-raid","knife-result",7301,MapDefinitionId{"map.v0.test"},{},std::nullopt},{p.revision,"knife-deploy"}).succeeded);
}
}
TEST(MeleeAttackTest, ExtractionKeepsSameKnifeAndActualDurability){
 auto p=makeNewPublishedProfile("extract-knife",content());const auto id=knife(p);deployKnife(p);
 ASSERT_TRUE(p.pendingRaid);EXPECT_NE(std::find(p.pendingRaid->carriedRootAssetIds.begin(),p.pendingRaid->carriedRootAssetIds.end(),id),p.pendingRaid->carriedRootAssetIds.end());
 p.assets.findMutable(id)->currentDurability=9900;ASSERT_TRUE(settlePendingRaid(p,content(),"knife-result",RaidResultOutcome::Extracted).succeeded);
 EXPECT_EQ(p.assets.find(id)->currentDurability,9900U);EXPECT_TRUE(validateProfileState(p,content()).valid);
}
TEST(MeleeAttackTest, DeathCreatesOneLostKnifeAndSelfRecoveryPreservesWear){
 auto p=makeNewPublishedProfile("recover-knife",content());const auto id=knife(p);deployKnife(p);p.assets.findMutable(id)->currentDurability=9950;
 ASSERT_TRUE(settlePendingRaid(p,content(),"knife-result",RaidResultOutcome::PlayerDead).succeeded);ASSERT_NE(p.assets.find(id),nullptr);EXPECT_TRUE(std::holds_alternative<LostRaidAssetLocation>(p.assets.find(id)->location));
 ASSERT_TRUE(executeDeploy(p,content(),DeployCommand{"knife-recovery","knife-recovered",9403,MapDefinitionId{"map.v0.test"},{},"knife-result"},{p.revision,"knife-self"}).succeeded);
 ASSERT_TRUE(openObservedRaidSelfRecovery(p,content(),{p.revision,"knife-open"}).succeeded);
 ASSERT_TRUE(pickupObservedRaidLoot(p,content(),id,{p.revision,"knife-pick"}).succeeded);
 ASSERT_TRUE(settlePendingRaid(p,content(),"knife-recovered",RaidResultOutcome::Extracted).succeeded);EXPECT_EQ(p.assets.find(id)->currentDurability,9950U);
}

TEST(MeleeAttackTest, BasePerimeterUsesSharedDamageAndRetiresKilledIdentity){
 BaseWorld w;MeleeTestAccess::stagePerimeter(w);w.configureMelee(use(),true,91);
 auto input=swing();input.aimWorldPosition={1500,1026};static_cast<void>(w.update(input,.000001F));
 ASSERT_TRUE(w.melee().result().contact);ASSERT_TRUE(w.commitPreparedMelee());
 EXPECT_TRUE(w.perimeterEnemies().empty());ASSERT_EQ(w.perimeterRemovalsLastUpdate().size(),1U);
 EXPECT_EQ(w.perimeterRemovalsLastUpdate()[0].id,1U);EXPECT_FALSE(w.commitPreparedMelee());
}

TEST(MeleeAttackTest, DefenseSavesHealthWearAndRecoveryTogetherAndBlocksFailedDevice){
 const auto path=std::filesystem::temp_directory_path()/("melee-defense-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{path};
 GameFlow flow;flow.configurePersistence(path);ASSERT_TRUE(flow.startNewGame("melee-defense",false));
 auto &s=flow.gameSession();const auto id=MeleeTestAccess::equipKnife(s);
 ASSERT_TRUE(s.triggerDeveloperBaseSiegeWarning());ASSERT_TRUE(s.startBaseRealtimeDefense(flow.baseWorld()));
 const auto aim=MeleeTestAccess::stageDefense(flow.baseWorld());auto input=swing();input.aimWorldPosition=aim;
 const auto health=flow.baseWorld().baseDefenseEnemies()[0].health();
 flow.updateBase(input,.000001F);ASSERT_TRUE(flow.baseWorld().melee().result().committed);
 ASSERT_EQ(flow.baseWorld().baseDefenseEnemies().size(),1U);EXPECT_EQ(flow.baseWorld().baseDefenseEnemies()[0].health(),health-4);
 EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 std::string checkpointMessage;ASSERT_TRUE(validateBaseDefenseSnapshot(*flow.baseWorld().baseDefenseCheckpoint(),checkpointMessage))<<checkpointMessage;
 ASSERT_TRUE(MeleeTestAccess::flush(s))<<s.persistenceMessage();
 const auto loaded=SaveRepository(path).load(content());ASSERT_TRUE(loaded.profile)<<loaded.message;
 ASSERT_TRUE(loaded.profile->activeBaseDefense);EXPECT_EQ(loaded.profile->assets.find(id)->currentDurability,9950U);
 EXPECT_GT(loaded.profile->activeBaseDefense->meleeRecoverySeconds,.64F);
 EXPECT_EQ(loaded.profile->activeBaseDefense->enemies[0].health,health-4);
 auto legacy=*loaded.profile;legacy.stashLayoutVersion=0;
 EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(legacy,content().contentVersion(),53)),std::invalid_argument);
 ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));EXPECT_FALSE(MeleeTestAccess::flush(s));
 const auto before=baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint());
 flow.updateBase(input,.1F);EXPECT_TRUE(s.baseDefenseSaveBlocked());
 EXPECT_EQ(baseDefenseCheckpointHash(*flow.baseWorld().baseDefenseCheckpoint()),before);
 EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
 ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));ASSERT_TRUE(s.retryBaseDefenseSave());
 GameFlow restored;restored.configurePersistence(path);ASSERT_TRUE(restored.continueGame());
 EXPECT_NEAR(restored.baseWorld().melee().recoverySeconds(),flow.baseWorld().melee().recoverySeconds(),.000001F);
 EXPECT_EQ(restored.gameSession().profile().assets.find(id)->currentDurability,9950U);
}

TEST(MeleeAttackTest, PortalTransitionKeepsRecoveryAndDoesNotAttackOldSpace){
 RaidWorldConfig cfg;cfg.worldSize={800,600};cfg.playerSpawn={100,100};cfg.extractionPoint={{650,450},{100,100}};
 RaidInteriorWorldConfig room;room.id=RaidSpaceDefinitionId{"raid_space.test.melee"};room.displayName="Melee test";
 room.worldSize={480,360};room.exteriorEntrance={{80,80},{100,100}};room.exteriorReturn={100,100};
 room.interiorSpawn={80,80};room.interiorExit={{60,60},{120,120}};room.initialEnemies={EnemySpawn{{130,80},{32,32},12}};cfg.interiors.push_back(room);
 GameplayWorld w{cfg};w.configureMelee(use(),true,1);w.update(swing(),0);ASSERT_TRUE(w.commitPreparedMelee());
 GameplayInput portal;portal.interactJustPressed=true;w.update(portal,.1F);ASSERT_FALSE(w.inOutdoorRaidSpace());
 EXPECT_NEAR(w.melee().recoverySeconds(),.55F,.0001F);w.update(swing(),0);EXPECT_FALSE(w.melee().result().attempted);
 EXPECT_EQ(w.enemies()[0].health(),12);
}

TEST(MeleeAttackTest, LastPositiveDurabilityHitWorksThenBrokenSwingCannotDamage){
 GameSession s;const auto id=MeleeTestAccess::stage(s);auto &p=const_cast<ProfileState&>(s.profile());p.assets.findMutable(id)->currentDurability=1;
 s.update(swing(),0);EXPECT_EQ(s.world().enemies()[0].health(),8);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,0U);
 s.update({},.65F);s.update(swing(),0);EXPECT_EQ(s.world().enemies()[0].health(),8);EXPECT_TRUE(s.world().melee().result().broken);
}

TEST(MeleeAttackTest, MeleeDoesNotClearExistingBulletImpactSlow){
 EnemyRoster<int> e;spawn(e,1,{30,0});EXPECT_FALSE(e[0].takeDamage(1));ASSERT_TRUE(e[0].isImpactSlowed());
 WorldMeleeRuntime m;m.configure(use(),true);prepare(m,e);ASSERT_TRUE(m.commitPrepared(e));EXPECT_TRUE(e[0].isImpactSlowed());
}

TEST(MeleeAttackTest, ReturningFromRangeRestoresOriginalProfileWithoutKnife){
 GameFlow f;ASSERT_TRUE(f.startNewGame("melee-isolation",false));const auto before=profileStateFingerprint(f.gameSession().profile());
 ASSERT_TRUE(f.enterDeveloperRange());auto &s=f.gameSession();ASSERT_TRUE(s.grantDeveloperRangeLoadout(alpha_content::rifle,alpha_content::ammunition,std::nullopt));
 ASSERT_TRUE(s.grantDeveloperRangeItem(alpha_content::utilityKnife));ASSERT_TRUE(f.leaveDeveloperRange());
 EXPECT_EQ(profileStateFingerprint(f.gameSession().profile()),before);
}

TEST(MeleeAttackTest, NpcRecoveryUsesExistingFrozenResultAndPreservesKnifeIdentity){
 auto p=makeNewPublishedProfile("npc-knife",content());const auto id=knife(p);p.currency=2000;deployKnife(p);p.assets.findMutable(id)->currentDurability=9900;
 ASSERT_TRUE(settlePendingRaid(p,content(),"knife-result",RaidResultOutcome::PlayerDead).succeeded);
 ASSERT_TRUE(executeStartRecoveryTask(p,content(),"knife-result",{p.revision,"npc-start"}).succeeded);
 ASSERT_TRUE(recoveryTaskForAsset(p,id));const auto saved=deserializeProfileEnvelope(serializeProfileEnvelope(p,content().contentVersion()),content());ASSERT_TRUE(saved.profile);
 EXPECT_EQ(profileStateFingerprint(p),profileStateFingerprint(*saved.profile));
 p.worldClock.elapsedWorldMinutes=p.recoveryTask->completionWorldMinute;ASSERT_TRUE(applyRecoveryTaskThrough(p).becameReady);
 ASSERT_TRUE(executeCollectRecoveryTask(p,content(),{p.revision,"npc-collect"}).succeeded);
 if(const auto *asset=p.assets.find(id)){EXPECT_EQ(asset->currentDurability,9900U);EXPECT_FALSE(std::holds_alternative<LostRaidAssetLocation>(asset->location));}
 EXPECT_TRUE(validateProfileState(p,content()).valid);
}

TEST(MeleeAttackTest, CommittedEnemyScratchSurvivesSameFrameMeleeKill){
 GameSession s;const auto id=MeleeTestAccess::stage(s);auto &e=const_cast<Enemy&>(s.world().enemies()[0]);
 EXPECT_FALSE(e.takeDamage(8,EnemyImpactReaction::None));ASSERT_TRUE(e.tryStartAttack(EnemyAttackType::Scratch,{-1,0}));
 static_cast<void>(e.update(e.attackConfig()->windupDuration+.000001F,1280,720));ASSERT_TRUE(e.setPosition({160,100}));
 const auto hp=s.profile().currentHealth;s.update(swing(),0);
 EXPECT_LT(s.profile().currentHealth,hp);EXPECT_TRUE(s.world().enemies().empty());EXPECT_EQ(s.profile().assets.find(id)->currentDurability,9950U);
}

TEST(MeleeAttackTest, RaidWallRejectsContactAndMeleeDeathPublishesOneCorpseFact){
 for(bool wall:{false,true}){
  RaidWorldConfig cfg;cfg.worldSize={800,600};cfg.playerSpawn={100,100};cfg.extractionPoint={{650,450},{100,100}};
  EnemySpawn enemy{{160,100},{32,32},4};enemy.sourceEnemyId="knife-corpse-source";cfg.initialEnemies.push_back(enemy);
  if(wall)cfg.ballisticBlockers.push_back({1,{{145,80},{4,100}}});
  GameplayWorld w{cfg};w.configureMelee(use(),true,1);w.update(swing(),0);ASSERT_TRUE(w.commitPreparedMelee());
  EXPECT_EQ(w.melee().result().contact,!wall);EXPECT_EQ(w.pendingCorpseDeaths().size(),wall?0U:1U);
  if(!wall){EXPECT_EQ(w.pendingCorpseDeaths()[0].sourceEnemyId,"knife-corpse-source");EXPECT_TRUE(w.enemies().empty());
   EXPECT_FALSE(w.commitPreparedMelee());EXPECT_EQ(w.pendingCorpseDeaths().size(),1U);}
  EXPECT_TRUE(w.hitResultsLastUpdate().empty());EXPECT_TRUE(w.logicalBallistics().empty());
 }
}

TEST(MeleeAttackTest, GrabCannotBeEscapedThroughQuickMelee){
 GameSession s;const auto id=MeleeTestAccess::stage(s);ASSERT_TRUE(const_cast<Player&>(s.world().player()).applyControl(1));s.update(swing(),0);
 EXPECT_FALSE(s.world().melee().result().attempted);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,10000U);EXPECT_TRUE(s.world().player().isControlled());
}

TEST(MeleeAttackTest, InstantKnifeRepairRollsBackCurrencyAndDurabilityOnSaveFailure){
 const auto path=std::filesystem::temp_directory_path()/("melee-repair-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{path};
 auto p=makeNewPublishedProfile("melee-repair",content());p.currency=1000;const auto id=knife(p,false);p.assets.findMutable(id)->currentDurability=0;
 ASSERT_TRUE(SaveRepository(path).save(p,content().contentVersion()).succeeded);GameSession s;s.configurePersistence(path);ASSERT_TRUE(s.continueProfile());
 const auto before=profileStateFingerprint(s.profile());ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));
 EXPECT_FALSE(s.executeBaseGunsmithMaintenance(id,"repair-failed").succeeded);EXPECT_EQ(profileStateFingerprint(s.profile()),before);
 ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));ASSERT_TRUE(s.executeBaseGunsmithMaintenance(id,"repair-success").succeeded);
 EXPECT_EQ(s.profile().currency,930U);EXPECT_EQ(s.profile().assets.find(id)->currentDurability,10000U);
}

TEST(MeleeAttackTest, ActiveQuitLosesKnifeAndAbnormalExitRestoresExactStartingWear){
 auto p=makeNewPublishedProfile("knife-active-quit",content());const auto id=knife(p);deployKnife(p);p.assets.findMutable(id)->currentDurability=9900;
 ASSERT_TRUE(settlePendingRaid(p,content(),"knife-result",RaidResultOutcome::ActiveQuit).succeeded);ASSERT_NE(p.assets.find(id),nullptr);
 EXPECT_TRUE(std::holds_alternative<LostRaidAssetLocation>(p.assets.find(id)->location));
 const auto path=std::filesystem::temp_directory_path()/("melee-abnormal-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{path};
 auto initial=makeNewPublishedProfile("knife-abnormal",content());const auto knifeId=knife(initial);initial.assets.findMutable(knifeId)->currentDurability=9950;
 auto deployed=initial;deployKnife(deployed); // Save a legal pre-deploy equipment layout.
 for(auto slot:{EquipmentSlotKind::PrimaryWeapon,EquipmentSlotKind::Backpack}){
  const auto asset=equippedAsset(deployed,slot);ASSERT_TRUE(asset);
  ASSERT_TRUE(executeInventory(initial,content(),InventoryEquipCommand{*asset,slot},{initial.revision,"abnormal-equip-"+std::to_string(static_cast<int>(slot))}).succeeded);
 }
 ASSERT_TRUE(SaveRepository(path).save(initial,content().contentVersion()).succeeded);const auto before=profileStateFingerprint(initial);
 {GameSession s;s.configurePersistence(path);ASSERT_TRUE(s.continueProfile());ASSERT_TRUE(s.deployAlpha(42,MapDefinitionId{"map.v0.test"}));
  const_cast<ProfileState&>(s.profile()).assets.findMutable(knifeId)->currentDurability=9700;}
 GameSession reopened;reopened.configurePersistence(path);ASSERT_TRUE(reopened.continueProfile());
 EXPECT_EQ(profileStateFingerprint(reopened.profile()),before);EXPECT_EQ(reopened.profile().assets.find(knifeId)->currentDurability,9950U);
}
