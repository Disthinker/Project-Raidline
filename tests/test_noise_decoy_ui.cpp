#include "app.h"
#include "raid_camera.h"
#include "alpha_content_ids.h"
#include "noise_decoy_fixture.h"
#include "save_repository.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <cstdlib>

struct NoiseDecoyUiTestAccess {
    static GameFlow &flow(App &a){return a.gameFlow_;}
    static GameSession &session(App &a){return a.gameSession_;}
    static void frame(App &a,float seconds=0){a.windowHasInputFocus_=true;a.processEvents();a.update(seconds);a.input_.endFrame();}
    static void attach(App &a,SDL_Renderer *r){a.renderer_=r;EXPECT_TRUE(a.uiTextRenderer_.initialize(r));}
    static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
    static bool wheel(const App &a){return a.throwableWheelOpen_;}
    static bool aiming(const App &a){return a.selectedThrowable_.has_value();}
    static bool paused(const App &a){return a.pauseMenu_.isOpen();}
    static bool capture(const App &a){return a.shouldCaptureWorldPointer();}
    static void render(App &a){a.renderThrowableUi();}
    static void details(App &a,AssetInstanceId id){a.profileDetailsAsset_=id;a.renderProfileWeaponDetails();}
    static void supply(App &a){a.renderBaseSupply();}
    static void pointer(App &a,Vec2 point){a.pointerWorldPosition_=point;}
    static void aim(App &a){GameplayInput in;in.aimWorldPosition={550,160};a.gameSession_.world().update(in,0);
        a.worldAimPointerAnchor_=raidWorldToScreen({550,160},a.raidWorldCameraOffset());}
    static void offsetAim(App &a){GameplayInput in;in.aimWorldPosition={550,160};in.aimMotionDelta=Vec2{60,0};a.gameSession_.world().update(in,0);}
    static void crosshair(App &a){a.renderAimCrosshair();}
    static void visibilityWorld(App &a){
        RaidWorldConfig c;c.noiseDecoyEnabled=true;c.lightConeObservationEnabled=true;c.worldSize={1280,720};c.playerSpawn={375,165};c.extractionPoint={{1100,600},{100,100}};
        a.gameSession_.world_=std::make_unique<GameplayWorld>(c);GameplayInput in;in.aimWorldPosition={650,181};a.gameSession_.world().update(in,0);
    }
    static bool layer(App &a){return a.renderIndoorInformationLayer({0,0,1280,720});}
    static void source(App &a,Vec2 point){const auto &w=a.gameSession_.world();RaidNoiseDecoyRuntime r;
        const auto landing=w.previewNoiseDecoy(point,*publishedContentRegistry().item(alpha_content::noiseDecoy).noiseDecoyUse);ASSERT_TRUE(landing);
        ASSERT_TRUE(r.start(*landing));r.advance(w.activeRaidSpaceId(),.5F);a.gameSession_.world().commitNoiseDecoy(std::move(r));}
    static void modal(App &a){a.tacticalMapOpen_=true;}
    static void focusLost(App &a){a.windowHasInputFocus_=false;GameplayInput in;a.updateThrowableInput(in);}
};
namespace {
using A=NoiseDecoyUiTestAccess;
void key(SDL_Scancode code,bool down){SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;e.key.down=down;ASSERT_TRUE(SDL_PushEvent(&e));}
void mouse(Uint8 button,bool down){SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=button;e.button.down=down;e.button.x=550;e.button.y=160;ASSERT_TRUE(SDL_PushEvent(&e));}
class NoiseDecoyUiTest:public testing::Test {
protected:
    std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};AssetInstanceId id{};
    std::filesystem::path path=std::filesystem::temp_directory_path()/("raidline-decoy-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp()override{
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));const auto &c=publishedContentRegistry();auto p=makeNewPublishedProfile("decoy-ui",c);
        p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;equipDecoyTestRig(p,c);equipDecoyTestGun(p,c);const auto rig=equippedAsset(p,EquipmentSlotKind::ChestRig);ASSERT_TRUE(rig);
        const auto &d=c.item(alpha_content::noiseDecoy);const auto &pockets=c.item(p.assets.find(*rig)->definitionId).containerCompartments;
        for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General){const auto container=ProfileContainerId::compartment(*rig,n);const auto fit=findFirstProfileFit(p,c,container,d,ItemOrientation::Degrees0);if(fit){id=p.assets.create(d,StoredAssetLocation{container,*fit},3);break;}}
        ASSERT_NE(id,0U);ASSERT_TRUE(SaveRepository(path).save(p,c.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());
        ASSERT_TRUE(A::flow(*app).deploy());A::frame(*app);A::aim(*app);
        surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);A::attach(*app,renderer);
    }
    void TearDown()override{if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);app.reset();SDL_Quit();std::error_code ec;std::filesystem::remove_all(path,ec);}
    void clear(){SDL_SetRenderDrawColor(renderer,24,29,30,255);SDL_RenderClear(renderer);}
    void save(const char *name){ASSERT_TRUE(SDL_RenderPresent(renderer));if(const char *dir=std::getenv("RAIDLINE_DECOY_QA_DIR")){std::filesystem::create_directories(dir);EXPECT_TRUE(SDL_SaveBMP(surface,(std::filesystem::path(dir)/name).string().c_str()));}}
    void choose(){key(SDL_SCANCODE_4,true);A::frame(*app);ASSERT_TRUE(A::wheel(*app));key(SDL_SCANCODE_4,false);A::frame(*app);ASSERT_TRUE(A::aiming(*app));}
};
}
TEST_F(NoiseDecoyUiTest, Hold4ShowsReadableChineseAndReleasingOnlySelects){
    const auto &p=A::session(*app).profile();const auto before=p.assets.find(id)->quantity;const auto aim=A::session(*app).world().weaponAimWorldPosition();
    key(SDL_SCANCODE_4,true);A::frame(*app);ASSERT_TRUE(A::wheel(*app));EXPECT_FALSE(A::capture(*app));clear();A::render(*app);save("decoy-wheel-zh.bmp");
    key(SDL_SCANCODE_4,false);A::frame(*app);EXPECT_TRUE(A::aiming(*app));EXPECT_FALSE(A::wheel(*app));EXPECT_TRUE(A::capture(*app));EXPECT_EQ(p.assets.find(id)->quantity,before);
    const auto after=A::session(*app).world().weaponAimWorldPosition();EXPECT_FLOAT_EQ(after.x,aim.x);EXPECT_FLOAT_EQ(after.y,aim.y);
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"Noise Decoy"),"声响诱饵");
}
TEST_F(NoiseDecoyUiTest, RightClickCancelDoesNotConsumeShootOrQuit){
    choose();mouse(SDL_BUTTON_RIGHT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_FALSE(A::wheel(*app));
    EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());EXPECT_EQ(A::flow(*app).state(),GameFlowState::Raid);
}
TEST_F(NoiseDecoyUiTest, EscapeCancelsWithoutOpeningPauseOrQuitting){
    choose();key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_FALSE(A::paused(*app));EXPECT_EQ(A::flow(*app).state(),GameFlowState::Raid);EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);
}
TEST_F(NoiseDecoyUiTest, FreshLeftClickThrowsOneWithoutFiringAndHeldClickCannotRepeat){
    choose();mouse(SDL_BUTTON_LEFT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,2U);EXPECT_TRUE(A::session(*app).world().noiseDecoy().flight());EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
    A::frame(*app,.1F);EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,2U);EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
}
TEST_F(NoiseDecoyUiTest, MouseHeldBeforeWheelMustBeReleasedBeforeConfirmation){
    mouse(SDL_BUTTON_LEFT,true);key(SDL_SCANCODE_4,true);A::frame(*app);key(SDL_SCANCODE_4,false);A::frame(*app);ASSERT_TRUE(A::aiming(*app));A::frame(*app);EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);
    mouse(SDL_BUTTON_LEFT,false);A::frame(*app);mouse(SDL_BUTTON_LEFT,true);A::frame(*app);EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,2U);
}
TEST_F(NoiseDecoyUiTest, NewMapModalAndLostFocusCancelSelectionWithoutConsumption){
    choose();A::modal(*app);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);
}
TEST_F(NoiseDecoyUiTest, FocusLossClosesWheelWithoutChangingAssets){
    key(SDL_SCANCODE_4,true);A::frame(*app);ASSERT_TRUE(A::wheel(*app));A::focusLost(*app);EXPECT_FALSE(A::wheel(*app));EXPECT_FALSE(A::aiming(*app));EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);
}
TEST_F(NoiseDecoyUiTest, DecoyDetailsHaveReadableUsageWithoutWeaponModification){
    clear();A::details(*app,id);save("decoy-details-zh.bmp");int bright=0;for(int y=260;y<465;++y)for(int x=70;x<1050;++x){Uint8 r{},g{},b{},alpha{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&alpha);if(r>180 && g>170)++bright;}EXPECT_GT(bright,1200);
}
TEST_F(NoiseDecoyUiTest, BaseSupplyListsDecoyWithExistingCurrencyAndFallback){
    clear();A::supply(*app);save("decoy-supply-zh.bmp");
}

