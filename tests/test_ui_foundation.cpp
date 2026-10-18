#include "app.h"
#include "raid_camera.h"
#include "ui_foundation.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <nlohmann/json.hpp>

struct UiFoundationTestAccess {
  static void attach(App &a, SDL_Window *w, SDL_Renderer *r,
                     const std::filesystem::path &path) {
    a.window_ = w;
    a.renderer_ = r;
    a.settingsPath_ = path;
    ASSERT_TRUE(a.uiTextRenderer_.initialize(r));
  }
  static void detach(App &a) {
    a.uiTextRenderer_.shutdown();
    a.renderer_ = nullptr;
    a.window_ = nullptr;
  }
  static void frame(App &a, float dt = 0.016F) {
    a.processEvents();
    a.update(dt);
    a.syncRaidPointerCapture();
    a.pendingRelativeAimMotion_ = {};
    a.input_.endFrame();
  }
  static void draw(App &a, bool paused = false) {
    a.renderFoundationMenu(paused);
  }
  static void settings(App &a, bool enabled) { a.settingsOpen_ = enabled; }
  static bool settings(const App &a) { return a.settingsOpen_; }
  static auto &flow(App &a) { return a.gameFlow_; }
  static auto &text(App &a) { return a.uiTextRenderer_; }
  static auto window(const App &a) { return a.uiWindowSize_; }
  static void window(App &a, UiWindowSize size) { a.uiWindowSize_ = size; }
  static auto &selection(App &a) { return a.profileAssetSelection_; }
  static const auto &hint(App &a) { return a.uiHoveredHint_; }
  static void pause(App &a, bool settings) {
    a.pauseMenu_.open();
    if (settings)
      a.pauseMenu_.showSettings();
  }
  static void resetPause(App &a) { a.pauseMenu_.close(); }
  static bool captured(const App &a) { return a.relativeMouseModeActive_; }
  static bool fire(App &a) { return a.makeBaseGameplayInput().firePressed; }
  static Vec2 aim(App &a) {
    return a.gameFlow_.baseWorld().weaponAimWorldPosition();
  }
  static Vec2 screenAim(App &a) {
    return raidWorldToScreen(aim(a), a.baseWorldCameraOffset());
  }
};
namespace {
using A = UiFoundationTestAccess;
std::filesystem::path temporaryPath() {
  return std::filesystem::temp_directory_path() /
         ("raidline-ui-foundation-" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
}
struct TemporaryDirectory {
  std::filesystem::path path = temporaryPath();
  ~TemporaryDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};
void pointEquals(Vec2 a, Vec2 b) {
  EXPECT_NEAR(a.x, b.x, 0.02F);
  EXPECT_NEAR(a.y, b.y, 0.02F);
}
} // namespace
TEST(UiFoundationTest, PresetsRespectWorkAreaAndSmallDesktopFallback) {
  EXPECT_TRUE(uiWindowSizeAllowed({1600, 900}, 1920, 1040));
  EXPECT_FALSE(uiWindowSizeAllowed({1920, 1080}, 1920, 1040));
  EXPECT_FALSE(uiWindowSizeAllowed({999, 999}, 3000, 2000));
  const auto fallback = safeUiWindowSize({1920, 1080}, 1200, 680);
  EXPECT_LE(fallback.width, 1200);
  EXPECT_LE(fallback.height, 680);
  EXPECT_GT(fallback.width, 0);
  EXPECT_EQ(safeUiWindowSize({1920, 1080}, 1920, 1040).width, 1600);
}
TEST(UiFoundationTest, WindowLogicalRoundTripsPreserveCornersAndLetterbox) {
  for (auto size : {UiWindowSize{1280, 720}, UiWindowSize{1600, 900},
                    UiWindowSize{1920, 1080}, UiWindowSize{1920, 1200}}) {
    auto view = UiViewport::fromWindow(size.width, size.height);
    for (auto p :
         {Vec2{0, 0}, Vec2{1279, 719}, Vec2{640, 360}, Vec2{17.5F, 690.25F}})
      pointEquals(view.toLogical(view.toWindow(p)), p);
  }
  auto view = UiViewport::fromWindow(1920, 1200);
  EXPECT_GT(view.y, 0);
  EXPECT_LT(view.toLogical({10, 0}).y, 0);
}
TEST(UiFoundationTest, LanguageAndWindowRoundTripPreserveUnknownSettings) {
  TemporaryDirectory temp;
  std::filesystem::create_directories(temp.path);
  const auto path = temp.path / "设置.json";
  {
    std::ofstream out(path);
    out << R"({"language":"en-US","future":{"enabled":true}})";
  }
  auto settings = loadUiClientSettings(path);
  settings.window = {1600, 900};
  ASSERT_TRUE(saveUiClientSettings(path, settings)) << SDL_GetError();
  ASSERT_TRUE(saveUiLanguage(path, UiLanguage::SimplifiedChinese));
  auto loaded = loadUiClientSettings(path);
  EXPECT_EQ(loaded.window.width, 1600);
  EXPECT_EQ(loaded.language, UiLanguage::SimplifiedChinese);
  nlohmann::json document;
  {
    std::ifstream in(path);
    in >> document;
  }
  EXPECT_TRUE(document["future"]["enabled"].get<bool>());
  settings = loaded;
  settings.window = {1920, 1080};
  ASSERT_TRUE(saveUiClientSettings(path, settings));
  EXPECT_EQ(loadUiLanguage(path), UiLanguage::SimplifiedChinese);
}
TEST(UiFoundationTest,
     LegacyCorruptAndWrongTypeSettingsHaveSafeIndependentDefaults) {
  TemporaryDirectory temp;
  std::filesystem::create_directories(temp.path);
  const auto path = temp.path / "settings.json";
  for (auto value :
       {R"({"language":"en-US"})",
        R"({"language":"en-US","window":{"width":"large","height":1080}})",
        R"({"language":"en-US","window":{"width":-1,"height":0}})"}) {
    {
      std::ofstream out(path);
      out << value;
    }
    auto s = loadUiClientSettings(path);
    EXPECT_EQ(s.language, UiLanguage::English);
    EXPECT_EQ(s.window.width, 1280);
  }
  {
    std::ofstream out(path);
    out << "bad json";
  }
  EXPECT_EQ(loadUiClientSettings(path).language, UiLanguage::SimplifiedChinese);
  EXPECT_FALSE(saveUiClientSettings(temp.path, {}));
  EXPECT_TRUE(std::filesystem::is_directory(temp.path));
}
TEST(UiFoundationTest, Utf8WrappingAndEllipsisNeverSplitCodePoints) {
  const auto measure = [](std::string_view text) {
    float width = 0;
    for (unsigned char c : text)
      if ((c & 0xc0) != 0x80)
        width += c < 128 ? 1.0F : 2.0F;
    return width;
  };
  const std::string text = "边境交换站 - 1920×1080 长提示";
  auto lines = wrapUiText(text, 8, 0, measure);
  std::string rebuilt;
  for (const auto &line : lines) {
    EXPECT_LE(line.width, 8);
    rebuilt += line.text;
    EXPECT_TRUE(nlohmann::json::accept(nlohmann::json(line.text).dump()));
  }
  EXPECT_EQ(rebuilt, text);
  lines = wrapUiText(text, 8, 2, measure);
  ASSERT_EQ(lines.size(), 2);
  EXPECT_TRUE(lines.back().text.ends_with("…"));
  EXPECT_LE(lines.back().width, 8);
  EXPECT_TRUE(wrapUiText(text, 0, 0, measure).empty());
  EXPECT_TRUE(wrapUiText("中文", 1, 0, measure).empty());
}
TEST(UiFoundationTest, HoverAnimationIsBoundedAndHitAreasStayFixed) {
  EXPECT_EQ(uiHoverAmount(-1), 0);
  EXPECT_EQ(uiHoverAmount(1), 1);
  EXPECT_GT(uiHoverAmount(0.08F), uiHoverAmount(0.02F));
  const auto button = uiSettingsSizeButton(1);
  EXPECT_TRUE(uiContains(button, {button.x, button.y}));
  EXPECT_FALSE(uiContains(button, {button.x + button.w, button.y}));
  EXPECT_FALSE(uiContains(button, {button.x, button.y + button.h}));
}

class UiFoundationUiTest : public testing::Test {
protected:
  TemporaryDirectory temp;
  std::unique_ptr<App> app;
  SDL_Window *window{};
  SDL_Renderer *renderer{};
  void SetUp() override {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_DISPLAY_USABLE_BOUNDS, "0,0,3840,2160");
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
    window = SDL_CreateWindow("offscreen UI regression", 1280, 720,
                              SDL_WINDOW_HIDDEN);
    ASSERT_NE(window, nullptr);
    renderer = SDL_CreateRenderer(window, "software");
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_SetRenderLogicalPresentation(
        renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
    app = std::make_unique<App>();
    A::attach(*app, window, renderer, temp.path / "settings.json");
    A::flow(*app).configurePersistence(temp.path / "profile");
    SDL_Event e{};
    while (SDL_PollEvent(&e)) {
    }
  }
  void TearDown() override {
    A::detach(*app);
    app.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_ResetHint(SDL_HINT_DISPLAY_USABLE_BOUNDS);
  }
  UiViewport viewport() {
    int w{}, h{};
    SDL_GetWindowSize(window, &w, &h);
    return UiViewport::fromWindow(w, h);
  }
  void move(Vec2 p) {
    auto q = viewport().toWindow(p);
    SDL_WarpMouseInWindow(window, q.x, q.y);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = q.x;
    e.motion.y = q.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
  }
  void click(Vec2 p, bool release = true) {
    auto q = viewport().toWindow(p);
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = q.x;
    e.button.y = q.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
    if (release) {
      e.type = SDL_EVENT_MOUSE_BUTTON_UP;
      ASSERT_TRUE(SDL_PushEvent(&e));
    }
  }
  void key(SDL_Scancode code) {
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = code;
    ASSERT_TRUE(SDL_PushEvent(&e));
    e.type = SDL_EVENT_KEY_UP;
    ASSERT_TRUE(SDL_PushEvent(&e));
  }
  void enterBase() {
    auto profile = makeNewPublishedProfile("ui-foundation-test",
                                           publishedContentRegistry());
    ASSERT_TRUE(SaveRepository{temp.path / "profile"}
                    .save(profile, publishedContentRegistry().contentVersion())
                    .succeeded);
    ASSERT_TRUE(A::flow(*app).continueGame());
    A::frame(*app);
    ASSERT_TRUE(A::captured(*app));
  }
};
TEST_F(UiFoundationUiTest, RealMenuClicksUseLogicalCoordinatesAtEveryPreset) {
  for (auto size : kUiWindowSizes) {
    ASSERT_TRUE(SDL_SetWindowSize(window, size.width, size.height));
    SDL_SyncWindow(window);
    A::settings(*app, false);
    click({640, 479});
    A::frame(*app);
    ASSERT_TRUE(A::settings(*app));
    click({640, 589});
    A::frame(*app);
    EXPECT_FALSE(A::settings(*app));
  }
}
TEST_F(UiFoundationUiTest,
       DisplayChangeKeepsLanguageAndCancelsOnlyPendingSelection) {
  A::settings(*app, true);
  A::text(*app).setLanguage(UiLanguage::English);
  A::selection(*app) = ProfileAssetSelection{42, 1, ItemOrientation::Degrees0};
  ASSERT_TRUE(SDL_SetWindowPosition(window, 2500, 1400));
  click({640, 416});
  A::frame(*app);
  EXPECT_EQ(A::window(*app).width, 1600);
  int x{}, y{};
  SDL_GetWindowPosition(window, &x, &y);
  EXPECT_GE(x, 0);
  EXPECT_GE(y, 0);
  EXPECT_LE(x + 1600, 3840);
  EXPECT_LE(y + 900, 2160);
  EXPECT_FALSE(A::selection(*app));
  EXPECT_EQ(loadUiClientSettings(temp.path / "settings.json").window.width,
            1600);
  click({640, 314});
  A::frame(*app);
  EXPECT_EQ(loadUiLanguage(temp.path / "settings.json"),
            UiLanguage::SimplifiedChinese);
  EXPECT_EQ(loadUiClientSettings(temp.path / "settings.json").window.width,
            1600);
}
TEST_F(UiFoundationUiTest,
       LargeWindowMenuCursorAndHeldResumeCannotMoveAimOrFire) {
  ASSERT_TRUE(SDL_SetWindowSize(window, 1920, 1080));
  SDL_SyncWindow(window);
  enterBase();
  const auto aim = A::aim(*app);
  const auto reticle = A::screenAim(*app);
  key(SDL_SCANCODE_ESCAPE);
  A::frame(*app);
  ASSERT_FALSE(A::captured(*app));
  Vec2 cursor{};
  SDL_GetMouseState(&cursor.x, &cursor.y);
  pointEquals(cursor, viewport().toWindow(reticle));
  move({610, 300});
  click({610, 300}, false);
  A::frame(*app);
  ASSERT_TRUE(A::captured(*app));
  EXPECT_FALSE(A::fire(*app));
  A::frame(*app);
  pointEquals(A::aim(*app), aim);
  EXPECT_FALSE(A::fire(*app));
}
TEST_F(UiFoundationUiTest,
       DisabledContinueExplainsItselfWithoutCreatingAProfile) {
  move({610, 350});
  A::frame(*app);
  A::draw(*app);
  A::frame(*app, 0.5F);
  A::draw(*app);
  EXPECT_EQ(A::hint(*app), "NO SAVED GAME. START A NEW GAME.");
  click({610, 350});
  A::frame(*app);
  EXPECT_EQ(A::flow(*app).state(), GameFlowState::MainMenu);
  EXPECT_FALSE(std::filesystem::exists(temp.path / "profile/profile.json"));
}
TEST_F(UiFoundationUiTest, TextMeasuresWrapsAndReusesBoundedTextures) {
  auto &text = A::text(*app);
  text.setLanguage(UiLanguage::SimplifiedChinese);
  const auto width = text.measure("中文长说明 ABC", 18);
  ASSERT_GT(width, 0);
  text.setRasterScale(1.5F);
  EXPECT_NEAR(text.measure("中文长说明 ABC", 18), width, 4.0F);
  auto lines = wrapUiText("中文长说明：窗口大小改变后仍须完整可读。", 120, 0,
                          [&](auto s) { return text.measure(s, 18); });
  EXPECT_GT(lines.size(), 1);
  for (auto &line : lines)
    EXPECT_LE(line.width, 120);
  for (int i = 0; i < 1100; ++i)
    text.renderBox(renderer, {0, 0, 800, 60}, "缓存 " + std::to_string(i));
  EXPECT_LE(text.cachedLabelCount(), 1024);
  const auto count = text.cachedLabelCount();
  for (int i = 0; i < 100; ++i)
    text.renderBox(renderer, {0, 0, 800, 60}, "缓存 1099");
  EXPECT_EQ(count, text.cachedLabelCount());
}
TEST_F(UiFoundationUiTest, OffscreenMenuSamplesAtEverySizeAndLanguage) {
  const char *output = SDL_getenv("RAIDLINE_UI_QA_DIR");
  for (auto size : kUiWindowSizes)
    for (auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
      ASSERT_TRUE(SDL_SetWindowSize(window, size.width, size.height));
      SDL_SyncWindow(window);
      A::window(*app, size);
      A::text(*app).setLanguage(language);
      A::text(*app).setRasterScale(size.width / 1280.0F);
      for (int page = 0; page < 3; ++page) {
        A::resetPause(*app);
        A::settings(*app, page == 1);
        if (page == 2)
          A::pause(*app, false);
        A::draw(*app, page == 2);
        auto *surface = SDL_RenderReadPixels(renderer, nullptr);
        ASSERT_NE(surface, nullptr);
        EXPECT_EQ(surface->w, size.width);
        EXPECT_EQ(surface->h, size.height);
        if (output) {
          std::filesystem::create_directories(output);
          auto path =
              std::filesystem::path(output) /
              (std::to_string(size.width) + "-" + uiLanguageCode(language) +
               "-" + std::to_string(page) + ".bmp");
          ASSERT_TRUE(SDL_SaveBMP(surface, path.string().c_str()));
        }
        SDL_DestroySurface(surface);
      }
    }
}
