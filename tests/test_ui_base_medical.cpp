#include "app.h"
#include "ui_app_persistence_wait.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiBaseMedicalTestAccess {
    static void bindWindow(App &a,SDL_Window *window){a.window_=window;}
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.medicalUi_; }
    static auto &projection(App &a) { a.refreshBaseMedicalUi(); return a.medicalProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.medicalFeedback_; }
    static auto generation(App &a) { return a.medicalProjectionGeneration_; }
    static void settle(App &a) {
        ASSERT_TRUE(raidline::test::waitForUiAppPersistence(a.gameSession_,
            [&] { return a.profileUiBusy() || static_cast<bool>(a.pendingInventoryBarrierAction_); },
            [&] { a.processEvents(); a.update(0); a.syncRaidPointerCapture(); a.pendingRelativeAimMotion_={}; a.input_.endFrame(); })) << a.gameSession_.persistenceMessage();
    }
    static void frame(App &a, float dt = 0) { a.processEvents(); a.update(dt); a.syncRaidPointerCapture(); a.pendingRelativeAimMotion_={}; a.input_.endFrame(); settle(a); }
    static void draw(App &a) { a.renderBaseMedicalService(); }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
    static void close(App &a) { a.gameFlow_.closeBaseFacility(); a.resetBaseMedicalUi(); }
    static void directEvent(App &a, UiBaseMedicalEvent e) { a.handleBaseMedicalEvent(e);  settle(a); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static void refresh(App &a) { a.refreshBaseMedicalUi(true); a.medicalFeedback_.clear(); }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
};
namespace {
using A = UiBaseMedicalTestAccess; using Control = UiBaseMedicalControl; using Tab = UiBaseMedicalTab;
const ItemDefinitionId paper{"item.loot.toilet_paper"};
const ItemDefinitionId bandage{"item.medical.bandage_basic"};
const ItemDefinitionId firstAidStock{"item.loot.first_aid_stock"};
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
class UiBaseMedicalTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app; SDL_Window *window{};
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    std::filesystem::path path;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }
    void SetUp() override {
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
        window=SDL_CreateWindow("headless UI capture regression",1280,720,SDL_WINDOW_HIDDEN);
        ASSERT_NE(window,nullptr);
        ASSERT_NO_THROW(saveDirectory.claim("raidline-medical-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("medical-ui", content);
        p.currency = 10000; p.currentHealth = 64; p.tutorial = TutorialProgress::Complete;
        p.homeFounding.hintsDismissed = true;
        p.basePopulation.injuredResidents = 1;
        p.basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)] = 1;
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>();A::bindWindow(*app,window); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Medical));A::frame(*app);
        attach(1280, 720);
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
        detach(); if(app)A::bindWindow(*app,nullptr);SDL_DestroyWindow(window);window=nullptr;app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void control(Control c) { auto point = center(uiBaseMedicalControlRect(c, A::state(*app).tab)); pointer(point, true); pointer(point, false); A::frame(*app); }
    void click(SDL_FRect rect) { const auto point = center(rect); pointer(point, true); pointer(point, false); A::frame(*app); }
    SDL_FRect cabinetCell(int x, int y) {
        const auto grid = uiBaseMedicalReserveRect();
        const auto size = kUiBaseMedicalReserveCellSize;
        return {grid.x + x * size, grid.y + y * size, size, size};
    }
    void down(Control c = Control::Main) { pointer(center(uiBaseMedicalControlRect(c)), true); A::frame(*app); }
    void up(Control c = Control::Main) { pointer(center(uiBaseMedicalControlRect(c)), false); A::frame(*app); }
    AssetInstanceId create(const ItemDefinitionId &id, std::uint32_t n,
        ProfileContainerId container = ProfileContainerId::stash()) {
        const auto &d = content.item(id);
        const auto fit = findFirstProfileFit(profile(), content, container, d, ItemOrientation::Degrees0);
        if (!fit) { ADD_FAILURE() << "fixture container is full"; return 0; }
        const auto result = profile().assets.create(d, StoredAssetLocation{container, *fit}, n); ++profile().revision; return result;
    }
    std::uint64_t quantity(const ItemDefinitionId &id) {
        std::uint64_t result = 0; for (const auto &[n, a] : profile().assets.records()) if (a.definitionId == id) result += a.quantity; return result;
    }
    void supplies() {
        create(bandage, 1, ProfileContainerId::medicalReserve());
        create(bandage, 1, ProfileContainerId::medicalReserve());
        ASSERT_TRUE(queryStartResidentTreatment(profile(), content).canCommit);
    }
    void wheel(Vec2 p) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = -1; e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
};

