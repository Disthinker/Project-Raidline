#include "app.h"
#include "alpha_content_ids.h"
#include "inventory_domain.h"
#include "test_temp_directory.h"
#include "developer_test_range.h"
#include "raid_lifecycle.h"
#include "raid_tactical_map_presentation.h"
#include <algorithm>
#include <cmath>
#include "ui_raid_hud_presentation.h"
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>

struct UiRaidHudTestAccess {
    static auto &flow(App &a){return a.gameFlow_;}
    static auto &session(App &a){return a.gameSession_;}
    static auto &text(App &a){return a.uiTextRenderer_;}
    static bool attach(App &a,SDL_Renderer *r,float scale){a.renderer_=r;if(!a.uiTextRenderer_.initialize(r))return false;a.uiTextRenderer_.setRasterScale(scale);return true;}
    static void detach(App &a){a.indoorInformationLayer_.reset();a.indoorShadowMask_.reset();a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
    static void clearPanels(App &a){a.developerWeaponPanelOpen_=false;a.developerRangePanelOpen_=false;a.developerPerformanceOverlayOpen_=false;a.tacticalMapOpen_=false;}
    static void hud(App &a){a.renderDebugText();a.renderMeleeStatus();a.renderThrowableUi();}
    static void raid(App &a){a.renderRaidScreen();}
    static void fieldTrade(App &a, const RaidFieldTradeView &view) { a.drawFieldIntelligenceTradePanel(view); }
    static void range(App &a){a.developerRangePanelOpen_=true;a.renderDeveloperRangePanel();a.developerRangePanelOpen_=false;}
    static void developer(App &a){a.developerWeaponPanelOpen_=true;a.renderDeveloperWeaponPanel();a.developerWeaponPanelOpen_=false;}
    static void performance(App &a){a.developerPerformanceOverlayOpen_=true;a.framePerformance_.record({.21F,2.40F,3.20F,10.86F,16.67F});a.refreshDeveloperPerformanceOverlay();a.renderDeveloperPerformanceOverlay();a.developerPerformanceOverlayOpen_=false;}
    static void home(App &a){a.tacticalMapOpen_=true;a.renderHomeRegionMap();a.tacticalMapOpen_=false;}
    static void tactical(App &a){a.tacticalMapOpen_=true;a.renderRaidTacticalMap();a.tacticalMapOpen_=false;}
    static void loading(App &a,float progress){a.renderRaidLoadingScreen(publishedContentRegistry().maps().front(),{progress,RaidDeploymentProgressStage::BuildingWorld});}
    static void hints(App &a){a.renderFirstRaidHintCard({"FIRST RAID | TAB PREPARE | H HIDE HINTS","DRAG TO EQUIP; HOVER YOUR WEAPON TO CHECK FIRE READINESS","ARMOR AND MEDICINE ARE OPTIONAL; EMPTY-HAND DEPLOY IS ALLOWED"},640,92,620);}
};
namespace {
using A=UiRaidHudTestAccess;
class UiRaidHudTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};std::filesystem::path path;
    void SetUp()override{
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-final-hud-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();app=std::make_unique<App>();A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).startNewGame("final-hud-isolated"));
        attach(1280,720);
    }
    void attach(int w,int h){surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));ASSERT_TRUE(A::attach(*app,renderer,w/1280.0F));}
    void detach(){if (app) A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;}
    void TearDown()override{detach();app.reset();SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void clear(){SDL_SetRenderDrawColor(renderer,3,8,11,255);ASSERT_TRUE(SDL_RenderClear(renderer));}
    std::size_t bright(SDL_Rect b){std::size_t n{};for(int y=b.y;y<b.y+b.h;++y)for(int x=b.x;x<b.x+b.w;++x){Uint8 r{},g{},bl{},a{};if(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&bl,&a)&&r>150&&g>150&&bl>140)++n;}return n;}
    void range(){ASSERT_TRUE(A::flow(*app).enterDeveloperRange());const auto weapons=developerRangeWeapons();const auto ammo=developerRangeAmmunition(weapons.front());ASSERT_TRUE(A::session(*app).grantDeveloperRangeLoadout(weapons.front(),ammo.front(),std::nullopt));A::clearPanels(*app);}
};
}

TEST_F(UiRaidHudTest, PlayerHudKeepsAimingCenterFreeAndDiagnosticsRequireF9){
    range();clear();A::hud(*app);SDL_RenderPresent(renderer);
    EXPECT_GT(bright({26,553,226,28}),60U); // actual HP glyphs; no panel border is inside
    EXPECT_GT(bright({20,546,610,78}),100U);
    EXPECT_EQ(bright({656,12,608,350}),0U);
    EXPECT_EQ(bright({480,210,320,220}),0U);
    clear();A::performance(*app);SDL_RenderPresent(renderer);
    EXPECT_GT(bright({670,280,580,68}),100U);
}

