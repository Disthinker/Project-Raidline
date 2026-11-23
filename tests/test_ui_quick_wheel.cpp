#include "app.h"
#include "ui_app_persistence_wait.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include "noise_decoy_fixture.h"
#include "raid_camera.h"
#include "weapon_handling_fixture.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiQuickWheelTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static void settle(App &a) {
        ASSERT_TRUE(raidline::test::waitForUiAppPersistence(a.gameSession_,
            [&] { return a.profileUiBusy() || static_cast<bool>(a.pendingInventoryBarrierAction_); },
            [&] { a.processEvents(); a.update(0); a.input_.endFrame(); })) << a.gameSession_.persistenceMessage();
    }
    static void frame(App &a, float seconds=0) { a.processEvents(); a.update(seconds); a.input_.endFrame(); settle(a); }
    static void point(App &a, Vec2 p) {
        if(a.medicalWheelOpen_ || a.throwableWheelOpen_)a.wheelPointerOffset_={p.x-640,p.y-360};
        else a.pointerWorldPosition_=p;
    }
    static bool medical(const App &a) { return a.medicalWheelOpen_; }
    static bool throwable(const App &a) { return a.throwableWheelOpen_; }
    static auto aiming(const App &a) { return a.selectedThrowable_; }
    static auto medicalIds(const App &a) { return a.medicalWheelOptions_; }
    static auto throwableIds(const App &a) { return a.throwableWheelOptions_; }
    static auto medicalIndex(const App &a) { return a.medicalWheelSelectedIndex_; }
    static auto throwableIndex(const App &a) { return a.throwableWheelSelected_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static const auto &layout(App &a) { return a.quickWheelLayout_; }
    static const auto &projection(App &a) { return a.quickWheelProjection_; }
    static const auto &message(App &a) { return a.uiMessage_; }
    static bool paused(const App &a) { return a.pauseMenu_.isOpen(); }
    static bool capture(const App &a) { return a.shouldCaptureWorldPointer(); }
    static void captureNow(App &a) {a.syncRaidPointerCapture();}
    static void bindWindow(App &a,SDL_Window *w) {a.window_=w;}
    static auto offset(const App &a) {return a.wheelPointerOffset_;}
    static void clock(App &a,float dt) { a.uiClockSeconds_+=dt; }
    static void anchor(App &a) {
        a.worldAimPointerAnchor_=raidWorldToScreen(a.gameSession_.world().weaponAimWorldPosition(),a.raidWorldCameraOffset());
    }
    static void draw(App &a) { a.renderMedicalWheel(); a.renderThrowableUi(); }
    static void aimAt(App &a, Vec2 world) {
        a.pointerWorldPosition_ = a.worldAimPointerAnchor_ = raidWorldToScreen(world, a.raidWorldCameraOffset());
    }
    static void drawSmoke(App &a, Rect view) { a.renderRaidSmokeCover(view); }
    static void drawFire(App &a) { a.renderRaidFireZones(); }
    static void drawAlarms(App &a) { a.renderRaidAlarmDevices(); }
    static std::string_view raidFeedback(App &a) { return a.raidHudFeedback(); }
    static void drawRaid(App &a) { a.renderRaidScreen(); }
    static void loadRaidTextures(App &a) {
        const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        const auto texture = [&](const std::filesystem::path &path) {
            const auto utf8 = (root/path).u8string();
            return Texture{IMG_LoadTexture(a.renderer_,reinterpret_cast<const char *>(utf8.c_str()))};
        };
        a.backgroundTexture_ = texture(defaultV0MapDefinition().backgroundTexturePath);
        a.playerTexture_ = texture("characters/protagonist_left_minimal_256x320.png");
        a.playerMoveHorizontalTexture_ = texture("characters/player/default/player_default_move_horizontal_6f_1536x640.png");
        a.enemyMoveHorizontalTexture_ = texture("characters/enemy/default/enemy_default_move_horizontal_6f_1536x640.png");
        ASSERT_TRUE(a.backgroundTexture_.valid() && a.playerTexture_.valid() &&
            a.playerMoveHorizontalTexture_.valid() && a.enemyMoveHorizontalTexture_.valid());
    }
    static bool beginBackdrop(App &a) {return a.quickWheelBackdrop_.begin(a.renderer_);}
    static bool composeBackdrop(App &a) {return a.quickWheelBackdrop_.compose();}
    static void attach(App &a,SDL_Renderer *r) { a.renderer_=r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); }
    static void detach(App &a) {
        for(auto &t:a.inventoryItemTextures_)t.reset();
        for(auto &t:a.worldItemTextures_)t.reset();
        a.backgroundTexture_.reset(); a.playerTexture_.reset();
        a.playerMoveHorizontalTexture_.reset(); a.enemyMoveHorizontalTexture_.reset();
        a.raidSmokeTerrain_.reset(); a.indoorInformationLayer_.reset(); a.indoorShadowMask_.reset();
        a.quickWheelBackdrop_.reset();a.uiTextRenderer_.shutdown(); a.renderer_=nullptr;
    }
    static void loadArt(App &a) {
        const auto root=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        for(std::size_t i=0;i<itemCount();++i) {
            const auto &d=publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i]=loadInventoryTexture(a.renderer_,root/d.inventoryTexturePath);
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid());
        }
    }
};
namespace {
using A=UiQuickWheelTestAccess;
void key(SDL_Scancode code,bool down) {
    SDL_Event e{}; e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP; e.key.scancode=code;e.key.down=down;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void mouse(Uint8 button,bool down) {
    SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button=button;e.button.down=down;e.button.x=640;e.button.y=360;ASSERT_TRUE(SDL_PushEvent(&e));
}
Vec2 direction(std::size_t i,std::size_t n) {
    const float angle=static_cast<float>(i)*2*std::numbers::pi_v<float>/static_cast<float>(n)-std::numbers::pi_v<float>/2;
    return {640+std::cos(angle)*180,360+std::sin(angle)*180};
}
void inside(SDL_FRect r,SDL_FRect parent={0,0,1280,720}) {
    EXPECT_GE(r.x,parent.x);EXPECT_GE(r.y,parent.y);
    EXPECT_LE(r.x+r.w,parent.x+parent.w+.02F);EXPECT_LE(r.y+r.h,parent.y+parent.h+.02F);
}
class UiQuickWheelTest:public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    std::filesystem::path path;
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    AssetInstanceId rig{};std::vector<AssetInstanceId> medicines,decoys;
    MapDefinitionId deploymentMap{"map.v0.test"};
    auto &session() { return A::session(*app); }
    // Only this exclusively owned temporary test profile is mutated for edge cases.
    ProfileState &profile() { return const_cast<ProfileState &>(session().profile()); }
    virtual void prepareBase(ProfileState &,const ContentRegistry &) {}
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-wheel-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        const auto &c=publishedContentRegistry();auto p=makeNewPublishedProfile("quick-wheel",c);
        p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
        equipDecoyTestGun(p,c);
        rig=p.assets.create(c.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
        auto put=[&](const ItemDefinitionId &definition,std::uint32_t quantity=1) {
            const auto &d=c.item(definition);const auto &pockets=c.item(p.assets.find(rig)->definitionId).containerCompartments;
            for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General) {
                auto container=ProfileContainerId::compartment(rig,n);
                if(auto fit=findFirstProfileFit(p,c,container,d,ItemOrientation::Degrees0))
                    return p.assets.create(d,StoredAssetLocation{container,*fit},quantity);
            }
            return AssetInstanceId{};
        };
        for(const auto &id:{alpha_content::medkit,alpha_content::bandage,alpha_content::tourniquet,alpha_content::painkiller}) {
            const auto asset=put(id);ASSERT_NE(asset,0U);medicines.push_back(asset);
        }
        for(int n=0;n<3;++n) { const auto asset=put(alpha_content::noiseDecoy,3);ASSERT_NE(asset,0U);decoys.push_back(asset); }
        prepareBase(p,c);
        p.currentHealth=70;p.medicalStatus={BleedingSeverity::Light,40000,1000,0,15000};
        const auto valid=validateProfileState(p,c);ASSERT_TRUE(valid.valid)<<valid.message;
        ASSERT_TRUE(SaveRepository(path).save(p,c.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());ASSERT_TRUE(A::flow(*app).deploy(deploymentMap));A::frame(*app);
        attach(1280,720);
    }
    void attach(int w,int h) {
        surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app,renderer);A::text(*app).setRasterScale(w/1280.0F);
    }
    void detach() {
        if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;
    }
    void TearDown() override {
        detach();app.reset();SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void openMedical() { A::point(*app,{640,360});key(SDL_SCANCODE_5,true);A::frame(*app);ASSERT_TRUE(A::medical(*app)); }
    void openThrowable() { A::point(*app,{640,360});key(SDL_SCANCODE_4,true);A::frame(*app);ASSERT_TRUE(A::throwable(*app)); }
    void cancel() { key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,false);key(SDL_SCANCODE_4,false);key(SDL_SCANCODE_5,false);A::frame(*app); }
    void fillTwelve() {
        for(auto id:medicines)ASSERT_TRUE(profile().assets.erase(id));medicines.clear();
        for(auto id:decoys)ASSERT_TRUE(profile().assets.erase(id));decoys.clear();
        const auto &c=publishedContentRegistry();const auto &d=c.item(alpha_content::noiseDecoy);
        const auto &pockets=c.item(profile().assets.find(rig)->definitionId).containerCompartments;
        for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General)
            for(int y=0;y<pockets[n].height;++y)for(int x=0;x<pockets[n].width;++x)
                decoys.push_back(profile().assets.create(d,StoredAssetLocation{ProfileContainerId::compartment(rig,n),{x,y}},1));
        ++profile().revision;ASSERT_EQ(decoys.size(),20U);
    }
    void draw() {
        ASSERT_TRUE(A::beginBackdrop(*app));
        SDL_SetRenderDrawColor(renderer,62,77,67,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,118,128,113,255);SDL_FRect world{520,210,220,300};SDL_RenderFillRect(renderer,&world);
        ASSERT_TRUE(A::composeBackdrop(*app));
        A::draw(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
};

class UiWorkshopFieldWheelTest : public UiQuickWheelTest {
protected:
    AssetInstanceId output{};
    void prepareContract(ProfileState &p,const ContentRegistry &c,
        const BaseManufacturingRecipeDefinitionId &recipe,const ItemDefinitionId &item,
        const ItemDefinitionId &materialId,int minutes) {
        for (const auto &id : {materialId,alpha_content::lootScrap}) {
            const auto &material = c.item(id);
            const auto fit = findFirstProfileFit(p,c,ProfileContainerId::stash(),material,ItemOrientation::Degrees0);
            ASSERT_TRUE(fit);
            ASSERT_NE(p.assets.create(material,StoredAssetLocation{ProfileContainerId::stash(),*fit}),0U);
        }
        const auto order = executeStartBaseManufacturing(p,c,{recipe},{p.revision,"wheel-contract"});
        ASSERT_TRUE(order.succeeded && order.outputAssetId) << order.message;
        static_cast<void>(advanceWorldClock(p.worldClock,minutes));
        ASSERT_TRUE(applyBaseManufacturingThrough(p,c).completed);
        output = *order.outputAssetId;
        ASSERT_EQ(p.assets.find(output)->definitionId,item);
        const auto &definition = c.item(item);
        const auto &pockets = c.item(p.assets.find(rig)->definitionId).containerCompartments;
        for (std::uint32_t i=0; i<pockets.size(); ++i) {
            const auto slot = ProfileContainerId::compartment(rig,i);
            const auto fit = findFirstProfileFit(p,c,slot,definition,ItemOrientation::Degrees0);
            if (!fit) continue;
            ASSERT_TRUE(executeInventory(p,c,InventoryMoveCommand{output,0U,{slot,*fit}},
                {p.revision,"carry-contract-output"}).succeeded);
            return;
        }
        FAIL() << "No rig space for contract output";
    }
    void armOutput() {
        openThrowable();
        const auto ids = A::throwableIds(*app);
        const auto it = std::find(ids.begin(),ids.end(),output);
        ASSERT_NE(it,ids.end());
        const auto index = static_cast<std::size_t>(it-ids.begin());
        A::point(*app,direction(index,8)); A::frame(*app); draw();
        ASSERT_TRUE(A::projection(*app).entries[index].usable);
        key(SDL_SCANCODE_4,false); A::frame(*app);
        ASSERT_EQ(A::aiming(*app),output);
    }
};

class UiFireZoneWheelTest : public UiWorkshopFieldWheelTest {
    void prepareBase(ProfileState &p,const ContentRegistry &c) override {
        prepareContract(p,c,kRaidFireBottleRecipeId,kRaidFireBottleItemId,kRaidFuelCanId,120);
    }
};

class UiProximityAlarmWheelTest : public UiWorkshopFieldWheelTest {
    void prepareBase(ProfileState &p,const ContentRegistry &c) override {
        deploymentMap=kHospitalFloorplanMapId;
        prepareContract(p,c,kRaidAlarmRecipeId,kRaidAlarmItemId,ItemDefinitionId{"item.loot.copper_wire_spool"},90);
    }
};

TEST_F(UiProximityAlarmWheelTest, ContractDeviceIsPlacedOnceAndHiddenHostileRaisesOnlyTheGenericWarning) {
    A::loadRaidTextures(*app);
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    EXPECT_EQ(localizeUiText(A::text(*app).language(),"Proximity Alarm"),"警戒器");
    EXPECT_EQ(localizeUiText(A::text(*app).language(),"Proximity Alarm Contract"),"警戒器委托");
    ASSERT_EQ(publishedContentRegistry().item(kRaidAlarmItemId).category,ItemCategory::Consumable);
    armOutput();
    key(SDL_SCANCODE_ESCAPE,true); A::frame(*app); key(SDL_SCANCODE_ESCAPE,false); A::frame(*app);
    EXPECT_EQ(A::message(*app),"ALARM PLACEMENT CANCELLED");
    ASSERT_NE(profile().assets.find(output),nullptr);
    armOutput();
    auto &world=session().world();
    auto &player=const_cast<Player &>(world.player());
    const auto &use=*publishedContentRegistry().item(kRaidAlarmItemId).alarmUse;
    const Vec2 observer{player.position().x+player.size()*.5F,player.position().y+player.size()*.5F};
    auto &actors=WeaponHandlingTestAccess::actors(world);
    ASSERT_FALSE(actors.empty());
    const Vec2 mapSize=world.playerWorldSize();
    const Vec2 far{observer.x<mapSize.x*.5F?mapSize.x-140.0F:140.0F,
                   observer.y<mapSize.y*.5F?mapSize.y-140.0F:140.0F};
    const Vec2 staging{(observer.x+far.x)*.5F,(observer.y+far.y)*.5F};
    for (auto &enemy : actors) ASSERT_TRUE(enemy.setPosition(staging));
    std::optional<RaidAlarmPlacement> desired;
    for(unsigned i=0; i<16 && !desired; ++i) {
        const float a=2*std::numbers::pi_v<float>*i/16;
        const Vec2 point{observer.x+std::cos(a)*62.0F,observer.y+std::sin(a)*62.0F};
        desired=world.previewProximityAlarm(point,use);
    }
    ASSERT_TRUE(desired);
    GameplayInput aim{}; aim.aimWorldPosition=desired->point; world.update(aim,.016F);
    A::aimAt(*app,desired->point);
    mouse(SDL_BUTTON_LEFT,true); A::frame(*app);
    ASSERT_EQ(world.proximityAlarms().entries().size(),1U) << session().persistenceMessage();
    EXPECT_EQ(A::message(*app),"ALARM DEPLOYED");
    const auto device=world.proximityAlarms().entries().front();
    EXPECT_FALSE(device.triggered);
    const auto lampVisible = [&] {
        const SDL_Rect viewport{640-static_cast<int>(std::round(device.point.x)),
            360-static_cast<int>(std::round(device.point.y)),9999,9999};
        SDL_SetRenderViewport(renderer,&viewport);
        SDL_SetRenderDrawColor(renderer,12,18,16,255); SDL_RenderClear(renderer);
        A::drawAlarms(*app); SDL_SetRenderViewport(renderer,nullptr); SDL_RenderPresent(renderer);
        for(int y=357; y<363; ++y) for(int x=639; x<646; ++x) {
            Uint8 r{},g{},b{},a{};
            if (SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a) && r==165 && g==220 && b==210) return true;
        }
        return false;
    };
    EXPECT_TRUE(lampVisible());
    EXPECT_EQ(profile().assets.find(output),nullptr);
    EXPECT_FALSE(world.shotFiredLastUpdate());
    A::frame(*app,.05F);
    EXPECT_EQ(world.proximityAlarms().entries().size(),1U);
    mouse(SDL_BUTTON_LEFT,false); A::frame(*app);
    // Leave the owner's optical range before a real hostile reaches the device.
    ASSERT_TRUE(player.setPosition({far.x-player.size()*.5F,far.y-player.size()*.5F}));
    aim.aimWorldPosition=far; world.update(aim,.016F);
    auto &enemy=WeaponHandlingTestAccess::actors(world).front();
    const auto id=enemy.combatTargetId();
    const int health=enemy.health();
    ASSERT_TRUE(enemy.setPosition({device.point.x-enemy.size().x*.5F,device.point.y-enemy.size().y*.5F}));
    EXPECT_FALSE(world.raidEnemyObservable(id));
    EXPECT_FALSE(lampVisible());
    A::frame(*app,.016F);
    ASSERT_TRUE(world.proximityAlarms().entries().front().triggered);
    EXPECT_EQ(WeaponHandlingTestAccess::actors(world).front().health(),health);
    EXPECT_FALSE(world.raidEnemyObservable(id));
    EXPECT_EQ(A::raidFeedback(*app),"ALARM TRIGGERED");
    EXPECT_EQ(localizeUiText(A::text(*app).language(),A::raidFeedback(*app)),"警戒器已触发");
    A::frame(*app,.016F);
    EXPECT_FALSE(world.proximityAlarmTriggeredLastUpdate());
    if (const char *folder=std::getenv("RAIDLINE_ALARM_UI_DIR")) {
        std::filesystem::create_directories(folder);
        A::drawRaid(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto file=(std::filesystem::path(folder)/"alarm-warning-1280-zh.png").u8string();
        EXPECT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(file.c_str())));
    }
    for (int i=0; i<80 && world.proximityAlarmAlertSeconds()>0; ++i) A::frame(*app,.1F);
    EXPECT_EQ(world.proximityAlarmAlertSeconds(),0.0F);
    EXPECT_NE(A::raidFeedback(*app),"ALARM TRIGGERED");
}

