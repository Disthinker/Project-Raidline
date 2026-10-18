#include "app.h"
#include "alpha_content_ids.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <fstream>
#include <SDL3_image/SDL_image.h>

struct InventoryPresentationUiTestAccess
{
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &view(App &a) { return a.profilePresentation_; }
    static auto &gesture(App &a) { return a.profileInventoryInteraction_; }
    static auto menu(App &a) { return a.profileContextMenu_; }
    static void event(App &a, InventoryUiEvent e)
    {
        a.handleProfileInventoryUiEvent(e, false);
    }
    static void raidEvent(App &a, InventoryUiEvent e)
    {
        a.handleProfileInventoryUiEvent(e, true);
    }
    static void right(App &a, MousePosition p)
    {
        a.handleProfileRightClick(p, false);
    }
    static void open(App &a)
    {
        a.inventoryOverlayState_.openContainerInventory();
        a.profilePresentationProfileId_ = a.gameSession_.profile().profileId;
    }
    static void cancel(App &a) { a.handleInventoryCancel(); }
    static void close(App &a) { a.closeInventory(); }
    static void hover(App &a, float dt)
    {
        a.updateProfilePresentationHover(dt);
    }
    static void frame(App &a)
    {
        a.processEvents();
        a.update(0);
        a.input_.endFrame();
    }
    static bool isOpen(const App &a)
    {
        return a.inventoryOverlayState_.isOpen();
    }
    static void attach(App &a, SDL_Renderer *r)
    {
        a.renderer_ = r;
        ASSERT_TRUE(a.uiTextRenderer_.initialize(r));
    }
    static void detach(App &a)
    {
        for (auto &t : a.inventoryItemTextures_)
            t.reset();
        a.uiTextRenderer_.shutdown();
        a.renderer_ = nullptr;
    }
    static void draw(App &a, UiLanguage language)
    {
        a.uiTextRenderer_.setLanguage(language);
        a.renderProfileInventory(true, false);
    }
    static void texture(App &a, SDL_Texture *texture)
    {
        a.inventoryItemTextures_[static_cast<std::size_t>(ItemId::Rifle)] =
            Texture{texture};
    }
    static void drawAsset(App &a, const AssetRecord &asset, Uint8 alpha = 255)
    {
        a.renderProfileAsset(asset, {100, 100, 194, 112}, 64, alpha, false);
    }
    static const auto &message(App &a) { return a.uiMessage_; }
    static void box(App &a, SDL_FRect bounds)
    {
        a.uiTextRenderer_.renderBox(a.renderer_, bounds, "SHOULD STAY CLIPPED");
    }
    static void loadInventoryArt(App &a)
    {
        const auto assets=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        for(std::size_t i=0;i<itemCount();++i)
        {
            const auto &definition=publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i]=Texture{IMG_LoadTexture(a.renderer_,(assets/definition.inventoryTexturePath).string().c_str())};
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid())<<definition.inventoryTexturePath;
        }
    }
};
namespace
{
using A = InventoryPresentationUiTestAccess;
class InventoryPresentationUiTest : public testing::Test
{
  protected:
    std::unique_ptr<App> app;
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("raidline-inventory-presentation-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    AssetInstanceId rifle{}, bag{}, loot{};
    SDL_Surface *surface{};
    SDL_Renderer *renderer{};
    auto &session() { return A::flow(*app).gameSession(); }
    const auto &profile() { return session().profile(); }
    void SetUp() override
    {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        const auto &c = publishedContentRegistry();
        auto p = makeNewAlphaProfile("inventory-presentation", c);
        // Keep the UI coordinates independent of starter-pack packing order.
        // These three explicit assets are the complete isolated fixture.
        p.assets = AssetRegistry{};
        rifle = p.assets.create(
            c.item(alpha_content::rifle),
            StoredAssetLocation{ProfileContainerId::stash(), {2, 3}});
        bag = p.assets.create(
            c.item(ItemDefinitionId{"item.container.backpack_expedition"}),
            EquippedAssetLocation{EquipmentSlotKind::Backpack});
        loot = p.assets.create(
            c.item(alpha_content::ammunition),
            StoredAssetLocation{ProfileContainerId::compartment(bag, 0),
                                {8, 5}},
            20);
        ASSERT_TRUE(validateProfileState(p, c).valid)
            << validateProfileState(p, c).message;
        ASSERT_TRUE(SaveRepository{path}.save(p, c.contentVersion()).succeeded);
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());
        A::open(*app);
    }
    void TearDown() override
    {
        if (app)
            A::detach(*app);
        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        app.reset();
        SDL_Quit();
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    void pointer(InventoryPointerEventType type, MousePosition p)
    {
        A::event(*app, InventoryPointerEvent{type, p});
    }
    void click(MousePosition p)
    {
        pointer(InventoryPointerEventType::LeftButtonDown, p);
        pointer(InventoryPointerEventType::LeftButtonUp, p);
    }
    void page(int delta)
    {
        A::event(*app, InventoryNavigationEvent{{700, 140}, delta, 0, 0});
    }
    void reload(ProfileState p)
    {
        ASSERT_TRUE(SaveRepository{path}.save(
            p, publishedContentRegistry().contentVersion()).succeeded);
        app = std::make_unique<App>();
        A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame());
        A::open(*app);
    }
    void render(int width = 1280, int height = 720)
    {
        surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
        ASSERT_NE(surface, nullptr);
        renderer = SDL_CreateSoftwareRenderer(surface);
        ASSERT_NE(renderer, nullptr);
        A::attach(*app, renderer);
        const auto v = UiViewport::fromWindow(width, height);
        SDL_SetRenderScale(renderer, v.scale, v.scale);
    }
};
TEST_F(InventoryPresentationUiTest,
       PageAndTabChangesPreserveOriginalProfileAndSavedBytes)
{
    const auto fingerprint = profileStateFingerprint(profile());
    const auto read = [&]
    {
        std::ifstream file(path / "profile.json", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    const auto bytes = read();
    for (int i = 0; i < 5; ++i)
        page(1);
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{16, 8}));
    click({450, 110});
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Backpack);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    EXPECT_EQ(read(), bytes);
}
TEST_F(InventoryPresentationUiTest,
       StashItemRemainsWholeAndClicksNeverEnterPanningOrReturnMode)
{
    A::right(*app, {812, 336});
    ASSERT_TRUE(A::menu(*app));
    EXPECT_EQ(A::menu(*app)->instanceId, rifle);
    page(1);
    EXPECT_FALSE(A::menu(*app));
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{8, 0}));
    page(-1);
    const auto fingerprint = profileStateFingerprint(profile());
    click({812, 336});
    EXPECT_FALSE(A::view(*app).returnContainer);
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    A::event(*app, InventoryNavigationEvent{{700, 140}, 0, 1, 1});
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       PageDuringDragRetainsSourceGrabAndEscOnlyCancels)
{
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {824, 348});
    const auto before = A::gesture(*app).activeDragVisual();
    ASSERT_TRUE(before);
    const auto source = A::gesture(*app).source();
    page(1);
    const auto after = A::gesture(*app).activeDragVisual();
    ASSERT_TRUE(after);
    EXPECT_EQ(after->grabOffsetInCells, before->grabOffsetInCells);
    EXPECT_EQ(A::gesture(*app).source()->instanceId, source->instanceId);
    A::cancel(*app);
    EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
    EXPECT_TRUE(A::isOpen(*app));
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       WholeItemMovesToAnotherPageAndCommitsOnlyOnce)
{
    const auto revision = profile().revision;
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {824, 348});
    page(1);
    pointer(InventoryPointerEventType::Motion, {700, 140});
    pointer(InventoryPointerEventType::LeftButtonUp, {700, 140});
    const auto location =
        std::get<StoredAssetLocation>(profile().assets.find(rifle)->location);
    EXPECT_EQ(location.origin, (GridPosition{8, 0})) << A::message(*app);
    EXPECT_EQ(profile().revision, revision + 1);
    EXPECT_TRUE(
        validateProfileState(profile(), publishedContentRegistry()).valid);
    pointer(InventoryPointerEventType::LeftButtonUp, {700, 140});
    EXPECT_EQ(profile().revision, revision + 1);
}
TEST_F(InventoryPresentationUiTest, PageEdgeRejectsWholeAndRotatedFootprints)
{
    const auto fingerprint = profileStateFingerprint(profile());
    for (bool rotate : {false, true})
    {
        pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
        pointer(InventoryPointerEventType::Motion, {824, 348});
        ASSERT_TRUE(A::gesture(*app).activeDragVisual());
        if (rotate)
            A::event(*app, InventoryRotateEvent{});
        const MousePosition edge = rotate ? MousePosition{700, 462}
                                          : MousePosition{1068, 140};
        pointer(InventoryPointerEventType::Motion, edge);
        pointer(InventoryPointerEventType::LeftButtonUp, edge);
        EXPECT_EQ(profileStateFingerprint(profile()), fingerprint)
            << A::message(*app);
        EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
        EXPECT_TRUE(A::isOpen(*app));
    }
}
TEST_F(InventoryPresentationUiTest,
       HoverTabWhileDraggingPreservesSourceAndDoesNotTransfer)
{
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {450, 110});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    A::hover(*app, .34F);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    A::hover(*app, .02F);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Backpack);
    EXPECT_EQ(A::gesture(*app).source()->instanceId, rifle);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       HiddenBackpackCanReceiveQuickTransferAndLocateOffersActualTab)
{
    const auto &c = publishedContentRegistry();
    const auto id = profile().assets.find(loot)->instanceId;
    ASSERT_TRUE(session()
                    .executeProfileInventory(
                        InventoryMoveCommand{
                            id, 0, {ProfileContainerId::stash(), {1, 3}}},
                        "move-loot")
                    .succeeded);
    A::event(*app, InventoryQuickTransferEvent{MousePosition{736, 324}});
    const auto stored =
        std::get<StoredAssetLocation>(profile().assets.find(id)->location);
    EXPECT_EQ(stored.container.ownerAssetId, bag);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    EXPECT_EQ(A::view(*app).locateAsset, id);
    click({1000, 665});
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Backpack);
}
TEST_F(InventoryPresentationUiTest,
       QuickTransferUsesOtherWarehousePagesAndCanLocateThem)
{
    const auto &c = publishedContentRegistry();
    auto p = profile();
    // Fill the first 8 columns over its entire height. Free space starts on
    // page 2.
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 8; ++x)
            if (!profileAssetAtCell(p, c, ProfileContainerId::stash(), {x, y}))
                static_cast<void>(p.assets.create(
                    c.item(alpha_content::lootCola),
                    StoredAssetLocation{ProfileContainerId::stash(), {x, y}}));
    ASSERT_TRUE(validateProfileState(p, c).valid);
    ASSERT_TRUE(SaveRepository{path}.save(p, c.contentVersion()).succeeded);
    app = std::make_unique<App>();
    A::flow(*app).configurePersistence(path);
    ASSERT_TRUE(A::flow(*app).continueGame());
    A::open(*app);
    A::view(*app).tab = ProfileInventoryTab::Backpack;
    A::event(*app, InventoryQuickTransferEvent{
                       MousePosition{42 + 8 * 64 + 4, 148 + 5 * 64 + 4}});
    const auto stored =
        std::get<StoredAssetLocation>(profile().assets.find(loot)->location);
    EXPECT_EQ(stored.container, ProfileContainerId::stash());
    EXPECT_GE(stored.origin.x, 8);
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    click({1000, 665});
    EXPECT_GE(A::view(*app).originFor(ProfileContainerId::stash()).x, 1);
}
TEST_F(InventoryPresentationUiTest,
       CloseResetsPageStateAndNoLongerRetainsContainerTargets)
{
    page(1);
    A::close(*app);
    EXPECT_TRUE(A::view(*app).positions.empty());
    EXPECT_FALSE(A::view(*app).lastTarget);
    EXPECT_FALSE(A::gesture(*app).source());
}
TEST_F(InventoryPresentationUiTest,
       QuantityAndRotationRemainLockedAcrossNavigationUntilCancelled)
{
    const auto fingerprint = profileStateFingerprint(profile());
    A::view(*app).tab = ProfileInventoryTab::Backpack;
    A::event(*app, InventoryPartialTransferEvent{{558, 472}, false, true});
    pointer(InventoryPointerEventType::Motion, {560, 480});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    EXPECT_EQ(A::gesture(*app).source()->quantity, 10U);
    page(1);
    EXPECT_EQ(A::gesture(*app).source()->quantity, 10U);
    A::cancel(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    A::view(*app).tab = ProfileInventoryTab::Equipment;
    page(-1);
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {824, 348});
    A::event(*app, InventoryRotateEvent{});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    const auto orientation = A::gesture(*app).activeDragVisual()->orientation;
    page(1);
    EXPECT_EQ(A::gesture(*app).activeDragVisual()->orientation, orientation);
    A::cancel(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       EscWinsOverPageKeyInSameFrameWithoutClosingInventory)
{
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {824, 348});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    for (auto code : {SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_ESCAPE})
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = code;
        event.key.down = true;
        ASSERT_TRUE(SDL_PushEvent(&event));
    }
    const auto fingerprint = profileStateFingerprint(profile());
    A::frame(*app);
    EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
    EXPECT_TRUE(A::isOpen(*app));
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}