TEST_F(UiRaidHudTest, ActionAndHazardPaintingShowReadableCurrentFactsWithoutMutatingProfile){
    const auto revision=A::session(*app).profile().revision;
    const auto count=A::session(*app).profile().assets.records().size();
    UiRaidHudProjection p;p.health="HP 12 / 100";p.healthFraction=.12F;p.danger=true;p.armor="ARMOR 3 / 80";
    p.weapon="M4A1";p.ammunition="AMMO 29 + 1";p.condition="MALFUNCTION · SWEEP MOUSE TO CLEAR";
    p.action="EXTRACTING 65%";p.actionActive=true;p.actionFraction=.65F;p.medical="HEAVY BLEED · APPLY TOURNIQUET";
    p.feedback="The action was rejected; original equipment is preserved.";p.slots={"1 M4A1","2 FS-12","3 93R"};
    // The medical band contains text only. Compare its real orange glyph mask
    // against an independent text-renderer reference, never a panel border.
    const SDL_FRect medicalBounds{16,500,400,42};
    const SDL_FRect conditionBounds{276,630,360,22};
    const auto orangeGlyphMask=[&](SDL_Rect bounds){
        std::vector<bool> mask;mask.reserve(bounds.w*bounds.h);
        for(int y=bounds.y;y<bounds.y+bounds.h;++y)for(int x=bounds.x;x<bounds.x+bounds.w;++x){
            Uint8 r{},g{},b{},alpha{};
            EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&alpha));
            mask.push_back(r>110 && g>75 && b>40 && r>g+20 && g>b+20);
        }
        return mask;
    };
    for(const auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese}){
        A::text(*app).setLanguage(language);
        p.condition=language==UiLanguage::SimplifiedChinese?"卡弹 · 移动鼠标排障":"MALFUNCTION · SWEEP MOUSE TO CLEAR";
        p.armor=language==UiLanguage::SimplifiedChinese?"护甲 3 / 80":"ARMOR 3 / 80";
        p.feedback=language==UiLanguage::SimplifiedChinese?"操作被拒绝；原装备保留。":"The action was rejected; original equipment is preserved.";
        p.light=language==UiLanguage::SimplifiedChinese?"手电开启 · 68% · U":"LIGHT ON · 68% · U";
        clear();renderUiRaidHud(renderer,A::text(*app),p);SDL_RenderPresent(renderer);
        EXPECT_GT(bright({26,553,226,28}),60U);
        EXPECT_GT(bright({446,485,388,28}),50U);
        const auto layout=layoutUiRaidHud();
        const auto feedback=layoutUiCompactFeedback(renderer,A::text(*app),p.feedback,
            {layout.feedback.x,layout.feedback.y+layout.feedback.h});
        const SDL_Rect feedbackPixels{static_cast<int>(std::floor(feedback.text.x)),
            static_cast<int>(std::floor(feedback.text.y)),
            static_cast<int>(std::ceil(feedback.text.x+feedback.text.w)-std::floor(feedback.text.x)),
            static_cast<int>(std::ceil(feedback.text.y+feedback.text.h)-std::floor(feedback.text.y))};
        EXPECT_GT(bright(feedbackPixels),50U);
        const auto actualMedical=orangeGlyphMask({16,500,400,42});
        const auto actualCondition=orangeGlyphMask({276,630,360,22});
        EXPECT_GT(bright({650,630,400,22}),30U); // actual light label; no card/border here
        if(const auto *env=std::getenv("RAIDLINE_UI_FINAL_REVIEW_DIR")){
            const auto destination=std::filesystem::path(env);ASSERT_TRUE(destination.is_absolute());
            std::filesystem::create_directories(destination);
            ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string("aux-raid-hazard-1280-")+uiLanguageCode(language)+".png")).string().c_str()));
        }
        clear();SDL_SetRenderDrawColor(renderer,240,178,120,255);
        A::text(*app).renderBox(renderer,medicalBounds,p.medical,{13,UiTextAlign::Left,2});
        SDL_RenderPresent(renderer);
        const auto expectedMedical=orangeGlyphMask({16,500,400,42});
        EXPECT_GT(std::count(expectedMedical.begin(),expectedMedical.end(),true),40);
        EXPECT_EQ(actualMedical,expectedMedical);
        clear();SDL_SetRenderDrawColor(renderer,240,178,120,255);
        A::text(*app).renderBox(renderer,conditionBounds,p.condition,{12,UiTextAlign::Left,1});
        SDL_RenderPresent(renderer);
        const auto expectedCondition=orangeGlyphMask({276,630,360,22});
        EXPECT_GT(std::count(expectedCondition.begin(),expectedCondition.end(),true),40);
        EXPECT_EQ(actualCondition,expectedCondition);
    }
    EXPECT_EQ(A::session(*app).profile().revision,revision);EXPECT_EQ(A::session(*app).profile().assets.records().size(),count);
    clear();p.actionActive=false;p.action.clear();p.feedback.clear();renderUiRaidHud(renderer,A::text(*app),p);SDL_RenderPresent(renderer);
    EXPECT_EQ(bright({446,485,388,28}),0U);
}

