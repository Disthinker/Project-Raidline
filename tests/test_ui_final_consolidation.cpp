#include "app.h"
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

// Existing BaseWorld test access is used only to place an isolated fixture at
// a real service entrance or perimeter point. Production interaction queries
// still choose the actual target; no synthetic target is supplied to App.
struct EnemyLifecycleTestAccess {
  static void place(BaseWorld &world, Vec2 center) {
    const auto size=world.playerSize();
    world.playerPosition_={center.x-size.x*.5F,center.y-size.y*.5F};
  }
};

struct UiFinalConsolidationTestAccess {
  static auto &flow(App &a) { return a.gameFlow_; }
  static auto &session(App &a) { return a.gameSession_; }
  static auto &text(App &a) { return a.uiTextRenderer_; }
  static auto &message(App &a) { return a.uiMessage_; }
  static void message(App &a,std::string value) { a.uiMessage_=std::move(value); }
  static auto &scroll(App &a) { return a.finalFeedbackScroll_; }
  static bool settings(App &a) { return a.settingsOpen_; }
  static void settings(App &a, bool value) { a.settingsOpen_=value; }
  static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
  static void events(App &a) { a.processEvents(); a.input_.endFrame(); }
  static auto page(App &a) { return a.baseConstructionPage_; }
  static void revision(App &a) { ++const_cast<ProfileState &>(a.gameSession_.profile()).revision; }
  static void build(App &a) { a.baseConstructionPanelOpen_=true; }
  static void closeBuild(App &a) { a.baseConstructionPanelOpen_=false; }
  static void pause(App &a) { a.pauseMenu_.open(); }
  static void resume(App &a) { a.pauseMenu_.close(); }
  static void overwrite(App &a,bool value) { a.newGameOverwriteArmed_=value; }
  static void abandon(App &a,bool value) { a.baseDefenseAbandonArmed_=value; }
  static void drawMenu(App &a,bool paused) { a.renderFoundationMenu(paused); }
  static void drawBase(App &a) { a.renderBaseDailyHud(); a.renderBaseCombatVitals(); }
  static void drawFullBase(App &a) { a.renderBase(); }
  static void presentationTick(App &a,float delta) { a.updateBasePresentation(delta); }
  static bool baseCombatStatusVisible(App &a) { return a.baseCombatStatusVisible(); }
  static float toastSeconds(App &a) { return a.baseToastSeconds_; }
  static Vec2 baseCamera(App &a) { return a.baseWorldCameraOffset(); }
  static void constructionView(App &a,BaseFacilityKind kind,std::size_t zoom) {
    a.baseConstructionPanelOpen_=true;a.selectedBaseFixedFacility_=kind;
    a.baseConstructionZoomIndex_=zoom;a.focusBaseFixedFacility(kind);
  }
  static void closeConstructionView(App &a) {
    a.baseConstructionPanelOpen_=false;a.selectedBaseFixedFacility_.reset();a.deactivateBaseBuildCamera();
  }
  static bool mapOpen(App &a) { return a.tacticalMapOpen_; }
  static void toggleBaseMap(App &a) {
    SDL_Event event{};event.type=SDL_EVENT_KEY_DOWN;event.key.scancode=SDL_SCANCODE_M;
    a.input_.handleEvent(event);a.updateBase(0);a.input_.endFrame();
    event.type=SDL_EVENT_KEY_UP;a.input_.handleEvent(event);a.input_.endFrame();
  }
  static void drawSupply(App &a) { a.renderBaseSupply(); }
  static void drawKitchen(App &a) { a.renderBaseKitchenWater(); }
  static void drawWorldHints(App &a) { a.renderExtractionPoint(); }
  static void drawRaid(App &a) { a.renderRaidScreen(); }
  static void loadArt(App &a) { ASSERT_TRUE(a.loadTextures()); }
  static Vec2 raidCamera(App &a) { return a.raidWorldCameraOffset(); }
  static SDL_FRect labelBounds(App &a, float x, float y, std::string_view label) { return a.worldLabelBounds(x,y,label); }
  static void drawFounding(App &a) { a.homeFoundingPrompt_=std::string{homePlotDefinitions().front().id}; a.renderBase(); a.homeFoundingPrompt_.reset(); }
  static void kitchenClick(App &a) { a.handleBasePointerClick({{580,560},false,false}); }
  static bool fire(App &a) { return a.makeBaseGameplayInput().firePressed; }
  static void attach(App &a, SDL_Renderer *r, float scale,const std::filesystem::path &path) {
    a.renderer_=r; a.settingsPath_=path; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
  }
  static void detach(App &a) {
    a.indoorInformationLayer_.reset();a.indoorShadowMask_.reset();
    a.backgroundTexture_.reset();a.playerTexture_.reset();a.playerMoveHorizontalTexture_.reset();a.enemyMoveHorizontalTexture_.reset();
    for(auto &texture:a.worldItemTextures_)texture.reset();for(auto &texture:a.inventoryItemTextures_)texture.reset();
    a.uiTextRenderer_.shutdown(); a.renderer_=nullptr;
  }
};
namespace {
using A=UiFinalConsolidationTestAccess;
void pointer(Vec2 p,bool down) {
  SDL_Event e{}; e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
  e.button.button=SDL_BUTTON_LEFT; e.button.x=p.x; e.button.y=p.y; ASSERT_TRUE(SDL_PushEvent(&e));
}
void event(Uint32 type) { SDL_Event e{}; e.type=type; ASSERT_TRUE(SDL_PushEvent(&e)); }
class UiFinalConsolidationTest: public testing::Test {
protected:
  std::unique_ptr<App> app; SDL_Surface *surface{}; SDL_Renderer *renderer{}; std::filesystem::path path;
  void SetUp() override {
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
    path=std::filesystem::temp_directory_path()/("raidline-final-all-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ASSERT_TRUE(std::filesystem::create_directory(path)); app=std::make_unique<App>();
    A::flow(*app).configurePersistence(path/"profile"); attach(1280,720);
  }
  void attach(int w,int h) {
    surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface,nullptr);
    renderer=SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer,nullptr);
    ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    A::attach(*app,renderer,w/1280.0F,path/"settings.json");
  }
  void detach() { A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); renderer=nullptr; surface=nullptr; }
  void TearDown() override {
    detach(); app.reset(); SDL_Quit();
    const auto resolved=std::filesystem::canonical(path);
    ASSERT_TRUE(std::filesystem::equivalent(resolved.parent_path(),std::filesystem::temp_directory_path()));
    ASSERT_TRUE(resolved.filename().string().starts_with("raidline-final-all-"));
    std::error_code ec; std::filesystem::remove_all(resolved,ec);
  }
  void frame() { A::frame(*app); }
  void base() { ASSERT_TRUE(A::flow(*app).startNewGame("final-all-isolated",false)); frame(); }
  void establishedBase() {
    const auto &content=publishedContentRegistry();
    auto profile=makeNewAlphaProfile("final-declutter-isolated",content);
    profile.tutorial=TutorialProgress::Complete;profile.homeFounding.hintsDismissed=true;
    ASSERT_TRUE(SaveRepository{path/"profile"}.save(profile,content.contentVersion()).succeeded);
    ASSERT_TRUE(A::flow(*app).continueGame());
    A::message(*app,"");A::presentationTick(*app,0);
  }
  void wheel(Vec2 p,float delta) {
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.mouse_x=p.x;e.wheel.mouse_y=p.y;e.wheel.y=delta;
    ASSERT_TRUE(SDL_PushEvent(&e));
  }
  void clear() { SDL_SetRenderDrawColor(renderer,5,10,12,255); ASSERT_TRUE(SDL_RenderClear(renderer)); }
  std::size_t bright(SDL_FRect b,float scale=1) {
    std::size_t n{};
    for(int y=static_cast<int>(b.y*scale); y<static_cast<int>((b.y+b.h)*scale);++y)
      for(int x=static_cast<int>(b.x*scale); x<static_cast<int>((b.x+b.w)*scale);++x) {
        Uint8 r{},g{},bl{},al{}; if(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&bl,&al)&&r>145&&g>145&&bl>140)++n;
      }
    return n;
  }
  void save(const std::string &name) {
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    if(const auto *out=std::getenv("RAIDLINE_UI_FINAL_MENU_REVIEW_DIR")) {
      std::filesystem::create_directories(out);
      ASSERT_TRUE(IMG_SavePNG(surface,(std::filesystem::path(out)/(name+".png")).string().c_str()));
    }
  }
  std::vector<bool> glyphs(SDL_FRect box,float scale) {
    std::vector<bool> mask;
    for(int y=static_cast<int>(std::floor(box.y*scale));y<static_cast<int>(std::ceil((box.y+box.h)*scale));++y)
      for(int x=static_cast<int>(std::floor(box.x*scale));x<static_cast<int>(std::ceil((box.x+box.w)*scale));++x) {
        Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
        mask.push_back(r>180&&g>190&&b>185);
      }
    return mask;
  }
  std::vector<Uint32> pixels(SDL_FRect box,float scale) {
    std::vector<Uint32> result;
    for(int y=static_cast<int>(std::floor(box.y*scale));y<static_cast<int>(std::ceil((box.y+box.h)*scale));++y)
      for(int x=static_cast<int>(std::floor(box.x*scale));x<static_cast<int>(std::ceil((box.x+box.w)*scale));++x) {
        Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
        result.push_back((static_cast<Uint32>(r)<<24)|(static_cast<Uint32>(g)<<16)|(static_cast<Uint32>(b)<<8)|a);
      }
    return result;
  }
  void expectCompleteText(SDL_FRect box,std::string_view expected,int font,float scale,UiTextAlign align=UiTextAlign::Left) {
    ASSERT_TRUE(SDL_RenderPresent(renderer));const auto actual=glyphs(box,scale);
    clear();SDL_SetRenderDrawColor(renderer,222,231,226,255);
    A::text(*app).renderBox(renderer,box,expected,{font,align,1});
    ASSERT_TRUE(SDL_RenderPresent(renderer));const auto reference=glyphs(box,scale);
    ASSERT_EQ(actual.size(),reference.size());EXPECT_GT(std::count(reference.begin(),reference.end(),true),12);
    const int width=static_cast<int>(std::ceil((box.x+box.w)*scale))-static_cast<int>(std::floor(box.x*scale));
    const int height=static_cast<int>(reference.size())/width;std::size_t missing{};
    for(std::size_t i=0;i<reference.size();++i)if(reference[i]) {
      const int x=static_cast<int>(i)%width,y=static_cast<int>(i)/width;bool present{};
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if(x+dx>=0&&x+dx<width&&y+dy>=0&&y+dy<height)
          present=present||actual[static_cast<std::size_t>((y+dy)*width+x+dx)];
      if(!present)++missing;
    }
    EXPECT_EQ(missing,0U)<<"independent complete-text reference: "<<expected;
  }
  std::size_t oldPanelEdge(float x,float y,float width,float scale) {
    std::size_t count{};
    for(int px=static_cast<int>(std::ceil((x+4)*scale));px<static_cast<int>(std::floor((x+width-4)*scale));++px) {
      bool edge{};
      for(int dy=-1;dy<=1;++dy) {
        Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,px,static_cast<int>(std::round(y*scale))+dy,&r,&g,&b,&a));
        edge=edge||(r==77&&g==112&&b==108)||(r==199&&g==137&&b==101);
      }
      if(edge)++count;
    }
    return count;
  }
  void expectNoOldBasePanels(float scale) {
    // Independent coordinates of the rejected candidate's persistent panels,
    // including the completed tutorial banner, not production layout helpers.
    EXPECT_LT(oldPanelEdge(24,532,650,scale),25U);
    EXPECT_LT(oldPanelEdge(704,546,552,scale),25U);
    EXPECT_LT(oldPanelEdge(24,594,650,scale),25U);
    EXPECT_LT(oldPanelEdge(24,658,650,scale),25U);
    EXPECT_LT(oldPanelEdge(330,22,620,scale),25U);
  }
};
}

