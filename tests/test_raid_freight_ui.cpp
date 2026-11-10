#include "app.h"
#include "test_temp_directory.h"
#include "ui_item_context_presentation.h"
#include "ui_item_details_presentation.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>

struct RaidFreightUiTestAccess {
    static GameFlow &flow(App &app) { return app.gameFlow_; }
    static void frame(App &app, float elapsed = 0) {
        app.processEvents();
        app.update(elapsed);
        app.input_.endFrame();
    }
    static void focus(App &app) { app.windowHasInputFocus_ = true; }
    static bool capture(const App &app) { return app.shouldCaptureWorldPointer(); }
    static bool tacticalMap(const App &app) { return app.tacticalMapOpen_; }
    static bool inventoryOpen(const App &app) { return app.inventoryOverlayState_.isOpen(); }
    static auto external(const App &app) { return app.openedRaidSceneContainerId_; }
    static void details(App &app, AssetInstanceId id) {
        app.inventoryOverlayState_.openPlayerInventory();
        app.profileDetailsAsset_ = id;
    }
    static void quickTransfer(App &app, MousePosition position) {
        app.handleProfileInventoryUiEvent(InventoryQuickTransferEvent{position}, true);
    }
    static void attach(App &app, SDL_Renderer *renderer, int width) {
        app.renderer_ = renderer;
        ASSERT_TRUE(app.uiTextRenderer_.initialize(renderer));
        app.uiTextRenderer_.setRasterScale(static_cast<float>(width) / 1280.0F);
        app.uiTextRenderer_.setLanguage(UiLanguage::SimplifiedChinese);
    }
    static void render(App &app) { app.renderRaidScreen(); }
    static void detach(App &app) {
        app.quickWheelBackdrop_.reset();
        app.indoorInformationLayer_.reset();
        app.indoorShadowMask_.reset();
        app.uiTextRenderer_.shutdown();
        app.renderer_ = nullptr;
    }
};

namespace {
using Access = RaidFreightUiTestAccess;

void key(SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    ASSERT_TRUE(SDL_PushEvent(&event));
}

class RaidFreightUiTest : public testing::Test {
protected:
    const ContentRegistry &content = publishedContentRegistry();
    raidline::test::TemporaryDirectory directory;
    std::unique_ptr<App> app;
    AssetInstanceId keyId{}, crate{}, original{};
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};

