#include "app.h"
#include "alpha_content_ids.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <SDL3_image/SDL_image.h>

struct UiItemContextTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &view(App &a) { return a.profilePresentation_; }
    static auto &menu(App &a) { return a.profileContextMenu_; }
    static auto &hover(App &a) { return a.profileItemHover_; }
    static auto &card(App &a) { return a.profileItemCard_; }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static auto details(App &a) { return a.profileDetailsAsset_; }
    static auto gesture(App &a) { return a.profileInventoryInteraction_.pointerGestureActive(); }
    static bool guard(App &a) { return a.profileMenuReleaseGuard_; }
    static const auto &message(App &a) { return a.uiMessage_; }
    static void open(App &a) {
        a.inventoryOverlayState_.openContainerInventory();
        a.profilePresentationProfileId_ = a.gameSession_.profile().profileId;
    }
    static void point(App &a, MousePosition p, float dt = 0) {
        a.pointerWorldPosition_ = Vec2{p.x, p.y};
        a.profilePresentationPointer_ = p;
        a.updateProfileItemContext(dt);
    }
    static void tick(App &a, float dt) { a.updateProfileItemContext(dt); }
    static void right(App &a, MousePosition p) { a.handleProfileRightClick(p, false); }
    static void event(App &a, InventoryUiEvent event) { a.handleProfileInventoryUiEvent(event, false); }
    static void cancel(App &a) { a.handleInventoryCancel(); }
    static void close(App &a) { a.closeInventory(); }
    static bool inventory(App &a) { return a.inventoryOverlayState_.isOpen(); }
    static void attach(App &a, SDL_Renderer *r) {
        a.renderer_ = r;
        ASSERT_TRUE(a.uiTextRenderer_.initialize(r));
    }
    static void detach(App &a) {
        for (auto &texture : a.inventoryItemTextures_) texture.reset();
        a.uiTextRenderer_.shutdown(); a.renderer_ = nullptr;
    }
    static void loadArt(App &a) {
        const auto root=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        for(std::size_t i=0;i<itemCount();++i) {
            const auto &definition=publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i]=Texture{IMG_LoadTexture(a.renderer_,(root/definition.inventoryTexturePath).string().c_str())};
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid()) << definition.inventoryTexturePath;
        }
    }
    static void drawInventory(App &a) { a.renderProfileInventory(true, false); }
    static void clock(App &a, float seconds) { a.uiClockSeconds_ += seconds; }
    static void drawMenu(App &a) { a.renderProfileContextMenu(false); }
    static void drawCard(App &a) { a.renderProfileItemCard(true, false); }
    static auto supply(App &a, AssetInstanceId id, bool raid = false) {
        return a.weaponSupplyTooltipLines(id, true, raid);
    }
    static void focus(App &a, bool value) { a.windowHasInputFocus_ = value; }
};
namespace {
using A = UiItemContextTestAccess;
float measure(std::string_view text, int size) {
    float width{};
    for (const unsigned char c : text)
        if ((c & 0xc0) != 0x80) width += c < 0x80 ? size * .6F : float(size);
    return width;
}
void inside(SDL_FRect inner, SDL_FRect outer = {0, 0, 1280, 720}) {
    EXPECT_GE(inner.x, outer.x); EXPECT_GE(inner.y, outer.y);
    EXPECT_LE(inner.x + inner.w, outer.x + outer.w + .01F);
    EXPECT_LE(inner.y + inner.h, outer.y + outer.h + .01F);
}
bool has(const std::vector<std::string> &lines, std::string_view text) {
    return std::any_of(lines.begin(), lines.end(), [&](const auto &s) {
        return s.find(text) != std::string::npos;
    });
}
class UiItemContextTest : public testing::Test {
protected:
    std::unique_ptr<App> app;
    std::filesystem::path path;
    bool ownsDirectory{};
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    AssetInstanceId rifle{}, bag{}, ammo{}, magazine{}, medkit{};
    auto &session() { return A::flow(*app).gameSession(); }
    const auto &profile() { return session().profile(); }
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned n = 0; n < 100 && !ownsDirectory; ++n) {
            path = std::filesystem::temp_directory_path() /
                ("raidline-ui-item-" + std::to_string(seed) + "-" + std::to_string(n));
            std::error_code ec;
            ownsDirectory = std::filesystem::create_directory(path, ec);
            ASSERT_FALSE(ec) << ec.message();
        }
        ASSERT_TRUE(ownsDirectory);
        const auto &content = publishedContentRegistry();
        auto p = makeNewAlphaProfile("ui-item-context", content);
        p.assets = AssetRegistry{};
        p.currentHealth = 70;
        rifle = p.assets.create(content.item(ItemDefinitionId{"item.weapon.rifle_5_45_service"}),
            EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon});
        bag = p.assets.create(content.item(ItemDefinitionId{"item.container.backpack_expedition"}),
            EquippedAssetLocation{EquipmentSlotKind::Backpack});
        ammo = p.assets.create(content.item(alpha_content::ammunition),
            StoredAssetLocation{ProfileContainerId::stash(), {2, 3}}, 20);
        magazine = p.assets.create(content.item(alpha_content::magazine),
            StoredAssetLocation{ProfileContainerId::stash(), {5, 0}});
        medkit = p.assets.create(content.item(alpha_content::medkit),
            StoredAssetLocation{ProfileContainerId::stash(), {0, 0}});
        ASSERT_TRUE(validateProfileState(p, content).valid) << validateProfileState(p, content).message;
        const auto saved = SaveRepository{path}.save(p, content.contentVersion());
        ASSERT_TRUE(saved.succeeded) << saved.message;
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());
        A::open(*app);
    }
    void TearDown() override {
        if (app) A::detach(*app);
        SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
        app.reset(); SDL_Quit();
        // Delete only the exclusively claimed, resolved child of the temp root.
        if (ownsDirectory && path.lexically_normal().parent_path() ==
            std::filesystem::temp_directory_path().lexically_normal() &&
            path.filename().string().starts_with("raidline-ui-item-")) {
            std::error_code ec; std::filesystem::remove_all(path, ec);
        }
    }
    MousePosition row(UiItemAction action) {
        EXPECT_TRUE(A::menu(*app));
        if (!A::menu(*app)) return {};
        const auto &menu = *A::menu(*app);
        for (std::size_t i = 0; i < menu.actions.size(); ++i)
            if (menu.actions[i].id == action) {
                const auto r = menu.layout.rows[i].bounds;
                return {r.x + r.w / 2, r.y + r.h / 2};
            }
        ADD_FAILURE() << "Missing original action"; return {};
    }
    void pointer(InventoryPointerEventType type, MousePosition p) {
        A::event(*app, InventoryPointerEvent{type, p});
    }
    void click(MousePosition p) {
        pointer(InventoryPointerEventType::LeftButtonDown, p);
        pointer(InventoryPointerEventType::LeftButtonUp, p);
    }
    void drawSetup(int width = 1280, int height = 720) {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface); ASSERT_NE(renderer, nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer, 1280, 720, SDL_LOGICAL_PRESENTATION_LETTERBOX));
        A::attach(*app, renderer);
        A::text(*app).setRasterScale(width / 1280.0F);
    }
};

