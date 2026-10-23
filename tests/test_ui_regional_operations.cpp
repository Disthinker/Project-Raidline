#include "app.h"
#include "alpha_content_ids.h"
#include "base_facility_layout_domain.h"
#include "base_manufacturing_domain.h"
#include "ui_base_deployment_presentation.h"
#include "ui_regional_operations_presentation.h"

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

struct UiRegionalOperationsTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.regionalUi_; }
    static auto &projection(App &a) { a.refreshRegionalOperationsUi(); return a.regionalProjection_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.regionalFeedback_; }
    static auto &warning(App &a) { return a.deploymentWarningArmed_; }
    static auto &regional(App &a) { return a.regionalOperationsOpen_; }
    static auto &lost(App &a) { return a.lostRaidRecordsOpen_; }
    static auto &anchor(App &a) { return a.worldAimPointerAnchor_; }
    static auto &intelligence(App &a) { return a.selectedRaidIntelligence_; }
    static auto &recovery(App &a) { return a.selectedRaidSelfRecoveryRecordId_; }
    static auto &mapIndex(App &a) { return a.selectedRaidMapIndex_; }
    static auto &construction(App &a) { return a.baseConstructionPanelOpen_; }
    static auto generation(App &a) { return a.regionalProjectionGeneration_; }
    static void refresh(App &a) { a.refreshRegionalOperationsUi(true); }
    static void reset(App &a) { a.resetRegionalOperationsUi(); }
    static void draw(App &a) { a.renderRegionalOperations(); }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void legacy(App &a, Vec2 p) { a.handleBasePointerClick({{p.x, p.y}}); }
    static void pause(App &a) { a.pauseMenu_.open(); }
    static void resume(App &a) { a.pauseMenu_.close(); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
    }
    static void detach(App &a) { a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr; }
};