TEST_F(UiFinalConsolidationTest, PendingConstructionPressCannotSurviveResizeFocusLossOrMenuRoundTrip) {
  base(); A::build(*app);
  pointer({287,625},true); frame(); event(SDL_EVENT_WINDOW_RESIZED); frame();
  pointer({287,625},false); frame(); EXPECT_EQ(A::page(*app),BaseConstructionPage::Purchase);
  pointer({287,625},true); frame(); event(SDL_EVENT_WINDOW_FOCUS_LOST); frame();
  event(SDL_EVENT_WINDOW_FOCUS_GAINED); frame(); A::resume(*app); A::build(*app);
  pointer({287,625},false); frame(); EXPECT_EQ(A::page(*app),BaseConstructionPage::Purchase);
  pointer({287,625},true); pointer({287,625},false); frame(); EXPECT_EQ(A::page(*app),BaseConstructionPage::Owned);
  EXPECT_FALSE(A::fire(*app));
}

TEST_F(UiFinalConsolidationTest, ConstructionPagePressRejectsChangedRevisionAndOnlyFirstClickInFrameWins) {
  base(); A::build(*app);
  pointer({287,625},true); frame(); EXPECT_EQ(A::page(*app),BaseConstructionPage::Purchase);
  A::revision(*app); pointer({287,625},false); frame(); EXPECT_EQ(A::page(*app),BaseConstructionPage::Purchase);
  pointer({287,625},true); pointer({287,625},false);
  pointer({150,625},true); pointer({150,625},false); frame();
  EXPECT_EQ(A::page(*app),BaseConstructionPage::Owned); EXPECT_FALSE(A::fire(*app));
}

