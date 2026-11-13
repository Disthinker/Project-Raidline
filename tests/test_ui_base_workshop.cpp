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

struct UiBaseWorkshopTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.workshopUi_; }
    static auto &projection(App &a) { a.refreshBaseWorkshopUi(); return a.workshopProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.workshopFeedback_; }
    static auto generation(App &a) { return a.workshopProjectionGeneration_; }
    static void settle(App &a) {
        ASSERT_TRUE(raidline::test::waitForUiAppPersistence(a.gameSession_,
            [&] { return a.profileUiBusy() || static_cast<bool>(a.pendingInventoryBarrierAction_); },
            [&] { a.processEvents(); a.update(0); a.input_.endFrame(); })) << a.gameSession_.persistenceMessage();
    }
    static void frame(App &a, float dt = 0) { a.processEvents(); a.update(dt); a.input_.endFrame(); settle(a); }
    static void draw(App &a) { a.renderBaseWorkshop(); }
    static void drawGenerator(App &a) { a.renderBaseGenerator(); }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
    static void close(App &a) { a.gameFlow_.closeBaseFacility(); a.resetBaseWorkshopUi(); }
    static void directEvent(App &a, UiWorkshopEvent e) { a.handleBaseWorkshopEvent(e);  settle(a); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static void refresh(App &a) { a.refreshBaseWorkshopUi(true); a.workshopFeedback_.clear(); }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
};
namespace {
using A = UiBaseWorkshopTestAccess; using Control = UiWorkshopControl;
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
class UiBaseWorkshopTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    std::filesystem::path path;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-workshop-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("workshop-ui", content);
        p.currency = 10000; p.currentHealth = 64; p.tutorial = TutorialProgress::Complete;
        p.homeFounding.hintsDismissed = true;
        p.basePopulation.injuredResidents = 0;
        p.basePopulation.injuredByProfession.fill(0);
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Workshop));
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
        detach(); app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void control(Control c) { auto point = center(uiWorkshopControlRect(c)); pointer(point, true); pointer(point, false); A::frame(*app); }
    void down(Control c = Control::Main) { pointer(center(uiWorkshopControlRect(c)), true); A::frame(*app); }
    void up(Control c = Control::Main) { pointer(center(uiWorkshopControlRect(c)), false); A::frame(*app); }
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