TEST_F(UiRaidHudTest, AllAuxiliarySurfacesRenderInBothLanguagesAndThreeWindowSizes){
    std::filesystem::path destination;if(const auto *env=std::getenv("RAIDLINE_UI_FINAL_REVIEW_DIR")){destination=std::filesystem::path(env);ASSERT_TRUE(destination.is_absolute());std::filesystem::create_directories(destination);}
    for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese}){
        detach();attach(size.width,size.height);A::text(*app).setLanguage(language);
        const auto save=[&](const char *name){ASSERT_TRUE(SDL_RenderPresent(renderer));if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string("aux-")+name+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));};
        clear();A::home(*app);save("home-map");
        range();const auto revision=A::session(*app).profile().revision;
        clear();A::hud(*app);save("raid-hud");
        clear();A::range(*app);save("range");
        clear();A::developer(*app);save("runtime");
        clear();A::performance(*app);save("performance");
        clear();A::hints(*app);save("first-raid");
        clear();A::loading(*app,.43F);save("loading");
        EXPECT_EQ(A::session(*app).profile().revision,revision);
        ASSERT_TRUE(A::flow(*app).leaveDeveloperRange());
    }
}

TEST_F(UiRaidHudTest, FieldIntelligenceTradePanelKeepsAimAreaClearAtEveryWindowSize)
{
    RaidFieldTradeView trade;
    trade.balance = kFieldIntelligenceSalePrice;
    trade.sellableFile = AssetInstanceId{1};
    trade.offers = {{{kFieldIntelligenceSourceMapId, RaidIntelligenceCategory::Transport, 36, false},
                     {kFieldIntelligenceTargetMapId, RaidIntelligenceCategory::Transport, 58, false},
                     {kFieldIntelligenceSourceMapId, RaidIntelligenceCategory::Enemy, 42, true}}};

    const char *reviewPath = std::getenv("RAIDLINE_UI_FIELD_TRADE_REVIEW_DIR");
    const auto destination = reviewPath ? std::filesystem::path(reviewPath) : std::filesystem::path{};
    if (reviewPath)
    {
        ASSERT_TRUE(destination.is_absolute());
        std::filesystem::create_directories(destination);
    }
    for (const auto size : kUiWindowSizes)
    {
        detach();
        attach(size.width, size.height);
        const auto physical = [&](SDL_Rect bounds)
        {
            const float scale = size.width / 1280.0F;
            return SDL_Rect{
                static_cast<int>(bounds.x * scale), static_cast<int>(bounds.y * scale),
                static_cast<int>(bounds.w * scale), static_cast<int>(bounds.h * scale)};
        };
        for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese})
        {
            SCOPED_TRACE(size.width);
            SCOPED_TRACE(uiLanguageCode(language));
            A::text(*app).setLanguage(language);
            clear();
            A::fieldTrade(*app, trade);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            EXPECT_EQ(bright(physical({488, 224, 300, 250})), 0U);
            EXPECT_GT(bright(physical({838, 108, 248, 24})), 30U);
            EXPECT_GT(bright(physical({838, 266, 248, 38})), 30U);
            EXPECT_GT(bright(physical({1160, 269, 80, 24})), 10U);
            if (reviewPath)
            {
                const auto name = "field-trade-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
            }
        }
    }
}

TEST_F(UiRaidHudTest, RoadBlockadeWarningAndDiscoveredMapMarkerFitAllWindowSizes)
{
    auto &session = A::session(*app);
    ASSERT_TRUE(session.deployAlpha(2U, kFreightCrateMapId)) << session.persistenceMessage();
    ASSERT_TRUE(session.world().roadBlockade());
    const auto blockade = *session.profile().pendingRaid->highRiskCrisis->roadBlockade;
    const Vec2 center{blockade.bounds.position.x + blockade.bounds.size.x * 0.5F,
        blockade.bounds.position.y + blockade.bounds.size.y * 0.5F};
    const auto &map = session.world().tacticalMap();
    ASSERT_TRUE(session.triggerDeveloperHighRisk());
    for (const auto &enemy : session.world().enemies())
        static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({blockade.routeEntry.x - half, blockade.routeEntry.y - half}));
    GameplayInput lookAway;
    lookAway.aimWorldPosition = Vec2{2.0F * blockade.routeEntry.x - center.x,
        2.0F * blockade.routeEntry.y - center.y};
    lookAway.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
    session.update(lookAway, 0.125F);
    ASSERT_FALSE(map.discoveredRoadBlockade());
    ASSERT_EQ(session.world().roadBlockade()->phase, RaidRoadBlockadePhase::Warning);

    const char *reviewPath = std::getenv("RAIDLINE_UI_ROAD_BLOCKADE_REVIEW_DIR");
    const auto destination = reviewPath ? std::filesystem::path(reviewPath) : std::filesystem::path{};
    if (reviewPath)
    {
        ASSERT_TRUE(destination.is_absolute());
        std::filesystem::create_directories(destination);
    }
    const auto orangePixels = [&](SDL_Rect bounds)
    {
        unsigned count{};
        for (int y = bounds.y; y < bounds.y + bounds.h; ++y)
            for (int x = bounds.x; x < bounds.x + bounds.w; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) &&
                    r > 180 && g > 125 && g < 205 && b < 135)
                    ++count;
            }
        return count;
    };
    for (const auto size : kUiWindowSizes)
    {
        detach();
        attach(size.width, size.height);
        const float scale = size.width / 1280.0F;
        const auto physical = [scale](SDL_Rect bounds) {
            return SDL_Rect{static_cast<int>(bounds.x * scale), static_cast<int>(bounds.y * scale),
                static_cast<int>(bounds.w * scale), static_cast<int>(bounds.h * scale)};
        };
        for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese})
        {
            SCOPED_TRACE(size.width);
            SCOPED_TRACE(uiLanguageCode(language));
            A::text(*app).setLanguage(language);
            clear();
            A::hud(*app);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            EXPECT_GT(orangePixels(physical({16, 110, 448, 40})), 20U);
            EXPECT_EQ(bright(physical({480, 210, 320, 220})), 0U);
            if (reviewPath)
            {
                const auto name = "road-warning-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
            }
        }
    }
    GameplayInput lookAt = lookAway;
    lookAt.aimWorldPosition = center;
    session.update(lookAt, 0.125F);
    ASSERT_TRUE(map.discoveredRoadBlockade());
    for (unsigned frame = 0; frame < 40; ++frame)
        session.update(lookAt, 0.125F);
    ASSERT_EQ(map.discoveredRoadBlockade()->phase, RaidRoadBlockadePhase::Closed);
    for (const auto size : kUiWindowSizes)
    {
        detach();
        attach(size.width, size.height);
        const float scale = size.width / 1280.0F;
        const auto chart = kRaidTacticalChart;
        const auto worldSize = session.world().raidSpaceWorldSize();
        const Vec2 marker{chart.position.x + center.x / worldSize.x * chart.size.x,
            chart.position.y + center.y / worldSize.y * chart.size.y};
        for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese})
        {
            SCOPED_TRACE(size.width);
            SCOPED_TRACE(uiLanguageCode(language));
            A::text(*app).setLanguage(language);
            clear();
            A::tactical(*app);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            const SDL_Rect badge{static_cast<int>((marker.x - 5.0F) * scale),
                static_cast<int>((marker.y - 5.0F) * scale),
                static_cast<int>(10.0F * scale), static_cast<int>(10.0F * scale)};
            EXPECT_GT(orangePixels(badge), 10U);
            const SDL_Rect status{static_cast<int>(978 * scale), static_cast<int>(95 * scale),
                static_cast<int>(158 * scale), static_cast<int>(17 * scale)};
            EXPECT_GT(bright(status), 14U);
            if (reviewPath)
            {
                const auto name = "road-map-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
            }
        }
    }
}

