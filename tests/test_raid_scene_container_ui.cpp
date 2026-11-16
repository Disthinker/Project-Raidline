#include "app.h"
#include "raid_lifecycle.h"
#include "profile_ui_test_wait.h"
#include "test_temp_directory.h"
#include <SDL3_image/SDL_image.h>
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
    static void bagTab(App &a) { static_cast<void>(a); /* Backpack is already on the shared page. */ }
    static void quick(App &a, MousePosition pos) { a.handleProfileInventoryUiEvent(InventoryQuickTransferEvent{pos}, true); }
    static void pointer(App &a, InventoryPointerEventType event, MousePosition pos)
    {
        a.handleProfileInventoryUiEvent(InventoryPointerEvent{event, pos}, true);
    }
    static void tactical(App &a)
    {
        a.tacticalMapOpen_ = true;
        a.renderRaidTacticalMap();
        a.tacticalMapOpen_ = false;
    }
    static bool renderer(App &a, SDL_Renderer *r) { a.renderer_=r; return a.uiTextRenderer_.initialize(r); }
    static void language(App &a, UiLanguage lang) { a.uiTextRenderer_.setLanguage(lang); }
    static void paint(App &a, UiLanguage lang) { a.uiTextRenderer_.setLanguage(lang); a.renderInventoryOverlay(); }
    static void expectedCorpseTitle(App &a) {
        SDL_SetRenderDrawColor(a.renderer_,216,228,224,255);
        a.uiTextRenderer_.renderBox(a.renderer_,{808,64,190,24},"尸体",{16,UiTextAlign::Left,1});
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_=nullptr; }
};
namespace {
using A=RaidSceneContainerUiTestAccess;
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=code; e.key.down=true; ASSERT_TRUE(SDL_PushEvent(&e));
    e.type=SDL_EVENT_KEY_UP; e.key.down=false; ASSERT_TRUE(SDL_PushEvent(&e));
}

