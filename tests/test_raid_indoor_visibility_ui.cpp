#include "app.h"
#include "save_repository.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>

struct RaidIndoorVisibilityUiTestAccess
{
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static void frame(App &a)
    {
        a.windowHasInputFocus_ = true;
        a.processEvents();
        a.update(0);
        a.input_.endFrame();
    }
    static bool open(App &a) { return a.inventoryOverlayState_.isOpen(); }
    static bool drag(App &a)
    {
        return a.profileInventoryInteraction_.pointerGestureActive();
    }
    static bool capture(App &a) { return a.shouldCaptureWorldPointer(); }
    static bool attach(App &a, SDL_Renderer *r)
    {
        a.renderer_ = r;
        return a.uiTextRenderer_.initialize(r);
    }
    static bool layer(App &a, const SDL_Rect &viewport)
    {
        return a.renderIndoorInformationLayer(viewport);
    }
    static void paint(App &a) { a.renderRaidScreen(); }
    static void directPaint(App &a)
    {
        a.renderBackground();
        a.renderExtractionPoint();
        a.renderRaidSpacePortal();
        a.renderBallisticBlockers();
        a.renderAlphaRaidLoot();
        a.renderEnemyAttackTelegraphs();
        a.renderEnemies();
        a.renderPlayer();
        a.renderShotPresentations();
        a.renderShotFeedbackPresentations();
        a.renderParticles();
    }
    static void enemyAlpha(App &a, Uint8 alpha)
    {
        EXPECT_TRUE(
            SDL_SetTextureAlphaMod(a.enemyMoveHorizontalTexture_.get(), alpha));
    }
    static Vec2 camera(App &a) { return a.raidWorldCameraOffset(); }
    static void detach(App &a)
    {
        a.indoorInformationLayer_.reset();
        a.indoorShadowMask_.reset();
        a.enemyMoveHorizontalTexture_.reset();
        a.playerMoveHorizontalTexture_.reset();
        a.playerTexture_.reset();
        a.uiTextRenderer_.shutdown();
        a.renderer_ = nullptr;
    }
    static void sprites(App &a)
    {
        const auto make = [&](Uint32 color)
        {
            SDL_Texture *t =
                SDL_CreateTexture(a.renderer_, SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_STATIC, 1536, 640);
            EXPECT_NE(t, nullptr);
            std::vector<Uint32> pixels(1536U * 640U, color);
            EXPECT_TRUE(SDL_UpdateTexture(t, nullptr, pixels.data(), 1536 * 4));
            EXPECT_TRUE(SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND));
            return Texture{t};
        };
        a.enemyMoveHorizontalTexture_ = make(0xEA32D2FFU);
        a.playerMoveHorizontalTexture_ = make(0x18CB88FFU);
        a.playerTexture_ = make(0x18CB88FFU);
    }
    static void syntheticRoom(App &a, bool large = false)
    {
        auto &session = a.gameSession_;
        const auto interior = session.profile().pendingRaid->interiors.front();
        RaidWorldConfig config;
        config.worldSize =
            publishedContentRegistry()
                .map(session.profile().pendingRaid->mapDefinitionId)
                .worldSize;
        config.extractionPoint = {{100, 100}, {100, 100}};
        config.playerSpawn = {interior.exteriorEntrance.position.x + 20,
                              interior.exteriorEntrance.position.y + 20};
        RaidInteriorWorldConfig room;
        room.id = interior.id;
        room.displayName = interior.displayName;
        room.worldSize = large ? Vec2{2400, 1440} : interior.worldSize;
        room.exteriorEntrance = interior.exteriorEntrance;
        room.exteriorReturn = interior.exteriorReturn;
        room.interiorExit = interior.interiorExit;
        room.interiorSpawn = {375, 165};
        room.initialEnemies = {EnemySpawn{
            large ? Vec2{1400, 300} : Vec2{500, 140}, {50, 50}, 100}};
        // Deliberately thin test wall near the existing box: both legal player
        // positions remain within 72 pixels, isolating sight from old range
        // checks.
        room.ballisticBlockers = {{1, {{440, 80}, {4, 180}}}};
        if (large)
            room.ballisticBlockers.clear();
        config.interiors.push_back(room);
        session.world_ = std::make_unique<GameplayWorld>(config);
        const float half = session.world().player().size() * 0.5F;
        const auto portal = interior.exteriorEntrance;
        ASSERT_TRUE(
            const_cast<Player &>(session.world().player())
                .setPosition({portal.position.x + portal.size.x / 2 - half,
                              portal.position.y + portal.size.y / 2 - half}));
        GameplayInput enter;
        enter.interactJustPressed = true;
        session.update(enter, 0);
        ASSERT_EQ(session.world().activeRaidSpaceId(), interior.id);
    }
    static void beginDrag(App &a, AssetInstanceId id)
    {
        const auto &p = a.gameSession_.profile();
        const auto *asset = p.assets.find(id);
        const auto pos = std::get<StoredAssetLocation>(asset->location).origin;
        const auto footprint = inventoryFootprint(
            publishedContentRegistry().item(asset->definitionId),
            asset->orientation);
        ASSERT_TRUE(a.profileInventoryInteraction_.beginPointerPress(
            {id, p.revision, asset->location, 0, asset->orientation}, pos, pos,
            {680, 140}, {asset->orientation, footprint, true, {0, 0}}));
        a.profileInventoryInteraction_.updatePointerPosition({230, 530},
                                                             std::nullopt);
    }
};

