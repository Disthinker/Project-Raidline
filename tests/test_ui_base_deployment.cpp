#include "app.h"
#include "test_temp_directory.h"
#include "ui_app_persistence_wait.h"
#include "alpha_content_ids.h"
#include "base_workforce_domain.h"
#include "ui_base_deployment_presentation.h"
#include "ui_lost_recovery_presentation.h"
#include "raid_tactical_map_presentation.h"
#include "weapon_handling_fixture.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
    static void bindWindow(App &a,SDL_Window *window){a.window_=window;}
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
    static void drawFireSupportOrders(App &a) { a.renderFireSupportOrders(); }
    static bool ordersOpen(const App &a) { return a.fireSupportOrdersOpen_; }
    static bool treatmentOpen(const App &a) { return a.fireSupportTreatmentOpen_; }
    static auto treatmentKit(const App &a) { return a.fireSupportTreatmentKit_; }
    static const auto &fieldTrade(const App &a) { return a.fieldIntelligenceTradeUi_; }
    static void drawFieldTrade(App &a) { a.renderFieldIntelligenceTrade(); }
    static bool tacticalOpen(const App &a) { return a.tacticalMapOpen_; }
    static void drawTactical(App &a) { a.renderRaidTacticalMap(); }
    static const auto &itemMenu(const App &a) { return a.profileContextMenu_; }
    static bool inventoryOpen(const App &a) { return a.inventoryOverlayState_.isOpen(); }
    static void drawInventory(App &a) { a.renderProfileInventory(false, true); }
    static std::vector<std::string> cacheLabels(App &a) {
        a.deferredWorldLabels_.clear();
        a.deferWorldLabels_ = true;
        if (const auto *cache = a.gameSession_.fieldClueCacheLoot()) a.renderFieldClueCache(*cache);
        a.deferWorldLabels_ = false;
        std::vector<std::string> labels;
        for (const auto &label : a.deferredWorldLabels_) labels.push_back(label.text);
        a.deferredWorldLabels_.clear();
        return labels;
    }
    static void drawRaid(App &a) { a.renderRaidScreen(); }
    static std::vector<std::string> enemyLabels(App &a) {
        a.deferredWorldLabels_.clear();
        a.deferWorldLabels_ = true;
        a.renderEnemies();
        a.deferWorldLabels_ = false;
        std::vector<std::string> labels;
        for (const auto &label : a.deferredWorldLabels_) labels.push_back(label.text);
        a.deferredWorldLabels_.clear();
        return labels;
    }
    static std::vector<std::string> drawBarrels(App &a) {
        a.deferredWorldLabels_.clear();
        a.deferWorldLabels_ = true;
        a.renderRaidVolatileBarrels();
        a.deferWorldLabels_ = false;
        std::vector<std::string> labels;
        for (const auto &label : a.deferredWorldLabels_) labels.push_back(label.text);
        a.deferredWorldLabels_.clear();
        return labels;
    }
    static Vec2 barrelScreen(App &a, Vec2 center) {
        return raidWorldToScreen(center,a.raidWorldCameraOffset());
    }
    static void loadPlayerArt(App &a) {
        const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        a.playerTexture_ = loadInventoryTexture(a.renderer_,root/"characters/protagonist_left_minimal_256x320.png");
        a.playerMoveHorizontalTexture_ = loadInventoryTexture(a.renderer_,
            root/"characters/player/default/player_default_move_horizontal_6f_1536x640.png");
        ASSERT_TRUE(a.playerTexture_.valid() && a.playerMoveHorizontalTexture_.valid());
    }
    static void drawEscortMarkers(App &a, const HospitalEscortView &view) {
        const auto &r = kRaidTacticalChart;
        a.renderHospitalEscortMap(view, {r.position.x, r.position.y, r.size.x, r.size.y},
            a.gameSession_.world().playerWorldSize());
    }
    static std::vector<std::string> escortWorldLabels(App &a, const HospitalEscortView &view) {
        a.deferredWorldLabels_.clear();
        a.deferWorldLabels_ = true;
        a.renderHospitalEscortWorld(view);
        a.deferWorldLabels_ = false;
        std::vector<std::string> result;
        for (const auto &label : a.deferredWorldLabels_) result.push_back(label.text);
        a.deferredWorldLabels_.clear();
        return result;
    }
    static bool captured(const App &a) { return a.relativeMouseModeActive_; }
    static bool paused(const App &a) { return a.pauseMenu_.isOpen(); }
    static Vec2 screenAim(const App &a) {
        return raidWorldToScreen(a.gameSession_.world().weaponAimWorldPosition(), a.raidWorldCameraOffset());
    }
    static bool primaryActive(const App &a) { return a.input_.isPrimaryPointerPressed(); }
    static void frame(App &a, float seconds = 0) {
        a.processEvents(); a.update(seconds); a.syncRaidPointerCapture(); a.pendingRelativeAimMotion_={}; a.input_.endFrame();
        ASSERT_TRUE(raidline::test::waitForUiAppPersistence(a.gameSession_,
            [&] { return a.profileUiBusy() || bool(a.pendingInventoryBarrierAction_); },
            [&] { a.processEvents(); a.update(0); a.syncRaidPointerCapture(); a.pendingRelativeAimMotion_={}; a.input_.endFrame(); }));
    }
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
    static void detach(App &a) {
        a.playerTexture_.reset(); a.playerMoveHorizontalTexture_.reset(); a.raidSmokeTerrain_.reset();
        a.indoorInformationLayer_.reset(); a.indoorShadowMask_.reset();
        a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
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
    std::unique_ptr<App> app; SDL_Window *window{};
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    WorldSnapshot initialWorld;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }

    void SetUp() override {
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"));
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS));
        window=SDL_CreateWindow("headless UI capture regression",1280,720,SDL_WINDOW_HIDDEN);
        ASSERT_NE(window,nullptr);
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
        app = std::make_unique<App>();A::bindWindow(*app,window);
        A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::RaidGate));A::frame(*app);
        attach(1280, 720); A::text(*app).setLanguage(UiLanguage::English); A::refresh(*app);
        initialWorld = captureWorld();
    }
    void drawRendezvousArrival(std::string_view state) {
        for (const auto size : kUiWindowSizes) {
            for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
                detach();
                attach(size.width, size.height);
                A::text(*app).setLanguage(language);
                const auto before = profileStateFingerprint(profile());
                ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
                ASSERT_TRUE(SDL_RenderClear(renderer));
                A::draw(*app);
                ASSERT_TRUE(SDL_RenderPresent(renderer));
                EXPECT_EQ(profileStateFingerprint(profile()), before);
                if (const char *output = std::getenv("RAIDLINE_RENDEZVOUS_UI_REVIEW_DIR"); output && *output) {
                    const auto destination = std::filesystem::path(output);
                    ASSERT_TRUE(destination.is_absolute());
                    std::filesystem::create_directories(destination);
                    const auto name = "arrival-" + std::string(state) + "-" +
                        std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                    ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
                }
            }
        }
        detach();
        attach(1280, 720);
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
        detach(); if(app)A::bindWindow(*app,nullptr);SDL_DestroyWindow(window);window=nullptr;app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void click(Vec2 p) { pointer(p, true); pointer(p, false); A::frame(*app); }
    void control(C c) { click(center(uiDeploymentControlRect(c))); }
    void down(Vec2 p) { pointer(p, true); A::frame(*app); }
    void up(Vec2 p) { pointer(p, false); A::frame(*app); }
    void down(C c) { down(center(uiDeploymentControlRect(c))); }
    void up(C c) { up(center(uiDeploymentControlRect(c))); }
    AssetInstanceId addToStorage(ProfileContainerId location, const ItemDefinitionId &item, std::uint32_t quantity) {
        const auto &definition = content.item(item);
        const auto fit = findFirstProfileFit(profile(), content, location, definition, ItemOrientation::Degrees0);
        return fit ? profile().assets.create(definition, StoredAssetLocation{location, *fit}, quantity) : AssetInstanceId{};
    }
    void wheel(Vec2 p) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = -1;
        e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    void reopen() {
        A::flow(*app).closeBaseFacility(); A::reset(*app); A::closeInventory(*app); A::resume(*app);
        A::lost(*app) = A::regional(*app) = false;
        ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::RaidGate));A::frame(*app); A::refresh(*app);
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