namespace {
using A = UiRegionalOperationsTestAccess;
using C = UiRegionalOperationsControl;
using T = UiRegionalOperationsTab;
const RegionalOutpostDefinitionId kRelay{"regional_outpost.old_service_relay"};
const RegionalOutpostDefinitionId kAshOutpost{"regional_outpost.ashworks_logistics_yard"};
const RegionalOutpostDefinitionId kGreyOutpost{"regional_outpost.greyline_yard"};
const RegionalBaseSiteDefinitionId kAshSite{"regional_base_site.ashworks_logistics_yard"};
const RegionalBaseSiteDefinitionId kGreySite{"regional_base_site.greyline_yard"};
const BaseFacilityDefinitionId kKitchen{"base_facility.kitchen_water"};
const BaseFacilityDefinitionId kWorkshop{"base_facility.workshop"};

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

class UiRegionalOperationsTest : public testing::Test {
protected:
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    const ContentRegistry &content = publishedContentRegistry();
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }

    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path = std::filesystem::temp_directory_path() /
            ("raidline-regional-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        auto p = makeNewAlphaProfile("regional-ui", content);
        p.currency = 10000; p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        // New profiles keep their starting rifle in Stash. Equip through the
        // real inventory command before seeding this isolated, validated save.
        const auto rifle = std::find_if(p.assets.records().begin(), p.assets.records().end(),
            [](const auto &entry) { return entry.second.definitionId == alpha_content::rifle; });
        ASSERT_NE(rifle, p.assets.records().end());
        const auto rifleId = rifle->first;
        const auto equipped = executeInventory(p, content,
            InventoryEquipCommand{rifleId, EquipmentSlotKind::PrimaryWeapon},
            {p.revision, "regional-fixture-equip"});
        ASSERT_TRUE(equipped.succeeded) << equipped.message;
        p.assets.findMutable(rifleId)->chamberedRound = MagazineRoundRecord{alpha_content::ammunition, std::nullopt};
        ++p.revision;
        ASSERT_TRUE(validateProfileState(p, content).valid) << validateProfileState(p, content).message;
        ASSERT_TRUE(projectWeaponSupply(p, content, false).anyWeaponCanFire);
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
        if (path.empty() || !std::filesystem::exists(path)) return;
        const auto resolved = std::filesystem::canonical(path);
        ASSERT_TRUE(std::filesystem::equivalent(resolved.parent_path(), std::filesystem::temp_directory_path()));
        ASSERT_TRUE(resolved.filename().string().starts_with("raidline-regional-ui-"));
        std::error_code error; std::filesystem::remove_all(resolved, error);
    }
    void click(Vec2 p) { pointer(p, true); pointer(p, false); A::frame(*app); }
    void control(C c) { click(center(uiRegionalOperationsControlRect(c))); }
    void down(C c) { pointer(center(uiRegionalOperationsControlRect(c)), true); A::frame(*app); }
    void up(C c) { pointer(center(uiRegionalOperationsControlRect(c)), false); A::frame(*app); }
    void open() {
        if (!A::regional(*app)) click(center(uiDeploymentControlRect(UiDeploymentControl::Regional)));
        ASSERT_TRUE(A::regional(*app)); A::refresh(*app);
    }
    void outpost(const RegionalOutpostDefinitionId &id) {
        control(C::Routes);
        const auto rows = A::projection(*app).outposts;
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &r) { return r.id == id; });
        ASSERT_NE(it, rows.end());
        const auto index = static_cast<std::size_t>(it - rows.begin());
        const auto page = index / uiRegionalOperationsPageSize();
        while (A::state(*app).listPage < page) control(C::Next);
        while (A::state(*app).listPage > page) control(C::Previous);
        click(center(uiRegionalOperationsRowRect(index % uiRegionalOperationsPageSize())));
        ASSERT_EQ(A::projection(*app).selectedOutpostId, id);
    }
    void site(const RegionalBaseSiteDefinitionId &id) {
        control(C::Base);
        const auto rows = A::projection(*app).sites;
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &r) { return r.id == id; });
        ASSERT_NE(it, rows.end());
        const auto index = static_cast<std::size_t>(it - rows.begin());
        const auto page = index / uiRegionalOperationsPageSize();
        while (A::state(*app).listPage < page) control(C::Next);
        while (A::state(*app).listPage > page) control(C::Previous);
        click(center(uiRegionalOperationsRowRect(index % uiRegionalOperationsPageSize())));
        ASSERT_EQ(A::projection(*app).selectedSiteId, id);
    }
    void prepareAshworks() {
        profile().regionalOperations.baseSites.at(kAshSite).unlocked = true;
        auto &o = profile().regionalOperations.outposts.at(kAshOutpost); o.unlocked = true; o.established = true;
        ++profile().revision; A::refresh(*app);
    }
    void migrationReady() {
        prepareAshworks();
        profile().baseConstruction.kitchenWaterLevel = 1;
        profile().baseConstruction.facilities[kKitchen] = BaseConstructionState::FacilityPlacement::Installed;
        initializeBaseFacilityLayouts(profile(), content); ++profile().revision; A::refresh(*app);
    }
    void repairReady() {
        prepareAshworks(); profile().baseConstruction.materialUnits = 20; ++profile().revision; A::refresh(*app);
    }
    void relayEstablished(bool staffed = false, bool disrupted = false) {
        auto &o = profile().regionalOperations.outposts.at(kRelay);
        o.established = true; o.assignedStaff = {};
        if (staffed) o.assignedStaff[0] = 2;
        o.disrupted = disrupted; o.shortcutOperationsSinceRestoration = disrupted ? 3U : 0U;
        ++profile().revision; A::refresh(*app);
    }
    void unsafe() {
        const auto weapon = equippedAsset(profile(), EquipmentSlotKind::PrimaryWeapon); ASSERT_TRUE(weapon);
        auto *record = profile().assets.findMutable(*weapon); ASSERT_NE(record, nullptr);
        record->chamberedRound.reset(); ++profile().revision; A::refresh(*app);
        ASSERT_FALSE(projectWeaponSupply(profile(), content, false).anyWeaponCanFire);
    }
    void expiringRecord() {
        const std::string id = "regional-expiring-record";
        const auto &m = content.maps().front(); profile().committedSettlements.insert(id);
        profile().lostRaidRecords.emplace(id, LostRaidRecord{id, "lost-raid", id, m.id,
            m.operationBriefing.difficulty.empty() ? "normal" : m.operationBriefing.difficulty,
            RaidResultOutcome::PlayerDead, profile().worldClock.elapsedWorldMinutes, kLostRaidRecordRetainedSettlementCount});
        static_cast<void>(profile().assets.create(content.item(alpha_content::pistol),
            LostRaidAssetLocation{id, EquipmentSlotKind::Sidearm}, 1));
        ++profile().revision; A::refresh(*app);
    }
    void differentOrdinaryMap(const MapDefinitionId &missionMap) {
        const auto it = std::find_if(content.maps().begin(), content.maps().end(), [&](const auto &m) { return m.id != missionMap; });
        ASSERT_NE(it, content.maps().end()); A::mapIndex(*app) = static_cast<std::size_t>(it - content.maps().begin());
        A::intelligence(*app).set(RaidIntelligenceCategory::Enemy, true);
    }
    void blockSave() {
        ASSERT_TRUE(SaveRepository(path).save(profile(), content.contentVersion()).succeeded);
        const auto blocked = path / "blocked-save-root"; std::ofstream(blocked) << "ordinary file";
        A::session(*app).configurePersistence(blocked);
    }
    void noRaid() {
        EXPECT_EQ(A::flow(*app).state(), GameFlowState::Base); EXPECT_FALSE(profile().pendingRaid);
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer, 10, 16, 18, 255); SDL_RenderClear(renderer);
        A::draw(*app); SDL_RenderPresent(renderer);
    }
    void wheel(SDL_FRect bounds) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = -1;
        const auto p = center(bounds); e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y;
        ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    int bright(SDL_FRect r) {
        const float scale = surface->w / 1280.0F; int result{};
        for (int y = std::max(0, static_cast<int>(r.y * scale)); y < std::min(surface->h, static_cast<int>((r.y + r.h) * scale)); ++y)
            for (int x = std::max(0, static_cast<int>(r.x * scale)); x < std::min(surface->w, static_cast<int>((r.x + r.w) * scale)); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                if (SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha) && red > 150 && green > 150 && blue > 150) ++result;
            }
        return result;
    }
};
}

TEST_F(UiRegionalOperationsTest, ThreeNativeTabsAndReturnOnlyBrowseTheExistingProfile) {
    const auto fp = profileStateFingerprint(profile()); const auto minute = profile().worldClock.elapsedWorldMinutes;
    EXPECT_EQ(A::state(*app).tab, T::Routes); draw();
    control(C::Base); EXPECT_EQ(A::state(*app).tab, T::Base); draw();
    control(C::Sweep); EXPECT_EQ(A::state(*app).tab, T::Sweep); draw();
    control(C::Routes); EXPECT_EQ(A::state(*app).tab, T::Routes);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute); noRaid();
    control(C::Close); EXPECT_FALSE(A::regional(*app)); EXPECT_EQ(A::flow(*app).activeBaseFacility(), BaseFacilityKind::RaidGate);
}

