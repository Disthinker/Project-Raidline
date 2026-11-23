#include "app.h"
#include "ui_app_persistence_wait.h"
#include "alpha_content_ids.h"
#include "ui_localization.h"
#include "developer_test_range.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

struct MeleeUiTestAccess {
 static GameFlow &flow(App &a){return a.gameFlow_;}
 static void frame(App &a,float seconds=0){a.windowHasInputFocus_=true;a.processEvents();a.update(seconds);a.input_.endFrame(); ASSERT_TRUE(raidline::test::waitForUiAppPersistence(a.gameSession_,
       [&] { return a.profileUiBusy() || static_cast<bool>(a.pendingInventoryBarrierAction_); },
       [&] { a.windowHasInputFocus_=true;a.processEvents();a.update(0);a.input_.endFrame(); })) << a.gameSession_.persistenceMessage(); }
 static void attach(App &a,SDL_Renderer *r){a.renderer_=r;EXPECT_TRUE(a.uiTextRenderer_.initialize(r));}
 static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
 static void inventory(App &a){a.inventoryOverlayState_.openPlayerInventory();a.renderProfileInventory(true,false);}
 static void detail(App &a,AssetInstanceId id){a.profileDetailsAsset_=id;a.renderProfileWeaponDetails();}
 static void supply(App &a){a.renderBaseSupply();}
 static void range(App &a){ASSERT_TRUE(a.gameFlow_.enterDeveloperRange());a.developerRangePanelOpen_=true;
  const auto supplies=developerRangeSupplies();const auto it=std::find(supplies.begin(),supplies.end(),alpha_content::utilityKnife);ASSERT_NE(it,supplies.end());
  a.developerRangeSelection_[3]=static_cast<std::size_t>(it-supplies.begin());a.renderDeveloperRangePanel();}
 static void map(App &a,bool open){a.tacticalMapOpen_=open;}
 static void paintRaid(App &a) { a.renderRaidScreen(); }
 static std::string meleeLabel(const App &a) { return a.meleeStatusLabel(); }
 static void storeKnife(App &a,AssetInstanceId id){auto &s=a.gameSession_;const auto &c=publishedContentRegistry();
  const auto fit=findFirstProfileFit(s.profile(),c,ProfileContainerId::stash(),c.item(alpha_content::utilityKnife),ItemOrientation::Degrees0);ASSERT_TRUE(fit);
  ASSERT_TRUE(s.executeProfileInventory(InventoryMoveCommand{id,0,{ProfileContainerId::stash(),*fit}},"ui-store-knife").succeeded);}
 static void serviceClick(App &a,MousePosition p){a.handleBaseSupplyEvent({UiBaseSupplyEventKind::Down,{p.x,p.y}});a.handleBaseSupplyEvent({UiBaseSupplyEventKind::Up,{p.x,p.y}});frame(a);}
 static std::optional<AssetInstanceId> serviceSelection(const App &a){return a.supplyUi_.selection ? std::optional{a.supplyUi_.selection->instanceId}:std::nullopt;}
 static void context(App &a){a.handleProfileRightClick({178,540},false);}
 static bool menu(const App &a){return a.profileContextMenu_.has_value();}
 static void firstAction(App &a){ASSERT_TRUE(a.profileContextMenu_);
  const auto r=a.profileContextMenu_->layout.rows.front().bounds;const MousePosition p{r.x+r.w/2,r.y+r.h/2};
  a.handleProfileInventoryUiEvent(InventoryPointerEvent{InventoryPointerEventType::LeftButtonDown,p},false);
  a.handleProfileInventoryUiEvent(InventoryPointerEvent{InventoryPointerEventType::LeftButtonUp,p},false);}
 static std::optional<AssetInstanceId> selected(const App &a){return a.profileDetailsAsset_;}
 static bool modifying(const App &a){return a.inventoryWeaponComponentsOpen_;}
 static void empty(App &a){a.profileDetailsAsset_.reset();a.closeInventory();}
 static void swing(App &a){WorldMeleeRuntime melee;melee.configure(MeleeUseDefinition{},true);EnemyRoster<int> targets;
  melee.beginFrame(0);melee.prepare(true,false,{350,360},{1,0},targets,[](Vec2,Vec2){return true;});a.renderMeleeSwing(melee);}
};
namespace {
using A=MeleeUiTestAccess;
class MeleeInventoryUiTest:public testing::Test {
protected:
 void key(SDL_Scancode code, bool down) {
     SDL_Event event{};
     event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
     event.key.scancode = code;
     event.key.down = down;
     ASSERT_TRUE(SDL_PushEvent(&event));
 }
 std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};AssetInstanceId id{};
 std::filesystem::path path=std::filesystem::temp_directory_path()/("raidline-melee-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 void SetUp()override{
  ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));const auto &content=publishedContentRegistry();auto p=makeNewPublishedProfile("melee-ui",content);
  p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
  const auto &def=content.item(alpha_content::utilityKnife);const auto fit=findFirstProfileFit(p,content,ProfileContainerId::stash(),def,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
  id=p.assets.create(def,StoredAssetLocation{ProfileContainerId::stash(),*fit});
  ASSERT_TRUE(executeInventory(p,content,InventoryEquipCommand{id,EquipmentSlotKind::QuickMelee},{p.revision,"equip"}).succeeded);
  p.assets.findMutable(id)->currentDurability=9950;
  ASSERT_TRUE(SaveRepository(path).save(p,content.contentVersion()).succeeded);app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::frame(*app);
  surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);A::attach(*app,renderer);
 }
 void TearDown()override{if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);app.reset();SDL_Quit();std::error_code ec;std::filesystem::remove_all(path,ec);}
 void save(const char *name){ASSERT_TRUE(SDL_RenderPresent(renderer));if(const char *dir=std::getenv("RAIDLINE_MELEE_QA_DIR")){std::filesystem::create_directories(dir);EXPECT_TRUE(SDL_SaveBMP(surface,(std::filesystem::path(dir)/name).string().c_str()));}}
};
}
TEST_F(MeleeInventoryUiTest, IndependentSlotRightClickOpensKnifeDetailsWithoutModification){
 A::inventory(*app);save("melee-inventory-zh.bmp");A::context(*app);ASSERT_TRUE(A::menu(*app));A::firstAction(*app);
 EXPECT_EQ(A::selected(*app),id);EXPECT_FALSE(A::modifying(*app));A::detail(*app,id);save("melee-details-zh.bmp");
}
TEST_F(MeleeInventoryUiTest, ChineseSupplyAndReadableSwingHaveNoAssetDependency){
 A::supply(*app);save("melee-supply-zh.bmp");SDL_SetRenderDrawColor(renderer,24,29,30,255);SDL_RenderClear(renderer);A::swing(*app);save("melee-swing.bmp");
 int pixels=0;for(int y=280;y<440;++y)for(int x=330;x<450;++x){Uint8 r{},g{},b{},a{};ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));if(r>150&&g>150)++pixels;}EXPECT_GT(pixels,30);
}
TEST_F(MeleeInventoryUiTest, RangePanelShowsChineseUtilityKnifeChoice){
 A::range(*app);save("melee-range-zh.bmp");
 EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"Utility Knife"),"通用工具刀");
}
TEST_F(MeleeInventoryUiTest, SupplyCanSelectAndRepairNewKnifeBeyondLegacyTwelveRows){
 A::storeKnife(*app,id);ASSERT_GT(assetsInContainer(A::flow(*app).gameSession().profile(),ProfileContainerId::stash()).size(),12U);
 ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Supply));
 A::serviceClick(*app,{460,158});A::serviceClick(*app,{100,210});ASSERT_EQ(A::serviceSelection(*app),id);
 A::supply(*app);save("melee-supply-knife-zh.bmp");const auto currency=A::flow(*app).gameSession().profile().currency;
 A::serviceClick(*app,{1050,570});const auto &p=A::flow(*app).gameSession().profile();
 EXPECT_EQ(p.assets.find(id)->currentDurability,10000U);EXPECT_EQ(p.currency,currency-21U);EXPECT_FALSE(p.gunsmithMaintenanceJob);
}
TEST_F(MeleeInventoryUiTest, BaseMapPreventsVUntilARealReleaseAndNewPress){
 SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_V;event.key.down=true;
 A::map(*app,true);ASSERT_TRUE(SDL_PushEvent(&event));A::frame(*app,.000001F);EXPECT_FALSE(A::flow(*app).baseWorld().melee().result().attempted);
 A::map(*app,false);A::frame(*app,.000001F);EXPECT_FALSE(A::flow(*app).baseWorld().melee().result().attempted);
 event.type=SDL_EVENT_KEY_UP;event.key.down=false;ASSERT_TRUE(SDL_PushEvent(&event));A::frame(*app,.000001F);
 event.type=SDL_EVENT_KEY_DOWN;event.key.down=true;ASSERT_TRUE(SDL_PushEvent(&event));A::frame(*app,.000001F);
 EXPECT_TRUE(A::flow(*app).baseWorld().melee().result().attempted);
}
TEST(MeleeInventoryUiTextTest, VUsesExistingEdgeAndReleaseInputRules){
 InputSystem input;SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_V;e.key.down=true;input.handleEvent(e);
 EXPECT_TRUE(input.wasActionJustPressed(GameAction::QuickMelee));EXPECT_TRUE(input.isActionPressed(GameAction::QuickMelee));input.endFrame();
 e.key.repeat=true;input.handleEvent(e);EXPECT_FALSE(input.wasActionJustPressed(GameAction::QuickMelee));
 e.type=SDL_EVENT_KEY_UP;e.key.down=false;input.handleEvent(e);EXPECT_TRUE(input.wasActionJustReleased(GameAction::QuickMelee));
}

