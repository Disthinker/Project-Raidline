#include "weapon_handling_fixture.h"
#include "base_world.h"
#include "developer_test_range.h"
#include "game_flow.h"
#include "profile_ui_test_wait.h"
#include "test_temp_directory.h"

#include <gtest/gtest.h>
#include <fstream>
#include <limits>
#include <set>

// This executable has one test translation unit. Use the existing World friend
// only to observe physical shot/recoil state; all updates use real services.
struct EnemyLifecycleTestAccess {
    static WorldShootingCheckpoint shooting(const BaseWorld &world){return world.shooting_.checkpoint();}
};

namespace {
GameplayInput releaseIntent(AssetInstanceId weapon,std::uint64_t token,float age=0)
{
    auto input=handlingReloadIntent(weapon,RaidReloadInputKind::Automatic,std::nullopt,age);
    input.raidReloadIntents.front().gestureId=token;return input;
}
GameplayInput urgentIntent(AssetInstanceId weapon,std::uint64_t token,double interval,float age=0)
{
    auto input=handlingReloadIntent(weapon,RaidReloadInputKind::UpgradeEmergency,std::nullopt,age);
    auto &intent=input.raidReloadIntents.front();intent.gestureId=token;intent.doubleTapSeconds=interval;return input;
}
WeaponHandlingLoadout prepareBase(GameSession &session,BaseWorld &world)
{
    handlingRequire(session.startNewProfile("world-weapon-runtime"),"new Base profile");
    session.bindBasePlayerWorld(&world);
    const auto &content=publishedContentRegistry();
    const auto gun=handlingAssets(session.profile(),alpha_content::rifle).front();
    const auto rig=handlingAssets(session.profile(),alpha_content::chestRig).front();
    const auto pack=handlingAssets(session.profile(),alpha_content::backpack).front();
    const auto mags=handlingAssets(session.profile(),alpha_content::magazine);
    const auto ammo=handlingAssets(session.profile(),alpha_content::ammunition);
    for(const auto &[id,slot]:std::array{std::pair{gun,EquipmentSlotKind::PrimaryWeapon},std::pair{rig,EquipmentSlotKind::ChestRig},std::pair{pack,EquipmentSlotKind::Backpack}})
        handlingRequire(session.executeProfileInventory(InventoryEquipCommand{id,slot},"base-equip-"+std::to_string(id)).succeeded,"equip Base participant");
    for(unsigned i=0;i<2;++i) {
        handlingRequire(session.executeProfileWeaponAmmo(LoadMagazineCommand{mags[i],ammo[i],i?20U:10U},"base-load-"+std::to_string(i)).succeeded,"load Base magazine");
        handlingRequire(session.executeProfileInventory(InventoryMoveCommand{mags[i],0,{ProfileContainerId::compartment(rig,0),{static_cast<int>(i),0}}},"base-store-"+std::to_string(i)).succeeded,"store Base magazine");
    }
    handlingRequire(session.executeProfileWeaponAmmo(InstallMagazineAndChamberCommand{gun,mags[0]},"base-install").succeeded,"install Base magazine");
    static_cast<void>(session.updateBaseWorld(world,{},0));
    handlingRequire(validateProfileState(session.profile(),content).valid,"valid real Base profile");
    return {gun,mags[0],mags[1],rig};
}
AssetInstanceId addChestSupply(GameSession &session,AssetInstanceId rig,const ItemDefinition &definition)
{
    auto &profile=WeaponHandlingTestAccess::profile(session);
    const auto pocket=ProfileContainerId::compartment(rig,0);
    const auto fit=findFirstProfileFit(profile,publishedContentRegistry(),pocket,definition,ItemOrientation::Degrees0);
    handlingRequire(fit.has_value(),"real chest pocket has room");
    return profile.assets.create(definition,StoredAssetLocation{pocket,*fit});
}
const ItemDefinitionId kActivityLamp{"item.component.weapon_flashlight_basic"};
const ItemDefinitionId kActivityCell{"item.battery.universal"};
AssetInstanceId addBatteryParticipant(GameSession &session,const ItemDefinitionId &definition,
    ProfileContainerId container,std::uint32_t quantity=1)
{
    auto &profile=WeaponHandlingTestAccess::profile(session);const auto &content=publishedContentRegistry();
    const auto fit=findFirstProfileFit(profile,content,container,content.item(definition),ItemOrientation::Degrees0);
    handlingRequire(fit.has_value(),"battery fixture has a legal actual storage cell");
    return profile.assets.create(content.item(definition),StoredAssetLocation{container,*fit},quantity);
}
AssetInstanceId activityInstalledCell(const ProfileState &profile,AssetInstanceId device)
{
    for(const auto &[id,asset]:profile.assets.records())
        if(const auto *where=std::get_if<InstalledDeviceBatteryLocation>(&asset.location);where && where->deviceAssetId==device)return id;
    return 0;
}
void fillActivityCarriedCells(GameSession &session)
{
    auto &profile=WeaponHandlingTestAccess::profile(session);const auto &content=publishedContentRegistry();
    for(const auto slot:{EquipmentSlotKind::Backpack,EquipmentSlotKind::ChestRig}) {
        const auto owner=equippedAsset(profile,slot);if(!owner)continue;
        const auto count=content.item(profile.assets.find(*owner)->definitionId).containerCompartments.size();
        for(std::uint32_t i=0;i<count;++i) {
            const auto container=ProfileContainerId::compartment(*owner,i);
            while(const auto fit=findFirstProfileFit(profile,content,container,content.item(kActivityCell),ItemOrientation::Degrees0))
                static_cast<void>(profile.assets.create(content.item(kActivityCell),StoredAssetLocation{container,*fit}));
        }
    }
    handlingRequire(validateProfileState(profile,content).valid,"full battery fixture remains a legal Profile");
}
struct LoadedBaseBurst
{
    raidline::test::TemporaryDirectory directory;
    GameFlow flow;
    WeaponHandlingLoadout ids;
    std::set<std::string> historicalTransactions;
    LoadedBaseBurst(std::uint32_t schema,std::optional<std::uint64_t> savedSequence,
        const std::vector<std::uint64_t> &oldFireSuffixes,bool otherNamespaces=false)
    {
        directory.claim("raidline-burst-ids-",std::to_string(schema));
        GameSession source;BaseWorld sourceWorld;ids=prepareBase(source,sourceWorld);
        handlingRequire(source.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon,WeaponFireMode::ThreeRoundBurst},"saved-burst-mode").succeeded,"saved burst mode");
        auto candidate=source.profile();
        if(savedSequence)candidate.basePlayerEffects->dailyCommandSequence=*savedSequence;
        else candidate.basePlayerEffects.reset();
        for(const auto suffix:oldFireSuffixes) {
            candidate.committedTransactions.insert("base-fire:"+candidate.profileId+":"+std::to_string(suffix));++candidate.revision;
        }
        if(otherNamespaces) {
            candidate.committedTransactions.insert("other-prefix:"+candidate.profileId+":3");
            candidate.committedTransactions.insert("base-fire:another-profile:5");
            candidate.revision+=2;
        }
        const auto valid=validateProfileState(candidate,publishedContentRegistry());handlingRequire(valid.valid,valid.message);
        historicalTransactions=candidate.committedTransactions;
        SaveRepository repository{directory.path()};
        {std::ofstream save(repository.primaryPath(),std::ios::binary);save<<serializeProfileEnvelope(candidate,publishedContentRegistry().contentVersion(),schema);handlingRequire(bool(save),"write isolated legacy Profile");}
        flow.configurePersistence(directory.path());handlingRequire(flow.continueGame(),flow.gameSession().persistenceMessage());
        flow.updateBase({},0);handlingRequire(waitForProfileUiDurability(flow.gameSession()),flow.gameSession().persistenceMessage());
    }
};
void expectTwoBaseBursts(LoadedBaseBurst &fixture,const std::vector<std::uint64_t> &expectedSuffixes)
{
    auto &session=fixture.flow.gameSession();auto &world=fixture.flow.baseWorld();
    const auto rounds=magazineRoundCount(session.profile(),fixture.ids.oldMagazine);
    auto nextShot=EnemyLifecycleTestAccess::shooting(world).nextShotId;
    unsigned actualShots{};
    for(unsigned frame=0;frame<10;++frame) {
        GameplayInput input;
        // First click releases immediately. The second is held beyond three
        // rounds and must not start another group until a later fresh press.
        if(frame==0 || frame>=4)input.firePressed=input.firePhysicallyPressed=true;
        input.fireJustPressed=frame==0 || frame==4;
        fixture.flow.updateBase(input,.2F);
        const bool expected=frame<3 || (frame>=4 && frame<7);
        EXPECT_EQ(world.shotFiredLastUpdate(),expected)<<"frame="<<frame;
        if(expected){++actualShots;++nextShot;}
        EXPECT_EQ(EnemyLifecycleTestAccess::shooting(world).nextShotId,nextShot)<<"frame="<<frame;
        EXPECT_EQ(magazineRoundCount(session.profile(),fixture.ids.oldMagazine),rounds-actualShots)<<"frame="<<frame;
        ASSERT_TRUE(waitForProfileUiDurability(session))<<session.persistenceMessage();
    }
    EXPECT_EQ(actualShots,6U);
    for(const auto &old:fixture.historicalTransactions)EXPECT_TRUE(session.profile().committedTransactions.contains(old))<<old;
    std::vector<std::uint64_t> actualSuffixes;
    const auto stem="base-fire:"+session.profile().profileId+":";
    for(const auto &transaction:session.profile().committedTransactions)
        if(transaction.starts_with(stem) && !fixture.historicalTransactions.contains(transaction))
            actualSuffixes.push_back(std::stoull(transaction.substr(stem.size())));
    std::sort(actualSuffixes.begin(),actualSuffixes.end());EXPECT_EQ(actualSuffixes,expectedSuffixes);
}
}

