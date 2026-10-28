#include "app.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include "ui_raid_result_presentation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiRaidResultTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.raidResultUi_; }
    static auto &projection(App &a) { a.refreshRaidResultUi(); return a.raidResultProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &feedback(App &a) { return a.raidResultFeedback_; }
    static auto generation(App &a) { return a.raidResultProjectionGeneration_; }
    static void refresh(App &a) { a.refreshRaidResultUi(true); }
    static void reset(App &a) { a.resetRaidResultUi(); }
    static void draw(App &a) { a.renderRaidResult(); }
    // Visual extremes use value projections only. They never create a save,
    // run a command, or replace the real settled Profile used by input tests.
    static void visualProjection(App &a, UiRaidResultProjection projection, UiRaidResultState state = {}) {
        a.refreshRaidResultUi(true);
        a.raidResultProjection_ = std::move(projection);
        a.raidResultUi_ = std::move(state);
        a.raidResultViewKey_ = uiRaidResultViewKey(a.raidResultUi_);
    }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static bool paused(App &a) { return a.pauseMenu_.isOpen(); }
    static void seedCombatOverlays(App &a, AssetInstanceId id) {
        a.medicalWheelOpen_ = true; a.medicalWheelOptions_ = {id};
        a.throwableWheelOpen_ = true; a.throwableWheelOptions_ = {id};
        a.selectedThrowable_ = id;
        a.inventoryOverlayState_.openPlayerInventory(); a.tacticalMapOpen_ = true;
        a.wheelPointerCaptureActive_ = true;
    }
    static bool combatOverlaysOpen(const App &a) {
        return a.medicalWheelOpen_ || a.throwableWheelOpen_ ||
            a.inventoryOverlayState_.isOpen() || a.tacticalMapOpen_;
    }
    static bool combatOptionsRemain(const App &a) {
        return !a.medicalWheelOptions_.empty() || !a.throwableWheelOptions_.empty() ||
            a.selectedThrowable_.has_value();
    }
    static bool capture(const App &a) { return a.shouldCaptureWorldPointer(); }
    static void syncCapture(App &a) { a.syncRaidPointerCapture(); }
    static bool wheelCaptured(const App &a) { return a.wheelPointerCaptureActive_; }
    static bool relative(const App &a) { return a.relativeMouseModeActive_; }
    static bool cursorHidden(const App &a) { return a.systemCursorHidden_; }
    static bool hasWindow(const App &a) { return a.window_ != nullptr; }
    static const Texture &rifleTexture(const App &a) {
        return a.inventoryItemTextures_[static_cast<std::size_t>(*legacyItemId(alpha_content::rifle))];
    }
    static void attach(App &a, SDL_Renderer *renderer, float scale) {
        a.renderer_ = renderer;
        ASSERT_TRUE(a.uiTextRenderer_.initialize(renderer));
        a.uiTextRenderer_.setRasterScale(scale);
        const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
        for (std::size_t i = 0; i < itemCount(); ++i) {
            const auto &definition = publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i] = loadInventoryTexture(renderer, root / definition.inventoryTexturePath);
        }
    }
    static void detach(App &a) {
        for (auto &texture : a.inventoryItemTextures_) texture.reset();
        a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
};

