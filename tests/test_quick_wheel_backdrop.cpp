#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include "ui_quick_wheel_backdrop.h"
#include "ui_quick_wheel_presentation.h"

namespace {
class QuickWheelBackdropTest:public testing::Test {
protected:
    SDL_Surface *surface{};SDL_Renderer *renderer{};UiQuickWheelBackdrop backdrop;
    void SetUp() override {
        ASSERT_TRUE(SDL_Init(SDL_INIT_EVENTS));
        surface=SDL_CreateSurface(1280,720,SDL_PIXELFORMAT_RGBA32);ASSERT_NE(surface,nullptr);
        renderer=SDL_CreateSoftwareRenderer(surface);ASSERT_NE(renderer,nullptr);
    }
    void TearDown() override {backdrop.reset();SDL_DestroyRenderer(renderer);SDL_DestroySurface(surface);SDL_Quit();}
    void frame(float edge) {
        ASSERT_TRUE(backdrop.begin(renderer));SDL_SetRenderDrawColor(renderer,255,255,255,255);
        SDL_FRect rect{0,0,edge,720};ASSERT_TRUE(SDL_RenderFillRect(renderer,&rect));
        ASSERT_TRUE(backdrop.compose());ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
    Uint8 red(int x,int y) {Uint8 r,g,b,a;EXPECT_TRUE(SDL_ReadSurfacePixel(surface,x,y,&r,&g,&b,&a));return r;}
};
}
TEST_F(QuickWheelBackdropTest, SpatialFilteringBlursThisFrameAndKeepsBackgroundLive) {
    frame(640);const auto edge=red(639,360);EXPECT_GT(edge,0);EXPECT_LT(edge,255);
    EXPECT_EQ(red(80,360),255);EXPECT_EQ(red(1200,360),0);
    frame(704);EXPECT_GT(red(639,360),edge);EXPECT_EQ(red(1200,360),0);
}
TEST_F(QuickWheelBackdropTest, RestoresTargetViewportClipScaleColourAndBlend) {
    Texture target{SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,1280,720)};
    ASSERT_TRUE(target.valid());ASSERT_TRUE(SDL_SetRenderTarget(renderer,target.get()));
    ASSERT_TRUE(SDL_SetRenderLogicalPresentation(renderer,1280,720,SDL_LOGICAL_PRESENTATION_LETTERBOX));
    SDL_Rect viewport{8,12,1200,680},clip{20,30,800,600};
    SDL_SetRenderViewport(renderer,&viewport);SDL_SetRenderScale(renderer,.9F,.85F);SDL_SetRenderClipRect(renderer,&clip);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_ADD);SDL_SetRenderDrawColor(renderer,21,41,61,81);
    ASSERT_TRUE(backdrop.begin(renderer));SDL_SetRenderDrawColor(renderer,65,95,125,255);SDL_RenderClear(renderer);
    ASSERT_TRUE(backdrop.compose());EXPECT_EQ(SDL_GetRenderTarget(renderer),target.get());
    SDL_Rect actual{};SDL_GetRenderViewport(renderer,&actual);EXPECT_EQ(actual.x,viewport.x);EXPECT_EQ(actual.w,viewport.w);
    SDL_GetRenderClipRect(renderer,&actual);EXPECT_TRUE(SDL_RenderClipEnabled(renderer));EXPECT_EQ(actual.y,clip.y);EXPECT_EQ(actual.h,clip.h);
    float x,y;SDL_GetRenderScale(renderer,&x,&y);EXPECT_FLOAT_EQ(x,.9F);EXPECT_FLOAT_EQ(y,.85F);
    Uint8 r,g,b,a;SDL_GetRenderDrawColor(renderer,&r,&g,&b,&a);EXPECT_EQ(r,21);EXPECT_EQ(g,41);EXPECT_EQ(b,61);EXPECT_EQ(a,81);
    SDL_BlendMode blend;SDL_GetRenderDrawBlendMode(renderer,&blend);EXPECT_EQ(blend,SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(renderer,nullptr);
}
TEST_F(QuickWheelBackdropTest, ReusedTargetsHaveBoundedMeasuredSoftwareComposeCost) {
    // Diagnostic rendering evidence, separate from the unchanged battle 3ms
    // checks. Do not invent a GPU budget from software-renderer measurements.
    std::array<double,9> samples{};frame(640);
    for(std::size_t i=0;i<samples.size();++i) {frame(640+i);samples[i]=backdrop.lastComposeMilliseconds();}
    std::sort(samples.begin(),samples.end());
    std::printf("wheel software compose CPU median_ms=%.3f max_ms=%.3f\n",samples[4],samples[8]);
    EXPECT_TRUE(std::all_of(samples.begin(),samples.end(),[](double ms){return ms>=0 && ms<1000;}));
}
TEST(QuickWheelDirectionTest, FixedEightDirectionsCenterAndWindowIndependentOffset) {
    constexpr std::array<Vec2,8> direction{{{0,-100},{100,-100},{100,0},{100,100},{0,100},{-100,100},{-100,0},{-100,-100}}};
    for(std::size_t i=0;i<8;++i)EXPECT_EQ(quickWheelSector(direction[i]),i);
    EXPECT_FALSE(quickWheelSector({29,0}));EXPECT_EQ(quickWheelSector({30,0}),2);
    const auto huge=moveQuickWheelPointer({}, {10000,-10000});EXPECT_NEAR(std::hypot(huge.x,huge.y),200,.01);
    EXPECT_EQ(quickWheelSector(huge),1);const auto reverse=moveQuickWheelPointer(huge,{-400,400});EXPECT_EQ(quickWheelSector(reverse),5);
}
TEST(QuickWheelDirectionTest, NineDistinctTestModelsRemainDistinctAcrossTwoPages) {
    const auto path=std::filesystem::path{__FILE__}.parent_path().parent_path()/"assets/content/v1/core.json";
    std::ifstream in(path);ASSERT_TRUE(in);nlohmann::json json;in>>json;
    const auto bandage=std::find_if(json["items"].begin(),json["items"].end(),[](const auto &i){return i["id"]=="item.medical.bandage_basic";});
    ASSERT_NE(bandage,json["items"].end());const auto model=*bandage;
    for(int i=0;i<9;++i) {auto test=model;test["id"]="item.medical.wheel_test_"+std::to_string(i);json["items"].push_back(test);}
    const auto content=ContentRegistry::fromJson(json.dump());auto p=makeNewAlphaProfile("nine-wheel-types",content);p.assets=AssetRegistry{};
    p.medicalStatus={BleedingSeverity::Light,40000,1000,0,15000};
    const auto rig=p.assets.create(content.item(ItemDefinitionId{"item.container.chest_rig_assault"}),EquippedAssetLocation{EquipmentSlotKind::ChestRig});
    for(int i=0;i<9;++i) {
        const auto &d=content.item(ItemDefinitionId{"item.medical.wheel_test_"+std::to_string(i)});
        static_cast<void>(p.assets.create(d,StoredAssetLocation{ProfileContainerId::compartment(rig,0),{i%5,i/5}}));
    }
    ASSERT_TRUE(validateProfileState(p,content).valid);
    const auto ids=quickWheelCandidates(p,content,UiQuickWheelKind::Medical);ASSERT_EQ(ids.size(),9);
    const auto entries=projectUiQuickWheel(p,content,ids,UiQuickWheelKind::Medical);ASSERT_EQ(entries.size(),9);
    std::set<ItemDefinitionId> seen;
    for(std::size_t page=0;page<2;++page) {
        const auto layout=layoutUiQuickWheel(entries,page*8,UiQuickWheelKind::Medical,UiLanguage::English,[](auto,int){return 1.0F;},page);
        EXPECT_EQ(layout.markers.size(),8);
        for(std::size_t sector=0;sector<8 && page*8+sector<entries.size();++sector)
            EXPECT_TRUE(seen.insert(entries[page*8+sector].definitionId).second);
    }
    EXPECT_EQ(seen.size(),9);EXPECT_EQ(p.assets.records().size(),10);
}
