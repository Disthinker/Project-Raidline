#include "app.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"
#include "ui_base_deployment_presentation.h"
#include "ui_lost_recovery_presentation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiLostRecoveryTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.lostRecoveryUi_; }
    static auto &projection(App &a) { a.refreshLostRecoveryUi(); return a.lostRecoveryProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.lostRecoveryFeedback_; }
    static auto &legacyMessage(App &a) { return a.uiMessage_; }
    static auto &intelligence(App &a) { return a.selectedRaidIntelligence_; }
    static auto &target(App &a) { return a.selectedRaidSelfRecoveryRecordId_; }
    static auto &warning(App &a) { return a.deploymentWarningArmed_; }
    static auto &lost(App &a) { return a.lostRaidRecordsOpen_; }
    static auto &regional(App &a) { return a.regionalOperationsOpen_; }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
    static auto &deployment(App &a) { a.refreshBaseDeploymentUi(); return a.deploymentProjection_; }
    static const auto &selectedMap(App &a) { return a.selectedRaidMap(); }
    static auto generation(App &a) { return a.lostRecoveryProjectionGeneration_; }
    static void refresh(App &a) { a.refreshLostRecoveryUi(true); }
    static void reset(App &a) { a.resetLostRecoveryUi(); }
    static void draw(App &a) { a.renderLostRaidRecords(); }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static auto &clock(App &a) { return a.uiClockSeconds_; }
    static void pointerPosition(App &a, Vec2 p) { a.pointerWorldPosition_ = p; a.uiPointerInside_ = true; a.windowHasInputFocus_ = true; }
    static void button(App &a, UiLostRecoveryControl control, std::string_view label, bool selected = false, bool danger = false) {
        a.renderFoundationButton(8600 + static_cast<int>(control), uiLostRecoveryControlRect(control), label,
            uiLostRecoveryEnabled(control, a.lostRecoveryUi_, a.lostRecoveryProjection_), danger, selected, {}, 14);
    }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
};

namespace {
using A = UiLostRecoveryTestAccess;
using C = UiLostRecoveryControl;
using T = UiLostRecoveryTab;

Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down; e.button.x = p.x; e.button.y = p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void windowEvent(Uint32 type) { SDL_Event e{}; e.type = type; ASSERT_TRUE(SDL_PushEvent(&e)); }
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = code; e.key.down = true; ASSERT_TRUE(SDL_PushEvent(&e));
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; ASSERT_TRUE(SDL_PushEvent(&e));
}

