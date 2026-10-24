#include "app.h"
#include "ui_base_management_presentation.h"
#include "ui_base_defense_presentation.h"
#include "base_siege_warning_layout.h"
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>

struct UiBaseManagementTestAccess {
    static GameFlow &flow(App &a){return a.gameFlow_;}
    static UiTextRenderer &text(App &a){return a.uiTextRenderer_;}
    static void frame(App &a){a.processEvents();a.update(0);a.input_.endFrame();}
    static void attach(App &a,SDL_Renderer *r,float scale){a.renderer_=r;ASSERT_TRUE(a.uiTextRenderer_.initialize(r));a.uiTextRenderer_.setRasterScale(scale);}
    static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
    static void reset(App &a){
        a.baseConstructionPanelOpen_=true;a.baseConstructionPage_=BaseConstructionPage::Purchase;a.baseConstructionCatalogPage_=0;
        a.fortificationBuildUi_={};a.selectedBaseFixedFacility_.reset();a.selectedBasePlacedAssetId_.reset();
        a.baseFacilityContextMenu_.reset();a.basePlacementState_.reset();a.baseFixedFacilityPlacementState_.reset();
        a.baseDefenseResultVisible_=false;a.siegeWarningDismissed_=false;a.uiMessage_.clear();
        a.baseFinalDetailsOpen_=false;
        a.baseFinalFeedbackKey_.clear();a.baseFinalFeedbackScroll_=0;a.pointerWorldPosition_=Vec2{640,360};
    }
    static void select(App &a,BaseFacilityKind kind){a.selectedBaseFixedFacility_=kind;}
    static void owned(App &a){a.baseConstructionPage_=BaseConstructionPage::Owned;}
    static void menu(App &a,BaseFacilityKind kind,BaseFacilityContextMenuMode mode){a.baseFacilityContextMenu_=BaseFacilityContextMenu{{},kind,{1190,700},mode};}
    static void construction(App &a){a.renderBaseConstructionPanel();}
    static void kitchen(App &a){a.renderBaseKitchenWater();}
    static void queued(App &a){a.renderBaseSiegeQueuedNotice();}
    static void warning(App &a,bool dismissed=false){a.siegeWarningDismissed_=dismissed;a.renderBaseSiegeWarning();}
    static void defense(App &a,bool result=false){a.baseDefenseResultVisible_=result;a.renderBaseDefenseHud();}
    static void fortifications(App &a,bool owned=false,bool menu=false){a.fortificationBuildUi_.category=true;if(owned)a.baseConstructionPage_=BaseConstructionPage::Owned;if(menu){a.fortificationBuildUi_.menu=FortificationInstanceId{1};a.fortificationBuildUi_.menuAt={1010,454};}a.renderBaseConstructionPanel();}
    static void message(App &a,const std::string &s){a.uiMessage_=s;}
    static float &scroll(App &a){return a.baseFinalFeedbackScroll_;}
    static bool &details(App &a){return a.baseFinalDetailsOpen_;}
    static void closeBuild(App &a){a.baseConstructionPanelOpen_=false;}
    static auto &fortState(App &a){return a.fortificationBuildUi_;}
    static void fortCategory(App &a){a.fortificationBuildUi_.category=true;a.baseConstructionPage_=BaseConstructionPage::Owned;}
    static Vec2 focus(App &a,Vec2 at){
        a.baseConstructionZoomIndex_=2;a.baseBuildCamera_.activate(at,a.gameFlow_.baseWorld().worldSize(),a.baseBuildViewportWorldSize());
        return baseBuildWorldToScreen(at,a.baseWorldCameraOffset(),a.baseConstructionZoom());
    }
    static int action(App &a,Vec2 p){return a.baseManagementActionAt({p.x,p.y});}
    static void clickHandler(App &a,Vec2 p){a.handleBaseConstructionPanelClick({p.x,p.y});}
    static void inspector(App &a){a.renderBaseFacilityInspector();}
    static void placement(App &a,bool fortification) {
        const float zoom=a.baseConstructionZoom();const auto camera=a.baseWorldCameraOffset();
        const auto origin=baseBuildViewportOrigin(camera,zoom);const auto size=a.gameFlow_.baseWorld().worldSize();
        SDL_SetRenderScale(a.renderer_,zoom,zoom);
        const SDL_Rect viewport{static_cast<int>(std::lround(origin.x)),static_cast<int>(std::lround(origin.y)),static_cast<int>(std::ceil(size.x)),static_cast<int>(std::ceil(size.y))};
        SDL_SetRenderViewport(a.renderer_,&viewport);
        if(fortification) {
            auto &s=a.fortificationBuildUi_;s.category=true;s.placing=FortificationInstanceId{1};s.profileId=a.gameSession_.profile().profileId;
            const auto at=a.baseScreenToWorld({640,360});
            for(std::size_t i=0;i<4;++i)s.slots[i].footprint={{at.x-220+static_cast<float>(i)*140,at.y-20},{100,40}};
            s.hover=1;s.previews[1]=FortificationPlacementPlan{};s.previews[1]->canCommit=true;
            a.renderBaseFortificationPreview();
        } else {
            a.baseFixedFacilityPlacementState_=BaseFixedFacilityPlacementState{BaseFacilityKind::Storage,BaseFixedFacilityPlacementState::Mode::Move,true};
            a.renderBasePlacementPreview();
        }
        SDL_SetRenderViewport(a.renderer_,nullptr);SDL_SetRenderScale(a.renderer_,1,1);
    }
};

