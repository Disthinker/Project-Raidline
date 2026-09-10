#include "app.h"
#include "base_siege_warning_layout.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <memory>
#include <chrono>
#include <filesystem>

// Exercises the production SDL event queue and App update, without SDL video,
// renderer, window, audio device, or persistence repository.
struct BaseSiegeUiTestAccess {
    static GameFlow &flow(App &app) { return app.gameFlow_; }
    static void frame(App &app, float dt) {
        app.processEvents();
        app.update(dt);
        app.input_.endFrame();
    }
    static bool visible(const App &app) { return app.baseSiegeWarningVisible(); }
    static bool paused(const App &app) { return app.pauseMenu_.isOpen(); }
    static bool inventory(const App &app) { return app.inventoryOverlayState_.isOpen(); }
    static bool developer(const App &app) { return app.developerWeaponPanelOpen_; }
    static void pause(App &app) { app.pauseMenu_.open(); }
    static bool pointerPressed(const App &app) { return app.input_.isPrimaryPointerPressed(); }
};

namespace {
using Access = BaseSiegeUiTestAccess;
namespace layout = base_siege_warning_layout;
void click(SDL_FRect bounds) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = bounds.x + bounds.w * 0.5F;
    event.button.y = bounds.y + bounds.h * 0.5F;
    ASSERT_TRUE(SDL_PushEvent(&event));
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ASSERT_TRUE(SDL_PushEvent(&event));
}
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

TEST(BaseDailySaveUiTest, FailedCheckpointHasClickableRetryWithoutFireOrDefense) {
    struct TemporaryStore {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("raidline-daily-ui-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        ~TemporaryStore() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } store;
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
    struct QuitEvents { ~QuitEvents() { SDL_Quit(); } } quit;
    auto app = std::make_unique<App>();
    auto &flow = Access::flow(*app);
    flow.configurePersistence(store.path);
    ASSERT_TRUE(flow.startNewGame("daily-ui", false));
    Access::frame(*app, 0.000001F);
    auto &session = flow.gameSession();
    session.advanceBaseWorldClock(1);
    const auto obstruction = store.path / "profile.tmp.json";
    ASSERT_TRUE(std::filesystem::create_directory(obstruction));
    ASSERT_FALSE(session.checkpointWorldClock());
    ASSERT_TRUE(session.baseDailySaveBlocked());
    const auto accepted = profileStateFingerprint(session.profile());
    click({978, 197, 282, 32});
    Access::frame(*app, 0.1F);
    EXPECT_TRUE(session.baseDailySaveBlocked());
    EXPECT_EQ(profileStateFingerprint(session.profile()), accepted);
    EXPECT_FALSE(flow.baseWorld().shotFiredLastUpdate());
    EXPECT_FALSE(Access::pointerPressed(*app));
    key(SDL_SCANCODE_F10); Access::frame(*app, 0.1F);
    EXPECT_TRUE(Access::developer(*app));
    key(SDL_SCANCODE_ESCAPE); Access::frame(*app, 0.1F);
    EXPECT_FALSE(Access::developer(*app));
    key(SDL_SCANCODE_ESCAPE); Access::frame(*app, 0.1F);
    EXPECT_TRUE(Access::paused(*app));
    key(SDL_SCANCODE_ESCAPE); Access::frame(*app, 0.1F);
    EXPECT_FALSE(Access::paused(*app));
    ASSERT_TRUE(std::filesystem::remove(obstruction));
    click({978, 197, 282, 32});
    Access::frame(*app, 0.000001F);
    EXPECT_FALSE(session.baseDailySaveBlocked());
    EXPECT_FALSE(session.baseDefenseActive());
    EXPECT_FALSE(flow.baseWorld().shotFiredLastUpdate());
    EXPECT_FALSE(Access::pointerPressed(*app));
    const auto saved = SaveRepository{store.path}.load(publishedContentRegistry());
    ASSERT_TRUE(saved.profile);
    EXPECT_EQ(profileStateFingerprint(*saved.profile), accepted);
}
class BaseSiegeUiTest : public ::testing::Test {
protected:
    std::unique_ptr<App> app;
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        app = std::make_unique<App>();
        ASSERT_TRUE(Access::flow(*app).startNewGame("siege-ui-headless", false));
        frame(); // finish the normal, unrelated perimeter bootstrap first
        ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());
        frame();
        ASSERT_TRUE(Access::visible(*app));
    }
    void TearDown() override { app.reset(); SDL_Quit(); }
    GameSession &session() { return Access::flow(*app).gameSession(); }
    void frame(float dt = 0) { Access::frame(*app, dt); }
    void expire() {
        session().advanceBaseWorldClock(181);
        ASSERT_EQ(session().profile().baseSiege.warningRemainingSeconds, 0U);
        ASSERT_TRUE(session().profile().baseSiege.warningActive);
    }
    void choose(bool manual) {
        const auto sequence = session().profile().baseSiege.siegeSequence;
        click(manual ? layout::manual : layout::automatic);
        frame();
        EXPECT_EQ(session().baseDefenseActive(), manual);
        EXPECT_FALSE(Access::visible(*app));
        EXPECT_FALSE(Access::paused(*app));
        EXPECT_FALSE(Access::pointerPressed(*app));
        if (!manual) {
            EXPECT_FALSE(session().profile().baseSiege.warningActive);
            EXPECT_EQ(session().profile().baseSiege.lastResolvedSequence, sequence);
        }
    }
};

