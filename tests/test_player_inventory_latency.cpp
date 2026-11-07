#include "app.h"
#include "alpha_content_ids.h"
#include "test_temp_directory.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

struct PlayerInventoryLatencyTestAccess {
    static auto &flow(App &a){return a.gameFlow_;}
    static auto &session(App &a){return a.gameSession_;}
    static void open(App &a){a.inventoryOverlayState_.openContainerInventory();}
    static void frame(App &a){a.processEvents();a.update(0);a.input_.endFrame();}
    static bool attach(App &a,SDL_Renderer *r){a.renderer_=r;return a.uiTextRenderer_.initialize(r);}
    static void draw(App &a){a.renderProfileInventory(true,false);}
    static void detach(App &a){a.uiTextRenderer_.shutdown();a.renderer_=nullptr;}
    static std::size_t pending(App &a){return a.pendingInventoryUiFeedback_.size();}
};
namespace {
using A=PlayerInventoryLatencyTestAccess;
using Clock=std::chrono::steady_clock;
struct Move {AssetInstanceId id;StoredAssetLocation from,to;};
Move chooseMove(const ProfileState &p,const ContentRegistry &c) {
    for(const auto &[id,asset]:p.assets.records()) {
        const auto *location=std::get_if<StoredAssetLocation>(&asset.location);
        if(!location || location->container!=ProfileContainerId::stash())continue;
        const auto &definition=c.item(asset.definitionId);
        const auto footprint=inventoryFootprint(definition,asset.orientation);
        if(location->origin.x+footprint.width>8 || location->origin.y+footprint.height>8)continue;
        for(int y=0;y+footprint.height<=8;++y)for(int x=0;x+footprint.width<=8;++x) {
            if(location->origin==GridPosition{x,y})continue;
            if(profilePlacementFits(p,c,location->container,{x,y},definition,asset.orientation,id))
                return {id,*location,{location->container,{x,y}}};
        }
    }
    throw std::runtime_error("No visible alternate stash position");
}
void grow(ProfileState &p,const ContentRegistry &c,std::size_t count) {
    const auto &definition=c.item(alpha_content::ammunition);
    const auto size=profileContainerSize(p,c,ProfileContainerId::stash());
    // Preserve the visible page used by the real drag. The added inventory
    // still participates in the full command/validation/projection chain.
    for(int y=8;y<size.height && p.assets.records().size()<count;++y)
        for(int x=0;x<size.width && p.assets.records().size()<count;++x)
            if(profilePlacementFits(p,c,ProfileContainerId::stash(),{x,y},definition,ItemOrientation::Degrees0))
                static_cast<void>(p.assets.create(definition,
                    StoredAssetLocation{ProfileContainerId::stash(),{x,y}},1));
}
void pointer(Uint32 type,GridPosition cell) {
    SDL_Event e{};e.type=type;
    const float x=808+cell.x*56.0F+12,y=128+cell.y*56.0F+12;
    if(type==SDL_EVENT_MOUSE_MOTION){e.motion.x=x;e.motion.y=y;e.motion.xrel=56;e.motion.yrel=56;}
    else{e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;e.button.down=type==SDL_EVENT_MOUSE_BUTTON_DOWN;}
    ASSERT_TRUE(SDL_PushEvent(&e));
}
double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double p95(std::vector<double> values){std::sort(values.begin(),values.end());return values[static_cast<std::size_t>(std::ceil(values.size()*.95))-1];}
}

