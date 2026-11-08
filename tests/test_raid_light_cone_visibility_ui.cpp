#include "app.h"
#include "raid_camera.h"
#include "save_repository.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

struct RaidLightConeVisibilityUiTestAccess
{
    static void feedbackWorld(App &a)
    {
        RaidWorldConfig c;
        c.lightConeObservationEnabled = true;
        c.worldSize = {1280, 720};
        c.playerSpawn = {450, 289};
        c.extractionPoint = {{1100, 600}, {100, 100}};
        c.initialEnemies = {EnemySpawn{{600, 300}, {50, 50}, 4}};
        a.gameSession_.world_ = std::make_unique<GameplayWorld>(c);
    }
    static WeaponOpticMode optic(App &a) { return a.visibleWeaponOptic(); }
    static const WeaponOpticViewProjection *opticView(App &a) { return a.visibleWeaponOpticView(); }
    static void pointerGate(App &a, bool right, bool menuOpen)
    {
        if (menuOpen)
            a.inventoryOverlayState_.openPlayerInventory();
        else
            a.closeInventory();
        SDL_Event event{};
        event.type = right ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_RIGHT;
        event.button.down = right;
        a.input_.handleEvent(event);
        a.updateWeaponOpticPointerGate();
    }
    static bool opticAllowed(App &a) { return a.makeGameplayControls().allowWeaponOptics; }
    static void clearRecoveryWorld(App &a, Vec2 cache)
    {
        RaidWorldConfig c;
        c.lightConeObservationEnabled = true;
        c.worldSize =
            publishedContentRegistry()
                .map(a.gameSession_.profile().pendingRaid->mapDefinitionId)
                .worldSize;
        c.playerSpawn = {cache.x - 85, cache.y - 25};
        c.extractionPoint = {{c.worldSize.x - 200, c.worldSize.y - 200},
                             {100, 100}};
        a.gameSession_.world_ = std::make_unique<GameplayWorld>(c);
    }
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static void frame(App &a)
    {
        a.windowHasInputFocus_ = true;
        a.processEvents();
        a.update(0);
        a.input_.endFrame();
    }
    static void outdoors(App &a, std::vector<BallisticBlocker> walls = {},
                         int count = 1, bool coneEnabled = true)
    {
        RaidWorldConfig c;
        c.lightConeObservationEnabled = coneEnabled;
        c.worldSize = coneEnabled ? Vec2{1280, 720} : Vec2{2400, 1440};
        c.playerSpawn = {375, 165};
        c.extractionPoint = {{1100, 600}, {100, 100}};
        c.initialEnemies = {EnemySpawn{{500, 140}, {50, 50}, 100}};
        for (int n = 1; n < count; ++n)
            c.initialEnemies.push_back(
                EnemySpawn{{500.0F + (n % 10) * 60, 100.0F + (n / 10) * 50},
                           {50, 50},
                           100});
        if (!coneEnabled && count > 1)
            c.initialEnemies.back().position = {1800, 300};
        c.ballisticBlockers = std::move(walls);
        a.gameSession_.world_ = std::make_unique<GameplayWorld>(c);
    }
    static void opticRange(App &a, bool withWall = false, bool withTransitionMarker = false)
    {
        RaidWorldConfig config;
        config.lightConeObservationEnabled = true;
        config.worldSize = {2800, 1600};
        config.playerSpawn = {1184, 704};
        config.extractionPoint = {{2650, 1450}, {100, 100}};
        config.initialEnemies = {EnemySpawn{{1920, 695}, {50, 50}, 100}};
        config.outdoorLayout.layoutVersion = 4;
        config.outdoorColumns = 28;
        config.outdoorRows = 16;
        config.outdoorChunkSizeCells = 1;
        config.outdoorLayout.props.push_back(
            {1, RaidOutdoorPropKind::EngineeringEquipment, RaidOutdoorPropState::Intact,
             {{1910, 780}, {82, 50}}, 0, false});
        if (withTransitionMarker)
            config.outdoorLayout.props.push_back(
                {2, RaidOutdoorPropKind::EngineeringEquipment, RaidOutdoorPropState::Intact,
                 {{1460, 780}, {80, 70}}, 0, false});
        if (withWall)
            config.ballisticBlockers.push_back({1, {{1450, 570}, {10, 345}}});
        a.gameSession_.world_ = std::make_unique<GameplayWorld>(config);
        a.gameSession_.world_->setRaidDaylight(true);
    }
    static void menu(App &a, bool open)
    {
        if (open)
            a.inventoryOverlayState_.openPlayerInventory();
        else
            a.closeInventory();
    }
    static void anchor(App &a)
    {
        a.worldAimPointerAnchor_ =
            raidWorldToScreen(a.gameSession_.world().weaponAimWorldPosition(),
                              a.raidWorldCameraOffset());
    }
    static void frameSeconds(App &a, float seconds)
    {
        a.windowHasInputFocus_ = true;
        a.processEvents();
        a.update(seconds);
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
    static bool paintOptic(App &a, const WeaponOpticViewProjection &view)
    {
        const auto &world = a.gameSession_.world();
        return a.renderWeaponOpticWorld(view, false, world.weaponAimWorldPosition(),
                                        world.raidSpaceWorldSize());
    }
    static std::array<Vec2, 2> observationTargetScales(App &a)
    {
        const auto target = SDL_GetRenderTarget(a.renderer_);
        const std::array targets{a.indoorInformationLayer_.get(), a.indoorShadowMask_.get()};
        std::array<Vec2, 2> scales{};
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            EXPECT_NE(targets[i], nullptr);
            EXPECT_TRUE(SDL_SetRenderTarget(a.renderer_, targets[i]));
            EXPECT_TRUE(SDL_GetRenderScale(a.renderer_, &scales[i].x, &scales[i].y));
        }
        EXPECT_TRUE(SDL_SetRenderTarget(a.renderer_, target));
        return scales;
    }
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
        config.lightConeObservationEnabled = true;
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
// CTest runs individual Google Tests in separate processes. A timestamp alone
// does not reserve a directory; claim ownership before any save or cleanup.
std::filesystem::path claimUiSaveDirectory(
    std::string_view testName, std::uint64_t seed)
{
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path());
    const auto stem = "raidline-cone-ui-" + std::string{testName} + "-" +
        std::to_string(seed) + "-";
    for (unsigned attempt = 0; attempt < 128; ++attempt)
    {
        const auto candidate = root / (stem + std::to_string(attempt));
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error))
            return candidate;
        if (error && error != std::errc::file_exists)
            throw std::filesystem::filesystem_error{
                "could not claim UI test save directory", candidate, error};
    }
    throw std::runtime_error{"UI test save directory attempts exhausted"};
}

