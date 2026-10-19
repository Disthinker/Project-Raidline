#include "app.h"
#include "alpha_content_ids.h"
#include "noise_decoy_fixture.h"
#include "raid_camera.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiQuickWheelTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static void frame(App &a, float seconds=0) { a.processEvents(); a.update(seconds); a.input_.endFrame(); }
    static void point(App &a, Vec2 p) {
        if(a.medicalWheelOpen_ || a.throwableWheelOpen_)a.wheelPointerOffset_={p.x-640,p.y-360};
        else a.pointerWorldPosition_=p;
    }
    static bool medical(const App &a) { return a.medicalWheelOpen_; }
    static bool throwable(const App &a) { return a.throwableWheelOpen_; }
    static auto aiming(const App &a) { return a.selectedThrowable_; }
    static auto medicalIds(const App &a) { return a.medicalWheelOptions_; }
    static auto throwableIds(const App &a) { return a.throwableWheelOptions_; }
    static auto medicalIndex(const App &a) { return a.medicalWheelSelectedIndex_; }
    static auto throwableIndex(const App &a) { return a.throwableWheelSelected_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static const auto &layout(App &a) { return a.quickWheelLayout_; }
    static const auto &projection(App &a) { return a.quickWheelProjection_; }
    static const auto &message(App &a) { return a.uiMessage_; }
    static bool paused(const App &a) { return a.pauseMenu_.isOpen(); }
    static bool capture(const App &a) { return a.shouldCaptureWorldPointer(); }
    static void captureNow(App &a) {a.syncRaidPointerCapture();}
    static void bindWindow(App &a,SDL_Window *w) {a.window_=w;}
    static auto offset(const App &a) {return a.wheelPointerOffset_;}
    static void clock(App &a,float dt) { a.uiClockSeconds_+=dt; }
    static void anchor(App &a) {
        a.worldAimPointerAnchor_=raidWorldToScreen(a.gameSession_.world().weaponAimWorldPosition(),a.raidWorldCameraOffset());
    }
    static void draw(App &a) { a.renderMedicalWheel(); a.renderThrowableUi(); }
    static bool beginBackdrop(App &a) {return a.quickWheelBackdrop_.begin(a.renderer_);}
    static bool composeBackdrop(App &a) {return a.quickWheelBackdrop_.compose();}
    static void attach(App &a,SDL_Renderer *r) { a.renderer_=r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); }
    static void detach(App &a) {
        for(auto &t:a.inventoryItemTextures_)t.reset();
        a.quickWheelBackdrop_.reset();a.uiTextRenderer_.shutdown(); a.renderer_=nullptr;
    }
    static void loadArt(App &a) {
        const auto root=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        for(std::size_t i=0;i<itemCount();++i) {
            const auto &d=publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i]=loadInventoryTexture(a.renderer_,root/d.inventoryTexturePath);
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid());
        }
    }
};
namespace {
using A=UiQuickWheelTestAccess;
void key(SDL_Scancode code,bool down) {
    SDL_Event e{}; e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP; e.key.scancode=code;e.key.down=down;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void mouse(Uint8 button,bool down) {
    SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button=button;e.button.down=down;e.button.x=640;e.button.y=360;ASSERT_TRUE(SDL_PushEvent(&e));
}
Vec2 direction(std::size_t i,std::size_t n) {
    const float angle=static_cast<float>(i)*2*std::numbers::pi_v<float>/static_cast<float>(n)-std::numbers::pi_v<float>/2;
    return {640+std::cos(angle)*180,360+std::sin(angle)*180};
}
void inside(SDL_FRect r,SDL_FRect parent={0,0,1280,720}) {
    EXPECT_GE(r.x,parent.x);EXPECT_GE(r.y,parent.y);
    EXPECT_LE(r.x+r.w,parent.x+parent.w+.02F);EXPECT_LE(r.y+r.h,parent.y+parent.h+.02F);
}
class UiQuickWheelTest:public testing::Test {
protected:
    std::unique_ptr<App> app;
    std::filesystem::path path;
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    AssetInstanceId rig{};std::vector<AssetInstanceId> medicines,decoys;
    auto &session() { return A::session(*app); }
    // Only this exclusively owned temporary test profile is mutated for edge cases.
    ProfileState &profile() { return const_cast<ProfileState &>(session().profile()); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path=std::filesystem::temp_directory_path()/("raidline-wheel-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        const auto &c=publishedContentRegistry();auto p=makeNewPublishedProfile("quick-wheel",c);
        p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
        equipDecoyTestGun(p,c);
        rig=p.assets.create(c.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
        auto put=[&](const ItemDefinitionId &definition,std::uint32_t quantity=1) {
            const auto &d=c.item(definition);const auto &pockets=c.item(p.assets.find(rig)->definitionId).containerCompartments;
            for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General) {
                auto container=ProfileContainerId::compartment(rig,n);
                if(auto fit=findFirstProfileFit(p,c,container,d,ItemOrientation::Degrees0))
                    return p.assets.create(d,StoredAssetLocation{container,*fit},quantity);
            }
            return AssetInstanceId{};
        };
        for(const auto &id:{alpha_content::medkit,alpha_content::bandage,alpha_content::tourniquet,alpha_content::painkiller}) {
            const auto asset=put(id);ASSERT_NE(asset,0U);medicines.push_back(asset);
        }
        for(int n=0;n<3;++n) { const auto asset=put(alpha_content::noiseDecoy,3);ASSERT_NE(asset,0U);decoys.push_back(asset); }
        p.currentHealth=70;p.medicalStatus={BleedingSeverity::Light,40000,1000,0,15000};
        const auto valid=validateProfileState(p,c);ASSERT_TRUE(valid.valid)<<valid.message;
        ASSERT_TRUE(SaveRepository(path).save(p,c.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());ASSERT_TRUE(A::flow(*app).deploy());A::frame(*app);
        attach(1280,720);
    }
    void attach(int w,int h) {
        surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app,renderer);A::text(*app).setRasterScale(w/1280.0F);
    }
    void detach() {
        if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;
    }
    void TearDown() override {
        detach();app.reset();SDL_Quit();
        if(path.parent_path().lexically_normal()==std::filesystem::temp_directory_path().lexically_normal() && path.filename().string().starts_with("raidline-wheel-")) {
            std::error_code ec;std::filesystem::remove_all(path,ec);
        }
    }
    void openMedical() { A::point(*app,{640,360});key(SDL_SCANCODE_5,true);A::frame(*app);ASSERT_TRUE(A::medical(*app)); }
    void openThrowable() { A::point(*app,{640,360});key(SDL_SCANCODE_4,true);A::frame(*app);ASSERT_TRUE(A::throwable(*app)); }
    void cancel() { key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,false);key(SDL_SCANCODE_4,false);key(SDL_SCANCODE_5,false);A::frame(*app); }
    void fillTwelve() {
        for(auto id:medicines)ASSERT_TRUE(profile().assets.erase(id));medicines.clear();
        for(auto id:decoys)ASSERT_TRUE(profile().assets.erase(id));decoys.clear();
        const auto &c=publishedContentRegistry();const auto &d=c.item(alpha_content::noiseDecoy);
        const auto &pockets=c.item(profile().assets.find(rig)->definitionId).containerCompartments;
        for(std::uint32_t n=0;n<pockets.size();++n)if(pockets[n].pocketKind==ContainerPocketKind::General)
            for(int y=0;y<pockets[n].height;++y)for(int x=0;x<pockets[n].width;++x)
                decoys.push_back(profile().assets.create(d,StoredAssetLocation{ProfileContainerId::compartment(rig,n),{x,y}},1));
        ++profile().revision;ASSERT_EQ(decoys.size(),20U);
    }
    void draw() {
        ASSERT_TRUE(A::beginBackdrop(*app));
        SDL_SetRenderDrawColor(renderer,62,77,67,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,118,128,113,255);SDL_FRect world{520,210,220,300};SDL_RenderFillRect(renderer,&world);
        ASSERT_TRUE(A::composeBackdrop(*app));
        A::draw(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
};

TEST_F(UiQuickWheelTest, MedicalDefaultAndCenterRetainOriginalSelectedEffect) {
    openMedical();EXPECT_EQ(A::medicalIds(*app),medicines);EXPECT_EQ(A::medicalIndex(*app),0U);
    A::point(*app,direction(2,8));A::frame(*app);EXPECT_EQ(A::medicalIndex(*app),2U);
    A::point(*app,{641,361});A::frame(*app);EXPECT_EQ(A::medicalIndex(*app),2U);
    for(auto id:medicines)EXPECT_GT(profile().assets.find(id)->remainingCharges,0U);
}
TEST_F(UiQuickWheelTest, MedicalReleaseStartsOriginalActionExactlyOnce) {
    const auto charges=profile().assets.find(medicines[0])->remainingCharges;
    openMedical();key(SDL_SCANCODE_5,false);A::frame(*app);
    EXPECT_FALSE(A::medical(*app));ASSERT_TRUE(session().raidActionState().active());
    const auto *action=std::get_if<MedicalRaidAction>(&*session().raidActionState().active());
    ASSERT_NE(action,nullptr);EXPECT_EQ(action->medicalAssetId,medicines[0]);
    EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,charges);
    A::frame(*app);EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, SameMedicalModelTotalsAllInstancesAndBindsLowestRemainingUses) {
    const auto &c=publishedContentRegistry();const auto &d=c.item(alpha_content::medkit);
    const auto container=ProfileContainerId::compartment(rig,0);
    const auto fit=findFirstProfileFit(profile(),c,container,d,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    const auto low=profile().assets.create(d,StoredAssetLocation{container,*fit});
    profile().assets.findMutable(low)->remainingCharges=1;++profile().revision;
    const auto oldUses=profile().assets.find(medicines[0])->remainingCharges;
    openMedical();draw();ASSERT_EQ(A::projection(*app).entries.size(),4);
    EXPECT_EQ(A::medicalIds(*app).front(),low);
    EXPECT_EQ(A::projection(*app).entries.front().quantity,2);
    EXPECT_EQ(A::projection(*app).entries.front().remainingUses,oldUses+1);
    key(SDL_SCANCODE_5,false);A::frame(*app);
    ASSERT_TRUE(session().raidActionState().active());
    const auto *action=std::get_if<MedicalRaidAction>(&*session().raidActionState().active());
    ASSERT_NE(action,nullptr);EXPECT_EQ(action->medicalAssetId,low);
    A::frame(*app,6);
    EXPECT_EQ(profile().assets.find(low),nullptr);
    EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,oldUses);
}
TEST_F(UiQuickWheelTest, EmptySectorReleaseCannotUseOrAimAnotherItem) {
    const auto fingerprint=profileStateFingerprint(profile());
    openMedical();A::point(*app,direction(7,8));A::frame(*app);
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
    openThrowable();A::point(*app,direction(7,8));A::frame(*app);
    key(SDL_SCANCODE_4,false);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST_F(UiQuickWheelTest, HiddenRelativeWheelRoundTripKeepsWindowAimAndConsumesNoMotion) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_VIDEO));
    auto *window=SDL_CreateWindow("wheel input regression",1280,720,SDL_WINDOW_HIDDEN);ASSERT_NE(window,nullptr);
    struct WindowGuard {App *a;SDL_Window *w;~WindowGuard(){A::bindWindow(*a,nullptr);SDL_DestroyWindow(w);}} guard{app.get(),window};
    A::bindWindow(*app,window);A::anchor(*app);A::captureNow(*app);
    ASSERT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    const auto before=session().world().weaponAimWorldPosition();
    openMedical();A::captureNow(*app);EXPECT_FALSE(SDL_CursorVisible());
    EXPECT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.xrel=110;event.motion.yrel=-90;
    ASSERT_TRUE(SDL_PushEvent(&event));A::frame(*app);A::captureNow(*app);
    EXPECT_NEAR(A::offset(*app).x,110,.01);EXPECT_NEAR(A::offset(*app).y,-90,.01);
    cancel();A::captureNow(*app);A::frame(*app);
    EXPECT_TRUE(SDL_GetWindowRelativeMouseMode(window));
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().x,before.x);
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().y,before.y);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, InapplicableDefaultIsNotSilentlyReplacedWithUsableBandage) {
    profile().currentHealth=100;++profile().revision;openMedical();draw();
    EXPECT_FALSE(A::projection(*app).entries[0].usable);EXPECT_TRUE(A::projection(*app).entries[1].usable);
    key(SDL_SCANCODE_5,false);A::frame(*app);
    EXPECT_FALSE(session().raidActionState().active());EXPECT_EQ(A::message(*app),"SELECTED MEDICAL ITEM IS NOT APPLICABLE");
}
TEST_F(UiQuickWheelTest, MedicalEscapeCancelsWithoutUsePauseQuitOrFire) {
    const auto revision=profile().revision;openMedical();cancel();
    EXPECT_FALSE(A::medical(*app));EXPECT_FALSE(A::paused(*app));EXPECT_FALSE(session().raidActionState().active());
    EXPECT_EQ(A::flow(*app).state(),GameFlowState::Raid);EXPECT_EQ(profile().revision,revision);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(UiQuickWheelTest, MedicalRightClickDoesNotAddANewCancelControl) {
    openMedical();mouse(SDL_BUTTON_RIGHT,true);A::frame(*app);EXPECT_TRUE(A::medical(*app));
    EXPECT_FALSE(session().raidActionState().active());
}
TEST_F(UiQuickWheelTest, ThrowableReleaseOnlyArmsAndFreshClickConsumesOne) {
    openThrowable();EXPECT_EQ(A::throwableIds(*app),(std::vector<AssetInstanceId>{decoys.front()}));key(SDL_SCANCODE_4,false);A::frame(*app);
    ASSERT_EQ(A::aiming(*app),decoys[0]);EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
    mouse(SDL_BUTTON_LEFT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    EXPECT_EQ(profile().assets.find(decoys[0])->quantity,2U);EXPECT_FALSE(session().world().shotFiredLastUpdate());
    A::frame(*app);EXPECT_EQ(profile().assets.find(decoys[0])->quantity,2U);
}
TEST_F(UiQuickWheelTest, TwentyStacksGroupOnceAndOneConfirmedThrowConsumesOneInstance) {
    fillTwelve();openThrowable();draw();
    ASSERT_EQ(A::throwableIds(*app).size(),1);ASSERT_EQ(A::projection(*app).entries.size(),1);
    EXPECT_EQ(A::projection(*app).entries.front().quantity,20);EXPECT_EQ(A::layout(*app).markers.size(),8);
    A::point(*app,direction(0,8));A::frame(*app);A::point(*app,{640,360});A::frame(*app);
    EXPECT_EQ(A::throwableIndex(*app),0);key(SDL_SCANCODE_4,false);A::frame(*app);
    EXPECT_EQ(A::aiming(*app),decoys.front());
    mouse(SDL_BUTTON_LEFT,true);A::frame(*app);
    EXPECT_EQ(profile().assets.find(decoys.front()),nullptr);
    for(std::size_t i=1;i<decoys.size();++i)EXPECT_EQ(profile().assets.find(decoys[i])->quantity,1);
}
TEST_F(UiQuickWheelTest, ThrowableCancelAndHeldMouseCannotBecomeAnAccidentalShot) {
    mouse(SDL_BUTTON_LEFT,true);openThrowable();key(SDL_SCANCODE_4,false);A::frame(*app);
    EXPECT_TRUE(A::aiming(*app));EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
    mouse(SDL_BUTTON_RIGHT,true);A::frame(*app);EXPECT_FALSE(A::aiming(*app));
    A::frame(*app);EXPECT_FALSE(session().world().shotFiredLastUpdate());EXPECT_EQ(profile().assets.find(decoys[0])->quantity,3U);
}
TEST_F(UiQuickWheelTest, LostFocusClosesBothSelectorsWithoutConsumption) {
    openMedical();SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    EXPECT_FALSE(A::medical(*app));EXPECT_FALSE(A::throwable(*app));EXPECT_FALSE(session().raidActionState().active());
    for(auto id:decoys)EXPECT_EQ(profile().assets.find(id)->quantity,3U);
}
TEST_F(UiQuickWheelTest, TwoSelectorsRemainMutuallyExclusive) {
    openThrowable();key(SDL_SCANCODE_5,true);A::frame(*app);EXPECT_FALSE(A::medical(*app));EXPECT_TRUE(A::throwable(*app));
}
TEST_F(UiQuickWheelTest, MedicalModalRoundTripPreservesWindowAimAnchor) {
    A::anchor(*app); // Normal world capture has already established this anchor.
    const auto before=session().world().weaponAimWorldPosition();openMedical();EXPECT_FALSE(A::capture(*app));
    A::point(*app,{100,600});A::frame(*app);cancel();
    const auto after=session().world().weaponAimWorldPosition();EXPECT_FLOAT_EQ(before.x,after.x);EXPECT_FLOAT_EQ(before.y,after.y);
}
TEST_F(UiQuickWheelTest, MissingMedicalCandidateFailsInsteadOfChoosingAnotherInstance) {
    openMedical();ASSERT_TRUE(profile().assets.erase(medicines[0]));++profile().revision;draw();
    EXPECT_EQ(A::projection(*app).entries[0].instanceId,medicines[0]);EXPECT_FALSE(A::projection(*app).entries[0].usable);
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
}
TEST_F(UiQuickWheelTest, MissingThrowableCandidateCannotConsumeAReplacement) {
    openThrowable();ASSERT_TRUE(profile().assets.erase(decoys[0]));++profile().revision;draw();
    key(SDL_SCANCODE_4,false);A::frame(*app);mouse(SDL_BUTTON_LEFT,true);A::frame(*app);
    EXPECT_EQ(profile().assets.find(decoys[1])->quantity,3U);EXPECT_FALSE(session().world().noiseDecoy().flight());
}
TEST_F(UiQuickWheelTest, EmptyChestShowsEightEmptySectorsAndReleaseDoesNothing) {
    for(auto id:medicines)ASSERT_TRUE(profile().assets.erase(id));
    for(auto id:decoys)ASSERT_TRUE(profile().assets.erase(id));++profile().revision;
    openMedical();draw();EXPECT_EQ(A::layout(*app).markers.size(),8);EXPECT_TRUE(A::projection(*app).entries.empty());
    key(SDL_SCANCODE_5,false);A::frame(*app);EXPECT_FALSE(session().raidActionState().active());
    openThrowable();draw();EXPECT_EQ(A::layout(*app).markers.size(),8);
    key(SDL_SCANCODE_4,false);A::frame(*app);EXPECT_FALSE(A::aiming(*app));EXPECT_FALSE(session().world().noiseDecoy().flight());
}
TEST_F(UiQuickWheelTest, ProjectionRefreshesHealthAndBleedingWithoutPollingTheWholeInventory) {
    UiQuickWheelProjection projection;const auto &c=publishedContentRegistry();
    ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));const auto generation=projection.generation;
    EXPECT_FALSE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));
    --profile().medicalStatus.lightBleedingRemainingMs;--profile().medicalStatus.painScreamRemainingMs;
    EXPECT_FALSE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_EQ(projection.generation,generation);
    profile().currentHealth=100;ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_FALSE(projection.entries[0].usable);
    profile().medicalStatus.bleeding=BleedingSeverity::None;ASSERT_TRUE(projection.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_FALSE(projection.entries[1].usable);
}
TEST_F(UiQuickWheelTest, RenderingAndHighlightAnimationAreReadOnlyAndReuseTextTextures) {
    openMedical();draw();const auto generation=A::projection(*app).generation;const auto count=A::text(*app).cachedLabelCount();
    const auto revision=profile().revision;const auto charges=profile().assets.find(medicines[0])->remainingCharges;
    for(int i=0;i<12;++i){A::clock(*app,.01F);draw();}
    EXPECT_EQ(A::projection(*app).generation,generation);EXPECT_EQ(A::text(*app).cachedLabelCount(),count);
    EXPECT_EQ(profile().revision,revision);EXPECT_EQ(profile().assets.find(medicines[0])->remainingCharges,charges);
}
TEST_F(UiQuickWheelTest, CompactNamesUseActualDefinitionsAndKeepFullDetailNames) {
    const auto &d=publishedContentRegistry().item(ItemDefinitionId{"item.weapon.rifle_5_45_service"});
    const auto original=d.displayName;
    EXPECT_EQ(compactUiItemName(d,UiLanguage::SimplifiedChinese),"5.45步枪");
    EXPECT_EQ(compactUiItemName(d,UiLanguage::English),"5.45 Rifle");
    EXPECT_EQ(d.displayName,original);
    EXPECT_EQ(compactUiItemName(publishedContentRegistry().item(alpha_content::ammunition),UiLanguage::SimplifiedChinese),"9mm标");
}
TEST_F(UiQuickWheelTest, CacheRefreshKeepsOriginalInstanceOrderAndCorrectCountMeaning) {
    UiQuickWheelProjection p;const auto &c=publishedContentRegistry();
    ASSERT_TRUE(p.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));
    profile().assets.findMutable(medicines[0])->remainingCharges=1;++profile().revision;
    ASSERT_TRUE(p.refresh(profile(),c,medicines,UiQuickWheelKind::Medical));EXPECT_EQ(p.entries[0].remainingUses,1);
    auto ids=decoys;std::reverse(ids.begin(),ids.end());ASSERT_TRUE(p.refresh(profile(),c,ids,UiQuickWheelKind::Throwable));
    ASSERT_EQ(p.entries.size(),1);EXPECT_EQ(p.entries.front().instanceId,ids.front());EXPECT_EQ(p.entries.front().quantity,9);
}
TEST_F(UiQuickWheelTest, SharedDrawingPreservesRendererStateForFollowingAcceptedUi) {
    openMedical();SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer,21,41,61,81);SDL_SetRenderScale(renderer,.9F,.9F);
    SDL_Rect clip{50,50,1100,600};ASSERT_TRUE(SDL_SetRenderClipRect(renderer,&clip));A::draw(*app);
    SDL_BlendMode blend{};SDL_GetRenderDrawBlendMode(renderer,&blend);EXPECT_EQ(blend,SDL_BLENDMODE_ADD);
    Uint8 r{},g{},b{},alpha{};SDL_GetRenderDrawColor(renderer,&r,&g,&b,&alpha);
    EXPECT_EQ(r,21);EXPECT_EQ(g,41);EXPECT_EQ(b,61);EXPECT_EQ(alpha,81);
    float x{},y{};SDL_GetRenderScale(renderer,&x,&y);EXPECT_FLOAT_EQ(x,.9F);EXPECT_FLOAT_EQ(y,.9F);
    SDL_Rect after{};SDL_GetRenderClipRect(renderer,&after);EXPECT_TRUE(SDL_RenderClipEnabled(renderer));
    EXPECT_EQ(after.x,clip.x);EXPECT_EQ(after.y,clip.y);EXPECT_EQ(after.w,clip.w);EXPECT_EQ(after.h,clip.h);
}