TEST_F(UiBaseMedicalTest, PersonalCommitsOnlyOnReleaseAndPreservesMedicineAndWorldTime) {
    const auto quote = queryBaseMedicalService(profile(), content); ASSERT_TRUE(quote.canCommit);
    const auto funds = profile().currency; const auto drugs = quantity(alpha_content::medkit);
    const auto minute = profile().worldClock.elapsedWorldMinutes;
    const auto fp = profileStateFingerprint(profile()); down(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    up(); EXPECT_EQ(profile().currentHealth, 100); EXPECT_EQ(profile().currency, funds - quote.quotedCurrency);
    EXPECT_EQ(quantity(alpha_content::medkit), drugs); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute);
    const auto paid = profileStateFingerprint(profile()); up(); control(Control::Main); EXPECT_EQ(profileStateFingerprint(profile()), paid);
}
TEST_F(UiBaseMedicalTest, HealthyAndInsufficientFundsHaveDifferentReasonsAndNoCharge) {
    profile().currentHealth = 100; ++profile().revision;
    const auto healthy = profileStateFingerprint(profile()); control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()), healthy); EXPECT_NE(A::message(*app).find("does not need"), std::string::npos);
    profile().currentHealth = 50; profile().currency = 0; ++profile().revision;
    const auto poor = profileStateFingerprint(profile()); control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()), poor); EXPECT_NE(A::message(*app).find("insufficient"), std::string::npos);
}
TEST_F(UiBaseMedicalTest, ResidentPlanGroupsRowsButPreservesOriginalIdsAndConsumesAuthorizedItemsOnce) {
    const auto personal = create(bandage, 1);
    ASSERT_NE(personal, 0U);
    const auto personalQuantity = quantity(bandage);
    supplies(); control(Control::Residents);
    const auto &view = A::projection(*app);
    ASSERT_EQ(view.supplies.size(), 1U);
    EXPECT_EQ(view.supplies[0].charges, 2U);
    ASSERT_EQ(view.resident.supplies.size(), 2U);
    const auto currency = profile().currency; const auto drugs = quantity(alpha_content::medkit); const auto minute = profile().worldClock.elapsedWorldMinutes;
    down(); EXPECT_EQ(quantity(bandage), personalQuantity + 2U); up(); ASSERT_TRUE(profile().residentMedical.activeTreatment);
    EXPECT_EQ(quantity(bandage), personalQuantity);
    ASSERT_TRUE(profile().assets.find(personal));
    EXPECT_EQ(profile().assets.find(personal)->remainingCharges, 1U);
    EXPECT_EQ(quantity(alpha_content::medkit), drugs); EXPECT_EQ(profile().currency, currency);
    EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute); EXPECT_EQ(profile().basePopulation.injuredResidents, 1U);
    const auto fp = profileStateFingerprint(profile()); up(); control(Control::Main); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}
