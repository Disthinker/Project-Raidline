#include "app.h"
#include "raid_camera.h"
#include "save_repository.h"
#include <algorithm>
#include <chrono>
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
