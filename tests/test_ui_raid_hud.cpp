#include "app.h"
#include "developer_test_range.h"
#include "raid_lifecycle.h"
#include <algorithm>
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
    static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
    static void clearPanels(App &a){a.developerWeaponPanelOpen_=false;a.developerRangePanelOpen_=false;a.developerPerformanceOverlayOpen_=false;a.tacticalMapOpen_=false;}
    static void hud(App &a){a.renderDebugText();a.renderMeleeStatus();a.renderThrowableUi();}
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
    std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};std::filesystem::path path;
    void SetUp()override{
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path=std::filesystem::temp_directory_path()/("raidline-final-hud-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));app=std::make_unique<App>();A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).startNewGame("final-hud-isolated"));
        attach(1280,720);
    }
    void attach(int w,int h){surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));ASSERT_TRUE(A::attach(*app,renderer,w/1280.0F));}
    void detach(){A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;}
    void TearDown()override{detach();app.reset();SDL_Quit();if(path.empty()||!std::filesystem::exists(path))return;const auto resolved=std::filesystem::canonical(path);ASSERT_TRUE(std::filesystem::equivalent(resolved.parent_path(),std::filesystem::temp_directory_path()));ASSERT_TRUE(resolved.filename().string().starts_with("raidline-final-hud-"));std::error_code error;std::filesystem::remove_all(resolved,error);}
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
        EXPECT_GT(bright({446,485,388,28}),50U);EXPECT_GT(bright({446,529,388,34}),50U);
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

TEST(UiRaidHudLayoutTest, GuideCardsStayInsideWindowAtItsEdges){
    for(const auto x:{-90.0F,16.0F,1200.0F})for(const auto y:{-20.0F,590.0F,900.0F}){
        const auto b=uiAuxiliaryHintRect(4,x,y,620);EXPECT_GE(b.x,16);EXPECT_GE(b.y,12);EXPECT_LE(b.x+b.w,1264);EXPECT_LE(b.y+b.h,708);
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

TEST(UiRaidHudTextTest, InteriorPermissionIsTranslatedBeforeCompositeLabels){
    EXPECT_EQ(uiRaidInteriorMapTitle(UiLanguage::English,"Exchange Office",false),
        "INTERIOR MAP | Exchange Office | LOCAL VIEW ONLY");
    const auto local=uiRaidInteriorMapTitle(UiLanguage::SimplifiedChinese,"Exchange Office",false);
    EXPECT_EQ(local,"建筑内部地图 | 交易所办公室 | 仅显示当前位置周围");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,local),local);
    EXPECT_EQ(uiRaidInteriorMapTitle(UiLanguage::SimplifiedChinese,"Exchange Office",true),
        "建筑内部地图 | 交易所办公室 | 永久情报");
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
