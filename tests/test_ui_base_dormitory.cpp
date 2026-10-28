#include "app.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiBaseDormitoryTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.dormitoryUi_; }
    static auto &projection(App &a) { a.refreshBaseDormitoryUi(); return a.dormitoryProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.dormitoryFeedback_; }
    static auto generation(App &a) { return a.dormitoryProjectionGeneration_; }
    static void frame(App &a, float dt = 0) { a.processEvents(); a.update(dt); a.input_.endFrame(); }
    static void draw(App &a) { a.renderBaseDormitory(); }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
    static void close(App &a) { a.gameFlow_.closeBaseFacility(); a.resetBaseDormitoryUi(); }
    static void directEvent(App &a, UiDormitoryEvent e) { a.handleBaseDormitoryEvent(e); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static void refresh(App &a) { a.refreshBaseDormitoryUi(true); a.dormitoryFeedback_.clear(); }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
};
namespace {
using A = UiBaseDormitoryTestAccess; using Control = UiDormitoryControl;
const ItemDefinitionId paper{"item.loot.toilet_paper"};
Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down; e.button.x = p.x; e.button.y = p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void windowEvent(Uint32 type) { SDL_Event e{}; e.type = type; ASSERT_TRUE(SDL_PushEvent(&e)); }
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = code; e.key.down = true; ASSERT_TRUE(SDL_PushEvent(&e));
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; ASSERT_TRUE(SDL_PushEvent(&e));
}
class UiBaseDormitoryTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    std::filesystem::path path;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-dormitory-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("dormitory-ui", content);
        p.currency = 10000; p.currentHealth = 64; p.tutorial = TutorialProgress::Complete;
        p.homeFounding.hintsDismissed = true;
        p.basePopulation.injuredResidents = 0;
        p.basePopulation.injuredByProfession.fill(0);
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Dormitory));
        attach(1280, 720);
        A::text(*app).setLanguage(UiLanguage::English);
    }
    void attach(int width, int height) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, width / 1280.0F);
    }
    void detach() {
        if (app) A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); renderer = nullptr; surface = nullptr;
    }
    void TearDown() override {
        detach(); app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void control(Control c) { auto point = center(uiDormitoryControlRect(c)); pointer(point, true); pointer(point, false); A::frame(*app); }
    void down(Control c = Control::StartRest) { pointer(center(uiDormitoryControlRect(c)), true); A::frame(*app); }
    void up(Control c = Control::StartRest) { pointer(center(uiDormitoryControlRect(c)), false); A::frame(*app); }
    AssetInstanceId create(const ItemDefinitionId &id, std::uint32_t n) {
        const auto &d = content.item(id); const auto fit = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), d, ItemOrientation::Degrees0);
        if (!fit) { ADD_FAILURE() << "fixture Stash is full"; return 0; }
        const auto result = profile().assets.create(d, StoredAssetLocation{ProfileContainerId::stash(), *fit}, n); ++profile().revision; return result;
    }
    std::uint64_t quantity(const ItemDefinitionId &id) {
        std::uint64_t result = 0; for (const auto &[n, a] : profile().assets.records()) if (a.definitionId == id) result += a.quantity; return result;
    }
    void supplies() {
        for(const auto &s:content.baseManufacturingRecipes().front().inputs)create(s.itemDefinitionId,s.quantity);
        ASSERT_TRUE(queryStartBaseManufacturing(profile(),content,{content.baseManufacturingRecipes().front().id}).canCommit);
    }
    std::vector<AssetInstanceId> fill() {
        std::vector<AssetInstanceId> ids;
        while(findFirstProfileFit(profile(),content,ProfileContainerId::stash(),content.item(alpha_content::lootCola),ItemOrientation::Degrees0))ids.push_back(create(alpha_content::lootCola,1));
        return ids;
    }
    void wheel(Vec2 p) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = -1; e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
};