TEST(WorldWeaponActivityRuntime, OneClickCompletesExactlyThreeRealShotsAfterRelease)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon,WeaponFireMode::ThreeRoundBurst},"one-click-mode").succeeded);
    session.update({},0);
    const auto before=magazineRoundCount(session.profile(),ids.oldMagazine);
    GameplayInput press;press.fireJustPressed=press.firePressed=true;
    session.update(press,0);ASSERT_TRUE(session.world().shotFiredLastUpdate());
    session.update({},.2F);ASSERT_TRUE(session.world().shotFiredLastUpdate());
    session.update({},.2F);ASSERT_TRUE(session.world().shotFiredLastUpdate());
    session.update({},.2F);EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(magazineRoundCount(session.profile(),ids.oldMagazine),before-3);
}

TEST(WorldWeaponActivityRuntime, ModalCancelsReleasedBurstWithoutReplayingAfterClose)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon,WeaponFireMode::ThreeRoundBurst},"modal-mode").succeeded);
    session.update({},0);const auto before=magazineRoundCount(session.profile(),ids.oldMagazine);
    GameplayInput press;press.fireJustPressed=press.firePressed=true;session.update(press,0);
    GameplayInput modal;modal.inventoryOpen=true;session.update(modal,.2F);session.update({},.2F);
    EXPECT_EQ(magazineRoundCount(session.profile(),ids.oldMagazine),before-1);
    session.update(press,.2F);EXPECT_TRUE(session.world().shotFiredLastUpdate());
}

