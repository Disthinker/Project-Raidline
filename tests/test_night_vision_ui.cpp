#include "app.h"
#include "raid_camera.h"
#include "test_temp_directory.h"
#include "ui_app_persistence_wait.h"
#include "ui_item_context_presentation.h"
#include "ui_item_details_presentation.h"
#include "ui_raid_hud_presentation.h"
#include "weapon_component_domain.h"

#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

struct NightVisionUiTestAccess {
    static GameFlow &flow(App &app) { return app.gameFlow_; }
    static GameSession &session(App &app) { return app.gameSession_; }
    static bool paused(const App &app) { return app.pauseMenu_.isOpen(); }
    static bool capture(const App &app) { return app.shouldCaptureWorldPointer(); }
    static Vec2 offset(const App &app) { return app.wheelPointerOffset_; }
    static Vec2 reticle(const App &app) {
        return raidWorldToScreen(app.gameSession_.world().weaponAimWorldPosition(),
            app.raidWorldCameraOffset());
    }
    static void settle(App &app) {
        ASSERT_TRUE(raidline::test::waitForUiAppPersistence(app.gameSession_,
            [&] { return app.profileUiBusy() || static_cast<bool>(app.pendingInventoryBarrierAction_); },
            [&] { app.processEvents(); app.update(0); app.input_.endFrame(); }))
            << app.gameSession_.persistenceMessage();
    }
    static void frame(App &app, float elapsed = 0) {
        app.windowHasInputFocus_ = true;
        app.processEvents();
        app.update(elapsed);
        app.input_.endFrame();
        settle(app);
    }
    static void details(App &app, AssetInstanceId id) {
        app.inventoryOverlayState_.openPlayerInventory();
        app.profileDetailsAsset_ = id;
    }
    static void pointer(App &app, InventoryPointerEventType event, Vec2 at) {
        app.handleProfileInventoryUiEvent(InventoryPointerEvent{event, {at.x, at.y}}, false);
        settle(app);
    }
    static void attach(App &app, SDL_Renderer *renderer, int width, UiLanguage language) {
        app.renderer_ = renderer;
        ASSERT_TRUE(app.uiTextRenderer_.initialize(renderer));
        app.uiTextRenderer_.setRasterScale(width / 1280.0F);
        app.uiTextRenderer_.setLanguage(language);
    }
    static void drawDetails(App &app) { app.renderProfileWeaponDetails(); }
    static void drawRaid(App &app) { app.renderRaidScreen(); }
    static void detach(App &app) {
        app.quickWheelBackdrop_.reset();
        app.indoorInformationLayer_.reset();
        app.indoorShadowMask_.reset();
        app.uiTextRenderer_.shutdown();
        app.renderer_ = nullptr;
    }
};

namespace {
using A = NightVisionUiTestAccess;
using Pointer = InventoryPointerEventType;
const ItemDefinitionId helmetDefinition{"item.protective_gear.helmet_basic"};
const ItemDefinitionId batteryDefinition{"item.battery.universal"};
const ItemDefinitionId lampDefinition{"item.component.weapon_flashlight_basic"};
const MapDefinitionId hospital{"map.raid.hospital_district"};

Vec2 center(SDL_FRect bounds) { return {bounds.x + bounds.w / 2, bounds.y + bounds.h / 2}; }

AssetInstanceId find(const ProfileState &profile, const ItemDefinitionId &definition) {
    for (const auto &[id, asset] : profile.assets.records())
        if (asset.definitionId == definition) return id;
    return 0;
}
void key(SDL_Scancode scan, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scan;
    event.key.down = down;
    ASSERT_TRUE(SDL_PushEvent(&event));
}
void motion(float x, float y) {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = 640;
    event.motion.y = 360;
    event.motion.xrel = x;
    event.motion.yrel = y;
    ASSERT_TRUE(SDL_PushEvent(&event));
}
void mouse(bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = 640;
    event.button.y = 360;
    ASSERT_TRUE(SDL_PushEvent(&event));
}

class NightVisionUiTest : public testing::Test {
protected:
    const ContentRegistry &content = publishedContentRegistry();
    raidline::test::TemporaryDirectory directory;
    std::unique_ptr<App> app;
    AssetInstanceId helmet{}, nightVision{}, battery{}, rifle{}, lamp{};
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};

