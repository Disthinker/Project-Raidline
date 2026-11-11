#include "app.h"
#include "grenade_domain.h"
#include "noise_decoy_fixture.h"
#include "raid_camera.h"
#include "save_repository.h"
#include "ui_localization.h"
#include "ui_raid_illumination.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

// Reuse the existing narrow App test access entry; no production-only test UI.
struct NoiseDecoyUiTestAccess
{
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static void frame(App &a, float seconds=0) { a.windowHasInputFocus_=true; a.processEvents(); a.update(seconds); a.input_.endFrame(); }
    static bool wheel(const App &a) { return a.throwableWheelOpen_; }
    static auto aiming(const App &a) { return a.selectedThrowable_; }
    static bool paused(const App &a) { return a.pauseMenu_.isOpen(); }
    static auto &message(App &a) { return a.uiMessage_; }
    static bool capture(const App &a) { return a.shouldCaptureWorldPointer(); }
    static void sector(App &a, std::size_t sector) { a.throwableWheelSelected_=sector; }
    static void attach(App &a, SDL_Renderer *renderer) { a.renderer_=renderer; EXPECT_TRUE(a.uiTextRenderer_.initialize(renderer)); }
    static void detach(App &a) {
        a.indoorInformationLayer_.reset();
        a.indoorShadowMask_.reset();
        a.uiTextRenderer_.shutdown();
        a.renderer_=nullptr;
    }
    static void aim(App &a) {
        GameplayInput input; input.aimWorldPosition={550,160}; a.gameSession_.world().update(input,0);
        a.worldAimPointerAnchor_=raidWorldToScreen(a.gameSession_.world().weaponAimWorldPosition(),a.raidWorldCameraOffset());
    }
    static void details(App &a, AssetInstanceId id) { a.profileDetailsAsset_=id; a.renderProfileWeaponDetails(); }
    static void drawWorld(App &a) { EXPECT_TRUE(a.renderIndoorInformationLayer({0,0,1280,720})) << SDL_GetError(); }
    static void visibilityWorld(App &a, bool withWall = false) {
        RaidWorldConfig config; config.noiseDecoyEnabled=true; config.grenadeEnabled=true;
        config.lightConeObservationEnabled=true; config.worldSize={1280,720};config.playerSpawn={375,165};
        config.extractionPoint={{1100,600},{100,100}};
        if (withWall)
            config.ballisticBlockers.push_back({1, {{910, 205}, {26, 165}}});
        a.gameSession_.world_=std::make_unique<GameplayWorld>(config);
        a.gameSession_.world().setRaidDaylight(!withWall);
        GameplayInput input; input.aimWorldPosition={650,181}; a.gameSession_.world().update(input,0);
    }
    static void fuse(App &a, Vec2 point) {
        const auto &world=a.gameSession_.world();
        const auto &use=*publishedContentRegistry().item(ItemDefinitionId{"item.throwable.frag_grenade_basic"}).grenadeUse;
        const auto landing=world.previewGrenade(point,use); ASSERT_TRUE(landing);
        RaidGrenadeRuntime runtime; ASSERT_TRUE(runtime.start(*landing));
        runtime.advance(world.activeRaidSpaceId(),use.flightSeconds);
        a.gameSession_.world().commitGrenade(std::move(runtime));
    }
};