TEST(WorldWeaponActivityRuntime, EmergencyUsesRawIntervalAfterActionPassedQuarterSecond)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    session.update(releaseIntent(ids.weapon,71,.2F),.2F);session.update({},.1F);
    ASSERT_TRUE(session.playerActionState().active());
    ASSERT_GT(WeaponHandlingTestAccess::reload(session).elapsedSeconds,.25F);
    session.update(urgentIntent(ids.weapon,71,.20,.4F),.05F);
    ASSERT_TRUE(session.playerActionState().active());
    EXPECT_EQ(WeaponHandlingTestAccess::reload(session).plan.kind,RaidReloadKind::Emergency);
    EXPECT_TRUE(std::holds_alternative<RaidDroppedMagazineLocation>(session.profile().assets.find(ids.oldMagazine)->location));
    EXPECT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
}

TEST(WorldWeaponActivityRuntime, EmptyChamberDoubleRChoosesEmergencyReplacement)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    WeaponHandlingTestAccess::profile(session).assets.findMutable(ids.weapon)->chamberedRound.reset();
    session.update(releaseIntent(ids.weapon,72,.2F),.2F);
    ASSERT_TRUE(std::holds_alternative<ChamberRaidAction>(*session.playerActionState().active()));
    session.update({},.1F);session.update(urgentIntent(ids.weapon,72,.2),.05F);
    ASSERT_TRUE(session.playerActionState().active());
    EXPECT_EQ(WeaponHandlingTestAccess::reload(session).plan.kind,RaidReloadKind::Emergency);
    EXPECT_TRUE(std::holds_alternative<RaidDroppedMagazineLocation>(session.profile().assets.find(ids.oldMagazine)->location));
    EXPECT_FALSE(session.profile().assets.find(ids.weapon)->chamberedRound);
}

TEST(WorldWeaponActivityRuntime, StaleGestureCannotUpgradeAnotherReload)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    session.update(releaseIntent(ids.weapon,80,.2F),.2F);
    session.update(urgentIntent(ids.weapon,79,.1),.1F);
    EXPECT_EQ(WeaponHandlingTestAccess::reload(session).plan.kind,RaidReloadKind::Normal);
    EXPECT_EQ(installedMagazine(session.profile(),ids.weapon),ids.oldMagazine);
}

TEST(WorldWeaponActivityRuntime, BaseUsesRealScopeForStagedReloadWithoutPendingRaid)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    ASSERT_FALSE(session.profile().pendingRaid);
    ASSERT_TRUE(session.playerGameplayContext().available);
    static_cast<void>(session.updateBaseWorld(world,releaseIntent(ids.weapon,90,.2F),.2F));
    ASSERT_TRUE(session.playerActionState().active());
    EXPECT_TRUE(std::holds_alternative<BaseReloadHeldMagazineLocation>(session.profile().assets.find(ids.spareMagazine)->location));
    ASSERT_TRUE(session.profile().baseReloadRecovery);
    for(unsigned i=0;i<10;++i)static_cast<void>(session.updateBaseWorld(world,{},.2F));
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_EQ(installedMagazine(session.profile(),ids.weapon),ids.spareMagazine);
    EXPECT_FALSE(session.profile().baseReloadRecovery);
    ASSERT_TRUE(session.profile().basePlayerEffects);
    EXPECT_EQ(session.profile().basePlayerEffects->scope.activity,WeaponActionActivity::BaseDaily);
    EXPECT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
}

