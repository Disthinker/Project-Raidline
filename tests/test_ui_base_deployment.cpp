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
#include <tuple>
#include <vector>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiBaseDeploymentTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.deploymentUi_; }
    static auto &projection(App &a) { a.refreshBaseDeploymentUi(); return a.deploymentProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.deploymentFeedback_; }
    static auto &legacyMessage(App &a) { return a.uiMessage_; }
    static auto &intelligence(App &a) { return a.selectedRaidIntelligence_; }
    static auto &recovery(App &a) { return a.selectedRaidSelfRecoveryRecordId_; }
    static auto &mapIndex(App &a) { return a.selectedRaidMapIndex_; }
    static auto &loadoutIndex(App &a) { return a.selectedLoadoutArchetypeIndex_; }
    static auto &warning(App &a) { return a.deploymentWarningArmed_; }
    static auto &lost(App &a) { return a.lostRaidRecordsOpen_; }
    static auto &regional(App &a) { return a.regionalOperationsOpen_; }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
    static auto generation(App &a) { return a.deploymentProjectionGeneration_; }
    static const auto &selectedMap(App &a) { return a.selectedRaidMap(); }
    static void refresh(App &a) { a.refreshBaseDeploymentUi(true); }
    static void reset(App &a) { a.resetBaseDeploymentUi(); }
    static void draw(App &a) { a.renderBaseDeployment(); }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void direct(App &a, UiDeploymentEvent e) { a.handleBaseDeploymentEvent(e); }
    static bool confirm(App &a) { return a.handleBaseDeploymentConfirm(); }
    static void newGestureFrame(App &a) { a.deploymentConfirmConsumedThisFrame_ = false; }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static void closeInventory(App &a) { a.closeInventory(); }
    static SDL_FRect oldRegional(App &) { return {700,184,200,34}; } // Removed legacy hotspot.
    static SDL_FRect oldLost(App &a) { return a.raidLostRecordsButton(); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
};

namespace {
using A = UiBaseDeploymentTestAccess;
using C = UiDeploymentControl;
using K = UiDeploymentEventKind;
using T = UiDeploymentTab;

Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down; e.button.x = p.x; e.button.y = p.y;
    ASSERT_TRUE(SDL_PushEvent(&e));
}
void windowEvent(Uint32 type) { SDL_Event e{}; e.type = type; ASSERT_TRUE(SDL_PushEvent(&e)); }
void keyEvent(SDL_Scancode code, bool down, bool repeat = false) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code; e.key.down = down; e.key.repeat = repeat; ASSERT_TRUE(SDL_PushEvent(&e));
}
void key(SDL_Scancode code) { keyEvent(code, true); keyEvent(code, false); }

