#pragma once

#include "raid_map_generation.h"
#include "texture.h"

#include <array>
#include <vector>

class FrontierRoadArt
{
  public:
    bool load(SDL_Renderer *, const std::string &root);
    void draw(SDL_Renderer *, const RaidGeneratedMapLayout &, ContentRect ground,
              ContentRect visible);

  private:
    struct Stamp
    {
        int image, angle, layer;
        SDL_FRect bounds;
    };
    static constexpr int chunkSize = 1024;
    std::array<Texture, 17> textures_;
    std::vector<std::vector<Stamp>> chunks_;
    int columns_{}, rows_{};
    std::uint64_t hash_{};
    void build(const RaidGeneratedMapLayout &, Vec2 size);
    void stamp(int image, float x, float y, float width, float height, int angle, int layer);
};