namespace {
using A = UiRaidResultTestAccess;
using C = UiRaidResultControl;

Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down;
    e.button.x = p.x; e.button.y = p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void motion(Vec2 p) {
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = p.x; e.motion.y = p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void windowEvent(Uint32 type) {
    SDL_Event e{}; e.type = type; ASSERT_TRUE(SDL_PushEvent(&e));
}
void keyboard(SDL_Scancode code, bool down, bool repeat = false) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code; e.key.down = down; e.key.repeat = repeat;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void key(SDL_Scancode code) { keyboard(code, true); keyboard(code, false); }
bool contains(const std::vector<std::string> &lines, std::string_view token) {
    return std::any_of(lines.begin(), lines.end(), [&](const auto &line) {
        return line.find(token) != std::string::npos;
    });
}
LastRaidResult result(RaidResultOutcome outcome = RaidResultOutcome::Extracted,
                      std::size_t records = 0) {
    LastRaidResult r; r.settlementId = "readonly-projection-result"; r.outcome = outcome;
    for (std::size_t i = 0; i < records; ++i)
        r.returnedItemDefinitionIds.push_back(i % 2 == 0 ? alpha_content::ammunition : alpha_content::rifle);
    return r;
}
UiRaidResultProjection project(const std::optional<LastRaidResult> &r,
    const UiRaidResultState &state = {}, UiLanguage language = UiLanguage::English,
    bool testRange = false) {
    return projectUiRaidResult("readonly-projection", r, publishedContentRegistry(), language, state, testRange);
}

class UiRaidResultTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }

    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-result-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("result-ui", content);
        p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        ASSERT_TRUE(validateProfileState(p, content).valid) << validateProfileState(p, content).message;
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
        attach(1280, 720); A::text(*app).setLanguage(UiLanguage::English);
    }
    void attach(int width, int height) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, width / 1280.0F);
    }
    void detach() {
        if (app) A::detach(*app);
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
        renderer = nullptr; surface = nullptr;
    }
    void TearDown() override {
        detach(); app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void carryRecords(std::size_t count) {
        const auto pack = std::find_if(profile().assets.records().begin(), profile().assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
        ASSERT_NE(pack, profile().assets.records().end()); const auto packId = pack->first;
        const auto equipped = executeInventory(profile(), content,
            InventoryEquipCommand{packId, EquipmentSlotKind::Backpack},
            {profile().revision, "result-fixture-equip-backpack"});
        ASSERT_TRUE(equipped.succeeded) << equipped.message;
        for (std::size_t i = 0; i < count; ++i) {
            const auto origin = findFirstProfileFit(profile(), content, ProfileContainerId::compartment(packId, 0),
                content.item(alpha_content::ammunition), ItemOrientation::Degrees0);
            ASSERT_TRUE(origin);
            static_cast<void>(profile().assets.create(content.item(alpha_content::ammunition),
                StoredAssetLocation{ProfileContainerId::compartment(packId, 0), *origin},
                static_cast<std::uint32_t>(i % 50 + 2)));
            ++profile().revision;
        }
        ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
    }
    void finish(RaidResultOutcome outcome, bool frame = true) {
        ASSERT_TRUE(A::flow(*app).deploy()) << A::session(*app).persistenceMessage();
        if (outcome == RaidResultOutcome::Extracted) {
            for (const auto &enemy : A::session(*app).world().enemies())
                static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
            const auto extraction = A::session(*app).world().extractionPoint().bounds();
            ASSERT_TRUE(const_cast<Player &>(A::session(*app).world().player()).setPosition(extraction.position));
            for (int step = 0; step < 50 && A::flow(*app).state() == GameFlowState::Raid; ++step)
                A::flow(*app).update({}, 0.1F);
        } else if (outcome == RaidResultOutcome::PlayerDead) {
            ASSERT_TRUE(A::session(*app).world().damagePlayer(100)); A::flow(*app).update({}, 0);
        } else {
            ASSERT_TRUE(A::session(*app).activeQuitAlphaRaid()); A::flow(*app).update({}, 0);
        }
        ASSERT_EQ(A::flow(*app).state(), GameFlowState::RaidResult);
        ASSERT_TRUE(profile().lastRaidResult); ASSERT_EQ(profile().lastRaidResult->outcome, outcome);
        ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
        if (frame) A::frame(*app);
    }
    void click(Vec2 p) { pointer(p, true); pointer(p, false); A::frame(*app); }
    void control(C c) { click(center(uiRaidResultControlRect(c))); }
    void down(C c) { pointer(center(uiRaidResultControlRect(c)), true); A::frame(*app); }
    void up(C c) { pointer(center(uiRaidResultControlRect(c)), false); A::frame(*app); }
    void wheel(SDL_FRect r, int delta = -1) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = static_cast<float>(delta);
        const auto p = center(r); e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y;
        ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
        A::draw(*app); SDL_RenderPresent(renderer);
    }
    int bright(SDL_FRect bounds) {
        const float scale = surface->w / 1280.0F; int count{};
        for (int y = std::max(0, static_cast<int>(bounds.y * scale));
             y < std::min(surface->h, static_cast<int>((bounds.y + bounds.h) * scale)); ++y)
            for (int x = std::max(0, static_cast<int>(bounds.x * scale));
                 x < std::min(surface->w, static_cast<int>((bounds.x + bounds.w) * scale)); ++x) {
                Uint8 r{}, g{}, b{}, a{};
                if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) && r > 150 && g > 150 && b > 150) ++count;
            }
        return count;
    }
    void stillResult() { EXPECT_EQ(A::flow(*app).state(), GameFlowState::RaidResult); }
};
} // namespace

TEST(UiRaidResultProjectionTest, MissingResultIsNeutralAndNeverInventsASettlement) {
    const auto p = project(std::nullopt);
    EXPECT_FALSE(p.hasResult); EXPECT_FALSE(p.extracted); EXPECT_TRUE(p.returned.empty());
    EXPECT_TRUE(p.settlementId.empty()); EXPECT_EQ(p.pageCount, 1U); EXPECT_FALSE(p.status.empty());
}

TEST(UiRaidResultProjectionTest, DeveloperResultCannotReuseAnOlderPersistentRaid) {
    auto r = result(RaidResultOutcome::Extracted, 12); r.currencyDelta = 999;
    const auto p = project(r, {}, UiLanguage::English, true);
    EXPECT_TRUE(p.testRange); EXPECT_FALSE(p.hasResult); EXPECT_FALSE(p.extracted);
    EXPECT_TRUE(p.returned.empty()); EXPECT_EQ(p.currencyDelta, 0); EXPECT_TRUE(p.settlementId.empty());
}

TEST(UiRaidResultProjectionTest, ThreeNormalOutcomesHaveDistinctStatusAndOnlySuccessHasRecords) {
    std::vector<std::string> statuses;
    for (const auto outcome : {RaidResultOutcome::Extracted, RaidResultOutcome::PlayerDead, RaidResultOutcome::ActiveQuit}) {
        const auto p = project(result(outcome, 3));
        EXPECT_TRUE(p.hasResult); EXPECT_EQ(p.outcome, outcome); EXPECT_FALSE(p.legacyOutcome);
        EXPECT_EQ(p.extracted, outcome == RaidResultOutcome::Extracted);
        EXPECT_EQ(p.returned.size(), outcome == RaidResultOutcome::Extracted ? 3U : 0U);
        EXPECT_FALSE(p.status.empty()); statuses.push_back(p.status);
    }
    EXPECT_NE(statuses[0], statuses[1]); EXPECT_NE(statuses[1], statuses[2]); EXPECT_NE(statuses[0], statuses[2]);
}