TEST_F(UiBaseMedicalTest, ClosingAndReloadingActiveTreatmentKeepsItsWorldTimeAndIdentity) {
    supplies(); control(Control::Residents); control(Control::Main); const auto job = *profile().residentMedical.activeTreatment;
    control(Control::Close); ASSERT_FALSE(A::flow(*app).activeBaseFacility());
    ASSERT_TRUE(A::flow(*app).returnToMainMenu()); A::frame(*app);
    ASSERT_TRUE(A::flow(*app).continueGame()); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Medical));A::frame(*app);
    control(Control::Residents); EXPECT_EQ(profile().residentMedical.activeTreatment->jobId, job.jobId);
    EXPECT_EQ(A::projection(*app).population.remainingMinutes, job.completionWorldMinute - profile().worldClock.elapsedWorldMinutes);
    ASSERT_TRUE(A::session(*app).executeBaseRest(12, "medical-ui-rest").succeeded);
    EXPECT_FALSE(profile().residentMedical.activeTreatment); EXPECT_EQ(profile().basePopulation.injuredResidents, 0U);
}
TEST_F(UiBaseMedicalTest, ResidentUnavailabilityDoesNotDisablePersonalService) {
    profile().baseWorkforce.medicalWorker.reset(); ++profile().revision;
    control(Control::Residents); const auto fp = profileStateFingerprint(profile()); control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_NE(A::message(*app).find("no assigned"), std::string::npos);
    control(Control::Personal); control(Control::Main); EXPECT_EQ(profile().currentHealth, 100);
}
TEST_F(UiBaseMedicalTest, ChangedQuoteWithoutRevisionCancelsOldPress) {
    down(); const auto funds = profile().currency; profile().currentHealth -= 1;
    up(); EXPECT_EQ(profile().currency, funds); EXPECT_NE(A::message(*app).find("PLAN CHANGED"), std::string::npos);
    control(Control::Main); EXPECT_EQ(profile().currentHealth, 100);
}
TEST_F(UiBaseMedicalTest, ChangedMaterialInstanceWithoutRevisionCannotExecuteOldPlan) {
    supplies(); control(Control::Residents); down();
    const auto id = queryStartResidentTreatment(profile(), content).supplies.front().assetId;
    ASSERT_TRUE(profile().assets.erase(id));
    const auto fp = profileStateFingerprint(profile()); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_FALSE(profile().residentMedical.activeTreatment); EXPECT_NE(A::message(*app).find("PLAN CHANGED"), std::string::npos);
}
TEST_F(UiBaseMedicalTest, ChangedUpgradeActionCannotTurnStartPressIntoCancel) {
    profile().baseConstruction.materialUnits = 100; ++profile().revision;
    control(Control::Residents); control(Control::Facilities);
    const auto project = A::projection(*app).upgradeId; ASSERT_TRUE(project);
    ASSERT_TRUE(A::projection(*app).upgrade.canCommit);
    down(Control::Upgrade); ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(*project, "outside-ui-upgrade").succeeded);
    const auto fp = profileStateFingerprint(profile()); up(Control::Upgrade); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_TRUE(profile().baseConstruction.activeProject);
}
TEST_F(UiBaseMedicalTest, StaffingAndUpgradeKeepOriginalCommands) {
    profile().baseConstruction.materialUnits = 100; ++profile().revision;
    control(Control::Residents); control(Control::Facilities); ASSERT_TRUE(profile().baseWorkforce.medicalWorker);
    control(Control::Staffing); EXPECT_FALSE(profile().baseWorkforce.medicalWorker);
    control(Control::Staffing); EXPECT_TRUE(profile().baseWorkforce.medicalWorker);
    control(Control::Upgrade); EXPECT_TRUE(profile().baseConstruction.activeProject);
    control(Control::Upgrade); EXPECT_FALSE(profile().baseConstruction.activeProject);
}
TEST_F(UiBaseMedicalTest, EscapeTabWheelAndDragOutCancelUnfinishedPresses) {
    const auto beforeProfile=profile();const auto fp = profileStateFingerprint(profile()); down(); wheel(center(uiBaseMedicalDetailsRect())); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
    down(); SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 20; e.motion.y = 20; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
    down(); control(Control::Residents); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
    control(Control::Personal); down(); key(SDL_SCANCODE_ESCAPE); A::frame(*app); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
    EXPECT_FALSE(A::flow(*app).activeBaseFacility());
}
TEST_F(UiBaseMedicalTest, FocusLossPauseAndResizeCannotResumeOldPress) {
    const auto fp = profileStateFingerprint(profile()); down(); windowEvent(SDL_EVENT_WINDOW_RESIZED); A::frame(*app); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    down(); windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE); A::frame(*app); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app); down(); A::pause(*app); A::frame(*app); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}
TEST_F(UiBaseMedicalTest, LanguageAndProfileSwitchCancelOldPress) {
    const auto fp = profileStateFingerprint(profile()); down(); A::text(*app).setLanguage(UiLanguage::English); up(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    down(); profile().profileId = "other-medical-profile"; const auto other = profileStateFingerprint(profile()); up(); EXPECT_EQ(profileStateFingerprint(profile()), other);
    EXPECT_EQ(A::state(*app).tab, Tab::Personal);
}
TEST_F(UiBaseMedicalTest, SavingFailureKeepsAllParticipantsUnchanged) {
    const auto blocked = path / "blocked"; {std::ofstream file(blocked); file << "not a directory";}
    A::session(*app).configurePersistence(blocked); const auto fp = profileStateFingerprint(profile()); control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_NE(A::message(*app), "");
    supplies(); control(Control::Residents); const auto resident = profileStateFingerprint(profile()); control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()), resident); EXPECT_FALSE(profile().residentMedical.activeTreatment);
}
TEST_F(UiBaseMedicalTest, BrowsingRenderingAndHoverReuseReadOnlyProjection) {
    const auto fp = profileStateFingerprint(profile()); A::draw(*app); const auto generation = A::generation(*app);
    for (int i = 0; i < 30; ++i) { A::draw(*app); A::directEvent(*app, {UiBaseMedicalEventKind::Move, {40, 40}}); }
    EXPECT_EQ(A::generation(*app), generation); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    control(Control::Residents); control(Control::Details); control(Control::Facilities); A::draw(*app); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}