namespace {
using A=UiBaseManagementTestAccess;
Vec2 center(SDL_FRect box){return {box.x+box.w/2,box.y+box.h/2};}
class UiBaseManagementTest:public testing::Test {
protected:
    std::unique_ptr<App> app;
    std::filesystem::path path;
    SDL_Surface *surface{};SDL_Renderer *renderer{};
    const ContentRegistry &content=publishedContentRegistry();
    ProfileState &profile(){return const_cast<ProfileState&>(A::flow(*app).gameSession().profile());}
    GameSession &session(){return A::flow(*app).gameSession();}
    void SetUp()override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path=std::filesystem::temp_directory_path()/("raidline-ui-base-final-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        auto p=makeNewAlphaProfile("base-final-isolated",content);p.baseConstruction.materialUnits=100;
        ASSERT_TRUE(validateProfileState(p,content).valid)<<validateProfileState(p,content).message;
        ASSERT_TRUE(SaveRepository(path).save(p,content.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::reset(*app);
    }
    void attach(int width=1280,int height=720){
        surface=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));A::attach(*app,renderer,width/1280.0F);
    }
    void detach(){if(app && renderer)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;}
    void TearDown()override {
        detach();app.reset();SDL_Quit();if(path.empty() || !std::filesystem::exists(path))return;
        const auto actual=std::filesystem::canonical(path);
        ASSERT_TRUE(std::filesystem::equivalent(actual.parent_path(),std::filesystem::temp_directory_path()));
        ASSERT_TRUE(actual.filename().string().starts_with("raidline-ui-base-final-"));
        std::error_code ignored;std::filesystem::remove_all(actual,ignored);
    }
    void clear(){SDL_SetRenderDrawColor(renderer,10,16,18,255);SDL_RenderClear(renderer);}
    int bright(SDL_FRect box) {
        const float scale=surface->w/1280.0F;int count{};
        for(int y=std::max(0,static_cast<int>(box.y*scale));y<std::min(surface->h,static_cast<int>((box.y+box.h)*scale));++y)
            for(int x=std::max(0,static_cast<int>(box.x*scale));x<std::min(surface->w,static_cast<int>((box.x+box.w)*scale));++x) {
                Uint8 r{},g{},b{},a{};if(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a) && r>150 && g>150 && b>150)++count;
            }
        return count;
    }
    void pointer(Vec2 p,bool down) {
        SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.x=p.x;e.button.y=p.y;ASSERT_TRUE(SDL_PushEvent(&e));
    }
};
}

