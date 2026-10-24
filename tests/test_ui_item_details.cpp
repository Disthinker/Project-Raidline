#include "app.h"
#include "alpha_content_ids.h"
#include "ui_item_details_presentation.h"
#include "weapon_component_domain.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fmt/format.h>
#include <gtest/gtest.h>

struct UiItemDetailsTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &interaction(App &a) { return a.itemDetailsInteraction_; }
    static auto &page(App &a) { return a.deviceBatteryPage_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto &message(App &a) { return a.uiMessage_; }
    static bool dragging(App &a) { return a.deviceBatteryDrag_.has_value(); }
    static bool opened(App &a) { return a.profileDetailsAsset_.has_value(); }
    static void open(App &a, AssetInstanceId id) {
        a.inventoryOverlayState_.openPlayerInventory(); a.profileDetailsAsset_ = id;
    }
    static void pointer(App &a, InventoryPointerEventType kind, Vec2 p) {
        a.handleProfileInventoryUiEvent(InventoryPointerEvent{kind, {p.x, p.y}}, false);
    }
    static void wheel(App &a, Vec2 p, int delta) {
        a.handleProfileInventoryUiEvent(InventoryNavigationEvent{{p.x, p.y}, delta, 0, 0}, false);
    }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void draw(App &a) { a.renderProfileWeaponDetails(); }
    static void attach(App &a, SDL_Renderer *renderer, float scale) {
        a.renderer_ = renderer; ASSERT_TRUE(a.uiTextRenderer_.initialize(renderer));
        a.uiTextRenderer_.setRasterScale(scale);
        const auto root = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
        for (std::size_t i = 0; i < itemCount(); ++i) {
            const auto &d = publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i] = loadInventoryTexture(renderer, root / d.inventoryTexturePath);
        }
    }
    static void detach(App &a) {
        for (auto &texture : a.inventoryItemTextures_) texture.reset();
        a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
};