class UiBaseDeploymentTest : public testing::Test {
protected:
    struct WorldSnapshot {
        const GameplayWorld *identity{};
        GameSessionState sessionState{};
        std::size_t raidNumber{};
        ItemInstanceId sessionNextItem{}, worldNextItem{};
        RaidSessionState raidState{};
        RaidPhase phase{};
        float raidRemaining{}, extractionElapsed{};
        std::string spaceId, spaceName;
        std::array<float, 4> spaceSizeAndPlayerPosition{};
        bool alphaWorld{}, tacticalConfigured{};
        int tacticalColumns{}, tacticalRows{};
        std::size_t blockerCount{}, tacticalResourceCount{}, tacticalObjectiveCount{};
        std::vector<std::tuple<CombatTargetId, float, float, int>> enemies;
        std::vector<std::tuple<ItemInstanceId, ItemId, std::uint32_t, ItemOrientation, float, float>> groundItems;
        bool operator==(const WorldSnapshot &) const = default;
    };
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    WorldSnapshot initialWorld;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }

    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-deployment-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("deployment-ui", content);
        p.currency = 10000; p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        const auto rifle = std::find_if(p.assets.records().begin(), p.assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::rifle; });
        ASSERT_NE(rifle, p.assets.records().end());
        const auto id = rifle->first;
        const auto equip = executeInventory(p, content,
            InventoryEquipCommand{id, EquipmentSlotKind::PrimaryWeapon}, {p.revision, "deployment-fixture-equip"});
        ASSERT_TRUE(equip.succeeded) << equip.message;
        p.assets.findMutable(id)->chamberedRound = MagazineRoundRecord{alpha_content::ammunition, std::nullopt};
        ++p.revision;
        ASSERT_TRUE(validateProfileState(p, content).valid) << validateProfileState(p, content).message;
        ASSERT_TRUE(projectWeaponSupply(p, content, false).anyWeaponCanFire);
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::RaidGate));
        attach(1280, 720); A::text(*app).setLanguage(UiLanguage::English); A::refresh(*app);
        initialWorld = captureWorld();
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
    void control(C c) { click(center(uiDeploymentControlRect(c))); }
    void down(Vec2 p) { pointer(p, true); A::frame(*app); }
    void up(Vec2 p) { pointer(p, false); A::frame(*app); }
    void down(C c) { down(center(uiDeploymentControlRect(c))); }
    void up(C c) { up(center(uiDeploymentControlRect(c))); }
    void wheel(Vec2 p) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = -1;
        e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    void reopen() {
        A::flow(*app).closeBaseFacility(); A::reset(*app); A::closeInventory(*app); A::resume(*app);
        A::lost(*app) = A::regional(*app) = false;
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::RaidGate)); A::refresh(*app);
    }
    void map(std::size_t index) {
        ASSERT_LT(index, content.maps().size()); A::state(*app).mapPage = index / uiDeploymentMapPageSize();
        A::refresh(*app); click(center(uiDeploymentMapRect(index % uiDeploymentMapPageSize())));
        ASSERT_EQ(A::selectedMap(*app).id, content.maps()[index].id);
    }
    void intelligence() { control(C::Intelligence); ASSERT_EQ(A::state(*app).tab, T::Intelligence); }
    void intelAction(RaidIntelligenceCategory category) {
        click(center(uiDeploymentIntelligenceRect(raidIntelligenceCategoryIndex(category), true)));
    }
    void intelRow(RaidIntelligenceCategory category) {
        click(center(uiDeploymentIntelligenceRect(raidIntelligenceCategoryIndex(category))));
    }
    void interiorMap() {
        const auto it = std::find_if(content.maps().begin(), content.maps().end(), [](const auto &m) { return !m.interiors.empty(); });
        ASSERT_NE(it, content.maps().end()); map(static_cast<std::size_t>(it - content.maps().begin())); intelligence();
    }
    void unsafe() {
        const auto weapon = equippedAsset(profile(), EquipmentSlotKind::PrimaryWeapon); ASSERT_TRUE(weapon);
        auto *record = profile().assets.findMutable(*weapon); ASSERT_NE(record, nullptr);
        record->chamberedRound.reset(); ++profile().revision; A::refresh(*app);
        ASSERT_TRUE(A::projection(*app).cannotFire);
    }
    std::string lostRecord(std::size_t mapIndex = 0, bool expiring = true) {
        const std::string id = "deployment-lost-" + std::to_string(profile().lostRaidRecords.size());
        const auto &m = content.maps().at(mapIndex);
        profile().committedSettlements.insert(id);
        profile().lostRaidRecords.emplace(id, LostRaidRecord{id, "raid-" + id, id, m.id,
            m.operationBriefing.difficulty.empty() ? "normal" : m.operationBriefing.difficulty,
            RaidResultOutcome::PlayerDead, profile().worldClock.elapsedWorldMinutes,
            expiring ? kLostRaidRecordRetainedSettlementCount : 0U});
        profile().assets.create(content.item(alpha_content::pistol),
            LostRaidAssetLocation{id, EquipmentSlotKind::Sidearm}, 1);
        ++profile().revision;
        EXPECT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
        A::refresh(*app); return id;
    }
    void blockSave(bool temporary) {
        if (temporary) ASSERT_TRUE(std::filesystem::create_directory(path / "profile.tmp.json"));
        else { const auto blocked = path / "blocked"; std::ofstream(blocked) << "file"; A::session(*app).configurePersistence(blocked); }
    }
    WorldSnapshot captureWorld() {
        const auto &session = A::session(*app); const auto &world = session.world();
        const auto size = world.raidSpaceWorldSize(); const auto player = world.player().position();
        const auto &raid = world.raidSession(); const auto &map = world.tacticalMap();
        WorldSnapshot snapshot;
        snapshot.identity = &world; snapshot.sessionState = session.state(); snapshot.raidNumber = session.raidNumber();
        snapshot.sessionNextItem = session.nextItemInstanceId(); snapshot.worldNextItem = world.nextItemInstanceId();
        snapshot.raidState = raid.state(); snapshot.phase = raid.phase(); snapshot.raidRemaining = raid.raidTimeRemaining();
        snapshot.extractionElapsed = raid.extractionTimeElapsed(); snapshot.spaceId = world.activeRaidSpaceId().value();
        snapshot.spaceName = world.activeRaidSpaceDisplayName(); snapshot.spaceSizeAndPlayerPosition = {size.x, size.y, player.x, player.y};
        snapshot.alphaWorld = world.isAlphaRaidWorld(); snapshot.tacticalConfigured = map.configured();
        snapshot.tacticalColumns = map.columns(); snapshot.tacticalRows = map.rows();
        snapshot.blockerCount = world.ballisticBlockers().size(); snapshot.tacticalResourceCount = map.outdoorResourcePoints().size();
        snapshot.tacticalObjectiveCount = map.objectives().size();
        for (const auto &enemy : world.enemies()) snapshot.enemies.emplace_back(
            enemy.combatTargetId(), enemy.position().x, enemy.position().y, enemy.health());
        for (const auto &ground : world.groundItems()) snapshot.groundItems.emplace_back(
            ground.item().instanceId(), ground.item().definitionId(), ground.item().quantity(), ground.item().orientation(),
            ground.position().x, ground.position().y);
        return snapshot;
    }
    void expectNoRaid() {
        EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base); EXPECT_FALSE(profile().pendingRaid);
        // GameSession constructs a started legacy V0 world even in Base.
        // Browsing must neither replace it with a generated Raid nor alter its
        // identity, lifecycle, ID counters, map or enemy/ground-item snapshot.
        EXPECT_EQ(captureWorld(), initialWorld);
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
        A::draw(*app); SDL_RenderPresent(renderer);
    }
    int bright(SDL_FRect r) {
        const float scale = surface->w / 1280.0F; int result{};
        for (int y = static_cast<int>(r.y * scale); y < (r.y + r.h) * scale; ++y)
            for (int x = static_cast<int>(r.x * scale); x < (r.x + r.w) * scale; ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                if (SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha) && red > 150 && green > 150 && blue > 150) ++result;
            }
        return result;
    }
};

