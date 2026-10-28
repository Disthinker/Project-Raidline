#include "app.h"
#include "enemy_variant.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <vector>

struct RunnerInfectedUiTestAccess
{
    static GameplayWorld &world(App &app) { return app.gameSession_.world(); }
    static void replaceWorld(App &app, RaidWorldConfig config)
    { app.gameSession_.world_ = std::make_unique<GameplayWorld>(std::move(config)); }
    static bool attach(App &app, SDL_Renderer *renderer)
    {
        app.renderer_ = renderer;
        if (!app.uiTextRenderer_.initialize(renderer)) return false;
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STATIC, 1536, 640);
        if (!texture) return false;
        std::vector<Uint32> pixels(1536U * 640U, 0xEA32D2FFU);
        if (!SDL_UpdateTexture(texture, nullptr, pixels.data(), 1536 * 4))
        { SDL_DestroyTexture(texture); return false; }
        app.enemyMoveHorizontalTexture_ = Texture{texture};
        return true;
    }
    static void detach(App &app)
    {
        app.indoorInformationLayer_.reset(); app.indoorShadowMask_.reset();
        app.enemyMoveHorizontalTexture_.reset();
        app.uiTextRenderer_.shutdown(); app.renderer_ = nullptr;
    }
    static void language(App &app, UiLanguage language) { app.uiTextRenderer_.setLanguage(language); }
    static std::size_t cachedLabels(App &app) { return app.uiTextRenderer_.cachedLabelCount(); }
    static void drawEnemies(App &app) { app.renderEnemies(); }
    static bool drawObservedWorld(App &app) { return app.renderIndoorInformationLayer({0, 0, 1280, 720}); }
};

namespace
{
using Access = RunnerInfectedUiTestAccess;
RaidWorldConfig uiArena(bool runner, std::vector<BallisticBlocker> walls = {})
{
    RaidWorldConfig config;
    config.worldSize = {1280, 720}; config.playerSpawn = {375, 165};
    config.extractionPoint = {{1100, 600}, {100, 100}};
    config.lightConeObservationEnabled = true;
    // Deliberately give the ordinary control the same HP. Identity owns labels.
    config.initialEnemies.push_back(EnemySpawn{{500, 140}, {50, 50}, 8});
    if (runner) config.initialEnemies.back().variant = runnerEnemyVariantSnapshot();
    config.ballisticBlockers = std::move(walls);
    return config;
}
class RunnerInfectedUiTest : public testing::Test
{
protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    void SetUp() override
    {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        app = std::make_unique<App>();
        surface = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(Access::attach(*app, renderer));
        Access::language(*app, UiLanguage::SimplifiedChinese);
    }
    void TearDown() override
    {
        if (app) Access::detach(*app);
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
        app.reset(); SDL_Quit();
    }
    void stage(RaidWorldConfig config, Vec2 target = {800, 190})
    {
        Access::replaceWorld(*app, std::move(config));
        auto &world = Access::world(*app);
        GameplayInput input; input.aimWorldPosition = target;
        input.aimWorldBounds = Rect{{}, world.raidSpaceWorldSize()};
        world.update(input, 0);
        const auto actual = world.weaponAimWorldPosition();
        input.aimMotionDelta = Vec2{target.x - actual.x, target.y - actual.y};
        world.update(input, 0);
    }
    void clear()
    {
        SDL_SetRenderViewport(renderer, nullptr); SDL_SetRenderClipRect(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ASSERT_TRUE(SDL_RenderClear(renderer));
    }
    std::vector<Uint32> pixels(SDL_Rect region)
    {
        std::vector<Uint32> result;
        for (int y = region.y; y < region.y + region.h; ++y)
            for (int x = region.x; x < region.x + region.w; ++x)
            {
                Uint8 r{}, g{}, b{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &alpha));
                result.push_back((static_cast<Uint32>(r) << 16) |
                    (static_cast<Uint32>(g) << 8) | b);
            }
        return result;
    }
    void directPaint()
    { clear(); Access::drawEnemies(*app); ASSERT_TRUE(SDL_RenderPresent(renderer)); }
    void observedPaint()
    { clear(); ASSERT_TRUE(Access::drawObservedWorld(*app)); ASSERT_TRUE(SDL_RenderPresent(renderer)); }
};
}