TEST_F(UiFireZoneWheelTest, WorkshopOutputCanBeCancelledThenThrownToCreateAZoneThatHurtsAndExpires) {
    A::loadRaidTextures(*app); A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    EXPECT_EQ(localizeUiText(A::text(*app).language(),"Fire Bottle"),"燃烧瓶");
    EXPECT_EQ(localizeUiText(A::text(*app).language(),"Fire Zone Contract"),"火区装备委托");
    armOutput(); mouse(SDL_BUTTON_RIGHT,true); A::frame(*app);
    EXPECT_EQ(A::message(*app),"FIRE BOTTLE CANCELLED");
    ASSERT_NE(profile().assets.find(output),nullptr);
    mouse(SDL_BUTTON_RIGHT,false); A::frame(*app); armOutput();
    auto &world = session().world();
    const auto &player = world.player();
    const Vec2 observer{player.position().x+player.size()*.5F,player.position().y+player.size()*.5F};
    const auto &use = *publishedContentRegistry().item(kRaidFireBottleItemId).fireZoneUse;
    std::optional<FireZoneLanding> landing;
    for (unsigned i=0; i<16 && !landing; ++i) {
        const float a=2.0F*std::numbers::pi_v<float>*(static_cast<float>(i)-2.0F)/16.0F;
        const Vec2 target{observer.x+200*std::cos(a),observer.y+200*std::sin(a)};
        const auto probe=world.previewFireZone(target,use);
        if (!probe || std::hypot(probe->point.x-observer.x,probe->point.y-observer.y)<140) continue;
        GameplayInput aim{}; aim.aimWorldPosition=target; world.update(aim,.016F);
        A::aimAt(*app,target); landing=probe;
    }
    ASSERT_TRUE(landing);
    mouse(SDL_BUTTON_LEFT,true); A::frame(*app);
    ASSERT_TRUE(world.fireZones().flight()) << session().persistenceMessage();
    EXPECT_FALSE(world.shotFiredLastUpdate());
    EXPECT_EQ(A::message(*app),"FIRE BOTTLE THROWN");
    EXPECT_EQ(profile().assets.find(output),nullptr);
    const auto actual = world.fireZones().flight()->point;
    mouse(SDL_BUTTON_LEFT,false);
    for (int i=0; i<35 && world.fireZones().zones().empty(); ++i) A::frame(*app,.016F);
    ASSERT_EQ(world.fireZones().zones().size(),1U);
    auto &actors = WeaponHandlingTestAccess::actors(world);
    ASSERT_FALSE(actors.empty());
    const auto targetId=actors.front().combatTargetId();
    const int initialHealth=actors.front().health();
    const auto victim = [&]() -> Enemy & {
        auto &live=WeaponHandlingTestAccess::actors(world);
        return *std::find_if(live.begin(),live.end(),[&](const Enemy &e){return e.combatTargetId()==targetId;});
    };
    for (int i=0; i<12; ++i) {
        auto &enemy=victim();
        ASSERT_TRUE(enemy.setPosition({actual.x-enemy.size().x*.5F,actual.y-enemy.size().y*.5F}));
        A::frame(*app,.1F);
    }
    EXPECT_EQ(victim().health(),initialHealth-use.damagePerSecond);
    if (const char *folder=std::getenv("RAIDLINE_FIRE_UI_DIR")) {
        std::filesystem::create_directories(folder);
        A::drawRaid(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto path=(std::filesystem::path(folder)/"fire-zone-1280-zh.png").u8string();
        EXPECT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(path.c_str())));
    }
    for (int i=0; i<60 && !world.fireZones().zones().empty(); ++i) A::frame(*app,.1F);
    EXPECT_TRUE(world.fireZones().zones().empty());
}