TEST(UiRaidResultProjectionTest, LegacyOrUnknownOutcomeNeverInventsOrdinaryFailureOrReturnedLoot) {
    for (const auto outcome : {RaidResultOutcome::AbnormalQuit, static_cast<RaidResultOutcome>(255)}) {
        const auto p = project(result(outcome, 6));
        EXPECT_TRUE(p.legacyOutcome); EXPECT_FALSE(p.extracted); EXPECT_TRUE(p.returned.empty());
    }
}

TEST(UiRaidResultProjectionTest, EmptySuccessStaysSuccessWithoutInventingAReward) {
    const auto p = project(result());
    EXPECT_TRUE(p.hasResult); EXPECT_TRUE(p.extracted); EXPECT_TRUE(p.returned.empty());
    EXPECT_EQ(p.currencyDelta, 0); EXPECT_EQ(p.pageCount, 1U);
    EXPECT_FALSE(contains(p.summaryLines, "CURRENCY")); EXPECT_TRUE(contains(p.detailLines, "CURRENCY CHANGE: 0"));
}

TEST(UiRaidResultProjectionTest, AllHistoricalOccurrencesAreKeptInOrderRatherThanMergedAsStackQuantity) {
    const auto r = result(RaidResultOutcome::Extracted, 19); const auto p = project(r);
    ASSERT_EQ(p.returned.size(), r.returnedItemDefinitionIds.size()); EXPECT_EQ(p.pageCount, 3U);
    for (std::size_t i = 0; i < p.returned.size(); ++i) {
        EXPECT_EQ(p.returned[i].index, i); EXPECT_EQ(p.returned[i].definitionId, r.returnedItemDefinitionIds[i]);
        EXPECT_FALSE(p.returned[i].name.empty()); EXPECT_TRUE(p.returned[i].knownDefinition);
    }
    EXPECT_NE(p.recordNote.find("ORIGINAL GEAR"), std::string::npos);
}

TEST(UiRaidResultProjectionTest, UnknownHistoricalDefinitionRemainsAVisibleRecord) {
    auto r = result(RaidResultOutcome::Extracted, 1);
    r.returnedItemDefinitionIds.push_back(ItemDefinitionId{"item.removed.compatibility"});
    const auto p = project(r); ASSERT_EQ(p.returned.size(), 2U);
    EXPECT_FALSE(p.returned.back().knownDefinition); EXPECT_FALSE(p.returned.back().name.empty());
    EXPECT_EQ(p.returned.back().definitionId, r.returnedItemDefinitionIds.back());
}

TEST(UiRaidResultProjectionTest, FailureRecoveryTextUsesItsSavedRecordPresence) {
    auto r = result(RaidResultOutcome::PlayerDead);
    const auto noRecord = project(r); r.lostRaidRecordId = "record-committed"; const auto withRecord = project(r);
    EXPECT_NE(noRecord.detailLines, withRecord.detailLines);
    EXPECT_FALSE(noRecord.lostRaidRecordId); EXPECT_EQ(withRecord.lostRaidRecordId, r.lostRaidRecordId);
    EXPECT_FALSE(contains(withRecord.detailLines, "PERMANENTLY DESTROYED"));
}

TEST(UiRaidResultProjectionTest, RescueThreatTravelAndCurrencyComeDirectlyFromTheSavedResult) {
    auto r = result(RaidResultOutcome::ActiveQuit);
    r.currencyDelta = -12; r.travelMinutesApplied = 137;
    r.rescuedOrdinaryResidents = 2; r.rescuedInjuredResidents = 1; r.baseThreatReducedUnits = 40;
    const auto p = project(r);
    EXPECT_EQ(p.currencyDelta, -12); EXPECT_EQ(p.travelMinutesApplied, 137U);
    EXPECT_EQ(p.rescuedOrdinaryResidents, 2U); EXPECT_EQ(p.rescuedInjuredResidents, 1U);
    EXPECT_EQ(p.baseThreatReducedUnits, 40U);
    EXPECT_TRUE(contains(p.detailLines, "137"));
    EXPECT_TRUE(contains(p.summaryLines, "40")); EXPECT_TRUE(contains(p.summaryLines, "12"));
}

TEST(UiRaidResultProjectionTest, WishSufficientInsufficientAndExpiredHaveDifferentReadOnlyAdvice) {
    auto r = result();
    r.wishReturn = BaseWishReturnSummary{{{3, BasePriorityDefinitionId{"base_priority.test"}},
        BaseSupplyCategory::Food, 10, "test-version"}, 4, 12, false};
    const auto enough = project(r); EXPECT_EQ(enough.wishReturn, r.wishReturn);
    r.wishReturn->contribution = 2; const auto insufficient = project(r);
    r.wishReturn->expired = true; const auto expired = project(r);
    EXPECT_NE(enough.detailLines, insufficient.detailLines); EXPECT_NE(insufficient.detailLines, expired.detailLines);
    EXPECT_TRUE(contains(enough.detailLines, "MANUAL"));
}