TEST_F(RunnerInfectedUiTest, VisibleChineseLabelComesFromVariantAndReusesTextCache)
{
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "RUNNER"), "迅捷");
    EXPECT_EQ(localizeUiText(UiLanguage::English, "RUNNER"), "RUNNER");
    stage(uiArena(false));
    directPaint();
    const auto ordinaryLabelRegion = pixels({490, 102, 100, 23});
    EXPECT_EQ(Access::cachedLabels(*app), 0U);
    stage(uiArena(true));
    ASSERT_TRUE(Access::world(*app).raidEnemyObservable(Access::world(*app).enemies().front().combatTargetId()));
    directPaint();
    const auto runnerLabelRegion = pixels({490, 102, 100, 23});
    EXPECT_NE(runnerLabelRegion, ordinaryLabelRegion);
    EXPECT_GT(std::count_if(runnerLabelRegion.begin(), runnerLabelRegion.end(),
        [](Uint32 pixel) { return pixel != 0; }), 15);
    const auto warmCount = Access::cachedLabels(*app);
#if defined(_WIN32)
    EXPECT_EQ(warmCount, 1U);
#else
    // Linux's existing SDL debug-font backend draws without texture caching.
    EXPECT_EQ(warmCount, 0U);
#endif
    for (int frame = 0; frame < 40; ++frame) directPaint();
    EXPECT_EQ(Access::cachedLabels(*app), warmCount);
    EXPECT_EQ(pixels({490, 102, 100, 23}), runnerLabelRegion);
}

TEST_F(RunnerInfectedUiTest, WallAndRearHiddenRunnerDrawNoSpriteOrLabelPixels)
{
    for (bool wall : {false, true})
    {
        SCOPED_TRACE(wall);
        stage(uiArena(true, wall ? std::vector<BallisticBlocker>{{1, {{440, 80}, {4, 180}}}} :
            std::vector<BallisticBlocker>{}), wall ? Vec2{800, 190} : Vec2{100, 190});
        ASSERT_FALSE(Access::world(*app).raidEnemyObservable(Access::world(*app).enemies().front().combatTargetId()));
        directPaint();
        const auto hidden = pixels({480, 95, 120, 140});
        EXPECT_TRUE(std::all_of(hidden.begin(), hidden.end(), [](Uint32 value) { return value == 0; }));
        EXPECT_EQ(Access::cachedLabels(*app), 0U);
        EXPECT_EQ(Access::world(*app).enemies().size(), 1U);
    }
}

TEST_F(RunnerInfectedUiTest, CompletedInformationLayerClipsRunnerLabelAtObservationEdge)
{
    auto config = uiArena(false);
    // Both the Chinese Windows font and SDL's two 8px fallback glyphs cross
    // this boundary. Prove that with raw pixels before checking the mask.
    config.playerSpawn = {485, 284};
    stage(config, {900, 300});
    ASSERT_TRUE(Access::world(*app).raidEnemyObservable(Access::world(*app).enemies().front().combatTargetId()));
    directPaint();
    const auto rawOrdinary = pixels({485, 100, 120, 25});
    observedPaint();
    const auto ordinary = pixels({485, 100, 120, 25});
    config.initialEnemies.front().variant = runnerEnemyVariantSnapshot();
    stage(config, {900, 300});
    directPaint();
    const auto rawFast = pixels({485, 100, 120, 25});
    observedPaint();
    const auto fast = pixels({485, 100, 120, 25});
    const auto &view = Access::world(*app).raidObservation();
    std::size_t hiddenPixels = 0, changedVisiblePixels = 0;
    std::size_t rawHiddenGlyphPixels = 0, rawVisibleGlyphPixels = 0;
    for (int y = 100; y < 125; ++y)
        for (int x = 485; x < 605; ++x)
        {
            const auto index = static_cast<std::size_t>((y - 100) * 120 + x - 485);
            const bool visible = view.pointVisible({x + 0.5F, y + 0.5F});
            const bool rawGlyph = rawFast[index] != rawOrdinary[index];
            if (x < 499 && !visible)
            {
                ++hiddenPixels;
                if (rawGlyph) ++rawHiddenGlyphPixels;
                EXPECT_EQ(fast[index], ordinary[index]);
            }
            if (visible)
            {
                if (rawGlyph) ++rawVisibleGlyphPixels;
                if (fast[index] != ordinary[index]) ++changedVisiblePixels;
            }
        }
    EXPECT_GT(rawHiddenGlyphPixels, 0U);
    EXPECT_GT(rawVisibleGlyphPixels, 0U);
    EXPECT_GT(hiddenPixels, 100U);
    EXPECT_GT(changedVisiblePixels, 0U);
}