namespace
{
using Access=NoiseDecoyUiTestAccess;
const ItemDefinitionId grenade{"item.throwable.frag_grenade_basic"};
void key(SDL_Scancode code, bool down) {
    SDL_Event event{}; event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.scancode=code; event.key.down=down; ASSERT_TRUE(SDL_PushEvent(&event));
}
void mouse(Uint8 button, bool down) {
    SDL_Event event{}; event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button=button; event.button.down=down; event.button.x=550; event.button.y=160;
    ASSERT_TRUE(SDL_PushEvent(&event));
}
AssetInstanceId addRigItem(ProfileState &profile, const ItemDefinitionId &id, std::uint32_t quantity) {
    const auto &content=publishedContentRegistry();
    const auto rig=equippedAsset(profile,EquipmentSlotKind::ChestRig);
    if(!rig)return 0;
    const auto &definition=content.item(id);
    const auto &pockets=content.item(profile.assets.find(*rig)->definitionId).containerCompartments;
    for(std::uint32_t index{}; index<pockets.size(); ++index)
        if(pockets[index].pocketKind==ContainerPocketKind::General) {
            const auto container=ProfileContainerId::compartment(*rig,index);
            if(const auto fit=findFirstProfileFit(profile,content,container,definition,ItemOrientation::Degrees0))
                return profile.assets.create(definition,StoredAssetLocation{container,*fit},quantity);
        }
    return 0;
}
class GrenadeUiTest : public testing::Test
{
protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    AssetInstanceId grenadeId{}, secondGrenadeId{}, decoyId{};
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("raidline-grenade-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        const auto &content=publishedContentRegistry();
        auto profile=makeNewPublishedProfile("grenade-ui",content);
        profile.tutorial=TutorialProgress::Complete; profile.homeFounding.hintsDismissed=true;
        equipDecoyTestRig(profile,content); equipDecoyTestGun(profile,content);
        grenadeId=addRigItem(profile,grenade,2); secondGrenadeId=addRigItem(profile,grenade,1);
        decoyId=addRigItem(profile,alpha_content::noiseDecoy,3);
        ASSERT_NE(grenadeId,0U);ASSERT_NE(secondGrenadeId,0U);ASSERT_NE(decoyId,0U);
        ASSERT_TRUE(SaveRepository(path).save(profile,content.contentVersion()).succeeded);
        app=std::make_unique<App>(); Access::flow(*app).configurePersistence(path);
        ASSERT_TRUE(Access::flow(*app).continueGame()); ASSERT_TRUE(Access::flow(*app).deploy());
        Access::frame(*app);Access::aim(*app);
        surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);Access::attach(*app,renderer);
    }
    void TearDown() override {
        if(app)Access::detach(*app);
        SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);app.reset();SDL_Quit();
        std::error_code error;std::filesystem::remove_all(path,error);
    }
    auto &session() { return Access::session(*app); }
    void choose(std::size_t sector=0) {
        key(SDL_SCANCODE_4,true);Access::frame(*app);ASSERT_TRUE(Access::wheel(*app));
        Access::sector(*app,sector);key(SDL_SCANCODE_4,false);Access::frame(*app);
        ASSERT_TRUE(Access::aiming(*app));
    }
    void clear() { SDL_SetRenderDrawColor(renderer,24,29,30,255);SDL_RenderClear(renderer); }
};
}

TEST_F(GrenadeUiTest, WheelMergesGrenadeStacksWithoutMergingDecoyCapability)
{
    const auto &profile=session().profile();const auto &content=publishedContentRegistry();
    const auto ids=quickWheelCandidates(profile,content,UiQuickWheelKind::Throwable);
    ASSERT_EQ(ids.size(),2U);
    const auto entries=projectUiQuickWheel(profile,content,ids,UiQuickWheelKind::Throwable);
    ASSERT_EQ(entries.size(),2U);
    EXPECT_EQ(entries[0].definitionId,grenade);EXPECT_EQ(entries[0].quantity,3U);EXPECT_TRUE(entries[0].usable);
    EXPECT_EQ(entries[1].definitionId,alpha_content::noiseDecoy);EXPECT_EQ(entries[1].quantity,3U);EXPECT_TRUE(entries[1].usable);
    EXPECT_EQ(compactUiItemName(content.item(grenade),UiLanguage::SimplifiedChinese),"破片");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,content.item(grenade).displayName),"基础破片手雷");
}

TEST_F(GrenadeUiTest, GrenadeDetailsUseChineseWarningAndNoWeaponModificationAction)
{
    const auto &content=publishedContentRegistry();const auto &profile=session().profile();
    const auto model=projectUiItemDetails(profile,content,grenadeId,UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(model);EXPECT_EQ(model->kind,UiItemDetailsKind::Grenade);
    EXPECT_EQ(model->name,"基础破片手雷");EXPECT_TRUE(model->warning);EXPECT_EQ(model->metrics.size(),6U);
    EXPECT_NE(model->status.find("伤及自己"),std::string::npos);
    const auto actions=projectUiItemActions(profile,content,grenadeId,true);
    ASSERT_EQ(actions.size(),1U);EXPECT_EQ(actions[0].label,"GRENADE DETAILS");
    clear();Access::details(*app,grenadeId);EXPECT_TRUE(SDL_RenderPresent(renderer));
}

TEST_F(GrenadeUiTest, Release4OnlySelectsAndPreservesWindowCrosshair)
{
    const auto before=session().world().weaponAimWorldPosition();choose();
    EXPECT_EQ(*Access::aiming(*app),grenadeId);EXPECT_FALSE(Access::wheel(*app));EXPECT_TRUE(Access::capture(*app));
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,2U);
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().x,before.x);
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().y,before.y);
    EXPECT_FALSE(session().world().grenade().active());
}