TEST_F(UiBaseDormitoryTest, BrowsingIsReadOnlyAndReusesTheProjection) {
    const auto before=profileStateFingerprint(profile()); A::draw(*app); const auto generation=A::generation(*app);
    for(int i=0;i<12;++i){ A::draw(*app); A::directEvent(*app,{UiDormitoryEventKind::Move,{20,20}}); }
    EXPECT_EQ(A::generation(*app),generation); control(Control::Rations); control(Control::Rest);
    control(Control::Hour6); control(Control::Residents); EXPECT_EQ(profileStateFingerprint(profile()),before);
    EXPECT_FALSE(A::state(*app).restHours); profile().worldClock.elapsedWorldMinutes+=1;
    A::draw(*app); EXPECT_GT(A::generation(*app),generation);
}

TEST_F(UiBaseDormitoryTest, NativeRestRequiresReleaseThenClearsSelectionAndCannotRepeat) {
    const auto minutes=profile().worldClock.elapsedWorldMinutes; const auto fp=profileStateFingerprint(profile());
    A::legacy(*app,{300,550}); A::legacy(*app,{900,550}); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    control(Control::Rest); control(Control::Hour6); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    const auto arrival=A::projection(*app).rest.arrival;
    down(Control::StartRest); EXPECT_EQ(profileStateFingerprint(profile()),fp); up(Control::StartRest);
    EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,minutes+360); EXPECT_EQ(projectWorldClock(profile().worldClock),arrival);
    EXPECT_EQ(profile().currentHealth,64); EXPECT_FALSE(A::state(*app).restHours);
    const auto after=profileStateFingerprint(profile()); up(Control::StartRest); control(Control::StartRest);
    EXPECT_EQ(profileStateFingerprint(profile()),after);
}

TEST_F(UiBaseDormitoryTest, AllThreeRestPresetsKeepTheirOriginalDuration) {
    const auto seed=profile(); const std::array<Control,3> controls{Control::Hour1,Control::Hour6,Control::Hour12};
    const std::array<unsigned,3> hours{1,6,12};
    for(std::size_t i=0;i<3;++i){
        profile()=seed; A::state(*app)={}; A::refresh(*app); control(Control::Rest); control(controls[i]);
        ASSERT_EQ(A::projection(*app).restHours,hours[i]); control(Control::StartRest);
        EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,seed.worldClock.elapsedWorldMinutes+hours[i]*60);
        EXPECT_EQ(profile().currentHealth,seed.currentHealth);
    }
}

TEST_F(UiBaseDormitoryTest, MidnightPreviewAndConfirmationUseTheOriginalDailySettlement) {
    profile().worldClock.elapsedWorldMinutes=20*60; ++profile().revision;
    control(Control::Rest); control(Control::Hour6); const auto plan=A::projection(*app).rest;
    ASSERT_TRUE(plan.canCommit); EXPECT_EQ(plan.dailyCyclesCrossed,1U); EXPECT_EQ(plan.arrival.hour,2U);
    ProfileState expected=profile(); ASSERT_TRUE(::executeBaseRest(expected,content,{6},{expected.revision,"reference-rest"}).succeeded);
    control(Control::StartRest); EXPECT_EQ(profile().worldClock,expected.worldClock);
    EXPECT_EQ(profile().baseResources.pool,expected.baseResources.pool);
    EXPECT_EQ(profile().baseResources.resolvedDemandCycleCount,expected.baseResources.resolvedDemandCycleCount);
    EXPECT_EQ(profile().currentHealth,64); EXPECT_EQ(profile().baseMorale.tier,expected.baseMorale.tier);
}