class UiLostRecoveryTest : public testing::Test {
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
        ASSERT_NO_THROW(saveDirectory.claim("raidline-lost-recovery-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("lost-recovery-ui", content);
        p.currency = 10000; p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::RaidGate));
        attach(1280, 720); A::text(*app).setLanguage(UiLanguage::English);
        open();
    }
    void attach(int width, int height) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, width / 1280.0F);
    }
    void detach() {
        if (app) A::detach(*app);
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); renderer = nullptr; surface = nullptr;
    }
    void TearDown() override {
        detach(); app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void click(Vec2 p) { pointer(p, true); pointer(p, false); A::frame(*app); }
    void control(C c) { click(center(uiLostRecoveryControlRect(c))); }
    void down(C c) { pointer(center(uiLostRecoveryControlRect(c)), true); A::frame(*app); }
    void up(C c) { pointer(center(uiLostRecoveryControlRect(c)), false); A::frame(*app); }
    void open() {
        if (A::lost(*app)) return;
        click(center(uiDeploymentControlRect(UiDeploymentControl::LostRecords)));
        ASSERT_TRUE(A::lost(*app)); A::refresh(*app);
    }
    void wheel(SDL_FRect bounds, int delta = -1) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = static_cast<float>(delta);
        const auto p = center(bounds); e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y;
        ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    std::string record(std::size_t mapIndex = 0, std::uint32_t age = 0, std::size_t childCount = 0) {
        const auto index = profile().lostRaidRecords.size();
        const std::string id = "lost-ui-" + std::string(index < 10 ? "0" : "") + std::to_string(index);
        const auto &m = content.maps().at(mapIndex);
        profile().committedSettlements.insert(id);
        profile().lostRaidRecords.emplace(id, LostRaidRecord{id, "raid-" + id, id, m.id,
            m.operationBriefing.difficulty.empty() ? "normal" : m.operationBriefing.difficulty,
            RaidResultOutcome::PlayerDead, profile().worldClock.elapsedWorldMinutes, age});
        static_cast<void>(profile().assets.create(content.item(alpha_content::pistol), LostRaidAssetLocation{id, EquipmentSlotKind::Sidearm}, 1));
        if (childCount) {
            const auto pack = profile().assets.create(content.item(alpha_content::backpack),
                LostRaidAssetLocation{id, EquipmentSlotKind::Backpack}, 1);
            for (std::size_t i = 0; i < childCount; ++i) {
                const auto origin = findFirstProfileFit(profile(), content, ProfileContainerId::compartment(pack, 0),
                    content.item(alpha_content::ammunition), ItemOrientation::Degrees0);
                EXPECT_TRUE(origin); if (!origin) break;
                static_cast<void>(profile().assets.create(content.item(alpha_content::ammunition),
                    StoredAssetLocation{ProfileContainerId::compartment(pack, 0), *origin}, static_cast<std::uint32_t>(i + 2)));
            }
        }
        ++profile().revision;
        EXPECT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
        A::refresh(*app); return id;
    }
    void select(const std::string &id) {
        control(C::Records);
        const auto records = A::projection(*app).records;
        const auto it = std::find_if(records.begin(), records.end(), [&](const auto &r) { return r.recordId == id; });
        ASSERT_NE(it, records.end());
        const auto index = static_cast<std::size_t>(it - records.begin());
        const auto page = index / uiLostRecoveryPageSize();
        while (A::state(*app).recordPage < page) control(C::Next);
        while (A::state(*app).recordPage > page) control(C::Previous);
        click(center(uiLostRecoveryRecordRect(index % uiLostRecoveryPageSize())));
        ASSERT_EQ(A::projection(*app).selectedRecordId, id);
    }
    void start(const std::string &id) {
        select(id); control(C::Start);
        ASSERT_TRUE(profile().recoveryTask) << A::message(*app);
        ASSERT_EQ(profile().recoveryTask->sourceRecord.recordId, id);
    }
    void ready() {
        ASSERT_TRUE(profile().recoveryTask);
        profile().worldClock.elapsedWorldMinutes = profile().recoveryTask->completionWorldMinute;
        ASSERT_TRUE(applyRecoveryTaskThrough(profile()).becameReady); ++profile().revision; A::refresh(*app);
    }
    void ensureRecoveredRoot() {
        ASSERT_TRUE(profile().recoveryTask);
        const auto id = profile().recoveryTask->taskId;
        const auto it = std::find_if(profile().assets.records().begin(), profile().assets.records().end(), [&](const auto &entry) {
            const auto *location = std::get_if<RecoveryTaskAssetLocation>(&entry.second.location);
            return location && location->taskId == id;
        });
        ASSERT_NE(it, profile().assets.records().end()); profile().recoveryTask->recoveredAssetIds.insert(it->first);
    }
    void blockSave() {
        const auto blocked = path / "blocked-save-root"; std::ofstream(blocked) << "ordinary file";
        A::session(*app).configurePersistence(blocked);
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
        A::draw(*app); SDL_RenderPresent(renderer);
    }
    void hover(Vec2 p) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = p.x; e.motion.y = p.y;
        ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); draw();
        A::clock(*app) += 0.6F; draw();
    }
    std::vector<std::uint32_t> pixels(SDL_FRect r) {
        const float scale = surface->w / 1280.0F; std::vector<std::uint32_t> result;
        for (int y = static_cast<int>(r.y * scale); y < static_cast<int>((r.y + r.h) * scale); ++y)
            for (int x = static_cast<int>(r.x * scale); x < static_cast<int>((r.x + r.w) * scale); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                result.push_back((static_cast<std::uint32_t>(red) << 24) | (static_cast<std::uint32_t>(green) << 16) |
                    (static_cast<std::uint32_t>(blue) << 8) | alpha);
            }
        return result;
    }
    int bright(SDL_FRect r) {
        const float scale = surface->w / 1280.0F; int result{};
        const int x0 = std::max(0, static_cast<int>(r.x * scale));
        const int y0 = std::max(0, static_cast<int>(r.y * scale));
        for (int y = y0; y < std::min(surface->h, static_cast<int>((r.y + r.h) * scale)); ++y)
            for (int x = x0; x < std::min(surface->w, static_cast<int>((r.x + r.w) * scale)); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                if (SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha) && red > 150 && green > 150 && blue > 150) ++result;
            }
        return result;
    }
};
}

TEST_F(UiLostRecoveryTest, EmptyRecordsAndEmptyTaskRemainNavigableWithoutMutatingTheProfile) {
    const auto fp = profileStateFingerprint(profile()); const auto minute = profile().worldClock.elapsedWorldMinutes;
    const auto *world = &A::session(*app).world(); const auto nextId = A::session(*app).nextItemInstanceId();
    EXPECT_TRUE(A::projection(*app).records.empty()); EXPECT_FALSE(A::projection(*app).task);
    draw(); control(C::Task); EXPECT_EQ(A::state(*app).tab, T::Task); draw();
    control(C::Records); EXPECT_EQ(A::state(*app).tab, T::Records);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute);
    EXPECT_EQ(&A::session(*app).world(), world); EXPECT_EQ(A::session(*app).nextItemInstanceId(), nextId);
    EXPECT_FALSE(profile().pendingRaid); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
    control(C::Close); EXPECT_FALSE(A::lost(*app)); EXPECT_EQ(A::flow(*app).activeBaseFacility(), BaseFacilityKind::RaidGate);
}

TEST_F(UiLostRecoveryTest, EveryReturnedRecordIsSelectableAcrossNativePagination) {
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < uiLostRecoveryPageSize() * 2 + 1; ++i) ids.push_back(record());
    const auto query = queryLostRaidRecords(profile(), content);
    ASSERT_EQ(A::projection(*app).records.size(), ids.size()); const auto fp = profileStateFingerprint(profile());
    for (const auto &r : query) { select(r.recordId); EXPECT_EQ(A::projection(*app).selectedRecord->recordId, r.recordId); }
    EXPECT_GT(A::state(*app).recordPage, 0U); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    const auto last = A::state(*app).recordPage; control(C::Next); EXPECT_EQ(A::state(*app).recordPage, last);
}