TEST_F(UiBaseManagementTest,CatalogUsesOriginalProjectOrderAndStableIdentitiesWithoutMutation) {
    const auto before=profileStateFingerprint(profile());const auto original=projectBaseConstructionCatalog(profile(),content);
    for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        const auto rows=projectUiBaseConstructionCards(profile(),content,true,language);ASSERT_GE(rows.size(),original.size());
        for(std::size_t i=0;i<original.size();++i) {
            EXPECT_EQ(rows[i].identity,original[i].definitionId.value());EXPECT_EQ(rows[i].enabled,original[i].canCommit);
            EXPECT_EQ(rows[i].kind,UiBaseConstructionCardKind::Project);
            EXPECT_NE(rows[i].facts.find(std::to_string(original[i].materialCost)),std::string::npos);
            EXPECT_NE(rows[i].timing.find(std::to_string(original[i].durationMinutes)),std::string::npos);
        }
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,ReserveAndItemRowsNeverInventOwnership) {
    profile().baseConstruction.facilities.at(BaseFacilityDefinitionId{"base_facility.workshop"})=BaseConstructionState::FacilityPlacement::Reserve;
    profile().baseConstruction.facilityReserveStartedWorldMinutes[BaseFacilityDefinitionId{"base_facility.workshop"}]=profile().worldClock.elapsedWorldMinutes;
    const auto before=profileStateFingerprint(profile());
    const auto rows=projectUiBaseConstructionCards(profile(),content,false,UiLanguage::SimplifiedChinese);
    const auto workshop=std::find_if(rows.begin(),rows.end(),[](const auto &row){return row.identity=="base_facility.workshop";});
    ASSERT_NE(workshop,rows.end());EXPECT_EQ(workshop->kind,UiBaseConstructionCardKind::ReserveFacility);
    EXPECT_EQ(workshop->title,"工坊");EXPECT_FALSE(workshop->firstOwnedAsset);
    for(const auto &row:rows)if(row.firstOwnedAsset) {
        const auto *asset=profile().assets.find(*row.firstOwnedAsset);ASSERT_NE(asset,nullptr);
        EXPECT_EQ(asset->definitionId.value(),row.identity);
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,KitchenProjectsActualResourcesAndRemainsInactiveForOriginalNewProfile) {
    const auto before=profileStateFingerprint(profile());const auto facts=projectUiBaseKitchenFacts(profile(),content);
    EXPECT_FALSE(facts.installed);EXPECT_EQ(facts.level,0U);EXPECT_EQ(facts.pool,profile().baseResources.pool);
    EXPECT_EQ(facts.demand,populationAdjustedDailyDemand(profile().basePopulation));
    EXPECT_EQ(facts.lastShortfall,profile().baseResources.lastShortfall);EXPECT_EQ(profileStateFingerprint(profile()),before);
    EXPECT_EQ(uiBaseMoraleTrendTitle(BaseMoraleTrend::Steady,UiLanguage::SimplifiedChinese),"平稳");
    EXPECT_EQ(uiBaseOperationTitle(BaseOperationOverviewKind::BaseWish,UiLanguage::SimplifiedChinese),"基地愿望");
    EXPECT_EQ(uiBaseRemainingTime(960,UiLanguage::SimplifiedChinese),"16小时0分");
    EXPECT_EQ(uiBaseRemainingTime(7200,UiLanguage::English),"5d 0h 0m");
}

TEST_F(UiBaseManagementTest,AllSixInspectorButtonsFitAndResolveInsideTheirDrawnPanel) {
    const auto box=uiBaseManagementInspectorRect();const auto p=projectBaseFacilityManagement(profile(),content,BaseFacilityKind::Workshop);
    A::select(*app,BaseFacilityKind::Workshop);ASSERT_LE(p.quickActions.size()+1,6U);
    for(std::size_t i=0;i<p.quickActions.size()+1;++i) {
        const SDL_FRect button{box.x+16+static_cast<float>(i%2)*248,box.y+138+static_cast<float>(i/2)*42,240,34};
        EXPECT_GE(button.y,box.y);EXPECT_LE(button.y+button.h,box.y+box.h);
        EXPECT_EQ(A::action(*app,center(button)),i<p.quickActions.size()?20200+static_cast<int>(i):20210);
    }
    EXPECT_EQ(A::action(*app,{600,560}),0);
}

TEST_F(UiBaseManagementTest,ActionResolverHasOnlyCurrentVisibleRowsAndHonorsTopMenu) {
    EXPECT_EQ(A::action(*app,{400,620}),20010);A::owned(*app);
    const auto rows=projectUiBaseConstructionCards(profile(),content,false,UiLanguage::English);
    if(rows.empty())EXPECT_EQ(A::action(*app,{400,620}),0);
    A::menu(*app,BaseFacilityKind::Storage,BaseFacilityContextMenuMode::ResourceFlowArea);
    EXPECT_EQ(A::action(*app,{100,620}),0);
    const auto p=projectBaseFacilityManagement(profile(),content,BaseFacilityKind::Storage);
    EXPECT_FALSE(p.quickActions.empty());
}

TEST_F(UiBaseManagementTest,RejectedConstructionAndSaveFailureKeepExactAssetsCurrencyAndRevision) {
    const auto &id=content.baseConstructionProjects().front().id;
    profile().baseConstruction.materialUnits=0;const auto rejected=profileStateFingerprint(profile());
    const auto rows=projectUiBaseConstructionCards(profile(),content,true,UiLanguage::English);ASSERT_FALSE(rows.empty());EXPECT_FALSE(rows.front().enabled);
    A::clickHandler(*app,{400,620});EXPECT_EQ(profileStateFingerprint(profile()),rejected);
    profile().baseConstruction.materialUnits=100;
    const auto plan=queryStartBaseConstruction(profile(),content,{id});ASSERT_TRUE(plan.canCommit)<<plan.message;
    ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));const auto before=profileStateFingerprint(profile());
    A::clickHandler(*app,{400,620});EXPECT_EQ(profileStateFingerprint(profile()),before);
    EXPECT_FALSE(profile().baseConstruction.activeProject);
    ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));
    A::clickHandler(*app,{400,620});ASSERT_TRUE(profile().baseConstruction.activeProject);
    EXPECT_EQ(profile().baseConstruction.activeProject->definitionId,id);
}

TEST(UiBaseDefensePresentationTest,FrozenWaveFactsRetainCountsDirectionAndCountdown) {
    BaseDefenseSnapshot state;state.currentWave=0;state.elapsedSeconds=8.1F;state.breachLimit=6;
    state.wavePlans={BaseDefenseWaveSnapshot{10,{1000,100},{},{},{11,12}},BaseDefenseWaveSnapshot{20,{100,1000},{},{},{13}}};
    state.killedIds={11};state.breachedIds={12};
    const auto en=projectUiBaseDefenseFacts(state,{100,100},UiLanguage::English);
    EXPECT_EQ(en.total,3U);EXPECT_EQ(en.processed,2U);EXPECT_NE(en.progress.find("1/2"),std::string::npos);
    EXPECT_NE(en.breach.find("1/6"),std::string::npos);EXPECT_NE(en.nextWave.find("2s"),std::string::npos);
    EXPECT_NE(en.approaches.find("East / South"),std::string::npos);
    const auto zh=projectUiBaseDefenseFacts(state,{100,100},UiLanguage::SimplifiedChinese);
    EXPECT_EQ(zh.title,"基地防守");EXPECT_NE(zh.approaches.find("东 / 南"),std::string::npos);
}

TEST(UiBaseDefensePresentationTest,NoResultDoesNotInventDefeatAndActualLossCountsStayVisible) {
    BaseSiegeState state;auto noResult=projectUiBaseDefenseResultFacts(state,UiLanguage::SimplifiedChinese);
    EXPECT_EQ(noResult.title,"暂无防守结果");EXPECT_FALSE(noResult.defended);
    state.lastOutcome=BaseSiegeOutcome::SoftFailure;state.lastSecuritySpent=17;state.lastPopulationLost=1;
    const auto result=projectUiBaseDefenseResultFacts(state,UiLanguage::SimplifiedChinese);
    EXPECT_FALSE(result.defended);EXPECT_NE(result.losses.find("17"),std::string::npos);EXPECT_NE(result.losses.find("1"),std::string::npos);
    EXPECT_NE(result.retained.find("不会返还"),std::string::npos);
    state.lastOutcome=BaseSiegeOutcome::Defended;const auto win=projectUiBaseDefenseResultFacts(state,UiLanguage::English);
    EXPECT_TRUE(win.defended);EXPECT_NE(win.summary.find("7 safe days"),std::string::npos);
}