TEST_F(InventoryPresentationUiTest, ReturnsHoverSwitchesOnceWithoutReleasingDraggedAsset)
{
    auto p=profile();const auto &c=publishedContentRegistry();
    static_cast<void>(p.assets.create(c.item(alpha_content::ammunition),StoredAssetLocation{ProfileContainerId::baseIntake(),{12,10}},1));
    ASSERT_TRUE(validateProfileState(p,c).valid)<<validateProfileState(p,c).message;
    ASSERT_TRUE(SaveRepository{path}.save(p,c.contentVersion()).succeeded);
    app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::open(*app);
    const auto fingerprint=profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown,{812,336});pointer(InventoryPointerEventType::Motion,{1100,74});
    A::hover(*app,.36F);EXPECT_TRUE(A::view(*app).returns);A::hover(*app,1.0F);EXPECT_TRUE(A::view(*app).returns);
    EXPECT_EQ(A::gesture(*app).source()->instanceId,rifle);EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
    A::cancel(*app);EXPECT_TRUE(A::isOpen(*app));EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST_F(InventoryPresentationUiTest, LegacyEntryIsHiddenWhenEmptyAndUnavailableInRaid)
{
    click({930, 74});
    EXPECT_FALSE(A::view(*app).legacyOverflow);
    auto p = profile();
    static_cast<void>(p.assets.create(
        publishedContentRegistry().item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 1));
    reload(std::move(p));
    const auto fingerprint = profileStateFingerprint(profile());
    for (auto type : {InventoryPointerEventType::LeftButtonDown,
                      InventoryPointerEventType::LeftButtonUp})
        A::raidEvent(*app, InventoryPointerEvent{type, {930, 74}});
    EXPECT_FALSE(A::view(*app).legacyOverflow);
    click({930, 74});
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    // A leftover client tab must not expose the base holding area in Raid.
    A::raidEvent(*app, InventoryQuickTransferEvent{MousePosition{700, 140}});
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest, LegacyStorageIsWithdrawOnlyAndHidesAfterLastWithdrawal)
{
    auto p = profile();
    const auto pending = p.assets.create(
        publishedContentRegistry().item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 1);
    reload(std::move(p));
    click({930, 74});
    ASSERT_TRUE(A::view(*app).legacyOverflow);
    A::view(*app).tab = ProfileInventoryTab::Backpack;
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {558, 472});
    pointer(InventoryPointerEventType::Motion, {700, 140});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    pointer(InventoryPointerEventType::LeftButtonUp, {700, 140});
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
    ASSERT_TRUE(A::isOpen(*app));
    A::event(*app, InventoryQuickTransferEvent{MousePosition{700, 140}});
    ASSERT_NE(profile().assets.find(pending), nullptr);
    EXPECT_EQ(std::get<StoredAssetLocation>(
                  profile().assets.find(pending)->location).container,
              ProfileContainerId::stash()) << A::message(*app);
    EXPECT_TRUE(assetsInContainer(profile(),
                                 ProfileContainerId::legacyOverflow()).empty());
    A::view(*app).legacyOverflow = false;
    click({930, 74});
    EXPECT_FALSE(A::view(*app).legacyOverflow);
}
TEST_F(InventoryPresentationUiTest, LegacyHoverKeepsDraggedSourceAndContinuousOldItemsCanBeLocated)
{
    auto p = profile();
    const auto oldRifle = p.assets.create(
        publishedContentRegistry().item(alpha_content::rifle),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {6, 7}});
    reload(std::move(p));
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {812, 336});
    pointer(InventoryPointerEventType::Motion, {930, 74});
    A::hover(*app, .36F);
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    A::hover(*app, 1.0F);
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    ASSERT_TRUE(A::gesture(*app).source());
    EXPECT_EQ(A::gesture(*app).source()->instanceId, rifle);
    A::cancel(*app);
    A::right(*app, {1070, 600});
    ASSERT_TRUE(A::menu(*app));
    EXPECT_EQ(A::menu(*app)->instanceId, oldRifle);
    page(1); // Closes the menu, retaining access to the old item's other cells.
    click({700, 600});
    EXPECT_TRUE(A::view(*app).returnContainer);
    EXPECT_EQ(*A::view(*app).returnContainer,
              ProfileContainerId::legacyOverflow());
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       OffscreenTextCannotDisableParentViewportClipping)
{
    render();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    const SDL_Rect clip{668, 128, 512, 512};
    ASSERT_TRUE(SDL_SetRenderClipRect(renderer, &clip));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    A::box(*app, {1200, 140, 100, 40});
    SDL_RenderPresent(renderer);
    SDL_Rect after{};
    SDL_GetRenderClipRect(renderer, &after);
    EXPECT_TRUE(SDL_RenderClipEnabled(renderer));
    EXPECT_EQ(after.x, clip.x);
    EXPECT_EQ(after.w, clip.w);
    int bright = 0;
    for (int y = 128; y < 190; ++y)
        for (int x = 1180; x < 1280; ++x)
        {
            Uint8 rr{}, g{}, b{}, a{};
            SDL_ReadSurfacePixel(surface, x, y, &rr, &g, &b, &a);
            if (rr || g || b)
                ++bright;
        }
    EXPECT_EQ(bright, 0);
    SDL_SetRenderClipRect(renderer, nullptr);
}
TEST_F(InventoryPresentationUiTest,
       ThreeWindowSizesAndLanguagesRenderWithoutChangingProfile)
{
    auto prepared=profile();const auto &content=publishedContentRegistry();
    static_cast<void>(prepared.assets.create(content.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig}));
    static_cast<void>(prepared.assets.create(content.item(alpha_content::rifle),EquippedAssetLocation{EquipmentSlotKind::PrimaryWeapon}));
    static_cast<void>(prepared.assets.create(content.item(alpha_content::pistol),EquippedAssetLocation{EquipmentSlotKind::Sidearm}));
    ASSERT_TRUE(validateProfileState(prepared,content).valid)<<validateProfileState(prepared,content).message;
    ASSERT_TRUE(SaveRepository{path}.save(prepared,content.contentVersion()).succeeded);
    app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::open(*app);
    const auto fingerprint = profileStateFingerprint(profile());
    for (const auto size : kUiWindowSizes)
    {
        render(size.width, size.height);
        A::loadInventoryArt(*app);
        for (auto language :
             {UiLanguage::English, UiLanguage::SimplifiedChinese})
            for (int tab = 0; tab < 3; ++tab)
            {
                A::view(*app).tab = static_cast<ProfileInventoryTab>(tab);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                ASSERT_TRUE(SDL_RenderClear(renderer));
                A::draw(*app, language);
                ASSERT_TRUE(SDL_RenderPresent(renderer));
                if (const auto *dir = std::getenv("RAIDLINE_INVENTORY_QA_DIR"))
                {
                    std::filesystem::create_directories(dir);
                    const auto name =
                        std::to_string(size.width) + "-" +
                        std::to_string(static_cast<int>(language)) + "-" +
                        std::to_string(tab) + ".bmp";
                    const auto output =
                        (std::filesystem::path(dir) / name).u8string();
                    EXPECT_TRUE(SDL_SaveBMP(surface,
                        reinterpret_cast<const char *>(output.c_str())))
                        << SDL_GetError();
                }
            }
        A::detach(*app);
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        SDL_DestroySurface(surface);
        surface = nullptr;
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       RealInventoryTextureIsLetterboxedAndKeepsAlphaAfterGhost)
{
    render();
    SDL_Surface *sample = SDL_CreateSurface(96, 32, SDL_PIXELFORMAT_RGBA32);
    ASSERT_NE(sample, nullptr);
    ASSERT_TRUE(SDL_FillSurfaceRect(
        sample, nullptr, SDL_MapSurfaceRGBA(sample, 255, 0, 0, 255)));
    auto *texture = SDL_CreateTextureFromSurface(renderer, sample);
    ASSERT_NE(texture, nullptr);
    A::texture(*app, texture);
    SDL_DestroySurface(sample);
    A::drawAsset(*app, *profile().assets.find(rifle), 150);
    Uint8 alpha{};
    ASSERT_TRUE(SDL_GetTextureAlphaMod(texture, &alpha));
    EXPECT_EQ(alpha, 255);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    A::drawAsset(*app, *profile().assets.find(rifle));
    SDL_RenderPresent(renderer);
    int left = 300, right = 0, top = 220, bottom = 0;
    for (int y = 100; y < 212; ++y)
        for (int x = 100; x < 294; ++x)
        {
            Uint8 rr{}, g{}, b{}, a{};
            SDL_ReadSurfacePixel(surface, x, y, &rr, &g, &b, &a);
            if (rr > 240 && g < 10 && b < 10)
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    ASSERT_GT(right, left);
    ASSERT_GT(bottom, top);
    EXPECT_NEAR(static_cast<float>(right - left + 1) / (bottom - top + 1), 3.0F,
                .08F);
    EXPECT_GT(top, 110);
    EXPECT_LT(bottom, 202);
}
TEST_F(InventoryPresentationUiTest,
       LegacyHoldingAndReturnEntriesRenderInChineseAtEveryWindowSize)
{
    auto p = profile();
    const auto &content = publishedContentRegistry();
    static_cast<void>(p.assets.create(
        content.item(alpha_content::rifle),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {6, 7}}));
    static_cast<void>(p.assets.create(
        content.item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 10));
    static_cast<void>(p.assets.create(
        content.item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::baseIntake(), {12, 10}}, 1));
    reload(std::move(p));
    const auto fingerprint = profileStateFingerprint(profile());
    for (const auto size : kUiWindowSizes)
    {
        render(size.width, size.height);
        A::loadInventoryArt(*app);
        for (bool selected : {false, true})
        {
            A::view(*app).legacyOverflow = selected;
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            ASSERT_TRUE(SDL_RenderClear(renderer));
            A::draw(*app, UiLanguage::SimplifiedChinese);
            ASSERT_TRUE(SDL_RenderPresent(renderer));
            if (const auto *dir = std::getenv("RAIDLINE_INVENTORY_QA_DIR"))
            {
                std::filesystem::create_directories(dir);
                const auto name = "legacy-" + std::to_string(size.width) +
                    (selected ? "-selected.bmp" : "-available.bmp");
                const auto output =
                    (std::filesystem::path(dir) / name).u8string();
                EXPECT_TRUE(SDL_SaveBMP(surface,
                    reinterpret_cast<const char *>(output.c_str())))
                    << SDL_GetError();
            }
        }
        A::detach(*app);
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
        SDL_DestroySurface(surface);
        surface = nullptr;
    }
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
} // namespace