TEST_F(UiFireZoneWheelTest, FlameSurfaceAndDamageStopAtTheSameThinWallAndReachItsOpenEnd) {
    RaidWorldConfig c; c.worldSize={1280,720}; c.playerSpawn={298,286};
    c.lightConeObservationEnabled=true; c.grenadeEnabled=true;
    c.extractionPoint={{1100,600},{100,100}};
    c.ballisticBlockers.push_back({101,{{480,260},{4,70}}});
    WeaponHandlingTestAccess::world(session(),std::move(c));
    auto &world=session().world();
    const Vec2 center{462,310};
    GameplayInput aim{}; aim.aimWorldPosition=center; world.update(aim,.02F);
    const auto landing=world.previewFireZone(center,*publishedContentRegistry().item(kRaidFireBottleItemId).fireZoneUse);
    ASSERT_TRUE(landing);
    RaidFireZoneRuntime field; ASSERT_TRUE(field.start(*landing)); field.advance(.6F);
    world.commitFireZone(std::move(field));
    ASSERT_EQ(world.fireZones().zones().size(),1U);
    const auto &zone=world.fireZones().zones().front();
    const auto lit = [&](int x,int y) {
        Uint8 red{},green{},blue{},alpha{};
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&red,&green,&blue,&alpha));
        return red>green+12 && red>blue+16;
    };
    SDL_SetRenderDrawColor(renderer,15,20,25,255); SDL_RenderClear(renderer);
    A::drawFire(*app); SDL_RenderPresent(renderer);
    for (const auto &[pixel,burns] : std::array<std::pair<Vec2,bool>,4>{{
        {{470,311},true},{{493,310},false},{{493,358},true},{{590,310},false}}}) {
        EXPECT_EQ(world.fireZoneReaches(zone,pixel),burns);
        EXPECT_EQ(lit(static_cast<int>(pixel.x),static_cast<int>(pixel.y)),burns);
    }
}