TEST_F(UiBaseDeploymentTest, BrowsingTabsDetailsAndMapsDoesNotMutateOrGenerateRaid) {
    const auto before = profileStateFingerprint(profile()); const auto highWater = profile().assets.nextAssetId();
    for (std::size_t i = 0; i < content.maps().size(); ++i) { map(i); control(C::BriefingDetails); control(C::ReadinessDetails); }
    intelligence(); intelRow(RaidIntelligenceCategory::Enemy); control(C::Briefing); draw();
    EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_EQ(profile().assets.nextAssetId(), highWater); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, PublishedMapOrderAndEveryPageRemainReachable) {
    const auto &p = A::projection(*app); ASSERT_EQ(p.maps.size(), content.maps().size());
    for (std::size_t i = 0; i < p.maps.size(); ++i) EXPECT_EQ(p.maps[i].id, content.maps()[i].id);
    const auto fp = profileStateFingerprint(profile());
    const auto pages = uiDeploymentPageCount(content.maps().size(), uiDeploymentMapPageSize());
    for (std::size_t i = 1; i < pages; ++i) { control(C::MapNext); EXPECT_EQ(A::state(*app).mapPage, i); }
    for (std::size_t i = pages; i > 1; --i) { control(C::MapPrevious); EXPECT_EQ(A::state(*app).mapPage, i - 2); }
    for (std::size_t i = 0; i < content.maps().size(); ++i) map(i);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, AllThreePurchasesRequireReleaseAndBuyOnlyOneBeforeSelection) {
    intelligence();
    for (auto category : {RaidIntelligenceCategory::Transport, RaidIntelligenceCategory::Resource, RaidIntelligenceCategory::Enemy}) {
        SCOPED_TRACE(static_cast<int>(category));
        const auto quote = queryRaidIntelligencePurchase(profile(), content, {A::selectedMap(*app).id, category}); ASSERT_TRUE(quote.canCommit);
        const auto p = center(uiDeploymentIntelligenceRect(raidIntelligenceCategoryIndex(category), true));
        const auto fp = profileStateFingerprint(profile()); const auto money = profile().currency;
        down(p); EXPECT_EQ(profileStateFingerprint(profile()), fp); up(p);
        EXPECT_EQ(profile().currency, money - quote.price); EXPECT_TRUE(A::intelligence(*app).has(category));
        EXPECT_EQ(profile().raidIntelligence.count(A::selectedMap(*app).id, category), 1U);
        const auto bought = profileStateFingerprint(profile()); up(p); EXPECT_EQ(profileStateFingerprint(profile()), bought);
        intelAction(category); EXPECT_FALSE(A::intelligence(*app).has(category));
        intelAction(category); EXPECT_TRUE(A::intelligence(*app).has(category)); EXPECT_EQ(profileStateFingerprint(profile()), bought);
    }
}

TEST_F(UiBaseDeploymentTest, OwnedIntelligenceSelectionAndCancellationDoNotPayOrConsume) {
    for (auto &count : profile().raidIntelligence.counts[A::selectedMap(*app).id]) count = 2;
    ++profile().revision; intelligence(); const auto fp = profileStateFingerprint(profile());
    for (auto c : {RaidIntelligenceCategory::Transport, RaidIntelligenceCategory::Resource, RaidIntelligenceCategory::Enemy}) {
        intelAction(c); EXPECT_TRUE(A::intelligence(*app).has(c)); intelAction(c); EXPECT_FALSE(A::intelligence(*app).has(c));
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, InspectingIntelligenceRowsNeverPurchases) {
    interiorMap(); const auto fp = profileStateFingerprint(profile());
    for (auto c : {RaidIntelligenceCategory::Transport, RaidIntelligenceCategory::Resource, RaidIntelligenceCategory::Enemy}) intelRow(c);
    click(center(uiDeploymentInteriorRect(0))); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_TRUE(profile().raidInteriorIntelligence.knownLayouts.empty());
}

TEST_F(UiBaseDeploymentTest, PermanentInteriorPurchaseIsSavedAndNeverPaidTwice) {
    interiorMap(); const auto id = A::projection(*app).interiors.front().id;
    const auto quote = queryRaidInteriorIntelligencePurchase(profile(), content, {id}); ASSERT_TRUE(quote.canCommit);
    const auto money = profile().currency; click(center(uiDeploymentInteriorRect(0, true)));
    EXPECT_TRUE(profile().raidInteriorIntelligence.knows(id)); EXPECT_EQ(profile().currency, money - quote.price);
    const auto fp = profileStateFingerprint(profile()); click(center(uiDeploymentInteriorRect(0, true)));
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
    const auto loaded = SaveRepository(path).load(content); ASSERT_TRUE(loaded.profile); EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knows(id));
}

TEST_F(UiBaseDeploymentTest, PaidIntelligenceSurvivesClosingWithoutAnAutomaticRefund) {
    intelligence(); intelAction(RaidIntelligenceCategory::Enemy); const auto money = profile().currency;
    control(C::Close); EXPECT_FALSE(A::flow(*app).activeBaseFacility());
    const auto loaded = SaveRepository(path).load(content); ASSERT_TRUE(loaded.profile);
    EXPECT_EQ(loaded.profile->currency, money); EXPECT_EQ(loaded.profile->raidIntelligence.count(content.maps().front().id, RaidIntelligenceCategory::Enemy), 1U);
}

TEST_F(UiBaseDeploymentTest, InsufficientCurrencyKeepsRealPricesAndNeverFakesSelection) {
    interiorMap(); profile().currency = 0; ++profile().revision; A::refresh(*app);
    const auto &p = A::projection(*app); ASSERT_TRUE(p.intelligence.front().price); EXPECT_GT(*p.intelligence.front().price, 0U);
    ASSERT_TRUE(p.interiors.front().price); EXPECT_GT(*p.interiors.front().price, 0U);
    const auto fp = profileStateFingerprint(profile()); intelAction(RaidIntelligenceCategory::Transport); click(center(uiDeploymentInteriorRect(0, true)));
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_TRUE(profile().raidInteriorIntelligence.knownLayouts.empty());
}

TEST_F(UiBaseDeploymentTest, DirectorySaveFailurePreservesMoneyArchiveAndSelection) {
    interiorMap(); blockSave(false); const auto fp = profileStateFingerprint(profile());
    intelAction(RaidIntelligenceCategory::Resource); click(center(uiDeploymentInteriorRect(0, true)));
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiBaseDeploymentTest, TemporaryFileSaveFailurePreservesMoneyArchiveAndSelection) {
    interiorMap(); blockSave(true); const auto fp = profileStateFingerprint(profile());
    intelAction(RaidIntelligenceCategory::Enemy); click(center(uiDeploymentInteriorRect(0, true)));
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiBaseDeploymentTest, MapChangeWithoutRevisionCancelsHeldPurchase) {
    ASSERT_GT(content.maps().size(), 1U); intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point);
    A::mapIndex(*app) = 1; const auto fp = profileStateFingerprint(profile()); up(point);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiBaseDeploymentTest, OwnedCountChangeWithoutRevisionCancelsHeldPurchase) {
    intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point);
    profile().raidIntelligence.counts[A::selectedMap(*app).id][0] = 3;
    const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiBaseDeploymentTest, CurrencyChangeWithoutRevisionCancelsHeldPurchase) {
    intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point); --profile().currency;
    const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiBaseDeploymentTest, LanguageChangeCancelsHeldPurchase) {
    intelligence(); const auto point = center(uiDeploymentIntelligenceRect(1, true)); down(point);
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese); const auto fp = profileStateFingerprint(profile()); up(point);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiBaseDeploymentTest, SelectedIntelligenceChangeWithoutRevisionCancelsHeldAction) {
    intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point);
    A::intelligence(*app).set(RaidIntelligenceCategory::Enemy, true); const auto fp = profileStateFingerprint(profile()); up(point);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(A::intelligence(*app).has(RaidIntelligenceCategory::Transport));
}