TEST_F(UiBaseDeploymentTest, FireSupportDeploymentAndOrdersPreservePointerAndInventory)
{
    const auto eligibleMap = std::find_if(content.maps().begin(), content.maps().end(), [](const auto &m) {
        return m.id == kRaidFireSupportMapId;
    });
    ASSERT_NE(eligibleMap, content.maps().end());
    map(static_cast<std::size_t>(eligibleMap - content.maps().begin()));
    control(C::FireSupportToggle);
    EXPECT_TRUE(A::state(*app).fireSupport);
    EXPECT_EQ(A::projection(*app).fireSupport.failure, RaidFireSupportLoadoutFailure::NoAvailableCombatResident);
    control(C::Deploy);
    expectNoRaid();
    EXPECT_TRUE(A::state(*app).fireSupport);

    auto &residents = profile().basePopulation.professionResidents;
    --residents[baseProfessionIndex(BaseResidentProfession::General)];
    ++residents[baseProfessionIndex(BaseResidentProfession::Combat)];
    const auto stash = ProfileContainerId::stash();
    const auto rifle = addToStorage(stash, kRaidFireSupportRifleId, 1);
    const auto rounds = addToStorage(stash, kRaidFireSupportAmmunitionId, 30);
    const auto spare = addToStorage(stash, ItemDefinitionId{"item.magazine.5_45x39_30"}, 1);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(rounds, 0U);
    ASSERT_NE(spare, 0U);
    profile().assets.findMutable(spare)->magazineRounds.assign(5U, {kRaidFireSupportAmmunitionId, std::nullopt});
    ++profile().revision;
    ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
    A::refresh(*app);
    control(C::FireSupportRifle);
    control(C::FireSupportMagazine);
    ASSERT_EQ(A::state(*app).fireSupport, (RaidFireSupportLoadout{rifle, spare}));

    const auto renderAtWindowSizes = [&](bool orders) {
        for (const auto size : kUiWindowSizes) {
            for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
                detach();
                attach(size.width, size.height);
                A::text(*app).setLanguage(language);
                const auto before = profileStateFingerprint(profile());
                ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
                ASSERT_TRUE(SDL_RenderClear(renderer));
                if (orders) A::drawFireSupportOrders(*app);
                else A::draw(*app);
                ASSERT_TRUE(SDL_RenderPresent(renderer));
                EXPECT_EQ(profileStateFingerprint(profile()), before);
                EXPECT_GT(bright(orders ? SDL_FRect{940, 72, 310, 74} :
                    uiDeploymentControlRect(C::FireSupportMagazine)), 20);
                if (const char *output = std::getenv("RAIDLINE_FIRE_SUPPORT_UI_REVIEW_DIR"); output && *output) {
                    const std::filesystem::path destination(output);
                    ASSERT_TRUE(destination.is_absolute());
                    std::filesystem::create_directories(destination);
                    const auto name = std::string(orders ? "orders-" : "preparation-") +
                        std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png";
                    ASSERT_TRUE(IMG_SavePNG(surface, (destination / name).string().c_str()));
                }
            }
        }
        detach();
        attach(1280, 720);
        A::text(*app).setLanguage(UiLanguage::English);
    };
    renderAtWindowSizes(false);
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    ASSERT_TRUE(profile().pendingRaid && profile().pendingRaid->fireSupport);
    const auto &loan = *profile().pendingRaid->fireSupport;
    EXPECT_EQ(loan.rifle.assetId, rifle);
    ASSERT_TRUE(loan.spareMagazine);
    EXPECT_EQ(loan.spareMagazine->assetId, spare);

    auto &world = A::session(*app).world();
    ASSERT_TRUE(world.fireSupport());
    EXPECT_EQ(world.fireSupport()->movementOrder, FireSupportMovementOrder::Follow);
    EXPECT_EQ(world.fireSupport()->fireOrder, FireSupportFireOrder::HoldFire);
    ASSERT_TRUE(A::captured(*app));
    const auto before = world.weaponAimWorldPosition();
    const auto screen = A::screenAim(*app);
    key(SDL_SCANCODE_F2);
    A::frame(*app);
    ASSERT_TRUE(A::ordersOpen(*app));
    ASSERT_FALSE(A::captured(*app));
    Vec2 cursor{};
    SDL_GetMouseState(&cursor.x, &cursor.y);
    EXPECT_NEAR(cursor.x, std::clamp(screen.x, 0.0F, 1279.0F), 0.01F);
    EXPECT_NEAR(cursor.y, std::clamp(screen.y, 0.0F, 719.0F), 0.01F);
    renderAtWindowSizes(true);

    click({1176, 88});
    key(SDL_SCANCODE_4);
    A::frame(*app);
    ASSERT_TRUE(world.fireSupport());
    EXPECT_EQ(world.fireSupport()->movementOrder, FireSupportMovementOrder::Hold);
    EXPECT_EQ(world.fireSupport()->fireOrder, FireSupportFireOrder::FreeFire);
    down({1176, 126});
    key(SDL_SCANCODE_ESCAPE);
    A::frame(*app);
    EXPECT_FALSE(A::primaryActive(*app));
    up({1176, 126});
    EXPECT_FALSE(A::ordersOpen(*app));
    EXPECT_FALSE(A::paused(*app));
    EXPECT_TRUE(A::captured(*app));
    const auto after = world.weaponAimWorldPosition();
    EXPECT_NEAR(after.x, before.x, 0.01F);
    EXPECT_NEAR(after.y, before.y, 0.01F);
}