TEST(UiRaidHudLayoutTest, GuideCardsStayInsideWindowAtItsEdges){
    for(const auto x:{-90.0F,16.0F,1200.0F})for(const auto y:{-20.0F,590.0F,900.0F}){
        const auto b=uiAuxiliaryHintRect(4,x,y,620);EXPECT_GE(b.x,16);EXPECT_GE(b.y,12);EXPECT_LE(b.x+b.w,1264);EXPECT_LE(b.y+b.h,708);
    }
}

TEST_F(UiRaidHudTest, GameplayBottomLanesReserveCompleteFootprintsWithEightUnitGaps) {
    const auto separated=[](SDL_FRect a,SDL_FRect b) {
        return a.x+a.w+kUiGameplayHudGap<=b.x+.001F || b.x+b.w+kUiGameplayHudGap<=a.x+.001F ||
            a.y+a.h+kUiGameplayHudGap<=b.y+.001F || b.y+b.h+kUiGameplayHudGap<=a.y+.001F;
    };
    for(const auto size:kUiWindowSizes)for(const float dpi:{1.0F,1.25F,1.5F,2.0F}) {
        detach();attach(size.width,size.height);
        ASSERT_TRUE(SDL_SetFloatProperty(SDL_GetRendererProperties(renderer),kUiDisplayScaleProperty,dpi));
        for(const auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese}) {
            A::text(*app).setLanguage(language);
            const std::string message=language==UiLanguage::English
                ?"Saved changes are confirmed. A second line still has its own bounded footprint."
                :"更改已保存。多行反馈也必须避让同时可见的武器、控制和操作提示。";
            for(const bool base:{false,true})for(const bool transient:{false,true})
                for(const bool diagnostics:{false,true}) {
                    const auto l=layoutUiGameplayBottomHud(renderer,A::text(*app),
                        {base,true,true,transient,diagnostics,message});
                    std::vector<SDL_FRect> occupied{l.vitals,l.action,l.condition,l.light,l.melee,l.feedback.panel};
                    if(base) {occupied.push_back(l.interaction);if(diagnostics)occupied.push_back({16,562,1248,35});}
                    else {occupied.push_back(l.weapon);occupied.push_back(l.medical);occupied.push_back(l.throwableHint);
                        occupied.insert(occupied.end(),l.slots.begin(),l.slots.end());}
                    for(std::size_t i=0;i<occupied.size();++i) {
                        const auto a=occupied[i];EXPECT_GT(a.w,0);EXPECT_GT(a.h,0);
                        EXPECT_GE(a.x,0);EXPECT_GE(a.y,0);EXPECT_LE(a.x+a.w,1280);EXPECT_LE(a.y+a.h,720);
                        for(std::size_t j=i+1;j<occupied.size();++j)
                            EXPECT_TRUE(separated(a,occupied[j]))<<"base="<<base<<" dpi="<<dpi<<" pair="<<i<<","<<j;
                    }
                }
        }
    }
}

