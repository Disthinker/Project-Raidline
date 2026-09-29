#include "app.h"
#include <gtest/gtest.h>
#include <memory>

struct DeveloperRangeUiTestAccess {
    static GameFlow &flow(App &app) { return app.gameFlow_; }
    static void enter(App &app) {
        app.windowHasInputFocus_ = true;
        app.developerWeaponPanelOpen_ = true;
        app.handleDeveloperPanelClick({680, 120});
    }
    static bool capture(const App &app) { return app.shouldCaptureWorldPointer(); }
    static bool open(const App &app) { return app.developerRangePanelOpen_; }
    static void events(App &app) { app.processEvents(); app.update(0); app.input_.endFrame(); }
    static bool renderer(App &app, SDL_Renderer *renderer) {
        app.renderer_ = renderer;
        return app.uiTextRenderer_.initialize(renderer);
    }
    static void paint(App &app, UiLanguage language) {
        app.uiTextRenderer_.setLanguage(language);
        app.renderDeveloperRangePanel();
    }
    static void detach(App &app) { app.uiTextRenderer_.shutdown(); app.renderer_ = nullptr; }
};

namespace {
using Access = DeveloperRangeUiTestAccess;
void key(SDL_Scancode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = code;
    event.key.down = true;
    ASSERT_TRUE(SDL_PushEvent(&event));
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    ASSERT_TRUE(SDL_PushEvent(&event));
}
class DeveloperRangeUiTest : public testing::Test {
protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        app = std::make_unique<App>();
        ASSERT_TRUE(Access::flow(*app).startNewGame("range-ui-test"));
        Access::enter(*app);
        ASSERT_TRUE(Access::open(*app));
    }
    void TearDown() override {
        Access::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        app.reset();
        SDL_Quit();
    }
    std::size_t brightPixels(SDL_Rect rect) {
        std::size_t count{};
        for (int y = rect.y; y < rect.y + rect.h; ++y)
            for (int x = rect.x; x < rect.x + rect.w; ++x) {
                Uint8 r{}, g{}, b{}, a{};
                if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) && r > 150 && g > 150 && b > 150)
                    ++count;
            }
        return count;
    }
};
}

TEST_F(DeveloperRangeUiTest, ActualPanelTextContrastsWithBackgroundAndButtons)
{
    surface = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(surface, nullptr);
    renderer = SDL_CreateSoftwareRenderer(surface);
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(Access::renderer(*app, renderer));
    for (auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ASSERT_TRUE(SDL_RenderClear(renderer));
        Access::paint(*app, language);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        EXPECT_GT(brightPixels({130, 55, 800, 20}), 30U);  // title
        EXPECT_GT(brightPixels({130, 126, 800, 20}), 30U); // selected weapon
        EXPECT_GT(brightPixels({138, 520, 280, 20}), 30U); // grant button
        EXPECT_GT(brightPixels({130, 568, 850, 20}), 30U); // feedback
    }
}

TEST_F(DeveloperRangeUiTest, EntryEscapeAndF8UseTheSamePointerCapturePolicy)
{
    EXPECT_FALSE(Access::capture(*app));
    key(SDL_SCANCODE_ESCAPE);
    Access::events(*app);
    ASSERT_FALSE(Access::open(*app));
    EXPECT_TRUE(Access::capture(*app));
    key(SDL_SCANCODE_F8);
    Access::events(*app);
    ASSERT_TRUE(Access::open(*app));
    EXPECT_FALSE(Access::capture(*app));
    // Click the real close button while the pointer is available.
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 950;
    event.button.y = 640;
    ASSERT_TRUE(SDL_PushEvent(&event));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ASSERT_TRUE(SDL_PushEvent(&event));
    Access::events(*app);
    EXPECT_FALSE(Access::open(*app));
    EXPECT_TRUE(Access::capture(*app));
}