    GameSession &session() { return A::session(*app); }
    AssetInstanceId add(ProfileState &profile, const ItemDefinitionId &id, std::uint32_t count = 1) {
        const auto &definition = content.item(id);
        const auto fit = findFirstProfileFit(profile, content, ProfileContainerId::stash(), definition,
            ItemOrientation::Degrees0);
        EXPECT_TRUE(fit) << id.value();
        return fit ? profile.assets.create(definition,
            StoredAssetLocation{ProfileContainerId::stash(), *fit}, count) : 0;
    }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(directory.claim("raidline-night-vision-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
    }
    void TearDown() override {
        if (renderer) A::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        app.reset();
        SDL_Quit();
        ASSERT_NO_THROW(directory.cleanup());
    }
    void load(bool equippedForRaid) {
        auto profile = makeNewPublishedProfile("night-vision-ui", content);
        profile.tutorial = TutorialProgress::Complete;
        profile.homeFounding.hintsDismissed = true;
        profile.worldClock.elapsedWorldMinutes = 20 * kWorldMinutesPerHour;
        helmet = find(profile, helmetDefinition);
        if (!helmet) helmet = add(profile, helmetDefinition);
        nightVision = add(profile, kHospitalNightVisionId);
        battery = add(profile, batteryDefinition, 3);
        ASSERT_NE(helmet, 0U);
        ASSERT_NE(nightVision, 0U);
        ASSERT_NE(battery, 0U);
        if (equippedForRaid) {
            rifle = find(profile, ItemDefinitionId{"item.weapon.rifle_5_45_service"});
            ASSERT_NE(rifle, 0U);
            for (const auto &id : {ItemDefinitionId{"item.component.rail_handguard"}, lampDefinition}) {
                auto part = find(profile, id);
                if (!part) part = add(profile, id);
                ASSERT_NE(part, 0U);
                const auto installed = executeWeaponComponentChange(profile, content,
                    {rifle, content.item(id).weaponComponent->slot, part},
                    {profile.revision, std::string{id.value()}});
                ASSERT_TRUE(installed.succeeded) << installed.message;
            }
            lamp = find(profile, lampDefinition);
            ASSERT_TRUE(executeHelmetDeviceChange(profile, content, {helmet, nightVision},
                {profile.revision, "night-vision-fit"}).succeeded);
            for (const auto [id, slot] : {
                std::pair{helmet, EquipmentSlotKind::Helmet},
                std::pair{rifle, EquipmentSlotKind::PrimaryWeapon}})
                ASSERT_TRUE(executeInventory(profile, content, InventoryEquipCommand{id, slot},
                    {profile.revision, "equip-" + std::to_string(id)}).succeeded);
            for (auto id : {lamp, nightVision})
                ASSERT_TRUE(executeDeviceBatteryChange(profile, content, {id, battery}, {},
                    {profile.revision, "power-" + std::to_string(id)}).succeeded);
        }
        ASSERT_TRUE(SaveRepository(directory.path()).save(profile, content.contentVersion()).succeeded);
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(directory.path());
        ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app);
    }
    void attach(int width, int height, UiLanguage language) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, width, language);
    }
    void save(const char *name) {
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        if (const char *output = std::getenv("RAIDLINE_NIGHT_VISION_UI_QA_DIR")) {
            std::filesystem::create_directories(output);
            const auto path = (std::filesystem::path{output} / name).string();
            ASSERT_TRUE(SDL_SaveBMP(surface, path.c_str()));
        }
    }
    void click(SDL_FRect bounds) {
        A::pointer(*app, Pointer::LeftButtonDown, center(bounds));
        A::pointer(*app, Pointer::LeftButtonUp, center(bounds));
    }
    void holdU() {
        key(SDL_SCANCODE_U, true);
        A::frame(*app, .36F);
        ASSERT_TRUE(session().tacticalDeviceWheelOpen());
        EXPECT_FALSE(A::capture(*app));
    }
    void tapU() {
        key(SDL_SCANCODE_U, true);
        A::frame(*app, .03F);
        key(SDL_SCANCODE_U, false);
        A::frame(*app, .03F);
    }
};