TEST_F(UiRegionalOperationsTest, NativeEstablishmentPaysNothingAndDoesNotAutomaticallyAssignAGarrison) {
    outpost(kRelay); const auto before = profileStateFingerprint(profile());
    const auto money = profile().currency; const auto minute = profile().worldClock.elapsedWorldMinutes;
    down(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), before);
    up(C::Establish); ASSERT_TRUE(profile().regionalOperations.outposts.at(kRelay).established);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
    EXPECT_FALSE(regionalOutpostOnline(profile(), content, kRelay));
    EXPECT_EQ(profile().currency, money); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute);
    const auto committed = profileStateFingerprint(profile()); up(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), committed);
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile);
    EXPECT_EQ(profileStateFingerprint(*saved.profile), committed);
}

TEST_F(UiRegionalOperationsTest, FullHealthyGarrisonAndRecallUseTheirOwnExplicitCommands) {
    relayEstablished(); outpost(kRelay); const auto minute = profile().worldClock.elapsedWorldMinutes;
    control(C::Assign); EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 2U);
    EXPECT_TRUE(A::projection(*app).selectedOutpost->online);
    const auto assigned = profileStateFingerprint(profile()); up(C::Assign); EXPECT_EQ(profileStateFingerprint(profile()), assigned);
    control(C::Recall); EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
    EXPECT_FALSE(A::projection(*app).selectedOutpost->online); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute);
}

TEST_F(UiRegionalOperationsTest, AConstructionReservationCannotBeHiddenByAnEnabledGarrisonButton) {
    relayEstablished(); profile().basePopulation.ordinaryResidents = 4; profile().basePopulation.professionResidents = {2, 1, 1, 0};
    profile().baseWorkforce = {}; profile().baseWorkforce.workshopWorker.reset(); profile().baseWorkforce.medicalWorker.reset();
    profile().baseConstruction.activeProject = ActiveBaseConstructionProject{
        BaseConstructionProjectDefinitionId{"base_construction.dormitory.level_2"}, 4, 3,
        profile().worldClock.elapsedWorldMinutes, profile().worldClock.elapsedWorldMinutes + 360};
    ++profile().revision; A::refresh(*app); outpost(kRelay);
    ASSERT_TRUE(validateProfileState(profile(), content).valid);
    const auto fp = profileStateFingerprint(profile()); EXPECT_FALSE(A::projection(*app).selectedOutpost->assign.canCommit);
    control(C::Assign); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
}

TEST_F(UiRegionalOperationsTest, TheCurrentBaseLocationCannotBeTreatedAsAnOrdinaryOutpost) {
    outpost(kGreyOutpost); ASSERT_TRUE(A::projection(*app).selectedOutpost->activeBase);
    const auto fp = profileStateFingerprint(profile());
    for (const auto c : {C::Establish, C::Assign, C::Recall, C::Restore}) {
        EXPECT_FALSE(uiRegionalOperationsEnabled(c, A::state(*app), A::projection(*app)));
        control(c); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
    }
}

TEST_F(UiRegionalOperationsTest, OldDownOnlyCallbacksCannotEstablishAssignOrCloseTheNewPage) {
    outpost(kRelay); const auto fp = profileStateFingerprint(profile());
    for (const auto c : {C::Establish, C::Assign, C::Close}) A::legacy(*app, center(uiRegionalOperationsControlRect(c)));
    EXPECT_TRUE(A::regional(*app)); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
}

TEST_F(UiRegionalOperationsTest, AnOldEstablishPressCannotBecomeAssignmentAfterTheStateChanges) {
    outpost(kRelay); down(C::Establish);
    profile().regionalOperations.outposts.at(kRelay).established = true;
    const auto fp = profileStateFingerprint(profile()); up(C::Establish);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
}

TEST_F(UiRegionalOperationsTest, SameRevisionProfileChangesInvalidateAnOldAssignmentPress) {
    relayEstablished(); outpost(kRelay); down(C::Assign);
    ++profile().currency; const auto fp = profileStateFingerprint(profile()); up(C::Assign);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
}

TEST_F(UiRegionalOperationsTest, ChangingTheSelectedStableTargetCannotRetargetAnOldPress) {
    relayEstablished(); prepareAshworks(); outpost(kRelay); down(C::Assign);
    A::state(*app).selectedOutpostId = kAshOutpost; A::refresh(*app);
    const auto fp = profileStateFingerprint(profile()); up(C::Assign); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kRelay)), 0U);
    EXPECT_EQ(assignedRegionalOutpostStaff(profile().regionalOperations.outposts.at(kAshOutpost)), 0U);
}