TEST(UiRaidResultProjectionTest, ContextKeyTracksEveryHistoricalFieldWithoutDependingOnRevisionOrAssets) {
    const auto r = result(RaidResultOutcome::Extracted, 2);
    const auto base = uiRaidResultContextKey("profile", r, UiLanguage::English);
    std::vector<LastRaidResult> changed;
    auto copy = r; copy.settlementId += "new"; changed.push_back(copy);
    copy = r; copy.outcome = RaidResultOutcome::PlayerDead; changed.push_back(copy);
    copy = r; copy.returnedItemDefinitionIds.push_back(alpha_content::backpack); changed.push_back(copy);
    copy = r; copy.currencyDelta = -3; changed.push_back(copy);
    copy = r; copy.travelMinutesApplied = 45; changed.push_back(copy);
    copy = r; copy.rescuedOrdinaryResidents = 1; changed.push_back(copy);
    copy = r; copy.rescuedInjuredResidents = 1; changed.push_back(copy);
    copy = r; copy.baseThreatReducedUnits = 40; changed.push_back(copy);
    copy = r; copy.lostRaidRecordId = "lost"; changed.push_back(copy);
    copy = r; copy.wishReturn = BaseWishReturnSummary{}; changed.push_back(copy);
    for (const auto &different : changed)
        EXPECT_NE(uiRaidResultContextKey("profile", different, UiLanguage::English), base);
    EXPECT_NE(uiRaidResultContextKey("other", r, UiLanguage::English), base);
    EXPECT_NE(uiRaidResultContextKey("profile", r, UiLanguage::SimplifiedChinese), base);
    EXPECT_NE(uiRaidResultContextKey("profile", r, UiLanguage::English, true), base);
}

TEST(UiRaidResultProjectionTest, PageCountClampsAnOutOfRangeRequestAndCannotExposeRecordActions) {
    UiRaidResultState state; state.page = std::numeric_limits<std::size_t>::max();
    const auto p = project(result(RaidResultOutcome::Extracted, 17), state);
    EXPECT_EQ(uiRaidResultPageSize(), 8U); EXPECT_EQ(p.page, 2U); EXPECT_EQ(p.pageCount, 3U);
    EXPECT_TRUE(uiRaidResultEnabled(C::Previous, state, p)); EXPECT_FALSE(uiRaidResultEnabled(C::Next, state, p));
    for (std::size_t row = 0; row < uiRaidResultPageSize(); ++row)
        EXPECT_FALSE(hitUiRaidResult(center(uiRaidResultRowRect(row)), state, p));
}

TEST_F(UiRaidResultTest, NativeReturnRequiresDownAndUpOnTheSameButton) {
    finish(RaidResultOutcome::ActiveQuit); const auto before = profileStateFingerprint(profile());
    down(C::Return); stillResult(); EXPECT_EQ(profileStateFingerprint(profile()), before);
    up(C::Return); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiRaidResultTest, AReleaseWithoutPressCannotReturnToBase) {
    finish(RaidResultOutcome::PlayerDead); up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, MovingAwayCancelsReturnEvenWhenReleasedBackOnTheButton) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    motion({2, 2}); motion(center(uiRaidResultControlRect(C::Return))); A::frame(*app);
    up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, ReturningWithEnterDoesNotRepeatSettlementOrAdvanceTheClock) {
    carryRecords(2); finish(RaidResultOutcome::Extracted);
    const auto before = profileStateFingerprint(profile()); const auto settlements = profile().committedSettlements.size();
    key(SDL_SCANCODE_RETURN); A::frame(*app);
    EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_EQ(profile().committedSettlements.size(), settlements);
    keyboard(SDL_SCANCODE_RETURN, true, true); keyboard(SDL_SCANCODE_RETURN, false); A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_FALSE(profile().pendingRaid);
}

TEST_F(UiRaidResultTest, RepeatedEnterCannotAcknowledgeAResult) {
    finish(RaidResultOutcome::ActiveQuit);
    keyboard(SDL_SCANCODE_RETURN, true, true); keyboard(SDL_SCANCODE_RETURN, false); A::frame(*app); stillResult();
}

TEST_F(UiRaidResultTest, PointerHeldBeforeTheResultCannotSkipIt) {
    pointer(center(uiRaidResultControlRect(C::Return)), true); A::frame(*app);
    finish(RaidResultOutcome::ActiveQuit); up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, EnterHeldBeforeTheResultCannotSkipIt) {
    keyboard(SDL_SCANCODE_RETURN, true); A::frame(*app);
    finish(RaidResultOutcome::ActiveQuit);
    keyboard(SDL_SCANCODE_RETURN, true, true); keyboard(SDL_SCANCODE_RETURN, false); A::frame(*app); stillResult();
}

