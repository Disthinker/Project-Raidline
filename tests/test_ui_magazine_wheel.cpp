#include "app.h"
#include "alpha_content_ids.h"
#include "noise_decoy_fixture.h"
#include "first_raid_guidance.h"
#include "test_temp_directory.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <utility>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

// Existing service filesystem seam, used here to exercise actual App pending
// and failure flows without changing its public gameplay API.
struct InventoryCheckpointTestAccess {
    static void store(GameSession &session,BaseDefenseCheckpointWriter::WriteFunction write) {
        session.baseDailyWriter_=std::make_unique<BaseDefenseCheckpointWriter>(std::move(write));
    }
    static void defenseStore(GameSession &session,BaseDefenseCheckpointWriter::WriteFunction write) {
        session.baseDefenseWriter_=std::make_unique<BaseDefenseCheckpointWriter>(std::move(write));
    }
};
struct UiMagazineWheelTestAccess {
    static auto &flow(App &a){return a.gameFlow_;}
    static auto &session(App &a){return a.gameSession_;}
    static void frame(App &a,float dt=0){a.processEvents();a.update(dt);a.input_.endFrame();}
    static void open(App &a){a.openMagazineWheel(*a.gameSession_.activeAlphaWeapon());}
    static bool open(const App &a){return a.magazineWheelOpen_;}
    static const auto &ids(const App &a){return a.magazineWheelOptions_;}
    static bool otherWheel(const App &a){return a.medicalWheelOpen_ || a.throwableWheelOpen_;}
    static bool paused(const App &a){return a.pauseMenu_.isOpen();}
    static bool attach(App &a,SDL_Renderer *r,float scale) {
        a.renderer_=r;
        if(!a.uiTextRenderer_.initialize(r))return false;
        a.uiTextRenderer_.setRasterScale(scale);return true;
    }
    static void detach(App &a) {
        a.quickWheelBackdrop_.reset();a.indoorInformationLayer_.reset();a.indoorShadowMask_.reset();
        a.uiTextRenderer_.shutdown();a.renderer_=nullptr;
        for(auto &texture:a.inventoryItemTextures_)texture.reset();
    }
    static auto &text(App &a){return a.uiTextRenderer_;}
    static void draw(App &a) {
        ASSERT_TRUE(a.quickWheelBackdrop_.begin(a.renderer_));
        SDL_SetRenderDrawColor(a.renderer_,45,61,55,255);SDL_RenderClear(a.renderer_);
        SDL_SetRenderDrawColor(a.renderer_,148,164,127,255);
        SDL_FRect sample{490,180,120,350};SDL_RenderFillRect(a.renderer_,&sample);
        ASSERT_TRUE(a.quickWheelBackdrop_.compose());
        a.uiClockSeconds_+=0.2F;
        a.renderQuickWheel(UiQuickWheelKind::Magazine,a.magazineWheelOptions_,a.magazineWheelSelectedIndex_);
    }
    static void hud(App &a){a.renderDebugText();}
    static void cancel(App &a){a.cancelMagazineWheel();}
    static const auto &projection(const App &a){return a.quickWheelProjection_;}
    static const auto &layout(const App &a){return a.quickWheelLayout_;}
    static bool returnToBase(App &a) {
        return a.gameFlow_.returnToMainMenu() && a.gameFlow_.continueGame();
    }
    static void base(App &a){a.renderBase();}
    static void inventory(App &a){a.renderProfileInventory(true,false);}
    static void message(App &a,std::string value){a.uiMessage_=std::move(value);}
    static bool buildOpen(const App &a){return a.baseConstructionPanelOpen_;}
    static bool inventoryOpen(const App &a){return a.inventoryOverlayState_.isOpen();}
    static void openInventory(App &a){a.inventoryOverlayState_.openContainerInventory();}
    static void closeInventory(App &a){a.closeInventory();}
    static bool barrier(const App &a){return static_cast<bool>(a.pendingInventoryBarrierAction_);}
    static void pause(App &a){a.pauseMenu_.open();}
    static void command(App &a,PauseMenuCommand command){a.handlePauseMenuCommand(command);}
    static bool armed(const App &a){return a.baseDefenseAbandonArmed_;}
    static bool busy(const App &a){return a.profileUiBusy();}
    static const auto &message(const App &a){return a.uiMessage_;}
    static bool retry(App &a){return a.retryPendingProfileUi();}
    static auto continuationCount(const App &a){return a.profileUiContinuations_.size();}
    static bool saveProtection(const App &a){return a.raidSaveProtectionVisible();}
    static bool captureWanted(const App &a){return a.shouldCaptureWorldPointer();}
    static float saveScroll(const App &a){return a.baseFinalFeedbackScroll_;}
    static void fullFrame(App &a){a.render();}
    static auto bottom(App &a){return a.baseBottomHudLayout();}
    static auto visibleMessage(const App &a){return a.visibleFeedbackMessage();}
    static void presentation(App &a,float dt){a.updateBasePresentation(dt);}
    static bool feedbackWheel(App &a,const SDL_Event &event){return a.routeFinalFeedbackWheel(event);}
    static float feedbackScroll(const App &a){return a.finalFeedbackScroll_;}
    static bool diagnostics(const App &a){return a.developerPerformanceOverlayOpen_;}
    static bool buyAmmunition(App &a) {
        if(!a.gameFlow_.openBaseFacilityForManagement(BaseFacilityKind::Supply))return false;
        a.supplyUi_.selectTab(UiBaseSupplyTab::Purchase);a.refreshBaseSupplyRows(true);
        const auto row=std::find_if(a.supplyRows_.begin(),a.supplyRows_.end(),[](const auto &entry){return entry.key.definitionId==alpha_content::ammunition;});
        if(row==a.supplyRows_.end())return false;
        a.supplyUi_.selection=row->key;
        const auto button=uiBaseSupplyControlRect(UiBaseSupplyControl::Main);
        const Vec2 point{button.x+button.w/2,button.y+button.h/2};
        a.handleBaseSupplyEvent({UiBaseSupplyEventKind::Down,point});
        a.handleBaseSupplyEvent({UiBaseSupplyEventKind::Up,point});
        return a.profileUiBusy();
    }
    static bool move(App &a,const InventoryCommand &command) {
        return a.submitInventoryUi(command,"ui-inventory-failure",false,"INVENTORY UPDATED",SoundEventId::InventoryMoveOrPlace);
    }
};
namespace {
using A=UiMagazineWheelTestAccess;
void key(SDL_Scancode code,bool down,double seconds=0) {
    SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    e.key.scancode=code;e.key.down=down;
    if(seconds>0)e.common.timestamp=static_cast<Uint64>(seconds*1e9);
    ASSERT_TRUE(SDL_PushEvent(&e));
}
bool waitApp(App &app,const std::function<bool()> &done) {
    const auto deadline=SDL_GetTicksNS()+2'000'000'000ULL;
    while(!done() && SDL_GetTicksNS()<deadline){A::frame(app);SDL_Delay(1);}
    return done();
}
bool waitBaseInitialization(App &app) {
    // The first actual Base frame prepares its initial player checkpoint.
    // Poll that real operation before replacing the store or sending input.
    A::frame(app);
    return waitApp(app,[&] {
        const auto &session=A::session(app);
        return !session.profileUiCommitPending() && !A::busy(app) &&
            !session.inventoryPersistencePending() &&
            !session.baseDailyCheckpointStatus().outstanding() &&
            !session.baseDefenseCheckpointStatus().outstanding() &&
            !session.baseDailySaveBlocked() && !session.baseDefenseSaveBlocked();
    });
}
class UiMagazineWheelTest:public testing::Test {
protected:
    raidline::test::TemporaryDirectory directory;
    std::unique_ptr<App> app;
    std::vector<AssetInstanceId> magazines;
    AssetInstanceId weapon{};
    SDL_Surface *surface{};SDL_Renderer *renderer{};
    ProfileState &profile(){return const_cast<ProfileState &>(A::session(*app).profile());}
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(directory.claim("raidline-magazine-wheel-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        const auto &c=publishedContentRegistry();
        auto p=makeNewPublishedProfile("magazine-wheel",c);
        p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
        weapon=equipDecoyTestGun(p,c);
        AssetInstanceId installed{};
        for(const auto &[id,a]:p.assets.records())if(a.definitionId==alpha_content::magazine){installed=id;break;}
        ASSERT_NE(installed,0U);
        ASSERT_TRUE(executeWeaponAmmo(p,c,InstallMagazineCommand{weapon,installed},
            {p.revision,"magazine-wheel-initial-mag"}).succeeded);
        const auto rig=p.assets.create(c.item(ItemDefinitionId{"item.container.chest_rig_assault"}),
            EquippedAssetLocation{EquipmentSlotKind::ChestRig});
        const auto &definition=c.item(alpha_content::magazine);
        for(const int rounds:{19,7,7,0}) {
            const auto container=ProfileContainerId::compartment(rig,0);
            const auto fit=findFirstProfileFit(p,c,container,definition,ItemOrientation::Degrees0);
            ASSERT_TRUE(fit);
            const auto id=p.assets.create(definition,StoredAssetLocation{container,*fit});
            p.assets.findMutable(id)->magazineRounds.assign(rounds,MagazineRoundRecord{alpha_content::ammunition,std::nullopt});
            magazines.push_back(id);
        }
        p.assets.findMutable(magazines[2])->magazineRounds[2].definitionId=ItemDefinitionId{"item.ammunition.9mm_enhanced"};
        ASSERT_TRUE(validateProfileState(p,c).valid);
        ASSERT_TRUE(SaveRepository(directory.path()).save(p,c.contentVersion()).succeeded);
        app=std::make_unique<App>();
        A::flow(*app).configurePersistence(directory.path());
        ASSERT_TRUE(A::flow(*app).continueGame());ASSERT_TRUE(A::flow(*app).deploy());
        A::frame(*app);
    }
    void detach() {
        if(app)A::detach(*app);
        SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;
    }
    void TearDown() override {
        detach();app.reset();SDL_Quit();ASSERT_NO_THROW(directory.cleanup());
    }
    void attach(UiWindowSize size) {
        surface=SDL_CreateSurface(size.width,size.height,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        ASSERT_TRUE(A::attach(*app,renderer,std::min(size.width/1280.0F,size.height/720.0F)));
    }
    void verifyLegacyBaseBurst(bool mouse) {
        const auto &content=publishedContentRegistry();GameSession setup;
        ASSERT_TRUE(setup.startNewProfile(mouse?"app-legacy-mouse":"app-legacy-space"));
        const auto find=[&](ItemDefinitionId definition) {
            for(const auto &[id,asset]:setup.profile().assets.records())if(asset.definitionId==definition)return id;
            return AssetInstanceId{};
        };
        const auto gun=find(alpha_content::rifle),magazine=find(alpha_content::magazine),ammo=find(alpha_content::ammunition);
        ASSERT_NE(gun,0U);ASSERT_NE(magazine,0U);ASSERT_NE(ammo,0U);
        ASSERT_TRUE(setup.executeProfileInventory(InventoryEquipCommand{gun,EquipmentSlotKind::PrimaryWeapon},"legacy-equip").succeeded);
        ASSERT_TRUE(setup.executeProfileWeaponAmmo(LoadMagazineCommand{magazine,ammo,30},"legacy-load").succeeded);
        ASSERT_TRUE(setup.executeProfileWeaponAmmo(InstallMagazineAndChamberCommand{gun,magazine},"legacy-install").succeeded);
        ASSERT_TRUE(setup.executeProfileWeaponAmmo(SetWeaponFireModeCommand{gun,WeaponFireMode::ThreeRoundBurst},"legacy-mode").succeeded);
        auto legacy=setup.profile();legacy.basePlayerEffects.reset();
        for(int n=1;n<=64;++n) {
            legacy.committedTransactions.insert("base-fire:"+legacy.profileId+":"+std::to_string(n));++legacy.revision;
        }
        ASSERT_TRUE(validateProfileState(legacy,content).valid);
        const auto folder=directory.path()/(mouse?"legacy-mouse":"legacy-space");std::filesystem::create_directories(folder);
        SaveRepository repository(folder);
        {std::ofstream output(repository.primaryPath(),std::ios::binary);
            output<<serializeProfileEnvelope(legacy,content.contentVersion(),59);ASSERT_TRUE(output.good());}
        detach();app=std::make_unique<App>();A::flow(*app).configurePersistence(folder);
        ASSERT_TRUE(A::flow(*app).continueGame());ASSERT_TRUE(waitBaseInitialization(*app));
        ASSERT_EQ(A::session(*app).weaponFireMode(gun),WeaponFireMode::ThreeRoundBurst);
        const auto before=magazineRoundCount(profile(),magazine);
        const auto fire=[&](bool down) {
            if(!mouse){key(SDL_SCANCODE_SPACE,down);return;}
            SDL_Event event{};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.button=SDL_BUTTON_LEFT;event.button.down=down;event.button.x=640;event.button.y=360;
            ASSERT_TRUE(SDL_PushEvent(&event));
        };
        unsigned shots{};fire(true);
        for(int frame=0;frame<7;++frame) {
            if(frame==1)fire(false);
            A::frame(*app,.13F);
            const bool shot=A::flow(*app).baseWorld().shotFiredLastUpdate();
            EXPECT_EQ(shot,frame<3)<<"first burst frame="<<frame;
            shots+=shot;
        }
        EXPECT_EQ(shots,3U);EXPECT_EQ(magazineRoundCount(profile(),magazine),before-3);
        for(int n=1;n<=64;++n)EXPECT_TRUE(profile().committedTransactions.contains(
            "base-fire:"+legacy.profileId+":"+std::to_string(n)));
        // App's audio consumer uses this same committed shot flag; no audio
        // device or speaker playback is initialized by this headless fixture.
        ASSERT_TRUE(A::session(*app).requestWorldDurabilityBarrier().accepted);
        ASSERT_TRUE(waitApp(*app,[&]{return !A::session(*app).profileUiCommitPending();}));
        const auto saved=repository.load(content);ASSERT_TRUE(saved.profile)<<saved.message;
        EXPECT_EQ(magazineRoundCount(*saved.profile,magazine),before-3);
    }
};
}

TEST_F(UiMagazineWheelTest, LegacyBaseFirstMouseClickCommitsExactlyThreeShots) {verifyLegacyBaseBurst(true);}
TEST_F(UiMagazineWheelTest, LegacyBaseFirstSpacePressCommitsExactlyThreeShots) {verifyLegacyBaseBurst(false);}

TEST_F(UiMagazineWheelTest, SameModelDifferentRoundsAndMixedSequenceRemainFourInstances) {
    const auto before=profileStateFingerprint(profile());
    A::open(*app);ASSERT_TRUE(A::open(std::as_const(*app)));
    EXPECT_EQ(A::ids(*app),magazines);
    const auto entries=projectUiQuickWheel(profile(),publishedContentRegistry(),A::ids(*app),
        UiQuickWheelKind::Magazine,weapon);
    ASSERT_EQ(entries.size(),4U);
    EXPECT_EQ(entries[0].count,"19 / 30");EXPECT_EQ(entries[1].count,"7 / 30");
    EXPECT_EQ(entries[2].count,"7 / 30");EXPECT_EQ(entries[3].count,"0 / 30");
    EXPECT_FALSE(entries[1].mixedAmmunition);EXPECT_TRUE(entries[2].mixedAmmunition);
    for(std::size_t i=0;i<entries.size();++i) {
        EXPECT_EQ(entries[i].instanceId,magazines[i]);EXPECT_EQ(entries[i].quantity,1U);
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, MissingSelectedInstanceStaysInvalidInsteadOfSubstitutingAnother) {
    A::open(*app);auto ids=A::ids(*app);
    ASSERT_TRUE(profile().assets.erase(magazines[1]));++profile().revision;
    const auto entries=projectUiQuickWheel(profile(),publishedContentRegistry(),ids,UiQuickWheelKind::Magazine,weapon);
    ASSERT_EQ(entries.size(),4U);EXPECT_EQ(entries[1].instanceId,magazines[1]);
    EXPECT_FALSE(entries[1].usable);EXPECT_EQ(entries[2].instanceId,magazines[2]);
}
TEST_F(UiMagazineWheelTest, EscapeCancelsWithoutInventoryActionPauseOrShot) {
    A::open(*app);const auto before=profileStateFingerprint(profile());
    key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);
    EXPECT_FALSE(A::open(std::as_const(*app)));EXPECT_FALSE(A::paused(*app));
    EXPECT_FALSE(A::session(*app).raidActionState().active());
    EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, SameFrameRightClickCancelsAndDoesNotBecomeAimOrShot) {
    A::open(*app);const auto before=profileStateFingerprint(profile());
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_RIGHT;
    ASSERT_TRUE(SDL_PushEvent(&e));e.type=SDL_EVENT_MOUSE_BUTTON_UP;ASSERT_TRUE(SDL_PushEvent(&e));
    A::frame(*app);
    EXPECT_FALSE(A::open(std::as_const(*app)));EXPECT_FALSE(A::session(*app).raidActionState().active());
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, MagazineWheelBlocksMedicalAndThrowableSelection) {
    A::open(*app);key(SDL_SCANCODE_4,true);key(SDL_SCANCODE_5,true);A::frame(*app);
    EXPECT_TRUE(A::open(std::as_const(*app)));EXPECT_FALSE(A::otherWheel(*app));
}
TEST_F(UiMagazineWheelTest, FocusLossClosesWithoutMovingAnyMagazine) {
    A::open(*app);const auto before=profileStateFingerprint(profile());
    SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    EXPECT_FALSE(A::open(std::as_const(*app)));EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, RDownAloneDoesNotStartReloadAndReleaseStartsExactlyOne) {
    key(SDL_SCANCODE_R,true,10);A::frame(*app);
    EXPECT_FALSE(A::session(*app).raidActionState().active());
    key(SDL_SCANCODE_R,false,10.1);A::frame(*app);
    const auto &action=A::session(*app).raidActionState().active();
    ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).magazineAssetId,magazines[0]);
    EXPECT_FLOAT_EQ(std::get<ReloadRaidAction>(*action).elapsedSeconds,0);
}
TEST_F(UiMagazineWheelTest, LongPressReleaseBetweenFramesUsesExplicitMagazineSelection) {
    key(SDL_SCANCODE_R,true,10);key(SDL_SCANCODE_R,false,10.5);A::frame(*app);
    const auto &action=A::session(*app).raidActionState().active();
    ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).plan.source,RaidReloadSource::SelectedChestRig);
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).magazineAssetId,magazines[0]);
}
TEST_F(UiMagazineWheelTest, NormalReleaseAndSecondPressInOneFrameUpgradeSameMagazine) {
    key(SDL_SCANCODE_R,true,10);key(SDL_SCANCODE_R,false,10.1);
    key(SDL_SCANCODE_R,true,10.3);key(SDL_SCANCODE_R,false,10.31);A::frame(*app);
    const auto &action=A::session(*app).raidActionState().active();
    ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).plan.kind,RaidReloadKind::Emergency);
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).magazineAssetId,magazines[0]);
}
TEST_F(UiMagazineWheelTest, DelayedQueuedSecondPressUsesOriginalSdlIntervalAcrossFrames) {
    const double pressed=static_cast<double>(SDL_GetTicksNS())/1e9;
    key(SDL_SCANCODE_R,true,pressed);A::frame(*app,1.0F/60);
    SDL_Delay(20);
    const double released=static_cast<double>(SDL_GetTicksNS())/1e9;
    key(SDL_SCANCODE_R,false,released);A::frame(*app,1.0F/60);
    ASSERT_TRUE(A::session(*app).playerActionState().active());
    SDL_Delay(300);A::frame(*app,1.0F/60);
    // The queue is processed late, while its original second edge is inside
    // the physical 250ms window. Simulation has not removed the old mag.
    key(SDL_SCANCODE_R,true,released+.20);A::frame(*app,1.0F/60);
    const auto &action=A::session(*app).playerActionState().active();
    ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
    const auto &reload=std::get<ReloadRaidAction>(*action);
    EXPECT_EQ(reload.plan.kind,RaidReloadKind::Emergency);
    EXPECT_EQ(reload.magazineAssetId,magazines[0]);
}
TEST_F(UiMagazineWheelTest, OrdinaryCrossFrameDoubleTapWorksAtFifteenThirtyAndSixtyHz) {
    int sample=0;
    for(const float dt:{1.0F/15,1.0F/30,1.0F/60}) {
        if(sample) {
            ASSERT_TRUE(A::returnToBase(*app));
            ASSERT_TRUE(A::flow(*app).deploy());
        }
        A::cancel(*app);
        const double t=10+10*sample++;
        key(SDL_SCANCODE_R,false,t-1);A::frame(*app);
        key(SDL_SCANCODE_R,true,t);A::frame(*app,dt);
        key(SDL_SCANCODE_R,false,t+.05);A::frame(*app,dt);
        A::frame(*app,dt);
        key(SDL_SCANCODE_R,true,t+.25);A::frame(*app,dt);
        const auto &action=A::session(*app).playerActionState().active();
        ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
        EXPECT_EQ(std::get<ReloadRaidAction>(*action).plan.kind,RaidReloadKind::Emergency);
        key(SDL_SCANCODE_R,false,t+.26);A::frame(*app);
    }
}
TEST_F(UiMagazineWheelTest, BaseShortRUsesTheSameStagedActionAndStableSelectedMagazine) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));
    ASSERT_TRUE(A::session(*app).playerGameplayContext().available);
    key(SDL_SCANCODE_R,true,10);A::frame(*app,1.0F/60);
    EXPECT_FALSE(A::session(*app).playerActionState().active());
    key(SDL_SCANCODE_R,false,10.1);A::frame(*app,1.0F/60);
    const auto &action=A::session(*app).playerActionState().active();
    ASSERT_TRUE(action);ASSERT_TRUE(std::holds_alternative<ReloadRaidAction>(*action));
    EXPECT_EQ(std::get<ReloadRaidAction>(*action).magazineAssetId,magazines[0]);
}
TEST_F(UiMagazineWheelTest, PhysicalSpaceAndMouseHeldAcrossInventoryCloseNeedARealRelease) {
    const auto fire=[&](bool mouse,bool down) {
        if(!mouse){key(SDL_SCANCODE_SPACE,down);return;}
        SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.x=640;e.button.y=360;
        ASSERT_TRUE(SDL_PushEvent(&e));
    };
    const auto tab=[&] {
        key(SDL_SCANCODE_TAB,true);key(SDL_SCANCODE_TAB,false);A::frame(*app,1.0F/60);
    };
    for(const bool mouse:{false,true}) {
        SCOPED_TRACE(mouse?"mouse":"space");
        profile().assets.findMutable(weapon)->chamberedRound=MagazineRoundRecord{alpha_content::ammunition,std::nullopt};
        ++profile().revision;
        tab();ASSERT_TRUE(A::inventoryOpen(*app));
        fire(mouse,true);A::frame(*app,1.0F/60);
        EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
        tab();ASSERT_FALSE(A::inventoryOpen(*app));
        for(int frame=0;frame<3;++frame) {
            A::frame(*app,.1F);EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
        }
        fire(mouse,false);A::frame(*app,.1F);
        fire(mouse,true);A::frame(*app,.2F);
        EXPECT_TRUE(A::session(*app).world().shotFiredLastUpdate());
        fire(mouse,false);A::frame(*app,.2F);
    }
}
TEST_F(UiMagazineWheelTest, BaseBInCoreKeepsTheOriginalConstructionRoute) {
    ASSERT_TRUE(A::returnToBase(*app));A::frame(*app);
    ASSERT_TRUE(A::session(*app).playerGameplayContext().coreBuildAvailable);
    const auto mode=A::session(*app).weaponFireMode(weapon);
    key(SDL_SCANCODE_B,true,10);A::frame(*app);
    EXPECT_EQ(A::session(*app).weaponFireMode(weapon),mode);
}
TEST_F(UiMagazineWheelTest, FailedInventoryPreviewCanCloseToTheExistingProtectedRetryPage) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));A::openInventory(*app);
    const auto fail=std::make_shared<std::atomic<bool>>(true);const auto folder=directory.path();
    InventoryCheckpointTestAccess::store(A::session(*app),[fail,folder](const auto &p,auto version,auto *metrics) {
        if(fail->load())return SaveWriteResult{false,"injected UI save failure"};
        return SaveRepository(folder).save(p,version,metrics);
    });
    const auto id=magazines.back();const auto original=profile().assets.find(id)->location;
    const auto destination=findFirstProfileFit(profile(),publishedContentRegistry(),ProfileContainerId::stash(),
        publishedContentRegistry().item(alpha_content::magazine),ItemOrientation::Degrees0);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(A::move(*app,InventoryMoveCommand{id,0,{ProfileContainerId::stash(),*destination},ItemOrientation::Degrees0}));
    A::closeInventory(*app);ASSERT_TRUE(A::barrier(*app));
    ASSERT_TRUE(waitApp(*app,[&]{return A::session(*app).baseDailyCheckpointStatus().failed;}));
    A::frame(*app);
    EXPECT_FALSE(A::barrier(*app));EXPECT_TRUE(A::inventoryOpen(*app));
    EXPECT_FALSE(A::session(*app).inventoryPersistencePending());
    EXPECT_EQ(profile().assets.find(id)->location,original);
    key(SDL_SCANCODE_TAB,true);key(SDL_SCANCODE_TAB,false);A::frame(*app);
    EXPECT_FALSE(A::inventoryOpen(*app));
    EXPECT_TRUE(A::session(*app).baseDailyCheckpointStatus().failed);
    fail->store(false);ASSERT_TRUE(A::retry(*app));EXPECT_TRUE(A::busy(*app));
    ASSERT_TRUE(waitApp(*app,[&]{return !A::session(*app).profileUiCommitPending() && !A::busy(*app);}));
    EXPECT_FALSE(A::session(*app).baseDailySaveBlocked());
    EXPECT_EQ(A::message(*app),"PROFILE SAVE CONFIRMED");
    EXPECT_EQ(profile().assets.find(id)->location,original);

}
TEST_F(UiMagazineWheelTest, ConfirmedDefenseAbandonClosesPauseOnlyAfterTheTerminalWrite) {
    ASSERT_TRUE(A::flow(*app).returnToMainMenu());
    ASSERT_TRUE(A::flow(*app).startNewGame("ui-abandon-pending",false));
    auto &session=A::session(*app);
    ASSERT_TRUE(session.triggerDeveloperBaseSiegeWarning());
    ASSERT_TRUE(session.startBaseRealtimeDefense(A::flow(*app).baseWorld()));
    ASSERT_TRUE(session.checkpointWorldClock());
    const auto release=std::make_shared<std::atomic<bool>>(false);
    const auto entered=std::make_shared<std::atomic<bool>>(false);
    struct Release {std::shared_ptr<std::atomic<bool>> flag;~Release(){flag->store(true);}} unblock{release};
    const auto folder=directory.path();
    InventoryCheckpointTestAccess::defenseStore(session,[release,entered,folder](const auto &p,auto version,auto *metrics) {
        entered->store(true);while(!release->load())SDL_Delay(1);
        return SaveRepository(folder).save(p,version,metrics);
    });
    A::pause(*app);A::command(*app,PauseMenuCommand::AbandonBaseDefense);ASSERT_TRUE(A::armed(*app));
    A::command(*app,PauseMenuCommand::AbandonBaseDefense);
    ASSERT_TRUE(waitApp(*app,[&]{return entered->load();}));
    EXPECT_TRUE(A::paused(*app));EXPECT_TRUE(A::barrier(*app));EXPECT_TRUE(session.baseDefenseActive());
    release->store(true);
    ASSERT_TRUE(waitApp(*app,[&]{return !session.baseDefenseActive() && !A::barrier(*app);}));
    EXPECT_FALSE(A::paused(*app));EXPECT_FALSE(A::armed(*app));
    const auto saved=SaveRepository(folder).load(publishedContentRegistry());
    ASSERT_TRUE(saved.profile)<<saved.message;EXPECT_FALSE(saved.profile->activeBaseDefense);
}
TEST_F(UiMagazineWheelTest, NativeSupplyPurchaseOnlyAcknowledgesAfterDurabilityAndFailureRetryRetainsIntent) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));
    auto &session=A::session(*app);
    const auto original=profileStateFingerprint(profile());const auto credits=profile().currency;
    const auto fail=std::make_shared<std::atomic<bool>>(true);
    const auto calls=std::make_shared<std::atomic<unsigned>>(0);
    const auto folder=directory.path();
    InventoryCheckpointTestAccess::store(session,[fail,calls,folder](const auto &p,auto version,auto *metrics) {
        ++*calls;
        if(fail->load())return SaveWriteResult{false,"injected purchase save failure"};
        return SaveRepository(folder).save(p,version,metrics);
    });
    ASSERT_TRUE(A::buyAmmunition(*app));
    EXPECT_EQ(profileStateFingerprint(profile()),original);
    EXPECT_EQ(A::message(*app),"SAVING CHANGES");
    ASSERT_TRUE(waitApp(*app,[&]{return session.baseDailySaveBlocked();}));A::frame(*app);
    EXPECT_FALSE(A::busy(*app));EXPECT_EQ(profileStateFingerprint(profile()),original);
    EXPECT_EQ(A::message(*app),"injected purchase save failure");
    const auto failedCalls=calls->load();fail->store(false);
    ASSERT_TRUE(A::retry(*app));EXPECT_TRUE(A::busy(*app));
    ASSERT_TRUE(waitApp(*app,[&]{return !session.profileUiCommitPending() && !A::busy(*app);}));
    EXPECT_EQ(calls->load(),failedCalls+1);
    EXPECT_LT(profile().currency,credits);
    EXPECT_EQ(A::message(*app),"PURCHASED | CURRENCY "+std::to_string(profile().currency));
    const auto saved=SaveRepository(folder).load(publishedContentRegistry());ASSERT_TRUE(saved.profile)<<saved.message;
    EXPECT_EQ(profileStateFingerprint(*saved.profile),profileStateFingerprint(profile()));
}
TEST_F(UiMagazineWheelTest, PauseReturnToMenuWaitsAcrossFramesAndNavigatesWithTheDurableTicket) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));
    const auto release=std::make_shared<std::atomic<bool>>(false);
    const auto entered=std::make_shared<std::atomic<bool>>(false);
    struct Release {std::shared_ptr<std::atomic<bool>> flag;~Release(){flag->store(true);}} unblock{release};
    const auto folder=directory.path();
    InventoryCheckpointTestAccess::store(A::session(*app),[release,entered,folder](const auto &p,auto version,auto *metrics) {
        entered->store(true);while(!release->load())SDL_Delay(1);
        return SaveRepository(folder).save(p,version,metrics);
    });
    A::pause(*app);A::command(*app,PauseMenuCommand::MainMenu);
    EXPECT_TRUE(A::busy(*app));EXPECT_EQ(A::flow(*app).state(),GameFlowState::Base);
    ASSERT_TRUE(waitApp(*app,[&]{return entered->load();}));
    EXPECT_EQ(A::flow(*app).state(),GameFlowState::Base);EXPECT_TRUE(A::paused(*app));
    attach({1280,720});A::base(*app);EXPECT_TRUE(SDL_RenderPresent(renderer));
    release->store(true);
    ASSERT_TRUE(waitApp(*app,[&]{return A::flow(*app).state()==GameFlowState::MainMenu;}));
    EXPECT_FALSE(A::paused(*app));EXPECT_FALSE(A::busy(*app));
    EXPECT_EQ(A::message(*app),"RETURNED TO MAIN MENU");
}
TEST_F(UiMagazineWheelTest, UnregisteredAutomaticDeathSaveFailureShowsProtectionAndNativeMouseRetrySettlesOnce) {
    auto &session=A::session(*app);
    ASSERT_TRUE(A::captureWanted(*app));
    const auto fail=std::make_shared<std::atomic<bool>>(true);
    const auto calls=std::make_shared<std::atomic<unsigned>>(0);
    const auto folder=directory.path();
    std::string failureMessage="injected automatic settlement failure\n";
    for(int line=0;line<40;++line)failureMessage+="Complete protected settlement detail "+std::to_string(line)+".\n";
    failureMessage+="FINAL SETTLEMENT DETAIL";
    InventoryCheckpointTestAccess::store(session,[fail,calls,folder,failureMessage](const auto &p,auto version,auto *metrics) {
        ++*calls;
        if(fail->load())return SaveWriteResult{false,failureMessage};
        return SaveRepository(folder).save(p,version,metrics);
    });
    ASSERT_TRUE(session.world().damagePlayer(100000));
    A::frame(*app,.01F);
    ASSERT_TRUE(waitApp(*app,[&]{return session.state()==GameSessionState::SettlementBlocked;}));
    A::frame(*app);
    EXPECT_EQ(A::continuationCount(*app),0U);
    EXPECT_TRUE(A::saveProtection(*app));EXPECT_FALSE(A::busy(*app));
    EXPECT_FALSE(A::captureWanted(*app));
    EXPECT_EQ(A::message(*app),failureMessage);
    EXPECT_TRUE(profile().pendingRaid);EXPECT_FALSE(profile().lastRaidResult);
    attach({1280,720});A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);A::fullFrame(*app);
    EXPECT_EQ(A::saveScroll(*app),0);
    SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=-1000;
    wheel.wheel.mouse_x=500;wheel.wheel.mouse_y=280;ASSERT_TRUE(SDL_PushEvent(&wheel));
    A::frame(*app);A::fullFrame(*app);
    EXPECT_GT(A::saveScroll(*app),30);EXPECT_LT(A::saveScroll(*app),2000);
    if(const auto output=SDL_getenv("RAIDLINE_PLAYER_SYSTEMS_QA_DIR")) {
        std::filesystem::create_directories(output);
        const auto file=(std::filesystem::path{output}/"raid-save-protection-1280-zh-CN.png").u8string();
        ASSERT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(file.c_str())))<<SDL_GetError();
    }
    const auto failedCalls=calls->load();fail->store(false);
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.button=SDL_BUTTON_LEFT;
    event.button.down=true;event.button.x=1119;event.button.y=213;ASSERT_TRUE(SDL_PushEvent(&event));
    event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.down=false;ASSERT_TRUE(SDL_PushEvent(&event));
    A::frame(*app);
    ASSERT_TRUE(waitApp(*app,[&]{return A::flow(*app).state()==GameFlowState::RaidResult;}));
    EXPECT_FALSE(A::saveProtection(*app));EXPECT_EQ(calls->load(),failedCalls+1);
    ASSERT_TRUE(profile().lastRaidResult);EXPECT_EQ(profile().lastRaidResult->outcome,RaidResultOutcome::PlayerDead);
    EXPECT_FALSE(profile().pendingRaid);
    const auto saved=SaveRepository(folder).load(publishedContentRegistry());ASSERT_TRUE(saved.profile)<<saved.message;
    ASSERT_TRUE(saved.profile->lastRaidResult);EXPECT_EQ(saved.profile->lastRaidResult->outcome,RaidResultOutcome::PlayerDead);
}
TEST_F(UiMagazineWheelTest, UnregisteredAutomaticSuccessfulSettlementDoesNotAddFeedbackSpam) {
    EXPECT_TRUE(A::message(*app).empty());
    ASSERT_TRUE(A::session(*app).world().damagePlayer(100000));
    A::frame(*app,.01F);
    ASSERT_TRUE(waitApp(*app,[&]{return A::flow(*app).state()==GameFlowState::RaidResult;}));
    EXPECT_EQ(A::continuationCount(*app),0U);EXPECT_FALSE(A::saveProtection(*app));
    EXPECT_TRUE(A::message(*app).empty());
}
TEST_F(UiMagazineWheelTest, HDoesNotRetryAnUnrelatedFailedPurchase) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));
    profile().homeFounding.hintsDismissed=false;++profile().revision;
    const auto calls=std::make_shared<std::atomic<unsigned>>(0);
    InventoryCheckpointTestAccess::store(A::session(*app),[calls](const auto &,auto,auto *) {
        ++*calls;return SaveWriteResult{false,"injected unrelated purchase failure"};
    });
    ASSERT_TRUE(A::buyAmmunition(*app));
    ASSERT_TRUE(waitApp(*app,[&]{return A::session(*app).baseDailySaveBlocked();}));A::frame(*app);
    const auto failedCalls=calls->load();
    key(SDL_SCANCODE_H,true);key(SDL_SCANCODE_H,false);A::frame(*app);
    key(SDL_SCANCODE_H,true);key(SDL_SCANCODE_H,false);A::frame(*app);
    EXPECT_EQ(calls->load(),failedCalls);EXPECT_FALSE(profile().homeFounding.hintsDismissed);
    EXPECT_TRUE(A::session(*app).baseDailySaveBlocked());
}
TEST_F(UiMagazineWheelTest, DurableSavedCompletionAndConcurrentBaseHudShareOneSafeLayout) {
    ASSERT_TRUE(A::returnToBase(*app));ASSERT_TRUE(waitBaseInitialization(*app));
    const auto &content=publishedContentRegistry();
    const auto add=[&](ItemDefinitionId definition) {
        const auto &item=content.item(definition);
        const auto fit=findFirstProfileFit(profile(),content,ProfileContainerId::stash(),item,ItemOrientation::Degrees0);
        EXPECT_TRUE(fit);return fit?profile().assets.create(item,StoredAssetLocation{ProfileContainerId::stash(),*fit}):0;
    };
    const auto knife=add(alpha_content::utilityKnife);ASSERT_NE(knife,0U);
    ASSERT_TRUE(executeInventory(profile(),content,InventoryEquipCommand{knife,EquipmentSlotKind::QuickMelee},
        {profile().revision,"hud-equipped-knife"}).succeeded);
    // The published flashlight platform is the service 5.45 rifle. The basic
    // 9mm burst rifle used by other tests has no compatible component slots.
    weapon=add(ItemDefinitionId{"item.weapon.rifle_5_45_service"});ASSERT_NE(weapon,0U);
    ASSERT_TRUE(executeInventory(profile(),content,InventoryEquipCommand{weapon,EquipmentSlotKind::PrimaryWeapon},
        {profile().revision,"hud-equipped-light-platform"}).succeeded);
    for(const auto *name:{"item.component.rail_handguard","item.component.weapon_flashlight_basic"}) {
        AssetInstanceId id{};for(const auto &[candidate,asset]:profile().assets.records())
            if(asset.definitionId==ItemDefinitionId{name}){id=candidate;break;}
        if(!id)id=add(ItemDefinitionId{name});ASSERT_NE(id,0U);
        const auto installed=executeWeaponComponentChange(profile(),content,
            {weapon,content.item(profile().assets.find(id)->definitionId).weaponComponent->slot,id},
            {profile().revision,std::string{"hud-install-"}+name});
        ASSERT_TRUE(installed.succeeded)<<name<<": "<<installed.message;
    }
    const auto magazine=add(ItemDefinitionId{"item.magazine.5_45x39_30"});ASSERT_NE(magazine,0U);
    const auto ammunition=add(ItemDefinitionId{"item.ammunition.5_45x39_standard"});ASSERT_NE(ammunition,0U);
    ASSERT_TRUE(executeWeaponAmmo(profile(),content,LoadMagazineCommand{magazine,ammunition,1},
        {profile().revision,"hud-load-compatible-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(profile(),content,InstallMagazineCommand{weapon,magazine},
        {profile().revision,"hud-install-compatible-magazine"}).succeeded);
    ASSERT_TRUE(executeWeaponAmmo(profile(),content,SetWeaponFireModeCommand{weapon,WeaponFireMode::SemiAutomatic},
        {profile().revision,"hud-legal-mode"}).succeeded);
    auto &base=A::flow(*app).baseWorld();const auto parcel=base.baseParcel();
    bool outside{};
    for(const Vec2 position:{Vec2{parcel.position.x-96,parcel.position.y+parcel.size.y/2},
                            Vec2{parcel.position.x+parcel.size.x+96,parcel.position.y+parcel.size.y/2}})
        if(base.restorePlayerBody(position,{1,0},0,0) && base.playerSafetyZone()!=HomeRegionSafetyZone::SafeCore){outside=true;break;}
    ASSERT_TRUE(outside); // The actual Base weapon/mode card is visible here.
    const auto fail=std::make_shared<std::atomic<bool>>(true);
    const auto release=std::make_shared<std::atomic<bool>>(false);
    const auto entered=std::make_shared<std::atomic<bool>>(false);
    struct Release {std::shared_ptr<std::atomic<bool>> flag;~Release(){flag->store(true);}} unblock{release};
    const auto folder=directory.path();
    InventoryCheckpointTestAccess::store(A::session(*app),[fail,release,entered,folder](const auto &p,auto version,auto *metrics) {
        if(fail->load())return SaveWriteResult{false,"injected HUD save failure"};
        entered->store(true);while(!release->load())SDL_Delay(1);
        return SaveRepository(folder).save(p,version,metrics);
    });
    ASSERT_TRUE(A::session(*app).requestWorldDurabilityBarrier().accepted);
    ASSERT_TRUE(waitApp(*app,[&]{return A::session(*app).baseDailySaveBlocked();}));
    fail->store(false);ASSERT_TRUE(A::retry(*app));
    ASSERT_TRUE(waitApp(*app,[&]{return entered->load();}));
    EXPECT_EQ(A::message(*app),"SAVING CHANGES");
    EXPECT_NE(A::visibleMessage(*app),"PROFILE SAVE CONFIRMED");
    release->store(true);
    ASSERT_TRUE(waitApp(*app,[&]{return !A::session(*app).profileUiCommitPending() && !A::busy(*app);}));
    ASSERT_EQ(A::message(*app),"PROFILE SAVE CONFIRMED");
    ASSERT_TRUE(A::session(*app).activeWeaponLight().binding.deviceAssetId);
    const auto saved=SaveRepository(folder).load(content);ASSERT_TRUE(saved.profile)<<saved.message;
    ASSERT_EQ(equippedAsset(*saved.profile,EquipmentSlotKind::QuickMelee),knife);
    const auto separated=[](SDL_FRect a,SDL_FRect b) {
        return a.x+a.w+8<=b.x || b.x+b.w+8<=a.x || a.y+a.h+8<=b.y || b.y+b.h+8<=a.y;
    };
    for(const auto size:{UiWindowSize{1280,720},UiWindowSize{1920,1080}})
        for(const float dpi:{1.0F,1.25F,1.5F,2.0F})
            for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
                detach();attach(size);A::text(*app).setLanguage(language);
                ASSERT_TRUE(SDL_SetFloatProperty(SDL_GetRendererProperties(renderer),kUiDisplayScaleProperty,dpi));
                const auto layout=A::bottom(*app);
                EXPECT_TRUE(separated(layout.light,layout.melee));
                for(const auto occupied:{layout.light,layout.melee,layout.vitals,layout.action,layout.interaction,layout.condition})
                    EXPECT_TRUE(separated(layout.feedback.panel,occupied));
                A::fullFrame(*app); // Includes the actual outer App render and HUD order.
                if(const auto output=SDL_getenv("RAIDLINE_BASE_BURST_HUD_QA_DIR")) {
                    std::filesystem::create_directories(output);
                    const auto name="base-saved-concurrent-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+
                        "-dpi"+std::to_string(static_cast<int>(dpi*100))+".png";
                    ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/name).string().c_str()));
                }
            }
    A::presentation(*app,2.7F);EXPECT_FALSE(A::visibleMessage(*app).empty());
    A::presentation(*app,.4F);EXPECT_TRUE(A::visibleMessage(*app).empty());
    A::message(*app,"CHANGES SAVED | REOPEN THE PAGE");A::presentation(*app,0);A::presentation(*app,3.1F);
    EXPECT_EQ(A::visibleMessage(*app),"CHANGES SAVED | REOPEN THE PAGE");
    A::message(*app,"TRANSACTION IDS EXHAUSTED");A::presentation(*app,0);A::presentation(*app,3.1F);
    EXPECT_EQ(A::visibleMessage(*app),"TRANSACTION IDS EXHAUSTED");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"TRANSACTION IDS EXHAUSTED"),"操作编号已耗尽，操作已停止");
    std::string error;for(int line=0;line<40;++line)error+="ERROR DETAIL "+std::to_string(line)+"\n";
    error+="FINAL ERROR DETAIL";A::message(*app,error);A::fullFrame(*app);
    const auto errorLayout=A::bottom(*app);
    SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=-200;
    wheel.wheel.mouse_x=errorLayout.feedback.panel.x+errorLayout.feedback.panel.w/2;
    wheel.wheel.mouse_y=errorLayout.feedback.panel.y+errorLayout.feedback.panel.h/2;
    ASSERT_TRUE(A::feedbackWheel(*app,wheel));A::fullFrame(*app);
    EXPECT_GT(A::feedbackScroll(*app),30);
    wheel.wheel.mouse_x=errorLayout.feedback.panel.x+errorLayout.feedback.panel.w+20;
    EXPECT_FALSE(A::feedbackWheel(*app,wheel));
    if(const auto output=SDL_getenv("RAIDLINE_BASE_BURST_HUD_QA_DIR"))
        ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/"base-long-error-concurrent-1920-en-dpi200.png").string().c_str()));
    const double reloadTime=SDL_GetTicksNS()/1e9+.001;
    key(SDL_SCANCODE_R,true,reloadTime);A::frame(*app);
    key(SDL_SCANCODE_R,false,reloadTime+.1);A::frame(*app);
    ASSERT_TRUE(A::session(*app).playerActionState().active());
    EXPECT_TRUE(std::holds_alternative<ChamberRaidAction>(*A::session(*app).playerActionState().active()));
    EXPECT_EQ(A::message(*app),error);EXPECT_NE(A::visibleMessage(*app),"PROFILE SAVE CONFIRMED");
    const auto actionLayout=A::bottom(*app);
    EXPECT_TRUE(separated(actionLayout.action,actionLayout.feedback.panel));
    A::fullFrame(*app);
    if(const auto output=SDL_getenv("RAIDLINE_BASE_BURST_HUD_QA_DIR"))
        ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/"base-r-action-concurrent-1920-en-dpi200.png").string().c_str()));
    const auto wound=applyWoundRoll(profile().medicalStatus,{WoundSource::Scratch,0,17000});
    ASSERT_TRUE(wound.applied);++profile().revision;
    ASSERT_TRUE(validateProfileState(profile(),content).valid);
    key(SDL_SCANCODE_F9,true);key(SDL_SCANCODE_F9,false);A::frame(*app);
    ASSERT_TRUE(A::diagnostics(*app));
    ASSERT_TRUE(A::session(*app).playerActionState().active());
    EXPECT_TRUE(std::holds_alternative<ChamberRaidAction>(*A::session(*app).playerActionState().active()));
    const auto diagnosticsLayout=A::bottom(*app);
    const SDL_FRect diagnosticBlock{16,562,1248,35};
    for(const auto occupied:{diagnosticsLayout.action,diagnosticsLayout.condition,diagnosticsLayout.feedback.panel,
                            diagnosticsLayout.light,diagnosticsLayout.melee})
        EXPECT_TRUE(separated(occupied,diagnosticBlock));
    A::fullFrame(*app);
    unsigned conditionGlyphs{};
    const auto condition=diagnosticsLayout.condition;
    for(int y=static_cast<int>(condition.y*1.5F);y<static_cast<int>((condition.y+condition.h)*1.5F);++y)
        for(int x=static_cast<int>(condition.x*1.5F);x<static_cast<int>((condition.x+condition.w)*1.5F);++x) {
            Uint8 r{},g{},b{},a{};ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
            conditionGlyphs+=r>110 && g>75 && b>40 && r>g+20 && g>b+20;
        }
    EXPECT_GT(conditionGlyphs,30U);
    if(const auto output=SDL_getenv("RAIDLINE_BASE_BURST_HUD_QA_DIR"))
        ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/"base-r-diagnostics-concurrent-1920-en-dpi200.png").string().c_str()));
}