TEST_F(UiItemContextTest, DetailsExecuteOnMatchingReleaseOnceWithoutMutatingProfile) {
    const auto before = profileStateFingerprint(profile());
    A::right(*app, {63.093F, 378.0F}); const auto p = row(UiItemAction::WeaponDetails);
    pointer(InventoryPointerEventType::LeftButtonDown, p);
    EXPECT_FALSE(A::details(*app)); EXPECT_FALSE(A::gesture(*app));
    pointer(InventoryPointerEventType::LeftButtonDown, p);
    EXPECT_FALSE(A::details(*app));
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_EQ(A::details(*app), rifle); EXPECT_FALSE(A::menu(*app));
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    EXPECT_FALSE(A::flow(*app).baseWorld().shotFiredLastUpdate());
}
TEST_F(UiItemContextTest, ReleaseOutsideOrOnAnotherRowDoesNotExecute) {
    A::right(*app, {63.093F, 378.0F});
    const auto first = row(UiItemAction::WeaponDetails);
    pointer(InventoryPointerEventType::LeftButtonDown, first);
    pointer(InventoryPointerEventType::LeftButtonUp, row(UiItemAction::ModifyWeapon));
    EXPECT_FALSE(A::details(*app)); EXPECT_TRUE(A::menu(*app));
    pointer(InventoryPointerEventType::LeftButtonDown, first);
    pointer(InventoryPointerEventType::LeftButtonUp, {10, 710});
    EXPECT_FALSE(A::details(*app)); EXPECT_FALSE(A::gesture(*app));
}
TEST_F(UiItemContextTest, OutsideClickIsConsumedEvenOverAnotherItem) {
    const auto before = profileStateFingerprint(profile());
    A::right(*app, {63.093F, 378.0F}); click({927.0F, 313.5F});
    EXPECT_FALSE(A::menu(*app)); EXPECT_FALSE(A::gesture(*app));
    EXPECT_FALSE(A::guard(*app)); EXPECT_EQ(profileStateFingerprint(profile()), before);
    pointer(InventoryPointerEventType::LeftButtonDown, {927.0F, 313.5F});
    EXPECT_TRUE(A::gesture(*app));
}
TEST_F(UiItemContextTest, MenuBlocksQuickTransferAndPartialClickCannotStartDrag) {
    const auto before = profileStateFingerprint(profile());
    A::right(*app, {63.093F, 378.0F});
    A::event(*app, InventoryQuickTransferEvent{MousePosition{927.0F, 313.5F}});
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    const auto p = row(UiItemAction::WeaponDetails);
    A::event(*app, InventoryPartialTransferEvent{p, true, false});
    EXPECT_FALSE(A::gesture(*app)); EXPECT_FALSE(A::details(*app));
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_EQ(A::details(*app), rifle);
}
TEST_F(UiItemContextTest, EscapeCancelsHeldActionAndConsumesItsRelease) {
    A::right(*app, {63.093F, 378.0F}); const auto p = row(UiItemAction::WeaponDetails);
    pointer(InventoryPointerEventType::LeftButtonDown, p); A::cancel(*app);
    EXPECT_FALSE(A::menu(*app)); EXPECT_TRUE(A::inventory(*app));
    pointer(InventoryPointerEventType::LeftButtonUp, {927.0F, 313.5F});
    EXPECT_FALSE(A::guard(*app)); EXPECT_FALSE(A::details(*app)); EXPECT_FALSE(A::gesture(*app));
}
TEST_F(UiItemContextTest, PageAndCarriedScrollDoNotKeepOldMenuOrHover) {
    A::point(*app, {927.0F, 313.5F}, .5F); ASSERT_TRUE(A::hover(*app).visible());
    A::right(*app, {63.093F, 378.0F}); EXPECT_FALSE(A::hover(*app).visible());
    A::event(*app, InventoryNavigationEvent{{836.0F, 138.5F}, 1, 0, 0});
    EXPECT_FALSE(A::menu(*app)); A::tick(*app, 0); EXPECT_FALSE(A::hover(*app).visible());
    A::event(*app, InventoryNavigationEvent{{300,300},0,1,0}); EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    EXPECT_FALSE(A::hover(*app).visible());
}
TEST_F(UiItemContextTest, MovedTargetCannotExecuteStaleMenu) {
    A::right(*app, {63.093F, 378.0F}); const auto p = row(UiItemAction::WeaponDetails);
    pointer(InventoryPointerEventType::LeftButtonDown, p);
    const auto &c = publishedContentRegistry();
    const auto fit = findFirstProfileFit(profile(), c, ProfileContainerId::stash(),
        c.item(profile().assets.find(rifle)->definitionId), ItemOrientation::Degrees0);
    ASSERT_TRUE(fit);
    ASSERT_TRUE(session().executeProfileInventory(
        InventoryMoveCommand{rifle, 0, {ProfileContainerId::stash(), *fit}}, "menu-target-moved").succeeded);
    const auto after = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_FALSE(A::menu(*app)); EXPECT_FALSE(A::details(*app));
    EXPECT_EQ(profileStateFingerprint(profile()), after);
}
TEST_F(UiItemContextTest, EmptyMagazineAffordanceAndDomainFailureArePreserved) {
    const auto before = profileStateFingerprint(profile());
    A::right(*app, {1091.5F, 138.5F});
    ASSERT_TRUE(A::menu(*app));
    click(row(UiItemAction::UnloadMagazine));
    EXPECT_FALSE(A::menu(*app)); EXPECT_FALSE(A::message(*app).empty());
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}
TEST_F(UiItemContextTest, MedicalActionRunsOnlyAfterReleaseAndCannotRepeat) {
    A::right(*app, {818.5F, 138.5F}); const auto p = row(UiItemAction::UseMedical);
    const auto before = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, p);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_GT(profile().currentHealth, 70);
    const auto after = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonUp, p);
    EXPECT_EQ(profileStateFingerprint(profile()), after);
}
TEST_F(UiItemContextTest, HoverDelayAndStableAnchorSurviveMotionInsideItem) {
    A::point(*app, {927.0F, 313.5F}); A::tick(*app, .39F);
    EXPECT_FALSE(A::hover(*app).visible());
    const auto anchor = A::hover(*app).anchor;
    A::point(*app, {932.25F, 322.25F}, .02F); EXPECT_TRUE(A::hover(*app).visible());
    EXPECT_EQ(A::hover(*app).instanceId, ammo);
    EXPECT_FLOAT_EQ(A::hover(*app).anchor.x, anchor.x);
    A::point(*app, {1091.5F, 138.5F}); EXPECT_FALSE(A::hover(*app).visible());
    EXPECT_EQ(A::hover(*app).instanceId, magazine);
}
TEST_F(UiItemContextTest, DragMenuFocusLossAndCloseHideHoverImmediately) {
    A::point(*app, {927.0F, 313.5F}, .5F); ASSERT_TRUE(A::hover(*app).visible());
    pointer(InventoryPointerEventType::LeftButtonDown, {927.0F, 313.5F});
    A::tick(*app, .5F); EXPECT_FALSE(A::hover(*app).visible());
    A::right(*app, {63.093F, 378.0F}); EXPECT_FALSE(A::menu(*app));
    A::cancel(*app); A::point(*app, {63.093F, 378.0F}, .5F);
    ASSERT_TRUE(A::hover(*app).visible()); A::focus(*app, false); A::tick(*app, 0);
    EXPECT_FALSE(A::hover(*app).visible());
    A::focus(*app, true); A::point(*app, {63.093F, 378.0F}, .5F); A::close(*app);
    EXPECT_FALSE(A::hover(*app).visible()); EXPECT_FALSE(A::menu(*app));
}
TEST_F(UiItemContextTest, LanguageChangeInvalidatesMeasuredMenuAndHover) {
    A::right(*app, {63.093F, 378.0F}); A::text(*app).setLanguage(UiLanguage::English);
    A::tick(*app, 0); EXPECT_FALSE(A::menu(*app));
    A::point(*app, {927.0F, 313.5F}, .5F); ASSERT_TRUE(A::hover(*app).visible());
    A::text(*app).setLanguage(UiLanguage::SimplifiedChinese); A::tick(*app, 0);
    EXPECT_FALSE(A::hover(*app).visible());
}
TEST_F(UiItemContextTest, FocusLossDuringMenuPressDoesNotSwallowTheNextFreshClick) {
    A::right(*app, {63.093F, 378.0F});
    pointer(InventoryPointerEventType::LeftButtonDown, row(UiItemAction::WeaponDetails));
    A::focus(*app, false); A::tick(*app, 0);
    EXPECT_FALSE(A::menu(*app)); EXPECT_FALSE(A::guard(*app));
    A::focus(*app, true); A::right(*app, {63.093F, 378.0F});
    ASSERT_TRUE(A::menu(*app)); click(row(UiItemAction::WeaponDetails));
    EXPECT_EQ(A::details(*app), rifle);
}
TEST_F(UiItemContextTest, WeaponCardContainsEverySupplyLineAndOneImageWithoutProfileWrites) {
    drawSetup(); const auto before = profileStateFingerprint(profile());
    A::point(*app, {63.093F, 378.0F}, .6F); A::drawCard(*app);
    const auto lines = A::supply(*app, rifle);
    ASSERT_FALSE(lines.empty());
    for (const auto &line : lines)
        EXPECT_EQ(std::count_if(A::card(*app).text.begin(), A::card(*app).text.end(),
            [&](const auto &block) { return block.text == line; }), 1);
    inside(A::card(*app).panel);
    EXPECT_EQ(profileStateFingerprint(profile()), before);
    const auto labels = A::text(*app).cachedLabelCount();
    for (int i = 0; i < 30; ++i) { A::tick(*app, .01F); A::drawCard(*app); }
    EXPECT_EQ(A::text(*app).cachedLabelCount(), labels);
}
TEST_F(UiItemContextTest, DrawingRestoresClipBlendScaleAndColour) {
    drawSetup(); A::point(*app, {63.093F, 378.0F}, .6F);
    SDL_Rect clip{0, 0, 1260, 700}; SDL_SetRenderClipRect(renderer, &clip);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer, 11, 22, 33, 44);
    float x{}, y{}; SDL_GetRenderScale(renderer, &x, &y);
    A::drawCard(*app); A::right(*app, {63.093F, 378.0F}); A::drawMenu(*app);
    SDL_Rect after{}; SDL_GetRenderClipRect(renderer, &after);
    EXPECT_EQ(after.x, clip.x); EXPECT_EQ(after.w, clip.w); EXPECT_EQ(after.h, clip.h);
    SDL_BlendMode blend{}; SDL_GetRenderDrawBlendMode(renderer, &blend); EXPECT_EQ(blend, SDL_BLENDMODE_ADD);
    Uint8 r{}, g{}, b{}, a{}; SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    EXPECT_EQ(r, 11); EXPECT_EQ(g, 22); EXPECT_EQ(b, 33); EXPECT_EQ(a, 44);
    float ax{}, ay{}; SDL_GetRenderScale(renderer, &ax, &ay);
    EXPECT_FLOAT_EQ(ax, x); EXPECT_FLOAT_EQ(ay, y);
}
TEST_F(UiItemContextTest, PublishedSummariesAndCurrentSupplySnapshotFitInBothLanguages) {
    drawSetup();
    for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        A::text(*app).setLanguage(language);
        const UiItemMeasure actualMeasure = [&](auto text, int size) { return A::text(*app).measure(text, size); };
        for (const auto &definition : publishedContentRegistry().items()) {
            AssetRecord record; record.definitionId = definition.definitionId;
            const auto summary = projectUiItemSummary(record, definition);
            const auto supply = definition.weaponUse ? A::supply(*app, rifle, true) : std::vector<std::string>{};
            const auto layout = layoutUiItemCard({1160, 610, 64, 64}, definition.displayName,
                summary, supply, language, actualMeasure);
            inside(layout.panel);
            for (const auto &block : layout.text) inside(block.bounds, layout.panel);
        }
    }
}