TEST(WorldWeaponActivityRuntime, BaseReloadRestoresAsInterruptedWithoutDuplicatingMagazines)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    static_cast<void>(session.updateBaseWorld(world,releaseIntent(ids.weapon,91,.2F),.2F));
    static_cast<void>(session.updateBaseWorld(world,{},.4F));
    const auto oldRounds=magazineRoundCount(session.profile(),ids.oldMagazine);
    const auto newRounds=magazineRoundCount(session.profile(),ids.spareMagazine);
    ASSERT_TRUE(session.profile().baseReloadRecovery);
    ASSERT_TRUE(session.restoreBasePlayerRuntime(world));
    EXPECT_FALSE(session.playerActionState().active());EXPECT_FALSE(session.profile().baseReloadRecovery);
    EXPECT_EQ(magazineRoundCount(session.profile(),ids.oldMagazine),oldRounds);
    EXPECT_EQ(magazineRoundCount(session.profile(),ids.spareMagazine),newRounds);
    EXPECT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
}

TEST(WorldWeaponActivityRuntime, PhysicalHoldAcrossModalRequiresActualRelease)
{
    GameSession session;const auto ids=prepareHandlingSession(session);
    ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon,WeaponFireMode::ThreeRoundBurst},"physical-mode").succeeded);
    session.update({},0);const auto before=magazineRoundCount(session.profile(),ids.oldMagazine);
    GameplayInput press;press.firePressed=press.fireJustPressed=press.firePhysicallyPressed=true;
    session.update(press,0);
    GameplayInput modal;modal.inventoryOpen=true;modal.firePhysicallyPressed=true;session.update(modal,.2F);
    GameplayInput held;held.firePhysicallyPressed=true;session.update(held,.2F);session.update(held,.2F);
    EXPECT_EQ(magazineRoundCount(session.profile(),ids.oldMagazine),before-1);
    session.update({},0);session.update(press,0);EXPECT_TRUE(session.world().shotFiredLastUpdate());
}

TEST(WorldWeaponActivityRuntime, BaseContinuousMedicalKeepsAppliedPortionAndSingleChargeOnRecovery)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    const auto medical=addChestSupply(session,ids.rig,publishedContentRegistry().item(alpha_content::medkit));
    auto &profile=WeaponHandlingTestAccess::profile(session);profile.currentHealth=40;
    const auto charges=profile.assets.find(medical)->remainingCharges;
    ASSERT_TRUE(session.startAlphaMedical(medical));
    static_cast<void>(session.updateBaseWorld(world,{},1.0F));
    ASSERT_GT(session.profile().currentHealth,40);ASSERT_LT(session.profile().currentHealth,100);
    const int healed=session.profile().currentHealth;
    const auto *remaining=session.profile().assets.find(medical);
    const auto chargesAfter=remaining?remaining->remainingCharges:0U;
    EXPECT_EQ(chargesAfter,charges-1U);
    ASSERT_TRUE(session.profile().basePlayerEffects);
    ASSERT_TRUE(session.profile().basePlayerEffects->medical);
    ASSERT_TRUE(session.restoreBasePlayerRuntime(world));
    EXPECT_FALSE(session.playerActionState().active());
    static_cast<void>(session.updateBaseWorld(world,{},1.0F));
    EXPECT_EQ(session.profile().currentHealth,healed);
    remaining=session.profile().assets.find(medical);
    EXPECT_EQ(remaining?remaining->remainingCharges:0U,chargesAfter);
}

TEST(WorldWeaponActivityRuntime, BaseCoreThrowRejectsBeforeConsumptionAndArmedRecoveryDoesNotConsumeAgain)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    const auto &items=publishedContentRegistry().items();
    const auto definition=std::find_if(items.begin(),items.end(),[](const auto &item){return item.grenadeUse.has_value();});
    ASSERT_NE(definition,items.end());
    const auto grenade=addChestSupply(session,ids.rig,*definition);
    const auto center=session.playerGameplayContext().playerCenter;
    ASSERT_EQ(world.playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
    const auto revision=session.profile().revision;
    EXPECT_FALSE(session.throwThrowable(grenade,{center.x+120,center.y},{revision,"core-reject"}).succeeded);
    EXPECT_NE(session.profile().assets.find(grenade),nullptr);EXPECT_EQ(session.profile().revision,revision);
    const auto parcel=world.baseParcel();
    ASSERT_TRUE(world.restorePlayerBody({parcel.position.x-180,parcel.position.y+parcel.size.y/2},{-1,0},0,0));
    const auto origin=session.playerGameplayContext().playerCenter;
    ASSERT_TRUE(session.throwThrowable(grenade,{origin.x-150,origin.y},{session.profile().revision,"base-real-grenade"}).succeeded);
    EXPECT_EQ(session.profile().assets.find(grenade),nullptr);ASSERT_TRUE(world.grenade().flight());
    ASSERT_TRUE(session.profile().basePlayerEffects);
    const auto phase=world.grenade().flight()->elapsed;
    ASSERT_TRUE(session.restoreBasePlayerRuntime(world));
    ASSERT_TRUE(world.grenade().flight());EXPECT_EQ(world.grenade().flight()->elapsed,phase);
    EXPECT_EQ(session.profile().assets.find(grenade),nullptr);
    EXPECT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
}

