#include "app.h"
#include "alpha_content_ids.h"
#include "weapon_component_domain.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <gtest/gtest.h>
#include <SDL3_image/SDL_image.h>

struct UiBaseSupplyTestAccess {
    static auto &flow(App &a) { return a.gameFlow_; }
    static auto &session(App &a) { return a.gameSession_; }
    static auto &state(App &a) { return a.supplyUi_; }
    static const auto &message(App &a) { return a.uiMessage_; }
    static void message(App &a, std::string s) { a.uiMessage_=std::move(s); }
    static auto &text(App &a) { return a.uiTextRenderer_; }
    static bool components(App &a) { return a.weaponComponentsOpen_; }
    static void frame(App &a) { a.processEvents(); a.update(0); a.input_.endFrame(); }
    static void render(App &a) { a.renderBaseSupply(); }
    static void attach(App &a, SDL_Renderer *r, float scale) {
        a.renderer_=r; ASSERT_TRUE(a.uiTextRenderer_.initialize(r)); a.uiTextRenderer_.setRasterScale(scale);
        const auto root=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets";
        for(std::size_t i=0;i<itemCount();++i) {
            const auto &d=publishedContentRegistry().item(legacyItemDefinitionId(static_cast<ItemId>(i)));
            a.inventoryItemTextures_[i]=loadInventoryTexture(r,root/d.inventoryTexturePath);
        }
    }
    static void detach(App &a) {
        for(auto &t:a.inventoryItemTextures_)t.reset(); a.uiTextRenderer_.shutdown(); a.renderer_=nullptr;
    }
};
namespace {
using A=UiBaseSupplyTestAccess; using Tab=UiBaseSupplyTab; using Control=UiBaseSupplyControl;
Vec2 center(SDL_FRect r) { return {r.x+r.w/2,r.y+r.h/2}; }
void pointer(Vec2 p, bool down) {
    SDL_Event e{};e.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.button=SDL_BUTTON_LEFT;e.button.down=down;e.button.x=p.x;e.button.y=p.y;ASSERT_TRUE(SDL_PushEvent(&e));
}
void key(SDL_Scancode code,bool down) {
    SDL_Event e{};e.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;e.key.scancode=code;e.key.down=down;ASSERT_TRUE(SDL_PushEvent(&e));
}
class UiBaseSupplyTest:public testing::Test {
protected:
    std::unique_ptr<App> app; SDL_Surface *surface{}; SDL_Renderer *renderer{}; std::filesystem::path path;
    const ContentRegistry &content=publishedContentRegistry();
    auto &session() {return A::session(*app);}
    ProfileState &profile() {return const_cast<ProfileState &>(session().profile());}
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        path=std::filesystem::temp_directory_path()/("raidline-supply-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(path));
        auto p=makeNewPublishedProfile("supply-ui",content);p.currency=100000;p.tutorial=TutorialProgress::Complete;p.homeFounding.hintsDismissed=true;
        ASSERT_TRUE(SaveRepository(path).save(p,content.contentVersion()).succeeded);
        app=std::make_unique<App>();A::flow(*app).configurePersistence(path);ASSERT_TRUE(A::flow(*app).continueGame());
        A::frame(*app);ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Supply));
        attach(1280,720);
    }
    void attach(int w,int h) {
        surface=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));A::attach(*app,renderer,w/1280.0F);
    }
    void detach() {if(app)A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);renderer=nullptr;surface=nullptr;}
    void TearDown() override {
        detach();app.reset();SDL_Quit();
        if(path.parent_path().lexically_normal()==std::filesystem::temp_directory_path().lexically_normal() && path.filename().string().starts_with("raidline-supply-ui-")) {
            std::error_code ec;std::filesystem::remove_all(path,ec);
        }
    }
    void click(SDL_FRect box) {const auto p=center(box);pointer(p,true);pointer(p,false);A::frame(*app);}
    void control(Control c) {click(uiBaseSupplyControlRect(c));}
    void tab(Tab t) {click(uiBaseSupplyTabRect(static_cast<std::size_t>(t)));}
    auto rows() {return projectUiBaseSupplyRows(profile(),content,A::state(*app).tab,A::text(*app).language());}
    void select(const ItemDefinitionId &definition,AssetInstanceId id=0) {
        const auto list=rows();const auto it=std::find_if(list.begin(),list.end(),[&](const auto &r){return r.key.definitionId==definition && (!id || r.key.instanceId==id);});
        ASSERT_NE(it,list.end());const auto index=static_cast<std::size_t>(it-list.begin());
        const auto page=index/8;
        while(A::state(*app).currentPage()<page)control(Control::Next);
        while(A::state(*app).currentPage()>page)control(Control::Previous);
        click(uiBaseSupplyRowRect(index%8));ASSERT_EQ(A::state(*app).selection,it->key);
    }
    AssetInstanceId create(const ItemDefinitionId &id,std::uint32_t quantity=1) {
        const auto &d=content.item(id);const auto fit=findFirstProfileFit(profile(),content,ProfileContainerId::stash(),d,ItemOrientation::Degrees0);
        if(!fit){ADD_FAILURE()<<"test Stash is full";return 0;}
        const auto result=profile().assets.create(d,StoredAssetLocation{ProfileContainerId::stash(),*fit},quantity);++profile().revision;return result;
    }
    std::uint64_t quantity(const ItemDefinitionId &id) {
        std::uint64_t result=0;for(const auto &[n,a]:profile().assets.records())if(a.definitionId==id)result+=a.quantity;return result;
    }
    void fullStash() {
        const auto &d=content.item(alpha_content::ammunition);
        for(int n=0;n<4096;++n) {
            auto fit=findFirstProfileFit(profile(),content,ProfileContainerId::stash(),d,ItemOrientation::Degrees0);
            if(!fit){++profile().revision;return;}
            static_cast<void>(profile().assets.create(d,StoredAssetLocation{ProfileContainerId::stash(),*fit},1));
        }
        FAIL()<<"unbounded test Stash";
    }
    void hold(AssetInstanceId id) {
        auto *a=profile().assets.findMutable(id);ASSERT_NE(a,nullptr);const auto from=std::get<StoredAssetLocation>(a->location);
        const auto job=profile().nextBaseServiceJobId++;a->location=BaseServiceAssetLocation{job};
        profile().gunsmithMaintenanceJob=GunsmithMaintenanceJob{job,id,from.origin,0,240,130,10000};++profile().revision;
    }
    void draw(std::string name) {
        SDL_SetRenderDrawColor(renderer,13,20,24,255);SDL_RenderClear(renderer);A::render(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));
        if(const char *dir=std::getenv("RAIDLINE_SUPPLY_QA_DIR")) {
            std::filesystem::create_directories(dir);ASSERT_TRUE(SDL_SaveBMP(surface,(std::filesystem::path(dir)/(name+".bmp")).string().c_str()));
        }
    }
};
}