TEST_F(UiBaseManagementTest,LongFeedbackScrollActuallyPaintsTheEndAndNeverMutatesProfile) {
    attach();A::text(*app).setLanguage(UiLanguage::English);const auto before=profileStateFingerprint(profile());
    std::string value;for(int i=0;i<32;++i)value+="Complete original save failure reason line "+std::to_string(i)+".\n";
    value+="END OF ORIGINAL REASON";
    clear();float offset=0;renderUiBaseManagementFeedback(renderer,A::text(*app),{100,100,580,84},value,offset);SDL_RenderPresent(renderer);
    EXPECT_EQ(offset,0);EXPECT_GT(bright({100,100,562,84}),20);
    std::uint64_t startHash{};for(int y=100;y<184;++y)for(int x=100;x<662;++x){Uint8 r{},g{},b{},a{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);startHash=startHash*131+r+g+b;}
    clear();offset=10000;renderUiBaseManagementFeedback(renderer,A::text(*app),{100,100,580,84},value,offset);SDL_RenderPresent(renderer);
    EXPECT_GT(offset,0);EXPECT_LT(offset,10000);EXPECT_GT(bright({100,100,562,84}),20);
    std::uint64_t endHash{};for(int y=100;y<184;++y)for(int x=100;x<662;++x){Uint8 r{},g{},b{},a{};SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);endHash=endHash*131+r+g+b;}
    EXPECT_NE(startHash,endHash);EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,InspectorActuallyPaintsThirdRowAndKitchenPaintsAllResourcesAtEverySize) {
    for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        detach();attach(size.width,size.height);A::text(*app).setLanguage(language);A::reset(*app);A::select(*app,BaseFacilityKind::Workshop);
        clear();A::inspector(*app);SDL_RenderPresent(renderer);
        const auto p=projectBaseFacilityManagement(profile(),content,BaseFacilityKind::Workshop);const auto box=uiBaseManagementInspectorRect();
        ASSERT_GE(p.quickActions.size(),4U);
        EXPECT_GT(bright({box.x+16,box.y+138+42,240,34}),10);
        if(p.quickActions.size()>=4)EXPECT_GT(bright({box.x+16,box.y+138+84,240,34}),8);
        clear();A::kitchen(*app);SDL_RenderPresent(renderer);
        for(int i=0;i<4;++i)EXPECT_GT(bright({96+static_cast<float>(i%2)*288,265+static_cast<float>(i/2)*140,230,30}),10);
    }
}

TEST_F(UiBaseManagementTest,ActualMajorSurfacesPaintAtAllPublishedSizesAndLanguages) {
    const auto original=profile();
    std::filesystem::path destination;const char *output=std::getenv("RAIDLINE_UI_FINAL_BASE_REVIEW_DIR");
    if(output && *output) {
        destination=std::filesystem::path{output};ASSERT_TRUE(std::filesystem::equivalent(destination.parent_path(),std::filesystem::current_path()));
        ASSERT_TRUE(destination.filename().string().starts_with("qa-ui-final-base-"));ASSERT_FALSE(std::filesystem::exists(destination));ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    constexpr std::array names{"construction","owned-empty","workshop-inspector","resident-inspector","facility-menu","resident-menu","worker-menu","resource-menu",
        "fortification-purchase","fortification-repair-menu","facility-placement","fortification-placement","kitchen-water","siege-queued","siege-warning","siege-banner","defense-active","defense-success","defense-failure","long-feedback","siege-save-failure","daily-save-failure"};
    for(const auto size:kUiWindowSizes)for(const auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        detach();attach(size.width,size.height);A::text(*app).setLanguage(language);
        for(std::size_t scenario=0;scenario<names.size();++scenario) {
            SCOPED_TRACE(std::string(names[scenario])+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language));
            profile()=original;A::reset(*app);clear();
            if(scenario==1)A::owned(*app);
            if(scenario==2)A::select(*app,BaseFacilityKind::Workshop);
            if(scenario==3)A::select(*app,BaseFacilityKind::Dormitory);
            if(scenario>=4 && scenario<=7) {
                const auto mode=scenario==4?BaseFacilityContextMenuMode::Facility:scenario==5?BaseFacilityContextMenuMode::ResidentArea:
                    scenario==6?BaseFacilityContextMenuMode::WorkforceStation:BaseFacilityContextMenuMode::ResourceFlowArea;
                A::menu(*app,scenario==5?BaseFacilityKind::Dormitory:scenario==7?BaseFacilityKind::Storage:BaseFacilityKind::Workshop,mode);
            }
            if(scenario==9 || scenario==11) {
                profile().baseFortifications.instances.emplace(FortificationInstanceId{1},FortificationRecord{kWoodBarricadeDefinition,61,{}});profile().baseFortifications.nextInstanceId=2;
            }
            if((scenario>=13 && scenario<=15) || scenario==20) {
                profile().baseSiege.raidThreatUnits=100;profile().baseSiege.safeUntilWorldMinute=scenario==13?profile().worldClock.elapsedWorldMinutes+960:0;
                profile().baseSiege.warningActive=scenario!=13;profile().baseSiege.warningRemainingSeconds=179;profile().baseSiege.siegeSequence=1;
            }
            if(scenario==16) {
                BaseDefenseSnapshot s;s.breachLimit=6;s.wavePlans={BaseDefenseWaveSnapshot{5,{100,100},{},{},{1,2,3}}};s.killedIds={1};s.breachedIds={2};profile().activeBaseDefense=s;
            }
            if(scenario==17 || scenario==18) {
                profile().baseSiege.lastOutcome=scenario==17?BaseSiegeOutcome::Defended:BaseSiegeOutcome::SoftFailure;
                profile().baseSiege.lastSecuritySpent=17;profile().baseSiege.lastPopulationLost=scenario==18?1:0;
            }
            if(scenario==19) {std::string reason;for(int i=0;i<24;++i)reason+="Original save failure detail "+std::to_string(i)+"; retry keeps the current accepted profile.\n";A::message(*app,reason);A::details(*app)=true;}
            if(scenario==20) {
                std::string reason;
                for(int line=0;line<20;++line)reason+=language==UiLanguage::English?
                    "Original save failure cause "+std::to_string(line)+": keep the accepted profile and retry.\n":
                    "原始保存失败原因第"+std::to_string(line)+"行：保留当前档案，可在原入口重试。\n";
                A::message(*app,reason);
            }
            if(scenario==21) {
                session().advanceBaseWorldClock(1);ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));
                ASSERT_FALSE(session().checkpointWorldClock());ASSERT_TRUE(session().baseDailySaveBlocked());
            }
            const auto fingerprint=profileStateFingerprint(profile());
            if(scenario<8 || scenario==19)A::construction(*app);
            else if(scenario==8 || scenario==9)A::fortifications(*app,scenario==9,scenario==9);
            else if(scenario==10 || scenario==11)A::placement(*app,scenario==11);
            else if(scenario==12)A::kitchen(*app);
            else if(scenario==13)A::queued(*app);
            else if(scenario==14 || scenario==15 || scenario==20)A::warning(*app,scenario==15);
            else A::defense(*app,scenario>=17);
            SDL_RenderPresent(renderer);EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
            SDL_FRect label{88,574,260,22};
            if(scenario==10 || scenario==11)label={200,200,880,270};
            else if(scenario==12)label={80,94,1100,40};
            else if(scenario==13)label={344,85,592,38};
            else if(scenario==14)label={356,211,526,32};
            else if(scenario==15)label=base_siege_warning_layout::banner;
            else if(scenario==16)label={345,82,590,24};
            else if(scenario>=17 && scenario<=18)label={354,250,572,32};
            else if(scenario==20)label={356,211,526,32};
            else if(scenario==21)label={992,197,254,32};
            EXPECT_GT(bright(label),10)<<"actual title/status glyphs must paint";
            if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string(names[scenario])+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));
            if(scenario==21) {
                ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));ASSERT_TRUE(session().retryBaseDailySave());
                EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
            }
        }
    }
    profile()=original;
}