TEST_F(UiRaidResultTest, FocusLossCancelsAnAlreadyPressedReturn) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); A::frame(*app);
    up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, WindowResizeCancelsAnAlreadyPressedReturn) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    windowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED); A::frame(*app); up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, MouseLeavingTheWindowCancelsAnAlreadyPressedReturn) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE); windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app);
    up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, LanguageChangeCancelsAnAlreadyPressedReturn) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese); A::frame(*app); up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, PauseAndResumeCannotCarryAStaleReturnPress) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    A::pause(*app); A::frame(*app); EXPECT_TRUE(A::paused(*app)); stillResult();
    A::resume(*app); A::frame(*app); EXPECT_FALSE(A::paused(*app));
    up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, EscapeKeepsItsExistingMeaningAndCannotAcknowledgeTheResult) {
    finish(RaidResultOutcome::ActiveQuit); const auto before = profileStateFingerprint(profile());
    key(SDL_SCANCODE_ESCAPE); A::frame(*app); stillResult();
    EXPECT_FALSE(A::paused(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiRaidResultTest, FailedFirstHintAcknowledgmentStaysOnResultAndCanRetryWithoutResettlement) {
    profile().homeFounding.hintsDismissed = false; profile().tutorial = TutorialProgress::FindRaidGate; ++profile().revision;
    finish(RaidResultOutcome::ActiveQuit);
    const auto before = profileStateFingerprint(profile()); const auto settlements = profile().committedSettlements.size();
    ASSERT_TRUE(std::filesystem::create_directory(path / "profile.tmp.json"));
    control(C::Return); stillResult(); EXPECT_FALSE(A::feedback(*app).empty());
    EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_EQ(profile().committedSettlements.size(), settlements);
    const auto loaded = SaveRepository(path).load(content); ASSERT_TRUE(loaded.profile);
    EXPECT_EQ(profileStateFingerprint(*loaded.profile), before);
    ASSERT_TRUE(std::filesystem::remove(path / "profile.tmp.json"));
    auto expected = profile(); expected.homeFounding.hintsDismissed = true;
    expected.tutorial = TutorialProgress::Complete; ++expected.revision;
    control(C::Return); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(profile()), profileStateFingerprint(expected));
    EXPECT_EQ(profile().committedSettlements.size(), settlements);
}

TEST_F(UiRaidResultTest, HeldEnterAfterFocusRecoveryCannotRetryAFailedAcknowledgmentUntilAFreshPress) {
    profile().homeFounding.hintsDismissed = false; profile().tutorial = TutorialProgress::FindRaidGate; ++profile().revision;
    finish(RaidResultOutcome::ActiveQuit);
    const auto before = profileStateFingerprint(profile()); auto expected = profile();
    expected.homeFounding.hintsDismissed = true; expected.tutorial = TutorialProgress::Complete; ++expected.revision;
    ASSERT_TRUE(std::filesystem::create_directory(path / "profile.tmp.json"));
    // Leave the genuine initial key press held after the failed save.
    keyboard(SDL_SCANCODE_RETURN, true); A::frame(*app); stillResult();
    ASSERT_FALSE(A::feedback(*app).empty()); EXPECT_EQ(profileStateFingerprint(profile()), before);
    windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); A::frame(*app); stillResult();
    ASSERT_TRUE(std::filesystem::remove(path / "profile.tmp.json"));
    windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);
    keyboard(SDL_SCANCODE_RETURN, true, true); A::frame(*app); stillResult();
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    keyboard(SDL_SCANCODE_RETURN, false); A::frame(*app); stillResult();
    key(SDL_SCANCODE_RETURN); A::frame(*app);
    EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(profile()), profileStateFingerprint(expected));
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile);
    EXPECT_EQ(profileStateFingerprint(*saved.profile), profileStateFingerprint(expected));
    EXPECT_EQ(profile().committedSettlements.size(), 1U);
}

TEST_F(UiRaidResultTest, EnteringResultClearsCombatOverlaysAndCannotRequestWheelPointerCapture) {
    finish(RaidResultOutcome::ActiveQuit, false);
    ASSERT_FALSE(profile().assets.records().empty());
    const auto before = profileStateFingerprint(profile());
    // Only presentation flags are seeded. The real result remains the one
    // produced by ordinary deployment and settlement above.
    A::seedCombatOverlays(*app, profile().assets.records().begin()->first);
    ASSERT_TRUE(A::combatOverlaysOpen(*app)); ASSERT_TRUE(A::combatOptionsRemain(*app));
    ASSERT_TRUE(A::wheelCaptured(*app)); ASSERT_FALSE(A::hasWindow(*app));
    A::frame(*app); stillResult();
    EXPECT_FALSE(A::combatOverlaysOpen(*app)); EXPECT_FALSE(A::combatOptionsRemain(*app));
    EXPECT_FALSE(A::capture(*app));
    // No SDL window is opened or native pointer capture activated by this
    // regression. Synchronizing the cleaned presentation flags stays released.
    A::syncCapture(*app);
    EXPECT_FALSE(A::wheelCaptured(*app)); EXPECT_FALSE(A::relative(*app)); EXPECT_FALSE(A::cursorHidden(*app));
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiRaidResultTest, AllPagesUseActualHistoricalOccurrencesAndBrowsingPreservesExactAssets) {
    carryRecords(9); finish(RaidResultOutcome::Extracted);
    const auto before = profileStateFingerprint(profile()); const auto p = A::projection(*app);
    ASSERT_EQ(p.returned.size(), 10U); ASSERT_EQ(p.pageCount, 2U); EXPECT_EQ(p.page, 0U);
    control(C::Next); EXPECT_EQ(A::projection(*app).page, 1U);
    control(C::Details); EXPECT_TRUE(A::state(*app).details); draw();
    control(C::Details); EXPECT_FALSE(A::state(*app).details);
    control(C::Previous); EXPECT_EQ(A::projection(*app).page, 0U);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    for (std::size_t i = 0; i < p.returned.size(); ++i)
        EXPECT_EQ(p.returned[i].definitionId, profile().lastRaidResult->returnedItemDefinitionIds[i]);
}

TEST_F(UiRaidResultTest, WheelOverRecordsPagesWithoutClaimingItemsOrReturning) {
    carryRecords(9); finish(RaidResultOutcome::Extracted);
    const auto before = profileStateFingerprint(profile());
    wheel(layoutUiRaidResult().records); EXPECT_EQ(A::projection(*app).page, 1U); stillResult();
    wheel(layoutUiRaidResult().records); EXPECT_EQ(A::projection(*app).page, 1U);
    wheel(layoutUiRaidResult().records, 1); EXPECT_EQ(A::projection(*app).page, 0U);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiRaidResultTest, ClickingARecordHasNoInventoryActionOrProfileMutation) {
    carryRecords(2); finish(RaidResultOutcome::Extracted);
    const auto before = profileStateFingerprint(profile());
    for (std::size_t row = 0; row < 3; ++row) click(center(uiRaidResultRowRect(row)));
    EXPECT_EQ(profileStateFingerprint(profile()), before); stillResult();
}