namespace
{
using A = RaidIndoorVisibilityUiTestAccess;
class RaidIndoorVisibilityUiTest : public testing::Test
{
  protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    AssetInstanceId box{};
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("raidline-indoor-ui-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    GameSession &session() { return A::flow(*app).gameSession(); }
    void position(Vec2 p)
    {
        const float half = session().world().player().size() * 0.5F;
        ASSERT_TRUE(const_cast<Player &>(session().world().player())
                        .setPosition({p.x - half, p.y - half}));
    }
    void SetUp() override
    {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        auto profile =
            makeNewAlphaProfile("visibility-ui", publishedContentRegistry());
        static_cast<void>(profile.assets.create(
            publishedContentRegistry().item(
                ItemDefinitionId{"item.container.backpack_expedition"}),
            EquippedAssetLocation{EquipmentSlotKind::Backpack}));
        ASSERT_TRUE(
            SaveRepository{path}
                .save(profile, publishedContentRegistry().contentVersion())
                .succeeded);
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());
        ASSERT_TRUE(A::flow(*app).deploy(
            MapDefinitionId{"map.raid.frontier_exchange"}));
        A::syntheticRoom(*app);
        position({400, 190});
        for (const auto &[id, asset] : session().profile().assets.records())
            if (const auto *loc = std::get_if<RaidSceneContainerAssetLocation>(
                    &asset.location);
                loc && loc->spaceId == session().world().activeRaidSpaceId())
                box = id;
        ASSERT_NE(box, 0U);
        surface = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(A::attach(*app, renderer));
        A::sprites(*app);
    }
    void TearDown() override
    {
        if (app)
            A::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        app.reset();
        SDL_Quit();
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void key(SDL_Scancode key)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = key;
        e.key.down = true;
        ASSERT_TRUE(SDL_PushEvent(&e));
        e.type = SDL_EVENT_KEY_UP;
        e.key.down = false;
        ASSERT_TRUE(SDL_PushEvent(&e));
        A::frame(*app);
    }
    bool enemyPixel(int x, int y)
    {
        Uint8 r{}, g{}, b{}, a{};
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
        return r > 210 && g < 90 && b > 180;
    }
    void paint()
    {
        SDL_SetRenderViewport(renderer, nullptr);
        SDL_SetRenderClipRect(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 17, 20, 18, 255);
        ASSERT_TRUE(SDL_RenderClear(renderer));
        A::paint(*app);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
};

TEST_F(RaidIndoorVisibilityUiTest,
       ActualScreenHidesEnemyButKeepsBackgroundAndRestoresWhenObserved)
{
    const auto hash = profileStateFingerprint(session().profile());
    const auto id = session().world().enemies().front().combatTargetId();
    ASSERT_FALSE(session().world().raidEnemyObservable(id));
    paint();
    EXPECT_FALSE(enemyPixel(525, 165));
    Uint8 r{}, g{}, b{}, a{};
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 610, 400, &r, &g, &b, &a));
    EXPECT_GT(r, 0);
    EXPECT_GT(g, 0);
    position({650, 350});
    ASSERT_TRUE(session().world().raidEnemyObservable(id));
    paint();
    EXPECT_TRUE(enemyPixel(525, 165));
    position({400, 190});
    paint();
    EXPECT_FALSE(enemyPixel(525, 165));
    EXPECT_EQ(profileStateFingerprint(session().profile()), hash);
    EXPECT_EQ(session().world().enemies().size(), 1U);
    EXPECT_EQ(session().world().enemies().front().combatTargetId(), id);
    if (const char *dir = std::getenv("RAIDLINE_INDOOR_VISIBILITY_QA_DIR"))
    {
        std::filesystem::create_directories(dir);
        ASSERT_TRUE(SDL_SaveBMP(
            surface,
            (std::filesystem::path{dir} / "hidden-room.bmp").string().c_str()));
    }
}
TEST_F(RaidIndoorVisibilityUiTest,
       PartiallyVisibleSpriteIsClippedWithoutShowingItsHiddenTop)
{
    position({400, 300});
    const auto &enemy = session().world().enemies().front();
    ASSERT_TRUE(session().world().raidBoundsObservable(enemy.bounds()));
    ASSERT_FALSE(
        session().world().indoorObservation().pointVisible({525, 145}));
    ASSERT_TRUE(session().world().indoorObservation().pointVisible({525, 188}));
    paint();
    EXPECT_FALSE(enemyPixel(525, 145));
    EXPECT_TRUE(enemyPixel(525, 188));
    if (const char *dir = std::getenv("RAIDLINE_INDOOR_VISIBILITY_QA_DIR"))
    {
        std::filesystem::create_directories(dir);
        ASSERT_TRUE(SDL_SaveBMP(
            surface, (std::filesystem::path{dir} / "partial-room.bmp")
                         .string()
                         .c_str()));
        const auto builds = session().world().indoorObservationBuildCount();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; ++i)
            paint();
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count() /
                          20;
        EXPECT_EQ(session().world().indoorObservationBuildCount(), builds);
        std::ofstream metrics{std::filesystem::path{dir} /
                              "render-metrics.json"};
        metrics << "{\"renderer\":\"software\",\"scenario\":\"synthetic_one_"
                   "enemy\",\"frames\":20,\"mean_ms\":"
                << ms << ",\"observation_rebuilds_during_static_frames\":"
                << (session().world().indoorObservationBuildCount() - builds)
                << "}\n";
    }
}
TEST_F(RaidIndoorVisibilityUiTest,
       SameRangeHiddenBoxRejectsActualServiceAndRawInventoryWithoutLoss)
{
    ASSERT_TRUE(session().raidSceneContainerAvailable(box));
    const auto item = assetsInContainer(session().profile(),
                                        ProfileContainerId::compartment(box, 0))
                          .front()
                          ->instanceId;
    const auto bag =
        *equippedAsset(session().profile(), EquipmentSlotKind::Backpack);
    const InventoryCommand command = InventoryMoveCommand{
        item, 0, {ProfileContainerId::compartment(bag, 0), {0, 0}}};
    position({470, 190});
    const auto hash = profileStateFingerprint(session().profile());
    EXPECT_FALSE(session().raidSceneContainerAvailable(box));
    EXPECT_FALSE(
        session().queryRaidSceneContainerInventory(box, command).canCommit);
    EXPECT_FALSE(session()
                     .executeRaidSceneContainerInventory(box, command, "hidden")
                     .succeeded);
    EXPECT_FALSE(
        session().executeProfileInventory(command, "raw-hidden").succeeded);
    EXPECT_EQ(profileStateFingerprint(session().profile()), hash);
}
TEST_F(RaidIndoorVisibilityUiTest,
       LostSightRevokesActualPanelAndQueuedDragReleaseWithinExistingRange)
{
    key(SDL_SCANCODE_F);
    ASSERT_TRUE(A::open(*app));
    EXPECT_FALSE(A::capture(*app));
    const auto item = assetsInContainer(session().profile(),
                                        ProfileContainerId::compartment(box, 0))
                          .front()
                          ->instanceId;
    A::beginDrag(*app, item);
    ASSERT_TRUE(A::drag(*app));
    const auto hash = profileStateFingerprint(session().profile());
    position({470, 190});
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 230;
    e.button.y = 530;
    ASSERT_TRUE(SDL_PushEvent(&e));
    A::frame(*app);
    EXPECT_FALSE(A::open(*app));
    EXPECT_FALSE(A::drag(*app));
    EXPECT_TRUE(A::capture(*app));
    EXPECT_EQ(profileStateFingerprint(session().profile()), hash);
    position({400, 190});
    key(SDL_SCANCODE_F);
    EXPECT_TRUE(A::open(*app));
}
TEST_F(RaidIndoorVisibilityUiTest,
       RenderTargetsViewportClipBlendAndPenAreRestored)
{
    const SDL_Rect viewport{-40, -20, 1280, 720}, clip{15, 25, 600, 500};
    ASSERT_TRUE(SDL_SetRenderViewport(renderer, &viewport));
    ASSERT_TRUE(SDL_SetRenderClipRect(renderer, &clip));
    ASSERT_TRUE(SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD));
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 11, 22, 33, 44));
    const auto *target = SDL_GetRenderTarget(renderer);
    ASSERT_TRUE(A::layer(*app, viewport));
    EXPECT_EQ(SDL_GetRenderTarget(renderer), target);
    SDL_Rect actual{};
    ASSERT_TRUE(SDL_GetRenderViewport(renderer, &actual));
    EXPECT_EQ(actual.x, viewport.x);
    EXPECT_EQ(actual.y, viewport.y);
    ASSERT_TRUE(SDL_RenderClipEnabled(renderer));
    ASSERT_TRUE(SDL_GetRenderClipRect(renderer, &actual));
    EXPECT_EQ(actual.x, clip.x);
    EXPECT_EQ(actual.w, clip.w);
    SDL_BlendMode blend{};
    ASSERT_TRUE(SDL_GetRenderDrawBlendMode(renderer, &blend));
    EXPECT_EQ(blend, SDL_BLENDMODE_ADD);
    Uint8 r{}, g{}, b{}, a{};
    ASSERT_TRUE(SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a));
    EXPECT_EQ(r, 11);
    EXPECT_EQ(a, 44);
}