TEST_F(UiQuickWheelTest, MedicalDefaultAndCenterRetainOriginalSelectedEffect) {
    openMedical();EXPECT_EQ(A::medicalIds(*app),medicines);EXPECT_EQ(A::medicalIndex(*app),0U);
    A::point(*app,direction(2,8));A::frame(*app);EXPECT_EQ(A::medicalIndex(*app),2U);
    A::point(*app,{641,361});A::frame(*app);EXPECT_EQ(A::medicalIndex(*app),2U);
    for(auto id:medicines)EXPECT_GT(profile().assets.find(id)->remainingCharges,0U);
}
TEST_F(UiQuickWheelTest, MedicalReleaseStartsOriginalActionExactlyOnce) {
    const auto charges=profile().assets.find(medicines[0])->remainingCharges;
    openMedical();key(SDL_SCANCODE_5,false);A::frame(*app);
    EXPECT_FALSE(A::medical(*app));ASSERT_TRUE(session().raidActionState().active());
    const auto *action=std::get_if<MedicalRaidAction>(&*session().raidActionState().active());
    ASSERT_NE(action,nullptr);EXPECT_EQ(action->medicalAssetId,medicines[0]);
    EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,charges);
    A::frame(*app);EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, SameMedicalModelTotalsAllInstancesAndBindsLowestRemainingUses) {
    const auto &c=publishedContentRegistry();const auto &d=c.item(alpha_content::medkit);
    const auto container=ProfileContainerId::compartment(rig,0);
    const auto fit=findFirstProfileFit(profile(),c,container,d,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    const auto low=profile().assets.create(d,StoredAssetLocation{container,*fit});
    profile().assets.findMutable(low)->remainingCharges=1;++profile().revision;
    const auto oldUses=profile().assets.find(medicines[0])->remainingCharges;
    openMedical();draw();ASSERT_EQ(A::projection(*app).entries.size(),4);
    EXPECT_EQ(A::medicalIds(*app).front(),low);
    EXPECT_EQ(A::projection(*app).entries.front().quantity,2);
    EXPECT_EQ(A::projection(*app).entries.front().remainingUses,oldUses+1);
    key(SDL_SCANCODE_5,false);A::frame(*app);
    ASSERT_TRUE(session().raidActionState().active());
    const auto *action=std::get_if<MedicalRaidAction>(&*session().raidActionState().active());
    ASSERT_NE(action,nullptr);EXPECT_EQ(action->medicalAssetId,low);
    A::frame(*app,6);
    EXPECT_EQ(profile().assets.find(low),nullptr);
    EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,oldUses);
}
TEST_F(UiQuickWheelTest, EmptySectorReleaseCannotUseOrAimAnotherItem) {
    const auto fingerprint=profileStateFingerprint(profile());
    openMedical();A::point(*app,direction(7,8));A::frame(*app);
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
    openThrowable();A::point(*app,direction(7,8));A::frame(*app);
    key(SDL_SCANCODE_4,false);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST_F(UiQuickWheelTest, HiddenRelativeWheelRoundTripKeepsWindowAimAndConsumesNoMotion) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    auto *window=SDL_CreateWindow("wheel input regression",1280,720,SDL_WINDOW_HIDDEN);ASSERT_NE(window,nullptr);
    struct WindowGuard {App *a;SDL_Window *w;~WindowGuard(){A::bindWindow(*a,nullptr);SDL_DestroyWindow(w);}} guard{app.get(),window};
    A::bindWindow(*app,window);A::anchor(*app);A::captureNow(*app);
    ASSERT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    const auto before=session().world().weaponAimWorldPosition();
    openMedical();A::captureNow(*app);EXPECT_FALSE(SDL_CursorVisible());
    EXPECT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.xrel=110;event.motion.yrel=-90;
    ASSERT_TRUE(SDL_PushEvent(&event));A::frame(*app);A::captureNow(*app);
    EXPECT_NEAR(A::offset(*app).x,110,.01);EXPECT_NEAR(A::offset(*app).y,-90,.01);
    cancel();A::captureNow(*app);A::frame(*app);
    EXPECT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().x,before.x);
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().y,before.y);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, InapplicableDefaultIsNotSilentlyReplacedWithUsableBandage) {
    profile().currentHealth=100;++profile().revision;openMedical();draw();
    EXPECT_FALSE(A::projection(*app).entries[0].usable);EXPECT_TRUE(A::projection(*app).entries[1].usable);
    key(SDL_SCANCODE_5,false);A::frame(*app);
    EXPECT_FALSE(session().raidActionState().active());EXPECT_EQ(A::message(*app),"SELECTED MEDICAL ITEM IS NOT APPLICABLE");
}
TEST_F(UiQuickWheelTest, MedicalEscapeCancelsWithoutUsePauseQuitOrFire) {
    const auto revision=profile().revision;openMedical();cancel();
    EXPECT_FALSE(A::medical(*app));EXPECT_FALSE(A::paused(*app));EXPECT_FALSE(session().raidActionState().active());
    EXPECT_EQ(A::flow(*app).state(),GameFlowState::Raid);EXPECT_EQ(profile().revision,revision);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, MedicalRightClickDoesNotAddANewCancelControl) {
    openMedical();mouse(SDL_BUTTON_RIGHT,true);A::frame(*app);EXPECT_TRUE(A::medical(*app));
    EXPECT_FALSE(session().raidActionState().active());
}
TEST_F(UiQuickWheelTest, ThrowableReleaseOnlyArmsAndFreshClickConsumesOne) {
    openThrowable();EXPECT_EQ(A::throwableIds(*app),(std::vector<AssetInstanceId>{decoys.front()}));key(SDL_SCANCODE_4,false);A::frame(*app);
    ASSERT_EQ(A::aiming(*app),decoys[0]);EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
    mouse(SDL_BUTTON_LEFT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    EXPECT_EQ(profile().assets.find(decoys[0])->quantity,2U);EXPECT_FALSE(session().world().shotFiredLastUpdate());
    A::frame(*app);EXPECT_EQ(profile().assets.find(decoys[0])->quantity,2U);
}
TEST_F(UiQuickWheelTest, SmokeWheelCancelsThenThrowsAndConcealsOnlyTheScreenedWorld) {
    const auto &content = publishedContentRegistry();
    A::loadRaidTextures(*app);
    const auto slot = std::get<StoredAssetLocation>(profile().assets.find(decoys.back())->location);
    ASSERT_TRUE(profile().assets.erase(decoys.back()));
    const auto &definition = content.item(kRaidSmokeItemId);
    const auto id = profile().assets.create(definition, slot);
    ++profile().revision;
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    ASSERT_EQ(localizeUiText(A::text(*app).language(), definition.displayName), "烟雾弹");
    const auto select = [&] {
        openThrowable();
        const auto ids = A::throwableIds(*app);
        const auto found = std::find(ids.begin(), ids.end(), id);
        ASSERT_NE(found, ids.end());
        const std::size_t sector = static_cast<std::size_t>(found - ids.begin());
        A::point(*app, direction(sector,8));
        A::frame(*app); draw();
        ASSERT_TRUE(A::projection(*app).entries[sector].usable);
        key(SDL_SCANCODE_4,false); A::frame(*app);
        ASSERT_EQ(A::aiming(*app), id);
    };
    select();
    mouse(SDL_BUTTON_RIGHT,true); A::frame(*app);
    EXPECT_FALSE(A::aiming(*app));
    EXPECT_EQ(A::message(*app),"SMOKE CANCELLED");
    ASSERT_NE(profile().assets.find(id),nullptr);
    mouse(SDL_BUTTON_RIGHT,false); A::frame(*app);
    select();
    const auto &world = session().world();
    const auto &player = world.player();
    const Vec2 observer{player.position().x+player.size()*.5F,player.position().y+player.size()*.5F};
    // Pick an open throw from the actual published ordinary deployment.
    std::optional<SmokeLanding> landing;
    for (unsigned i=0; i<16 && !landing; ++i) {
        const float a = 2.0F*std::numbers::pi_v<float>*(static_cast<float>(i)-2.0F)/16.0F;
        const Vec2 aim{observer.x+210*std::cos(a),observer.y+210*std::sin(a)};
        const auto candidate = world.previewSmoke(aim,*definition.smokeUse);
        if (candidate && std::hypot(candidate->point.x-observer.x,candidate->point.y-observer.y)>160.0F) {
            A::aimAt(*app,aim); A::frame(*app); landing = candidate;
        }
    }
    ASSERT_TRUE(landing);
    mouse(SDL_BUTTON_LEFT,true); A::frame(*app);
    ASSERT_FALSE(A::aiming(*app));
    ASSERT_EQ(A::message(*app),"SMOKE THROWN") << session().persistenceMessage();
    ASSERT_TRUE(world.smoke().flight());
    EXPECT_EQ(profile().assets.find(id),nullptr);
    EXPECT_FALSE(world.shotFiredLastUpdate());
    mouse(SDL_BUTTON_LEFT,false);
    for (unsigned i=0; i<40 && world.smoke().clouds().empty(); ++i) A::frame(*app,1.0F/60.0F);
    ASSERT_EQ(world.smoke().clouds().size(),1U);
    const auto &cloud = world.smoke().clouds().front();
    const float distance = std::hypot(cloud.point.x-observer.x,cloud.point.y-observer.y);
    ASSERT_GT(distance,cloud.radius);
    const Vec2 ray{(cloud.point.x-observer.x)/distance,(cloud.point.y-observer.y)/distance};
    const Vec2 screened{cloud.point.x+ray.x*(cloud.radius+14),cloud.point.y+ray.y*(cloud.radius+14)};
    const Vec2 side{observer.x-ray.y*90,observer.y+ray.x*90};
    const Vec2 contact{observer.x+ray.x*18,observer.y+ray.y*18};
    const auto width = 1280.0F, height = 720.0F;
    const Vec2 camera{std::round((screened.x+observer.x-width)*.5F),
                      std::round((screened.y+observer.y-height)*.5F)};
    const SDL_Rect viewport{-static_cast<int>(camera.x),-static_cast<int>(camera.y),10000,10000};
    ASSERT_TRUE(SDL_SetRenderViewport(renderer,&viewport));
    SDL_SetRenderDrawColor(renderer,25,35,45,255); SDL_RenderClear(renderer);
    const auto marker = [&](Vec2 p) {
        SDL_SetRenderDrawColor(renderer,246,0,246,255);
        const SDL_FRect r{p.x-3,p.y-3,7,7}; SDL_RenderFillRect(renderer,&r);
    };
    for(const Vec2 p : {screened,side,contact}) marker(p);
    A::drawSmoke(*app,{camera,{width,height}});
    ASSERT_TRUE(SDL_SetRenderViewport(renderer,nullptr));
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    const auto magenta = [&](Vec2 p) {
        Uint8 red{},green{},blue{},alpha{};
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface,static_cast<int>(std::round(p.x-camera.x)),
            static_cast<int>(std::round(p.y-camera.y)),&red,&green,&blue,&alpha));
        return red>240 && green<5 && blue>240;
    };
    EXPECT_FALSE(magenta(screened));
    EXPECT_TRUE(magenta(side));
    EXPECT_TRUE(magenta(contact));
    if (const char *directory=std::getenv("RAIDLINE_SMOKE_UI_DIR")) {
        std::filesystem::create_directories(directory);
        A::drawRaid(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto image=(std::filesystem::path(directory)/"smoke-cover-1280-zh.png").u8string();
        EXPECT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(image.c_str())));
    }
}