TEST_F(UiItemContextTest, MenuAndHoverDoNotWriteTheIsolatedSave) {
    const auto bytes = [&] {
        std::map<std::filesystem::path, std::string> files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(path))
            if (entry.is_regular_file()) {
                std::ifstream stream(entry.path(), std::ios::binary);
                files[entry.path().lexically_relative(path)] = std::string{
                    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
            }
        return files;
    };
    const auto before = bytes(); ASSERT_FALSE(before.empty());
    A::point(*app, {63.093F, 378.0F}, .6F); A::right(*app, {63.093F, 378.0F}); A::cancel(*app);
    A::event(*app, InventoryNavigationEvent{{836.0F, 138.5F}, 1, 0, 0});
    A::point(*app, {927.0F, 313.5F}, 1); A::close(*app);
    EXPECT_EQ(bytes(), before);
}
TEST_F(UiItemContextTest, WeaponDurabilityUsesTheAcceptedEquipmentDisplayUnits) {
    const auto *asset = profile().assets.find(rifle); ASSERT_NE(asset, nullptr);
    const auto &definition = publishedContentRegistry().item(asset->definitionId);
    const auto before = profileStateFingerprint(profile());
    const auto summary = projectUiItemSummary(*asset, definition);
    EXPECT_TRUE(has(summary, "DURABILITY 100.00/100.00"));
    EXPECT_FALSE(has(summary, "10000/10000"));
    EXPECT_EQ(profileStateFingerprint(profile()), before);
}