void removeUiSaveDirectory(const std::filesystem::path &path)
{
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::temp_directory_path(), error);
    if (error) return;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || resolved.parent_path() != root ||
        !resolved.filename().string().starts_with("raidline-cone-ui-"))
        return;
    std::filesystem::remove_all(resolved, error);
}

TEST(RaidLightConeVisibilityUiDirectoryTest, SameSeedClaimsIndependentOwnedDirectories)
{
    const auto seed = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto first = claimUiSaveDirectory("same-seed", seed);
    const auto second = claimUiSaveDirectory("same-seed", seed);
    EXPECT_NE(first, second);
    EXPECT_TRUE(std::filesystem::is_directory(first));
    EXPECT_TRUE(std::filesystem::is_directory(second));
    const auto marker = second / "owner.txt";
    { std::ofstream output{marker}; output << "second owner"; }
    removeUiSaveDirectory(first);
    EXPECT_FALSE(std::filesystem::exists(first));
    EXPECT_TRUE(std::filesystem::is_directory(second));
    EXPECT_TRUE(std::filesystem::exists(marker));
    removeUiSaveDirectory(second);
}

using A = RaidLightConeVisibilityUiTestAccess;
class RaidLightConeVisibilityUiTest : public testing::Test
{
  protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    AssetInstanceId box{};
    std::filesystem::path path = claimUiSaveDirectory(
        testing::UnitTest::GetInstance()->current_test_info()->name(),
        static_cast<std::uint64_t>(
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
        SCOPED_TRACE("isolated save directory: " + path.string());
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        auto profile =
            makeNewAlphaProfile("visibility-ui", publishedContentRegistry());
        static_cast<void>(profile.assets.create(
            publishedContentRegistry().item(
                ItemDefinitionId{"item.container.backpack_expedition"}),
            EquippedAssetLocation{EquipmentSlotKind::Backpack}));
        const auto saved = SaveRepository{path}.save(
            profile, publishedContentRegistry().contentVersion());
        ASSERT_TRUE(saved.succeeded) << saved.message;
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame())
            << A::flow(*app).gameSession().persistenceMessage();
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
        removeUiSaveDirectory(path);
    }
    void resize(int width, int height)
    {
        A::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720,
                                                     SDL_LOGICAL_PRESENTATION_LETTERBOX));
        ASSERT_TRUE(A::attach(*app, renderer));
        A::sprites(*app);
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
    std::array<Uint8, 4> logicalPixel(Vec2 pixel)
    {
        std::array<Uint8, 4> rgba{};
        const float scale = static_cast<float>(surface->w) / 1280.0F;
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface,
            static_cast<int>(std::lround(pixel.x * scale)),
            static_cast<int>(std::lround(pixel.y * scale)),
            &rgba[0], &rgba[1], &rgba[2], &rgba[3]));
        return rgba;
    }
    bool projectedEnemyPixel(Vec2 pixel)
    {
        const auto rgba = logicalPixel(pixel);
        return rgba[0] > 210 && rgba[1] < 90 && rgba[2] > 180;
    }
    void expectOrdinaryObservationTargetScales()
    {
        const auto scales = A::observationTargetScales(*app);
        for (const auto scale : scales)
        {
            EXPECT_FLOAT_EQ(scale.x, 1.0F);
            EXPECT_FLOAT_EQ(scale.y, 1.0F);
        }
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

void face(GameSession &session, Vec2 target)
{
    GameplayInput i;
    i.aimWorldPosition = target;
    i.aimWorldBounds = Rect{{}, session.world().raidSpaceWorldSize()};
    const_cast<GameplayWorld &>(session.world()).update(i, 0);
    const auto actual = session.world().weaponAimWorldPosition();
    i.aimMotionDelta = Vec2{target.x - actual.x, target.y - actual.y};
    const_cast<GameplayWorld &>(session.world()).update(i, 0);
}
void saveConeImage(SDL_Surface *surface, const char *name)
{
    if (const char *dir = std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
    {
        std::filesystem::create_directories(dir);
        EXPECT_TRUE(SDL_SaveBMP(
            surface, (std::filesystem::path{dir} / name).string().c_str()));
    }
}
TEST_F(RaidLightConeVisibilityUiTest, OutdoorsUsesConeAndShowsEntireOwnAvatar)
{
    A::outdoors(*app);
    face(session(), {800, 190});
    paint();
    EXPECT_TRUE(enemyPixel(525, 165));
    saveConeImage(surface, "day-front.bmp");
    Uint8 r{}, g{}, b{}, a{};
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 385, 190, &r, &g, &b, &a));
    EXPECT_GT(g, 150);
    face(session(), {100, 190});
    paint();
    EXPECT_FALSE(enemyPixel(525, 165));
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 415, 190, &r, &g, &b, &a));
    EXPECT_GT(g, 150);
    ASSERT_TRUE(SDL_ReadSurfacePixel(surface, 600, 400, &r, &g, &b, &a));
    EXPECT_GT(r + g + b, 0);
    saveConeImage(surface, "day-back.bmp");
}
TEST_F(RaidLightConeVisibilityUiTest,
       NightRangeAndPartialSideEdgeAreClippedInPixels)
{
    A::outdoors(*app);
    face(session(), {800, 190});
    const_cast<GameplayWorld &>(session().world()).setRaidDaylight(false);
    GameplayInput i;
    i.aimWorldPosition = Vec2{800, 190};
    const_cast<GameplayWorld &>(session().world()).update(i, 0.5F);
    ASSERT_TRUE(const_cast<Enemy &>(session().world().enemies().front())
                    .setPosition({500, 140}));
    paint();
    EXPECT_TRUE(enemyPixel(525, 165));
    saveConeImage(surface, "night-front.bmp");
    // Apex to the right of the actor: a daytime half-plane must not render its
    // rear half.
    position({525, 300});
    face(session(), {900, 300});
    const_cast<GameplayWorld &>(session().world()).setRaidDaylight(true);
    i.aimWorldPosition = Vec2{900, 300};
    const_cast<GameplayWorld &>(session().world()).update(i, 0.5F);
    ASSERT_TRUE(const_cast<Enemy &>(session().world().enemies().front())
                    .setPosition({500, 140}));
    paint();
    EXPECT_FALSE(enemyPixel(510, 165));
    EXPECT_TRUE(enemyPixel(540, 165));
    saveConeImage(surface, "partial-sector.bmp");
}
TEST_F(RaidLightConeVisibilityUiTest, OutdoorWallHidesActorWithoutRemovingIt)
{
    A::outdoors(*app, {{1, {{440, 80}, {4, 180}}}});
    face(session(), {800, 190});
    paint();
    EXPECT_FALSE(enemyPixel(525, 165));
    EXPECT_EQ(session().world().enemies().size(), 1U);
    saveConeImage(surface, "outdoor-wall.bmp");
    position({470, 190});
    face(session(), {800, 190});
    paint();
    EXPECT_TRUE(enemyPixel(525, 165));
    saveConeImage(surface, "outdoor-revealed.bmp");
}
TEST_F(RaidLightConeVisibilityUiTest,
       ActualMenuMouseCannotScanWorldAndCloseKeepsReticle)
{
    A::outdoors(*app);
    face(session(), {800, 190});
    const auto before = session().world().raidObservationKey();
    const auto reticle = session().world().weaponAimWorldPosition();
    A::anchor(*app);
    key(SDL_SCANCODE_TAB);
    ASSERT_TRUE(A::open(*app));
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = 100;
    e.motion.y = 650;
    e.motion.xrel = -600;
    e.motion.yrel = 500;
    ASSERT_TRUE(SDL_PushEvent(&e));
    A::frameSeconds(*app, 0.05F);
    const auto locked = session().world().raidObservationKey();
    EXPECT_FLOAT_EQ(locked.direction.x, before.direction.x);
    EXPECT_FLOAT_EQ(locked.direction.y, before.direction.y);
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().x, reticle.x);
    key(SDL_SCANCODE_TAB);
    ASSERT_FALSE(A::open(*app));
    EXPECT_FLOAT_EQ(session().world().weaponAimWorldPosition().x, reticle.x);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
}
TEST_F(RaidLightConeVisibilityUiTest,
       ContainerFacingLossCancelsQueuedDragBeforeAnyMutation)
{
    position({400, 190});
    face(session(), {410, 190});
    ASSERT_TRUE(session().raidSceneContainerAvailable(box));
    key(SDL_SCANCODE_F);
    ASSERT_TRUE(A::open(*app));
    const auto item = assetsInContainer(session().profile(),
                                        ProfileContainerId::compartment(box, 0))
                          .front()
                          ->instanceId;
    A::beginDrag(*app, item);
    ASSERT_TRUE(A::drag(*app));
    const auto hash = profileStateFingerprint(session().profile());
    // Simulate the current runtime observation changing before queued release.
    position({380, 190});
    face(session(), {100, 190});
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = 230;
    e.button.y = 530;
    ASSERT_TRUE(SDL_PushEvent(&e));
    A::frame(*app);
    EXPECT_FALSE(A::open(*app));
    EXPECT_FALSE(A::drag(*app));
    EXPECT_EQ(profileStateFingerprint(session().profile()), hash);
}

