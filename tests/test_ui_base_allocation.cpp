#include "app.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <set>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiBaseAllocationTestAccess {
    static auto &flow(App &a){return a.gameFlow_;}
    static auto &session(App &a){return a.gameSession_;}
    static auto &state(App &a){return a.allocationUi_;}
    static auto &projection(App &a){a.refreshBaseAllocationUi();return a.allocationProjection_;}
    static auto &text(App &a){return a.uiTextRenderer_;}
    static auto &message(App &a){return a.allocationFeedback_;}
    static auto generation(App &a){return a.allocationProjectionGeneration_;}
    static auto &anchor(App &a){return a.worldAimPointerAnchor_;}
    static void refresh(App &a){a.refreshBaseAllocationUi(true);}
    static void frame(App &a,float dt=0){a.processEvents();a.update(dt);a.input_.endFrame();}
    static void draw(App &a){a.renderBaseAllocation();}
    static void legacy(App &a,Vec2 p){a.handleBasePointerClick({{p.x,p.y}});}
    static void pause(App &a){a.pauseMenu_.open();}
    static void resume(App &a){a.pauseMenu_.close();}
    static void reset(App &a){a.resetBaseAllocationUi();}
    static void direct(App &a,UiAllocationEvent e){a.handleBaseAllocationEvent(e);}
    static void attach(App &a,SDL_Renderer *r,float scale){a.renderer_=r;ASSERT_TRUE(a.uiTextRenderer_.initialize(r));a.uiTextRenderer_.setRasterScale(scale);}
    static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
};
namespace {
using A=UiBaseAllocationTestAccess;using C=UiAllocationControl;using T=UiAllocationTab;
const ItemDefinitionId scrap{"item.loot.scrap_parts"},electronics{"item.loot.electronics"},paper{"item.loot.toilet_paper"};
Vec2 center(SDL_FRect r){return {r.x+r.w/2,r.y+r.h/2};}
void pointer(Vec2 p,bool down){SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.x=p.x;e.button.y=p.y;ASSERT_TRUE(SDL_PushEvent(&e));}
void windowEvent(Uint32 type){SDL_Event e{};e.type=type;ASSERT_TRUE(SDL_PushEvent(&e));}
void key(SDL_Scancode code){SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=code;e.key.down=true;ASSERT_TRUE(SDL_PushEvent(&e));e.type=SDL_EVENT_KEY_UP;e.key.down=false;ASSERT_TRUE(SDL_PushEvent(&e));}
class UiBaseAllocationTest:public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};std::filesystem::path path;
    const ContentRegistry &content=publishedContentRegistry();
    ProfileState &profile(){return const_cast<ProfileState &>(A::session(*app).profile());}
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-allocation-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p=makeNewAlphaProfile("allocation-ui",content);p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
        ASSERT_TRUE(SaveRepository(path).save(p,content.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::frame(*app);
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Allocation));attach(1280,720);A::text(*app).setLanguage(UiLanguage::English);
    }
    void attach(int w,int h){surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));A::attach(*app,renderer,w/1280.0F);}
    void detach(){if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;}
    void TearDown() override {
        detach();app.reset();SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void click(Vec2 p){pointer(p,true);pointer(p,false);A::frame(*app);}
    void control(C c){click(center(uiAllocationControlRect(c)));}
    void tab(T t){control(static_cast<C>(t));}
    void down(C c=C::Main){pointer(center(uiAllocationControlRect(c)),true);A::frame(*app);}
    void up(C c=C::Main){pointer(center(uiAllocationControlRect(c)),false);A::frame(*app);}
    void wheel(Vec2 p){SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=-1;e.wheel.mouse_x=p.x;e.wheel.mouse_y=p.y;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);}
    AssetInstanceId create(const ItemDefinitionId &id,std::uint32_t n=1){const auto &d=content.item(id);const auto fit=findFirstProfileFit(profile(),content,ProfileContainerId::stash(),d,ItemOrientation::Degrees0);if(!fit){ADD_FAILURE()<<"fixture Stash full";return 0;}const auto result=profile().assets.create(d,StoredAssetLocation{ProfileContainerId::stash(),*fit},n);++profile().revision;return result;}
    void select(const ItemDefinitionId &id,AssetInstanceId instance=0){A::refresh(*app);const auto &rows=A::projection(*app).rows;const auto it=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.definitionId==id && r.instanceId==instance;});ASSERT_NE(it,rows.end());const auto index=static_cast<std::size_t>(it-rows.begin());auto &s=A::state(*app);s.pages[static_cast<int>(s.tab)]=index/uiAllocationPageSize(s.tab);click(center(uiAllocationRowRect(s.tab,index%uiAllocationPageSize(s.tab))));}
    ItemDefinitionId wishType(){A::refresh(*app);const auto &p=A::projection(*app);const auto w=std::find_if(p.wishes.begin(),p.wishes.end(),[&](const auto &v){return p.selectedWish && v.definitionId==p.selectedWish->definitionId;});if(w==p.wishes.end()){ADD_FAILURE()<<"fixture needs a wish";return {};}const ItemDefinition *best=nullptr;for(const auto &d:content.items())if(baseSupplyContribution(d,w->category)>0 && (!best || baseSupplyContribution(d,w->category)<baseSupplyContribution(*best,w->category)))best=&d;if(!best){ADD_FAILURE()<<"fixture needs contribution";return {};}return best->definitionId;}
    ItemDefinitionId prepareWish(bool enough=true){tab(T::Wishes);const auto id=wishType();const auto &p=A::projection(*app);const auto w=std::find_if(p.wishes.begin(),p.wishes.end(),[&](const auto &v){return p.selectedWish && v.definitionId==p.selectedWish->definitionId;});const auto unit=baseSupplyContribution(content.item(id),w->category);std::vector<AssetInstanceId> old;for(const auto &[n,a]:profile().assets.records())if(a.definitionId==id)old.push_back(n);for(auto n:old)EXPECT_TRUE(profile().assets.erase(n));++profile().revision;const auto count=enough?w->requiredContribution/unit+1:1;for(std::uint32_t i=0;i<count;++i)create(id);select(id);return id;}
    void blockedSave(){const auto blocked=path/"blocked";{std::ofstream f(blocked);f<<"file";}A::session(*app).configurePersistence(blocked);}
};

