#include "tactical_device_domain.h"
#include "raid_visibility_fixture.h"
#include "weapon_component_domain.h"
#include "economy_domain.h"
#include "save_repository.h"
#include "raid_lifecycle.h"
#include "lost_raid_domain.h"
#include "recovery_task_domain.h"
#include "gameplay_world.h"
#include "game_session.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
const auto &c() { return publishedContentRegistry(); }
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"}, rail{"item.component.rail_handguard"},
    grip{"item.component.vertical_grip"}, lamp{"item.component.weapon_flashlight_basic"}, cell{"item.battery.universal"};
AssetInstanceId find(const ProfileState &p, const ItemDefinitionId &d) {
    for (const auto &[id,a]:p.assets.records()) if (a.definitionId==d) return id;
    return 0;
}
AssetInstanceId add(ProfileState &p, const ItemDefinitionId &d, std::uint32_t n=1) {
    const auto fit=findFirstProfileFit(p,c(),ProfileContainerId::stash(),c().item(d),ItemOrientation::Degrees0);
    EXPECT_TRUE(fit); if (!fit) return 0;
    return p.assets.create(c().item(d),StoredAssetLocation{ProfileContainerId::stash(),*fit},n);
}
InventoryReceipt install(ProfileState &p,const ItemDefinitionId &d,const std::string &tx) {
    return executeWeaponComponentChange(p,c(),{find(p,rifle),c().item(d).weaponComponent->slot,find(p,d)},{p.revision,tx});
}
ProfileState ready() {
    auto p=makeNewPublishedProfile("weapon-light-test",c()); add(p,lamp); add(p,cell,3);
    EXPECT_TRUE(install(p,rail,"rail").succeeded); EXPECT_TRUE(install(p,grip,"grip").succeeded);
    EXPECT_TRUE(install(p,lamp,"lamp").succeeded);
    EXPECT_TRUE(executeDeviceBatteryChange(p,c(),{find(p,lamp),find(p,cell)},{},{p.revision,"cell"}).succeeded);
    return p;
}
WeaponLightBinding binding(const ProfileState &p) { return findWeaponLight(p,c(),find(p,rifle)); }
void on(ProfileState &p) { ASSERT_TRUE(executeDeviceSwitch(p,c(),find(p,lamp),{p.revision,"on"}).succeeded); }
void equip(ProfileState &p, AssetInstanceId id, EquipmentSlotKind slot, const char *tx) {
    EXPECT_TRUE(executeInventory(p,c(),InventoryEquipCommand{id,slot},{p.revision,tx}).succeeded);
}
void deploy(ProfileState &p) {
    equip(p,find(p,rifle),EquipmentSlotKind::PrimaryWeapon,"equip");
    equip(p,find(p,ItemDefinitionId{"item.container.backpack_small"}),EquipmentSlotKind::Backpack,"bag");
    ASSERT_TRUE(executeDeploy(p,c(),DeployCommand{"light-raid","light-result",7301,MapDefinitionId{"map.v0.test"},{},std::nullopt},{p.revision,"deploy"}).succeeded);
}
Vec2 polar(Vec2 o,float degrees,float r) { const float a=degrees*3.14159265359F/180; return {o.x+std::cos(a)*r,o.y+std::sin(a)*r}; }
}