TEST_F(UiLostRecoveryTest, CompleteReadOnlyAssetListSeparatesEntryCountFromStackQuantities) {
    const auto id = record(0, 0, uiLostRecoveryAssetVisibleRows() + 3); select(id);
    const auto expected = queryLostRaidRecords(profile(), content).front().assets;
    const auto &p = A::projection(*app);
    ASSERT_EQ(p.assets.size(), expected.size()); EXPECT_GT(p.assets.size(), uiLostRecoveryAssetVisibleRows());
    std::uint64_t sum{}; for (const auto &asset : expected) sum += asset.quantity;
    EXPECT_EQ(p.totalQuantity, sum); EXPECT_GT(p.totalQuantity, p.assets.size());
    const auto fp = profileStateFingerprint(profile()); draw();
    EXPECT_GT(A::state(*app).assetMaximum, 0); wheel(uiLostRecoveryAssetsRect());
    EXPECT_GT(A::state(*app).assetScroll, 0); draw();
    for (const auto &asset : A::projection(*app).assets) {
        EXPECT_NE(profile().assets.find(asset.id), nullptr);
        EXPECT_EQ(lostRaidRecordForAsset(profile(), asset.id), id);
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().pendingRaid);
}

TEST_F(UiLostRecoveryTest, LongFailureReasonCanBeScrolledToItsFinalConstraint) {
    select(record());
    for (int i = 0; i < 18; ++i) A::message(*app) += "A complete recovery failure reason with all original constraints.\n";
    A::message(*app) += "FINAL CAPACITY DETAIL 9876";
    const auto fp = profileStateFingerprint(profile()); draw(); ASSERT_GT(A::state(*app).feedbackMaximum, 0);
    for (int i = 0; i < 40; ++i) wheel(uiLostRecoveryFeedbackRect());
    draw(); EXPECT_EQ(A::state(*app).feedbackScroll, A::state(*app).feedbackMaximum);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_GT(bright(uiLostRecoveryFeedbackRect()), 8);
}

TEST_F(UiLostRecoveryTest, LastOpportunityKeepsZeroRemainingAsAValidSelectableRecord) {
    const auto id = record(0, kLostRaidRecordRetainedSettlementCount); select(id);
    const auto &p = A::projection(*app); ASSERT_TRUE(p.selectedRecord);
    EXPECT_EQ(p.selectedRecord->retainedSettlementsRemaining, 0U);
    EXPECT_FALSE(p.selectionExpired); EXPECT_TRUE(p.quote.canCommit);
    EXPECT_TRUE(uiLostRecoveryEnabled(C::Start, A::state(*app), p));
    EXPECT_TRUE(uiLostRecoveryEnabled(C::SelfRecovery, A::state(*app), p));
    const auto fp = profileStateFingerprint(profile()); draw(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, InsufficientFundsShowsPublishedServicePriceAndCannotStart) {
    const auto id = record(); profile().currency = 0; ++profile().revision; select(id);
    const auto &p = A::projection(*app); EXPECT_FALSE(p.quote.canCommit); ASSERT_TRUE(p.serviceFee); ASSERT_TRUE(p.durationMinutes);
    EXPECT_EQ(*p.serviceFee, content.map(p.selectedRecord->mapDefinitionId).recovery.serviceFee);
    EXPECT_EQ(*p.durationMinutes, content.map(p.selectedRecord->mapDefinitionId).recovery.durationMinutes);
    EXPECT_GT(*p.serviceFee, 0U); EXPECT_GT(*p.durationMinutes, 0U);
    const auto fp = profileStateFingerprint(profile()); control(C::Start);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_TRUE(profile().lostRaidRecords.contains(id));
}

TEST_F(UiLostRecoveryTest, NativeDownDoesNotCommissionAndReleasePaysExactlyOnce) {
    const auto id = record(0, 1, 2); select(id); const auto quote = A::projection(*app).quote;
    ASSERT_TRUE(quote.canCommit); const auto fp = profileStateFingerprint(profile()); const auto balance = profile().currency;
    down(C::Start); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
    up(C::Start); ASSERT_TRUE(profile().recoveryTask); EXPECT_EQ(profile().currency, balance - quote.serviceFee);
    EXPECT_FALSE(profile().lostRaidRecords.contains(id));
    EXPECT_EQ(profile().recoveryTask->sourceRecord.recordId, id); const auto paid = profileStateFingerprint(profile());
    up(C::Start); EXPECT_EQ(profileStateFingerprint(profile()), paid);
    EXPECT_TRUE(validateProfileState(profile(), content).valid);
}

TEST_F(UiLostRecoveryTest, LegacyDownOnlyHotspotsCannotCommissionCancelOrCollect) {
    const auto id = record(); select(id); const auto fp = profileStateFingerprint(profile());
    for (const auto p : {Vec2{500, 494}, Vec2{735, 494}, Vec2{1010, 494}, center(uiLostRecoveryControlRect(C::Start))}) A::legacy(*app, p);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
    start(id); const auto paid = profileStateFingerprint(profile());
    A::legacy(*app, center(uiLostRecoveryControlRect(C::Cancel))); EXPECT_EQ(profileStateFingerprint(profile()), paid);
    ready(); control(C::Task); const auto completed = profileStateFingerprint(profile());
    A::legacy(*app, center(uiLostRecoveryControlRect(C::Collect))); EXPECT_EQ(profileStateFingerprint(profile()), completed);
}

TEST_F(UiLostRecoveryTest, TaskOnlyViewRemainsVisibleAndDoesNotRevealFrozenResultBeforeReady) {
    const auto id = record(0, 0, 5); start(id); control(C::Task);
    ASSERT_TRUE(A::projection(*app).task); EXPECT_TRUE(A::projection(*app).records.empty());
    EXPECT_FALSE(A::projection(*app).task->readyForCollection);
    EXPECT_FALSE(A::projection(*app).task->recoveredAssetCount);
    EXPECT_GT(A::projection(*app).task->totalAssetCount, 0U);
    const auto fp = profileStateFingerprint(profile()); draw(); control(C::Collect); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    ready(); ASSERT_TRUE(A::projection(*app).task->recoveredAssetCount);
    EXPECT_EQ(*A::projection(*app).task->recoveredAssetCount, profile().recoveryTask->recoveredAssetIds.size());
    EXPECT_EQ(A::projection(*app).task->remainingWorldMinutes, 0U);
}

TEST_F(UiLostRecoveryTest, CancelRequiresItsOwnConfirmationAndRestoresOriginalRecordWithoutRefund) {
    const auto id = record(0, 2, 4); const auto original = profile().lostRaidRecords.at(id); start(id); control(C::Task);
    const auto paid = profile().currency; const auto owned = profile().assets.records().size();
    const auto fp = profileStateFingerprint(profile()); control(C::Cancel);
    EXPECT_TRUE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    ASSERT_TRUE(A::state(*app).confirmationKey); control(C::ConfirmCancel);
    EXPECT_FALSE(profile().recoveryTask); ASSERT_TRUE(profile().lostRaidRecords.contains(id));
    EXPECT_EQ(profile().lostRaidRecords.at(id), original); EXPECT_EQ(profile().currency, paid);
    EXPECT_EQ(profile().assets.records().size(), owned); EXPECT_EQ(A::projection(*app).selectedRecordId, id);
    EXPECT_TRUE(validateProfileState(profile(), content).valid);
}

TEST_F(UiLostRecoveryTest, KeepingAConfirmedCancellationLeavesEverythingUnchanged) {
    start(record()); control(C::Task); const auto fp = profileStateFingerprint(profile());
    control(C::Cancel); ASSERT_TRUE(A::state(*app).confirmationKey); control(C::KeepTask);
    EXPECT_FALSE(A::state(*app).confirmationKey); EXPECT_TRUE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, CollectionMovesRecoveredRootsToStashOnlyOnce) {
    start(record(0, 0, 4)); ensureRecoveredRoot(); ready(); control(C::Task);
    const auto ids = profile().recoveryTask->recoveredAssetIds; ASSERT_FALSE(ids.empty());
    const auto balance = profile().currency; down(C::Collect); ASSERT_TRUE(profile().recoveryTask); up(C::Collect);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_EQ(profile().currency, balance);
    for (const auto id : ids) { ASSERT_NE(profile().assets.find(id), nullptr); EXPECT_FALSE(recoveryTaskForAsset(profile(), id)); }
    EXPECT_TRUE(validateProfileState(profile(), content).valid);
    const auto fp = profileStateFingerprint(profile()); control(C::Collect); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, FullStashKeepsTheWholeReadyTaskAndAllowsRetryAfterSpaceIsFreed) {
    start(record()); ensureRecoveredRoot(); ready(); control(C::Task);
    const auto taskId = profile().recoveryTask->taskId; std::vector<AssetInstanceId> fillers;
    const auto &filler = content.item(alpha_content::ammunition);
    while (const auto origin = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), filler, ItemOrientation::Degrees0))
        fillers.push_back(profile().assets.create(filler, StoredAssetLocation{ProfileContainerId::stash(), *origin}, 1));
    ++profile().revision; A::refresh(*app); ASSERT_TRUE(validateProfileState(profile(), content).valid);
    const auto fp = profileStateFingerprint(profile()); control(C::Collect);
    ASSERT_TRUE(profile().recoveryTask); EXPECT_EQ(profile().recoveryTask->taskId, taskId);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(A::message(*app).empty());
    for (const auto id : fillers) static_cast<void>(profile().assets.erase(id));
    ++profile().revision; A::refresh(*app); control(C::Collect);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_TRUE(validateProfileState(profile(), content).valid);
}