TEST_F(UiBaseAllocationTest,BrowsingAndSelectionDoNotConsumeOrAdvanceTime) {
    create(alpha_content::lootCola);const auto material=create(scrap);const auto fp=profileStateFingerprint(profile());
    A::draw(*app);const auto generation=A::generation(*app);for(int i=0;i<12;++i){A::draw(*app);A::direct(*app,{UiAllocationEventKind::Move,{20,20}});}EXPECT_EQ(A::generation(*app),generation);
    control(C::Details);tab(T::Supply);select(alpha_content::lootCola);tab(T::Wishes);tab(T::Materials);select(scrap,material);tab(T::Overview);
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::state(*app).material);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,SupplyNeedsReleaseAndOnlyChangesTypePolicy) {
    const auto id=create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);
    const auto item=*profile().assets.find(id);const auto before=profileStateFingerprint(profile());const auto minute=profile().worldClock.elapsedWorldMinutes;
    down();EXPECT_EQ(profileStateFingerprint(profile()),before);up();
    ASSERT_EQ(profile().baseSupplyPolicy.assignments.at(alpha_content::lootCola),BaseSupplyCategory::Food);
    ASSERT_NE(profile().assets.find(id),nullptr);EXPECT_EQ(profile().assets.find(id)->quantity,item.quantity);EXPECT_EQ(profile().assets.find(id)->location,item.location);EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,minute);
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,ZeroStockAuthorizationStaysVisibleAndCanBeStopped) {
    create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);control(C::Main);
    std::vector<AssetInstanceId> ids;for(const auto &[id,a]:profile().assets.records())if(a.definitionId==alpha_content::lootCola)ids.push_back(id);
    for(auto id:ids)ASSERT_TRUE(profile().assets.erase(id));++profile().revision;
    select(alpha_content::lootCola);const auto &p=A::projection(*app);ASSERT_TRUE(p.supplySelection);ASSERT_TRUE(p.assignment.canCommit);
    EXPECT_EQ(std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return r.definitionId==alpha_content::lootCola;})->quantity,0U);
    control(C::Main);EXPECT_FALSE(profile().baseSupplyPolicy.assignments.contains(alpha_content::lootCola));
}
TEST_F(UiBaseAllocationTest,MovingAnExistingPolicyToAnotherCategoryRequiresExplicitAction) {
    const auto id=create(electronics);profile().baseSupplyPolicy.assignments[electronics]=BaseSupplyCategory::Medical;++profile().revision;
    tab(T::Supply);control(C::Security);select(electronics);EXPECT_EQ(profile().baseSupplyPolicy.assignments.at(electronics),BaseSupplyCategory::Medical);
    ASSERT_EQ(A::projection(*app).currentAssignment,BaseSupplyCategory::Medical);control(C::Main);
    EXPECT_EQ(profile().baseSupplyPolicy.assignments.at(electronics),BaseSupplyCategory::Security);EXPECT_NE(profile().assets.find(id),nullptr);
}
TEST_F(UiBaseAllocationTest,PolicyChangeWithoutRevisionCannotReverseTheNewPolicyOnRelease) {
    create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);down();
    profile().baseSupplyPolicy.assignments[alpha_content::lootCola]=BaseSupplyCategory::Food;const auto fp=profileStateFingerprint(profile());up();
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_NE(A::message(*app).find("PLAN CHANGED"),std::string::npos);
}
TEST_F(UiBaseAllocationTest,DailyNeedsUseOnlyExplicitlyAuthorizedPersonalItems) {
    const auto untouched=create(paper);create(alpha_content::lootCola);create(alpha_content::lootCola);profile().baseSupplyPolicy.assignments.clear();
    profile().worldClock.elapsedWorldMinutes=kWorldMinutesPerDay-1;profile().baseResources.pool={};++profile().revision;
    tab(T::Supply);select(alpha_content::lootCola);control(C::Main);
    const auto before=projectBaseSupplyReadiness(profile(),content,populationAdjustedDailyDemand(profile().basePopulation));ASSERT_GT(before.authorizedContribution.food,0U);
    const auto receipt=A::session(*app).executeBaseRest(1,"allocation-test-midnight");ASSERT_TRUE(receipt.succeeded)<<receipt.message;
    ASSERT_NE(profile().assets.find(untouched),nullptr);EXPECT_EQ(profile().assets.find(untouched)->quantity,1U);
    const auto after=projectBaseSupplyReadiness(profile(),content,populationAdjustedDailyDemand(profile().basePopulation));EXPECT_LT(after.authorizedContribution.food,before.authorizedContribution.food);
}
TEST_F(UiBaseAllocationTest,InsufficientWishKeepsTheRealSelectedContributionVisible) {
    prepareWish(false);const auto &p=A::projection(*app);ASSERT_GT(p.selectedContribution,0U);ASSERT_LT(p.selectedContribution,p.requiredContribution);ASSERT_FALSE(p.submission.canCommit);
    const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,WishConsumesOnlyTheFrozenWholeStacksAndLeavesLaterItems) {
    const auto type=prepareWish();const auto selected=A::projection(*app).chosen;ASSERT_TRUE(A::projection(*app).submission.canCommit);
    const auto later=create(type);A::draw(*app);ASSERT_EQ(A::projection(*app).chosen.size(),selected.size());
    down();for(const auto &a:selected)EXPECT_NE(profile().assets.find(a.id),nullptr);up();
    for(const auto &a:selected)EXPECT_EQ(profile().assets.find(a.id),nullptr);EXPECT_NE(profile().assets.find(later),nullptr);
    const auto fp=profileStateFingerprint(profile());up();control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,WishPreviewMatchesOriginalWholeStackAndExcessQuery) {
    prepareWish();const auto p=A::projection(*app);ASSERT_TRUE(p.submission.canCommit);ASSERT_GT(p.selectedContribution,p.requiredContribution);
    EXPECT_EQ(p.selectedContribution,p.submission.totalContribution);EXPECT_EQ(p.submission.excessContribution,p.selectedContribution-p.requiredContribution);
    EXPECT_EQ(p.chosen.size(),p.submission.consumedAssets.size());control(C::Main);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,WishSelectionDoesNotEnableAutomaticSupply) {
    prepareWish();EXPECT_TRUE(profile().baseSupplyPolicy.assignments.empty());const auto fp=profileStateFingerprint(profile());
    tab(T::Supply);EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,WishCycleIdentityInvalidatesThePreviousConfirmation) {
    prepareWish();down();++profile().basePriority.cycleIndex;const auto fp=profileStateFingerprint(profile());up();
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,ChangedQuantityWithoutRevisionCancelsWishSelection) {
    prepareWish();down();const auto chosen=A::state(*app).contributions.front();++profile().assets.findMutable(chosen.id)->quantity;
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,WishFocusAndFilterKeepTheOriginalPersistentIdentity) {
    prepareWish();const auto wish=A::projection(*app).selectedWish;const auto assets=profile().assets.records().size();control(C::Focus);
    EXPECT_EQ(profile().basePriority.focus,wish);EXPECT_EQ(profile().assets.records().size(),assets);control(C::Filter);EXPECT_TRUE(A::state(*app).focusOnly);
    control(C::ClearFocus);EXPECT_FALSE(profile().basePriority.focus);EXPECT_FALSE(A::state(*app).focusOnly);
}
TEST_F(UiBaseAllocationTest,MaterialProcessingUsesTheExplicitSecondInstanceOnly) {
    const auto first=create(scrap),second=create(scrap);tab(T::Materials);select(scrap,second);
    const auto before=profile().baseConstruction.materialUnits;const auto output=content.item(scrap).baseConstructionMaterialValue;
    down();ASSERT_NE(profile().assets.find(second),nullptr);up();EXPECT_NE(profile().assets.find(first),nullptr);EXPECT_EQ(profile().assets.find(second),nullptr);
    EXPECT_EQ(profile().baseConstruction.materialUnits,before+output);const auto fp=profileStateFingerprint(profile());up();control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,CapacityFailureKeepsTheWholeMaterialAndShowsItsCandidateOutput) {
    const auto id=create(scrap);profile().baseConstruction.materialUnits=content.maximumBaseConstructionMaterials()-1;++profile().revision;
    tab(T::Materials);select(scrap,id);ASSERT_FALSE(A::projection(*app).processing.canCommit);EXPECT_EQ(A::projection(*app).materialOutput,content.item(scrap).baseConstructionMaterialValue);
    const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_NE(profile().assets.find(id),nullptr);
}
TEST_F(UiBaseAllocationTest,CapacityChangeWithoutRevisionCancelsTheHeldProcessing) {
    const auto id=create(scrap);tab(T::Materials);select(scrap,id);down();++profile().baseConstruction.materialUnits;const auto fp=profileStateFingerprint(profile());up();
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_NE(profile().assets.find(id),nullptr);EXPECT_NE(A::message(*app).find("PLAN CHANGED"),std::string::npos);
}
TEST_F(UiBaseAllocationTest,MovingTheSameInstanceToAnotherPageInvalidatesItsPreview) {
    const auto id=create(scrap);tab(T::Materials);select(scrap,id);down();
    auto *a=profile().assets.findMutable(id);auto location=std::get<StoredAssetLocation>(a->location);location.origin={16,8};a->location=location;
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::state(*app).material);
}
TEST_F(UiBaseAllocationTest,RowIdentityCannotChangeWhenAnotherMaterialIsRemoved) {
    const auto first=create(scrap),second=create(scrap);tab(T::Materials);A::refresh(*app);
    const auto &rows=A::projection(*app).rows;const auto i=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.instanceId==first;})-rows.begin();
    A::state(*app).pages[3]=static_cast<std::size_t>(i)/6;const auto point=center(uiAllocationRowRect(T::Materials,i%6));pointer(point,true);A::frame(*app);
    ASSERT_TRUE(profile().assets.erase(first));const auto fp=profileStateFingerprint(profile());pointer(point,false);A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::state(*app).material);EXPECT_NE(profile().assets.find(second),nullptr);
}
TEST_F(UiBaseAllocationTest,NativeCancellationBoundariesNeverProcessAStaleMaterial) {
    const auto id=create(scrap);const auto seed=profile();
    for(int scenario=0;scenario<7;++scenario) {
        SCOPED_TRACE(scenario);
        profile()=seed;A::reset(*app);A::flow(*app).closeBaseFacility();ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Allocation));tab(T::Materials);select(scrap,id);down();
        ASSERT_EQ(A::state(*app).tab,T::Materials);
        if(scenario==0){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=20;e.motion.y=20;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);}
        if(scenario==1){windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE);windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER);A::frame(*app);}
        if(scenario==2){windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST);windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);A::frame(*app);A::resume(*app);}
        if(scenario==3){windowEvent(SDL_EVENT_WINDOW_RESIZED);A::frame(*app);}
        if(scenario==4)wheel(center(uiAllocationListRect(T::Materials)));
        if(scenario==5){tab(T::Overview);tab(T::Materials);}
        if(scenario==6){control(C::Close);ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Allocation));}
        const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp)<<scenario;
    }
}
TEST_F(UiBaseAllocationTest,PauseAndInventoryOpeningCancelTheCurrentTransactionGesture) {
    const auto id=create(scrap);tab(T::Materials);select(scrap,id);down();A::pause(*app);A::frame(*app);A::resume(*app);
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);
    down();key(SDL_SCANCODE_TAB);A::frame(*app);up();EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,SaveFailureCannotChangeAuthorization) {
    create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);blockedSave();const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,SaveFailureCannotSubmitAnyWishItem) {
    prepareWish();blockedSave();const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,SaveFailureCannotProcessMaterialOrAdvanceItsPool) {
    const auto id=create(scrap);tab(T::Materials);select(scrap,id);blockedSave();const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);ASSERT_NE(profile().assets.find(id),nullptr);
}
TEST_F(UiBaseAllocationTest,SaveFailureCannotPersistWishFocus) {
    tab(T::Wishes);blockedSave();const auto fp=profileStateFingerprint(profile());control(C::Focus);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,ReloadRetainsPoliciesAndFocusButNoTransientSelection) {
    create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);control(C::Main);tab(T::Wishes);control(C::Focus);const auto focus=profile().basePriority.focus;
    control(C::Close);static_cast<void>(A::flow(*app).returnToMainMenu());ASSERT_TRUE(A::flow(*app).continueGame());A::frame(*app);ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Allocation));A::draw(*app);
    EXPECT_EQ(profile().baseSupplyPolicy.assignments.at(alpha_content::lootCola),BaseSupplyCategory::Food);EXPECT_EQ(profile().basePriority.focus,focus);EXPECT_EQ(A::state(*app).tab,T::Overview);EXPECT_TRUE(A::state(*app).contributions.empty());
}
TEST_F(UiBaseAllocationTest,LegacyDownOnlyCallbacksCannotConsumeOrChangePolicy) {
    prepareWish();const auto fp=profileStateFingerprint(profile());for(auto p:{Vec2{660,360},Vec2{940,624},Vec2{660,624},Vec2{82,630}})A::legacy(*app,p);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,MenuMotionAndClosingPreserveTheAcceptedWindowAimAnchor) {
    A::anchor(*app)=Vec2{320,210};const auto before=A::flow(*app).baseWorld().weaponAimWorldPosition();
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=1100;e.motion.y=90;e.motion.xrel=400;e.motion.yrel=80;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);control(C::Close);
    ASSERT_TRUE(A::anchor(*app));EXPECT_FLOAT_EQ(A::anchor(*app)->x,320);EXPECT_FLOAT_EQ(A::anchor(*app)->y,210);const auto after=A::flow(*app).baseWorld().weaponAimWorldPosition();EXPECT_FLOAT_EQ(after.x,before.x);EXPECT_FLOAT_EQ(after.y,before.y);
}
TEST_F(UiBaseAllocationTest,CompleteFailureReasonsCanBeReadWithTheWheel) {
    for(int i=0;i<30;++i)A::message(*app)+="A complete failure reason remains reachable.\n";A::draw(*app);ASSERT_GT(A::state(*app).feedbackMaximum,0);
    const auto fp=profileStateFingerprint(profile());wheel(center(uiAllocationFeedbackRect()));EXPECT_GT(A::state(*app).feedbackScroll,0);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,MovingTheParentContainerInvalidatesTheSameChildInstance) {
    const auto container=std::find_if(content.items().begin(),content.items().end(),[](const auto &d){return !d.containerCompartments.empty();});
    ASSERT_NE(container,content.items().end());const auto owner=create(container->definitionId),id=create(scrap);
    const auto compartment=ProfileContainerId::compartment(owner,0);
    const auto fit=findFirstProfileFit(profile(),content,compartment,content.item(scrap),ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    profile().assets.findMutable(id)->location=StoredAssetLocation{compartment,*fit};++profile().revision;
    tab(T::Materials);select(scrap,id);down();auto *bag=profile().assets.findMutable(owner);auto loc=std::get<StoredAssetLocation>(bag->location);loc.origin={16,8};bag->location=loc;
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_FALSE(A::state(*app).material);
}
TEST_F(UiBaseAllocationTest,InaccessibleLegacyOverflowCannotBecomeANewMaterialSource) {
    const auto id=create(scrap);profile().assets.findMutable(id)->location=StoredAssetLocation{ProfileContainerId::legacyOverflow(),{0,0}};++profile().revision;
    tab(T::Materials);const auto &rows=A::projection(*app).rows;EXPECT_TRUE(std::none_of(rows.begin(),rows.end(),[&](const auto &r){return r.instanceId==id;}));
    const auto fp=profileStateFingerprint(profile());control(C::Main);EXPECT_EQ(profileStateFingerprint(profile()),fp);
}
TEST_F(UiBaseAllocationTest,MaterialSourceUsesTheExistingWarehousePageOrder) {
    const auto id=create(scrap);profile().assets.findMutable(id)->location=StoredAssetLocation{ProfileContainerId::stash(),{16,8}};++profile().revision;
    tab(T::Materials);select(scrap,id);ASSERT_TRUE(A::projection(*app).material);EXPECT_EQ(A::projection(*app).material->source,"STASH 6 · 1,1");
}
TEST_F(UiBaseAllocationTest,ProfileIdentityChangeCannotReuseThePreviousHeldCommand) {
    const auto id=create(scrap);tab(T::Materials);select(scrap,id);down();profile().profileId="another-allocation-profile";
    const auto fp=profileStateFingerprint(profile());up();EXPECT_EQ(profileStateFingerprint(profile()),fp);EXPECT_EQ(A::state(*app).tab,T::Overview);EXPECT_FALSE(A::state(*app).material);
}
TEST_F(UiBaseAllocationTest,AllMaterialRowsAndPagesAreReachableWithoutChangingInventory) {
    std::vector<AssetInstanceId> ids;for(int i=0;i<16;++i)ids.push_back(create(scrap));tab(T::Materials);const auto fp=profileStateFingerprint(profile());
    for(auto id:ids){select(scrap,id);ASSERT_TRUE(A::state(*app).material);EXPECT_EQ(A::state(*app).material->id,id);}EXPECT_EQ(profileStateFingerprint(profile()),fp);
    const auto last=A::state(*app).pages[3];tab(T::Overview);tab(T::Materials);EXPECT_EQ(A::state(*app).pages[3],last);EXPECT_FALSE(A::state(*app).material);
}
TEST_F(UiBaseAllocationTest,StockNumbersAndSupplyDetailsActuallyRenderInsideTheirBounds) {
    const auto bright=[&](SDL_FRect box){
        int count=0;for(int y=static_cast<int>(box.y);y<box.y+box.h;++y)for(int x=static_cast<int>(box.x);x<box.x+box.w;++x){
            Uint8 r{},g{},b{},a{};if(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a) && r>150 && g>150 && b>150)++count;
        }return count;
    };
    A::draw(*app);SDL_RenderPresent(renderer);for(int i=0;i<4;++i)EXPECT_GT(bright({78+292.0F*i,222,248,36}),20);
    create(alpha_content::lootCola);tab(T::Supply);select(alpha_content::lootCola);A::draw(*app);SDL_RenderPresent(renderer);
    const auto &rows=A::projection(*app).rows;const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.definitionId==alpha_content::lootCola;});ASSERT_NE(found,rows.end());
    const auto rect=uiAllocationRowRect(T::Supply,static_cast<std::size_t>(found-rows.begin())%6);EXPECT_GT(bright({rect.x+58,rect.y+22,rect.w-70,18}),10);
}
TEST_F(UiBaseAllocationTest,OffscreenMatrixCoversAllFourTabsAndFailureStates) {
    const auto wishId=prepareWish();const auto material=create(scrap);create(electronics);create(alpha_content::lootCola);const auto seed=profile();
    const char *output=std::getenv("RAIDLINE_UI_ALLOCATION_REVIEW_DIR");std::filesystem::path destination;
    if(output && *output) {
        destination=std::filesystem::absolute(output).lexically_normal();
        const auto root=std::filesystem::path(__FILE__).parent_path().parent_path()/"build"/"windows-debug";
        ASSERT_EQ(std::filesystem::weakly_canonical(destination.parent_path()),std::filesystem::weakly_canonical(root));
        ASSERT_TRUE(destination.filename().string().starts_with("qa-ui-base-allocation-v1-"));ASSERT_FALSE(std::filesystem::exists(destination));ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    constexpr std::array names{"overview","report","shortage","supply","zero-policy","other-policy","wish-short","wish-ready","wish-done","material","material-full","long-reason","three-wishes","material-pages"};
    for(auto size:kUiWindowSizes)for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        detach();attach(size.width,size.height);A::text(*app).setLanguage(language);
        for(int scenario=0;scenario<14;++scenario) {
            profile()=seed;A::reset(*app);auto &s=A::state(*app);s.details=scenario==1;
            if(scenario==2){profile().baseResources.pool={};profile().baseResources.lastShortfall={1,2,3,4};profile().baseMorale.tier=BaseMoraleTier::Low;}
            if(scenario>=3 && scenario<=5){s.tab=T::Supply;s.supplySelection=alpha_content::lootCola;}
            if(scenario==4){profile().baseSupplyPolicy.assignments[alpha_content::lootCola]=BaseSupplyCategory::Food;std::vector<AssetInstanceId> ids;for(const auto &[id,a]:profile().assets.records())if(a.definitionId==alpha_content::lootCola)ids.push_back(id);for(auto id:ids)ASSERT_TRUE(profile().assets.erase(id));}
            if(scenario==5){s.category=BaseSupplyCategory::Security;s.supplySelection=electronics;profile().baseSupplyPolicy.assignments[electronics]=BaseSupplyCategory::Medical;}
            if(scenario>=6 && scenario<=8){s.tab=T::Wishes;A::refresh(*app);auto &p=A::projection(*app);const auto row=std::find_if(p.rows.begin(),p.rows.end(),[&](const auto &r){return r.definitionId==wishId;});ASSERT_NE(row,p.rows.end());s.contributions=row->assets;if(scenario==6)s.contributions.resize(1);if(scenario==8)profile().basePriority.wishes.front().fulfilled=true;}
            if(scenario==9 || scenario==10){s.tab=T::Materials;A::refresh(*app);const auto &rows=A::projection(*app).rows;const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto &r){return r.instanceId==material;});ASSERT_NE(row,rows.end());s.material=row->assets.front();if(scenario==10)profile().baseConstruction.materialUnits=content.maximumBaseConstructionMaterials();}
            if(scenario==12){s.tab=T::Wishes;profile().basePriority.frozenPopulation=24;profile().basePriority.wishes.clear();for(const auto &id:selectBasePriorityDefinitions(profile().basePriority.cycleIndex,24,content))profile().basePriority.wishes.push_back({id,false});ASSERT_EQ(profile().basePriority.wishes.size(),3U);profile().basePriority.focus=BaseWishInstanceId{profile().basePriority.cycleIndex,profile().basePriority.wishes.front().definitionId};s.focusOnly=true;}
            if(scenario==13){s.tab=T::Materials;for(int i=0;i<16;++i)create(scrap);A::refresh(*app);const auto &rows=A::projection(*app).rows;ASSERT_GT(rows.size(),12U);s.material=rows.back().assets.front();s.pages[3]=(rows.size()-1)/6;}
            A::refresh(*app);if(scenario==11)for(int i=0;i<30;++i)A::message(*app)+="A complete reason remains reachable.\n";
            const auto fp=profileStateFingerprint(profile());SDL_SetRenderDrawColor(renderer,10,16,18,255);SDL_RenderClear(renderer);A::draw(*app);SDL_RenderPresent(renderer);EXPECT_EQ(profileStateFingerprint(profile()),fp);
            if(!destination.empty())ASSERT_TRUE(IMG_SavePNG(surface,(destination/(std::string(names[scenario])+"-"+std::to_string(size.width)+"-"+uiLanguageCode(language)+".png")).string().c_str()));
        }
    }
}
TEST(UiAllocationGeometryTest,HiddenControlsNeverHitAcrossTabs) {
    UiAllocationState state;UiAllocationProjection projection;
    EXPECT_FALSE(hitUiAllocation(center(uiAllocationControlRect(C::Main)),state,projection));
    state.tab=T::Materials;EXPECT_FALSE(hitUiAllocation(center(uiAllocationControlRect(C::Food)),state,projection));
    EXPECT_FALSE(hitUiAllocation(center(uiAllocationControlRect(C::Focus)),state,projection));
    state.tab=T::Supply;EXPECT_EQ(hitUiAllocation(center(uiAllocationControlRect(C::Food)),state,projection)->control,C::Food);
}
} // namespace