TEST_F(BaseSiegeUiTest, ManualClickStartsBeforeDeadline) { choose(true); }
TEST_F(BaseSiegeUiTest, AutoClickSettlesBeforeDeadline) { choose(false); }
TEST_F(BaseSiegeUiTest, ManualClickStartsAfterDeadline) { expire(); choose(true); }
TEST_F(BaseSiegeUiTest, AutoClickSettlesAfterDeadline) { expire(); choose(false); }

TEST_F(BaseSiegeUiTest, EscapeDismissesWithoutPauseOrDomainMutationAndTimerContinues) {
    const auto fingerprint = profileStateFingerprint(session().profile());
    const auto remaining = session().profile().baseSiege.warningRemainingSeconds;
    key(SDL_SCANCODE_ESCAPE);
    frame();
    EXPECT_FALSE(Access::visible(*app));
    EXPECT_FALSE(Access::paused(*app));
    EXPECT_EQ(profileStateFingerprint(session().profile()), fingerprint);
    frame(2);
    EXPECT_LT(session().profile().baseSiege.warningRemainingSeconds, remaining);
    EXPECT_FALSE(Access::visible(*app));
    key(SDL_SCANCODE_TAB);
    frame();
    EXPECT_TRUE(Access::inventory(*app));
    const auto beforeReopen = profileStateFingerprint(session().profile());
    click(layout::banner);
    frame();
    EXPECT_TRUE(Access::visible(*app));
    EXPECT_FALSE(Access::inventory(*app));
    EXPECT_EQ(profileStateFingerprint(session().profile()), beforeReopen);
}

TEST_F(BaseSiegeUiTest, CloseButtonAndF6ReopenAfterTimerExpired) {
    click(layout::close);
    frame();
    EXPECT_FALSE(Access::visible(*app));
    expire();
    frame();
    EXPECT_FALSE(Access::visible(*app));
    key(SDL_SCANCODE_F6);
    frame();
    ASSERT_TRUE(Access::visible(*app));
    choose(true);
}

TEST_F(BaseSiegeUiTest, FirstChoiceWinsAndDoesNotFallThrough) {
    const auto fingerprint = profileStateFingerprint(session().profile());
    click(layout::close);
    click(layout::manual);
    key(SDL_SCANCODE_ESCAPE);
    frame();
    EXPECT_FALSE(Access::visible(*app));
    EXPECT_FALSE(Access::paused(*app));
    EXPECT_FALSE(session().baseDefenseActive());
    EXPECT_EQ(profileStateFingerprint(session().profile()), fingerprint);
}