TEST_F(UiBaseManagementTest,DailySaveFailureRendersOriginalReasonAndRetryWithoutChangingAcceptedProfile) {
    attach();session().advanceBaseWorldClock(1);ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));
    ASSERT_FALSE(session().checkpointWorldClock());ASSERT_TRUE(session().baseDailySaveBlocked());
    const auto before=profileStateFingerprint(profile());clear();A::defense(*app);SDL_RenderPresent(renderer);
    EXPECT_GT(bright({992,197,254,32}),10);EXPECT_GT(bright({344,220,592,120}),10);EXPECT_EQ(profileStateFingerprint(profile()),before);
    ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));ASSERT_TRUE(session().retryBaseDailySave());EXPECT_FALSE(session().baseDailySaveBlocked());
    EXPECT_EQ(profileStateFingerprint(profile()),before);const auto saved=SaveRepository(path).load(content);ASSERT_TRUE(saved.profile);EXPECT_EQ(profileStateFingerprint(*saved.profile),before);
}

TEST_F(UiBaseManagementTest,CollapsedDetailsRestoreCentralFortificationPlacementAndExpandedDetailsBlockIt) {
    ASSERT_TRUE(session().executeBaseFortification(BuildFortificationCommand{kWoodBarricadeDefinition}).succeeded);
    A::fortCategory(*app);A::clickHandler(*app,{400,620});A::frame(*app);
    ASSERT_TRUE(A::fortState(*app).placing);ASSERT_FALSE(A::details(*app));
    std::optional<Vec2> screen;
    for(const auto &slot:A::fortState(*app).slots) {
        const auto plan=session().queryBaseFortificationPlacement(A::flow(*app).baseWorld(),{{1},slot.key});
        if(!plan.canCommit)continue;
        const auto box=slot.footprint;
        const auto point=A::focus(*app,{box.position.x+box.size.x/2,box.position.y+box.size.y/2});
        if(uiContains(uiBaseManagementExplanationRect(),point)){screen=point;break;}
    }
    ASSERT_TRUE(screen)<<"The original centered fixed defense slot must remain usable";
    const auto move=[&](Vec2 point){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=point.x;e.motion.y=point.y;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);};
    const auto click=[&](Vec2 point){pointer(point,true);pointer(point,false);A::frame(*app);};
    move(*screen);ASSERT_TRUE(A::fortState(*app).hover);
    const auto before=profileStateFingerprint(profile());
    click(center(uiBaseManagementDetailsButtonRect()));ASSERT_TRUE(A::details(*app));
    move(*screen);EXPECT_FALSE(A::fortState(*app).hover);
    click(*screen);EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_TRUE(A::fortState(*app).placing);
    click(center(uiBaseManagementDetailsButtonRect()));ASSERT_FALSE(A::details(*app));
    move(*screen);ASSERT_TRUE(A::fortState(*app).hover);
    click(*screen);EXPECT_TRUE(profile().baseFortifications.instances.at(FortificationInstanceId{1}).slot);
    EXPECT_FALSE(A::fortState(*app).placing);
}