TEST_F(UiBaseMedicalTest, PainkillerTimerOnlyRefreshesItsDisplayWithoutScanningSupplies) {
    profile().medicalStatus.painkillerRemainingMs = 120000; ++profile().revision;
    A::draw(*app); const auto generation = A::generation(*app);
    for (int i = 0; i < 30; ++i) {
        --profile().medicalStatus.painkillerRemainingMs; A::draw(*app);
        EXPECT_EQ(A::projection(*app).medical.painkillerRemainingMs, profile().medicalStatus.painkillerRemainingMs);
    }
    EXPECT_EQ(A::generation(*app), generation);
}
TEST_F(UiBaseMedicalTest, LegacyDownOnlyPathCannotTreatOrChangeStaff) {
    const auto fp = profileStateFingerprint(profile()); A::legacy(*app, {800, 270}); A::legacy(*app, {800, 490}); A::legacy(*app, {200, 545});
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}
TEST_F(UiBaseMedicalTest, WorldClockAndResidentChangesDoNotInvalidatePersonalCost) {
    down(); profile().worldClock.elapsedWorldMinutes += 1; profile().basePopulation.injuredResidents = 0;
    profile().basePopulation.injuredByProfession[baseProfessionIndex(BaseResidentProfession::General)] = 0;
    ++profile().revision; up(); EXPECT_EQ(profile().currentHealth, 100);
}
TEST_F(UiBaseMedicalTest, MedicalPointerMotionAndCloseKeepTheAcceptedWindowAimAnchor) {
    A::anchor(*app) = Vec2{320, 210}; const auto before = A::flow(*app).baseWorld().weaponAimWorldPosition();
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 1100; e.motion.y = 90; e.motion.xrel = 400; e.motion.yrel = 80;
    ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    ASSERT_TRUE(A::anchor(*app)); EXPECT_FLOAT_EQ(A::anchor(*app)->x, 320); EXPECT_FLOAT_EQ(A::anchor(*app)->y, 210);
    const auto after = A::flow(*app).baseWorld().weaponAimWorldPosition();
    EXPECT_FLOAT_EQ(after.x, before.x); EXPECT_FLOAT_EQ(after.y, before.y);
    control(Control::Close); ASSERT_TRUE(A::anchor(*app)); EXPECT_FLOAT_EQ(A::anchor(*app)->x, 320); EXPECT_FLOAT_EQ(A::anchor(*app)->y, 210);
}
TEST_F(UiBaseMedicalTest, ActiveResidentTreatmentCannotClearItsRequiredWorker) {
    supplies(); control(Control::Residents); control(Control::Main); control(Control::Facilities);
    ASSERT_FALSE(A::projection(*app).staffing.canCommit); const auto fp = profileStateFingerprint(profile());
    control(Control::Staffing); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_TRUE(profile().baseWorkforce.medicalWorker); EXPECT_TRUE(profile().residentMedical.activeTreatment);
}
TEST_F(UiBaseMedicalTest, AllThreeSizesAndBothLanguagesRenderWithoutMutatingPlans) {
    supplies(); const auto fp = profileStateFingerprint(profile());
    for (auto size : kUiWindowSizes) for (auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        for (auto tab : {Tab::Personal, Tab::Residents}) {
            A::state(*app).tab = tab; A::state(*app).facilities = true; A::state(*app).details[static_cast<std::size_t>(tab)] = true;
            A::draw(*app); EXPECT_EQ(profileStateFingerprint(profile()), fp);
        }
    }
}
TEST_F(UiBaseMedicalTest, FocusLostAndRestoredCannotCommitAnOldHold) {
    const auto fp = profileStateFingerprint(profile()); down(); windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); A::frame(*app);
    windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); A::resume(*app); A::frame(*app); up();
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
    control(Control::Main); EXPECT_EQ(profile().currentHealth, 100);
}
TEST_F(UiBaseMedicalTest, ExistingBleedingPriceAndPainkillerEffectArePreserved) {
    profile().medicalStatus = {BleedingSeverity::Heavy, 0, 500, 120000, 25000}; ++profile().revision;
    const auto quote = queryBaseMedicalService(profile(), content); ASSERT_TRUE(quote.canCommit); ASSERT_GT(quote.injuryCost, 0U);
    const auto funds = profile().currency; control(Control::Main);
    EXPECT_EQ(profile().currency, funds - quote.quotedCurrency); EXPECT_EQ(profile().medicalStatus.bleeding, BleedingSeverity::None);
    EXPECT_EQ(profile().medicalStatus.painkillerRemainingMs, 120000U);
}
TEST_F(UiBaseMedicalTest, PartialSuppliesAndNoPatientCannotSpendAnything) {
    create(bandage, 1, ProfileContainerId::medicalReserve());
    create(paper, 1);
    profile().baseSupplyPolicy.assignments[paper] = BaseSupplyCategory::Medical;
    ++profile().revision;
    control(Control::Residents); EXPECT_FALSE(A::projection(*app).resident.canCommit); EXPECT_TRUE(A::projection(*app).supplies.empty());
    const auto partial = profileStateFingerprint(profile()); control(Control::Main); EXPECT_EQ(profileStateFingerprint(profile()), partial);
    profile().basePopulation.injuredResidents = 0; profile().basePopulation.injuredByProfession.fill(0); ++profile().revision;
    control(Control::Main); EXPECT_NE(A::message(*app).find("no injured"), std::string::npos);
}
TEST_F(UiBaseMedicalTest, LongFeedbackCanBeScrolledWithoutLosingOrExecutingIt) {
    const auto fp = profileStateFingerprint(profile());
    A::message(*app) = std::string(100, 'x');
    for (int i = 0; i < 40; ++i) A::message(*app) += "\nA complete reason that must remain reachable.";
    const auto all = A::message(*app); A::draw(*app); ASSERT_GT(A::state(*app).feedbackMaximum, 0);
    wheel(center(uiBaseMedicalFeedbackRect())); EXPECT_GT(A::state(*app).feedbackScroll, 0);
    A::draw(*app); EXPECT_EQ(A::message(*app), all); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}
