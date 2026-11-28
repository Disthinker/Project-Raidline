#include "frontier_road_art.h"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <map>

namespace
{
constexpr std::array names{"asphalt_01",
                           "asphalt_02",
                           "asphalt_03",
                           "asphalt_04",
                           "asphalt_05",
                           "asphalt_06",
                           "curb_strip",
                           "curb_quarter",
                           "mark_yellow_double",
                           "mark_white_dash",
                           "mark_white_zebra",
                           "mark_white_stop",
                           "mark_white_arrow_left",
                           "mark_white_arrow_right",
                           "mark_white_arrow_straight",
                           "arrow_through_right",
                           "mark_yellow_dash"};
constexpr int dx[]{0, 1, 0, -1}, dy[]{-1, 0, 1, 0};
} // namespace
bool FrontierRoadArt::load(SDL_Renderer *renderer, const std::string &root)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        textures_[i] = Texture{
            IMG_LoadTexture(renderer, (root + "maps/roads/v1/" + names[i] + ".png").c_str())};
        if (!textures_[i].valid())
            return false;
        SDL_SetTextureScaleMode(textures_[i].get(), SDL_SCALEMODE_NEAREST);
    }
    return true;
}
void FrontierRoadArt::stamp(int image, float x, float y, float width, float height, int angle,
                            int layer)
{
    const int cx = std::clamp(static_cast<int>(x) / chunkSize, 0, columns_ - 1);
    const int cy = std::clamp(static_cast<int>(y) / chunkSize, 0, rows_ - 1);
    chunks_[cy * columns_ + cx].push_back(
        {image, angle, layer, {x - width / 2, y - height / 2, width, height}});
}
void FrontierRoadArt::build(const RaidGeneratedMapLayout &layout, Vec2 size)
{
    hash_ = layout.layoutHash;
    const int w = static_cast<int>(size.x) / 64, h = static_cast<int>(size.y) / 64;
    columns_ = (w * 64 + chunkSize - 1) / chunkSize;
    rows_ = (h * 64 + chunkSize - 1) / chunkSize;
    chunks_.assign(columns_ * rows_, {});
    std::vector<unsigned char> paved(w * h);
    using Point = std::pair<int, int>;
    using Arms = std::array<int, 4>;
    std::map<Point, Arms> nodes;
    for (const auto &s : layout.roadNetwork)
    {
        const int half = raidRoadHalfWidthTiles(s.kind), d = s.x1 == s.x2 ? 2 : 1;
        nodes[{s.x1, s.y1}][d] = nodes[{s.x2, s.y2}][(d + 2) % 4] = half;
        for (int y = std::max(0, s.y1 - half); y < std::min(h, s.y2 + half); ++y)
            for (int x = std::max(0, s.x1 - half); x < std::min(w, s.x2 + half); ++x)
                paved[y * w + x] = 1;
    }
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && paved[y * w + x];
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float cx = x * 64.0F + 32, cy = y * 64.0F + 32;
            if (inside(x, y))
            {
                const auto variant =
                    (layout.layoutHash ^ (x * 2654435761U) ^ (y * 2246822519U)) % 6;
                stamp(static_cast<int>(variant), cx, cy, 64, 64, 0, 0);
                continue;
            }
            // The unpaved neighbor owns its curb. A quarter tile replaces both
            // adjoining strips, keeping the road's entire clear width intact.
            bool corner = false;
            for (int d = 0; d < 4; ++d)
            {
                const int next = (d + 1) % 4;
                if (inside(x + dx[d], y + dy[d]) && inside(x + dx[next], y + dy[next]))
                {
                    stamp(7, cx, cy, 64, 64, 90 * d, 1);
                    corner = true;
                    break;
                }
            }
            if (!corner)
                for (int d = 0; d < 4; ++d)
                    if (inside(x + dx[d], y + dy[d]))
                        stamp(6, cx, cy, 64, 64, ((d + 2) % 4) * 90, 1);
        }
    for (const auto &s : layout.roadNetwork)
    {
        const int d = s.x1 == s.x2 ? 2 : 1, half = raidRoadHalfWidthTiles(s.kind);
        const int length = s.x2 - s.x1 + s.y2 - s.y1;
        const auto &a = nodes.at({s.x1, s.y1}), &b = nodes.at({s.x2, s.y2});
        const int start = *std::max_element(a.begin(), a.end()),
                  end = *std::max_element(b.begin(), b.end());
        const auto crossed = [](const Arms &arms) {
            return std::count_if(arms.begin(), arms.end(), [](int n) { return n > 0; }) >= 3;
        };
        const bool marked = half >= 2 && length >= start + end + 9;
        for (int i = start + (marked && crossed(a) ? 2 : 0);
             i < length - end - (marked && crossed(b) ? 2 : 0) && half >= 2; ++i)
        {
            const float cx = (s.x1 + dx[d] * (i + 0.5F)) * 64,
                        cy = (s.y1 + dy[d] * (i + 0.5F)) * 64;
            stamp(half == 4 ? 8 : 16, cx, cy, 64, 64, d == 2 ? 0 : 90, 2);
            if (half == 4)
                for (int side : {-1, 1})
                    stamp(9, cx + side * 128 * dy[d], cy + side * 128 * dx[d], 64, 64,
                          d == 2 ? 0 : 90, 2);
        }
        auto approach = [&](int x, int y, int arm, const Arms &arms, int radius) {
            if (half < 2 || length < start + end + 9 ||
                std::count_if(arms.begin(), arms.end(), [](int v) { return v > 0; }) < 3)
                return;
            const int travel = (arm + 2) % 4, right = (travel + 1) % 4, left = (travel + 3) % 4;
            auto mark = [&](int id, int setback, int lateral, int width, int height) {
                stamp(id, static_cast<float>(x * 64 + dx[arm] * setback + dx[right] * lateral),
                      static_cast<float>(y * 64 + dy[arm] * setback + dy[right] * lateral),
                      static_cast<float>(width), static_cast<float>(height), travel * 90, 3);
            };
            const int lane = half * 64;
            mark(10, radius * 64 + 48, half == 4 ? lane / 2 : 0, 256, 128);
            if (half == 4)
                mark(10, radius * 64 + 48, -lane / 2, 256, 128);
            mark(11, radius * 64 + 136, lane / 2, lane, 64);
            const int setback = radius * 64 + 240;
            if (half == 4 && arms[left])
                mark(12, setback, 64, 44, 88);
            const int id = arms[travel] ? (arms[right] ? 15 : 14)
                                        : (arms[right] ? 13 : (half == 2 && arms[left] ? 12 : -1));
            if (id >= 0)
                mark(id, setback, half == 4 ? 192 : 64, 44, 88);
        };
        approach(s.x1, s.y1, d, a, start);
        approach(s.x2, s.y2, (d + 2) % 4, b, end);
    }
}
void FrontierRoadArt::draw(SDL_Renderer *renderer, const RaidGeneratedMapLayout &layout,
                           ContentRect ground, ContentRect visible)
{
    if (chunks_.empty() || hash_ != layout.layoutHash)
        build(layout, ground.size);
    const float x = visible.position.x - ground.position.x,
                y = visible.position.y - ground.position.y;
    const int x0 = std::max(0, static_cast<int>(x - 256) / chunkSize),
              y0 = std::max(0, static_cast<int>(y - 256) / chunkSize);
    const int x1 = std::min(columns_ - 1, static_cast<int>(x + visible.size.x + 256) / chunkSize);
    const int y1 = std::min(rows_ - 1, static_cast<int>(y + visible.size.y + 256) / chunkSize);
    for (int layer = 0; layer < 4; ++layer)
        for (int cy = y0; cy <= y1; ++cy)
            for (int cx = x0; cx <= x1; ++cx)
                for (const auto &s : chunks_[cy * columns_ + cx])
                {
                    if (s.layer != layer)
                        continue;
                    SDL_FRect rect = s.bounds;
                    rect.x += ground.position.x;
                    rect.y += ground.position.y;
                    SDL_RenderTextureRotated(renderer, textures_[s.image].get(), nullptr, &rect,
                                             s.angle, nullptr, SDL_FLIP_NONE);
                }
}