TEST_F(RaidIndoorVisibilityUiTest,
       TranslucentVisibleSpriteMatchesDirectOpaqueBackgroundRendering)
{
    position({650, 350});
    A::enemyAlpha(*app, 128);
    paint();
    Uint8 r{}, g{}, b{}, a{};
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 525, 165, &r, &g, &b, &a));
    const auto actual = std::array<Uint8, 4>{r, g, b, a};
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderClipRect(renderer, nullptr);
    A::directPaint(*app);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 525, 165, &r, &g, &b, &a));
    EXPECT_EQ(actual, (std::array<Uint8, 4>{r, g, b, a}));
}

TEST_F(RaidIndoorVisibilityUiTest,
       HiddenAttackSourceDoesNotRevealItsVisibleTelegraph)
{
    position({350, 190});
    auto &enemy = const_cast<Enemy &>(session().world().enemies().front());
    ASSERT_TRUE(enemy.setPosition({390, 165}));
    enemy.hearTarget({350, 190});
    auto &world = const_cast<GameplayWorld &>(session().world());
    for (int i = 0; i < 10 && enemy.attackPhase() == EnemyAttackPhase::Idle;
         ++i)
        world.update({}, 0.02F);
    ASSERT_EQ(enemy.attackPhase(), EnemyAttackPhase::Windup);
    ASSERT_TRUE(enemy.setPosition({450, 165}));
    ASSERT_FALSE(world.raidEnemyObservable(enemy.combatTargetId()));
    const auto telegraph = enemy.attackTelegraphBounds();
    ASSERT_TRUE(telegraph);
    ASSERT_TRUE(world.indoorObservation().boundsVisible(*telegraph));
    paint();
    const auto sample = [&]
    {
        std::vector<Uint32> pixels;
        for (int y = 125; y < 260; ++y)
            for (int x = 350; x < 435; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                EXPECT_TRUE(
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                pixels.push_back((static_cast<Uint32>(r) << 24U) |
                                 (static_cast<Uint32>(g) << 16U) |
                                 (static_cast<Uint32>(b) << 8U) | a);
            }
        return pixels;
    };
    const auto hiddenAttack = sample();
    static_cast<void>(enemy.takeDamage(enemy.health()));
    paint();
    EXPECT_EQ(hiddenAttack, sample());
}

TEST_F(RaidIndoorVisibilityUiTest,
       OffscreenEnemyDoesNotLeakIntoTheOrdinaryVisibleCounter)
{
    A::syntheticRoom(*app, true);
    position({400, 190});
    auto &enemy = const_cast<Enemy &>(session().world().enemies().front());
    ASSERT_TRUE(session().world().raidEnemyObservable(enemy.combatTargetId()));
    ASSERT_GT(enemy.position().x, A::camera(*app).x + 1280);
    const auto sample = [&]
    {
        std::vector<Uint32> pixels;
        for (int y = 68; y < 90; ++y)
            for (int x = 20; x < 550; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                EXPECT_TRUE(
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                pixels.push_back((static_cast<Uint32>(r) << 24U) |
                                 (static_cast<Uint32>(g) << 16U) |
                                 (static_cast<Uint32>(b) << 8U) | a);
            }
        return pixels;
    };
    paint();
    const auto offscreen = sample();
    static_cast<void>(enemy.takeDamage(enemy.health()));
    paint();
    EXPECT_EQ(offscreen, sample());
}
} // namespace