TEST_F(UiBaseManagementTest,DailySaveProtectionTakesInputPriorityOverWarningWithoutResolvingOrAdvancingIt) {
    A::closeBuild(*app);A::frame(*app);
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());A::frame(*app);
    ASSERT_TRUE(profile().baseSiege.warningActive);
    session().advanceBaseWorldClock(1);ASSERT_TRUE(std::filesystem::create_directory(path/"profile.tmp.json"));
    ASSERT_FALSE(session().checkpointWorldClock());ASSERT_TRUE(session().baseDailySaveBlocked());
    const auto before=profileStateFingerprint(profile());const auto sequence=profile().baseSiege.siegeSequence;
    const auto remaining=profile().baseSiege.warningRemainingSeconds;
    attach();clear();A::warning(*app);SDL_RenderPresent(renderer);
    EXPECT_EQ(bright({356,211,526,32}),0)<<"save protection owns the foreground instead of a siege choice";
    clear();A::defense(*app);SDL_RenderPresent(renderer);EXPECT_GT(bright({992,197,254,32}),10);
    const auto retry=[&]{pointer({1119,213},true);pointer({1119,213},false);A::frame(*app);};
    retry();EXPECT_TRUE(session().baseDailySaveBlocked());EXPECT_EQ(profileStateFingerprint(profile()),before);
    ASSERT_TRUE(std::filesystem::remove(path/"profile.tmp.json"));retry();EXPECT_FALSE(session().baseDailySaveBlocked());
    EXPECT_TRUE(profile().baseSiege.warningActive);EXPECT_FALSE(session().baseDefenseActive());
    EXPECT_EQ(profile().baseSiege.siegeSequence,sequence);EXPECT_EQ(profile().baseSiege.warningRemainingSeconds,remaining);
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,WorldFacilityNamesStayShortWithoutChangingTheExistingPageTitles) {
    constexpr std::array kinds{BaseFacilityKind::Storage,BaseFacilityKind::Supply,
        BaseFacilityKind::Allocation,BaseFacilityKind::Medical,BaseFacilityKind::Dormitory,
        BaseFacilityKind::KitchenWater,BaseFacilityKind::Workshop,BaseFacilityKind::RaidGate};
    constexpr std::array chinese{"仓库","供应","分配","医疗","宿舍","厨房","工坊","出击"};
    constexpr std::array english{"Storage","Supply","Needs","Medical","Dorm","Kitchen","Workshop","Deploy"};
    const auto before=profileStateFingerprint(profile());
    for(std::size_t i=0;i<kinds.size();++i) {
        EXPECT_EQ(uiBaseFacilityShortName(kinds[i],UiLanguage::SimplifiedChinese),chinese[i]);
        EXPECT_EQ(uiBaseFacilityShortName(kinds[i],UiLanguage::English),english[i]);
    }
    EXPECT_EQ(uiBaseFacilityTitle(BaseFacilityKind::Supply,UiLanguage::SimplifiedChinese),"供应与回收");
    EXPECT_EQ(uiBaseFacilityTitle(BaseFacilityKind::Allocation,UiLanguage::English),"Resources & needs");
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,WorldGlyphsKeepDistinctSilhouettesAndNonColorStatusCuesAtEveryWindowSize) {
    constexpr std::array kinds{BaseFacilityWorkSocketKind::StorageHandling,
        BaseFacilityWorkSocketKind::MedicalBed,BaseFacilityWorkSocketKind::DormitoryBunk,
        BaseFacilityWorkSocketKind::KitchenProcessing,BaseFacilityWorkSocketKind::WorkshopBench};
    constexpr std::array facilities{BaseFacilityKind::Storage,BaseFacilityKind::Medical,
        BaseFacilityKind::Dormitory,BaseFacilityKind::KitchenWater,BaseFacilityKind::Workshop};
    constexpr std::array states{BaseFacilityWorldServiceStatus::Ready,BaseFacilityWorldServiceStatus::Working,
        BaseFacilityWorldServiceStatus::OutputReady,BaseFacilityWorldServiceStatus::NeedsStaff,
        BaseFacilityWorldServiceStatus::Blocked};
    const auto before=profileStateFingerprint(profile());
    for(const auto size:kUiWindowSizes) {
        attach(size.width,size.height);clear();
        SDL_SetRenderDrawColor(renderer,17,83,29,91);SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);
        const SDL_Rect clip{0,0,1280,720};ASSERT_TRUE(SDL_SetRenderClipRect(renderer,&clip));
        SDL_Rect viewportBefore{};SDL_GetRenderViewport(renderer,&viewportBefore);
        float scaleX{},scaleY{};SDL_GetRenderScale(renderer,&scaleX,&scaleY);
        for(std::size_t i=0;i<kinds.size();++i) {
            BaseFacilityWorldServiceProjection service;service.facility=facilities[i];service.status=states[i];
            renderUiBaseEntranceGlyph(renderer,{100+140.0F*i,110,24,28},service);
            BaseFacilityWorkSocketProjection socket{kinds[i],{{100+140.0F*i,260},{112,52}},{156+140.0F*i,286}};
            service.status=BaseFacilityWorldServiceStatus::Ready;
            renderUiBaseWorkSocketGlyph(renderer,socket,service);
            socket.kind=BaseFacilityWorkSocketKind::WorkshopBench;socket.bounds.position.y=420;
            service.facility=BaseFacilityKind::Workshop;service.status=states[i];
            renderUiBaseWorkSocketGlyph(renderer,socket,service);
        }
        Uint8 r{},g{},b{},a{};SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);
        EXPECT_EQ(r,17);EXPECT_EQ(g,83);EXPECT_EQ(b,29);EXPECT_EQ(a,91);
        SDL_BlendMode blend{};SDL_GetRenderDrawBlendMode(renderer,&blend);EXPECT_EQ(blend,SDL_BLENDMODE_ADD);
        SDL_Rect viewportAfter{},clipAfter{};SDL_GetRenderViewport(renderer,&viewportAfter);SDL_GetRenderClipRect(renderer,&clipAfter);
        EXPECT_EQ(viewportAfter.x,viewportBefore.x);EXPECT_EQ(viewportAfter.y,viewportBefore.y);
        EXPECT_EQ(viewportAfter.w,viewportBefore.w);EXPECT_EQ(viewportAfter.h,viewportBefore.h);
        EXPECT_TRUE(SDL_RenderClipEnabled(renderer));EXPECT_EQ(clipAfter.x,clip.x);EXPECT_EQ(clipAfter.y,clip.y);
        EXPECT_EQ(clipAfter.w,clip.w);EXPECT_EQ(clipAfter.h,clip.h);
        float afterX{},afterY{};SDL_GetRenderScale(renderer,&afterX,&afterY);
        EXPECT_FLOAT_EQ(afterX,scaleX);EXPECT_FLOAT_EQ(afterY,scaleY);ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto mask=[&](SDL_FRect area) {
            std::vector<bool> result;const float scale=size.width/1280.0F;
            for(int y=static_cast<int>(std::floor(area.y*scale));y<static_cast<int>(std::ceil((area.y+area.h)*scale));++y)
                for(int x=static_cast<int>(std::floor(area.x*scale));x<static_cast<int>(std::ceil((area.x+area.w)*scale));++x) {
                    Uint8 pr{},pg{},pb{},pa{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&pr,&pg,&pb,&pa));
                    result.push_back(pr!=10||pg!=16||pb!=18);
                }
            return result;
        };
        std::set<std::vector<bool>> silhouettes,statusShapes;
        for(std::size_t i=0;i<kinds.size();++i) {
            const float x=100+140.0F*i;
            const auto entrance=mask({x,110,24,28});EXPECT_GT(std::count(entrance.begin(),entrance.end(),true),20);
            const auto shape=mask({x,260,112,52});EXPECT_GT(std::count(shape.begin(),shape.end(),true),40);silhouettes.insert(shape);
            statusShapes.insert(mask({x,420,112,52}));
            const auto outer=mask({x-4,106,32,36});
            EXPECT_EQ(std::count(outer.begin(),outer.end(),true),std::count(entrance.begin(),entrance.end(),true))
                <<"entrance glyph must remain inside its visual box";
        }
        EXPECT_EQ(silhouettes.size(),5U)<<"boxes, beds, bunks, pot and workbench must not collapse to one marker";
        EXPECT_EQ(statusShapes.size(),5U)<<"state must still differ when all non-background colors become the same mask";
        if(const char *output=std::getenv("RAIDLINE_UI_BASE_DECLUTTER_GLYPH_DIR")) {
            const std::filesystem::path destination{output};ASSERT_TRUE(std::filesystem::create_directories(destination)||std::filesystem::exists(destination));
            ASSERT_TRUE(IMG_SavePNG(surface,(destination/("base-world-glyphs-"+std::to_string(size.width)+".png")).string().c_str()));
        }
        detach();
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}