TEST_F(UiBaseWorkshopTest, BrowsingAndHoverNeverReserveAssetsOrAdvanceTime) {
    supplies(); const auto fp=profileStateFingerprint(profile()); A::draw(*app);const auto generation=A::generation(*app);
    for(int i=0;i<20;++i){A::draw(*app);A::directEvent(*app,{UiWorkshopEventKind::Move,{40,40}});}
    EXPECT_EQ(A::generation(*app),generation); EXPECT_EQ(profileStateFingerprint(profile()),fp);
    for(std::size_t i=0;i<4;++i){pointer(center(uiWorkshopRecipeRect(i)),true);pointer(center(uiWorkshopRecipeRect(i)),false);A::frame(*app);}
    control(Control::Facilities); A::draw(*app); EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseWorkshopTest, NativeStartReservesOnceOnReleaseAndOldRouteIsInert) {
    supplies();const auto plan=queryStartBaseManufacturing(profile(),content,{content.baseManufacturingRecipes().front().id});ASSERT_TRUE(plan.canCommit);
    const auto fp=profileStateFingerprint(profile());A::legacy(*app,{800,545});A::legacy(*app,{200,545});EXPECT_EQ(profileStateFingerprint(profile()),fp);
    down();EXPECT_EQ(profileStateFingerprint(profile()),fp);up();ASSERT_TRUE(profile().baseManufacturing.activeOrder);
    const auto order=*profile().baseManufacturing.activeOrder;EXPECT_EQ(order.inputAssetIds.size(),plan.inputs.size());
    for(const auto &s:plan.inputs){const auto *a=profile().assets.find(s.assetId);ASSERT_NE(a,nullptr);EXPECT_TRUE(std::holds_alternative<BaseServiceAssetLocation>(a->location));}
    const auto after=profileStateFingerprint(profile());up();control(Control::Main);EXPECT_EQ(profileStateFingerprint(profile()),after);
    EXPECT_EQ(order.completionWorldMinute-order.startedWorldMinute,plan.durationMinutes);
}
TEST_F(UiBaseWorkshopTest, EveryPublishedRecipeKeepsItsOutputAndInputQuantities) {
    auto seed=profile();
    seed.baseConstruction.workshopLevel = 2;
    seed.baseWorkforce.workshopWorker = BaseResidentProfession::Engineering;
    seed.baseManufacturing.armorRepairRecipeUnlocked = true;
    for(std::size_t i=0;i<content.baseManufacturingRecipes().size();++i){
        profile()=seed; A::refresh(*app);const auto &r=content.baseManufacturingRecipes()[i];
        for (const auto &input : r.inputs)
        {
            const auto maxStack = content.item(input.itemDefinitionId).maxStackSize;
            for (auto remaining = input.quantity; remaining != 0;)
            {
                const auto quantity = std::min(remaining, maxStack);
                create(input.itemDefinitionId, quantity);
                remaining -= quantity;
            }
        }
        const auto page = i / kUiWorkshopPageSize;
        if (A::state(*app).page < page) control(Control::Next);
        ASSERT_EQ(A::state(*app).page, page);
        const auto point = center(uiWorkshopRecipeRect(i % kUiWorkshopPageSize));
        pointer(point, true); pointer(point, false); A::frame(*app);
        ASSERT_EQ(A::state(*app).selection, r.id);
        const auto before=profile().worldClock.elapsedWorldMinutes;control(Control::Main);ASSERT_TRUE(profile().baseManufacturing.activeOrder);
        const auto &order=*profile().baseManufacturing.activeOrder;EXPECT_EQ(order.recipeDefinitionId,r.id);
        const auto *output=profile().assets.find(order.outputAssetId);ASSERT_NE(output,nullptr);EXPECT_EQ(output->definitionId,r.outputItemDefinitionId);EXPECT_EQ(output->quantity,r.outputQuantity);
        for (const auto &input : r.inputs)
        {
            std::uint32_t reserved{};
            for (const auto assetId : order.inputAssetIds)
            {
                const auto *asset = profile().assets.find(assetId);
                ASSERT_NE(asset, nullptr);
                if (asset->definitionId == input.itemDefinitionId)
                {
                    reserved += asset->quantity;
                }
            }
            EXPECT_EQ(reserved, input.quantity);
        }
        EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,before);
    }
}
TEST_F(UiBaseWorkshopTest, CompletionAutomaticallyPlacesTheExistingOutputInStash) {
    supplies();control(Control::Main);ASSERT_TRUE(profile().baseManufacturing.activeOrder);const auto order=*profile().baseManufacturing.activeOrder;
    profile().worldClock.elapsedWorldMinutes=order.completionWorldMinute;const auto result=applyBaseManufacturingThrough(profile(),content);++profile().revision;
    ASSERT_TRUE(result.completed);EXPECT_FALSE(result.outputBlocked);EXPECT_FALSE(profile().baseManufacturing.activeOrder);
    const auto *output=profile().assets.find(order.outputAssetId);ASSERT_NE(output,nullptr);EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(output->location));
    for(auto id:order.inputAssetIds)EXPECT_EQ(profile().assets.find(id),nullptr);
    A::draw(*app);EXPECT_FALSE(A::projection(*app).order);EXPECT_FALSE(uiWorkshopEnabled(Control::Cancel,A::projection(*app)));
}
TEST_F(UiBaseWorkshopTest, FullStashRetainsOutputAndCollectionCannotDuplicateIt) {
    supplies();control(Control::Main);const auto order=*profile().baseManufacturing.activeOrder;const auto fillers=fill();
    profile().worldClock.elapsedWorldMinutes=order.completionWorldMinute;ASSERT_TRUE(applyBaseManufacturingThrough(profile(),content).outputBlocked);++profile().revision;
    A::refresh(*app);ASSERT_TRUE(A::projection(*app).production.outputReady);EXPECT_EQ(A::projection(*app).production.committedWorkers,0U);
    const auto blocked=profileStateFingerprint(profile());control(Control::Main);EXPECT_EQ(profileStateFingerprint(profile()),blocked);
    for(auto id:fillers)ASSERT_TRUE(profile().assets.erase(id));++profile().revision;control(Control::Main);
    EXPECT_FALSE(profile().baseManufacturing.activeOrder);ASSERT_NE(profile().assets.find(order.outputAssetId),nullptr);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile().assets.find(order.outputAssetId)->location));
    const auto quantityBefore=quantity(content.baseManufacturingRecipes().front().outputItemDefinitionId);control(Control::Main);
    EXPECT_EQ(quantity(content.baseManufacturingRecipes().front().outputItemDefinitionId),quantityBefore);
}
TEST_F(UiBaseWorkshopTest, CancelReturnsAllOriginalInputsWithoutRewindingWorldTime) {
    supplies();control(Control::Main);const auto order=*profile().baseManufacturing.activeOrder;const auto minute=profile().worldClock.elapsedWorldMinutes;
    control(Control::Cancel);EXPECT_FALSE(profile().baseManufacturing.activeOrder);EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,minute);
    EXPECT_EQ(profile().assets.find(order.outputAssetId),nullptr);
    for(auto id:order.inputAssetIds){const auto *a=profile().assets.find(id);ASSERT_NE(a,nullptr);EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(a->location));}
}
TEST_F(UiBaseWorkshopTest, CancelCapacityFailurePreservesEveryParticipant) {
    supplies();control(Control::Main);fill();const auto fp=profileStateFingerprint(profile());control(Control::Cancel);
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::projection(*app).returns.canCommit);EXPECT_FALSE(A::message(*app).empty());
}
TEST_F(UiBaseWorkshopTest, MaterialIdentityChangeCancelsTheOldStartPress) {
    supplies();down();const auto plan=A::projection(*app).start;ASSERT_FALSE(plan.inputs.empty());const auto &s=plan.inputs.front();
    ASSERT_TRUE(profile().assets.erase(s.assetId));create(s.definitionId,1);const auto fp=profileStateFingerprint(profile());up();
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(profile().baseManufacturing.activeOrder);
}
TEST_F(UiBaseWorkshopTest, AStartPressCannotTurnIntoCancelWhenAnOrderAppears) {
    supplies();down();ASSERT_TRUE(A::session(*app).executeStartBaseManufacturing(content.baseManufacturingRecipes().front().id,"external-start").succeeded);
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);ASSERT_TRUE(profile().baseManufacturing.activeOrder);
}
TEST_F(UiBaseWorkshopTest, ACancelPressCannotCollectAnOrderThatCompletesWhileHeld) {
    supplies();control(Control::Main);fill();down(Control::Cancel);
    profile().worldClock.elapsedWorldMinutes=profile().baseManufacturing.activeOrder->completionWorldMinute;
    ASSERT_TRUE(applyBaseManufacturingThrough(profile(),content).outputBlocked);++profile().revision;const auto fp=profileStateFingerprint(profile());up(Control::Cancel);
    EXPECT_EQ(profileStateFingerprint(profile()),fp);ASSERT_TRUE(profile().baseManufacturing.activeOrder->outputReady);
}
TEST_F(UiBaseWorkshopTest, MoveWheelResizeLanguagePauseAndCloseCancelAnOldPress) {
    supplies();const auto seed=profile();
    for(int scenario=0;scenario<7;++scenario){
        profile()=seed;A::resume(*app);A::close(*app);ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Workshop));A::refresh(*app);down();
        if(scenario==0)A::directEvent(*app,{UiWorkshopEventKind::Move,{10,10}});
        if(scenario==1)wheel(center(uiWorkshopDetailsRect()));
        if(scenario==2){windowEvent(SDL_EVENT_WINDOW_RESIZED);A::frame(*app);}
        if(scenario==3)A::text(*app).setLanguage(toggledUiLanguage(A::text(*app).language()));
        if(scenario==4){A::pause(*app);A::frame(*app);A::resume(*app);}
        if(scenario==5){windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST);A::frame(*app);windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);A::resume(*app);A::frame(*app);}
        if(scenario==6){control(Control::Close);ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Workshop));}
        const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp)<<scenario;
    }
}
TEST_F(UiBaseWorkshopTest, SaveFailureCannotReserveInputsOrCreateAnOrder) {
    supplies();const auto blocked=path/"blocked";{std::ofstream file(blocked);file<<"file";}A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile());control(Control::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(profile().baseManufacturing.activeOrder);
}
TEST_F(UiBaseWorkshopTest, SaveFailureCannotPartiallyReturnInputs) {
    supplies();control(Control::Main);const auto blocked=path/"blocked";{std::ofstream file(blocked);file<<"file";}A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile());control(Control::Cancel);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseWorkshopTest, ExistingOrderReloadKeepsItsFrozenIdentityAndTiming) {
    supplies();control(Control::Main);const auto order=profile().baseManufacturing.activeOrder;
    A::close(*app);static_cast<void>(A::flow(*app).returnToMainMenu());ASSERT_TRUE(A::flow(*app).continueGame());A::frame(*app);
    ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Workshop));A::draw(*app);EXPECT_EQ(profile().baseManufacturing.activeOrder,order);
}
TEST_F(UiBaseWorkshopTest, ReserveDisplayFreezesProgressAndDisablesProduction) {
    supplies();control(Control::Main);const BaseFacilityDefinitionId id{"base_facility.workshop"};
    profile().baseConstruction.facilities[id]=BaseConstructionState::FacilityPlacement::Reserve;
    profile().baseConstruction.facilityReserveStartedWorldMinutes[id]=profile().worldClock.elapsedWorldMinutes;
    ++profile().revision;const auto before=projectBaseManufacturing(profile());profile().worldClock.elapsedWorldMinutes+=100;
    A::draw(*app);EXPECT_EQ(A::projection(*app).production.remainingMinutes,before.remainingMinutes);EXPECT_FALSE(A::projection(*app).installed);
    const auto fp=profileStateFingerprint(profile());control(Control::Cancel);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseWorkshopTest, ActiveWorkerIsProtectedAndUpgradeRemainsAccessible) {
    supplies();control(Control::Main);control(Control::Facilities);const auto fp=profileStateFingerprint(profile());control(Control::Staffing);EXPECT_EQ(profileStateFingerprint(profile()),fp);
    control(Control::Cancel);profile().baseConstruction.materialUnits=100;++profile().revision;control(Control::Upgrade);EXPECT_TRUE(profile().baseConstruction.activeProject);
    control(Control::Upgrade);EXPECT_FALSE(profile().baseConstruction.activeProject);
}
TEST_F(UiBaseWorkshopTest, PointerMotionAndClosePreserveTheAcceptedWindowAimAnchor) {
    A::anchor(*app)=Vec2{320,210};const auto before=A::flow(*app).baseWorld().weaponAimWorldPosition();
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=1100;e.motion.y=90;e.motion.xrel=400;e.motion.yrel=80;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    control(Control::Close);ASSERT_TRUE(A::anchor(*app));EXPECT_FLOAT_EQ(A::anchor(*app)->x,320);EXPECT_FLOAT_EQ(A::anchor(*app)->y,210);
    const auto after=A::flow(*app).baseWorld().weaponAimWorldPosition();EXPECT_FLOAT_EQ(after.x,before.x);EXPECT_FLOAT_EQ(after.y,before.y);
}
TEST_F(UiBaseWorkshopTest, LongReasonsRemainReachableWithoutExecutingAnyAction) {
    auto &message=A::message(*app);for(int i=0;i<40;++i)message+="A complete reason must remain reachable.\n";A::draw(*app);
    ASSERT_GT(A::state(*app).feedbackMaximum,0);const auto fp=profileStateFingerprint(profile());wheel(center(uiWorkshopFeedbackRect()));
    EXPECT_GT(A::state(*app).feedbackScroll,0);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseWorkshopTest, SaveFailureCannotMoveReadyOutputIntoStash) {
    supplies();control(Control::Main);const auto fillers=fill();
    profile().worldClock.elapsedWorldMinutes=profile().baseManufacturing.activeOrder->completionWorldMinute;
    ASSERT_TRUE(applyBaseManufacturingThrough(profile(),content).outputBlocked);
    for(auto id:fillers)ASSERT_TRUE(profile().assets.erase(id));++profile().revision;
    const auto blocked=path/"blocked";{std::ofstream file(blocked);file<<"file";}A::session(*app).configurePersistence(blocked);
    const auto fp=profileStateFingerprint(profile());control(Control::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
    ASSERT_TRUE(profile().baseManufacturing.activeOrder->outputReady);
}
TEST_F(UiBaseWorkshopTest, ActiveProjectionKeepsFrozenTimingAndReusesCapacityPlans) {
    supplies();control(Control::Main);const auto order=*profile().baseManufacturing.activeOrder;
    A::draw(*app);const auto generation=A::generation(*app);
    for(int i=0;i<30;++i)A::draw(*app);EXPECT_EQ(A::generation(*app),generation);
    profile().baseMorale.tier=BaseMoraleTier::Low;profile().worldClock.elapsedWorldMinutes+=1;++profile().revision;A::draw(*app);
    EXPECT_EQ(A::projection(*app).production.remainingMinutes,order.completionWorldMinute-profile().worldClock.elapsedWorldMinutes);
    EXPECT_EQ(profile().baseManufacturing.activeOrder->completionWorldMinute,order.completionWorldMinute);
}
TEST_F(UiBaseWorkshopTest, MissingRequirementsRemainReadOnlyAndCannotStart) {
    const auto fp=profileStateFingerprint(profile());A::draw(*app);EXPECT_FALSE(A::projection(*app).start.canCommit);
    control(Control::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
    supplies();profile().baseWorkforce.workshopWorker.reset();++profile().revision;A::draw(*app);
    EXPECT_FALSE(A::projection(*app).start.canCommit);const auto noWorker=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),noWorker);
}
TEST_F(UiBaseWorkshopTest, OffscreenMatrixPreservesPlansAcrossSizesAndLanguages) {
    supplies();const auto seed=profile();const char *output=std::getenv("RAIDLINE_WORKSHOP_PREVIEW_DIR");std::filesystem::path destination;
    if(output && *output){destination=std::filesystem::absolute(output).lexically_normal();ASSERT_TRUE(destination.filename().string().starts_with("raidline-workshop-review-"));std::filesystem::create_directories(destination);}
    constexpr const char *names[]{"ready","missing","no-worker","active","output-full","reserve","upgrade","facilities","long-reason",
        "research-ready","research-active","research-unlocked"};
    for(int scenario=0;scenario<static_cast<int>(std::size(names));++scenario){
        profile()=seed;A::state(*app)={};A::message(*app).clear();
        if(scenario==1){const auto plan=queryStartBaseManufacturing(profile(),content,{content.baseManufacturingRecipes().front().id});ASSERT_TRUE(profile().assets.erase(plan.inputs.front().assetId));}
        if(scenario==2)profile().baseWorkforce.workshopWorker.reset();
        if(scenario==3 || scenario==4 || scenario==5)ASSERT_TRUE(A::session(*app).executeStartBaseManufacturing(content.baseManufacturingRecipes().front().id,"matrix-start").succeeded);
        if(scenario==4){fill();profile().worldClock.elapsedWorldMinutes=profile().baseManufacturing.activeOrder->completionWorldMinute;ASSERT_TRUE(applyBaseManufacturingThrough(profile(),content).outputBlocked);}
        if(scenario==5){const BaseFacilityDefinitionId id{"base_facility.workshop"};profile().baseConstruction.facilities[id]=BaseConstructionState::FacilityPlacement::Reserve;profile().baseConstruction.facilityReserveStartedWorldMinutes[id]=profile().worldClock.elapsedWorldMinutes;}
        if(scenario==6){profile().baseConstruction.materialUnits=100;const auto projection=projectUiWorkshop(profile(),content,UiLanguage::English,{});ASSERT_TRUE(projection.upgradeId);ASSERT_TRUE(A::session(*app).executeStartBaseConstruction(*projection.upgradeId,"matrix-upgrade").succeeded);}
        if(scenario>=9) {
            profile().baseConstruction.workshopLevel=2;
            profile().baseWorkforce.workshopWorker=BaseResidentProfession::Engineering;
            profile().baseManufacturing.armorRepairRecipeUnlocked=scenario==11;
            const auto selection=scenario==11?kArmorRepairRecipeId:kArmorRepairResearchId;
            A::state(*app).selection=selection;
            const auto projection=projectUiWorkshop(profile(),content,UiLanguage::English,selection);
            const auto it=std::find_if(projection.recipes.begin(),projection.recipes.end(),
                [&](const UiWorkshopRecipe &row){return row.id==selection;});
            ASSERT_NE(it,projection.recipes.end());
            A::state(*app).page=static_cast<std::size_t>(it-projection.recipes.begin())/kUiWorkshopPageSize;
            if(scenario!=11)ASSERT_NE(create(kArmorRepairBlueprintId,1),0U);
            if(scenario==10)ASSERT_TRUE(A::session(*app).executeStartBaseManufacturing(selection,"matrix-research").succeeded);
            if(scenario==11)for(const auto &input:content.baseManufacturingRecipe(selection).inputs)
                ASSERT_NE(create(input.itemDefinitionId,input.quantity),0U);
        }
        ++profile().revision;
        for(auto size:kUiWindowSizes)for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}){
            detach();attach(size.width,size.height);A::text(*app).setLanguage(language);A::state(*app).facilities=scenario==6 || scenario==7;A::refresh(*app);
            if(scenario==8)for(int i=0;i<20;++i)A::message(*app)+="A complete long failure reason. ";
            const auto fp=profileStateFingerprint(profile());SDL_SetRenderDrawColor(renderer,10,16,18,255);SDL_RenderClear(renderer);A::draw(*app);SDL_RenderPresent(renderer);
            EXPECT_EQ(profileStateFingerprint(profile()),fp);
            if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string(names[scenario])+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));
        }
    }
}
TEST_F(UiBaseWorkshopTest, ResearchUsesTheWorkshopControlsAndRevealsTheRecipe) {
    profile().baseConstruction.workshopLevel = 2;
    profile().baseWorkforce.workshopWorker = BaseResidentProfession::Engineering;
    ++profile().revision;
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    const auto &initial = A::projection(*app);
    const auto research = std::find_if(initial.recipes.begin(), initial.recipes.end(),
        [](const UiWorkshopRecipe &r) { return r.id == kArmorRepairResearchId; });
    ASSERT_NE(research, initial.recipes.end());
    const auto index = static_cast<std::size_t>(research - initial.recipes.begin());
    for (std::size_t page = 0; page < index / kUiWorkshopPageSize; ++page)
        control(Control::Next);
    const auto point = center(uiWorkshopRecipeRect(index % kUiWorkshopPageSize));
    pointer(point, true);
    pointer(point, false);
    A::frame(*app);
    ASSERT_EQ(A::projection(*app).selection, kArmorRepairResearchId);
    EXPECT_FALSE(A::projection(*app).start.canCommit);
    EXPECT_EQ(A::projection(*app).recipes[index].shortName, "防具维修研究");

    const auto blueprint = create(kArmorRepairBlueprintId, 1U);
    ASSERT_NE(blueprint, 0U);
    control(Control::Main);
    ASSERT_TRUE(profile().baseManufacturing.activeOrder);
    EXPECT_EQ(profile().baseManufacturing.activeOrder->inputAssetIds,
        std::vector<AssetInstanceId>{blueprint});
    EXPECT_TRUE(uiWorkshopEnabled(Control::Cancel, A::projection(*app)));
    EXPECT_FALSE(uiWorkshopEnabled(Control::Main, A::projection(*app)));
    control(Control::Cancel);
    ASSERT_FALSE(profile().baseManufacturing.activeOrder);
    ASSERT_NE(profile().assets.find(blueprint), nullptr);

    control(Control::Main);
    ASSERT_TRUE(profile().baseManufacturing.activeOrder);
    ASSERT_TRUE(A::session(*app).executeBaseRest(12, "finish-armor-research").succeeded);
    EXPECT_FALSE(profile().baseManufacturing.activeOrder);
    EXPECT_EQ(profile().assets.find(blueprint), nullptr);
    EXPECT_TRUE(profile().baseManufacturing.armorRepairRecipeUnlocked);
    EXPECT_EQ(A::projection(*app).selection, kArmorRepairRecipeId);
    const auto reloaded = SaveRepository(path).load(content);
    ASSERT_TRUE(reloaded.profile);
    EXPECT_TRUE(reloaded.profile->baseManufacturing.armorRepairRecipeUnlocked);
}