TEST_F(UiLostRecoveryTest, StartSaveFailureDoesNotPayMoveAssetsOrLoseSelection) {
    const auto id = record(0, 0, 3); select(id); blockSave(); const auto fp = profileStateFingerprint(profile());
    control(C::Start); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
    EXPECT_TRUE(profile().lostRaidRecords.contains(id)); EXPECT_EQ(A::projection(*app).selectedRecordId, id);
    EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiLostRecoveryTest, CancelSaveFailureRetainsTheTaskAndPaidBalance) {
    start(record()); control(C::Task); blockSave(); const auto fp = profileStateFingerprint(profile());
    control(C::Cancel); control(C::ConfirmCancel); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_TRUE(profile().recoveryTask); EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiLostRecoveryTest, CollectionSaveFailureRetainsAllTaskAssetsForRetry) {
    start(record(0, 0, 3)); ensureRecoveredRoot(); ready(); control(C::Task); blockSave();
    const auto fp = profileStateFingerprint(profile()); control(C::Collect);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(profile().recoveryTask); EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiLostRecoveryTest, SelfRecoveryReturnsToItsMapWithoutSpendingOrDeployingAndClearsOldSelections) {
    ASSERT_GT(content.maps().size(), 1U); const auto targetIndex = content.maps().size() - 1;
    const auto id = record(targetIndex); select(id); A::intelligence(*app).set(RaidIntelligenceCategory::Transport, true); A::warning(*app) = true;
    const auto fp = profileStateFingerprint(profile()); control(C::SelfRecovery);
    EXPECT_FALSE(A::lost(*app)); EXPECT_EQ(A::target(*app), id); EXPECT_EQ(A::selectedMap(*app).id, content.maps()[targetIndex].id);
    EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_FALSE(A::warning(*app)); EXPECT_FALSE(profile().pendingRaid);
    const auto &deployment = A::deployment(*app); EXPECT_EQ(deployment.selectedMapIndex, targetIndex);
    EXPECT_EQ(deployment.mapPage, targetIndex / uiDeploymentMapPageSize()); EXPECT_TRUE(deployment.recoveryValid);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
}

