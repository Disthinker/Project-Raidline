#include "app.h"
#include "test_temp_directory.h"
#include "alpha_content_ids.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>

struct UiFinalContainersTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static void frame(App &a) { a.windowHasInputFocus_ = true; a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void attach(App &a, SDL_Renderer *renderer, float scale) {
        a.renderer_ = renderer; ASSERT_TRUE(a.uiTextRenderer_.initialize(renderer));
        a.uiTextRenderer_.setRasterScale(scale);
        const auto assets = std::filesystem::path{__FILE__}.parent_path().parent_path() / "assets";
        for (std::size_t i = 0; i < itemCount(); ++i) {
            const auto &d = publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i] = loadInventoryTexture(renderer, assets / d.inventoryTexturePath);
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid());
        }
        a.playerTexture_ = loadInventoryTexture(renderer, assets / "characters/protagonist_left_minimal_256x320.png");
        ASSERT_TRUE(a.playerTexture_.valid());
    }
    static void detach(App &a) {
        for (auto &texture : a.inventoryItemTextures_) texture.reset();
        a.playerTexture_.reset(); a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
    static void language(App &a, UiLanguage language) { a.uiTextRenderer_.setLanguage(language); }
    static void draw(App &a, bool raid, std::optional<AssetInstanceId> external) {
        a.uiMessage_.clear(); a.renderProfileInventory(false, raid, external);
    }
    static void titleReference(App &a, const char *title) {
        const int size = a.uiTextRenderer_.language() == UiLanguage::English &&
            std::string_view{title} == "BASE GROUND CONTAINER" ? 13 : 16;
        EXPECT_LE(a.uiTextRenderer_.measure(localizeUiText(a.uiTextRenderer_.language(), title), size), 190);
        SDL_SetRenderDrawColor(a.renderer_, 216, 228, 224, 255);
        a.uiTextRenderer_.renderBox(a.renderer_, {808, 64, 190, 24}, title, {size, UiTextAlign::Left, 1});
    }
    static void counterReference(App &a, UiLanguage language) {
        const auto label = language == UiLanguage::English ? "COMP. 1/1" : "储物格 1/1";
        EXPECT_LE(a.uiTextRenderer_.measure(label, 14), 112);
        SDL_SetRenderDrawColor(a.renderer_, 164, 177, 190, 255);
        a.uiTextRenderer_.renderBox(a.renderer_, {1016, 66, 112, 20}, label, {14, UiTextAlign::Center, 1});
    }
    static void openBase(App &a, AssetInstanceId id) { a.openBasePlacedFacility(id); }
    static auto externalBase(App &a) { return a.openedBaseGroundContainerId_; }
    static auto externalRaid(App &a) { return a.openedRaidSceneContainerId_; }
    static void probe(App &a, const AssetRecord &asset) {
        SDL_Surface *sample = SDL_CreateSurface(96, 32, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(sample, nullptr);
        ASSERT_TRUE(SDL_FillSurfaceRect(sample, nullptr, SDL_MapSurfaceRGBA(sample, 255, 0, 0, 255)));
        auto *texture = SDL_CreateTextureFromSurface(a.renderer_, sample); SDL_DestroySurface(sample); ASSERT_NE(texture, nullptr);
        a.inventoryItemTextures_[static_cast<std::size_t>(ItemId::Rifle)] = Texture{texture};
        a.renderProfileAsset(asset, {100, 100, 194, 112}, 56, 255, false, true, false);
    }
};

namespace {
using A = UiFinalContainersTestAccess;
class UiFinalContainersTest : public testing::Test {
protected:
    raidline::test::TemporaryDirectory saveDirectory;
    std::unique_ptr<App> app;
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    std::filesystem::path path;
    AssetInstanceId rifle{}, container{};
    const ContentRegistry &content = publishedContentRegistry();
    auto &session() { return A::session(*app); }
    ProfileState &profile() { return const_cast<ProfileState &>(session().profile()); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        ASSERT_NO_THROW(saveDirectory.claim("raidline-final-containers-",
            testing::UnitTest::GetInstance()->current_test_info()->name()));
        path = saveDirectory.path();
        auto p = makeNewAlphaProfile("final-containers", content);
        p.tutorial = TutorialProgress::Complete; p.homeFounding.hintsDismissed = true;
        for (const auto &[id, asset] : p.assets.records()) if (asset.definitionId == alpha_content::rifle) { rifle = id; break; }
        ASSERT_NE(rifle, 0U);
        ASSERT_TRUE(executeInventory(p, content, InventoryEquipCommand{rifle, EquipmentSlotKind::PrimaryWeapon}, {p.revision, "container-fixture-rifle"}).succeeded);
        static_cast<void>(p.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}), EquippedAssetLocation{EquipmentSlotKind::Backpack}));
        ASSERT_TRUE(validateProfileState(p, content).valid);
        ASSERT_TRUE(SaveRepository(path).save(p, content.contentVersion()).succeeded);
        app = std::make_unique<App>(); A::flow(*app).configurePersistence(path); ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app);
    }
    void attach(UiWindowSize size) {
        surface = SDL_CreateSurface(size.width, size.height, SDL_PIXELFORMAT_RGBA32); ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer, size.width / 1280.0F);
    }
    void detach() {
        A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface); renderer = nullptr; surface = nullptr;
    }
    void TearDown() override {
        if (renderer) detach(); app.reset(); SDL_Quit();
        ASSERT_NO_THROW(saveDirectory.cleanup());
    }
    void position(Vec2 center) {
        const auto half = session().world().player().size() / 2;
        ASSERT_TRUE(const_cast<Player &>(session().world().player()).setPosition({center.x - half, center.y - half}));
    }
    void deployAndEnterScene() {
        ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));
        const auto interior = profile().pendingRaid->interiors.front();
        position({interior.exteriorEntrance.position.x + interior.exteriorEntrance.size.x / 2,
                  interior.exteriorEntrance.position.y + interior.exteriorEntrance.size.y / 2});
        GameplayInput input; input.interactJustPressed = true; A::flow(*app).update(input, 0);
        ASSERT_EQ(session().world().activeRaidSpaceId(), interior.id);
        for (const auto &[id, asset] : profile().assets.records())
            if (const auto *scene = std::get_if<RaidSceneContainerAssetLocation>(&asset.location); scene && scene->spaceId == interior.id) { container = id; break; }
        ASSERT_NE(container, 0U);
        position(std::get<RaidSceneContainerAssetLocation>(profile().assets.find(container)->location).position);
        A::frame(*app);
    }
    void openRaidContainer() {
        ASSERT_TRUE(session().canOpenRaidSceneContainer(container));
        SDL_Event event{}; event.type = SDL_EVENT_KEY_DOWN; event.key.scancode = SDL_SCANCODE_F; event.key.down = true;
        ASSERT_TRUE(SDL_PushEvent(&event)); event.type = SDL_EVENT_KEY_UP; event.key.down = false; ASSERT_TRUE(SDL_PushEvent(&event));
        A::frame(*app); ASSERT_EQ(A::externalRaid(*app), container);
    }
    void makeCorpse() {
        const auto interior = profile().pendingRaid->interiors.front();
        position({interior.interiorExit.position.x + interior.interiorExit.size.x / 2,
                  interior.interiorExit.position.y + interior.interiorExit.size.y / 2});
        GameplayInput input; input.interactJustPressed = true; A::flow(*app).update(input, 0);
        ASSERT_EQ(session().world().activeRaidSpaceId(), outdoorRaidSpaceId());
        const auto &snapshots = profile().pendingRaid->enemies;
        const auto source = std::find_if(snapshots.begin(), snapshots.end(), [](const auto &enemy) { return !enemy.sourceEnemyId.empty(); });
        ASSERT_NE(source, snapshots.end());
        const auto &actors = session().world().enemies();
        const auto actor = std::find_if(actors.begin(), actors.end(), [&](const auto &enemy) { return enemy.position().x == source->position.x && enemy.position().y == source->position.y; });
        ASSERT_NE(actor, actors.end()); static_cast<void>(const_cast<Enemy &>(*actor).takeDamage(actor->maxHealth()));
        A::flow(*app).update({}, 0.01F); container = 0;
        for (const auto &[id, asset] : profile().assets.records())
            if (const auto *body = std::get_if<RaidCorpseContainerAssetLocation>(&asset.location); body && body->deathPosition) {
                container = id; position(*body->deathPosition); break;
            }
        ASSERT_NE(container, 0U); A::frame(*app);
    }
    std::vector<Uint32> pixels(SDL_FRect bounds, float scale, bool titleMask = false,
                              SDL_Color ink = {216, 228, 224, 255}) {
        std::vector<Uint32> result;
        for (int y = static_cast<int>(bounds.y * scale); y < static_cast<int>((bounds.y + bounds.h) * scale); ++y)
            for (int x = static_cast<int>(bounds.x * scale); x < static_cast<int>((bounds.x + bounds.w) * scale); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{}; EXPECT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                result.push_back(titleMask ? static_cast<Uint32>(red == ink.r && green == ink.g && blue == ink.b) : (Uint32{red} << 24) | (Uint32{green} << 16) | (Uint32{blue} << 8) | alpha);
            }
        return result;
    }
    void clear() { SDL_SetRenderDrawColor(renderer, 8, 15, 20, 255); ASSERT_TRUE(SDL_RenderClear(renderer)); }
    void empty(bool raid) {
        const auto bag = equippedAsset(profile(), EquipmentSlotKind::Backpack); ASSERT_TRUE(bag);
        const auto destination = ProfileContainerId::compartment(*bag, 0);
        std::vector<AssetInstanceId> roots;
        for (const auto *asset : assetsInContainer(profile(), ProfileContainerId::compartment(container, 0)))
            roots.push_back(asset->instanceId);
        for (const auto id : roots) {
            const auto *asset = profile().assets.find(id); ASSERT_NE(asset, nullptr);
            const auto fit = findFirstProfileFit(profile(), content, destination, content.item(asset->definitionId), asset->orientation);
            ASSERT_TRUE(fit) << "The isolated expedition bag must fit this existing container item";
            const InventoryCommand command = InventoryMoveCommand{id, 0, {destination, *fit}};
            const auto receipt = raid
                ? session().executeRaidSceneContainerInventory(container, command, "final-container-empty-" + std::to_string(id))
                : A::flow(*app).executeBaseGroundContainerInventory(container, command, "final-container-empty-" + std::to_string(id));
            ASSERT_TRUE(receipt.succeeded) << receipt.message;
        }
        EXPECT_TRUE(assetsInContainer(profile(), ProfileContainerId::compartment(container, 0)).empty());
        ASSERT_TRUE(validateProfileState(profile(), content).valid) << validateProfileState(profile(), content).message;
    }
    void matrix(const char *name, const char *title, const char *chinese, bool raid) {
        ASSERT_FALSE(assetsInContainer(profile(), ProfileContainerId::compartment(container, 0)).empty());
        ASSERT_EQ(content.item(profile().assets.find(container)->definitionId).containerCompartments.size(), 1U);
        for (const bool isEmpty : {false, true}) {
            if (isEmpty) empty(raid);
            const auto fingerprint = profileStateFingerprint(profile());
            for (const auto size : kUiWindowSizes) for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
                attach(size); A::language(*app, language); const auto scale = size.width / 1280.0F;
                clear(); A::draw(*app, raid, std::nullopt); ASSERT_TRUE(SDL_RenderPresent(renderer));
                const auto left = pixels({32, 100, 754, 532}, scale);
                clear(); A::draw(*app, raid, container); ASSERT_TRUE(SDL_RenderPresent(renderer));
                EXPECT_EQ(left, pixels({32, 100, 754, 532}, scale)) << name << " must not cover the accepted loadout";
                const auto actual = pixels({808, 64, 190, 24}, scale, true);
                const auto counter = pixels({1004, 64, 136, 24}, scale, true, {164, 177, 190, 255});
                ASSERT_GT(std::count(actual.begin(), actual.end(), 1U), 10) << name;
                if (language == UiLanguage::SimplifiedChinese) EXPECT_EQ(localizeUiText(language, title), chinese);
                if (const char *directory = std::getenv("RAIDLINE_UI_FINAL_CONTAINERS_REVIEW_DIR")) {
                    std::filesystem::create_directories(directory);
                    const auto filename = std::string{name} + (isEmpty ? "-empty-" : "-filled-") + std::to_string(size.width) + "-" + uiLanguageCode(language) + ".bmp";
                    ASSERT_TRUE(SDL_SaveBMP(surface, (std::filesystem::path{directory} / filename).string().c_str()));
                }
                clear(); A::titleReference(*app, title); ASSERT_TRUE(SDL_RenderPresent(renderer));
                EXPECT_EQ(actual, pixels({808, 64, 190, 24}, scale, true)) << "real title must match an unobscured reference";
                clear(); A::counterReference(*app, language); ASSERT_TRUE(SDL_RenderPresent(renderer));
                const auto expectedCounter = pixels({1004, 64, 136, 24}, scale, true, {164, 177, 190, 255});
                ASSERT_GT(std::count(expectedCounter.begin(), expectedCounter.end(), 1U), 10);
                EXPECT_EQ(counter, expectedCounter) << "The complete compartment count 1/1 must actually paint without ellipsis";
                EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
                detach();
            }
        }
    }
};
} // namespace