TEST_F(UiBaseDormitoryTest, RestCompletesDueConstructionManufacturingAndResidentCare) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion);
    ASSERT_TRUE(profile().baseConstruction.activeProject); supplies();
    ASSERT_TRUE(A::session(*app).executeStartBaseManufacturing(content.baseManufacturingRecipes().front().id,"make-before-rest").succeeded);
    const auto output=profile().baseManufacturing.activeOrder->outputAssetId;
    profile().basePopulation.injuredResidents=1;
    profile().basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)]=1;
    create(paper,2); create(paper,2); profile().baseSupplyPolicy.assignments[paper]=BaseSupplyCategory::Medical; ++profile().revision;
    ASSERT_TRUE(A::session(*app).executeStartResidentTreatment("care-before-rest").succeeded);
    control(Control::Rest); control(Control::Hour12); control(Control::StartRest);
    EXPECT_FALSE(profile().baseConstruction.activeProject); EXPECT_EQ(profile().baseConstruction.dormitoryLevel,2U);
    EXPECT_FALSE(profile().baseManufacturing.activeOrder); EXPECT_FALSE(profile().residentMedical.activeTreatment);
    EXPECT_EQ(profile().basePopulation.injuredResidents,0U); EXPECT_EQ(profile().currentHealth,64);
    const auto *asset=profile().assets.find(output); ASSERT_NE(asset,nullptr);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(asset->location));
}

TEST_F(UiBaseDormitoryTest, SwitchingOrClosingDiscardsTheUnconfirmedRest) {
    control(Control::Rest); control(Control::Hour12); const auto before=profileStateFingerprint(profile());
    control(Control::Residents); control(Control::Rest); EXPECT_FALSE(A::state(*app).restHours);
    control(Control::Hour6); control(Control::Close); EXPECT_EQ(profileStateFingerprint(profile()),before);
    ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Dormitory)); A::draw(*app);
    EXPECT_EQ(A::state(*app).tab,UiDormitoryTab::Residents); EXPECT_FALSE(A::state(*app).restHours);
}

TEST_F(UiBaseDormitoryTest, AutoFillPreservesOccupiedJobsAndDoesNotChangeTime) {
    const auto workshop=profile().baseWorkforce.workshopWorker; profile().baseWorkforce.medicalWorker.reset(); ++profile().revision;
    const auto minutes=profile().worldClock.elapsedWorldMinutes; const auto expected=queryAutoFillBaseWorkers(profile());
    ASSERT_TRUE(expected.canCommit); down(Control::AutoFill); EXPECT_FALSE(profile().baseWorkforce.medicalWorker);
    up(Control::AutoFill); EXPECT_EQ(profile().baseWorkforce,expected.resultingState);
    EXPECT_EQ(profile().baseWorkforce.workshopWorker,workshop); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,minutes);
    const auto fp=profileStateFingerprint(profile()); control(Control::AutoFill); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}

TEST_F(UiBaseDormitoryTest, PartialAutoFillReportsRemainingVacancyWithoutInventingAWorker) {
    profile().baseWorkforce.workshopWorker.reset(); profile().baseWorkforce.medicalWorker.reset(); profile().basePopulation.ordinaryResidents=1;
    profile().basePopulation.professionResidents.fill(0);
    profile().basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::Engineering)]=1;
    ++profile().revision; ASSERT_TRUE(validateProfileState(profile(),content).valid) << validateProfileState(profile(),content).message;
    control(Control::AutoFill); ASSERT_TRUE(profile().baseWorkforce.workshopWorker);
    EXPECT_FALSE(profile().baseWorkforce.medicalWorker); EXPECT_NE(A::message(*app).find("SOME JOBS REMAIN VACANT"),std::string::npos);
    EXPECT_EQ(A::projection(*app).workforce.availableResidents,0U);
}

TEST_F(UiBaseDormitoryTest, ConstructionReservationsCannotBeReusedForAutoFill) {
    profile().baseWorkforce.workshopWorker.reset(); profile().baseWorkforce.medicalWorker.reset(); profile().basePopulation.ordinaryResidents=3;
    profile().basePopulation.professionResidents.fill(0);
    profile().basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::General)]=3;
    profile().baseConstruction.materialUnits=100; ++profile().revision;
    control(Control::StartExpansion); ASSERT_TRUE(profile().baseConstruction.activeProject);
    ASSERT_TRUE(validateProfileState(profile(),content).valid) << validateProfileState(profile(),content).message; EXPECT_EQ(A::projection(*app).workforce.availableResidents,0U);
    const auto fp=profileStateFingerprint(profile()); control(Control::AutoFill); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}