namespace {
using A = UiItemDetailsTestAccess;
using Event = InventoryPointerEventType;
const ItemDefinitionId rifle{"item.weapon.rifle_5_45_service"},
    lampDefinition{"item.component.weapon_flashlight_basic"}, cellDefinition{"item.battery.universal"};
Vec2 center(SDL_FRect r) { return {r.x + r.w / 2, r.y + r.h / 2}; }
AssetInstanceId find(const ProfileState &p, const ItemDefinitionId &definition) {
    for (const auto &[id, asset] : p.assets.records()) if (asset.definitionId == definition) return id;
    return 0;
}
class UiItemDetailsTest : public testing::Test {
protected:
    const ContentRegistry &content = publishedContentRegistry();
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    AssetInstanceId weapon{}, lamp{}, knife{}, decoy{};
    ProfileState &profile() { return const_cast<ProfileState &>(A::session(*app).profile()); }
    AssetInstanceId add(ProfileState &p, const ItemDefinitionId &id, std::uint32_t quantity = 1) {
        const auto &d = content.item(id);
        const auto fit = findFirstProfileFit(p, content, ProfileContainerId::stash(), d, ItemOrientation::Degrees0);
        EXPECT_TRUE(fit); if (!fit) return 0;
        return p.assets.create(d, StoredAssetLocation{ProfileContainerId::stash(), *fit}, quantity);
    }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path = std::filesystem::temp_directory_path() / ("raidline-final-details-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        auto p = makeNewPublishedProfile("final-details", content);
        p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        weapon = find(p, rifle);
        lamp = add(p, lampDefinition);
        knife = add(p, alpha_content::utilityKnife);
        decoy = add(p, alpha_content::noiseDecoy, 3);
        for (int i = 0; i < 12; ++i) ASSERT_NE(add(p, cellDefinition, 2), 0U);
        for (const auto &id : {ItemDefinitionId{"item.component.rail_handguard"}, lampDefinition}) {
            const auto &d = content.item(id);
            ASSERT_TRUE(executeWeaponComponentChange(p, content,
                {weapon, d.weaponComponent->slot, find(p, id)}, {p.revision, "details-fixture-" + std::string{id.value()}}).succeeded);
        }
        ASSERT_TRUE(executeDeviceBatteryChange(p, content, {lamp, find(p, cellDefinition)}, {}, {p.revision, "details-fixture-cell"}).succeeded);
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); A::frame(*app);
        attach(1280, 720); A::open(*app, weapon);
    }
    void attach(int width, int height) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, width / 1280.0F);
    }
    void detach() {
        A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
        renderer = nullptr; surface = nullptr;
    }
    void TearDown() override {
        if (app) detach(); app.reset(); SDL_Quit();
        const auto resolved = std::filesystem::weakly_canonical(path);
        ASSERT_TRUE(std::filesystem::equivalent(resolved.parent_path(), std::filesystem::temp_directory_path()));
        ASSERT_TRUE(resolved.filename().string().starts_with("raidline-final-details-"));
        std::error_code error; std::filesystem::remove_all(resolved, error);
    }
    void down(SDL_FRect r) { A::pointer(*app, Event::LeftButtonDown, center(r)); }
    void up(SDL_FRect r) { A::pointer(*app, Event::LeftButtonUp, center(r)); }
    void click(SDL_FRect r) { down(r); up(r); }
    void draw(const std::string &name) {
        SDL_SetRenderDrawColor(renderer, 11, 18, 21, 255); SDL_RenderClear(renderer);
        A::draw(*app); ASSERT_TRUE(SDL_RenderPresent(renderer));
        if (const char *directory = std::getenv("RAIDLINE_ITEM_DETAILS_QA_DIR")) {
            std::filesystem::create_directories(directory);
            ASSERT_TRUE(SDL_SaveBMP(surface, (std::filesystem::path{directory} / (name + ".bmp")).string().c_str()));
        }
    }
    int bright(SDL_FRect r, float scale) {
        int result{};
        for (int y = static_cast<int>(r.y * scale); y < static_cast<int>((r.y + r.h) * scale); ++y)
            for (int x = static_cast<int>(r.x * scale); x < static_cast<int>((r.x + r.w) * scale); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                if (red > 145 && green > 145 && blue > 145) ++result;
            }
        return result;
    }
    std::vector<bool> warningGlyphs(SDL_FRect r) {
        std::vector<bool> result;
        for (int y = static_cast<int>(r.y); y < static_cast<int>(r.y + r.h); ++y)
            for (int x = static_cast<int>(r.x); x < static_cast<int>(r.x + r.w); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{};
                EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                result.push_back(red == 238 && green == 205 && blue == 144);
            }
        return result;
    }
    void expectWarningLine(SDL_FRect bounds, std::string_view label, int size = 12) {
        const auto actual = warningGlyphs(bounds);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); ASSERT_TRUE(SDL_RenderClear(renderer));
        SDL_SetRenderDrawColor(renderer, 238, 205, 144, 255);
        A::text(*app).renderBox(renderer, bounds, label, {size, UiTextAlign::Left, 1});
        ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto expected = warningGlyphs(bounds);
        ASSERT_GT(std::count(expected.begin(), expected.end(), true), 15);
        EXPECT_EQ(actual, expected) << "The actual warning glyphs must match the full reference text";
    }
};
} // namespace