TEST_F(UiFinalConsolidationTest, LongMenuAndSupplyFailuresRemainScrollableWithoutTradingOrProfileChanges) {
  base(); A::settings(*app,true);
  std::string message;
  for(int i=0;i<24;++i) message+="SAVE FAILED: isolated-long-directory/"+std::to_string(i)+"/profile.tmp.json\n";
  message+="THE FINAL FAILURE REASON IS KEPT";
  A::message(*app)=message; A::pause(*app);
  clear(); A::drawMenu(*app,true); const auto before=profileStateFingerprint(A::session(*app).profile());
  // The pause feedback has its own visible scroll area, not an invisible
  // success hint. Drawing clamps the line offset to actual wrapped content.
  wheel({600,620},-80); frame(); clear(); A::drawMenu(*app,true);
  EXPECT_GT(A::scroll(*app),0); EXPECT_EQ(A::message(*app),message);
  EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
  A::resume(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Supply));
  clear(); A::drawSupply(*app); wheel({700,630},-80); frame(); clear(); A::drawSupply(*app);
  EXPECT_GT(A::scroll(*app),0); EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
}

TEST_F(UiFinalConsolidationTest, MenusConfirmationsAndDailyHudRenderBothLanguagesAtEveryWindowSize) {
  const auto before=profileStateFingerprint(A::session(*app).profile());
  for(const auto size:kUiWindowSizes) {
    detach(); attach(size.width,size.height); const float scale=size.width/1280.0F;
    for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
      A::text(*app).setLanguage(language);
      for(int state=0;state<5;++state) {
        SCOPED_TRACE(std::to_string(size.width)+uiLanguageCode(language)+std::to_string(state));
        A::settings(*app,state==2); A::overwrite(*app,state==1);
        A::message(*app,state>=3?"SAVE FAILED | Original profile unchanged. Check the directory and try again.":"");
        clear(); A::drawMenu(*app,state==4);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        EXPECT_GT(bright({350,170,570,44},scale),60);
        save(std::to_string(size.width)+"-"+uiLanguageCode(language)+"-menu-"+std::to_string(state));
      }
    }
  }
  EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
  base();
  A::message(*app,"");A::presentationTick(*app,0);
  const auto hudParcel=A::flow(*app).baseWorld().baseParcel();
  EnemyLifecycleTestAccess::place(A::flow(*app).baseWorld(),
      {hudParcel.position.x+hudParcel.size.x*.5F,hudParcel.position.y+hudParcel.size.y*.5F});
  ASSERT_EQ(A::flow(*app).baseWorld().playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
  for(const auto size:kUiWindowSizes) {
    detach(); attach(size.width,size.height);
    for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
      A::text(*app).setLanguage(language); clear(); A::drawBase(*app);
      ASSERT_TRUE(SDL_RenderPresent(renderer));
      EXPECT_GT(bright({40,32,254,80},size.width/1280.0F),30);
      EXPECT_FALSE(A::baseCombatStatusVisible(*app));
      EXPECT_EQ(bright({720,554,520,28},size.width/1280.0F),0U);
      EXPECT_EQ(bright({1040,638,216,62},size.width/1280.0F),0U);
      save(std::to_string(size.width)+"-"+uiLanguageCode(language)+"-base-hud");
    }
  }
  ASSERT_TRUE(A::session(*app).triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(A::session(*app).startBaseRealtimeDefense(A::flow(*app).baseWorld()));
  const auto defenseProfile=profileStateFingerprint(A::session(*app).profile());
  for(const auto size:kUiWindowSizes) {
    detach(); attach(size.width,size.height);
    for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
      A::text(*app).setLanguage(language); A::pause(*app); A::abandon(*app,true);
      for(const bool failed:{false,true}) {
        A::message(*app)=failed ? "SAVE FAILED | Original profile unchanged. Check the directory and try again." : "";
        clear(); A::drawMenu(*app,true); ASSERT_TRUE(SDL_RenderPresent(renderer));
        EXPECT_GT(bright({512,520,256,28},size.width/1280.0F),35);
        EXPECT_GT(bright({352,574,576,56},size.width/1280.0F),35);
        save(std::to_string(size.width)+"-"+uiLanguageCode(language)+(failed?"-defense-confirm-error":"-defense-confirm"));
      }
    }
  }
  EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),defenseProfile);
}