TEST_F(UiBaseDeploymentTest, InteriorKnowledgeChangeWithoutRevisionCancelsHeldPurchase) {
    interiorMap(); const auto id = A::projection(*app).interiors.front().id;
    const auto point = center(uiDeploymentInteriorRect(0, true)); down(point); profile().raidInteriorIntelligence.knownLayouts.insert(id);
    const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, DifferentReleaseTargetAndDuplicateReleaseNeverPurchase) {
    intelligence(); const auto transport = center(uiDeploymentIntelligenceRect(0, true));
    const auto fp = profileStateFingerprint(profile()); down(transport); up(center(uiDeploymentIntelligenceRect(1, true))); up(transport);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiBaseDeploymentTest, NativeCancellationBoundariesDiscardHeldPurchase) {
    const auto seed = profile();
    for (int scenario = 0; scenario < 7; ++scenario) {
        SCOPED_TRACE(scenario); profile() = seed; reopen(); intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point);
        if (scenario == 0) { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 8; e.motion.y = 8; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); }
        if (scenario == 1) { windowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE); windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app); }
        if (scenario == 2) { windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); A::frame(*app); A::resume(*app); }
        if (scenario == 3) { windowEvent(SDL_EVENT_WINDOW_RESIZED); A::frame(*app); }
        if (scenario == 4) wheel(point);
        if (scenario == 5) { control(C::Briefing); intelligence(); }
        if (scenario == 6) { control(C::Close); reopen(); intelligence(); }
        const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_TRUE(A::intelligence(*app).empty());
    }
}

TEST_F(UiBaseDeploymentTest, PauseAndTabInventoryCancelHeldPurchase) {
    intelligence(); const auto point = center(uiDeploymentIntelligenceRect(0, true)); down(point);
    A::pause(*app); A::frame(*app); A::resume(*app); const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    down(point); key(SDL_SCANCODE_TAB); A::frame(*app); A::closeInventory(*app); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, ChangingMapClearsOldIntelligenceRecoveryAndRiskConfirmation) {
    ASSERT_GT(content.maps().size(), 1U); const auto id = lostRecord(0, false); A::recovery(*app) = id;
    A::intelligence(*app).set(RaidIntelligenceCategory::Enemy, true); unsafe(); control(C::Deploy);
    ASSERT_TRUE(A::state(*app).confirmationKey); map(1);
    EXPECT_TRUE(A::intelligence(*app).empty()); EXPECT_FALSE(A::recovery(*app)); EXPECT_FALSE(A::state(*app).confirmationKey); EXPECT_FALSE(A::warning(*app));
}

TEST_F(UiBaseDeploymentTest, OrdinaryDeployUsesOneGestureAndDuplicateReleaseCannotDeployAgain) {
    const auto mapId = A::selectedMap(*app).id; const auto before = profile().worldClock.elapsedWorldMinutes;
    down(C::Deploy); expectNoRaid(); up(C::Deploy); ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid); ASSERT_TRUE(profile().pendingRaid);
    EXPECT_EQ(profile().pendingRaid->mapDefinitionId, mapId); EXPECT_GE(profile().worldClock.elapsedWorldMinutes, before);
    const auto fp = profileStateFingerprint(profile()); up(C::Deploy); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, UnsafeDeployRequiresTwoSeparateGesturesAndStillAllowsUnarmedEntry) {
    const auto weapon = equippedAsset(profile(), EquipmentSlotKind::PrimaryWeapon); ASSERT_TRUE(weapon);
    const auto &definition = content.item(profile().assets.find(*weapon)->definitionId);
    const auto fit = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), definition, ItemOrientation::Degrees0); ASSERT_TRUE(fit);
    const auto moved = executeInventory(profile(), content,
        InventoryMoveCommand{*weapon, 0, {ProfileContainerId::stash(), *fit}}, {profile().revision, "deployment-test-unarmed"});
    ASSERT_TRUE(moved.succeeded) << moved.message; ASSERT_FALSE(equippedAsset(profile(), EquipmentSlotKind::PrimaryWeapon));
    A::refresh(*app); control(C::Deploy); expectNoRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    const auto fp = profileStateFingerprint(profile()); up(C::Deploy); EXPECT_EQ(profileStateFingerprint(profile()), fp); expectNoRaid();
    control(C::Deploy); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid);
}

TEST_F(UiBaseDeploymentTest, ExpiringRecordRequiresTwoGesturesWithItsOwnReason) {
    lostRecord(); ASSERT_FALSE(A::projection(*app).cannotFire); ASSERT_TRUE(A::projection(*app).expiryRisk);
    EXPECT_EQ(A::projection(*app).riskReasons.size(), 1U); control(C::Deploy); expectNoRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    EXPECT_NE(A::legacyMessage(*app).find("EXPIRE"), std::string::npos);
    control(C::Deploy); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid);
}

TEST_F(UiBaseDeploymentTest, CombinedFireAndExpiryWarningsRemainDistinct) {
    unsafe(); lostRecord(); const auto &p = A::projection(*app); EXPECT_TRUE(p.cannotFire); EXPECT_TRUE(p.expiryRisk);
    ASSERT_EQ(p.riskReasons.size(), 2U); EXPECT_NE(p.riskReasons[0], p.riskReasons[1]); control(C::Deploy); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, MouseAndEnterInTheSameFrameCannotCompleteTwoConfirmations) {
    unsafe(); const auto point = center(uiDeploymentControlRect(C::Deploy));
    pointer(point, true); pointer(point, false); key(SDL_SCANCODE_RETURN); A::frame(*app); expectNoRaid();
    ASSERT_TRUE(A::state(*app).confirmationKey); key(SDL_SCANCODE_RETURN); A::frame(*app); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid);
}