TEST_F(UiBaseDeploymentTest, TeammateTreatmentSelectsOneCarriedKitAndKeepsOrdersIndependent)
{
    const auto ordinary = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &m) { return m.id == kRaidFireSupportMapId; });
    ASSERT_NE(ordinary, content.maps().end());
    auto &residents = profile().basePopulation.professionResidents;
    --residents[baseProfessionIndex(BaseResidentProfession::General)];
    ++residents[baseProfessionIndex(BaseResidentProfession::Combat)];
    const auto stash = ProfileContainerId::stash();
    ASSERT_NE(addToStorage(stash, kRaidFireSupportRifleId, 1), 0U);
    ASSERT_NE(addToStorage(stash, kRaidFireSupportAmmunitionId, 30), 0U);
    const auto backpack = std::find_if(profile().assets.records().begin(), profile().assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    ASSERT_NE(backpack, profile().assets.records().end());
    const auto packId = backpack->first;
    ASSERT_TRUE(executeInventory(profile(), content, InventoryEquipCommand{packId, EquipmentSlotKind::Backpack},
        {profile().revision, "support-treatment-equip-backpack"}).succeeded);
    const auto grid = ProfileContainerId::compartment(packId, 0);
    const auto first = addToStorage(grid, alpha_content::medkit, 1);
    const auto second = addToStorage(grid, alpha_content::medkit, 1);
    ASSERT_NE(first, 0U);
    ASSERT_NE(second, 0U);
    profile().assets.findMutable(second)->remainingCharges = 2;
    ++profile().revision;
    ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
    map(static_cast<std::size_t>(ordinary - content.maps().begin()));
    control(C::FireSupportToggle);
    control(C::FireSupportRifle);
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    auto &session = A::session(*app);
    auto &world = session.world();
    ASSERT_TRUE(world.fireSupport());
    // This case measures an uninterrupted five-second treatment from the
    // orders panel. Other ordinary-raid cases cover hostile contact.
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    A::frame(*app, 1.0F / 60.0F);
    ASSERT_EQ(session.fireSupportTreatmentKits(), (std::vector<AssetInstanceId>{first, second}));
    key(SDL_SCANCODE_F2);
    key(SDL_SCANCODE_5);
    A::frame(*app);
    ASSERT_TRUE(A::treatmentOpen(*app));
    ASSERT_EQ(A::treatmentKit(*app), first);
    key(SDL_SCANCODE_RETURN);
    A::frame(*app);
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_TRUE(A::treatmentOpen(*app));
    EXPECT_EQ(session.queryFireSupportTreatment(first).message, "teammate is at full health");

    auto &companion = WeaponHandlingTestAccess::support(world);
    companion.applyContact(PlayerDamageObservation{40}, 0.0F);
    companion.setPosition(world.player().position());
    A::frame(*app);
    const int injured = world.fireSupportHealth().value();
    ASSERT_LT(injured, kFireSupportMaxHealth);
    ASSERT_TRUE(session.queryFireSupportTreatment(second).canCommit) << session.queryFireSupportTreatment(second).message;
    key(SDL_SCANCODE_2);
    key(SDL_SCANCODE_4);
    key(SDL_SCANCODE_RIGHT);
    A::frame(*app);
    EXPECT_EQ(A::treatmentKit(*app), second);
    EXPECT_EQ(world.fireSupport()->movementOrder, FireSupportMovementOrder::Follow);
    EXPECT_EQ(world.fireSupport()->fireOrder, FireSupportFireOrder::HoldFire);
    key(SDL_SCANCODE_ESCAPE);
    A::frame(*app);
    EXPECT_TRUE(A::ordersOpen(*app));
    EXPECT_FALSE(A::treatmentOpen(*app));
    EXPECT_EQ(world.fireSupportHealth(), injured);
    EXPECT_EQ(profile().assets.find(second)->remainingCharges, 2U);

    click({1090, 168});
    click({1176, 108});
    ASSERT_EQ(A::treatmentKit(*app), second);
    down({1176, 188});
    companion.applyContact(PlayerDamageObservation{2}, 0.0F);
    up({1176, 188});
    EXPECT_TRUE(A::treatmentOpen(*app));
    EXPECT_FALSE(session.playerActionState().active());
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer));
    A::drawFireSupportOrders(*app);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_GT(bright({938, 58, 314, 28}), 20);
    EXPECT_GT(bright({1100, 173, 152, 34}), 20);
    if (const char *output = std::getenv("RAIDLINE_TEAMMATE_TREATMENT_UI_DIR"); output && *output) {
        const std::filesystem::path destination(output);
        ASSERT_TRUE(destination.is_absolute());
        std::filesystem::create_directories(destination);
        ASSERT_TRUE(IMG_SavePNG(surface, (destination / "treatment-1280-zh.png").string().c_str()));
    }

    const int beforeTreatment = world.fireSupportHealth().value();
    click({1176, 188});
    ASSERT_FALSE(A::ordersOpen(*app));
    ASSERT_TRUE(session.playerActionState().active());
    const auto *treatment = std::get_if<MedicalRaidAction>(&*session.playerActionState().active());
    ASSERT_NE(treatment, nullptr);
    EXPECT_TRUE(treatment->fireSupportSpace);
    EXPECT_EQ(treatment->medicalAssetId, second);
    EXPECT_EQ(profile().assets.find(second)->remainingCharges, 2U);
    int ticks = 0;
    for (; ticks < 360 && session.playerActionState().active(); ++ticks)
        A::frame(*app, 1.0F / 60.0F);
    SCOPED_TRACE(testing::Message() << "ticks=" << ticks << " session=" << session.persistenceMessage()
        << " panel=" << A::legacyMessage(*app) << " canTreat=" << session.queryFireSupportTreatment(second).message
        << " player=" << profile().currentHealth << " near=" << world.canInteractWithFireSupport());
    EXPECT_FALSE(session.playerActionState().active());
    EXPECT_EQ(world.fireSupportHealth(), std::min(kFireSupportMaxHealth, beforeTreatment + 30));
    EXPECT_EQ(profile().assets.find(first)->remainingCharges, 3U);
    EXPECT_EQ(profile().assets.find(second)->remainingCharges, 1U);
    EXPECT_EQ(world.fireSupport()->movementOrder, FireSupportMovementOrder::Follow);
    EXPECT_EQ(world.fireSupport()->fireOrder, FireSupportFireOrder::HoldFire);
}

TEST_F(UiBaseDeploymentTest, RiversideCacheDocumentContextAndMapPreserveFogAndSearchThroughF)
{
    const auto backpack = std::find_if(profile().assets.records().begin(), profile().assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    ASSERT_NE(backpack, profile().assets.records().end());
    ASSERT_TRUE(executeInventory(profile(), content, InventoryEquipCommand{backpack->first, EquipmentSlotKind::Backpack},
        {profile().revision, "cache-ui-equip"}).succeeded);
    const auto river = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &entry) { return entry.id == kFieldIntelligenceSourceMapId; });
    ASSERT_NE(river, content.maps().end());
    map(static_cast<std::size_t>(river - content.maps().begin()));
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    auto &session = A::session(*app);
    auto &world = session.world();
    ASSERT_NE(session.fieldClueCacheLoot(), nullptr);
    const Vec2 cachePosition = session.fieldClueCacheLoot()->position;
    const auto rewardId = session.fieldClueCacheLoot()->assetId;
    const auto &loot = profile().pendingRaid->loot;
    const auto file = std::find_if(loot.begin(), loot.end(), [](const auto &entry) {
        return entry.definitionId == kFieldIntelligenceDocumentId;
    });
    ASSERT_NE(file, loot.end());
    const auto fileId = file->assetId;
    const auto filePosition = file->position;
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    auto &player = const_cast<Player &>(world.player());
    const float half = player.size() * .5F;
    ASSERT_TRUE(player.setPosition({cachePosition.x - half, cachePosition.y - half}));
    A::frame(*app);
    EXPECT_EQ(A::cacheLabels(*app), (std::vector<std::string>{"F: Search cache"}));
    EXPECT_FALSE(profile().pendingRaid->fieldClueCache->decodedDocumentId);
    ASSERT_TRUE(player.setPosition({filePosition.x - half, filePosition.y - half}));
    A::frame(*app);
    key(SDL_SCANCODE_F); A::frame(*app);
    ASSERT_TRUE(assetIsCarried(profile(), fileId)) << session.persistenceMessage();
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    const auto drawMap = [&] {
        SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
        A::drawTactical(*app); SDL_RenderPresent(renderer);
    };
    const auto marker = [&] {
        const auto size = world.tacticalMap().worldSize();
        const auto &r = kRaidTacticalChart;
        const int x = static_cast<int>(r.position.x + r.size.x * cachePosition.x / size.x);
        const int y = static_cast<int>(r.position.y + r.size.y * cachePosition.y / size.y);
        int count{};
        for (int py = y - 7; py <= y + 7; ++py)
            for (int px = x - 7; px <= x + 7; ++px) {
                Uint8 red{}, green{}, blue{}, alpha{};
                if (SDL_ReadSurfacePixel(surface, px, py, &red, &green, &blue, &alpha) &&
                    red == 242 && green == 201 && blue == 126) ++count;
            }
        return count;
    };
    const auto knownCells = [&] {
        const auto &chart = world.tacticalMap();
        std::vector<bool> cells;
        for (int row = 0; row < chart.rows(); ++row)
            for (int col = 0; col < chart.columns(); ++col) cells.push_back(chart.cellRevealed(col, row));
        return cells;
    };
    const auto capture = [&](const char *filename) {
        if (const char *output = std::getenv("RAIDLINE_FIELD_CACHE_UI_DIR"); output && *output) {
            const std::filesystem::path dir(output);
            ASSERT_TRUE(dir.is_absolute());
            std::filesystem::create_directories(dir);
            ASSERT_TRUE(IMG_SavePNG(surface, (dir / filename).string().c_str()));
        }
    };
    key(SDL_SCANCODE_M); A::frame(*app);
    ASSERT_TRUE(A::tacticalOpen(*app));
    drawMap();
    EXPECT_EQ(marker(), 0);
    const auto fog = knownCells();
    key(SDL_SCANCODE_M); A::frame(*app);
    key(SDL_SCANCODE_TAB); A::frame(*app);
    ASSERT_TRUE(A::inventoryOpen(*app));
    const auto &stored = std::get<StoredAssetLocation>(profile().assets.find(fileId)->location);
    const auto grids = layoutProfileCarriedGrids(profile(), content, 0);
    const auto grid = std::find_if(grids.begin(), grids.end(), [&](const auto &g) { return g.container == stored.container; });
    ASSERT_NE(grid, grids.end());
    const Vec2 item{grid->bounds.x + (stored.origin.x + .5F)*kProfileItemCell,
        grid->bounds.y + (stored.origin.y + .5F)*kProfileItemCell};
    SDL_Event right{};
    right.type = SDL_EVENT_MOUSE_BUTTON_DOWN; right.button.button = SDL_BUTTON_RIGHT;
    right.button.down = true; right.button.x = item.x; right.button.y = item.y; ASSERT_TRUE(SDL_PushEvent(&right));
    right.type = SDL_EVENT_MOUSE_BUTTON_UP; right.button.down = false; ASSERT_TRUE(SDL_PushEvent(&right));
    A::frame(*app);
    const auto &menu = A::itemMenu(*app);
    ASSERT_TRUE(menu);
    const auto action = std::find_if(menu->actions.begin(), menu->actions.end(), [](const auto &row) {
        return row.id == UiItemAction::ReadCacheClue;
    });
    ASSERT_NE(action, menu->actions.end());
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, action->label), "解读密藏线索");
    const auto hit = center(menu->layout.rows[static_cast<std::size_t>(action - menu->actions.begin())].bounds);
    SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
    A::drawInventory(*app); SDL_RenderPresent(renderer); capture("field-cache-file-1280-zh.png");
    down(hit);
    EXPECT_TRUE(assetIsCarried(profile(), fileId));
    up(hit);
    ASSERT_EQ(profile().pendingRaid->fieldClueCache->decodedDocumentId, fileId) << A::legacyMessage(*app);
    EXPECT_FALSE(profile().assets.find(fileId));
    EXPECT_EQ(profile().pendingRaid->fieldIntelligenceTrade->balance, 0U);
    key(SDL_SCANCODE_TAB); A::frame(*app);
    key(SDL_SCANCODE_M); A::frame(*app);
    ASSERT_TRUE(A::tacticalOpen(*app));
    drawMap();
    EXPECT_GT(marker(), 12);
    EXPECT_EQ(knownCells(), fog);
    capture("field-cache-map-1280-zh.png");
    key(SDL_SCANCODE_M); A::frame(*app);
    ASSERT_TRUE(player.setPosition({cachePosition.x - half, cachePosition.y - half}));
    A::frame(*app);
    ASSERT_TRUE(session.fieldClueCacheInRange());
    key(SDL_SCANCODE_F); A::frame(*app);
    ASSERT_TRUE(session.fieldClueCacheLoot()->collected) << A::legacyMessage(*app);
    EXPECT_TRUE(assetIsCarried(profile(), rewardId));
    EXPECT_FALSE(session.fieldClueCacheInRange());
    EXPECT_EQ(A::cacheLabels(*app), (std::vector<std::string>{"Empty field cache"}));
}