TEST(WorldWeaponActivityRuntime, TrainingEmptyChamberNeedsHalfSecondRAndKeepsInfiniteAmmoException)
{
    GameSession session;
    ASSERT_TRUE(session.startDeveloperRange());
    ASSERT_TRUE(session.grantDeveloperRangeLoadout(alpha_content::rifle,alpha_content::ammunition,std::nullopt));
    const auto weapon=session.activeAlphaWeapon();ASSERT_TRUE(weapon);
    const auto magazine=installedMagazine(session.profile(),*weapon);ASSERT_TRUE(magazine);
    auto &profile=WeaponHandlingTestAccess::profile(session);
    profile.assets.findMutable(*weapon)->chamberedRound.reset();
    const auto before=magazineRoundCount(profile,*magazine);
    GameplayInput fire;fire.firePressed=fire.fireJustPressed=true;
    session.update(fire,0);
    EXPECT_FALSE(session.world().shotFiredLastUpdate());
    EXPECT_FALSE(session.profile().assets.find(*weapon)->chamberedRound);
    EXPECT_EQ(magazineRoundCount(session.profile(),*magazine),before);
    session.update(releaseIntent(*weapon,120),0);
    ASSERT_TRUE(session.playerActionState().active());
    ASSERT_TRUE(std::holds_alternative<ChamberRaidAction>(*session.playerActionState().active()));
    session.update({},.49F);EXPECT_FALSE(session.profile().assets.find(*weapon)->chamberedRound);
    session.update({},.01F);ASSERT_TRUE(session.profile().assets.find(*weapon)->chamberedRound);
    EXPECT_EQ(magazineRoundCount(session.profile(),*magazine),before-1U);
    profile.assets.findMutable(*weapon)->chamberedRound.reset();
    const auto infiniteBefore=magazineRoundCount(session.profile(),*magazine);
    session.update({},0);fire.developerInfiniteAmmo=true;session.update(fire,.2F);
    EXPECT_TRUE(session.world().shotFiredLastUpdate());
    EXPECT_EQ(magazineRoundCount(session.profile(),*magazine),infiniteBefore);
    EXPECT_FALSE(session.profile().assets.find(*weapon)->chamberedRound);
}

TEST(WorldWeaponActivityRuntime, BaseCoreBatteryServiceAndDurableCommandUseTheRealWarehouseAccess)
{
    GameSession session;BaseWorld world;static_cast<void>(prepareBase(session,world));
    ASSERT_TRUE(world.canAccessStash());
    const auto device=addBatteryParticipant(session,kActivityLamp,ProfileContainerId::stash());
    const auto source=addBatteryParticipant(session,kActivityCell,ProfileContainerId::stash(),3);
    const auto installed=session.changeDeviceBattery({device,source},"base-core-install");ASSERT_TRUE(installed.succeeded)<<installed.message;
    const auto old=activityInstalledCell(session.profile(),device);ASSERT_NE(old,0U);
    EXPECT_EQ(session.profile().assets.find(source)->quantity,2U);
    static_cast<void>(session.takeProfileUiCompletions());
    const auto queued=session.requestProfileUiCommand(DeviceBatteryCommand{device,source},"base-core-ui-replace");
    ASSERT_TRUE(queued.accepted)<<queued.message;EXPECT_FALSE(queued.pending);
    const auto done=session.takeProfileUiCompletions();ASSERT_EQ(done.size(),1U);ASSERT_TRUE(done.front().succeeded);ASSERT_TRUE(done.front().commandResult);
    const auto *receipt=std::get_if<InventoryReceipt>(&done.front().commandResult->receipt);ASSERT_NE(receipt,nullptr);EXPECT_TRUE(receipt->succeeded);
    EXPECT_NE(activityInstalledCell(session.profile(),device),old);EXPECT_EQ(session.profile().assets.find(source)->quantity,1U);
    ASSERT_TRUE(std::holds_alternative<StoredAssetLocation>(session.profile().assets.find(old)->location));
    EXPECT_EQ(std::get<StoredAssetLocation>(session.profile().assets.find(old)->location).container.kind,ProfileContainerKind::Stash);
    EXPECT_FALSE(session.profile().pendingRaid);EXPECT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
}