TEST_F(UiBaseSupplyTest, EveryCatalogRowReachableAndBrowsingNeverTrades) {
    const auto before=profileStateFingerprint(profile());const auto ids=content.fixedSupplyItemIds();ASSERT_EQ(ids.size(),29);
    EXPECT_NE(std::find(ids.begin(), ids.end(), ItemDefinitionId{"item.component.suppressor_5_45"}), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), ItemDefinitionId{"item.throwable.frag_grenade_basic"}), ids.end());
    for(const auto &id:ids) {select(id);draw("catalog");}
    tab(Tab::Recycle);control(Control::Next);tab(Tab::Maintenance);tab(Tab::Purchase);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, AmmoPurchaseUsesOriginalThirtyBatchAndOneClickOnly) {
    select(alpha_content::ammunition);const auto before=quantity(alpha_content::ammunition);const auto money=profile().currency;
    const auto main=center(uiBaseSupplyControlRect(Control::Main));pointer(main,true);A::frame(*app);
    EXPECT_EQ(quantity(alpha_content::ammunition),before);for(int n=0;n<5;++n)A::frame(*app);
    pointer(main,false);A::frame(*app);EXPECT_EQ(quantity(alpha_content::ammunition),before+30);
    EXPECT_EQ(profile().currency,money-content.item(alpha_content::ammunition).marketBuyPrice*30);
    ASSERT_TRUE(A::state(*app).selection);pointer(main,false);A::frame(*app);EXPECT_EQ(quantity(alpha_content::ammunition),before+30);
}
TEST_F(UiBaseSupplyTest, OrdinaryPurchaseUsesOneAndRetainsSelection) {
    select(alpha_content::noiseDecoy);const auto before=quantity(alpha_content::noiseDecoy);control(Control::Main);
    EXPECT_EQ(quantity(alpha_content::noiseDecoy),before+1);EXPECT_TRUE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, MoveOutAndBackCancelsArmedPurchase) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());
    const auto main=center(uiBaseSupplyControlRect(Control::Main));pointer(main,true);A::frame(*app);
    SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.x=20;move.motion.y=20;SDL_PushEvent(&move);A::frame(*app);
    pointer(main,false);A::frame(*app);EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, WheelCancelsPressAndClearsSelectionWithoutChangingStorage) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());
    const auto main=center(uiBaseSupplyControlRect(Control::Main));pointer(main,true);A::frame(*app);
    SDL_Event e{};e.type=SDL_EVENT_MOUSE_WHEEL;e.wheel.y=-1;e.wheel.mouse_x=400;e.wheel.mouse_y=300;SDL_PushEvent(&e);A::frame(*app);
    pointer(main,false);A::frame(*app);EXPECT_FALSE(A::state(*app).selection);EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, TabsKeepIndependentPagesAndClearSelections) {
    control(Control::Next);EXPECT_EQ(A::state(*app).currentPage(),1);
    tab(Tab::Recycle);EXPECT_EQ(A::state(*app).currentPage(),0);control(Control::Next);
    tab(Tab::Purchase);EXPECT_EQ(A::state(*app).currentPage(),1);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, CloseAndReopenResetsPagesAndIgnoresOldRelease) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());
    pointer(center(uiBaseSupplyControlRect(Control::Main)),true);A::frame(*app);
    key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);
    EXPECT_FALSE(A::flow(*app).activeBaseFacility());ASSERT_TRUE(A::flow(*app).openBaseFacilityForManagement(BaseFacilityKind::Supply));
    pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);
    EXPECT_EQ(A::state(*app).currentPage(),0);EXPECT_FALSE(A::state(*app).selection);EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, FocusLossDisarmsPurchase) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());pointer(center(uiBaseSupplyControlRect(Control::Main)),true);A::frame(*app);
    SDL_Event e{};e.type=SDL_EVENT_WINDOW_FOCUS_LOST;SDL_PushEvent(&e);A::frame(*app);
    e.type=SDL_EVENT_WINDOW_FOCUS_GAINED;SDL_PushEvent(&e);A::frame(*app);
    key(SDL_SCANCODE_ESCAPE,true);A::frame(*app);key(SDL_SCANCODE_ESCAPE,false);A::frame(*app);
    pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, RecycleBeyondTwelveUsesExactInstanceAndWholeStack) {
    for(int i=0;i<14;++i)create(alpha_content::ammunition,1);
    const auto target=create(alpha_content::ammunition,17);const auto other=create(alpha_content::ammunition,13);
    tab(Tab::Recycle);select(alpha_content::ammunition,target);EXPECT_GT(A::state(*app).currentPage(),0);
    const auto money=profile().currency;control(Control::Main);
    EXPECT_EQ(profile().assets.find(target),nullptr);ASSERT_NE(profile().assets.find(other),nullptr);EXPECT_EQ(profile().assets.find(other)->quantity,13);
    EXPECT_EQ(profile().currency,money+content.item(alpha_content::ammunition).marketRecyclePrice*17);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, RemovingTargetBetweenDownAndUpCannotRecycleNextRow) {
    const auto id=create(alpha_content::ammunition,17);tab(Tab::Recycle);select(alpha_content::ammunition,id);
    pointer(center(uiBaseSupplyControlRect(Control::Main)),true);A::frame(*app);ASSERT_TRUE(profile().assets.erase(id));++profile().revision;
    const auto before=profileStateFingerprint(profile());pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, RowPressBindsIdentityDespiteReordering) {
    const auto id=create(alpha_content::ammunition);tab(Tab::Recycle);select(alpha_content::ammunition,id);
    const auto list=rows();const auto i=std::find_if(list.begin(),list.end(),[&](auto &r){return r.key.instanceId==id;})-list.begin();
    const auto pos=center(uiBaseSupplyRowRect(i%8));pointer(pos,true);A::frame(*app);
    ASSERT_TRUE(profile().assets.erase(id));++profile().revision;pointer(pos,false);A::frame(*app);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, ReliefRecycleRejectionIsAtomicAndKeepsTarget) {
    const auto id=create(alpha_content::ammunition,9);profile().assets.findMutable(id)->reliefBatchId="ui-relief";++profile().revision;
    tab(Tab::Recycle);select(alpha_content::ammunition,id);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_EQ(A::state(*app).selection->instanceId,id);EXPECT_EQ(A::message(*app),"asset cannot be recycled");
}
TEST_F(UiBaseSupplyTest, InsufficientFundsRejectsWithoutInventingAFreeQuote) {
    profile().currency=0;++profile().revision;select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_EQ(A::message(*app),"currency is insufficient");
}
TEST_F(UiBaseSupplyTest, FullStashRejectsPurchaseAtomically) {
    fullStash();select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_EQ(A::message(*app),"Stash cannot hold the purchase");
}
TEST_F(UiBaseSupplyTest, SaveFailureLeavesCurrencyAssetsAndHighWaterUnchanged) {
    select(alpha_content::ammunition);const auto blocker=path/"not-a-directory";std::ofstream(blocker)<<"blocked";
    session().configurePersistence(blocker);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_FALSE(A::message(*app).empty());
}
TEST_F(UiBaseSupplyTest, FirearmMaintenanceIsInstantAndUsesExistingQuote) {
    const auto id=create(alpha_content::pistol);auto *a=profile().assets.findMutable(id);a->currentDurability=5000;a->currentMaximumDurability=8000;a->weaponMalfunction=WeaponMalfunctionType::Stovepipe;++profile().revision;
    const auto quote=queryGunsmithMaintenance(profile(),content,{id});ASSERT_TRUE(quote.canCommit);
    tab(Tab::Maintenance);select(alpha_content::pistol,id);const auto money=profile().currency;const auto time=profile().worldClock.elapsedWorldMinutes;control(Control::Main);
    EXPECT_EQ(profile().currency,money-quote.quotedCurrency);EXPECT_EQ(profile().worldClock.elapsedWorldMinutes,time);EXPECT_FALSE(profile().gunsmithMaintenanceJob);
    EXPECT_EQ(profile().assets.find(id)->currentDurability,10000);EXPECT_EQ(profile().assets.find(id)->weaponMalfunction,WeaponMalfunctionType::None);
}
TEST_F(UiBaseSupplyTest, MeleeMaintenanceRemainsAvailableAndFirst) {
    const auto id=create(alpha_content::utilityKnife);profile().assets.findMutable(id)->currentDurability=9950;++profile().revision;
    tab(Tab::Maintenance);ASSERT_EQ(rows().front().key.instanceId,id);select(alpha_content::utilityKnife,id);const auto money=profile().currency;control(Control::Main);
    EXPECT_EQ(profile().assets.find(id)->currentDurability,10000);EXPECT_EQ(profile().currency,money-21);
}
TEST_F(UiBaseSupplyTest, LegacyHeldWeaponCollectsWithoutStartingAnotherJob) {
    const auto id=create(alpha_content::pistol);hold(id);tab(Tab::Maintenance);draw("held");control(Control::Main);
    EXPECT_FALSE(profile().gunsmithMaintenanceJob);ASSERT_NE(profile().assets.find(id),nullptr);
    EXPECT_EQ(std::get<StoredAssetLocation>(profile().assets.find(id)->location).container,ProfileContainerId::stash());
}
TEST_F(UiBaseSupplyTest, FullStashPreservesLegacyHeldWeaponAndJob) {
    const auto id=create(alpha_content::pistol);hold(id);fullStash();tab(Tab::Maintenance);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_TRUE(profile().gunsmithMaintenanceJob);
}
TEST_F(UiBaseSupplyTest, ComponentRoundTripClearsSelectionAndDoesNotTrade) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());control(Control::Components);ASSERT_TRUE(A::components(*app));
    draw("components");click(uiComponentControl(UiComponentControl::Back));EXPECT_FALSE(A::components(*app));EXPECT_FALSE(A::state(*app).selection);
    pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, InstalledComponentsAreIncludedInWholeWeaponRecycleValueOnce) {
    const ItemDefinitionId weaponId{"item.weapon.rifle_5_45_service"}, partId{"item.component.precision_barrel"};
    AssetInstanceId weapon{},part{};
    for(const auto &[id,a]:profile().assets.records()) {if(a.definitionId==weaponId)weapon=id;if(a.definitionId==partId)part=id;}
    ASSERT_NE(weapon,0);ASSERT_NE(part,0);
    ASSERT_TRUE(executeWeaponComponentChange(profile(),content,{weapon,WeaponComponentSlot::Barrel,part},{profile().revision,"ui-install-part"}).succeeded);
    tab(Tab::Recycle);const auto list=rows();EXPECT_FALSE(std::any_of(list.begin(),list.end(),[&](const auto &r){return r.key.instanceId==part;}));
    select(weaponId,weapon);const auto it=std::find_if(list.begin(),list.end(),[&](const auto &r){return r.key.instanceId==weapon;});ASSERT_NE(it,list.end());
    ASSERT_TRUE(it->quote);EXPECT_GT(it->componentValue,0);const auto money=profile().currency;const auto value=*it->quote;
    control(Control::Main);EXPECT_EQ(profile().assets.find(weapon),nullptr);EXPECT_EQ(profile().assets.find(part),nullptr);EXPECT_EQ(profile().currency,money+value);
    control(Control::Main);EXPECT_EQ(profile().currency,money+value);
}
TEST_F(UiBaseSupplyTest, NonemptyContainerCannotBeRecycledAndChildrenAreNotListed) {
    const auto owner=create(alpha_content::backpack);const auto &d=content.item(alpha_content::ammunition);
    const auto container=ProfileContainerId::compartment(owner,0);const auto fit=findFirstProfileFit(profile(),content,container,d,ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    const auto child=profile().assets.create(d,StoredAssetLocation{container,*fit},5);++profile().revision;
    tab(Tab::Recycle);const auto list=rows();EXPECT_FALSE(std::any_of(list.begin(),list.end(),[&](const auto &r){return r.key.instanceId==child;}));
    select(alpha_content::backpack,owner);const auto before=profileStateFingerprint(profile());control(Control::Main);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_EQ(A::message(*app),"asset cannot be recycled");
}
TEST_F(UiBaseSupplyTest, MovingTargetOutOfDirectStashCancelsAnArmedRecycle) {
    const auto owner=create(alpha_content::backpack),id=create(alpha_content::ammunition,7);
    tab(Tab::Recycle);select(alpha_content::ammunition,id);pointer(center(uiBaseSupplyControlRect(Control::Main)),true);A::frame(*app);
    const auto container=ProfileContainerId::compartment(owner,0);const auto fit=findFirstProfileFit(profile(),content,container,content.item(alpha_content::ammunition),ItemOrientation::Degrees0);ASSERT_TRUE(fit);
    profile().assets.findMutable(id)->location=StoredAssetLocation{container,*fit};++profile().revision;
    const auto before=profileStateFingerprint(profile());pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);
    EXPECT_EQ(profileStateFingerprint(profile()),before);EXPECT_FALSE(A::state(*app).selection);
}
TEST_F(UiBaseSupplyTest, ReliefButtonCannotIssueAnUnneededBatch) {
    const auto before=profileStateFingerprint(profile());ASSERT_FALSE(isReliefEligible(profile(),content));control(Control::Relief);
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, OpeningInventoryCancelsPurchaseAndKeepsExistingMenuFlow) {
    select(alpha_content::ammunition);const auto before=profileStateFingerprint(profile());pointer(center(uiBaseSupplyControlRect(Control::Main)),true);A::frame(*app);
    key(SDL_SCANCODE_TAB,true);A::frame(*app);key(SDL_SCANCODE_TAB,false);A::frame(*app);
    pointer(center(uiBaseSupplyControlRect(Control::Main)),false);A::frame(*app);EXPECT_FALSE(A::flow(*app).activeBaseFacility());EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST_F(UiBaseSupplyTest, RenderingAllSizesAndLanguagesDoesNotMutateOrGrowTextCache) {
    const auto knife=create(alpha_content::utilityKnife);profile().assets.findMutable(knife)->currentDurability=9950;++profile().revision;
    const auto before=profileStateFingerprint(profile());
    for(const auto size:kUiWindowSizes) {
        detach();attach(size.width,size.height);
        for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
            A::text(*app).setLanguage(language);
            for(auto t:{Tab::Purchase,Tab::Recycle,Tab::Maintenance}) {
                tab(t);if(t==Tab::Purchase)select(alpha_content::rifle);else select(alpha_content::utilityKnife,knife);
                const auto name=std::to_string(size.width)+(language==UiLanguage::English?"-en-":"-zh-")+std::to_string(static_cast<int>(t));draw(name);
                const auto count=A::text(*app).cachedLabelCount();for(int n=0;n<10;++n)A::render(*app);EXPECT_EQ(A::text(*app).cachedLabelCount(),count);
                A::message(*app,"Stash cannot hold the purchase");draw(name+"-error");
            }
        }
    }
    EXPECT_EQ(profileStateFingerprint(profile()),before);
}
TEST(UiBaseSupplyProjectionTest, EmptyPagesClampAndEveryControlFitsPanel) {
    EXPECT_EQ(uiBaseSupplyPageCount(0),1);EXPECT_EQ(uiBaseSupplyPageCount(27),4);
    UiBaseSupplyState state;state.pages[0]=20;state.reconcile({});EXPECT_EQ(state.currentPage(),0);
    for(auto c:{Control::Previous,Control::Next,Control::Main,Control::Relief,Control::Components,Control::Close}) {
        const auto r=uiBaseSupplyControlRect(c);EXPECT_GE(r.x,40);EXPECT_GE(r.y,70);EXPECT_LE(r.x+r.w,1240);EXPECT_LE(r.y+r.h,670);
    }
}
TEST_F(UiBaseSupplyTest, ImportantReasonsAndShortControlsFitWithoutEllipsisInBothLanguages) {
    for(float scale:{1.0F,1.25F,1.5F}) {
      A::text(*app).setRasterScale(scale);
    for(auto language:{UiLanguage::SimplifiedChinese,UiLanguage::English}) {
        A::text(*app).setLanguage(language);
        for(auto label:{"BUY","RECYCLE","MAINT.","PREV","NEXT","CLOSE SUPPLY"}) {
            const auto localized=localizeUiText(language,label);
            constexpr float width=108.0F;
            EXPECT_LE(A::text(*app).measure(localized,14)*1.04F,width)<<label;
            if(language==UiLanguage::SimplifiedChinese) EXPECT_TRUE(std::any_of(localized.begin(),localized.end(),[](unsigned char c){return c>127;}));
        }
        EXPECT_LE(A::text(*app).measure(localizeUiText(language,"WEAPON COMPONENTS"),14)*1.04F,194.0F);
        for(auto reason:{"Stash cannot hold the purchase","currency is insufficient","asset cannot be recycled",
            "currency is insufficient for gunsmith service","weapon already has factory condition"}) {
            const auto localized=localizeUiText(language,reason);
            const auto lines=wrapUiText(localized,676,0,[&](auto s){return A::text(*app).measure(s,12);});
            EXPECT_LE(lines.size(),2U)<<reason;
            EXPECT_TRUE(std::none_of(lines.begin(),lines.end(),[](const auto &s){return s.text.find("…")!=std::string::npos;}));
        }
        EXPECT_EQ(localizeUiText(UiLanguage::SimplifiedChinese,"BACK TO SUPPLY"),"返回供应与回收");
    }
    }
}