TEST_F(UiRegionalOperationsTest, LanguageAndTabChangesCancelUnfinishedCommands) {
    outpost(kRelay); down(C::Establish); A::text(*app).setLanguage(UiLanguage::SimplifiedChinese);
    const auto fp = profileStateFingerprint(profile()); up(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    down(C::Establish); control(C::Base); up(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiRegionalOperationsTest, FocusLeaveResizeAndMenusCancelUnfinishedCommands) {
    outpost(kRelay);
    for (const auto type : {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MOUSE_LEAVE, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED}) {
        SCOPED_TRACE(type); const auto fp = profileStateFingerprint(profile()); down(C::Establish);
        windowEvent(type); A::frame(*app); windowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED); windowEvent(SDL_EVENT_WINDOW_MOUSE_ENTER); A::frame(*app);
        up(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    }
    down(C::Establish); A::pause(*app); A::frame(*app); A::resume(*app);
    const auto fp = profileStateFingerprint(profile()); up(C::Establish); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiRegionalOperationsTest, LeavingTheButtonCancelsRatherThanExecutingOnLaterRelease) {
    outpost(kRelay); down(C::Establish); SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 4; e.motion.y = 4;
    ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app); const auto fp = profileStateFingerprint(profile()); up(C::Establish);
    EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiRegionalOperationsTest, EnterAndEscapeCannotFallThroughToOrdinaryDeployment) {
    outpost(kRelay); const auto fp = profileStateFingerprint(profile()); key(SDL_SCANCODE_RETURN); A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
    down(C::Establish); key(SDL_SCANCODE_ESCAPE); A::frame(*app); up(C::Establish);
    EXPECT_FALSE(A::regional(*app)); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
}

TEST_F(UiRegionalOperationsTest, MenuAndRegionalNavigationPreserveTheAcceptedWindowAimAnchor) {
    A::anchor(*app) = Vec2{1111, 163}; const auto anchor = *A::anchor(*app);
    control(C::Base); control(C::Sweep); control(C::Routes); draw(); control(C::Close);
    ASSERT_TRUE(A::anchor(*app)); EXPECT_EQ(A::anchor(*app)->x, anchor.x); EXPECT_EQ(A::anchor(*app)->y, anchor.y);
    EXPECT_FALSE(A::lost(*app)); open(); EXPECT_TRUE(A::regional(*app));
}

TEST_F(UiRegionalOperationsTest, MissingMigrationFacilitiesRemainAVisibleDomainFailure) {
    prepareAshworks(); site(kAshSite); const auto &p = *A::projection(*app).selectedSite;
    EXPECT_FALSE(p.migration.canCommit); EXPECT_EQ(p.migration.missingRequiredFacilities, std::vector<BaseFacilityDefinitionId>{kKitchen});
    EXPECT_FALSE(uiRegionalOperationsEnabled(C::Migrate, A::state(*app), A::projection(*app)));
    const auto fp = profileStateFingerprint(profile()); control(C::Migrate); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiRegionalOperationsTest, NativeMigrationUsesOriginalTimeUniqueCoreAndFacilityReserveExactlyOnce) {
    migrationReady(); site(kAshSite); const auto plan = A::projection(*app).selectedSite->migration; ASSERT_TRUE(plan.canCommit) << plan.message;
    const auto minute = profile().worldClock.elapsedWorldMinutes;
    const auto core = profile().regionalOperations.technologyCore.instanceId; const auto nextId = A::session(*app).nextItemInstanceId();
    down(C::Migrate); EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute); up(C::Migrate);
    EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute + plan.migrationMinutes);
    EXPECT_EQ(profile().regionalOperations.technologyCore.instanceId, core);
    EXPECT_EQ(profile().regionalOperations.technologyCore.baseSiteDefinitionId, kAshSite);
    EXPECT_EQ(A::session(*app).nextItemInstanceId(), nextId);
    EXPECT_TRUE(profile().regionalOperations.outposts.at(kGreyOutpost).established);
    EXPECT_EQ(profile().baseConstruction.facilities.at(kWorkshop), BaseConstructionState::FacilityPlacement::Reserve);
    EXPECT_EQ(A::projection(*app).activeBaseSiteId, kAshSite);
    const auto fp = profileStateFingerprint(profile()); up(C::Migrate); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile); EXPECT_EQ(profileStateFingerprint(*saved.profile), fp);
}

TEST_F(UiRegionalOperationsTest, MigrationSaveFailureDoesNotMoveCoreAdvanceTimeOrReserveFacilities) {
    migrationReady(); site(kAshSite); blockSave(); const auto fp = profileStateFingerprint(profile());
    control(C::Migrate); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile); EXPECT_EQ(profileStateFingerprint(*saved.profile), fp);
    EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiRegionalOperationsTest, RemoteFeatureRepairConsumesTheOriginalCostAndTimeWithoutActivatingItsEffect) {
    repairReady(); site(kAshSite); const auto plan = A::projection(*app).selectedSite->repair; ASSERT_TRUE(plan.canCommit) << plan.message;
    const auto minute = profile().worldClock.elapsedWorldMinutes; const auto material = profile().baseConstruction.materialUnits;
    control(C::RepairFeature); EXPECT_EQ(profile().baseConstruction.materialUnits, material - plan.materialUnits);
    EXPECT_EQ(profile().worldClock.elapsedWorldMinutes, minute + plan.durationMinutes);
    EXPECT_TRUE(profile().regionalOperations.baseSites.at(kAshSite).uniqueFeatureRepaired);
    EXPECT_TRUE(A::projection(*app).selectedSite->featureRepaired); EXPECT_FALSE(A::projection(*app).selectedSite->featureActive);
    EXPECT_EQ(activeBaseSiteManufacturingDurationPercent(profile(), content), 100U);
    const auto fp = profileStateFingerprint(profile()); control(C::RepairFeature); EXPECT_EQ(profileStateFingerprint(profile()), fp);
}

TEST_F(UiRegionalOperationsTest, FeatureRepairSaveFailurePreservesTheWholeProfileAndSavedState) {
    repairReady(); site(kAshSite); blockSave(); const auto fp = profileStateFingerprint(profile());
    control(C::RepairFeature); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile); EXPECT_EQ(profileStateFingerprint(*saved.profile), fp);
}

TEST_F(UiRegionalOperationsTest, ConstructionShortcutOnlyNavigatesAndDoesNotPurchaseAnything) {
    prepareAshworks(); site(kAshSite); const auto fp = profileStateFingerprint(profile());
    control(C::OpenConstruction); EXPECT_TRUE(A::construction(*app)); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
}