TEST_F(UiQuickWheelTest, ThreeSizesTwoLanguagesAndFiveScenariosHaveReadableOffscreenPreviews) {
    for(int scenario=0;scenario<5;++scenario) {
        if(scenario==0)openMedical();
        if(scenario==1){profile().currentHealth=100;profile().medicalStatus={};++profile().revision;}
        if(scenario==2){cancel();openThrowable();}
        if(scenario==3){cancel();for(std::size_t i=1;i<decoys.size();++i)ASSERT_TRUE(profile().assets.erase(decoys[i]));decoys.resize(1);++profile().revision;openThrowable();}
        if(scenario==4){cancel();fillTwelve();openThrowable();A::point(*app,direction(0,8));A::frame(*app);}
        for(auto size:kUiWindowSizes)for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
            detach();attach(size.width,size.height);A::loadArt(*app);A::text(*app).setLanguage(language);
            A::clock(*app,.2F);draw();inside(A::layout(*app).panel);EXPECT_TRUE(A::layout(*app).rows.empty());
            for(const auto &r:A::layout(*app).rows){inside(r.bounds,A::layout(*app).panel);inside(r.name.bounds,r.bounds);inside(r.count.bounds,r.bounds);}
            for(const auto &r:A::layout(*app).markers)inside(r,A::layout(*app).panel);
            if(const auto dir=std::getenv("RAIDLINE_QUICK_WHEEL_QA_DIR")) {
                std::filesystem::create_directories(dir);
                const auto name=std::to_string(scenario)+"-"+std::to_string(size.width)+(language==UiLanguage::English?"-en.png":"-zh.png");
                const auto utf8=(std::filesystem::path(dir)/name).u8string();
                ASSERT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(utf8.c_str())))<<SDL_GetError();
            }
        }
    }
}