TEST_F(UiItemDetailsTest, ProjectionUsesEffectiveAttributesAndActualMeleeAndDecoyDefinitionsWithoutMutating) {
    const auto before = profileStateFingerprint(profile());
    const auto gun = projectUiItemDetails(profile(), content, weapon, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(gun); ASSERT_EQ(gun->metrics.size(), 6U);
    EXPECT_EQ(gun->metrics[0].value, fmt::format("{}", effectiveWeaponUse(profile(), content, weapon).recoilControl));
    EXPECT_EQ(gun->components.size(), 2U); EXPECT_EQ(gun->title, "枪械详情");
    const auto melee = projectUiItemDetails(profile(), content, knife, UiLanguage::English);
    ASSERT_TRUE(melee); EXPECT_EQ(melee->metrics[1].value, fmt::format("{}", content.item(alpha_content::utilityKnife).meleeUse->damage));
    const auto throwable = projectUiItemDetails(profile(), content, decoy, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(throwable); EXPECT_EQ(throwable->metrics[0].value, "3");
    EXPECT_EQ(throwable->metrics[2].value, fmt::format("{:.0f}", content.item(alpha_content::noiseDecoy).noiseDecoyUse->range));
    EXPECT_EQ(before, profileStateFingerprint(profile()));
    EXPECT_FALSE(projectUiItemDetails(profile(), content, 0, UiLanguage::English));
}

TEST_F(UiItemDetailsTest, AllThreeDetailsRenderActualTextAndAttributeValuesInBothLanguagesAtThreeWindowSizes) {
    const auto before = profileStateFingerprint(profile());
    for (const auto size : kUiWindowSizes) {
        detach(); attach(size.width, size.height);
        const float scale = size.width / 1280.0F;
        for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
            A::text(*app).setLanguage(language);
            for (const auto id : {weapon, knife, decoy}) {
                A::open(*app, id); A::message(*app).clear();
                const auto model = projectUiItemDetails(profile(), content, id, language); ASSERT_TRUE(model);
                const std::string name = std::to_string(size.width) + "-" + uiLanguageCode(language) + "-" + std::to_string(id);
                draw(name);
                EXPECT_GT(bright({70, 96, 600, 32}, scale), 40) << name;
                for (std::size_t i = 0; i < model->metrics.size(); ++i) {
                    auto box = uiItemDetailsMetric(model->kind, i);
                    box.y += model->kind == UiItemDetailsKind::Weapon ? 21 : 29;
                    box.h = 30;
                    EXPECT_GT(bright(box, scale), 4) << name << " metric " << i;
                }
                EXPECT_GT(bright(uiItemDetailsImage(model->kind), scale), 20) << name;
            }
        }
    }
    EXPECT_EQ(before, profileStateFingerprint(profile()));
}

TEST_F(UiItemDetailsTest, BatteryPagingCommitsOnlyMatchingReleaseAndCancelsMoveOutAndRepeat) {
    ASSERT_EQ(A::page(*app), 0U);
    const auto before = profileStateFingerprint(profile());
    down(uiItemDetailsNext()); down(uiItemDetailsNext()); EXPECT_EQ(A::page(*app), 0U);
    A::pointer(*app, Event::Motion, {600, 600}); up(uiItemDetailsNext()); EXPECT_EQ(A::page(*app), 0U);
    click(uiItemDetailsNext()); EXPECT_EQ(A::page(*app), 1U);
    up(uiItemDetailsNext()); EXPECT_EQ(A::page(*app), 1U);
    click(uiItemDetailsPrevious()); EXPECT_EQ(A::page(*app), 0U);
    EXPECT_EQ(before, profileStateFingerprint(profile()));
}

TEST_F(UiItemDetailsTest, RemoveBatteryRequiresReleaseAndDuplicateReleaseCannotMutateAgain) {
    const auto before = profileStateFingerprint(profile());
    const auto installed = findWeaponLight(profile(), content, weapon).batteryAssetId;
    down(uiItemDetailsRemoveBattery()); EXPECT_EQ(before, profileStateFingerprint(profile()));
    up(uiItemDetailsRemoveBattery()); EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, 0U);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile().assets.find(installed)->location));
    const auto after = profileStateFingerprint(profile()); up(uiItemDetailsRemoveBattery());
    EXPECT_EQ(after, profileStateFingerprint(profile()));
}

TEST_F(UiItemDetailsTest, DragRetainsOriginalTransactionAndChangingPageLanguageOrTargetCancelsIt) {
    const auto installed = findWeaponLight(profile(), content, weapon).batteryAssetId;
    down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
    A::page(*app) = 1; up(uiItemDetailsBatterySlot()); EXPECT_FALSE(A::dragging(*app));
    EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
    A::page(*app) = 0; down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
    A::text(*app).setLanguage(UiLanguage::English); up(uiItemDetailsBatterySlot());
    EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
    down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
    A::open(*app, knife); up(uiItemDetailsBatterySlot()); EXPECT_FALSE(A::dragging(*app));
    EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
    A::open(*app, weapon); down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
    up(uiItemDetailsBatterySlot()); EXPECT_NE(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
    EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(profile().assets.find(installed)->location));
}

TEST_F(UiItemDetailsTest, RevisionChangeRejectsCapturedSourceAndPreservesAllBatteryOwnership) {
    const auto installed = findWeaponLight(profile(), content, weapon).batteryAssetId;
    down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
    ++profile().revision;
    const auto before = profileStateFingerprint(profile());
    up(uiItemDetailsBatterySlot()); EXPECT_FALSE(A::dragging(*app));
    EXPECT_EQ(A::message(*app), "BATTERY SOURCE CHANGED");
    EXPECT_EQ(before, profileStateFingerprint(profile()));
    EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
}