TEST_F(UiBaseDormitoryTest, ExpansionStartsOnReleaseAndCancelRefundsTheOriginalMaterial) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; A::refresh(*app);
    ASSERT_TRUE(A::projection(*app).project); const auto project=*A::projection(*app).project;
    const auto fp=profileStateFingerprint(profile()); down(Control::StartExpansion); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    up(Control::StartExpansion); ASSERT_TRUE(profile().baseConstruction.activeProject);
    EXPECT_EQ(profile().baseConstruction.materialUnits,100-project.materialCost);
    EXPECT_EQ(profile().baseConstruction.activeProject->definitionId,project.id);
    EXPECT_EQ(profile().baseConstruction.activeProject->completionWorldMinute-profile().baseConstruction.activeProject->startedWorldMinute,project.durationMinutes);
    control(Control::StartExpansion); ASSERT_TRUE(profile().baseConstruction.activeProject);
    control(Control::CancelExpansion); EXPECT_FALSE(profile().baseConstruction.activeProject); EXPECT_EQ(profile().baseConstruction.materialUnits,100U);
}

TEST_F(UiBaseDormitoryTest, FullMaterialPoolRejectsTheWholeCancellation) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion);
    ASSERT_TRUE(profile().baseConstruction.activeProject); const auto project=profile().baseConstruction.activeProject;
    profile().baseConstruction.materialUnits=content.maximumBaseConstructionMaterials(); ++profile().revision;
    A::refresh(*app); EXPECT_FALSE(uiDormitoryEnabled(Control::CancelExpansion,A::projection(*app)));
    const auto fp=profileStateFingerprint(profile()); control(Control::CancelExpansion);
    EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_EQ(profile().baseConstruction.activeProject,project);
    EXPECT_NE(A::message(*app).find("refund"),std::string::npos);
}

TEST_F(UiBaseDormitoryTest, AnotherFacilityProjectCannotBeCancelledByThisPage) {
    profile().baseConstruction.materialUnits=100; ++profile().revision;
    const auto it=std::find_if(content.baseConstructionProjects().begin(),content.baseConstructionProjects().end(),[](const auto &p){return p.target==BaseFacilityUpgradeTarget::Workshop;});
    ASSERT_NE(it,content.baseConstructionProjects().end());
    ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(it->id,"other-project").succeeded);
    const auto fp=profileStateFingerprint(profile()); control(Control::StartExpansion); control(Control::CancelExpansion);
    EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_FALSE(A::projection(*app).ownExpansion);
}

TEST_F(UiBaseDormitoryTest, MissingMaterialAndCompletedStatesCannotStartASecondProject) {
    profile().baseConstruction.materialUnits=0; ++profile().revision;
    const auto empty=profileStateFingerprint(profile()); control(Control::StartExpansion); EXPECT_EQ(profileStateFingerprint(profile()),empty);
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion);
    ASSERT_TRUE(profile().baseConstruction.activeProject);
    profile().worldClock.elapsedWorldMinutes=profile().baseConstruction.activeProject->completionWorldMinute;
    ASSERT_TRUE(applyBaseConstructionThrough(profile(),content).completed); ++profile().revision;
    A::draw(*app); EXPECT_TRUE(A::projection(*app).expansionComplete);
    const auto done=profileStateFingerprint(profile()); control(Control::StartExpansion); EXPECT_EQ(profileStateFingerprint(profile()),done);
}