TEST(RaidLostWeaponPursuitUiTest, RealCorpsePanelRendersAndDragsOriginalGun)
{
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
    raidline::test::TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claim("raidline-pursuit-ui-", "original-gun"));
    const auto &content = publishedContentRegistry();
    const MapDefinitionId map{"map.raid.riverside"};
    const auto &definition = content.item(ItemDefinitionId{"item.weapon.rifle_5_45_service"});
    auto profile = makeNewAlphaProfile("raid-pursuit-ui", content);
    profile.currency = 1000;
    profile.tutorial = TutorialProgress::Complete;
    const auto gun = profile.assets.create(definition, EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
    const auto magazine = profile.assets.create(content.item(ItemDefinitionId{"item.magazine.5_45x39_30"}),
        InstalledMagazineLocation{gun});
    const ItemDefinitionId standard{"item.ammunition.5_45x39_standard"};
    const ItemDefinitionId enhanced{"item.ammunition.5_45x39_enhanced"};
    const auto stash = ProfileContainerId::stash();
    const auto &kitDefinition = content.item(ItemDefinitionId{"item.maintenance.weapon_kit_basic"});
    const auto kitPosition = findFirstProfileFit(profile, content, stash, kitDefinition, ItemOrientation::Degrees0);
    ASSERT_TRUE(kitPosition);
    const auto kit = profile.assets.create(kitDefinition, StoredAssetLocation{stash, *kitPosition});
    const auto ammoPosition = findFirstProfileFit(profile, content, stash, content.item(standard), ItemOrientation::Degrees0);
    ASSERT_TRUE(ammoPosition);
    const auto spareAmmo = profile.assets.create(content.item(standard), StoredAssetLocation{stash, *ammoPosition}, 5);
    const std::vector<MagazineRoundRecord> mixed{{standard}, {enhanced}, {standard}};
    profile.assets.findMutable(magazine)->magazineRounds = mixed;
    profile.assets.findMutable(gun)->chamberedRound = MagazineRoundRecord{enhanced};
    profile.assets.findMutable(gun)->currentDurability = 7800;
    const auto armor = profile.assets.create(content.item(ItemDefinitionId{"item.protective_gear.body_armor_heavy"}),
        EquippedAssetLocation{EquipmentSlotKind::BodyArmor});
    const auto original = executeDeploy(profile, content,
        {"original-raid", "original-loss", 23, map}, {profile.revision, "original-deploy"});
    ASSERT_TRUE(original.succeeded) << original.message;
    ASSERT_TRUE(settlePendingRaid(profile, content, "original-loss", RaidResultOutcome::PlayerDead).succeeded);
    auto &record = profile.lostRaidRecords.at("original-loss");
    record.subsequentRaidSettlementCount = 1;
    ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);

    App app;
    auto &flow = A::flow(app);
    flow.configurePersistence(directory.path());
    ASSERT_TRUE(flow.continueGame());
    ASSERT_TRUE(flow.deploy(map, {}, record.recordId));
    auto &session = flow.gameSession();
    const auto &recovery = session.profile().pendingRaid->selfRecovery;
    ASSERT_TRUE(recovery && recovery->pursuit) << session.persistenceMessage();
    const auto pursuit = *recovery->pursuit;
    EXPECT_TRUE(session.world().noiseDecoyEnabled());
    EXPECT_FALSE(session.world().grenadeEnabled());
    EXPECT_FALSE(session.raidSceneContainerAvailable(pursuit.corpseAssetId));
    EXPECT_FALSE(session.raidSelfRecoveryProjection());

    const auto &enemies = session.world().enemies();
    const auto carrier = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy &actor) {
        return actor.position().x == pursuit.carrierSpawn.x && actor.position().y == pursuit.carrierSpawn.y;
    });
    ASSERT_NE(carrier, enemies.end());
    auto &actor = const_cast<Enemy &>(*carrier);
    static_cast<void>(actor.takeDamage(actor.maxHealth()));
    flow.update({}, 0.01F);
    const auto death = session.world().lostWeaponCarrierDeath();
    ASSERT_TRUE(death);
    auto &player = const_cast<Player &>(session.world().player());
    ASSERT_TRUE(player.setPosition({death->x - player.size() * 0.5F, death->y - player.size() * 0.5F}));
    A::frame(app);
    ASSERT_EQ(session.nearbyRaidSceneContainer(), pursuit.corpseAssetId);
    key(SDL_SCANCODE_F);
    A::frame(app);
    ASSERT_EQ(A::external(app), pursuit.corpseAssetId);
    ASSERT_TRUE(A::open(app));
    EXPECT_FALSE(A::capture(app));

    const auto *review = std::getenv("RAIDLINE_PURSUIT_UI_REVIEW_DIR");
    for (const auto size : kUiWindowSizes)
    {
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurface(size.width, size.height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        ASSERT_TRUE(surface);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
        ASSERT_TRUE(renderer);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer.get(), 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        ASSERT_TRUE(A::renderer(app, renderer.get()));
        for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese})
        {
            A::language(app, language);
            const auto render = [&](const char *name, auto draw)
            {
                SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
                EXPECT_TRUE(SDL_RenderClear(renderer.get()));
                draw();
                EXPECT_TRUE(SDL_RenderPresent(renderer.get()));
                if (review)
                {
                    const auto file = std::filesystem::path(review) /
                        (std::string{name} + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png");
                    EXPECT_TRUE(IMG_SavePNG(surface.get(), file.string().c_str()));
                }
            };
            render("pursuit-corpse", [&] { A::paint(app, language); });
            render("pursuit-map", [&] { A::tactical(app); });
        }
        A::detach(app);
    }

    const MousePosition start{817, 137};
    const MousePosition destination{60, 388};
    A::pointer(app, InventoryPointerEventType::LeftButtonDown, start);
    A::pointer(app, InventoryPointerEventType::Motion, destination);
    EXPECT_TRUE(A::drag(app));
    A::pointer(app, InventoryPointerEventType::LeftButtonUp, destination);
    EXPECT_EQ(equippedAsset(session.profile(), EquipmentSlotKind::PrimaryWeapon), gun);
    EXPECT_TRUE(session.profile().pendingRaid->selfRecovery->pursuit->claimed);
    EXPECT_EQ(lostRaidRecordForAsset(session.profile(), armor), "original-loss");
    EXPECT_FALSE(lostRaidRecordForAsset(session.profile(), gun));
    EXPECT_EQ(session.profile().assets.find(magazine)->magazineRounds, mixed);
    key(SDL_SCANCODE_ESCAPE);
    A::frame(app);

    const auto extraction = session.profile().pendingRaid->extractionPoint;
    ASSERT_TRUE(player.setPosition({
        extraction.position.x + (extraction.size.x - player.size()) * 0.5F,
        extraction.position.y + (extraction.size.y - player.size()) * 0.5F}));
    for (int frame = 0; frame < 30 && session.profile().pendingRaid && !session.profileUiCommitPending(); ++frame)
        flow.update({}, 0.15F);
    ASSERT_TRUE(waitForProfileUiDurability(session)) << session.persistenceMessage();
    flow.update({}, 0.0F);
    ASSERT_EQ(flow.state(), GameFlowState::RaidResult) << session.persistenceMessage();
    EXPECT_EQ(session.profile().lastRaidResult->outcome, RaidResultOutcome::Extracted);
    EXPECT_EQ(session.profile().assets.find(gun)->chamberedRound, MagazineRoundRecord{enhanced});
    EXPECT_EQ(session.profile().assets.find(magazine)->magazineRounds, mixed);
    EXPECT_EQ(session.profile().lostRaidRecords.at("original-loss").subsequentRaidSettlementCount, 2U);
    ASSERT_TRUE(flow.returnToBase());
    const auto maintenance = session.executeBaseWeaponMaintenance(kit, gun, "pursuit-base-maintenance");
    ASSERT_TRUE(maintenance.succeeded) << maintenance.message;
    EXPECT_GT(session.profile().assets.find(gun)->currentDurability, 7800U);
    const auto reload = session.executeProfileWeaponAmmo(
        LoadMagazineCommand{magazine, spareAmmo, 5}, "pursuit-base-load");
    ASSERT_TRUE(reload.succeeded) << reload.message;
    const auto rearmedCount = mixed.size() + 5U;
    EXPECT_EQ(session.profile().assets.find(magazine)->magazineRounds.size(), rearmedCount);
    ASSERT_TRUE(flow.deploy(map));
    EXPECT_EQ(session.activeAlphaWeapon(), gun);
    GameplayInput fire;
    fire.fireJustPressed = fire.firePressed = fire.firePhysicallyPressed = true;
    flow.update(fire, 0.0F);
    EXPECT_LT(session.profile().assets.find(magazine)->magazineRounds.size(), rearmedCount);
    SDL_Quit();
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
    A::quick(*app,{808+initial.x*56.0F+4,128+initial.y*56.0F+4}); ASSERT_TRUE(assetIsCarried(session().profile(),item));
    const auto carried=std::get<StoredAssetLocation>(session().profile().assets.find(item)->location);
    A::bagTab(*app); A::quick(*app,{282+carried.origin.x*56.0F+4,152+carried.origin.y*56.0F+4}); EXPECT_EQ(raidSceneContainerRoot(session().profile(),item),box);
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
        for (int y = 64; y < 88; ++y) {
            for (int x = 808; x < 998; ++x) {
                Uint8 r{}, g{}, b{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &alpha));
                mask.push_back(r == 216 && g == 228 && b == 224);
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
    A::quick(*app,{808+origin.x*56.0F+4,128+origin.y*56.0F+4});
    EXPECT_TRUE(assetIsCarried(session().profile(),item));
    const auto *after=session().profile().assets.find(item); ASSERT_NE(after,nullptr);
    const auto stored=std::get<StoredAssetLocation>(after->location);
    EXPECT_EQ(stored.container.ownerAssetId,bag);
    // Depositing a carried item uses the same transaction consumer.
    A::bagTab(*app); A::quick(*app,{282+stored.origin.x*56.0F+4,152+stored.origin.y*56.0F+4});
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