    GameSession &session() { return Access::flow(*app).gameSession(); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(directory.claim("raidline-freight-ui-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
    }
    void TearDown() override {
        if (renderer) Access::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        app.reset();
        SDL_Quit();
        ASSERT_NO_THROW(directory.cleanup());
    }
    void position(Vec2 center) {
        const float half = session().world().player().size() * 0.5F;
        ASSERT_TRUE(const_cast<Player &>(session().world().player())
            .setPosition({center.x - half, center.y - half}));
    }
    void moveTo(AssetInstanceId id, float distance = 0) {
        const auto *asset = session().profile().assets.find(id);
        ASSERT_NE(asset, nullptr);
        const auto target = std::get<RaidSceneContainerAssetLocation>(asset->location).position;
        position({target.x - distance, target.y});
        if (distance > 0) {
            GameplayInput aim;
            aim.aimWorldPosition = target;
            aim.aimWorldBounds = Rect{{}, session().world().raidSpaceWorldSize()};
            auto &world = const_cast<GameplayWorld &>(session().world());
            world.update(aim, 0);
            const auto actual = world.weaponAimWorldPosition();
            aim.aimMotionDelta = Vec2{target.x - actual.x, target.y - actual.y};
            world.update(aim, 0);
        }
        Access::frame(*app);
    }
    void deploy(bool carry) {
        auto profile = makeNewAlphaProfile("freight-ui", content);
        const auto &bagDef = content.item(ItemDefinitionId{"item.container.backpack_expedition"});
        const auto bag = profile.assets.create(bagDef, EquippedAssetLocation{EquipmentSlotKind::Backpack});
        const auto container = carry ? ProfileContainerId::compartment(bag, 0) : ProfileContainerId::stash();
        const auto &definition = content.item(kFreightCrateKeyId);
        const auto fit = findFirstProfileFit(profile, content, container, definition, ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);
        keyId = profile.assets.create(definition, StoredAssetLocation{container, *fit});
        ASSERT_TRUE(SaveRepository{directory.path()}.save(profile, content.contentVersion()).succeeded);

        app = std::make_unique<App>();
        Access::focus(*app);
        auto &flow = Access::flow(*app);
        flow.configurePersistence(directory.path());
        ASSERT_TRUE(flow.continueGame());
        ASSERT_TRUE(flow.deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
        const auto &pending = *session().profile().pendingRaid;
        ASSERT_TRUE(pending.sealedFreightCrate);
        crate = pending.sealedFreightCrate->containerAssetId;
        const auto room = std::find_if(pending.interiors.begin(), pending.interiors.end(),
            [](const auto &interior) { return interior.id == kFreightCrateSpaceId; });
        ASSERT_NE(room, pending.interiors.end());
        for (const auto &[id, asset] : session().profile().assets.records()) {
            const auto *location = std::get_if<RaidSceneContainerAssetLocation>(&asset.location);
            if (id != crate && location && location->spaceId == room->id) original = id;
        }
        ASSERT_NE(original, 0U);
        const auto entrance = room->exteriorEntrance;
        position({entrance.position.x + entrance.size.x / 2, entrance.position.y + entrance.size.y / 2});
        GameplayInput enter;
        enter.interactJustPressed = true;
        flow.update(enter, 0);
        ASSERT_EQ(session().world().activeRaidSpaceId(), kFreightCrateSpaceId);
        // Keep this input/overlay fixture independent from enemy pursuit timing.
        for (const auto &enemy : session().world().enemies())
            static_cast<void>(const_cast<Enemy &>(enemy).takeDamage(enemy.maxHealth()));
        flow.update({}, 0.01F);
    }
    void tick(int count = 1) {
        for (int step = 0; step < count; ++step) Access::frame(*app, 0.1F);
    }
    void tap(SDL_Scancode scancode) {
        key(scancode, true);
        key(scancode, false);
        Access::frame(*app);
    }
    void capture(int width, int height, const char *filename) {
        if (renderer) {
            Access::detach(*app);
            SDL_DestroyRenderer(renderer);
            SDL_DestroySurface(surface);
            renderer = nullptr;
            surface = nullptr;
        }
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        Access::attach(*app, renderer, width);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        ASSERT_TRUE(SDL_RenderClear(renderer));
        Access::render(*app);
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        if (const char *output = std::getenv("RAIDLINE_FREIGHT_UI_QA_DIR")) {
            std::filesystem::create_directories(output);
            const auto path = (std::filesystem::path{output} / filename).string();
            ASSERT_TRUE(SDL_SaveBMP(surface, path.c_str()));
        }
    }
};

TEST_F(RaidFreightUiTest, StashedKeyCannotOpenSealedBoxAndOriginalBoxStillOpensOnTap) {
    ASSERT_NO_FATAL_FAILURE(deploy(false));
    ASSERT_NO_FATAL_FAILURE(moveTo(crate, 32.0F));
    ASSERT_TRUE(session().raidFreightUnlock());
    EXPECT_FALSE(session().raidFreightUnlock()->hasKey);
    EXPECT_FALSE(session().raidSceneContainerAvailable(crate));
    ASSERT_NO_FATAL_FAILURE(capture(1024, 576, "freight-locked-1024-zh.bmp"));
    key(SDL_SCANCODE_F, true);
    tick(22);
    EXPECT_FALSE(Access::inventoryOpen(*app));
    EXPECT_FALSE(Access::external(*app));
    EXPECT_FALSE(session().profile().pendingRaid->sealedFreightCrate->unlocked);
    key(SDL_SCANCODE_F, false);
    tick();

    ASSERT_NO_FATAL_FAILURE(moveTo(original));
    tap(SDL_SCANCODE_F);
    EXPECT_TRUE(Access::inventoryOpen(*app));
    EXPECT_EQ(Access::external(*app), original);
}

TEST_F(RaidFreightUiTest, ReleaseAndMapResetProgressThenHoldOpensExactCrateAndTransfersSupply) {
    ASSERT_NO_FATAL_FAILURE(deploy(true));
    ASSERT_NO_FATAL_FAILURE(moveTo(crate, 32.0F));
    ASSERT_TRUE(session().raidFreightUnlock());
    EXPECT_TRUE(session().raidFreightUnlock()->hasKey);
    key(SDL_SCANCODE_F, true);
    tick(5);
    ASSERT_TRUE(session().raidFreightUnlock());
    EXPECT_NEAR(session().raidFreightUnlock()->progress, 0.25F, 0.001F);
    ASSERT_NO_FATAL_FAILURE(capture(1024, 576, "freight-unlock-1024-zh.bmp"));
    key(SDL_SCANCODE_F, false);
    tick();
    ASSERT_TRUE(session().raidFreightUnlock());
    EXPECT_EQ(session().raidFreightUnlock()->progress, 0.0F);

    key(SDL_SCANCODE_F, true);
    tick(5);
    tap(SDL_SCANCODE_M);
    ASSERT_TRUE(Access::tacticalMap(*app));
    ASSERT_TRUE(session().raidFreightUnlock());
    EXPECT_EQ(session().raidFreightUnlock()->progress, 0.0F);
    tap(SDL_SCANCODE_M);
    ASSERT_FALSE(Access::tacticalMap(*app));
    tick(22);
    EXPECT_FALSE(Access::inventoryOpen(*app));
    key(SDL_SCANCODE_F, false);
    tick();

    key(SDL_SCANCODE_F, true);
    tick(19);
    EXPECT_FALSE(Access::inventoryOpen(*app));
    tick(2);
    ASSERT_TRUE(Access::inventoryOpen(*app));
    EXPECT_EQ(Access::external(*app), crate);
    EXPECT_FALSE(Access::capture(*app));
    EXPECT_TRUE(session().profile().pendingRaid->sealedFreightCrate->unlocked);
    ASSERT_NO_FATAL_FAILURE(capture(1920, 1080, "freight-open-1920-zh.bmp"));

    const auto compartment = ProfileContainerId::compartment(crate, 0);
    const auto contents = assetsInContainer(session().profile(), compartment);
    ASSERT_EQ(contents.size(), 1U);
    const auto supply = contents.front()->instanceId;
    EXPECT_EQ(contents.front()->definitionId, kFreightCrateSupplyId);
    const auto origin = std::get<StoredAssetLocation>(contents.front()->location).origin;
    Access::quickTransfer(*app, {808 + origin.x * 56.0F + 4, 128 + origin.y * 56.0F + 4});
    EXPECT_TRUE(assetIsCarried(session().profile(), supply));
    EXPECT_TRUE(assetIsCarried(session().profile(), keyId));
    EXPECT_TRUE(assetsInContainer(session().profile(), compartment).empty());

    key(SDL_SCANCODE_F, false);
    tick();
    tap(SDL_SCANCODE_ESCAPE);
    EXPECT_FALSE(Access::inventoryOpen(*app));
    EXPECT_TRUE(Access::capture(*app));
    tap(SDL_SCANCODE_F);
    EXPECT_EQ(Access::external(*app), crate);
    EXPECT_TRUE(assetsInContainer(session().profile(), compartment).empty());
}

TEST_F(RaidFreightUiTest, KeyUsesBilingualExistingInspectionAndCompactGridName) {
    ASSERT_NO_FATAL_FAILURE(deploy(true));
    const auto &profile = session().profile();
    const auto actions = projectUiItemActions(profile, content, keyId, true);
    ASSERT_EQ(actions.size(), 1U);
    EXPECT_EQ(actions.front().id, UiItemAction::FreightKeyDetails);
    const auto details = projectUiItemDetails(profile, content, keyId, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(details);
    EXPECT_EQ(details->kind, UiItemDetailsKind::FreightKey);
    EXPECT_EQ(details->name, "货运器材箱钥匙");
    EXPECT_EQ(compactUiItemName(content.item(kFreightCrateKeyId), UiLanguage::SimplifiedChinese), "货箱钥匙");
    ASSERT_EQ(details->notes.size(), 2U);
    EXPECT_EQ(details->notes.front(), "边境交换站 · 货运装卸间");
    const auto english = projectUiItemDetails(profile, content, keyId, UiLanguage::English);
    ASSERT_TRUE(english);
    ASSERT_EQ(english->notes.size(), 2U);
    EXPECT_EQ(english->notes.front(), "FRONTIER EXCHANGE | FREIGHT SERVICE BAY");
    EXPECT_EQ(localizeUiText(UiLanguage::English, "SEALED FREIGHT CRATE"), "SEALED FREIGHT CRATE");
    Access::details(*app, keyId);
    ASSERT_NO_FATAL_FAILURE(capture(1024, 576, "freight-key-details-1024-zh.bmp"));
}
} // namespace