TEST_F(UiQuickWheelTest, TwentyStacksGroupOnceAndOneConfirmedThrowConsumesOneInstance) {
    fillTwelve();openThrowable();draw();
    ASSERT_EQ(A::throwableIds(*app).size(),1);ASSERT_EQ(A::projection(*app).entries.size(),1);
    EXPECT_EQ(A::projection(*app).entries.front().quantity,20);EXPECT_EQ(A::layout(*app).markers.size(),8);
    A::point(*app,direction(0,8));A::frame(*app);A::point(*app,{640,360});A::frame(*app);
    EXPECT_EQ(A::throwableIndex(*app),0);key(SDL_SCANCODE_4,false);A::frame(*app);
    EXPECT_EQ(A::aiming(*app),decoys.front());
    mouse(SDL_BUTTON_LEFT,true);A::frame(*app);
    EXPECT_EQ(profile().assets.find(decoys.front()),nullptr);
    for(std::size_t i=1;i<decoys.size();++i)EXPECT_EQ(profile().assets.find(decoys[i])->quantity,1);
}
TEST_F(UiQuickWheelTest, ThrowableCancelAndHeldMouseCannotBecomeAnAccidentalShot) {
    mouse(SDL_BUTTON_LEFT,true);openThrowable();key(SDL_SCANCODE_4,false);A::frame(*app);
    EXPECT_TRUE(A::aiming(*app));EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
    mouse(SDL_BUTTON_RIGHT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    A::frame(*app);EXPECT_FALSE(session().world().shotFiredLastUpdate());EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
}
TEST_F(UiQuickWheelTest, LostFocusClosesBothSelectorsWithoutConsumption) {
    openMedical();SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    EXPECT_FALSE(A::medical(*app));EXPECT_FALSE(A::throwable(*app));EXPECT_FALSE(session().raidActionState().active());
    for(auto id:decoys)EXPECT_EQ(profile().assets.find(id)->quantity,3U);
}
TEST_F(UiQuickWheelTest, TwoSelectorsRemainMutuallyExclusive) {
    openThrowable();key(SDL_SCANCODE_5,true);A::frame(*app);EXPECT_FALSE(A::medical(*app));EXPECT_TRUE(A::throwable(*app));
}
TEST_F(UiQuickWheelTest, MedicalModalRoundTripPreservesWindowAimAnchor) {
    A::anchor(*app); // Normal world capture has already established this anchor.
    const auto before=session().world().weaponAimWorldPosition();openMedical();EXPECT_FALSE(A::capture(*app));
    A::point(*app,{100,600});A::frame(*app);cancel();
    const auto after=session().world().weaponAimWorldPosition();EXPECT_FLOAT_EQ(before.x,after.x);EXPECT_FLOAT_EQ(before.y,after.y);
}
TEST_F(UiQuickWheelTest, MissingMedicalCandidateFailsInsteadOfChoosingAnotherInstance) {
    openMedical();ASSERT_TRUE(profile().assets.erase(medicines[0]));++profile().revision;draw();
    EXPECT_EQ(A::projection(*app).entries[0].instanceId,medicines[0]);EXPECT_FALSE(A::projection(*app).entries[0].usable);
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
}
TEST_F(UiQuickWheelTest, MissingThrowableCandidateCannotConsumeAReplacement) {
    openThrowable();ASSERT_TRUE(profile().assets.erase(decoys[0]));++profile().revision;draw();
    key(SDL_SCANCODE_4,false);A::frame(*app);mouse(SDL_BUTTON_LEFT,true);A::frame(*app);
    EXPECT_EQ(profile().assets.find(decoys[1])->quantity,3U);EXPECT_FALSE(session().world().noiseDecoy().flight());
}
TEST_F(UiQuickWheelTest, EmptyChestShowsEightEmptySectorsAndReleaseDoesNothing) {
    for(auto id:medicines)ASSERT_TRUE(profile().assets.erase(id));
    for(auto id:decoys)ASSERT_TRUE(profile().assets.erase(id));++profile().revision;
    openMedical();draw();EXPECT_EQ(A::layout(*app).markers.size(),8);EXPECT_TRUE(A::projection(*app).entries.empty());
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
    openThrowable();draw();EXPECT_EQ(A::layout(*app).markers.size(),8);
    key(SDL_SCANCODE_4,false);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_FALSE(session().world().noiseDecoy().flight());
}
TEST_F(UiQuickWheelTest, ProjectionRefreshesHealthAndBleedingWithoutPollingTheWholeInventory) {
    UiQuickWheelProjection projection;const auto &c=publishedContentRegistry();
    ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));const auto generation=projection.generation;
    EXPECT_FALSE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));
    --profile().medicalStatus.lightBleedingRemainingMs;--profile().medicalStatus.painScreamRemainingMs;
    EXPECT_FALSE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_EQ(projection.generation,generation);
    profile().currentHealth=100;ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_FALSE(projection.entries[0].usable);
    profile().medicalStatus.bleeding=BleedingSeverity::None;ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_FALSE(projection.entries[1].usable);
}
TEST_F(UiQuickWheelTest, RenderingAndHighlightAnimationAreReadOnlyAndReuseTextTextures) {
    openMedical();draw();const auto generation=A::projection(*app).generation;const auto count=A::text(*app).cachedLabelCount();
    const auto revision=profile().revision;const auto charges=profile().assets.find(medicines[0])->remainingCharges;
    for(int i=0;i<12;++i){A::clock(*app,.01F);draw();}
    EXPECT_EQ(A::projection(*app).generation,generation);EXPECT_EQ(A::text(*app).cachedLabelCount(),count);
    EXPECT_EQ(profile().revision,revision);EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,charges);
}
TEST_F(UiQuickWheelTest, CompactNamesUseActualDefinitionsAndKeepFullDetailNames) {
    const auto &d=publishedContentRegistry().item(ItemDefinitionId{"item.weapon.rifle_5_45_service"});
    const auto original=d.displayName;
    EXPECT_EQ(compactUiItemName(d,UiLanguage::SimplifiedChinese),"5.45步枪");
    EXPECT_EQ(compactUiItemName(d,UiLanguage::English),"5.45 Rifle");
    EXPECT_EQ(d.displayName,original);
    EXPECT_EQ(compactUiItemName(publishedContentRegistry().item(alpha_content::ammunition),UiLanguage::SimplifiedChinese),"9mm标");
}
TEST_F(UiQuickWheelTest, CacheRefreshKeepsOriginalInstanceOrderAndCorrectCountMeaning) {
    UiQuickWheelProjection p;const auto &c=publishedContentRegistry();
    ASSERT_TRUE(p.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));
    profile().assets.findMutable(medicines[0])->remainingCharges=1;++profile().revision;
    ASSERT_TRUE(p.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_EQ(p.entries[0].remainingUses,1);
    auto ids=decoys;std::reverse(ids.begin(),ids.end());ASSERT_TRUE(p.refresh(profile(),c,ids,UiQuickWheelKind::Throwable));
    ASSERT_EQ(p.entries.size(),1);EXPECT_EQ(p.entries.front().instanceId,ids.front());EXPECT_EQ(p.entries.front().quantity,9);
}
TEST_F(UiQuickWheelTest, SharedDrawingPreservesRendererStateForFollowingAcceptedUi) {
    openMedical();SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer,21,41,61,81);SDL_SetRenderScale(renderer,.9F,.9F);
    SDL_Rect clip{50,50,1100,600};ASSERT_TRUE(SDL_SetRenderClipRect(renderer,&clip));A::draw(*app);
    SDL_BlendMode blend{};SDL_GetRenderDrawBlendMode(renderer,&blend);EXPECT_EQ(blend,SDL_BLENDMODE_ADD);
    Uint8 r{},g{},b{},alpha{};SDL_GetRenderDrawColor(renderer,&r,&g,&b,&alpha);
    EXPECT_EQ(r,21);EXPECT_EQ(g,41);EXPECT_EQ(b,61);EXPECT_EQ(alpha,81);
    float x{},y{};SDL_GetRenderScale(renderer,&x,&y);EXPECT_FLOAT_EQ(x,.9F);EXPECT_FLOAT_EQ(y,.9F);
    SDL_Rect after{};SDL_GetRenderClipRect(renderer,&after);EXPECT_TRUE(SDL_RenderClipEnabled(renderer));
    EXPECT_EQ(after.x,clip.x);EXPECT_EQ(after.y,clip.y);EXPECT_EQ(after.w,clip.w);EXPECT_EQ(after.h,clip.h);
}