TEST_F(UiBaseDeploymentTest, RiversideRetreatCanBeInterceptedOrLostOutOfSight)
{
    const auto river = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &entry) { return entry.id == kFieldIntelligenceSourceMapId; });
    ASSERT_NE(river, content.maps().end());
    map(static_cast<std::size_t>(river - content.maps().begin()));
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    auto &session = A::session(*app);
    auto &world = session.world();
    ASSERT_EQ(world.armedScavengers().size(), 2U);
    ASSERT_EQ(profile().pendingRaid->armedScavengers.size(), 2U);
    const auto first = world.armedScavengers()[0].id, second = world.armedScavengers()[1].id;
    const auto firstLoadout = profile().pendingRaid->armedScavengers[0];
    const auto secondLoadout = profile().pendingRaid->armedScavengers[1];
    ASSERT_TRUE(firstLoadout.retreatPoint && secondLoadout.retreatPoint);
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        if (!enemy.isArmedScavenger()) static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    A::frame(*app, .016F);
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    auto &player = const_cast<Player &>(world.player());
    const auto actor = [&](CombatTargetId id) -> Enemy * {
        auto &enemies = WeaponHandlingTestAccess::actors(world);
        const auto it = std::find_if(enemies.begin(), enemies.end(), [&](const auto &e){return e.combatTargetId() == id;});
        return it == enemies.end() ? nullptr : &*it;
    };
    const auto bodyCenter = [&](CombatTargetId id) {
        const auto bounds = actor(id)->bounds();
        return Vec2{bounds.position.x + bounds.size.x * .5F, bounds.position.y + bounds.size.y * .5F};
    };
    const auto placeInClearSight = [&](CombatTargetId id) {
        const auto p = bodyCenter(id);
        const std::array<Vec2,8> offsets{{{-80,0},{80,0},{0,-80},{0,80},{-56,-56},{56,-56},{-56,56},{56,56}}};
        for (const auto d : offsets) {
            if (!player.setPosition({p.x + d.x - player.size()*.5F, p.y + d.y - player.size()*.5F})) continue;
            GameplayInput aim; aim.aimWorldPosition = p; world.update(aim, 0);
            const auto probe = world.previewSmoke(p, SmokeUseDefinition{});
            if (world.raidEnemyObservable(id) && probe && std::hypot(probe->point.x-p.x, probe->point.y-p.y)<1.0F)
                return true;
        }
        return false;
    };
    const auto retreatLabel = [&] {
        const auto labels = A::enemyLabels(*app);
        return std::find(labels.begin(),labels.end(),"FALLING BACK") != labels.end();
    };
    ASSERT_NE(actor(first), nullptr);
    ASSERT_TRUE(placeInClearSight(first));
    ASSERT_FALSE(actor(first)->takeDamage(actor(first)->health() - 1));
    const auto firstCenter = bodyCenter(first);
    A::anchor(*app) = A::barrelScreen(*app,firstCenter);
    A::frame(*app, .016F);
    ASSERT_TRUE(world.armedScavengers()[0].retreating);
    ASSERT_TRUE(retreatLabel());
    if (const char *output = std::getenv("RAIDLINE_RETREAT_UI_DIR"); output && *output) {
        const std::filesystem::path dir(output);
        ASSERT_TRUE(dir.is_absolute());
        std::filesystem::create_directories(dir);
        A::loadPlayerArt(*app); A::drawRaid(*app); SDL_RenderPresent(renderer);
        ASSERT_TRUE(IMG_SavePNG(surface,(dir / "riverside-retreat-1280-zh.png").string().c_str()));
    }
    const auto shot = A::barrelScreen(*app,bodyCenter(first));
    A::anchor(*app) = shot;
    pointer(shot,true); A::frame(*app,.016F); pointer(shot,false); A::frame(*app,.016F);
    for (int i=0; i<24 && world.armedScavengers()[0].alive; ++i) A::frame(*app,.016F);
    ASSERT_FALSE(world.armedScavengers()[0].alive);
    EXPECT_FALSE(world.armedScavengers()[0].escaped);
    const auto *corpse = profile().assets.find(firstLoadout.corpseAssetId);
    ASSERT_NE(corpse, nullptr);
    EXPECT_TRUE(std::get<RaidCorpseContainerAssetLocation>(corpse->location).deathPosition);
    EXPECT_NE(profile().assets.find(firstLoadout.pistolAssetId), nullptr);

    ASSERT_NE(actor(second), nullptr);
    ASSERT_TRUE(placeInClearSight(second));
    const auto mapLocations = std::array{bodyCenter(second), *secondLoadout.retreatPoint};
    const auto mapRegions = [&] {
        key(SDL_SCANCODE_M); A::frame(*app);
        EXPECT_TRUE(A::tacticalOpen(*app));
        SDL_SetRenderDrawColor(renderer,10,16,18,255); SDL_RenderClear(renderer);
        A::drawTactical(*app); SDL_RenderPresent(renderer);
        const auto &r = kRaidTacticalChart;
        const auto size = world.tacticalMap().worldSize();
        std::vector<Uint32> pixels;
        for (const auto p : mapLocations) {
            const int x = static_cast<int>(r.position.x + r.size.x*p.x/size.x);
            const int y = static_cast<int>(r.position.y + r.size.y*p.y/size.y);
            for (int py=y-12;py<=y+12;++py) for (int px=x-12;px<=x+12;++px) {
                Uint8 red{},green{},blue{},alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface,px,py,&red,&green,&blue,&alpha));
                pixels.push_back(Uint32(red)<<24 | Uint32(green)<<16 | Uint32(blue)<<8 | alpha);
            }
        }
        key(SDL_SCANCODE_M); A::frame(*app);
        EXPECT_FALSE(A::tacticalOpen(*app));
        return pixels;
    };
    A::anchor(*app) = A::barrelScreen(*app,bodyCenter(second));
    A::frame(*app);
    const auto beforeRetreat = mapRegions();
    ASSERT_FALSE(actor(second)->takeDamage(actor(second)->health()-1));
    A::anchor(*app) = A::barrelScreen(*app,bodyCenter(second));
    A::frame(*app,.016F);
    ASSERT_TRUE(world.armedScavengers()[1].retreating);
    ASSERT_TRUE(retreatLabel());
    const auto cover = world.previewSmoke(bodyCenter(second),SmokeUseDefinition{});
    ASSERT_TRUE(cover);
    RaidSmokeRuntime smoke; ASSERT_TRUE(smoke.start(*cover)); smoke.advance(.55F);
    world.commitSmoke(std::move(smoke));
    EXPECT_FALSE(world.raidEnemyObservable(second));
    EXPECT_FALSE(retreatLabel());
    EXPECT_TRUE(mapRegions() == beforeRetreat) << "retreat must not identify a hidden actor or exit on the map";
    for (int i=0; i<1800 && !world.armedScavengers()[1].escaped; ++i) A::frame(*app,.05F);
    ASSERT_TRUE(world.armedScavengers()[1].escaped);
    EXPECT_FALSE(world.armedScavengers()[1].alive);
    EXPECT_FALSE(retreatLabel());
    EXPECT_TRUE(profile().pendingRaid->armedScavengers[1].escaped);
    const auto *escapedRoot = profile().assets.find(secondLoadout.corpseAssetId);
    ASSERT_NE(escapedRoot, nullptr);
    EXPECT_FALSE(std::get<RaidCorpseContainerAssetLocation>(escapedRoot->location).deathPosition);
    EXPECT_EQ(actor(second), nullptr);
    EXPECT_FALSE(session.canOpenRaidSceneContainer(secondLoadout.corpseAssetId));
}