TEST_F(UiItemDetailsTest, BackNeedsMatchingReleaseBlankClickStaysOpenAndDoesNotPassThrough) {
    const auto before = profileStateFingerprint(profile());
    click({600, 675, 40, 20}); EXPECT_TRUE(A::opened(*app));
    down(uiItemDetailsBack()); EXPECT_TRUE(A::opened(*app));
    A::pointer(*app, Event::Motion, {700, 120}); up(uiItemDetailsBack()); EXPECT_TRUE(A::opened(*app));
    click(uiItemDetailsBack()); EXPECT_FALSE(A::opened(*app)); EXPECT_FALSE(A::dragging(*app));
    EXPECT_FALSE(A::interaction(*app).held); EXPECT_EQ(before, profileStateFingerprint(profile()));
}

TEST_F(UiItemDetailsTest, ResizeAndMouseLeaveCancelCapturedBatteryAndRemovePress) {
    const auto installed = findWeaponLight(profile(), content, weapon).batteryAssetId;
    for (const auto eventType : {SDL_EVENT_WINDOW_RESIZED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, SDL_EVENT_WINDOW_MOUSE_LEAVE}) {
        down(uiItemDetailsRemoveBattery()); ASSERT_TRUE(A::interaction(*app).held);
        SDL_Event event{}; event.type = eventType; event.window.data1 = 1280; event.window.data2 = 720;
        ASSERT_TRUE(SDL_PushEvent(&event)); A::frame(*app);
        up(uiItemDetailsRemoveBattery());
        EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
        down(uiItemDetailsBattery(0)); ASSERT_TRUE(A::dragging(*app));
        ASSERT_TRUE(SDL_PushEvent(&event)); A::frame(*app);
        up(uiItemDetailsBatterySlot()); EXPECT_FALSE(A::dragging(*app));
        EXPECT_EQ(findWeaponLight(profile(), content, weapon).batteryAssetId, installed);
    }
}

TEST_F(UiItemDetailsTest, EmptyLightBrokenKnifeAndLongFailureRemainReadableAndReadOnly) {
    ASSERT_TRUE(A::session(*app).changeDeviceBattery({lamp, 0}, "details-empty-fixture").succeeded);
    A::message(*app) = "SPACE REQUIRED FOR REMOVED BATTERY";
    draw("empty-battery-and-feedback-zh"); expectWarningLine({70, 644, 1004, 18}, A::message(*app));
    profile().assets.findMutable(knife)->currentDurability = 0; ++profile().revision;
    A::open(*app, knife);
    const auto before = profileStateFingerprint(profile());
    const auto model = projectUiItemDetails(profile(), content, knife, UiLanguage::SimplifiedChinese);
    ASSERT_TRUE(model); EXPECT_TRUE(model->warning); EXPECT_EQ(model->metrics.front().value, "0.0 / 100.0");
    draw("broken-knife-zh"); expectWarningLine({70, 452, 1120, 31}, model->status, 16);
    A::open(*app, decoy); A::text(*app).setLanguage(UiLanguage::English);
    A::message(*app) = "Save failed. The profile remains in memory; retry using the existing return path. No successful operation was reported.";
    draw("long-failure-en"); expectWarningLine({70, 662, 1004, 18}, A::message(*app));
    EXPECT_EQ(before, profileStateFingerprint(profile()));
}

TEST_F(UiItemDetailsTest, CompleteFailureCanScrollToItsTailAndScrollingCancelsAWaitingOperation) {
    A::open(*app, weapon);
    A::message(*app).clear();
    for (int i = 0; i < 30; ++i) A::message(*app) += "A complete save failure must remain readable.\n";
    A::message(*app) += "FINAL FAILURE CONSTRAINT";
    draw("long-weapon-failure-first"); ASSERT_GT(A::interaction(*app).feedbackMaximum, 0);
    const auto before = profileStateFingerprint(profile());
    down(uiItemDetailsRemoveBattery());
    A::wheel(*app, center(uiItemDetailsFeedback(UiItemDetailsKind::Weapon)), 100);
    up(uiItemDetailsRemoveBattery());
    EXPECT_EQ(A::interaction(*app).feedbackScroll, A::interaction(*app).feedbackMaximum);
    draw("long-weapon-failure-tail"); expectWarningLine({70, 678, 1004, 18}, "FINAL FAILURE CONSTRAINT");
    EXPECT_EQ(A::message(*app).substr(A::message(*app).size() - 24), "FINAL FAILURE CONSTRAINT");
    EXPECT_EQ(before, profileStateFingerprint(profile()));
}