TEST_F(UiBaseMedicalTest, CabinetMovesTheSelectedInstanceAndShowsItsDestinationBeforeSpending)
{
    const auto id = create(firstAidStock, 1);
    const auto other = create(firstAidStock, 1);
    ASSERT_NE(id, 0U);
    ASSERT_NE(other, 0U);
    control(Control::Residents);
    const auto &items = A::projection(*app).warehouseMedicine;
    const auto item = std::ranges::find_if(items, [id](const auto &row) { return row.id == id; });
    ASSERT_NE(item, items.end());
    const auto index = static_cast<std::size_t>(item - items.begin());
    A::state(*app).warehousePage = index / kUiBaseMedicalWarehouseRows;
    click(uiBaseMedicalWarehouseRowRect(index % kUiBaseMedicalWarehouseRows));
    click(cabinetCell(2, 3));
    ASSERT_EQ(A::state(*app).selectedMedicine, id);
    const StoredAssetLocation exactCabinet{ProfileContainerId::medicalReserve(), {2, 3}};
    const auto preview = projectUiBaseMedicalTransfer(profile(), content, A::state(*app), A::projection(*app));
    ASSERT_TRUE(preview.canCommit) << preview.message;
    EXPECT_EQ(preview.command->destination, exactCabinet);
    const auto before = profileStateFingerprint(profile());
    down(Control::ReserveTransfer);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    up(Control::ReserveTransfer);
    const auto *asset = profile().assets.find(id);
    ASSERT_TRUE(asset);
    EXPECT_EQ(std::get<StoredAssetLocation>(asset->location), exactCabinet);
    EXPECT_TRUE(assetIsBaseAccessible(profile(), other));
    EXPECT_FALSE(assetIsBaseAccessible(profile(), id));
    click(cabinetCell(3, 3));
    EXPECT_EQ(A::state(*app).selectedMedicine, id);
    const auto back = projectUiBaseMedicalTransfer(profile(), content, A::state(*app), A::projection(*app));
    ASSERT_TRUE(back.canCommit) << back.message;
    control(Control::ReserveTransfer);
    ASSERT_TRUE(profile().assets.find(id));
    EXPECT_EQ(std::get<StoredAssetLocation>(profile().assets.find(id)->location), back.command->destination);
    EXPECT_TRUE(assetIsBaseAccessible(profile(), id));
}