TEST_F(UiFinalConsolidationTest, ActualBaseCompositionUsesShortFacilityNamesAndOneRealInteractionTarget) {
  establishedBase();
  auto &world=A::flow(*app).baseWorld();
  const auto &facilities=world.facilities();
  const auto dorm=std::find_if(facilities.begin(),facilities.end(),[](const auto &facility){return facility.kind==BaseFacilityKind::Dormitory;});
  const auto workshop=std::find_if(facilities.begin(),facilities.end(),[](const auto &facility){return facility.kind==BaseFacilityKind::Workshop;});
  ASSERT_NE(dorm,facilities.end());ASSERT_TRUE(dorm->active);
  ASSERT_NE(workshop,facilities.end());ASSERT_TRUE(workshop->active);
  const auto parcel=world.baseParcel();
  std::optional<Vec2> quietPoint;
  for(float y=parcel.position.y+80;y<parcel.position.y+parcel.size.y-80&&!quietPoint;y+=80)
    for(float x=parcel.position.x+80;x<parcel.position.x+parcel.size.x-80&&!quietPoint;x+=80) {
      bool inBuilding{};
      for(const auto &facility:facilities)
        inBuilding=inBuilding||(x>facility.bounds.position.x-40&&x<facility.bounds.position.x+facility.bounds.size.x+40&&
            y>facility.bounds.position.y-60&&y<facility.bounds.position.y+facility.bounds.size.y+60);
      if(inBuilding)continue;
      EnemyLifecycleTestAccess::place(world,{x,y});
      if(!world.interactableFacility()&&world.playerSafetyZone()==HomeRegionSafetyZone::SafeCore)quietPoint=Vec2{x,y};
    }
  ASSERT_TRUE(quietPoint);
  const auto before=profileStateFingerprint(A::session(*app).profile());
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    SCOPED_TRACE(std::to_string(size.width)+" "+uiLanguageCode(language));
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    const float scale=size.width/1280.0F;const auto prefix="declutter-"+std::to_string(size.width)+"-"+uiLanguageCode(language);
    EnemyLifecycleTestAccess::place(world,*quietPoint);ASSERT_FALSE(world.interactableFacility());
    clear();A::drawFullBase(*app);save(prefix+"-safe");
    EXPECT_FALSE(A::baseCombatStatusVisible(*app));expectNoOldBasePanels(scale);
    auto &profile=const_cast<ProfileState&>(A::session(*app).profile());
    profile.tutorial=TutorialProgress::FindStorage;const auto tutorialBefore=profileStateFingerprint(profile);
    clear();A::drawFullBase(*app);save(prefix+"-unfinished-tutorial");
    EXPECT_GT(oldPanelEdge(330,22,620,scale),350U);
    expectCompleteText({346,32,588,52},language==UiLanguage::SimplifiedChinese?"目标：找到仓储区":"OBJECTIVE: FIND STORAGE",14,scale);
    EXPECT_EQ(profileStateFingerprint(profile),tutorialBefore);profile.tutorial=TutorialProgress::Complete;
    for(const auto *facility:{&*dorm,&*workshop}) {
      EnemyLifecycleTestAccess::place(world,baseFacilityAccessGeometry(*facility).entrancePoint);
      ASSERT_EQ(world.interactableFacility(),facility->kind);ASSERT_FALSE(A::flow(*app).nearestBaseGroundAsset());
      clear();A::drawFullBase(*app);save(prefix+(facility->kind==BaseFacilityKind::Dormitory?"-dorm":"-workshop"));
      expectNoOldBasePanels(scale);
      const bool isDorm=facility->kind==BaseFacilityKind::Dormitory;
      const auto shortName=language==UiLanguage::SimplifiedChinese?(isDorm?"宿舍":"工坊"):(isDorm?"Dorm":"Workshop");
      const auto prompt=fmt::format(fmt::runtime(language==UiLanguage::SimplifiedChinese?"E · 打开 {}":"E · Open {}"),shortName);
      expectCompleteText({450,678,380,22},prompt,13,scale,UiTextAlign::Center);
      clear();A::drawFullBase(*app);
      const auto bounds=A::labelBounds(*app,facility->bounds.position.x+18,facility->bounds.position.y+facility->bounds.size.y*.5F,shortName);
      const auto camera=A::baseCamera(*app);
      const SDL_FRect screen{bounds.x+std::lround(-camera.x),bounds.y+std::lround(-camera.y),bounds.w,bounds.h};
      ASSERT_GE(screen.x,0);ASSERT_GE(screen.y,0);ASSERT_LE(screen.x+screen.w,1280);ASSERT_LE(screen.y+screen.h,720);
      expectCompleteText(screen,shortName,12,scale);
    }
    // Selecting and zooming must retain the code glyphs/short names instead of
    // bringing the former entrance/worker/resident ledgers back into the world.
    for(const std::size_t zoom:{3U,4U}) {
      A::constructionView(*app,BaseFacilityKind::Dormitory,zoom);
      clear();A::drawFullBase(*app);save(prefix+"-construction-"+std::to_string(zoom));
      A::closeConstructionView(*app);
    }
    EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
  }
}