TEST_F(UiItemContextTest, SoftwarePreviewUsesAllThreeWindowScalesAndBothLanguages) {
    for (const auto size : kUiWindowSizes)
        for (const auto language : {UiLanguage::SimplifiedChinese, UiLanguage::English}) {
            drawSetup(size.width, size.height); A::loadArt(*app);
            A::text(*app).setLanguage(language);
            SDL_SetRenderDrawColor(renderer, 8, 12, 18, 255); SDL_RenderClear(renderer);
            A::point(*app, {63.093F, 378.0F}); A::tick(*app, .6F); A::drawInventory(*app);
            SDL_RenderPresent(renderer);
            const auto save = [&](const char *kind) {
#if defined(_WIN32)
                const auto *dir = _wgetenv(L"RAIDLINE_ITEM_CONTEXT_QA_DIR");
#else
                const auto *dir = std::getenv("RAIDLINE_ITEM_CONTEXT_QA_DIR");
#endif
                if (dir) {
                    const auto output = std::filesystem::path{dir};
                    std::filesystem::create_directories(output);
                    const auto name = std::string{kind} + "-" + uiLanguageCode(language) +
                        "-" + std::to_string(size.width) + ".bmp";
                    const auto utf8 = (output / name).u8string();
                    EXPECT_TRUE(SDL_SaveBMP(surface, reinterpret_cast<const char *>(utf8.c_str()))) << SDL_GetError();
                }
            };
            save("weapon-card");
            A::right(*app, {63.093F, 378.0F}); ASSERT_TRUE(A::menu(*app));
            auto &menu = *A::menu(*app);
            menu.layout = layoutUiItemContext({1279, 719}, menu.layout.title.text,
                menu.actions, language,
                [&](auto text, int fontSize) { return A::text(*app).measure(text, fontSize); });
            const auto p = row(UiItemAction::ModifyWeapon);
            A::point(*app, p); A::drawInventory(*app); A::clock(*app, .13F);
            A::drawInventory(*app); SDL_RenderPresent(renderer);
            inside(menu.layout.panel); save("edge-menu");
            A::cancel(*app);
            A::detach(*app); SDL_DestroyRenderer(renderer); SDL_DestroySurface(surface);
            renderer = nullptr; surface = nullptr;
        }
}