TEST_F(UiBaseMedicalTest, CabinetPriorityAndAutomaticAdmissionPersistWithoutChangingExistingJob)
{
    supplies();
    control(Control::Residents);
    control(Control::Main);
    ASSERT_TRUE(profile().residentMedical.activeTreatment);
    const auto job = *profile().residentMedical.activeTreatment;
    control(Control::Priority);
    click(uiBaseMedicalPriorityButtonRect(Control::MoveEarlier, 1));
    EXPECT_EQ(profile().residentMedicalReservePolicy.professionPriority.front(), BaseResidentProfession::Engineering);
    control(Control::AutoAdmission);
    EXPECT_TRUE(profile().residentMedicalReservePolicy.automaticAdmission);
    EXPECT_EQ(profile().residentMedical.activeTreatment, job);
    const auto reloaded = SaveRepository(path).load(content);
    ASSERT_TRUE(reloaded.profile) << reloaded.message;
    EXPECT_EQ(reloaded.profile->residentMedicalReservePolicy, profile().residentMedicalReservePolicy);
    EXPECT_EQ(reloaded.profile->residentMedical.activeTreatment, job);
}

TEST_F(UiBaseMedicalTest, OffscreenStateMatrixHasNoSideEffects) {
    supplies(); const ProfileState seed = profile();
    const char *output = std::getenv("RAIDLINE_MEDICAL_PREVIEW_DIR");
    std::filesystem::path destination;
    if (output && *output) {
        destination = std::filesystem::absolute(output).lexically_normal();
        ASSERT_TRUE(destination.filename().string().starts_with("raidline-medical-review-"));
        std::filesystem::create_directories(destination);
    }
    constexpr const char *names[]{"personal", "healthy", "poor", "bleeding", "resident", "missing-worker", "partial", "active", "upgrade", "admission", "cabinet-placement"};
    for (int scenario = 0; scenario < static_cast<int>(std::size(names)); ++scenario) {
        profile() = seed;
        if (scenario == 1) profile().currentHealth = 100;
        if (scenario == 2) profile().currency = 0;
        if (scenario == 3) profile().medicalStatus = {BleedingSeverity::Heavy, 0, 500, 120000, 25000};
        if (scenario == 5) profile().baseWorkforce.medicalWorker.reset();
        if (scenario == 6) { const auto id = queryStartResidentTreatment(profile(), content).supplies.front().assetId; ASSERT_TRUE(profile().assets.erase(id)); }
        if (scenario == 7) ASSERT_TRUE(A::session(*app).executeStartResidentTreatment("matrix-treatment").succeeded);
        if (scenario == 8) {
            profile().baseConstruction.materialUnits = 100;
            const auto projection = projectUiBaseMedical(profile(), content, UiLanguage::English); ASSERT_TRUE(projection.upgradeId);
            ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(*projection.upgradeId, "matrix-upgrade").succeeded);
        }
        const auto deposit = scenario == 10 ? create(firstAidStock, 1) : 0;
        ++profile().revision;
        const auto fp = profileStateFingerprint(profile());
        for (auto size : kUiWindowSizes) for (auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
            detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
            A::state(*app) = {}; A::state(*app).tab = scenario < 4 ? Tab::Personal : Tab::Residents;
            A::state(*app).facilities = scenario == 8;
            A::state(*app).priorityOpen = scenario == 9;
            A::state(*app).details = {true, true};
            if (deposit) {
                A::state(*app).selectedMedicine = deposit;
                A::state(*app).reserveDestination = GridPosition{1, 2};
            }
            A::refresh(*app);
            SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer); A::draw(*app); SDL_RenderPresent(renderer);
            EXPECT_EQ(profileStateFingerprint(profile()), fp);
            if (!destination.empty()) {
                const auto name = std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
            }
        }
    }
}
} // namespace
