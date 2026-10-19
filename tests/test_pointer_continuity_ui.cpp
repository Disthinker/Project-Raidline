#include "app.h"
#include "raid_camera.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>

// Exercise actual SDL capture and event routing with a hidden dummy-driver window.
struct PointerContinuityUiTestAccess {
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static void attach(App &a, SDL_Window *w) { a.window_ = w; }
    static void detach(App &a) { a.window_ = nullptr; }
    static void frame(App &a, float deltaTime = 0) {
        a.processEvents(); a.update(deltaTime); a.syncRaidPointerCapture();
        a.pendingRelativeAimMotion_ = {}; a.input_.endFrame();
    }
    static Vec2 detailsPoint(App &a) {
        const auto r=a.profileContextMenu_->layout.rows.front().bounds;
        return {r.x+r.w/2,r.y+r.h/2};
    }
    static void capture(App &a) { a.syncRaidPointerCapture(); }
    static bool captured(const App &a) { return a.relativeMouseModeActive_; }
    static Vec2 aim(App &a) {
        return a.gameFlow_.isRaidScreen() ? a.gameSession_.world().weaponAimWorldPosition()
                                         : a.gameFlow_.baseWorld().weaponAimWorldPosition();
    }
    static Vec2 screenAim(App &a) {
        return raidWorldToScreen(aim(a), a.gameFlow_.isRaidScreen()
            ? a.raidWorldCameraOffset() : a.baseWorldCameraOffset());
    }
    static bool inventory(const App &a) { return a.inventoryOverlayState_.isOpen(); }
    static bool pause(const App &a) { return a.pauseMenu_.isOpen(); }
    static bool settings(const App &a) { return a.pauseMenu_.settingsOpen(); }
    static bool details(const App &a) { return a.profileDetailsAsset_.has_value(); }
    static bool firePressed(const App &a) { return a.makeGameplayInput().firePressed; }
    static void enterRange(App &a) {
        a.developerWeaponPanelOpen_ = true;
        a.handleDeveloperPanelClick({680, 120});
    }
    static bool rangePanel(const App &a) { return a.developerRangePanelOpen_; }
    static auto sceneBox(const App &a) { return a.openedRaidSceneContainerId_; }
};