TEST_F(UiItemContextTest, EveryExistingSupplyStatusAndSuggestionRemainsReadable) {
    drawSetup();
    const UiItemMeasure actualMeasure = [&](auto text, int size) { return A::text(*app).measure(text, size); };
    WeaponSupplyProjection supply;
    WeaponSupplyReadiness weapon;
    weapon.definitionKnown = true; weapon.slot = EquipmentSlotKind::PrimaryWeapon;
    weapon.chamberedRounds = 1; weapon.installedMagazineRounds = 17;
    weapon.carriedLooseRounds = 120; weapon.quickReloadMagazineId = 51;
    weapon.quickReloadMagazineRounds = 23; weapon.quickReloadCanInstall = false;
    weapon.otherLoadedSpareMagazineCount = 2; weapon.otherLoadedSpareMagazineRounds = 45;
    for (auto kind : {WeaponSupplySuggestionKind::LoadMagazine,
                     WeaponSupplySuggestionKind::InstallMagazine,
                     WeaponSupplySuggestionKind::ChamberWeapon,
                     WeaponSupplySuggestionKind::MoveSpareToChestRig,
                     WeaponSupplySuggestionKind::ClearMalfunction,
                     WeaponSupplySuggestionKind::MaintainWeapon})
        weapon.suggestions.push_back({kind});
    for (auto result : {WeaponAmmoResult::Dry, WeaponAmmoResult::Broken,
                       WeaponAmmoResult::BlockedByMalfunction, WeaponAmmoResult::Chambered}) {
        weapon.fireResult = result;
        const auto lines = projectUiWeaponSupplyLines(weapon, supply, true,
                                                       EquipmentSlotKind::SecondaryWeapon);
        EXPECT_TRUE(has(lines, "CURRENT CHAMBER 1 | INSTALLED MAG 17 | CARRIED LOOSE 120"));
        EXPECT_TRUE(has(lines, "R CANDIDATE: 23 ROUNDS | CANNOT INSTALL"));
        EXPECT_TRUE(has(lines, "OTHER LOADED SPARES 2 / 45 ROUNDS | NOT R-READY"));
        EXPECT_TRUE(has(lines, "SELECT THIS WEAPON"));
        EXPECT_TRUE(has(lines, "OPTIONAL: CARRY MEDICINE"));
        EXPECT_TRUE(has(lines, "OPTIONAL: EQUIP BODY ARMOR"));
        EXPECT_TRUE(has(lines, "DRAG COMPATIBLE AMMO"));
        EXPECT_TRUE(has(lines, "DRAG THE MAGAZINE"));
        EXPECT_TRUE(has(lines, "MOVE A LOADED SPARE"));
        EXPECT_TRUE(has(lines, "SHAKE THE MOUSE"));
        EXPECT_TRUE(has(lines, "MAINTENANCE KIT"));
        if (result == WeaponAmmoResult::Broken) EXPECT_TRUE(has(lines, "BROKEN WEAPON"));
        if (result == WeaponAmmoResult::BlockedByMalfunction) EXPECT_TRUE(has(lines, "JAMMED"));
        if (result == WeaponAmmoResult::Chambered) EXPECT_TRUE(has(lines, "CHAMBER EMPTY"));
        for (auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
            const auto layout = layoutUiItemCard({1200, 640, 64, 64},
                "A long name for a rifle with an installed weapon light and magazine",
                {"ITEM SIZE 5 x 2 | QUANTITY 1", "WEIGHT 3.00 KG", "DURABILITY 80/100"},
                lines, language, actualMeasure);
            inside(layout.panel);
            EXPECT_EQ(layout.text.size(), lines.size() + 4);
            for (const auto &block : layout.text) inside(block.bounds, layout.panel);
        }
    }
    weapon.canFireNow = true;
    EXPECT_TRUE(has(projectUiWeaponSupplyLines(weapon, supply, false, weapon.slot), "FIRE READY NOW"));
    weapon.definitionKnown = false;
    EXPECT_TRUE(has(projectUiWeaponSupplyLines(weapon, supply, false, weapon.slot), "UNKNOWN WEAPON"));
    weapon.quickReloadMagazineId.reset();
    EXPECT_TRUE(has(projectUiWeaponSupplyLines(weapon, supply, false, weapon.slot), "R CANDIDATE: NONE"));
}