TEST(MeleeInventoryUiTextTest, HeavyChordUsesTheModifiersAtTheMeleeEdge) {
    InputSystem input;
    auto key = [&](SDL_Scancode code, bool down, bool repeat = false) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = code;
        event.key.down = down;
        event.key.repeat = repeat;
        input.handleEvent(event);
    };
    key(SDL_SCANCODE_V, true);
    key(SDL_SCANCODE_LALT, true);
    EXPECT_FALSE(input.heavyMeleeJustPressed());
    EXPECT_FALSE(input.heavyMeleeHeld());
    key(SDL_SCANCODE_V, false);
    input.endFrame();
    key(SDL_SCANCODE_V, true);
    EXPECT_TRUE(input.heavyMeleeJustPressed());
    EXPECT_TRUE(input.heavyMeleeHeld());
    input.endFrame();
    key(SDL_SCANCODE_V, true, true);
    EXPECT_FALSE(input.heavyMeleeJustPressed());
    key(SDL_SCANCODE_RALT, true);
    key(SDL_SCANCODE_LALT, false);
    EXPECT_TRUE(input.heavyMeleeHeld());
    key(SDL_SCANCODE_RALT, false);
    key(SDL_SCANCODE_RALT, true);
    EXPECT_FALSE(input.heavyMeleeHeld());
    key(SDL_SCANCODE_V, false);
    key(SDL_SCANCODE_V, true);
    EXPECT_TRUE(input.heavyMeleeHeld());
    SDL_Event lost{};
    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    input.handleEvent(lost);
    EXPECT_FALSE(input.heavyMeleeHeld());
    EXPECT_FALSE(input.heavyMeleeJustPressed());
    EXPECT_FALSE(input.isActionPressed(GameAction::QuickMelee));
}