TEST_F(UiRaidHudTest, TacticalMapsKeepUnknownInteriorClippedAndPreserveIntelligence){
    ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
    auto &session=A::session(*app);
    ASSERT_TRUE(session.profile().pendingRaid);
    ASSERT_FALSE(session.profile().pendingRaid->interiors.empty());
    const auto interiors=session.profile().pendingRaid->interiors;
    const auto room=*std::max_element(interiors.begin(),interiors.end(),[](const auto &a,const auto &b){return a.worldSize.x*a.worldSize.y<b.worldSize.x*b.worldSize.y;});
    ASSERT_FALSE(room.layoutKnown);
    const auto position=[&](ContentRect b){const float half=session.world().player().size()*.5F;ASSERT_TRUE(const_cast<Player &>(session.world().player()).setPosition({b.position.x+b.size.x*.5F-half,b.position.y+b.size.y*.5F-half}));};
    const auto interact=[&](){GameplayInput input;input.interactJustPressed=true;session.update(input,0);};
    std::filesystem::path destination;if(const auto *env=std::getenv("RAIDLINE_UI_FINAL_REVIEW_DIR")){destination=std::filesystem::path(env);ASSERT_TRUE(destination.is_absolute());std::filesystem::create_directories(destination);}
    for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese}){
        detach();attach(size.width,size.height);A::text(*app).setLanguage(language);
        const auto save=[&](const char *name){ASSERT_TRUE(SDL_RenderPresent(renderer));if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string("aux-map-")+name+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));};
        ASSERT_TRUE(session.world().inOutdoorRaidSpace());
        for(auto category:{RaidIntelligenceCategory::Transport,RaidIntelligenceCategory::Resource,RaidIntelligenceCategory::Enemy})EXPECT_FALSE(session.world().tacticalMap().hasIntelligence(category));
        const auto outside=profileStateFingerprint(session.profile());
        clear();A::tactical(*app);save("outdoor-unknown");EXPECT_EQ(profileStateFingerprint(session.profile()),outside);
        position(room.exteriorEntrance);interact();ASSERT_EQ(session.world().activeRaidSpaceId(),room.id);
        const auto projection=session.world().activeInteriorMapProjection();ASSERT_TRUE(projection);EXPECT_FALSE(projection->layoutKnown);
        EXPECT_LT(projection->visibleBounds.size.x*projection->visibleBounds.size.y,projection->worldSize.x*projection->worldSize.y);
        const auto inside=profileStateFingerprint(session.profile());
        clear();A::tactical(*app);save("interior-local");EXPECT_EQ(profileStateFingerprint(session.profile()),inside);
        const float scale=size.width/1280.0F;
        std::size_t black{};const SDL_Rect chart{static_cast<int>(144*scale),static_cast<int>(114*scale),static_cast<int>(992*scale),static_cast<int>(504*scale)};
        for(int y=chart.y;y<chart.y+chart.h;y+=4)for(int x=chart.x;x<chart.x+chart.w;x+=4){Uint8 r{},g{},b{},a{};ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));if(r<5&&g<5&&b<5)++black;}
        EXPECT_GT(black,static_cast<std::size_t>(chart.w*chart.h/40));
        position(room.interiorExit);interact();ASSERT_TRUE(session.world().inOutdoorRaidSpace());
    }
}