TEST(UiItemContextLayoutTest, CornersLongLabelsAndThreeWindowSizesUseSameHitRectangles) {
    const std::vector<UiItemActionEntry> actions{
        {UiItemAction::WeaponDetails, "WEAPON DETAILS"},
        {UiItemAction::ModifyWeapon, "MODIFY WEAPON"},
        {UiItemAction::UnloadMagazine, "UNLOAD MAGAZINE (3 SEC)"}};
    for (auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese})
        for (auto point : {Vec2{0,0}, Vec2{1279,0}, Vec2{0,719}, Vec2{1279,719}}) {
            const auto layout = layoutUiItemContext(point,
                "A long weapon name with an installed flashlight and magazine", actions, language, measure);
            inside(layout.panel);
            for (std::size_t i = 0; i < layout.rows.size(); ++i) {
                inside(layout.rows[i].bounds, layout.panel);
                const auto r = layout.rows[i].bounds;
                for (auto size : kUiWindowSizes) {
                    const auto v = UiViewport::fromWindow(size.width, size.height);
                    const auto logical = v.toLogical(v.toWindow({r.x + 4, r.y + 4}));
                    EXPECT_EQ(layout.rowAt(logical), i);
                }
            }
            EXPECT_FALSE(layout.rowAt({layout.title.bounds.x, layout.title.bounds.y}));
        }
}
TEST(UiItemContextLayoutTest, FailedOrRepeatedReleaseCannotChooseAnAction) {
    UiItemMenuPress press;
    EXPECT_FALSE(press.up(UiItemAction::WeaponDetails));
    press.down(UiItemAction::WeaponDetails);
    press.down(UiItemAction::ModifyWeapon);
    EXPECT_FALSE(press.up(UiItemAction::ModifyWeapon));
    press.down(UiItemAction::WeaponDetails);
    EXPECT_EQ(press.up(UiItemAction::WeaponDetails), UiItemAction::WeaponDetails);
    EXPECT_FALSE(press.up(UiItemAction::WeaponDetails));
}
TEST(UiItemContextLayoutTest, NewLabelsHaveChineseTranslations) {
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "ITEM SIZE 2 x 3 | QUANTITY 4"),
              "占格 2 x 3 | 数量 4");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "REMAINING USES 3"), "剩余次数 3");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese, "ITEM ACTION NO LONGER AVAILABLE"),
              "物品状态已变化，请重新打开菜单");
}
}