TEST_F(UiRegionalOperationsTest, ReserveShortcutDoesNotInstallOrChangeTheSavedFacility) {
    migrationReady(); site(kAshSite); control(C::Migrate); ASSERT_EQ(profile().baseConstruction.facilities.at(kWorkshop), BaseConstructionState::FacilityPlacement::Reserve);
    site(kAshSite); const auto fp = profileStateFingerprint(profile()); control(C::OpenReserve);
    EXPECT_TRUE(A::construction(*app)); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_EQ(profile().baseConstruction.facilities.at(kWorkshop), BaseConstructionState::FacilityPlacement::Reserve);
}

TEST_F(UiRegionalOperationsTest, CandidateClearanceUsesItsFrozenTargetInsteadOfTheOrdinarySelectedMap) {
    site(kAshSite); const auto missionMap = *content.regionalBaseSite(kAshSite).clearanceMapDefinitionId;
    differentOrdinaryMap(missionMap); control(C::ClearSite);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid); ASSERT_TRUE(profile().pendingRaid); const auto &r = *profile().pendingRaid;
    EXPECT_EQ(r.mapDefinitionId, missionMap); ASSERT_TRUE(r.baseSiteClearance);
    EXPECT_EQ(r.baseSiteClearance->baseSiteDefinitionId, kAshSite); EXPECT_FALSE(r.outpostRestoration); EXPECT_FALSE(r.basePerimeterSweep);
    EXPECT_FALSE(profile().regionalOperations.baseSites.at(kAshSite).unlocked);
    EXPECT_TRUE(A::intelligence(*app).empty());
}

TEST_F(UiRegionalOperationsTest, OutpostRecoveryUsesItsFrozenTargetAndDoesNotInstantlyRestoreTheShortcut) {
    relayEstablished(true, true); outpost(kRelay); const auto missionMap = content.regionalOutpost(kRelay).restorationMapDefinitionId;
    differentOrdinaryMap(missionMap); control(C::Restore);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid); ASSERT_TRUE(profile().pendingRaid); const auto &r = *profile().pendingRaid;
    EXPECT_EQ(r.mapDefinitionId, missionMap); ASSERT_TRUE(r.outpostRestoration);
    EXPECT_EQ(r.outpostRestoration->outpostDefinitionId, kRelay); EXPECT_FALSE(r.baseSiteClearance); EXPECT_FALSE(r.basePerimeterSweep);
    EXPECT_TRUE(profile().regionalOperations.outposts.at(kRelay).disrupted);
}

TEST_F(UiRegionalOperationsTest, PerimeterSweepUsesItsFrozenTargetWithoutApplyingTheSettlementReductionEarly) {
    profile().baseSiege.raidThreatUnits = kBasePerimeterSweepMinimumThreat; ++profile().revision; A::refresh(*app); control(C::Sweep);
    const auto plan = A::projection(*app).sweep; ASSERT_TRUE(plan.canDeploy) << plan.message;
    const auto before = totalBaseThreat(profile().baseSiege); differentOrdinaryMap(plan.mapDefinitionId); control(C::DeploySweep);
    ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid); ASSERT_TRUE(profile().pendingRaid); const auto &r = *profile().pendingRaid;
    EXPECT_EQ(r.mapDefinitionId, plan.mapDefinitionId); ASSERT_TRUE(r.basePerimeterSweep);
    EXPECT_EQ(r.basePerimeterSweep->baseSiteDefinitionId, plan.baseSiteDefinitionId);
    EXPECT_FALSE(r.outpostRestoration); EXPECT_FALSE(r.baseSiteClearance); EXPECT_EQ(totalBaseThreat(profile().baseSiege), before);
}

TEST_F(UiRegionalOperationsTest, UnsafeRegionalDeploymentNeedsTwoSeparateCompleteGestures) {
    site(kAshSite); unsafe(); control(C::ClearSite); noRaid(); ASSERT_TRUE(A::warning(*app)); ASSERT_TRUE(A::state(*app).confirmationKey);
    const auto fp = profileStateFingerprint(profile()); up(C::ClearSite); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
    control(C::ClearSite); EXPECT_EQ(A::flow(*app).state(), GameFlowState::Raid); ASSERT_TRUE(profile().pendingRaid->baseSiteClearance);
}

TEST_F(UiRegionalOperationsTest, TwoQueuedGesturesInOneFrameCannotConsumeTheirOwnRiskConfirmation) {
    site(kAshSite); unsafe(); const auto point = center(uiRegionalOperationsControlRect(C::ClearSite));
    pointer(point, true); pointer(point, false); pointer(point, true); pointer(point, false);
    A::frame(*app); noRaid(); ASSERT_TRUE(A::warning(*app)); ASSERT_TRUE(A::state(*app).confirmationKey);
    control(C::ClearSite); ASSERT_EQ(A::flow(*app).state(), GameFlowState::Raid);
    ASSERT_TRUE(profile().pendingRaid); EXPECT_TRUE(profile().pendingRaid->baseSiteClearance);
}

TEST_F(UiRegionalOperationsTest, AConfirmationForOutpostRecoveryCannotAuthorizeAPerimeterSweep) {
    relayEstablished(true, true); profile().baseSiege.raidThreatUnits = kBasePerimeterSweepMinimumThreat; ++profile().revision;
    outpost(kRelay); unsafe(); control(C::Restore); noRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    control(C::Sweep); EXPECT_FALSE(A::warning(*app)); control(C::DeploySweep); noRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    control(C::DeploySweep); ASSERT_TRUE(profile().pendingRaid); EXPECT_TRUE(profile().pendingRaid->basePerimeterSweep);
    EXPECT_FALSE(profile().pendingRaid->outpostRestoration);
}