TEST_F(UiQuickWheelTest, ThreeSizesTwoLanguagesAndFiveScenariosHaveReadableOffscreenPreviews) {
    for(int scenario=0;scenario<5;++scenario) {
        if(scenario==0)openMedical();
        if(scenario==1){profile().currentHealth=100;profile().medicalStatus={};++profile().revision;}
        if(scenario==2){cancel();openThrowable();}
        if(scenario==3){cancel();for(std::size_t i=1;i<decoys.size();++i)ASSERT_TRUE(profile().assets.erase(decoys[i]));decoys.resize(1);++profile().revision;openThrowable();}
        if(scenario==4){cancel();fillTwelve();openThrowable();A::point(*app,direction(0,8));A::frame(*app);}
        for(auto size:kUiWindowSizes)for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
            detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
            A::clock(*app,.2F);draw();inside(A::layout(*app).panel);EXPECT_TRUE(A::layout(*app).rows.empty());
            for(const auto &r:A::layout(*app).rows){inside(r.bounds,A::layout(*app).panel);inside(r.name.bounds,r.bounds);inside(r.count.bounds,r.bounds);}
            for(const auto &r:A::layout(*app).markers)inside(r,A::layout(*app).panel);
            if(const auto dir=std::getenv("RAIDLINE_QUICK_WHEEL_QA_DIR")) {
                std::filesystem::create_directories(dir);
                const auto name=std::to_string(scenario)+"-"+std::to_string(size.width)+(language==UiLanguage::English?"-en.png":"-zh.png");
                const auto utf8=(std::filesystem::path(dir)/name).u8string();
                ASSERT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(utf8.c_str())))<<SDL_GetError();
            }
        }
    }
}