TEST_F(UiFinalConsolidationTest, ActualBaseNoticeFadesAfterThreeVisibleSecondsWhileErrorsAndOriginalMessageRemain) {
  establishedBase();
  // A legacy Alpha fixture initially has no Home Perimeter snapshot. Its first
  // ordinary simulation update creates that original roster and ground loot.
  // Complete this real setup before measuring presentation-only preservation.
  const auto loaded=A::session(*app).profile();ASSERT_TRUE(loaded.homePerimeter.sites.empty());
  A::flow(*app).updateBase({},0);
  const auto &initialized=A::session(*app).profile();
  ASSERT_TRUE(initialized.homePerimeter.sites.contains(RegionalBaseSiteDefinitionId{A::flow(*app).baseWorld().siteDefinitionId()}));
  auto expectedInitialization=loaded;expectedInitialization.assets=initialized.assets;
  expectedInitialization.homePerimeter=initialized.homePerimeter;expectedInitialization.revision=initialized.revision;
  expectedInitialization.committedTransactions=initialized.committedTransactions;
  EXPECT_EQ(profileStateFingerprint(initialized),profileStateFingerprint(expectedInitialization));
  const auto fixtureDiff=nlohmann::json::diff(
      nlohmann::json::parse(serializeProfileEnvelope(loaded,publishedContentRegistry().contentVersion())),
      nlohmann::json::parse(serializeProfileEnvelope(initialized,publishedContentRegistry().contentVersion())));
  ASSERT_FALSE(fixtureDiff.empty());
  if(const auto *out=std::getenv("RAIDLINE_UI_FINAL_MENU_REVIEW_DIR")) {
    std::filesystem::create_directories(out);
    std::ofstream stream(std::filesystem::path(out)/"base-initialization-profile-diff.json");
    stream<<fixtureDiff.dump(2)<<'\n';ASSERT_TRUE(stream.good());
  }
  const auto before=profileStateFingerprint(initialized);
  constexpr SDL_FRect notice{24,646,360,24},failure{24,590,400,110};
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    SCOPED_TRACE(std::to_string(size.width)+" "+uiLanguageCode(language));
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    const float scale=size.width/1280.0F;const auto prefix="declutter-notice-"+std::to_string(size.width)+"-"+uiLanguageCode(language);
    A::message(*app,"");A::presentationTick(*app,0);clear();A::drawFullBase(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    const auto empty=pixels(notice,scale);
    A::message(*app,"PROFILE LOADED");A::presentationTick(*app,0);
    clear();A::drawFullBase(*app);save(prefix+"-initial");
    const auto initialNotice=pixels(notice,scale);EXPECT_NE(initialNotice,empty);
    expectCompleteText(notice,language==UiLanguage::SimplifiedChinese?"存档已载入":"PROFILE LOADED",13,scale);
    A::pause(*app);A::presentationTick(*app,20);A::resume(*app);EXPECT_FLOAT_EQ(A::toastSeconds(*app),0);
    A::build(*app);A::presentationTick(*app,20);A::closeBuild(*app);EXPECT_FLOAT_EQ(A::toastSeconds(*app),0);
    A::presentationTick(*app,2.7F);clear();A::drawFullBase(*app);save(prefix+"-fading");
    const auto fading=pixels(notice,scale);EXPECT_NE(fading,empty);
    const auto difference=[&](const std::vector<Uint32> &image){
      std::uint64_t total{};EXPECT_EQ(image.size(),empty.size());
      for(std::size_t i=0;i<image.size();++i)for(const auto shift:{8,16,24})
        total+=static_cast<std::uint64_t>(std::abs(static_cast<int>((image[i]>>shift)&255U)-static_cast<int>((empty[i]>>shift)&255U)));
      return total;
    };
    EXPECT_GT(difference(fading),0U);EXPECT_LT(difference(fading),difference(initialNotice));
    A::presentationTick(*app,.4F);clear();A::drawFullBase(*app);save(prefix+"-expired");
    EXPECT_EQ(pixels(notice,scale),empty);EXPECT_EQ(A::message(*app),"PROFILE LOADED");
    expectNoOldBasePanels(scale);
    // Exercise the real map-close adapter, rather than inventing a success
    // classification solely for the rendering fixture.
    ASSERT_FALSE(A::mapOpen(*app));A::toggleBaseMap(*app);ASSERT_TRUE(A::mapOpen(*app));
    A::toggleBaseMap(*app);ASSERT_FALSE(A::mapOpen(*app));ASSERT_EQ(A::message(*app),"HOME REGION MAP CLOSED");
    A::presentationTick(*app,0);clear();A::drawFullBase(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_NE(pixels(notice,scale),empty);A::presentationTick(*app,3.1F);
    clear();A::drawFullBase(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_EQ(pixels(notice,scale),empty);EXPECT_EQ(A::message(*app),"HOME REGION MAP CLOSED");
    for(const auto &error:{std::string{"SAVE FAILED | isolated-profile-directory/profile.json | original profile unchanged"},
                          std::string{"UNCLASSIFIED TEST FAILURE | keep the complete reason"}}) {
      A::message(*app,error);A::presentationTick(*app,0);clear();A::drawFullBase(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
      const auto initial=pixels(failure,scale);EXPECT_GT(bright(failure,scale),30);
      A::presentationTick(*app,90);clear();A::drawFullBase(*app);
      save(prefix+(error.starts_with("SAVE")?"-save-failed":"-unknown-failure"));
      EXPECT_EQ(pixels(failure,scale),initial);EXPECT_EQ(A::message(*app),error);
    }
    EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
  }
  detach();attach(1280,720);A::loadArt(*app);A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
  std::string longError;
  for(int line=0;line<28;++line)longError+="Isolated full save failure line "+std::to_string(line)+".\n";
  const std::string tail="END OF ORIGINAL SAVE FAILURE";longError+=tail;
  A::message(*app,longError);A::presentationTick(*app,0);clear();A::drawFullBase(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
  const auto first=pixels(failure,1);wheel({100,630},-1000);A::events(*app);
  clear();A::drawFullBase(*app);save("declutter-long-error-1280-zh-CN-tail");
  EXPECT_GT(A::scroll(*app),0);EXPECT_NE(pixels(failure,1),first);EXPECT_EQ(A::message(*app),longError);
  // Four visible rows leave the final line at y=600+3*ceil(12*1.4).
  expectCompleteText({36,651,366,17},tail,12,1);
  EXPECT_EQ(profileStateFingerprint(A::session(*app).profile()),before);
}

TEST_F(UiFinalConsolidationTest, ActualBaseGroundTargetKeepsPriorityOverTheNearbyFacility) {
  establishedBase();auto &world=A::flow(*app).baseWorld();
  const auto &facilities=world.facilities();
  const auto dorm=std::find_if(facilities.begin(),facilities.end(),[](const auto &facility){return facility.kind==BaseFacilityKind::Dormitory;});
  ASSERT_NE(dorm,facilities.end());ASSERT_TRUE(dorm->active);
  const auto entrance=baseFacilityAccessGeometry(*dorm).entrancePoint;
  EnemyLifecycleTestAccess::place(world,entrance);ASSERT_EQ(world.interactableFacility(),BaseFacilityKind::Dormitory);
  auto &profile=const_cast<ProfileState&>(A::session(*app).profile());
  const auto id=profile.assets.create(publishedContentRegistry().item(ItemDefinitionId{"item.medical.bandage_basic"}),
      BaseGroundAssetLocation{RegionalBaseSiteDefinitionId{world.siteDefinitionId()},entrance},1);
  ASSERT_TRUE(validateProfileState(profile,publishedContentRegistry()).valid);
  const auto before=profileStateFingerprint(profile);
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    const auto target=A::flow(*app).nearestBaseGroundAsset();ASSERT_TRUE(target);ASSERT_EQ(target->assetId,id);
    ASSERT_EQ(world.interactableFacility(),BaseFacilityKind::Dormitory);
    clear();A::drawFullBase(*app);save("declutter-ground-"+std::to_string(size.width)+"-"+uiLanguageCode(language));
    expectNoOldBasePanels(size.width/1280.0F);
    expectCompleteText({450,678,380,22},language==UiLanguage::SimplifiedChinese?"E · 拾取 绷带 ×1":"E · Pick up Bandage ×1",
        13,size.width/1280.0F,UiTextAlign::Center);
    EXPECT_EQ(profileStateFingerprint(profile),before);
  }
}

TEST_F(UiFinalConsolidationTest, ActualBaseInjuryPerimeterAndDefenseKeepCompactVitalsWithoutDailyWeaponInstructions) {
  establishedBase();auto &profile=const_cast<ProfileState&>(A::session(*app).profile());
  auto &world=A::flow(*app).baseWorld();const auto parcel=world.baseParcel();
  const Vec2 safe{parcel.position.x+parcel.size.x*.5F,parcel.position.y+parcel.size.y*.5F};
  EnemyLifecycleTestAccess::place(world,safe);ASSERT_EQ(world.playerSafetyZone(),HomeRegionSafetyZone::SafeCore);
  EXPECT_FALSE(A::baseCombatStatusVisible(*app));profile.currentHealth=86;
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    SCOPED_TRACE(std::to_string(size.width)+" "+uiLanguageCode(language));
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    const float scale=size.width/1280.0F;const auto prefix="declutter-danger-"+std::to_string(size.width)+"-"+uiLanguageCode(language);
    EnemyLifecycleTestAccess::place(world,safe);const auto before=profileStateFingerprint(profile);
    ASSERT_TRUE(A::baseCombatStatusVisible(*app));clear();A::drawFullBase(*app);save(prefix+"-injured");
    expectNoOldBasePanels(scale);expectCompleteText({1080,643,160,22},language==UiLanguage::SimplifiedChinese?"生命 86":"HP 86",14,scale);
    EXPECT_EQ(profileStateFingerprint(profile),before);
    EnemyLifecycleTestAccess::place(world,{parcel.position.x-500,parcel.position.y+parcel.size.y*.5F});
    ASSERT_NE(world.playerSafetyZone(),HomeRegionSafetyZone::SafeCore);ASSERT_TRUE(A::baseCombatStatusVisible(*app));
    clear();A::drawFullBase(*app);save(prefix+"-perimeter");expectNoOldBasePanels(scale);
    expectCompleteText({1052,674,192,18},language==UiLanguage::SimplifiedChinese?"无枪械":"NO FIREARM",12,scale);
    EXPECT_EQ(profileStateFingerprint(profile),before);
  }
  EnemyLifecycleTestAccess::place(world,safe);
  profile.currentHealth=100;profile.medicalStatus={BleedingSeverity::Light,40000,1000,0,25000};
  EXPECT_TRUE(A::baseCombatStatusVisible(*app));profile.medicalStatus.painkillerRemainingMs=180000;
  EXPECT_TRUE(A::baseCombatStatusVisible(*app));profile.medicalStatus={};EXPECT_FALSE(A::baseCombatStatusVisible(*app));
  ASSERT_TRUE(A::session(*app).triggerDeveloperBaseSiegeWarning());
  ASSERT_TRUE(A::session(*app).startBaseRealtimeDefense(world));const auto defense=profileStateFingerprint(profile);
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    EXPECT_TRUE(A::baseCombatStatusVisible(*app));clear();A::drawFullBase(*app);
    save("declutter-danger-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+"-defense");
    expectNoOldBasePanels(size.width/1280.0F);
    expectCompleteText({1080,643,160,22},language==UiLanguage::SimplifiedChinese?"生命 100":"HP 100",14,size.width/1280.0F);
    EXPECT_EQ(profileStateFingerprint(profile),defense);
  }
}

TEST_F(UiFinalConsolidationTest, ActualExtractionCountdownPaintsItsLocalizedRemainingTimeAtEverySize) {
  const auto &content=publishedContentRegistry();
  const auto profile=makeNewAlphaProfile("final-countdown-isolated",content);
  ASSERT_TRUE(SaveRepository{path/"profile"}.save(profile,content.contentVersion()).succeeded);
  ASSERT_TRUE(A::flow(*app).continueGame());
  ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
  auto &session=A::session(*app);
  const auto point=session.world().extractionPoint().bounds();
  auto &player=const_cast<Player &>(session.world().player());
  const Vec2 center{point.position.x+point.size.x*.5F,point.position.y+point.size.y*.5F};
  ASSERT_TRUE(player.setPosition({center.x-player.size()*.5F,center.y-player.size()*.5F}));
  A::flow(*app).update({},.2F);
  ASSERT_EQ(session.world().raidSession().state(),RaidSessionState::Extracting);
  const auto before=profileStateFingerprint(session.profile());
  const auto &raid=session.world().raidSession();
  const float remaining=std::max(0.0F,raid.extractionDuration()-raid.extractionTimeElapsed());
  const Vec2 camera=A::raidCamera(*app);
  const SDL_Rect viewport{static_cast<int>(std::lround(-camera.x)),static_cast<int>(std::lround(-camera.y)),
      static_cast<int>(session.world().raidSpaceWorldSize().x),static_cast<int>(session.world().raidSpaceWorldSize().y)};
  for(const auto size:kUiWindowSizes) {
    detach();attach(size.width,size.height);const float scale=size.width/1280.0F;
    const auto glyphs=[&](SDL_FRect box){
      std::vector<bool> mask;
      for(int y=static_cast<int>(std::floor(box.y*scale));y<static_cast<int>(std::ceil((box.y+box.h)*scale));++y)
        for(int x=static_cast<int>(std::floor(box.x*scale));x<static_cast<int>(std::ceil((box.x+box.w)*scale));++x){
          Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
          mask.push_back(r==222&&g==231&&b==226);
        }
      return mask;
    };
    for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
      A::text(*app).setLanguage(language);
      const auto expected=language==UiLanguage::SimplifiedChinese
          ?fmt::format("保持位置 {:.1f}秒",remaining):fmt::format("HOLD POSITION {:.1f}s",remaining);
      const auto bounds=A::labelBounds(*app,point.position.x+22,point.position.y+48,expected);
      const SDL_FRect screen{bounds.x+viewport.x,bounds.y+viewport.y,bounds.w,bounds.h};
      clear();ASSERT_TRUE(SDL_SetRenderViewport(renderer,&viewport));A::drawWorldHints(*app);
      ASSERT_TRUE(SDL_SetRenderViewport(renderer,nullptr));ASSERT_TRUE(SDL_RenderPresent(renderer));
      const auto actual=glyphs(screen);
      save("countdown-"+std::to_string(size.width)+"-"+uiLanguageCode(language));
      clear();SDL_SetRenderDrawColor(renderer,222,231,226,255);
      A::text(*app).renderBox(renderer,screen,expected,{12,UiTextAlign::Left,1});
      ASSERT_TRUE(SDL_RenderPresent(renderer));const auto reference=glyphs(screen);
      EXPECT_GT(std::count(reference.begin(),reference.end(),true),20);
      EXPECT_EQ(actual,reference)<<"actual remaining-time text must be complete and localized";
    }
  }
  EXPECT_EQ(profileStateFingerprint(session.profile()),before);
}

TEST_F(UiFinalConsolidationTest, ActualCorpseLabelUsesObservedSideAndSurvivesTheOriginalInformationMask) {
  const auto &content=publishedContentRegistry();
  auto profile=makeNewAlphaProfile("supplemental-world-labels",content);
  static_cast<void>(profile.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}),EquippedAssetLocation{EquipmentSlotKind::Backpack}));
  ASSERT_TRUE(SaveRepository{path/"profile"}.save(profile,content.contentVersion()).succeeded);
  ASSERT_TRUE(A::flow(*app).continueGame());ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
  auto &session=A::session(*app);
  const auto center=[](const auto &r){return Vec2{r.position.x+r.size.x*.5F,r.position.y+r.size.y*.5F};};
  const auto position=[&](Vec2 p){
    auto &player=const_cast<Player&>(session.world().player());
    ASSERT_TRUE(player.setPosition({p.x-player.size()*.5F,p.y-player.size()*.5F}));
    GameplayInput input;input.aimWorldPosition=Vec2{p.x+60,p.y};A::flow(*app).update(input,0);
  };
  // Reproduce the actual doorway round trip and frozen-origin corpse from
  // the review, rather than fabricating an unobservable UI-only target.
  const auto room=session.profile().pendingRaid->interiors.front();
  position(center(room.exteriorEntrance));GameplayInput enter;enter.interactJustPressed=true;
  A::flow(*app).update(enter,0);ASSERT_EQ(session.world().activeRaidSpaceId(),room.id);
  position(center(room.interiorExit));A::flow(*app).update(enter,0);ASSERT_TRUE(session.world().inOutdoorRaidSpace());
  const auto &snapshot=session.profile().pendingRaid->enemies;
  const auto source=std::find_if(snapshot.begin(),snapshot.end(),[](const auto &e){return !e.sourceEnemyId.empty();});
  ASSERT_NE(source,snapshot.end());
  auto &actors=const_cast<std::vector<Enemy>&>(session.world().enemies());
  const auto actor=std::find_if(actors.begin(),actors.end(),[&](const auto &e){return e.position().x==source->position.x&&e.position().y==source->position.y;});
  ASSERT_NE(actor,actors.end());static_cast<void>(actor->takeDamage(actor->maxHealth()));A::flow(*app).update({},.01F);
  std::optional<Vec2> death;
  for(const auto &[id,asset]:session.profile().assets.records())
    if(const auto *corpse=std::get_if<RaidCorpseContainerAssetLocation>(&asset.location);corpse&&corpse->deathPosition){death=*corpse->deathPosition;break;}
  ASSERT_TRUE(death);position(*death);
  const auto before=profileStateFingerprint(session.profile());
  const auto &player=session.world().player();const auto camera=A::raidCamera(*app);
  const SDL_FRect avatar{std::round(player.position().x+(player.size()-64)*.5F),std::round(player.position().y+(player.size()-80)*.5F),64,80};
  const SDL_FRect view{camera.x,camera.y,1280,720};
  const float screenX=std::lround(-camera.x),screenY=std::lround(-camera.y);
  static_cast<void>(session.world().raidObservation());const auto builds=session.world().indoorObservationBuildCount();
  for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
    SCOPED_TRACE(std::to_string(size.width)+" "+uiLanguageCode(language));
    detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
    const auto expected=language==UiLanguage::SimplifiedChinese?"F：搜索尸体":"F: SEARCH CORPSE";
    const float width=std::clamp(A::text(*app).measure(expected,12),24.0F,280.0F);
    const SDL_FRect original{death->x-26,death->y-30,width,18};
    const auto unfiltered=uiFinalWorldLabelBounds(original,avatar,view);
    EXPECT_GT(unfiltered.x,avatar.x+avatar.w);
    const auto selected=A::labelBounds(*app,original.x,original.y,expected);
    EXPECT_TRUE(selected.x+selected.w<avatar.x || selected.x>avatar.x+avatar.w ||
        selected.y+selected.h<avatar.y || selected.y>avatar.y+avatar.h);
    for(const Vec2 sample:std::array<Vec2,5>{{{selected.x+1,selected.y+1},
        {selected.x+selected.w-1,selected.y+1},{selected.x+1,selected.y+selected.h-1},
        {selected.x+selected.w-1,selected.y+selected.h-1},{selected.x+selected.w*.5F,selected.y+9}}})
      EXPECT_TRUE(session.world().raidBoundsObservable({sample,{.25F,.25F}}));
    const SDL_FRect screen{selected.x+screenX,selected.y+screenY,selected.w,selected.h};
    const float scale=size.width/1280.0F;
    const int sampleWidth=static_cast<int>(std::ceil((screen.x+screen.w)*scale))-static_cast<int>(std::floor(screen.x*scale));
    const auto glyphs=[&]{
      std::vector<bool> mask;
      for(int y=static_cast<int>(std::floor(screen.y*scale));y<static_cast<int>(std::ceil((screen.y+screen.h)*scale));++y)
        for(int x=static_cast<int>(std::floor(screen.x*scale));x<static_cast<int>(std::ceil((screen.x+screen.w)*scale));++x){
          Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
          // Windows glyph alpha and fractional scaling need not leave an
          // exactly opaque source-color pixel. Keep all high-contrast strokes.
          mask.push_back(r>180&&g>190&&b>185);
        }
      return mask;
    };
    clear();A::drawRaid(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));const auto actual=glyphs();
    save("corpse-observed-label-"+std::to_string(size.width)+"-"+uiLanguageCode(language));
    // The observed information is painted into a logical-size texture before
    // nearest-neighbour composition. A direct physical-window font draw has
    // different rasterization at 125/150 percent and is not a valid reference.
    const auto target=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>(
        SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1280,720),SDL_DestroyTexture);
    ASSERT_NE(target,nullptr);ASSERT_TRUE(SDL_SetTextureBlendMode(target.get(),SDL_BLENDMODE_NONE));
    ASSERT_TRUE(SDL_SetTextureScaleMode(target.get(),SDL_SCALEMODE_NEAREST));
    ASSERT_TRUE(SDL_SetRenderTarget(renderer,target.get()));ASSERT_TRUE(SDL_SetRenderViewport(renderer,nullptr));
    clear();SDL_SetRenderDrawColor(renderer,222,231,226,255);
    A::text(*app).renderBox(renderer,screen,expected,{12,UiTextAlign::Left,1});
    ASSERT_TRUE(SDL_SetRenderTarget(renderer,nullptr));clear();
    const std::array<SDL_Vertex,4> quad{{{{0,0},{1,1,1,1},{0,0}},{{1280,0},{1,1,1,1},{1,0}},
        {{1280,720},{1,1,1,1},{1,1}},{{0,720},{1,1,1,1},{0,1}}}};
    const std::array<int,6> triangles{{0,1,2,0,2,3}};
    ASSERT_TRUE(SDL_RenderGeometry(renderer,target.get(),quad.data(),4,triangles.data(),6));
    ASSERT_TRUE(SDL_RenderPresent(renderer));const auto reference=glyphs();
    save("corpse-reference-label-"+std::to_string(size.width)+"-"+uiLanguageCode(language));
    EXPECT_GT(std::count(reference.begin(),reference.end(),true),30);
    ASSERT_EQ(actual.size(),reference.size());
    // A fan triangle and a reference quad can choose adjacent texels at a
    // fractional window scale. Allow that one physical-pixel raster difference
    // while requiring every independent glyph stroke to survive the mask.
    std::size_t missing{};const int sampleHeight=static_cast<int>(actual.size())/sampleWidth;
    for(std::size_t i=0;i<reference.size();++i)if(reference[i]) {
      const int x=static_cast<int>(i)%sampleWidth,y=static_cast<int>(i)/sampleWidth;
      bool present{};
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        if(x+dx>=0&&x+dx<sampleWidth&&y+dy>=0&&y+dy<sampleHeight)
          present=present||actual[static_cast<std::size_t>((y+dy)*sampleWidth+x+dx)];
      if(!present)++missing;
    }
    EXPECT_EQ(missing,0U)<<"each independently rendered word must remain visible; logical label="<<screen.x<<","<<screen.y;
    EXPECT_EQ(session.world().indoorObservationBuildCount(),builds);
  }
  EXPECT_EQ(profileStateFingerprint(session.profile()),before);
}