TEST_F(GrenadeUiTest, RightClickAndEscapeCancelWithoutConsumptionShotOrQuit)
{
    choose();mouse(SDL_BUTTON_RIGHT,true);Access::frame(*app);
    EXPECT_FALSE(Access::aiming(*app));EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,2U);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
    mouse(SDL_BUTTON_RIGHT,false);Access::frame(*app);choose();key(SDL_SCANCODE_ESCAPE,true);Access::frame(*app);
    EXPECT_FALSE(Access::aiming(*app));EXPECT_FALSE(Access::paused(*app));
    EXPECT_EQ(Access::flow(*app).state(),GameFlowState::Raid);
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,2U);
}

TEST_F(GrenadeUiTest, FreshClickThrowsExactlyOneAndHeldClickCannotFireOrRepeat)
{
    choose();mouse(SDL_BUTTON_LEFT,true);Access::frame(*app);
    EXPECT_FALSE(Access::aiming(*app));EXPECT_TRUE(session().world().grenade().flight());
    EXPECT_FALSE(session().world().noiseDecoy().flight());EXPECT_FALSE(session().world().shotFiredLastUpdate());
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,1U);
    Access::frame(*app,.1F);
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,1U);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}

TEST_F(GrenadeUiTest, FuseRejectsOpeningAnotherWheelButDoesNotConsumeAnotherStack)
{
    choose();mouse(SDL_BUTTON_LEFT,true);Access::frame(*app);
    mouse(SDL_BUTTON_LEFT,false);Access::frame(*app,.5F);ASSERT_TRUE(session().world().grenade().fuse());
    key(SDL_SCANCODE_4,true);Access::frame(*app);
    EXPECT_FALSE(Access::wheel(*app));EXPECT_FALSE(Access::aiming(*app));
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,1U);
    EXPECT_EQ(session().profile().assets.find(secondGrenadeId)->quantity,1U);
    EXPECT_EQ(Access::message(*app),"LIVE GRENADE | WAIT FOR EXPLOSION");
}

TEST_F(GrenadeUiTest, DecoySelectionStillUsesOriginalRuntimeAndInventory)
{
    choose(1);EXPECT_EQ(*Access::aiming(*app),decoyId);
    mouse(SDL_BUTTON_LEFT,true);Access::frame(*app);
    EXPECT_TRUE(session().world().noiseDecoy().flight());EXPECT_FALSE(session().world().grenade().active());
    EXPECT_EQ(session().profile().assets.find(decoyId)->quantity,2U);
    EXPECT_EQ(session().profile().assets.find(grenadeId)->quantity,2U);
}

TEST_F(GrenadeUiTest, IlluminationUsesTheExistingWheelAndOnlyConsumesOnClick)
{
    auto &profile = const_cast<ProfileState &>(session().profile());
    const auto flare = addRigItem(profile, kThrownIlluminationItemId, 2);
    ASSERT_NE(flare, 0U);
    ++profile.revision;
    const auto &content = publishedContentRegistry();
    const auto model = projectUiItemDetails(profile, content, flare, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(model);
    EXPECT_EQ(model->name, "照明弹");
    EXPECT_EQ(model->title, "照明弹详情");
    clear();
    Access::details(*app, flare);
    EXPECT_TRUE(SDL_RenderPresent(renderer));

    choose();
    ASSERT_EQ(Access::aiming(*app), flare);
    mouse(SDL_BUTTON_RIGHT, true);
    Access::frame(*app);
    EXPECT_FALSE(Access::aiming(*app));
    EXPECT_EQ(profile.assets.find(flare)->quantity, 2U);
    EXPECT_EQ(Access::message(*app), "ILLUMINATION CANCELLED");
    mouse(SDL_BUTTON_RIGHT, false);
    Access::frame(*app);

    choose();
    mouse(SDL_BUTTON_LEFT, true);
    Access::frame(*app);
    EXPECT_TRUE(session().world().thrownIllumination().flight());
    ASSERT_NE(profile.assets.find(flare), nullptr);
    EXPECT_EQ(profile.assets.find(flare)->quantity, 1U);
    EXPECT_EQ(Access::message(*app), "ILLUMINATION THROWN");
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}

TEST_F(GrenadeUiTest, IlluminationCompositingIntersectsPlayerAndSourceFans)
{
    const Rect screen{{0, 0}, {1280, 720}};
    const auto fan = [](Vec2 center, std::vector<Vec2> boundary) {
        RaidVisibilityProjection view;
        view.valid = true;
        view.observer = center;
        view.boundary = std::move(boundary);
        view.maximumDistance = 300.0F;
        return view;
    };
    const auto observer = fan({200, 260}, {{200, 200}, {520, 200}, {520, 320}, {200, 320}});
    auto source = std::make_shared<RaidIlluminationCoverage>();
    source->fans[0] = fan({450, 275}, {{370, 220}, {650, 220}, {650, 340}, {370, 340}});
    auto view = observer;
    view.illumination = std::make_shared<RaidIlluminatedObservation>(
        observer, RaidIlluminationObservationKey{source});
    const auto triangles = clipUiIlluminatedObservation(view, screen);
    ASSERT_FALSE(triangles.empty());
    for (const auto &triangle : triangles)
        for (const auto p : triangle) {
            EXPECT_GE(p.x, 369.99F);
            EXPECT_LE(p.x, 520.01F);
            EXPECT_GE(p.y, 219.99F);
            EXPECT_LE(p.y, 320.01F);
        }

    clear();
    drawUiThrownIllumination(renderer, source->fans, screen);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    Uint8 outside{}, outsideG{}, outsideB{}, outsideA{};
    Uint8 center{}, centerG{}, centerB{}, centerA{};
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 449, 275, &center, &centerG, &centerB, &centerA));
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 700, 275, &outside, &outsideG, &outsideB, &outsideA));
    EXPECT_GT(center, outside + 35);
    EXPECT_GT(centerG, outsideG + 20);
}