TEST_F(UiRaidResultTest, PageAndDetailsChangesCancelAnAlreadyPressedReturn) {
    carryRecords(9); finish(RaidResultOutcome::Extracted);
    down(C::Return); control(C::Next); up(C::Return); stillResult();
    down(C::Return); control(C::Details); up(C::Return); stillResult();
}

TEST_F(UiRaidResultTest, AResultContentChangeWithoutARevisionStillInvalidatesItsPress) {
    finish(RaidResultOutcome::ActiveQuit); down(C::Return);
    const auto oldGeneration = A::generation(*app);
    // Result-only compatibility input, never persisted: the view key must
    // inspect result content, rather than trusting a Profile revision alone.
    profile().lastRaidResult->currencyDelta = -1;
    A::frame(*app); up(C::Return); stillResult();
    EXPECT_GT(A::generation(*app), oldGeneration);
}

TEST_F(UiRaidResultTest, FirstResultFrameDoesNotConsumeAPreexistingClickOrEnter) {
    finish(RaidResultOutcome::ActiveQuit, false);
    pointer(center(uiRaidResultControlRect(C::Return)), true);
    pointer(center(uiRaidResultControlRect(C::Return)), false); key(SDL_SCANCODE_RETURN);
    A::frame(*app); stillResult();
}

TEST_F(UiRaidResultTest, OneFrameCannotChainDetailsAndReturnIntoTwoIntents) {
    finish(RaidResultOutcome::ActiveQuit);
    pointer(center(uiRaidResultControlRect(C::Details)), true); pointer(center(uiRaidResultControlRect(C::Details)), false);
    pointer(center(uiRaidResultControlRect(C::Return)), true); pointer(center(uiRaidResultControlRect(C::Return)), false);
    A::frame(*app); stillResult(); EXPECT_TRUE(A::state(*app).details);
}

TEST_F(UiRaidResultTest, IdleFramesReuseTheResultProjectionRatherThanRescanningAssets) {
    carryRecords(9); finish(RaidResultOutcome::Extracted);
    const auto generation = A::generation(*app); const auto before = profileStateFingerprint(profile());
    for (int i = 0; i < 5; ++i) { A::frame(*app); draw(); }
    EXPECT_EQ(A::generation(*app), generation); EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiRaidResultTest, FailedOutcomeNativeReturnPreservesItsCommittedLostRecordAndUsesMedicalArrival) {
    carryRecords(1); finish(RaidResultOutcome::PlayerDead);
    ASSERT_TRUE(profile().lastRaidResult->lostRaidRecordId); const auto lost = *profile().lastRaidResult->lostRaidRecordId;
    ASSERT_TRUE(profile().lostRaidRecords.contains(lost)); const auto before = profileStateFingerprint(profile());
    control(C::Return); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_TRUE(profile().lostRaidRecords.contains(lost)); EXPECT_EQ(profileStateFingerprint(profile()), before);
    BaseWorld expected; expected.configureSite(A::flow(*app).baseWorld().siteDefinitionId()); expected.resetAtMedicalPoint();
    EXPECT_FLOAT_EQ(A::flow(*app).baseWorld().playerPosition().x, expected.playerPosition().x);
    EXPECT_FLOAT_EQ(A::flow(*app).baseWorld().playerPosition().y, expected.playerPosition().y);
}

TEST_F(UiRaidResultTest, SuccessNativeReturnKeepsCarriedSlotsAndUsesRaidGateArrival) {
    carryRecords(2); finish(RaidResultOutcome::Extracted); const auto before = profileStateFingerprint(profile());
    control(C::Return); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    BaseWorld expected; expected.configureSite(A::flow(*app).baseWorld().siteDefinitionId()); expected.resetAtRaidGate();
    EXPECT_FLOAT_EQ(A::flow(*app).baseWorld().playerPosition().x, expected.playerPosition().x);
    EXPECT_FLOAT_EQ(A::flow(*app).baseWorld().playerPosition().y, expected.playerPosition().y);
}

TEST_F(UiRaidResultTest, RestartAfterResultLoadsBaseWithoutReplayingOrResettlingTheResult) {
    finish(RaidResultOutcome::ActiveQuit); const auto before = profileStateFingerprint(profile());
    GameFlow reopened; reopened.configurePersistence(path); ASSERT_TRUE(reopened.continueGame());
    EXPECT_EQ(reopened.state(), GameFlowState::Base);
    EXPECT_EQ(profileStateFingerprint(reopened.gameSession().profile()), before);
}

TEST_F(UiRaidResultTest, HideGuidanceUsesItsOriginalSaveRuleAndDoesNotReturnOrSettleAgain) {
    profile().homeFounding.hintsDismissed = false; profile().tutorial = TutorialProgress::FindRaidGate; ++profile().revision;
    finish(RaidResultOutcome::ActiveQuit);
    auto expected = profile(); expected.homeFounding.hintsDismissed = true; ++expected.revision;
    key(SDL_SCANCODE_H); A::frame(*app); stillResult();
    EXPECT_EQ(profileStateFingerprint(profile()), profileStateFingerprint(expected));
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile);
    EXPECT_EQ(profileStateFingerprint(*saved.profile), profileStateFingerprint(expected));
}