TEST(UiQuickWheelLayoutTest, NineTypesUseTwoFixedEightSectorPagesWithoutLosingSelection) {
    std::vector<UiQuickWheelEntry> entries;
    for(int i=0;i<9;++i)entries.push_back({static_cast<AssetInstanceId>(i+1),"type","x1","",true});
    for(std::size_t page=0;page<2;++page) {
        const auto l=layoutUiQuickWheel(entries,page*8,UiQuickWheelKind::Throwable,UiLanguage::English,[](auto,int){return 1.0F;},page);
        ASSERT_EQ(l.markers.size(),8);EXPECT_EQ(l.page,page);EXPECT_EQ(l.selected,page*8);EXPECT_FALSE(l.range.text.empty());
        for(std::size_t i=0;i<8;++i) {
            const auto p=direction(i,8);const auto m=l.markers[i];
            EXPECT_NEAR(m.x+m.w/2,640+(p.x-640)*160/180.0F,.02F);
            EXPECT_NEAR(m.y+m.h/2,360+(p.y-360)*160/180.0F,.02F);
        }
        EXPECT_TRUE(l.rows.empty());
    }
}
TEST(UiQuickWheelLayoutTest, RingOnlyLayoutHasNoSidePanelOrSurroundingHints) {
    const std::vector<UiQuickWheelEntry> entries{{1,"Medkit","x1","",true}};
    for(auto kind:{UiQuickWheelKind::Medical,UiQuickWheelKind::Throwable}) {
        const auto l=layoutUiQuickWheel(entries,0,kind,UiLanguage::English,[](auto,int){return 1.0F;});
        EXPECT_TRUE(l.title.text.empty());EXPECT_TRUE(l.subtitle.text.empty());EXPECT_TRUE(l.hint.text.empty());
        EXPECT_TRUE(l.rows.empty());EXPECT_EQ(l.summary.w,0);EXPECT_EQ(l.summary.h,0);EXPECT_EQ(l.markers.size(),8);
    }
}
TEST(UiQuickWheelLayoutTest, EmphasisStartsImmediatelyAndCannotDelayConfirmation) {
    UiQuickWheelEmphasis e;e.select(3,UiQuickWheelKind::Medical,5);EXPECT_EQ(e.id,3U);EXPECT_FLOAT_EQ(e.amount(5),0);
    EXPECT_NEAR(e.amount(5.06F),.5F,.001F);EXPECT_FLOAT_EQ(e.amount(6),1);
    e.select(4,UiQuickWheelKind::Medical,6);EXPECT_EQ(e.id,4U);EXPECT_FLOAT_EQ(e.amount(6),0);
}
TEST(UiQuickWheelLayoutTest, EmptyProjectionKeepsEightEmptySectorsWithoutInventedItems) {
    const auto l=layoutUiQuickWheel({},99,UiQuickWheelKind::Medical,UiLanguage::English,[](auto s,int size){return s.size()*size*.6F;});
    EXPECT_TRUE(l.rows.empty());EXPECT_EQ(l.markers.size(),8);EXPECT_TRUE(l.name.text.empty());
}
}