TEST_F(UiBaseDeploymentTest, HeldAndRepeatedEnterCannotBecomeTheSecondGesture) {
    unsafe(); keyEvent(SDL_SCANCODE_RETURN, true); A::frame(*app); expectNoRaid();
    for (int i = 0; i < 3; ++i) { keyEvent(SDL_SCANCODE_RETURN, true, true); A::frame(*app); expectNoRaid(); }
    keyEvent(SDL_SCANCODE_RETURN, false); A::frame(*app); expectNoRaid();
    key(SDL_SCANCODE_RETURN); A::frame(*app); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid);
}

TEST_F(UiBaseDeploymentTest, RiskChangeWithoutRevisionInvalidatesTheOldConfirmation) {
    const auto id = lostRecord(0, false); unsafe(); control(C::Deploy); ASSERT_TRUE(A::state(*app).confirmationKey);
    const auto revision = profile().revision;
    profile().lostRaidRecords.at(id).subsequentRaidSettlementCount = kLostRaidRecordRetainedSettlementCount;
    EXPECT_EQ(profile().revision, revision); control(C::Deploy); expectNoRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
}

TEST_F(UiBaseDeploymentTest, ClosingAndReopeningCannotReuseAConfirmationOrRelease) {
    unsafe(); control(C::Deploy); ASSERT_TRUE(A::state(*app).confirmationKey); down(C::Deploy); control(C::Close); reopen();
    up(C::Deploy); expectNoRaid(); EXPECT_FALSE(A::state(*app).confirmationKey); control(C::Deploy); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, LoadoutReferenceChangesNeitherAssetsNorChosenMap) {
    const auto fp = profileStateFingerprint(profile()); const auto selected = A::selectedMap(*app).id;
    ASSERT_FALSE(A::projection(*app).loadouts.empty());
    for (std::size_t i = 0; i < A::projection(*app).loadouts.size() + 1; ++i) control(C::LoadoutNext);
    control(C::LoadoutPrevious); EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_EQ(A::selectedMap(*app).id, selected);
}

TEST_F(UiBaseDeploymentTest, StashAmmunitionDoesNotBecomeCarriedAmmoOrMakeADryGunFireable) {
    unsafe(); const auto id = equippedAsset(profile(), EquipmentSlotKind::PrimaryWeapon); ASSERT_TRUE(id);
    const auto &s = A::projection(*app).weaponSupply; ASSERT_FALSE(s.weapons.empty()); const auto before = s.weapons.front().carriedLooseRounds;
    const auto fit = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), content.item(alpha_content::ammunition), ItemOrientation::Degrees0);
    ASSERT_TRUE(fit); profile().assets.create(content.item(alpha_content::ammunition), StoredAssetLocation{ProfileContainerId::stash(), *fit}, 19); ++profile().revision;
    A::refresh(*app); EXPECT_TRUE(A::projection(*app).cannotFire); EXPECT_EQ(A::projection(*app).weaponSupply.weapons.front().carriedLooseRounds, before);
    const auto fp = profileStateFingerprint(profile()); control(C::ReadinessDetails); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, LostSubpageSetsItsOriginalTargetAndSynchronizesTheMapList) {
    ASSERT_GT(content.maps().size(), 1U); const auto targetIndex = content.maps().size() - 1;
    const auto targetPage = targetIndex / uiDeploymentMapPageSize();
    if (content.maps().size() > uiDeploymentMapPageSize()) ASSERT_GT(targetPage, 0U);
    const auto &targetMap = content.maps()[targetIndex]; const auto id = lostRecord(targetIndex, false);
    const auto fp = profileStateFingerprint(profile());
    control(C::LostRecords); ASSERT_TRUE(A::lost(*app));
    click(center(uiLostRecoveryRecordRect(0)));
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::SelfRecovery)));
    ASSERT_FALSE(A::lost(*app)); ASSERT_EQ(A::recovery(*app), id); A::refresh(*app);
    const auto &p = A::projection(*app);
    EXPECT_EQ(A::selectedMap(*app).id, targetMap.id); EXPECT_EQ(p.selectedMapId, targetMap.id);
    EXPECT_EQ(p.selectedMapIndex, targetIndex); EXPECT_EQ(A::state(*app).mapPage, targetPage); EXPECT_EQ(p.mapPage, targetPage);
    ASSERT_LT(targetIndex, p.maps.size()); EXPECT_EQ(p.maps[targetIndex].id, targetMap.id);
    const auto visibleFirst = p.mapPage * uiDeploymentMapPageSize();
    const auto visibleEnd = std::min(visibleFirst + uiDeploymentMapPageSize(), p.maps.size());
    EXPECT_LE(visibleFirst, targetIndex); EXPECT_LT(targetIndex, visibleEnd);
    EXPECT_TRUE(p.recoveryValid); EXPECT_TRUE(p.recoveryMapMatches); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, OriginalSubpageHotspotsAreNotInterceptedByTheNewPage) {
    const auto id = lostRecord(0, false); control(C::LostRecords); ASSERT_TRUE(A::lost(*app));
    click(center(uiLostRecoveryRecordRect(0)));
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::Start)));
    ASSERT_TRUE(profile().recoveryTask); EXPECT_EQ(profile().recoveryTask->sourceRecord.recordId, id);
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::Task)));
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::Cancel)));
    ASSERT_TRUE(profile().recoveryTask);
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::ConfirmCancel))); EXPECT_FALSE(profile().recoveryTask);
    click(center(uiLostRecoveryControlRect(UiLostRecoveryControl::Close))); ASSERT_FALSE(A::lost(*app)); control(C::Regional); ASSERT_TRUE(A::regional(*app));
    A::legacy(*app, center(A::oldRegional(*app))); EXPECT_TRUE(A::regional(*app));
    click(center(uiRegionalOperationsControlRect(UiRegionalOperationsControl::Close))); EXPECT_FALSE(A::regional(*app));
}