TEST(WeaponLightTest, SupplyHasExplicitPricesAndNoStarterGrant) {
    auto p=makeNewPublishedProfile("supply",c()); EXPECT_EQ(find(p,lamp),0); EXPECT_EQ(find(p,cell),0);
    EXPECT_EQ(c().item(lamp).marketBuyPrice,150U); EXPECT_EQ(c().item(cell).marketBuyPrice,20U);
    p.currency=1000;
    ASSERT_TRUE(executeEconomy(p,c(),PurchaseCommand{lamp,1},{p.revision,"buy-light"}).succeeded);
    ASSERT_TRUE(executeEconomy(p,c(),PurchaseCommand{cell,11},{p.revision,"buy-cell"}).succeeded);
    EXPECT_EQ(p.currency,630U);
    std::uint32_t batteries=0; for(const auto &[id,a]:p.assets.records()) if(a.definitionId==cell) { batteries+=a.quantity; EXPECT_LE(a.quantity,10U); EXPECT_EQ(a.remainingCharges,600000U); }
    EXPECT_EQ(batteries,11U); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, IndependentMountRequiresRailAndCascadesWithGripAndBattery) {
    auto p=makeNewPublishedProfile("mount",c()); add(p,lamp);
    const auto before=profileStateFingerprint(p); EXPECT_FALSE(install(p,lamp,"invalid").succeeded); EXPECT_EQ(before,profileStateFingerprint(p));
    p=ready(); auto b=binding(p); p.assets.findMutable(b.batteryAssetId)->remainingCharges=123456;
    const auto plan=queryWeaponComponentChange(p,c(),{find(p,rifle),WeaponComponentSlot::Handguard,0});
    ASSERT_TRUE(plan.canCommit)<<plan.message; EXPECT_EQ(plan.returned.size(),3U);
    auto full=p; while(findFirstProfileFit(full,c(),ProfileContainerId::stash(),c().item(cell),ItemOrientation::Degrees0)) add(full,cell);
    const auto fingerprint=profileStateFingerprint(full);
    EXPECT_FALSE(executeWeaponComponentChange(full,c(),{find(full,rifle),WeaponComponentSlot::Handguard,0},{full.revision,"full"}).succeeded);
    EXPECT_EQ(profileStateFingerprint(full),fingerprint);
    ASSERT_TRUE(executeWeaponComponentChange(p,c(),{find(p,rifle),WeaponComponentSlot::Handguard,0},{p.revision,"remove"}).succeeded);
    for(const auto &d:{rail,grip,lamp}) EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(p.assets.find(find(p,d))->location));
    EXPECT_EQ(std::get<InstalledDeviceBatteryLocation>(p.assets.find(b.batteryAssetId)->location).deviceAssetId,b.deviceAssetId);
    EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,123456U); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, BatteryPreviewReplacementAndRetryAreAtomic) {
    auto p=ready(); auto b=binding(p); const auto incoming=find(p,cell); // remaining full stack
    p.assets.findMutable(b.batteryAssetId)->remainingCharges=43210;
    const auto fingerprint=profileStateFingerprint(p);
    EXPECT_TRUE(queryDeviceBatteryChange(p,c(),{b.deviceAssetId,incoming},{}).canCommit); EXPECT_EQ(fingerprint,profileStateFingerprint(p));
    ASSERT_TRUE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,incoming},{},{p.revision,"replace"}).succeeded);
    EXPECT_EQ(p.assets.find(incoming)->quantity,1U); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,43210U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(p.assets.find(b.batteryAssetId)->location));
    EXPECT_EQ(queryWeaponLight(p,c(),binding(p),true).remainingMs,600000U);
    const auto after=profileStateFingerprint(p); EXPECT_TRUE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,incoming},{},{0,"replace"}).alreadyCommitted); EXPECT_EQ(after,profileStateFingerprint(p));
    EXPECT_FALSE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,incoming},{},{0,"stale"}).succeeded); EXPECT_EQ(after,profileStateFingerprint(p));
}
TEST(WeaponLightTest, FullStashReplacementFromStackAndRemovalLeaveAllBytesUnchanged) {
    auto p=ready(); const auto b=binding(p); const auto source=find(p,cell);
    while(findFirstProfileFit(p,c(),ProfileContainerId::stash(),c().item(cell),ItemOrientation::Degrees0)) add(p,cell);
    const auto before=profileStateFingerprint(p);
    for(auto incoming:{source,AssetInstanceId{0}}) {
        EXPECT_FALSE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,incoming},{},{p.revision,"no-space"}).succeeded);
        EXPECT_EQ(before,profileStateFingerprint(p));
    }
}
TEST(WeaponLightTest, UsedCellsNeverMergeInEitherDirectionAndFullCellsCanMerge) {
    auto p=ready(); auto used=add(p,cell); auto full=add(p,cell,2); p.assets.findMutable(used)->remainingCharges=500;
    const auto usedLoc=std::get<StoredAssetLocation>(p.assets.find(used)->location), fullLoc=std::get<StoredAssetLocation>(p.assets.find(full)->location);
    const auto before=profileStateFingerprint(p);
    EXPECT_FALSE(executeInventory(p,c(),InventoryMoveCommand{used,0,fullLoc},{p.revision,"merge-used"}).succeeded);
    EXPECT_FALSE(executeInventory(p,c(),InventoryMoveCommand{full,1,usedLoc},{p.revision,"merge-full"}).succeeded); EXPECT_EQ(before,profileStateFingerprint(p));
    auto second=add(p,cell,2); ASSERT_TRUE(executeInventory(p,c(),InventoryMoveCommand{second,0,fullLoc},{p.revision,"merge-ok"}).succeeded);
    EXPECT_EQ(p.assets.find(full)->quantity,4U); EXPECT_EQ(p.assets.find(full)->remainingCharges,600000U);
    p.assets.findMutable(used)->quantity=2; EXPECT_FALSE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, OnlyOutputConsumesEnergyAndRestoringCannotRecharge) {
    auto p=ready(); on(p); const auto b=binding(p); const auto rev=p.revision;
    advanceWeaponLight(p,c(),b,false,10000); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,600000U);
    advanceWeaponLight(p,c(),b,true,480); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,120000U);
    advanceWeaponLight(p,c(),b,true,25); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,95000U);
    EXPECT_TRUE(queryWeaponLight(p,c(),b,true).flickering); EXPECT_FALSE(queryWeaponLight(p,c(),b,true).output);
    advanceWeaponLight(p,c(),b,true,.15F); EXPECT_TRUE(queryWeaponLight(p,c(),b,true).interrupted);
    advanceWeaponLight(p,c(),b,true,10000); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,95000U);
    ASSERT_TRUE(restoreWeaponLight(p,c(),b)); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,95000U);
    for(int n=0;n<3;++n) { advanceWeaponLight(p,c(),b,true,25); advanceWeaponLight(p,c(),b,true,.15F); ASSERT_TRUE(restoreWeaponLight(p,c(),b)); }
    EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,20000U);
    advanceWeaponLight(p,c(),b,true,20); EXPECT_EQ(p.assets.find(b.batteryAssetId),nullptr);
    EXPECT_TRUE(p.assets.find(b.deviceAssetId)->deviceState->switchOn); EXPECT_FALSE(queryWeaponLight(p,c(),b,true).output);
    EXPECT_EQ(p.revision,rev); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, FractionalFramesAndSwitchIntentNeverResetLowFault) {
    auto p=ready(); on(p); const auto b=binding(p);
    for(int n=0;n<1000;++n) advanceWeaponLight(p,c(),b,true,.0005F);
    EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,599500U);
    p.assets.findMutable(b.batteryAssetId)->remainingCharges=120000;
    advanceWeaponLight(p,c(),b,true,25.2F); ASSERT_TRUE(queryWeaponLight(p,c(),b,true).interrupted);
    ASSERT_TRUE(executeDeviceSwitch(p,c(),b.deviceAssetId,{p.revision,"off"}).succeeded);
    ASSERT_TRUE(executeDeviceSwitch(p,c(),b.deviceAssetId,{p.revision,"back-on"}).succeeded);
    EXPECT_TRUE(queryWeaponLight(p,c(),b,true).interrupted); EXPECT_FALSE(queryWeaponLight(p,c(),b,true).output);
    ASSERT_TRUE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,find(p,cell)},{},{p.revision,"reseat"}).succeeded);
    EXPECT_TRUE(queryWeaponLight(p,c(),binding(p),true).output);
}
TEST(WeaponLightTest, Schema53RoundTripLegacyAndMalformedGraphs) {
    auto p=ready(); on(p); auto b=binding(p); advanceWeaponLight(p,c(),b,true,501.123F);
    const auto json=serializeProfileEnvelope(p,c().contentVersion()); auto result=deserializeProfileEnvelope(json,c());
    ASSERT_TRUE(result.profile)<<result.message; EXPECT_EQ(profileStateFingerprint(p),profileStateFingerprint(*result.profile));
    EXPECT_THROW(static_cast<void>(serializeProfileEnvelope(p,c().contentVersion(),52)),std::invalid_argument);
    auto old=makeNewPublishedProfile("legacy",c()); auto migrated=deserializeProfileEnvelope(serializeProfileEnvelope(old,"raid-armored-infected-content-64",52),c());
    ASSERT_TRUE(migrated.profile)<<migrated.message; EXPECT_EQ(find(*migrated.profile,lamp),0); EXPECT_EQ(find(*migrated.profile,cell),0);
    auto invalid=p; invalid.assets.findMutable(b.batteryAssetId)->location=InstalledDeviceBatteryLocation{find(p,rifle)}; EXPECT_FALSE(validateProfileState(invalid,c()).valid);
    invalid=p; add(invalid,cell); invalid.assets.findMutable(find(invalid,cell))->location=InstalledDeviceBatteryLocation{b.deviceAssetId}; EXPECT_FALSE(validateProfileState(invalid,c()).valid);
    invalid=p; invalid.assets.findMutable(b.deviceAssetId)->deviceState.reset(); EXPECT_FALSE(validateProfileState(invalid,c()).valid);
}
TEST(WeaponLightTest, DeathAndNpcRecoveryKeepBatteryWithItsParentAndCharge) {
    auto p=ready(); auto b=binding(p); p.assets.findMutable(b.batteryAssetId)->remainingCharges=77123; deploy(p);
    ASSERT_TRUE(settlePendingRaid(p,c(),"light-result",RaidResultOutcome::PlayerDead).succeeded);
    EXPECT_EQ(lostRaidRecordForAsset(p,b.batteryAssetId),lostRaidRecordForAsset(p,b.weaponAssetId));
    auto result=deserializeProfileEnvelope(serializeProfileEnvelope(p,c().contentVersion()),c()); ASSERT_TRUE(result.profile)<<result.message;
    p=*result.profile; ASSERT_TRUE(executeStartRecoveryTask(p,c(),"light-result",{p.revision,"recover"}).succeeded);
    const bool recovered=p.recoveryTask->recoveredAssetIds.contains(b.weaponAssetId);
    EXPECT_EQ(p.recoveryTask->recoveredAssetIds.contains(b.deviceAssetId),recovered); EXPECT_EQ(p.recoveryTask->recoveredAssetIds.contains(b.batteryAssetId),recovered);
    p.worldClock.elapsedWorldMinutes=p.recoveryTask->completionWorldMinute; ASSERT_TRUE(applyRecoveryTaskThrough(p).becameReady);
    ASSERT_TRUE(executeCollectRecoveryTask(p,c(),{p.revision,"collect"}).succeeded);
    if(recovered) { ASSERT_NE(p.assets.find(b.batteryAssetId),nullptr); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,77123U); }
    else EXPECT_EQ(p.assets.find(b.batteryAssetId),nullptr);
    EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, RaidOverflowIsOwnedVisiblePickupAndSettlementDoesNotRewardAbandonedCells) {
    auto p=ready(); const auto b=binding(p); deploy(p);
    auto receipt=executeDeviceBatteryChange(p,c(),{b.deviceAssetId,0},{true,RaidDroppedBatteryLocation{"light-raid",outdoorRaidSpaceId(),{300,300}}},{p.revision,"remove"});
    ASSERT_TRUE(receipt.succeeded)<<receipt.message; // Empty backpack receives cell before ground fallback.
    EXPECT_TRUE(assetIsCarried(p,b.batteryAssetId));
    auto *old=p.assets.findMutable(b.batteryAssetId); old->location=RaidDroppedBatteryLocation{"light-raid",outdoorRaidSpaceId(),{300,300}};
    auto index=RaidSpaceBlockerIndex::build({2000,2000},{}); RaidObservationKey key{outdoorRaidSpaceId(),{270,300},{1,0},110,500,RaidLightEnvironment::WeakIndoor,1};
    auto view=buildRaidSpaceConeVisibility(*index,key); const auto before=profileStateFingerprint(p);
    auto stale=key; stale.direction={-1,0}; EXPECT_FALSE(pickupDroppedBattery(p,c(),b.batteryAssetId,{p.revision,"stale"},&view,&stale).succeeded); EXPECT_EQ(before,profileStateFingerprint(p));
    ASSERT_TRUE(pickupDroppedBattery(p,c(),b.batteryAssetId,{p.revision,"pickup"},&view,&key).succeeded); EXPECT_TRUE(assetIsCarried(p,b.batteryAssetId));
    p.assets.findMutable(b.batteryAssetId)->location=RaidDroppedBatteryLocation{"light-raid",outdoorRaidSpaceId(),{300,300}};
    ASSERT_TRUE(settlePendingRaid(p,c(),"light-result",RaidResultOutcome::Extracted).succeeded); EXPECT_EQ(p.assets.find(b.batteryAssetId),nullptr); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, ExactUnionKeepsTransitionNotchesAndWalls) {
    auto index=RaidSpaceBlockerIndex::build({4000,4000},{}); const Vec2 o{2000,2000};
    RaidObservationKey key{outdoorRaidSpaceId(),o,{1,0},140,800,RaidLightEnvironment::DayOutdoor,1,11,12,120,900};
    const auto view=buildRaidSpaceConeVisibility(*index,key);
    if(!view.validGeometry()) if(const char *dir=std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR")) {nlohmann::json j; j["angle"]=key.angleDegrees;j["distance"]=key.maximumDistance;j["observer"]={key.observer.x,key.observer.y};j["direction"]={key.direction.x,key.direction.y};for(auto v:view.boundary)j["boundary"].push_back({v.x,v.y});std::ofstream(std::filesystem::path(dir)/"invalid-lamp-proof.json")<<j.dump(2);}
    ASSERT_TRUE(view.validGeometry()); ASSERT_TRUE(view.matches(key));
    EXPECT_FALSE(view.pointVisible(polar(o,65,850))); EXPECT_TRUE(view.pointVisible(polar(o,65,700))); EXPECT_TRUE(view.pointVisible(polar(o,55,850))); EXPECT_FALSE(view.pointVisible(polar(o,150,50)));
    const std::vector<BallisticBlocker> blockers{{1,{{2100,1700},{20,600}}}}; auto walls=RaidSpaceBlockerIndex::build({4000,4000},blockers); const auto blocked=buildRaidSpaceConeVisibility(*walls,key);
    ASSERT_TRUE(blocked.validGeometry()); EXPECT_FALSE(blocked.pointVisible({2300,2000})); EXPECT_TRUE(blocked.pointVisible({2050,2000}));
    auto forged=view; for(auto &v:forged.boundary) if(std::hypot(v.x-o.x,v.y-o.y)>799 && std::hypot(v.x-o.x,v.y-o.y)<801) {const auto a=std::atan2(v.y-o.y,v.x-o.x);v=polar(o,a*180/3.14159265359F,900);}
    EXPECT_FALSE(forged.validGeometry());
    auto bridge=view;
    for(std::size_t i=0;i<bridge.boundary.size();++i) {
        const auto v=bridge.boundary[i]; const auto a=std::atan2(v.y-o.y,v.x-o.x)*180/3.14159265359F;
        if(std::abs(a+60)<.001F && std::hypot(v.x-o.x,v.y-o.y)<801) {bridge.boundary.erase(bridge.boundary.begin()+i);break;}
    }
    EXPECT_FALSE(bridge.validGeometry());
}
TEST(WeaponLightTest, FullCarriedContainersDropOnlyTheOutgoingCellWithoutChangingLootBudgets) {
    auto p=ready(); auto b=binding(p); const auto source=find(p,cell);
    const auto bag=find(p,ItemDefinitionId{"item.container.backpack_small"});
    equip(p,bag,EquipmentSlotKind::Backpack,"bag"); equip(p,find(p,rifle),EquipmentSlotKind::PrimaryWeapon,"rifle");
    const auto container=ProfileContainerId::compartment(bag,0);
    const auto fit=findFirstProfileFit(p,c(),container,c().item(cell),ItemOrientation::Degrees0); ASSERT_TRUE(fit);
    ASSERT_TRUE(executeInventory(p,c(),InventoryMoveCommand{source,0,{container,*fit}},{p.revision,"carry"}).succeeded);
    while(const auto pos=findFirstProfileFit(p,c(),container,c().item(cell),ItemOrientation::Degrees0)) static_cast<void>(p.assets.create(c().item(cell),StoredAssetLocation{container,*pos}));
    ASSERT_TRUE(executeDeploy(p,c(),DeployCommand{"light-raid","light-result",7301,MapDefinitionId{"map.v0.test"},{},std::nullopt},{p.revision,"deploy"}).succeeded);
    const auto loot=p.pendingRaid->loot.size(); const auto rules=p.pendingRaid->rulesVersion;
    p.assets.findMutable(b.batteryAssetId)->remainingCharges=11223;
    ASSERT_TRUE(executeDeviceBatteryChange(p,c(),{b.deviceAssetId,source},{true,RaidDroppedBatteryLocation{"light-raid",outdoorRaidSpaceId(),{300,300}}},{p.revision,"overflow"}).succeeded);
    ASSERT_TRUE(std::holds_alternative<RaidDroppedBatteryLocation>(p.assets.find(b.batteryAssetId)->location));
    EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,11223U); EXPECT_EQ(p.assets.find(source)->quantity,1U);
    EXPECT_EQ(p.pendingRaid->loot.size(),loot); EXPECT_EQ(p.pendingRaid->rulesVersion,rules);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(p,c().contentVersion()),c()); ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(profileStateFingerprint(*loaded.profile),profileStateFingerprint(p));
    ASSERT_TRUE(settlePendingRaid(p,c(),"light-result",RaidResultOutcome::PlayerDead).succeeded);
    EXPECT_EQ(p.assets.find(b.batteryAssetId),nullptr); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, ExtractionAndSelfRecoveryPreserveActualChargeWithoutFreeCells) {
    auto p=ready(); on(p); const auto b=binding(p); deploy(p); advanceWeaponLight(p,c(),b,true,501);
    ASSERT_TRUE(validateProfileState(p,c()).valid);
    auto exhausted=p;
    for(int n=0;n<5 && exhausted.assets.find(b.batteryAssetId);++n) {
        advanceWeaponLight(exhausted,c(),b,true,30);
        static_cast<void>(restoreWeaponLight(exhausted,c(),b));
    }
    EXPECT_EQ(exhausted.assets.find(b.batteryAssetId),nullptr);
    EXPECT_TRUE(validateProfileState(exhausted,c()).valid);
    ASSERT_TRUE(settlePendingRaid(exhausted,c(),"light-result",RaidResultOutcome::Extracted).succeeded);
    auto extracted=p; ASSERT_TRUE(settlePendingRaid(extracted,c(),"light-result",RaidResultOutcome::Extracted).succeeded);
    const auto loaded=deserializeProfileEnvelope(serializeProfileEnvelope(extracted,c().contentVersion()),c()); ASSERT_TRUE(loaded.profile)<<loaded.message;
    EXPECT_EQ(loaded.profile->assets.find(b.batteryAssetId)->remainingCharges,99000U);
    ASSERT_TRUE(settlePendingRaid(p,c(),"light-result",RaidResultOutcome::PlayerDead).succeeded);
    ASSERT_TRUE(executeDeploy(p,c(),DeployCommand{"recover-light","recovered-light",9403,MapDefinitionId{"map.v0.test"},{},"light-result"},{p.revision,"self-recovery"}).succeeded);
    ASSERT_TRUE(openObservedRaidSelfRecovery(p,c(),{p.revision,"open"}).succeeded);
    ASSERT_TRUE(pickupObservedRaidLoot(p,c(),b.weaponAssetId,{p.revision,"pickup"}).succeeded);
    ASSERT_TRUE(settlePendingRaid(p,c(),"recovered-light",RaidResultOutcome::Extracted).succeeded);
    ASSERT_NE(p.assets.find(b.batteryAssetId),nullptr); EXPECT_EQ(p.assets.find(b.batteryAssetId)->remainingCharges,99000U); EXPECT_TRUE(validateProfileState(p,c()).valid);
}
TEST(WeaponLightTest, AllHeadingsAndEnvironmentTransitionsKeepProofValidAtFarCoordinates) {
    auto index=RaidSpaceBlockerIndex::build({25600,14400},{});
    for(int n=0;n<360;++n) for(float t:{0.0F,.25F,.5F,.75F,1.0F}) {
        const float a=n*3.14159265359F/180;
        RaidObservationKey key{outdoorRaidSpaceId(),{22051,6031},{std::cos(a),std::sin(a)},180-70*t,1000-500*t,RaidLightEnvironment::DayOutdoor,1,11,12,120,900};
        const auto view=buildRaidSpaceConeVisibility(*index,key);
        if(!view.validGeometry()) if(const char *dir=std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR")) {nlohmann::json j; j["angle"]=key.angleDegrees;j["distance"]=key.maximumDistance;j["n"]=n;j["observer"]={key.observer.x,key.observer.y};j["direction"]={key.direction.x,key.direction.y};j["boundary"]=nlohmann::json::array();for(auto v:view.boundary)j["boundary"].push_back({v.x,v.y});std::ofstream(std::filesystem::path(dir)/"invalid-lamp-proof.json")<<j.dump(2);}
        ASSERT_TRUE(view.validGeometry())<<n<<":"<<t;
        EXPECT_TRUE(view.pointVisible({key.observer.x+key.direction.x*100,key.observer.y+key.direction.y*100}));
    }
}
TEST(WeaponLightTest, NearbyBackgroundNeverGrantsRearInformationAndPowerChangeInvalidatesCache) {
    RaidWorldConfig cfg; cfg.lightConeObservationEnabled=true; cfg.worldSize={3000,3000}; cfg.playerSpawn={975,975}; cfg.extractionPoint={{2800,2800},{100,100}};
    GameplayWorld world(cfg); GameplayInput input; input.aimWorldPosition={1500,1000}; world.setRaidDaylight(false); world.update(input,.6F);
    const auto original=world.raidObservation(); world.setWeaponLight(11,12,120,900,120); const auto lit=world.raidObservation(); ASSERT_TRUE(lit.validGeometry()); EXPECT_NE(lit.lightAssetId,original.lightAssetId);
    EXPECT_FALSE(world.raidBoundsObservable({{940,995},{10,10}})); bool background=false;
    for(const auto &v:world.weaponLightBackgroundObservations()) {EXPECT_TRUE(v.validGeometry()); background|=v.pointVisible({950,1000});} EXPECT_TRUE(background);
    world.setWeaponLight(0,0,0,0,0); ASSERT_EQ(world.raidObservation().boundary.size(),original.boundary.size()); for(std::size_t i=0;i<original.boundary.size();++i) {EXPECT_FLOAT_EQ(world.raidObservation().boundary[i].x,original.boundary[i].x);EXPECT_FLOAT_EQ(world.raidObservation().boundary[i].y,original.boundary[i].y);}
}

namespace {
void measurePoweredLight(const RaidSpaceBlockerIndex &index,const RaidSpaceDefinitionId &space,Vec2 origin,
                         RaidLightEnvironment environment,const std::string &name) {
    std::ofstream csv;
    if(const char *dir=std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR")) {
        std::filesystem::create_directories(dir);
        csv.open(std::filesystem::path(dir)/"weapon-light-performance.csv",std::ios::app);
    }
    for(int count:{32,100}) {
        std::vector<double> samples; volatile int seen=0; auto p=ready(); on(p); const auto battery=binding(p);
        const auto parameters=raidSightParameters(environment);
        for(int n=0;n<160;++n) {
            const float a=n*.09F;
            RaidObservationKey key{space,{origin.x+3*std::sin(a),origin.y+3*std::cos(a)},
                {std::cos(a),std::sin(a)},parameters.angleDegrees,parameters.maximumDistance,environment,1,11,12,120,900};
            const auto start=std::chrono::steady_clock::now();
            advanceWeaponLight(p,c(),battery,true,.016F); static_cast<void>(queryWeaponLight(p,c(),battery,true));
            const auto view=buildRaidSpaceConeVisibility(index,key); RaidVisibilityBoundsQuery query(view);
            for(int side=0;side<2;++side) {
                auto near=key; near.direction={side?-1.0F:1.0F,0}; near.angleDegrees=180; near.maximumDistance=120;
                near.lightAssetId=near.lightBatteryId=0; near.lightAngleDegrees=near.lightDistance=0;
                const auto weak=buildRaidSpaceConeVisibility(index,near); static_cast<void>(weak);
            }
            for(int i=0;i<count;++i) {
                const auto pos=polar(key.observer,i*360.0F/count,100+(i%7)*110.0F);
                seen=seen+(query.boundsVisible({pos,{50,50}})?1:0);
            }
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(n>=10) samples.push_back(elapsed);
        }
        ASSERT_EQ(samples.size(),150U); std::sort(samples.begin(),samples.end()); const auto p95=samples[142];
        if(csv) csv<<name<<','<<count<<",150,"<<p95<<','<<samples.back()<<'\n';
        EXPECT_LE(p95,3.0)<<name<<':'<<count;
        testing::Test::RecordProperty(name+"_"+std::to_string(count)+"_p95_ms",std::to_string(p95));
    }
}
}
TEST(WeaponLightTest, LampAndNearBackgroundPreparationMeetUnchanged3msBudgetFor32And100Targets) {
    std::vector<BallisticBlocker> walls;
    for(int n=0;n<48;++n) {
        const auto a=n*6.28318530718F/48;
        walls.push_back({static_cast<std::uint64_t>(n+1),{{1000+std::cos(a)*300,1000+std::sin(a)*300},{5,40}}});
    }
    const auto index=RaidSpaceBlockerIndex::build({3000,3000},walls); ASSERT_TRUE(index);
    measurePoweredLight(*index,outdoorRaidSpaceId(),{1000,1000},RaidLightEnvironment::DayOutdoor,"dense-48-day");
    measurePoweredLight(*index,outdoorRaidSpaceId(),{1000,1000},RaidLightEnvironment::WeakIndoor,"dense-48-weak");
}
TEST(WeaponLightTest, PoweredPublishedSpacesKeepTheOriginalObservationBudget) {
    for(const auto map:{MapDefinitionId{"map.raid.frontier_exchange"},MapDefinitionId{"map.raid.hospital_district"}}) {
        GameSession session; ASSERT_TRUE(session.startNewProfile("light-perf")); ASSERT_TRUE(session.deployAlpha(9917,map));
        const auto &raid=*session.profile().pendingRaid; std::vector<BallisticBlocker> walls;
        for(const auto &r:raid.spatialLayout.ballisticBlockers) walls.push_back({static_cast<std::uint64_t>(walls.size()+1),{r.position,r.size}});
        const auto index=RaidSpaceBlockerIndex::build(c().map(map).worldSize,walls); ASSERT_TRUE(index);
        const auto p=session.world().player().position(); const auto half=session.world().player().size()/2;
        for(auto environment:{RaidLightEnvironment::DayOutdoor,RaidLightEnvironment::NightOutdoor})
            measurePoweredLight(*index,outdoorRaidSpaceId(),{p.x+half,p.y+half},environment,std::string(map.value())+(environment==RaidLightEnvironment::DayOutdoor?"-day":"-night"));
        for(const auto &room:raid.interiors) {
            walls.clear(); for(const auto &r:room.ballisticBlockers) walls.push_back({static_cast<std::uint64_t>(walls.size()+1),{r.position,r.size}});
            const auto index=RaidSpaceBlockerIndex::build(room.worldSize,walls); ASSERT_TRUE(index);
            measurePoweredLight(*index,room.id,{room.interiorSpawn.x+half,room.interiorSpawn.y+half},RaidLightEnvironment::WeakIndoor,std::string(room.id.value()));
        }
    }
}
TEST(WeaponLightTest, PoweredFourConsumerQueriesReuseCurrentSnapshotWithin3ms) {
    for(int count:{32,100}) {
        RaidWorldConfig config;config.lightConeObservationEnabled=true;config.worldSize={3000,3000};config.playerSpawn={984,984};config.extractionPoint={{2800,2800},{100,100}};
        for(int n=0;n<48;++n) {const auto a=n*6.28318530718F/48;config.ballisticBlockers.push_back({static_cast<std::uint64_t>(n+1),{{1000+std::cos(a)*300,1000+std::sin(a)*300},{5,40}}});}
        for(int n=0;n<count;++n) {const auto p=polar({1000,1000},n*360.0F/count,100+(n%7)*90.0F);config.initialEnemies.push_back({p,{50,50},100});}
        GameplayWorld world(config);world.setWeaponLight(11,12,120,900,120);std::vector<double> samples;volatile int seen=0;
        for(int n=0;n<160;++n) {
            const float a=n*.09F;const Vec2 center{1000+3*std::sin(a),1000+3*std::cos(a)};
            ASSERT_TRUE(const_cast<Player&>(world.player()).setPosition({center.x-16,center.y-16}));
            GameplayInput i;i.aimWorldPosition=Vec2{center.x+std::cos(a)*400,center.y+std::sin(a)*400};i.aimWorldBounds=Rect{{},config.worldSize};world.update(i,0);
            const auto solves=world.enemyObservationSolveCount();const auto start=std::chrono::steady_clock::now();
            static_cast<void>(world.raidObservation());for(int consumer=0;consumer<4;++consumer)for(const auto &enemy:world.enemies())seen=seen+(world.raidEnemyObservable(enemy.combatTargetId())?1:0);
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            EXPECT_EQ(world.enemyObservationSolveCount()-solves,static_cast<std::uint64_t>(count));if(n>=10)samples.push_back(elapsed);
        }
        ASSERT_EQ(samples.size(),150U);std::sort(samples.begin(),samples.end());EXPECT_LE(samples[142],3.0)<<count;
        RecordProperty("powered_four_consumers_"+std::to_string(count)+"_p95_ms",std::to_string(samples[142]));
    }
}