TEST_F(UiBaseDormitoryTest, SaveFailureCannotAdvanceRestOrSettleDailyNeeds) {
    control(Control::Rest); control(Control::Hour12); const auto blocked=path/"blocked";
    {std::ofstream file(blocked);file<<"file";} A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile()); control(Control::StartRest); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    EXPECT_EQ(A::state(*app).restHours,12U); EXPECT_FALSE(A::message(*app).empty());
}
TEST_F(UiBaseDormitoryTest, SaveFailureCannotAssignAnyWorker) {
    profile().baseWorkforce.workshopWorker.reset(); profile().baseWorkforce.medicalWorker.reset(); ++profile().revision; const auto blocked=path/"blocked";
    {std::ofstream file(blocked);file<<"file";} A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile()); control(Control::AutoFill); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseDormitoryTest, SaveFailureCannotReserveConstructionMaterial) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; const auto blocked=path/"blocked";
    {std::ofstream file(blocked);file<<"file";} A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile()); control(Control::StartExpansion); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseDormitoryTest, SaveFailureCannotPartiallyRefundOrCancelConstruction) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion);
    ASSERT_TRUE(profile().baseConstruction.activeProject); const auto blocked=path/"blocked";
    {std::ofstream file(blocked);file<<"file";} A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile()); control(Control::CancelExpansion); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}

TEST_F(UiBaseDormitoryTest, ChangedArrivalWithoutRevisionRequiresAFreshConfirmation) {
    control(Control::Rest); control(Control::Hour6); down(Control::StartRest);
    profile().worldClock.elapsedWorldMinutes+=1; const auto fp=profileStateFingerprint(profile()); up(Control::StartRest);
    EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_NE(A::message(*app).find("PLAN CHANGED"),std::string::npos);
}
TEST_F(UiBaseDormitoryTest, ChangedAutoFillWithoutRevisionCannotAddAnUnexpectedJob) {
    profile().baseWorkforce.workshopWorker.reset(); ++profile().revision; down(Control::AutoFill);
    profile().baseWorkforce.medicalWorker.reset(); const auto fp=profileStateFingerprint(profile()); up(Control::AutoFill);
    EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_FALSE(profile().baseWorkforce.workshopWorker);
}
TEST_F(UiBaseDormitoryTest, ProjectStartingDuringPressCannotTurnStartIntoCancel) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; down(Control::StartExpansion);
    const auto id=A::projection(*app).project->id;
    ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(id,"concurrent-start").succeeded);
    const auto fp=profileStateFingerprint(profile()); up(Control::StartExpansion); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    EXPECT_TRUE(profile().baseConstruction.activeProject);
}
TEST_F(UiBaseDormitoryTest, ReplacedProjectWithSameDefinitionCannotConsumeAnOldCancelPress) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion); down(Control::CancelExpansion);
    --profile().baseConstruction.activeProject->startedWorldMinute; --profile().baseConstruction.activeProject->completionWorldMinute;
    ASSERT_TRUE(validateProfileState(profile(),content).valid) << validateProfileState(profile(),content).message;
    const auto fp=profileStateFingerprint(profile()); up(Control::CancelExpansion); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}