TEST(PlayerInventoryLatencyTest, RealSdlReleasePublishesPendingPreviewForSixtyAndTwoHundredAssets) {
    ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
    raidline::test::TemporaryDirectory directory;
    ASSERT_NO_THROW(directory.claim("raidline-player-inventory-latency-",
        std::string_view{testing::UnitTest::GetInstance()->current_test_info()->name()}.substr(0,24)));
    const auto &content=publishedContentRegistry();
    auto original=makeNewAlphaProfile("inventory-latency-synthetic",content);
    std::string baseline="synthetic-60";
    if(const auto source=SDL_getenv("RAIDLINE_INVENTORY_LATENCY_PROFILE_DIR")) {
        const auto clone=directory.path()/"readonly-source-clone";std::filesystem::create_directories(clone);
        for(const auto &entry:std::filesystem::directory_iterator(std::filesystem::path(source)))
            if(entry.is_regular_file())std::filesystem::copy_file(entry.path(),clone/entry.path().filename());
        const auto loaded=SaveRepository(clone).load(content);
        ASSERT_TRUE(loaded.profile)<<loaded.message;original=*loaded.profile;baseline="actual-clone-60";
    }
    original.tutorial=TutorialProgress::Complete;original.homeFounding.hintsDismissed=true;
    grow(original,content,60);ASSERT_EQ(original.assets.records().size(),60U);
    const auto originalMove=chooseMove(original,content);
    std::ofstream csv;
    if(const auto output=SDL_getenv("RAIDLINE_PLAYER_SYSTEMS_QA_DIR")) {
        std::filesystem::create_directories(output);csv.open(std::filesystem::path(output)/"actual-app-inventory-latency.csv");
        csv<<"scenario,assets,sample,release_to_preview_ms,release_to_software_present_ms,pending_requests\n";
    }
    for(const std::size_t count:{60U,200U}) {
        auto profile=original;grow(profile,content,count);ASSERT_EQ(profile.assets.records().size(),count);
        ASSERT_TRUE(validateProfileState(profile,content).valid);
        const auto folder=directory.path()/std::to_string(count);
        ASSERT_TRUE(SaveRepository(folder).save(profile,content.contentVersion()).succeeded);
        auto app=std::make_unique<App>();A::flow(*app).configurePersistence(folder);
        ASSERT_TRUE(A::flow(*app).continueGame());A::open(*app);A::frame(*app);
        auto *surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        auto *renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
        ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
        ASSERT_TRUE(A::attach(*app,renderer));A::draw(*app);SDL_RenderPresent(renderer);
        std::vector<double> preview,present;
        for(int sample=0;sample<11;++sample) {
            const auto from=sample%2?originalMove.to:originalMove.from;
            const auto to=sample%2?originalMove.from:originalMove.to;
            pointer(SDL_EVENT_MOUSE_BUTTON_DOWN,from.origin);A::frame(*app);
            pointer(SDL_EVENT_MOUSE_MOTION,to.origin);A::frame(*app);
            const auto started=Clock::now();pointer(SDL_EVENT_MOUSE_BUTTON_UP,to.origin);A::frame(*app);
            const double previewMs=elapsed(started);
            const auto *moved=A::session(*app).profileForInventoryPresentation().assets.find(originalMove.id);
            ASSERT_NE(moved,nullptr);ASSERT_EQ(std::get<StoredAssetLocation>(moved->location),to);
            SDL_SetRenderDrawColor(renderer,15,23,26,255);SDL_RenderClear(renderer);
            A::draw(*app);ASSERT_TRUE(SDL_RenderPresent(renderer));const double presentMs=elapsed(started);
            preview.push_back(previewMs);present.push_back(presentMs);
            if(csv)csv<<(count==60?baseline:"synthetic-stress-200")<<','<<count<<','<<sample<<','
                <<previewMs<<','<<presentMs<<','<<A::pending(*app)<<'\n';
        }
        std::cout<<"APP_INVENTORY_LATENCY scenario="<<(count==60?baseline:"synthetic-stress-200")
            <<" assets="<<count<<" samples="<<preview.size()<<" preview_p95_ms="<<p95(preview)
            <<" software_present_p95_ms="<<p95(present)<<'\n';
        EXPECT_LT(p95(preview),16.7)<<"Real SDL release-to-preview main-thread budget";
        A::detach(*app);SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);app.reset();
    }
    if(csv)csv.close();SDL_Quit();ASSERT_NO_THROW(directory.cleanup());
}