TEST_F(UiBaseDeploymentTest, RiversideBarterUsesTheSameContactWithPointerAndKeyboard)
{
    const auto riverside = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &m) { return m.id == kFieldIntelligenceSourceMapId; });
    ASSERT_NE(riverside, content.maps().end());
    const auto backpack = std::find_if(profile().assets.records().begin(), profile().assets.records().end(),
        [](const auto &entry) { return entry.second.definitionId == alpha_content::backpack; });
    ASSERT_NE(backpack, profile().assets.records().end());
    const auto packId = backpack->first;
    ASSERT_TRUE(executeInventory(profile(), content, InventoryEquipCommand{packId, EquipmentSlotKind::Backpack},
        {profile().revision, "barter-ui-equip-pack"}).succeeded);
    const auto carried = ProfileContainerId::compartment(packId, 0);
    const auto electronics = addToStorage(carried, ItemDefinitionId{"item.loot.electronics"}, 1);
    const auto scrap = addToStorage(carried, ItemDefinitionId{"item.loot.scrap_parts"}, 1);
    ASSERT_NE(electronics, 0U);
    ASSERT_NE(scrap, 0U);
    ++profile().revision;
    map(static_cast<std::size_t>(riverside - content.maps().begin()));
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    auto &session = A::session(*app);
    auto &world = session.world();
    ASSERT_TRUE(profile().pendingRaid->fieldIntelligenceTrade);
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    const auto contact = profile().pendingRaid->fieldIntelligenceTrade->position;
    auto &player = const_cast<Player &>(world.player());
    const auto half = player.size() * .5F;
    ASSERT_TRUE(player.setPosition({contact.x - half, contact.y - half}));
    A::frame(*app);
    ASSERT_EQ(session.fieldIntelligenceTradeStatus(), RaidFieldTradeAccess::Available);
    key(SDL_SCANCODE_E);
    A::frame(*app);
    ASSERT_TRUE(A::fieldTrade(*app).open);
    ASSERT_TRUE(session.queryFieldBarter(0).canCommit) << session.queryFieldBarter(0).message;
    ASSERT_TRUE(session.queryFieldBarter(1).canCommit) << session.queryFieldBarter(1).message;
    const auto before = *session.fieldIntelligenceTradeView();
    const auto baseCurrency = profile().currency;
    const auto carriedQuantity = [&](const ItemDefinitionId &item) {
        std::uint32_t total = 0;
        for (const auto &[id, asset] : profile().assets.records())
            if (asset.definitionId == item && assetIsCarried(profile(), id)) total += asset.quantity;
        return total;
    };
    const auto ammo = session.queryFieldBarter(0).outputDefinitionId;
    const auto lure = session.queryFieldBarter(1).outputDefinitionId;
    const auto ammoBefore = carriedQuantity(ammo), luresBefore = carriedQuantity(lure);

    // A document-free Enter still addresses the original first row.
    key(SDL_SCANCODE_RETURN);
    A::frame(*app);
    EXPECT_EQ(A::fieldTrade(*app).feedback, "Carry a document to sell it");
    down({1200, 390});
    ++profile().revision; // A separate inventory edit invalidates this quote.
    up({1200, 390});
    EXPECT_NE(profile().assets.find(electronics), nullptr);
    EXPECT_EQ(carriedQuantity(ammo), ammoBefore);
    click({1200, 390});
    EXPECT_TRUE(session.queryFieldBarter(0).exchanged);
    EXPECT_EQ(profile().assets.find(electronics), nullptr);
    EXPECT_EQ(carriedQuantity(ammo), ammoBefore + 12U);
    click({1200, 390});
    EXPECT_EQ(A::fieldTrade(*app).feedback, "barter already claimed this Raid");
    EXPECT_EQ(carriedQuantity(ammo), ammoBefore + 12U);

    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer));
    A::drawFieldTrade(*app);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    EXPECT_GT(bright({841, 370, 240, 40}), 20);
    EXPECT_GT(bright({841, 422, 240, 40}), 20);
    if (const char *out = std::getenv("RAIDLINE_BARTER_UI_DIR"); out && *out) {
        const std::filesystem::path directory(out);
        ASSERT_TRUE(directory.is_absolute());
        std::filesystem::create_directories(directory);
        ASSERT_TRUE(IMG_SavePNG(surface, (directory / "riverside-barter-1280-zh.png").string().c_str()));
    }
    key(SDL_SCANCODE_DOWN); // from the clicked fifth row to the sixth
    key(SDL_SCANCODE_RETURN);
    A::frame(*app);
    ASSERT_TRUE(session.queryFieldBarter(1).exchanged);
    EXPECT_EQ(profile().assets.find(scrap), nullptr);
    EXPECT_EQ(carriedQuantity(lure), luresBefore + 1U);
    EXPECT_EQ(session.fieldIntelligenceTradeView()->balance, before.balance);
    EXPECT_EQ(session.fieldIntelligenceTradeView()->fileSold, before.fileSold);
    EXPECT_EQ(profile().currency, baseCurrency);
    for (std::size_t index = 0; index < before.offers.size(); ++index)
        EXPECT_EQ(session.fieldIntelligenceTradeView()->offers[index].purchased, before.offers[index].purchased);
    key(SDL_SCANCODE_ESCAPE);
    A::frame(*app);
    EXPECT_FALSE(A::fieldTrade(*app).open);
    EXPECT_FALSE(A::paused(*app));
}