TEST_F(UiRaidHudTest, FuelExitStatusRequiresMapDiscoveryAndPaintsLiveProgress) {
    auto &session = A::session(*app);
    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto &content = publishedContentRegistry();
    const auto backpack = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    ASSERT_NE(backpack, profile.assets.records().end());
    const auto backpackId = backpack->first;
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{backpackId, EquipmentSlotKind::Backpack},
        {profile.revision, "equip-fuel-ui-backpack"}).succeeded);
    ASSERT_TRUE(A::flow(*app).deploy(kFreightCrateMapId));
    ASSERT_TRUE(session.raidFuelExtractionEnabled());
    const auto &world = session.world();
    const auto &map = world.tacticalMap();
    const auto kind = RaidMapExtractionKind::EmergencyConditional;
    ASSERT_FALSE(map.extractionVisible(kind));
    clear(); A::tactical(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_EQ(bright({674, 622, 462, 16}), 0U);

    const auto &exit = world.conditionalExtractionPoint();
    ASSERT_TRUE(exit);
    const auto bounds = exit->bounds();
    const float half = world.player().size() * 0.5F;
    ASSERT_TRUE(const_cast<Player &>(world.player()).setPosition({
        bounds.position.x + bounds.size.x * 0.5F - half,
        bounds.position.y + bounds.size.y * 0.5F - half}));
    session.update({}, 0.0F);
    ASSERT_TRUE(map.extractionVisible(kind));

    std::filesystem::path destination;
    if (const auto *env = std::getenv("RAIDLINE_UI_FINAL_REVIEW_DIR")) {
        destination = std::filesystem::path(env);
        ASSERT_TRUE(destination.is_absolute());
        std::filesystem::create_directories(destination);
    }
    const auto renderAndCheck = [&](const char *name, bool tactical) {
        const auto fingerprint = profileStateFingerprint(profile);
        for (const auto size : kUiWindowSizes)
            for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
                detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
                clear();
                if (tactical) A::tactical(*app);
                else A::raid(*app);
                ASSERT_TRUE(SDL_RenderPresent(renderer));
                const float scale = size.width / 1280.0F;
                const auto pixels = [scale](SDL_Rect r) {
                    return SDL_Rect{static_cast<int>(r.x * scale), static_cast<int>(r.y * scale),
                                    static_cast<int>(r.w * scale), static_cast<int>(r.h * scale)};
                };
                EXPECT_GT(bright(pixels(tactical ? SDL_Rect{674, 622, 462, 16}
                                                  : SDL_Rect{446, 485, 388, 23})), 20U);
                EXPECT_EQ(profileStateFingerprint(profile), fingerprint);
                if (!destination.empty()) ASSERT_TRUE(IMG_SavePNG(surface,
                    (destination / (std::string("fuel-exit-") + name + "-" +
                        std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png")).string().c_str()));
            }
    };
    renderAndCheck("wait-map", true);
    ASSERT_TRUE(session.triggerDeveloperHighRisk());
    EXPECT_FALSE(session.conditionalExtractionEligible());
    renderAndCheck("missing-hud", false);

    const auto container = ProfileContainerId::compartment(backpackId, 0);
    const auto &fuel = content.item(kRaidFuelCanId);
    const auto origin = findFirstProfileFit(profile, content, container, fuel, ItemOrientation::Degrees0);
    ASSERT_TRUE(origin);
    static_cast<void>(profile.assets.create(fuel, StoredAssetLocation{container, *origin}));
    EXPECT_TRUE(session.conditionalExtractionEligible());
    renderAndCheck("ready-map", true);
    session.update({}, 0.5F);
    ASSERT_EQ(world.raidSession().extractionRoute(), RaidExtractionRoute::Fuel);
    ASSERT_GT(world.raidSession().extractionProgress(), 0.0F);
    renderAndCheck("progress-hud", false);
}

TEST(UiRaidHudTextTest, InteriorPermissionIsTranslatedBeforeCompositeLabels){
    EXPECT_EQ(uiRaidInteriorMapTitle(UiLanguage::English,"Exchange Office",false),
        "INTERIOR MAP | Exchange Office | LOCAL VIEW ONLY");
    const auto local=uiRaidInteriorMapTitle(UiLanguage::SimplifiedChinese,"Exchange Office",false);
    EXPECT_EQ(local,"建筑内部地图 | 交易所办公室 | 仅显示当前位置周围");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,local),local);
    EXPECT_EQ(uiRaidInteriorMapTitle(UiLanguage::SimplifiedChinese,"Exchange Office",true),
        "建筑内部地图 | 交易所办公室 | 永久情报");
}

TEST_F(UiRaidHudTest, FreightBreachProgressUsesExistingActionAreaAtEveryWindowSize)
{
    auto &session = A::session(*app);
    auto &profile = const_cast<ProfileState &>(session.profile());
    const auto &content = publishedContentRegistry();
    const auto pack = std::find_if(profile.assets.records().begin(), profile.assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    ASSERT_NE(pack, profile.assets.records().end());
    const auto packId = pack->first;
    ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{packId, EquipmentSlotKind::Backpack},
        {profile.revision, "equip-breach-ui-pack"}).succeeded);
    const auto container = ProfileContainerId::compartment(packId, 0);
    const auto &item = content.item(kFreightBreachWedgesId);
    const auto fit = findFirstProfileFit(profile, content, container, item, ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    const auto wedges = profile.assets.create(item, StoredAssetLocation{container, *fit});
    ASSERT_TRUE(A::flow(*app).deploy(kFreightCrateMapId));
    ASSERT_TRUE(session.profile().pendingRaid && session.profile().pendingRaid->freightBreachEnabled);
    const auto &rooms = session.profile().pendingRaid->interiors;
    const auto room = std::find_if(rooms.begin(), rooms.end(),
        [](const auto &entry) { return entry.id == kFreightCrateSpaceId; });
    ASSERT_NE(room, rooms.end());
    auto &player = const_cast<Player &>(session.world().player());
    const float half = player.size() * 0.5F;
    ASSERT_TRUE(player.setPosition({
        room->exteriorEntrance.position.x + room->exteriorEntrance.size.x * 0.5F - half,
        room->exteriorEntrance.position.y + room->exteriorEntrance.size.y * 0.5F - half}));
    GameplayInput enter{};
    enter.interactPressed = enter.interactJustPressed = true;
    session.update(enter, 0.0F);
    ASSERT_EQ(session.world().activeRaidSpaceId(), kFreightCrateSpaceId);
    ASSERT_TRUE(assetIsCarried(session.profile(), wedges));
    for (const auto &enemy : session.world().enemies())
        static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
    session.update({}, 0.0F);
    ASSERT_TRUE(player.setPosition({650.0F - half, 260.0F - half}));
    GameplayInput aim{};
    aim.aimWorldPosition = {740.0F, 260.0F};
    aim.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
    session.update(aim, 0.0F);
    ASSERT_TRUE(session.raidFreightBreach());
    aim.interactPressed = aim.interactJustPressed = true;
    session.update(aim, 0.125F);
    const auto progress = session.raidFreightBreach();
    ASSERT_TRUE(progress && progress->hasTool);
    ASSERT_GT(progress->progress, 0.0F);

    std::filesystem::path destination;
    if (const auto *output = std::getenv("RAIDLINE_FREIGHT_BREACH_QA_DIR"))
    {
        destination = output;
        std::filesystem::create_directories(destination);
    }
    for (const auto size : kUiWindowSizes)
    {
        detach();
        attach(size.width, size.height);
        A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
        clear();
        A::raid(*app);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const float scale = size.width / 1280.0F;
        EXPECT_GT(bright({static_cast<int>(446 * scale), static_cast<int>(485 * scale),
                          static_cast<int>(388 * scale), static_cast<int>(23 * scale)}), 20U);
        if (!destination.empty())
            ASSERT_TRUE(IMG_SavePNG(surface,
                (destination / ("freight-breach-" + std::to_string(size.width) + ".png")).string().c_str()));
    }
}

TEST_F(UiRaidHudTest, WorldLabelsPaintLightGlyphsAtEveryScaleAndRestoreRendererState) {
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const std::string label = "F: OPEN STORAGE";
        const auto localized = localizeUiText(language, label);
        const float width = std::clamp(A::text(*app).measure(localized, 12), 24.0F, 280.0F);
        const SDL_FRect bounds{420, 240, width, 18};
        const float scale = size.width / 1280.0F;
        const auto glyphMask = [&] {
            std::vector<bool> mask;
            for (int y = static_cast<int>(bounds.y * scale); y < static_cast<int>((bounds.y + bounds.h) * scale); ++y)
                for (int x = static_cast<int>(bounds.x * scale); x < static_cast<int>((bounds.x + bounds.w) * scale); ++x) {
                    Uint8 r{}, g{}, b{}, alpha{};
                    EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &alpha));
                    mask.push_back(r > 145 && g > 145 && b > 140);
                }
            return mask;
        };

        clear();
        // The preceding world draw can leave a dark, partly transparent pen.
        // Actual world-label glyphs must not inherit that low-contrast color.
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 32, 41, 24, 87));
        ASSERT_TRUE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
        uiFinalWorldLabel(renderer, A::text(*app), bounds.x, bounds.y, label);
        Uint8 r{}, g{}, b{}, alpha{}; SDL_BlendMode blend{};
        ASSERT_TRUE(SDL_GetRenderDrawColor(renderer, &r, &g, &b, &alpha));
        ASSERT_TRUE(SDL_GetRenderDrawBlendMode(renderer, &blend));
        EXPECT_EQ(r, 32); EXPECT_EQ(g, 41); EXPECT_EQ(b, 24); EXPECT_EQ(alpha, 87);
        EXPECT_EQ(blend, SDL_BLENDMODE_ADD);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto actual = glyphMask();

        // Independent font-renderer reference: a readable label over the same
        // dark backplate. No panel border can satisfy the glyph mask.
        clear(); ASSERT_TRUE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 17, 26, 27, 205));
        const SDL_FRect back{bounds.x - 4, bounds.y - 2, bounds.w + 8, 20};
        ASSERT_TRUE(SDL_RenderFillRect(renderer, &back));
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 222, 231, 226, 255));
        A::text(*app).renderBox(renderer, bounds, label, {12, UiTextAlign::Left, 1});
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto expected = glyphMask();
        EXPECT_GT(std::count(expected.begin(), expected.end(), true), 30);
        EXPECT_EQ(actual, expected);
    }
}