TEST_F(UiBaseDeploymentTest, OrdinaryUiRefreshAndResetDoNotClearARegionalConfirmation) {
    control(C::Regional); ASSERT_TRUE(A::regional(*app)); A::warning(*app) = true;
    A::refresh(*app); A::reset(*app); draw(); EXPECT_TRUE(A::warning(*app)); EXPECT_TRUE(A::regional(*app)); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, LegacyOrdinaryPageDownOnlyHotspotsCannotPurchaseOrDeploy) {
    const auto fp = profileStateFingerprint(profile());
    for (const auto p : {Vec2{450, 245}, Vec2{830, 245}, Vec2{500, 350}, Vec2{640, 530}}) A::legacy(*app, p);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, MenuMotionAndClosingPreserveTheWindowAimAnchor) {
    A::anchor(*app) = Vec2{320, 210}; const auto before = A::flow(*app).baseWorld().weaponAimWorldPosition();
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 1100; e.motion.y = 90; e.motion.xrel = 400; e.motion.yrel = 80;
    ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); control(C::Close);
    ASSERT_TRUE(A::anchor(*app)); EXPECT_FLOAT_EQ(A::anchor(*app)->x, 320); EXPECT_FLOAT_EQ(A::anchor(*app)->y, 210);
    const auto after = A::flow(*app).baseWorld().weaponAimWorldPosition(); EXPECT_FLOAT_EQ(after.x, before.x); EXPECT_FLOAT_EQ(after.y, before.y);
}

TEST_F(UiBaseDeploymentTest, CachedRenderingDoesNotReprojectOrMutateEveryFrame) {
    draw(); const auto generation = A::generation(*app); const auto fp = profileStateFingerprint(profile());
    for (int i = 0; i < 12; ++i) { draw(); A::direct(*app, {K::Move, {20, 20}}); }
    EXPECT_EQ(A::generation(*app), generation); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    ++profile().worldClock.elapsedWorldMinutes; draw(); EXPECT_GT(A::generation(*app), generation);
}

TEST_F(UiBaseDeploymentTest, FullFailureReasonRemainsScrollableWithoutMutatingProfile) {
    for (int i = 0; i < 30; ++i) A::message(*app) += "A complete deployment failure reason remains reachable.\n";
    draw(); ASSERT_GT(A::state(*app).feedbackMaximum, 0); const auto fp = profileStateFingerprint(profile());
    wheel(center(uiDeploymentFeedbackRect())); EXPECT_GT(A::state(*app).feedbackScroll, 0); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, ActionBindingIncludesCatalogPriceAndStableCategoryOrInteriorIdentity) {
    const auto fp = profileStateFingerprint(profile());
    auto catalog = content; auto &changed = const_cast<MapDefinition &>(catalog.maps().front());
    UiDeploymentState state; state.tab = T::Intelligence;
    const auto projection = projectUiDeployment(profile(), catalog, UiLanguage::English, changed.id, {}, std::nullopt, 0, state);
    UiDeploymentHit hit{C::IntelligenceAction, 0, changed.id, RaidIntelligenceCategory::Transport};
    const auto old = uiDeploymentActionKey(hit, state, projection); ++changed.operationBriefing.prices[0];
    const auto repriced = projectUiDeployment(profile(), catalog, UiLanguage::English, changed.id, {}, std::nullopt, 0, state);
    EXPECT_NE(uiDeploymentActionKey(hit, state, repriced), old);
    hit.category = RaidIntelligenceCategory::Enemy; hit.index = 2; EXPECT_NE(uiDeploymentActionKey(hit, state, projection), old);
    changed.operationBriefing.prices[0] = 0;
    const auto unavailable = projectUiDeployment(profile(), catalog, UiLanguage::English, changed.id, {}, std::nullopt, 0, state);
    EXPECT_FALSE(unavailable.intelligence.front().price); EXPECT_FALSE(unavailable.intelligence.front().purchase.canCommit);
    interiorMap(); const auto &interior = A::projection(*app).interiors.front();
    hit = {C::InteriorAction, 0, A::selectedMap(*app).id, RaidIntelligenceCategory::Transport, interior.id};
    const auto interiorKey = uiDeploymentActionKey(hit, state, A::projection(*app));
    hit.interiorId = RaidSpaceDefinitionId{}; EXPECT_NE(uiDeploymentActionKey(hit, state, A::projection(*app)), interiorKey);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiBaseDeploymentTest, OwnedNumbersPricesAndSecondLinesActuallyRenderWithinTextBounds) {
    const auto paintedAdvance = [&](std::string_view text, int size) {
#if defined(_WIN32)
        return A::text(*app).measure(text, size);
#else
        // Layout uses estimated widths, while SDL's debug font advances one
        // fixed cell per UTF-8 codepoint, including unsupported characters.
        static_cast<void>(size);
        return static_cast<float>(std::count_if(text.begin(), text.end(),
            [](unsigned char byte) { return (byte & 0xC0U) != 0x80U; }) *
            SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
#endif
    };
#if !defined(_WIN32)
    const auto verifyDebugDigits = [&](Vec2 origin, std::string_view digits, SDL_FRect clip) {
        constexpr float cell = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        EXPECT_GE(origin.x, clip.x);
        EXPECT_GE(origin.y, clip.y);
        EXPECT_LE(origin.x + cell * digits.size(), clip.x + clip.w);
        EXPECT_LE(origin.y + cell, clip.y + clip.h);
        for (std::size_t i = 0; i < digits.size(); ++i)
            EXPECT_GT(bright({origin.x + cell * i, origin.y, cell, cell}), 4)
                << "missing digit " << i << " of " << digits;
    };
#endif
    for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        A::text(*app).setLanguage(language); A::state(*app).tab = T::Intelligence;
        profile().raidIntelligence.counts[A::selectedMap(*app).id][0] = 12; ++profile().revision; A::refresh(*app); draw();
        const auto row = uiDeploymentIntelligenceRect(0);
        const auto ownedPrefix = localizeUiText(language, "INTEL STOCK") + " ";
        const auto numberX = row.x + 12 + paintedAdvance(ownedPrefix, 12);
        EXPECT_GT(bright({numberX, row.y + 33, 28, 17}), 4) << "owned count cannot disappear when its text box is shorter than the font line";
        const auto action = uiDeploymentIntelligenceRect(1, true); const auto &quote = A::projection(*app).intelligence[1]; ASSERT_TRUE(quote.price);
        const auto prefix = localizeUiText(language, "BUY AND SELECT") + " ¥";
        const auto label = prefix + std::to_string(*quote.price);
        const auto priceX = action.x + 12 + (action.w - 24 - A::text(*app).measure(label, 14)) / 2 + paintedAdvance(prefix, 14);
        EXPECT_GT(bright({priceX, action.y + action.h / 2 - 12.6F, 36, 25.2F}), 4);
#if !defined(_WIN32)
        verifyDebugDigits({numberX, row.y + 33}, "12", {row.x + 12, row.y + 33, row.w - 24, 17});
        verifyDebugDigits({priceX, action.y + action.h / 2 - 12.6F}, std::to_string(*quote.price),
            {action.x + 12, action.y + action.h / 2 - 12.6F, action.w - 24, 25.2F});
#endif
        A::state(*app).tab = T::Briefing; A::refresh(*app); draw();
        EXPECT_GT(bright({310, 187, 501, 21}), 12) << "briefing second line";
        EXPECT_GT(bright({872, 209, 327, 21}), 12) << "actual fire readiness second line";
        // renderFoundationButton reserves 12 logical pixels at both sides;
        // its maximum hover zoom is 1.04, while the hit box stays fixed.
        for (const auto &[control, label] : std::array<std::pair<C, const char *>, 7>{{
            {C::Regional, "REGIONS"}, {C::LostRecords, "LOST ITEMS"}, {C::Close, "CLOSE"},
            {C::BriefingDetails, "DETAILS"}, {C::BriefingDetails, "COLLAPSE"},
            {C::ReadinessDetails, "DETAILS"}, {C::ReadinessDetails, "COLLAPSE"}}}) {
            const auto width = A::text(*app).measure(localizeUiText(language, label), 14);
            EXPECT_LE(width * 1.04F + 24.0F, uiDeploymentControlRect(control).w)
                << uiLanguageCode(language) << " " << label;
        }
    }
}