TEST(WorldWeaponActivityRuntime, BasePerimeterBatteryUsesCarriedCellsAndRejectsWarehouseAndFullReturnWithoutMutation)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    const auto pocket=ProfileContainerId::compartment(ids.rig,0);
    const auto device=addBatteryParticipant(session,kActivityLamp,pocket);
    const auto source=addBatteryParticipant(session,kActivityCell,pocket,3);
    const auto warehouse=addBatteryParticipant(session,kActivityCell,ProfileContainerId::stash());
    const auto parcel=world.baseParcel();ASSERT_TRUE(world.restorePlayerBody(
        {parcel.position.x-kHomePerimeterTransitionWidth-100,parcel.position.y+100},{1,0},0,0));
    ASSERT_FALSE(world.canAccessStash());ASSERT_EQ(session.playerGameplayContext().activity.activity,WeaponActionActivity::BaseDaily);
    const auto before=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.changeDeviceBattery({device,warehouse},"outside-stash-rejected").succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()),before);
    const auto installed=session.changeDeviceBattery({device,source},"outside-carried-install");ASSERT_TRUE(installed.succeeded)<<installed.message;
    const auto cell=activityInstalledCell(session.profile(),device);ASSERT_NE(cell,0U);EXPECT_TRUE(assetIsCarried(session.profile(),cell));
    fillActivityCarriedCells(session);const auto full=profileStateFingerprint(session.profile());
    const auto rejected=session.changeDeviceBattery({device,0},"outside-full-remove");EXPECT_FALSE(rejected.succeeded);EXPECT_EQ(rejected.error,DomainErrorCode::Capacity);
    EXPECT_EQ(profileStateFingerprint(session.profile()),full);EXPECT_EQ(activityInstalledCell(session.profile(),device),cell);
    EXPECT_FALSE(session.profile().pendingRaid);
}

TEST(WorldWeaponActivityRuntime, RealDefenseBatteryChangeUsesItsEventAndCarriedStorageEvenInsideCore)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    const auto pocket=ProfileContainerId::compartment(ids.rig,0);
    const auto device=addBatteryParticipant(session,kActivityLamp,pocket);
    const auto source=addBatteryParticipant(session,kActivityCell,pocket);
    const auto warehouse=addBatteryParticipant(session,kActivityCell,ProfileContainerId::stash());
    ASSERT_TRUE(session.triggerDeveloperBaseSiegeWarning())<<session.persistenceMessage();
    ASSERT_TRUE(session.startBaseRealtimeDefense(world))<<session.persistenceMessage();
    ASSERT_TRUE(session.baseDefenseActive());ASSERT_TRUE(world.canAccessStash());
    const auto context=session.playerGameplayContext();ASSERT_EQ(context.activity.activity,WeaponActionActivity::BaseDefense);
    EXPECT_EQ(context.activity.activityId,session.profile().activeBaseDefense->eventId);
    const auto installed=session.changeDeviceBattery({device,source},"defense-carried-cell");ASSERT_TRUE(installed.succeeded)<<installed.message;
    const auto before=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.changeDeviceBattery({device,warehouse},"defense-stash-rejected").succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()),before);
    EXPECT_NE(activityInstalledCell(session.profile(),device),0U);EXPECT_FALSE(session.profile().pendingRaid);
}

TEST(WorldWeaponActivityRuntime, TrainingBatteryChangesNeedNoFakeRaidAndLeavingRestoresTheOriginalProfile)
{
    GameFlow flow;ASSERT_TRUE(flow.startNewGame("battery-training-isolation",false));
    ASSERT_TRUE(flow.gameSession().checkpointWorldClock());const auto original=profileStateFingerprint(flow.gameSession().profile());
    ASSERT_TRUE(flow.enterDeveloperRange());auto &session=flow.gameSession();
    ASSERT_TRUE(session.grantDeveloperRangeLoadout(alpha_content::rifle,alpha_content::ammunition,std::nullopt));
    // These mechanics use the existing code fallback assets. They are not in
    // the range's published-art grant catalog, whose refusal stays unchanged.
    const auto beforeGrant=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.grantDeveloperRangeItem(kActivityLamp));EXPECT_FALSE(session.grantDeveloperRangeItem(kActivityCell));
    EXPECT_EQ(profileStateFingerprint(session.profile()),beforeGrant);
    const auto rig=equippedAsset(session.profile(),EquipmentSlotKind::ChestRig);ASSERT_TRUE(rig);
    const auto pocket=ProfileContainerId::compartment(*rig,0);
    // Arrange only the disposable private Profile, as the other runtime
    // fixtures do; all battery transitions still call the real service.
    const auto device=addBatteryParticipant(session,kActivityLamp,pocket);
    const auto source=addBatteryParticipant(session,kActivityCell,pocket);
    ASSERT_TRUE(validateProfileState(session.profile(),publishedContentRegistry()).valid);
    ASSERT_EQ(session.playerGameplayContext().activity.activity,WeaponActionActivity::Training);ASSERT_FALSE(session.profile().pendingRaid);
    const auto installed=session.changeDeviceBattery({device,source},"training-cell");ASSERT_TRUE(installed.succeeded)<<installed.message;
    const auto cell=activityInstalledCell(session.profile(),device);ASSERT_NE(cell,0U);EXPECT_TRUE(assetIsCarried(session.profile(),cell));
    const auto warehouse=addBatteryParticipant(session,kActivityCell,ProfileContainerId::stash());const auto before=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.changeDeviceBattery({device,warehouse},"training-stash-rejected").succeeded);EXPECT_EQ(profileStateFingerprint(session.profile()),before);
    fillActivityCarriedCells(session);const auto full=profileStateFingerprint(session.profile());
    EXPECT_FALSE(session.changeDeviceBattery({device,0},"training-full-remove").succeeded);EXPECT_EQ(profileStateFingerprint(session.profile()),full);
    ASSERT_TRUE(flow.leaveDeveloperRange());EXPECT_EQ(profileStateFingerprint(flow.gameSession().profile()),original);
}