TEST_F(UiRaidHudTest, WorldLabelsAvoidAvatarWithoutRelocatingOffscreenInformation) {
    const SDL_FRect view{0, 0, 1280, 720};
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const std::string label = "HOLD F: TRIGGER";
        const auto localized = localizeUiText(language, label);
        const float width = std::clamp(A::text(*app).measure(localized, 12), 24.0F, 280.0F);
        const float scale = size.width / 1280.0F;
        const auto glyphMask = [&](const SDL_FRect &bounds) {
            std::vector<bool> mask;
            for (int y = static_cast<int>(bounds.y * scale); y < static_cast<int>((bounds.y + bounds.h) * scale); ++y)
                for (int x = static_cast<int>(bounds.x * scale); x < static_cast<int>((bounds.x + bounds.w) * scale); ++x) {
                    Uint8 r{}, g{}, b{}, alpha{};
                    EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &alpha));
                    mask.push_back(r > 145 && g > 145 && b > 140);
                }
            return mask;
        };
        for (const bool atRightEdge : {false, true}) {
            const SDL_FRect avatar{atRightEdge ? 1200.0F : 608.0F, 320, 64, 80};
            const SDL_FRect original{atRightEdge ? 1190.0F : 600.0F, 335, width, 18};
            const auto moved = uiFinalWorldLabelBounds(original, avatar, view);
            const SDL_FRect expectedBounds{atRightEdge ? avatar.x - width - 8 : avatar.x + avatar.w + 8,
                                           original.y, width, 18};
            EXPECT_FLOAT_EQ(moved.x, expectedBounds.x); EXPECT_FLOAT_EQ(moved.y, expectedBounds.y);
            EXPECT_GE(moved.x - 4, view.x); EXPECT_GE(moved.y - 2, view.y);
            EXPECT_LE(moved.x + moved.w + 4, view.x + view.w);
            EXPECT_LE(moved.y + moved.h, view.y + view.h);
            EXPECT_TRUE(moved.x + moved.w + 4 <= avatar.x || moved.x - 4 >= avatar.x + avatar.w);

            clear(); ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 20, 30, 25, 255));
            uiFinalWorldLabel(renderer, A::text(*app), original.x, original.y, label, avatar, view);
            // Production draws the actor after world labels. The full moved
            // glyphs must survive that real ordering, not merely a rect check.
            ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 62, 53, 42, 255));
            ASSERT_TRUE(SDL_RenderFillRect(renderer, &avatar));
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            const auto actual = glyphMask(expectedBounds);
            if (const char *output = std::getenv("RAIDLINE_UI_FINAL_REVIEW_DIR")) {
                std::filesystem::create_directories(output);
                const auto name = std::string("aux-world-label-") + (atRightEdge ? "edge-" : "right-") +
                    std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                ASSERT_TRUE(IMG_SavePNG(surface, (std::filesystem::path(output) / name).string().c_str()));
            }

            clear(); ASSERT_TRUE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
            ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 17, 26, 27, 205));
            const SDL_FRect back{expectedBounds.x - 4, expectedBounds.y - 2, expectedBounds.w + 8, 20};
            ASSERT_TRUE(SDL_RenderFillRect(renderer, &back));
            ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 222, 231, 226, 255));
            A::text(*app).renderBox(renderer, expectedBounds, label, {12, UiTextAlign::Left, 1});
            ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 62, 53, 42, 255));
            ASSERT_TRUE(SDL_RenderFillRect(renderer, &avatar));
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            const auto expected = glyphMask(expectedBounds);
            EXPECT_GT(std::count(expected.begin(), expected.end(), true), 30);
            EXPECT_EQ(actual, expected);
        }

        // Geometry alone can choose a label position beyond the current
        // observation cone. App may reject that candidate using the original
        // cached visibility query; the next visible side still avoids actors.
        const SDL_FRect visibleAvatar{608, 320, 64, 80};
        const SDL_FRect covered{600, 335, width, 18};
        int checkedCandidates{};
        const auto filtered = uiFinalWorldLabelBounds(covered, visibleAvatar, view,
            [&](const SDL_FRect &candidate) {
                ++checkedCandidates;
                return candidate.x + candidate.w < visibleAvatar.x;
            });
        const SDL_FRect filteredReference{visibleAvatar.x - width - 8, covered.y, width, 18};
        EXPECT_EQ(checkedCandidates, 2);
        EXPECT_FLOAT_EQ(filtered.x, filteredReference.x);
        EXPECT_FLOAT_EQ(filtered.y, filteredReference.y);
        clear(); ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 20, 30, 25, 255));
        uiFinalWorldLabel(renderer, A::text(*app), filtered.x, filtered.y, label);
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 62, 53, 42, 255));
        ASSERT_TRUE(SDL_RenderFillRect(renderer, &visibleAvatar));
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto filteredActual = glyphMask(filteredReference);
        clear(); ASSERT_TRUE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND));
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 17, 26, 27, 205));
        const SDL_FRect filteredBack{filteredReference.x - 4, filteredReference.y - 2, width + 8, 20};
        ASSERT_TRUE(SDL_RenderFillRect(renderer, &filteredBack));
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 222, 231, 226, 255));
        A::text(*app).renderBox(renderer, filteredReference, label, {12, UiTextAlign::Left, 1});
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 62, 53, 42, 255));
        ASSERT_TRUE(SDL_RenderFillRect(renderer, &visibleAvatar));
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto filteredExpected = glyphMask(filteredReference);
        EXPECT_GT(std::count(filteredExpected.begin(), filteredExpected.end(), true), 30);
        EXPECT_EQ(filteredActual, filteredExpected);
        const auto rejected = uiFinalWorldLabelBounds(covered, visibleAvatar, view,
            [](const SDL_FRect &) { return false; });
        EXPECT_FLOAT_EQ(rejected.x, covered.x); EXPECT_FLOAT_EQ(rejected.y, covered.y);

        // Even when the avatar straddles the edge, an offscreen label is not
        // clamped next to it and exposed inside the window.
        const SDL_FRect outside{1290, 335, width, 18};
        const SDL_FRect edgeAvatar{1250, 320, 64, 80};
        const auto unchanged = uiFinalWorldLabelBounds(outside, edgeAvatar, view);
        EXPECT_FLOAT_EQ(unchanged.x, outside.x); EXPECT_FLOAT_EQ(unchanged.y, outside.y);
        clear(); uiFinalWorldLabel(renderer, A::text(*app), outside.x, outside.y, label, edgeAvatar, view);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto hidden = glyphMask(view);
        EXPECT_EQ(std::count(hidden.begin(), hidden.end(), true), 0);

        // A non-overlapping label stays anchored, including translated world
        // viewports: the helper has no hard-coded screen-space origin.
        const SDL_FRect separate{20, 60, width, 18};
        const auto separateResult = uiFinalWorldLabelBounds(separate, edgeAvatar, view);
        EXPECT_FLOAT_EQ(separateResult.x, separate.x); EXPECT_FLOAT_EQ(separateResult.y, separate.y);
        const SDL_FRect shiftedView{1000, 500, 1280, 720};
        const auto shifted = uiFinalWorldLabelBounds({2190, 835, width, 18}, {2200, 820, 64, 80}, shiftedView);
        EXPECT_FLOAT_EQ(shifted.x, 2200 - width - 8); EXPECT_FLOAT_EQ(shifted.y, 835);
    }
}