namespace {
using A = PointerContinuityUiTestAccess;
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = code; e.key.down = true;
    ASSERT_TRUE(SDL_PushEvent(&e));
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; ASSERT_TRUE(SDL_PushEvent(&e));
}
void relativeMotion(Vec2 delta) {
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 640; e.motion.y = 360; e.motion.xrel = delta.x; e.motion.yrel = delta.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void click(Vec2 p, Uint8 button = SDL_BUTTON_LEFT, bool release = true) {
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = button;
    e.button.x = p.x; e.button.y = p.y; ASSERT_TRUE(SDL_PushEvent(&e));
    if (release) { e.type = SDL_EVENT_MOUSE_BUTTON_UP; ASSERT_TRUE(SDL_PushEvent(&e)); }
}
void expectPoint(Vec2 actual, Vec2 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.01F); EXPECT_NEAR(actual.y, expected.y, 0.01F);
}
class PointerContinuityUiTest : public testing::Test {
protected:
    std::unique_ptr<App> app;
    SDL_Window *window{};
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-pointer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
        window = SDL_CreateWindow("headless pointer regression", 1280, 720, SDL_WINDOW_HIDDEN);
        ASSERT_NE(window, nullptr);
        const auto &content = publishedContentRegistry();
        auto profile = makeNewPublishedProfile("pointer-test", content);
        AssetInstanceId weapon{};
        for (const auto &[id, asset] : profile.assets.records())
            if (asset.definitionId == ItemDefinitionId{"item.weapon.rifle_5_45_service"}) weapon = id;
        ASSERT_NE(weapon, 0U);
        ASSERT_TRUE(executeInventory(profile, content,
            InventoryEquipCommand{weapon, EquipmentSlotKind::PrimaryWeapon},
            {profile.revision, "pointer-equip"}).succeeded);
        ASSERT_TRUE(SaveRepository{path}.save(profile, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::attach(*app, window);
        A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app); ASSERT_TRUE(A::captured(*app));
        relativeMotion({137, -63}); A::frame(*app);
    }
    void TearDown() override {
        if (app) A::detach(*app); app.reset(); SDL_DestroyWindow(window); SDL_Quit();
        std::error_code ec; std::filesystem::remove_all(path, ec);
    }
    Vec2 cursor() { Vec2 p{}; SDL_GetMouseState(&p.x, &p.y); return p; }
    void moveUi(Vec2 p) { SDL_WarpMouseInWindow(window, p.x, p.y); A::frame(*app); }
};
TEST_F(PointerContinuityUiTest, InventoryReleaseStartsAtReticleAndUiMotionDoesNotChangeAim) {
    const auto before = A::aim(*app); const auto screen = A::screenAim(*app);
    key(SDL_SCANCODE_TAB); A::frame(*app);
    ASSERT_TRUE(A::inventory(*app)); ASSERT_FALSE(A::captured(*app));
    expectPoint(cursor(), {std::clamp(screen.x, 0.0F, 1279.0F), std::clamp(screen.y, 0.0F, 719.0F)});
    moveUi({313, 617}); expectPoint(A::aim(*app), before);
    key(SDL_SCANCODE_TAB); A::frame(*app); ASSERT_TRUE(A::captured(*app));
    A::frame(*app); expectPoint(A::aim(*app), before);
    relativeMotion({-17, 11}); A::frame(*app);
    expectPoint(A::aim(*app), {before.x - 17, before.y + 11});
}
TEST_F(PointerContinuityUiTest, PauseAndSettingsReturnKeepUiPositionAndResumeWithoutAimJump) {
    const auto before = A::aim(*app); const auto screen = A::screenAim(*app);
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); ASSERT_TRUE(A::pause(*app));
    expectPoint(cursor(), {std::clamp(screen.x, 0.0F, 1279.0F), std::clamp(screen.y, 0.0F, 719.0F)});
    moveUi({610, 350}); click({610, 350}); A::frame(*app); ASSERT_TRUE(A::settings(*app));
    expectPoint(cursor(), {610, 350});
    moveUi({940, 231}); key(SDL_SCANCODE_ESCAPE); A::frame(*app);
    ASSERT_FALSE(A::settings(*app)); ASSERT_TRUE(A::pause(*app)); expectPoint(cursor(), {940, 231});
    key(SDL_SCANCODE_ESCAPE); A::frame(*app);
    ASSERT_FALSE(A::pause(*app)); ASSERT_TRUE(A::captured(*app));
    A::frame(*app); expectPoint(A::aim(*app), before);
}
TEST_F(PointerContinuityUiTest, RaidInventoryDoesNotConsumeUiMotionOnClosingFrame) {
    ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
    A::frame(*app); relativeMotion({-41, 73}); A::frame(*app);
    const auto before = A::aim(*app);
    key(SDL_SCANCODE_TAB); A::frame(*app); ASSERT_TRUE(A::inventory(*app));
    moveUi({980, 501}); expectPoint(A::aim(*app), before);
    key(SDL_SCANCODE_TAB); A::frame(*app); A::frame(*app);
    expectPoint(A::aim(*app), before);
}
TEST_F(PointerContinuityUiTest, DetailsAndBackKeepUiCursorUntilReturningToWorld) {
    const auto before = A::aim(*app);
    key(SDL_SCANCODE_TAB); A::frame(*app); ASSERT_TRUE(A::inventory(*app));
    moveUi({240, 150}); click({240, 150}, SDL_BUTTON_RIGHT); A::frame(*app);
    expectPoint(cursor(), {240, 150});
    const auto target=A::detailsPoint(*app);
    moveUi(target); click(target); A::frame(*app); ASSERT_TRUE(A::details(*app));
    expectPoint(cursor(), target);
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); ASSERT_FALSE(A::details(*app));
    ASSERT_TRUE(A::inventory(*app)); expectPoint(cursor(), target);
    key(SDL_SCANCODE_TAB); A::frame(*app); A::frame(*app); expectPoint(A::aim(*app), before);
}
TEST_F(PointerContinuityUiTest, FocusReturnDoesNotFeedDesktopCursorIntoAim) {
    const auto before = A::aim(*app);
    SDL_Event e{}; e.type = SDL_EVENT_WINDOW_FOCUS_LOST; ASSERT_TRUE(SDL_PushEvent(&e));
    A::frame(*app); ASSERT_TRUE(A::pause(*app)); ASSERT_FALSE(A::captured(*app));
    moveUi({1010, 615});
    e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); A::frame(*app);
    ASSERT_TRUE(A::captured(*app)); expectPoint(A::aim(*app), before);
}
TEST_F(PointerContinuityUiTest, HeldResumeClickCannotLeakIntoFireAfterRecapture) {
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); ASSERT_TRUE(A::pause(*app));
    moveUi({610, 300}); click({610, 300}, SDL_BUTTON_LEFT, false); A::frame(*app);
    ASSERT_FALSE(A::pause(*app)); ASSERT_TRUE(A::captured(*app));
    EXPECT_FALSE(A::firePressed(*app)); A::frame(*app); EXPECT_FALSE(A::firePressed(*app));
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.button = SDL_BUTTON_LEFT;
    ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    click({610, 300}, SDL_BUTTON_LEFT, false); A::frame(*app); EXPECT_TRUE(A::firePressed(*app));
}
TEST_F(PointerContinuityUiTest, TestRangeF8PanelUsesSameCursorAndAimContinuity) {
    A::enterRange(*app); A::frame(*app); ASSERT_TRUE(A::rangePanel(*app));
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); A::frame(*app);
    relativeMotion({111, 97}); A::frame(*app); const auto before = A::aim(*app);
    const auto screen = A::screenAim(*app);
    key(SDL_SCANCODE_F8); A::frame(*app); ASSERT_TRUE(A::rangePanel(*app));
    expectPoint(cursor(), {std::clamp(screen.x, 0.0F, 1279.0F), std::clamp(screen.y, 0.0F, 719.0F)});
    moveUi({960, 390}); key(SDL_SCANCODE_ESCAPE); A::frame(*app); A::frame(*app);
    expectPoint(A::aim(*app), before);
}
TEST_F(PointerContinuityUiTest, RealBuildingAndSceneContainerEntryKeepAimAndCursor) {
    ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
    A::frame(*app);
    auto &session = A::flow(*app).gameSession();
    const auto interior = session.profile().pendingRaid->interiors.front();
    const auto position = [&](Vec2 c) {
        auto &p = const_cast<Player &>(session.world().player());
        ASSERT_TRUE(p.setPosition({c.x-p.size()*0.5F, c.y-p.size()*0.5F}));
        A::frame(*app);
        const auto screen = A::screenAim(*app);
        relativeMotion({935-screen.x, 245-screen.y}); A::frame(*app);
    };
    position({interior.exteriorEntrance.position.x + interior.exteriorEntrance.size.x*0.5F,
              interior.exteriorEntrance.position.y + interior.exteriorEntrance.size.y*0.5F});
    const auto entranceScreen = A::screenAim(*app);
    key(SDL_SCANCODE_F); A::frame(*app);
    ASSERT_EQ(session.world().activeRaidSpaceId(), interior.id);
    expectPoint(A::screenAim(*app), entranceScreen);
    A::frame(*app); expectPoint(A::screenAim(*app), entranceScreen);
    AssetInstanceId box{}; Vec2 boxCenter{};
    for (const auto &[id, asset] : session.profile().assets.records()) {
        const auto *location = std::get_if<RaidSceneContainerAssetLocation>(&asset.location);
        if (location && location->spaceId == interior.id) { box=id; boxCenter=location->position; }
    }
    ASSERT_NE(box, 0U); position(boxCenter);
    const auto before = A::aim(*app); const auto screen = A::screenAim(*app);
    key(SDL_SCANCODE_F); A::frame(*app); ASSERT_EQ(A::sceneBox(*app), box);
    ASSERT_FALSE(A::captured(*app));
    expectPoint(cursor(), {std::clamp(screen.x,0.0F,1279.0F), std::clamp(screen.y,0.0F,719.0F)});
    moveUi({1015, 440}); key(SDL_SCANCODE_ESCAPE); A::frame(*app); A::frame(*app);
    expectPoint(A::aim(*app), before);
    position({interior.interiorExit.position.x + interior.interiorExit.size.x*0.5F,
              interior.interiorExit.position.y + interior.interiorExit.size.y*0.5F});
    const auto exitScreen = A::screenAim(*app);
    key(SDL_SCANCODE_F); A::frame(*app); ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    expectPoint(A::screenAim(*app), exitScreen);
    A::frame(*app); expectPoint(A::screenAim(*app), exitScreen);
}
TEST_F(PointerContinuityUiTest, BaseReticleStaysInsideWindowAndReversesImmediately) {
    for (const auto delta : {Vec2{-10000,-10000}, Vec2{10000,-10000},
                             Vec2{10000,10000}, Vec2{-10000,10000}}) {
        relativeMotion(delta); A::frame(*app);
        const auto edge = A::screenAim(*app);
        EXPECT_GE(edge.x,0); EXPECT_LE(edge.x,1279);
        EXPECT_GE(edge.y,0); EXPECT_LE(edge.y,719);
        const Vec2 reverse{delta.x > 0 ? -7.0F : 7.0F, delta.y > 0 ? -9.0F : 9.0F};
        relativeMotion(reverse); A::frame(*app);
        expectPoint(A::screenAim(*app), {edge.x+reverse.x,edge.y+reverse.y});
    }
}
TEST_F(PointerContinuityUiTest, RaidReticleStaysInsideWindowAndReversesImmediately) {
    ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
    A::frame(*app);
    for (const auto delta : {Vec2{-10000,-10000}, Vec2{10000,-10000},
                             Vec2{10000,10000}, Vec2{-10000,10000}}) {
        relativeMotion(delta); A::frame(*app);
        const auto edge = A::screenAim(*app);
        EXPECT_GE(edge.x,0); EXPECT_LE(edge.x,1279);
        EXPECT_GE(edge.y,0); EXPECT_LE(edge.y,719);
        const Vec2 reverse{delta.x > 0 ? -7.0F : 7.0F, delta.y > 0 ? -9.0F : 9.0F};
        relativeMotion(reverse); A::frame(*app);
        expectPoint(A::screenAim(*app), {edge.x+reverse.x,edge.y+reverse.y});
    }
}
TEST_F(PointerContinuityUiTest, ScrollingRaidCameraKeepsReticleAtWindowEdgeInSameFrame) {
    ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
    auto &world=A::flow(*app).gameSession().world();
    auto &player=const_cast<Player &>(world.player());
    const auto size=world.raidSpaceWorldSize();
    ASSERT_TRUE(player.setPosition({size.x*0.5F,size.y*0.5F})); A::frame(*app);
    relativeMotion({10000,-10000}); A::frame(*app); expectPoint(A::screenAim(*app),{1279,0});
    const auto playerBefore=player.position();
    SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_W; event.key.down=true;
    ASSERT_TRUE(SDL_PushEvent(&event)); A::frame(*app,0.03F);
    EXPECT_LT(player.position().y,playerBefore.y);
    expectPoint(A::screenAim(*app),{1279,0});
    event.type=SDL_EVENT_KEY_UP; event.key.down=false; ASSERT_TRUE(SDL_PushEvent(&event)); A::frame(*app);
    expectPoint(A::screenAim(*app),{1279,0});
}
Vec2 simulationCenter(const GameplayWorld &world) {
    const auto p = world.player().position(); const auto half = world.player().size()*0.5F;
    return {p.x+half,p.y+half};
}
Vec2 simulationCamera(const GameplayWorld &world) {
    return raidCameraOffset(simulationCenter(world),world.raidSpaceWorldSize(),{1280,720});
}
Vec2 simulationScreenAim(const GameplayWorld &world) {
    return raidWorldToScreen(world.weaponAimWorldPosition(),simulationCamera(world));
}
GameplayInput windowInput(const GameplayWorld &world, Vec2 anchor = {640,360}) {
    GameplayInput input;
    const auto camera = simulationCamera(world);
    input.aimViewport = Rect{camera,{1280,720}};
    input.aimWorldPosition = raidScreenToWorld(anchor,camera);
    input.aimWorldBounds = raidReticleWorldBounds(camera,{1280,720});
    input.aimMotionDelta = Vec2{};
    return input;
}
TEST(PointerContinuitySimulationTest, EnterExitAndFirstFrameKeepWindowAim) {
    RaidWorldConfig config; config.worldSize = {1280,720}; config.playerSpawn = {90,90};
    config.extractionPoint = {{1100,560},{100,100}};
    config.initialEnemies.clear();
    RaidInteriorWorldConfig interior; interior.id = RaidSpaceDefinitionId{"raid_space.test.pointer"};
    interior.displayName = "Pointer Test"; interior.worldSize = {1280,720};
    interior.exteriorEntrance = {{80,80},{160,160}}; interior.exteriorReturn = {92,92};
    interior.interiorSpawn = {120,120}; interior.interiorExit = {{100,100},{160,160}};
    config.interiors.push_back(interior); GameplayWorld world{std::move(config)};
    auto aim = windowInput(world,{335,195});
    world.update(aim,0);
    const auto screen = simulationScreenAim(world);
    GameplayInput enter = aim; enter.interactJustPressed = true; enter.aimMotionDelta = Vec2{9000,-6000};
    world.update(enter,0); ASSERT_TRUE(world.spaceTransitionedLastUpdate());
    expectPoint(simulationScreenAim(world),screen);
    aim = windowInput(world,{335,195}); world.update(aim,0);
    expectPoint(simulationScreenAim(world),screen);
    GameplayInput exit=aim; exit.interactJustPressed=true; world.update(exit,0);
    ASSERT_TRUE(world.inOutdoorRaidSpace()); expectPoint(simulationScreenAim(world),screen);
    aim=windowInput(world,{335,195}); world.update(aim,0); expectPoint(simulationScreenAim(world),screen);
}
TEST(PointerContinuitySimulationTest, SmallerSpaceKeepsWindowAimAndConsumesFirstMotionImmediately) {
    RaidWorldConfig config; config.worldSize={1280,720}; config.playerSpawn={90,90};
    config.extractionPoint={{1100,560},{100,100}}; config.initialEnemies.clear();
    RaidInteriorWorldConfig interior; interior.id=RaidSpaceDefinitionId{"raid_space.test.small_pointer"};
    interior.displayName="Small Pointer Test"; interior.worldSize={480,360};
    interior.exteriorEntrance={{80,80},{160,160}}; interior.exteriorReturn={92,92};
    interior.interiorSpawn={60,60}; interior.interiorExit={{50,50},{100,100}};
    config.interiors.push_back(interior); GameplayWorld world{std::move(config)};
    auto input = windowInput(world,{1100,600});
    world.update(input,0); input.interactJustPressed=true; world.update(input,0);
    ASSERT_TRUE(world.spaceTransitionedLastUpdate()); expectPoint(simulationScreenAim(world), {1100,600});
    input=windowInput(world,{1100,600}); world.update(input,0);
    expectPoint(simulationScreenAim(world), {1100,600});
    input.aimMotionDelta=Vec2{-10,-20}; world.update(input,0);
    expectPoint(simulationScreenAim(world), {1090,580});
}
TEST(PointerContinuitySimulationTest, CameraEdgesMovementAndRepeatedPortalsKeepWindowAim) {
    for (const auto entrance : {Vec2{100,100}, Vec2{2450,100}, Vec2{100,1350},
                                Vec2{2450,1350}, Vec2{1280,720}}) {
        SCOPED_TRACE(::testing::Message() << "entrance " << entrance.x << ',' << entrance.y);
        RaidWorldConfig config; config.worldSize={2560,1440};
        config.playerSpawn={entrance.x-20,entrance.y-20};
        config.extractionPoint={{1800,1000},{100,100}}; config.initialEnemies.clear();
        RaidInteriorWorldConfig interior; interior.id=RaidSpaceDefinitionId{"raid_space.test.window_edges"};
        interior.displayName="Window Edge Test"; interior.worldSize={2400,1440};
        interior.exteriorEntrance={{entrance.x-45,entrance.y-45},{90,90}};
        interior.exteriorReturn=config.playerSpawn; interior.interiorSpawn={1100,700};
        interior.interiorExit={{1000,600},{300,300}}; config.interiors.push_back(interior);
        GameplayWorld world{std::move(config)};
        world.update(windowInput(world),0);
        auto point = windowInput(world); point.aimMotionDelta=Vec2{270,-130}; world.update(point,0);
        const auto screen = simulationScreenAim(world);
        for (int trip=0; trip<3; ++trip) {
            auto enter=windowInput(world); enter.interactJustPressed=true;
            world.update(enter,0); ASSERT_FALSE(world.inOutdoorRaidSpace());
            expectPoint(simulationScreenAim(world),screen);
            auto move=windowInput(world); move.moveRight=true; world.update(move,0.03F);
            expectPoint(simulationScreenAim(world),screen);
            auto exit=windowInput(world); exit.interactJustPressed=true; world.update(exit,0);
            ASSERT_TRUE(world.inOutdoorRaidSpace()); expectPoint(simulationScreenAim(world),screen);
            auto still=windowInput(world); world.update(still,0);
            expectPoint(simulationScreenAim(world),screen);
        }
    }
}
TEST(PointerContinuitySimulationTest, PortalFrameFiresAtActualWindowReticleWithinMapBoundary) {
    RaidWorldConfig config; config.worldSize={2560,1440}; config.playerSpawn={1900,1100};
    config.extractionPoint={{100,100},{100,100}}; config.initialEnemies.clear();
    RaidInteriorWorldConfig interior; interior.id=RaidSpaceDefinitionId{"raid_space.test.window_shot"};
    interior.displayName="Window Shot Test"; interior.worldSize={480,360};
    interior.exteriorEntrance={{1850,1050},{200,200}}; interior.exteriorReturn=config.playerSpawn;
    interior.interiorSpawn={100,100}; interior.interiorExit={{50,50},{200,200}};
    config.interiors.push_back(interior); GameplayWorld world{std::move(config)};
    const auto definition=*publishedContentRegistry().item(ItemDefinitionId{"item.weapon.rifle_5_45_service"}).weaponUse;
    auto handling=deriveWeaponHandling(definition);
    handling.minimumSpreadDegrees=0; handling.maximumSpreadDegrees=0;
    handling.spreadPerShotDegrees=0;
    world.configureWeaponFire(definition,handling,false);
    world.update(windowInput(world,{1100,600}),0);
    auto enter=windowInput(world,{1100,600}); enter.interactJustPressed=true; enter.fireJustPressed=true;
    world.update(enter,0); ASSERT_TRUE(world.spaceTransitionedLastUpdate());
    expectPoint(simulationScreenAim(world),{1100,600});
    const auto center=simulationCenter(world); const auto aim=world.weaponAimWorldPosition();
    const Vec2 vector{aim.x-center.x,aim.y-center.y}; const auto magnitude=std::hypot(vector.x,vector.y);
    ASSERT_EQ(world.logicalBallistics().size(),1U);
    expectPoint(world.logicalBallistics().front().direction(),{vector.x/magnitude,vector.y/magnitude});
    const auto impact=world.logicalBallistics().front().impactPosition();
    EXPECT_GE(impact.x,0); EXPECT_LE(impact.x,480); EXPECT_GE(impact.y,0); EXPECT_LE(impact.y,360);
}
TEST(PointerContinuitySimulationTest, BaseCameraMovementKeepsWindowAnchorInSameFrame) {
    BaseWorld world;
    const Vec2 viewport{200,120};
    const auto camera = [&] {
        const auto position=world.playerPosition(); const auto size=world.playerSize();
        return raidCameraOffset({position.x+size.x*0.5F,position.y+size.y*0.5F},world.worldSize(),viewport);
    };
    const auto inputAtWindow = [&] {
        GameplayInput input;
        input.aimViewport=Rect{camera(),viewport};
        input.aimWorldPosition=raidScreenToWorld({150,10},camera());
        input.aimWorldBounds=raidReticleWorldBounds(camera(),viewport);
        input.aimMotionDelta=Vec2{};
        return input;
    };
    static_cast<void>(world.update(inputAtWindow(),0));
    expectPoint(raidWorldToScreen(world.weaponAimWorldPosition(),camera()),{150,10});
    const auto before=camera();
    auto move=inputAtWindow(); move.moveLeft=true; static_cast<void>(world.update(move,0.05F));
    EXPECT_NE(camera().x,before.x);
    expectPoint(raidWorldToScreen(world.weaponAimWorldPosition(),camera()),{150,10});
}
}