TEST_F(UiBaseDeploymentTest, FocusLossCancelsArmedEnterAndReleaseCannotDeploy) {
    unsafe(); keyEvent(SDL_SCANCODE_RETURN, true); A::frame(*app); expectNoRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    windowEvent(SDL_EVENT_WINDOW_FOCUS_LOST); A::frame(*app);
    windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); A::frame(*app); A::resume(*app);
    keyEvent(SDL_SCANCODE_RETURN, false); A::frame(*app); expectNoRaid(); EXPECT_FALSE(A::state(*app).confirmationKey);
    key(SDL_SCANCODE_RETURN); A::frame(*app); expectNoRaid();
    key(SDL_SCANCODE_RETURN); A::frame(*app); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid);
}

TEST_F(UiBaseDeploymentTest, OffscreenMatrixCoversWindowsLanguagesAndCriticalStates) {
    interiorMap(); const auto seed = profile(); const auto selectedMap = A::mapIndex(*app);
    const char *output = std::getenv("RAIDLINE_UI_DEPLOYMENT_REVIEW_DIR"); std::filesystem::path destination;
    if (output && *output) {
        destination = std::filesystem::absolute(output).lexically_normal();
        const auto root = std::filesystem::path(__FILE__).parent_path().parent_path() / "build" / "windows-debug";
        ASSERT_EQ(std::filesystem::weakly_canonical(destination.parent_path()), std::filesystem::weakly_canonical(root));
        const auto name = destination.filename().string(); const std::string prefix = "qa-ui-base-deployment-v1-";
        ASSERT_TRUE(name.starts_with(prefix)); ASSERT_GT(name.size(), prefix.size());
        ASSERT_TRUE(std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(), [](char c) { return c >= '0' && c <= '9'; }));
        ASSERT_FALSE(std::filesystem::exists(destination)); ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    // Fixed basename contract for contact sheets. Every name is emitted at all three sizes and both languages.
    constexpr std::array names{"briefing", "intelligence", "owned", "empty-interiors", "permanent-known", "no-money",
        "long-name", "long-reason", "map-pages", "details", "fire-unsafe", "expiry", "combined-risk"};
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
            SCOPED_TRACE(std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language));
            profile() = seed; A::mapIndex(*app) = selectedMap; A::intelligence(*app) = {}; A::recovery(*app).reset(); A::reset(*app);
            auto &s = A::state(*app);
            if (scenario >= 1 && scenario <= 5) s.tab = T::Intelligence;
            if (scenario == 2) { profile().raidIntelligence.counts[A::selectedMap(*app).id] = {2, 3, 4}; A::intelligence(*app).set(RaidIntelligenceCategory::Resource, true); }
            if (scenario == 3) {
                const auto it = std::find_if(content.maps().begin(), content.maps().end(), [](const auto &m) { return m.interiors.empty(); });
                ASSERT_NE(it, content.maps().end()); A::mapIndex(*app) = static_cast<std::size_t>(it - content.maps().begin());
            }
            if (scenario == 4) for (const auto &interior : A::selectedMap(*app).interiors) profile().raidInteriorIntelligence.knownLayouts.insert(interior.id);
            if (scenario == 5) profile().currency = 0;
            if (scenario == 8) s.mapPage = uiDeploymentPageCount(content.maps().size(), uiDeploymentMapPageSize()) - 1;
            if (scenario == 9) s.briefingDetails = s.readinessDetails = true;
            if (scenario == 10 || scenario == 12) unsafe();
            if (scenario == 11 || scenario == 12) lostRecord();
            A::refresh(*app);
            if (scenario == 6) A::projection(*app).mapName = "A deliberately very long published location name that must remain readable without leaking any Raid layout";
            if (scenario == 7) for (int line = 0; line < 25; ++line) A::message(*app) += "Complete deployment reason, including all remaining constraints and recovery facts.\n";
            const auto fp = profileStateFingerprint(profile()); draw(); EXPECT_EQ(profileStateFingerprint(profile()), fp); expectNoRaid();
            if (!destination.empty()) ASSERT_TRUE(IMG_SavePNG(surface,
                (destination / (std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png")).string().c_str()));
        }
    }
}

TEST(UiDeploymentGeometryTest, LongListsClampToTheLastPageAndHitOnlyVisibleStableIds) {
    // Extend only an in-memory catalog copy. The published five-map catalog,
    // persistent profiles and client selection remain outside this test.
    auto catalog = publishedContentRegistry();
    const auto selected = std::find_if(catalog.maps().begin(), catalog.maps().end(),
        [](const auto &map) { return !map.interiors.empty(); });
    ASSERT_NE(selected, catalog.maps().end());
    const auto selectedIndex = static_cast<std::size_t>(selected - catalog.maps().begin());
    const auto selectedId = selected->id;
    const auto mapSeed = *selected;
    const auto interiorSeed = selected->interiors.front();
    auto &maps = const_cast<std::vector<MapDefinition> &>(catalog.maps());
    ASSERT_LT(maps.size(), 13U);
    while (maps.size() < 13) {
        auto map = mapSeed;
        map.id = MapDefinitionId{"map.ui_deployment_paged_" + std::to_string(maps.size())};
        map.interiors.clear(); maps.push_back(std::move(map));
    }
    auto &interiors = maps[selectedIndex].interiors;
    ASSERT_LT(interiors.size(), 7U);
    while (interiors.size() < 7) {
        auto interior = interiorSeed;
        interior.id = RaidSpaceDefinitionId{"space.ui_deployment_paged_" + std::to_string(interiors.size())};
        interiors.push_back(std::move(interior));
    }
    auto profile = makeNewAlphaProfile("deployment-pagination", catalog);
    const auto profileBefore = profileStateFingerprint(profile);
    UiDeploymentState state;
    state.tab = T::Intelligence; state.mapPage = state.interiorPage = 999;
    state.selectedInterior = interiors.back().id; state.confirmationKey = "unchanged-client-confirmation";
    state.briefingScroll = 2; state.readinessScroll = 3; state.feedbackScroll = 4;
    const auto stateBefore = uiDeploymentViewKey(state);
    const auto p = projectUiDeployment(profile, catalog, UiLanguage::English, selectedId, {}, std::nullopt, 0, state);
    ASSERT_EQ(p.maps.size(), 13U); ASSERT_EQ(p.interiors.size(), 7U);
    EXPECT_EQ(uiDeploymentPageCount(p.maps.size(), uiDeploymentMapPageSize()), 3U);
    EXPECT_EQ(uiDeploymentPageCount(p.interiors.size(), uiDeploymentInteriorPageSize()), 4U);
    EXPECT_EQ(p.mapPage, 2U); EXPECT_EQ(p.interiorPage, 3U);
    EXPECT_FALSE(uiDeploymentEnabled(C::MapNext, state, p)); EXPECT_TRUE(uiDeploymentEnabled(C::MapPrevious, state, p));
    EXPECT_FALSE(uiDeploymentEnabled(C::InteriorNext, state, p)); EXPECT_TRUE(uiDeploymentEnabled(C::InteriorPrevious, state, p));
    const auto mapHit = hitUiDeployment(center(uiDeploymentMapRect(0)), state, p);
    ASSERT_TRUE(mapHit); EXPECT_EQ(mapHit->control, C::Map); EXPECT_EQ(mapHit->index, 12U); EXPECT_EQ(mapHit->mapId, maps.back().id);
    EXPECT_NE(mapHit->mapId, p.maps.front().id); EXPECT_FALSE(hitUiDeployment(center(uiDeploymentMapRect(1)), state, p));
    for (const bool action : {false, true}) {
        const auto hit = hitUiDeployment(center(uiDeploymentInteriorRect(0, action)), state, p);
        ASSERT_TRUE(hit); EXPECT_EQ(hit->control, action ? C::InteriorAction : C::Interior);
        EXPECT_EQ(hit->index, 6U); EXPECT_EQ(hit->interiorId, interiors.back().id); EXPECT_NE(hit->interiorId, p.interiors.front().id);
        EXPECT_FALSE(hitUiDeployment(center(uiDeploymentInteriorRect(1, action)), state, p));
    }
    auto briefing = state; briefing.tab = T::Briefing;
    for (const bool action : {false, true}) {
        EXPECT_FALSE(hitUiDeployment(center(uiDeploymentIntelligenceRect(0, action)), briefing, p));
        EXPECT_FALSE(hitUiDeployment(center(uiDeploymentInteriorRect(0, action)), briefing, p));
    }
    EXPECT_FALSE(hitUiDeployment(center(uiDeploymentControlRect(C::InteriorPrevious)), briefing, p));
    EXPECT_FALSE(hitUiDeployment(center(uiDeploymentControlRect(C::InteriorNext)), briefing, p));
    auto first = state; first.mapPage = first.interiorPage = 0;
    const auto firstPage = projectUiDeployment(profile, catalog, UiLanguage::English, selectedId, {}, std::nullopt, 0, first);
    EXPECT_EQ(firstPage.mapPage, 0U); EXPECT_EQ(firstPage.interiorPage, 0U);
    const auto firstMapHit = hitUiDeployment(center(uiDeploymentMapRect(0)), first, firstPage);
    ASSERT_TRUE(firstMapHit); EXPECT_EQ(firstMapHit->index, 0U); EXPECT_EQ(firstMapHit->mapId, maps.front().id);
    EXPECT_EQ(uiDeploymentViewKey(state), stateBefore); EXPECT_EQ(state.mapPage, 999U); EXPECT_EQ(state.interiorPage, 999U);
    EXPECT_EQ(state.confirmationKey, "unchanged-client-confirmation"); EXPECT_FALSE(state.press);
    EXPECT_EQ(state.briefingScroll, 2); EXPECT_EQ(state.readinessScroll, 3); EXPECT_EQ(state.feedbackScroll, 4);
    EXPECT_EQ(profileStateFingerprint(profile), profileBefore);
}
} // namespace