TEST_F(UiRaidResultTest, FailedHideGuidanceSavePreservesThePreferenceAndCanRetry) {
    profile().homeFounding.hintsDismissed = false; profile().tutorial = TutorialProgress::FindRaidGate; ++profile().revision;
    finish(RaidResultOutcome::ActiveQuit); const auto before = profileStateFingerprint(profile());
    ASSERT_TRUE(std::filesystem::create_directory(path / "profile.tmp.json"));
    key(SDL_SCANCODE_H); A::frame(*app); stillResult();
    EXPECT_FALSE(profile().homeFounding.hintsDismissed); EXPECT_EQ(profileStateFingerprint(profile()), before);
    ASSERT_TRUE(std::filesystem::remove(path / "profile.tmp.json"));
    key(SDL_SCANCODE_H); A::frame(*app); stillResult(); EXPECT_TRUE(profile().homeFounding.hintsDismissed);
}

TEST_F(UiRaidResultTest, FeedbackScrollCancelsReturnPressAndKeepsTheFullFailureReason) {
    finish(RaidResultOutcome::ActiveQuit);
    for (int i = 0; i < 24; ++i)
        A::feedback(*app) += "Complete result acknowledgement failure detail with a retry instruction and final constraint.\n";
    draw(); ASSERT_GT(A::state(*app).feedbackMaximum, 0);
    const auto before = profileStateFingerprint(profile()); down(C::Return);
    wheel(uiRaidResultFeedbackRect()); up(C::Return); stillResult();
    EXPECT_GT(A::state(*app).feedbackScroll, 0); EXPECT_EQ(profileStateFingerprint(profile()), before);
    draw(); EXPECT_GT(bright(uiRaidResultFeedbackRect()), 20);
    EXPECT_NE(A::feedback(*app).find("final constraint"), std::string::npos);
}

TEST_F(UiRaidResultTest, RescueThreatAndSignedCurrencyDigitsActuallyPaintAtAllWindowSizesAndLanguages) {
    finish(RaidResultOutcome::ActiveQuit);
    auto r = result(RaidResultOutcome::PlayerDead);
    r.rescuedOrdinaryResidents = 2; r.rescuedInjuredResidents = 1;
    r.baseThreatReducedUnits = 40; r.currencyDelta = -12; r.travelMinutesApplied = 137;
    const auto layout = layoutUiRaidResult();
    const auto advance = [&](std::string_view text) {
#if defined(_WIN32)
        return A::text(*app).measure(text, 14);
#else
        return static_cast<float>(std::count_if(text.begin(), text.end(),
            [](unsigned char byte) { return (byte & 0xc0U) != 0x80U; }) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
#endif
    };
    const auto digits = [&](const std::string &line, std::string_view number, std::size_t row) {
        const auto at = line.rfind(number); ASSERT_NE(at, std::string::npos);
        const float x = layout.summary.x + advance(std::string_view(line).substr(0, at));
        const float y = layout.summary.y + 21.0F * static_cast<float>(row);
        ASSERT_LE(x + advance(number), layout.summary.x + layout.summary.w);
        for (std::size_t i = 0; i < number.size(); ++i) {
            const auto characterX = x + advance(number.substr(0, i));
            const auto width = advance(number.substr(i, 1));
            EXPECT_GT(bright({characterX - 1, y, width + 2, 20}), 3)
                << "missing summary numeric glyph " << number[i] << " at " << characterX << ',' << y;
        }
    };
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        SCOPED_TRACE(std::to_string(size.width) + "-" + uiLanguageCode(language));
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const auto p = project(r, {}, language); ASSERT_EQ(p.summaryLines.size(), 3U);
        A::visualProjection(*app, p); draw();
        digits(p.summaryLines[0], "3", 0); digits(p.summaryLines[1], "-40", 1); digits(p.summaryLines[2], "-12", 2);
        EXPECT_GT(bright(layout.title), 20); EXPECT_GT(bright(layout.status), 20);
        EXPECT_GT(bright(layout.returnButton), 20);
    }
}

TEST_F(UiRaidResultTest, LongRecordNamesActuallyPaintASecondLineAtEveryWindowSizeAndLanguage) {
    carryRecords(1); finish(RaidResultOutcome::Extracted);
    const auto actual = profile().lastRaidResult;
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        auto p = project(actual, {}, language); ASSERT_FALSE(p.returned.empty());
        p.returned[0].name = language == UiLanguage::English
            ? "A very long historical equipment model designation requiring a visible second text line"
            : "一个需要完整显示第二行文字的历史装备型号与名称";
        A::visualProjection(*app, std::move(p)); draw();
        const auto row = uiRaidResultRowRect(0);
        EXPECT_GT(bright({row.x + 94, row.y + 14 + 19.6F, row.w - 108, 18}), 8);
    }
}

