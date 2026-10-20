#include "app.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiWeaponComponentsTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &rows(App &a) { a.refreshWeaponComponents(); return a.componentRows_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.uiMessage_; }
    static auto &press(App &a) { return a.componentPress_; }
    static auto &scroll(App &a) { return a.componentDetailScroll_; }
    static auto weapon(App &a) { return a.componentWeapon_; }
    static auto choice(App &a) { return a.componentChoice_; }
    static bool action(App &a) { return a.componentActionSelected_; }
    static auto &preview(App &a) { return a.componentPreview_; }
    static auto weaponPage(App &a) { return a.componentWeaponPage_; }
    static auto choicePage(App &a) { return a.componentChoicePage_; }
    static bool opened(App &a) { return a.weaponComponentsOpen_ || a.inventoryWeaponComponentsOpen_; }
    static bool inventory(App &a) { return a.inventoryOverlayState_.isOpen(); }
    static void open(App &a, AssetInstanceId id, bool inventory = false) {
        if (inventory) { a.gameFlow_.closeBaseFacility(); a.resetBaseSupplyUi(); a.inventoryOverlayState_.openContainerInventory(); }
        ASSERT_TRUE(a.openWeaponComponentsUi(id));
        a.inventoryWeaponComponentsOpen_ = inventory; a.weaponComponentsOpen_ = !inventory;
    }
    static auto &supply(App &a) { return a.supplyUi_; }
    static auto &presentation(App &a) { return a.profilePresentation_; }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void draw(App &a) { a.renderWeaponComponents(); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_ = r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
        const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
        for (std::size_t i = 0; i < itemCount(); ++i) {
            const auto &d = publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i] = loadInventoryTexture(r, root / d.inventoryTexturePath);
        }
    }
    static void detach(App &a) {
        for (auto &t : a.inventoryItemTextures_) t.reset(); a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
};
namespace {
using A = UiWeaponComponentsTestAccess;
using Control = UiComponentControl;
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"}, barrel{"item.component.precision_barrel"},
    rail{"item.component.rail_handguard"}, grip{"item.component.vertical_grip"};
Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button = SDL_BUTTON_LEFT; e.button.down = down; e.button.x = p.x; e.button.y = p.y; ASSERT_TRUE(SDL_PushEvent(&e));
}
void key(SDL_Scancode scancode) {
    SDL_Event e{}; e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = scancode; e.key.down = true; ASSERT_TRUE(SDL_PushEvent(&e));
    e.type = SDL_EVENT_KEY_UP; e.key.down = false; ASSERT_TRUE(SDL_PushEvent(&e));
}
class UiWeaponComponentsTest : public testing::Test {
protected:
    const ContentRegistry &content = publishedContentRegistry();
    std::unique_ptr<App> app;
    std::filesystem::path path;
    SDL_Surface *surface{}; SDL_Renderer *renderer{};
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }
    AssetInstanceId find(const ItemDefinitionId &id) {
        for (const auto &[n, a] : profile().assets.records()) if (a.definitionId == id) return n;
        return 0;
    }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path = std::filesystem::temp_directory_path() / ("raidline-component-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        auto p = makeNewPublishedProfile("component-ui", content);
        p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app); ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Supply));
        attach(1280, 720); A::open(*app, find(rifle));
    }
    void attach(int w, int h) {
        surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, w / 1280.0F);
    }
    void detach() { A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); }
    void TearDown() override {
        detach(); app.reset(); SDL_Quit();
        const auto resolved = std::filesystem::canonical(path);
        ASSERT_TRUE(std::filesystem::equivalent(resolved.parent_path(), std::filesystem::temp_directory_path()));
        ASSERT_TRUE(path.filename().string().starts_with("raidline-component-ui-"));
        std::error_code ec; std::filesystem::remove_all(resolved, ec);
    }
    void click(SDL_FRect r) { const auto p = center(r); pointer(p, true); pointer(p, false); A::frame(*app); }
    void control(Control c) { click(uiComponentControl(c)); }
    void slot(WeaponComponentSlot s) { click(uiComponentSlot(static_cast<std::size_t>(s))); }
    void select(AssetInstanceId id) {
        auto rows = A::rows(*app).choices;
        auto it = std::find_if(rows.begin(), rows.end(), [id](const auto &r) { return r.id == id; });
        ASSERT_NE(it, rows.end()); const auto index = static_cast<std::size_t>(it - rows.begin());
        while (A::choicePage(*app) < index / kUiComponentPageSize) control(Control::ChoiceNext);
        while (A::choicePage(*app) > index / kUiComponentPageSize) control(Control::ChoicePrevious);
        click(uiComponentChoiceRow(index % kUiComponentPageSize)); ASSERT_TRUE(A::action(*app)); EXPECT_EQ(A::choice(*app), id);
    }
    AssetInstanceId create(const ItemDefinitionId &id) {
        const auto &d = content.item(id); const auto fit = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), d, ItemOrientation::Degrees0);
        if (!fit) { ADD_FAILURE() << "fixture Stash is full"; return 0; }
        const auto result = profile().assets.create(d, StoredAssetLocation{ProfileContainerId::stash(), *fit}); ++profile().revision; return result;
    }
    void install(const ItemDefinitionId &id) {
        const auto &d = content.item(id); const auto receipt = A::session(*app).executeBaseWeaponComponentChange(
            {find(rifle), d.weaponComponent->slot, find(id)}, "fixture-" + std::string{id.value()});
        ASSERT_TRUE(receipt.succeeded) << receipt.message;
    }
    void fullStash(bool bumpRevision = true) {
        const auto &d = content.item(ItemDefinitionId{"item.ammunition.9mm_basic"});
        for (int i = 0; i < 10000; ++i) {
            const auto fit = findFirstProfileFit(profile(), content, ProfileContainerId::stash(), d, ItemOrientation::Degrees0);
            if (!fit) { if (bumpRevision) ++profile().revision; return; }
            static_cast<void>(profile().assets.create(d, StoredAssetLocation{ProfileContainerId::stash(), *fit}, 1));
        }
        FAIL() << "unbounded fixture";
    }
    void wheel(Vec2 p, int delta) {
        SDL_Event e{}; e.type = SDL_EVENT_MOUSE_WHEEL; e.wheel.y = static_cast<float>(-delta);
        e.wheel.mouse_x = p.x; e.wheel.mouse_y = p.y; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    }
    void draw(const std::string &name) {
        SDL_SetRenderDrawColor(renderer, 13, 20, 24, 255); SDL_RenderClear(renderer); A::draw(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
        if (const char *dir = std::getenv("RAIDLINE_COMPONENT_QA_DIR")) {
            std::filesystem::create_directories(dir); ASSERT_TRUE(SDL_SaveBMP(surface, (std::filesystem::path(dir) / (name + ".bmp")).string().c_str()));
        }
    }
};
}