TEST_F(UiBaseManagementTest,WorldGlyphsConsumeActualFacilityFactsWithoutMovingAccessOrWorkGeometry) {
    attach();const auto before=profileStateFingerprint(profile());const auto &world=A::flow(*app).baseWorld();
    const auto geometry=[&] {
        std::vector<float> values;
        for(const auto &facility:world.facilities()) {
            const auto access=baseFacilityAccessGeometry(facility);
            values.insert(values.end(),{facility.bounds.position.x,facility.bounds.position.y,facility.bounds.size.x,facility.bounds.size.y,
                access.entrancePoint.x,access.entrancePoint.y,access.workZone.position.x,access.workZone.position.y,access.workZone.size.x,access.workZone.size.y});
            if(const auto socket=baseFacilityWorkSocket(facility))values.insert(values.end(),{
                socket->bounds.position.x,socket->bounds.position.y,socket->bounds.size.x,socket->bounds.size.y,socket->interactionPoint.x,socket->interactionPoint.y});
        }
        return values;
    };
    const auto originalGeometry=geometry();const auto originalTarget=world.interactableFacility();
    const auto position=world.playerPosition();std::size_t rendered{};
    for(const auto &facility:world.facilities()) {
        if(!facility.active)continue;
        const auto access=baseFacilityAccessGeometry(facility);
        const SDL_Rect viewport{static_cast<int>(std::lround(640-access.entrancePoint.x)),
            static_cast<int>(std::lround(360-access.entrancePoint.y)),static_cast<int>(world.worldSize().x),static_cast<int>(world.worldSize().y)};
        clear();ASSERT_TRUE(SDL_SetRenderViewport(renderer,&viewport));
        const auto service=projectBaseFacilityWorldService(profile(),content,facility.kind);
        renderUiBaseEntranceGlyph(renderer,{access.entrancePoint.x-12,access.entrancePoint.y-14,24,28},service);
        if(const auto socket=baseFacilityWorkSocket(facility)) {
            renderUiBaseWorkSocketGlyph(renderer,*socket,service,
                projectBaseFacilityWorkerWorldStatus(profile(),content,facility.kind),
                projectBaseResidentWorldStatus(profile(),facility.kind),
                projectBaseResourceFlowWorldStatus(profile(),content,facility.kind));
        }
        ASSERT_TRUE(SDL_RenderPresent(renderer));++rendered;
        EXPECT_EQ(geometry(),originalGeometry);EXPECT_EQ(world.interactableFacility(),originalTarget);
        EXPECT_FLOAT_EQ(world.playerPosition().x,position.x);EXPECT_FLOAT_EQ(world.playerPosition().y,position.y);
        EXPECT_EQ(profileStateFingerprint(profile()),before);
    }
    EXPECT_GE(rendered,7U);SDL_SetRenderViewport(renderer,nullptr);
}