TEST_F(UiBaseDeploymentTest, HospitalBarrelIsVisibleCanBeShotAndLeavesOnlyOneVisibleWreck)
{
    const auto hospital = std::find_if(content.maps().begin(),content.maps().end(),
        [](const auto &m){return m.id==kHospitalFloorplanMapId;});
    ASSERT_NE(hospital,content.maps().end());
    map(static_cast<std::size_t>(hospital-content.maps().begin()));
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(),GameFlowState::Raid);
    auto &session = A::session(*app);
    auto &world = session.world();
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    ASSERT_FALSE(world.volatileBarrels().empty());
    const auto barrel = world.volatileBarrels().front();
    const auto center = barrel.center;
    ASSERT_FALSE(barrel.spent);
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    auto &player = const_cast<Player &>(world.player());
    GameplayInput aim{}; aim.aimWorldPosition = center;
    const std::array<Vec2,8> offsets{{{-92,0},{92,0},{0,-92},{0,92},{-70,-70},{70,-70},{-70,70},{70,70}}};
    bool clear = false;
    for (const auto delta : offsets) {
        if (!player.setPosition({center.x+delta.x-player.size()*.5F,center.y+delta.y-player.size()*.5F})) continue;
        world.update(aim,.016F);
        if (!world.raidBoundsObservable(volatileBarrelBounds(center))) continue;
        const auto probe = world.previewSmoke(center,SmokeUseDefinition{});
        if (probe && std::hypot(probe->point.x-center.x,probe->point.y-center.y)<1.0F) { clear=true; break; }
    }
    ASSERT_TRUE(clear);
    const auto chart = [&] {
        const SDL_Rect viewport{640-static_cast<int>(std::round(center.x)),360-static_cast<int>(std::round(center.y)),9999,9999};
        SDL_SetRenderViewport(renderer,&viewport);
        SDL_SetRenderDrawColor(renderer,10,16,18,255);
        SDL_RenderClear(renderer);
        const auto labels = A::drawBarrels(*app);
        SDL_SetRenderViewport(renderer,nullptr);
        SDL_RenderPresent(renderer);
        return labels;
    };
    const auto pixels = [&](SDL_Color color) {
        unsigned count = 0;
        for (int y=344; y<377; ++y) for(int x=624; x<657; ++x) {
            Uint8 r{},g{},b{},a{};
            if (SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a) &&
                r==color.r && g==color.g && b==color.b) ++count;
        }
        return count;
    };
    const auto snapshot = [&](const char *filename) {
        if (const char *folder = std::getenv("RAIDLINE_BARREL_UI_DIR")) {
            std::filesystem::create_directories(folder);
            A::loadPlayerArt(*app); A::drawRaid(*app); SDL_RenderPresent(renderer);
            const auto path = (std::filesystem::path(folder)/filename).u8string();
            EXPECT_TRUE(IMG_SavePNG(surface,reinterpret_cast<const char *>(path.c_str())));
        }
    };
    const SDL_Color band{222,182,114,255}, wreck{143,127,105,255};
    const auto intact = chart();
    EXPECT_NE(std::find(intact.begin(),intact.end(),"Fuel barrel"),intact.end());
    EXPECT_GT(pixels(band),20U);
    snapshot("hospital-barrel-intact-1280-zh.png");

    const auto landing = world.previewSmoke(center, SmokeUseDefinition{});
    ASSERT_TRUE(landing);
    RaidSmokeRuntime smoke;
    ASSERT_TRUE(smoke.start(*landing)); smoke.advance(.55F);
    world.commitSmoke(std::move(smoke));
    EXPECT_TRUE(chart().empty());
    EXPECT_EQ(pixels(band),0U);
    world.update(aim,8.1F);
    ASSERT_TRUE(world.smoke().clouds().empty());
    const auto screen = A::barrelScreen(*app,center);
    A::anchor(*app) = screen;
    pointer(screen,true); A::frame(*app,.016F);
    pointer(screen,false); A::frame(*app,.016F);
    ASSERT_TRUE(world.volatileBarrels().front().spent) << session.persistenceMessage();
    const auto labels = chart();
    EXPECT_NE(std::find(labels.begin(),labels.end(),"Spent fuel barrel"),labels.end());
    EXPECT_EQ(pixels(band),0U);
    EXPECT_GT(pixels(wreck),8U);
    snapshot("hospital-barrel-spent-1280-zh.png");

    // An older world with no frozen barrel entries must draw exactly nothing.
    RaidWorldConfig old; old.worldSize={1600,1000}; old.playerSpawn={320,250};
    old.extractionPoint={{1250,720},{120,100}};
    WeaponHandlingTestAccess::world(session,std::move(old));
    ASSERT_TRUE(session.world().volatileBarrels().empty());
    EXPECT_TRUE(chart().empty());
    EXPECT_EQ(pixels(wreck),0U); EXPECT_EQ(pixels(band),0U);
}