TEST_F(UiLostRecoveryTest, ClearingSelfRecoveryTargetKeepsTheRecordAndAllAssets) {
    const auto id = record(); A::target(*app) = id; select(id); const auto fp = profileStateFingerprint(profile());
    control(C::ClearSelfRecovery); EXPECT_FALSE(A::target(*app)); EXPECT_TRUE(profile().lostRaidRecords.contains(id));
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().pendingRaid);
}

TEST_F(UiLostRecoveryTest, OneNpcTaskDoesNotPreventSelfRecoveryOfAnotherLegalRecord) {
    const auto first = record(); const auto second = record(content.maps().size() - 1);
    start(first); const auto taskId = profile().recoveryTask->taskId; select(second);
    EXPECT_FALSE(A::projection(*app).quote.canCommit);
    EXPECT_TRUE(uiLostRecoveryEnabled(C::SelfRecovery, A::state(*app), A::projection(*app)));
    const auto fp = profileStateFingerprint(profile()); control(C::SelfRecovery);
    EXPECT_EQ(A::target(*app), second); ASSERT_TRUE(profile().recoveryTask); EXPECT_EQ(profile().recoveryTask->taskId, taskId);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().pendingRaid);
}

TEST_F(UiLostRecoveryTest, CommissioningCurrentSelfRecoveryTargetClearsOnlyThatTarget) {
    const auto id = record(); A::target(*app) = id; start(id); EXPECT_FALSE(A::target(*app));
    EXPECT_TRUE(profile().recoveryTask); EXPECT_FALSE(profile().lostRaidRecords.contains(id));
}

TEST_F(UiLostRecoveryTest, CurrencyChangeBetweenDownAndUpInvalidatesTheOldCommissionPress) {
    const auto id = record(); select(id); down(C::Start); profile().currency = 0; ++profile().revision;
    const auto fp = profileStateFingerprint(profile()); up(C::Start);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(A::state(*app).press);
}

TEST_F(UiLostRecoveryTest, SameRevisionContextChangeCannotExecuteAStaleCommission) {
    const auto id = record(); select(id); down(C::Start); profile().currency -= 1;
    const auto fp = profileStateFingerprint(profile()); up(C::Start);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, ATaskBecomingReadyCannotTurnAnOldDisabledPressIntoCollection) {
    start(record()); ensureRecoveredRoot(); control(C::Task); down(C::Collect);
    ready(); const auto fp = profileStateFingerprint(profile()); up(C::Collect);
    ASSERT_TRUE(profile().recoveryTask); EXPECT_TRUE(profile().recoveryTask->readyForCollection);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, TaskReadinessChangesInvalidateAnArmedCancellation) {
    start(record()); control(C::Task); control(C::Cancel); ASSERT_TRUE(A::state(*app).confirmationKey);
    ready(); const auto fp = profileStateFingerprint(profile()); control(C::ConfirmCancel);
    EXPECT_TRUE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_FALSE(A::state(*app).confirmationKey);
}

TEST_F(UiLostRecoveryTest, CapacityChangeWithoutARevisionCannotReuseAnOldCollectionPress) {
    start(record()); ensureRecoveredRoot(); ready(); control(C::Task); down(C::Collect);
    const auto &filler = content.item(alpha_content::ammunition);
    while (const auto origin = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), filler, ItemOrientation::Degrees0))
        static_cast<void>(profile().assets.create(filler, StoredAssetLocation{ProfileContainerId::stash(), *origin}, 1));
    const auto fp = profileStateFingerprint(profile()); up(C::Collect);
    EXPECT_TRUE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, NativeCommissionAndCancellationPersistTheSameAssetIdsAndRecord) {
    const auto id = record(0, 2, 5); start(id);
    const auto commissioned = SaveRepository(path).load(content);
    ASSERT_TRUE(commissioned.profile); EXPECT_EQ(profileStateFingerprint(*commissioned.profile), profileStateFingerprint(profile()));
    ASSERT_TRUE(commissioned.profile->recoveryTask);
    EXPECT_EQ(commissioned.profile->recoveryTask->sourceRecord.recordId, id);
    control(C::Task); control(C::Cancel); control(C::ConfirmCancel);
    const auto cancelled = SaveRepository(path).load(content);
    ASSERT_TRUE(cancelled.profile); EXPECT_EQ(profileStateFingerprint(*cancelled.profile), profileStateFingerprint(profile()));
    EXPECT_TRUE(cancelled.profile->lostRaidRecords.contains(id)); EXPECT_FALSE(cancelled.profile->recoveryTask);
}

TEST_F(UiLostRecoveryTest, RemovingTheSelectedRecordCannotRetargetItsOldPressToAnotherRow) {
    const auto first = record(); const auto second = record(); select(first); down(C::Start);
    for (const auto &asset : queryLostRaidRecords(profile(), content)) if (asset.recordId == first)
        for (const auto &entry : asset.assets) static_cast<void>(profile().assets.erase(entry.assetId));
    profile().lostRaidRecords.erase(first); ++profile().revision;
    const auto fp = profileStateFingerprint(profile()); up(C::Start);
    EXPECT_FALSE(profile().recoveryTask); EXPECT_TRUE(profile().lostRaidRecords.contains(second)); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, LanguageChangeBetweenPressAndReleaseCancelsTheOldAction) {
    select(record()); down(C::Start); A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    const auto fp = profileStateFingerprint(profile()); up(C::Start); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
}