TEST_F(NightVisionUiTest, HelmetDetailsFitSwapAndRemoveThroughDurableBaseCommands) {
    load(false);
    ASSERT_TRUE(A::flow(*app).baseWorld().canAccessStash());
    const auto actions = projectUiItemActions(session().profile(), content, helmet, false);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().id, UiItemAction::HelmetDetails);
    const auto deviceDetails = projectUiItemDetails(session().profile(), content, nightVision, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(deviceDetails);
    EXPECT_EQ(deviceDetails->title, "夜视仪详情");
    EXPECT_EQ(deviceDetails->name, "头戴夜视仪");

    A::details(*app, helmet);
    click(uiItemDetailsHelmetDeviceButton());
    ASSERT_EQ(findNightVision(session().profile(), content, helmet).deviceAssetId, nightVision);
    A::pointer(*app, Pointer::LeftButtonDown, center(uiItemDetailsBattery(0)));
    A::pointer(*app, Pointer::LeftButtonUp, center(uiItemDetailsBatterySlot()));
    const auto installed = findNightVision(session().profile(), content, helmet);
    ASSERT_NE(installed.batteryAssetId, 0U);
    const auto first = SaveRepository(directory.path()).load(content);
    ASSERT_TRUE(first.profile);
    EXPECT_EQ(findNightVision(*first.profile, content, helmet).batteryAssetId, installed.batteryAssetId);

    attach(1024, 576, UiLanguage::SimplifiedChinese);
    A::drawDetails(*app);
    save("helmet-details-1024-zh.bmp");

    const auto remove = center(uiItemDetailsHelmetDeviceButton());
    A::pointer(*app, Pointer::LeftButtonDown, remove);
    A::pointer(*app, Pointer::Motion, {600, 320});
    A::pointer(*app, Pointer::LeftButtonUp, remove);
    EXPECT_EQ(findNightVision(session().profile(), content, helmet).deviceAssetId, nightVision);
    click(uiItemDetailsHelmetDeviceButton());
    EXPECT_EQ(findNightVision(session().profile(), content, helmet).deviceAssetId, 0U);
    const auto saved = SaveRepository(directory.path()).load(content);
    ASSERT_TRUE(saved.profile);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(saved.profile->assets.find(nightVision)->location));
    const auto &cell = *saved.profile->assets.find(installed.batteryAssetId);
    EXPECT_EQ(std::get<InstalledDeviceBatteryLocation>(cell.location).deviceAssetId, nightVision);
}

TEST_F(NightVisionUiTest, WheelNeutralAndEscapeCancelThenReleaseTogglesOnlySelectedDevice) {
    load(true);
    ASSERT_TRUE(A::flow(*app).deploy(hospital));
    A::frame(*app);
    const auto anchor = A::reticle(*app);
    tapU();
    EXPECT_TRUE(session().activeWeaponLight().switchOn);
    EXPECT_FALSE(session().activeNightVision().switchOn);

    holdU();
    key(SDL_SCANCODE_U, false);
    A::frame(*app);
    EXPECT_FALSE(session().tacticalDeviceWheelOpen());
    EXPECT_TRUE(session().activeWeaponLight().switchOn);

    holdU();
    motion(130, 0);
    A::frame(*app);
    EXPECT_EQ(quickWheelSector(A::offset(*app)), kTacticalWheelVisionSector);
    key(SDL_SCANCODE_ESCAPE, true);
    key(SDL_SCANCODE_ESCAPE, false);
    A::frame(*app);
    EXPECT_FALSE(session().tacticalDeviceWheelOpen());
    EXPECT_FALSE(A::paused(*app));
    key(SDL_SCANCODE_U, false);
    A::frame(*app);
    EXPECT_FALSE(session().activeNightVision().switchOn);

    holdU();
    motion(130, 0);
    mouse(true);
    A::frame(*app);
    attach(1920, 1080, UiLanguage::SimplifiedChinese);
    A::drawRaid(*app);
    save("tactical-wheel-1920-zh.bmp");
    key(SDL_SCANCODE_U, false);
    motion(-260, 0); // Subsequent motion must not retarget a queued key release.
    A::frame(*app);
    EXPECT_FALSE(session().tacticalDeviceWheelOpen());
    EXPECT_TRUE(session().activeNightVision().switchOn);
    EXPECT_TRUE(session().activeWeaponLight().switchOn);
    EXPECT_FALSE(session().world().shotFiredLastUpdate());
    EXPECT_NEAR(A::reticle(*app).x, anchor.x, 0.01F);
    EXPECT_NEAR(A::reticle(*app).y, anchor.y, 0.01F);
    mouse(false);
    A::frame(*app);
    tapU();
    EXPECT_FALSE(session().activeNightVision().switchOn);
    EXPECT_TRUE(session().activeWeaponLight().switchOn);
}
} // namespace