TEST_F(UiMagazineWheelTest, RealReturnGuidanceLeavesSaveProtectionAndItsLongErrorReadable) {
    profile().homeFounding.hintsDismissed=false;++profile().revision;
    ASSERT_TRUE(A::session(*app).world().damagePlayer(100000));
    ASSERT_TRUE(waitApp(*app,[&]{return A::flow(*app).state()==GameFlowState::RaidResult;}));
    A::frame(*app);
    key(SDL_SCANCODE_RETURN,true);key(SDL_SCANCODE_RETURN,false);A::frame(*app);
    ASSERT_TRUE(waitApp(*app,[&]{return A::flow(*app).state()==GameFlowState::Base && !A::busy(*app);}));
    ASSERT_TRUE(waitBaseInitialization(*app));
    // Retain an unhidden legacy guidance preference on a real completed
    // settlement; the normal Return command itself dismisses that preference.
    profile().homeFounding.hintsDismissed=false;++profile().revision;
    ASSERT_EQ(firstRaidGuidanceStage(profile()),FirstRaidGuidanceStage::Return);
    std::string error;for(int line=0;line<40;++line)error+="RETURN SAVE ERROR "+std::to_string(line)+"\n";
    error+="FINAL RETURN ERROR DETAIL";
    InventoryCheckpointTestAccess::store(A::session(*app),[error](const auto &,auto,auto *) {
        return SaveWriteResult{false,error};
    });
    ASSERT_TRUE(A::session(*app).requestWorldDurabilityBarrier().accepted);
    ASSERT_TRUE(waitApp(*app,[&]{return A::session(*app).baseDailySaveBlocked();}));
    attach({1280,720});A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);A::fullFrame(*app);
    const auto mask=[&] {
        std::vector<Uint32> result;
        for(int y=210;y<254;++y)for(int x=640;x<950;++x) {
            Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
            result.push_back((Uint32{r}<<24)|(Uint32{g}<<16)|(Uint32{b}<<8)|a);
        }return result;
    };
    const auto withGuidance=mask();profile().homeFounding.hintsDismissed=true;A::fullFrame(*app);
    EXPECT_EQ(withGuidance,mask());profile().homeFounding.hintsDismissed=false;
    SDL_Event wheel{};wheel.type=SDL_EVENT_MOUSE_WHEEL;wheel.wheel.y=-200;wheel.wheel.mouse_x=640;wheel.wheel.mouse_y=280;
    ASSERT_TRUE(SDL_PushEvent(&wheel));A::frame(*app);A::fullFrame(*app);
    EXPECT_GT(A::saveScroll(*app),30);EXPECT_EQ(A::message(*app),error);
    if(const auto output=SDL_getenv("RAIDLINE_BASE_BURST_HUD_QA_DIR")) {
        std::filesystem::create_directories(output);
        ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/"base-return-save-protection-1280-zh.png").string().c_str()));
    }
}