TEST_F(UiLostRecoveryTest, FocusLeaveResizeAndModalTransitionsInvalidateOldPointerPresses) {
    const auto id = record(); select(id);
    for (const auto type : {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MOUSE_LEAVE, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED}) {
        SCOPED_TRACE(type); const auto fp = profileStateFingerprint(profile()); down(C::Start);
        windowEvent(type); A::frame(*app); windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app);
        up(C::Start); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
    }
    down(C::Start); A::pause(*app); A::frame(*app); A::resume(*app); const auto fp = profileStateFingerprint(profile()); up(C::Start);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().recoveryTask);
}

TEST_F(UiLostRecoveryTest, LeavingAButtonBeforeReleaseCancelsRatherThanStartingATask) {
    select(record()); const auto fp = profileStateFingerprint(profile()); down(C::Start);
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 4; e.motion.y = 4; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    up(C::Start); EXPECT_FALSE(profile().recoveryTask); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiLostRecoveryTest, ReturnAndEscapeDoNotCommissionOrDeployFromTheSubpage) {
    select(record()); const auto fp = profileStateFingerprint(profile()); key(SDL_SCANCODE_RETURN); A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().pendingRaid); EXPECT_FALSE(profile().recoveryTask);
    down(C::Start); key(SDL_SCANCODE_ESCAPE); A::frame(*app); up(C::Start);
    EXPECT_FALSE(A::lost(*app)); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().pendingRaid);
}

TEST_F(UiLostRecoveryTest, ProjectionCacheRefreshesForWorldTimeButNotForUnchangedAnimationFrames) {
    start(record()); control(C::Task); A::refresh(*app); const auto generation = A::generation(*app);
    for (int i = 0; i < 6; ++i) { A::projection(*app); draw(); }
    EXPECT_EQ(A::generation(*app), generation); const auto remaining = A::projection(*app).task->remainingWorldMinutes;
    ++profile().worldClock.elapsedWorldMinutes; A::projection(*app);
    EXPECT_GT(A::generation(*app), generation); EXPECT_EQ(A::projection(*app).task->remainingWorldMinutes, remaining - 1);
}

TEST_F(UiLostRecoveryTest, MenuTravelDoesNotMoveTheAcceptedWindowAimAnchor) {
    const auto id = record(); A::anchor(*app) = Vec2{1111, 163}; const auto anchor = *A::anchor(*app);
    select(id); control(C::Task); control(C::Records); draw(); control(C::Close);
    ASSERT_TRUE(A::anchor(*app)); EXPECT_EQ(A::anchor(*app)->x, anchor.x); EXPECT_EQ(A::anchor(*app)->y, anchor.y);
}

TEST_F(UiLostRecoveryTest, ServicePricesQuantitiesAndTaskNumbersPaintWithinTheirActualTextBoxes) {
    const auto id = record(0, 1, 3); select(id);
    const auto paintedAdvance = [&](std::string_view text, int size) {
#if defined(_WIN32)
        return A::text(*app).measure(text, size);
#else
        // The portable debug renderer advances a fixed cell per UTF-8 codepoint,
        // including characters it cannot draw. Its advance is not measure().
        static_cast<void>(size);
        return static_cast<float>(std::count_if(text.begin(), text.end(),
            [](unsigned char byte) { return (byte & 0xC0U) != 0x80U; }) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
#endif
    };
    const auto digits = [&](SDL_FRect bounds, std::string prefix, std::string value, int size = 14) {
        const auto x = bounds.x + paintedAdvance(prefix, size);
        EXPECT_GE(x, bounds.x); EXPECT_LE(x + paintedAdvance(value, size), bounds.x + bounds.w);
        EXPECT_GE(bounds.h, size * 1.4F);
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto characterX = x + paintedAdvance(std::string_view(value).substr(0, i), size);
            const auto characterWidth = paintedAdvance(std::string_view(value).substr(i, 1), size);
            EXPECT_GT(bright({characterX - 1, bounds.y, characterWidth + 2, bounds.h}), 4)
                << "missing numeric glyph " << value[i] << " at " << characterX << "," << bounds.y;
        }
    };
    for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        A::text(*app).setLanguage(language); A::refresh(*app); draw();
        const auto &p = A::projection(*app); ASSERT_TRUE(p.serviceFee);
        digits({914, 210, 288, 23}, localizeUiText(language, "NPC SERVICE FEE") + "  ¥", std::to_string(*p.serviceFee));
        digits({914, 280, 288, 23}, localizeUiText(language, "HIGH TENDENCY") + "  ", std::to_string(p.quote.highTendencyAssets));
        const auto row = uiLostRecoveryAssetRect(2); ASSERT_GT(p.assets.size(), 2U);
        digits({row.x + 394, row.y + 10, 80, 20}, "×", std::to_string(p.assets[2].quantity));
        EXPECT_GT(bright({uiLostRecoveryRecordRect(0).x + 14, uiLostRecoveryRecordRect(0).y + 29, 252, 17}), 8)
            << "record second line must not disappear because its text box is shorter than the font line";
    }
    start(id); ensureRecoveredRoot(); ready(); control(C::Task);
    for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        A::text(*app).setLanguage(language); A::refresh(*app); draw(); const auto &task = *A::projection(*app).task;
        digits({78, 280, 500, 26}, localizeUiText(language, "PAID") + "  ¥", std::to_string(task.paidCurrency));
        digits({604, 280, 592, 26}, localizeUiText(language, "TIME REMAINING") + "  ", "0");
        digits({78, 326, 1118, 28}, localizeUiText(language, "RECOVERY RESULT") + "  ", std::to_string(*task.recoveredAssetCount), 16);
    }
}