TEST_F(UiFinalContainersTest, RealSceneStorageFilledAndEmptyRenderWithoutCoveringLoadoutAtEverySizeAndLanguage) {
    deployAndEnterScene(); openRaidContainer(); matrix("scene", "SCENE STORAGE", "场景储物箱", true);
}

TEST_F(UiFinalContainersTest, RealCorpseFilledAndEmptyRenderWithoutCoveringLoadoutAtEverySizeAndLanguage) {
    deployAndEnterScene(); makeCorpse(); openRaidContainer(); matrix("corpse", "CORPSE", "尸体", true);
}

TEST_F(UiFinalContainersTest, BaseGroundStorageFilledAndEmptyUsesOriginalAccessAndPaintsEverySizeAndLanguage) {
    auto &world = A::flow(*app).baseWorld();
    const Vec2 center{world.playerPosition().x + world.playerSize().x / 2, world.playerPosition().y + world.playerSize().y / 2};
    container = profile().assets.create(content.item(ItemDefinitionId{"item.container.base_storage_crate"}),
        BaseGroundAssetLocation{RegionalBaseSiteDefinitionId{world.siteDefinitionId()}, {center.x + 40, center.y}});
    static_cast<void>(profile().assets.create(content.item(alpha_content::ammunition), StoredAssetLocation{ProfileContainerId::compartment(container, 0), {0, 0}}, 12));
    ++profile().revision;
    ASSERT_TRUE(validateProfileState(profile(), content).valid);
    ASSERT_TRUE(A::flow(*app).queryBaseGroundContainerAccess(container).canCommit);
    A::openBase(*app, container); ASSERT_EQ(A::externalBase(*app), container);
    matrix("base-ground", "BASE GROUND CONTAINER", "基地地面容器", false);
}

