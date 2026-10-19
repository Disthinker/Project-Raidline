#include "app.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <memory>

struct WeaponInventoryUiTestAccess {
    static GameFlow &flow(App &a) { return a.gameFlow_; }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static auto menu(App &a) { return a.profileContextMenu_; }
    static auto details(App &a) { return a.profileDetailsAsset_; }
    static bool modifying(App &a) { return a.inventoryWeaponComponentsOpen_; }
    static void showStash(App &a) { a.inventoryOverlayState_.openContainerInventory(); }
    static GridPosition locate(App &a,AssetInstanceId id) { a.locateProfileInventoryAsset(id,true,std::nullopt);return a.profilePresentation_.originFor(ProfileContainerId::stash()); }
    static bool inventory(App &a) { return a.inventoryOverlayState_.isOpen(); }
    static auto weapon(App &a) { return a.componentWeapon_; }
    static const auto &message(App &a) { return a.uiMessage_; }
    static void raidRightClick(App &a) { a.handleProfileRightClick({64,382},true); }
    static MousePosition actionPoint(App &a, UiItemAction action) {
        for (std::size_t i=0;i<a.profileContextMenu_->actions.size();++i)
            if(a.profileContextMenu_->actions[i].id==action) {
                const auto r=a.profileContextMenu_->layout.rows[i].bounds;
                return {r.x+r.w/2,r.y+r.h/2};
            }
        ADD_FAILURE()<<"Missing menu action";return {};
    }
    static void raidMenuClick(App &a) {
        const auto p=actionPoint(a,UiItemAction::ModifyWeapon);
        a.handleProfileInventoryUiEvent(InventoryPointerEvent{InventoryPointerEventType::LeftButtonDown,p},true);
        a.handleProfileInventoryUiEvent(InventoryPointerEvent{InventoryPointerEventType::LeftButtonUp,p},true);
    }
};
namespace {
using A = WeaponInventoryUiTestAccess;
AssetInstanceId find(const ProfileState &profile, const char *id) {
    for (const auto &[key, asset] : profile.assets.records()) if (asset.definitionId == ItemDefinitionId{id}) return key;
    return 0;
}
void click(float x, float y, Uint8 button = SDL_BUTTON_LEFT) {
    SDL_Event e{}; e.type=SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button=button; e.button.x=x; e.button.y=y;
    ASSERT_TRUE(SDL_PushEvent(&e)); e.type=SDL_EVENT_MOUSE_BUTTON_UP; ASSERT_TRUE(SDL_PushEvent(&e));
}
void key(SDL_Scancode code) {
    SDL_Event e{}; e.type=SDL_EVENT_KEY_DOWN; e.key.scancode=code; e.key.down=true;
    ASSERT_TRUE(SDL_PushEvent(&e)); e.type=SDL_EVENT_KEY_UP; e.key.down=false; ASSERT_TRUE(SDL_PushEvent(&e));
}
class WeaponInventoryUiTest : public testing::Test {
protected:
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("raidline-weapon-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::unique_ptr<App> app;
    AssetInstanceId weapon{};
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        const auto &content=publishedContentRegistry();
        auto p=makeNewPublishedProfile("weapon-ui", content);
        weapon=find(p,"item.weapon.rifle_5_45_service");
        ASSERT_TRUE(executeInventory(p,content,InventoryEquipCommand{weapon,EquipmentSlotKind::PrimaryWeapon},{p.revision,"equip-ui"}).succeeded);
        ASSERT_TRUE(SaveRepository{path}.save(p,content.contentVersion()).succeeded);
        app=std::make_unique<App>(); A::flow(*app).configurePersistence(path);
        ASSERT_TRUE(A::flow(*app).continueGame()); frame();
        key(SDL_SCANCODE_TAB); frame(); ASSERT_TRUE(A::inventory(*app));
    }
    void TearDown() override { app.reset(); SDL_Quit(); std::error_code ec; std::filesystem::remove_all(path,ec); }
    void frame() { A::frame(*app); }
    void tap(float x,float y, Uint8 button=SDL_BUTTON_LEFT) { click(x,y,button); frame(); }
    void openMenu() { tap(64,382,SDL_BUTTON_RIGHT); ASSERT_TRUE(A::menu(*app)); EXPECT_EQ(A::menu(*app)->instanceId,weapon); }
    void choose(UiItemAction action) { const auto p=A::actionPoint(*app,action);tap(p.x,p.y); }
    const ProfileState &profile() { return A::flow(*app).gameSession().profile(); }
};
TEST_F(WeaponInventoryUiTest, EquippedWeaponDetailsEscapeAndOutsideClickPreserveInventory) {
    const auto fingerprint=profileStateFingerprint(profile());
    openMenu(); choose(UiItemAction::WeaponDetails); ASSERT_EQ(A::details(*app),weapon);
    EXPECT_FALSE(A::modifying(*app));
    key(SDL_SCANCODE_ESCAPE); frame(); EXPECT_FALSE(A::details(*app)); EXPECT_TRUE(A::inventory(*app));
    openMenu(); tap(600,600); EXPECT_FALSE(A::menu(*app));
    EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
    EXPECT_FALSE(A::flow(*app).baseWorld().shotFiredLastUpdate());
}
TEST_F(WeaponInventoryUiTest, InventoryModificationInstallsSelectedWeaponAndReturnsWithoutClickThrough) {
    openMenu(); choose(UiItemAction::ModifyWeapon); ASSERT_TRUE(A::modifying(*app)); EXPECT_EQ(A::weapon(*app),weapon);
    const auto part=find(profile(),"item.component.precision_barrel");
    tap(420,260); // first component after restore-default row
    tap(800,565);
    const auto *location=std::get_if<InstalledWeaponComponentLocation>(&profile().assets.find(part)->location);
    ASSERT_NE(location,nullptr); EXPECT_EQ(location->weaponAssetId,weapon);
    tap(900,105); EXPECT_FALSE(A::modifying(*app)); EXPECT_TRUE(A::inventory(*app));
    openMenu(); choose(UiItemAction::ModifyWeapon); ASSERT_TRUE(A::modifying(*app));
    key(SDL_SCANCODE_TAB); frame(); EXPECT_FALSE(A::inventory(*app)); EXPECT_FALSE(A::modifying(*app));
    EXPECT_FALSE(A::flow(*app).baseWorld().shotFiredLastUpdate());
}
TEST_F(WeaponInventoryUiTest, StoredWeaponOpensDetailsFromWarehouseGrid) {
    const auto &content=publishedContentRegistry();
    const auto &definition=content.item(profile().assets.find(weapon)->definitionId);
    const auto origin=findFirstProfileFit(profile(),content,ProfileContainerId::stash(),definition,ItemOrientation::Degrees0);
    ASSERT_TRUE(origin);
    ASSERT_TRUE(A::flow(*app).gameSession().executeProfileInventory(
        InventoryMoveCommand{weapon,0,{ProfileContainerId::stash(),*origin},ItemOrientation::Degrees0},"store-for-menu").succeeded);
    A::showStash(*app);
    const auto view=A::locate(*app,weapon);
    const float x=808.0F+(origin->x-view.x)*56.0F+4, y=128.0F+(origin->y-view.y)*56.0F+4;
    tap(x,y,SDL_BUTTON_RIGHT); ASSERT_TRUE(A::menu(*app)); EXPECT_EQ(A::menu(*app)->instanceId,weapon);
    choose(UiItemAction::WeaponDetails);
    EXPECT_EQ(A::details(*app),weapon);
    tap(900,105); EXPECT_FALSE(A::details(*app)); EXPECT_TRUE(A::inventory(*app));
}
TEST_F(WeaponInventoryUiTest, RaidModificationIsRejectedAndDoesNotMutate) {
    const auto fingerprint=profileStateFingerprint(profile());
    A::raidRightClick(*app); ASSERT_TRUE(A::menu(*app)); A::raidMenuClick(*app);
    EXPECT_FALSE(A::modifying(*app)); EXPECT_EQ(A::message(*app),"MODIFICATION REQUIRES BASE");
    EXPECT_EQ(profileStateFingerprint(profile()),fingerprint);
}
TEST(WeaponInventoryLabelsTest, MenuAndReturnAreBilingual) {
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"WEAPON DETAILS"),"枪械详情");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"MODIFY WEAPON"),"改装");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"BACK TO INVENTORY"),"返回背包");
}
}