TEST_F(UiLostRecoveryTest, EveryButtonPaintsItsFullShortLabelAtEveryWindowSizeAndLanguage) {
    const auto id = record(); const auto seed = profile();
    const auto check = [&](C control, std::string_view label, bool selected = false, bool danger = false) {
        const auto rect = uiLostRecoveryControlRect(control);
        const auto localized = localizeUiText(A::text(*app).language(), label);
        EXPECT_LE(A::text(*app).measure(localized, 14) * 1.04F, rect.w - 24)
            << "hovered label cannot fit: " << localized;
        const auto actual = pixels(rect);
        A::button(*app, control, label, selected, danger); SDL_RenderPresent(renderer);
        const auto expected = pixels(rect);
        ASSERT_EQ(actual.size(), expected.size());
        std::size_t differences{};
        for (std::size_t i = 0; i < actual.size(); ++i) differences += actual[i] != expected[i];
        EXPECT_EQ(differences, 0U) << "page must paint its complete short label: " << localized;
    };
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        SCOPED_TRACE(std::to_string(size.width) + "-" + uiLanguageCode(language));
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        profile() = seed; A::reset(*app); A::lost(*app) = true; A::target(*app).reset();
        A::pointerPosition(*app, {1, 1}); A::state(*app).selectedRecordId = id; A::refresh(*app); draw();
        check(C::Records, "LOST RECORDS", true); check(C::Task, "NPC COMMISSION");
        check(C::Previous, "<"); check(C::Next, ">"); check(C::Start, "COMMISSION NPC");
        check(C::SelfRecovery, "SET RECOVERY TARGET"); check(C::Close, "BACK");
        A::target(*app) = id; A::refresh(*app); draw(); check(C::ClearSelfRecovery, "CLEAR RECOVERY TARGET", true);
        ASSERT_TRUE(executeStartRecoveryTask(profile(), content, id, {profile().revision, "button-label-start"}).succeeded);
        A::state(*app).tab = T::Task; A::refresh(*app); draw();
        check(C::Records, "LOST RECORDS"); check(C::Task, "NPC COMMISSION", true);
        check(C::Collect, "COLLECT RECOVERED ASSETS"); check(C::Cancel, "CANCEL | NO REFUND", false, true);
        A::state(*app).confirmationKey = A::projection(*app).contextKey; A::refresh(*app); draw();
        check(C::ConfirmCancel, "CONFIRM CANCEL", false, true); check(C::KeepTask, "KEEP TASK");
    }
}

TEST_F(UiLostRecoveryTest, LongRecordTitleActuallyPaintsItsSecondLineAtEveryWindowSize) {
    const auto id = record(); select(id);
    const std::string title = "A very long source location title containing several readable words and a final district name 9876";
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        SCOPED_TRACE(std::to_string(size.width) + "-" + uiLanguageCode(language));
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language); A::refresh(*app);
        const auto lines = wrapUiText(title, 490, 2, [&](auto value) { return A::text(*app).measure(value, 16); });
        ASSERT_EQ(lines.size(), 2U); A::projection(*app).selectedRecord->mapDisplayName = title;
        draw(); EXPECT_GT(bright({378, 197, 490, 21}), 16)
            << "the title's declared second line must paint, not be suppressed by a one-line-height box";
    }
}

TEST_F(UiLostRecoveryTest, NativeHoverMakesFullSourceAndLocalizedItemNamesReadableWithoutHidingErrors) {
    const auto id = record(0, 0, 1); select(id);
    const std::string source = "A source location whose complete district name and identifying suffix 9876 must remain readable";
    for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        A::text(*app).setLanguage(language); A::refresh(*app); A::message(*app).clear();
        auto &p = A::projection(*app); ASSERT_FALSE(p.records.empty()); p.records.front().mapDisplayName = source;
        hover(center(uiLostRecoveryRecordRect(0))); const auto hoveredSource = pixels(uiLostRecoveryFeedbackRect());
        A::message(*app) = localizeUiText(language, source); draw();
        EXPECT_EQ(hoveredSource, pixels(uiLostRecoveryFeedbackRect())) << "the complete hovered source must reach the readable feedback area";
        A::message(*app).clear();
        const auto assets = A::projection(*app).assets;
        const auto pack = std::find_if(assets.begin(), assets.end(), [](const auto &asset) { return asset.definitionId == alpha_content::backpack; });
        ASSERT_NE(pack, assets.end()); const auto row = static_cast<std::size_t>(pack - assets.begin());
        const auto fullName = localizeUiText(language, content.item(pack->definitionId).displayName);
        EXPECT_NE(fullName, pack->name) << "fixture must distinguish the full item name from its compact list label";
        hover(center(uiLostRecoveryAssetRect(row))); const auto hoveredItem = pixels(uiLostRecoveryFeedbackRect());
        A::message(*app) = fullName; draw(); EXPECT_EQ(hoveredItem, pixels(uiLostRecoveryFeedbackRect()));
        A::message(*app) = "Stash has no room for recovered assets"; draw(); const auto error = pixels(uiLostRecoveryFeedbackRect());
        hover(center(uiLostRecoveryRecordRect(0))); EXPECT_EQ(error, pixels(uiLostRecoveryFeedbackRect()));
        hover(center(uiLostRecoveryAssetRect(row))); EXPECT_EQ(error, pixels(uiLostRecoveryFeedbackRect()));
    }
}