TEST_F(UiBaseDeploymentTest, HospitalEscortMapShowsPickupAndHandoffAndFCommandsTheSameSurvivor)
{
    const auto hospital = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &m) { return m.id == kHospitalFloorplanMapId; });
    ASSERT_NE(hospital, content.maps().end());
    map(static_cast<std::size_t>(hospital-content.maps().begin()));
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    auto &world = A::session(*app).world();
    ASSERT_TRUE(world.hospitalEscort());
    for (auto &enemy : WeaponHandlingTestAccess::actors(world))
        static_cast<void>(enemy.takeDamage(enemy.maxHealth()));
    const auto initial = *world.hospitalEscort();
    ASSERT_EQ(initial.state, HospitalEscortState::AwaitingContact);
    const auto &zone = initial.handoff;
    const Vec2 goal{zone.position.x + zone.size.x/2, zone.position.y + zone.size.y/2};
    key(SDL_SCANCODE_M); A::frame(*app);
    ASSERT_TRUE(A::tacticalOpen(*app));
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255);
    ASSERT_TRUE(SDL_RenderClear(renderer));
    A::drawTactical(*app);
    ASSERT_TRUE(SDL_RenderPresent(renderer));
    const auto markerPixels = [&](Vec2 position, SDL_Color expected) {
        const auto extent = world.playerWorldSize();
        const auto &chart = kRaidTacticalChart;
        const int cx = static_cast<int>(chart.position.x + position.x / extent.x * chart.size.x);
        const int cy = static_cast<int>(chart.position.y + position.y / extent.y * chart.size.y);
        int count = 0;
        for (int y = cy - 8; y <= cy + 8; ++y) for (int x = cx - 8; x <= cx + 8; ++x) {
            Uint8 r{}, g{}, b{}, alpha{};
            if (SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&alpha) &&
                r==expected.r && g==expected.g && b==expected.b) ++count;
        }
        return count;
    };
    EXPECT_GT(markerPixels(initial.position, {247,199,111,255}), 6);
    EXPECT_GT(markerPixels(goal, {116,228,190,255}), 8);
    if (const char *out = std::getenv("RAIDLINE_ESCORT_UI_DIR"); out && *out) {
        const std::filesystem::path directory(out);
        ASSERT_TRUE(directory.is_absolute());
        std::filesystem::create_directories(directory);
        ASSERT_TRUE(IMG_SavePNG(surface, (directory / "hospital-escort-map-1280-zh.png").string().c_str()));
    }
    auto downView = initial;
    downView.state = HospitalEscortState::Down;
    const auto extent = world.playerWorldSize();
    downView.position = {extent.x * (goal.x < extent.x * .5F ? .85F : .15F),
                         extent.y * (goal.y < extent.y * .5F ? .85F : .15F)};
    for (const bool visible : {false, true}) {
        downView.visible = visible;
        ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255));
        ASSERT_TRUE(SDL_RenderClear(renderer));
        A::drawEscortMarkers(*app, downView);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        EXPECT_GT(markerPixels(goal, {213,141,132,255}), 8);
        if (visible) EXPECT_GT(markerPixels(downView.position, {213,141,132,255}), 6);
        else EXPECT_EQ(markerPixels(downView.position, {213,141,132,255}), 0);
    }
    key(SDL_SCANCODE_M); A::frame(*app);
    ASSERT_FALSE(A::tacticalOpen(*app));
    auto &player = const_cast<Player &>(world.player());
    const auto place = [&](Vec2 center) {
        const float half = player.size() * .5F;
        return player.setPosition({center.x-half,center.y-half});
    };
    ASSERT_TRUE(place(initial.position));
    for (const auto state : {HospitalEscortState::Following, HospitalEscortState::Waiting, HospitalEscortState::Following}) {
        keyEvent(SDL_SCANCODE_F, true); A::frame(*app);
        ASSERT_EQ(world.hospitalEscort()->state, state);
        keyEvent(SDL_SCANCODE_F, false); A::frame(*app);
    }
    ASSERT_TRUE(place(goal));
    for (int tick=0; tick<600 && !world.hospitalEscort()->atHandoff; ++tick) A::frame(*app,0.1F);
    ASSERT_TRUE(world.hospitalEscort()->atHandoff);
    ASSERT_FALSE(world.hospitalEscort()->threatened);
    auto &population = profile().basePopulation;
    ASSERT_LE(population.ordinaryResidents, population.bedCapacity);
    population.professionResidents[baseProfessionIndex(BaseResidentProfession::General)] +=
        population.bedCapacity - population.ordinaryResidents;
    population.ordinaryResidents = population.bedCapacity;
    ++profile().revision;
    const auto residents = population.ordinaryResidents;
    const auto admission = A::session(*app).ordinarySurvivorRescuePlan();
    ASSERT_TRUE(admission && admission->canCommit && admission->bedShortfallAfter == 1U);
    const auto labels = A::escortWorldLabels(*app, *world.hospitalEscort());
    const auto estimate = std::string("居民 ") + std::to_string(admission->residentsBefore) + "→" +
        std::to_string(admission->residentsAfter) + " · 床位 " + std::to_string(admission->bedCapacity) +
        " · 口粮 " + std::to_string(admission->dailyRationsAfter) + "/日";
    EXPECT_NE(std::find(labels.begin(), labels.end(), estimate), labels.end());
    EXPECT_NE(std::find(labels.begin(), labels.end(), "交接后缺 1 个床位"), labels.end());
    if (const char *out = std::getenv("RAIDLINE_ESCORT_UI_DIR"); out && *out) {
        A::drawRaid(*app);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        ASSERT_TRUE(IMG_SavePNG(surface, (std::filesystem::path(out) / "hospital-escort-handoff-1280-zh.png").string().c_str()));
    }
    keyEvent(SDL_SCANCODE_F, true);
    A::frame(*app, .5F);
    EXPECT_FALSE(profile().pendingRaid->rescue->secured);
    EXPECT_GT(world.ordinarySurvivorRescueProgress(), 0.0F);
    keyEvent(SDL_SCANCODE_F, false); A::frame(*app);
    EXPECT_FLOAT_EQ(world.ordinarySurvivorRescueProgress(), 0.0F);
    keyEvent(SDL_SCANCODE_F, true);
    for (int tick=0; tick<25 && !profile().pendingRaid->rescue->secured; ++tick) A::frame(*app, .1F);
    keyEvent(SDL_SCANCODE_F, false); A::frame(*app);
    EXPECT_TRUE(profile().pendingRaid->rescue->secured);
    EXPECT_EQ(world.hospitalEscort()->state, HospitalEscortState::Delivered);
    EXPECT_EQ(profile().basePopulation.ordinaryResidents, residents+1U);
}

TEST_F(UiBaseDeploymentTest, BrowsingTabsDetailsAndMapsDoesNotMutateOrGenerateRaid) {
    const auto before = profileStateFingerprint(profile()); const auto highWater = profile().assets.nextAssetId();
    for (std::size_t i = 0; i < content.maps().size(); ++i) { map(i); control(C::BriefingDetails); control(C::ReadinessDetails); }
    intelligence(); intelRow(RaidIntelligenceCategory::Enemy); control(C::Briefing); draw();
    EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_EQ(profile().assets.nextAssetId(), highWater); expectNoRaid();
}

TEST_F(UiBaseDeploymentTest, FireSupportIsOfferedForThreeOrdinaryMapsAndNeverForRecovery) {
    const auto before = profileStateFingerprint(profile());
    for (const auto &map : content.maps()) {
        SCOPED_TRACE(map.id.value());
        const auto ordinary = projectUiDeployment(profile(), content, UiLanguage::SimplifiedChinese,
            map.id, {}, std::nullopt, 0, {});
        const bool supportsTeam = map.id == kRaidFireSupportMapId ||
            map.id == MapDefinitionId{"map.raid.hospital_district"} ||
            map.id == MapDefinitionId{"map.raid.frontier_exchange"};
        EXPECT_EQ(ordinary.fireSupport.mapEligible, supportsTeam);
        const auto recovery = projectUiDeployment(profile(), content, UiLanguage::SimplifiedChinese,
            map.id, {}, std::optional<std::string>{"recovery-preview"}, 0, {});
        EXPECT_FALSE(recovery.fireSupport.mapEligible);
    }
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiBaseDeploymentTest, SquadRescueSelectionUsesOnlyTheChosenOrdinaryMap) {
    const auto greyline = std::ranges::find(content.maps(), kRaidFireSupportMapId, &MapDefinition::id);
    const auto frontier = std::ranges::find(content.maps(), kRaidFireSupportReplacementMapId, &MapDefinition::id);
    ASSERT_NE(greyline, content.maps().end());
    ASSERT_NE(frontier, content.maps().end());
    map(static_cast<std::size_t>(greyline - content.maps().begin()));
    control(C::Squad);
    ASSERT_TRUE(A::state(*app).squadOpen);
    control(C::SquadRecruit);
    EXPECT_EQ(A::state(*app).specialistRescue, RaidSquadRole::Support);
    key(SDL_SCANCODE_RETURN); A::frame(*app);
    expectNoRaid();
    EXPECT_FALSE(A::state(*app).squadOpen);
    key(SDL_SCANCODE_F1); A::frame(*app);
    ASSERT_TRUE(A::state(*app).squadOpen);
    down(C::SquadRecruit); up(C::SquadDone);
    EXPECT_EQ(A::state(*app).specialistRescue, RaidSquadRole::Support);
    draw();
    const auto recruitButton = uiDeploymentControlRect(C::SquadRecruit);
    EXPECT_GT(bright({recruitButton.x + 12, recruitButton.y + 6,
        recruitButton.w - 24, recruitButton.h - 12}), 10);
    control(C::SquadDone);
    map(static_cast<std::size_t>(frontier - content.maps().begin()));
    EXPECT_FALSE(A::state(*app).specialistRescue);
    control(C::Squad); control(C::SquadRecruit);
    EXPECT_EQ(A::state(*app).specialistRescue, RaidSquadRole::Sniper);
    control(C::SquadDone); control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    ASSERT_TRUE(profile().pendingRaid);
    EXPECT_EQ(profile().pendingRaid->specialistRescue, RaidSquadRole::Sniper);
    EXPECT_TRUE(profile().pendingRaid->squadOrder.empty());
}