TEST_F(BaseSiegeUiTest, DeveloperPanelOwnsEscapeAndClosingFrameClicks) {
    key(SDL_SCANCODE_F10);
    frame();
    ASSERT_TRUE(Access::developer(*app));
    key(SDL_SCANCODE_ESCAPE);
    click(layout::manual);
    frame();
    EXPECT_FALSE(Access::developer(*app));
    EXPECT_FALSE(Access::paused(*app));
    EXPECT_TRUE(Access::visible(*app));
    EXPECT_FALSE(session().baseDefenseActive());
    choose(true);
}

TEST_F(BaseSiegeUiTest, PauseMenuDoesNotPassClicksToWarning) {
    Access::pause(*app);
    click(layout::manual);
    frame();
    EXPECT_FALSE(session().baseDefenseActive());
    EXPECT_TRUE(session().profile().baseSiege.warningActive);
}

TEST_F(BaseSiegeUiTest, NewProfileWarningReopensDismissedPopup) {
    click(layout::close);
    frame();
    ASSERT_FALSE(Access::visible(*app));
    ASSERT_TRUE(Access::flow(*app).returnToMainMenu());
    ASSERT_TRUE(Access::flow(*app).startNewGame("another-siege-ui", false));
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());
    frame();
    EXPECT_TRUE(Access::visible(*app));
}

TEST_F(BaseSiegeUiTest, DoubleAutoClickResolvesOnlyOnce) {
    const auto sequence = session().profile().baseSiege.siegeSequence;
    click(layout::automatic);
    click(layout::automatic);
    frame();
    EXPECT_EQ(session().profile().baseSiege.lastResolvedSequence, sequence);
    const auto siege = session().profile().baseSiege;
    const auto resources = session().profile().baseResources;
    const auto transactions = session().profile().committedTransactions;
    frame();
    EXPECT_EQ(session().profile().baseSiege, siege);
    EXPECT_EQ(session().profile().baseResources, resources);
    EXPECT_EQ(session().profile().committedTransactions, transactions);
}

TEST_F(BaseSiegeUiTest, ManualChoiceWinsOverSavedPresetOnDeadlineFrame) {
    choose(false);
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());
    ASSERT_TRUE(session().profile().baseSiege.autoDefensePresetSaved);
    session().advanceBaseWorldClock(179);
    ASSERT_EQ(session().profile().baseSiege.warningRemainingSeconds, 1U);
    const auto resolved = session().profile().baseSiege.lastResolvedSequence;
    click(layout::manual);
    frame(2);
    EXPECT_TRUE(session().baseDefenseActive());
    EXPECT_EQ(session().profile().baseSiege.lastResolvedSequence, resolved);
}

TEST_F(BaseSiegeUiTest, DismissalDoesNotCancelSavedAutomaticPreset) {
    choose(false);
    ASSERT_TRUE(session().triggerDeveloperBaseSiegeWarning());
    frame();
    ASSERT_TRUE(Access::visible(*app));
    const auto sequence = session().profile().baseSiege.siegeSequence;
    click(layout::close);
    frame();
    EXPECT_FALSE(Access::visible(*app));
    frame(181);
    EXPECT_FALSE(session().profile().baseSiege.warningActive);
    EXPECT_EQ(session().profile().baseSiege.lastResolvedSequence, sequence);
}

TEST(BaseSiegeUiLocalizationTest, DismissAndReopenHintsHaveChinese) {
    for (const auto *text : {"ESC / X: KEEP PREPARING | WARNING TIMER CONTINUES",
                            "SIEGE TIMER 02:59 | F6 / CLICK: DEFENSE OPTIONS"}) {
        EXPECT_EQ(localizeUiText(UiLanguage::English, text), text);
        const auto chinese = localizeUiText(UiLanguage::SimplifiedChinese, text);
        EXPECT_NE(chinese, text);
        EXPECT_EQ(chinese.find("DEFENSE OPTIONS"), std::string::npos);
    }
}
} // namespace