TEST_F(UiWeaponComponentsTest, EnterAndBrowseNeverChooseRestoreOrMutateProfile) {
    const auto before = profileStateFingerprint(profile());
    ASSERT_FALSE(A::action(*app)); EXPECT_FALSE(A::preview(*app));
    control(Control::Confirm); slot(WeaponComponentSlot::Handguard); slot(WeaponComponentSlot::Barrel);
    for (int i = 0; i < 20; ++i) draw("browse");
    EXPECT_FALSE(A::action(*app)); EXPECT_FALSE(A::preview(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, ExplicitChoicePreviewAndSingleConfirmationPersistExactlySelectedInstance) {
    const auto weapon = find(rifle), part = find(barrel); const auto before = profileStateFingerprint(profile());
    select(part); ASSERT_TRUE(A::preview(*app)); ASSERT_TRUE(A::preview(*app)->canCommit); EXPECT_EQ(profileStateFingerprint(profile()), before);
    const auto expected = A::preview(*app)->after;
    const auto point = center(uiComponentControl(Control::Confirm)); pointer(point, true); A::frame(*app);
    for (int i = 0; i < 3; ++i) A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), before); pointer(point, false); A::frame(*app);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(profile().assets.find(part)->location).weaponAssetId, weapon);
    EXPECT_EQ(effectiveWeaponUse(profile(), content, weapon), expected); EXPECT_FALSE(A::action(*app));
    const auto committed = profileStateFingerprint(profile()); pointer(point, false); A::frame(*app); EXPECT_EQ(profileStateFingerprint(profile()), committed);
    GameSession reopened; reopened.configurePersistence(path); ASSERT_TRUE(reopened.continueProfile());
    EXPECT_EQ(effectiveWeaponUse(reopened.profile(), content, weapon), expected);
}
TEST_F(UiWeaponComponentsTest, SlotAndWeaponSwitchClearPendingOperationButKeepAssets) {
    install(barrel); A::open(*app, find(rifle)); const auto before = profileStateFingerprint(profile());
    select(0); ASSERT_TRUE(A::preview(*app)->canCommit); slot(WeaponComponentSlot::Grip);
    EXPECT_FALSE(A::action(*app)); control(Control::Confirm); EXPECT_EQ(profileStateFingerprint(profile()), before);
    slot(WeaponComponentSlot::Barrel); control(Control::Confirm); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, MissingInterfaceIsExplainedAndCannotCommit) {
    slot(WeaponComponentSlot::Grip); select(find(grip)); ASSERT_TRUE(A::preview(*app));
    EXPECT_FALSE(A::preview(*app)->canCommit); EXPECT_EQ(A::preview(*app)->message, "UNDERBARREL RAIL REQUIRED");
    const auto before = profileStateFingerprint(profile()); control(Control::Confirm); draw("missing-interface"); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, RestoreListsEveryCascadingPartAndReturnsAllWithOriginalIdentity) {
    install(rail); install(grip);
    A::open(*app, find(rifle)); slot(WeaponComponentSlot::Handguard); select(0);
    ASSERT_TRUE(A::preview(*app)->canCommit); ASSERT_EQ(A::preview(*app)->returned.size(), 2);
    const auto returned = A::preview(*app)->returned;
    draw("cascade"); control(Control::Confirm);
    for (const auto &r : returned) EXPECT_EQ(std::get<StoredAssetLocation>(profile().assets.find(r.assetId)->location), r.destination);
    EXPECT_FALSE(A::action(*app));
}
TEST_F(UiWeaponComponentsTest, FullWarehouseRejectsEntireRemovalWithoutChangingAnyParticipant) {
    install(rail); install(grip); fullStash(); A::open(*app, find(rifle)); slot(WeaponComponentSlot::Handguard); select(0);
    ASSERT_TRUE(A::preview(*app)); EXPECT_FALSE(A::preview(*app)->canCommit);
    const auto before = profileStateFingerprint(profile()); control(Control::Confirm); draw("full"); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, MoveOutsideAndBackCancelsPress) {
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); const auto p = center(uiComponentControl(Control::Confirm));
    pointer(p, true); A::frame(*app); ASSERT_TRUE(A::press(*app));
    SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.x = 20; e.motion.y = 20; SDL_PushEvent(&e); A::frame(*app);
    pointer(p, false); A::frame(*app); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, WheelCancelsPressAndInvisibleSelection) {
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); const auto p = center(uiComponentControl(Control::Confirm));
    pointer(p, true); A::frame(*app); wheel({500, 400}, 1); pointer(p, false); A::frame(*app);
    EXPECT_FALSE(A::action(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, FocusLossCancelsArmedConfirmationAcrossFrames) {
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); const auto p = center(uiComponentControl(Control::Confirm));
    pointer(p, true); A::frame(*app);
    SDL_Event e{}; e.type = SDL_EVENT_WINDOW_FOCUS_LOST; SDL_PushEvent(&e); A::frame(*app);
    e.type = SDL_EVENT_WINDOW_FOCUS_GAINED; SDL_PushEvent(&e); A::frame(*app); pointer(p, false); A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, RevisionChangeInvalidatesPreviewAndNeverChangesAnotherGun) {
    select(find(barrel)); const auto p = center(uiComponentControl(Control::Confirm)); pointer(p, true); A::frame(*app);
    const auto other = create(rifle); const auto before = profileStateFingerprint(profile()); pointer(p, false); A::frame(*app);
    EXPECT_FALSE(A::action(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_NE(A::weapon(*app), other);
}
TEST_F(UiWeaponComponentsTest, DeletedSelectionWithoutRevisionCannotTurnIntoRestoreDefault) {
    select(find(barrel)); const auto p = center(uiComponentControl(Control::Confirm)); pointer(p, true); A::frame(*app);
    ASSERT_TRUE(profile().assets.erase(find(barrel))); const auto before = profileStateFingerprint(profile()); pointer(p, false); A::frame(*app);
    EXPECT_FALSE(A::action(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, MissingWeaponNeverFallsBackToFirstSameModel) {
    const auto other = create(rifle); A::open(*app, other); select(find(barrel));
    ASSERT_TRUE(profile().assets.erase(other)); ++profile().revision; draw("missing-weapon");
    EXPECT_EQ(A::weapon(*app), 0); EXPECT_FALSE(A::action(*app)); const auto before = profileStateFingerprint(profile());
    control(Control::Confirm); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, PreviewChangedWithoutRevisionMustBeChosenAgain) {
    install(barrel); A::open(*app, find(rifle)); select(0); ASSERT_TRUE(A::preview(*app)->canCommit);
    fullStash(false); const auto before = profileStateFingerprint(profile()); control(Control::Confirm);
    EXPECT_FALSE(A::action(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
    EXPECT_EQ(A::message(*app), "COMPONENT STATE CHANGED - SELECT AGAIN");
}
TEST_F(UiWeaponComponentsTest, FailedSaveKeepsProfileAndTargetIntact) {
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); const auto blocked = path / "blocked";
    { std::ofstream file(blocked); file << "blocked"; } A::session(*app).configurePersistence(blocked);
    control(Control::Confirm); EXPECT_EQ(profileStateFingerprint(profile()), before); EXPECT_TRUE(A::action(*app));
    EXPECT_FALSE(A::message(*app).empty()); draw("save-failed");
}
TEST_F(UiWeaponComponentsTest, EveryWeaponAndComponentInstanceAcrossIndependentPagesIsReachable) {
    for (int i = 0; i < 7; ++i) { create(rifle); create(barrel); }
    A::open(*app, find(rifle)); const auto before = profileStateFingerprint(profile());
    const auto weapons = A::rows(*app).weapons;
    for (std::size_t i = 0; i < weapons.size(); ++i) {
        while (A::weaponPage(*app) < i / 6) control(Control::WeaponNext);
        click(uiComponentWeaponRow(i % 6)); EXPECT_EQ(A::weapon(*app), weapons[i].id); EXPECT_FALSE(A::action(*app));
    }
    const auto choices = A::rows(*app).choices;
    for (const auto &c : choices) select(c.id);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    control(Control::WeaponPrevious); EXPECT_FALSE(A::action(*app)); EXPECT_EQ(A::choicePage(*app), 1);
}
TEST_F(UiWeaponComponentsTest, ReturnPreservesSupplyTabPageAndDropsUnconfirmedAction) {
    A::supply(*app).tab = UiBaseSupplyTab::Recycle; A::supply(*app).pages[1] = 1;
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); control(Control::Back);
    EXPECT_FALSE(A::opened(*app)); EXPECT_EQ(A::supply(*app).tab, UiBaseSupplyTab::Recycle); EXPECT_EQ(A::supply(*app).pages[1], 1);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, InventoryInputReturnsToSameViewportAndEscapeDiscardsPreview) {
    A::open(*app, find(rifle), true); A::presentation(*app).remember(ProfileContainerId::stash(), {8, 8});
    select(find(barrel)); const auto before = profileStateFingerprint(profile()); key(SDL_SCANCODE_ESCAPE); A::frame(*app);
    EXPECT_FALSE(A::opened(*app)); EXPECT_TRUE(A::inventory(*app)); EXPECT_EQ(A::presentation(*app).originFor(ProfileContainerId::stash()), (GridPosition{8, 8}));
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, TabClosesInventoryAndLateReleaseCannotConfirm) {
    A::open(*app, find(rifle), true); select(find(barrel)); const auto before = profileStateFingerprint(profile());
    const auto p = center(uiComponentControl(Control::Confirm)); pointer(p, true); A::frame(*app); key(SDL_SCANCODE_TAB); A::frame(*app);
    EXPECT_FALSE(A::opened(*app)); EXPECT_FALSE(A::inventory(*app)); pointer(p, false); A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, IdleRenderingReusesRowsAndPreviewWithoutCloningAgain) {
    install(barrel); A::open(*app, find(rifle)); select(0); ASSERT_FALSE(A::preview(*app)->returned.empty());
    const auto *returns = A::preview(*app)->returned.data(); const auto *rows = A::rows(*app).weapons.data();
    const auto before = profileStateFingerprint(profile());
    for (int i = 0; i < 25; ++i) { draw("cached"); EXPECT_EQ(A::preview(*app)->returned.data(), returns); EXPECT_EQ(A::rows(*app).weapons.data(), rows); }
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, ReturnCoordinatesUseIsolatedWarehousePageMapping) {
    EXPECT_EQ(uiComponentStashDestination(profile(), content, {8, 0}), "STASH PAGE 2 (1, 1)");
    EXPECT_EQ(uiComponentStashDestination(profile(), content, {0, 8}), "STASH PAGE 4 (1, 1)");
}
TEST_F(UiWeaponComponentsTest, ThreeWindowSizesAndBothLanguagesRenderAllImportantStates) {
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const auto suffix = std::to_string(size.width) + (language == UiLanguage::SimplifiedChinese ? "-zh" : "-en");
        A::open(*app, find(rifle)); draw("current-" + suffix);
        select(find(barrel)); draw("preview-" + suffix);
        slot(WeaponComponentSlot::Grip); select(find(grip)); draw("rejected-" + suffix);
        A::message(*app) = std::string(12, ' ') + "STASH SPACE REQUIRED FOR ALL REMOVED COMPONENTS\n" + std::string(250, 'W');
        draw("long-reason-" + suffix); wheel(center(uiComponentDetails()), 3); draw("scrolled-" + suffix);
    }
    install(rail); install(grip);
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const auto suffix = std::to_string(size.width) + (language == UiLanguage::SimplifiedChinese ? "-zh" : "-en");
        A::open(*app, find(rifle)); slot(WeaponComponentSlot::Handguard); select(0); draw("cascade-" + suffix);
        wheel(center(uiComponentDetails()), 3); draw("cascade-scrolled-" + suffix);
    }
    for (int i = 0; i < 7; ++i) { create(rifle); create(barrel); }
    for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        detach(); attach(size.width, size.height); A::text(*app).setLanguage(language);
        const auto suffix = std::to_string(size.width) + (language == UiLanguage::SimplifiedChinese ? "-zh" : "-en");
        A::open(*app, find(rifle)); draw("paged-first-" + suffix);
        control(Control::WeaponNext); control(Control::ChoiceNext); draw("paged-last-" + suffix);
    }
}

TEST_F(UiWeaponComponentsTest, MouseLeaveInSameBatchDiscardsQueuedInventoryPress) {
    A::open(*app, find(rifle), true); select(find(barrel)); const auto before = profileStateFingerprint(profile());
    const auto p = center(uiComponentControl(Control::Confirm)); pointer(p, true);
    SDL_Event e{}; e.type = SDL_EVENT_WINDOW_MOUSE_LEAVE; ASSERT_TRUE(SDL_PushEvent(&e)); A::frame(*app);
    EXPECT_FALSE(A::press(*app)); pointer(p, false); A::frame(*app); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiWeaponComponentsTest, SameModelDifferentInstanceReceivesOnlyItsOwnConfirmedChange) {
    const auto original = find(rifle), second = create(rifle), part = find(barrel);
    A::open(*app, second); select(part); control(Control::Confirm);
    EXPECT_EQ(std::get<InstalledWeaponComponentLocation>(profile().assets.find(part)->location).weaponAssetId, second);
    EXPECT_EQ(effectiveWeaponUse(profile(), content, original), content.item(rifle).weaponUse.value());
    EXPECT_TRUE(validateProfileState(profile(), content).valid);
}

TEST_F(UiWeaponComponentsTest, LightAndItsBatteryKeepIdentityThroughCascadingRemoval) {
    install(rail); install(grip);
    ItemDefinitionId lightDef, cellDef;
    for (const auto &d : content.items()) {
        if (d.weaponComponent && d.weaponComponent->slot == WeaponComponentSlot::Light && d.weaponComponent->weaponDefinitionId == rifle) lightDef = d.definitionId;
        if (d.battery) cellDef = d.definitionId;
    }
    const auto light = find(lightDef) ? find(lightDef) : create(lightDef);
    const auto cell = find(cellDef) ? find(cellDef) : create(cellDef);
    A::open(*app, find(rifle)); slot(WeaponComponentSlot::Light); select(light); ASSERT_TRUE(A::preview(*app)->canCommit); control(Control::Confirm);
    ASSERT_TRUE(A::session(*app).changeDeviceBattery({light, cell}, "fixture-light-battery").succeeded);
    slot(WeaponComponentSlot::Handguard); select(0); ASSERT_TRUE(A::preview(*app)->canCommit); EXPECT_EQ(A::preview(*app)->returned.size(), 3);
    control(Control::Confirm);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile().assets.find(light)->location));
    EXPECT_EQ(std::get<InstalledDeviceBatteryLocation>(profile().assets.find(cell)->location).deviceAssetId, light);
    EXPECT_TRUE(validateProfileState(profile(), content).valid);
}
TEST_F(UiWeaponComponentsTest, EmptyWeaponListIsReadableAndDoesNotSendEmptyCommand) {
    ASSERT_TRUE(A::session(*app).executeProfileEconomy(RecycleCommand{find(rifle)}, "fixture-empty-weapons").succeeded);
    A::open(*app, 0); EXPECT_TRUE(A::rows(*app).weapons.empty()); const auto before = profileStateFingerprint(profile());
    for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
        A::text(*app).setLanguage(language); draw(language == UiLanguage::English ? "empty-en" : "empty-zh"); control(Control::Confirm);
    }
    EXPECT_FALSE(A::action(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
}