TEST_F(UiBaseDormitoryTest, FocusResizeScrollLanguageAndMoveOutCancelTheWholePress) {
    control(Control::Rest); control(Control::Hour6);
    for(int scenario=0;scenario<6;++scenario){
        down(Control::StartRest);
        if(scenario==0){windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); A::frame(*app); windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); A::frame(*app);}
        if(scenario==1){windowEvent(SDL_EVENT_WINDOW_RESIZED); A::frame(*app);}
        if(scenario==2){wheel(center(uiDormitoryFeedbackRect()));}
        if(scenario==3){A::text(*app).setLanguage(UiLanguage::SimplifiedChinese); A::draw(*app); A::text(*app).setLanguage(UiLanguage::English);}
        if(scenario==4){A::directEvent(*app,{UiDormitoryEventKind::Move,{10,10}}); A::directEvent(*app,{UiDormitoryEventKind::Move,center(uiDormitoryControlRect(Control::StartRest))});}
        if(scenario==5){windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE); A::frame(*app); windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app);}
        const auto fp=profileStateFingerprint(profile()); up(Control::StartRest); EXPECT_EQ(profileStateFingerprint(profile()),fp)<<scenario;
    }
}
TEST_F(UiBaseDormitoryTest, SameBatchWindowLeaveDropsQueuedDownAndUp) {
    control(Control::Rest); control(Control::Hour6); const auto fp=profileStateFingerprint(profile());
    pointer(center(uiDormitoryControlRect(Control::StartRest)),true); windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE);
    pointer(center(uiDormitoryControlRect(Control::StartRest)),false); A::frame(*app); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseDormitoryTest, PauseInventoryAndEscapeCannotConfirmAPendingRest) {
    control(Control::Rest); control(Control::Hour6); down(Control::StartRest); const auto fp=profileStateFingerprint(profile());
    A::pause(*app); A::frame(*app); A::resume(*app); up(Control::StartRest); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    down(Control::StartRest); key(SDL_SCANCODE_TAB); A::frame(*app); up(Control::StartRest);
    EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_FALSE(A::flow(*app).activeBaseFacility());
    key(SDL_SCANCODE_TAB); A::frame(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Dormitory));
    control(Control::Rest); control(Control::Hour6); down(Control::StartRest); key(SDL_SCANCODE_ESCAPE); A::frame(*app);
    up(Control::StartRest); EXPECT_EQ(profileStateFingerprint(profile()),fp); EXPECT_FALSE(A::flow(*app).activeBaseFacility());
}
TEST_F(UiBaseDormitoryTest, ReloadKeepsTheFrozenExpansionAndDoesNotRestoreTransientSelection) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; control(Control::StartExpansion);
    const auto project=profile().baseConstruction.activeProject; control(Control::Rest); control(Control::Hour6);
    A::close(*app); static_cast<void>(A::flow(*app).returnToMainMenu()); ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
    ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Dormitory)); A::draw(*app);
    EXPECT_EQ(profile().baseConstruction.activeProject,project); EXPECT_FALSE(A::state(*app).restHours);
}
TEST_F(UiBaseDormitoryTest, MenuMotionAndCloseKeepTheAcceptedWindowAimAnchor) {
    A::anchor(*app)=Vec2{320,210}; const auto before=A::flow(*app).baseWorld().weaponAimWorldPosition();
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=1100;e.motion.y=90;e.motion.xrel=400;e.motion.yrel=80;
    ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); control(Control::Close);
    ASSERT_TRUE(A::anchor(*app)); EXPECT_FLOAT_EQ(A::anchor(*app)->x,320); EXPECT_FLOAT_EQ(A::anchor(*app)->y,210);
    const auto after=A::flow(*app).baseWorld().weaponAimWorldPosition(); EXPECT_FLOAT_EQ(after.x,before.x); EXPECT_FLOAT_EQ(after.y,before.y);
}
TEST_F(UiBaseDormitoryTest, FullFeedbackRemainsReachableWithTheMouseWheel) {
    A::draw(*app); for(int i=0;i<30;++i)A::message(*app)+="A complete failure reason remains reachable.\n";
    A::draw(*app); ASSERT_GT(A::state(*app).feedbackMaximum,0); const auto fp=profileStateFingerprint(profile());
    wheel(center(uiDormitoryFeedbackRect())); EXPECT_GT(A::state(*app).feedbackScroll,0); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}