TEST_F(UiBaseDeploymentTest, SquadSelectsRealLoansAndFunctionKeysAddressEnlistmentOrder) {
    using R = RaidSquadRole;
    const auto greyline = std::ranges::find(content.maps(), kRaidFireSupportMapId, &MapDefinition::id);
    ASSERT_NE(greyline, content.maps().end());
    auto &pop = profile().basePopulation;
    const auto combat = baseProfessionIndex(BaseResidentProfession::Combat);
    const auto general = baseProfessionIndex(BaseResidentProfession::General);
    ASSERT_GE(pop.professionResidents[general], 2U);
    pop.professionResidents[general] -= 2; pop.professionResidents[combat] += 2;
    profile().squadRoster[R::Support] = {};
    const auto stash = ProfileContainerId::stash();
    const auto pistol = addToStorage(stash, kRaidSquadSupportWeaponId, 1);
    const auto medicine = addToStorage(stash, alpha_content::medkit, 1);
    ASSERT_NE(pistol, 0U); ASSERT_NE(medicine, 0U);
    ASSERT_NE(addToStorage(stash, kRaidSquadSupportAmmoId, 30), 0U);
    const auto rifle = addToStorage(stash, kRaidFireSupportRifleId, 1);
    ASSERT_NE(rifle, 0U);
    ASSERT_NE(addToStorage(stash, kRaidFireSupportAmmunitionId, 30), 0U);
    ++profile().revision;
    ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
    map(static_cast<std::size_t>(greyline - content.maps().begin()));
    control(C::Squad); control(C::SquadSupport); control(C::SquadToggle);
    ASSERT_EQ(A::state(*app).squad.size(), 1U);
    const auto &choices = A::projection(*app).squadOptions[static_cast<std::size_t>(R::Support)];
    const auto maxChoices = choices.cargo.size();
    for (std::size_t i = 0; i < maxChoices; ++i) {
        control(C::SquadCargoOne);
        if (A::state(*app).squad.front().cargoAssetIds == std::vector<AssetInstanceId>{medicine}) break;
    }
    ASSERT_EQ(A::state(*app).squad.front().cargoAssetIds, std::vector<AssetInstanceId>{medicine});
    control(C::SquadFire); control(C::SquadToggle); control(C::SquadDone);
    ASSERT_EQ(A::state(*app).squad.size(), 2U);
    control(C::Deploy);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid) << A::legacyMessage(*app);
    ASSERT_EQ(profile().pendingRaid->squadOrder, (std::vector<R>{R::Support, R::Fire}));
    auto &world = A::session(*app).world();
    key(SDL_SCANCODE_F2); A::frame(*app); ASSERT_TRUE(A::ordersOpen(*app));
    key(SDL_SCANCODE_2); A::frame(*app);
    ASSERT_TRUE(world.squadMember(R::Support));
    EXPECT_EQ(world.squadMember(R::Support)->movementOrder, FireSupportMovementOrder::Hold);
    EXPECT_EQ(world.squadMember(R::Fire)->movementOrder, FireSupportMovementOrder::Follow);
    key(SDL_SCANCODE_F3); A::frame(*app);
    key(SDL_SCANCODE_4); A::frame(*app);
    EXPECT_EQ(world.squadMember(R::Fire)->fireOrder, FireSupportFireOrder::FreeFire);
    EXPECT_EQ(world.squadMember(R::Support)->fireOrder, FireSupportFireOrder::HoldFire);
    key(SDL_SCANCODE_F1); A::frame(*app);
    key(SDL_SCANCODE_7); A::frame(*app);
    EXPECT_EQ(world.squadMember(R::Fire)->movementOrder, FireSupportMovementOrder::Spread);
    EXPECT_EQ(world.squadMember(R::Support)->movementOrder, FireSupportMovementOrder::Spread);
    key(SDL_SCANCODE_ESCAPE); A::frame(*app);
    EXPECT_FALSE(A::ordersOpen(*app));
    EXPECT_TRUE(A::captured(*app));
}

TEST_F(UiBaseDeploymentTest, FuelBriefingIsLimitedToOrdinaryFrontierDeployments) {
    const auto fingerprint = profileStateFingerprint(profile());
    for (const auto &map : content.maps()) {
        const auto ordinary = projectUiDeployment(profile(), content, UiLanguage::English,
            map.id, {}, std::nullopt, 0, {});
        EXPECT_EQ(ordinary.fuelExit, map.id == kFreightCrateMapId);
        const auto recovery = projectUiDeployment(profile(), content, UiLanguage::English,
            map.id, {}, std::optional<std::string>{"recovery-preview"}, 0, {});
        EXPECT_FALSE(recovery.fuelExit);
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}

TEST_F(UiBaseDeploymentTest, ArmedScavengerBriefingAppearsOnlyForOrdinaryRiverside) {
    for (const auto &map : content.maps()) {
        SCOPED_TRACE(map.id.value());
        const auto ordinary = projectUiDeployment(profile(), content, UiLanguage::SimplifiedChinese,
            map.id, {}, std::nullopt, 0, {});
        EXPECT_EQ(ordinary.armedScavengerEncounter, map.id == MapDefinitionId{"map.raid.riverside"});
        const auto recovery = projectUiDeployment(profile(), content, UiLanguage::SimplifiedChinese,
            map.id, {}, std::string{"recovery-preview"}, 0, {});
        EXPECT_FALSE(recovery.armedScavengerEncounter);
    }
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

TEST_F(UiBaseDeploymentTest, PurchasingTheDepotPlanLeavesTheDistrictHospitalPlanUnknown) {
    const auto &maps = content.maps();
    const auto hospital = std::find_if(maps.begin(), maps.end(),
        [](const auto &entry) { return entry.id == kHospitalFloorplanMapId; });
    ASSERT_NE(hospital, maps.end());
    map(static_cast<std::size_t>(hospital - maps.begin()));
    intelligence();
    const auto rows = A::projection(*app).interiors;
    const auto depot = std::find_if(rows.begin(), rows.end(),
        [](const auto &row) { return row.id == kHospitalDepotSpaceId; });
    ASSERT_NE(depot, rows.end());
    EXPECT_EQ(depot->price, 120U);
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, depot->name), "救护车保障站");
    const auto visibleRow = static_cast<std::size_t>(depot - rows.begin());
    ASSERT_LT(visibleRow, uiDeploymentInteriorPageSize());
    const auto funds = profile().currency;
    click(center(uiDeploymentInteriorRect(visibleRow, true)));
    const auto loaded = SaveRepository(path).load(content);
    ASSERT_TRUE(loaded.profile);
    EXPECT_EQ(loaded.profile->currency, funds - 120U);
    EXPECT_TRUE(loaded.profile->raidInteriorIntelligence.knows(kHospitalDepotSpaceId));
    EXPECT_FALSE(loaded.profile->raidInteriorIntelligence.knows(kHospitalFloorplanSpaceId));
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
    A::pause(*app); A::frame(*app); A::resume(*app); const auto beforeProfile=profile();const auto fp = profileStateFingerprint(profile()); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
    down(point); key(SDL_SCANCODE_TAB); A::frame(*app); A::closeInventory(*app); up(point); EXPECT_EQ(profileStateFingerprint(profile()), fp) << raidline::test::describeUiProfileChange(beforeProfile,profile());
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

TEST_F(UiBaseDeploymentTest, NightRendezvousArrivalStatusSurfacesAtGate) {
    const auto ashworks = std::find_if(content.maps().begin(), content.maps().end(),
        [](const auto &definition) { return definition.id == kRaidNightRendezvousMapId; });
    ASSERT_NE(ashworks, content.maps().end());
    map(static_cast<std::size_t>(ashworks - content.maps().begin()));
    using Availability = RaidNightRendezvousAvailability;
    EXPECT_EQ(A::projection(*app).nightRendezvous.availability, Availability::Locked);

    auto &p = profile();
    p.nightRendezvousUnlocked = true;
    p.worldClock.elapsedWorldMinutes = 2U * kWorldMinutesPerDay + 20U * 60U;
    ++p.revision;
    A::refresh(*app);
    EXPECT_EQ(A::projection(*app).travel.outboundMinutes, 150U);
    EXPECT_EQ(A::projection(*app).nightRendezvous.availability, Availability::Missed);
    const auto missed = uiNightRendezvousArrivalText(
        A::projection(*app).nightRendezvous, UiLanguage::SimplifiedChinese);
    EXPECT_NE(missed.find("抵达时接应未开放"), std::string::npos);
    EXPECT_NE(missed.find("第7天"), std::string::npos);
    drawRendezvousArrival("missed");

    p.worldClock.elapsedWorldMinutes = 2U * kWorldMinutesPerDay + 18U * 60U;
    ++p.revision;
    A::refresh(*app);
    EXPECT_EQ(A::projection(*app).nightRendezvous.availability, Availability::Available);
    EXPECT_EQ(uiNightRendezvousArrivalText(A::projection(*app).nightRendezvous, UiLanguage::English),
        "NIGHT RENDEZVOUS OPEN · 18:00–22:00");
    drawRendezvousArrival("available");
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