TEST(WorldWeaponActivityRuntime, Schema59BaseBurstSkipsOldFireLedgerOnTheFirstTapAndDoesNotRestartWhileHeld)
{
    LoadedBaseBurst fixture{59,std::nullopt,{1,2,3,4,5,6,7,8}};
    EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(fixture.flow.gameSession()),0U);
    expectTwoBaseBursts(fixture,{9,10,11,12,13,14});
}

TEST(WorldWeaponActivityRuntime, Schema60BaseBurstPreservesSavedSequenceAndSkipsOnlyItsSparseFullIds)
{
    LoadedBaseBurst fixture{60,1U,{2,4,7},true};
    EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(fixture.flow.gameSession()),1U);
    expectTwoBaseBursts(fixture,{3,5,6,8,9,10});
}

TEST(WorldWeaponActivityRuntime, BaseFireTransactionExhaustionRejectsBeforeAnyShotOrRecoilAndNeverWraps)
{
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    for(const bool occupiedLast:{false,true}) {
        SCOPED_TRACE(occupiedLast);
        LoadedBaseBurst fixture{60,occupiedLast?maximum-1:maximum,occupiedLast?std::vector{maximum}:std::vector<std::uint64_t>{}};
        auto &session=fixture.flow.gameSession();auto &world=fixture.flow.baseWorld();
        auto expected=session.profile();expected.basePlayerEffects->dailyCommandSequence=maximum;
        const auto before=EnemyLifecycleTestAccess::shooting(world);
        GameplayInput input;input.firePressed=input.firePhysicallyPressed=input.fireJustPressed=true;
        for(unsigned frame=0;frame<3;++frame) {
            fixture.flow.updateBase(input,0);input.fireJustPressed=false;
            EXPECT_FALSE(world.shotFiredLastUpdate());
        }
        const auto after=EnemyLifecycleTestAccess::shooting(world);
        EXPECT_EQ(after.nextShotId,before.nextShotId);EXPECT_EQ(after.spreadRandomState,before.spreadRandomState);
        EXPECT_EQ(after.recoilRandomState,before.recoilRandomState);EXPECT_EQ(after.aimVectors[6],before.aimVectors[6]);
        EXPECT_EQ(profileStateFingerprint(session.profile()),profileStateFingerprint(expected));
        EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(session),maximum);
        EXPECT_NE(session.persistenceMessage().find("TRANSACTION IDS EXHAUSTED"),std::string::npos);
    }
}

TEST(WorldWeaponActivityRuntime, RaidFireTransactionExhaustionRejectsBeforeAnyShotOrRecoilAndNeverWraps)
{
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    for(const bool occupiedLast:{false,true}) {
        SCOPED_TRACE(occupiedLast);GameSession session;const auto ids=prepareHandlingSession(session);
        ASSERT_TRUE(session.executeProfileWeaponAmmo(SetWeaponFireModeCommand{ids.weapon,WeaponFireMode::ThreeRoundBurst},"overflow-mode").succeeded);
        session.update({},0);WeaponHandlingTestAccess::commandSequence(session,occupiedLast?maximum-1:maximum);
        auto &profile=WeaponHandlingTestAccess::profile(session);
        if(occupiedLast)profile.committedTransactions.insert("fire:"+profile.pendingRaid->raidId+":"+std::to_string(maximum));
        const auto fingerprint=profileStateFingerprint(profile);const auto before=WeaponHandlingTestAccess::shooting(session.world());
        GameplayInput input;input.firePressed=input.firePhysicallyPressed=input.fireJustPressed=true;
        for(unsigned frame=0;frame<3;++frame){session.update(input,0);input.fireJustPressed=false;EXPECT_FALSE(session.world().shotFiredLastUpdate());}
        const auto after=WeaponHandlingTestAccess::shooting(session.world());
        EXPECT_EQ(after.nextShotId,before.nextShotId);EXPECT_EQ(after.spreadRandomState,before.spreadRandomState);
        EXPECT_EQ(after.recoilRandomState,before.recoilRandomState);EXPECT_EQ(after.aimVectors[6],before.aimVectors[6]);
        EXPECT_EQ(profileStateFingerprint(session.profile()),fingerprint);EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(session),maximum);
        EXPECT_NE(session.persistenceMessage().find("TRANSACTION IDS EXHAUSTED"),std::string::npos);
    }
}