TEST_F(MeleeInventoryUiTest, RaidHeavyWindupCancelsOnReleaseWithoutAFallbackQuickStrike) {
    ASSERT_TRUE(A::flow(*app).deploy(kHospitalFloorplanMapId));
    auto &world = A::flow(*app).gameSession().world();
    key(SDL_SCANCODE_LALT, true);
    key(SDL_SCANCODE_V, true);
    A::frame(*app, .05F);
    A::frame(*app, .10F);
    ASSERT_GT(world.melee().heavyChargeProgress(), 0.0F);
    EXPECT_FALSE(world.melee().result().attempted);
    EXPECT_EQ(A::meleeLabel(*app), "重击蓄力");
    A::paintRaid(*app);
    save("heavy-melee-windup-zh.bmp");
    key(SDL_SCANCODE_LALT, false);
    A::frame(*app, .05F);
    EXPECT_FLOAT_EQ(world.melee().heavyChargeProgress(), 0.0F);
    EXPECT_FALSE(world.melee().result().attempted);
    key(SDL_SCANCODE_LALT, true);
    for (int tick = 0; tick < 8; ++tick) {
        A::frame(*app, .1F);
        EXPECT_FALSE(world.melee().result().attempted);
    }
    key(SDL_SCANCODE_V, false);
    key(SDL_SCANCODE_LALT, false);
    A::frame(*app);
    key(SDL_SCANCODE_V, true);
    A::frame(*app);
    EXPECT_TRUE(world.melee().result().attempted);
}

TEST_F(MeleeInventoryUiTest, HoldingAltVStrikesOnceAndOpeningTheMapCancelsANewWindup) {
    ASSERT_TRUE(A::flow(*app).deploy(kHospitalFloorplanMapId));
    const auto &world = A::flow(*app).gameSession().world();
    key(SDL_SCANCODE_RALT, true);
    key(SDL_SCANCODE_V, true);
    int attempts = 0;
    for (int tick = 0; tick < 35; ++tick) {
        A::frame(*app, .05F);
        if (world.melee().result().attempted) {
            ++attempts;
            EXPECT_TRUE(world.melee().heavySwing());
        }
    }
    EXPECT_EQ(attempts, 1);
    key(SDL_SCANCODE_V, false);
    A::frame(*app);
    key(SDL_SCANCODE_V, true);
    A::frame(*app, .1F);
    A::frame(*app, .1F);
    ASSERT_GT(world.melee().heavyChargeProgress(), 0.0F);
    key(SDL_SCANCODE_M, true);
    key(SDL_SCANCODE_M, false);
    A::frame(*app);
    EXPECT_FLOAT_EQ(world.melee().heavyChargeProgress(), 0.0F);
    key(SDL_SCANCODE_M, true);
    key(SDL_SCANCODE_M, false);
    A::frame(*app);
    for (int tick = 0; tick < 12; ++tick) {
        A::frame(*app, .05F);
        EXPECT_FALSE(world.melee().result().attempted);
    }
}
