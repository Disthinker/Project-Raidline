#include "app.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include <vector>

struct RaidSceneContainerUiTestAccess {
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static void frame(App &a, float dt=0) { a.windowHasInputFocus_=true; a.processEvents(); a.update(dt); a.input_.endFrame(); }
    static bool capture(App &a) { return a.shouldCaptureWorldPointer(); }
    static bool open(App &a) { return a.inventoryOverlayState_.isOpen(); }
    static auto external(App &a) { return a.openedRaidSceneContainerId_; }
    static bool drag(App &a) { return a.profileInventoryInteraction_.pointerGestureActive(); }
    static void dragFrom(App &a, AssetInstanceId id) {
        const auto &p=a.gameSession_.profile(); const auto *asset=p.assets.find(id);
        const auto pos=std::get<StoredAssetLocation>(asset->location).origin;
        const auto footprint=inventoryFootprint(publishedContentRegistry().item(asset->definitionId),asset->orientation);
        ASSERT_TRUE(a.profileInventoryInteraction_.beginPointerPress({id,p.revision,asset->location,0,asset->orientation},pos,pos,{680,140},{asset->orientation,footprint,true,{0,0}}));
        a.profileInventoryInteraction_.updatePointerPosition({720,190}, std::nullopt);
    }
    static void quick(App &a, MousePosition pos) { a.handleProfileInventoryUiEvent(InventoryQuickTransferEvent{pos}, true); }
    static bool renderer(App &a, SDL_Renderer *r) { a.renderer_=r; return a.uiTextRenderer_.initialize(r); }
    static void paint(App &a, UiLanguage lang) { a.uiTextRenderer_.setLanguage(lang); a.renderInventoryOverlay(); }
    static void expectedCorpseTitle(App &a) {
        SDL_SetRenderDrawColor(a.renderer_, 230, 230, 210, 255);
        a.uiTextRenderer_.render(a.renderer_, 668, 70, "尸体");
        a.uiTextRenderer_.render(a.renderer_, 668, 112, "尸体 | 储物格 1");
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_=nullptr; }
};
namespace {
using A=RaidSceneContainerUiTestAccess;
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=code; e.key.down=true; ASSERT_TRUE(SDL_PushEvent(&e));
    e.type=SDL_EVENT_KEY_UP; e.key.down=false; ASSERT_TRUE(SDL_PushEvent(&e));
}
class RaidSceneContainerUiTest : public testing::Test {
protected:
    std::unique_ptr<App> app;
    std::filesystem::path path=std::filesystem::temp_directory_path()/ ("raidline-scene-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AssetInstanceId box{}, bag{};
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    GameSession &session() { return A::flow(*app).gameSession(); }
    void position(Vec2 center) {
        const float half=session().world().player().size()*0.5F;
        ASSERT_TRUE(const_cast<Player&>(session().world().player()).setPosition({center.x-half,center.y-half}));
    }
    Vec2 boxPosition() { return std::get<RaidSceneContainerAssetLocation>(session().profile().assets.find(box)->location).position; }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        const auto &content=publishedContentRegistry();
        auto p=makeNewAlphaProfile("scene-ui",content);
        bag=p.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}),EquippedAssetLocation{EquipmentSlotKind::Backpack});
        ASSERT_TRUE(SaveRepository{path}.save(p,content.contentVersion()).succeeded);
        app=std::make_unique<App>(); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
        const auto interior=session().profile().pendingRaid->interiors.front();
        position({interior.exteriorEntrance.position.x+interior.exteriorEntrance.size.x/2,interior.exteriorEntrance.position.y+interior.exteriorEntrance.size.y/2});
        GameplayInput enter; enter.interactJustPressed=true; A::flow(*app).update(enter,0);
        ASSERT_EQ(session().world().activeRaidSpaceId(),interior.id);
        for (const auto &[id,a]:session().profile().assets.records())
            if (const auto *loc=std::get_if<RaidSceneContainerAssetLocation>(&a.location);loc && loc->spaceId==interior.id) box=id;
        ASSERT_NE(box,0U); position(boxPosition()); A::frame(*app);
    }
    void TearDown() override {
        if (app) A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); app.reset(); SDL_Quit();
        std::error_code ec; std::filesystem::remove_all(path,ec);
    }
    void open() { key(SDL_SCANCODE_F); A::frame(*app); ASSERT_EQ(A::external(*app),box); ASSERT_TRUE(A::open(*app)); }
    void corpse()
    {
        const auto exit=session().profile().pendingRaid->interiors.front().interiorExit;
        position({exit.position.x+exit.size.x/2,exit.position.y+exit.size.y/2});
        GameplayInput leave; leave.interactJustPressed=true; A::flow(*app).update(leave,0);
        ASSERT_EQ(session().world().activeRaidSpaceId(),outdoorRaidSpaceId());
        const auto &snapshot=session().profile().pendingRaid->enemies;
        const auto source=std::find_if(snapshot.begin(),snapshot.end(),[](const auto &e){return !e.sourceEnemyId.empty();});
        ASSERT_NE(source,snapshot.end());
        const auto &actors=session().world().enemies();
        const auto enemy=std::find_if(actors.begin(),actors.end(),[&](const auto &e){return e.position().x==source->position.x && e.position().y==source->position.y;});
        ASSERT_NE(enemy,actors.end()); static_cast<void>(const_cast<Enemy&>(*enemy).takeDamage(enemy->maxHealth()));
        A::flow(*app).update({},0.01F);
        box=0;
        for(const auto &[id,a]:session().profile().assets.records())
            if(const auto *body=std::get_if<RaidCorpseContainerAssetLocation>(&a.location);body && body->deathPosition)
            {box=id;position(*body->deathPosition);break;}
        ASSERT_NE(box,0U); A::frame(*app);
    }
};
TEST_F(RaidSceneContainerUiTest, CorpseActualFEntryReleasesPointerAndReopensWithoutReroll)
{
    corpse(); ASSERT_TRUE(session().world().pendingCorpseDeaths().empty());
    const auto hash=profileStateFingerprint(session().profile()); open(); EXPECT_FALSE(A::capture(*app));
    key(SDL_SCANCODE_ESCAPE);A::frame(*app);EXPECT_FALSE(A::open(*app));EXPECT_TRUE(A::capture(*app));
    open();EXPECT_EQ(profileStateFingerprint(session().profile()),hash);
}
TEST_F(RaidSceneContainerUiTest, CorpseActualQuickTransferDepositAndBilingualTitleKeepTheRoot)
{
    corpse();open();
    const auto item=assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0)).front()->instanceId;
    const auto initial=std::get<StoredAssetLocation>(session().profile().assets.find(item)->location).origin;
    A::quick(*app,{668+initial.x*24.0F+4,132+initial.y*24.0F+4}); ASSERT_TRUE(assetIsCarried(session().profile(),item));
    const auto carried=std::get<StoredAssetLocation>(session().profile().assets.find(item)->location);
    A::quick(*app,{214+carried.origin.x*20.0F+4,520+carried.origin.y*20.0F+4}); EXPECT_EQ(raidSceneContainerRoot(session().profile(),item),box);
    surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
    renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);ASSERT_TRUE(A::renderer(*app,renderer));
    for(auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese})
    {
        SDL_SetRenderDrawColor(renderer,0,0,0,255);ASSERT_TRUE(SDL_RenderClear(renderer));A::paint(*app,language);ASSERT_TRUE(SDL_RenderPresent(renderer));
        std::size_t bright{};
        for(int y=68;y<121;++y)for(int x=668;x<1248;++x){Uint8 r{},g{},b{},a{};if(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a)&&r>150&&g>150&&b>150)++bright;}
        EXPECT_GT(bright,60U);
    }
    EXPECT_TRUE(std::holds_alternative<RaidCorpseContainerAssetLocation>(session().profile().assets.find(box)->location));
    EXPECT_TRUE(validateProfileState(session().profile(),publishedContentRegistry()).valid);
}
TEST_F(RaidSceneContainerUiTest, CorpseChineseGridTitleMatchesReadableReferenceWithoutOverlap)
{
    corpse();
    open();
    const auto fingerprint = profileStateFingerprint(session().profile());
    surface = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(surface, nullptr);
    renderer = SDL_CreateSoftwareRenderer(surface);
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(A::renderer(*app, renderer));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    ASSERT_TRUE(SDL_RenderClear(renderer));
    A::paint(*app, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(SDL_RenderPresent(renderer));

    const auto titleMask = [&]() {
        std::vector<bool> mask;
        for (int y = 70; y < 132; ++y) {
            if (y >= 90 && y < 112) continue;
            for (int x = 668; x < 1248; ++x) {
                Uint8 r{}, g{}, b{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &alpha));
                mask.push_back(r == 230 && g == 230 && b == 210);
            }
        }
        return mask;
    };
    const auto actual = titleMask();
    if (const char *qaDirectory = std::getenv("RAIDLINE_UI_ZH_QA_DIR")) {
        std::filesystem::create_directories(qaDirectory);
        const auto image = std::filesystem::path{qaDirectory} / "corpse-zh.bmp";
        ASSERT_TRUE(SDL_SaveBMP(surface, image.string().c_str()));
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    ASSERT_TRUE(SDL_RenderClear(renderer));
    A::expectedCorpseTitle(*app);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    const auto expected = titleMask();
    ASSERT_GT(std::count(expected.begin(), expected.end(), true), 0);
    EXPECT_TRUE(actual == expected)
        << "The real corpse title must match the Chinese reference without hint overlap";
    EXPECT_EQ(profileStateFingerprint(session().profile()), fingerprint);
}

TEST_F(RaidSceneContainerUiTest, CorpseControlRevokesPanelAndQueuedDragWithoutMutation)
{
    corpse();open();const auto item=assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0)).front()->instanceId;
    A::dragFrom(*app,item);ASSERT_TRUE(A::drag(*app));const auto hash=profileStateFingerprint(session().profile());
    static_cast<void>(const_cast<Player&>(session().world().player()).applyControl(1));
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;e.button.x=400;e.button.y=400;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    EXPECT_FALSE(A::open(*app));EXPECT_FALSE(A::drag(*app));EXPECT_FALSE(A::external(*app));EXPECT_EQ(profileStateFingerprint(session().profile()),hash);
}
TEST_F(RaidSceneContainerUiTest, CorpseLeavingRangeRevokesPanelAndWorldKeepsRunning)
{
    corpse();open();const auto old=session().world().player().position();
    SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_D;e.key.down=true;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app,0.1F);
    EXPECT_GT(session().world().player().position().x,old.x);
    e.type=SDL_EVENT_KEY_UP;e.key.down=false;ASSERT_TRUE(SDL_PushEvent(&e));position({old.x+200,old.y+25});A::frame(*app);
    EXPECT_FALSE(A::open(*app));EXPECT_FALSE(A::external(*app));EXPECT_TRUE(A::capture(*app));
}
TEST_F(RaidSceneContainerUiTest, CorpseSpaceRoundTripClosesDragAndKeepsFinalPositionAndContents)
{
    corpse();open();const auto location=std::get<RaidCorpseContainerAssetLocation>(session().profile().assets.find(box)->location);
    const auto item=assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0)).front()->instanceId;
    A::dragFrom(*app,item);const auto interior=session().profile().pendingRaid->interiors.front();
    position({interior.exteriorEntrance.position.x+interior.exteriorEntrance.size.x/2,interior.exteriorEntrance.position.y+interior.exteriorEntrance.size.y/2});
    GameplayInput transition;transition.interactJustPressed=true;A::flow(*app).update(transition,0);A::frame(*app);
    ASSERT_EQ(session().world().activeRaidSpaceId(),interior.id);EXPECT_FALSE(A::open(*app));EXPECT_FALSE(A::drag(*app));
    EXPECT_FALSE(session().raidSceneContainerAvailable(box));
    position({interior.interiorExit.position.x+interior.interiorExit.size.x/2,interior.interiorExit.position.y+interior.interiorExit.size.y/2});
    A::flow(*app).update(transition,0);A::frame(*app);ASSERT_EQ(session().world().activeRaidSpaceId(),outdoorRaidSpaceId());
    position(*location.deathPosition);open();EXPECT_EQ(std::get<RaidCorpseContainerAssetLocation>(session().profile().assets.find(box)->location),location);
    EXPECT_EQ(raidSceneContainerRoot(session().profile(),item),box);
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"F: SEARCH CORPSE"),"F：搜索尸体");
    EXPECT_NE(localizeUiText(UiLanguage::SimplifiedChinese,"CORPSE | COMPARTMENT 1").find("尸体"),std::string::npos);
}
TEST_F(RaidSceneContainerUiTest, RealFEntryReleasesPointerAndEscapeReopensWithoutReroll) {
    EXPECT_TRUE(A::capture(*app)); const auto hash=profileStateFingerprint(session().profile());
    open(); EXPECT_FALSE(A::capture(*app)); EXPECT_EQ(profileStateFingerprint(session().profile()),hash);
    EXPECT_FALSE(session().world().raidSession().signalCalledLastUpdate());
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); EXPECT_FALSE(A::open(*app)); EXPECT_FALSE(A::external(*app)); EXPECT_TRUE(A::capture(*app));
    open(); EXPECT_EQ(profileStateFingerprint(session().profile()),hash);
}
TEST_F(RaidSceneContainerUiTest, LeavingRangeRevokesActualPanelAndDragBeforeQueuedRelease) {
    open(); const auto item=assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0)).front()->instanceId;
    A::dragFrom(*app,item); ASSERT_TRUE(A::drag(*app)); const auto hash=profileStateFingerprint(session().profile());
    auto far=boxPosition(); far.x+=100; position(far);
    SDL_Event e{}; e.type=SDL_EVENT_MOUSE_BUTTON_UP; e.button.button=SDL_BUTTON_LEFT; e.button.x=400; e.button.y=400; ASSERT_TRUE(SDL_PushEvent(&e));
    A::frame(*app); EXPECT_FALSE(A::open(*app)); EXPECT_FALSE(A::external(*app)); EXPECT_FALSE(A::drag(*app));
    EXPECT_EQ(profileStateFingerprint(session().profile()),hash); EXPECT_TRUE(A::capture(*app));
}
TEST_F(RaidSceneContainerUiTest, QuickTransferUsesTheActualExternalGridAndSupportsPutBack) {
    open(); const auto item=assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0)).front()->instanceId;
    const auto *asset=session().profile().assets.find(item); const auto origin=std::get<StoredAssetLocation>(asset->location).origin;
    const auto definition=asset->definitionId;
    const auto boxedQuantity=[&] { std::uint32_t total{}; for (const auto *a:assetsInContainer(session().profile(),ProfileContainerId::compartment(box,0))) if (a->definitionId==definition) total+=a->quantity; return total; };
    const auto originalQuantity=boxedQuantity();
    A::quick(*app,{668+origin.x*24.0F+4,132+origin.y*24.0F+4});
    EXPECT_TRUE(assetIsCarried(session().profile(),item));
    const auto *after=session().profile().assets.find(item); ASSERT_NE(after,nullptr);
    const auto stored=std::get<StoredAssetLocation>(after->location);
    EXPECT_EQ(stored.container.ownerAssetId,bag);
    // Depositing a carried item uses the same transaction consumer.
    A::quick(*app,{214+stored.origin.x*20.0F+4,520+stored.origin.y*20.0F+4});
    EXPECT_FALSE(assetIsCarried(session().profile(),item));
    EXPECT_EQ(boxedQuantity(),originalQuantity);
    if (session().profile().assets.find(item)) EXPECT_EQ(raidSceneContainerRoot(session().profile(),item),box);
    EXPECT_TRUE(validateProfileState(session().profile(),publishedContentRegistry()).valid);
}
TEST_F(RaidSceneContainerUiTest, ActualEnglishAndChineseTitlesAreVisibleWithoutWholeBoxPickup) {
    open(); surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface,nullptr);
    renderer=SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer,nullptr); ASSERT_TRUE(A::renderer(*app,renderer));
    for (auto language:{UiLanguage::English,UiLanguage::SimplifiedChinese}) {
        SDL_SetRenderDrawColor(renderer,0,0,0,255); ASSERT_TRUE(SDL_RenderClear(renderer)); A::paint(*app,language); ASSERT_TRUE(SDL_RenderPresent(renderer));
        for (const auto rect:{SDL_Rect{668,68,580,23},SDL_Rect{668,98,580,23}}) {
            std::size_t bright{};
            for (int y=rect.y;y<rect.y+rect.h;++y) for (int x=rect.x;x<rect.x+rect.w;++x) {
                Uint8 r{},g{},b{},a{}; if (SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a)&&r>150&&g>150&&b>150) ++bright;
            }
            EXPECT_GT(bright,30U);
        }
    }
    const auto hash=profileStateFingerprint(session().profile());
    SDL_Event e{}; e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.button=SDL_BUTTON_LEFT;e.button.x=1040;e.button.y=85;ASSERT_TRUE(SDL_PushEvent(&e));
    e.type=SDL_EVENT_MOUSE_BUTTON_UP;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(session().profile()),hash);
    EXPECT_TRUE(std::holds_alternative<RaidSceneContainerAssetLocation>(session().profile().assets.find(box)->location));
}
TEST_F(RaidSceneContainerUiTest, OpenInventoryKeepsSimulationRunningWithMovementAndRevokesOnReturnOutside) {
    open(); const auto old=session().world().player().position();
    SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.scancode=SDL_SCANCODE_D;e.key.down=true;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app,0.1F);
    EXPECT_GT(session().world().player().position().x,old.x);
    e.type=SDL_EVENT_KEY_UP;e.key.down=false;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app);
    const auto exit=session().profile().pendingRaid->interiors.front().interiorExit;
    position({exit.position.x+exit.size.x/2,exit.position.y+exit.size.y/2});
    GameplayInput leave;leave.interactJustPressed=true;A::flow(*app).update(leave,0);A::frame(*app);
    EXPECT_EQ(session().world().activeRaidSpaceId(),outdoorRaidSpaceId());
    EXPECT_FALSE(A::open(*app));EXPECT_FALSE(A::drag(*app));EXPECT_FALSE(A::external(*app));
}
}
