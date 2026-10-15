#include "app.h"
#include "weapon_component_domain.h"
#include "ui_localization.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

struct TacticalDeviceUiTestAccess {
    static GameFlow &flow(App &a) {return a.gameFlow_;}
    static void frame(App &a,float seconds=0) {a.windowHasInputFocus_=true;a.processEvents();a.update(seconds);a.input_.endFrame();}
    static void detail(App &a,AssetInstanceId weapon) {a.inventoryOverlayState_.openPlayerInventory();a.profileDetailsAsset_=weapon;}
    static bool dragging(App &a) {return a.deviceBatteryDrag_.has_value();}
    static bool details(App &a) {return a.profileDetailsAsset_.has_value();}
    static void menu(App &a,bool open) {if(open)a.inventoryOverlayState_.openPlayerInventory();else a.closeInventory();}
    static void clearWorld(App &a) {RaidWorldConfig c;c.lightConeObservationEnabled=true;c.worldSize={1280,720};c.playerSpawn={375,165};c.extractionPoint={{1100,600},{100,100}};a.gameSession_.world_=std::make_unique<GameplayWorld>(c);}
    static void enemies(App &a) {
        RaidWorldConfig c;c.lightConeObservationEnabled=true;c.worldSize={1280,720};c.playerSpawn={375,165};c.extractionPoint={{1100,600},{100,100}};
        c.initialEnemies={EnemySpawn{{300,165},{50,50},100},EnemySpawn{{1100,165},{50,50},100}};
        a.gameSession_.world_=std::make_unique<GameplayWorld>(c);
        auto &w=*a.gameSession_.world_;w.setRaidDaylight(false);GameplayInput i;i.aimWorldPosition=Vec2{1200,190};w.update(i,.6F);
        ASSERT_TRUE(const_cast<Enemy&>(w.enemies()[0]).setPosition({300,165}));ASSERT_TRUE(const_cast<Enemy&>(w.enemies()[1]).setPosition({1100,165}));
        auto *t=SDL_CreateTexture(a.renderer_,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_STATIC,1536,640);ASSERT_NE(t,nullptr);
        std::vector<Uint32> pixels(1536U*640U,0xEA32D2FFU);ASSERT_TRUE(SDL_UpdateTexture(t,nullptr,pixels.data(),1536*4));ASSERT_TRUE(SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND));a.enemyMoveHorizontalTexture_=Texture{t};
    }
    static void fault(App &a,bool gun) {auto b=a.gameSession_.activeWeaponLight().binding;auto &p=a.gameSession_.profile_;p.assets.findMutable(b.deviceAssetId)->deviceState->interrupted=true;p.assets.findMutable(b.deviceAssetId)->deviceState->switchOn=true;if(gun)p.assets.findMutable(b.weaponAssetId)->weaponMalfunction=WeaponMalfunctionType::Stovepipe;a.gameSession_.synchronizeWeaponLight();}
    static void attach(App &a,SDL_Renderer *r) {a.renderer_=r;EXPECT_TRUE(a.uiTextRenderer_.initialize(r));}
    static void paintDetail(App &a) {a.renderProfileWeaponDetails();}
    static void paintSupply(App &a) {a.renderBaseSupply();}
    static void paintModification(App &a,AssetInstanceId id) {a.componentWeapon_=id;a.componentSlot_=WeaponComponentSlot::Light;a.renderWeaponComponents();}
    static void paintRaid(App &a) {a.renderRaidScreen();}
    static void detach(App &a) {a.indoorInformationLayer_.reset();a.indoorShadowMask_.reset();a.enemyMoveHorizontalTexture_.reset();a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
};
namespace {
using A=TacticalDeviceUiTestAccess;
const auto &c() {return publishedContentRegistry();}
AssetInstanceId find(const ProfileState &p,const char *d) {for(const auto &[id,a]:p.assets.records())if(a.definitionId==ItemDefinitionId{d})return id;return 0;}
void button(float x,float y,bool down) {SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;ASSERT_TRUE(SDL_PushEvent(&e));}
void key(SDL_Scancode scan,bool down) {SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=scan;e.key.down=down;ASSERT_TRUE(SDL_PushEvent(&e));}
class WeaponLightUiTest:public testing::Test {
protected:
    std::unique_ptr<App> app;SDL_Surface *surface{};SDL_Renderer *renderer{};AssetInstanceId weapon{},lamp{},source{};
    std::filesystem::path path=std::filesystem::temp_directory_path()/("raidline-light-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    GameSession &session(){return A::flow(*app).gameSession();}
    void SetUp()override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));auto p=makeNewPublishedProfile("light-ui",c());
        weapon=find(p,"item.weapon.rifle_5_45_service");
        for(const auto *d:{"item.component.weapon_flashlight_basic","item.battery.universal"}) {const auto &def=c().item(ItemDefinitionId{d});const auto fit=findFirstProfileFit(p,c(),ProfileContainerId::stash(),def,ItemOrientation::Degrees0);ASSERT_TRUE(fit);static_cast<void>(p.assets.create(def,StoredAssetLocation{ProfileContainerId::stash(),*fit},def.battery?3:1));}
        lamp=find(p,"item.component.weapon_flashlight_basic");source=find(p,"item.battery.universal");
        for(const auto *d:{"item.component.rail_handguard","item.component.weapon_flashlight_basic"}) {const auto id=find(p,d);ASSERT_TRUE(executeWeaponComponentChange(p,c(),{weapon,c().item(p.assets.find(id)->definitionId).weaponComponent->slot,id},{p.revision,d}).succeeded);}
        ASSERT_TRUE(executeInventory(p,c(),InventoryEquipCommand{weapon,EquipmentSlotKind::PrimaryWeapon},{p.revision,"equip"}).succeeded);
        const auto bag=find(p,"item.container.backpack_small");ASSERT_TRUE(executeInventory(p,c(),InventoryEquipCommand{bag,EquipmentSlotKind::Backpack},{p.revision,"bag"}).succeeded);
        ASSERT_TRUE(executeInventory(p,c(),InventoryMoveCommand{source,0,{ProfileContainerId::compartment(bag,0),{0,0}}},{p.revision,"carry-batteries"}).succeeded);
        ASSERT_TRUE(executeInventory(p,c(),InventoryEquipCommand{find(p,"item.weapon.pistol_basic"),EquipmentSlotKind::Sidearm},{p.revision,"pistol"}).succeeded);
        ASSERT_TRUE(executeDeviceBatteryChange(p,c(),{lamp,source},{},{p.revision,"battery"}).succeeded);
        ASSERT_TRUE(SaveRepository(path).save(p,c().contentVersion()).succeeded);app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());A::frame(*app);
        surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);A::attach(*app,renderer);
    }
    void TearDown()override {if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);app.reset();SDL_Quit();std::error_code ec;std::filesystem::remove_all(path,ec);}
    void save(const char *name) {ASSERT_TRUE(SDL_RenderPresent(renderer));if(const char *dir=std::getenv("RAIDLINE_LIGHT_CONE_QA_DIR")) {std::filesystem::create_directories(dir);EXPECT_TRUE(SDL_SaveBMP(surface,(std::filesystem::path(dir)/name).string().c_str()));}}
    void raid() {ASSERT_TRUE(A::flow(*app).deploy(MapDefinitionId{"map.raid.frontier_exchange"}));A::clearWorld(*app);A::frame(*app);}
};
TEST_F(WeaponLightUiTest, DetailsDragReplacesOneCellAndEscapeCancelsWithoutFiring) {
    A::paintSupply(*app);save("weapon-light-supply-zh.bmp");A::paintModification(*app,weapon);save("weapon-light-modification-zh.bmp");
    A::detail(*app,weapon);A::paintDetail(*app);save("weapon-light-details-zh.bmp");
    int bright=0;for(int y=100;y<120;++y)for(int x=70;x<190;++x){Uint8 r{},g{},b{},a{};ASSERT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));if(r>150&&g>150&&b>150)++bright;}EXPECT_GT(bright,40);
    const auto old=session().activeWeaponLight().binding.batteryAssetId;
    button(85,510,true);A::frame(*app);ASSERT_TRUE(A::dragging(*app));button(1120,380,false);A::frame(*app);
    const auto b=findWeaponLight(session().profile(),c(),weapon);EXPECT_NE(b.batteryAssetId,old);EXPECT_EQ(session().profile().assets.find(source)->quantity,1U);EXPECT_TRUE(std::holds_alternative<StoredAssetLocation>(session().profile().assets.find(old)->location));
    button(85,510,true);A::frame(*app);ASSERT_TRUE(A::dragging(*app));key(SDL_SCANCODE_ESCAPE,true);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);EXPECT_FALSE(A::dragging(*app));EXPECT_FALSE(A::details(*app));
    button(1120,380,false);A::frame(*app);EXPECT_EQ(findWeaponLight(session().profile(),c(),weapon).batteryAssetId,b.batteryAssetId);EXPECT_FALSE(A::flow(*app).baseWorld().shotFiredLastUpdate());
}
TEST_F(WeaponLightUiTest, ShortUReleaseTogglesOnceLongPressAndMenuConsumeWithoutToggle) {
    raid();const auto b=session().activeWeaponLight().binding;key(SDL_SCANCODE_U,true);A::frame(*app,.05F);EXPECT_FALSE(session().activeWeaponLight().switchOn);
    key(SDL_SCANCODE_U,false);A::frame(*app,.05F);EXPECT_TRUE(session().activeWeaponLight().output);const auto remaining=session().activeWeaponLight().remainingMs;
    key(SDL_SCANCODE_U,true);A::frame(*app,.4F);key(SDL_SCANCODE_U,false);A::frame(*app,.01F);EXPECT_TRUE(session().activeWeaponLight().switchOn);EXPECT_LT(session().activeWeaponLight().remainingMs,remaining);
    key(SDL_SCANCODE_U,true);A::frame(*app,.01F);A::menu(*app,true);A::frame(*app);key(SDL_SCANCODE_U,false);A::frame(*app);A::menu(*app,false);A::frame(*app);EXPECT_TRUE(session().activeWeaponLight().switchOn);
    EXPECT_EQ(session().activeWeaponLight().binding.batteryAssetId,b.batteryAssetId);A::paintRaid(*app);save("weapon-light-raid-zh.bmp");
}
TEST_F(WeaponLightUiTest, DeviceGestureWorksWithoutGunFaultAndCanClearBothTogether) {
    raid();for(bool gun:{false,true}) {A::fault(*app,gun);const auto charge=session().activeWeaponLight().remainingMs;bool cleared=false;for(int n=0;n<7;++n)cleared|=session().observeAlphaWeaponClearMotion({n%2?-50.0F:50.0F,0});EXPECT_TRUE(cleared);EXPECT_FALSE(session().activeWeaponLight().interrupted);EXPECT_EQ(session().activeWeaponLight().remainingMs,charge);EXPECT_EQ(session().profile().assets.find(weapon)->weaponMalfunction,WeaponMalfunctionType::None);}
}
TEST_F(WeaponLightUiTest, BaseAndPausedRaidDoNotUseCharge) {
    auto before=session().activeWeaponLight().remainingMs;A::frame(*app,1);EXPECT_EQ(session().activeWeaponLight().remainingMs,before);raid();ASSERT_TRUE(session().toggleWeaponLight().succeeded);before=session().activeWeaponLight().remainingMs;
    key(SDL_SCANCODE_ESCAPE,true);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);A::frame(*app,100);EXPECT_EQ(session().activeWeaponLight().remainingMs,before);
}
TEST_F(WeaponLightUiTest, RaidDetailsReplaceFromCarriedStackAndKeepOldCharge) {
    raid();ASSERT_TRUE(session().toggleWeaponLight().succeeded);session().update({},1);
    const auto old=session().activeWeaponLight().binding.batteryAssetId;const auto charge=session().activeWeaponLight().remainingMs;
    A::detail(*app,weapon);button(85,510,true);A::frame(*app);ASSERT_TRUE(A::dragging(*app));button(1120,380,false);A::frame(*app);
    EXPECT_NE(session().activeWeaponLight().binding.batteryAssetId,old);EXPECT_EQ(session().profile().assets.find(old)->remainingCharges,charge);EXPECT_TRUE(assetIsCarried(session().profile(),old));EXPECT_TRUE(session().activeWeaponLight().output);
    A::paintDetail(*app);save("weapon-light-raid-battery-zh.bmp");
}
TEST_F(WeaponLightUiTest, SwitchingStopsChargeAndRetainsIntentAndFault) {
    raid();ASSERT_TRUE(session().toggleWeaponLight().succeeded);session().update({},.1F);const auto b=session().activeWeaponLight().binding;
    const auto charge=session().activeWeaponLight().remainingMs;GameplayInput i;i.weaponSlotJustPressed=EquipmentSlotKind::Sidearm;session().update(i,.05F);
    for(int n=0;n<15;++n)session().update({},.1F);EXPECT_FALSE(session().activeWeaponLight().output);EXPECT_EQ(session().profile().assets.find(b.batteryAssetId)->remainingCharges,charge);
    i.weaponSlotJustPressed=EquipmentSlotKind::PrimaryWeapon;session().update(i,.05F);for(int n=0;n<15;++n)session().update({},.1F);EXPECT_TRUE(session().activeWeaponLight().output);
    A::fault(*app,false);const auto low=session().activeWeaponLight().remainingMs;i.weaponSlotJustPressed=EquipmentSlotKind::Sidearm;session().update(i,.05F);for(int n=0;n<15;++n)session().update({},.1F);
    i.weaponSlotJustPressed=EquipmentSlotKind::PrimaryWeapon;session().update(i,.05F);for(int n=0;n<15;++n)session().update({},.1F);EXPECT_TRUE(session().activeWeaponLight().interrupted);EXPECT_EQ(session().activeWeaponLight().remainingMs,low);
}
TEST_F(WeaponLightUiTest, PauseCancelsPendingUAndMenuMouseCannotRestoreDevice) {
    raid();key(SDL_SCANCODE_U,true);A::frame(*app,.01F);key(SDL_SCANCODE_ESCAPE,true);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);
    key(SDL_SCANCODE_ESCAPE,true);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);key(SDL_SCANCODE_U,false);A::frame(*app);EXPECT_FALSE(session().activeWeaponLight().switchOn);
    A::fault(*app,false);A::menu(*app,true);for(int n=0;n<7;++n){SDL_Event e{};e.type=SDL_EVENT_MOUSE_MOTION;e.motion.x=500;e.motion.y=300;e.motion.xrel=n%2?-50.0F:50.0F;ASSERT_TRUE(SDL_PushEvent(&e));A::frame(*app,.01F);}EXPECT_TRUE(session().activeWeaponLight().interrupted);
}
TEST_F(WeaponLightUiTest, ContinuousOutputDoesNotWriteCheckpointEveryFrame) {
    raid();const auto file=SaveRepository(path).primaryPath();const auto read=[&]{std::ifstream f(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});};const auto original=read();
    ASSERT_TRUE(session().toggleWeaponLight().succeeded);for(int n=0;n<100;++n)session().update({},.016F);EXPECT_EQ(read(),original);EXPECT_LT(session().activeWeaponLight().remainingMs,600000U);
    GameSession reopened;reopened.configurePersistence(path);ASSERT_TRUE(reopened.continueProfile());
    EXPECT_FALSE(reopened.alphaRaidActive());const auto restored=findWeaponLight(reopened.profile(),c(),weapon);
    EXPECT_EQ(queryWeaponLight(reopened.profile(),c(),restored,false).remainingMs,600000U);EXPECT_FALSE(queryWeaponLight(reopened.profile(),c(),restored,false).switchOn);
}
TEST_F(WeaponLightUiTest, PoweredBeamShowsFarFrontEnemyAndNeverRearEnemyInWeakBackground) {
    raid();A::enemies(*app);ASSERT_TRUE(session().toggleWeaponLight().succeeded);A::paintRaid(*app);save("weapon-light-front-and-rear.bmp");
    const auto pink=[&](int x,int y){Uint8 r{},g{},b{},a{};EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));return r>210&&g<90&&b>180;};
    EXPECT_TRUE(pink(1125,190));EXPECT_FALSE(pink(325,190));EXPECT_FALSE(session().world().raidEnemyObservable(session().world().enemies()[0].combatTargetId()));
    ASSERT_TRUE(session().toggleWeaponLight().succeeded);A::paintRaid(*app);save("weapon-light-off-night.bmp");EXPECT_FALSE(pink(1125,190));
}
TEST(WeaponLightLabelsTest, PlayerControlsAndItemsAreChinese) {
    EXPECT_EQ(localizeUiLabel(UiLanguage::SimplifiedChinese,"Weapon Flashlight",14),"武器手电");
    EXPECT_EQ(localizeUiLabel(UiLanguage::SimplifiedChinese,"Universal Battery",14),"通用电池");
    EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"SUPPLY & RECOVERY | CURRENCY 200"),"供应与回收 | 货币 200");
    for(const auto *text:{"WEAPON LIGHT","BATTERY SLOT","Universal Battery","DRAG ONE BATTERY INTO LIGHT","ONLY WEAPON LIGHT | TAP U","LIGHT INTERRUPTED | SHAKE TO RESTORE"}) EXPECT_NE(localizeUiText(UiLanguage::SimplifiedChinese,text),text);
}
}