TEST(UiLostRecoveryGeometryTest, LongListsClampAndOnlyVisibleStableRecordIdsCanBeHit) {
    const auto &content = publishedContentRegistry(); auto profile = makeNewAlphaProfile("recovery-geometry", content);
    profile.worldClock.elapsedWorldMinutes = 1000;
    for (std::size_t i = 0; i < uiLostRecoveryPageSize() * 2 + 1; ++i) {
        const auto id = "geometry-record-" + std::to_string(i); profile.committedSettlements.insert(id);
        const auto &map = content.maps().front(); profile.lostRaidRecords.emplace(id, LostRaidRecord{id, "raid-" + id, id, map.id, "normal", RaidResultOutcome::PlayerDead, i, 0});
        static_cast<void>(profile.assets.create(content.item(alpha_content::pistol), LostRaidAssetLocation{id, EquipmentSlotKind::Sidearm}, 1));
    }
    ASSERT_TRUE(validateProfileState(profile, content).valid) << validateProfileState(profile, content).message;
    UiLostRecoveryState state; state.recordPage = 999;
    const auto fp = profileStateFingerprint(profile); const auto p = projectUiLostRecovery(profile, content, UiLanguage::English, std::nullopt, state);
    EXPECT_EQ(p.recordPage, uiLostRecoveryPageCount(p.records.size()) - 1);
    state.recordPage = p.recordPage; const auto hit = hitUiLostRecovery(center(uiLostRecoveryRecordRect(0)), state, p);
    ASSERT_TRUE(hit); EXPECT_EQ(hit->recordId, p.records.at(p.recordPage * uiLostRecoveryPageSize()).recordId);
    EXPECT_FALSE(hitUiLostRecovery(center(uiLostRecoveryRecordRect(1)), state, p));
    state.tab = T::Task; EXPECT_FALSE(hitUiLostRecovery(center(uiLostRecoveryRecordRect(0)), state, p));
    EXPECT_EQ(profileStateFingerprint(profile), fp);
}

TEST_F(UiLostRecoveryTest, OffscreenMatrixCoversLanguagesSizesAndCriticalStates) {
    const auto id = record(0, 0, uiLostRecoveryAssetVisibleRows() + 4); const auto seed = profile();
    const char *output = std::getenv("RAIDLINE_UI_LOST_RECOVERY_REVIEW_DIR"); std::filesystem::path destination;
    if (output && *output) {
        destination = std::filesystem::path(output); const auto root = std::filesystem::current_path();
        ASSERT_TRUE(std::filesystem::equivalent(destination.parent_path(), root));
        const std::string prefix = "qa-ui-lost-recovery-v1-"; const auto name = destination.filename().string();
        ASSERT_TRUE(name.starts_with(prefix)); ASSERT_GT(name.size(), prefix.size());
        ASSERT_TRUE(std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(), [](char c) { return c >= '0' && c <= '9'; }));
        ASSERT_FALSE(std::filesystem::exists(destination)); ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    constexpr std::array names{"empty", "record", "last-opportunity", "no-money", "assets-scrolled", "task-active", "task-ready", "cancel-confirm", "long-name", "long-reason", "record-pages"};
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
            SCOPED_TRACE(std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language));
            profile() = seed; A::reset(*app); A::lost(*app) = true; A::target(*app).reset();
            if (scenario == 0) {
                for (const auto &asset : queryLostRaidRecords(profile(), content).front().assets) static_cast<void>(profile().assets.erase(asset.assetId));
                profile().lostRaidRecords.clear();
            } else A::state(*app).selectedRecordId = id;
            if (scenario == 2) profile().lostRaidRecords.at(id).subsequentRaidSettlementCount = kLostRaidRecordRetainedSettlementCount;
            if (scenario == 3) profile().currency = 0;
            if (scenario == 5 || scenario == 6 || scenario == 7) {
                ASSERT_TRUE(executeStartRecoveryTask(profile(), content, id, {profile().revision, "preview-start-" + std::to_string(scenario)}).succeeded);
                A::state(*app).tab = T::Task;
                if (scenario == 6) ready();
            }
            if (scenario == 10) {
                for (std::size_t i = 0; i < uiLostRecoveryPageSize() + 1; ++i) record();
                A::state(*app).recordPage = 1;
            }
            A::refresh(*app);
            if (scenario == 4) { draw(); A::state(*app).assetScroll = A::state(*app).assetMaximum; }
            if (scenario == 7) control(C::Cancel);
            if (scenario == 8) {
                A::projection(*app).selectedRecord->mapDisplayName = "A deliberately very long source location name that must remain readable without leaking any Raid layout";
                for (auto &asset : A::projection(*app).assets) asset.name = "A long item name with model and calibre that must not overlap quantities";
            }
            if (scenario == 9) for (int line = 0; line < 24; ++line) A::message(*app) += "Complete recovery reason including every relevant constraint and remaining capacity requirement.\n";
            const auto fp = profileStateFingerprint(profile()); draw(); EXPECT_EQ(profileStateFingerprint(profile()), fp);
            EXPECT_FALSE(profile().pendingRaid); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base);
            EXPECT_GT(bright(uiLostRecoveryPanelRect()), 50);
            if (!destination.empty()) ASSERT_TRUE(IMG_SavePNG(surface,
                (destination / (std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png")).string().c_str()));
        }
    }
}
