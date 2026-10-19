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
        a.playerTexture_.reset();a.uiTextRenderer_.shutdown();
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
    static void trimmedTexture(App &a,Texture texture)
    {
        a.inventoryItemTextures_[static_cast<std::size_t>(ItemId::Rifle)]=std::move(texture);
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
            a.inventoryItemTextures_[i]=loadInventoryTexture(a.renderer_,assets/definition.inventoryTexturePath);
            ASSERT_TRUE(a.inventoryItemTextures_[i].valid())<<definition.inventoryTexturePath;
        }
        a.playerTexture_=loadInventoryTexture(a.renderer_,assets/"characters/protagonist_left_minimal_256x320.png");
        ASSERT_TRUE(a.playerTexture_.valid());
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
        A::event(*app, InventoryNavigationEvent{{836.0F, 138.5F}, delta, 0, 0});
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
       PageAndHeaderClicksPreserveOriginalProfileAndSavedBytes)
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
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    EXPECT_EQ(read(), bytes);
}
TEST_F(InventoryPresentationUiTest,
       StashItemRemainsWholeAndClicksNeverEnterPanningOrReturnMode)
{
    A::right(*app, {934.0F, 310.0F});
    ASSERT_TRUE(A::menu(*app));
    EXPECT_EQ(A::menu(*app)->instanceId, rifle);
    page(1);
    EXPECT_FALSE(A::menu(*app));
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{8, 0}));
    page(-1);
    const auto fingerprint = profileStateFingerprint(profile());
    click({934.0F, 310.0F});
    EXPECT_FALSE(A::view(*app).returnContainer);
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    A::event(*app, InventoryNavigationEvent{{836.0F, 138.5F}, 0, 1, 1});
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       PageDuringDragRetainsSourceGrabAndEscOnlyCancels)
{
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {944.5F, 320.5F});
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
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {944.5F, 320.5F});
    page(1);
    pointer(InventoryPointerEventType::Motion, {836.0F, 138.5F});
    pointer(InventoryPointerEventType::LeftButtonUp, {836.0F, 138.5F});
    const auto location =
        std::get<StoredAssetLocation>(profile().assets.find(rifle)->location);
    EXPECT_EQ(location.origin, (GridPosition{8, 0})) << A::message(*app);
    EXPECT_EQ(profile().revision, revision + 1);
    EXPECT_TRUE(
        validateProfileState(profile(), publishedContentRegistry()).valid);
    pointer(InventoryPointerEventType::LeftButtonUp, {836.0F, 138.5F});
    EXPECT_EQ(profile().revision, revision + 1);
}
TEST_F(InventoryPresentationUiTest, PageEdgeRejectsWholeAndRotatedFootprints)
{
    const auto fingerprint = profileStateFingerprint(profile());
    for (bool rotate : {false, true})
    {
        pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
        pointer(InventoryPointerEventType::Motion, {944.5F, 320.5F});
        ASSERT_TRUE(A::gesture(*app).activeDragVisual());
        if (rotate)
            A::event(*app, InventoryRotateEvent{});
        const MousePosition edge = rotate ? MousePosition{836.0F, 420.25F}
                                          : MousePosition{1158.0F, 138.5F};
        pointer(InventoryPointerEventType::Motion, edge);
        pointer(InventoryPointerEventType::LeftButtonUp, edge);
        EXPECT_EQ(profileStateFingerprint(profile()), fingerprint)
            << A::message(*app);
        EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
        EXPECT_TRUE(A::isOpen(*app));
    }
}
TEST_F(InventoryPresentationUiTest,
       HeaderHoverWhileDraggingPreservesSourceWithoutSwitchingPages)
{
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {450, 110});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    A::hover(*app, .34F);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    A::hover(*app, .02F);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    EXPECT_EQ(A::gesture(*app).source()->instanceId, rifle);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest,
       VisibleBackpackReceivesQuickTransferAndLocateKeepsSamePage)
{
    const auto &c = publishedContentRegistry();
    const auto id = profile().assets.find(loot)->instanceId;
    ASSERT_TRUE(session()
                    .executeProfileInventory(
                        InventoryMoveCommand{
                            id, 0, {ProfileContainerId::stash(), {1, 3}}},
                        "move-loot")
                    .succeeded);
    A::event(*app, InventoryQuickTransferEvent{MousePosition{867.5F, 299.5F}});
    const auto stored =
        std::get<StoredAssetLocation>(profile().assets.find(id)->location);
    EXPECT_EQ(stored.container.ownerAssetId, bag);
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
    EXPECT_EQ(A::view(*app).locateAsset, id);
    click({1032.0F, 615.0F});
    EXPECT_EQ(A::view(*app).tab, ProfileInventoryTab::Equipment);
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
                       MousePosition{282 + 8 * 56 + 4, 152 + 5 * 56 + 4}});
    const auto stored =
        std::get<StoredAssetLocation>(profile().assets.find(loot)->location);
    EXPECT_EQ(stored.container, ProfileContainerId::stash());
    EXPECT_GE(stored.origin.x, 8);
    EXPECT_EQ(A::view(*app).originFor(ProfileContainerId::stash()),
              (GridPosition{}));
    click({1032.0F, 615.0F});
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
TEST_F(InventoryPresentationUiTest, SharedCarriedScrollKeepsSourceGrabAndQuantityUntilCancel)
{
    const auto &c=publishedContentRegistry();auto p=profile();
    p.assets.create(c.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    reload(std::move(p));const auto fingerprint=profileStateFingerprint(profile());
    A::event(*app,InventoryNavigationEvent{{300,300},0,0,4});
    EXPECT_EQ(A::view(*app).chestScroll,108);
    pointer(InventoryPointerEventType::LeftButtonDown,{734,580});
    pointer(InventoryPointerEventType::Motion,{746,592});
    const auto before=A::gesture(*app).activeDragVisual();ASSERT_TRUE(before);
    ASSERT_TRUE(A::gesture(*app).source());EXPECT_EQ(A::gesture(*app).source()->instanceId,loot);
    A::event(*app,InventoryNavigationEvent{{300,300},0,0,-1});
    const auto after=A::gesture(*app).activeDragVisual();ASSERT_TRUE(after);
    EXPECT_EQ(A::view(*app).chestScroll,52);EXPECT_EQ(before->grabOffsetInCells,after->grabOffsetInCells);
    EXPECT_EQ(A::gesture(*app).source()->instanceId,loot);EXPECT_EQ(profile().assets.find(loot)->quantity,20);
    A::cancel(*app);EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST_F(InventoryPresentationUiTest, WarehouseDragCanReachVisibleBackpackWithoutAnyTabSwitch)
{
    pointer(InventoryPointerEventType::LeftButtonDown,{934,310});
    pointer(InventoryPointerEventType::Motion,{946,322});ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    pointer(InventoryPointerEventType::Motion,{296,166});
    pointer(InventoryPointerEventType::LeftButtonUp,{296,166});
    const auto stored=std::get<StoredAssetLocation>(profile().assets.find(rifle)->location);
    EXPECT_EQ(stored.container,ProfileContainerId::compartment(bag,0));EXPECT_EQ(stored.origin,(GridPosition{0,0}));
    EXPECT_EQ(A::view(*app).tab,ProfileInventoryTab::Equipment);
    EXPECT_TRUE(validateProfileState(profile(),publishedContentRegistry()).valid);
}
TEST_F(InventoryPresentationUiTest,
       QuantityAndRotationRemainLockedAcrossNavigationUntilCancelled)
{
    const auto fingerprint = profileStateFingerprint(profile());
    A::view(*app).tab = ProfileInventoryTab::Backpack;
    A::event(*app, InventoryPartialTransferEvent{{734.0F, 436.0F}, false, true});
    pointer(InventoryPointerEventType::Motion, {736.0F, 444.0F});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    EXPECT_EQ(A::gesture(*app).source()->quantity, 10U);
    page(1);
    EXPECT_EQ(A::gesture(*app).source()->quantity, 10U);
    A::cancel(*app);
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    A::view(*app).tab = ProfileInventoryTab::Equipment;
    page(-1);
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {944.5F, 320.5F});
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
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {944.5F, 320.5F});
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
    pointer(InventoryPointerEventType::LeftButtonDown,{934.0F, 310.0F});pointer(InventoryPointerEventType::Motion,{1064.0F, 76.0F});
    A::hover(*app,.36F);EXPECT_TRUE(A::view(*app).returns);A::hover(*app,1.0F);EXPECT_TRUE(A::view(*app).returns);
    EXPECT_EQ(A::gesture(*app).source()->instanceId,rifle);EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
    A::cancel(*app);EXPECT_TRUE(A::isOpen(*app));EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST_F(InventoryPresentationUiTest, LegacyEntryIsHiddenWhenEmptyAndUnavailableInRaid)
{
    click({1190.0F, 76.0F});
    EXPECT_FALSE(A::view(*app).legacyOverflow);
    auto p = profile();
    static_cast<void>(p.assets.create(
        publishedContentRegistry().item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 1));
    reload(std::move(p));
    const auto fingerprint = profileStateFingerprint(profile());
    for (auto type : {InventoryPointerEventType::LeftButtonDown,
                      InventoryPointerEventType::LeftButtonUp})
        A::raidEvent(*app, InventoryPointerEvent{type, {1190.0F, 76.0F}});
    EXPECT_FALSE(A::view(*app).legacyOverflow);
    click({1190.0F, 76.0F});
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    // A leftover client tab must not expose the base holding area in Raid.
    A::raidEvent(*app, InventoryQuickTransferEvent{MousePosition{836.0F, 138.5F}});
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
}
TEST_F(InventoryPresentationUiTest, LegacyStorageIsWithdrawOnlyAndHidesAfterLastWithdrawal)
{
    auto p = profile();
    const auto pending = p.assets.create(
        publishedContentRegistry().item(alpha_content::ammunition),
        StoredAssetLocation{ProfileContainerId::legacyOverflow(), {0, 0}}, 1);
    reload(std::move(p));
    click({1190.0F, 76.0F});
    ASSERT_TRUE(A::view(*app).legacyOverflow);
    A::view(*app).tab = ProfileInventoryTab::Backpack;
    const auto fingerprint = profileStateFingerprint(profile());
    pointer(InventoryPointerEventType::LeftButtonDown, {734.0F, 436.0F});
    pointer(InventoryPointerEventType::Motion, {836.0F, 138.5F});
    ASSERT_TRUE(A::gesture(*app).activeDragVisual());
    pointer(InventoryPointerEventType::LeftButtonUp, {836.0F, 138.5F});
    EXPECT_EQ(profileStateFingerprint(profile()), fingerprint);
    EXPECT_FALSE(A::gesture(*app).pointerGestureActive());
    ASSERT_TRUE(A::isOpen(*app));
    A::event(*app, InventoryQuickTransferEvent{MousePosition{836.0F, 138.5F}});
    ASSERT_NE(profile().assets.find(pending), nullptr);
    EXPECT_EQ(std::get<StoredAssetLocation>(
                  profile().assets.find(pending)->location).container,
              ProfileContainerId::stash()) << A::message(*app);
    EXPECT_TRUE(assetsInContainer(profile(),
                                 ProfileContainerId::legacyOverflow()).empty());
    A::view(*app).legacyOverflow = false;
    click({1190.0F, 76.0F});
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
    pointer(InventoryPointerEventType::LeftButtonDown, {934.0F, 310.0F});
    pointer(InventoryPointerEventType::Motion, {1190.0F, 76.0F});
    A::hover(*app, .36F);
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    A::hover(*app, 1.0F);
    EXPECT_TRUE(A::view(*app).legacyOverflow);
    ASSERT_TRUE(A::gesture(*app).source());
    EXPECT_EQ(A::gesture(*app).source()->instanceId, rifle);
    A::cancel(*app);
    A::right(*app, {1159.75F, 541.0F});
    ASSERT_TRUE(A::menu(*app));
    EXPECT_EQ(A::menu(*app)->instanceId, oldRifle);
    page(1); // Closes the menu, retaining access to the old item's other cells.
    click({836.0F, 541.0F});
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
    static_cast<void>(prepared.assets.create(content.item(ItemDefinitionId{"item.weapon.rifle_5_45_service"}),EquippedAssetLocation{EquipmentSlotKind::SecondaryWeapon}));
    static_cast<void>(prepared.assets.create(content.item(alpha_content::utilityKnife),EquippedAssetLocation{EquipmentSlotKind::QuickMelee}));
    static_cast<void>(prepared.assets.create(content.item(alpha_content::helmet),EquippedAssetLocation{EquipmentSlotKind::Helmet}));
    static_cast<void>(prepared.assets.create(content.item(alpha_content::bodyArmor),EquippedAssetLocation{EquipmentSlotKind::BodyArmor}));
    const auto rig=*equippedAsset(prepared,EquipmentSlotKind::ChestRig);
    int x=0;
    for(auto id:{alpha_content::medkit,alpha_content::bandage,alpha_content::tourniquet,alpha_content::painkiller,alpha_content::noiseDecoy})
        static_cast<void>(prepared.assets.create(content.item(id),StoredAssetLocation{ProfileContainerId::compartment(rig,0),{x++,0}}));
    for(int row=0;row<3;++row)for(auto id:{alpha_content::lootCola,alpha_content::lootScrap,alpha_content::lootElectronics,alpha_content::ammunition,alpha_content::medkit,alpha_content::weaponMaintenanceKit,alpha_content::armorMaintenanceKit}) {
        const auto &d=content.item(id);const auto fit=findFirstProfileFit(prepared,content,ProfileContainerId::stash(),d,ItemOrientation::Degrees0);
        ASSERT_TRUE(fit);static_cast<void>(prepared.assets.create(d,StoredAssetLocation{ProfileContainerId::stash(),*fit}));
    }

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
                A::view(*app).chestScroll=tab==1?maximumProfileCarriedScroll(profile(),content):0;
                A::view(*app).remember(ProfileContainerId::stash(),tab==2?GridPosition{8,0}:GridPosition{});
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
TEST_F(InventoryPresentationUiTest, TransparentPaddingIsRemovedOnlyForDrawingAndSourceFileStaysIntact)
{
    render();auto *sample=SDL_CreateSurface(128,64,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(sample,nullptr);
    ASSERT_TRUE(SDL_FillSurfaceRect(sample,nullptr,SDL_MapSurfaceRGBA(sample,0,0,0,0)));
    const SDL_Rect visible{16,20,96,24};ASSERT_TRUE(SDL_FillSurfaceRect(sample,&visible,SDL_MapSurfaceRGBA(sample,255,0,0,255)));
    const auto file=path/"padded-item.png";const auto utf8=file.u8string();
    ASSERT_TRUE(IMG_SavePNG(sample,reinterpret_cast<const char *>(utf8.c_str())));SDL_DestroySurface(sample);
    const auto bytes=[&] {std::ifstream in(file,std::ios::binary);return std::string{std::istreambuf_iterator<char>{in},{}};};
    const auto original=bytes();auto texture=loadInventoryTexture(renderer,file);ASSERT_TRUE(texture.valid());
    const auto source=texture.contentBounds();EXPECT_FLOAT_EQ(source.x,16);EXPECT_FLOAT_EQ(source.y,20);
    EXPECT_FLOAT_EQ(source.w,96);EXPECT_FLOAT_EQ(source.h,24);A::trimmedTexture(*app,std::move(texture));
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);A::drawAsset(*app,*profile().assets.find(rifle));SDL_RenderPresent(renderer);
    int left=300,right=0,top=220,bottom=0;
    for(int y=100;y<212;++y)for(int x=100;x<294;++x) {
        Uint8 r,g,b,a;SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a);
        if(r>240 && g<10 && b<10) {left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
    }
    ASSERT_GT(right,left);EXPECT_GE(right-left+1,190);EXPECT_NEAR(static_cast<float>(right-left+1)/(bottom-top+1),4,.1);
    EXPECT_EQ(bytes(),original);
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
