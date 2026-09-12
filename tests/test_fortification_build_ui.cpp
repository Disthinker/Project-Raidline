#include "app.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>

// Real event/update path, without a window, renderer, audio device or user save.
struct FortificationBuildUiTestAccess {
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static auto &ui(App &a) { return a.fortificationBuildUi_; }
    static bool open(App &a) { return a.baseConstructionPanelOpen_; }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static Vec2 focus(App &a, Vec2 world, std::size_t zoom) {
        a.baseConstructionZoomIndex_ = zoom;
        a.baseBuildCamera_.activate(world, a.gameFlow_.baseWorld().worldSize(), a.baseBuildViewportWorldSize());
        return baseBuildWorldToScreen(world, a.baseWorldCameraOffset(), a.baseConstructionZoom());
    }
};
struct EnemyLifecycleTestAccess {
    static void place(BaseWorld &w, Vec2 p) { w.playerPosition_=p; }
    static void spawn(BaseWorld &w, Vec2 p) { w.perimeterEnemies_.spawn(Enemy{p,{40,52},{},100,912},p); }
};
namespace {
using A = FortificationBuildUiTestAccess;
namespace ui = fortification_ui;
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=code; e.key.down=true;
    ASSERT_TRUE(SDL_PushEvent(&e)); e.type=SDL_EVENT_KEY_UP; e.key.down=false; ASSERT_TRUE(SDL_PushEvent(&e));
}
void pointer(Vec2 p) {
    SDL_Event e{}; e.type=SDL_EVENT_MOUSE_MOTION; e.motion.x=p.x; e.motion.y=p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void click(Vec2 p, Uint8 button=SDL_BUTTON_LEFT) {
    pointer(p);
    SDL_Event e{}; e.type=SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button=button; e.button.x=p.x; e.button.y=p.y;
    ASSERT_TRUE(SDL_PushEvent(&e)); e.type=SDL_EVENT_MOUSE_BUTTON_UP; ASSERT_TRUE(SDL_PushEvent(&e));
}
Vec2 center(SDL_FRect r) { return {r.x+r.w/2,r.y+r.h/2}; }
class FortificationBuildUiTest : public testing::Test {
protected:
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-wood-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::unique_ptr<App> app;
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        auto p=makeNewAlphaProfile("wood-ui",publishedContentRegistry());
        p.baseConstruction.materialUnits=100;
        ASSERT_TRUE(SaveRepository{path}.save(p,publishedContentRegistry().contentVersion()).succeeded);
        app=std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); frame();
        key(SDL_SCANCODE_B); frame(); ASSERT_TRUE(A::open(*app));
        tap(ui::category); ASSERT_TRUE(A::ui(*app).category);
    }
    void TearDown() override {
        app.reset(); SDL_Quit(); std::error_code ec; std::filesystem::remove_all(path,ec);
    }
    void frame() { A::frame(*app); }
    void tap(SDL_FRect r, Uint8 button=SDL_BUTTON_LEFT) { click(center(r),button); frame(); }
    GameSession &session() { return A::flow(*app).gameSession(); }
    BaseWorld &world() { return A::flow(*app).baseWorld(); }
    void buy() { tap(ui::purchase); tap(ui::card(0)); ASSERT_EQ(session().profile().baseFortifications.instances.size(),1U); }
    void reloadDamaged(unsigned hp) {
        auto p=session().profile(); p.baseFortifications.instances.at({1}).durability=hp;
        app.reset();
        ASSERT_TRUE(SaveRepository{path}.save(p,publishedContentRegistry().contentVersion()).succeeded);
        app=std::make_unique<App>(); A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); frame();
        key(SDL_SCANCODE_B); frame(); tap(ui::category);
    }
    Vec2 preview(std::size_t zoom=2) {
        tap(ui::card(0)); frame();
        for (const auto &slot:A::ui(*app).slots) {
            auto plan=session().queryBaseFortificationPlacement(world(),{{1},slot.key});
            if (!plan.canCommit) continue;
            const auto r=slot.footprint;
            const auto screen=A::focus(*app,{r.position.x+r.size.x/2,r.position.y+r.size.y/2},zoom);
            pointer(screen); frame(); return screen;
        }
        ADD_FAILURE()<<"No legal fixed position"; return {};
    }
};
TEST_F(FortificationBuildUiTest, PurchaseSameFrameDoesNotDoubleSubmitOrFire) {
    click(center(ui::card(0))); click(center(ui::card(0))); frame();
    EXPECT_EQ(session().profile().baseFortifications.instances.size(),1U);
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,92U);
    EXPECT_FALSE(world().shotFiredLastUpdate());
    EXPECT_FALSE(A::ui(*app).placing);
}
TEST_F(FortificationBuildUiTest, ZoomPanPreviewCacheInstallAndRightClickStore) {
    buy();
    for (std::size_t zoom=0;zoom<5;++zoom) {
        const auto screen=preview(zoom);
        ASSERT_TRUE(A::ui(*app).hover);
        ASSERT_TRUE(A::ui(*app).previews[*A::ui(*app).hover]->canCommit);
        const auto proofs=A::ui(*app).proofCount;
        const auto geometry=world().placementGeometryRevision();
        for(int i=0;i<100;++i) frame();
        EXPECT_EQ(A::ui(*app).proofCount,proofs);
        EXPECT_EQ(world().placementGeometryRevision(),geometry);
        click(screen); frame();
        ASSERT_TRUE(session().profile().baseFortifications.instances.at({1}).slot);
        ASSERT_EQ(world().fortifications().size(),1U);
        click(screen,SDL_BUTTON_RIGHT); frame(); ASSERT_TRUE(A::ui(*app).menu);
        tap(ui::menuRow(A::ui(*app).menuAt,1));
        EXPECT_FALSE(session().profile().baseFortifications.instances.at({1}).slot);
        EXPECT_TRUE(world().fortifications().empty());
    }
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,92U);
}
TEST_F(FortificationBuildUiTest, InvalidDropEscapeAndMenuDismissNeverModifyProfile) {
    buy(); preview(); const auto before=profileStateFingerprint(session().profile());
    click({4,4}); frame(); EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    key(SDL_SCANCODE_ESCAPE); frame(); EXPECT_FALSE(A::ui(*app).placing); EXPECT_TRUE(A::open(*app));
    tap(ui::card(0),SDL_BUTTON_RIGHT); ASSERT_TRUE(A::ui(*app).menu);
    click({4,4}); frame(); EXPECT_FALSE(A::ui(*app).menu);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
}
TEST_F(FortificationBuildUiTest, SaveFailureDoesNotInstallOrChangeCollision) {
    buy(); const auto screen=preview(); const auto before=profileStateFingerprint(session().profile());
    ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));
    click(screen); frame();
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    EXPECT_TRUE(world().fortifications().empty());
    EXPECT_TRUE(A::ui(*app).placing);
}
TEST_F(FortificationBuildUiTest, FocusLossCancelsTransientSelection) {
    buy(); preview(); const auto before=profileStateFingerprint(session().profile());
    SDL_Event e{}; e.type=SDL_EVENT_WINDOW_FOCUS_LOST; ASSERT_TRUE(SDL_PushEvent(&e)); frame();
    EXPECT_FALSE(A::ui(*app).placing);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
}
TEST_F(FortificationBuildUiTest, ChangedActorPositionInvalidatesGreenPreviewAndCommit) {
    buy(); const auto screen=preview();
    const auto before=profileStateFingerprint(session().profile());
    ASSERT_TRUE(A::ui(*app).hover);
    const auto r=A::ui(*app).slots[*A::ui(*app).hover].footprint;
    const auto proofs=A::ui(*app).proofCount;
    EnemyLifecycleTestAccess::place(world(),r.position);
    click(screen); frame();
    EXPECT_EQ(A::ui(*app).proofCount,proofs+1);
    ASSERT_TRUE(A::ui(*app).hover);
    EXPECT_FALSE(A::ui(*app).previews[*A::ui(*app).hover]->canCommit);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    EXPECT_TRUE(world().fortifications().empty());
}
TEST_F(FortificationBuildUiTest, RepairMenuUsesPublicMaterialAndSurvivesReload) {
    buy(); reloadDamaged(61);
    tap(ui::owned); tap(ui::card(0),SDL_BUTTON_RIGHT); ASSERT_TRUE(A::ui(*app).menu);
    tap(ui::menuRow(A::ui(*app).menuAt,2));
    EXPECT_EQ(session().profile().baseFortifications.instances.at({1}).durability,120U);
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,90U);
    const auto saved=SaveRepository{path}.load(publishedContentRegistry()); ASSERT_TRUE(saved.profile);
    EXPECT_EQ(profileStateFingerprint(*saved.profile),profileStateFingerprint(session().profile()));
}
TEST_F(FortificationBuildUiTest, WarningAllowsRepairButNeverPurchaseStoreOrMove) {
    buy(); const auto screen=preview(); click(screen); frame();
    reloadDamaged(61);
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning()); frame();
    key(SDL_SCANCODE_ESCAPE); frame(); // dismiss warning, not resolve it
    key(SDL_SCANCODE_B); frame(); ASSERT_TRUE(A::open(*app)); ASSERT_TRUE(A::ui(*app).category);
    const auto &f=world().fortifications()[0];
    const auto p=A::focus(*app,{f.footprint.position.x+f.footprint.size.x/2,f.footprint.position.y+f.footprint.size.y/2},0);
    const auto before=profileStateFingerprint(session().profile());
    tap(ui::purchase); tap(ui::card(0));
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    for(unsigned row=0;row<2;++row) {
        click(p,SDL_BUTTON_RIGHT); frame(); ASSERT_TRUE(A::ui(*app).menu);
        tap(ui::menuRow(A::ui(*app).menuAt,row));
        EXPECT_EQ(profileStateFingerprint(session().profile()),before);
        EXPECT_FALSE(A::ui(*app).placing);
    }
    click(p,SDL_BUTTON_RIGHT); frame(); ASSERT_TRUE(A::ui(*app).menu);
    tap(ui::menuRow(A::ui(*app).menuAt,2));
    EXPECT_EQ(session().profile().baseFortifications.instances.at({1}).durability,120U);
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,90U);
    EXPECT_TRUE(session().profile().baseSiege.warningActive);
}
TEST_F(FortificationBuildUiTest, BrokenRepairCannotTrapPlayerAndFootprintStaysReserved) {
    buy(); const auto screen=preview(); click(screen); frame(); reloadDamaged(0);
    const auto r=world().fortifications()[0].footprint;
    const auto outside=world().playerPosition();
    EnemyLifecycleTestAccess::place(world(),r.position);
    const auto before=profileStateFingerprint(session().profile());
    EXPECT_FALSE(session().repairBaseFortification(world(),{1}).succeeded);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    bool reserved=false;
    for(const auto &b:world().basePlacementBlockers())
        reserved |= b.position.x==r.position.x && b.position.y==r.position.y && b.size.x==r.size.x && b.size.y==r.size.y;
    EXPECT_TRUE(reserved);
    EnemyLifecycleTestAccess::place(world(),outside);
    EXPECT_TRUE(session().repairBaseFortification(world(),{1}).succeeded);
    EXPECT_EQ(session().profile().baseFortifications.instances.at({1}).durability,120U);
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,88U);
}
TEST_F(FortificationBuildUiTest, MovePreservesStableIdentityDurabilityAndPrice) {
    buy(); const auto screen=preview(); click(screen); frame(); reloadDamaged(61);
    const auto old=*session().profile().baseFortifications.instances.at({1}).slot;
    const auto r=world().fortifications()[0].footprint;
    const auto at=A::focus(*app,{r.position.x+r.size.x/2,r.position.y+r.size.y/2},4);
    click(at,SDL_BUTTON_RIGHT); frame(); ASSERT_TRUE(A::ui(*app).menu);
    tap(ui::menuRow(A::ui(*app).menuAt,0)); frame(); ASSERT_TRUE(A::ui(*app).placing);
    bool moved=false;
    for(const auto &s:A::ui(*app).slots) {
        if(s.key==old || !session().queryBaseFortificationPlacement(world(),{{1},s.key}).canCommit) continue;
        const auto target=A::focus(*app,{s.footprint.position.x+s.footprint.size.x/2,s.footprint.position.y+s.footprint.size.y/2},1);
        click(target); frame(); moved=true; break;
    }
    ASSERT_TRUE(moved);
    const auto &wood=session().profile().baseFortifications.instances.at({1});
    EXPECT_NE(wood.slot,old); EXPECT_EQ(wood.durability,61U);
    EXPECT_EQ(session().profile().baseConstruction.materialUnits,92U);
}
TEST_F(FortificationBuildUiTest, RepairAndInstallNeverEmbedExistingPerimeterEnemy) {
    buy(); const auto screen=preview(); click(screen); frame(); reloadDamaged(0);
    const auto r=world().fortifications()[0].footprint;
    EnemyLifecycleTestAccess::spawn(world(),r.position);
    const auto before=profileStateFingerprint(session().profile());
    EXPECT_FALSE(session().repairBaseFortification(world(),{1}).succeeded);
    EXPECT_FALSE(session().queryBaseFortificationPlacement(world(),{{1},*session().profile().baseFortifications.instances.at({1}).slot}).canCommit);
    EXPECT_EQ(profileStateFingerprint(session().profile()),before);
    EXPECT_EQ(world().perimeterEnemies().back().combatTargetId(),912U);
    EXPECT_EQ(world().perimeterEnemies().back().position().x,r.position.x);
}
TEST(FortificationBuildUiLocalizationTest, CoreOperationsHaveChinese) {
    for (const auto *s:{"SHOW FORTIFICATIONS","SHOW FACILITIES","BUY TO RESERVE","STORE IN RESERVE",
         "WARNING: FORTIFICATION REPAIR ONLY","FIXED DEFENSE POSITIONS ONLY"})
        EXPECT_NE(localizeUiText(UiLanguage::SimplifiedChinese,s),s);
}
}