TEST_F(UiFinalContainersTest, ProductionAssetDrawingRetainsWideImageAspectAtEveryPublishedWindowSize) {
    const auto fingerprint = profileStateFingerprint(profile());
    for (const auto size : kUiWindowSizes) {
        attach(size); clear(); A::probe(*app, *profile().assets.find(rifle)); ASSERT_TRUE(SDL_RenderPresent(renderer));
        const auto scale = size.width / 1280.0F;
        int left = size.width, right = 0, top = size.height, bottom = 0;
        for (int y = static_cast<int>(100 * scale); y < static_cast<int>(212 * scale); ++y)
            for (int x = static_cast<int>(100 * scale); x < static_cast<int>(294 * scale); ++x) {
                Uint8 red{}, green{}, blue{}, alpha{}; ASSERT_TRUE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
                if (red > 240 && green < 10 && blue < 10) { left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y); }
            }
        ASSERT_GT(right, left); ASSERT_GT(bottom, top);
        EXPECT_NEAR(static_cast<float>(right - left + 1) / (bottom - top + 1), 3.0F, 0.08F);
        EXPECT_GT(top, static_cast<int>(110 * scale)); EXPECT_LT(bottom, static_cast<int>(202 * scale));
        EXPECT_EQ(profileStateFingerprint(profile()), fingerprint); detach();
    }
}