TEST_F(UiMagazineWheelTest, ActualBaseFeedbackAndInventoryRenderBothLanguagesAtEverySize) {
    ASSERT_TRUE(A::returnToBase(*app));A::frame(*app);
    const auto before=profileStateFingerprint(profile());
    struct DisplayCase {UiWindowSize size;float dpi;};
    const DisplayCase displays[]={{{1280,720},1},{{1280,720},1.25F},{{1600,900},1.5F},
        {{1920,1080},1},{{1920,1080},2},{{3840,2160},1},{{3840,2160},2},{{2560,1080},1.5F}};
    for(const auto display:displays)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        const auto size=display.size;
        detach();attach(size);A::text(*app).setLanguage(language);
        ASSERT_TRUE(SDL_SetFloatProperty(SDL_GetRendererProperties(renderer),kUiDisplayScaleProperty,display.dpi));
        for(const bool inventory:{false,true}) {
            A::message(*app,language==UiLanguage::SimplifiedChinese
                ? "保存失败：未确认的整理已撤回，之前确认的物品保持原位。详细原因：写入被中断，请确认存储位置可用后重试；已有存档仍受保护。滚轮查看完整错误原因。"
                : "Save failed: pending moves were reverted; confirmed items are preserved. The disk write was interrupted. Retry when storage is available; the saved profile remains protected. Scroll for complete details.");
            SDL_SetRenderDrawColor(renderer,15,23,26,255);SDL_RenderClear(renderer);
            if(inventory)A::inventory(*app);else A::base(*app);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            if(const auto output=SDL_getenv("RAIDLINE_PLAYER_SYSTEMS_QA_DIR")) {
                std::filesystem::create_directories(output);
                const auto name=std::string{inventory?"actual-inventory-":"actual-base-feedback-"}+
                    std::to_string(size.width)+"-"+uiLanguageCode(language)+"-dpi"+
                    std::to_string(static_cast<int>(display.dpi*100))+".png";
                ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(output)/name).string().c_str()));
            }
        }
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, ThreeSizesBothLanguagesKeepAcceptedRingAndShortModeText) {
    A::open(*app);const auto before=profileStateFingerprint(profile());
    for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        detach();attach(size);A::text(*app).setLanguage(language);A::draw(*app);
        const auto &projection=A::projection(*app);
        ASSERT_EQ(projection.entries.size(),4U);
        EXPECT_EQ(A::layout(*app).markers.size(),8U);EXPECT_TRUE(A::layout(*app).rows.empty());
        EXPECT_TRUE(A::layout(*app).count.text.empty());
        for(const auto mode:{WeaponFireMode::SemiAutomatic,WeaponFireMode::ThreeRoundBurst,WeaponFireMode::Automatic})
            EXPECT_LT(A::text(*app).measure(uiWeaponFireModeName(mode,language),15),110);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        if(const auto dir=std::getenv("RAIDLINE_MAGAZINE_WHEEL_QA_DIR")) {
            std::filesystem::create_directories(dir);
            const auto name=std::string{"magazine-"}+std::to_string(size.width)+(language==UiLanguage::English?"-en.png":"-zh.png");
            const auto path=(std::filesystem::path{dir}/name).u8string();
            ASSERT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(path.c_str())))<<SDL_GetError();
        }
        A::cancel(*app);SDL_SetRenderDrawColor(renderer,45,61,55,255);SDL_RenderClear(renderer);
        A::hud(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
        if(const auto dir=std::getenv("RAIDLINE_MAGAZINE_WHEEL_QA_DIR")) {
            const auto name=std::string{"mode-hud-"}+std::to_string(size.width)+(language==UiLanguage::English?"-en.png":"-zh.png");
            const auto path=(std::filesystem::path{dir}/name).u8string();
            ASSERT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(path.c_str())))<<SDL_GetError();
        }
        A::open(*app);
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiMagazineWheelTest, Mixed545SelectedSectorDistinguishesTopFromChamberAtThreeSizes) {
    const auto &content = publishedContentRegistry();
    auto seed = makeNewPublishedProfile("mixed-5-45-wheel", content);
    seed.tutorial = TutorialProgress::Complete;
    seed.homeFounding.hintsDismissed = true;
    const ItemDefinitionId enhanced{"item.ammunition.5_45x39_enhanced"};
    const auto rifle = seed.assets.create(content.item(ItemDefinitionId{"item.weapon.rifle_5_45_service"}),
        EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
    seed.assets.findMutable(rifle)->chamberedRound = MagazineRoundRecord{enhanced, std::nullopt};
    equipDecoyTestRig(seed, content);
    const auto rig = equippedAsset(seed, EquipmentSlotKind::ChestRig);
    ASSERT_TRUE(rig);
    const auto pocket = ProfileContainerId::compartment(*rig, 0);
    const auto &magazineDefinition = content.item(ItemDefinitionId{"item.magazine.5_45x39_30"});
    const auto fit = findFirstProfileFit(seed, content, pocket, magazineDefinition, ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    const auto magazine = seed.assets.create(magazineDefinition, StoredAssetLocation{pocket, *fit});
    seed.assets.findMutable(magazine)->magazineRounds = {
        {kExpanding545AmmunitionId, std::nullopt}, {enhanced, std::nullopt},
        {kExpanding545AmmunitionId, std::nullopt}, {kExpanding545AmmunitionId, std::nullopt}};
    ASSERT_TRUE(validateProfileState(seed, content).valid);

    const auto path = directory.path() / "mixed-5-45";
    std::filesystem::create_directories(path);
    ASSERT_TRUE(SaveRepository(path).save(seed, content.contentVersion()).succeeded);
    app = std::make_unique<App>();
    A::flow(*app).configurePersistence(path);
    ASSERT_TRUE(A::flow(*app).continueGame()); ASSERT_TRUE(A::flow(*app).deploy());
    A::frame(*app);
    ASSERT_EQ(A::session(*app).activeAlphaWeapon(), rifle);
    A::open(*app);
    ASSERT_TRUE(A::open(std::as_const(*app)));
    ASSERT_EQ(A::ids(*app), std::vector<AssetInstanceId>{magazine});
    const auto before = profileStateFingerprint(profile());

    for (const auto size : {UiWindowSize{1024, 576}, UiWindowSize{1280, 720}, UiWindowSize{1920, 1080}})
        for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
            detach(); attach(size); A::text(*app).setLanguage(language); A::draw(*app);
            const auto &entries = A::projection(*app).entries;
            ASSERT_EQ(entries.size(), 1U);
            EXPECT_TRUE(entries.front().mixedAmmunition);
            EXPECT_EQ(entries.front().magazineTopRound, "HP");
            EXPECT_EQ(entries.front().chamberRound, "E");
            const auto &layout = A::layout(*app);
            EXPECT_EQ(layout.markers.size(), 8U);
            EXPECT_TRUE(layout.rows.empty());
            const auto &label = layout.count;
            const bool chinese = language == UiLanguage::SimplifiedChinese;
            EXPECT_TRUE(label.text.starts_with("4/30"));
            EXPECT_TRUE(chinese ? label.text.ends_with("顶扩 膛强") :
                label.text.ends_with("TOP:HP CH:E") || label.text.ends_with("T:HP C:E"));
            EXPECT_EQ(localizeUiText(language, label.text), label.text);
            EXPECT_LE(A::text(*app).measure(label.text, label.size), 110.0F);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            if (const auto output = std::getenv("RAIDLINE_MAGAZINE_WHEEL_QA_DIR")) {
                std::filesystem::create_directories(output);
                const auto name = std::string{"mixed-5-45-wheel-"} + std::to_string(size.width) + (chinese ? "-zh.png" : "-en.png");
                const auto png = (std::filesystem::path{output} / name).u8string();
                ASSERT_TRUE(IMG_SavePNG(surface, reinterpret_cast<const char *>(png.c_str()))) << SDL_GetError();
            }
        }
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST(UiWeaponModeMenuTest, BothActivitiesOfferLegalVariableModesAndFixedPistolNeverOffersCycle) {
    const auto &content=publishedContentRegistry();auto profile=makeNewPublishedProfile("mode-menu",content);
    AssetInstanceId rifle{},pistol{};
    for(const auto &[id,a]:profile.assets.records()) {
        if(a.definitionId==alpha_content::rifle)rifle=id;
        if(a.definitionId==alpha_content::pistol)pistol=id;
    }
    ASSERT_NE(rifle,0U);ASSERT_NE(pistol,0U);
    const auto contains=[](const auto &actions) {return std::any_of(actions.begin(),actions.end(),
        [](const auto &a){return a.id==UiItemAction::CycleFireMode;});};
    EXPECT_TRUE(contains(projectUiItemActions(profile,content,rifle,false)));
    EXPECT_TRUE(contains(projectUiItemActions(profile,content,rifle,true)));
    EXPECT_FALSE(contains(projectUiItemActions(profile,content,pistol,false)));
    EXPECT_FALSE(contains(projectUiItemActions(profile,content,pistol,true)));
}
TEST(UiWeaponModeMenuTest, BothActivitiesUseExplicitRAndBaseAlsoOffersTheSameContextAction) {
    WeaponSupplyReadiness weapon;weapon.definitionKnown=true;weapon.fireResult=WeaponAmmoResult::Chambered;
    weapon.suggestions.push_back({WeaponSupplySuggestionKind::ChamberWeapon});
    const auto raid=projectUiWeaponSupplyLines(weapon,{},true,weapon.slot);
    const auto base=projectUiWeaponSupplyLines(weapon,{},false,weapon.slot);
    EXPECT_EQ(raid.front(),"CHAMBER EMPTY | PRESS R TO CHAMBER");
    EXPECT_EQ(base.front(),"CHAMBER EMPTY | PRESS R TO CHAMBER");
    EXPECT_NE(std::find(raid.begin(),raid.end(),"CLOSE TAB; PRESS R TO CHAMBER"),raid.end());
    EXPECT_NE(std::find(base.begin(),base.end(),"RIGHT-CLICK THE WEAPON AND CHOOSE CHAMBER"),base.end());
}

