#include "frontier_road_art.h"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <memory>

TEST(FrontierRoadArtTest, SoftwareRendererLeavesClosedTArmUnpaved)
{
    // East-west main route with a south approach. Place the junction near
    // the camera so each sample also exercises world-to-viewport translation.
    RaidGeneratedMapLayout layout;
    layout.layoutHash = 17;
    layout.roadNetwork = {{0, 20, 20, 20, RaidOutdoorRoadKind::Primary},
                          {20, 20, 40, 20, RaidOutdoorRoadKind::Primary},
                          {20, 20, 20, 40, RaidOutdoorRoadKind::Secondary}};
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(1024, 1024, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    ASSERT_NE(surface, nullptr);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    ASSERT_NE(renderer, nullptr);
    FrontierRoadArt art;
    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets";
    ASSERT_TRUE(art.load(renderer.get(), assets.generic_string() + "/")) << SDL_GetError();
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), 1, 2, 3, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    const SDL_Rect viewport{-768, -768, 2560, 2560};
    ASSERT_TRUE(SDL_SetRenderViewport(renderer.get(), &viewport));
    art.draw(renderer.get(), layout, {{0, 0}, {2560, 2560}}, {{768, 768}, {1024, 1024}});
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
    auto paved = [&](int x, int y) {
        Uint8 r{}, g{}, b{}, a{};
        EXPECT_TRUE(SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a));
        return r != 1 || g != 2 || b != 3;
    };
    EXPECT_FALSE(paved(512, 100)); // closed north
    EXPECT_TRUE(paved(512, 900));  // open south
    EXPECT_TRUE(paved(100, 512));  // west
    EXPECT_TRUE(paved(900, 512));  // east
}