TEST_F(UiRegionalOperationsTest, ExpiringLostRecordsKeepTheirOriginalRegionalRiskConfirmation) {
    site(kAshSite); expiringRecord(); control(C::ClearSite); noRaid(); ASSERT_TRUE(A::state(*app).confirmationKey);
    EXPECT_EQ(profile().lostRaidRecords.at("regional-expiring-record").subsequentRaidSettlementCount, kLostRaidRecordRetainedSettlementCount);
    control(C::ClearSite); ASSERT_TRUE(profile().pendingRaid); EXPECT_TRUE(profile().pendingRaid->baseSiteClearance);
}

TEST_F(UiRegionalOperationsTest, RegionalDeploymentSaveFailureCannotCreateOrStartAPartialRaid) {
    site(kAshSite); blockSave(); const auto fp = profileStateFingerprint(profile()); control(C::ClearSite);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid(); EXPECT_FALSE(A::message(*app).empty());
}

TEST_F(UiRegionalOperationsTest, EstablishmentSaveFailureDoesNotClaimTheOutpostOrAllocatePeople) {
    outpost(kRelay); blockSave(); const auto fp = profileStateFingerprint(profile()); control(C::Establish);
    EXPECT_EQ(profileStateFingerprint(profile()), fp); EXPECT_FALSE(profile().regionalOperations.outposts.at(kRelay).established);
    const auto saved = SaveRepository(path).load(content); ASSERT_TRUE(saved.profile); EXPECT_EQ(profileStateFingerprint(*saved.profile), fp);
}

TEST_F(UiRegionalOperationsTest, LongFailureReasonsRemainScrollableWithoutMutatingOrHidingTheirTail) {
    for (int i = 0; i < 24; ++i) A::message(*app) += "Complete regional reason including every remaining condition and resource requirement.\n";
    A::message(*app) += "FINAL REGIONAL CONSTRAINT"; const auto fp = profileStateFingerprint(profile()); draw();
    ASSERT_GT(A::state(*app).feedbackMaximum, 0); wheel(uiRegionalOperationsFeedbackRect()); EXPECT_GT(A::state(*app).feedbackScroll, 0);
    A::state(*app).feedbackScroll = A::state(*app).feedbackMaximum; draw();
    EXPECT_NE(A::message(*app).find("FINAL REGIONAL CONSTRAINT"), std::string::npos); EXPECT_EQ(profileStateFingerprint(profile()), fp);
    EXPECT_GT(bright(uiRegionalOperationsFeedbackRect()), 20);
}

TEST_F(UiRegionalOperationsTest, ProjectionIsCachedAcrossAnimationFramesButRefreshesWhenWorldTimeChanges) {
    A::refresh(*app); const auto generation = A::generation(*app);
    for (int i = 0; i < 6; ++i) { A::projection(*app); draw(); }
    EXPECT_EQ(A::generation(*app), generation);
    ++profile().worldClock.elapsedWorldMinutes; A::projection(*app); EXPECT_GT(A::generation(*app), generation);
}

TEST(UiRegionalOperationsProjectionTest, QueriesRemainPureAndDoNotInventAWholePageSiegeLock) {
    const auto &content = publishedContentRegistry(); auto profile = makeNewAlphaProfile("regional-projection", content);
    profile.baseSiege.raidThreatUnits = kBaseSiegeThreatThreshold; profile.baseSiege.warningActive = true;
    profile.baseSiege.warningRemainingSeconds = kBaseSiegeWarningSeconds;
    UiRegionalOperationsState state; state.selectedOutpostId = kRelay;
    const auto fp = profileStateFingerprint(profile); const auto p = projectUiRegionalOperations(profile, content, UiLanguage::English, state);
    ASSERT_TRUE(p.selectedOutpost); EXPECT_EQ(p.selectedOutpost->establish.canCommit,
        queryEstablishRegionalOutpost(profile, content, {kRelay}).canCommit);
    EXPECT_TRUE(p.selectedOutpost->establish.canCommit); EXPECT_TRUE(uiRegionalOperationsEnabled(C::Establish, state, p));
    EXPECT_FALSE(p.sweep.canDeploy); EXPECT_EQ(profileStateFingerprint(profile), fp);
}

TEST(UiRegionalOperationsProjectionTest, LockedRepairMetadataDoesNotTurnItsFailedQueryIntoAFreeAction) {
    const auto &content = publishedContentRegistry(); const auto profile = makeNewAlphaProfile("regional-locked-feature", content);
    UiRegionalOperationsState s; s.tab = T::Base; s.selectedSiteId = kAshSite;
    const auto p = projectUiRegionalOperations(profile, content, UiLanguage::English, s); ASSERT_TRUE(p.selectedSite);
    EXPECT_FALSE(p.selectedSite->repair.canCommit); EXPECT_GT(p.selectedSite->repairMaterialUnits, 0U);
    EXPECT_GT(p.selectedSite->repairDurationMinutes, 0U); EXPECT_FALSE(uiRegionalOperationsEnabled(C::RepairFeature, s, p));
    EXPECT_FALSE(uiRegionalOperationsEnabled(C::Migrate, s, p));
}