TEST_F(UiBaseDormitoryTest, OffscreenMatrixCoversResidentsExpansionRationsAndRest) {
    profile().baseConstruction.materialUnits=100; ++profile().revision; const auto seed=profile();
    const char *output=std::getenv("RAIDLINE_DORMITORY_PREVIEW_DIR"); std::filesystem::path destination;
    if(output && *output){
        destination=std::filesystem::absolute(output).lexically_normal();
        ASSERT_TRUE(destination.filename().string().starts_with("raidline-dormitory-review-"));
        const auto root=std::filesystem::path(__FILE__).parent_path().parent_path()/".codex"/"tmp";
        ASSERT_EQ(std::filesystem::weakly_canonical(destination.parent_path()),std::filesystem::weakly_canonical(root));
        std::filesystem::create_directories(destination);
    }
    constexpr const char *names[]{"ready","crowded","empty","active","refund-full","other-project","complete","rations","rest-empty","rest-preview","rest-midnight","long-reason"};
    for(int scenario=0;scenario<12;++scenario){
        profile()=seed; A::state(*app)={}; A::message(*app).clear();
        if(scenario==1){profile().basePopulation.ordinaryResidents+=8; profile().basePopulation.professionResidents[baseProfessionIndex(BaseResidentProfession::General)]+=8;}
        if(scenario==2){profile().basePopulation.ordinaryResidents=0; profile().basePopulation.professionResidents.fill(0); profile().baseWorkforce.workshopWorker.reset(); profile().baseWorkforce.medicalWorker.reset();}
        if(scenario==3 || scenario==4 || scenario==6){
            const auto p=projectUiDormitory(profile(),content,{}); ASSERT_TRUE(p.project);
            ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(p.project->id,"matrix-expansion").succeeded);
            if(scenario==4)profile().baseConstruction.materialUnits=content.maximumBaseConstructionMaterials();
            if(scenario==6){profile().worldClock.elapsedWorldMinutes=profile().baseConstruction.activeProject->completionWorldMinute; ASSERT_TRUE(applyBaseConstructionThrough(profile(),content).completed);}
        }
        if(scenario==5){const auto it=std::find_if(content.baseConstructionProjects().begin(),content.baseConstructionProjects().end(),[](const auto &p){return p.target==BaseFacilityUpgradeTarget::Workshop;}); ASSERT_NE(it,content.baseConstructionProjects().end()); ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(it->id,"matrix-other-project").succeeded);}
        if(scenario==10)profile().worldClock.elapsedWorldMinutes=20*60;
        ++profile().revision;
        for(auto size:kUiWindowSizes)for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}){
            detach();attach(size.width,size.height); A::text(*app).setLanguage(language);
            A::state(*app).rations=scenario==7;
            A::state(*app).tab=scenario>=8 && scenario<=10 ? UiDormitoryTab::Rest : UiDormitoryTab::Residents;
            A::state(*app).restHours=scenario==9 || scenario==10 ? std::optional<std::uint32_t>{6} : std::nullopt;
            A::refresh(*app); if(scenario==11)for(int i=0;i<20;++i)A::message(*app)+="A complete long failure reason. ";
            const auto fp=profileStateFingerprint(profile()); SDL_SetRenderDrawColor(renderer,10,16,18,255); SDL_RenderClear(renderer);
            A::draw(*app); SDL_RenderPresent(renderer); EXPECT_EQ(profileStateFingerprint(profile()),fp);
            if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string(names[scenario])+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));
        }
    }
}
TEST(UiDormitoryGeometryTest, HiddenControlsCannotActAcrossTabsAndCancelIsSeparate) {
    UiDormitoryState state; UiDormitoryProjection p;
    EXPECT_FALSE(hitUiDormitory(center(uiDormitoryControlRect(Control::CancelExpansion)),state,p));
    state.tab=UiDormitoryTab::Rest;
    EXPECT_FALSE(hitUiDormitory(center(uiDormitoryControlRect(Control::AutoFill)),state,p));
    EXPECT_EQ(hitUiDormitory(center(uiDormitoryControlRect(Control::StartRest)),state,p),Control::StartRest);
    EXPECT_FALSE(uiDormitoryEnabled(Control::StartRest,p));
    EXPECT_NE(uiDormitoryControlRect(Control::CancelExpansion).x,uiDormitoryControlRect(Control::StartExpansion).x);
}
} // namespace