TEST_F(GrenadeUiTest, IlluminationWorldShowsTheLitOpeningAcrossWindowSizes)
{
    Access::visibilityWorld(*app, true);
    auto &world = session().world();
    ASSERT_FLOAT_EQ(world.raidObservation().maximumDistance, 600);
    const Rect lit{{1076, 176}, {8, 8}};
    const Rect blocked{{1046, 276}, {8, 8}};
    ASSERT_FALSE(world.raidBoundsObservable(lit));
    ASSERT_FALSE(world.raidBoundsObservable(blocked));
    const auto &use = *publishedContentRegistry().item(kThrownIlluminationItemId).thrownIlluminationUse;
    auto landing = world.previewThrownIllumination({835, 181}, use);
    ASSERT_TRUE(landing);
    landing->effectIdentity = "offscreen-flare";
    ThrownIlluminationRuntime runtime;
    ASSERT_TRUE(runtime.start(*landing));
    runtime.advance(use.flightSeconds);
    world.commitThrownIllumination(std::move(runtime));
    EXPECT_TRUE(world.raidBoundsObservable(lit));
    EXPECT_FALSE(world.raidBoundsObservable(blocked));

    for (const auto size : kUiWindowSizes)
    {
        Access::detach(*app);
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        SDL_DestroySurface(surface);
        surface = SDL_CreateSurface(size.width, size.height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        Access::attach(*app, renderer);
        clear();
        Access::drawWorld(*app);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto readRed = [&](int x, int y) {
            Uint8 red{}, green{}, blue{}, alpha{};
            EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x * size.width / 1280,
                y * size.height / 720, &red, &green, &blue, &alpha));
            return red;
        };
        EXPECT_GT(readRed(1080, 180), readRed(1050, 280));
        if (const char *folder = std::getenv("RAIDLINE_FLARE_PREVIEW_DIR"))
        {
            const std::filesystem::path output{folder};
            ASSERT_TRUE(output.filename().string().starts_with("raidline-flare-review-"));
            std::filesystem::create_directories(output);
            const auto image = output / ("world-" + std::to_string(size.width) + ".bmp");
            EXPECT_TRUE(SDL_SaveBMP(surface, image.string().c_str()));
        }
    }
}

TEST_F(GrenadeUiTest, HiddenGrenadeFuseDoesNotRevealPixelsOutsideTheExistingCone)
{
    Access::visibilityWorld(*app);clear();Access::drawWorld(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    const auto capture=[&] {
        std::vector<Uint32> pixels;
        for(int y=150;y<215;++y)for(int x=60;x<140;++x) {
            Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
            pixels.push_back((r<<24)|(g<<16)|(b<<8)|a);
        }
        return pixels;
    };
    const auto before=capture();ASSERT_FALSE(session().world().raidBoundsObservable({{94,175},{12,12}}));
    Access::fuse(*app,{100,181});clear();Access::drawWorld(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_EQ(capture(),before);
    session().world().commitGrenade({}); // The visible control is an independent prepared scene.
    Access::fuse(*app,{650,181});clear();Access::drawWorld(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    int visible{};
    for(int y=165;y<198;++y)for(int x=634;x<668;++x) {
        Uint8 r{},g{},b{},a{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);
        if(r>170 && g>120 && b<160)++visible;
    }
    EXPECT_GT(visible,10);
}