TEST_F(UiRaidResultTest, ApprovedWideRifleImageActuallyPaintsAtItsOriginalAspectInsideTheRecordCell) {
    finish(RaidResultOutcome::ActiveQuit);
    auto r = result(); r.returnedItemDefinitionIds = {alpha_content::rifle};
    for (const auto size : kUiWindowSizes) {
        SCOPED_TRACE(std::to_string(size.width));
        detach(); attach(size.width, size.height);
        ASSERT_TRUE(A::rifleTexture(*app).valid()) << "approved rifle image must load for visual verification";
        const auto source = A::rifleTexture(*app).contentBounds();
        ASSERT_GT(source.w, source.h);
        A::visualProjection(*app, project(r)); draw();
        const auto row = uiRaidResultRowRect(0);
        const SDL_FRect image{row.x + 8, row.y + 6, 74, row.h - 12};
        const auto fit = fitInventoryTexture(source.w, source.h,
            {image.x, image.y, image.w, image.h}, ItemOrientation::Degrees0, 1);
        const float scale = surface->w / 1280.0F;
        int left = surface->w, right = -1, top = surface->h, bottom = -1, painted{};
        // The record's image column has only a flat RGB(33,46,47) background
        // outside the texture. Scan the real rendered pixels, including its
        // surrounding gutter, so a stretched or overflowing image fails.
        for (int y = static_cast<int>((row.y + 1) * scale); y < static_cast<int>((row.y + row.h - 1) * scale); ++y)
            for (int x = static_cast<int>((row.x + 1) * scale); x < static_cast<int>((row.x + 90) * scale); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                ASSERT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                const int difference = std::abs(static_cast<int>(red) - 33) +
                    std::abs(static_cast<int>(green) - 46) + std::abs(static_cast<int>(blue) - 47);
                if (difference <= 6) continue;
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y); ++painted;
            }
        ASSERT_GT(painted, 30); ASSERT_GE(right, left); ASSERT_GE(bottom, top);
        const float visibleWidth = (right - left + 1) / scale;
        const float visibleHeight = (bottom - top + 1) / scale;
        EXPECT_GE(left / scale, image.x - 1); EXPECT_LE((right + 1) / scale, image.x + image.w + 1);
        EXPECT_GE(top / scale, image.y - 1); EXPECT_LE((bottom + 1) / scale, image.y + image.h + 1);
        EXPECT_LE(visibleWidth, fit.width + 2); EXPECT_LE(visibleHeight, fit.height + 2);
        EXPECT_GT(visibleWidth, fit.width * 0.75F); EXPECT_GT(visibleHeight, fit.height * 0.5F);
        EXPECT_NEAR(visibleWidth / visibleHeight, source.w / source.h, source.w / source.h * 0.25F + 0.15F);
    }
}

TEST_F(UiRaidResultTest, CriticalStatesActuallyPaintAtEveryPublishedWindowSizeAndLanguage) {
    carryRecords(9); finish(RaidResultOutcome::Extracted);
    const auto committed = profileStateFingerprint(profile()); const auto actual = profile().lastRaidResult;
    const char *output = std::getenv("RAIDLINE_UI_RAID_RESULT_REVIEW_DIR"); std::filesystem::path destination;
    if (output && *output) {
        destination = std::filesystem::path(output);
        ASSERT_TRUE(std::filesystem::equivalent(destination.parent_path(), std::filesystem::current_path()));
        const std::string prefix = "qa-ui-raid-result-v1-"; const auto name = destination.filename().string();
        ASSERT_TRUE(name.starts_with(prefix)); ASSERT_GT(name.size(), prefix.size());
        ASSERT_TRUE(std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(),
            [](char c) { return c >= '0' && c <= '9'; }));
        ASSERT_FALSE(std::filesystem::exists(destination)); ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    constexpr std::array names{"success", "second-page", "details", "empty-success", "death-lost-record",
        "abandoned-no-record", "rescued-with-threat", "wish-ready", "wish-expired", "no-result",
        "test-session", "long-name", "long-feedback"};
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
            SCOPED_TRACE(std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language));
            std::optional<LastRaidResult> r = actual; UiRaidResultState state; state.details = scenario == 2;
            if (scenario == 1) state.page = 1;
            if (scenario == 3) r->returnedItemDefinitionIds.clear();
            if (scenario == 4) { r->outcome = RaidResultOutcome::PlayerDead; r->lostRaidRecordId = "readonly-lost-record"; }
            if (scenario == 5) { r->outcome = RaidResultOutcome::ActiveQuit; r->lostRaidRecordId.reset(); }
            if (scenario == 6) {
                r->outcome = RaidResultOutcome::PlayerDead; r->rescuedOrdinaryResidents = 2;
                r->rescuedInjuredResidents = 1; r->baseThreatReducedUnits = 40; r->currencyDelta = -12;
                r->travelMinutesApplied = 137; state.details = true;
            }
            if (scenario == 7 || scenario == 8) {
                r->wishReturn = BaseWishReturnSummary{{{3, content.basePriorities().front().id},
                    BaseSupplyCategory::Food, 10, "readonly-projection"}, 4, 12, scenario == 8};
                state.details = true;
            }
            if (scenario == 9) r.reset();
            auto p = project(r, state, language, scenario == 10);
            if (scenario == 11) {
                ASSERT_FALSE(p.returned.empty());
                p.returned.front().name = language == UiLanguage::English
                    ? "A deliberately very long historical model designation with a readable second line"
                    : "需要完整显示第二行说明文字的历史装备型号名称";
            }
            A::feedback(*app).clear(); A::visualProjection(*app, std::move(p), state);
            if (scenario == 12) for (int line = 0; line < 24; ++line)
                A::feedback(*app) += "A complete acknowledgement save failure explanation with a retry instruction.\n";
            draw(); EXPECT_EQ(profileStateFingerprint(profile()), committed); stillResult();
            const auto layout = layoutUiRaidResult();
            EXPECT_GT(bright(layout.title), 20); EXPECT_GT(bright(layout.status), 20);
            EXPECT_GT(bright(layout.returnButton), 20); EXPECT_GT(bright(layout.recordHeading), 20);
            if (!destination.empty()) ASSERT_TRUE(IMG_SavePNG(surface,
                (destination / (std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png")).string().c_str()));
        }
    }
}