TEST(UiRegionalOperationsProjectionTest, AnExpiredStableSelectionCannotFallBackToAnotherActionTarget) {
    const auto &content = publishedContentRegistry(); const auto profile = makeNewAlphaProfile("regional-expired-selection", content);
    UiRegionalOperationsState s; s.selectedOutpostId = RegionalOutpostDefinitionId{"regional.missing"};
    const auto p = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    EXPECT_TRUE(p.selectionExpired); EXPECT_FALSE(p.selectedOutpost); EXPECT_FALSE(p.selectedOutpostId);
    for (const auto c : {C::Establish, C::Assign, C::Recall, C::Restore}) EXPECT_FALSE(uiRegionalOperationsEnabled(c, s, p));
    s.tab = T::Base; s.selectedSiteId = RegionalBaseSiteDefinitionId{"regional.missing"};
    const auto base = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    EXPECT_TRUE(base.selectionExpired); EXPECT_FALSE(base.selectedSite);
    for (const auto c : {C::Migrate, C::RepairFeature, C::ClearSite}) EXPECT_FALSE(uiRegionalOperationsEnabled(c, s, base));
}

TEST_F(UiRegionalOperationsTest, RepeatedClientRefreshNeverReplacesAnExpiredSelectionWithAnotherTarget) {
    const auto fingerprint = profileStateFingerprint(profile());
    const RegionalOutpostDefinitionId missingOutpost{"regional.missing"};
    A::state(*app).selectedOutpostId = missingOutpost;
    for (int iteration = 0; iteration < 3; ++iteration) {
        A::refresh(*app); draw();
        EXPECT_EQ(A::state(*app).selectedOutpostId, missingOutpost);
        EXPECT_FALSE(A::projection(*app).selectedOutpost);
    }
    const RegionalBaseSiteDefinitionId missingSite{"regional.missing"};
    A::state(*app).tab = T::Base;
    A::state(*app).selectedSiteId = missingSite;
    for (int iteration = 0; iteration < 3; ++iteration) {
        A::refresh(*app); draw();
        EXPECT_EQ(A::state(*app).selectedSiteId, missingSite);
        EXPECT_FALSE(A::projection(*app).selectedSite);
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint); noRaid();
}

TEST(UiRegionalOperationsProjectionTest, RepairedFeatureOnlyOperatesAtItsActiveLocationWithItsConsumerInstalled) {
    const auto &content = publishedContentRegistry(); auto profile = makeNewAlphaProfile("regional-feature-location", content);
    profile.regionalOperations.baseSites.at(kAshSite).unlocked = true;
    profile.regionalOperations.baseSites.at(kAshSite).uniqueFeatureRepaired = true;
    profile.regionalOperations.outposts.at(kAshOutpost).unlocked = true;
    profile.regionalOperations.outposts.at(kAshOutpost).established = true;
    UiRegionalOperationsState s; s.tab = T::Base; s.selectedSiteId = kAshSite;
    auto p = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    ASSERT_TRUE(p.selectedSite); EXPECT_TRUE(p.selectedSite->featureRepaired); EXPECT_FALSE(p.selectedSite->featureActive);
    profile.regionalOperations.activeBaseNodeId = content.regionalBaseSite(kAshSite).nodeId;
    profile.regionalOperations.technologyCore.baseSiteDefinitionId = kAshSite;
    p = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    ASSERT_TRUE(p.selectedSite); EXPECT_TRUE(p.selectedSite->featureActive);
    EXPECT_EQ(p.selectedSite->manufacturingDurationPercent, activeBaseSiteManufacturingDurationPercent(profile, content));
    profile.baseConstruction.facilities[kWorkshop] = BaseConstructionState::FacilityPlacement::Reserve;
    p = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    EXPECT_FALSE(p.selectedSite->featureActive); EXPECT_TRUE(p.selectedSite->featureRepaired);
}

TEST(UiRegionalOperationsProjectionTest, EveryVisibleSyntheticPageUsesStableIdsAndNeverHitsHiddenRows) {
    const auto &content = publishedContentRegistry(); auto profile = makeNewAlphaProfile("regional-long-list", content);
    UiRegionalOperationsState s; auto p = projectUiRegionalOperations(profile, content, UiLanguage::English, s);
    ASSERT_FALSE(p.outposts.empty()); const auto row = p.outposts.front(); p.outposts.clear();
    for (std::size_t i = 0; i < uiRegionalOperationsPageSize() * 2 + 1; ++i) {
        auto copy = row; copy.id = RegionalOutpostDefinitionId{"regional.test." + std::to_string(i)}; p.outposts.push_back(copy);
    }
    const auto pageCount = uiRegionalOperationsPageCount(p.outposts.size()); ASSERT_EQ(pageCount, 3U);
    for (std::size_t page = 0; page < pageCount; ++page) {
        s.listPage = p.listPage = page;
        for (std::size_t visible = 0; visible < uiRegionalOperationsPageSize(); ++visible) {
            const auto hit = hitUiRegionalOperations(center(uiRegionalOperationsRowRect(visible)), s, p);
            const auto index = page * uiRegionalOperationsPageSize() + visible;
            if (index < p.outposts.size()) { ASSERT_TRUE(hit); EXPECT_EQ(hit->control, C::Outpost); EXPECT_EQ(hit->outpostId, p.outposts[index].id); }
            else EXPECT_FALSE(hit);
        }
    }
}