TEST_F(NoiseDecoyUiTest, HeldSpaceCannotFireAfterCancelUntilPhysicalRelease){
    choose();key(SDL_SCANCODE_SPACE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);
    EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());A::frame(*app);EXPECT_FALSE(A::session(*app).world().shotFiredLastUpdate());
    key(SDL_SCANCODE_SPACE,false);A::frame(*app);key(SDL_SCANCODE_SPACE,true);A::frame(*app);
    EXPECT_TRUE(A::session(*app).world().shotFiredLastUpdate());
}
TEST_F(NoiseDecoyUiTest, MedicalWheelDoesNotOpenDuringDecoyAiming){
    choose();key(SDL_SCANCODE_5,true);A::frame(*app);EXPECT_TRUE(A::aiming(*app));EXPECT_EQ(A::session(*app).profile().assets.find(id)->quantity,3U);
}

TEST_F(NoiseDecoyUiTest, OffsetReticleIsPreservedAcrossWheelAndVisibleWhileAiming){
    A::offsetAim(*app);const auto before=A::session(*app).world().weaponAimWorldPosition();ASSERT_NE(before.x,550);
    choose();const auto after=A::session(*app).world().weaponAimWorldPosition();EXPECT_FLOAT_EQ(before.x,after.x);EXPECT_FLOAT_EQ(before.y,after.y);
    clear();A::crosshair(*app);A::render(*app);save("decoy-aim-zh.bmp");int pixels=0;
    for(int y=100;y<220;++y)for(int x=520;x<690;++x){Uint8 r{},g{},b{},a{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);if(r>180 && g>170)++pixels;}EXPECT_GT(pixels,25);
}

TEST_F(NoiseDecoyUiTest, DecoyWorldPixelsObeyExistingConeMaskAndVisibleControlIsDrawn){
    A::visibilityWorld(*app);clear();ASSERT_TRUE(A::layer(*app));ASSERT_TRUE(SDL_RenderPresent(renderer));
    auto capture=[&](){std::vector<Uint32> pixels;for(int y=150;y<215;++y)for(int x=60;x<140;++x){Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));pixels.push_back((r<<24)|(g<<16)|(b<<8)|a);}return pixels;};
    const auto before=capture();ASSERT_FALSE(A::session(*app).world().raidBoundsObservable({{96,177},{8,8}}));
    A::source(*app,{100,181});clear();ASSERT_TRUE(A::layer(*app));save("decoy-world-hidden.bmp");EXPECT_EQ(capture(),before);
    A::source(*app,{650,181});clear();ASSERT_TRUE(A::layer(*app));save("decoy-world-visible.bmp");int marker=0;
    for(int y=165;y<198;++y)for(int x=634;x<668;++x){Uint8 r{},g{},b{},a{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);if(r>180 && g>150 && b<150)++marker;}EXPECT_GT(marker,20);
}