TEST(WorldWeaponActivityRuntime, LastUnusedBaseFireIdCommitsOnceAndThePendingBurstCannotWrap)
{
    const auto maximum=std::numeric_limits<std::uint64_t>::max();LoadedBaseBurst fixture{60,maximum-1,{}};
    auto &session=fixture.flow.gameSession();auto &world=fixture.flow.baseWorld();
    const auto rounds=magazineRoundCount(session.profile(),fixture.ids.oldMagazine);const auto before=EnemyLifecycleTestAccess::shooting(world).nextShotId;
    GameplayInput press;press.firePressed=press.firePhysicallyPressed=press.fireJustPressed=true;
    fixture.flow.updateBase(press,.2F);ASSERT_TRUE(world.shotFiredLastUpdate());
    EXPECT_TRUE(session.profile().committedTransactions.contains("base-fire:"+session.profile().profileId+":"+std::to_string(maximum)));
    fixture.flow.updateBase({},.2F);EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_EQ(EnemyLifecycleTestAccess::shooting(world).nextShotId,before+1);
    EXPECT_EQ(magazineRoundCount(session.profile(),fixture.ids.oldMagazine),rounds-1);
    EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(session),maximum);
}

TEST(WorldWeaponActivityRuntime, ReloadAllocationSkipsHistoricalStageFamiliesWithoutReusingTheirParent)
{
    GameSession session;BaseWorld world;const auto ids=prepareBase(session,world);
    auto &profile=WeaponHandlingTestAccess::profile(session);const auto stem="reload-action:"+profile.profileId+":";
    const auto start=WeaponHandlingTestAccess::commandSequence(session);
    for(std::uint64_t offset=1;offset<=3;++offset) {
        profile.committedTransactions.insert(stem+std::to_string(start+offset)+(offset==2?"/cancel":"/0"));++profile.revision;
    }
    const auto historical=profile.committedTransactions;
    ASSERT_TRUE(validateProfileState(profile,publishedContentRegistry()).valid);
    ASSERT_TRUE(session.startAlphaReload(ids.weapon,ids.spareMagazine))<<session.persistenceMessage();
    const auto &action=std::get<ReloadRaidAction>(*session.playerActionState().active());
    const auto parent=action.plan.binding.actionId;EXPECT_EQ(parent,stem+std::to_string(start+4));
    static_cast<void>(session.updateBaseWorld(world,{},.2F));
    EXPECT_TRUE(std::holds_alternative<BaseReloadHeldMagazineLocation>(session.profile().assets.find(ids.spareMagazine)->location));
    EXPECT_TRUE(session.profile().committedTransactions.contains(parent+"/0"));
    for(unsigned frame=0;frame<10;++frame)static_cast<void>(session.updateBaseWorld(world,{},.2F));
    EXPECT_FALSE(session.playerActionState().active());EXPECT_EQ(installedMagazine(session.profile(),ids.weapon),ids.spareMagazine);
    for(const auto &transaction:historical)EXPECT_TRUE(session.profile().committedTransactions.contains(transaction));
}

TEST(WorldWeaponActivityRuntime, FailedBaseBarrierRetriesTheSameFiredCandidateAndTheNextClickUsesNewIds)
{
    LoadedBaseBurst fixture{60,0U,{1,2,3,4}};auto &session=fixture.flow.gameSession();auto &world=fixture.flow.baseWorld();
    const auto rounds=magazineRoundCount(session.profile(),fixture.ids.oldMagazine);
    for(unsigned frame=0;frame<3;++frame) {
        GameplayInput input;if(frame==0)input.firePressed=input.firePhysicallyPressed=input.fireJustPressed=true;
        fixture.flow.updateBase(input,.2F);ASSERT_TRUE(world.shotFiredLastUpdate());ASSERT_TRUE(waitForProfileUiDurability(session));
    }
    EXPECT_EQ(magazineRoundCount(session.profile(),fixture.ids.oldMagazine),rounds-3);
    const auto sequence=WeaponHandlingTestAccess::commandSequence(session);const auto fired=profileStateFingerprint(session.profile());
    const auto obstruction=fixture.directory.path()/"profile.tmp.json";ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    const auto first=session.requestWorldDurabilityBarrier();ASSERT_TRUE(first.accepted);EXPECT_FALSE(waitForProfileUiDurability(session));
    auto failed=session.takeProfileUiCompletions();ASSERT_FALSE(failed.empty());EXPECT_FALSE(failed.back().succeeded);
    EXPECT_EQ(profileStateFingerprint(session.profile()),fired);
    ASSERT_TRUE(std::filesystem::remove(obstruction));const auto retry=session.retryProfileUiCommit();ASSERT_TRUE(retry.accepted);EXPECT_NE(retry.requestId,first.requestId);
    ASSERT_TRUE(waitForProfileUiDurability(session));EXPECT_EQ(profileStateFingerprint(session.profile()),fired);
    EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(session),sequence);
    const auto saved=SaveRepository{fixture.directory.path()}.load(publishedContentRegistry());ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_EQ(profileStateFingerprint(*saved.profile),fired);
    fixture.flow.updateBase({},.2F);
    for(unsigned frame=0;frame<3;++frame) {
        GameplayInput input;if(frame==0)input.firePressed=input.firePhysicallyPressed=input.fireJustPressed=true;
        fixture.flow.updateBase(input,.2F);EXPECT_TRUE(world.shotFiredLastUpdate());
    }
    EXPECT_EQ(magazineRoundCount(session.profile(),fixture.ids.oldMagazine),rounds-6);
    EXPECT_EQ(WeaponHandlingTestAccess::commandSequence(session),sequence+3);
}