TEST_F(UiRegionalOperationsTest, GarrisonCostTimeAndThreatDigitsActuallyPaintWithinTheirPublishedTextBoxes) {
    const auto paintedAdvance = [&](std::string_view text, int size) {
#if defined(_WIN32)
        return A::text(*app).measure(text, size);
#else
        static_cast<void>(size);
        return static_cast<float>(std::count_if(text.begin(), text.end(),
            [](unsigned char byte) { return (byte & 0xC0U) != 0x80U; }) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
#endif
    };
    const auto digits = [&](SDL_FRect bounds, const std::string &prefix, const std::string &value) {
        const auto x = bounds.x + paintedAdvance(prefix, 14);
        EXPECT_GE(x, bounds.x); EXPECT_LE(x + paintedAdvance(value, 14), bounds.x + bounds.w);
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto characterX = x + paintedAdvance(std::string_view(value).substr(0, i), 14);
            const auto width = paintedAdvance(std::string_view(value).substr(i, 1), 14);
            EXPECT_GT(bright({characterX - 1, bounds.y, width + 2, bounds.h}), 4)
                << "missing numeric glyph " << value[i] << " at " << characterX << ',' << bounds.y;
        }
    };
    repairReady(); migrationReady();
    profile().baseSiege.raidThreatUnits = kBasePerimeterSweepMinimumThreat; ++profile().revision;
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language); A::refresh(*app);
        const auto tr = [&](std::string_view value) { return localizeUiText(language, value); };
        outpost(kRelay); draw(); const auto o = *A::projection(*app).selectedOutpost;
        digits({454, 234, 748, 24}, tr("GARRISON") + " ", std::to_string(o.assignedStaff));
        digits({454, 234, 748, 24}, tr("GARRISON") + " " + std::to_string(o.assignedStaff) + "/", std::to_string(o.requiredStaff));
        digits({454, 260, 748, 24}, tr("SHORTCUT SAFE OPERATIONS") + " ", std::to_string(o.remainingSafeOperations));
        site(kAshSite); draw(); const auto siteData = *A::projection(*app).selectedSite;
        digits({454, 234, 748, 24}, tr("MIGRATION TIME") + " ", std::to_string(siteData.migrationMinutes / kWorldMinutesPerHour));
        digits({454, 260, 748, 24}, tr("REPAIR MATERIALS") + " ", std::to_string(siteData.repairMaterialUnits));
        control(C::Sweep); draw(); const auto p = A::projection(*app);
        digits({454, 234, 748, 24}, tr("BASE THREAT") + " ", std::to_string(p.threat.totalThreatUnits));
        digits({454, 260, 748, 24}, tr("REGULAR RAID THREAT") + " +", std::to_string(kBaseSiegeRaidThreatUnits));
    }
}

TEST_F(UiRegionalOperationsTest, ALongDetailTitleActuallyPaintsItsSecondLineAtEveryWindowSize) {
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language); A::refresh(*app);
        ASSERT_TRUE(A::projection(*app).selectedOutpost);
        A::projection(*app).selectedOutpost->name = "A deliberately very long regional location name with a second line that must remain readable while its operation costs stay in their own boxes";
        draw(); EXPECT_GT(bright({454, 202, 748, 26}), 12);
    }
}

TEST_F(UiRegionalOperationsTest, CriticalStatesActuallyPaintAtEveryPublishedWindowSizeAndLanguage) {
    const auto seed = profile();
    const char *output = std::getenv("RAIDLINE_UI_REGIONAL_OPERATIONS_REVIEW_DIR"); std::filesystem::path destination;
    if (output && *output) {
        destination = std::filesystem::path(output); ASSERT_TRUE(std::filesystem::equivalent(destination.parent_path(), std::filesystem::current_path()));
        const std::string prefix = "qa-ui-regional-operations-v1-"; const auto name = destination.filename().string();
        ASSERT_TRUE(name.starts_with(prefix)); ASSERT_GT(name.size(), prefix.size());
        ASSERT_TRUE(std::all_of(name.begin() + static_cast<std::ptrdiff_t>(prefix.size()), name.end(), [](char c) { return c >= '0' && c <= '9'; }));
        ASSERT_FALSE(std::filesystem::exists(destination)); ASSERT_TRUE(std::filesystem::create_directory(destination));
    }
    constexpr std::array names{"routes", "established", "online", "disrupted", "base-locked", "migration-blocked", "migration-ready", "repair-ready", "repaired-remote", "sweep-locked", "sweep-ready", "long-name", "long-reason"};
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
            SCOPED_TRACE(std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language));
            profile() = seed; A::reset(*app); A::regional(*app) = true;
            if (scenario >= 1 && scenario <= 3) {
                relayEstablished(scenario >= 2, scenario == 3); A::state(*app).selectedOutpostId = kRelay;
            }
            if (scenario >= 4 && scenario <= 8) { A::state(*app).tab = T::Base; A::state(*app).selectedSiteId = kAshSite; }
            if (scenario == 5) prepareAshworks();
            if (scenario == 6) migrationReady();
            if (scenario == 7 || scenario == 8) repairReady();
            if (scenario == 8) profile().regionalOperations.baseSites.at(kAshSite).uniqueFeatureRepaired = true;
            if (scenario == 9 || scenario == 10) A::state(*app).tab = T::Sweep;
            if (scenario == 10) profile().baseSiege.raidThreatUnits = kBasePerimeterSweepMinimumThreat;
            A::refresh(*app);
            if (scenario == 11) {
                ASSERT_TRUE(A::projection(*app).selectedOutpost);
                A::projection(*app).selectedOutpost->name = "A deliberately very long published regional location name whose full text must remain readable without hiding the operation numbers";
            }
            if (scenario == 12) for (int line = 0; line < 24; ++line) A::message(*app) += "Complete regional failure reason including the final resource and staffing constraint.\n";
            const auto fp = profileStateFingerprint(profile()); draw(); EXPECT_EQ(profileStateFingerprint(profile()), fp); noRaid();
            EXPECT_GT(bright(uiRegionalOperationsPanelRect()), 50);
            EXPECT_GT(bright({454, 234, 748, 24}), 12);
            if (!destination.empty()) ASSERT_TRUE(IMG_SavePNG(surface,
                (destination / (std::string(names[scenario]) + "-" + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".png")).string().c_str()));
        }
    }
}