TEST_F(RaidLightConeVisibilityUiTest,
       LostCacheHoldStopsOnFacingLossAndFinalOpenRechecks)
{
    ASSERT_TRUE(session().activeQuitAlphaRaid());
    ASSERT_FALSE(session().profile().lostRaidRecords.empty());
    const auto record = session().profile().lostRaidRecords.begin()->first;
    ASSERT_TRUE(session().deployAlpha(
        9917, MapDefinitionId{"map.raid.frontier_exchange"}, {}, record));
    const auto recovery = *session().profile().pendingRaid->selfRecovery;
    ASSERT_GT(recovery.cachePosition.x, 85);
    A::clearRecoveryWorld(*app, recovery.cachePosition);
    GameplayInput i;
    i.aimWorldPosition = recovery.cachePosition;
    i.aimWorldBounds = Rect{{}, session().world().raidSpaceWorldSize()};
    i.interactPressed = true;
    session().update(i, 0);
    const auto current = session().world().raidObservationKey();
    ASSERT_TRUE(session().raidSelfRecoveryProjection()->interactionInRange)
        << "cache=" << recovery.cachePosition.x << ","
        << recovery.cachePosition.y << " observer=" << current.observer.x << ","
        << current.observer.y << " direction=" << current.direction.x << ","
        << current.direction.y
        << " health=" << session().world().player().health()
        << " geometry=" << session().world().raidObservation().validGeometry()
        << " valid=" << session().world().raidObservation().valid
        << " observed="
        << session().world().raidBoundsObservable(
               {{recovery.cachePosition.x - 30, recovery.cachePosition.y - 22},
                {60, 44}})
        << " params=" << current.angleDegrees << "," << current.maximumDistance
        << " active=" << session().world().raidSession().isActive();
    session().update(i, recovery.interactionDurationSeconds / 2);
    ASSERT_GT(session().raidSelfRecoveryProjection()->interactionProgress, 0);
    ASSERT_FALSE(session().profile().pendingRaid->selfRecovery->opened);
    i.aimWorldPosition =
        Vec2{recovery.cachePosition.x - 600, recovery.cachePosition.y};
    session().update(i, 0);
    EXPECT_FALSE(session().raidSelfRecoveryProjection()->interactionInRange);
    EXPECT_FLOAT_EQ(session().raidSelfRecoveryProjection()->interactionProgress,
                    0);
    EXPECT_FALSE(session().profile().pendingRaid->selfRecovery->opened);
    i.aimWorldPosition = recovery.cachePosition;
    session().update(i, recovery.interactionDurationSeconds + 0.01F);
    EXPECT_TRUE(session().profile().pendingRaid->selfRecovery->opened);
}
TEST_F(RaidLightConeVisibilityUiTest,
       ActualSoftwareDrawingWithThirtyTwoAndHundredActors)
{
    for (int count : {32, 100})
    {
        A::outdoors(*app, {}, count);
        face(session(), {900, 190});
        ASSERT_EQ(session().world().enemies().size(),
                  static_cast<std::size_t>(count));
        paint();
        const auto builds = session().world().indoorObservationBuildCount();
        std::vector<double> samples;
        const int frames = std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR") ? 30 : 1;
        for (int n = 0; n < frames; ++n)
        {
            const auto start = std::chrono::steady_clock::now();
            paint();
            samples.push_back(std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
        }
        EXPECT_EQ(session().world().indoorObservationBuildCount(), builds);
        if (const char *dir = std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
        {
            std::sort(samples.begin(), samples.end());
            std::ofstream metrics{std::filesystem::path{dir} /
                                      "software-draw-performance.csv",
                                  std::ios::app};
            metrics
                << count << ',' << frames << ','
                << samples[static_cast<std::size_t>(std::ceil(frames * 0.95)) -
                           1]
                << ',' << samples.back() << ',' << 0 << '\n';
            saveConeImage(surface,
                          count == 32 ? "actors-32.bmp" : "actors-100.bmp");
        }
    }
}
TEST_F(RaidLightConeVisibilityUiTest,
       ActualLethalHeadFeedbackPixelsStayVisibleThenHideWithSource)
{
    A::feedbackWorld(*app);
    auto &world = const_cast<GameplayWorld &>(session().world());
    auto weapon = *itemDefinition(ItemId::Rifle).weaponUse;
    weapon.baseDamage = 4;
    auto handling = deriveWeaponHandling(weapon);
    handling.minimumSpreadDegrees = handling.maximumSpreadDegrees = 0;
    world.configureWeaponFire(weapon, handling, false);
    GameplayInput i;
    i.aimWorldPosition = Vec2{625, 305};
    i.aimWorldBounds = Rect{{}, world.raidSpaceWorldSize()};
    world.update(i, 0);
    i.fireJustPressed = true;
    world.update(i, 0.001F);
    i.fireJustPressed = false;
    for (int n = 0; n < 100 && world.hitResultsLastUpdate().empty(); ++n)
        world.update(i, 0.001F);
    ASSERT_FALSE(world.hitResultsLastUpdate().empty());
    ASSERT_TRUE(world.hitResultsLastUpdate().front().targetKilled);
    ASSERT_EQ(world.hitResultsLastUpdate().front().semantic,
              HitSemantic::Headshot);
    const auto specialPixels = [&]
    {
        const auto point = world.weaponAimWorldPosition();
        int count{};
        for (int y = static_cast<int>(point.y) - 7;
             y <= static_cast<int>(point.y) + 7; ++y)
            for (int x = static_cast<int>(point.x) - 7;
                 x <= static_cast<int>(point.x) + 7; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                EXPECT_TRUE(
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                if (r > 240 && g > 170 && g < 210 && b > 55 && b < 90)
                    ++count;
            }
        return count;
    };
    paint();
    EXPECT_GT(specialPixels(), 0);
    saveConeImage(surface, "visible-lethal-feedback.bmp");
    face(session(), {200, 305});
    paint();
    EXPECT_GT(world.hitFeedbackPresentation().remainingSeconds, 0);
    EXPECT_EQ(specialPixels(), 0);
    saveConeImage(surface, "hidden-feedback.bmp");
}
TEST_F(RaidLightConeVisibilityUiTest,
       UnscopedOutdoorRetainsFullObservationAndOriginalPresentation)
{
    A::outdoors(*app, {}, 2, false);
    face(session(), {100, 190});
    EXPECT_FALSE(session().world().lightConeObservationEnabled());
    EXPECT_FLOAT_EQ(session().world().raidObservationKey().angleDegrees, 360);
    paint();
    EXPECT_TRUE(enemyPixel(525, 165));
    const auto sample = [&]
    {
        std::vector<Uint32> pixels;
        for (int y = 68; y < 90; ++y)
            for (int x = 20; x < 550; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                EXPECT_TRUE(
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                pixels.push_back((static_cast<Uint32>(r) << 24) |
                                 (static_cast<Uint32>(g) << 16) |
                                 (static_cast<Uint32>(b) << 8) | a);
            }
        return pixels;
    };
    const auto oldCounter = sample();
    auto &enemy = const_cast<Enemy &>(session().world().enemies().back());
    static_cast<void>(enemy.takeDamage(enemy.health()));
    paint();
    EXPECT_NE(oldCounter, sample()); // Out-of-scope diagnostics retain their
                                     // original total count.
}
TEST_F(RaidLightConeVisibilityUiTest,
       HiddenEnemyDoesNotLeakThroughRightHandAiDiagnostics)
{
    A::outdoors(*app);
    face(session(), {100, 190});
    const auto sample = [&]
    {
        std::vector<Uint32> pixels;
        for (int y = 68; y < 83; ++y)
            for (int x = 980; x < 1280; ++x)
            {
                Uint8 r{}, g{}, b{}, a{};
                EXPECT_TRUE(
                    SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
                pixels.push_back((static_cast<Uint32>(r) << 24) |
                                 (static_cast<Uint32>(g) << 16) |
                                 (static_cast<Uint32>(b) << 8) | a);
            }
        return pixels;
    };
    paint();
    const auto unseen = sample();
    auto &enemy = const_cast<Enemy &>(session().world().enemies().front());
    static_cast<void>(enemy.takeDamage(enemy.health()));
    paint();
    EXPECT_EQ(unseen, sample());
}
} // namespace


namespace
{
constexpr Vec2 kOpticTestWindow{1280, 720};
constexpr Vec2 kOpticRightAim{1130, 360};
constexpr Vec2 kFarEquipmentCenter{1951, 805};
constexpr std::array<Uint8, 4> kOpaqueBlack{0, 0, 0, 255};

Vec2 opticTestPlayerCenter(const GameplayWorld &world)
{
    const auto &player = world.player();
    return {player.position().x + player.size() * 0.5F,
            player.position().y + player.size() * 0.5F};
}

Vec2 ordinaryOpticTestCamera(const GameplayWorld &world)
{
    return raidCameraOffset(opticTestPlayerCenter(world),
                            world.raidSpaceWorldSize(), kOpticTestWindow);
}

GameplayInput opticTestInput(const GameplayWorld &world, Vec2 anchor)
{
    const Vec2 camera = ordinaryOpticTestCamera(world);
    GameplayInput input;
    input.aimDownSights = true;
    input.aimWorldPosition = raidScreenToWorld(anchor, camera);
    input.aimMotionDelta = Vec2{};
    input.aimWorldBounds = raidReticleWorldBounds(camera, kOpticTestWindow);
    input.aimViewport = Rect{camera, kOpticTestWindow};
    return input;
}

void prepareTestOptic(GameSession &session, WeaponOpticMode mode, Vec2 anchor)
{
    auto &world = const_cast<GameplayWorld &>(session.world());
    auto weapon = *publishedContentRegistry().item(
        ItemDefinitionId{"item.weapon.rifle_5_45_service"}).weaponUse;
    weapon.optic = mode;
    world.configureWeaponFire(weapon);
    face(session, raidScreenToWorld(anchor, ordinaryOpticTestCamera(world)));
}

void openTestOptic(GameSession &session, WeaponOpticMode mode, Vec2 anchor)
{
    prepareTestOptic(session, mode, anchor);
    auto &world = const_cast<GameplayWorld &>(session.world());
    const auto input = opticTestInput(world, anchor);
    for (int frame = 0; frame < 64; ++frame)
        world.update(input, 1.0F / 60.0F);
    ASSERT_EQ(world.activeWeaponOptic(), mode);
    ASSERT_TRUE(world.weaponOpticView());
}

Vec2 enemyCenter(const GameplayWorld &world)
{
    const auto bounds = world.enemies().front().bounds();
    return {bounds.position.x + bounds.size.x * 0.5F,
            bounds.position.y + bounds.size.y * 0.5F};
}

Vec2 opticTestEnemySample(const GameplayWorld &world)
{
    const auto center = enemyCenter(world);
    return {center.x, center.y - 14.0F};
}
}

TEST_F(RaidLightConeVisibilityUiTest, LowOpticWidensViewportAndFollowsAimWithoutMovingPlayer)
{
    for (const int width : {1024, 1920})
    {
        SCOPED_TRACE(width);
        resize(width, width * 9 / 16);
        A::opticRange(*app);
        auto &world = const_cast<GameplayWorld &>(session().world());
        const auto ordinaryCamera = ordinaryOpticTestCamera(world);
        const auto playerBefore = opticTestPlayerCenter(world);
        const auto ordinaryPlayer = raidWorldToScreen(playerBefore, ordinaryCamera);

        A::pointerGate(*app, true, false);
        prepareTestOptic(session(), WeaponOpticMode::Low, kOpticRightAim);
        auto input = opticTestInput(world, kOpticRightAim);
        world.update(input, 1.0F / 60.0F);
        ASSERT_TRUE(world.weaponOpticView());
        ASSERT_EQ(A::opticView(*app), &*world.weaponOpticView());
        EXPECT_EQ(A::optic(*app), WeaponOpticMode::None);
        EXPECT_GT(world.weaponOpticView()->scale, 0.80F);
        EXPECT_LT(world.weaponOpticView()->scale, 1.0F);
        EXPECT_LT(world.weaponOpticView()->worldToScreen(playerBefore).x, ordinaryPlayer.x);
        for (int frame = 1; frame < 64; ++frame)
            world.update(input, 1.0F / 60.0F);
        ASSERT_EQ(A::optic(*app), WeaponOpticMode::Low);
        const auto view = *world.weaponOpticView();
        EXPECT_FLOAT_EQ(view.scale, 0.80F);
        EXPECT_FLOAT_EQ(view.lensRadius, 0.0F);
        EXPECT_FLOAT_EQ(world.raidObservationKey().angleDegrees,
                        raidSightParameters(RaidLightEnvironment::DayOutdoor).angleDegrees);
        EXPECT_FLOAT_EQ(view.worldBounds().size.x, 1600.0F);
        EXPECT_FLOAT_EQ(view.worldBounds().size.y, 900.0F);
        EXPECT_FLOAT_EQ(opticTestPlayerCenter(world).x, playerBefore.x);
        EXPECT_FLOAT_EQ(opticTestPlayerCenter(world).y, playerBefore.y);
        const auto projectedPlayer = view.worldToScreen(playerBefore);
        EXPECT_LT(projectedPlayer.x, ordinaryPlayer.x - 25.0F);

        const auto enemy = opticTestEnemySample(world);
        ASSERT_GT(enemy.x - 40.0F, ordinaryCamera.x + kOpticTestWindow.x);
        ASSERT_TRUE(world.raidObservation().pointVisible(enemy));
        const auto projected = view.worldToScreen(enemy);
        ASSERT_GT(projected.x, 0.0F);
        ASSERT_LT(projected.x, kOpticTestWindow.x);
        const auto inverse = view.screenToWorld(projected);
        EXPECT_NEAR(inverse.x, enemy.x, 0.001F);
        EXPECT_NEAR(inverse.y, enemy.y, 0.001F);

        paint();
        EXPECT_TRUE(projectedEnemyPixel(projected));
        const auto equipment = logicalPixel(view.worldToScreen(kFarEquipmentCenter));
        EXPECT_GT(equipment[0], 90);
        EXPECT_GT(equipment[1], 70);
        EXPECT_LT(equipment[2], 70);
        const auto playerPixel = logicalPixel(view.worldToScreen(
            {playerBefore.x - 12.0F, playerBefore.y}));
        EXPECT_GT(playerPixel[1], 150);
        EXPECT_GT(playerPixel[2], 100);
        saveConeImage(surface, width == 1920 ? "optic-v2-low-1920.bmp" : "optic-v2-low-1024.bmp");

        const auto aim = world.weaponAimWorldPosition();
        for (int frame = 0; frame < 12; ++frame)
            world.update(input, 1.0F / 60.0F);
        ASSERT_TRUE(world.weaponOpticView());
        EXPECT_FLOAT_EQ(world.weaponOpticView()->camera.x, view.camera.x);
        EXPECT_FLOAT_EQ(world.weaponOpticView()->camera.y, view.camera.y);
        EXPECT_NEAR(world.weaponAimWorldPosition().x, aim.x, 0.01F);
        EXPECT_NEAR(world.weaponAimWorldPosition().y, aim.y, 0.01F);

        input.aimMotionDelta = Vec2{-810, 0};
        world.update(input, 1.0F / 60.0F);
        ASSERT_TRUE(world.weaponOpticView());
        const auto swept = *world.weaponOpticView();
        EXPECT_GT(swept.worldToScreen(playerBefore).x, ordinaryPlayer.x + 25.0F);
        EXPECT_FLOAT_EQ(opticTestPlayerCenter(world).x, playerBefore.x);
        const auto scoped = swept.worldToScreen(world.weaponAimWorldPosition());

        A::pointerGate(*app, false, false);
        input.aimDownSights = false;
        input.aimMotionDelta = Vec2{};
        world.update(input, 1.0F / 60.0F);
        EXPECT_EQ(A::optic(*app), WeaponOpticMode::None);
        ASSERT_TRUE(world.weaponOpticView());
        ASSERT_EQ(A::opticView(*app), &*world.weaponOpticView());
        EXPECT_GT(world.weaponOpticView()->scale, view.scale);
        EXPECT_LT(world.weaponOpticView()->scale, 1.0F);
        const auto lowering = world.weaponOpticView()->worldToScreen(world.weaponAimWorldPosition());
        EXPECT_NEAR(lowering.x, scoped.x, 1.0F);
        EXPECT_NEAR(lowering.y, scoped.y, 1.0F);
        paint();
        for (int frame = 0; frame < 64 && world.weaponOpticView(); ++frame)
            world.update(input, 1.0F / 60.0F);
        EXPECT_FALSE(world.weaponOpticView());
        EXPECT_EQ(A::opticView(*app), nullptr);
        const auto released = raidWorldToScreen(world.weaponAimWorldPosition(), ordinaryCamera);
        EXPECT_NEAR(released.x, scoped.x, 1.0F);
        EXPECT_NEAR(released.y, scoped.y, 1.0F);
        paint();
        expectOrdinaryObservationTargetScales();
        EXPECT_FALSE(projectedEnemyPixel(projected));
    }
}

TEST_F(RaidLightConeVisibilityUiTest, HighOpticSweepsBeyondOrdinaryViewportWithBlackPeriphery)
{
    for (const int width : {1024, 1920})
    {
        SCOPED_TRACE(width);
        resize(width, width * 9 / 16);
        A::opticRange(*app);
        A::pointerGate(*app, true, false);
        openTestOptic(session(), WeaponOpticMode::High, kOpticRightAim);
        auto &world = const_cast<GameplayWorld &>(session().world());
        ASSERT_EQ(A::optic(*app), WeaponOpticMode::High);
        const auto view = *world.weaponOpticView();
        EXPECT_FLOAT_EQ(view.scale, 0.90F);
        EXPECT_FLOAT_EQ(view.lensRadius, 720.0F * 0.27F);
        const auto ordinaryCamera = ordinaryOpticTestCamera(world);
        const auto enemy = opticTestEnemySample(world);
        ASSERT_GT(enemy.x - 40.0F, ordinaryCamera.x + kOpticTestWindow.x);
        ASSERT_TRUE(world.raidObservation().pointVisible(enemy));
        const auto projected = view.worldToScreen(enemy);
        const auto reticle = view.worldToScreen(world.weaponAimWorldPosition());
        ASSERT_GT(projected.x, 0.0F);
        ASSERT_LT(projected.x, kOpticTestWindow.x);
        ASSERT_LT(std::hypot(projected.x - reticle.x, projected.y - reticle.y), view.lensRadius);

        paint();
        EXPECT_TRUE(projectedEnemyPixel(projected));
        for (const auto pixel : {Vec2{300, 315}, Vec2{580, 410}})
            EXPECT_EQ(logicalPixel(pixel), kOpaqueBlack);
        saveConeImage(surface, width == 1920 ? "optic-v2-high-1920.bmp" : "optic-v2-high-1024.bmp");
        if (width == 1920 && std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
        {
            // Software rendering is timed separately from the simulation gate.
            std::array<double, 24> frames{};
            for (double &sample : frames)
            {
                const auto start = std::chrono::steady_clock::now();
                paint();
                sample = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            }
            std::sort(frames.begin(), frames.end());
            RecordProperty("software_frame_p95_ms", std::to_string(frames[22]));
        }

        auto input = opticTestInput(world, kOpticRightAim);
        input.aimMotionDelta = Vec2{-310, 0};
        world.update(input, 1.0F / 60.0F);
        input.aimMotionDelta = Vec2{};
        for (int frame = 0; frame < 90; ++frame)
            world.update(input, 1.0F / 60.0F);
        ASSERT_TRUE(world.weaponOpticView());
        const auto swept = *world.weaponOpticView();
        EXPECT_LT(swept.camera.x, view.camera.x - 60.0F);
        const auto movedReticle = swept.worldToScreen(world.weaponAimWorldPosition());
        EXPECT_LT(movedReticle.x, reticle.x - 100.0F);
        const auto stillObservable = opticTestEnemySample(world);
        ASSERT_TRUE(world.raidObservation().pointVisible(stillObservable));
        const auto outsideLens = swept.worldToScreen(stillObservable);
        ASSERT_GT(outsideLens.x, 0.0F);
        ASSERT_LT(outsideLens.x, kOpticTestWindow.x);
        ASSERT_GT(std::hypot(outsideLens.x - movedReticle.x,
                             outsideLens.y - movedReticle.y), swept.lensRadius + 4.0F);
        paint();
        EXPECT_EQ(logicalPixel(outsideLens), kOpaqueBlack);
        saveConeImage(surface, width == 1920 ? "optic-v2-sweep-1920.bmp" : "optic-v2-sweep-1024.bmp");

        A::opticRange(*app, true);
        openTestOptic(session(), WeaponOpticMode::High, kOpticRightAim);
        const auto &blockedWorld = session().world();
        const auto blocked = opticTestEnemySample(blockedWorld);
        ASSERT_FALSE(blockedWorld.raidObservation().pointVisible(blocked));
        const auto blockedView = *blockedWorld.weaponOpticView();
        const auto blockedPixel = blockedView.worldToScreen(blocked);
        const auto blockedReticle = blockedView.worldToScreen(blockedWorld.weaponAimWorldPosition());
        ASSERT_LT(std::hypot(blockedPixel.x - blockedReticle.x,
                             blockedPixel.y - blockedReticle.y), blockedView.lensRadius);
        paint();
        EXPECT_FALSE(projectedEnemyPixel(blockedPixel));
        saveConeImage(surface, width == 1920 ? "optic-v2-wall-1920.bmp" : "optic-v2-wall-1024.bmp");

        A::opticRange(*app);
        auto &nightWorld = const_cast<GameplayWorld &>(session().world());
        nightWorld.setRaidDaylight(false);
        openTestOptic(session(), WeaponOpticMode::High, kOpticRightAim);
        const auto dark = opticTestEnemySample(nightWorld);
        ASSERT_FALSE(nightWorld.raidObservation().pointVisible(dark));
        const auto nightView = *nightWorld.weaponOpticView();
        const auto darkPixel = nightView.worldToScreen(dark);
        const auto nightReticle = nightView.worldToScreen(nightWorld.weaponAimWorldPosition());
        ASSERT_LT(std::hypot(darkPixel.x - nightReticle.x,
                             darkPixel.y - nightReticle.y), nightView.lensRadius);
        paint();
        EXPECT_FALSE(projectedEnemyPixel(darkPixel));
    }
}

TEST_F(RaidLightConeVisibilityUiTest, HighOpticRendersIntermediateFramesAndReverses)
{
    constexpr Vec2 marker{1495, 821};
    constexpr Vec2 healthBar{100, 611};
    constexpr float frameTime = 1.0F / 60.0F;
    for (const int width : {1024, 1920})
    {
        SCOPED_TRACE(width);
        resize(width, width * 9 / 16);
        A::opticRange(*app, false, true);
        auto &world = const_cast<GameplayWorld &>(session().world());
        A::pointerGate(*app, false, false);
        prepareTestOptic(session(), WeaponOpticMode::High, kOpticRightAim);
        const auto ordinaryCamera = ordinaryOpticTestCamera(world);
        const auto startAnchor = raidWorldToScreen(world.weaponAimWorldPosition(), ordinaryCamera);
        const auto expectAnchor = [&](const WeaponOpticViewProjection &view)
        {
            const auto anchor = view.worldToScreen(world.weaponAimWorldPosition());
            EXPECT_NEAR(anchor.x, startAnchor.x, 1.0F);
            EXPECT_NEAR(anchor.y, startAnchor.y, 1.0F);
        };
        const auto sampleMarker = [&](const WeaponOpticViewProjection &view)
        {
            const auto point = view.worldToScreen(marker);
            const auto reticle = view.worldToScreen(world.weaponAimWorldPosition());
            EXPECT_GT(std::hypot(point.x - reticle.x, point.y - reticle.y), view.lensRadius + 4.0F);
            return logicalPixel(point);
        };

        paint();
        const auto ordinaryMarker = logicalPixel(raidWorldToScreen(marker, ordinaryCamera));
        const auto ordinaryHud = logicalPixel(healthBar);
        EXPECT_GT(ordinaryMarker[0], 60);
        EXPECT_GT(ordinaryHud[1], 100);
        ASSERT_EQ(A::opticView(*app), nullptr);

        A::pointerGate(*app, true, false);
        auto input = opticTestInput(world, kOpticRightAim);
        world.update(input, frameTime);
        ASSERT_TRUE(world.weaponOpticView());
        const auto first = *world.weaponOpticView();
        ASSERT_EQ(A::opticView(*app), &*world.weaponOpticView());
        EXPECT_EQ(A::optic(*app), WeaponOpticMode::None);
        EXPECT_GT(first.transitionWeight, 0.0F);
        EXPECT_LT(first.transitionWeight, 1.0F);
        EXPECT_LT(first.scale, 1.0F);
        EXPECT_GT(first.scale, 0.90F);
        expectAnchor(first);
        paint();
        const auto openingMarker = sampleMarker(first);
        EXPECT_GT(openingMarker[0], 0);
        EXPECT_LE(openingMarker[0], ordinaryMarker[0]);
        EXPECT_EQ(logicalPixel(healthBar), ordinaryHud);

        for (int frame = 1; frame < 5; ++frame)
            world.update(input, frameTime);
        ASSERT_TRUE(world.weaponOpticView());
        const auto middle = *world.weaponOpticView();
        EXPECT_GT(middle.transitionWeight, first.transitionWeight);
        EXPECT_LT(middle.transitionWeight, 1.0F);
        EXPECT_LT(middle.scale, first.scale);
        expectAnchor(middle);
        const auto buildsBefore = world.indoorObservationBuildCount();
        paint();
        EXPECT_LE(world.indoorObservationBuildCount(), buildsBefore + 1);
        const auto middleMarker = sampleMarker(middle);
        EXPECT_GT(middleMarker[0], 0);
        EXPECT_LT(middleMarker[0], openingMarker[0]);
        EXPECT_EQ(logicalPixel(healthBar), ordinaryHud);
        saveConeImage(surface, width == 1920
            ? "optic-v3-high-opening-1920.bmp" : "optic-v3-high-opening-1024.bmp");
        if (width == 1920 && std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR"))
        {
            // Time the same settled intermediate pose separately from simulation.
            std::array<double, 24> frames{};
            const auto built = world.indoorObservationBuildCount();
            for (double &sample : frames)
            {
                const auto start = std::chrono::steady_clock::now();
                paint();
                sample = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
            }
            EXPECT_EQ(world.indoorObservationBuildCount(), built);
            std::sort(frames.begin(), frames.end());
            RecordProperty("transition_software_frame_p95_ms", std::to_string(frames[22]));
        }

        A::pointerGate(*app, false, false);
        input.aimDownSights = false;
        world.update(input, frameTime);
        ASSERT_TRUE(world.weaponOpticView());
        const auto reversing = *world.weaponOpticView();
        ASSERT_EQ(A::opticView(*app), &*world.weaponOpticView());
        EXPECT_GT(reversing.transitionWeight, 0.0F);
        EXPECT_LT(reversing.transitionWeight, middle.transitionWeight);
        EXPECT_GT(reversing.scale, middle.scale);
        expectAnchor(reversing);
        paint();
        const auto reversingMarker = sampleMarker(reversing);
        EXPECT_GT(reversingMarker[0], middleMarker[0]);
        EXPECT_LT(reversingMarker[0], ordinaryMarker[0]);
        EXPECT_EQ(logicalPixel(healthBar), ordinaryHud);
        saveConeImage(surface, width == 1920
            ? "optic-v3-high-lowering-1920.bmp" : "optic-v3-high-lowering-1024.bmp");

        A::pointerGate(*app, true, false);
        input.aimDownSights = true;
        world.update(input, frameTime);
        ASSERT_TRUE(world.weaponOpticView());
        EXPECT_NEAR(world.weaponOpticView()->transitionWeight, middle.transitionWeight, 0.001F);
        expectAnchor(*world.weaponOpticView());
        for (int frame = 0; frame < 64 && A::optic(*app) == WeaponOpticMode::None; ++frame)
            world.update(input, frameTime);
        ASSERT_EQ(A::optic(*app), WeaponOpticMode::High);
        ASSERT_TRUE(world.weaponOpticView());
        EXPECT_FLOAT_EQ(world.weaponOpticView()->transitionWeight, 1.0F);
        expectAnchor(*world.weaponOpticView());
        paint();
        EXPECT_EQ(sampleMarker(*world.weaponOpticView()), kOpaqueBlack);
        EXPECT_EQ(logicalPixel(healthBar), ordinaryHud);

        A::pointerGate(*app, false, false);
        input.aimDownSights = false;
        world.update(input, frameTime);
        EXPECT_EQ(A::optic(*app), WeaponOpticMode::None);
        ASSERT_TRUE(world.weaponOpticView());
        ASSERT_EQ(A::opticView(*app), &*world.weaponOpticView());
        EXPECT_GT(world.weaponOpticView()->transitionWeight, 0.0F);
        EXPECT_LT(world.weaponOpticView()->transitionWeight, 1.0F);
        expectAnchor(*world.weaponOpticView());
        paint();

        // A menu cancels even while the camera is lowering. Its next ordinary
        // update consumes the previous projection without moving the pointer.
        A::pointerGate(*app, true, true);
        EXPECT_EQ(A::opticView(*app), nullptr);
        EXPECT_FALSE(A::opticAllowed(*app));
        input.aimDownSights = true;
        input.allowWeaponOptics = false;
        world.update(input, frameTime);
        EXPECT_FALSE(world.weaponOpticView());
        const auto resetAnchor = raidWorldToScreen(world.weaponAimWorldPosition(), ordinaryCamera);
        EXPECT_NEAR(resetAnchor.x, startAnchor.x, 1.0F);
        EXPECT_NEAR(resetAnchor.y, startAnchor.y, 1.0F);
        A::pointerGate(*app, false, false);
        paint();
        expectOrdinaryObservationTargetScales();
    }
}

TEST_F(RaidLightConeVisibilityUiTest, ConsumedRightButtonCannotReopenOpticThroughMenu)
{
    A::opticRange(*app);
    A::pointerGate(*app, true, false);
    openTestOptic(session(), WeaponOpticMode::High, kOpticRightAim);
    ASSERT_EQ(A::optic(*app), WeaponOpticMode::High);
    auto &world = const_cast<GameplayWorld &>(session().world());
    ASSERT_TRUE(world.weaponOpticView());
    const auto beforeMenu = world.weaponOpticView()->worldToScreen(world.weaponAimWorldPosition());
    A::pointerGate(*app, true, true);
    EXPECT_EQ(A::optic(*app), WeaponOpticMode::None);
    EXPECT_EQ(world.activeWeaponOptic(), WeaponOpticMode::None);
    A::pointerGate(*app, true, false);
    EXPECT_FALSE(A::opticAllowed(*app));
    A::pointerGate(*app, false, false);
    auto released = opticTestInput(world, kOpticRightAim);
    released.aimDownSights = false;
    world.update(released, 1.0F / 60.0F);
    EXPECT_FALSE(world.weaponOpticView());
    const auto afterMenu = raidWorldToScreen(world.weaponAimWorldPosition(),
                                             ordinaryOpticTestCamera(world));
    EXPECT_NEAR(afterMenu.x, beforeMenu.x, 1.0F);
    EXPECT_NEAR(afterMenu.y, beforeMenu.y, 1.0F);
    A::pointerGate(*app, true, false);
    EXPECT_TRUE(A::opticAllowed(*app));
}


TEST_F(RaidLightConeVisibilityUiTest, OrdinaryViewMatchesBeforeAndAfterOpticPasses)
{
    constexpr Vec2 healthBar{100.0F, 611.0F};
    for (const int width : {1280, 1920})
    {
        SCOPED_TRACE(width);
        resize(width, width * 9 / 16);
        A::opticRange(*app, false, true);
        face(session(), {1750.0F, 720.0F});
        const auto &world = session().world();

        // Hold all simulation and HUD state fixed. The only operation between
        // ordinary frames is a real scoped draw into the shared targets.
        paint();
        expectOrdinaryObservationTargetScales();
        const auto *pixels = static_cast<const Uint8 *>(surface->pixels);
        const auto byteCount = static_cast<std::size_t>(surface->pitch) * surface->h;
        const std::vector<Uint8> ordinary(pixels, pixels + byteCount);
        const auto healthBefore = logicalPixel(healthBar);
        ASSERT_GT(healthBefore[1], 100);

        for (const auto mode : {WeaponOpticMode::Low, WeaponOpticMode::High})
        {
            SCOPED_TRACE(static_cast<int>(mode));
            for (const float weight : {0.5F, 1.0F})
            {
                SCOPED_TRACE(weight);
                const auto scoped = weaponOpticProjection(mode, opticTestPlayerCenter(world),
                    world.raidSpaceWorldSize(), kOpticTestWindow, {1110.0F, 360.0F}, weight);
                ASSERT_TRUE(A::paintOptic(*app, scoped));
                const auto scopedScale = A::observationTargetScales(*app).front();
                ASSERT_FLOAT_EQ(scopedScale.x, scoped.scale);
                ASSERT_FLOAT_EQ(scopedScale.y, scoped.scale);

                paint();
                expectOrdinaryObservationTargetScales();
                EXPECT_EQ(logicalPixel(healthBar), healthBefore);
                EXPECT_TRUE(std::equal(ordinary.begin(), ordinary.end(),
                    static_cast<const Uint8 *>(surface->pixels)))
                    << "The ordinary frame must exactly match its pre-optic scene and HUD.";
            }
        }
    }
}

TEST_F(RaidLightConeVisibilityUiTest, DoorCancelsLowOpticAndRestoresOrdinaryObservationScale)
{
    for (const int width : {1280, 1920})
    {
        SCOPED_TRACE(width);
        resize(width, width * 9 / 16);
        A::syntheticRoom(*app, true);
        auto &world = const_cast<GameplayWorld &>(session().world());
        const auto portals = world.visibleRaidSpacePortals();
        ASSERT_EQ(portals.size(), 1U);
        ASSERT_TRUE(portals.front().returnsOutside);
        const auto door = portals.front().bounds;
        position({door.position.x + door.size.x / 2.0F,
                  door.position.y + door.size.y / 2.0F});
        ASSERT_TRUE(world.raidSpacePortalInteractionInRange());
        A::pointerGate(*app, true, false);
        openTestOptic(session(), WeaponOpticMode::Low, kOpticRightAim);
        ASSERT_TRUE(world.weaponOpticView());
        const auto before = world.weaponOpticView()->worldToScreen(world.weaponAimWorldPosition());
        paint();

        auto input = opticTestInput(world, kOpticRightAim);
        input.interactJustPressed = true;
        world.update(input, 0.0F);
        ASSERT_TRUE(world.spaceTransitionedLastUpdate());
        ASSERT_TRUE(world.inOutdoorRaidSpace());
        EXPECT_FALSE(world.weaponOpticView());
        const auto after = raidWorldToScreen(world.weaponAimWorldPosition(),
                                             ordinaryOpticTestCamera(world));
        EXPECT_NEAR(after.x, before.x, 1.0F);
        EXPECT_NEAR(after.y, before.y, 1.0F);
        paint();
        expectOrdinaryObservationTargetScales();
        A::pointerGate(*app, false, false);
    }
}