TEST_F(UiBaseManagementTest,WorkPointGlyphWarningsUseOnlyTheMatchingOriginalFactProjections) {
    attach();const auto before=profileStateFingerprint(profile());
    BaseFacilityWorldServiceProjection service;service.facility=BaseFacilityKind::Workshop;
    BaseFacilityWorkSocketProjection socket{BaseFacilityWorkSocketKind::WorkshopBench,{{100,100},{112,52}},{156,126}};
    const auto paint=[&](const std::optional<BaseFacilityWorkerWorldProjection> &worker,
        const std::optional<BaseResidentWorldProjection> &residents,
        const std::optional<BaseResourceFlowWorldProjection> &flow) {
        clear();renderUiBaseWorkSocketGlyph(renderer,socket,service,worker,residents,flow);
        EXPECT_TRUE(SDL_RenderPresent(renderer));std::vector<bool> result;
        for(int y=100;y<152;++y)for(int x=100;x<212;++x) {
            Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));
            result.push_back(r!=10||g!=16||b!=18);
        }
        return result;
    };
    const auto personMask=[](const std::vector<bool> &image) {
        std::vector<bool> result;
        // Right of the bed/table body and below its status badge.
        for(int y=122;y<147;++y)for(int x=174;x<188;++x)
            result.push_back(image[static_cast<std::size_t>((y-100)*112+x-100)]);
        return result;
    };
    const auto outline=paint({},{},{});
    const auto noPerson=personMask(outline);EXPECT_EQ(std::count(noPerson.begin(),noPerson.end(),true),0);
    BaseFacilityWorkerWorldProjection worker;worker.facility=service.facility;worker.workSocket=socket.kind;
    worker.status=BaseFacilityWorkerWorldStatus::Missing;const auto missing=paint(worker,{},{});
    EXPECT_NE(missing,outline)<<"missing staff adds a geometric warning, not only a different color";
    const auto missingPerson=personMask(missing);EXPECT_GT(std::count(missingPerson.begin(),missingPerson.end(),true),0);
    worker.facility=BaseFacilityKind::Storage;EXPECT_EQ(paint(worker,{},{}),outline);
    worker.facility=service.facility;worker.workSocket=BaseFacilityWorkSocketKind::MedicalBed;
    EXPECT_EQ(paint(worker,{},{}),outline);worker.workSocket=socket.kind;
    worker.facility=service.facility;worker.status=BaseFacilityWorkerWorldStatus::Working;
    const auto occupied=paint(worker,{},{});
    EXPECT_GT(std::count(occupied.begin(),occupied.end(),true),std::count(outline.begin(),outline.end(),true));
    const auto occupiedPerson=personMask(occupied);
    EXPECT_GT(std::count(occupiedPerson.begin(),occupiedPerson.end(),true),std::count(missingPerson.begin(),missingPerson.end(),true));
    service.facility=BaseFacilityKind::Dormitory;socket.kind=BaseFacilityWorkSocketKind::DormitoryBunk;
    BaseResidentWorldProjection residents;residents.facility=service.facility;residents.workSocket=socket.kind;
    const auto emptyBeds=paint({},residents,{});residents.residents=2;residents.status=BaseResidentWorldStatus::Stable;
    const auto usedBeds=paint({},residents,{});EXPECT_NE(emptyBeds,usedBeds);
    EXPECT_NE(personMask(emptyBeds),personMask(usedBeds));
    residents.residents=3;EXPECT_EQ(personMask(paint({},residents,{})),personMask(usedBeds))
        <<"resident presence is one figure, not a count of people or workers";
    residents.facility=BaseFacilityKind::Medical;EXPECT_EQ(personMask(paint({},residents,{})),noPerson);
    residents.facility=service.facility;
    residents.status=BaseResidentWorldStatus::Overcrowded;EXPECT_NE(paint({},residents,{}),usedBeds);
    service.facility=BaseFacilityKind::Storage;socket.kind=BaseFacilityWorkSocketKind::StorageHandling;
    BaseResourceFlowWorldProjection flow;flow.facility=service.facility;flow.workSocket=socket.kind;
    const auto emptyBoxes=paint({},{},flow);flow.status=BaseResourceFlowWorldStatus::Prepared;
    const auto prepared=paint({},{},flow);EXPECT_NE(emptyBoxes,prepared);
    EXPECT_EQ(personMask(prepared),noPerson)<<"resource availability must not invent personnel";
    flow.status=BaseResourceFlowWorldStatus::Shortage;EXPECT_NE(paint({},{},flow),prepared);
    service.status=BaseFacilityWorldServiceStatus::Blocked;
    const auto blocked=paint({},{},{});flow.status=BaseResourceFlowWorldStatus::Prepared;
    EXPECT_EQ(paint({},{},flow),blocked)<<"a resource marker must not hide the service's blocked state";
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