TEST_F(UiBaseWorkshopTest, GeneratorPanelSupportsTargetActionsAtAllWindowSizes) {
    A::close(*app);
    auto &flow = A::flow(*app);
    auto &session = A::session(*app);
    EXPECT_FALSE(flow.openBaseFacilityForManagement(BaseFacilityKind::Generator));
    profile().baseConstruction.materialUnits = 6;
    ASSERT_TRUE(session.executeStartBaseConstruction(kBaseGeneratorProjectId, "generator-ui-build").succeeded);
    ASSERT_TRUE(session.executeBaseRest(8, "generator-ui-build-rest").succeeded);
    EXPECT_FALSE(flow.openBaseFacilityForManagement(BaseFacilityKind::Generator));
    const auto &parcel = flow.baseWorld().baseParcel();
    const Vec2 placement{parcel.position.x + parcel.size.x * kBaseGeneratorDefaultCenter.x,
        parcel.position.y + parcel.size.y * kBaseGeneratorDefaultCenter.y};
    const auto installed = flow.installBaseFacilityAt(BaseFacilityKind::Generator, placement, "generator-ui-install");
    ASSERT_TRUE(installed.succeeded) << installed.message;
    supplies();
    ASSERT_TRUE(session.executeStartBaseManufacturing(content.baseManufacturingRecipes().front().id, "generator-ui-order").succeeded);
    ASSERT_TRUE(flow.openBaseFacilityForManagement(BaseFacilityKind::Generator));
    ASSERT_TRUE(profile().baseManufacturing.activeOrder);
    const auto job = profile().baseManufacturing.activeOrder->jobId;

    std::filesystem::path destination;
    if (const char *output = std::getenv("RAIDLINE_GENERATOR_PREVIEW_DIR"); output && *output) {
        destination = std::filesystem::absolute(output).lexically_normal();
        ASSERT_TRUE(destination.filename().string().starts_with("raidline-generator-review-"));
        std::filesystem::create_directories(destination);
    }
    const auto drawState = [&](std::string_view state) {
        for (const auto size : kUiWindowSizes) {
            for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
                detach();
                attach(size.width, size.height);
                A::text(*app).setLanguage(language);
                const auto before = profileStateFingerprint(profile());
                ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
                ASSERT_TRUE(SDL_RenderClear(renderer));
                A::drawGenerator(*app);
                ASSERT_TRUE(SDL_RenderPresent(renderer));
                EXPECT_EQ(profileStateFingerprint(profile()), before);
                if (!destination.empty()) {
                    const auto file = std::string(state) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                    ASSERT_TRUE(IMG_SavePNG(surface, (destination / file).string().c_str()));
                }
            }
        }
    };
    constexpr Vec2 workshopButton{424, 418};
    constexpr Vec2 residentButton{856, 418};
    constexpr Vec2 pauseButton{424, 468};
    constexpr Vec2 resumeButton{856, 468};
    const auto click = [&](Vec2 location) {
        pointer(location, true);
        pointer(location, false);
        A::frame(*app);
    };

    drawState("missing-fuel");
    click(workshopButton);
    EXPECT_FALSE(profile().emergencyPower);
    const auto first = create(kRaidFuelCanId, 1);
    const auto second = create(kRaidFuelCanId, 1);
    ASSERT_NE(first, 0U);
    ASSERT_NE(second, 0U);
    drawState("ready");
    click(residentButton);
    EXPECT_FALSE(profile().emergencyPower);

    pointer(workshopButton, true);
    A::frame(*app);
    EXPECT_FALSE(profile().emergencyPower);
    pointer(workshopButton, false);
    A::frame(*app);
    ASSERT_TRUE(profile().emergencyPower);
    EXPECT_EQ(profile().emergencyPower->activeJobId, job);
    EXPECT_EQ(profile().assets.find(first), nullptr);
    EXPECT_NE(profile().assets.find(second), nullptr);
    drawState("running");

    click(workshopButton);
    EXPECT_EQ(profile().emergencyPower->activeJobId, job);
    click(pauseButton);
    EXPECT_FALSE(profile().emergencyPower->activeJobId);
    EXPECT_EQ(profile().emergencyPower->remainingMinutes, kBaseEmergencyPowerBatchMinutes);
    drawState("paused");

    click(pauseButton);
    EXPECT_FALSE(profile().emergencyPower->activeJobId);
    click(resumeButton);
    ASSERT_TRUE(profile().emergencyPower);
    EXPECT_EQ(profile().emergencyPower->activeJobId, job);
    EXPECT_NE(profile().assets.find(second), nullptr);
    const auto saved = SaveRepository(path).load(content);
    ASSERT_TRUE(saved.profile);
    EXPECT_EQ(saved.profile->emergencyPower, profile().emergencyPower);
}

TEST(UiWorkshopGeometryTest, PaginationAndStateActionsRemainSeparate) {
    EXPECT_EQ(uiWorkshopPageCount(0),1U);EXPECT_EQ(uiWorkshopPageCount(6),1U);EXPECT_EQ(uiWorkshopPageCount(7),2U);
    UiWorkshopState s;UiWorkshopProjection p;
    for(int i=0;i<7;++i)p.recipes.push_back({BaseManufacturingRecipeDefinitionId{"test."+std::to_string(i)}});
    s.page=1;const auto hit=hitUiWorkshop(center(uiWorkshopRecipeRect(0)),s,p);ASSERT_TRUE(hit);EXPECT_EQ(hit->recipe,p.recipes[6].id);
    p.order=BaseManufacturingOrder{};EXPECT_FALSE(hitUiWorkshop(center(uiWorkshopRecipeRect(0)),s,p));
    EXPECT_NE(uiWorkshopControlRect(Control::Cancel).x,uiWorkshopControlRect(Control::Main).x);
}
} // namespace