TEST(UiQuickWheelLayoutTest, NineTypesUseTwoFixedEightSectorPagesWithoutLosingSelection) {
    std::vector<UiQuickWheelEntry> entries;
    for(int i=0;i<9;++i)entries.push_back({static_cast<AssetInstanceId>(i+1),"type","x1","",true});
    for(std::size_t page=0;page<2;++page) {
        const auto l=layoutUiQuickWheel(entries,page*8,UiQuickWheelKind::Throwable,UiLanguage::English,[](auto,int){return 1.0F;},page);
        ASSERT_EQ(l.markers.size(),8);EXPECT_EQ(l.page,page);EXPECT_EQ(l.selected,page*8);EXPECT_FALSE(l.range.text.empty());
        for(std::size_t i=0;i<8;++i) {
            const auto p=direction(i,8);const auto m=l.markers[i];
            EXPECT_NEAR(m.x+m.w/2,640+(p.x-640)*160/180.0F,.02F);
            EXPECT_NEAR(m.y+m.h/2,360+(p.y-360)*160/180.0F,.02F);
        }
        EXPECT_TRUE(l.rows.empty());
    }
}
TEST(UiQuickWheelLayoutTest, RingOnlyLayoutHasNoSidePanelOrSurroundingHints) {
    const std::vector<UiQuickWheelEntry> entries{{1,"Medkit","x1","",true}};
    for(auto kind:{UiQuickWheelKind::Medical,UiQuickWheelKind::Throwable}) {
        const auto l=layoutUiQuickWheel(entries,0,kind,UiLanguage::English,[](auto,int){return 1.0F;});
        EXPECT_TRUE(l.title.text.empty());EXPECT_TRUE(l.subtitle.text.empty());EXPECT_TRUE(l.hint.text.empty());
        EXPECT_TRUE(l.rows.empty());EXPECT_EQ(l.summary.w,0);EXPECT_EQ(l.summary.h,0);EXPECT_EQ(l.markers.size(),8);
    }
}
TEST(UiQuickWheelLayoutTest, EmphasisStartsImmediatelyAndCannotDelayConfirmation) {
    UiQuickWheelEmphasis e;e.select(3,UiQuickWheelKind::Medical,5);EXPECT_EQ(e.id,3U);EXPECT_FLOAT_EQ(e.amount(5),0);
    EXPECT_NEAR(e.amount(5.06F),.5F,.001F);EXPECT_FLOAT_EQ(e.amount(6),1);
    e.select(4,UiQuickWheelKind::Medical,6);EXPECT_EQ(e.id,4U);EXPECT_FLOAT_EQ(e.amount(6),0);
}
TEST(UiQuickWheelLayoutTest, EmptyProjectionKeepsEightEmptySectorsWithoutInventedItems) {
    const auto l=layoutUiQuickWheel({},99,UiQuickWheelKind::Medical,UiLanguage::English,[](auto s,int size){return s.size()*size*.6F;});
    EXPECT_TRUE(l.rows.empty());EXPECT_EQ(l.markers.size(),8);EXPECT_TRUE(l.name.text.empty());
}
}
