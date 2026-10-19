#include "app.h"
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
 static void frame(App &a,float seconds=0){a.windowHasInputFocus_=true;a.processEvents();a.update(seconds);a.input_.endFrame();}
 static void attach(App &a,SDL_Renderer *r){a.renderer_=r;EXPECT_TRUE(a.uiTextRenderer_.initialize(r));}
 static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
 static void inventory(App &a){a.inventoryOverlayState_.openPlayerInventory();a.renderProfileInventory(true,false);}
 static void detail(App &a,AssetInstanceId id){a.profileDetailsAsset_=id;a.renderProfileWeaponDetails();}
 static void supply(App &a){a.renderBaseSupply();}
 static void range(App &a){ASSERT_TRUE(a.gameFlow_.enterDeveloperRange());a.developerRangePanelOpen_=true;
  const auto supplies=developerRangeSupplies();const auto it=std::find(supplies.begin(),supplies.end(),alpha_content::utilityKnife);ASSERT_NE(it,supplies.end());
  a.developerRangeSelection_[3]=static_cast<std::size_t>(it-supplies.begin());a.renderDeveloperRangePanel();}
 static void map(App &a,bool open){a.tacticalMapOpen_=open;}
 static void storeKnife(App &a,AssetInstanceId id){auto &s=a.gameSession_;const auto &c=publishedContentRegistry();
  const auto fit=findFirstProfileFit(s.profile(),c,ProfileContainerId::stash(),c.item(alpha_content::utilityKnife),ItemOrientation::Degrees0);ASSERT_TRUE(fit);
  ASSERT_TRUE(s.executeProfileInventory(InventoryMoveCommand{id,0,{ProfileContainerId::stash(),*fit}},"ui-store-knife").succeeded);}
 static void serviceClick(App &a,MousePosition p){a.handleBasePointerClick({p,false});}
 static std::optional<AssetInstanceId> serviceSelection(const App &a){return a.profileAssetSelection_ ? std::optional{a.profileAssetSelection_->instanceId}:std::nullopt;}
 static void context(App &a){a.handleProfileRightClick({450,540},false);}
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
 A::serviceClick(*app,{680,175});ASSERT_EQ(A::serviceSelection(*app),id);
 A::supply(*app);save("melee-supply-knife-zh.bmp");const auto currency=A::flow(*app).gameSession().profile().currency;
 A::serviceClick(*app,{950,575});const auto &p=A::flow(*app).gameSession().profile();
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
